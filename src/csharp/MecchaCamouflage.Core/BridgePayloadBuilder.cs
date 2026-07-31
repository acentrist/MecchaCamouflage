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
        var paintSource = new Dictionary<string, object?>
        {
            ["kind"] = image is null ? "environment_capture" : "imported_image"
        };
        if (image is not null)
        {
            paintSource["image_paint_width"] = image.Width;
            paintSource["image_paint_height"] = image.Height;
            paintSource["image_paint_rgba_base64"] = image.RgbaBase64;
            paintSource["image_paint_alpha_mode"] = image.AlphaMode;
            paintSource["image_paint_background_r"] = ToUnit(image.BackgroundColor.R);
            paintSource["image_paint_background_g"] = ToUnit(image.BackgroundColor.G);
            paintSource["image_paint_background_b"] = ToUnit(image.BackgroundColor.B);
            paintSource["image_paint_placement"] = image.Placement;
            paintSource["image_paint_body_type"] = image.BodyType;
            paintSource["image_paint_front_region_mode"] = image.FrontRegionMode;
            paintSource["image_paint_right_region_mode"] = image.RightRegionMode;
            paintSource["image_paint_back_region_mode"] = image.BackRegionMode;
            paintSource["image_paint_left_region_mode"] = image.LeftRegionMode;
            paintSource["image_paint_fill_color_r"] = ToUnit(image.FillColor.R);
            paintSource["image_paint_fill_color_g"] = ToUnit(image.FillColor.G);
            paintSource["image_paint_fill_color_b"] = ToUnit(image.FillColor.B);
            paintSource["image_paint_fill_metallic"] = image.FillMetallic;
            paintSource["image_paint_fill_roughness"] = image.FillRoughness;
            paintSource["image_paint_fill_emissive"] = image.FillEmissive;
            paintSource["image_paint_brush_size_texels"] = image.BrushSizeTexels;
            paintSource["image_paint_color_compression_tolerance"] = image.ColorCompressionTolerance;
            paintSource["image_paint_metallic"] = image.Metallic;
            paintSource["image_paint_roughness"] = image.Roughness;
            paintSource["image_paint_emissive"] = image.Emissive;
            paintSource["image_paint_revision"] = image.Revision;
        }
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
            ["paint_source"] = paintSource,
            ["tuning"] = new Dictionary<string, object?>
            {
                ["brush_size_texels"] = paint.BrushSizeTexels,
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
            }
        };
        if (options.DiagnosticStrokeLimit > 0)
            payload["diagnostic_stroke_limit"] = Math.Clamp(options.DiagnosticStrokeLimit, 1, 10_000);
        return JsonSerializer.Serialize(payload, PayloadJsonOptions) + "\n";
    }

    private static double ToUnit(byte value) =>
        double.Parse((value / 255.0).ToString("0.########", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
}
