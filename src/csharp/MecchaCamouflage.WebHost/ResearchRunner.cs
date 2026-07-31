using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using MecchaCamouflage.Controller;
using MecchaCamouflage.Core;

namespace MecchaCamouflage.WebHost;

/// <summary>
/// Explicit, local-only runtime investigation entry point. It deliberately shares the packaged
/// controller and its authenticated bridge instead of opening a second diagnostic socket.
/// </summary>
internal static class ResearchRunner
{
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

    private sealed record Options(
        int ProcessId,
        string DeclaredRole,
        string ArtifactDirectory,
        bool Paint,
        bool PreviewOnly,
        bool UnPreviewOnly,
        int HoldSeconds,
        int PressureSampleMs,
        bool TextureSnapshot,
        int TextureSampleMs,
        ResearchTextureTarget TextureTarget,
        int TextureDiscoverySeconds,
        int StrokeLimit,
        int? QueueTarget,
        int? ReplayStrokeIndex,
        int? TargetChannelOverride,
        int? ApplyModeOverride,
        int? CancelAfterMs,
        bool CancelWhenActive,
        int? ShutdownAfterMs,
        RgbColor? FillColorOverride,
        RgbColor? PaintColorOverride,
        double? MetallicOverride,
        double? RoughnessOverride,
        double? EmissiveOverride,
        double? FillMetallicOverride,
        double? FillRoughnessOverride,
        double? FillEmissiveOverride,
        RegionMode? FrontRegionModeOverride,
        RegionMode? SideRegionModeOverride,
        RegionMode? BackRegionModeOverride);

    private sealed record ReplyArtifact(string Name, DateTimeOffset StartedUtc, DateTimeOffset CompletedUtc, BridgeReply Reply);
    private sealed record TimedReply(DateTimeOffset StartedUtc, DateTimeOffset CompletedUtc, BridgeReply Reply);
    private sealed record ActivePaintObservation(bool Observed, string Stage, int Step);
    private sealed record CancelTimedReply(TimedReply TimedReply, ActivePaintObservation? ActiveObservation);
    private sealed record PaintProgressSample(
        DateTimeOffset CapturedUtc,
        string Stage,
        bool Terminal,
        int Step,
        int Total,
        double PaintEtaMs,
        string PaintEtaSource);

    public static bool IsRequested(string[] args) =>
        args.Any(arg => string.Equals(arg, "--research-replication", StringComparison.Ordinal));

