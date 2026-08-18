using System;

namespace VisionBridge.Models
{
    public class LogEntry
    {
        public DateTime Timestamp { get; set; } = DateTime.Now;
        public string Level { get; set; } = "INFO";
        public string Message { get; set; } = string.Empty;

        public override string ToString()
        {
            return $"[{Timestamp:HH:mm:ss}] [{Level}] {Message}";
        }
    }
}
