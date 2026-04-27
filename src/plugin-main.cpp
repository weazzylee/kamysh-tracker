#include "media-monitor.hpp"
#include "settings-dialog.hpp"
#include "settings.hpp"
#include "twitch-client.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QWidget>
#include <array>
#include <cstdio>
#include <memory>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("kamyshtracker", "en-US")

namespace {

std::unique_ptr<kamyshtracker::SettingsStore> settingsStore;
std::unique_ptr<kamyshtracker::MediaMonitor> mediaMonitor;
std::unique_ptr<kamyshtracker::TwitchClient> twitchClient;
QPointer<kamyshtracker::SettingsDialog> settingsDialog;

constexpr std::array<int, 3> minimumObsVersion = {30, 0, 0};

std::array<int, 3> parseVersion(const char *version)
{
    std::array<int, 3> parsed = {0, 0, 0};
    if (!version)
        return parsed;

    std::sscanf(version, "%d.%d.%d", &parsed[0], &parsed[1], &parsed[2]);
    return parsed;
}

bool isAtLeast(std::array<int, 3> version, std::array<int, 3> minimum)
{
    return version[0] > minimum[0] ||
        (version[0] == minimum[0] && version[1] > minimum[1]) ||
        (version[0] == minimum[0] && version[1] == minimum[1] && version[2] >= minimum[2]);
}

void openSettings(void *)
{
    auto *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
    if (mediaMonitor)
        mediaMonitor->start();

    if (!settingsDialog) {
        settingsDialog = new kamyshtracker::SettingsDialog(
            *settingsStore,
            *mediaMonitor,
            *twitchClient,
            mainWindow);
        settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(settingsDialog, &QObject::destroyed, [] {
            if (!settingsStore || !mediaMonitor)
                return;

            const auto settings = settingsStore->load();
            if (!settings.enabled || settings.twitchAccessToken.empty())
                mediaMonitor->stop();
        });
    }

    settingsDialog->show();
    settingsDialog->raise();
    settingsDialog->activateWindow();
}

}

bool obs_module_load()
{
    const char *obsVersion = obs_get_version_string();
    if (!isAtLeast(parseVersion(obsVersion), minimumObsVersion)) {
        blog(LOG_ERROR,
            "[kamyshtracker] OBS Studio 30.0.0 or newer is required; current OBS version is %s",
            obsVersion ? obsVersion : "unknown");
        return false;
    }

    blog(LOG_INFO, "[kamyshtracker] loading native OBS plugin");

    settingsStore = std::make_unique<kamyshtracker::SettingsStore>();
    mediaMonitor = std::make_unique<kamyshtracker::MediaMonitor>();
    twitchClient = std::make_unique<kamyshtracker::TwitchClient>();

    auto settings = settingsStore->load();
    twitchClient->configure(
        settings,
        [] { return mediaMonitor ? mediaMonitor->current() : kamyshtracker::MediaState{}; },
        nullptr);
    twitchClient->setSettingsChangedCallback([](const kamyshtracker::PluginSettings &updatedSettings) {
        if (settingsStore)
            settingsStore->save(updatedSettings);
    });

    if (settings.enabled && !settings.twitchAccessToken.empty()) {
        mediaMonitor->start();
        twitchClient->start();
    }

    obs_frontend_add_tools_menu_item("KamyshTracker", openSettings, nullptr);
    return true;
}

void obs_module_unload()
{
    blog(LOG_INFO, "[kamyshtracker] unloading native OBS plugin");

    if (settingsDialog)
        settingsDialog->close();

    if (twitchClient)
        twitchClient->stop();
    if (mediaMonitor)
        mediaMonitor->stop();

    twitchClient.reset();
    mediaMonitor.reset();
    settingsStore.reset();
}

const char *obs_module_description()
{
    return "KamyshTracker: SMTC now-playing Twitch chat command for OBS";
}
