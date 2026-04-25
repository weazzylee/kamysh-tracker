#pragma once

#include "media-state.hpp"
#include "settings.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace kamyshtracker {

struct ObsTwitchAccount {
    bool isTwitch = false;
    std::string login;
    std::string serviceName;
};

class TwitchClient {
public:
    using MediaProvider = std::function<MediaState()>;
    using StatusCallback = std::function<void(const std::string &)>;
    using SettingsChangedCallback = std::function<void(const PluginSettings &)>;

    TwitchClient();
    ~TwitchClient();

    TwitchClient(const TwitchClient &) = delete;
    TwitchClient &operator=(const TwitchClient &) = delete;

    void configure(PluginSettings settings, MediaProvider mediaProvider, StatusCallback statusCallback);
    void setSettingsChangedCallback(SettingsChangedCallback callback);
    void start();
    void stop();

    bool loginWithBrowser(const PluginSettings &inputSettings, PluginSettings &settings, std::string &error);
    void logout(PluginSettings &settings);
    bool sendTestMessage(std::string &error);

    [[nodiscard]] bool isReadyFor(const ObsTwitchAccount &obsAccount) const;
    [[nodiscard]] std::string status() const;

private:
    void eventSubLoop();
    bool refreshUserInfo(PluginSettings &settings, std::string &error);
    bool ensureToken(std::string &error);
    bool subscribeChat(const std::string &sessionId, std::string &error);
    bool sendChatMessage(const std::wstring &message, std::string &error);
    void handleChatText(const std::string &chatterLogin, const std::wstring &message);
    [[nodiscard]] std::wstring renderResponse(const MediaState &state) const;

    mutable std::mutex mutex_;
    PluginSettings settings_;
    MediaProvider mediaProvider_;
    StatusCallback statusCallback_;
    SettingsChangedCallback settingsChangedCallback_;
    std::string status_ = "Not connected";
    std::atomic_bool running_{false};
    std::thread worker_;
    std::chrono::steady_clock::time_point lastReply_ = std::chrono::steady_clock::time_point::min();
};

ObsTwitchAccount readObsTwitchAccount();

}
