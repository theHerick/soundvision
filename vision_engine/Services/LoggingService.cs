using System;
using VisionBridge.Models;

namespace VisionBridge.Services
{
    public class LoggingService
    {
        private static readonly Lazy<LoggingService> _instance = new Lazy<LoggingService>(() => new LoggingService());
        public static LoggingService Instance => _instance.Value;

        public event Action<LogEntry>? LogReceived;

        private LoggingService() { }

        public void LogInfo(string message) => Log("INFO", message);
        public void LogSuccess(string message) => Log("SUCCESS", message);
        public void LogWarn(string message) => Log("WARN", message);
        public void LogError(string message) => Log("ERROR", message);

        public void Log(string level, string message)
        {
            var entry = new LogEntry
            {
                Timestamp = DateTime.Now,
                Level = level,
                Message = message
            };

            LogReceived?.Invoke(entry);
        }
    }
}
