#include "twitch-client.hpp"

#include <obs-frontend-api.h>
#include <obs-service.h>
#include <obs.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QGuiApplication>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cwctype>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace kamyshtracker {
namespace {

constexpr auto authBase = "https://id.twitch.tv/oauth2/authorize";
constexpr auto tokenUrl = "https://id.twitch.tv/oauth2/token";
constexpr auto deviceUrl = "https://id.twitch.tv/oauth2/device";
constexpr auto usersUrl = "https://api.twitch.tv/helix/users";
constexpr auto eventSubUrl = "https://api.twitch.tv/helix/eventsub/subscriptions";
constexpr auto eventSubSocketUrl = "wss://eventsub.wss.twitch.tv/ws";
constexpr auto chatMessagesUrl = "https://api.twitch.tv/helix/chat/messages";
constexpr auto tokenValidationTtl = std::chrono::minutes(10);
constexpr auto obsAccountCacheTtl = std::chrono::seconds(30);

struct InternetHandleDeleter {
    void operator()(void *handle) const
    {
        if (handle)
            WinHttpCloseHandle(static_cast<HINTERNET>(handle));
    }
};

using InternetHandle = std::unique_ptr<void, InternetHandleDeleter>;

struct WebSocketConnection {
    InternetHandle session;
    InternetHandle connect;
    InternetHandle socket;
};

struct OAuthCallbackResult {
    QString code;
    QString state;
    QString error;
    QString errorDescription;
};

OAuthCallbackResult waitForOAuthCallback(quint16 port, int timeoutSeconds)
{
    OAuthCallbackResult result;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        result.error = "local_server_failed";
        result.errorDescription = "WSAStartup failed";
        return result;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        result.error = "local_server_failed";
        result.errorDescription = "socket failed";
        WSACleanup();
        return result;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(server, 1) == SOCKET_ERROR) {
        result.error = "local_server_failed";
        result.errorDescription = "Could not listen on localhost callback port";
        closesocket(server);
        WSACleanup();
        return result;
    }

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(server, &readSet);
    timeval timeout{};
    timeout.tv_sec = timeoutSeconds;
    timeout.tv_usec = 0;

    if (select(0, &readSet, nullptr, nullptr, &timeout) <= 0) {
        result.error = "timeout";
        result.errorDescription = "OAuth callback timed out";
        closesocket(server);
        WSACleanup();
        return result;
    }

    SOCKET client = accept(server, nullptr, nullptr);
    closesocket(server);
    if (client == INVALID_SOCKET) {
        result.error = "local_server_failed";
        result.errorDescription = "accept failed";
        WSACleanup();
        return result;
    }

    std::string request(8192, '\0');
    const int received = recv(client, request.data(), static_cast<int>(request.size() - 1), 0);
    if (received > 0) {
        request.resize(received);
        const auto firstLineEnd = request.find("\r\n");
        const auto firstLine = request.substr(0, firstLineEnd == std::string::npos ? request.size() : firstLineEnd);
        const auto firstSpace = firstLine.find(' ');
        const auto secondSpace = firstSpace == std::string::npos ? std::string::npos : firstLine.find(' ', firstSpace + 1);
        const auto target = firstSpace == std::string::npos || secondSpace == std::string::npos
            ? std::string("/")
            : firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);

        QUrl callback(QStringLiteral("http://localhost") + QString::fromStdString(target));
        QUrlQuery query(callback);
        result.code = query.queryItemValue("code");
        result.state = query.queryItemValue("state");
        result.error = query.queryItemValue("error");
        result.errorDescription = query.queryItemValue("error_description");
    }

    const QByteArray html = result.error.isEmpty()
        ? QByteArray("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
            "<html><body><h3>KamyshTracker login complete.</h3>You can close this tab.</body></html>")
        : QByteArray("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n"
            "<html><body><h3>KamyshTracker login failed.</h3>"
            "<p>Check the Twitch Developer Console redirect URI.</p></body></html>");
    send(client, html.constData(), static_cast<int>(html.size()), 0);
    shutdown(client, SD_SEND);
    closesocket(client);
    WSACleanup();
    return result;
}

QString base64Url(QByteArray value)
{
    value = value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    return QString::fromLatin1(value);
}

QString randomVerifier()
{
    QByteArray bytes;
    bytes.resize(48);
    for (char &byte : bytes)
        byte = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    return base64Url(bytes);
}

