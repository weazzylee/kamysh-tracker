#pragma once

#include <string>

namespace kamyshtracker {

struct PluginSettings {
    bool enabled = true;
    bool requireStreamingActive = false;
    int replyCooldownSeconds = 30;
    std::wstring commandTrigger = L"!трек !track !shazam !шазам";
    std::wstring responseTemplate = L"Сейчас играет: {artist} - {title}";
    std::wstring notPlayingTemplate = L"Сейчас ничего не играет";
    std::string twitchClientId = "gvtl52d300qthfi0ryc4n7f5s7ocz1";
    std::string oauthRedirectUri = "http://localhost:17635/callback";
    std::string twitchAccessToken;
    std::string twitchRefreshToken;
    std::string twitchLogin;
    std::string twitchUserId;
    std::string twitchBroadcasterId;
};

class SettingsStore {
public:
    SettingsStore();

    [[nodiscard]] PluginSettings load() const;
    void save(const PluginSettings &settings) const;
    void clearTokens(PluginSettings &settings) const;

private:
    std::string path_;
};

}
