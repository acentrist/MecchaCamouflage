using System.Text.Json;
using System.Text.Json.Nodes;

namespace MecchaCamouflage.Core;

public sealed class SettingsStore
{
    // v1.6.1 introduced dedicated Fill PBR controls with a mirror-like default.
    // That was especially misleading because Front defaults to Fill while the
    // normal material controls default to a dielectric surface.  Only migrate
    // the exact old defaults; any custom Fill material remains authoritative.
    private const int FillPbrDefaultsFixLayoutVersion = 39;

    private static readonly JsonSerializerOptions Options = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower
    };

    private readonly AppPaths paths;

    public SettingsStore(AppPaths paths)
    {
        this.paths = paths;
    }

    public AppSettings Load()
    {
        if (!File.Exists(paths.ConfigPath))
            return Clamp(new AppSettings());

        var text = File.ReadAllText(paths.ConfigPath);
        var root = JsonNode.Parse(text)?.AsObject();
        if (root is null)
            return Clamp(new AppSettings());

        var settings = new AppSettings();
        // A persisted config without a layout version predates versioned migration.
        settings.LayoutVersion = ReadInt(root, "layout_version", 0);
        settings.PanelX = ReadDouble(root, "panel_x", settings.PanelX);
        settings.PanelY = ReadDouble(root, "panel_y", settings.PanelY);
        settings.PanelWidth = ReadDouble(root, "panel_width", settings.PanelWidth);
        settings.PanelHeight = ReadDouble(root, "panel_height", settings.PanelHeight);
        settings.Language = ReadString(root, "language", settings.Language);
        settings.GameProcessName = ReadString(root, "game_process_name", settings.GameProcessName);
        settings.AlwaysOnTop = ReadBool(root, "always_on_top", settings.AlwaysOnTop);
        settings.Opacity = ReadDouble(root, "opacity", settings.Opacity);
        if (RgbColor.TryParse(ReadString(root, "theme_color", settings.ThemeColor.ToHex()), out var theme))
            settings.ThemeColor = theme;
        settings.StartHotkey = ReadString(root, "start_hotkey", settings.StartHotkey);
        settings.StopHotkey = ReadString(root, "stop_hotkey", settings.StopHotkey);
        settings.PreviewHotkey = ReadString(root, "preview_hotkey", settings.PreviewHotkey);
        settings.UnPreviewHotkey = ReadString(root, "unpreview_hotkey", settings.UnPreviewHotkey);
        settings.ImageStartHotkey = ReadString(root, "image_start_hotkey", settings.ImageStartHotkey);
        settings.ImagePreviewHotkey = ReadString(root, "image_preview_hotkey", settings.ImagePreviewHotkey);
        settings.ImageUnPreviewHotkey = ReadString(root, "image_unpreview_hotkey", settings.ImageUnPreviewHotkey);
        settings.ImageStopHotkey = ReadString(root, "image_stop_hotkey", settings.ImageStopHotkey);
        settings.ActiveImageDesignId = ReadString(root, "active_image_design_id", settings.ActiveImageDesignId);
        settings.LogRetentionDays = ReadInt(root, "log_retention_days", settings.LogRetentionDays);

        if (root.TryGetPropertyValue("image", out var imageNode) && imageNode is JsonObject)
        {
            try
            {
                settings.Image = imageNode.Deserialize<ImagePaintSettings>(Options) ?? new ImagePaintSettings();
            }
            catch (JsonException)
            {
                settings.Image = new ImagePaintSettings();
            }
        }

        var paint = settings.Paint;
        // v1.6.3 retires the two-pass brush pipeline. Existing configs retain
        // the detail brush, falling back to Brush 1 only when it was the sole
        // enabled legacy brush.
        var hasSingleBrush = root.TryGetPropertyValue("brush_size_texels", out _);
        var legacyBrush1Enabled = ReadBool(root, "brush_1_enabled", false);
        var legacyBrush1Size = ReadDouble(root, "brush_1_size_texels", 25.0);
        var legacyBrush2Enabled = ReadBool(root, "brush_2_enabled", true);
        var legacyBrush2Size = ReadDouble(root, "brush_2_size_texels", paint.BrushSizeTexels);
        paint.BrushSizeTexels = hasSingleBrush
            ? ReadDouble(root, "brush_size_texels", paint.BrushSizeTexels)
            : legacyBrush2Enabled || !legacyBrush1Enabled ? legacyBrush2Size : legacyBrush1Size;
        paint.SideSourceMaxUv = ReadDouble(root, "side_source_max_uv", paint.SideSourceMaxUv);
        paint.FrontBackSourceMaxUv = ReadDouble(root, "front_back_source_max_uv", paint.FrontBackSourceMaxUv);
        paint.FrontRegionMode = ReadRegionMode(root, "front_region_mode", paint.FrontRegionMode);
        paint.SideRegionMode = ReadRegionMode(root, "side_region_mode", paint.SideRegionMode);
        paint.BackRegionMode = ReadRegionMode(root, "back_region_mode", paint.BackRegionMode);
        paint.AutoMaterial = ReadBool(root, "auto_material", paint.AutoMaterial);
        paint.Metallic = ReadDouble(root, "metallic", paint.Metallic);
        paint.Roughness = ReadDouble(root, "roughness", paint.Roughness);
        paint.Emissive = ReadDouble(root, "emissive", paint.Emissive);
        if (RgbColor.TryParse(ReadString(root, "fill_color", paint.FillColor.ToHex()), out var fill))
            paint.FillColor = fill;
        paint.FillMetallic = ReadDouble(root, "fill_metallic", paint.FillMetallic);
        paint.FillRoughness = ReadDouble(root, "fill_roughness", paint.FillRoughness);
        paint.FillEmissive = ReadDouble(root, "fill_emissive", paint.FillEmissive);
        paint.ColorCompressionTolerance = ReadDouble(root, "color_compression_tolerance", paint.ColorCompressionTolerance);
        var hasPersistedFillPbr =
            root.TryGetPropertyValue("fill_metallic", out _) &&
            root.TryGetPropertyValue("fill_roughness", out _) &&
            root.TryGetPropertyValue("fill_emissive", out _);
        if (settings.LayoutVersion < FillPbrDefaultsFixLayoutVersion &&
            hasPersistedFillPbr &&
            Math.Abs(paint.FillMetallic - 1.0) < 0.000001 &&
            Math.Abs(paint.FillRoughness) < 0.000001 &&
            Math.Abs(paint.FillEmissive) < 0.000001)
        {
            paint.FillMetallic = paint.Metallic;
            paint.FillRoughness = paint.Roughness;
            paint.FillEmissive = paint.Emissive;
        }

        return Clamp(settings);
    }

    public void Save(AppSettings settings)
    {
        paths.EnsureBaseDirectories();
        var clamped = Clamp(settings);
        var json = JsonSerializer.Serialize(ToConfigDto(clamped), Options);
        var tmp = paths.ConfigPath + ".tmp";
        File.WriteAllText(tmp, json + Environment.NewLine);
        if (File.Exists(paths.ConfigPath))
            File.Delete(paths.ConfigPath);
        File.Move(tmp, paths.ConfigPath);
    }

    public static AppSettings Clamp(AppSettings settings)
    {
        settings.LayoutVersion = AppSettings.CurrentLayoutVersion;
        settings.PanelWidth = Math.Clamp(settings.PanelWidth, 960.0, 3200.0);
        settings.PanelHeight = Math.Clamp(settings.PanelHeight, 640.0, 2200.0);
        settings.Opacity = Math.Clamp(settings.Opacity, 0.35, 1.0);
        if (string.IsNullOrWhiteSpace(settings.Language))
            settings.Language = LocalizationCatalog.DetectSystemLanguage();
        if (!LocalizationCatalog.IsSupported(settings.Language))
            settings.Language = "en";
        settings.LogRetentionDays = Math.Clamp(settings.LogRetentionDays, 1, 90);
        if (string.IsNullOrWhiteSpace(settings.GameProcessName))
            settings.GameProcessName = "PenguinHotel-Win64-Shipping.exe";
        if (string.IsNullOrWhiteSpace(settings.StartHotkey))
            settings.StartHotkey = "F1";
        if (string.IsNullOrWhiteSpace(settings.PreviewHotkey))
            settings.PreviewHotkey = "F2";
        if (string.IsNullOrWhiteSpace(settings.UnPreviewHotkey))
            settings.UnPreviewHotkey = "F3";
        if (string.IsNullOrWhiteSpace(settings.StopHotkey))
            settings.StopHotkey = "F4";
        if (string.IsNullOrWhiteSpace(settings.ImageStartHotkey))
            settings.ImageStartHotkey = "F5";
        if (string.IsNullOrWhiteSpace(settings.ImagePreviewHotkey))
            settings.ImagePreviewHotkey = "F6";
        if (string.IsNullOrWhiteSpace(settings.ImageUnPreviewHotkey))
            settings.ImageUnPreviewHotkey = "F7";
        if (string.IsNullOrWhiteSpace(settings.ImageStopHotkey))
            settings.ImageStopHotkey = "F8";

        settings.Paint.BrushSizeTexels = Math.Clamp(settings.Paint.BrushSizeTexels, 1.0, 10.0);
        settings.Paint.SideSourceMaxUv = Math.Clamp(settings.Paint.SideSourceMaxUv, 0.001, 0.50);
        settings.Paint.FrontBackSourceMaxUv = Math.Clamp(settings.Paint.FrontBackSourceMaxUv, 0.001, 2.00);
        settings.Paint.Metallic = Math.Clamp(settings.Paint.Metallic, 0.0, 1.0);
        settings.Paint.Roughness = Math.Clamp(settings.Paint.Roughness, 0.0, 1.0);
        settings.Paint.Emissive = Math.Clamp(settings.Paint.Emissive, 0.0, 1.0);
        settings.Paint.FillMetallic = Math.Clamp(settings.Paint.FillMetallic, 0.0, 1.0);
        settings.Paint.FillRoughness = Math.Clamp(settings.Paint.FillRoughness, 0.0, 1.0);
        settings.Paint.FillEmissive = Math.Clamp(settings.Paint.FillEmissive, 0.0, 1.0);
        settings.Paint.ColorCompressionTolerance = Math.Clamp(settings.Paint.ColorCompressionTolerance, 0.0, 10.0);
        settings.Image ??= new ImagePaintSettings();
        settings.Image.ClampDraft();
        if (settings.Image.Enabled && !settings.Image.TryValidate(out _))
            settings.Image = new ImagePaintSettings();
        return settings;
    }

    private static object ToConfigDto(AppSettings settings) => new
    {
        layout_version = settings.LayoutVersion,
        panel_x = settings.PanelX,
        panel_y = settings.PanelY,
        panel_width = settings.PanelWidth,
        panel_height = settings.PanelHeight,
        language = settings.Language,
        log_retention_days = settings.LogRetentionDays,
        game_process_name = settings.GameProcessName,
        always_on_top = settings.AlwaysOnTop,
        opacity = settings.Opacity,
        theme_color = settings.ThemeColor.ToHex(),
        start_hotkey = settings.StartHotkey,
        preview_hotkey = settings.PreviewHotkey,
        unpreview_hotkey = settings.UnPreviewHotkey,
        stop_hotkey = settings.StopHotkey,
        image_start_hotkey = settings.ImageStartHotkey,
        image_preview_hotkey = settings.ImagePreviewHotkey,
        image_unpreview_hotkey = settings.ImageUnPreviewHotkey,
        image_stop_hotkey = settings.ImageStopHotkey,
        active_image_design_id = settings.ActiveImageDesignId,
        brush_size_texels = settings.Paint.BrushSizeTexels,
        side_source_max_uv = settings.Paint.SideSourceMaxUv,
        front_back_source_max_uv = settings.Paint.FrontBackSourceMaxUv,
        front_region_mode = RegionModeText(settings.Paint.FrontRegionMode),
        side_region_mode = RegionModeText(settings.Paint.SideRegionMode),
        back_region_mode = RegionModeText(settings.Paint.BackRegionMode),
        auto_material = settings.Paint.AutoMaterial,
        metallic = settings.Paint.Metallic,
        roughness = settings.Paint.Roughness,
        emissive = settings.Paint.Emissive,
        fill_color = settings.Paint.FillColor.ToHex(),
        fill_metallic = settings.Paint.FillMetallic,
        fill_roughness = settings.Paint.FillRoughness,
        fill_emissive = settings.Paint.FillEmissive,
        color_compression_tolerance = settings.Paint.ColorCompressionTolerance
    };

    public static string RegionModeText(RegionMode mode) => mode switch
    {
        RegionMode.Fill => "fill",
        RegionMode.Skip => "skip",
        _ => "paint"
    };

    private static RegionMode ReadRegionMode(JsonObject root, string key, RegionMode fallback)
    {
        var mode = ReadString(root, key, "");
        if (Enum.TryParse<RegionMode>(mode, true, out var parsed))
            return parsed;
        return fallback;
    }

    private static string ReadString(JsonObject root, string key, string fallback) =>
        root.TryGetPropertyValue(key, out var value) && value is not null ? value.GetValue<string>() : fallback;

    private static bool ReadBool(JsonObject root, string key, bool fallback) =>
        root.TryGetPropertyValue(key, out var value) && value is not null ? value.GetValue<bool>() : fallback;

    private static int ReadInt(JsonObject root, string key, int fallback) =>
        root.TryGetPropertyValue(key, out var value) && value is not null ? value.GetValue<int>() : fallback;

    private static double ReadDouble(JsonObject root, string key, double fallback) =>
        root.TryGetPropertyValue(key, out var value) && value is not null ? value.GetValue<double>() : fallback;
}