std::string winHttpError(const char *operation);

QByteArray httpRequest(
    const QString &method,
    const QUrl &url,
    const QByteArray &body,
    const QList<QPair<QByteArray, QByteArray>> &headers,
    int *statusCode,
    QString *error)
{
    if (statusCode)
        *statusCode = 0;
    if (error)
        error->clear();

    const auto scheme = url.scheme().toLower();
    const bool secure = scheme == "https";
    if (!secure && scheme != "http") {
        if (error)
            *error = "Unsupported URL scheme";
        return {};
    }

    const auto host = url.host().toStdWString();
    const auto path = QString(url.path().isEmpty() ? "/" : url.path()).toStdWString() +
        (url.query().isEmpty() ? std::wstring{} : (L"?" + url.query(QUrl::FullyEncoded).toStdWString()));
    const INTERNET_PORT port = url.port(secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);

    InternetHandle session(WinHttpOpen(
        L"KamyshTracker/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        if (error)
            *error = QString::fromStdString(winHttpError("WinHttpOpen"));
        return {};
    }

    DWORD timeout = 30000;
    WinHttpSetOption(static_cast<HINTERNET>(session.get()), WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(static_cast<HINTERNET>(session.get()), WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(static_cast<HINTERNET>(session.get()), WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

    InternetHandle connect(WinHttpConnect(static_cast<HINTERNET>(session.get()), host.c_str(), port, 0));
    if (!connect) {
        if (error)
            *error = QString::fromStdString(winHttpError("WinHttpConnect"));
        return {};
    }

    InternetHandle request(WinHttpOpenRequest(
        static_cast<HINTERNET>(connect.get()),
        method.toStdWString().c_str(),
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) {
        if (error)
            *error = QString::fromStdString(winHttpError("WinHttpOpenRequest"));
        return {};
    }

    QString headerText;
    for (const auto &header : headers)
        headerText += QString::fromLatin1(header.first) + ": " + QString::fromLatin1(header.second) + "\r\n";
    const auto wideHeaders = headerText.toStdWString();

    if (!WinHttpSendRequest(
            static_cast<HINTERNET>(request.get()),
            wideHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wideHeaders.c_str(),
            static_cast<DWORD>(wideHeaders.empty() ? 0 : wideHeaders.size()),
            body.isEmpty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(body.constData()),
            static_cast<DWORD>(body.size()),
            static_cast<DWORD>(body.size()),
            0) ||
        !WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr)) {
        if (error)
            *error = QString::fromStdString(winHttpError("WinHttpReceiveResponse"));
        return {};
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(
            static_cast<HINTERNET>(request.get()),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX) &&
        statusCode) {
        *statusCode = static_cast<int>(status);
    }

    QByteArray response;
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(static_cast<HINTERNET>(request.get()), &available)) {
            if (error)
                *error = QString::fromStdString(winHttpError("WinHttpQueryDataAvailable"));
            break;
        }
        if (available == 0)
            break;

        const auto oldSize = response.size();
        response.resize(oldSize + static_cast<int>(available));
        DWORD read = 0;
        if (!WinHttpReadData(
                static_cast<HINTERNET>(request.get()),
                response.data() + oldSize,
                available,
                &read)) {
            if (error)
                *error = QString::fromStdString(winHttpError("WinHttpReadData"));
            response.resize(oldSize);
            break;
        }
        response.resize(oldSize + static_cast<int>(read));
    }

    return response;
}

bool parseUser(const QByteArray &body, PluginSettings &settings, QString *error)
{
    const auto doc = QJsonDocument::fromJson(body);
    const auto data = doc.object().value("data").toArray();
    if (data.isEmpty()) {
        if (error)
            *error = "Twitch did not return user data";
        return false;
    }

    const auto user = data.first().toObject();
    settings.twitchUserId = user.value("id").toString().toStdString();
    settings.twitchBroadcasterId = settings.twitchUserId;
    settings.twitchLogin = user.value("login").toString().toStdString();
    return !settings.twitchUserId.empty() && !settings.twitchLogin.empty();
}

void replaceAll(std::wstring &text, const std::wstring &from, const std::wstring &to)
{
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

bool isCommandSeparator(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' ||
        c == L',' || c == L';' ||
        c == L'\u00a0' || c == L'\u1680' || c == L'\u2028' || c == L'\u2029' ||
        c == L'\u202f' || c == L'\u205f' || c == L'\u3000' ||
        (c >= L'\u2000' && c <= L'\u200a');
}

std::wstring trimCommandText(const std::wstring &value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), isCommandSeparator);
    if (first == value.end())
        return {};

    const auto last = std::find_if_not(value.rbegin(), value.rend(), isCommandSeparator).base();
    return std::wstring(first, last);
}

std::wstring firstCommandToken(const std::wstring &value)
{
    const auto trimmed = trimCommandText(value);
    const auto separator = std::find_if(trimmed.begin(), trimmed.end(), isCommandSeparator);
    return std::wstring(trimmed.begin(), separator);
}

std::wstring lowerCopy(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(::towlower(c));
    });
    return value;
}

