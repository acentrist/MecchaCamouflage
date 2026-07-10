using System.Globalization;
using System.Text.Json;

namespace MecchaCamouflage.Core;

public sealed record PaintRequestOptions(bool PreviewOnly = false, bool UnPreviewOnly = false, bool ResearchArtifacts = false);

public static class BridgePayloadBuilder
{
    // The native packed route self-resolves its batch limit from replication
    // pressure (see HostSession.DefaultPackedBatchLimit). The legacy
    // "server_batch_limit" tuning key is therefore NOT sent for the normal
    // presets. Only the Ultra preset opts into an explicit hard cap at the
    // validated native maximum, because Ultra also drives 1ms pacing and must
    // not let the self-resolver exceed the throughput the native side has been
    // validated for.
    private const int UltraValidatedBatchLimit = 50;

    public static string BuildPaintPayload(AppSettings settings, int processId, string processName, PaintRequestOptions options)
    {
        var paint = SettingsStore.Clamp(settings).Paint;

        var tuning = new Dictionary<string, object?>
        {
            ["stroke_size_texels"] = paint.StrokeSizeTexels,
            ["server_batch_delay_ms"] = paint.PackedBatchDelayMs,
            ["coverage_step_texels"] = paint.CoverageStepTexels,
            ["side_source_max_uv"] = paint.SideSourceMaxUv,
            ["front_back_source_max_uv"] = paint.FrontBackSourceMaxUv,
            ["auto_material"] = paint.AutoMaterial,
            ["auto_material_properties"] = paint.AutoMaterial,
            ["metallic"] = paint.Metallic,
            ["roughness"] = paint.Roughness,
            ["front_region_mode"] = SettingsStore.RegionModeText(paint.FrontRegionMode),
            ["side_region_mode"] = SettingsStore.RegionModeText(paint.SideRegionMode),
            ["back_region_mode"] = SettingsStore.RegionModeText(paint.BackRegionMode),
            ["enable_front_paint"] = paint.FrontRegionMode == RegionMode.Paint,
            ["enable_side_paint"] = paint.SideRegionMode == RegionMode.Paint,
            ["enable_back_paint"] = paint.BackRegionMode == RegionMode.Paint,
            ["fill_color"] = paint.FillColor.ToHex(),
            ["fill_color_r"] = ToUnit(paint.FillColor.R),
            ["fill_color_g"] = ToUnit(paint.FillColor.G),
            ["fill_color_b"] = ToUnit(paint.FillColor.B),
            ["fill_metallic"] = paint.FillMetallic,
            ["fill_roughness"] = paint.FillRoughness,
            ["quality_preset"] = SettingsStore.QualityPresetText(paint.QualityPreset),
            ["replication_pacing_enabled"] = true,
            ["enable_bilinear"] = paint.BilinearColorSampling,
            ["bicubic_color_sampling"] = paint.BicubicColorSampling,
            ["dither_strength"] = paint.DitherStrength,
            ["min_roughness"] = paint.MinRoughness,
            ["falloff_hardness_pct"] = paint.FalloffHardnessPct,
            ["coverage_supersample"] = paint.CoverageSupersample,
            ["edge_aware_sharpening"] = paint.EdgeAwareSharpening,
            ["sharpen_strength"] = paint.SharpenStrength,
            ["detection_artifacts"] = paint.EnableDetectionArtifacts,
            ["detection_detail"] = paint.DetectionDetail,
            ["detection_density"] = paint.DetectionDensity,
            ["local_image_enabled"] = paint.UseLocalImageSource,
            ["local_image_path"] = paint.LocalImagePath,
            ["bypass_live_capture"] = paint.UseLocalImageSource && paint.BypassLiveCapture
        };

        if (paint.QualityPreset == PaintQualityPreset.Ultra)
        {
            tuning["server_batch_limit"] = UltraValidatedBatchLimit;
        }

        var payload = new Dictionary<string, object?>
        {
            ["type"] = "paint_full_route",
            ["native_apply_mode"] = "mesh_first_paint",
            ["route"] = "f10_mesh_first_paint",
            ["server_batch_rpc"] = "packed",
            ["packed_route"] = "component",
            ["preview_only"] = options.PreviewOnly,
            ["unpreview_only"] = options.UnPreviewOnly,
            ["research_artifacts"] = options.ResearchArtifacts,
            ["process"] = new Dictionary<string, object?>
            {
                ["pid"] = processId,
                ["name"] = processName
            },
            ["tuning"] = tuning
        };
        return JsonSerializer.Serialize(payload) + "\n";
    }

    private static double ToUnit(byte value) =>
        double.Parse((value / 255.0).ToString("0.########", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
}
