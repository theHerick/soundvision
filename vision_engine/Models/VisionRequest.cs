using System.Text.Json.Serialization;

namespace VisionBridge.Models
{
    public class VisionRequest
    {
        [JsonPropertyName("deviceId")]
        public string DeviceId { get; set; } = "esp32cam_01";

        [JsonPropertyName("status")]
        public string Status { get; set; } = "pending";

        [JsonPropertyName("timestamp")]
        public long Timestamp { get; set; }

        [JsonPropertyName("image")]
        public string ImageBase64 { get; set; } = string.Empty;
    }
}