bool matchesCommandTrigger(const std::wstring &message, const std::wstring &triggers)
{
    const auto normalizedMessage = lowerCopy(firstCommandToken(message));
    if (normalizedMessage.empty())
        return false;

    size_t pos = 0;
    while (pos < triggers.size()) {
        while (pos < triggers.size() && isCommandSeparator(triggers[pos]))
            ++pos;
        const auto start = pos;
        while (pos < triggers.size() && !isCommandSeparator(triggers[pos]))
            ++pos;
        if (start == pos)
            continue;

        if (normalizedMessage == lowerCopy(triggers.substr(start, pos - start)))
            return true;
    }
    return false;
}

void interruptibleSleep(const std::atomic_bool &running, std::chrono::seconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (running && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

std::string winHttpError(const char *operation)
{
    return std::string(operation) + " failed: " + std::to_string(GetLastError());
}

bool parseWebSocketUrl(const std::string &url, std::wstring &host, INTERNET_PORT &port, std::wstring &path)
{
    std::string httpUrl = url;
    if (httpUrl.rfind("wss://", 0) == 0)
        httpUrl.replace(0, 6, "https://");
    else if (httpUrl.rfind("ws://", 0) == 0)
        httpUrl.replace(0, 5, "http://");

    const auto wideUrl = QString::fromStdString(httpUrl).toStdWString();
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &components))
        return false;
    if (components.nScheme != INTERNET_SCHEME_HTTPS && components.nScheme != INTERNET_SCHEME_HTTP)
        return false;

    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (path.empty())
        path = L"/";
    port = components.nPort;
    return !host.empty();
}

}

TwitchClient::TwitchClient() = default;

TwitchClient::~TwitchClient()
{
    stop();
}

void TwitchClient::configure(PluginSettings settings, MediaProvider mediaProvider, StatusCallback statusCallback)
{
    std::lock_guard lock(mutex_);
    settings_ = std::move(settings);
    mediaProvider_ = std::move(mediaProvider);
    statusCallback_ = std::move(statusCallback);
    tokenValidatedUntil_ = std::chrono::steady_clock::time_point::min();
    obsAccountCacheUntil_ = std::chrono::steady_clock::time_point::min();
}

void TwitchClient::setSettingsChangedCallback(SettingsChangedCallback callback)
{
    std::lock_guard lock(mutex_);
    settingsChangedCallback_ = std::move(callback);
}

void TwitchClient::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
        return;

    worker_ = std::thread([this] { eventSubLoop(); });
    replyWorker_ = std::thread([this] { replyLoop(); });
}

void TwitchClient::stop()
{
    if (!running_.exchange(false))
        return;

    closeActiveEventSubSocket();
    replyWake_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (replyWorker_.joinable())
        replyWorker_.join();

    {
        std::lock_guard lock(replyMutex_);
        replyQueue_.clear();
    }
}

