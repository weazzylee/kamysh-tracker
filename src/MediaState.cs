using System;

namespace MediaTracker
{
    public class MediaState
    {
        public string Artist { get; init; } = "";
        public string Title { get; init; } = "";
        public bool IsPlaying { get; init; } = false;
        public string SourceApp { get; init; } = "";
        public DateTimeOffset Timestamp { get; init; } = DateTimeOffset.UtcNow;

        public string ToEndpointString() =>
            IsPlaying && !string.IsNullOrWhiteSpace(Artist + Title)
                ? $"{Artist} - {Title}"
                : "Сейчас ничего не играет";

        public string ToDisplayString() =>
            !string.IsNullOrWhiteSpace(Artist + Title)
                ? $"{Artist} - {Title}"
                : "Сейчас ничего не играет";
    }
}