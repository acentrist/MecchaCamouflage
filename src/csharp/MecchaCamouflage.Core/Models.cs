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
    public double BrushSizeTexels { get; set; } = 5.0;
    public RegionMode FrontRegionMode { get; set; } = RegionMode.Skip;
    public RegionMode SideRegionMode { get; set; } = RegionMode.Paint;
    public RegionMode BackRegionMode { get; set; } = RegionMode.Paint;
    public double Metallic { get; set; } = 0.0;
    public double Roughness { get; set; } = 1.0;
    public double Emissive { get; set; } = 0.0;
    public RgbColor FillColor { get; set; } = RgbColor.White;
    public double FillMetallic { get; set; } = 1.0;
    public double FillRoughness { get; set; } = 0.0;
    public double FillEmissive { get; set; } = 0.0;
    public double ColorCompressionTolerance { get; set; } = 5.0;

    public bool UsesFill =>
        FrontRegionMode == RegionMode.Fill ||
        SideRegionMode == RegionMode.Fill ||
        BackRegionMode == RegionMode.Fill;
}

public sealed class EspSettings
{
    public bool Enabled { get; set; } = true;
    public string TargetScope { get; set; } = "all";
    public bool Boxes { get; set; } = true;
    public bool Skeletons { get; set; } = true;
    public bool Names { get; set; } = true;
    public bool Distance { get; set; } = true;
    public bool Snaplines { get; set; } = true;
    public RgbColor HiderColor { get; set; } = new(0, 255, 136);
    public RgbColor HunterColor { get; set; } = new(255, 0, 0);
}

/// <summary>
/// An editable source image. The browser owns decoding and compositing; the
/// controller only persists the original bytes and normalized placement.
/// </summary>
public sealed class ImagePaintLayer
{
    public const int MaximumSourceBytes = 12 * 1024 * 1024;

    public string AssetId { get; set; } = "";
    public string FileName { get; set; } = "";
    public string MimeType { get; set; } = "";
    public string DataBase64 { get; set; } = "";
    public double CenterX { get; set; } = 0.5;
    public double CenterY { get; set; } = 0.5;
    public double Width { get; set; } = 1.0;
    public double Height { get; set; } = 1.0;
    public double CropX { get; set; } = 0.0;
    public double CropY { get; set; } = 0.0;
    public double CropWidth { get; set; } = 1.0;
    public double CropHeight { get; set; } = 1.0;
    public bool WrapAtlasSeam { get; set; }
    public bool MirrorFrontBack { get; set; }

    public bool TryValidate(out string message)
    {
        message = "";
        if (MimeType is not ("image/png" or "image/jpeg" or "image/webp"))
        {
            message = "Image layers must be PNG, JPEG, or WebP files.";
            return false;
        }
        if (string.IsNullOrWhiteSpace(FileName) || FileName.Length > 260)
        {
            message = "Image layer file names must be present and shorter than 260 characters.";
            return false;
        }
        byte[] bytes;
        try
        {
            bytes = Convert.FromBase64String(DataBase64);
        }
        catch (FormatException)
        {
            message = "Image layer data is not valid Base64.";
            return false;
        }
        if (bytes.Length is < 1 or > MaximumSourceBytes)
        {
            message = "Each image layer must be between 1 byte and 12 MiB.";
            return false;
        }
        if (!double.IsFinite(CenterX) || !double.IsFinite(CenterY) ||
            !double.IsFinite(Width) || !double.IsFinite(Height) ||
            !double.IsFinite(CropX) || !double.IsFinite(CropY) ||
            !double.IsFinite(CropWidth) || !double.IsFinite(CropHeight) ||
            Width <= 0.0 || Height <= 0.0 || CropWidth <= 0.0 || CropHeight <= 0.0)
        {
            message = "Image layer placement is invalid.";
            return false;
        }
        if (CropX < 0.0 || CropY < 0.0 || CropWidth > 1.0 || CropHeight > 1.0 ||
            CropX + CropWidth > 1.000001 || CropY + CropHeight > 1.000001)
        {
            message = "Image layer crop must stay inside the source image.";
            return false;
        }
        return true;
    }
}

/// <summary>
/// Persisted editor state plus the canonical four-face RGBA raster used by
/// native paint. Storing both makes F5 available after a restart without
/// sacrificing editable source layers.
/// </summary>
public sealed class ImagePaintSettings
{
    public const int CanvasWidth = 1024;
    public const int CanvasHeight = 512;
    public const int CanvasByteLength = CanvasWidth * CanvasHeight * 4;
    public const long MaximumTotalSourceBytes = 64L * 1024 * 1024;
    public const int BackgroundPbrCanvasEncodingVersion = 2;

    public static bool IsSupportedBodyType(string? bodyType) =>
        bodyType is "round" or "cube" or "fukuyoka";

