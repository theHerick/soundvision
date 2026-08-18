using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Playwright;
using VisionBridge.Models;
using VisionBridge.Vision;

namespace VisionBridge.Services
{
    public class GoogleLensService : IVisionRecognitionService, IAsyncDisposable
    {
        private readonly BrowserConfig _browserConfig;
        private readonly ApplicationConfig _appConfig;
        private IPlaywright? _playwright;
        private IBrowserContext? _context;
        private IPage? _page;
        private readonly SemaphoreSlim _lock = new SemaphoreSlim(1, 1);

        public event Action<bool>? BrowserStatusChanged;

        private bool _isBrowserReady;
        public bool IsBrowserReady
        {
            get => _isBrowserReady;
            private set
            {
                if (_isBrowserReady != value)
                {
                    _isBrowserReady = value;
                    BrowserStatusChanged?.Invoke(value);
                }
            }
        }

        private static readonly HashSet<string> BlacklistedWords = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "google", "lens", "pesquisar", "imagens", "comprar", "traduzir", "texto", "login",
            "configurações", "privacidade", "termos", "ajuda", "feedback", "fazer login",
            "pesquisa por imagem", "encontrar fonte da imagem", "copiar texto", "escutar",
            "pesquisas relacionadas", "resultados visuais", "correspondências exatas",
            "produtos", "visual", "search", "images", "buy", "translate", "text", "privacy", "terms"
        };

        public GoogleLensService(BrowserConfig browserConfig, ApplicationConfig appConfig)
        {
            _browserConfig = browserConfig;
            _appConfig = appConfig;
        }

        public async Task InitializeBrowserAsync()
        {
            await _lock.WaitAsync();
            try
            {
                await EnsureBrowserPageInternalAsync();
            }
            finally
            {
                _lock.Release();
            }
        }

        private async Task EnsureBrowserPageInternalAsync()
        {
            if (_page != null && !_page.IsClosed)
            {
                IsBrowserReady = true;
                return;
            }

            LoggingService.Instance.LogInfo("Iniciando Chromium via Microsoft Playwright...");

            _playwright ??= await Playwright.CreateAsync();

            var profilePath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, _browserConfig.ProfilePath);
            Directory.CreateDirectory(profilePath);

            var options = new BrowserTypeLaunchPersistentContextOptions
            {
                Headless = _browserConfig.Headless,
                ViewportSize = new ViewportSize { Width = 1280, Height = 900 },
                Args = new[]
                {
                    "--no-sandbox",
                    "--disable-setuid-sandbox",
                    "--disable-infobars",
                    "--disable-background-timer-throttling",
                    "--disable-backgrounding-occluded-windows",
                    "--disable-renderer-backgrounding",
                    "--start-maximized"
                },
                Timeout = _browserConfig.TimeoutSeconds * 1000
            };

            _context = await _playwright.Chromium.LaunchPersistentContextAsync(profilePath, options);
            
            _context.Close += (_, _) =>
            {
                IsBrowserReady = false;
                LoggingService.Instance.LogWarn("Navegador foi fechado.");
            };

            var pages = _context.Pages;
            _page = pages.Count > 0 ? pages[0] : await _context.NewPageAsync();

            _page.Close += (_, _) =>
            {
                IsBrowserReady = false;
                LoggingService.Instance.LogWarn("Página do navegador foi fechada.");
            };

            _page.SetDefaultTimeout(_browserConfig.TimeoutSeconds * 1000);

            LoggingService.Instance.LogInfo("Acessando Google Lens...");
            await _page.GotoAsync(_browserConfig.LensUrl, new PageGotoOptions { WaitUntil = WaitUntilState.DOMContentLoaded });
            
            try
            {
                await _page.BringToFrontAsync();
            }
            catch { }

