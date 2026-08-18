using System.Text.Json.Serialization;

namespace VisionBridge.Models
{
    public class VisionResult
    {
        [JsonPropertyName("object")]
        public string Object { get; set; } = string.Empty;

        [JsonPropertyName("color")]
        public string Color { get; set; } = string.Empty;

        [JsonPropertyName("description")]
        public string Description { get; set; } = string.Empty;

        [JsonPropertyName("rawResult")]
        public string RawResult { get; set; } = string.Empty;
    }
}
