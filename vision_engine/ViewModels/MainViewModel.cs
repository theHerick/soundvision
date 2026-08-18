using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using VisionBridge.Models;
using VisionBridge.Services;

namespace VisionBridge.ViewModels
{
    public class RelayCommand : ICommand
    {
        private readonly Action<object?> _execute;
        private readonly Func<object?, bool>? _canExecute;

        public RelayCommand(Action<object?> execute, Func<object?, bool>? canExecute = null)
        {
            _execute = execute ?? throw new ArgumentNullException(nameof(execute));
            _canExecute = canExecute;
        }

        public bool CanExecute(object? parameter) => _canExecute?.Invoke(parameter) ?? true;
        public void Execute(object? parameter) => _execute(parameter);
        public event EventHandler? CanExecuteChanged
        {
            add => CommandManager.RequerySuggested += value;
            remove => CommandManager.RequerySuggested -= value;
        }
    }

    public class MainViewModel : INotifyPropertyChanged
    {
        private readonly AppConfig _config;
        private FirebaseService _firebaseService;
        private FirebaseStreamService _streamService;
        private readonly GoogleLensService _lensService;
        private readonly ColorAnalyzerService _colorAnalyzer;
        private readonly VisionProcessor _processor;
        private readonly HttpClient _pingHttpClient = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        private CancellationTokenSource? _cts;

        private bool _isFirebaseOnline;
        public bool IsFirebaseOnline
        {
            get => _isFirebaseOnline;
            set { _isFirebaseOnline = value; OnPropertyChanged(); }
        }

        private string _firebaseStatusDetail = "Checando...";
        public string FirebaseStatusDetail
        {
            get => _firebaseStatusDetail;
            set { _firebaseStatusDetail = value; OnPropertyChanged(); }
        }

        private bool _isSseConnected;
        public bool IsSseConnected
        {
            get => _isSseConnected;
            set { _isSseConnected = value; OnPropertyChanged(); }
        }

        private string _sseStatusDetail = "Desconectado";
        public string SseStatusDetail
        {
            get => _sseStatusDetail;
            set { _sseStatusDetail = value; OnPropertyChanged(); }
        }

        private bool _isLensReady;
        public bool IsLensReady
        {
            get => _isLensReady;
            set { _isLensReady = value; OnPropertyChanged(); }
        }

        private string _lensStatusDetail = "Iniciando Chromium...";
        public string LensStatusDetail
        {
            get => _lensStatusDetail;
            set { _lensStatusDetail = value; OnPropertyChanged(); }
        }

        private string _statusText = "Inicializando LensLocalAPI...";
        public string StatusText
        {
            get => _statusText;
            set { _statusText = value; OnPropertyChanged(); }
        }

        private bool _isSettingsOpen;
        public bool IsSettingsOpen
        {
            get => _isSettingsOpen;
            set { _isSettingsOpen = value; OnPropertyChanged(); }
        }

        private string _configFirebaseUrl = string.Empty;
        public string ConfigFirebaseUrl
        {
            get => _configFirebaseUrl;
            set { _configFirebaseUrl = value; OnPropertyChanged(); }
        }

        private string _configAuthToken = string.Empty;
        public string ConfigAuthToken
        {
            get => _configAuthToken;
            set { _configAuthToken = value; OnPropertyChanged(); }
        }

        private string _configRequestsPath = "requests";
        public string ConfigRequestsPath
        {
            get => _configRequestsPath;
            set { _configRequestsPath = value; OnPropertyChanged(); }
        }

        private string _configQueuePath = "queue";
        public string ConfigQueuePath
        {
            get => _configQueuePath;
            set { _configQueuePath = value; OnPropertyChanged(); }
        }

        private BitmapSource? _lastImageSource;
        public BitmapSource? LastImageSource
        {
            get => _lastImageSource;
            set { _lastImageSource = value; OnPropertyChanged(); }
        }

