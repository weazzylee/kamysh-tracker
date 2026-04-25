#pragma once

#include "media-state.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace kamyshtracker {

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
    void publishIfChanged(MediaState state);

    mutable std::mutex mutex_;
    MediaState current_;
    Callback callback_;
    std::atomic_bool running_{false};
    std::thread worker_;
};

}