    public static async Task<int> RunAsync(string[] args)
    {
        Options options;
        try
        {
            options = Parse(args);
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("Research runner arguments: " + ex.Message);
            return 2;
        }

        if (!ResearchArtifactsEnabled())
        {
            Console.Error.WriteLine("Research runner requires MECCHA_RESEARCH_ARTIFACTS=1.");
            return 2;
        }

        Directory.CreateDirectory(options.ArtifactDirectory);
        var runDirectory = Path.Combine(
            options.ArtifactDirectory,
            "run-" + DateTimeOffset.UtcNow.ToString("yyyyMMddTHHmmssfffZ", CultureInfo.InvariantCulture) + "-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(runDirectory);
        var eventWatchPath = Path.Combine(runDirectory, "eventwatch-live.json");
        var summary = new Dictionary<string, object?>
        {
            ["runner"] = "research-replication",
            ["started_utc"] = DateTimeOffset.UtcNow,
            ["declared_role"] = options.DeclaredRole,
            ["pid"] = options.ProcessId,
            ["paint_requested"] = options.Paint,
            ["preview_only"] = options.PreviewOnly,
            ["unpreview_only"] = options.UnPreviewOnly,
            ["hold_seconds"] = options.HoldSeconds,
            ["pressure_sample_ms"] = options.PressureSampleMs,
            ["texture_snapshot_requested"] = options.TextureSnapshot,
            ["texture_sample_ms"] = options.TextureSampleMs,
            ["research_texture_target"] = options.TextureTarget.ToString(),
            ["texture_discovery_seconds"] = options.TextureDiscoverySeconds,
            ["stroke_limit"] = options.StrokeLimit,
            ["queue_target"] = options.QueueTarget,
            ["replay_stroke_index"] = options.ReplayStrokeIndex,
            ["target_channel_override"] = options.TargetChannelOverride,
            ["apply_mode_override"] = options.ApplyModeOverride,
            ["fill_color_override"] = options.FillColorOverride?.ToHex(),
            ["paint_color_override"] = options.PaintColorOverride?.ToHex(),
            ["metallic_override"] = options.MetallicOverride,
            ["roughness_override"] = options.RoughnessOverride,
            ["emissive_override"] = options.EmissiveOverride,
            ["fill_metallic_override"] = options.FillMetallicOverride,
            ["fill_roughness_override"] = options.FillRoughnessOverride,
            ["fill_emissive_override"] = options.FillEmissiveOverride,
            ["front_region_mode_override"] = options.FrontRegionModeOverride is RegionMode frontMode
                ? SettingsStore.RegionModeText(frontMode)
                : null,
            ["side_region_mode_override"] = options.SideRegionModeOverride is RegionMode sideMode
                ? SettingsStore.RegionModeText(sideMode)
                : null,
            ["back_region_mode_override"] = options.BackRegionModeOverride is RegionMode backMode
                ? SettingsStore.RegionModeText(backMode)
                : null,
            ["artifact_directory"] = runDirectory,
            ["eventwatch_path"] = eventWatchPath,
            ["success"] = false
        };
        if (options.CancelAfterMs is int configuredCancelAfterMs)
        {
            summary["cancel_after_ms"] = configuredCancelAfterMs;
            summary["cancel_requested"] = true;
        }
        if (options.CancelWhenActive)
        {
            summary["cancel_when_active"] = true;
            summary["cancel_requested"] = true;
        }
        if (options.ShutdownAfterMs is int configuredShutdownAfterMs)
        {
            summary["shutdown_after_ms"] = configuredShutdownAfterMs;
            summary["shutdown_during_paint_requested"] = true;
        }
        WriteJson(Path.Combine(runDirectory, "run-start.json"), summary);

        var session = new HostSession(VersionInfo.Current);
        if (options.FillColorOverride is not null)
            session.Settings.Paint.FillColor = options.FillColorOverride;
        if (options.MetallicOverride is double metallicOverride)
            session.Settings.Paint.Metallic = metallicOverride;
        if (options.RoughnessOverride is double roughnessOverride)
            session.Settings.Paint.Roughness = roughnessOverride;
        if (options.EmissiveOverride is double emissiveOverride)
            session.Settings.Paint.Emissive = emissiveOverride;
        if (options.FillMetallicOverride is double fillMetallicOverride)
            session.Settings.Paint.FillMetallic = fillMetallicOverride;
        if (options.FillRoughnessOverride is double fillRoughnessOverride)
            session.Settings.Paint.FillRoughness = fillRoughnessOverride;
        if (options.FillEmissiveOverride is double fillEmissiveOverride)
            session.Settings.Paint.FillEmissive = fillEmissiveOverride;
        if (options.FrontRegionModeOverride is RegionMode frontRegionMode)
            session.Settings.Paint.FrontRegionMode = frontRegionMode;
        if (options.SideRegionModeOverride is RegionMode sideRegionMode)
            session.Settings.Paint.SideRegionMode = sideRegionMode;
        if (options.BackRegionModeOverride is RegionMode backRegionMode)
            session.Settings.Paint.BackRegionMode = backRegionMode;
        var exitCode = 1;
        var shutdownDuringPaintAttempted = false;
        var shutdownDuringPaintEventWatchChecked = false;
        var shutdownDuringPaintEventWatchStopped = false;
        try
        {
            using var process = Process.GetProcessById(options.ProcessId);
            if (process.HasExited)
                throw new InvalidOperationException("The requested target process already exited.");
            var executablePath = process.MainModule?.FileName;
            if (string.IsNullOrWhiteSpace(executablePath))
                throw new InvalidOperationException("The requested target executable path is unavailable.");

            summary["game_executable"] = executablePath;
            summary["game_file_bytes"] = new FileInfo(executablePath).Length;
            summary["game_file_version"] = FileVersionInfo.GetVersionInfo(executablePath).FileVersion;
            summary["controller_version"] = VersionInfo.Current;
            summary["paint_settings"] = PaintSettingsArtifact(session.Settings);

            var ready = await session.Runtime.EnsureResearchReadyAsync(
                process.Id,
                new ResearchBridgeOptions(eventWatchPath));
            summary["bridge_ready"] = ready;
            if (!ready)
            {
                // Preserve the failed start as evidence. The shared generation manager has already
                // authenticated and quiesced any obsolete resident, or failed closed before this
                // research runner can issue a probe or paint request.
                summary["bridge_start_failed"] = true;
                throw new InvalidOperationException("The authenticated research bridge did not become ready.");
            }

            var bridgeIdentity = session.Runtime.ActiveResearchBridgeIdentity
                ?? throw new InvalidOperationException("Research bridge identity was unavailable after successful startup.");
            summary["bridge_instance_id"] = bridgeIdentity.InstanceId.ToString("N");
            summary["bridge_hash"] = bridgeIdentity.BridgeHash;
            summary["runtime_bundle_id"] = bridgeIdentity.RuntimeBundleId;
            summary["staged_bridge_path"] = bridgeIdentity.BridgePath;

            var eventWatch = await WaitForEventWatchAsync(eventWatchPath, bridgeIdentity, TimeSpan.FromSeconds(10));
            SnapshotEventWatch(eventWatchPath, Path.Combine(runDirectory, "eventwatch-start.json"));
            summary["eventwatch_ready"] = eventWatch.Ready;
            summary["eventwatch_stage"] = eventWatch.Stage;
            if (!eventWatch.Ready)
                throw new InvalidOperationException("Event-watch did not start with hooks and PaintAtUVWithBrush resolved.");

            await CaptureProbeAsync(session, ResearchProbeKind.Replication, "replication-before", runDirectory);
            await CaptureProbeAsync(session, ResearchProbeKind.ReplicationPressure, "pressure-before", runDirectory);
            string? textureExpectedComponent = null;
            if (options.TextureSnapshot)
            {
                if (options.TextureTarget == ResearchTextureTarget.EventwatchDirectReceiver)
                {
                    var discovery = await WaitForDirectReceiverAsync(
                        eventWatchPath,
                        bridgeIdentity,
                        TimeSpan.FromSeconds(options.TextureDiscoverySeconds));
                    summary["texture_target_discovered"] = discovery.Observed;
                    summary["texture_target_discovery_receiver"] = discovery.Component;
                    summary["texture_target_discovery_calls"] = discovery.Calls;
                    summary["texture_target_discovery_eventwatch_entry"] = "PaintAtUVWithBrush";
                    SnapshotEventWatch(eventWatchPath, Path.Combine(runDirectory, "eventwatch-texture-target-discovered.json"));
                    if (!discovery.Observed)
                    {
                        throw new InvalidOperationException(
                            "No remote PaintAtUVWithBrush receiver was observed during texture discovery.");
                    }
                    textureExpectedComponent = discovery.Component;
                    summary["texture_target_expected_component"] = textureExpectedComponent;
                }
                await CaptureProbeAsync(
                    session,
                    ResearchProbeKind.ReplicationTexture,
                    "texture-before",
                    runDirectory,
                    options.TextureTarget,
                    textureExpectedComponent);
            }

            if (options.Paint)
            {
                var payload = BridgePayloadBuilder.BuildPaintPayload(
                    session.Settings,
                    process.Id,
                    Path.GetFileName(executablePath),
                    new PaintRequestOptions(options.PreviewOnly, options.UnPreviewOnly, ResearchArtifacts: true));
                payload = AddResearchPaintControls(payload, options);
                WriteJson(
                    Path.Combine(runDirectory, "paint-request.json"),
                    new
                    {
                        declared_role = options.DeclaredRole,
                        requested_utc = DateTimeOffset.UtcNow,
                        settings = PaintSettingsArtifact(session.Settings),
                        payload
                });
                var paintTask = SendPaintWithTimingAsync(session.Runtime, payload);
                var progressTimelineTask = CapturePaintProgressTimelineAsync(session.Runtime.ProgressPath, paintTask);
                Task<CancelTimedReply>? cancelTask = options.CancelAfterMs is int cancelAfterMs
                    ? CancelPaintAfterDelayAsync(session.Runtime, cancelAfterMs, paintTask)
                    : options.CancelWhenActive
                        ? CancelPaintWhenActiveAsync(session.Runtime, session.Runtime.ProgressPath, paintTask)
                        : null;
                Task<TimedReply>? shutdownTask = options.ShutdownAfterMs is int shutdownAfterMs
                    ? ShutdownAfterDelayAsync(session.Runtime, shutdownAfterMs)
                    : null;
                shutdownDuringPaintAttempted = shutdownTask is not null;
                if (cancelTask is not null)
                    await Task.WhenAll(paintTask, cancelTask);
                else if (shutdownTask is not null)
                    await Task.WhenAll(paintTask, shutdownTask);
                var paint = await paintTask;
                var reply = paint.Reply;
                var progressTimeline = await progressTimelineTask;
                WriteJson(Path.Combine(runDirectory, "paint-progress-timeline.json"), progressTimeline);
                summary["paint_progress_sample_count"] = progressTimeline.Count;
                summary["paint_progress_peak_eta_ms"] = progressTimeline.Count == 0
                    ? null
                    : progressTimeline.Max(sample => sample.PaintEtaMs);
                summary["paint_progress_eta_sources"] = progressTimeline
                    .Select(sample => sample.PaintEtaSource)
                    .Where(source => !string.IsNullOrWhiteSpace(source))
                    .Distinct(StringComparer.Ordinal)
                    .ToArray();
                WriteJson(
                    Path.Combine(runDirectory, "paint-reply.json"),
                    new ReplyArtifact("paint", paint.StartedUtc, paint.CompletedUtc, reply));
                summary["paint_success"] = reply.Success;
                summary["paint_stage"] = reply.Stage;
                if (options.PreviewOnly && reply.Ok && reply.Success)
                {
                    // This runner creates a short-lived bridge.  The preview snapshot lives in
                    // that bridge, so ending it before restoring the material leaves the game
                    // looking previewed with no snapshot left to undo it.  Restore on the same
                    // bridge before normal runner shutdown and retain the reply as evidence.
                    summary["preview_cleanup_requested"] = true;
                    var cleanupPayload = BridgePayloadBuilder.BuildPaintPayload(
                        session.Settings,
                        process.Id,
                        Path.GetFileName(executablePath),
                        new PaintRequestOptions(UnPreviewOnly: true, ResearchArtifacts: true));
                    var cleanup = await SendPaintWithTimingAsync(session.Runtime, cleanupPayload);
                    WriteJson(
                        Path.Combine(runDirectory, "preview-cleanup-reply.json"),
                        new ReplyArtifact("preview-cleanup", cleanup.StartedUtc, cleanup.CompletedUtc, cleanup.Reply));
                    summary["preview_cleanup_reply"] = ReplySummary(cleanup.Reply);
                    summary["preview_cleanup_success"] = cleanup.Reply.Ok && cleanup.Reply.Success;
                    if (!cleanup.Reply.Ok || !cleanup.Reply.Success)
                    {
                        throw new InvalidOperationException(
                            $"Preview cleanup failed at {cleanup.Reply.Stage}: {cleanup.Reply.Message}");
                    }
                }
                if (reply.Ok && reply.Success && !options.PreviewOnly && !options.UnPreviewOnly)
                {
                    var uvReplayArtifact = ResearchUvReplayArtifacts.StageAndRender(reply, runDirectory);
                    summary["uv_replay_plan_written"] = uvReplayArtifact.Success;
                    summary["uv_replay_plan_path"] = uvReplayArtifact.PlanPath;
                    summary["uv_replay_atlas_path"] = uvReplayArtifact.AtlasPath;
                    if (!uvReplayArtifact.Success)
                    {
                        summary["uv_replay_artifact_error"] = uvReplayArtifact.Error;
                        // The replay sidecar is diagnostic evidence only. Its absence must not
                        // turn an otherwise completed paint into a false-negative cross-client
                        // transport result (notably when a sandbox blocks the sidecar write).
                        summary["uv_replay_plan_disposition"] = "unavailable_diagnostic_sidecar";
                    }
                }
                else if (reply.Ok && reply.Success)
                {
                    summary["uv_replay_plan_written"] = false;
                    summary["uv_replay_plan_disposition"] = "not_applicable_preview_operation";
                }
                else
                {
                    // Native writes this sidecar at planning time. A failed or cancelled paint
                    // must not leave a planned path that looks like a rendered result.
                    summary["uv_replay_plan_written"] = false;
                    summary["uv_replay_plan_disposition"] = "not_staged_non_successful_paint";
                }
                if (cancelTask is not null)
                {
                    var timedCancel = await cancelTask;
                    var cancel = timedCancel.TimedReply;
                    WriteJson(
                        Path.Combine(runDirectory, "cancel-paint-reply.json"),
                        new ReplyArtifact("cancel-paint", cancel.StartedUtc, cancel.CompletedUtc, cancel.Reply));
                    var cancelledJobs = HostSession.CancelledPaintJobCount(cancel.Reply);
                    var cancelAdmissionLatched = HostSession.NativePaintRequestCancellationLatched(cancel.Reply);
                    var paintCancelled = IsPaintCancellationReply(reply);
                    summary["cancel_reply"] = ReplySummary(cancel.Reply);
                    summary["cancelled_paint_jobs"] = cancelledJobs;
                    summary["cancel_admission_latched"] = cancelAdmissionLatched;
                    summary["paint_terminal_result"] = ReplySummary(reply);
                    summary["paint_cancel_observed"] = paintCancelled;
                    if (timedCancel.ActiveObservation is ActivePaintObservation activeObservation)
                    {
                        summary["cancel_active_progress_observed"] = activeObservation.Observed;
                        summary["cancel_active_progress_stage"] = activeObservation.Stage;
                        summary["cancel_active_progress_step"] = activeObservation.Step;
                    }
                    if (!cancel.Reply.Ok || !cancel.Reply.Success)
                    {
                        throw new InvalidOperationException(
                            $"Scheduled paint cancel request failed at {cancel.Reply.Stage}: {cancel.Reply.Message}");
                    }
                    if (cancelledJobs is null || (cancelledJobs <= 0 && !cancelAdmissionLatched))
                    {
                        throw new InvalidOperationException(
                            "Scheduled paint cancel did not reach or latch the active native paint request.");
                    }
                    if (!paintCancelled)
                    {
                        throw new InvalidOperationException(
                            $"Scheduled paint cancel did not produce a cancellation terminal result at {reply.Stage}: {reply.Message}");
                    }
                }
                else if (shutdownTask is not null)
                {
                    var shutdown = await shutdownTask;
                    WriteJson(
                        Path.Combine(runDirectory, "shutdown-during-paint-reply.json"),
                        new ReplyArtifact("shutdown-during-paint", shutdown.StartedUtc, shutdown.CompletedUtc, shutdown.Reply));
                    var activePaintQuiescent = ReplyMetadataBool(shutdown.Reply, "active_paint_quiescent");
                    var paintRequestWasInProgress = ReplyMetadataBool(shutdown.Reply, "paint_request_was_in_progress");
                    var paintCancelled = IsPaintCancellationReply(reply);
                    shutdownDuringPaintEventWatchStopped =
                        await WaitForEventWatchStoppedAsync(eventWatchPath, TimeSpan.FromSeconds(3));
                    shutdownDuringPaintEventWatchChecked = true;
                    SnapshotEventWatch(
                        eventWatchPath,
                        Path.Combine(runDirectory, "eventwatch-stopped-during-paint.json"));
                    summary["shutdown_during_paint_reply"] = ReplySummary(shutdown.Reply);
                    summary["shutdown_success"] = shutdown.Reply.Ok && shutdown.Reply.Success;
                    summary["active_paint_quiescent"] = activePaintQuiescent;
                    summary["paint_request_was_in_progress"] = paintRequestWasInProgress;
                    summary["paint_terminal_result"] = ReplySummary(reply);
                    summary["paint_cancel_observed"] = paintCancelled;
                    summary["eventwatch_stopped"] = shutdownDuringPaintEventWatchStopped;
                    if (!shutdown.Reply.Ok || !shutdown.Reply.Success)
                    {
                        throw new InvalidOperationException(
                            $"Scheduled bridge shutdown failed at {shutdown.Reply.Stage}: {shutdown.Reply.Message}");
                    }
                    if (activePaintQuiescent != true)
                    {
                        throw new InvalidOperationException(
                            "Scheduled bridge shutdown did not prove that active paint was quiescent.");
                    }
                    if (!paintCancelled)
                    {
                        throw new InvalidOperationException(
                            $"Scheduled bridge shutdown did not produce a paint cancellation terminal result at {reply.Stage}: {reply.Message}");
                    }
                    if (!shutdownDuringPaintEventWatchStopped)
                    {
                        throw new InvalidOperationException(
                            "Scheduled bridge shutdown did not stop event-watch within the observation window.");
                    }
                }
                else if (!reply.Success)
                    throw new InvalidOperationException("Normal direct paint did not complete: " + reply.Message);
            }

            if (options.ShutdownAfterMs is null)
            {
                await HoldWithPressureSamplesAsync(
                    session,
                    options,
                    runDirectory,
                    textureExpectedComponent);

                SnapshotEventWatch(eventWatchPath, Path.Combine(runDirectory, "eventwatch-before-post-probes.json"));
                await CaptureProbeAsync(session, ResearchProbeKind.ReplicationPressure, "pressure-after", runDirectory);
                await CaptureProbeAsync(session, ResearchProbeKind.Replication, "replication-after", runDirectory);
                if (options.TextureSnapshot)
                {
                    await CaptureProbeAsync(
                        session,
                        ResearchProbeKind.ReplicationTexture,
                        "texture-after",
                        runDirectory,
                        options.TextureTarget,
                        textureExpectedComponent);
                    if (options.TextureTarget == ResearchTextureTarget.AllRuntimePaintComponents)
                    {
                        // This passive receiver probe intentionally exports every eligible
                        // RuntimePaintableComponent. Keep its raw before/after inventories as
                        // the evidence instead of rendering a misleading single-target PNG.
                        summary["texture_inventory_scope"] = "all_runtime_paint_components";
                        summary["texture_inventory_delta_artifact"] = "raw_before_after_only";
                    }
                    else
                    {
                        var textureDeltaArtifact = ResearchTextureDeltaArtifacts.StageAndRender(
                            Path.Combine(runDirectory, "texture-before.json"),
                            Path.Combine(runDirectory, "texture-after.json"),
                            runDirectory,
                            textureExpectedComponent);
                        summary["albedo_delta_png_written"] = textureDeltaArtifact.Success;
                        summary["albedo_delta_texture_size"] = textureDeltaArtifact.TextureSize;
                        summary["albedo_delta_changed_pixels"] = textureDeltaArtifact.ChangedPixels;
                        summary["albedo_delta_before_png_path"] = textureDeltaArtifact.BeforePngPath;
                        summary["albedo_delta_after_png_path"] = textureDeltaArtifact.AfterPngPath;
                        summary["albedo_delta_mask_path"] = textureDeltaArtifact.DeltaMaskPath;
                        summary["albedo_delta_component"] = textureDeltaArtifact.Component;
                        summary["albedo_delta_target_source"] = textureDeltaArtifact.TargetSource;
                        if (!textureDeltaArtifact.Success)
                        {
                            summary["albedo_delta_artifact_error"] = textureDeltaArtifact.Error;
                            throw new InvalidOperationException(
                                "Requested albedo texture delta is incomplete: " + textureDeltaArtifact.Error);
                        }
                    }
                }
                SnapshotEventWatch(eventWatchPath, Path.Combine(runDirectory, "eventwatch-final.json"));
            }

            summary["success"] = true;
            exitCode = 0;
        }
        catch (Exception ex)
        {
            summary["error"] = ex.ToString();
            WriteJson(Path.Combine(runDirectory, "error.json"), new { failed_utc = DateTimeOffset.UtcNow, error = ex.ToString() });
        }
        finally
        {
            try
            {
                if (shutdownDuringPaintAttempted)
                {
                    // The scheduled shutdown is the operation under test. Never hide an
                    // indeterminate result by automatically issuing a second shutdown.
                    summary["normal_shutdown_skipped"] = true;
                    if (!shutdownDuringPaintEventWatchChecked)
                    {
                        shutdownDuringPaintEventWatchStopped =
                            await WaitForEventWatchStoppedAsync(eventWatchPath, TimeSpan.FromSeconds(3));
                        shutdownDuringPaintEventWatchChecked = true;
                        SnapshotEventWatch(
                            eventWatchPath,
                            Path.Combine(runDirectory, "eventwatch-stopped-during-paint.json"));
                        summary["eventwatch_stopped"] = shutdownDuringPaintEventWatchStopped;
                    }
                    if (!shutdownDuringPaintEventWatchStopped)
                    {
                        summary["eventwatch_cleanup_failed"] = true;
                        summary["success"] = false;
                        exitCode = 1;
                    }
                }
                else if (session.Runtime.HasActiveBridgeInstance)
                {
                    var started = DateTimeOffset.UtcNow;
                    var reply = await session.Runtime.ShutdownAsync();
                    WriteJson(
                        Path.Combine(runDirectory, "shutdown-reply.json"),
                        new ReplyArtifact("shutdown", started, DateTimeOffset.UtcNow, reply));
                    var eventWatchStopped = await WaitForEventWatchStoppedAsync(eventWatchPath, TimeSpan.FromSeconds(3));
                    SnapshotEventWatch(eventWatchPath, Path.Combine(runDirectory, "eventwatch-stopped.json"));
                    summary["shutdown_success"] = reply.Ok && reply.Success;
                    summary["eventwatch_stopped"] = eventWatchStopped;
                    if (!reply.Ok || !reply.Success || !eventWatchStopped)
                    {
                        summary["eventwatch_cleanup_failed"] = true;
                        summary["success"] = false;
                        exitCode = 1;
                    }
                }
            }
            catch (Exception ex)
            {
                summary["shutdown_error"] = ex.ToString();
                summary["eventwatch_cleanup_failed"] = true;
                summary["success"] = false;
                exitCode = 1;
            }

            summary["completed_utc"] = DateTimeOffset.UtcNow;
            summary["controller_log"] = session.Log.Text;
            WriteJson(Path.Combine(runDirectory, "run-summary.json"), summary);
        }

        return exitCode;
    }

    private static async Task<BridgeReply> CaptureProbeAsync(
        HostSession session,
        ResearchProbeKind kind,
        string name,
        string artifactDirectory,
        ResearchTextureTarget textureTarget = ResearchTextureTarget.ResolvedComponent,
        string? expectedTextureComponent = null,
        bool preserveTextureBaseline = false)
    {
        var started = DateTimeOffset.UtcNow;
        var reply = await session.Runtime.SendResearchProbeAsync(
            kind,
            textureTarget,
            expectedTextureComponent,
            preserveTextureBaseline);
        WriteJson(Path.Combine(artifactDirectory, name + ".json"), new ReplyArtifact(name, started, DateTimeOffset.UtcNow, reply));
        if (!reply.Ok || !reply.Success)
            throw new InvalidOperationException($"Research probe {name} failed: {reply.Message}");
        if (kind == ResearchProbeKind.ReplicationTexture && !string.IsNullOrWhiteSpace(expectedTextureComponent))
            VerifyTextureProbeTarget(reply, expectedTextureComponent);
        return reply;
    }

    private static void VerifyTextureProbeTarget(BridgeReply reply, string expectedTextureComponent)
    {
        using var document = JsonDocument.Parse(reply.Raw);
        if (!document.RootElement.TryGetProperty("metadata", out var metadata) ||
            !metadata.TryGetProperty("research_texture_export_target_component", out var component) ||
            component.ValueKind != JsonValueKind.String ||
            !string.Equals(component.GetString(), expectedTextureComponent, StringComparison.OrdinalIgnoreCase) ||
            !metadata.TryGetProperty("research_texture_export_target_expected_component", out var echoedExpected) ||
            echoedExpected.ValueKind != JsonValueKind.String ||
            !string.Equals(echoedExpected.GetString(), expectedTextureComponent, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException(
                "Research texture probe did not retain the receiver selected during discovery.");
        }
    }

    private static async Task<TimedReply> SendPaintWithTimingAsync(
        RuntimeBridgeService runtime,
        string payload)
    {
        var started = DateTimeOffset.UtcNow;
        var reply = await runtime.SendPaintAsync(payload);
        return new TimedReply(started, DateTimeOffset.UtcNow, reply);
    }

    private static async Task<CancelTimedReply> CancelPaintAfterDelayAsync(
        RuntimeBridgeService runtime,
        int delayMs,
        Task paintTask)
    {
        await Task.Delay(delayMs);
        return new CancelTimedReply(await CancelPaintAsync(runtime, paintTask), null);
    }

    private static async Task<CancelTimedReply> CancelPaintWhenActiveAsync(
        RuntimeBridgeService runtime,
        string progressPath,
        Task paintTask)
    {
        var observation = await WaitForActivePaintProgressAsync(progressPath, paintTask, TimeSpan.FromSeconds(20));
        if (!observation.Observed)
        {
            throw new InvalidOperationException(
                $"Active paint progress was not observed before cancellation (last stage {observation.Stage}, step {observation.Step}).");
        }
        return new CancelTimedReply(await CancelPaintAsync(runtime, paintTask), observation);
    }

    private static async Task<TimedReply> CancelPaintAsync(
        RuntimeBridgeService runtime,
        Task paintTask)
    {
        var started = DateTimeOffset.UtcNow;
        var reply = await runtime.CancelPaintAsync();
        for (var attempt = 0;
             attempt < 40 &&
             !paintTask.IsCompleted &&
             reply.Success &&
             HostSession.CancelledPaintJobCount(reply) is 0 &&
             !HostSession.NativePaintRequestCancellationLatched(reply);
             ++attempt)
        {
            await Task.Delay(25);
            reply = await runtime.CancelPaintAsync();
        }
        return new TimedReply(started, DateTimeOffset.UtcNow, reply);
    }

    private static async Task<ActivePaintObservation> WaitForActivePaintProgressAsync(
        string progressPath,
        Task paintTask,
        TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        var stage = "missing";
        var step = 0;
        while (DateTimeOffset.UtcNow < deadline && !paintTask.IsCompleted)
        {
            if (TryReadEventWatch(progressPath, out var document))
            {
                using (document)
                {
                    var root = document.RootElement;
                    stage = ReadStage(root);
                    step = root.TryGetProperty("step", out var stepElement) && stepElement.TryGetInt32(out var parsedStep)
                        ? parsedStep
                        : 0;
                    var terminal = root.TryGetProperty("terminal", out var terminalElement) &&
                                   terminalElement.ValueKind is JsonValueKind.True or JsonValueKind.False &&
                                   terminalElement.GetBoolean();
                    // A nonzero completed count during direct paint proves the async job has
                    // left planner admission and entered the game-owned paint queue.
                    if (!terminal && string.Equals(stage, "mesh_direct_paint", StringComparison.Ordinal) && step > 0)
                        return new ActivePaintObservation(true, stage, step);
                }
            }
            await Task.Delay(10);
        }
        return new ActivePaintObservation(false, paintTask.IsCompleted ? "paint_completed" : stage, step);
    }

    private static async Task<IReadOnlyList<PaintProgressSample>> CapturePaintProgressTimelineAsync(
        string progressPath,
        Task paintTask)
    {
        var samples = new List<PaintProgressSample>();
        while (!paintTask.IsCompleted)
        {
            if (TryReadEventWatch(progressPath, out var document))
            {
                using (document)
                {
                    var root = document.RootElement;
                    var stage = ReadStage(root);
                    var terminal = root.TryGetProperty("terminal", out var terminalElement) &&
                                   terminalElement.ValueKind is JsonValueKind.True or JsonValueKind.False &&
                                   terminalElement.GetBoolean();
                    var step = root.TryGetProperty("step", out var stepElement) && stepElement.TryGetInt32(out var parsedStep)
                        ? parsedStep
                        : 0;
                    var total = root.TryGetProperty("total_steps", out var totalElement) && totalElement.TryGetInt32(out var parsedTotal)
                        ? parsedTotal
                        : 0;
                    var eta = root.TryGetProperty("paint_eta_ms", out var etaElement) && etaElement.TryGetDouble(out var parsedEta)
                        ? parsedEta
                        : -1.0;
                    var etaSource = root.TryGetProperty("paint_eta_source", out var etaSourceElement) &&
                                    etaSourceElement.ValueKind == JsonValueKind.String
                        ? etaSourceElement.GetString() ?? ""
                        : "";
                    var sample = new PaintProgressSample(
                        DateTimeOffset.UtcNow, stage, terminal, step, total, eta, etaSource);
                    if (samples.Count == 0 ||
                        samples[^1].Stage != sample.Stage ||
                        samples[^1].Terminal != sample.Terminal ||
                        samples[^1].Step != sample.Step ||
                        Math.Abs(samples[^1].PaintEtaMs - sample.PaintEtaMs) >= 1.0 ||
                        samples[^1].PaintEtaSource != sample.PaintEtaSource)
                    {
                        samples.Add(sample);
                    }
                }
            }
            await Task.Delay(10);
        }
        return samples;
    }

    private static async Task<TimedReply> ShutdownAfterDelayAsync(
        RuntimeBridgeService runtime,
        int delayMs)
    {
        await Task.Delay(delayMs);
        var started = DateTimeOffset.UtcNow;
        var reply = await runtime.ShutdownAsync();
        return new TimedReply(started, DateTimeOffset.UtcNow, reply);
    }

    private static object ReplySummary(BridgeReply reply) => new
    {
        ok = reply.Ok,
        success = reply.Success,
        stage = reply.Stage,
        message = reply.Message
    };

    private static bool IsPaintCancellationReply(BridgeReply reply) =>
        reply.Ok &&
        !reply.Success &&
        (reply.Stage.Contains("cancel", StringComparison.OrdinalIgnoreCase) ||
         reply.Message.Contains("paint cancelled", StringComparison.OrdinalIgnoreCase) ||
         reply.Message.Contains("paint canceled", StringComparison.OrdinalIgnoreCase));

    private static bool? ReplyMetadataBool(BridgeReply reply, string name)
    {
        if (string.IsNullOrWhiteSpace(reply.Raw))
            return null;
        try
        {
            using var document = JsonDocument.Parse(reply.Raw);
            if (!document.RootElement.TryGetProperty("metadata", out var metadata) ||
                metadata.ValueKind != JsonValueKind.Object ||
                !metadata.TryGetProperty(name, out var value) ||
                value.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
            {
                return null;
            }
            return value.GetBoolean();
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static async Task HoldWithPressureSamplesAsync(
        HostSession session,
        Options options,
        string artifactDirectory,
        string? textureExpectedComponent)
    {
        if (options.HoldSeconds <= 0)
            return;
        if (options.PressureSampleMs <= 0 && options.TextureSampleMs <= 0)
        {
            await Task.Delay(TimeSpan.FromSeconds(options.HoldSeconds));
            return;
        }

        var pressureSample = 0;
        var textureSample = 0;
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(options.HoldSeconds);
        var nextPressure = options.PressureSampleMs > 0
            ? DateTimeOffset.UtcNow + TimeSpan.FromMilliseconds(options.PressureSampleMs)
            : DateTimeOffset.MaxValue;
        var nextTexture = options.TextureSampleMs > 0
            ? DateTimeOffset.UtcNow + TimeSpan.FromMilliseconds(options.TextureSampleMs)
            : DateTimeOffset.MaxValue;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var now = DateTimeOffset.UtcNow;
            var next = nextPressure < nextTexture ? nextPressure : nextTexture;
            var delay = (next < deadline ? next : deadline) - now;
            if (delay > TimeSpan.Zero)
                await Task.Delay(delay);
            now = DateTimeOffset.UtcNow;
            if (now >= deadline)
                break;
            if (now >= nextPressure)
            {
                await CaptureProbeAsync(
                    session,
                    ResearchProbeKind.ReplicationPressure,
                    $"pressure-sample-{pressureSample:D3}",
                    artifactDirectory);
                ++pressureSample;
                nextPressure = DateTimeOffset.UtcNow + TimeSpan.FromMilliseconds(options.PressureSampleMs);
            }
            if (now >= nextTexture)
            {
                await CaptureProbeAsync(
                    session,
                    ResearchProbeKind.ReplicationTexture,
                    $"texture-sample-{textureSample:D3}",
                    artifactDirectory,
                    options.TextureTarget,
                    textureExpectedComponent,
                    preserveTextureBaseline: true);
                ++textureSample;
                nextTexture = DateTimeOffset.UtcNow + TimeSpan.FromMilliseconds(options.TextureSampleMs);
            }
        }
    }

    private static async Task<(bool Ready, string Stage)> WaitForEventWatchAsync(
        string path,
        ResearchBridgeIdentity expectedIdentity,
        TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        var stage = "missing";
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (TryReadEventWatch(path, out var document))
            {
                using (document)
                {
                    stage = ReadStage(document.RootElement);
                    if (EventWatchReady(document.RootElement, expectedIdentity))
                        return (true, stage);
                }
            }
            await Task.Delay(100);
        }
        return (false, stage);
    }

    private static async Task<bool> WaitForEventWatchStoppedAsync(string path, TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (TryReadEventWatch(path, out var document))
            {
                using (document)
                {
                    if (string.Equals(ReadStage(document.RootElement), "event_watch_stopped", StringComparison.Ordinal))
                        return true;
                }
            }
            await Task.Delay(100);
        }
        return false;
    }

    private static async Task<(bool Observed, string Component, int Calls)> WaitForDirectReceiverAsync(
        string path,
        ResearchBridgeIdentity expectedIdentity,
        TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (TryReadEventWatch(path, out var document))
            {
                using (document)
                {
                    var root = document.RootElement;
                    if (EventWatchReady(root, expectedIdentity) &&
                        root.TryGetProperty("entries", out var entries) &&
                        entries.TryGetProperty("PaintAtUVWithBrush", out var receiverEntry) &&
                        receiverEntry.TryGetProperty("calls", out var callsElement) &&
                        callsElement.TryGetInt32(out var calls) && calls > 0 &&
                        receiverEntry.TryGetProperty("last_process_event_object", out var receiverElement) &&
                        receiverElement.ValueKind == JsonValueKind.String &&
                        TryNormalizeNonZeroHexAddress(receiverElement.GetString(), out var receiver))
                    {
                        return (true, receiver, calls);
                    }
                }
            }
            await Task.Delay(100);
        }
        return (false, "", 0);
    }

    private static bool TryReadEventWatch(string path, out JsonDocument document)
    {
        document = null!;
        try
        {
            if (!File.Exists(path))
                return false;
            document = JsonDocument.Parse(File.ReadAllText(path));
            return true;
        }
        catch (Exception ex) when (ex is IOException or JsonException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    private static bool EventWatchReady(JsonElement root, ResearchBridgeIdentity expectedIdentity)
    {
        if (!root.TryGetProperty("pid", out var pid) || !pid.TryGetInt32(out var watchedPid) ||
            watchedPid != expectedIdentity.ProcessId)
            return false;
        if (!root.TryGetProperty("instance_id", out var instanceId) ||
            instanceId.ValueKind != JsonValueKind.String ||
            !string.Equals(instanceId.GetString(), expectedIdentity.InstanceId.ToString("N"), StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }
        if (!root.TryGetProperty("bridge_hash", out var bridgeHash) ||
            bridgeHash.ValueKind != JsonValueKind.String ||
            !string.Equals(bridgeHash.GetString(), expectedIdentity.BridgeHash, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }
        if (!root.TryGetProperty("runtime_bundle_id", out var runtimeBundleId) ||
            runtimeBundleId.ValueKind != JsonValueKind.String ||
            !string.Equals(
                runtimeBundleId.GetString(),
                expectedIdentity.RuntimeBundleId,
                StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }
        if (!root.TryGetProperty("hook_slots", out var hooks) || !hooks.TryGetInt32(out var hookSlots) ||
            hookSlots <= 0)
            return false;
        if (!root.TryGetProperty("entries", out var entries) ||
            !entries.TryGetProperty("PaintAtUVWithBrush", out var direct) ||
            !direct.TryGetProperty("function", out var function))
        {
            return false;
        }
        return function.ValueKind == JsonValueKind.String && HasNonZeroHex(function.GetString());
    }

    private static bool HasNonZeroHex(string? value) =>
        TryNormalizeNonZeroHexAddress(value, out _);

    private static bool TryNormalizeNonZeroHexAddress(string? value, out string normalized)
    {
        normalized = "";
        if (string.IsNullOrWhiteSpace(value) || value.Length < 3 ||
            value[0] != '0' || (value[1] != 'x' && value[1] != 'X') ||
            !ulong.TryParse(value[2..], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var address) ||
            address == 0)
        {
            return false;
        }
        normalized = "0x" + address.ToString("x", CultureInfo.InvariantCulture);
        return true;
    }

    private static string ReadStage(JsonElement root) =>
        root.TryGetProperty("stage", out var stage) && stage.ValueKind == JsonValueKind.String
            ? stage.GetString() ?? "unknown"
            : "unknown";

    private static void SnapshotEventWatch(string source, string destination)
    {
        try
        {
            if (File.Exists(source))
                File.Copy(source, destination, overwrite: true);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // The live writer is best-effort. The runner's reply artifacts remain valid evidence.
        }
    }

    private static object PaintSettingsArtifact(AppSettings settings)
    {
        var paint = SettingsStore.Clamp(settings).Paint;
        return new
        {
            brush_size_texels = paint.BrushSizeTexels,
            color_compression_tolerance = paint.ColorCompressionTolerance,
            front_region_mode = SettingsStore.RegionModeText(paint.FrontRegionMode),
            side_region_mode = SettingsStore.RegionModeText(paint.SideRegionMode),
            back_region_mode = SettingsStore.RegionModeText(paint.BackRegionMode),
            fill_color = paint.FillColor.ToHex(),
            metallic = paint.Metallic,
            roughness = paint.Roughness,
            emissive = paint.Emissive,
            fill_metallic = paint.FillMetallic,
            fill_roughness = paint.FillRoughness,
            fill_emissive = paint.FillEmissive
        };
    }

    private static bool ResearchArtifactsEnabled() => BuildFeatures.ResearchArtifactsEnabled;

    private static string AddResearchPaintControls(string payload, Options options)
    {
        var root = JsonNode.Parse(payload)?.AsObject()
            ?? throw new InvalidOperationException("Normal paint payload was not a JSON object.");
        root["research_stroke_limit"] = options.StrokeLimit;
        root["research_uv_replay_atlas"] = true;
        if (options.QueueTarget is int queueTarget)
            root["research_direct_queue_target_strokes"] = queueTarget;
        if (options.ReplayStrokeIndex is int replayStrokeIndex)
            root["research_replay_stroke_index"] = replayStrokeIndex;
        if (options.TargetChannelOverride is int targetChannel)
            root["research_target_channel"] = targetChannel;
        if (options.ApplyModeOverride is int applyMode)
            root["research_apply_mode"] = applyMode;
        if (options.PaintColorOverride is RgbColor paintColor)
        {
            root["research_force_paint_color"] = true;
            root["research_paint_color_r"] = paintColor.R / 255.0;
            root["research_paint_color_g"] = paintColor.G / 255.0;
            root["research_paint_color_b"] = paintColor.B / 255.0;
        }
        return root.ToJsonString() + Environment.NewLine;
    }

    private static Options Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        var paint = false;
        var previewOnly = false;
        var unpreviewOnly = false;
        var textureSnapshot = false;
        var requested = false;
        for (var index = 0; index < args.Length; ++index)
        {
            switch (args[index])
            {
                case "--research-replication":
                    requested = true;
                    break;
                case "--paint":
                    paint = true;
                    break;
                case "--preview-only":
                    previewOnly = true;
                    break;
                case "--unpreview-only":
                    unpreviewOnly = true;
                    break;
                case "--texture-snapshot":
                    textureSnapshot = true;
                    break;
                case "--cancel-when-active":
                    values["--cancel-when-active"] = "true";
                    break;
                case "--pid":
                case "--role":
                case "--out":
                case "--hold-seconds":
                case "--pressure-sample-ms":
                case "--texture-sample-ms":
                case "--texture-target":
                case "--texture-discovery-seconds":
                case "--stroke-limit":
                case "--queue-target":
                case "--replay-stroke-index":
                case "--target-channel":
                case "--apply-mode":
                case "--cancel-after-ms":
                case "--shutdown-after-ms":
                case "--fill-color":
                case "--paint-color":
                case "--metallic":
                case "--roughness":
                case "--emissive":
                case "--fill-metallic":
                case "--fill-roughness":
                case "--fill-emissive":
                case "--front-mode":
                case "--side-mode":
                case "--back-mode":
                    if (++index >= args.Length)
                        throw new ArgumentException($"Missing value for {args[index - 1]}.");
                    values[args[index - 1]] = args[index];
                    break;
                default:
                    throw new ArgumentException("Unknown research argument: " + args[index]);
            }
        }

        if (!requested)
            throw new ArgumentException("--research-replication is required.");
        if (previewOnly && unpreviewOnly)
            throw new ArgumentException("--preview-only and --unpreview-only are mutually exclusive.");
        if ((previewOnly || unpreviewOnly) && !paint)
            throw new ArgumentException("--preview-only and --unpreview-only require --paint.");
        if (!values.TryGetValue("--pid", out var pidText) ||
            !int.TryParse(pidText, NumberStyles.None, CultureInfo.InvariantCulture, out var processId) || processId <= 0)
        {
            throw new ArgumentException("--pid must be a positive decimal process ID.");
        }
        if (!values.TryGetValue("--role", out var role) ||
            (role != "host" && role != "joining-client"))
        {
            throw new ArgumentException("--role must be host or joining-client. This is a declared test label, not a detected network role.");
        }
        if (!values.TryGetValue("--out", out var output) || string.IsNullOrWhiteSpace(output))
            throw new ArgumentException("--out is required.");

        var holdSeconds = 15;
        if (values.TryGetValue("--hold-seconds", out var holdText) &&
            (!int.TryParse(holdText, NumberStyles.None, CultureInfo.InvariantCulture, out holdSeconds) || holdSeconds < 0 || holdSeconds > 600))
        {
            throw new ArgumentException("--hold-seconds must be an integer from 0 through 600.");
        }

        var pressureSampleMs = 0;
        if (values.TryGetValue("--pressure-sample-ms", out var sampleText) &&
            (!int.TryParse(sampleText, NumberStyles.None, CultureInfo.InvariantCulture, out pressureSampleMs) ||
             (pressureSampleMs != 0 && (pressureSampleMs < 250 || pressureSampleMs > 10_000))))
        {
            throw new ArgumentException("--pressure-sample-ms must be 0 or an integer from 250 through 10000.");
        }

        var textureTarget = values.GetValueOrDefault("--texture-target", "resolved") switch
        {
            "resolved" => ResearchTextureTarget.ResolvedComponent,
            "eventwatch-direct-receiver" => ResearchTextureTarget.EventwatchDirectReceiver,
            "inventory-all" => ResearchTextureTarget.AllRuntimePaintComponents,
            _ => throw new ArgumentException(
                "--texture-target must be resolved, eventwatch-direct-receiver, or inventory-all.")
        };
        if (textureTarget != ResearchTextureTarget.ResolvedComponent && !textureSnapshot)
        {
            throw new ArgumentException("a non-default --texture-target requires --texture-snapshot.");
        }
        var textureSampleMs = 0;
        if (values.TryGetValue("--texture-sample-ms", out var textureSampleText) &&
            (!int.TryParse(textureSampleText, NumberStyles.None, CultureInfo.InvariantCulture, out textureSampleMs) ||
             (textureSampleMs != 0 && (textureSampleMs < 250 || textureSampleMs > 10_000))))
        {
            throw new ArgumentException("--texture-sample-ms must be 0 or an integer from 250 through 10000.");
        }
        if (textureSampleMs > 0 && !textureSnapshot)
            throw new ArgumentException("--texture-sample-ms requires --texture-snapshot.");
        if (textureSampleMs > 0 && holdSeconds == 0)
            throw new ArgumentException("--texture-sample-ms requires a positive --hold-seconds.");
        var textureDiscoverySeconds = 0;
        if (values.TryGetValue("--texture-discovery-seconds", out var discoveryText) &&
            (!int.TryParse(discoveryText, NumberStyles.None, CultureInfo.InvariantCulture, out textureDiscoverySeconds) ||
             textureDiscoverySeconds < 1 || textureDiscoverySeconds > 120))
        {
            throw new ArgumentException("--texture-discovery-seconds must be an integer from 1 through 120.");
        }
        if (textureDiscoverySeconds > 0 && textureTarget != ResearchTextureTarget.EventwatchDirectReceiver)
        {
            throw new ArgumentException("--texture-discovery-seconds requires eventwatch-direct-receiver.");
        }
        if (textureTarget == ResearchTextureTarget.EventwatchDirectReceiver && textureDiscoverySeconds == 0)
        {
            throw new ArgumentException("an event-watch --texture-target requires --texture-discovery-seconds.");
        }

        var strokeLimit = 0;
        if (values.TryGetValue("--stroke-limit", out var strokeLimitText) &&
            (!int.TryParse(strokeLimitText, NumberStyles.None, CultureInfo.InvariantCulture, out strokeLimit) ||
             strokeLimit < 0 || strokeLimit > 100_000))
        {
            throw new ArgumentException("--stroke-limit must be an integer from 0 through 100000; 0 means unlimited.");
        }

        int? queueTarget = null;
        if (values.TryGetValue("--queue-target", out var queueTargetText))
        {
            if (!int.TryParse(queueTargetText, NumberStyles.None, CultureInfo.InvariantCulture, out var parsedQueueTarget) ||
                parsedQueueTarget < 1 || parsedQueueTarget > 16)
            {
                throw new ArgumentException("--queue-target must be an integer from 1 through 16.");
            }
            queueTarget = parsedQueueTarget;
        }
        if (queueTarget is not null && !paint)
            throw new ArgumentException("--queue-target requires --paint.");

        int? replayStrokeIndex = null;
        if (values.TryGetValue("--replay-stroke-index", out var replayStrokeIndexText))
        {
            if (!int.TryParse(replayStrokeIndexText, NumberStyles.None, CultureInfo.InvariantCulture, out var parsedReplayStrokeIndex) ||
                parsedReplayStrokeIndex < 0 || parsedReplayStrokeIndex > 100_000)
            {
                throw new ArgumentException("--replay-stroke-index must be an integer from 0 through 100000.");
            }
            replayStrokeIndex = parsedReplayStrokeIndex;
        }
        if (replayStrokeIndex is not null && !paint)
            throw new ArgumentException("--replay-stroke-index requires --paint.");

        int? targetChannelOverride = null;
        if (values.TryGetValue("--target-channel", out var targetChannelText))
        {
            if (!int.TryParse(targetChannelText, NumberStyles.None, CultureInfo.InvariantCulture, out var parsedTargetChannel) ||
                parsedTargetChannel < 0 || parsedTargetChannel > 7)
            {
                throw new ArgumentException("--target-channel must be an integer from 0 through 7.");
            }
            targetChannelOverride = parsedTargetChannel;
        }
        if (targetChannelOverride is not null && !paint)
            throw new ArgumentException("--target-channel requires --paint.");

        int? applyModeOverride = null;
        if (values.TryGetValue("--apply-mode", out var applyModeText))
        {
            if (!int.TryParse(applyModeText, NumberStyles.None, CultureInfo.InvariantCulture, out var parsedApplyMode) ||
                parsedApplyMode < 0 || parsedApplyMode > 2)
            {
                throw new ArgumentException("--apply-mode must be 0 (override), 1 (alpha blend), or 2 (additive).");
            }
            applyModeOverride = parsedApplyMode;
        }
        if (applyModeOverride is not null && !paint)
            throw new ArgumentException("--apply-mode requires --paint.");

        int? cancelAfterMs = null;
        if (values.TryGetValue("--cancel-after-ms", out var cancelAfterText))
        {
            if (!int.TryParse(cancelAfterText, NumberStyles.None, CultureInfo.InvariantCulture, out var parsedCancelAfterMs) ||
                parsedCancelAfterMs < 1 || parsedCancelAfterMs > 60_000)
            {
                throw new ArgumentException("--cancel-after-ms must be an integer from 1 through 60000.");
            }
            cancelAfterMs = parsedCancelAfterMs;
        }

        int? shutdownAfterMs = null;
        if (values.TryGetValue("--shutdown-after-ms", out var shutdownAfterText))
        {
            if (!int.TryParse(shutdownAfterText, NumberStyles.None, CultureInfo.InvariantCulture, out var parsedShutdownAfterMs) ||
                parsedShutdownAfterMs < 1 || parsedShutdownAfterMs > 60_000)
            {
                throw new ArgumentException("--shutdown-after-ms must be an integer from 1 through 60000.");
            }
            shutdownAfterMs = parsedShutdownAfterMs;
        }

        var cancelWhenActive = values.ContainsKey("--cancel-when-active");
        if ((cancelAfterMs is not null && cancelWhenActive) ||
            (cancelAfterMs is not null && shutdownAfterMs is not null) ||
            (cancelWhenActive && shutdownAfterMs is not null))
        {
            throw new ArgumentException("--cancel-after-ms, --cancel-when-active, and --shutdown-after-ms are mutually exclusive.");
        }
        if (cancelAfterMs is not null && !paint)
            throw new ArgumentException("--cancel-after-ms requires --paint.");
        if (cancelWhenActive && !paint)
            throw new ArgumentException("--cancel-when-active requires --paint.");
        if (shutdownAfterMs is not null && !paint)
            throw new ArgumentException("--shutdown-after-ms requires --paint.");
        if (textureSnapshot && shutdownAfterMs is not null)
        {
            throw new ArgumentException(
                "--texture-snapshot cannot be combined with --shutdown-after-ms because it cannot safely capture an after image.");
        }
        RgbColor? fillColorOverride = null;
        if (values.TryGetValue("--fill-color", out var fillColorText))
        {
            if (!RgbColor.TryParse(fillColorText, out var parsedFillColor))
                throw new ArgumentException("--fill-color must be a six-digit RGB color such as #FF00AA.");
            fillColorOverride = parsedFillColor;
        }

        RgbColor? paintColorOverride = null;
        if (values.TryGetValue("--paint-color", out var paintColorText))
        {
            if (!RgbColor.TryParse(paintColorText, out var parsedPaintColor))
                throw new ArgumentException("--paint-color must be a six-digit RGB color such as #00FF00.");
            paintColorOverride = parsedPaintColor;
        }

        static double? ParseUnitIntervalOverride(
            IReadOnlyDictionary<string, string> parsedValues,
            string option)
        {
            if (!parsedValues.TryGetValue(option, out var text))
                return null;
            if (!double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) ||
                !double.IsFinite(value) || value < 0.0 || value > 1.0)
            {
                throw new ArgumentException($"{option} must be a number from 0 through 1.");
            }
            return value;
        }

        var metallicOverride = ParseUnitIntervalOverride(values, "--metallic");
        var roughnessOverride = ParseUnitIntervalOverride(values, "--roughness");
        var emissiveOverride = ParseUnitIntervalOverride(values, "--emissive");
        var fillMetallicOverride = ParseUnitIntervalOverride(values, "--fill-metallic");
        var fillRoughnessOverride = ParseUnitIntervalOverride(values, "--fill-roughness");
        var fillEmissiveOverride = ParseUnitIntervalOverride(values, "--fill-emissive");

        static RegionMode? ParseRegionModeOverride(
            IReadOnlyDictionary<string, string> parsedValues,
            string option)
        {
            if (!parsedValues.TryGetValue(option, out var text))
                return null;
            if (!Enum.TryParse<RegionMode>(text, true, out var mode))
                throw new ArgumentException($"{option} must be paint, fill, or skip.");
            return mode;
        }

        var frontRegionModeOverride = ParseRegionModeOverride(values, "--front-mode");
        var sideRegionModeOverride = ParseRegionModeOverride(values, "--side-mode");
        var backRegionModeOverride = ParseRegionModeOverride(values, "--back-mode");

        return new Options(
            processId,
            role,
            Path.GetFullPath(output),
            paint,
            previewOnly,
            unpreviewOnly,
            holdSeconds,
            pressureSampleMs,
            textureSnapshot,
            textureSampleMs,
            textureTarget,
            textureDiscoverySeconds,
            strokeLimit,
            queueTarget,
            replayStrokeIndex,
            targetChannelOverride,
            applyModeOverride,
            cancelAfterMs,
            cancelWhenActive,
            shutdownAfterMs,
            fillColorOverride,
            paintColorOverride,
            metallicOverride,
            roughnessOverride,
            emissiveOverride,
            fillMetallicOverride,
            fillRoughnessOverride,
            fillEmissiveOverride,
            frontRegionModeOverride,
            sideRegionModeOverride,
            backRegionModeOverride);
    }

    private static void WriteJson(string path, object value)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, JsonSerializer.Serialize(value, JsonOptions) + Environment.NewLine);
    }
}