    public static string NormalizeBodyType(string? bodyType) =>
        bodyType?.Trim().ToLowerInvariant() switch
        {
            "cube" => "cube",
            "fukuyoka" => "fukuyoka",
            _ => "round"
        };

    public bool Enabled { get; set; }
    public int Revision { get; set; }
    // Version 2 reserves alpha 254 for Background pixels. Earlier imported
    // canvases cannot distinguish Background PBR from opaque layer pixels.
    public int CanvasEncodingVersion { get; set; }
    public string BodyType { get; set; } = "round";
    public string AlphaMode { get; set; } = "skip";
    // Image Paint always draws the imported raster on enabled faces. These
    // modes only choose the base beneath it: Fill first, or leave it skipped.
    public string FrontRegionMode { get; set; } = "skip";
    public string RightRegionMode { get; set; } = "skip";
    public string BackRegionMode { get; set; } = "skip";
    public string LeftRegionMode { get; set; } = "skip";
    // Image Fill belongs to the saved Image design. It intentionally mirrors
    // the normal Paint Fill controls, but does not inherit mutable Paint-tab
    // state when a preset is loaded later.
    public RgbColor FillColor { get; set; } = RgbColor.White;
    public double FillMetallic { get; set; } = 1.0;
    public double FillRoughness { get; set; } = 0.0;
    public double FillEmissive { get; set; } = 0.0;
    // Retained only to deserialize pre-Image-Fill manifests. New designs do
    // not use a separate background material or color.
    public RgbColor BackgroundColor { get; set; } = new(188, 188, 188);
    public string Placement { get; set; } = "fit";
    // Read only during migration of v1 image states. New designs store these
    // choices on every source layer because transforms are image-specific.
    public bool WrapFaces { get; set; }
    public bool MirrorFrontBack { get; set; }
    public double BrushSizeTexels { get; set; } = 5.0;
    public double ColorCompressionTolerance { get; set; } = 0.0;
    public double Metallic { get; set; } = 0.0;
    public double Roughness { get; set; } = 1.0;
    public double Emissive { get; set; } = 0.0;
    public double BackgroundMetallic { get; set; } = 0.0;
    public double BackgroundRoughness { get; set; } = 1.0;
    public double BackgroundEmissive { get; set; } = 0.0;
    public string CanvasRgbaBase64 { get; set; } = "";
    public List<ImagePaintLayer> Layers { get; set; } = [];

    public bool TryValidate(out string message)
    {
        message = "";
        if (!Enabled)
            return true;
        if (Revision < 1)
        {
            message = "Image design revision must be positive.";
            return false;
        }
        if (CanvasEncodingVersion is not (0 or BackgroundPbrCanvasEncodingVersion))
        {
            message = "Image canvas encoding version is invalid.";
            return false;
        }
        if (!IsSupportedBodyType(BodyType))
        {
            message = "Image body type must be round, cube, or fukuyoka.";
            return false;
        }
        if (AlphaMode is not ("skip" or "background"))
        {
            message = "Image transparency must be skip or background.";
            return false;
        }
        if (FrontRegionMode is not ("fill" or "skip") ||
            RightRegionMode is not ("fill" or "skip") ||
            BackRegionMode is not ("fill" or "skip") ||
            LeftRegionMode is not ("fill" or "skip"))
        {
            message = "Image region modes must be fill or skip.";
            return false;
        }
        if (Layers.Count < 1)
        {
            message = "Image paint needs at least one layer.";
            return false;
        }
        long totalSourceBytes = 0;
        foreach (var layer in Layers)
        {
            if (!layer.TryValidate(out message))
                return false;
            totalSourceBytes += Convert.FromBase64String(layer.DataBase64).LongLength;
            if (totalSourceBytes > MaximumTotalSourceBytes)
            {
                message = "Image layers must total no more than 64 MiB.";
                return false;
            }
        }
        byte[] raster;
        try
        {
            raster = Convert.FromBase64String(CanvasRgbaBase64);
        }
        catch (FormatException)
        {
            message = "Image canvas data is not valid Base64.";
            return false;
        }
        if (raster.Length != CanvasByteLength)
        {
            message = "Image canvas must be 1024 by 512 RGBA pixels.";
            return false;
        }
        if (!double.IsFinite(BrushSizeTexels) || BrushSizeTexels is < 1.0 or > 10.0 ||
            !double.IsFinite(ColorCompressionTolerance) || ColorCompressionTolerance is < 0.0 or > 10.0 ||
            !double.IsFinite(Metallic) || !double.IsFinite(Roughness) || !double.IsFinite(Emissive) ||
            !double.IsFinite(FillMetallic) || !double.IsFinite(FillRoughness) || !double.IsFinite(FillEmissive) ||
            !double.IsFinite(BackgroundMetallic) || !double.IsFinite(BackgroundRoughness) || !double.IsFinite(BackgroundEmissive) ||
            Metallic is < 0.0 or > 1.0 || Roughness is < 0.0 or > 1.0 || Emissive is < 0.0 or > 1.0 ||
            FillMetallic is < 0.0 or > 1.0 || FillRoughness is < 0.0 or > 1.0 || FillEmissive is < 0.0 or > 1.0 ||
            BackgroundMetallic is < 0.0 or > 1.0 || BackgroundRoughness is < 0.0 or > 1.0 || BackgroundEmissive is < 0.0 or > 1.0)
        {
            message = "Image brush size or material values are invalid.";
            return false;
        }
        return true;
    }