        private string _lastImagePath = string.Empty;
        public string LastImagePath
        {
            get => _lastImagePath;
            set { _lastImagePath = value; OnPropertyChanged(); }
        }

        private string _lastObject = "Aguardando imagem...";
        public string LastObject
        {
            get => _lastObject;
            set { _lastObject = value; OnPropertyChanged(); }
        }

        private string _lastColor = "—";
        public string LastColor
        {
            get => _lastColor;
            set { _lastColor = value; OnPropertyChanged(); }
        }

        private string _lastDescription = "Nenhuma imagem analisada até o momento. Envie via ESP32 ou clique em 'TESTAR IMAGEM LOCAL'.";
        public string LastDescription
        {
            get => _lastDescription;
            set { _lastDescription = value; OnPropertyChanged(); }
        }

        private string _lastRawResult = string.Empty;
        public string LastRawResult
        {
            get => _lastRawResult;
            set { _lastRawResult = value; OnPropertyChanged(); }
        }

        private string _lastRequestId = "—";
        public string LastRequestId
        {
            get => _lastRequestId;
            set { _lastRequestId = value; OnPropertyChanged(); }
        }

        private string _lastDeviceId = "esp32cam_01";
        public string LastDeviceId
        {
            get => _lastDeviceId;
            set { _lastDeviceId = value; OnPropertyChanged(); }
        }

        private string _lastTimestamp = "—";
        public string LastTimestamp
        {
            get => _lastTimestamp;
            set { _lastTimestamp = value; OnPropertyChanged(); }
        }

        private string _lastImageSize = "0 KB";
        public string LastImageSize
        {
            get => _lastImageSize;
            set { _lastImageSize = value; OnPropertyChanged(); }
        }

        private bool _debugModeEnabled;
        public bool DebugModeEnabled
        {
            get => _debugModeEnabled;
            set
            {
                _debugModeEnabled = value;
                _config.Application.DebugMode = value;
                OnPropertyChanged();
                LoggingService.Instance.LogInfo($"Modo Debug {(value ? "habilitado" : "desabilitado")}.");
            }
        }

        public ObservableCollection<LogEntry> Logs { get; } = new ObservableCollection<LogEntry>();

        public ICommand TestLocalImageCommand { get; }
        public ICommand CheckConnectionsCommand { get; }
        public ICommand ClearLogsCommand { get; }
        public ICommand ToggleSettingsCommand { get; }
        public ICommand SaveSettingsCommand { get; }

        public MainViewModel(AppConfig config)
        {
            _config = config;
            _debugModeEnabled = config.Application.DebugMode;

            ConfigFirebaseUrl = config.Firebase.DatabaseUrl;
            ConfigAuthToken = config.Firebase.AuthToken;
            ConfigRequestsPath = config.Firebase.RequestsPath;
            ConfigQueuePath = config.Firebase.QueuePath;

            _firebaseService = new FirebaseService(config.Firebase);
            _streamService = new FirebaseStreamService(config.Firebase);
            _lensService = new GoogleLensService(config.Browser, config.Application);
            _colorAnalyzer = new ColorAnalyzerService();
            _processor = new VisionProcessor(_firebaseService, _lensService, _colorAnalyzer, config.Application);

            TestLocalImageCommand = new RelayCommand(async _ => await ExecuteTestLocalImageAsync());
            CheckConnectionsCommand = new RelayCommand(async _ => await CheckAllConnectionsRealtimeAsync());
            ClearLogsCommand = new RelayCommand(_ => Application.Current.Dispatcher.Invoke(() => Logs.Clear()));
            ToggleSettingsCommand = new RelayCommand(_ => IsSettingsOpen = !IsSettingsOpen);
            SaveSettingsCommand = new RelayCommand(async _ => await SaveSettingsAsync());

            LoggingService.Instance.LogReceived += OnLogReceived;
            _streamService.ConnectionStatusChanged += status =>
            {
                IsSseConnected = status;
                SseStatusDetail = status ? "Conectado" : "Desconectado";
            };

            _lensService.BrowserStatusChanged += isReady =>
            {
                IsLensReady = isReady;
                LensStatusDetail = isReady ? "Pronto" : "Indisponível";
            };

            _streamService.NewRequestReceived += reqId => _processor.EnqueueRequest(reqId);

            _processor.ProcessingStarted += reqId =>
            {
                StatusText = $"Processando requisição {reqId}...";
                LastRequestId = reqId;
            };

            _processor.ProcessingCompleted += (reqId, result, imagePath, deviceId) =>
            {
                Application.Current.Dispatcher.Invoke(() =>
                {
                    LastObject = result.Object;
                    LastColor = result.Color;
                    LastDescription = result.Description;
                    LastRawResult = result.RawResult;
                    LastRequestId = reqId;
                    LastDeviceId = deviceId;
                    LastTimestamp = DateTime.Now.ToString("HH:mm:ss");
                    StatusText = "Concluído. Aguardando nova imagem...";

                    if (File.Exists(imagePath))
                    {
                        var fi = new FileInfo(imagePath);
                        LastImageSize = $"{fi.Length / 1024} KB";
                        SetImagePreviewFromPath(imagePath);
                    }
                });
            };
        }

