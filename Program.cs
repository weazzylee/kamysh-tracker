// .NET 8, Windows-only app using WinForms tray + WebApplication HTTP + Windows SMTC.

using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MediaTracker
{
    static class Program
    {
        private const string MutexName = "Global\\MediaTracker_SingleInstance_Mutex_v3";
        private const string UrlPrefix = "http://127.0.0.1:5050";

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

            Application.Run(new TrayApplicationContext(mediaMonitor, async () =>
            {
                cts.Cancel();
                try { await httpServer.StopAsync(); } catch { }
                try { await mediaMonitor.StopAsync(); } catch { }
            }));
        }
    }

}
