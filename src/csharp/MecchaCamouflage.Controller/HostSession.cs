using System.Diagnostics;
using System.Text.Json;
using MecchaCamouflage.Core;

namespace MecchaCamouflage.Controller;

public sealed class HostSession
{
    private const int DefaultPackedBatchLimit = 20;
    private const int DefaultPackedPacingMs = 75;

    private static readonly string[] ResetKeys =
    [
        "paint.brushSizeTexels",
        "paint.packedBatchDelayMs",
        "paint.coverageStepTexels",
        "paint.autoMaterial",
        "paint.metallic",
        "paint.roughness",
        "paint.frontRegionMode",
        "paint.sideRegionMode",
        "paint.backRegionMode",
        "paint.fillColor",
        "paint.fillMetallic",
        "paint.fillRoughness",
        "paint.qualityPreset",
        "paint.bilinearColorSampling",
        "paint.ditherStrength",
        "paint.minRoughness",
        "paint.falloffHardnessPct",
        "paint.coverageSupersample",
        "paint.edgeAwareSharpening",
        "paint.sharpenStrength",
        "paint.enableDetectionArtifacts",
        "paint.detectionDetail",
        "paint.useLocalImageSource",
        "paint.localImagePath",
        "paint.bypassLiveCapture",
        "app.processName",
        "app.alwaysOnTop",
        "app.opacity",
        "app.themeColor",
        "app.startHotkey",
        "app.previewHotkey",
        "app.unpreviewHotkey",
        "app.stopHotkey"
    ];

    public HostSession(string version)
    {
        Paths = new AppPaths(version);
        DiagnosticsState.EnsureInitialized(Paths, version);
        Store = new SettingsStore(Paths);
        Settings = Store.Load();
        Log = new RuntimeLog(Paths);
        Runtime = new RuntimeBridgeService(Paths, Log);
    }

    public LocalizationCatalog Localization { get; } = LocalizationCatalog.Load();
    public AppPaths Paths { get; }
    public SettingsStore Store { get; }
    public RuntimeLog Log { get; }
    public RuntimeBridgeService Runtime { get; }
    public AppSettings Settings { get; private set; }
    public bool PaintRunning { get; private set; }
    private readonly SemaphoreSlim bridgeWarmupGate = new(1, 1);
    private DateTimeOffset nextBridgeWarmupAttempt;
    private DateTimeOffset currentPaintStartedAt = DateTimeOffset.MinValue;
    private bool finalProgressLogged;
    private bool currentProgressIsServerPaint;
    private bool nativePaintMayBeRunning;

    public async Task<UiSnapshot> GetSnapshotAsync(CancellationToken cancellationToken = default)
    {
        var process = Runtime.FindGameProcess(Settings.GameProcessName);
        var ping = await Runtime.PingAsync(cancellationToken, RuntimeBridgeService.BridgeProbeTimeout);
        var progress = ReadCurrentProgressSnapshot(liveOnly: true);
        var bridgeReady = process is not null &&
            Runtime.IsConnected &&
            ping.Ok &&
            ping.Success &&
            (ping.ProcessId is null || ping.ProcessId == process.Id);
        return CreateSnapshot(
            process is null ? "waiting" : "attached",
            bridgeReady ? "connected" : "waiting",
            bridgeReady ? "ready" : "stopped",
            progress);
    }

    public async Task WarmupBridgeAsync(CancellationToken cancellationToken = default)
    {
        var now = DateTimeOffset.UtcNow;
        if (now < nextBridgeWarmupAttempt)
            return;
        if (!await bridgeWarmupGate.WaitAsync(0, cancellationToken))
            return;
        try
        {
            var process = Runtime.FindGameProcess(Settings.GameProcessName);
            if (process is null)
            {
                _ = await Runtime.EnsureReadyAsync(Settings.GameProcessName, cancellationToken);
                nextBridgeWarmupAttempt = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(2);
                return;
            }
            var ready = await Runtime.EnsureReadyAsync(Settings.GameProcessName, cancellationToken);
            nextBridgeWarmupAttempt = ready
                ? DateTimeOffset.UtcNow + TimeSpan.FromSeconds(2)
                : DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception ex)
        {
            Log.Warn("Bridge warmup failed: " + ex.Message);
            nextBridgeWarmupAttempt = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        }
        finally
        {
            bridgeWarmupGate.Release();
        }
    }

    public HostCommandResult UpdateSetting(string key, JsonElement value)
    {
        return UpdateSettings([new SettingChange(key, value)]);
    }

    public HostCommandResult UpdateSettings(IEnumerable<SettingChange> changes)
    {
        var previous = Clone(Settings);
        try
        {
            var next = Clone(Settings);
            foreach (var change in changes)
                ApplySetting(next, change.Key, change.Value);
            return CommitSettings(next, previous);
        }
        catch (Exception ex)
        {
            Settings = previous;
            return new HostCommandResult(false, ex.Message);
        }
    }

    public HostCommandResult ResetSetting(string key)
    {
        var previous = Clone(Settings);
        try
        {
            var next = Clone(Settings);
            ResetOne(next, new AppSettings(), key);
            return CommitSettings(next, previous);
        }
        catch (Exception ex)
        {
            Settings = previous;
            return new HostCommandResult(false, ex.Message);
        }
    }

