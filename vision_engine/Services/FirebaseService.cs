using System;
using System.Net.Http;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using VisionBridge.Models;

namespace VisionBridge.Services
{
    public class FirebaseService
    {
        private readonly HttpClient _httpClient;
        private readonly FirebaseConfig _config;

        public FirebaseService(FirebaseConfig config)
        {
            _config = config;
            _httpClient = new HttpClient();
        }

        private string BuildUrl(string path)
        {
            var baseUrl = _config.DatabaseUrl.TrimEnd('/');
            var cleanPath = path.TrimStart('/');
            var url = $"{baseUrl}/{cleanPath}";

            if (!string.IsNullOrWhiteSpace(_config.AuthToken))
            {
                url += (url.Contains('?') ? "&" : "?") + $"auth={_config.AuthToken}";
            }

            return url;
        }

        public async Task<VisionRequest?> GetRequestAsync(string requestId, CancellationToken ct = default)
        {
            var url = BuildUrl($"{_config.RequestsPath}/{requestId}.json");
            try
            {
                var response = await _httpClient.GetAsync(url, ct);
                if (!response.IsSuccessStatusCode)
                {
                    LoggingService.Instance.LogError($"Firebase GET falhou: {response.StatusCode}");
                    return null;
                }

                var json = await response.Content.ReadAsStringAsync(ct);
                if (string.IsNullOrWhiteSpace(json) || json == "null")
                    return null;

                return JsonSerializer.Deserialize<VisionRequest>(json);
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Exceção ao buscar request {requestId} no Firebase: {ex.Message}");
                return null;
            }
        }

        public async Task<bool> UpdateStatusAsync(string requestId, string status, CancellationToken ct = default)
        {
            var url = BuildUrl($"{_config.RequestsPath}/{requestId}.json");
            var payload = new { status };
            var json = JsonSerializer.Serialize(payload);
            var content = new StringContent(json, Encoding.UTF8, "application/json");

            try
            {
                var request = new HttpRequestMessage(new HttpMethod("PATCH"), url) { Content = content };
                var response = await _httpClient.SendAsync(request, ct);
                return response.IsSuccessStatusCode;
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Erro ao atualizar status para '{status}': {ex.Message}");
                return false;
            }
        }

        public async Task<bool> SaveResultAsync(string requestId, VisionResult result, string deviceId = "esp32cam", CancellationToken ct = default)
        {
            var url = BuildUrl($"{_config.RequestsPath}/{requestId}.json");
            var payload = new
            {
                status = "completed",
                result = result
            };

            var json = JsonSerializer.Serialize(payload);
            var content = new StringContent(json, Encoding.UTF8, "application/json");

            try
            {
                var request = new HttpRequestMessage(new HttpMethod("PATCH"), url) { Content = content };
                var response = await _httpClient.SendAsync(request, ct);

                try
                {
                    var latestUrl = BuildUrl("latest_result.json");
                    var latestPayload = new
                    {
                        requestId = requestId,
                        deviceId = deviceId,
                        timestamp = DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
                        result = result
                    };
                    var latestJson = JsonSerializer.Serialize(latestPayload);
                    var latestContent = new StringContent(latestJson, Encoding.UTF8, "application/json");
                    await _httpClient.PutAsync(latestUrl, latestContent, ct);
                }
                catch { }

                return response.IsSuccessStatusCode;
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Erro ao salvar resultado no Firebase: {ex.Message}");
                return false;
            }
        }

        public async Task<bool> SaveErrorAsync(string requestId, string errorMessage, CancellationToken ct = default)
        {
            var url = BuildUrl($"{_config.RequestsPath}/{requestId}.json");
            var payload = new
            {
                status = "error",
                error = errorMessage
            };

            var json = JsonSerializer.Serialize(payload);
            var content = new StringContent(json, Encoding.UTF8, "application/json");

            try
            {
                var request = new HttpRequestMessage(new HttpMethod("PATCH"), url) { Content = content };
                var response = await _httpClient.SendAsync(request, ct);
                return response.IsSuccessStatusCode;
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Erro ao salvar mensagem de erro no Firebase: {ex.Message}");
                return false;
            }
        }

        public async Task<bool> DeleteImagePayloadAsync(string requestId, CancellationToken ct = default)
        {
            var url = BuildUrl($"{_config.RequestsPath}/{requestId}/image.json");
            try
            {
                var response = await _httpClient.DeleteAsync(url, ct);
                return response.IsSuccessStatusCode;
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Erro ao excluir payload Base64 da request {requestId}: {ex.Message}");
                return false;
            }
        }

        public async Task<bool> DeleteQueueItemAsync(string requestId, CancellationToken ct = default)
        {
            var url = BuildUrl($"{_config.QueuePath}/{requestId}.json");
            try
            {
                var response = await _httpClient.DeleteAsync(url, ct);
                return response.IsSuccessStatusCode;
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Erro ao excluir item da fila {requestId}: {ex.Message}");
                return false;
            }
        }
    }
}
