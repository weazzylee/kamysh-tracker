#pragma once

#include "media-state.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <winrt/Windows.Media.Control.h>

namespace kamyshtracker {

struct MediaSessionSubscription {
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession session{nullptr};
    winrt::event_token mediaPropertiesChanged{};
    winrt::event_token playbackInfoChanged{};
};

class MediaMonitor {
public:
    using Callback = std::function<void(const MediaState &)>;

    MediaMonitor();
    ~MediaMonitor();

    MediaMonitor(const MediaMonitor &) = delete;
    MediaMonitor &operator=(const MediaMonitor &) = delete;

    bool start();
    void stop();

    [[nodiscard]] MediaState current() const;
    void setCallback(Callback callback);

private:
    void workerLoop();
    void refresh();
    void refresh(
        const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager &manager,
        std::vector<MediaSessionSubscription> &subscriptions,
        const std::function<void()> &requestRefresh);
    void publishIfChanged(MediaState state);

    mutable std::mutex mutex_;
    MediaState current_;
    Callback callback_;
    std::atomic_bool running_{false};
    std::thread worker_;
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    bool refreshRequested_ = false;
};

}
