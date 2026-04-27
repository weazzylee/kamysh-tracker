#include "media-monitor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <utility>
#include <thread>
#include <vector>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

namespace kamyshtracker {
namespace {

using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

constexpr auto fallbackRefreshInterval = std::chrono::seconds(45);
constexpr auto eventCoalesceDelay = std::chrono::milliseconds(350);

constexpr std::array<std::wstring_view, 5> musicKeywords = {
    L"spotify", L"yandex", L"yandexmusic", L"yamusic", L"yandex.music"
};

constexpr std::array<std::wstring_view, 14> nonMusicKeywords = {
    L"twitch", L"youtube", L"netflix", L"prime video", L"primevideo", L"mozilla",
    L"firefox", L"chrome", L"edge", L"vimeo", L"soundcloud", L"facebook",
    L"instagram", L"browser"
};

std::wstring lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

bool containsAny(const std::wstring &text, const auto &keywords)
{
    const auto lowered = lower(text);
    for (const auto keyword : keywords) {
        if (lowered.find(keyword) != std::wstring::npos)
            return true;
    }
    return false;
}

struct SessionInfo {
    GlobalSystemMediaTransportControlsSession session{nullptr};
    std::wstring artist;
    std::wstring title;
    bool isPlaying = false;
    std::wstring id;
};

MediaState toState(const SessionInfo &info, bool isPlaying)
{
    return MediaState{
        .artist = info.artist,
        .title = info.title,
        .isPlaying = isPlaying,
        .sourceApp = info.id,
        .timestamp = std::chrono::system_clock::now(),
    };
}

void clearSubscriptions(std::vector<MediaSessionSubscription> &subscriptions)
{
    for (auto &subscription : subscriptions) {
        try {
            if (subscription.session) {
                subscription.session.MediaPropertiesChanged(subscription.mediaPropertiesChanged);
                subscription.session.PlaybackInfoChanged(subscription.playbackInfoChanged);
            }
        } catch (...) {
        }
    }
    subscriptions.clear();
}

}

MediaMonitor::MediaMonitor() = default;

MediaMonitor::~MediaMonitor()
{
    stop();
}

bool MediaMonitor::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
        return true;

    {
        std::lock_guard lock(wakeMutex_);
        refreshRequested_ = true;
    }
    worker_ = std::thread([this] { workerLoop(); });
    return true;
}

