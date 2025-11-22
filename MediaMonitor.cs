using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Windows.Media.Control;
using Windows.Media;

namespace MediaTracker
{
    public class MediaMonitor : IDisposable
    {
        // whitelist ключевых слов для музыкальных приложений (проверяем SourceAppUserModelId)
        private static readonly string[] MusicAppKeywords =
        [
            "spotify",
            "yandex",
            "yandexmusic",
            "yamusic",
            "yandex.music"
        ];

        // blacklist ключевых слов для явно не-музыкальных источников (встречаются в title или id)
        private static readonly string[] NonMusicKeywords =
        [
            "twitch",
            "youtube",
            "netflix",
            "prime video",
            "primevideo",
            "mozilla",
            "firefox",
            "chrome",
            "edge",
            "vimeo",
            "soundcloud", // soundcloud — может быть муз., но при желании убрать
            "facebook",
            "instagram"
        ];

        private readonly object _lock = new();
        private GlobalSystemMediaTransportControlsSessionManager? _manager;
        private GlobalSystemMediaTransportControlsSession? _session;
        private CancellationTokenSource? _backgroundCts;
        private System.Threading.Timer? _debounceTimer;
        private List<GlobalSystemMediaTransportControlsSession> _subscribedSessions = new();

        private MediaState _current = new();
        public MediaState Current
        {
            get { lock (_lock) return _current; }
            private set { lock (_lock) { _current = value; } }
        }

        public event Action<MediaState>? StateChanged;
        public bool IsStarted { get; private set; } = false;

        public async Task StartAsync(CancellationToken token = default)
        {
            if (IsStarted) return;
            IsStarted = true;

            _backgroundCts = CancellationTokenSource.CreateLinkedTokenSource(token);

            _manager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();

            _manager.SessionsChanged += Manager_SessionsChanged;
            _manager.CurrentSessionChanged += Manager_CurrentSessionChanged;

            UpdateCurrentSession();
            ScheduleRefresh(0);
        }

        public async Task StopAsync()
        {
            if (!IsStarted) return;
            IsStarted = false;

            try
            {
                _debounceTimer?.Change(Timeout.Infinite, Timeout.Infinite);
                _backgroundCts?.Cancel();

                lock (_lock)
                {
                    if (_session != null)
                    {
                        try
                        {
                            _session.MediaPropertiesChanged -= Session_MediaPropertiesChanged;
                            _session.PlaybackInfoChanged -= Session_PlaybackInfoChanged;
                        }
                        catch { }
                    }

                    if (_manager != null)
                    {
                        try
                        {
                            _manager.SessionsChanged -= Manager_SessionsChanged;
                            _manager.CurrentSessionChanged -= Manager_CurrentSessionChanged;
                        }
                        catch { }
                    }

                    // Unsubscribe from all sessions
                    foreach (var s in _subscribedSessions)
                    {
                        try
                        {
                            s.MediaPropertiesChanged -= Session_MediaPropertiesChanged;
                            s.PlaybackInfoChanged -= Session_PlaybackInfoChanged;
                        }
                        catch { }
                    }
                    _subscribedSessions.Clear();
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"MediaMonitor.StopAsync exception: {ex}");
            }

            await Task.CompletedTask;
        }

        private void Manager_CurrentSessionChanged(GlobalSystemMediaTransportControlsSessionManager? sender, CurrentSessionChangedEventArgs? args)
            => ScheduleSessionUpdate();

        private void Manager_SessionsChanged(GlobalSystemMediaTransportControlsSessionManager? sender, object? args)
            => ScheduleSessionUpdate();

        private void ScheduleSessionUpdate()
        {
            UpdateCurrentSession();
            ScheduleRefresh();
        }