    public HostCommandResult ResetSection(string section)
    {
        var previous = Clone(Settings);
        var next = Clone(Settings);
        var defaults = new AppSettings();
        switch (section.Trim().ToLowerInvariant())
        {
            case "runtime":
                return new HostCommandResult(true);
            case "paint.geometry":
            case "geometry":
                next.Paint.StrokeSizeTexels = defaults.Paint.StrokeSizeTexels;
                next.Paint.CoverageStepTexels = defaults.Paint.StrokeSizeTexels;
                next.Paint.PackedBatchDelayMs = defaults.Paint.PackedBatchDelayMs;
                break;
            case "paint.material":
            case "material":
                next.Paint.AutoMaterial = defaults.Paint.AutoMaterial;
                next.Paint.Metallic = defaults.Paint.Metallic;
                next.Paint.Roughness = defaults.Paint.Roughness;
                break;
            case "paint.quality":
            case "quality":
                next.Paint.QualityPreset = defaults.Paint.QualityPreset;
                next.Paint.BilinearColorSampling = defaults.Paint.BilinearColorSampling;
                next.Paint.DitherStrength = defaults.Paint.DitherStrength;
                next.Paint.MinRoughness = defaults.Paint.MinRoughness;
                next.Paint.FalloffHardnessPct = defaults.Paint.FalloffHardnessPct;
                next.Paint.CoverageSupersample = defaults.Paint.CoverageSupersample;
                next.Paint.EdgeAwareSharpening = defaults.Paint.EdgeAwareSharpening;
                next.Paint.SharpenStrength = defaults.Paint.SharpenStrength;
                break;
            case "paint.detection":
            case "detection":
                next.Paint.EnableDetectionArtifacts = defaults.Paint.EnableDetectionArtifacts;
                next.Paint.DetectionDetail = defaults.Paint.DetectionDetail;
                break;
            case "paint.source":
            case "source":
                next.Paint.UseLocalImageSource = defaults.Paint.UseLocalImageSource;
                next.Paint.LocalImagePath = defaults.Paint.LocalImagePath;
                next.Paint.BypassLiveCapture = defaults.Paint.BypassLiveCapture;
                break;
            case "regions":
                next.Paint.FrontRegionMode = defaults.Paint.FrontRegionMode;
                next.Paint.SideRegionMode = defaults.Paint.SideRegionMode;
                next.Paint.BackRegionMode = defaults.Paint.BackRegionMode;
                break;
            case "fill":
            case "fill.material":
                next.Paint.FillColor = defaults.Paint.FillColor;
                next.Paint.FillMetallic = defaults.Paint.FillMetallic;
                next.Paint.FillRoughness = defaults.Paint.FillRoughness;
                break;
            case "app":
                next.GameProcessName = defaults.GameProcessName;
                next.AlwaysOnTop = defaults.AlwaysOnTop;
                next.Opacity = defaults.Opacity;
                next.ThemeColor = defaults.ThemeColor;
                next.StartHotkey = defaults.StartHotkey;
                next.PreviewHotkey = defaults.PreviewHotkey;
                next.UnPreviewHotkey = defaults.UnPreviewHotkey;
                next.StopHotkey = defaults.StopHotkey;
                break;
            default:
                return new HostCommandResult(false, $"Unknown section: {section}");
        }
        return CommitSettings(next, previous);
    }

    public HostCommandResult ResetAllSettings()
    {
        var defaults = new AppSettings
        {
            Language = Settings.Language,
            PanelX = Settings.PanelX,
            PanelY = Settings.PanelY,
            PanelWidth = Settings.PanelWidth,
            PanelHeight = Settings.PanelHeight
        };
        Settings = defaults;
        Store.Save(Settings);
        return new HostCommandResult(true);
    }

    public void SetWindowSnapshot(double width, double height, double x, double y)
    {
        if (width > 0)
            Settings.PanelWidth = width;
        if (height > 0)
            Settings.PanelHeight = height;
        Settings.PanelX = x;
        Settings.PanelY = y;
        Settings = SettingsStore.Clamp(Settings);
        Store.Save(Settings);
    }

    public async Task<HostCommandResult> RunPaintAsync(bool previewOnly, bool unpreviewOnly, CancellationToken cancellationToken = default)
    {
        if (Settings.Paint.UseLocalImageSource &&
            (!File.Exists(Settings.Paint.LocalImagePath) ||
             !string.Equals(Path.GetExtension(Settings.Paint.LocalImagePath), ".bmp", StringComparison.OrdinalIgnoreCase)))
            return new HostCommandResult(false, "Local image source is missing or invalid. Select the image again.");
        if (PaintRunning || nativePaintMayBeRunning)
        {
            const string alreadyRunning = "Paint: already running.";
            Log.Warn(alreadyRunning);
            return new HostCommandResult(false, alreadyRunning);
        }
        PaintRunning = true;
        currentPaintStartedAt = DateTimeOffset.UtcNow;
        currentProgressIsServerPaint = !previewOnly && !unpreviewOnly;
        finalProgressLogged = false;
        TryDeleteProgressSnapshot();
        try
        {
            var ready = await Runtime.EnsureReadyAsync(Settings.GameProcessName, cancellationToken);
            if (!ready)
                return new HostCommandResult(false, "Bridge is not connected.");
            var process = Runtime.FindGameProcess(Settings.GameProcessName);
            if (process is null)
            {
                Log.Warn("Game process not found.");
                return new HostCommandResult(false, "Game process not found.");
            }
            var startedMessage = previewOnly ? "Preview: started." : (unpreviewOnly ? "UnPreview: started." : "Paint: started.");
            Log.Info(startedMessage);
            var payload = BridgePayloadBuilder.BuildPaintPayload(
                Settings,
                process.Id,
                Settings.GameProcessName,
                new PaintRequestOptions(previewOnly, unpreviewOnly, Environment.GetEnvironmentVariable("MECCHA_RESEARCH_ARTIFACTS") == "1"));
            var response = await Runtime.SendPaintAsync(payload, cancellationToken);
            var message = FriendlyBridgeMessage(response.Message.Length > 0 ? response.Message : response.Stage);
            if (response.Success)
            {
                nativePaintMayBeRunning = false;
                LogFinalProgressOnce();
                Log.Info(message);
                return new HostCommandResult(true, message);
            }
            if (message == "Paint: canceled.")
            {
                nativePaintMayBeRunning = false;
                return new HostCommandResult(false, message);
            }
            if (IsGuardWarning(message))
            {
                if (message == "Paint: already running.")
                    nativePaintMayBeRunning = true;
                else
                    nativePaintMayBeRunning = false;
                Log.Warn(message);
                return new HostCommandResult(false, message);
            }
            nativePaintMayBeRunning = false;
            LogFailureProgressOnce();
            Log.Error(message);
            return new HostCommandResult(false, message);
        }
        finally
        {
            PaintRunning = false;
            currentProgressIsServerPaint = false;
        }
    }