    public void ClampDraft()
    {
        Revision = Math.Max(0, Revision);
        CanvasEncodingVersion = CanvasEncodingVersion == BackgroundPbrCanvasEncodingVersion
            ? BackgroundPbrCanvasEncodingVersion
            : 0;
        BodyType = NormalizeBodyType(BodyType);
        AlphaMode = AlphaMode is "background" ? "background" : "skip";
        FrontRegionMode = FrontRegionMode is "skip" ? "skip" : "fill";
        RightRegionMode = RightRegionMode is "skip" ? "skip" : "fill";
        BackRegionMode = BackRegionMode is "skip" ? "skip" : "fill";
        LeftRegionMode = LeftRegionMode is "skip" ? "skip" : "fill";
        BrushSizeTexels = Math.Clamp(BrushSizeTexels, 1.0, 10.0);
        ColorCompressionTolerance = Math.Clamp(ColorCompressionTolerance, 0.0, 10.0);
        Metallic = Math.Clamp(Metallic, 0.0, 1.0);
        Roughness = Math.Clamp(Roughness, 0.0, 1.0);
        Emissive = Math.Clamp(Emissive, 0.0, 1.0);
        FillMetallic = Math.Clamp(FillMetallic, 0.0, 1.0);
        FillRoughness = Math.Clamp(FillRoughness, 0.0, 1.0);
        FillEmissive = Math.Clamp(FillEmissive, 0.0, 1.0);
        BackgroundMetallic = Math.Clamp(BackgroundMetallic, 0.0, 1.0);
        BackgroundRoughness = Math.Clamp(BackgroundRoughness, 0.0, 1.0);
        BackgroundEmissive = Math.Clamp(BackgroundEmissive, 0.0, 1.0);
        Layers ??= [];
        MigrateLegacyLayerTransforms();
    }

    /// <summary>
    /// Converts the retired design-wide Wrap/Mirror fields to the equivalent
    /// per-layer state. This is intentionally idempotent so it is safe for
    /// config, active-state, and preset loading paths to call it.
    /// </summary>
    public void MigrateLegacyLayerTransforms()
    {
        Layers ??= [];
        if (!WrapFaces && !MirrorFrontBack)
            return;

        foreach (var layer in Layers)
        {
            layer.WrapAtlasSeam |= WrapFaces;
            layer.MirrorFrontBack |= MirrorFrontBack;
        }
        WrapFaces = false;
        MirrorFrontBack = false;
    }
}

public sealed class AppSettings
{
    public const int CurrentLayoutVersion = 46;
    public int LayoutVersion { get; set; } = CurrentLayoutVersion;
    public double PanelX { get; set; } = -1.0;
    public double PanelY { get; set; } = -1.0;
    public double PanelWidth { get; set; } = 960.0;
    public double PanelHeight { get; set; } = 640.0;
    public string Language { get; set; } = "";
    public int LogRetentionDays { get; set; } = 14;
    public string GameProcessName { get; set; } = "PenguinHotel-Win64-Shipping.exe";
    public bool AlwaysOnTop { get; set; } = true;
    public double Opacity { get; set; } = 0.99;
    public RgbColor ThemeColor { get; set; } = RgbColor.White;
    public string StartHotkey { get; set; } = "F1";
    public string PreviewHotkey { get; set; } = "F2";
    public string UnPreviewHotkey { get; set; } = "F3";
    public string StopHotkey { get; set; } = "F4";
    public string ImageStartHotkey { get; set; } = "F5";
    public string ImagePreviewHotkey { get; set; } = "F6";
    public string ImageUnPreviewHotkey { get; set; } = "F7";
    public string ImageStopHotkey { get; set; } = "F8";
    /// <summary>Version-local reference to the armed design in image-designs.</summary>
    public string ActiveImageDesignId { get; set; } = "";
    public PaintSettings Paint { get; set; } = new();
    public ImagePaintSettings Image { get; set; } = new();
    public EspSettings Esp { get; set; } = new();
}
