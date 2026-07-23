using MecchaCamouflage.Core;
using System.Text.Json;

namespace MecchaCamouflage.Controller;

public sealed record UiSnapshot(
    string Version,
    string Language,
    RuntimeSnapshot Runtime,
    SettingsSnapshot Settings,
    SettingsSnapshot Defaults,
    ResetSnapshot ResetState,
    IReadOnlyList<LocaleSnapshot> Locales,
    IReadOnlyDictionary<string, IReadOnlyDictionary<string, string>> Translations);

public sealed record RuntimeSnapshot(
    string Process,
    string Bridge,
    string Service,
    double ProgressPercent,
    string PaintProgressSource,
    string PaintPass,
    string PaintPassProgress,
    string PaintPassEta,
    string PaintEta,
    string PaintElapsed,
    string Logs,
    bool PaintRunning,
    bool ProgressVisible,
    DiagnosticsSnapshot Diagnostics);

public sealed record SettingsSnapshot(PaintSnapshot Paint, AppSnapshot App, ImageSnapshot Image);

public sealed record PaintSnapshot(
    double BrushSizeTexels,
    bool AutoMaterial,
    double Metallic,
    double Roughness,
    double Emissive,
    string FrontRegionMode,
    string SideRegionMode,
    string BackRegionMode,
    string FillColor,
    double FillMetallic,
    double FillRoughness,
    double FillEmissive,
    bool UsesFill,
    double ColorCompressionTolerance = 0.0);

public sealed record AppSnapshot(
    string ProcessName,
    bool AlwaysOnTop,
    double Opacity,
    string ThemeColor,
    string StartHotkey,
    string PreviewHotkey,
    string UnPreviewHotkey,
    string StopHotkey,
    string ImageStartHotkey,
    string ImagePreviewHotkey,
    string ImageUnPreviewHotkey,
    string ImageStopHotkey);

/// <summary>
/// Snapshot metadata deliberately omits large source image blobs. The web
/// editor fetches those separately after opening the Image tab.
/// </summary>
public sealed record ImageSnapshot(
    bool Enabled,
    int Revision,
    string BodyType,
    string AlphaMode,
    string Placement,
    double BrushSizeTexels,
    double ColorCompressionTolerance,
    double Metallic,
    double Roughness,
    double Emissive,
    string FillColor,
    double FillMetallic,
    double FillRoughness,
    double FillEmissive,
    int LayerCount,
    bool HasCommittedCanvas);

/// <summary>
/// Read-only current-pose geometry for the Image editor's four-face guide.
/// Vertices stay in mesh component space; the packaged profile supplies the
/// fixed index buffer and profile identity.
/// </summary>
public sealed record ImageGuideVertex(double X, double Y, double Z);

/// <summary>
/// A triangle already projected by native through the exact Image Paint atlas
/// mapping. The Web UI draws it directly and does not reimplement face logic.
/// </summary>
public sealed record ImageGuideTriangle(
    double U0,
    double V0,
    double U1,
    double V1,
    double U2,
    double V2,
    bool Edge);

public sealed record ImageGuideSnapshot(
    bool Success,
    string Message,
    string ProfileId,
    IReadOnlyList<ImageGuideVertex> Vertices,
    IReadOnlyList<ImageGuideTriangle>? Triangles = null);

/// <summary>
/// One development-time capture of a body's actual neutral standing pose.
/// This is never used by the editor at runtime: the caller commits the result
/// to the versioned mesh profile and the editor then reads that static profile.
/// </summary>
public sealed record ImageReferencePoseTransform(
    int Index,
    double X,
    double Y,
    double Z,
    double RotationX,
    double RotationY,
    double RotationZ,
    double RotationW,
    double ScaleX,
    double ScaleY,
    double ScaleZ);

public sealed record ImageReferencePoseVertex(int Index, double X, double Y, double Z);

public sealed record ImageReferencePoseSnapshot(
    bool Success,
    string Message,
    string ProfileId,
    IReadOnlyList<ImageReferencePoseTransform> ComponentTransforms,
    IReadOnlyList<ImageReferencePoseVertex> Vertices);

public sealed record ResetSnapshot(
    IReadOnlyDictionary<string, bool> Settings,
    IReadOnlyDictionary<string, bool> Sections);

public sealed record LocaleSnapshot(string Code, string NativeName);

public sealed record HostCommandResult(bool Success, string Message = "");

public sealed record SettingChange(string Key, JsonElement Value);

public sealed record ProgressSnapshot(
    string Phase,
    string Result,
    bool Terminal,
    int Step,
    int TotalSteps,
    double Progress,
    double PaintEtaMs,
    double PaintElapsedMs,
    string ReplayCurrentPass = "",
    int ReplayCurrentPassStart = -1,
    int ReplayCurrentPassEnd = -1,
    string ReplayProgressSource = "",
    int ReplayCurrentPassCompleted = -1,
    int ReplayCurrentPassTotal = -1,
    double ReplayCurrentPassEtaMs = -1.0,
    double PreprocessingMs = 0.0,
    double PoseMs = 0.0,
    double CaptureMs = 0.0,
    double SampleMs = 0.0,
    double AdaptivePlanMs = 0.0);

public sealed record HotkeySet(
    string Start,
    string Preview,
    string UnPreview,
    string Stop,
    string ImageStart,
    string ImagePreview,
    string ImageUnPreview,
    string ImageStop)
{
    public HotkeySet(string start, string preview, string unPreview, string stop)
        : this(start, preview, unPreview, stop, "F5", "F6", "F7", "F8")
    {
    }

    public static HotkeySet From(AppSettings settings) =>
        new(
            settings.StartHotkey,
            settings.PreviewHotkey,
            settings.UnPreviewHotkey,
            settings.StopHotkey,
            settings.ImageStartHotkey,
            settings.ImagePreviewHotkey,
            settings.ImageUnPreviewHotkey,
            settings.ImageStopHotkey);

    public void ApplyTo(AppSettings settings)
    {
        settings.StartHotkey = Start;
        settings.PreviewHotkey = Preview;
        settings.UnPreviewHotkey = UnPreview;
        settings.StopHotkey = Stop;
        settings.ImageStartHotkey = ImageStart;
        settings.ImagePreviewHotkey = ImagePreview;
        settings.ImageUnPreviewHotkey = ImageUnPreview;
        settings.ImageStopHotkey = ImageStop;
    }

    public bool TryValidate(out string message)
    {
        message = "";
        var values = new[] { Start, Preview, UnPreview, Stop, ImageStart, ImagePreview, ImageUnPreview, ImageStop };
        foreach (var value in values)
        {
            if (!IsFunctionKey(value))
            {
                message = "Hotkeys must be F1 through F24.";
                return false;
            }
        }
        if (values.Select(Normalize).Distinct(StringComparer.OrdinalIgnoreCase).Count() != values.Length)
        {
            message = "Hotkeys must not be duplicated.";
            return false;
        }
        return true;
    }

    public static bool IsFunctionKey(string? value)
    {
        var normalized = Normalize(value);
        return normalized.Length >= 2 &&
               normalized[0] == 'F' &&
               int.TryParse(normalized[1..], out var number) &&
               number is >= 1 and <= 24;
    }

    public static string Normalize(string? value) => (value ?? "").Trim().ToUpperInvariant();
}

public sealed class HotkeyKeyState
{
    private readonly HashSet<uint> pressedKeys = [];

    public bool TryBeginPress(uint virtualKey) => pressedKeys.Add(virtualKey);

    public void EndPress(uint virtualKey) => pressedKeys.Remove(virtualKey);

    public void Clear() => pressedKeys.Clear();
}