    public async Task<HostCommandResult> StopPaintAsync(CancellationToken cancellationToken = default)
    {
        if (!PaintRunning && !nativePaintMayBeRunning && !Runtime.IsConnected)
        {
            const string noActivePaint = "Paint: no active paint to cancel.";
            Log.Warn(noActivePaint);
            return new HostCommandResult(false, noActivePaint);
        }
        var response = await Runtime.CancelPaintAsync(cancellationToken);
        var message = FriendlyBridgeMessage(response.Message.Length > 0 ? response.Message : response.Stage);
        var cancelledJobs = CancelledPaintJobCount(response);
        if (response.Success && cancelledJobs == 0)
        {
            const string noActivePaint = "Paint: no active paint to cancel.";
            nativePaintMayBeRunning = false;
            Log.Warn(noActivePaint);
            PaintRunning = false;
            return new HostCommandResult(false, noActivePaint);
        }
        if (response.Success)
        {
            nativePaintMayBeRunning = false;
            LogFinalProgressOnce();
            Log.Info("Paint: canceled.");
        }
        else if (IsGuardWarning(message))
        {
            Log.Warn(message);
        }
        else
        {
            Log.Error("Paint: cancel failed: " + message);
        }
        PaintRunning = false;
        return new HostCommandResult(response.Success, response.Success ? "Paint: canceled." : message);
    }

    public static int? CancelledPaintJobCount(BridgeReply response)
    {
        if (string.IsNullOrWhiteSpace(response.Raw))
            return null;
        try
        {
            using var doc = JsonDocument.Parse(response.Raw);
            if (!doc.RootElement.TryGetProperty("metadata", out var metadata) ||
                metadata.ValueKind != JsonValueKind.Object)
            {
                return null;
            }
            var active = Int(metadata, "cancelled_active_paint_jobs", 0);
            var queued = Int(metadata, "cancelled_queued_paint_jobs", 0);
            return Math.Max(0, active) + Math.Max(0, queued);
        }
        catch
        {
            return null;
        }
    }

    public void OpenLogs()
    {
        Directory.CreateDirectory(Paths.DiagnosticsDirectory);
        Process.Start(new ProcessStartInfo(Paths.VersionRoot) { UseShellExecute = true });
    }

    public string ClipboardLogText()
    {
        if (!currentProgressIsServerPaint)
            return Log.Text;
        var progress = ReadCurrentProgressSnapshot(liveOnly: false);
        if (progress is null)
            return Log.Text;
        var line = FormatProgressLogLine(progress);
        if (line.Length == 0)
            return Log.Text;
        return string.IsNullOrWhiteSpace(Log.Text)
            ? line
            : Log.Text.TrimEnd() + Environment.NewLine + line;
    }

    public async Task ShutdownBridgeAsync()
    {
        var response = await Runtime.PingAsync();
        if (response.Ok)
            _ = await Runtime.ShutdownAsync();
    }

    private HostCommandResult CommitSettings(AppSettings next, AppSettings previous)
    {
        next = SettingsStore.Clamp(next);
        var hotkeys = HotkeySet.From(next);
        if (!hotkeys.TryValidate(out var message))
        {
            Settings = previous;
            return new HostCommandResult(false, message);
        }
        Settings = next;
        Store.Save(Settings);
        return new HostCommandResult(true);
    }

