namespace MecchaCamouflage.Core;

public enum RegionMode
{
    Paint,
    Fill,
    Skip
}

// Accuracy presets for the mesh-first paint pipeline. Ordered from the fastest,
// lowest-fidelity pass to the most accurate. The native bridge consumes the
// resolved per-field values (not the preset name) so behavior stays explicit.
public enum PaintQualityPreset
{
    Fast,
    Balanced,
    High,
    Ultra
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
    public int PackedBatchDelayMs { get; set; } = 20;
    public double SideSourceMaxUv { get; set; } = 0.08;
    public double FrontBackSourceMaxUv { get; set; } = 0.45;
    public RegionMode FrontRegionMode { get; set; } = RegionMode.Fill;
    public RegionMode SideRegionMode { get; set; } = RegionMode.Paint;
    public RegionMode BackRegionMode { get; set; } = RegionMode.Paint;
    public bool AutoMaterial { get; set; } = false;
    public double Metallic { get; set; } = 0.0;
    public double Roughness { get; set; } = 1.0;
    public RgbColor FillColor { get; set; } = RgbColor.White;
    public double FillMetallic { get; set; } = 1.0;
    public double FillRoughness { get; set; } = 0.0;

    // --- Accuracy / quality enhancement (v2) ---------------------------------
    // These drive the native color-capture and brush-shaping algorithms. Each
    // field maps to an explicit native tuning key. The defaults below match the
    // High preset so a fresh install is internally consistent. Older payloads
    // that omit these keys fall back to legacy behavior on the native side.
    public PaintQualityPreset QualityPreset { get; set; } = PaintQualityPreset.High;
    // Bilinear color capture instead of nearest-texel sampling -> smoother, more
    // accurate corak/pattern edges when projecting the reference texture.
    public bool BilinearColorSampling { get; set; } = true;
    // Higher-order bicubic (Catmull-Rom) color resampling. Strictly sharper and
    // more accurate than bilinear for fine corak/pattern edges. Preset-driven:
    // enabled only at Ultra so it is an opt-in accuracy tier, not a default cost.
    public bool BicubicColorSampling { get; set; } = false;
    // Ordered (Bayer) dithering strength in [0,1], expressed as a fraction of one
    // 8-bit quantization step. Breaks up gradient banding on the packed route.
    public double DitherStrength { get; set; } = 0.5;
    // Lower clamp for per-stroke roughness in [0,1]. Prevents mirror-like
    // artifacts when the sampled source is near-zero roughness.
    public double MinRoughness { get; set; } = 0.08;
    // Brush edge hardness as a percentage in [0,100]. 100 = crisp lines.
    public int FalloffHardnessPct { get; set; } = 85;
    // Coverage sampling density multiplier in [1,3]. Higher = denser plan
    // samples = finer detail, at the cost of more replicated strokes.
    public int CoverageSupersample { get; set; } = 2;
    // Edge-adaptive unsharp reconstruction. The filter is applied in linear
    // light and gated by local luminance contrast, so flat gradients remain
    // stable while fine pattern boundaries regain captured micro-contrast.
    public bool EdgeAwareSharpening { get; set; } = true;
    public double SharpenStrength { get; set; } = 0.18;

    // Independent detection-visualization debug controls (not part of quality
    // presets). When enabled, the native bridge writes UV color/region maps and
    // a screen projection that visualize detected corak/elemen/garis so the user
    // can confirm mapping exactness. DetectionDetail (1..5) scales the UV map
    // resolution and tightens per-sample marks; higher = more exact detail.
    public bool EnableDetectionArtifacts { get; set; } = false;
    public int DetectionDetail { get; set; } = 4;
    // Safe bypass: skips only live camera color capture when a validated local
    // bitmap is selected. Process, loader, planner, and replication checks stay active.
    public bool UseLocalImageSource { get; set; } = false;
    public string LocalImagePath { get; set; } = "";
    public bool BypassLiveCapture { get; set; } = true;

    // Density used by the native planner. Detection detail now changes the
    // actual sampling plan as well as optional diagnostic maps.
    public double DetectionDensity => DetectionDetail switch
    {
        <= 1 => 0.85,
        2 => 1.00,
        3 => 1.15,
        4 => 1.35,
        _ => 1.60
    };

    public double EffectivePlannerDensity => Math.Max(CoverageSupersample, DetectionDensity);
    public double EffectiveCoverageStepTexels => Math.Max(1.0, CoverageStepTexels / EffectivePlannerDensity);

    // Resolves every accuracy field from a preset using a monotonic gradient so
    // "higher preset" always means "more accurate / denser", never a regression.
    public void ApplyQualityPreset(PaintQualityPreset preset)
    {
        QualityPreset = preset;
        switch (preset)
        {
            case PaintQualityPreset.Fast:
                StrokeSizeTexels = 8.0;
                CoverageStepTexels = 8.0;
                BilinearColorSampling = false;
                BicubicColorSampling = false;
                DitherStrength = 0.0;
                MinRoughness = 0.0;
                FalloffHardnessPct = 100;
                CoverageSupersample = 1;
                DetectionDetail = 1;
                EdgeAwareSharpening = false;
                SharpenStrength = 0.0;
                PackedBatchDelayMs = 75;
                break;
            case PaintQualityPreset.Balanced:
                StrokeSizeTexels = 6.0;
                CoverageStepTexels = 6.0;
                BilinearColorSampling = true;
                BicubicColorSampling = false;
                DitherStrength = 0.35;
                MinRoughness = 0.05;
                FalloffHardnessPct = 90;
                CoverageSupersample = 1;
                DetectionDetail = 2;
                EdgeAwareSharpening = false;
                SharpenStrength = 0.0;
                PackedBatchDelayMs = 50;
                break;
            case PaintQualityPreset.High:
                StrokeSizeTexels = 5.0;
                CoverageStepTexels = 5.0;
                BilinearColorSampling = true;
                BicubicColorSampling = false;
                DitherStrength = 0.5;
                MinRoughness = 0.08;
                FalloffHardnessPct = 85;
                CoverageSupersample = 2;
                DetectionDetail = 4;
                EdgeAwareSharpening = true;
                SharpenStrength = 0.18;
                PackedBatchDelayMs = 20;
                break;
            case PaintQualityPreset.Ultra:
                StrokeSizeTexels = 4.0;
                CoverageStepTexels = 4.0;
                BilinearColorSampling = true;
                BicubicColorSampling = true;
                DitherStrength = 0.6;
                MinRoughness = 0.1;
                FalloffHardnessPct = 82;
                CoverageSupersample = 3;
                DetectionDetail = 5;
                EdgeAwareSharpening = true;
                SharpenStrength = 0.42;
                PackedBatchDelayMs = 1;
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(preset), preset, "Unknown paint quality preset.");
        }
    }

    public bool UsesFill =>
        FrontRegionMode == RegionMode.Fill ||
        SideRegionMode == RegionMode.Fill ||
        BackRegionMode == RegionMode.Fill;
}

public sealed class AppSettings
{
    public const int CurrentLayoutVersion = 35;
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
