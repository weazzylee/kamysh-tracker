using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MediaTracker
{
    public class TrayApplicationContext : ApplicationContext
    {
        private readonly NotifyIcon _trayIcon;
        private readonly ContextMenuStrip _menu;
        private readonly MediaMonitor _monitor;
        private readonly Func<Task> _onExit;
        private SynchronizationContext? _uiContext;
        private DateTime _lastBalloon = DateTime.MinValue;

        public TrayApplicationContext(MediaMonitor monitor, Func<Task> onExit)
        {
            _monitor = monitor ?? throw new ArgumentNullException(nameof(monitor));
            _onExit = onExit ?? (() => Task.CompletedTask);

            _uiContext = SynchronizationContext.Current;

            _menu = new ContextMenuStrip();
            _menu.Items.Add(new ToolStripMenuItem("MediaTracker")
            {
                Enabled = false
            });
            _menu.Items.Add(new ToolStripSeparator());
            _menu.Items.Add(new ToolStripMenuItem("Pause/Play", null, PlayPause_Click));
            _menu.Items.Add(new ToolStripMenuItem("Copy now playing", null, CopyNowPlaying_Click));
            _menu.Items.Add(new ToolStripSeparator());
            _menu.Items.Add(new ToolStripMenuItem("Exit", null, Exit_Click));

            _trayIcon = new NotifyIcon()
            {
                Icon = new Icon(typeof(TrayApplicationContext).Assembly.GetManifestResourceStream("MediaTracker.Resources.icon.ico")!),
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
            => _ = _monitor.TogglePlayPauseAsync().ContinueWith(t => { if (t.IsFaulted) Debug.WriteLine("PlayPause task failed: " + t.Exception); });

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

        private async void Exit_Click(object? sender, EventArgs e)
        {
            _monitor.StateChanged -= OnMediaStateChanged;
            _trayIcon.Visible = false;
            _trayIcon.Dispose();
            await _onExit();
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