    public static string FriendlyBridgeMessage(string message)
    {
        if (string.IsNullOrWhiteSpace(message))
            return "";

        var value = message.Trim();
        var lower = value.ToLowerInvariant();

        if (lower.Contains("already running"))
            return "Paint: already running.";
        if (lower is "mesh_first_paint_done" ||
            lower is "paint completed." ||
            lower is "paint completed" ||
            lower is "paint: completed." ||
            lower is "paint: completed" ||
            lower.Contains("mesh-first paint completed") ||
            lower.Contains("sent through serverpaintbatch"))
            return "Paint: completed.";
        if (lower.Contains("mesh-first paint cancelled") || lower.Contains("mesh-first paint canceled"))
            return "Paint: canceled.";
        if (lower is "mesh_preview_done" || lower.Contains("local preview material texture imported"))
            return "Preview: applied.";
        if (lower is "mesh_unpreview_done" || lower.Contains("local preview material texture restored"))
            return "Preview: restored.";
        if (lower is "mesh_preview_failed" || lower.Contains("local preview material texture import failed"))
            return "Preview: failed.";
        if (lower is "mesh_unpreview_failed" || lower.Contains("local preview material restore failed"))
            return "Preview: restore failed.";
        if (lower is "mesh_unpreview_snapshot_unavailable" || lower.Contains("no local preview snapshot is available"))
            return "Preview: no active preview to restore.";
        if (lower is "mesh_unpreview_component_mismatch")
            return "The saved preview belongs to a different mesh.";
        if (lower is "mesh_local_visual_sync_failed")
            return "Paint: completed, but local preview failed.";
        if (lower is "mesh_paint_context_changed" || lower.Contains("paint_context_changed"))
            return "Paint: stopped because the game paint component changed.";
        if (lower.Contains("game paint component is no longer available"))
            return "Paint: stopped because the game paint component is no longer available.";
        if (lower.Contains("local pawn is no longer available"))
            return "Paint: stopped because the local pawn is no longer available.";
        if (lower.Contains("local pawn changed"))
            return "Paint: stopped because the local pawn changed.";
        if (lower.Contains("paint_component_unavailable"))
            return "Paint: stopped because the game paint component is unavailable.";
        if (lower.Contains("unsafe color-transfer candidates"))
            return "Paint: blocked because the current mesh sampling was unsafe.";
        if (lower is "mesh_server_packed_batch_unavailable" || lower.Contains("serverpackedpaintbatch is unavailable"))
            return "Packed multiplayer paint sync is unavailable.";
        if (lower is "mesh_server_packed_batch_incompatible" || lower.Contains("serverpackedpaintbatch requires"))
            return "Paint failed because strokes could not be packed.";
        if (lower is "mesh_server_packed_source_id_unavailable" || lower.Contains("source id is unavailable"))
            return "Packed multiplayer paint source id is unavailable.";
        if (lower is "mesh_server_batch_failed")
            return "Paint: failed while sending strokes.";

        return value
            .Replace("mesh-first ", "", StringComparison.OrdinalIgnoreCase)
            .Replace("mesh first ", "", StringComparison.OrdinalIgnoreCase)
            .Replace("mesh_first_", "", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsGuardWarning(string message) =>
        message.Equals("Paint: already running.", StringComparison.OrdinalIgnoreCase) ||
        message.Equals("Paint: no active paint to cancel.", StringComparison.OrdinalIgnoreCase) ||
        message.Equals("Preview: no active preview to restore.", StringComparison.OrdinalIgnoreCase);

    private static AppSettings Clone(AppSettings source) =>
        JsonSerializer.Deserialize<AppSettings>(JsonSerializer.Serialize(source)) ?? new AppSettings();

    private UiSnapshot CreateSnapshot(string process, string bridge, string service, ProgressSnapshot? progress)
    {
        var defaults = new AppSettings();
        var percent = 0.0;
        var eta = "-";
        var elapsed = "-";
        var batch = "-";
        var delay = "-";
        var timingLabel = "delay";
        var queue = "-";
        if (progress is not null)
        {
            percent = progress.TotalSteps > 0
                ? Math.Clamp(progress.Step * 100.0 / progress.TotalSteps, 0.0, 100.0)
                : Math.Clamp(progress.Progress * 100.0, 0.0, 100.0);
            eta = FormatEta(progress);
            elapsed = FormatDuration(progress.PaintElapsedMs);
            batch = FormatBatch(progress);
            delay = FormatDelay(progress);
            timingLabel = TimingLabel(progress);
            queue = FormatQueue(progress);
        }

        return new UiSnapshot(
            VersionInfo.Current,
            Settings.Language,
            new RuntimeSnapshot(process, bridge, service, percent, eta, elapsed, batch, delay, timingLabel, queue, Log.Text, PaintRunning, progress is not null, DiagnosticsState.Snapshot(Paths)),
            ToSnapshot(Settings),
            ToSnapshot(defaults),
            BuildResetSnapshot(Settings, defaults),
            LocalizationCatalog.SupportedLocales.Select(locale => new LocaleSnapshot(locale.Code, locale.NativeName)).ToArray(),
            Localization.All);
    }

    private static SettingsSnapshot ToSnapshot(AppSettings settings)
    {
        var paint = settings.Paint;
        return new SettingsSnapshot(
            new PaintSnapshot(
                paint.StrokeSizeTexels,
                paint.PackedBatchDelayMs,
                paint.CoverageStepTexels,
                paint.AutoMaterial,
                paint.Metallic,
                paint.Roughness,
                SettingsStore.RegionModeText(paint.FrontRegionMode),
                SettingsStore.RegionModeText(paint.SideRegionMode),
                SettingsStore.RegionModeText(paint.BackRegionMode),
                paint.FillColor.ToHex(),
                paint.FillMetallic,
                paint.FillRoughness,
                paint.UsesFill,
                SettingsStore.QualityPresetText(paint.QualityPreset),
                paint.BilinearColorSampling,
                paint.BicubicColorSampling,
                paint.DitherStrength,
                paint.MinRoughness,
                paint.FalloffHardnessPct,
                paint.CoverageSupersample,
                paint.EnableDetectionArtifacts,
                paint.DetectionDetail,
                paint.UseLocalImageSource,
                paint.LocalImagePath,
                paint.BypassLiveCapture),
            new AppSnapshot(
                settings.GameProcessName,
                settings.AlwaysOnTop,
                settings.Opacity,
                settings.ThemeColor.ToHex(),
                settings.StartHotkey,
                settings.PreviewHotkey,
                settings.UnPreviewHotkey,
                settings.StopHotkey));
    }

    private static string FormatBatch(ProgressSnapshot progress)
    {
        var effectiveBatch = EffectiveBatch(progress);
        if (progress.ReplicationPacingEnabled && effectiveBatch > 0)
        {
            var max = progress.ReplicationPacingResolvedBatchLimit > 0 ? progress.ReplicationPacingResolvedBatchLimit : progress.ReplicationPacingRequestedBatchLimit;
            return max > 0 ? $"{effectiveBatch}/{max}" : effectiveBatch.ToString(System.Globalization.CultureInfo.InvariantCulture);
        }
        var batch = effectiveBatch;
        return batch > 0 ? batch.ToString(System.Globalization.CultureInfo.InvariantCulture) : "-";
    }

    private static string FormatDelay(ProgressSnapshot progress)
    {
        var delay = EffectiveDelay(progress);
        return delay >= 0 ? $"{delay}ms" : "-";
    }

    private static string TimingLabel(ProgressSnapshot _) => "delay";

    private static int EffectiveBatch(ProgressSnapshot progress)
    {
        if (progress.ReplicationPacingEnabled && progress.ReplicationPacingBatchLimit > 0)
            return progress.ReplicationPacingBatchLimit;
        if (progress.ServerBatchLimit > 0)
            return progress.ServerBatchLimit;
        return DefaultPackedBatchLimit;
    }

    private static int EffectiveDelay(ProgressSnapshot progress)
    {
        if (progress.ReplicationPacingEnabled && progress.ReplicationPacingDelayMs > 0)
            return progress.ReplicationPacingDelayMs;
        if (progress.ServerBatchDelayMs > 0)
            return progress.ServerBatchDelayMs;
        return DefaultPackedPacingMs;
    }

    private static string FormatQueue(ProgressSnapshot progress)
    {
        if (progress.ReplicationQueuedStrokeCount < 0)
            return "-";
        var strokes = progress.ReplicationQueuedStrokeCount == 1 ? "stroke" : "strokes";
        if (progress.ReplicationPacingQueueDrainStrokesPerSec > 0.0 && double.IsFinite(progress.ReplicationPacingQueueDrainStrokesPerSec))
        {
            var drain = progress.ReplicationPacingQueueDrainStrokesPerSec >= 10.0
                ? Math.Round(progress.ReplicationPacingQueueDrainStrokesPerSec).ToString(System.Globalization.CultureInfo.InvariantCulture)
                : Math.Round(progress.ReplicationPacingQueueDrainStrokesPerSec, 1).ToString(System.Globalization.CultureInfo.InvariantCulture);
            return $"{progress.ReplicationQueuedStrokeCount} {strokes} (drain {drain}/s)";
        }
        return $"{progress.ReplicationQueuedStrokeCount} {strokes}";
    }

    private static ResetSnapshot BuildResetSnapshot(AppSettings settings, AppSettings defaults)
    {
        var map = ResetKeys.ToDictionary(key => key, key => !SettingEquals(settings, defaults, key), StringComparer.OrdinalIgnoreCase);
        var sections = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase)
        {
            ["paint.geometry"] = map["paint.brushSizeTexels"] || map["paint.coverageStepTexels"] || map["paint.packedBatchDelayMs"],
            ["paint.material"] = map["paint.autoMaterial"] || map["paint.metallic"] || map["paint.roughness"],
            ["regions"] = map["paint.frontRegionMode"] || map["paint.sideRegionMode"] || map["paint.backRegionMode"],
            ["fill.material"] = map["paint.fillColor"] || map["paint.fillMetallic"] || map["paint.fillRoughness"],
            ["paint.quality"] = map["paint.qualityPreset"] || map["paint.bilinearColorSampling"] || map["paint.ditherStrength"] ||
                    map["paint.minRoughness"] || map["paint.falloffHardnessPct"] || map["paint.coverageSupersample"] ||
                    map["paint.edgeAwareSharpening"] || map["paint.sharpenStrength"],
            ["paint.detection"] = map["paint.enableDetectionArtifacts"] || map["paint.detectionDetail"],
            ["paint.source"] = map["paint.useLocalImageSource"] || map["paint.localImagePath"] || map["paint.bypassLiveCapture"],
            ["app"] = map["app.processName"] || map["app.alwaysOnTop"] || map["app.opacity"] || map["app.themeColor"] ||
                    map["app.startHotkey"] || map["app.previewHotkey"] || map["app.unpreviewHotkey"] || map["app.stopHotkey"]
        };
        return new ResetSnapshot(map, sections);
    }

    private static bool SettingEquals(AppSettings left, AppSettings right, string key) => key switch
    {
        "paint.brushSizeTexels" => Nearly(left.Paint.StrokeSizeTexels, right.Paint.StrokeSizeTexels),
        "paint.packedBatchDelayMs" => left.Paint.PackedBatchDelayMs == right.Paint.PackedBatchDelayMs,
        "paint.coverageStepTexels" => Nearly(left.Paint.CoverageStepTexels, right.Paint.CoverageStepTexels),
        "paint.autoMaterial" => left.Paint.AutoMaterial == right.Paint.AutoMaterial,
        "paint.metallic" => Nearly(left.Paint.Metallic, right.Paint.Metallic),
        "paint.roughness" => Nearly(left.Paint.Roughness, right.Paint.Roughness),
        "paint.frontRegionMode" => left.Paint.FrontRegionMode == right.Paint.FrontRegionMode,
        "paint.sideRegionMode" => left.Paint.SideRegionMode == right.Paint.SideRegionMode,
        "paint.backRegionMode" => left.Paint.BackRegionMode == right.Paint.BackRegionMode,
        "paint.fillColor" => left.Paint.FillColor == right.Paint.FillColor,
        "paint.fillMetallic" => Nearly(left.Paint.FillMetallic, right.Paint.FillMetallic),
        "paint.fillRoughness" => Nearly(left.Paint.FillRoughness, right.Paint.FillRoughness),
        "paint.qualityPreset" => left.Paint.QualityPreset == right.Paint.QualityPreset,
        "paint.bilinearColorSampling" => left.Paint.BilinearColorSampling == right.Paint.BilinearColorSampling,
        "paint.ditherStrength" => Nearly(left.Paint.DitherStrength, right.Paint.DitherStrength),
        "paint.minRoughness" => Nearly(left.Paint.MinRoughness, right.Paint.MinRoughness),
        "paint.falloffHardnessPct" => left.Paint.FalloffHardnessPct == right.Paint.FalloffHardnessPct,
        "paint.coverageSupersample" => left.Paint.CoverageSupersample == right.Paint.CoverageSupersample,
        "paint.edgeAwareSharpening" => left.Paint.EdgeAwareSharpening == right.Paint.EdgeAwareSharpening,
        "paint.sharpenStrength" => Nearly(left.Paint.SharpenStrength, right.Paint.SharpenStrength),
        "paint.enableDetectionArtifacts" => left.Paint.EnableDetectionArtifacts == right.Paint.EnableDetectionArtifacts,
        "paint.detectionDetail" => left.Paint.DetectionDetail == right.Paint.DetectionDetail,
        "paint.useLocalImageSource" => left.Paint.UseLocalImageSource == right.Paint.UseLocalImageSource,
        "paint.localImagePath" => left.Paint.LocalImagePath == right.Paint.LocalImagePath,
        "paint.bypassLiveCapture" => left.Paint.BypassLiveCapture == right.Paint.BypassLiveCapture,
        "app.processName" => left.GameProcessName == right.GameProcessName,
        "app.alwaysOnTop" => left.AlwaysOnTop == right.AlwaysOnTop,
        "app.opacity" => Nearly(left.Opacity, right.Opacity),
        "app.themeColor" => left.ThemeColor == right.ThemeColor,
        "app.startHotkey" => left.StartHotkey == right.StartHotkey,
        "app.previewHotkey" => left.PreviewHotkey == right.PreviewHotkey,
        "app.unpreviewHotkey" => left.UnPreviewHotkey == right.UnPreviewHotkey,
        "app.stopHotkey" => left.StopHotkey == right.StopHotkey,
        _ => true
    };

    private static void ResetOne(AppSettings settings, AppSettings defaults, string key)
    {
        switch (key)
        {
            case "paint.brushSizeTexels":
            case "paint.coverageStepTexels":
                settings.Paint.StrokeSizeTexels = defaults.Paint.StrokeSizeTexels;
                settings.Paint.CoverageStepTexels = defaults.Paint.StrokeSizeTexels;
                settings.Paint.PackedBatchDelayMs = defaults.Paint.PackedBatchDelayMs;
                break;
            case "paint.packedBatchDelayMs": settings.Paint.PackedBatchDelayMs = defaults.Paint.PackedBatchDelayMs; break;
            case "paint.autoMaterial": settings.Paint.AutoMaterial = defaults.Paint.AutoMaterial; break;
            case "paint.metallic": settings.Paint.Metallic = defaults.Paint.Metallic; break;
            case "paint.roughness": settings.Paint.Roughness = defaults.Paint.Roughness; break;
            case "paint.frontRegionMode": settings.Paint.FrontRegionMode = defaults.Paint.FrontRegionMode; break;
            case "paint.sideRegionMode": settings.Paint.SideRegionMode = defaults.Paint.SideRegionMode; break;
            case "paint.backRegionMode": settings.Paint.BackRegionMode = defaults.Paint.BackRegionMode; break;
            case "paint.fillColor": settings.Paint.FillColor = defaults.Paint.FillColor; break;
            case "paint.fillMetallic": settings.Paint.FillMetallic = defaults.Paint.FillMetallic; break;
            case "paint.fillRoughness": settings.Paint.FillRoughness = defaults.Paint.FillRoughness; break;
            case "paint.qualityPreset": settings.Paint.QualityPreset = defaults.Paint.QualityPreset; break;
            case "paint.bilinearColorSampling": settings.Paint.BilinearColorSampling = defaults.Paint.BilinearColorSampling; break;
            case "paint.ditherStrength": settings.Paint.DitherStrength = defaults.Paint.DitherStrength; break;
            case "paint.minRoughness": settings.Paint.MinRoughness = defaults.Paint.MinRoughness; break;
            case "paint.falloffHardnessPct": settings.Paint.FalloffHardnessPct = defaults.Paint.FalloffHardnessPct; break;
            case "paint.coverageSupersample": settings.Paint.CoverageSupersample = defaults.Paint.CoverageSupersample; break;
            case "paint.edgeAwareSharpening": settings.Paint.EdgeAwareSharpening = defaults.Paint.EdgeAwareSharpening; break;
            case "paint.sharpenStrength": settings.Paint.SharpenStrength = defaults.Paint.SharpenStrength; break;
            case "paint.enableDetectionArtifacts": settings.Paint.EnableDetectionArtifacts = defaults.Paint.EnableDetectionArtifacts; break;
            case "paint.detectionDetail": settings.Paint.DetectionDetail = defaults.Paint.DetectionDetail; break;
            case "paint.useLocalImageSource": settings.Paint.UseLocalImageSource = defaults.Paint.UseLocalImageSource; break;
            case "paint.localImagePath": settings.Paint.LocalImagePath = defaults.Paint.LocalImagePath; break;
            case "paint.bypassLiveCapture": settings.Paint.BypassLiveCapture = defaults.Paint.BypassLiveCapture; break;
            case "app.processName": settings.GameProcessName = defaults.GameProcessName; break;
            case "app.alwaysOnTop": settings.AlwaysOnTop = defaults.AlwaysOnTop; break;
            case "app.opacity": settings.Opacity = defaults.Opacity; break;
            case "app.themeColor": settings.ThemeColor = defaults.ThemeColor; break;
            case "app.startHotkey": settings.StartHotkey = defaults.StartHotkey; break;
            case "app.previewHotkey": settings.PreviewHotkey = defaults.PreviewHotkey; break;
            case "app.unpreviewHotkey": settings.UnPreviewHotkey = defaults.UnPreviewHotkey; break;
            case "app.stopHotkey": settings.StopHotkey = defaults.StopHotkey; break;
            default: throw new ArgumentException($"Unknown setting: {key}");
        }
    }

    private static void ApplySetting(AppSettings settings, string key, JsonElement value)
    {
        switch (key)
        {
            case "paint.brushSizeTexels":
            case "paint.coverageStepTexels":
                settings.Paint.StrokeSizeTexels = value.GetDouble();
                settings.Paint.CoverageStepTexels = settings.Paint.StrokeSizeTexels;
                break;
            case "paint.packedBatchDelayMs": settings.Paint.PackedBatchDelayMs = value.GetInt32(); break;
            case "paint.autoMaterial": settings.Paint.AutoMaterial = value.GetBoolean(); break;
            case "paint.metallic": settings.Paint.Metallic = value.GetDouble(); break;
            case "paint.roughness": settings.Paint.Roughness = value.GetDouble(); break;
            case "paint.frontRegionMode": settings.Paint.FrontRegionMode = ParseRegionMode(value.GetString()); break;
            case "paint.sideRegionMode": settings.Paint.SideRegionMode = ParseRegionMode(value.GetString()); break;
            case "paint.backRegionMode": settings.Paint.BackRegionMode = ParseRegionMode(value.GetString()); break;
            case "paint.fillColor":
                if (!RgbColor.TryParse(value.GetString(), out var fill))
                    throw new ArgumentException("Fill color must be #RRGGBB.");
                settings.Paint.FillColor = fill;
                break;
            case "paint.fillMetallic": settings.Paint.FillMetallic = value.GetDouble(); break;
            case "paint.fillRoughness": settings.Paint.FillRoughness = value.GetDouble(); break;
            case "paint.qualityPreset": settings.Paint.ApplyQualityPreset(SettingsStore.ParseQualityPreset(value.GetString())); break;
            case "paint.bilinearColorSampling": settings.Paint.BilinearColorSampling = value.GetBoolean(); break;
            case "paint.ditherStrength": settings.Paint.DitherStrength = value.GetDouble(); break;
            case "paint.minRoughness": settings.Paint.MinRoughness = value.GetDouble(); break;
            case "paint.falloffHardnessPct": settings.Paint.FalloffHardnessPct = value.GetInt32(); break;
            case "paint.coverageSupersample": settings.Paint.CoverageSupersample = value.GetInt32(); break;
            case "paint.edgeAwareSharpening": settings.Paint.EdgeAwareSharpening = value.GetBoolean(); break;
            case "paint.sharpenStrength": settings.Paint.SharpenStrength = value.GetDouble(); break;
            case "paint.enableDetectionArtifacts": settings.Paint.EnableDetectionArtifacts = value.GetBoolean(); break;
            case "paint.detectionDetail": settings.Paint.DetectionDetail = value.GetInt32(); break;
            case "paint.useLocalImageSource": settings.Paint.UseLocalImageSource = value.GetBoolean(); break;
            case "paint.localImagePath": settings.Paint.LocalImagePath = value.GetString() ?? ""; break;
            case "paint.bypassLiveCapture": settings.Paint.BypassLiveCapture = value.GetBoolean(); break;
            case "app.language": settings.Language = value.GetString() ?? settings.Language; break;
            case "app.processName": settings.GameProcessName = value.GetString() ?? settings.GameProcessName; break;
            case "app.alwaysOnTop": settings.AlwaysOnTop = value.GetBoolean(); break;
            case "app.opacity": settings.Opacity = value.GetDouble(); break;
            case "app.themeColor":
                if (!RgbColor.TryParse(value.GetString(), out var theme))
                    throw new ArgumentException("Theme color must be #RRGGBB.");
                settings.ThemeColor = theme;
                break;
            case "app.startHotkey": settings.StartHotkey = HotkeySet.Normalize(value.GetString()); break;
            case "app.previewHotkey": settings.PreviewHotkey = HotkeySet.Normalize(value.GetString()); break;
            case "app.unpreviewHotkey": settings.UnPreviewHotkey = HotkeySet.Normalize(value.GetString()); break;
            case "app.stopHotkey": settings.StopHotkey = HotkeySet.Normalize(value.GetString()); break;
            default: throw new ArgumentException($"Unknown setting: {key}");
        }
    }

    private static RegionMode ParseRegionMode(string? value)
    {
        if (Enum.TryParse<RegionMode>(value, true, out var mode) && Enum.IsDefined(mode))
            return mode;
        throw new ArgumentException("Region mode must be paint, fill, or skip.");
    }

    private void TryDeleteProgressSnapshot()
    {
        try
        {
            var path = Runtime.ProgressPath;
            if (!string.IsNullOrWhiteSpace(path) && File.Exists(path))
                File.Delete(path);
        }
        catch
        {
            // Best-effort cleanup only; stale progress is still rejected by timestamp.
        }
    }

    private ProgressSnapshot? ReadCurrentProgressSnapshot(bool liveOnly)
    {
        if (currentPaintStartedAt == DateTimeOffset.MinValue)
            return null;
        var progress = ReadProgressSnapshot(Runtime.ProgressPath, out var writeTime);
        if (progress is null)
            progress = ReadFallbackProgressSnapshot(out writeTime);
        if (progress is null)
            return null;
        var cutoff = currentPaintStartedAt.AddSeconds(-1);
        if (writeTime < cutoff)
        {
            progress = ReadFallbackProgressSnapshot(out writeTime);
            if (progress is null || writeTime < cutoff)
                return null;
        }
        if (liveOnly && !PaintRunning)
            return null;
        if (liveOnly && !currentProgressIsServerPaint)
            return null;
        return progress;
    }

    private ProgressSnapshot? ReadFallbackProgressSnapshot(out DateTimeOffset writeTime)
    {
        writeTime = DateTimeOffset.MinValue;
        if (currentPaintStartedAt == DateTimeOffset.MinValue)
            return null;
        var cutoff = currentPaintStartedAt.AddSeconds(-1);
        try
        {
            foreach (var path in ProgressSnapshotCandidatePaths(Paths, Runtime.ProgressPath))
            {
                var progress = ReadProgressSnapshot(path, out var candidateWriteTime);
                if (progress is null || candidateWriteTime < cutoff)
                    continue;
                writeTime = candidateWriteTime;
                return progress;
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
        }
        return null;
    }

    public static string[] ProgressSnapshotCandidatePaths(AppPaths paths, string? preferredPath = null)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var candidates = new List<string>();

        AddCandidate(preferredPath);
        AddProgressDirectory(paths.BridgeProgressDirectory);

        return candidates
            .OrderByDescending(SafeLastWriteTimeUtc)
            .ToArray();

        void AddProgressDirectory(string directory)
        {
            if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory))
                return;
            try
            {
                foreach (var path in Directory.EnumerateFiles(directory, "*.progress.json"))
                    AddCandidate(path);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            {
            }
        }

        void AddCandidate(string? path)
        {
            if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
                return;
            try
            {
                var fullPath = Path.GetFullPath(path);
                if (seen.Add(fullPath))
                    candidates.Add(fullPath);
            }
            catch (Exception ex) when (ex is ArgumentException or NotSupportedException or PathTooLongException or IOException or UnauthorizedAccessException)
            {
            }
        }

        static DateTime SafeLastWriteTimeUtc(string path)
        {
            try
            {
                return File.GetLastWriteTimeUtc(path);
            }
            catch
            {
                return DateTime.MinValue;
            }
        }
    }