        private void UpdateCurrentSession()
        {
            try
            {
                if (_manager == null) return;
                var sessions = _manager.GetSessions().ToList();

                // Unsubscribe from old sessions
                foreach (var s in _subscribedSessions)
                {
                    try
                    {
                        s.MediaPropertiesChanged -= Session_MediaPropertiesChanged;
                        s.PlaybackInfoChanged -= Session_PlaybackInfoChanged;
                    }
                    catch { }
                }
                _subscribedSessions.Clear();

                // Subscribe to new sessions
                _subscribedSessions.AddRange(sessions);
                foreach (var s in sessions)
                {
                    s.MediaPropertiesChanged += Session_MediaPropertiesChanged;
                    s.PlaybackInfoChanged += Session_PlaybackInfoChanged;
                }

                var pick = _manager.GetCurrentSession();

                if (pick != null)
                {
                    string id = pick.SourceAppUserModelId ?? "";
                    if (!id.Contains("spotify", StringComparison.OrdinalIgnoreCase) &&
                        !id.Contains("yandex", StringComparison.OrdinalIgnoreCase))
                    {
                        pick = sessions.FirstOrDefault(s => s.SourceAppUserModelId?.Contains("spotify", StringComparison.OrdinalIgnoreCase) == true)
                               ?? sessions.FirstOrDefault(s => s.SourceAppUserModelId?.Contains("yandex", StringComparison.OrdinalIgnoreCase) == true)
                               ?? pick;
                    }
                }
                else
                {
                    pick = sessions.FirstOrDefault(s => s.SourceAppUserModelId?.Contains("spotify", StringComparison.OrdinalIgnoreCase) == true)
                           ?? sessions.FirstOrDefault(s => s.SourceAppUserModelId?.Contains("yandex", StringComparison.OrdinalIgnoreCase) == true)
                           ?? sessions.FirstOrDefault();
                }

                lock (_lock)
                {
                    if (_session != null && _session != pick)
                    {
                        try
                        {
                            _session.MediaPropertiesChanged -= Session_MediaPropertiesChanged;
                            _session.PlaybackInfoChanged -= Session_PlaybackInfoChanged;
                        }
                        catch { }
                    }

                    _session = pick;

                    if (_session != null)
                    {
                        _session.MediaPropertiesChanged += Session_MediaPropertiesChanged;
                        _session.PlaybackInfoChanged += Session_PlaybackInfoChanged;
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"UpdateCurrentSession error: {ex}");
            }
        }

        private void Session_MediaPropertiesChanged(GlobalSystemMediaTransportControlsSession? sender, MediaPropertiesChangedEventArgs? args)
            => ScheduleRefresh();

        private void Session_PlaybackInfoChanged(GlobalSystemMediaTransportControlsSession? sender, PlaybackInfoChangedEventArgs? args)
            => ScheduleRefresh();

        private void ScheduleRefresh(int debounceMs = 150)
        {
            lock (_lock)
            {
                _debounceTimer?.Change(Timeout.Infinite, Timeout.Infinite);
                if (_debounceTimer == null)
                {
                    _debounceTimer = new System.Threading.Timer(_ => _ = RefreshNowPlayingAsync(), null, debounceMs, Timeout.Infinite);
                }
                else
                {
                    _debounceTimer.Change(debounceMs, Timeout.Infinite);
                }
            }
        }

        private async Task RefreshNowPlayingAsync()
        {
            try
            {
                if (_manager == null)
                {
                    PublishStateIfChanged(new MediaState());
                    return;
                }

                var sessions = _manager.GetSessions().ToList();
                if (!sessions.Any()) { PublishStateIfChanged(new MediaState()); return; }

                var tasks = sessions.Select(async s =>
                {
                    try
                    {
                        var propsTask = s.TryGetMediaPropertiesAsync().AsTask();
                        var completed = await Task.WhenAny(propsTask, Task.Delay(800));
                        if (completed != propsTask) return (s: s, artist: "", title: "", isPlaying: false, id: s.SourceAppUserModelId ?? "");
                        var props = await propsTask;
                        var info = s.GetPlaybackInfo();
                        string artist = props?.Artist?.Trim() ?? "";
                        string title = props?.Title?.Trim() ?? "";
                        bool isPlaying = info?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
                        string id = s.SourceAppUserModelId ?? "";
                        return (s, artist, title, isPlaying, id);
                    }
                    catch { return (s: s, artist: "", title: "", isPlaying: false, id: s.SourceAppUserModelId ?? ""); }
                }).ToArray();

                var infos = (await Task.WhenAll(tasks)).ToList();

                // 1) Сначала — играющие сессии из whitelist (Spotify, Yandex)
                var playingWhitelisted = infos
                    .Where(x => x.isPlaying && ContainsAnyKeyword(x.id, MusicAppKeywords))
                    .OrderBy(x => !ContainsAnyKeyword(x.id, MusicAppKeywords)) // сохраняем порядок (необязательно)
                    .FirstOrDefault();

                if (playingWhitelisted.s != null)
                {
                    PublishStateIfChanged(new MediaState
                    {
                        Artist = playingWhitelisted.artist,
                        Title = playingWhitelisted.title,
                        IsPlaying = true,
                        SourceApp = playingWhitelisted.id,
                        Timestamp = DateTimeOffset.UtcNow
                    });
                    return;
                }

                // 2) Если нет — ищем играющие сессии, которые выглядят как музыка:
                //    - есть artist OR title выглядит не как страница (не содержит blacklist)
                var playingLikelyMusic = infos
                    .Where(x => x.isPlaying && ( !string.IsNullOrWhiteSpace(x.artist) || !ContainsAnyKeyword(x.title + x.id, NonMusicKeywords) ))
                    .OrderBy(x => string.IsNullOrWhiteSpace(x.artist) ? 1 : 0) // предпочитаем с artist
                    .FirstOrDefault();

                if (playingLikelyMusic.s != null)
                {
                    PublishStateIfChanged(new MediaState
                    {
                        Artist = playingLikelyMusic.artist,
                        Title = playingLikelyMusic.title,
                        IsPlaying = true,
                        SourceApp = playingLikelyMusic.id,
                        Timestamp = DateTimeOffset.UtcNow
                    });
                    return;
                }

                // 3) Если всё ещё ничего — (опционально) считать другие playing как fallback,
                //    но фильтруем по blacklist (не показываем Twitch/YouTube если можно)
                var playingOther = infos
                    .Where(x => x.isPlaying && !ContainsAnyKeyword(x.title + x.id, NonMusicKeywords))
                    .FirstOrDefault();

                if (playingOther.s != null)
                {
                    PublishStateIfChanged(new MediaState
                    {
                        Artist = playingOther.artist,
                        Title = playingOther.title,
                        IsPlaying = true,
                        SourceApp = playingOther.id,
                        Timestamp = DateTimeOffset.UtcNow
                    });
                    return;
                }

                // 4) Ничего подходящего не играет — показываем лучший last-known (как раньше)
                var currentSession = _manager.GetCurrentSession();
                var displayPick = infos.FirstOrDefault(x => x.s == currentSession);
                if (displayPick.s == null)
                {
                    displayPick = infos
                        .OrderBy(x =>
                        {
                            if (ContainsAnyKeyword(x.id, MusicAppKeywords)) return 0;
                            if (!string.IsNullOrWhiteSpace(x.artist)) return 1;
                            return 2;
                        })
                        .FirstOrDefault();
                }

                if (displayPick.s != null)
                {
                    PublishStateIfChanged(new MediaState
                    {
                        Artist = displayPick.artist,
                        Title = displayPick.title,
                        IsPlaying = false,
                        SourceApp = displayPick.id,
                        Timestamp = DateTimeOffset.UtcNow
                    });
                }
                else
                {
                    PublishStateIfChanged(new MediaState());
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"RefreshNowPlayingAsync (aggregate) error: {ex}");
                PublishStateIfChanged(new MediaState());
            }
        }

        private void PublishStateIfChanged(MediaState newState)
        {
            bool changed = false;
            lock (_lock)
            {
                var prev = _current;
                if (!string.Equals(prev.Artist, newState.Artist, StringComparison.Ordinal)
                    || !string.Equals(prev.Title, newState.Title, StringComparison.Ordinal)
                    || prev.IsPlaying != newState.IsPlaying
                    || !string.Equals(prev.SourceApp, newState.SourceApp, StringComparison.Ordinal))
                {
                    _current = newState;
                    changed = true;
                }
            }
            if (changed)
            {
                try { StateChanged?.Invoke(newState); } catch { }
            }
        }

        public async Task<bool> TogglePlayPauseAsync()
        {
            var state = Current;
            var sessions = _manager?.GetSessions() ?? Enumerable.Empty<GlobalSystemMediaTransportControlsSession>();
            var session = sessions.FirstOrDefault(s => string.Equals(s.SourceAppUserModelId, state.SourceApp, StringComparison.OrdinalIgnoreCase))
                          ?? _session;
            if (session == null) return false;

            try
            {
                var info = session.GetPlaybackInfo();
                if (info?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing)
                    await session.TryPauseAsync();
                else
                    await session.TryPlayAsync();

                ScheduleRefresh();
                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"TogglePlayPauseAsync failed: {ex}");
                return false;
            }
        }

        public void Dispose()
        {
            try { _debounceTimer?.Change(Timeout.Infinite, Timeout.Infinite); } catch { }
            try { _backgroundCts?.Cancel(); } catch { }
            try { _ = StopAsync(); } catch { }
        }

        private static bool ContainsAnyKeyword(string text, string[] keywords)
        {
            if (string.IsNullOrWhiteSpace(text)) return false;
            foreach (var k in keywords)
                if (text.IndexOf(k, StringComparison.OrdinalIgnoreCase) >= 0)
                    return true;
            return false;
        }
    }
}