#include "media-monitor.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
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

    worker_ = std::thread([this] { workerLoop(); });
    return true;
}

void MediaMonitor::stop()
{
    if (!running_.exchange(false))
        return;

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

    while (running_) {
        refresh();
        for (int i = 0; i < 20 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    winrt::uninit_apartment();
}

void MediaMonitor::refresh()
{
    try {
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        auto sessions = manager.GetSessions();
        std::vector<SessionInfo> infos;
        infos.reserve(sessions.Size());

        for (const auto &session : sessions) {
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