void MediaMonitor::stop()
{
    if (!running_.exchange(false))
        return;

    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

MediaState MediaMonitor::current() const
{
    std::lock_guard lock(mutex_);
    return current_;
}

void MediaMonitor::setCallback(Callback callback)
{
    std::lock_guard lock(mutex_);
    callback_ = std::move(callback);
}

void MediaMonitor::workerLoop()
{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    winrt::event_token sessionsChanged{};
    winrt::event_token currentSessionChanged{};
    std::vector<MediaSessionSubscription> subscriptions;

    auto requestRefresh = [this] {
        {
            std::lock_guard lock(wakeMutex_);
            refreshRequested_ = true;
        }
        wake_.notify_one();
    };

    try {
        manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        sessionsChanged = manager.SessionsChanged([requestRefresh](auto &&, auto &&) {
            requestRefresh();
        });
        currentSessionChanged = manager.CurrentSessionChanged([requestRefresh](auto &&, auto &&) {
            requestRefresh();
        });
    } catch (...) {
        publishIfChanged({});
    }

    auto nextFallback = std::chrono::steady_clock::now();
    while (running_) {
        bool shouldRefresh = false;
        bool eventDriven = false;
        {
            std::unique_lock lock(wakeMutex_);
            if (!refreshRequested_) {
                wake_.wait_until(lock, nextFallback, [this] {
                    return !running_ || refreshRequested_;
                });
            }

            if (!running_)
                break;

            const auto now = std::chrono::steady_clock::now();
            eventDriven = refreshRequested_;
            shouldRefresh = refreshRequested_ || now >= nextFallback;
            refreshRequested_ = false;
        }

        if (!shouldRefresh)
            continue;

        if (eventDriven) {
            std::unique_lock lock(wakeMutex_);
            wake_.wait_for(lock, eventCoalesceDelay, [this] {
                return !running_;
            });
            if (!running_)
                break;
            refreshRequested_ = false;
        }

        if (manager)
            refresh(manager, subscriptions, requestRefresh);
        else
            refresh();
        nextFallback = std::chrono::steady_clock::now() + fallbackRefreshInterval;
    }

    clearSubscriptions(subscriptions);
    try {
        if (manager) {
            manager.SessionsChanged(sessionsChanged);
            manager.CurrentSessionChanged(currentSessionChanged);
        }
    } catch (...) {
    }

    winrt::uninit_apartment();
}

void MediaMonitor::refresh()
{
    try {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        std::vector<MediaSessionSubscription> subscriptions;
        refresh(manager, subscriptions, {});
    } catch (...) {
        publishIfChanged({});
    }
}

void MediaMonitor::refresh(
    const GlobalSystemMediaTransportControlsSessionManager &manager,
    std::vector<MediaSessionSubscription> &subscriptions,
    const std::function<void()> &requestRefresh)
{
    try {
        auto sessions = manager.GetSessions();
        clearSubscriptions(subscriptions);

        std::vector<SessionInfo> infos;
        infos.reserve(sessions.Size());
        subscriptions.reserve(sessions.Size());

        for (const auto &session : sessions) {
            if (requestRefresh) {
                MediaSessionSubscription subscription;
                subscription.session = session;
                subscription.mediaPropertiesChanged = session.MediaPropertiesChanged([requestRefresh](auto &&, auto &&) {
                    requestRefresh();
                });
                subscription.playbackInfoChanged = session.PlaybackInfoChanged([requestRefresh](auto &&, auto &&) {
                    requestRefresh();
                });
                subscriptions.push_back(std::move(subscription));
            }

            SessionInfo info;
            info.session = session;
            info.id = std::wstring(session.SourceAppUserModelId().c_str());

            try {
                auto props = session.TryGetMediaPropertiesAsync().get();
                info.artist = std::wstring(props.Artist().c_str());
                info.title = std::wstring(props.Title().c_str());
            } catch (...) {
            }

            try {
                const auto playback = session.GetPlaybackInfo();
                info.isPlaying =
                    playback.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            } catch (...) {
            }

            infos.push_back(std::move(info));
        }

        if (infos.empty()) {
            publishIfChanged({});
            return;
        }

        auto playingWhitelisted = std::find_if(infos.begin(), infos.end(), [](const SessionInfo &info) {
            return info.isPlaying && containsAny(info.id, musicKeywords);
        });
        if (playingWhitelisted != infos.end()) {
            publishIfChanged(toState(*playingWhitelisted, true));
            return;
        }

        auto playingLikelyMusic = std::find_if(infos.begin(), infos.end(), [](const SessionInfo &info) {
            return info.isPlaying &&
                (!info.artist.empty() || !containsAny(info.title + info.id, nonMusicKeywords));
        });
        if (playingLikelyMusic != infos.end()) {
            publishIfChanged(toState(*playingLikelyMusic, true));
            return;
        }

        auto playingOther = std::find_if(infos.begin(), infos.end(), [](const SessionInfo &info) {
            return info.isPlaying && !containsAny(info.title + info.id, nonMusicKeywords);
        });
        if (playingOther != infos.end()) {
            publishIfChanged(toState(*playingOther, true));
            return;
        }

        auto currentSession = manager.GetCurrentSession();
        auto displayPick = std::find_if(infos.begin(), infos.end(), [&](const SessionInfo &info) {
            return info.session == currentSession;
        });

        if (displayPick == infos.end()) {
            std::stable_sort(infos.begin(), infos.end(), [](const SessionInfo &left, const SessionInfo &right) {
                const auto rank = [](const SessionInfo &info) {
                    if (containsAny(info.id, musicKeywords))
                        return 0;
                    if (!info.artist.empty())
                        return 1;
                    return 2;
                };
                return rank(left) < rank(right);
            });
            displayPick = infos.begin();
        }

        publishIfChanged(toState(*displayPick, false));
    } catch (...) {
        publishIfChanged({});
    }
}

void MediaMonitor::publishIfChanged(MediaState state)
{
    Callback callback;
    {
        std::lock_guard lock(mutex_);
        if (current_.artist == state.artist && current_.title == state.title &&
            current_.isPlaying == state.isPlaying && current_.sourceApp == state.sourceApp)
            return;

        current_ = std::move(state);
        callback = callback_;
    }

    if (callback)
        callback(current());
}

}