    private void LogFinalProgressOnce()
    {
        if (!currentProgressIsServerPaint)
            return;
        if (finalProgressLogged)
            return;
        var progress = ReadCurrentProgressSnapshot(liveOnly: false);
        if (progress is null)
            return;
        var line = FormatProgressLogLine(progress);
        if (line.Length == 0)
            return;
        finalProgressLogged = true;
        Log.Info(line);
    }

    private void LogFailureProgressOnce()
    {
        if (!currentProgressIsServerPaint)
            return;
        if (finalProgressLogged)
            return;
        var progress = ReadCurrentProgressSnapshot(liveOnly: false);
        if (progress is null || !ShouldLogFailureProgress(progress))
            return;
        var line = FormatProgressLogLine(progress);
        if (line.Length == 0)
            return;
        finalProgressLogged = true;
        Log.Info(line);
    }

    private static bool ShouldLogFailureProgress(ProgressSnapshot progress)
    {
        var phase = progress.Phase.Trim().ToLowerInvariant();
        return phase is "server_batch" or "local_sync" or "local_texture_import" or "texture_sync_observe" or "server_texture_sync" or "failed" or "cancelled" ||
               phase.StartsWith("mesh_server_batch", StringComparison.OrdinalIgnoreCase) ||
               phase.StartsWith("mesh_paint_", StringComparison.OrdinalIgnoreCase);
    }