bool TwitchClient::loginWithBrowser(const PluginSettings &inputSettings, PluginSettings &settings, std::string &error)
{
    const auto &clientId = inputSettings.twitchClientId;
    const auto &clientSecret = inputSettings.twitchClientSecret;
    if (clientId.empty()) {
        error = "Twitch Client ID is required";
        return false;
    }

    QUrlQuery deviceBody;
    deviceBody.addQueryItem("client_id", QString::fromStdString(clientId));
    deviceBody.addQueryItem("scopes", "user:read:chat user:write:chat");

    int status = 0;
    QString networkError;
    const auto deviceResponse = httpRequest(
        "POST",
        QUrl(deviceUrl),
        deviceBody.query(QUrl::FullyEncoded).toUtf8(),
        {{ "Content-Type", "application/x-www-form-urlencoded" }},
        &status,
        &networkError);

    if (status < 200 || status >= 300) {
        error = ("Device authorization failed: " + networkError + " " + QString::fromUtf8(deviceResponse)).toStdString();
        return false;
    }

    const auto deviceJson = QJsonDocument::fromJson(deviceResponse).object();
    const auto deviceCode = deviceJson.value("device_code").toString();
    const auto userCode = deviceJson.value("user_code").toString();
    const auto verificationUri = deviceJson.value("verification_uri").toString();
    const auto verificationUriComplete = deviceJson.value("verification_uri_complete").toString();
    const int expiresIn = deviceJson.value("expires_in").toInt(1800);
    const int intervalSeconds = std::max(1, deviceJson.value("interval").toInt(5));

    if (deviceCode.isEmpty() || verificationUri.isEmpty()) {
        error = "Twitch did not return a device authorization code";
        return false;
    }

    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(userCode);

    {
        std::lock_guard lock(mutex_);
        status_ = ("Open Twitch activation and enter code: " + userCode).toStdString();
    }

    const QUrl activate(verificationUriComplete.isEmpty() ? verificationUri : verificationUriComplete);
    QDesktopServices::openUrl(activate);

    QByteArray tokenResponse;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn);
    while (std::chrono::steady_clock::now() < deadline) {
        QUrlQuery tokenBody;
        tokenBody.addQueryItem("client_id", QString::fromStdString(clientId));
        if (!clientSecret.empty())
            tokenBody.addQueryItem("client_secret", QString::fromStdString(clientSecret));
        tokenBody.addQueryItem("device_code", deviceCode);
        tokenBody.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");

        status = 0;
        networkError.clear();
        tokenResponse = httpRequest(
            "POST",
            QUrl(tokenUrl),
            tokenBody.query(QUrl::FullyEncoded).toUtf8(),
            {{ "Content-Type", "application/x-www-form-urlencoded" }},
            &status,
            &networkError);

        if (status >= 200 && status < 300)
            break;

        const auto pollJson = QJsonDocument::fromJson(tokenResponse).object();
        const auto message = pollJson.value("message").toString();
        const auto pollStatus = pollJson.value("status").toInt(status);
        if (pollStatus != 400 || (!message.contains("authorization", Qt::CaseInsensitive) &&
            !message.contains("pending", Qt::CaseInsensitive))) {
            error = ("Token exchange failed: " + networkError + " " + QString::fromUtf8(tokenResponse)).toStdString();
            return false;
        }

        const auto waitUntil = std::chrono::steady_clock::now() + std::chrono::seconds(intervalSeconds);
        while (std::chrono::steady_clock::now() < waitUntil)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    if (status < 200 || status >= 300) {
        error = "Twitch device authorization timed out";
        return false;
    }

    const auto tokenJson = QJsonDocument::fromJson(tokenResponse).object();
    settings.twitchClientId = clientId;
    settings.twitchClientSecret = clientSecret;
    settings.twitchAccessToken = tokenJson.value("access_token").toString().toStdString();
    settings.twitchRefreshToken = tokenJson.value("refresh_token").toString().toStdString();

    QString userError;
    if (!refreshUserInfo(settings, error))
        return false;

    MediaProvider provider;
    StatusCallback statusCallback;
    {
        std::lock_guard lock(mutex_);
        provider = mediaProvider_;
        statusCallback = statusCallback_;
    }
    configure(settings, std::move(provider), std::move(statusCallback));
    return true;
}

void TwitchClient::logout(PluginSettings &settings)
{
    settings.twitchAccessToken.clear();
    settings.twitchRefreshToken.clear();
    settings.twitchLogin.clear();
    settings.twitchUserId.clear();
    settings.twitchBroadcasterId.clear();
    MediaProvider provider;
    StatusCallback statusCallback;
    {
        std::lock_guard lock(mutex_);
        provider = mediaProvider_;
        statusCallback = statusCallback_;
    }
    configure(settings, std::move(provider), std::move(statusCallback));
}

bool TwitchClient::sendTestMessage(std::string &error)
{
    PluginSettings snapshot;
    MediaProvider provider;
    {
        std::lock_guard lock(mutex_);
        snapshot = settings_;
        provider = mediaProvider_;
    }

    if (!snapshot.enabled) {
        error = "Plugin is disabled";
        return false;
    }

    if (!isReadyFor(readObsTwitchAccount())) {
        error = "Twitch account is not ready or does not match OBS";
        return false;
    }

    if (snapshot.requireStreamingActive && !obs_frontend_streaming_active()) {
        error = "Streaming is not active";
        return false;
    }

    if (!ensureToken(error))
        return false;

    const auto state = provider ? provider() : MediaState{};
    return sendChatMessage(renderResponse(state), error);
}

