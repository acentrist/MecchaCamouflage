namespace MecchaCamouflage.Core;

public enum RegionMode
{
    Paint,
    Fill,
    Skip
}

public sealed record RgbColor(byte R, byte G, byte B)
{
    public static RgbColor White { get; } = new(255, 255, 255);

    public string ToHex() => $"#{R:X2}{G:X2}{B:X2}";

    public static bool TryParse(string? text, out RgbColor color)
    {
        color = White;
        if (string.IsNullOrWhiteSpace(text))
            return false;

        var value = text.Trim();
        if (value.StartsWith("#", StringComparison.Ordinal))
            value = value[1..];
        if (value.Length != 6)
            return false;
        if (!byte.TryParse(value[..2], System.Globalization.NumberStyles.HexNumber, null, out var r))
            return false;
        if (!byte.TryParse(value.Substring(2, 2), System.Globalization.NumberStyles.HexNumber, null, out var g))
            return false;
        if (!byte.TryParse(value.Substring(4, 2), System.Globalization.NumberStyles.HexNumber, null, out var b))
            return false;
        color = new RgbColor(r, g, b);
        return true;
    }
}

public sealed class PaintSettings
{
    public double StrokeSizeTexels { get; set; } = 5.0;
    public double CoverageStepTexels { get; set; } = 5.0;
    public int PackedBatchDelayMs { get; set; } = 75;
    public double SideSourceMaxUv { get; set; } = 0.08;
    public double FrontBackSourceMaxUv { get; set; } = 0.45;
    public RegionMode FrontRegionMode { get; set; } = RegionMode.Paint;
    public RegionMode SideRegionMode { get; set; } = RegionMode.Paint;
    public RegionMode BackRegionMode { get; set; } = RegionMode.Paint;
    public bool AutoMaterial { get; set; } = false;
    public double Metallic { get; set; } = 0.0;
    public double Roughness { get; set; } = 1.0;
    public RgbColor FillColor { get; set; } = RgbColor.White;
    public double FillMetallic { get; set; } = 1.0;
    public double FillRoughness { get; set; } = 0.0;

    public bool UsesFill =>
        FrontRegionMode == RegionMode.Fill ||
        SideRegionMode == RegionMode.Fill ||
        BackRegionMode == RegionMode.Fill;
}

public sealed class AppSettings
{
    public const int CurrentLayoutVersion = 34;
    public int LayoutVersion { get; set; } = CurrentLayoutVersion;
    public double PanelX { get; set; } = -1.0;
    public double PanelY { get; set; } = -1.0;
    public double PanelWidth { get; set; } = 1100.0;
    public double PanelHeight { get; set; } = 720.0;
    public string Language { get; set; } = "";
    public int LogRetentionDays { get; set; } = 14;
    public string GameProcessName { get; set; } = "PenguinHotel-Win64-Shipping.exe";
    public bool AlwaysOnTop { get; set; } = true;
    public double Opacity { get; set; } = 1.0;
    public RgbColor ThemeColor { get; set; } = RgbColor.White;
    public string StartHotkey { get; set; } = "F1";
    public string PreviewHotkey { get; set; } = "F2";
    public string UnPreviewHotkey { get; set; } = "F3";
    public string StopHotkey { get; set; } = "F4";
    public PaintSettings Paint { get; set; } = new();
}
