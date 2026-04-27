#include "settings-dialog.hpp"

#include <obs-frontend-api.h>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QClipboard>
#include <QPushButton>
#include <QGuiApplication>
#include <QTimer>
#include <QVBoxLayout>

namespace kamyshtracker {
namespace {

QString qs(const std::wstring &value)
{
    return QString::fromStdWString(value);
}

std::wstring ws(const QString &value)
{
    return value.toStdWString();
}

QString accountLabel(const ObsTwitchAccount &account)
{
    if (!account.isTwitch)
        return "OBS service is not Twitch";
    if (account.login.empty())
        return "Twitch selected, OBS hides login; OAuth account will be used";
    return QString::fromStdString(account.login);
}

}

SettingsDialog::SettingsDialog(SettingsStore &store, MediaMonitor &monitor, TwitchClient &twitch, QWidget *parent)
    : QDialog(parent), store_(store), monitor_(monitor), twitch_(twitch)
{
    setWindowTitle("KamyshTracker");
    resize(620, 420);

    settings_ = store_.load();

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    smtcStatus_ = new QLabel(this);
    obsAccount_ = new QLabel(this);
    oauthAccount_ = new QLabel(this);
    twitchStatus_ = new QLabel(this);

    enabled_ = new QCheckBox(this);
    requireStreaming_ = new QCheckBox(this);
    clientId_ = new QLineEdit(this);
    clientSecret_ = new QLineEdit(this);
    clientSecret_->setEchoMode(QLineEdit::Password);
    clientSecret_->setPlaceholderText("Optional; required for confidential Twitch apps");
    redirectUri_ = new QLineEdit(this);
    redirectUri_->setPlaceholderText("http://localhost:17635/callback");
    command_ = new QLineEdit(this);
    responseTemplate_ = new QLineEdit(this);
    notPlayingTemplate_ = new QLineEdit(this);
    cooldown_ = new QSpinBox(this);
    cooldown_->setRange(0, 3600);
    cooldown_->setSuffix(" sec");

    form->addRow("SMTC", smtcStatus_);
    form->addRow("OBS Twitch account", obsAccount_);
    form->addRow("OAuth account", oauthAccount_);
    form->addRow("Twitch status", twitchStatus_);
    form->addRow("Enabled", enabled_);
    form->addRow("Require stream active", requireStreaming_);
    form->addRow("Twitch Client ID", clientId_);
    form->addRow("Twitch Client Secret", clientSecret_);
    redirectUri_->setVisible(false);
    form->addRow("Commands", command_);
    form->addRow("Playing response", responseTemplate_);
    form->addRow("Not playing response", notPlayingTemplate_);
    form->addRow("Cooldown", cooldown_);
    layout->addLayout(form);

    auto *actions = new QHBoxLayout();
    auto *login = new QPushButton("Login", this);
    auto *logout = new QPushButton("Logout", this);
    auto *test = new QPushButton("Test chat message", this);
    auto *copyStatus = new QPushButton("Copy status", this);
    actions->addWidget(login);
    actions->addWidget(logout);
    actions->addWidget(test);
    actions->addWidget(copyStatus);
    actions->addStretch(1);
    layout->addLayout(actions);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    layout->addWidget(buttons);

    connect(login, &QPushButton::clicked, this, [this] {
        saveUi();
        std::string error;
        if (!twitch_.loginWithBrowser(settings_, settings_, error)) {
            QMessageBox::warning(this, "KamyshTracker", QString::fromStdString(error));
            return;
        }

        store_.save(settings_);
        twitch_.configure(settings_, [&] { return monitor_.current(); }, nullptr);
        twitch_.start();
        refreshStatus();
    });

    connect(copyStatus, &QPushButton::clicked, this, [this] {
        if (auto *clipboard = QGuiApplication::clipboard())
            clipboard->setText(twitchStatus_->text());
    });

    connect(logout, &QPushButton::clicked, this, [this] {
        twitch_.logout(settings_);
        store_.save(settings_);
        refreshStatus();
    });

    connect(test, &QPushButton::clicked, this, [this] {
        saveUi();
        store_.save(settings_);
        twitch_.configure(settings_, [&] { return monitor_.current(); }, nullptr);
        std::string error;
        if (!twitch_.sendTestMessage(error))
            QMessageBox::warning(this, "KamyshTracker", QString::fromStdString(error));
        refreshStatus();
    });

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveUi();
        store_.save(settings_);
        twitch_.configure(settings_, [&] { return monitor_.current(); }, nullptr);
        refreshStatus();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    loadUi();
    refreshStatus();

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SettingsDialog::refreshStatus);
    timer->start(2000);
}

void SettingsDialog::loadUi()
{
    enabled_->setChecked(settings_.enabled);
    requireStreaming_->setChecked(settings_.requireStreamingActive);
    clientId_->setText(QString::fromStdString(settings_.twitchClientId));
    clientSecret_->setText(QString::fromStdString(settings_.twitchClientSecret));
    redirectUri_->setText(QString::fromStdString(settings_.oauthRedirectUri));
    command_->setText(qs(settings_.commandTrigger));
    responseTemplate_->setText(qs(settings_.responseTemplate));
    notPlayingTemplate_->setText(qs(settings_.notPlayingTemplate));
    cooldown_->setValue(settings_.replyCooldownSeconds);
}

void SettingsDialog::saveUi()
{
    settings_.enabled = enabled_->isChecked();
    settings_.requireStreamingActive = requireStreaming_->isChecked();
    settings_.twitchClientId = clientId_->text().trimmed().toStdString();
    settings_.twitchClientSecret = clientSecret_->text().trimmed().toStdString();
    settings_.oauthRedirectUri = redirectUri_->text().trimmed().toStdString();
    settings_.commandTrigger = ws(command_->text());
    settings_.responseTemplate = ws(responseTemplate_->text());
    settings_.notPlayingTemplate = ws(notPlayingTemplate_->text());
    settings_.replyCooldownSeconds = cooldown_->value();
}

void SettingsDialog::refreshStatus()
{
    const auto state = monitor_.current();
    smtcStatus_->setText(qs(state.displayText(settings_.notPlayingTemplate)));
    obsAccount_->setText(accountLabel(readObsTwitchAccount()));
    oauthAccount_->setText(settings_.twitchLogin.empty()
        ? "Not authorized"
        : QString::fromStdString(settings_.twitchLogin));
    twitchStatus_->setText(QString::fromStdString(twitch_.status()));
}

}
