using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.Hosting;

namespace MediaTracker
{
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
            var builder = WebApplication.CreateBuilder();
            builder.WebHost.UseUrls(_url);
            var app = builder.Build();

            app.MapGet("/", () => _monitor.Current.ToEndpointString());
            app.MapGet("/json", () => _monitor.Current);

            _host = app;
            try
            {
                await app.StartAsync(token);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"HttpServer start failed: {ex}");
                throw;
            }
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
}