    private string FormatProgressLogLine(ProgressSnapshot progress)
    {
        var percent = progress.TotalSteps > 0
            ? Math.Clamp(progress.Step * 100.0 / progress.TotalSteps, 0.0, 100.0)
            : Math.Clamp(progress.Progress * 100.0, 0.0, 100.0);
        var rounded = (int)Math.Round(percent);
        return $"Paint: {rounded}% {ProgressBar(rounded)} | batch {FormatBatch(progress)} | {TimingLabel(progress)} {FormatDelay(progress)} | queue {FormatQueue(progress)} | ETA {FormatEta(progress)} | elapsed {FormatDuration(progress.PaintElapsedMs)}";
    }

    private static string ProgressBar(int percent)
    {
        const int width = 16;
        var filled = Math.Clamp((int)Math.Round((percent / 100.0) * width), 0, width);
        return "[" + new string('#', filled) + new string('-', width - filled) + "]";
    }

    private static ProgressSnapshot? ReadProgressSnapshot(string path, out DateTimeOffset writeTime)
    {
        writeTime = DateTimeOffset.MinValue;
        if (string.IsNullOrWhiteSpace(path) || !File.Exists(path))
            return null;
        try
        {
            writeTime = File.GetLastWriteTimeUtc(path);
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var root = doc.RootElement;
            return new ProgressSnapshot(
                Text(root, "phase", Text(root, "stage", "")),
                Text(root, "result", ""),
                Bool(root, "terminal", false),
                Int(root, "step", 0),
                Int(root, "total_steps", Int(root, "total_strokes", 0)),
                Number(root, "progress", 0.0),
                Int(root, "server_batch_limit", -1),
                Int(root, "server_batch_delay_ms", -1),
                Number(root, "paint_eta_ms", -1.0),
                Number(root, "paint_elapsed_ms", Number(root, "elapsed_ms", -1.0)),
                Bool(root, "replication_pacing_enabled", Bool(root, "adaptive_batch_enabled", false)),
                Int(root, "replication_pacing_requested_batch_limit", Int(root, "adaptive_requested_batch_limit", -1)),
                Int(root, "replication_pacing_resolved_batch_limit", Int(root, "adaptive_resolved_batch_limit", -1)),
                Int(root, "replication_pacing_requested_delay_ms", Int(root, "adaptive_requested_delay_ms", -1)),
                Int(root, "replication_pacing_batch_limit", Int(root, "adaptive_batch_limit", -1)),
                Int(root, "replication_pacing_delay_ms", Int(root, "adaptive_delay_ms", -1)),
                Text(root, "replication_pacing_pressure_level", Text(root, "adaptive_pressure_level", "unknown")),
                Int(root, "replication_pacing_backoff_count", Int(root, "adaptive_backoff_count", 0)),
                Number(root, "replication_pacing_queue_drain_strokes_per_sec", Number(root, "adaptive_queue_drain_strokes_per_sec", -1.0)),
                Number(root, "replication_pacing_send_strokes_per_sec", Number(root, "adaptive_send_strokes_per_sec", -1.0)),
                Number(root, "replication_pacing_model_eta_ms", Number(root, "adaptive_model_eta_ms", -1.0)),
                Int(root, "replication_queued_batch_count", -1),
                Int(root, "replication_queued_stroke_count", -1),
                Int(root, "replication_max_strokes_per_tick", -1),
                Number(root, "replication_estimated_ticks_to_drain", -1.0));
        }
        catch
        {
            return null;
        }
    }

