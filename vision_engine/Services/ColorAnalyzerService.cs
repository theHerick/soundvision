using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;

namespace VisionBridge.Services
{
    public class ColorAnalyzerService
    {
        public string IdentifyDominantColor(string imagePath)
        {
            if (!File.Exists(imagePath)) return "Desconhecido";

            try
            {
                using var bitmap = new Bitmap(imagePath);

                long rTotal = 0, gTotal = 0, bTotal = 0;
                int sampledPixels = 0;

                int stepX = Math.Max(1, bitmap.Width / 60);
                int stepY = Math.Max(1, bitmap.Height / 60);

                int startX = bitmap.Width / 4;
                int endX = (bitmap.Width * 3) / 4;
                int startY = bitmap.Height / 4;
                int endY = (bitmap.Height * 3) / 4;

                for (int x = startX; x < endX; x += stepX)
                {
                    for (int y = startY; y < endY; y += stepY)
                    {
                        Color pixel = bitmap.GetPixel(x, y);
                        rTotal += pixel.R;
                        gTotal += pixel.G;
                        bTotal += pixel.B;
                        sampledPixels++;
                    }
                }

                if (sampledPixels == 0) return "Desconhecido";

                int avgR = (int)(rTotal / sampledPixels);
                int avgG = (int)(gTotal / sampledPixels);
                int avgB = (int)(bTotal / sampledPixels);

                return ClassifyRgbToName(avgR, avgG, avgB);
            }
            catch (Exception ex)
            {
                LoggingService.Instance.LogWarn($"Erro na análise de cor local: {ex.Message}");
                return "Indefinido";
            }
        }

        private string ClassifyRgbToName(int r, int g, int b)
        {
            Color.FromArgb(r, g, b).GetHue();
            float sat = Color.FromArgb(r, g, b).GetSaturation();
            float val = Math.Max(r, Math.Max(g, b)) / 255f;

            if (val < 0.18f) return "Preto";
            if (val > 0.85f && sat < 0.12f) return "Branco";
            if (sat < 0.15f) return "Cinza";

            if (r > 180 && g > 180 && b < 100) return "Amarelo";
            if (r > 200 && g > 100 && b < 50) return "Laranja";
            if (r > 150 && b > 150 && g < 100) return "Roxo";
            if (g > r && g > b) return "Verde";
            if (b > r && b > g) return "Azul";
            if (r > g && r > b) return "Vermelho";

            return "Colorido";
        }
    }
}