        private void SetImagePreviewFromPath(string path)
        {
            try
            {
                if (File.Exists(path))
                {
                    var bitmap = new BitmapImage();
                    bitmap.BeginInit();
                    bitmap.CacheOption = BitmapCacheOption.OnLoad;
                    bitmap.UriSource = new Uri(path, UriKind.Absolute);
                    bitmap.EndInit();
                    bitmap.Freeze();

                    LastImageSource = bitmap;
                    LastImagePath = path;
                }
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogWarn($"Erro ao renderizar preview da imagem: {ex.Message}");
            }
        }

        public async Task StartServicesAsync()
        {
            _cts = new CancellationTokenSource();
            LoggingService.Instance.LogInfo("Iniciando serviços do LensLocalAPI...");

            StatusText = "Iniciando navegador Chromium...";
            try
            {
                await _lensService.InitializeBrowserAsync();
                IsLensReady = true;
                LensStatusDetail = "Pronto";
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Erro ao iniciar navegador: {ex.Message}");
                StatusText = "Erro ao iniciar Playwright Browser.";
                LensStatusDetail = "Erro";
            }

            _ = Task.Run(() => _processor.StartProcessingLoopAsync(_cts.Token), _cts.Token);

            if (!string.IsNullOrWhiteSpace(_config.Firebase.DatabaseUrl) && !_config.Firebase.DatabaseUrl.Contains("YOUR_FIREBASE"))
            {
                StatusText = "Conectando ao Firebase SSE Stream...";
                _ = Task.Run(() => _streamService.StartListeningAsync(_cts.Token), _cts.Token);
            }
            else
            {
                LoggingService.Instance.LogWarn("Firebase URL não configurada. Preencha o seu Firebase nas configurações.");
                StatusText = "Aguardando configuração do Firebase.";
                FirebaseStatusDetail = "Não configurado";
            }

            _ = Task.Run(() => StartPeriodicHealthCheckLoopAsync(_cts.Token), _cts.Token);
        }