    private static string FormatDuration(double milliseconds)
    {
        if (!double.IsFinite(milliseconds) || milliseconds < 0.0)
            return "-";
        if (milliseconds < 1000.0)
            return "0s";
        var totalSeconds = (int)Math.Round(milliseconds / 1000.0);
        if (totalSeconds < 60)
            return totalSeconds + "s";
        var minutes = totalSeconds / 60;
        var seconds = totalSeconds % 60;
        if (minutes < 60)
            return $"{minutes}m {seconds:00}s";
        var hours = minutes / 60;
        minutes %= 60;
        return $"{hours}h {minutes:00}m";
    }

    private static string FormatEta(ProgressSnapshot progress)
    {
        if (progress.Terminal)
            return string.Equals(progress.Result, "done", StringComparison.OrdinalIgnoreCase) ? "0s" : "-";

        return FormatDuration(progress.PaintEtaMs);
    }

    private static string Text(JsonElement root, string key, string fallback) =>
        root.TryGetProperty(key, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() ?? fallback : fallback;

    private static bool Bool(JsonElement root, string key, bool fallback) =>
        root.TryGetProperty(key, out var value) && value.ValueKind is JsonValueKind.True or JsonValueKind.False ? value.GetBoolean() : fallback;

    private static int Int(JsonElement root, string key, int fallback) =>
        root.TryGetProperty(key, out var value) && value.TryGetInt32(out var parsed) ? parsed : fallback;

    private static double Number(JsonElement root, string key, double fallback) =>
        root.TryGetProperty(key, out var value) && value.TryGetDouble(out var parsed) ? parsed : fallback;

    private static bool Nearly(double left, double right) => Math.Abs(left - right) < 0.000001;
}