bool TwitchClient::isReadyFor(const ObsTwitchAccount &obsAccount) const
{
    std::lock_guard lock(mutex_);
    if (!settings_.enabled || settings_.twitchAccessToken.empty() || settings_.twitchLogin.empty())
        return false;
    if (!obsAccount.isTwitch)
        return false;
    if (obsAccount.login.empty())
        return true;
    return QString::fromStdString(settings_.twitchLogin).compare(
        QString::fromStdString(obsAccount.login), Qt::CaseInsensitive) == 0;
}

std::string TwitchClient::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

void TwitchClient::replyLoop()
{
    while (running_) {
        MediaState state;
        {
            std::unique_lock lock(replyMutex_);
            replyWake_.wait(lock, [this] {
                return !running_ || !replyQueue_.empty();
            });

            if (!running_)
                break;

            state = std::move(replyQueue_.front());
            replyQueue_.pop_front();
        }

        PluginSettings snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot = settings_;
        }

        if (!snapshot.enabled)
            continue;

        if (snapshot.requireStreamingActive && !obs_frontend_streaming_active())
            continue;

        if (!isReadyFor(cachedObsAccount()))
            continue;

        std::string error;
        if (!ensureToken(error) || !sendChatMessage(renderResponse(state), error)) {
            std::lock_guard lock(mutex_);
            status_ = error;
        } else {
            std::lock_guard lock(mutex_);
            lastReply_ = std::chrono::steady_clock::now();
        }
    }
}

void TwitchClient::eventSubLoop()
{
    std::string socketUrl = eventSubSocketUrl;
    while (running_) {
        PluginSettings snapshot;
        {
            std::lock_guard lock(mutex_);
            snapshot = settings_;
        }

        if (!snapshot.enabled || snapshot.twitchAccessToken.empty() || snapshot.twitchClientId.empty()) {
            {
                std::lock_guard lock(mutex_);
                status_ = "Not authorized";
            }
            interruptibleSleep(running_, std::chrono::seconds(2));
            continue;
        }

        std::string error;
        if (!ensureToken(error)) {
            {
                std::lock_guard lock(mutex_);
                status_ = "Token error: " + error;
            }
            interruptibleSleep(running_, std::chrono::seconds(10));
            continue;
        }

        QString sessionId;
        bool subscribed = false;
        bool reconnecting = false;
        std::string nextSocketUrl;
        void *rawSocket = nullptr;

        if (!connectEventSubSocket(socketUrl, rawSocket, error)) {
            {
                std::lock_guard lock(mutex_);
                status_ = "WebSocket error: " + error;
            }
            interruptibleSleep(running_, std::chrono::seconds(10));
            continue;
        }

        std::unique_ptr<WebSocketConnection> socket(static_cast<WebSocketConnection *>(rawSocket));
        {
            std::lock_guard lock(mutex_);
            activeEventSubSocket_ = socket->socket.get();
            status_ = "EventSub connected";
        }

        while (running_) {
            std::string message;
            if (!receiveEventSubMessage(socket.get(), message, error)) {
                if (running_) {
                    std::lock_guard lock(mutex_);
                    status_ = "WebSocket error: " + error;
                }
                break;
            }

            const auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(message));
            const auto root = doc.object();
            const auto metadata = root.value("metadata").toObject();
            const auto type = metadata.value("message_type").toString();
            const auto payload = root.value("payload").toObject();

            if (type == "session_welcome") {
                sessionId = payload.value("session").toObject().value("id").toString();
                std::string subscribeError;
                subscribed = subscribeChat(sessionId.toStdString(), subscribeError);
                std::lock_guard lock(mutex_);
                status_ = subscribed ? "Chat command active" : "Subscribe failed: " + subscribeError;
            } else if (type == "notification") {
                const auto event = payload.value("event").toObject();
                const auto chatter = event.value("chatter_user_login").toString().toStdString();
                const auto text = event.value("message").toObject().value("text").toString().toStdWString();
                handleChatText(chatter, text);
            } else if (type == "session_reconnect") {
                const auto reconnectUrl = payload.value("session").toObject().value("reconnect_url").toString();
                if (!reconnectUrl.isEmpty()) {
                    reconnecting = true;
                    nextSocketUrl = reconnectUrl.toStdString();
                    break;
                }
            }
        }

        {
            std::lock_guard lock(mutex_);
            if (activeEventSubSocket_ == socket->socket.get())
                activeEventSubSocket_ = nullptr;
        }
        WinHttpWebSocketClose(static_cast<HINTERNET>(socket->socket.get()), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);

        if (reconnecting) {
            socketUrl = nextSocketUrl;
            continue;
        }

        socketUrl = eventSubSocketUrl;
        if (running_)
            interruptibleSleep(running_, std::chrono::seconds(subscribed ? 2 : 10));
    }
}

