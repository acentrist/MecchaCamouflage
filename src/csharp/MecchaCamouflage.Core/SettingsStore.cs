using System.Text.Json;
using System.Text.Json.Nodes;

namespace MecchaCamouflage.Core;

public sealed class SettingsStore
{
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
        var path = File.Exists(paths.ConfigPath) ? paths.ConfigPath : paths.LegacyConfigPath;
        if (!File.Exists(path))
            return Clamp(new AppSettings());

        var text = File.ReadAllText(path);
        var root = JsonNode.Parse(text)?.AsObject();
        if (root is null)
            return Clamp(new AppSettings());

        var settings = new AppSettings();
        settings.LayoutVersion = ReadInt(root, "layout_version", settings.LayoutVersion);
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
        settings.StartHotkey = ReadString(root, "start_hotkey", ReadString(root, "paint_hotkey", settings.StartHotkey));
        settings.StopHotkey = ReadString(root, "stop_hotkey", settings.StopHotkey);
        settings.PreviewHotkey = ReadString(root, "preview_hotkey", settings.PreviewHotkey);
        settings.UnPreviewHotkey = ReadString(root, "unpreview_hotkey", settings.UnPreviewHotkey);
        settings.LogRetentionDays = ReadInt(root, "log_retention_days", settings.LogRetentionDays);

        var paint = settings.Paint;
        paint.StrokeSizeTexels = ReadDouble(root, "stroke_size_texels", paint.StrokeSizeTexels);
        paint.PackedBatchDelayMs = ReadInt(root, "packed_batch_delay_ms", paint.PackedBatchDelayMs);
        paint.CoverageStepTexels = ReadDouble(root, "coverage_step_texels", paint.CoverageStepTexels);
        paint.SideSourceMaxUv = ReadDouble(root, "side_source_max_uv", paint.SideSourceMaxUv);
        paint.FrontBackSourceMaxUv = ReadDouble(root, "front_back_source_max_uv", paint.FrontBackSourceMaxUv);
        paint.FrontRegionMode = ReadRegionMode(root, "front_region_mode", "enable_front_paint", paint.FrontRegionMode);
        paint.SideRegionMode = ReadRegionMode(root, "side_region_mode", "enable_side_paint", paint.SideRegionMode);
        paint.BackRegionMode = ReadRegionMode(root, "back_region_mode", "enable_back_paint", paint.BackRegionMode);
        paint.AutoMaterial = ReadBool(root, "auto_material", ReadBool(root, "auto_material_properties", paint.AutoMaterial));
        paint.Metallic = ReadDouble(root, "metallic", paint.Metallic);
        paint.Roughness = ReadDouble(root, "roughness", paint.Roughness);
        if (RgbColor.TryParse(ReadString(root, "fill_color", paint.FillColor.ToHex()), out var fill))
            paint.FillColor = fill;
        paint.FillMetallic = ReadDouble(root, "fill_metallic", paint.FillMetallic);
        paint.FillRoughness = ReadDouble(root, "fill_roughness", paint.FillRoughness);
        paint.QualityPreset = ReadQualityPreset(root, "quality_preset", paint.QualityPreset);
        paint.BilinearColorSampling = ReadBool(root, "bilinear_color_sampling", ReadBool(root, "enable_bilinear", paint.BilinearColorSampling));
        paint.BicubicColorSampling = ReadBool(root, "bicubic_color_sampling", paint.BicubicColorSampling);
        paint.DitherStrength = ReadDouble(root, "dither_strength", paint.DitherStrength);
        paint.MinRoughness = ReadDouble(root, "min_roughness", paint.MinRoughness);
        paint.FalloffHardnessPct = ReadInt(root, "falloff_hardness_pct", paint.FalloffHardnessPct);
        paint.CoverageSupersample = ReadInt(root, "coverage_supersample", paint.CoverageSupersample);
        paint.EdgeAwareSharpening = ReadBool(root, "edge_aware_sharpening", paint.EdgeAwareSharpening);
        paint.SharpenStrength = ReadDouble(root, "sharpen_strength", paint.SharpenStrength);
        paint.EnableDetectionArtifacts = ReadBool(root, "enable_detection_artifacts", paint.EnableDetectionArtifacts);
        paint.DetectionDetail = ReadInt(root, "detection_detail", paint.DetectionDetail);
        paint.UseLocalImageSource = ReadBool(root, "use_local_image_source", paint.UseLocalImageSource);
        paint.LocalImagePath = ReadString(root, "local_image_path", paint.LocalImagePath);
        paint.BypassLiveCapture = ReadBool(root, "bypass_live_capture", paint.BypassLiveCapture);

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

