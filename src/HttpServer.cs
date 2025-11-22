using System;
using System.Diagnostics;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace MediaTracker
{
    public class HttpServer
    {
        private readonly MediaMonitor _monitor;
        private readonly string _url;
        private HttpListener? _listener;
        private CancellationTokenSource? _cts;

        public HttpServer(MediaMonitor monitor, string url)
        {
            _monitor = monitor;
            _url = url.TrimEnd('/');
        }

        public async Task StartAsync(CancellationToken token = default)
        {
            _cts = CancellationTokenSource.CreateLinkedTokenSource(token);
            _listener = new HttpListener();
            _listener.Prefixes.Add(_url + "/");
            _listener.Start();

            while (!_cts.Token.IsCancellationRequested)
            {
                try
                {
                    var context = await _listener.GetContextAsync().WaitAsync(_cts.Token);
                    _ = HandleRequestAsync(context);
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Debug.WriteLine($"HttpServer error: {ex}");
                }
            }
        }

        private async Task HandleRequestAsync(HttpListenerContext context)
        {
            try
            {
                string responseString;
                string contentType;
                if (context.Request.Url?.AbsolutePath == "/json")
                {
                    responseString = JsonSerializer.Serialize(_monitor.Current);
                    contentType = "application/json";
                }
                else
                {
                    responseString = _monitor.Current.ToEndpointString();
                    contentType = "text/plain; charset=utf-8";
                }
                var buffer = Encoding.UTF8.GetBytes(responseString);
                context.Response.ContentType = contentType;
                context.Response.ContentLength64 = buffer.Length;
                await context.Response.OutputStream.WriteAsync(buffer, 0, buffer.Length);
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"HandleRequest error: {ex}");
                context.Response.StatusCode = 500;
            }
            finally
            {
                context.Response.Close();
            }
        }

        public async Task StopAsync()
        {
            _cts?.Cancel();
            if (_listener != null)
            {
                _listener.Stop();
                _listener.Close();
            }
            await Task.CompletedTask;
        }
    }
}