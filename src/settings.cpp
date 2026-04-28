#include "settings.hpp"

#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <QString>
#include <QUrl>

namespace kamyshtracker {
namespace {

std::wstring widen(const char *value)
{
    return QString::fromUtf8(value ? value : "").toStdWString();
}

std::string narrow(const std::wstring &value)
{
    return QString::fromStdWString(value).toUtf8().toStdString();
}

std::string safeString(const char *value)
{
    return value ? value : "";
}

bool isUsableRedirectUri(const std::string &value)
{
    const QUrl url(QString::fromStdString(value));
    return url.isValid() && url.scheme() == "http" &&
        (url.host() == "localhost" || url.host() == "127.0.0.1") &&
        url.port() > 0 &&
        !url.path().isEmpty() &&
        url.path() != "/";
}

bool shouldUseDefaultCommandTrigger(const std::wstring &value)
{
    return value == L"!\u0442\u0440\u0435\u043a" ||
        value == L"!\u0421\u201a\u0421\u0402\u0420\u00b5\u0420\u0454" ||
        value == L"!\u0421\u201a\u0421\u0402\u0420\u00b5\u0420\u0454 !track !shazam !\u0421\u20ac\u0420\u00b0\u0420\u00b7\u0420\u00b0\u0420\u0458";
}

}

SettingsStore::SettingsStore()
{
    char *profilePath = obs_frontend_get_current_profile_path();
    if (profilePath) {
        path_ = std::string(profilePath) + "/kamyshtracker.ini";
        bfree(profilePath);
    } else {
        path_ = "kamyshtracker.ini";
    }
}

PluginSettings SettingsStore::load() const
{
    PluginSettings settings;
    config_t *config = nullptr;
    if (config_open(&config, path_.c_str(), CONFIG_OPEN_ALWAYS) != CONFIG_SUCCESS || !config)
        return settings;

    config_set_default_bool(config, "general", "enabled", settings.enabled);
    config_set_default_bool(config, "general", "require_streaming_active", settings.requireStreamingActive);

    config_set_default_string(config, "general", "command_trigger", narrow(settings.commandTrigger).c_str());
    config_set_default_string(config, "general", "response_template", narrow(settings.responseTemplate).c_str());
    config_set_default_string(config, "general", "not_playing_template", narrow(settings.notPlayingTemplate).c_str());
    config_set_default_string(config, "twitch", "client_id", settings.twitchClientId.c_str());
    config_set_default_string(config, "twitch", "client_secret", settings.twitchClientSecret.c_str());
    config_set_default_string(config, "twitch", "redirect_uri", settings.oauthRedirectUri.c_str());

    settings.enabled = config_get_bool(config, "general", "enabled");
    settings.requireStreamingActive = config_get_bool(config, "general", "require_streaming_active");

    settings.commandTrigger = widen(config_get_string(config, "general", "command_trigger"));
    if (shouldUseDefaultCommandTrigger(settings.commandTrigger))
        settings.commandTrigger = PluginSettings{}.commandTrigger;
    settings.responseTemplate = widen(config_get_string(config, "general", "response_template"));
    settings.notPlayingTemplate = widen(config_get_string(config, "general", "not_playing_template"));
    settings.twitchClientId = safeString(config_get_string(config, "twitch", "client_id"));
    if (settings.twitchClientId.empty())
        settings.twitchClientId = PluginSettings{}.twitchClientId;
    settings.twitchClientSecret = safeString(config_get_string(config, "twitch", "client_secret"));
    settings.oauthRedirectUri = safeString(config_get_string(config, "twitch", "redirect_uri"));
    if (!isUsableRedirectUri(settings.oauthRedirectUri))
        settings.oauthRedirectUri = PluginSettings{}.oauthRedirectUri;
    settings.twitchAccessToken = safeString(config_get_string(config, "twitch", "access_token"));
    settings.twitchRefreshToken = safeString(config_get_string(config, "twitch", "refresh_token"));
    settings.twitchLogin = safeString(config_get_string(config, "twitch", "login"));
    settings.twitchUserId = safeString(config_get_string(config, "twitch", "user_id"));
    settings.twitchBroadcasterId = safeString(config_get_string(config, "twitch", "broadcaster_id"));

    config_close(config);
    return settings;
}

void SettingsStore::save(const PluginSettings &settings) const
{
    config_t *config = nullptr;
    if (config_open(&config, path_.c_str(), CONFIG_OPEN_ALWAYS) != CONFIG_SUCCESS || !config)
        return;

    config_set_bool(config, "general", "enabled", settings.enabled);
    config_set_bool(config, "general", "require_streaming_active", settings.requireStreamingActive);

    config_set_string(config, "general", "command_trigger", narrow(settings.commandTrigger).c_str());
    config_set_string(config, "general", "response_template", narrow(settings.responseTemplate).c_str());
    config_set_string(config, "general", "not_playing_template", narrow(settings.notPlayingTemplate).c_str());
    config_set_string(config, "twitch", "client_id", settings.twitchClientId.c_str());
    config_set_string(config, "twitch", "client_secret", settings.twitchClientSecret.c_str());
    config_set_string(config, "twitch", "redirect_uri", settings.oauthRedirectUri.c_str());
    config_set_string(config, "twitch", "access_token", settings.twitchAccessToken.c_str());
    config_set_string(config, "twitch", "refresh_token", settings.twitchRefreshToken.c_str());
    config_set_string(config, "twitch", "login", settings.twitchLogin.c_str());
    config_set_string(config, "twitch", "user_id", settings.twitchUserId.c_str());
    config_set_string(config, "twitch", "broadcaster_id", settings.twitchBroadcasterId.c_str());
    config_save_safe(config, "tmp", nullptr);
    config_close(config);
}

void SettingsStore::clearTokens(PluginSettings &settings) const
{
    settings.twitchAccessToken.clear();
    settings.twitchRefreshToken.clear();
    settings.twitchLogin.clear();
    settings.twitchUserId.clear();
    settings.twitchBroadcasterId.clear();
    save(settings);
}

}
