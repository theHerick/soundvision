namespace VisionBridge.Models
{
    public class AppConfig
    {
        public FirebaseConfig Firebase { get; set; } = new FirebaseConfig();
        public BrowserConfig Browser { get; set; } = new BrowserConfig();
        public ApplicationConfig Application { get; set; } = new ApplicationConfig();
    }

    public class FirebaseConfig
    {
        public string DatabaseUrl { get; set; } = string.Empty;
        public string QueuePath { get; set; } = "queue";
        public string RequestsPath { get; set; } = "requests";
        public string AuthToken { get; set; } = string.Empty;
    }

    public class BrowserConfig
    {
        public bool Headless { get; set; } = false;
        public string ProfilePath { get; set; } = "browser-profile";
        public int TimeoutSeconds { get; set; } = 30;
        public string LensUrl { get; set; } = "https://lens.google.com";
    }

    public class ApplicationConfig
    {
        public int MaxImageSizeMB { get; set; } = 5;
        public int HistoryLimit { get; set; } = 50;
        public string TempDirectory { get; set; } = "temp";
        public string DebugDirectory { get; set; } = "debug";
        public bool DebugMode { get; set; } = false;
    }
}
