using System;
using System.IO;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using VisionBridge.Models;
using VisionBridge.Vision;

namespace VisionBridge.Services
{
    public class VisionProcessor
    {
        private readonly FirebaseService _firebaseService;
        private readonly IVisionRecognitionService _visionService;
        private readonly ColorAnalyzerService _colorAnalyzer;
        private readonly ApplicationConfig _appConfig;

        private readonly Channel<string> _requestChannel = Channel.CreateUnbounded<string>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });

        public event Action<string>? ProcessingStarted;
        public event Action<string, VisionResult, string, string>? ProcessingCompleted;
        public event Action<string, string>? ProcessingFailed;

        public VisionProcessor(
            FirebaseService firebaseService,
            IVisionRecognitionService visionService,
            ColorAnalyzerService colorAnalyzer,
            ApplicationConfig appConfig)
        {
            _firebaseService = firebaseService;
            _visionService = visionService;
            _colorAnalyzer = colorAnalyzer;
            _appConfig = appConfig;
        }

        public void EnqueueRequest(string requestId)
        {
            _requestChannel.Writer.TryWrite(requestId);
        }

        public async Task StartProcessingLoopAsync(CancellationToken cancellationToken)
        {
            var reader = _requestChannel.Reader;

            while (await reader.WaitToReadAsync(cancellationToken))
            {
                while (reader.TryRead(out var requestId))
                {
                    try
                    {
                        await ProcessSingleRequestAsync(requestId, cancellationToken);
                    }
                    catch (Exception ex)
                    {
                        LoggingService.Instance.LogError($"Exceção não tratada na fila do processor para {requestId}: {ex.Message}");
                    }
                }
            }
        }

        public async Task ProcessSingleRequestAsync(string requestId, CancellationToken cancellationToken)
        {
            LoggingService.Instance.LogInfo($"Iniciando processamento da requisição {requestId}...");
            ProcessingStarted?.Invoke(requestId);

            await _firebaseService.UpdateStatusAsync(requestId, "processing", cancellationToken);

            var request = await _firebaseService.GetRequestAsync(requestId, cancellationToken);

            if (request == null || string.IsNullOrWhiteSpace(request.ImageBase64))
            {
                LoggingService.Instance.LogError($"Requisição {requestId} não possui payload de imagem válido.");
                await _firebaseService.SaveErrorAsync(requestId, "Imagem Base64 ausente ou inválida", cancellationToken);
                await _firebaseService.DeleteQueueItemAsync(requestId, cancellationToken);
                ProcessingFailed?.Invoke(requestId, "Payload inválido");
                return;
            }

            var tempDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, _appConfig.TempDirectory);
            Directory.CreateDirectory(tempDir);
            var imagePath = Path.Combine(tempDir, $"{requestId}.jpg");

            try
            {
                var imageBytes = Convert.FromBase64String(CleanBase64(request.ImageBase64));
                await File.WriteAllBytesAsync(imagePath, imageBytes, cancellationToken);

                var visionResult = await _visionService.AnalyzeImageAsync(imagePath, cancellationToken);

                var localColor = _colorAnalyzer.IdentifyDominantColor(imagePath);
                if (string.IsNullOrWhiteSpace(visionResult.Color) || visionResult.Color == "cor")
                {
                    visionResult.Color = localColor;
                }

                LoggingService.Instance.LogSuccess($"Análise concluída: Objeto = '{visionResult.Object}', Cor = '{visionResult.Color}'");

                await _firebaseService.SaveResultAsync(requestId, visionResult, request.DeviceId, cancellationToken);
                await _firebaseService.DeleteImagePayloadAsync(requestId, cancellationToken);
                await _firebaseService.DeleteQueueItemAsync(requestId, cancellationToken);

                ProcessingCompleted?.Invoke(requestId, visionResult, imagePath, request.DeviceId);
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Falha no processamento da requisição {requestId}: {ex.Message}");
                await _firebaseService.SaveErrorAsync(requestId, ex.Message, cancellationToken);
                await _firebaseService.DeleteQueueItemAsync(requestId, cancellationToken);
                ProcessingFailed?.Invoke(requestId, ex.Message);
            }
        }

        public async Task ProcessLocalImageAsync(string imagePath)
        {
            var reqId = $"local_{DateTime.Now:HHmmss}";
            ProcessingStarted?.Invoke(reqId);

            try
            {
                var visionResult = await _visionService.AnalyzeImageAsync(imagePath);
                var localColor = _colorAnalyzer.IdentifyDominantColor(imagePath);
                
                if (string.IsNullOrWhiteSpace(visionResult.Color) || visionResult.Color == "cor")
                {
                    visionResult.Color = localColor;
                }

                ProcessingCompleted?.Invoke(reqId, visionResult, imagePath, "local_test");
            }
            catch (Exception ex)
            {
                ProcessingFailed?.Invoke(reqId, ex.Message);
                throw;
            }
        }

        private string CleanBase64(string base64)
        {
            if (base64.Contains(","))
            {
                return base64.Split(',')[1];
            }
            return base64.Trim();
        }
    }
}