            IsBrowserReady = true;
            LoggingService.Instance.LogSuccess("Google Lens pronto para recebimento de imagens.");
        }

        public async Task<VisionResult> AnalyzeImageAsync(string imagePath, CancellationToken cancellationToken = default)
        {
            if (!File.Exists(imagePath))
            {
                throw new FileNotFoundException($"Arquivo de imagem não encontrado: {imagePath}");
            }

            await _lock.WaitAsync(cancellationToken);
            try
            {
                await EnsureBrowserPageInternalAsync();

                LoggingService.Instance.LogInfo("Navegando para página inicial do Google Lens...");
                await _page!.GotoAsync(_browserConfig.LensUrl, new PageGotoOptions { WaitUntil = WaitUntilState.DOMContentLoaded });
                await Task.Delay(1000, cancellationToken);

                try { await _page.BringToFrontAsync(); } catch { }

                LoggingService.Instance.LogInfo($"Enviando imagem ao Google Lens: {Path.GetFileName(imagePath)}");

                var fileInput = await _page.QuerySelectorAsync("input[type='file']");

                if (fileInput == null)
                {
                    var cameraIcon = await _page.QuerySelectorAsync("div[role='button'][aria-label*='imagem'], button[aria-label*='Search by image']");
                    if (cameraIcon != null)
                    {
                        await cameraIcon.ClickAsync();
                        await Task.Delay(500, cancellationToken);
                    }

                    fileInput = await _page.WaitForSelectorAsync("input[type='file']", new PageWaitForSelectorOptions
                    {
                        Timeout = _browserConfig.TimeoutSeconds * 1000
                    });
                }

                if (fileInput == null)
                {
                    throw new InvalidOperationException("Não foi possível localizar o campo de upload do Google Lens.");
                }

                await fileInput.SetInputFilesAsync(imagePath);
                LoggingService.Instance.LogInfo("Upload da imagem concluído no Google Lens.");

                await Task.Delay(2000, cancellationToken);

                var promptInputSelectors = new[]
                {
                    "textarea[aria-label*='Perguntar']",
                    "textarea[placeholder*='Perguntar']",
                    "textarea[name='q']",
                    "input[name='q']",
                    "textarea",
                    "div[contenteditable='true']"
                };

                IElementHandle? promptInput = null;
                foreach (var sel in promptInputSelectors)
                {
                    promptInput = await _page.QuerySelectorAsync(sel);
                    if (promptInput != null) break;
                }

                if (promptInput != null)
                {
                    var promptText = "O que e isso e qual a cor predominante? Responda estritamente no formato JSON assim: {\"object\": \"nome_do_objeto\", \"color\": \"cor\", \"description\": \"descricao\"}";
                    LoggingService.Instance.LogInfo("Enviando instrução de formato JSON ao Google Search AI...");

                    await promptInput.FocusAsync();
                    await promptInput.FillAsync(promptText);
                    await Task.Delay(400, cancellationToken);
                    await promptInput.PressAsync("Enter");

                    try
                    {
                        await _page.WaitForSelectorAsync("pre, code, div[class*='code']", new PageWaitForSelectorOptions
                        {
                            Timeout = 7000
                        });
                    }
                    catch
                    {
                        await Task.Delay(4000, cancellationToken);
                    }
                }

                var result = await ExtractResultFromPageAsync(_page);

                if (_appConfig.DebugMode && (string.IsNullOrWhiteSpace(result.Object) || result.Object == "Objeto não identificado"))
                {
                    await SaveDebugArtifactsAsync(_page, Path.GetFileNameWithoutExtension(imagePath));
                }

                LoggingService.Instance.LogSuccess($"Resultado obtido: Objeto = '{result.Object}', Cor = '{result.Color}'");
                return result;
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogError($"Falha na análise via Google Lens: {ex.Message}");

                if (_page != null && _appConfig.DebugMode)
                {
                    await SaveDebugArtifactsAsync(_page, $"error_{DateTime.Now:HHmmss}");
                }

                IsBrowserReady = false;
                throw;
            }
            finally
            {
                _lock.Release();
            }
        }

        private async Task<VisionResult> ExtractResultFromPageAsync(IPage page)
        {
            var result = new VisionResult();

            var codeElements = await page.QuerySelectorAllAsync("pre, code, div[class*='code'], pre code");
            foreach (var elem in codeElements)
            {
                var codeText = (await elem.InnerTextAsync())?.Trim();
                if (!string.IsNullOrWhiteSpace(codeText) && codeText.Contains("\"object\""))
                {
                    var jsonMatchInCode = Regex.Match(codeText, @"\{[\s\S]*?\}");
                    if (jsonMatchInCode.Success)
                    {
                        try
                        {
                            using var doc = JsonDocument.Parse(jsonMatchInCode.Value);
                            var root = doc.RootElement;
                            var objStr = root.TryGetProperty("object", out var o) ? o.GetString() : null;
                            var colStr = root.TryGetProperty("color", out var c) ? c.GetString() : null;
                            var descStr = root.TryGetProperty("description", out var d) ? d.GetString() : null;

                            if (!string.IsNullOrWhiteSpace(objStr))
                            {
                                result.Object = objStr;
                                result.Color = colStr ?? "";
                                result.Description = descStr ?? "";
                                result.RawResult = jsonMatchInCode.Value;
                                return result;
                            }
                        }
                        catch { }
                    }
                }
            }

            var bodyText = await page.InnerTextAsync("body");
            var jsonMatch = Regex.Match(bodyText, @"\{[\s\S]*?""object""[\s\S]*?""description""[\s\S]*?\}", RegexOptions.IgnoreCase);
            if (!jsonMatch.Success)
            {
                jsonMatch = Regex.Match(bodyText, @"\{[\s\S]*?""object""[\s\S]*?\}", RegexOptions.IgnoreCase);
            }

            if (jsonMatch.Success)
            {
                try
                {
                    var jsonStr = jsonMatch.Value;
                    using var doc = JsonDocument.Parse(jsonStr);
                    var root = doc.RootElement;

                    if (root.TryGetProperty("object", out var objElem))
                    {
                        result.Object = objElem.GetString() ?? "";
                    }
                    if (root.TryGetProperty("color", out var colorElem))
                    {
                        result.Color = colorElem.GetString() ?? "";
                    }
                    if (root.TryGetProperty("description", out var descElem))
                    {
                        result.Description = descElem.GetString() ?? "";
                    }

                    if (!string.IsNullOrWhiteSpace(result.Object))
                    {
                        result.RawResult = jsonStr;
                        return result;
                    }
                }
                catch { }
            }

            var mainTitleSelectors = new[]
            {
                "div[role='heading'][aria-level='2']",
                "h2.L3n4id",
                "div.VfPpkd-vQ43id",
                "a[data-title]",
                "div[aria-label*='Visual match'] h2"
            };

            foreach (var selector in mainTitleSelectors)
            {
                var elements = await page.QuerySelectorAllAsync(selector);
                foreach (var elem in elements)
                {
                    var text = (await elem.InnerTextAsync())?.Trim();
                    if (IsValidObjectTitle(text))
                    {
                        result.Object = CleanText(text!);
                        result.RawResult = text!;
                        result.Description = $"Identificado via Google Lens: {result.Object}";
                        return result;
                    }
                }
            }

            result.Object = "Objeto não identificado";
            result.RawResult = "Nenhum resultado conclusivo";
            result.Description = "Não foi possível determinar o objeto exato no Google Lens.";
            return result;
        }

        private bool IsValidObjectTitle(string? text)
        {
            if (string.IsNullOrWhiteSpace(text)) return false;
            text = text.Trim();

            if (text.Length < 2 || text.Length > 100) return false;
            if (BlacklistedWords.Contains(text)) return false;

            return true;
        }

        private string CleanText(string text)
        {
            return Regex.Replace(text, @"\s+", " ").Trim();
        }

        private async Task SaveDebugArtifactsAsync(IPage page, string prefix)
        {
            try
            {
                var debugDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, _appConfig.DebugDirectory);
                Directory.CreateDirectory(debugDir);

                var timestamp = DateTime.Now.ToString("yyyyMMdd_HHmmss");
                var screenshotPath = Path.Combine(debugDir, $"{prefix}_{timestamp}.png");
                await page.ScreenshotAsync(new PageScreenshotOptions { Path = screenshotPath, FullPage = true });
            }
            catch { }
        }

        public async ValueTask DisposeAsync()
        {
            if (_context != null)
            {
                await _context.CloseAsync();
                _context = null;
            }

            _playwright?.Dispose();
            _playwright = null;
            _lock.Dispose();
        }
    }
}