        settings.Paint.StrokeSizeTexels = Math.Clamp(settings.Paint.StrokeSizeTexels, 1.0, 10.0);
        settings.Paint.PackedBatchDelayMs = Math.Clamp(settings.Paint.PackedBatchDelayMs, 1, 100);
        settings.Paint.CoverageStepTexels = settings.Paint.StrokeSizeTexels;
        settings.Paint.SideSourceMaxUv = Math.Clamp(settings.Paint.SideSourceMaxUv, 0.001, 0.50);
        settings.Paint.FrontBackSourceMaxUv = Math.Clamp(settings.Paint.FrontBackSourceMaxUv, 0.001, 2.00);
        settings.Paint.Metallic = Math.Clamp(settings.Paint.Metallic, 0.0, 1.0);
        settings.Paint.Roughness = Math.Clamp(settings.Paint.Roughness, 0.0, 1.0);
        settings.Paint.FillMetallic = Math.Clamp(settings.Paint.FillMetallic, 0.0, 1.0);
        settings.Paint.FillRoughness = Math.Clamp(settings.Paint.FillRoughness, 0.0, 1.0);
        settings.Paint.DitherStrength = Math.Clamp(settings.Paint.DitherStrength, 0.0, 1.0);
        settings.Paint.MinRoughness = Math.Clamp(settings.Paint.MinRoughness, 0.0, 1.0);
        settings.Paint.FalloffHardnessPct = Math.Clamp(settings.Paint.FalloffHardnessPct, 0, 100);
        settings.Paint.CoverageSupersample = Math.Clamp(settings.Paint.CoverageSupersample, 1, 3);
        settings.Paint.SharpenStrength = Math.Clamp(settings.Paint.SharpenStrength, 0.0, 1.0);
        settings.Paint.DetectionDetail = Math.Clamp(settings.Paint.DetectionDetail, 1, 5);
        settings.Paint.LocalImagePath = (settings.Paint.LocalImagePath ?? "").Trim();
        if (settings.Paint.LocalImagePath.Length > 1024) settings.Paint.LocalImagePath = "";
        if (settings.Paint.UseLocalImageSource && string.IsNullOrWhiteSpace(settings.Paint.LocalImagePath)) settings.Paint.UseLocalImageSource = false;
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
        stroke_size_texels = settings.Paint.StrokeSizeTexels,
        packed_batch_delay_ms = settings.Paint.PackedBatchDelayMs,
        coverage_step_texels = settings.Paint.CoverageStepTexels,
        side_source_max_uv = settings.Paint.SideSourceMaxUv,
        front_back_source_max_uv = settings.Paint.FrontBackSourceMaxUv,
        front_region_mode = RegionModeText(settings.Paint.FrontRegionMode),
        side_region_mode = RegionModeText(settings.Paint.SideRegionMode),
        back_region_mode = RegionModeText(settings.Paint.BackRegionMode),
        auto_material = settings.Paint.AutoMaterial,
        auto_material_properties = settings.Paint.AutoMaterial,
        metallic = settings.Paint.Metallic,
        roughness = settings.Paint.Roughness,
        fill_color = settings.Paint.FillColor.ToHex(),
        fill_metallic = settings.Paint.FillMetallic,
        fill_roughness = settings.Paint.FillRoughness,
        quality_preset = QualityPresetText(settings.Paint.QualityPreset),
        bilinear_color_sampling = settings.Paint.BilinearColorSampling,
        bicubic_color_sampling = settings.Paint.BicubicColorSampling,
        dither_strength = settings.Paint.DitherStrength,
        min_roughness = settings.Paint.MinRoughness,
        falloff_hardness_pct = settings.Paint.FalloffHardnessPct,
        coverage_supersample = settings.Paint.CoverageSupersample,
        edge_aware_sharpening = settings.Paint.EdgeAwareSharpening,
        sharpen_strength = settings.Paint.SharpenStrength,
        enable_detection_artifacts = settings.Paint.EnableDetectionArtifacts,
        detection_detail = settings.Paint.DetectionDetail,
        use_local_image_source = settings.Paint.UseLocalImageSource,
        local_image_path = settings.Paint.LocalImagePath,
        bypass_live_capture = settings.Paint.BypassLiveCapture
    };

    public static string QualityPresetText(PaintQualityPreset preset) => preset switch
    {
        PaintQualityPreset.Fast => "fast",
        PaintQualityPreset.Balanced => "balanced",
        PaintQualityPreset.Ultra => "ultra",
        _ => "high"
    };

    public static PaintQualityPreset ParseQualityPreset(string? value, PaintQualityPreset fallback = PaintQualityPreset.High)
    {
        if (!string.IsNullOrWhiteSpace(value) && Enum.TryParse<PaintQualityPreset>(value.Trim(), true, out var parsed))
            return parsed;
        return fallback;
    }

    private static PaintQualityPreset ReadQualityPreset(JsonObject root, string key, PaintQualityPreset fallback) =>
        ParseQualityPreset(ReadString(root, key, ""), fallback);

    public static string RegionModeText(RegionMode mode) => mode switch
    {
        RegionMode.Fill => "fill",
        RegionMode.Skip => "skip",
        _ => "paint"
    };

    private static RegionMode ReadRegionMode(JsonObject root, string key, string legacyBoolKey, RegionMode fallback)
    {
        var mode = ReadString(root, key, "");
        if (Enum.TryParse<RegionMode>(mode, true, out var parsed))
            return parsed;
        if (root.TryGetPropertyValue(legacyBoolKey, out var legacy) && legacy is not null)
            return legacy.GetValue<bool>() ? RegionMode.Paint : RegionMode.Fill;
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
