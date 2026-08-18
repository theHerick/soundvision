using System;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using VisionBridge.Models;

namespace VisionBridge.Services
{
    public class FirebaseStreamService
    {
        private readonly FirebaseConfig _config;
        private readonly HttpClient _httpClient;

        public event Action<string>? NewRequestReceived;
        public event Action<bool>? ConnectionStatusChanged;

        public FirebaseStreamService(FirebaseConfig config)
        {
            _config = config;
            _httpClient = new HttpClient
            {
                Timeout = Timeout.InfiniteTimeSpan
            };
        }

        public async Task StartListeningAsync(CancellationToken cancellationToken)
        {
            if (string.IsNullOrWhiteSpace(_config.DatabaseUrl) || _config.DatabaseUrl.Contains("YOUR_FIREBASE"))
            {
                LoggingService.Instance.LogWarn("Firebase URL não configurada. Serviço SSE suspenso.");
                ConnectionStatusChanged?.Invoke(false);
                return;
            }

            var baseUrl = _config.DatabaseUrl.TrimEnd('/');
            var url = $"{baseUrl}/{_config.QueuePath}.json?accept=text/event-stream";

            if (!string.IsNullOrWhiteSpace(_config.AuthToken))
            {
                url += $"&auth={_config.AuthToken}";
            }

            int backoffSeconds = 2;

            while (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    LoggingService.Instance.LogInfo("Conectando ao Firebase SSE Event Stream...");

                    using var request = new HttpRequestMessage(HttpMethod.Get, url);
                    request.Headers.Accept.Add(new System.Net.Http.Headers.MediaTypeWithQualityHeaderValue("text/event-stream"));

                    using var response = await _httpClient.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);

                    if (!response.IsSuccessStatusCode)
                    {
                        LoggingService.Instance.LogError($"Conexão SSE recusada. Código HTTP: {response.StatusCode}");
                        ConnectionStatusChanged?.Invoke(false);
                        await Task.Delay(TimeSpan.FromSeconds(backoffSeconds), cancellationToken);
                        backoffSeconds = Math.Min(backoffSeconds * 2, 30);
                        continue;
                    }

                    ConnectionStatusChanged?.Invoke(true);
                    LoggingService.Instance.LogSuccess("Conectado ao Firebase SSE Stream com sucesso! Aguardando fotos...");
                    backoffSeconds = 2;

                    using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
                    using var reader = new StreamReader(stream);

                    string currentEvent = string.Empty;

                    while (!reader.EndOfStream && !cancellationToken.IsCancellationRequested)
                    {
                        var line = await reader.ReadLineAsync(cancellationToken);
                        if (line == null) break;

                        if (line.StartsWith("event: "))
                        {
                            currentEvent = line.Substring(7).Trim();
                        }
                        else if (line.StartsWith("data: "))
                        {
                            var dataStr = line.Substring(6).Trim();
                            ProcessSseData(currentEvent, dataStr);
                        }
                    }
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    LoggingService.Instance.LogError($"Queda na conexão SSE Stream: {ex.Message}");
                    ConnectionStatusChanged?.Invoke(false);
                    await Task.Delay(TimeSpan.FromSeconds(backoffSeconds), cancellationToken);
                    backoffSeconds = Math.Min(backoffSeconds * 2, 30);
                }
            }

            ConnectionStatusChanged?.Invoke(false);
        }

        private void ProcessSseData(string eventName, string dataStr)
        {
            if (string.IsNullOrWhiteSpace(dataStr) || dataStr == "null") return;

            try
            {
                using var doc = JsonDocument.Parse(dataStr);
                var root = doc.RootElement;

                if (root.ValueKind == JsonValueKind.Object)
                {
                    if (root.TryGetProperty("path", out var pathElem) && root.TryGetProperty("data", out var dataElem))
                    {
                        var path = pathElem.GetString() ?? "";

                        if (path == "/" && dataElem.ValueKind == JsonValueKind.Object)
                        {
                            foreach (var prop in dataElem.EnumerateObject())
                            {
                                if (prop.Value.ValueKind == JsonValueKind.True || prop.Value.ValueKind == JsonValueKind.Object)
                                {
                                    LoggingService.Instance.LogInfo($"Nova requisição identificada na fila: {prop.Name}");
                                    NewRequestReceived?.Invoke(prop.Name);
                                }
                            }
                        }
                        else if (path.Length > 1)
                        {
                            var reqId = path.TrimStart('/');
                            if (dataElem.ValueKind == JsonValueKind.True || dataElem.ValueKind == JsonValueKind.Object)
                            {
                                LoggingService.Instance.LogInfo($"Nova requisição recebida via evento SSE: {reqId}");
                                NewRequestReceived?.Invoke(reqId);
                            }
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogWarn($"Falha ao decodificar evento SSE: {ex.Message}");
            }
        }
    }
}
