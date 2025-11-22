// Program.cs
// .NET 8, Windows-only app using WinForms tray + Kestrel HTTP + Windows SMTC (GlobalSystemMediaTransportControls).

using System;
using System.Diagnostics;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Windows.Media.Control;
using Windows.Media;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;

namespace MediaTracker
{
    static class Program
    {
        private const string MutexName = "Global\\MediaTracker_SingleInstance_Mutex_v2";
        private const string UrlPrefix = "http://localhost:5050";

        [STAThread]
        static void Main()
        {
            bool createdNew;
            using var mutex = new Mutex(true, MutexName, out createdNew);
            if (!createdNew)
            {
                MessageBox.Show("Приложение уже запущено.", "Media Tracker", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            ApplicationConfiguration.Initialize();

            using var cts = new CancellationTokenSource();

            var mediaMonitor = new MediaMonitor();
            var httpServer = new HttpServer(mediaMonitor, UrlPrefix);

            _ = mediaMonitor.StartAsync(cts.Token).ContinueWith(t =>
            {
                if (t.IsFaulted) Debug.WriteLine("MediaMonitor start failed: " + t.Exception);
            });

            _ = httpServer.StartAsync(cts.Token).ContinueWith(t =>
            {
                if (t.IsFaulted) Debug.WriteLine("HttpServer start failed: " + t.Exception);
            });

            Application.Run(new TrayApplicationContext(mediaMonitor, () =>
            {
                cts.Cancel();
                Task.Run(async () =>
                {
                    try { await httpServer.StopAsync(); } catch { }
                    try { await mediaMonitor.StopAsync(); } catch { }
                }).Wait(TimeSpan.FromSeconds(2));
            }));
        }
    }

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

    public class MediaMonitor : IDisposable
    {
        private readonly object _lock = new();
        private GlobalSystemMediaTransportControlsSessionManager? _manager;
        private GlobalSystemMediaTransportControlsSession? _session;
        private CancellationTokenSource? _backgroundCts;
        private CancellationTokenSource? _debounceCts;

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
                _debounceCts?.Cancel();
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
            Task.Run(() =>
            {
                UpdateCurrentSession();
                ScheduleRefresh();
            });
        }

        private void UpdateCurrentSession()
        {
            try
            {
                if (_manager == null) return;
                var sessions = _manager.GetSessions().ToList();

                // 1) Try current session first (Windows-provided)
                var pick = _manager.GetCurrentSession();

                // 2) If current is non-priority, try to prefer spotify/yandex among sessions
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
                _debounceCts?.Cancel();
                _debounceCts = new CancellationTokenSource();
                var token = _debounceCts.Token;

                _ = Task.Run(async () =>
                {
                    try
                    {
                        await Task.Delay(debounceMs, token);
                        if (token.IsCancellationRequested) return;
                        await RefreshNowPlayingAsync();
                    }
                    catch (TaskCanceledException) { }
                    catch (Exception ex) { Debug.WriteLine($"ScheduleRefresh task error: {ex}"); }
                }, token);
            }
        }

        /// <summary>
        /// NEW: Scan all sessions and pick the best one:
        ///  - prefer any session that is Playing (Spotify > Yandex > others)
        ///  - if none playing: keep last known (for display) but IsPlaying=false so endpoint returns "Сейчас ничего не играет"
        /// This avoids being stuck on a paused Spotify session while Yandex actually plays.
        /// </summary>
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
                if (sessions.Count == 0)
                {
                    PublishStateIfChanged(new MediaState());
                    return;
                }

                // gather info for all sessions
                var infos = new System.Collections.Generic.List<(GlobalSystemMediaTransportControlsSession session, string artist, string title, bool isPlaying, string id)>();

                foreach (var s in sessions)
                {
                    try
                    {
                        var props = await s.TryGetMediaPropertiesAsync();
                        var info = s.GetPlaybackInfo();
                        string artist = props?.Artist?.Trim() ?? "";
                        string title = props?.Title?.Trim() ?? "";
                        bool isPlaying = info?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
                        string id = s.SourceAppUserModelId ?? "";

                        infos.Add((s, artist, title, isPlaying, id));
                    }
                    catch
                    {
                        // ignore problematic session
                    }
                }

                // pick any that is playing with priority Spotify > Yandex > any
                var playingPick = infos
                    .Where(x => x.isPlaying)
                    .OrderBy(x =>
                    {
                        if (x.id.Contains("spotify", StringComparison.OrdinalIgnoreCase)) return 0;
                        if (x.id.Contains("yandex", StringComparison.OrdinalIgnoreCase)) return 1;
                        return 2;
                    })
                    .FirstOrDefault();

                if (playingPick.session != null)
                {
                    // some session is playing — use it
                    PublishStateIfChanged(new MediaState
                    {
                        Artist = playingPick.artist,
                        Title = playingPick.title,
                        IsPlaying = true,
                        SourceApp = playingPick.id,
                        Timestamp = DateTimeOffset.UtcNow
                    });
                    return;
                }

                // none playing — fall back to sensible last-known display
                // prefer current session, then priority Spotify/Yandex, then first session
                var currentSession = _manager.GetCurrentSession();
                (GlobalSystemMediaTransportControlsSession session, string artist, string title, bool isPlaying, string id)? displayPick = null;

                if (currentSession != null)
                {
                    var match = infos.FirstOrDefault(x => x.session == currentSession);
                    if (match.session != null) displayPick = match;
                }

                if (!displayPick.HasValue || displayPick.Value.session == null)
                {
                    displayPick = infos
                        .OrderBy(x =>
                        {
                            if (x.id.Contains("spotify", StringComparison.OrdinalIgnoreCase)) return 0;
                            if (x.id.Contains("yandex", StringComparison.OrdinalIgnoreCase)) return 1;
                            return 2;
                        })
                        .FirstOrDefault();
                }

                if (displayPick.HasValue && displayPick.Value.session != null)
                {
                    PublishStateIfChanged(new MediaState
                    {
                        Artist = displayPick.Value.artist,
                        Title = displayPick.Value.title,
                        IsPlaying = false,
                        SourceApp = displayPick.Value.id,
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
            GlobalSystemMediaTransportControlsSession? session;
            lock (_lock) { session = _session; }

            if (session == null)
                return false;

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
            try { _debounceCts?.Cancel(); } catch { }
            try { _backgroundCts?.Cancel(); } catch { }
            try { _ = StopAsync(); } catch { }
        }
    }

    public class HttpServer
    {
        private readonly MediaMonitor _monitor;
        private readonly string _url;
        private IHost? _host;

        public HttpServer(MediaMonitor monitor, string url)
        {
            _monitor = monitor;
            _url = url.TrimEnd('/');
        }

        public async Task StartAsync(CancellationToken token = default)
        {
            var builder = Host.CreateDefaultBuilder()
                .ConfigureWebHostDefaults(webBuilder =>
                {
                    webBuilder.UseUrls(_url);
                    webBuilder.Configure(app =>
                    {
                        app.UseRouting();
                        app.UseEndpoints(endpoints =>
                        {
                            endpoints.MapGet("/", async context =>
                            {
                                var state = _monitor.Current;
                                context.Response.ContentType = "text/plain; charset=utf-8";
                                await context.Response.WriteAsync(state.ToEndpointString());
                            });

                            endpoints.MapGet("/json", async context =>
                            {
                                var state = _monitor.Current;
                                context.Response.ContentType = "application/json; charset=utf-8";
                                var json = JsonSerializer.Serialize(new
                                {
                                    artist = state.Artist,
                                    title = state.Title,
                                    isPlaying = state.IsPlaying,
                                    source = state.SourceApp,
                                    timestamp = state.Timestamp
                                });
                                await context.Response.WriteAsync(json);
                            });
                        });
                    });
                });

            _host = builder.Build();
            await _host.StartAsync(token);
        }

        public async Task StopAsync()
        {
            if (_host != null)
            {
                try
                {
                    await _host.StopAsync(TimeSpan.FromSeconds(2));
                    _host.Dispose();
                }
                catch (Exception ex) { Debug.WriteLine($"HttpServer.StopAsync exception: {ex}"); }
                _host = null;
            }
        }
    }

    public class TrayApplicationContext : ApplicationContext
    {
        private readonly NotifyIcon _trayIcon;
        private readonly ContextMenuStrip _menu;
        private readonly MediaMonitor _monitor;
        private readonly Action _onExit;
        private SynchronizationContext? _uiContext;
        private DateTime _lastBalloon = DateTime.MinValue;

        public TrayApplicationContext(MediaMonitor monitor, Action onExit)
        {
            _monitor = monitor ?? throw new ArgumentNullException(nameof(monitor));
            _onExit = onExit ?? (() => { });

            _uiContext = SynchronizationContext.Current;

            _menu = new ContextMenuStrip();
            _menu.Items.Add(new ToolStripMenuItem("Pause/Play", null, PlayPause_Click));
            _menu.Items.Add(new ToolStripMenuItem("Copy now playing", null, CopyNowPlaying_Click));
            _menu.Items.Add(new ToolStripSeparator());
            _menu.Items.Add(new ToolStripMenuItem("Exit", null, Exit_Click));

            _trayIcon = new NotifyIcon()
            {
                Icon = SystemIcons.Application,
                ContextMenuStrip = _menu,
                Text = "Media Tracker",
                Visible = true
            };
            _trayIcon.DoubleClick += (_, _) => ShowNowPlayingBalloon();

            _monitor.StateChanged += OnMediaStateChanged;
            UpdateTooltip(_monitor.Current);
        }

        private void OnMediaStateChanged(MediaState s)
        {
            if (_uiContext != null)
                _uiContext.Post(_ => UpdateTooltip(s), null);
            else
                UpdateTooltip(s);
        }

        private void UpdateTooltip(MediaState s)
        {
            try
            {
                string text = s.ToDisplayString();
                if (text.Length > 120) text = text.Substring(0, 117) + "...";
                _trayIcon.Text = text;
            }
            catch { }
        }

        private void PlayPause_Click(object? sender, EventArgs e)
        {
            _ = _monitor.TogglePlayPauseAsync().ContinueWith(t =>
            {
                if (t.IsFaulted) Debug.WriteLine("PlayPause task failed: " + t.Exception);
            });
        }

        private void CopyNowPlaying_Click(object? sender, EventArgs e)
        {
            var s = _monitor.Current;
            string text = s.ToDisplayString();
            try
            {
                Clipboard.SetText(text);
                ShowBalloonLimited("Copied", text);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"CopyNowPlaying failed: {ex}");
            }
        }

        private void Exit_Click(object? sender, EventArgs e)
        {
            try { _trayIcon.Visible = false; } catch { }
            _monitor.StateChanged -= OnMediaStateChanged;
            _onExit?.Invoke();
            Application.Exit();
        }

        private void ShowNowPlayingBalloon()
        {
            var s = _monitor.Current;
            ShowBalloonLimited("Now playing", s.ToDisplayString());
        }

        private void ShowBalloonLimited(string title, string text)
        {
            if ((DateTime.UtcNow - _lastBalloon) < TimeSpan.FromSeconds(2)) return;
            _lastBalloon = DateTime.UtcNow;

            try { _trayIcon.ShowBalloonTip(2000, title, text, ToolTipIcon.Info); } catch { }
        }
    }
}
