#pragma once

#include <chrono>
#include <string>

namespace kamyshtracker {

struct MediaState {
    std::wstring artist;
    std::wstring title;
    bool isPlaying = false;
    std::wstring sourceApp;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();

    [[nodiscard]] bool hasText() const
    {
        return !artist.empty() || !title.empty();
    }

    [[nodiscard]] std::wstring displayText(const std::wstring &fallback) const
    {
        if (!isPlaying || !hasText())
            return fallback;

        if (artist.empty())
            return title;
        if (title.empty())
            return artist;
        return artist + L" - " + title;
    }
};

}
