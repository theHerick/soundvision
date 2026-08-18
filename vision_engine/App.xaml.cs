using System;
using System.IO;
using System.Windows;
using Microsoft.Extensions.Configuration;
using VisionBridge.Models;
using VisionBridge.ViewModels;

namespace VisionBridge
{
    public partial class App : Application
    {
        private MainViewModel? _mainViewModel;

        private async void Application_Startup(object sender, StartupEventArgs e)
        {
            try
            {
                var builder = new ConfigurationBuilder()
                    .SetBasePath(AppDomain.CurrentDomain.BaseDirectory)
                    .AddJsonFile("appsettings.json", optional: false, reloadOnChange: true);

                var configuration = builder.Build();
                var config = new AppConfig();
                configuration.Bind(config);

                _mainViewModel = new MainViewModel(config);

                var mainWindow = new MainWindow
                {
                    DataContext = _mainViewModel
                };

                mainWindow.Show();

                await _mainViewModel.StartServicesAsync();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Falha crítica ao iniciar a aplicação: {ex.Message}", "Erro de Inicialização", MessageBoxButton.OK, MessageBoxImage.Error);
                Shutdown(1);
            }
        }

        private async void Application_Exit(object sender, ExitEventArgs e)
        {
            if (_mainViewModel != null)
            {
                await _mainViewModel.StopServicesAsync();
            }
        }
    }
}