bool TwitchClient::connectEventSubSocket(const std::string &url, void *&socket, std::string &error)
{
    socket = nullptr;

    std::wstring host;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    std::wstring path;
    if (!parseWebSocketUrl(url, host, port, path)) {
        error = "Invalid EventSub WebSocket URL";
        return false;
    }

    InternetHandle session(WinHttpOpen(
        L"KamyshTracker/2.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session) {
        error = winHttpError("WinHttpOpen");
        return false;
    }

    InternetHandle connect(WinHttpConnect(static_cast<HINTERNET>(session.get()), host.c_str(), port, 0));
    if (!connect) {
        error = winHttpError("WinHttpConnect");
        return false;
    }

    InternetHandle request(WinHttpOpenRequest(
        static_cast<HINTERNET>(connect.get()),
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!request) {
        error = winHttpError("WinHttpOpenRequest");
        return false;
    }

    if (!WinHttpSetOption(static_cast<HINTERNET>(request.get()), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        error = winHttpError("WinHttpSetOption");
        return false;
    }

    if (!WinHttpSendRequest(
            static_cast<HINTERNET>(request.get()),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(static_cast<HINTERNET>(request.get()), nullptr)) {
        error = winHttpError("WinHttpReceiveResponse");
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        static_cast<HINTERNET>(request.get()),
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 101) {
        error = "EventSub WebSocket upgrade failed with HTTP " + std::to_string(statusCode);
        return false;
    }

    InternetHandle upgraded(WinHttpWebSocketCompleteUpgrade(static_cast<HINTERNET>(request.get()), 0));
    if (!upgraded) {
        error = winHttpError("WinHttpWebSocketCompleteUpgrade");
        return false;
    }
    request.reset();

    auto connection = std::make_unique<WebSocketConnection>();
    connection->session = std::move(session);
    connection->connect = std::move(connect);
    connection->socket = std::move(upgraded);
    socket = connection.release();
    return true;
}

bool TwitchClient::receiveEventSubMessage(void *socket, std::string &message, std::string &error)
{
    auto *connection = static_cast<WebSocketConnection *>(socket);
    if (!connection || !connection->socket) {
        error = "EventSub WebSocket is not connected";
        return false;
    }

    message.clear();
    std::vector<char> buffer(16 * 1024);

    while (running_) {
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType{};
        const DWORD result = WinHttpWebSocketReceive(
            static_cast<HINTERNET>(connection->socket.get()),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &bytesRead,
            &bufferType);

        if (result != ERROR_SUCCESS) {
            error = "WinHttpWebSocketReceive failed: " + std::to_string(result);
            return false;
        }

        if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            error = "EventSub WebSocket closed";
            return false;
        }

        if (bufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
            bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            message.append(buffer.data(), buffer.data() + bytesRead);
            if (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                return true;
        }
    }

    error = "EventSub WebSocket stopped";
    return false;
}

void TwitchClient::closeActiveEventSubSocket()
{
    void *socket = nullptr;
    {
        std::lock_guard lock(mutex_);
        socket = activeEventSubSocket_;
    }

    if (socket)
        WinHttpWebSocketClose(static_cast<HINTERNET>(socket), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
}

bool TwitchClient::refreshUserInfo(PluginSettings &settings, std::string &error)
{
    int status = 0;
    QString networkError;
    const auto body = httpRequest(
        "GET",
        QUrl(usersUrl),
        {},
        {
            { "Authorization", ("Bearer " + settings.twitchAccessToken).c_str() },
            { "Client-Id", settings.twitchClientId.c_str() },
        },
        &status,
        &networkError);

    if (status < 200 || status >= 300) {
        error = ("Get Users failed: " + networkError + " " + QString::fromUtf8(body)).toStdString();
        return false;
    }

    QString parseError;
    if (!parseUser(body, settings, &parseError)) {
        error = parseError.toStdString();
        return false;
    }
    return true;
}

bool TwitchClient::ensureToken(std::string &error)
{
    PluginSettings snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = settings_;
        if (std::chrono::steady_clock::now() < tokenValidatedUntil_ &&
            !settings_.twitchAccessToken.empty() && !settings_.twitchLogin.empty())
            return true;
    }

    if (snapshot.twitchAccessToken.empty())
        return false;

    if (refreshUserInfo(snapshot, error)) {
        std::lock_guard lock(mutex_);
        settings_.twitchLogin = snapshot.twitchLogin;
        settings_.twitchUserId = snapshot.twitchUserId;
        settings_.twitchBroadcasterId = snapshot.twitchBroadcasterId;
        tokenValidatedUntil_ = std::chrono::steady_clock::now() + tokenValidationTtl;
        return true;
    }

    if (snapshot.twitchRefreshToken.empty())
        return false;

    QUrlQuery refreshBody;
    refreshBody.addQueryItem("grant_type", "refresh_token");
    refreshBody.addQueryItem("refresh_token", QString::fromStdString(snapshot.twitchRefreshToken));
    refreshBody.addQueryItem("client_id", QString::fromStdString(snapshot.twitchClientId));
    if (!snapshot.twitchClientSecret.empty())
        refreshBody.addQueryItem("client_secret", QString::fromStdString(snapshot.twitchClientSecret));

    int status = 0;
    QString networkError;
    const auto tokenResponse = httpRequest(
        "POST",
        QUrl(tokenUrl),
        refreshBody.query(QUrl::FullyEncoded).toUtf8(),
        {{ "Content-Type", "application/x-www-form-urlencoded" }},
        &status,
        &networkError);

    if (status < 200 || status >= 300) {
        const auto responseText = QString::fromUtf8(tokenResponse);
        if (snapshot.twitchClientSecret.empty() && responseText.contains("missing client secret", Qt::CaseInsensitive)) {
            error = "Token refresh failed: Twitch Client Secret is required for this Client ID. "
                "Open Tools -> KamyshTracker, fill Twitch Client Secret, save, and login again; "
                "or switch the Twitch application to Public client type.";
            return false;
        }
        error = ("Token refresh failed: " + networkError + " " + QString::fromUtf8(tokenResponse)).toStdString();
        return false;
    }

    const auto tokenJson = QJsonDocument::fromJson(tokenResponse).object();
    snapshot.twitchAccessToken = tokenJson.value("access_token").toString().toStdString();
    snapshot.twitchRefreshToken = tokenJson.value("refresh_token").toString().toStdString();
    if (!refreshUserInfo(snapshot, error))
        return false;

    SettingsChangedCallback callback;
    {
        std::lock_guard lock(mutex_);
        settings_ = snapshot;
        tokenValidatedUntil_ = std::chrono::steady_clock::now() + tokenValidationTtl;
        callback = settingsChangedCallback_;
    }
    if (callback)
        callback(snapshot);
    return true;
}

ObsTwitchAccount TwitchClient::cachedObsAccount()
{
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        if (now < obsAccountCacheUntil_)
            return obsAccountCache_;
    }

    auto account = readObsTwitchAccount();
    {
        std::lock_guard lock(mutex_);
        obsAccountCache_ = account;
        obsAccountCacheUntil_ = now + obsAccountCacheTtl;
    }
    return account;
}

bool TwitchClient::subscribeChat(const std::string &sessionId, std::string &error)
{
    PluginSettings snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = settings_;
    }

    QJsonObject condition;
    condition["broadcaster_user_id"] = QString::fromStdString(snapshot.twitchBroadcasterId);
    condition["user_id"] = QString::fromStdString(snapshot.twitchUserId);

    QJsonObject transport;
    transport["method"] = "websocket";
    transport["session_id"] = QString::fromStdString(sessionId);

    QJsonObject payload;
    payload["type"] = "channel.chat.message";
    payload["version"] = "1";
    payload["condition"] = condition;
    payload["transport"] = transport;

    int status = 0;
    QString networkError;
    const auto body = httpRequest(
        "POST",
        QUrl(eventSubUrl),
        QJsonDocument(payload).toJson(QJsonDocument::Compact),
        {
            { "Authorization", ("Bearer " + snapshot.twitchAccessToken).c_str() },
            { "Client-Id", snapshot.twitchClientId.c_str() },
            { "Content-Type", "application/json" },
        },
        &status,
        &networkError);

    if (status < 200 || status >= 300) {
        error = ("EventSub subscribe failed: " + networkError + " " + QString::fromUtf8(body)).toStdString();
        return false;
    }
    return true;
}

bool TwitchClient::sendChatMessage(const std::wstring &message, std::string &error)
{
    PluginSettings snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = settings_;
    }

    QJsonObject payload;
    payload["broadcaster_id"] = QString::fromStdString(snapshot.twitchBroadcasterId);
    payload["sender_id"] = QString::fromStdString(snapshot.twitchUserId);
    payload["message"] = QString::fromStdWString(message);

    int status = 0;
    QString networkError;
    const auto body = httpRequest(
        "POST",
        QUrl(chatMessagesUrl),
        QJsonDocument(payload).toJson(QJsonDocument::Compact),
        {
            { "Authorization", ("Bearer " + snapshot.twitchAccessToken).c_str() },
            { "Client-Id", snapshot.twitchClientId.c_str() },
            { "Content-Type", "application/json" },
        },
        &status,
        &networkError);

    if (status < 200 || status >= 300) {
        error = ("Send chat message failed: " + networkError + " " + QString::fromUtf8(body)).toStdString();
        return false;
    }

    const auto data = QJsonDocument::fromJson(body).object().value("data").toArray();
    if (!data.isEmpty() && !data.first().toObject().value("is_sent").toBool()) {
        error = "Twitch rejected the chat message";
        return false;
    }
    return true;
}

void TwitchClient::enqueueReply(MediaState state)
{
    {
        std::lock_guard lock(replyMutex_);
        if (!replyQueue_.empty())
            return;
        replyQueue_.push_back(std::move(state));
    }
    replyWake_.notify_one();
}

void TwitchClient::handleChatText(const std::string &, const std::wstring &message)
{
    PluginSettings snapshot;
    MediaProvider provider;
    {
        std::lock_guard lock(mutex_);
        if (!running_)
            return;
        snapshot = settings_;
        provider = mediaProvider_;
    }

    if (!snapshot.enabled || !matchesCommandTrigger(message, snapshot.commandTrigger))
        return;

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(mutex_);
        if (lastReply_ != std::chrono::steady_clock::time_point::min() &&
            now - lastReply_ < std::chrono::seconds(snapshot.replyCooldownSeconds))
            return;
    }

    try {
        const auto state = provider ? provider() : MediaState{};
        enqueueReply(state);
    } catch (...) {
    }
}

