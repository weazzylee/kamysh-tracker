#pragma once

#include "media-monitor.hpp"
#include "settings.hpp"
#include "twitch-client.hpp"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>

namespace kamyshtracker {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(SettingsStore &store, MediaMonitor &monitor, TwitchClient &twitch, QWidget *parent = nullptr);

private:
    void loadUi();
    void saveUi();
    void refreshStatus();

    SettingsStore &store_;
    MediaMonitor &monitor_;
    TwitchClient &twitch_;
    PluginSettings settings_;

    QLabel *smtcStatus_ = nullptr;
    QLabel *obsAccount_ = nullptr;
    QLabel *oauthAccount_ = nullptr;
    QLabel *twitchStatus_ = nullptr;
    QCheckBox *enabled_ = nullptr;
    QCheckBox *requireStreaming_ = nullptr;
    QLineEdit *clientId_ = nullptr;
    QLineEdit *clientSecret_ = nullptr;
    QLineEdit *redirectUri_ = nullptr;
    QLineEdit *command_ = nullptr;
    QLineEdit *responseTemplate_ = nullptr;
    QLineEdit *notPlayingTemplate_ = nullptr;
};

}