        public async Task SaveSettingsAsync()
        {
            try
            {
                LoggingService.Instance.LogInfo("Salvando configurações do Firebase...");

                _config.Firebase.DatabaseUrl = ConfigFirebaseUrl.Trim();
                _config.Firebase.AuthToken = ConfigAuthToken.Trim();
                _config.Firebase.RequestsPath = string.IsNullOrWhiteSpace(ConfigRequestsPath) ? "requests" : ConfigRequestsPath.Trim();
                _config.Firebase.QueuePath = string.IsNullOrWhiteSpace(ConfigQueuePath) ? "queue" : ConfigQueuePath.Trim();

                var configPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "appsettings.json");
                var jsonOptions = new JsonSerializerOptions { WriteIndented = true };
                
                var jsonObject = new
                {
                    Firebase = _config.Firebase,
                    Browser = _config.Browser,
                    Application = _config.Application
                };

                var updatedJson = JsonSerializer.Serialize(jsonObject, jsonOptions);
                await File.WriteAllTextAsync(configPath, updatedJson);

                LoggingService.Instance.LogSuccess("Configurações salvas no appsettings.json com sucesso!");
                IsSettingsOpen = false;

                _firebaseService = new FirebaseService(_config.Firebase);
                _streamService = new FirebaseStreamService(_config.Firebase);

                await CheckAllConnectionsRealtimeAsync();
                _ = Task.Run(() => _streamService.StartListeningAsync(_cts?.Token ?? CancellationToken.None));
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Erro ao salvar configurações: {ex.Message}");
            }
        }

        public async Task CheckAllConnectionsRealtimeAsync()
        {
            await PingFirebaseAsync();
        }

        private async Task StartPeriodicHealthCheckLoopAsync(CancellationToken cancellationToken)
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await PingFirebaseAsync();
                await Task.Delay(10000, cancellationToken);
            }
        }

        private async Task PingFirebaseAsync()
        {
            if (string.IsNullOrWhiteSpace(_config.Firebase.DatabaseUrl) || _config.Firebase.DatabaseUrl.Contains("YOUR_FIREBASE"))
            {
                IsFirebaseOnline = false;
                FirebaseStatusDetail = "Não configurado";
                return;
            }

            try
            {
                var sw = Stopwatch.StartNew();
                var url = $"{_config.Firebase.DatabaseUrl.TrimEnd('/')}/.json?shallow=true";
                if (!string.IsNullOrWhiteSpace(_config.Firebase.AuthToken))
                {
                    url += $"&auth={_config.Firebase.AuthToken}";
                }

                var response = await _pingHttpClient.GetAsync(url);
                sw.Stop();

                if (response.IsSuccessStatusCode)
                {
                    IsFirebaseOnline = true;
                    FirebaseStatusDetail = $"Online ({sw.ElapsedMilliseconds} ms)";
                }
                else
                {
                    IsFirebaseOnline = false;
                    FirebaseStatusDetail = $"HTTP {(int)response.StatusCode}";
                }
            }
            catch (Exception ex)
            {
                IsFirebaseOnline = false;
                FirebaseStatusDetail = "Off-line";
            }
        }

        public async Task StopServicesAsync()
        {
            _cts?.Cancel();
            await _lensService.DisposeAsync();
        }

        private async Task ExecuteTestLocalImageAsync()
        {
            var dialog = new OpenFileDialog
            {
                Filter = "Imagens (*.jpg;*.jpeg;*.png)|*.jpg;*.jpeg;*.png|Todos os arquivos (*.*)|*.*",
                Title = "Selecione uma imagem para testar com o Google Lens"
            };

            if (dialog.ShowDialog() == true)
            {
                StatusText = "Processando imagem local selecionada...";
                SetImagePreviewFromPath(dialog.FileName);

                var fi = new FileInfo(dialog.FileName);
                LastImageSize = $"{fi.Length / 1024} KB";

                try
                {
                    await _processor.ProcessLocalImageAsync(dialog.FileName);
                }
                catch (Exception ex)
                {
                    LoggingService.Instance.LogError($"Falha no teste local: {ex.Message}");
                    StatusText = "Erro durante teste local.";
                }
            }
        }

        private void OnLogReceived(LogEntry entry)
        {
            Application.Current?.Dispatcher.BeginInvoke(new Action(() =>
            {
                Logs.Add(entry);
                if (Logs.Count > _config.Application.HistoryLimit)
                {
                    Logs.RemoveAt(0);
                }
            }));
        }

        public event PropertyChangedEventHandler? PropertyChanged;
        protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