std::wstring TwitchClient::renderResponse(const MediaState &state) const
{
    PluginSettings snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = settings_;
    }

    std::wstring response = state.isPlaying && state.hasText()
        ? snapshot.responseTemplate
        : snapshot.notPlayingTemplate;

    replaceAll(response, L"{artist}", state.artist);
    replaceAll(response, L"{title}", state.title);
    replaceAll(response, L"{track}", state.displayText(snapshot.notPlayingTemplate));
    replaceAll(response, L"{source}", state.sourceApp);
    return response;
}

ObsTwitchAccount readObsTwitchAccount()
{
    ObsTwitchAccount account;
    obs_service_t *service = obs_frontend_get_streaming_service();
    if (!service)
        return account;

    const char *serviceName = obs_service_get_name(service);
    if (serviceName)
        account.serviceName = serviceName;

    obs_data_t *settings = obs_service_get_settings(service);
    const char *serviceValue = obs_data_get_string(settings, "service");
    const QString serviceText = QString("%1 %2")
        .arg(QString::fromUtf8(serviceName ? serviceName : ""))
        .arg(QString::fromUtf8(serviceValue ? serviceValue : ""));
    account.isTwitch = serviceText.contains("twitch", Qt::CaseInsensitive);

    const char *connectUser = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_USERNAME);
    if (connectUser && *connectUser)
        account.login = connectUser;

    for (const char *key : {"login", "username", "channel", "twitch_login", "account_login"}) {
        const char *value = obs_data_get_string(settings, key);
        if (value && *value) {
            account.login = value;
            break;
        }
    }

    obs_data_release(settings);
    return account;
}

}
