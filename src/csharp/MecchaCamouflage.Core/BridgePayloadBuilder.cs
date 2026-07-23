using System.Globalization;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace MecchaCamouflage.Core;

public sealed record ImagePaintOptions(
    int Width,
    int Height,
    string RgbaBase64,
    string AlphaMode,
    RgbColor BackgroundColor,
    string Placement,
    string BodyType,
    string FrontRegionMode,
    string RightRegionMode,
    string BackRegionMode,
    string LeftRegionMode,
    RgbColor FillColor,
    double FillMetallic,
    double FillRoughness,
    double FillEmissive,
    double BrushSizeTexels,
    double ColorCompressionTolerance,
    double Metallic,
    double Roughness,
    double Emissive,
    double BackgroundMetallic,
    double BackgroundRoughness,
    double BackgroundEmissive,
    int Revision);

public sealed record PaintRequestOptions(
    bool PreviewOnly = false,
    bool UnPreviewOnly = false,
    bool ResearchArtifacts = false,
    int DiagnosticStrokeLimit = 0,
    ImagePaintOptions? Image = null);

public static class BridgePayloadBuilder
{
    private static readonly JsonSerializerOptions PayloadJsonOptions = new()
    {
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public static string BuildPaintPayload(AppSettings settings, int processId, string processName, PaintRequestOptions options)
    {
        var paint = SettingsStore.Clamp(settings).Paint;
        var image = options.Image;
        var payload = new Dictionary<string, object?>
        {
            ["type"] = "paint_full_route",
            ["native_apply_mode"] = "native_recorded_paint",
            ["route"] = "native_recorded_paint",
            ["preview_only"] = options.PreviewOnly,
            ["unpreview_only"] = options.UnPreviewOnly,
            ["research_artifacts"] = options.ResearchArtifacts,
            ["process"] = new Dictionary<string, object?>
            {
                ["pid"] = processId,
                ["name"] = processName
            },
            ["tuning"] = new Dictionary<string, object?>
            {
                ["brush_size_texels"] = paint.BrushSizeTexels,
                ["side_source_max_uv"] = paint.SideSourceMaxUv,
                ["front_back_source_max_uv"] = paint.FrontBackSourceMaxUv,
                ["auto_material"] = paint.AutoMaterial,
                ["metallic"] = paint.Metallic,
                ["roughness"] = paint.Roughness,
                ["emissive"] = paint.Emissive,
                ["front_region_mode"] = SettingsStore.RegionModeText(paint.FrontRegionMode),
                ["side_region_mode"] = SettingsStore.RegionModeText(paint.SideRegionMode),
                ["back_region_mode"] = SettingsStore.RegionModeText(paint.BackRegionMode),
                ["fill_color"] = paint.FillColor.ToHex(),
                ["fill_color_r"] = ToUnit(paint.FillColor.R),
                ["fill_color_g"] = ToUnit(paint.FillColor.G),
                ["fill_color_b"] = ToUnit(paint.FillColor.B),
                ["fill_metallic"] = paint.FillMetallic,
                ["fill_roughness"] = paint.FillRoughness,
                ["fill_emissive"] = paint.FillEmissive,
                ["color_compression_tolerance"] = paint.ColorCompressionTolerance
            },
            ["image_paint_enabled"] = image is not null,
            ["image_paint_width"] = image?.Width ?? 0,
            ["image_paint_height"] = image?.Height ?? 0,
            ["image_paint_rgba_base64"] = image?.RgbaBase64 ?? "",
            ["image_paint_alpha_mode"] = image?.AlphaMode ?? "skip",
            ["image_paint_background_r"] = ToUnit(image?.BackgroundColor.R ?? 255),
            ["image_paint_background_g"] = ToUnit(image?.BackgroundColor.G ?? 255),
            ["image_paint_background_b"] = ToUnit(image?.BackgroundColor.B ?? 255),
            ["image_paint_placement"] = image?.Placement ?? "fit",
            ["image_paint_body_type"] = image?.BodyType ?? "round",
            ["image_paint_front_region_mode"] = image?.FrontRegionMode ?? "fill",
            ["image_paint_right_region_mode"] = image?.RightRegionMode ?? "fill",
            ["image_paint_back_region_mode"] = image?.BackRegionMode ?? "fill",
            ["image_paint_left_region_mode"] = image?.LeftRegionMode ?? "fill",
            ["image_paint_fill_color_r"] = ToUnit(image?.FillColor.R ?? 255),
            ["image_paint_fill_color_g"] = ToUnit(image?.FillColor.G ?? 255),
            ["image_paint_fill_color_b"] = ToUnit(image?.FillColor.B ?? 255),
            ["image_paint_fill_metallic"] = image?.FillMetallic ?? 1.0,
            ["image_paint_fill_roughness"] = image?.FillRoughness ?? 0.0,
            ["image_paint_fill_emissive"] = image?.FillEmissive ?? 0.0,
            ["image_paint_brush_size_texels"] = image?.BrushSizeTexels ?? 5.0,
            ["image_paint_color_compression_tolerance"] = image?.ColorCompressionTolerance ?? 0.0,
            ["image_paint_metallic"] = image?.Metallic ?? 0.0,
            ["image_paint_roughness"] = image?.Roughness ?? 1.0,
            ["image_paint_emissive"] = image?.Emissive ?? 0.0,
            ["image_paint_revision"] = image?.Revision ?? 0
        };
        if (options.DiagnosticStrokeLimit > 0)
            payload["diagnostic_stroke_limit"] = Math.Clamp(options.DiagnosticStrokeLimit, 1, 10_000);
        return JsonSerializer.Serialize(payload, PayloadJsonOptions) + "\n";
    }

    private static double ToUnit(byte value) =>
        double.Parse((value / 255.0).ToString("0.########", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
}
