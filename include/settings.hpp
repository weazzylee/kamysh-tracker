#pragma once

#include <string>

namespace kamyshtracker {

struct PluginSettings {
    bool enabled = true;
    bool requireStreamingActive = false;
    static constexpr int replyCooldownSeconds = 1;
    std::wstring commandTrigger = L"!\u0442\u0440\u0435\u043a !track !shazam !\u0448\u0430\u0437\u0430\u043c";
    std::wstring responseTemplate = L"\u0421\u0435\u0439\u0447\u0430\u0441 \u0438\u0433\u0440\u0430\u0435\u0442: {artist} - {title}";
    std::wstring notPlayingTemplate = L"\u0421\u0435\u0439\u0447\u0430\u0441 \u043d\u0438\u0447\u0435\u0433\u043e \u043d\u0435 \u0438\u0433\u0440\u0430\u0435\u0442";
    std::string twitchClientId = "7hfl0kpbpsxpz0j2v1tg0q6hrvi6iw";
    std::string twitchClientSecret;
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
