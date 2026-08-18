using System.Threading;
using System.Threading.Tasks;
using VisionBridge.Models;

namespace VisionBridge.Vision
{
    public interface IVisionRecognitionService
    {
        Task<VisionResult> AnalyzeImageAsync(string imagePath, CancellationToken cancellationToken = default);
    }
}
