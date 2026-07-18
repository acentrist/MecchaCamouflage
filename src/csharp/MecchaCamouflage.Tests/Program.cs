using System.Text.Json;
using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Text;
using MecchaCamouflage.Controller;
using MecchaCamouflage.Core;

var tests = new List<(string Name, Action Run)>
{
    ("paint defaults expose coarse and detail brushes", PaintDefaultsExposeCoarseAndDetailBrushes),
    ("brush selection persists", BrushSelectionPersists),
    ("brush settings clamp to supported ranges", TwoPassBrushSettingsClampToSupportedRanges),
    ("paint defaults expose batch sliders", PaintDefaultsExposeBatchSliders),
    ("app defaults use 99 percent opacity", AppDefaultsUse99PercentOpacity),
    ("payload sends active brushes", PayloadSendsTwoPassBrushPipeline),
    ("native accepts the Brush 1 configured range", NativeAcceptsBrush1ConfiguredRange),
    ("native local route is signature resolved instead of build gated", NativeLocalRouteIsSignatureResolvedInsteadOfBuildGated),
    ("native manual direct route is signature resolved instead of build gated", NativeManualDirectRouteIsSignatureResolvedInsteadOfBuildGated),
    ("native local failures use fixed server packed fallback", NativeLocalFailuresUseFixedServerPackedFallback),
    ("native production radius follows each triangle and fill stays fixed", NativeProductionRadiusFollowsEachTriangleAndFillStaysFixed),
    ("native spatial replay follows the current pose and camera", NativeSpatialReplayFollowsCurrentPoseAndCamera),
    ("payload includes packed route and fill material", PayloadIncludesPackedRouteAndFillMaterial),
    ("payload sends batch slider values", PayloadSendsBatchSliderValues),
    ("pre-mode pacing preserves saved delay", PreModePacingPreservesSavedDelay),
    ("legacy auto pacing migrates to fastest sliders", LegacyAutoPacingMigratesToFastestSliders),
    ("legacy manual pacing migrates to sliders", LegacyManualPacingMigratesToSliders),
    ("legacy compatibility pacing migrates to sliders", LegacyCompatibilityPacingMigratesToSliders),
    ("settings clamp batch sliders", SettingsClampBatchSliders),
    ("locales have complete keys", LocalesHaveCompleteKeys),
    ("color parser accepts rrggbb", ColorParserAcceptsHex),
    ("runtime log keeps repeated guard messages", RuntimeLogKeepsRepeatedGuardMessages),
    ("asset validation rejects stale ready cache", AssetValidationRejectsStaleReadyCache),
    ("copy if invalid repairs corrupt target", CopyIfInvalidRepairsCorruptTarget),
    ("research event-watch sidecar uses exact staged bridge path", ResearchEventWatchSidecarUsesExactStagedBridgePath),
    ("research texture probe is explicitly dispatched", ResearchTextureProbeIsExplicitlyDispatched),
    ("native research replay plan preserves actual pass strokes", NativeResearchReplayPlanPreservesActualPassStrokes),
    ("research runner can isolate one planned replay stroke", ResearchRunnerCanIsolateOnePlannedReplayStroke),
    ("research runner records two-pass brushes and packed local queue mode", ResearchRunnerRecordsTwoPassBrushesAndPackedLocalQueueMode),
    ("UV replay atlas separates passes and packed radii", UvReplayAtlasSeparatesPassesAndPackedRadii),
    ("research replay sidecar is staged as a UV PNG", ResearchReplaySidecarIsStagedAsUvPng),
    ("research replay sidecar refuses a non-successful paint", ResearchReplaySidecarRefusesNonSuccessfulPaint),
    ("research texture probes stage an actual delta PNG", ResearchTextureProbesStageActualDeltaPng),
    ("research texture probes reject a component switch", ResearchTextureProbesRejectComponentSwitch),
    ("research texture probes reject an unexpected discovery receiver", ResearchTextureProbesRejectUnexpectedDiscoveryReceiver),
    ("diagnostic summary includes file not found details", DiagnosticSummaryIncludesFileNotFoundDetails),
    ("diagnostics log write is best effort when file is locked", DiagnosticsLogWriteIsBestEffortWhenFileLocked),
    ("runtime log write is best effort when file is locked", RuntimeLogWriteIsBestEffortWhenFileLocked),
    ("auto material defaults off", AutoMaterialDefaultsOff),
    ("front region defaults to fill", FrontRegionDefaultsToFill),
    ("bridge messages are user friendly", BridgeMessagesAreUserFriendly),
    ("paint fallback warning preserves native reason and fixed pacing", PaintFallbackWarningPreservesNativeReasonAndFixedPacing),
    ("manual direct fallback warning preserves native reason and effective pacing", ManualDirectFallbackWarningPreservesNativeReasonAndEffectivePacing),
    ("settings detect supported system language", SettingsDetectSupportedSystemLanguage),
    ("ui snapshot exposes two-pass brushes and batch sliders", UiSnapshotExposesTwoPassBrushesAndBatchSliders),
    ("web ui exposes two-pass brush sliders", WebUiExposesTwoPassBrushSliders),
    ("web UI keeps theme color on readonly range and checkbox controls", WebUiKeepsThemeColorOnReadonlyControls),
    ("web ui renders pass progress and total eta", WebUiRendersPassProgressAndTotalEta),
    ("raw hotkeys suppress repeat until key-up", RawHotkeysSuppressRepeatUntilKeyUp),
    ("raw hotkeys do not reserve system keys", RawHotkeysDoNotReserveSystemKeys),
    ("native progress exposes replay pass state", NativeProgressExposesReplayPassState),
    ("hotkey validation rejects duplicates", HotkeyValidationRejectsDuplicates),
    ("host session reset restores setting default", HostSessionResetRestoresDefault),
    ("host session brush updates are independent and detail syncs coverage", HostSessionBrushUpdatesAreIndependentAndDetailSyncsCoverage),
    ("host session rejects disabling every brush", HostSessionRejectsDisablingEveryBrush),
    ("host session updates batch sliders", HostSessionUpdatesBatchSliders),
    ("host session quantizes decimal batch slider updates", HostSessionQuantizesDecimalBatchSliderUpdates),
    ("host session rolls back invalid hotkey update", HostSessionRollsBackInvalidHotkeyUpdate),
    ("host session applies multiple setting updates atomically", HostSessionAppliesMultipleSettingUpdatesAtomically),
    ("host session rolls back duplicate hotkey batch", HostSessionRollsBackDuplicateHotkeyBatch),
    ("host session rolls back invalid fill color batch", HostSessionRollsBackInvalidFillColorBatch),
    ("host session rolls back invalid theme color batch", HostSessionRollsBackInvalidThemeColorBatch),
    ("host session rolls back invalid region mode batch", HostSessionRollsBackInvalidRegionModeBatch),
    ("host session progress candidates use bridge state", HostSessionProgressCandidatesUseBridgeState),
    ("host session does not cross bridge instances during a preferred progress write", HostSessionDoesNotFallbackWhenPreferredProgressIsMalformed),
    ("host session waits for a missing preferred progress file", HostSessionDoesNotFallbackWhenPreferredProgressIsMissing),
    ("host session does not cross bridge instances for stale preferred progress", HostSessionDoesNotFallbackWhenPreferredProgressIsStale),
    ("host session presents native pass progress and receiver queue", HostSessionPresentsNativePassProgressAndReceiverQueue),
    ("host session logs each pass transition once per job", HostSessionLogsEachPassTransitionOnce),
    ("host session snapshot ignores pre-paint progress", HostSessionSnapshotIgnoresPrePaintProgress),
    ("host session warns when cancel has no active paint", HostSessionWarnsWhenCancelHasNoActivePaint),
    ("host session pre-dispatch cancel prevents a late paint send", HostSessionPreDispatchCancelPreventsLatePaintSend),
    ("host session retries cancel across native admission", HostSessionRetriesCancelAcrossNativeAdmission),
    ("host session counts native cancel jobs", HostSessionCountsNativeCancelJobs),
    ("host session keeps cancellation pending until native terminal reply", HostSessionKeepsCancellationPendingUntilNativeTerminalReply),
    ("bridge start block has a fixed portable layout", BridgeStartBlockHasFixedPortableLayout),
    ("injector result requires matching bridge identity", InjectorResultRequiresMatchingBridgeIdentity),
    ("bridge hello serializes and validates identity", BridgeHelloSerializesAndValidatesIdentity),
    ("bridge client sends hello before the command", BridgeClientSendsHelloBeforeCommand),
    ("bridge shutdown client outlives native quiescence budget", BridgeShutdownClientOutlivesNativeQuiescenceBudget),
    ("native stop paths latch in-flight paint admission", NativeStopPathsLatchInFlightPaintAdmission),
    ("bridge shutdown permits a fresh instance", BridgeShutdownPermitsFreshInstance),
    ("stale bridge shutdown preserves a replacement instance", StaleBridgeShutdownPreservesReplacementInstance),
    ("stale bridge request preserves replacement connection state", StaleBridgeRequestPreservesReplacementConnectionState),
    ("runtime exposes exact PID bridge startup", RuntimeExposesExactPidBridgeStartup),
    ("web startup lifecycle stabilizes after navigation and ui ready", WebStartupLifecycleStabilizesAfterNavigationAndUiReady),
    ("direct bridge names avoid historical loader pattern", DirectBridgeNamesAvoidHistoricalLoaderPattern),
    ("release packaging contains only direct bridge components", ReleasePackagingContainsOnlyDirectBridge),
    ("release build excludes research runner and devtools", ReleaseBuildExcludesResearchRunnerAndDevTools)
};

var failed = 0;
foreach (var test in tests)
{
    try
    {
        test.Run();
        Console.WriteLine($"PASS {test.Name}");
    }
    catch (Exception ex)
    {
        ++failed;
        Console.Error.WriteLine($"FAIL {test.Name}: {ex.Message}");
    }
}

return failed == 0 ? 0 : 1;

static void PaintDefaultsExposeCoarseAndDetailBrushes()
{
    var paint = new AppSettings().Paint;

    Assert(!paint.Brush1Enabled, "brush 1 should default off");
    Assert(paint.Brush2Enabled, "brush 2 should default on");
    Assert(Math.Abs(paint.Brush1SizeTexels - 25.0) < 0.000001, "brush 1 should default to 25 texels");
    Assert(Math.Abs(paint.Brush2SizeTexels - 7.5) < 0.000001, "brush 2 should default to 7.5 texels");
    Assert(Math.Abs(paint.CoverageStepTexels - paint.Brush2SizeTexels) < 0.000001, "coverage compatibility should follow brush 2");
}

static void BrushSelectionPersists()
{
    using var temp = new TempHome();
    var paths = new AppPaths("brush-selection-persistence-test");
    var settings = new AppSettings();
    settings.Paint.Brush1Enabled = true;
    settings.Paint.Brush1SizeTexels = 42.5;
    settings.Paint.Brush2Enabled = false;
    settings.Paint.Brush2SizeTexels = 2.5;
    settings.Paint.BatchAutoAdapt = false;

    new SettingsStore(paths).Save(settings);
    var loaded = new SettingsStore(paths).Load();
    Assert(loaded.Paint.Brush1Enabled && !loaded.Paint.Brush2Enabled, "enabled brushes should round-trip");
    Assert(Math.Abs(loaded.Paint.Brush1SizeTexels - 42.5) < 0.000001, "brush 1 size should round-trip");
    Assert(Math.Abs(loaded.Paint.Brush2SizeTexels - 2.5) < 0.000001, "brush 2 size should round-trip");
    Assert(!loaded.Paint.BatchAutoAdapt, "batch auto adapt should round-trip");
    Assert(Math.Abs(loaded.Paint.CoverageStepTexels - 42.5) < 0.000001, "coverage should follow the only active brush");
    using var saved = JsonDocument.Parse(File.ReadAllText(paths.ConfigPath));
    Assert(saved.RootElement.GetProperty("brush_1_enabled").GetBoolean(), "brush 1 enabled should persist");
    Assert(!saved.RootElement.GetProperty("brush_2_enabled").GetBoolean(), "brush 2 enabled should persist");
    Assert(!saved.RootElement.GetProperty("batch_auto_adapt").GetBoolean(), "batch auto adapt should persist");
    Assert(!saved.RootElement.TryGetProperty("stroke_size_texels", out _), "the legacy brush key should not be persisted");
}

static void TwoPassBrushSettingsClampToSupportedRanges()
{
    var settings = new AppSettings();
    settings.Paint.Brush1SizeTexels = 5.0;
    settings.Paint.Brush2SizeTexels = 3.0;
    settings.Paint.CoverageStepTexels = 99.0;

    var clamped = SettingsStore.Clamp(settings);

    Assert(Math.Abs(clamped.Paint.Brush1SizeTexels - 10.0) < 0.000001, "brush 1 should clamp to 10 at the lower bound");
    Assert(Math.Abs(clamped.Paint.Brush2SizeTexels - 3.0) < 0.000001, "brush 2 should accept values above the new lower bound");
    Assert(Math.Abs(clamped.Paint.CoverageStepTexels - 3.0) < 0.000001, "coverage should follow the active brush 2");

    settings.Paint.Brush1SizeTexels = 35.0;
    settings.Paint.Brush2SizeTexels = 15.0;
    clamped = SettingsStore.Clamp(settings);

    Assert(Math.Abs(clamped.Paint.Brush1SizeTexels - 35.0) < 0.000001, "brush 1 should accept values below 50");
    Assert(Math.Abs(clamped.Paint.Brush2SizeTexels - 10.0) < 0.000001, "brush 2 should clamp to 10 at the upper bound");

    settings.Paint.Brush2SizeTexels = 0.5;
    settings.Paint.Brush1SizeTexels = 55.0;
    clamped = SettingsStore.Clamp(settings);
    Assert(Math.Abs(clamped.Paint.Brush1SizeTexels - 50.0) < 0.000001, "brush 1 should clamp to 50");
    Assert(Math.Abs(clamped.Paint.Brush2SizeTexels - 1.0) < 0.000001, "brush 2 should clamp to 1");
}

static void PaintDefaultsExposeBatchSliders()
{
    var paint = new AppSettings().Paint;

    Assert(paint.BatchAutoAdapt, "batch auto adapt should default on");
    Assert(paint.PackedBatchLimit == 20, "batch limit should retain its default");
    Assert(paint.PackedBatchPacingMs == 50, "batch pacing should default to the fastest safe interval");
}

static void AppDefaultsUse99PercentOpacity()
{
    using var temp = new TempHome();
    var defaults = new AppSettings();
    var loaded = new SettingsStore(new AppPaths("opacity-default-test")).Load();

    Assert(Math.Abs(defaults.Opacity - 0.99) < 0.000001, "a new app settings instance should default to 99 percent opacity");
    Assert(Math.Abs(loaded.Opacity - 0.99) < 0.000001, "a new persisted settings file should inherit the 99 percent opacity default");
}

static void PayloadSendsTwoPassBrushPipeline()
{
    var settings = new AppSettings();
    settings.Paint.Brush1SizeTexels = 17.5;
    settings.Paint.Brush2SizeTexels = 7.5;
    settings.Paint.Brush1Enabled = true;
    settings.Paint.Brush2Enabled = false;
    settings.Paint.BatchAutoAdapt = false;

    var payload = BridgePayloadBuilder.BuildPaintPayload(settings, 42, "Game.exe", new PaintRequestOptions());
    using var doc = JsonDocument.Parse(payload);
    var tuning = doc.RootElement.GetProperty("tuning");

    Assert(tuning.GetProperty("brush_1_enabled").GetBoolean(), "payload should enable brush 1");
    Assert(Math.Abs(tuning.GetProperty("brush_1_size_texels").GetDouble() - 17.5) < 0.000001, "payload should send brush 1");
    Assert(!tuning.GetProperty("brush_2_enabled").GetBoolean(), "payload should disable brush 2");
    Assert(Math.Abs(tuning.GetProperty("brush_2_size_texels").GetDouble() - 7.5) < 0.000001, "payload should send brush 2");
    Assert(!tuning.GetProperty("server_batch_auto_adapt").GetBoolean(), "payload should send the batch auto-adapt mode");
    Assert(!tuning.TryGetProperty("brush_pipeline_version", out _), "payload should not version the brush pipeline");
    Assert(!tuning.TryGetProperty("stroke_size_texels", out _), "payload should not send the legacy stroke size");
    Assert(Math.Abs(tuning.GetProperty("coverage_step_texels").GetDouble() - 17.5) < 0.000001, "coverage should follow the only active brush");
}

static void NativeAcceptsBrush1ConfiguredRange()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("json_number_field(request, \"brush_1_size_texels\", 25.0)", StringComparison.Ordinal) &&
           bridge.Contains("10.0, 50.0", StringComparison.Ordinal),
        "native paint payload parsing must preserve the configured 10-50 Brush 1 range");
}

static void NativeLocalRouteIsSignatureResolvedInsteadOfBuildGated()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    var start = bridge.LastIndexOf("auto resolve_local_packed_queue_route(", StringComparison.Ordinal);
    var end = bridge.IndexOf("auto resolve_internal_no_resend_route(", start, StringComparison.Ordinal);
    Assert(start >= 0 && end > start, "local packed route resolver should be present");
    var resolver = bridge[start..end];

    Assert(!resolver.Contains("TimeDateStamp ==", StringComparison.Ordinal) &&
           !resolver.Contains("ExpectedThunkRva", StringComparison.Ordinal) &&
           !resolver.Contains("main_module_build_identity_mismatch", StringComparison.Ordinal),
        "local route must not reject a build solely because its identity or RVAs changed");
    Assert(resolver.Contains("MulticastPackedPaintBatch_param_layout_mismatch", StringComparison.Ordinal) &&
           resolver.Contains("slot <= 0x800", StringComparison.Ordinal) &&
           resolver.Contains("candidates.size() != 1", StringComparison.Ordinal),
        "local route must retain schema validation and require one signature/call-chain candidate");
}

static void NativeManualDirectRouteIsSignatureResolvedInsteadOfBuildGated()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    var start = bridge.LastIndexOf("auto resolve_internal_no_resend_route(", StringComparison.Ordinal);
    var end = bridge.IndexOf("auto sdk_validate_internal_common_no_resend_preconditions(", start, StringComparison.Ordinal);
    Assert(start >= 0 && end > start, "manual direct resolver should be present");
    var resolver = bridge[start..end];

    Assert(!resolver.Contains("TimeDateStamp ==", StringComparison.Ordinal) &&
           !resolver.Contains("ExpectedThunkRva", StringComparison.Ordinal) &&
           !resolver.Contains("main_module_build_identity_mismatch", StringComparison.Ordinal) &&
           !resolver.Contains("main_module_text_identity_mismatch", StringComparison.Ordinal),
        "manual direct route must not reject a build solely because its identity or RVAs changed");
    Assert(resolver.Contains("PaintAtUVWithBrush_param_layout_mismatch", StringComparison.Ordinal) &&
           resolver.Contains("internal_rel32_call_target", StringComparison.Ordinal) &&
           resolver.Contains("matches.size() != 1", StringComparison.Ordinal),
        "manual direct route must retain schema, signature, relative-call, and uniqueness validation");
}

static void NativeLocalFailuresUseFixedServerPackedFallback()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    var contract = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "include", "runtime_contract.hpp"));
    var responseJson = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge_json.inc"));

    Assert(contract.Contains("ServerPackedFallbackBatchLimit = 20", StringComparison.Ordinal) &&
           contract.Contains("ServerPackedFallbackPacingMs = 50", StringComparison.Ordinal),
        "fallback pacing should be one explicit native contract");
    Assert(bridge.Contains("\\\"local_route_mode\\\":\\\"server_packed_fallback\\\"", StringComparison.Ordinal) &&
           bridge.Contains("\\\"fallback_reason\\\"", StringComparison.Ordinal) &&
           bridge.Contains("\\\"fallback_batch_limit\\\"", StringComparison.Ordinal) &&
           bridge.Contains("\\\"fallback_pacing_ms\\\"", StringComparison.Ordinal),
        "fallback replies should expose the unified metadata contract");
    Assert(responseJson.Contains("\"local_route_mode\"", StringComparison.Ordinal) &&
           responseJson.Contains("\"fallback_reason\"", StringComparison.Ordinal) &&
           responseJson.Contains("\"fallback_batch_limit\"", StringComparison.Ordinal) &&
           responseJson.Contains("\"fallback_pacing_ms\"", StringComparison.Ordinal),
        "production response compaction must retain fallback metadata for the controller warning");
    Assert(bridge.Contains("the exact local receiver queue could not be measured before any server batch was submitted", StringComparison.Ordinal) &&
           bridge.Contains("The exact local receiver queue could not be measured before the next server batch; no new batch was submitted.", StringComparison.Ordinal),
        "initial and mid-job exact queue probe failures should activate fallback with their original text");
    Assert(bridge.Contains("mesh_local_packed_queue_still_draining", StringComparison.Ordinal) &&
           bridge.Contains("component_queue_not_empty", StringComparison.Ordinal),
        "a readable nonzero previous queue must remain a blocking condition");
}

static void NativeProductionRadiusFollowsEachTriangleAndFillStaysFixed()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("const double fill_stroke_radius_texels = 100.0;", StringComparison.Ordinal) &&
           bridge.Contains("\\\"fill_stroke_radius_source\\\":\\\"fixed_100_texels\\\"", StringComparison.Ordinal),
        "fill radius should be independent from either brush");
    Assert(bridge.Contains("production_triangle_world_radius_per_stroke", StringComparison.Ordinal) &&
           bridge.Contains("runtime_contract::resolve_packed_triangle_world_radius", StringComparison.Ordinal) &&
           bridge.Contains("replay_triangle_world_radius_normalization", StringComparison.Ordinal) &&
           bridge.Contains("per_stroke", StringComparison.Ordinal) &&
           !bridge.Contains("packed_radius_calibration_failed", StringComparison.Ordinal) &&
           !bridge.Contains("packed_radius_calibration_target_mesh_mismatch", StringComparison.Ordinal),
        "production paint should derive one world radius per triangle without a calibration failure gate");
}

static void NativeSpatialReplayFollowsCurrentPoseAndCamera()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("sdk_project_world_to_screen(ref, ctx, sample.world_position", StringComparison.Ordinal) &&
           bridge.Contains("current_pose_camera_projection", StringComparison.Ordinal),
        "replay order should be derived from each current-pose world sample in the current camera");
    Assert(!bridge.Contains("profile_reference_z_desc_rows_camera_right_asc", StringComparison.Ordinal) &&
           !bridge.Contains("sample.reference_position.Z", StringComparison.Ordinal),
        "replay order must not use the mesh profile reference pose");
}

static void PayloadIncludesPackedRouteAndFillMaterial()
{
    var settings = new AppSettings();
    settings.Paint.PackedBatchLimit = 13;
    settings.Paint.PackedBatchPacingMs = 88;
    settings.Paint.FrontRegionMode = RegionMode.Fill;
    settings.Paint.SideRegionMode = RegionMode.Skip;
    settings.Paint.BackRegionMode = RegionMode.Paint;
    settings.Paint.FillColor = new RgbColor(241, 17, 17);
    settings.Paint.FillMetallic = 1.0;
    settings.Paint.FillRoughness = 0.0;

    var payload = BridgePayloadBuilder.BuildPaintPayload(settings, 42, "Game.exe", new PaintRequestOptions());
    using var doc = JsonDocument.Parse(payload);
    Assert(doc.RootElement.GetProperty("server_batch_rpc").GetString() == "packed", "payload should request packed server route");
    Assert(doc.RootElement.GetProperty("packed_route").GetString() == "component", "payload should request component packed route");
    var tuning = doc.RootElement.GetProperty("tuning");
    Assert(tuning.GetProperty("front_region_mode").GetString() == "fill", "front mode missing");
    Assert(tuning.GetProperty("side_region_mode").GetString() == "skip", "side mode missing");
    Assert(tuning.GetProperty("back_region_mode").GetString() == "paint", "back mode missing");
    Assert(tuning.GetProperty("server_batch_limit").GetInt32() == 13, "server batch limit should be sent");
    Assert(tuning.GetProperty("server_batch_pacing_ms").GetInt32() == 88, "server batch pacing should be sent");
    Assert(tuning.GetProperty("fill_color").GetString() == "#F11111", "fill color missing");
    Assert(Math.Abs(tuning.GetProperty("fill_color_r").GetDouble() - (241.0 / 255.0)) < 0.00001, "fill red not normalized");
    Assert(!tuning.TryGetProperty("enable_front_paint", out _), "legacy front bool must not be sent");
    Assert(!tuning.TryGetProperty("enable_side_paint", out _), "legacy side bool must not be sent");
    Assert(!tuning.TryGetProperty("enable_back_paint", out _), "legacy back bool must not be sent");
    Assert(!tuning.TryGetProperty("auto_material_properties", out _), "legacy material key must not be sent");
    Assert(!tuning.TryGetProperty("adaptive_batch_enabled", out _), "payload should not send legacy adaptive tuning");
    Assert(!tuning.TryGetProperty("server_pacing_mode", out _), "removed pacing mode must not be sent");
    Assert(!tuning.TryGetProperty("server_batch_delay_ms", out _), "legacy delay wording must not be sent");
}

static void LegacyAutoPacingMigratesToFastestSliders()
{
    using var temp = new TempHome();
    var paths = new AppPaths("pacing-settings-migration-test");
    Directory.CreateDirectory(paths.ConfigDirectory);
    File.WriteAllText(paths.ConfigPath, """
    {
      "layout_version": 35,
      "pacing_mode": "auto_fast",
      "packed_batch_delay_ms": 175
    }
    """);

    var settings = new SettingsStore(paths).Load();

    Assert(settings.Paint.PackedBatchLimit == 20, "auto pacing should migrate to maximum batch size");
    Assert(settings.Paint.PackedBatchPacingMs == 50, "auto pacing should migrate to fastest safe pacing");
    Assert(settings.LayoutVersion == AppSettings.CurrentLayoutVersion, "migration should advance the layout version");

    new SettingsStore(paths).Save(settings);
    using var saved = JsonDocument.Parse(File.ReadAllText(paths.ConfigPath));
    Assert(saved.RootElement.GetProperty("packed_batch_limit").GetInt32() == 20, "migrated batch limit should persist");
    Assert(saved.RootElement.GetProperty("packed_batch_pacing_ms").GetInt32() == 50, "migrated pacing should persist");
    Assert(!saved.RootElement.TryGetProperty("pacing_mode", out _), "removed pacing mode should not be persisted");
    Assert(!saved.RootElement.TryGetProperty("packed_batch_delay_ms", out _), "legacy delay wording should not be persisted");
}

static void PreModePacingPreservesSavedDelay()
{
    using var temp = new TempHome();
    foreach (var savedDelay in new[] { 75, 175 })
    {
        var paths = new AppPaths($"pre-mode-pacing-settings-migration-{savedDelay}-test");
        Directory.CreateDirectory(paths.ConfigDirectory);
        File.WriteAllText(paths.ConfigPath, $$"""
        {
          "layout_version": 35,
          "packed_batch_delay_ms": {{savedDelay}}
        }
        """);

        var settings = new SettingsStore(paths).Load();

        Assert(settings.Paint.PackedBatchLimit == 20, "pre-mode pacing should migrate to maximum batch size");
        Assert(settings.Paint.PackedBatchPacingMs == savedDelay, "pre-mode pacing should retain its saved interval");
    }
}

static void LegacyManualPacingMigratesToSliders()
{
    using var temp = new TempHome();
    var paths = new AppPaths("manual-pacing-settings-migration-test");
    Directory.CreateDirectory(paths.ConfigDirectory);
    File.WriteAllText(paths.ConfigPath, """
    {
      "layout_version": 35,
      "pacing_mode": "manual_slower",
      "packed_batch_delay_ms": 175
    }
    """);

    var settings = new SettingsStore(paths).Load();

    Assert(settings.Paint.PackedBatchLimit == 20, "manual pacing should migrate to maximum batch size");
    Assert(settings.Paint.PackedBatchPacingMs == 175, "manual pacing should retain its selected interval");
}

static void LegacyCompatibilityPacingMigratesToSliders()
{
    using var temp = new TempHome();
    var paths = new AppPaths("compatibility-pacing-settings-migration-test");
    Directory.CreateDirectory(paths.ConfigDirectory);
    File.WriteAllText(paths.ConfigPath, """
    {
      "layout_version": 35,
      "pacing_mode": "compatibility",
      "packed_batch_delay_ms": 500
    }
    """);

    var settings = new SettingsStore(paths).Load();

    Assert(settings.Paint.PackedBatchLimit == 6, "compatibility mode should migrate to six strokes per RPC");
    Assert(settings.Paint.PackedBatchPacingMs == 75, "compatibility mode should migrate to 75 ms pacing");
}

static void PayloadSendsBatchSliderValues()
{
    var settings = new AppSettings();
    settings.Paint.BatchAutoAdapt = false;
    settings.Paint.PackedBatchLimit = 7;
    settings.Paint.PackedBatchPacingMs = 125;

    var payload = BridgePayloadBuilder.BuildPaintPayload(settings, 42, "Game.exe", new PaintRequestOptions());
    using var document = JsonDocument.Parse(payload);
    var tuning = document.RootElement.GetProperty("tuning");

    Assert(!tuning.GetProperty("server_batch_auto_adapt").GetBoolean(), "manual batch mode should map directly");
    Assert(tuning.GetProperty("server_batch_limit").GetInt32() == 7, "batch limit should map directly");
    Assert(tuning.GetProperty("server_batch_pacing_ms").GetInt32() == 125, "batch pacing should map directly");

    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    Assert(bridge.Contains("json_bool_field(request,\n                            \"server_batch_auto_adapt\"", StringComparison.Ordinal),
        "native bridge should read the batch auto-adapt flag");
    Assert(bridge.Contains("runtime_contract::resolve_configured_pacing(", StringComparison.Ordinal),
        "native bridge should select automatic or manual pacing from the flag");
}

static void LocalesHaveCompleteKeys()
{
    var catalog = LocalizationCatalog.Load();
    var all = catalog.All;
    var englishKeys = all["en"].Keys.Order().ToArray();
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        Assert(all.ContainsKey(locale.Code), $"missing locale {locale.Code}");
        var keys = all[locale.Code].Keys.Order().ToArray();
        Assert(englishKeys.SequenceEqual(keys), $"key mismatch for {locale.Code}");
    }
}

static void ColorParserAcceptsHex()
{
    Assert(RgbColor.TryParse("F11111", out var color), "hex without # should parse");
    Assert(color.ToHex() == "#F11111", "hex roundtrip failed");
}

static void RuntimeLogKeepsRepeatedGuardMessages()
{
    using var temp = new TempHome();
    var paths = new AppPaths("runtime-log-repeat-test");
    var log = new RuntimeLog(paths);

    log.Warn("Paint: no active paint to cancel.");
    log.Warn("Paint: no active paint to cancel.");

    var count = log.Text
        .Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries)
        .Count(line => line.Contains("[WARN] Paint: no active paint to cancel.", StringComparison.OrdinalIgnoreCase));
    Assert(count == 2, "repeated user guard warnings should be logged");
}

static void AssetValidationRejectsStaleReadyCache()
{
    using var temp = new TempHome();
    var root = Path.Combine(Path.GetTempPath(), "meccha-asset-test-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(Path.Combine(root, "native"));
        var file = Path.Combine(root, "native", "runtime-bridge.dll");
        File.WriteAllText(file, "bridge");
        var asset = new PackagedAssetEntry(
            "native.bridge",
            "native/runtime-bridge.dll",
            "packaged/native/runtime-bridge.dll",
            new FileInfo(file).Length,
            PackagedAssets.Sha256File(file),
            true);
        var manifest = new PackagedAssetManifest(1, "asset-test", DateTimeOffset.UtcNow.ToString("O"), [asset]);

        var missingReady = PackagedAssets.ValidateExtractedAssetSet(root, manifest);
        Assert(!missingReady.Valid, "cache without ready.json should be invalid");

        File.WriteAllText(Path.Combine(root, "ready.json"), """{"assetSetId":"asset-test"}""");
        var valid = PackagedAssets.ValidateExtractedAssetSet(root, manifest);
        Assert(valid.Valid, "cache with matching ready.json and file hash should be valid");

        File.WriteAllText(file, "corrupt");
        var corrupt = PackagedAssets.ValidateExtractedAssetSet(root, manifest);
        Assert(!corrupt.Valid && corrupt.Code == "MC-RT-011", "corrupt required file should be invalid");
    }
    finally
    {
        if (Directory.Exists(root))
            Directory.Delete(root, recursive: true);
    }
}

static void CopyIfInvalidRepairsCorruptTarget()
{
    var root = Path.Combine(Path.GetTempPath(), "meccha-copy-test-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(root);
        var source = Path.Combine(root, "source.bin");
        var target = Path.Combine(root, "nested", "target.bin");
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        File.WriteAllText(source, "expected");
        File.WriteAllText(target, "bad");

        var copied = PackagedAssets.CopyIfInvalid(source, target);

        Assert(copied, "corrupt target should be replaced");
        Assert(File.ReadAllText(target) == "expected", "target should match source");
        Assert(!PackagedAssets.CopyIfInvalid(source, target), "matching target should not be copied again");
    }
    finally
    {
        if (Directory.Exists(root))
            Directory.Delete(root, recursive: true);
    }
}

static void ResearchEventWatchSidecarUsesExactStagedBridgePath()
{
    var root = Path.Combine(Path.GetTempPath(), "meccha-eventwatch-test-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(root);
        var bridge = Path.Combine(root, "meccha-direct-bridge.dll");
        var output = Path.Combine(root, "artifacts", "eventwatch.json");
        File.WriteAllText(bridge, "bridge");

        var sidecar = ResearchBridgeArtifacts.StageEventWatchSidecar(bridge, output);

        Assert(sidecar == Path.GetFullPath(bridge) + ".eventwatch.path", "event-watch sidecar must belong to the exact staged bridge");
        Assert(File.Exists(sidecar), "event-watch path sidecar should be written before injection");
        Assert(File.ReadAllText(sidecar).Trim() == Path.GetFullPath(output), "event-watch sidecar should contain the normalized artifact path");
        Assert(!File.Exists(Path.GetFullPath(bridge) + ".eventwatch"), "research staging should not create the ambiguous fallback sidecar");
    }
    finally
    {
        if (Directory.Exists(root))
            Directory.Delete(root, recursive: true);
    }
}

static void DiagnosticSummaryIncludesFileNotFoundDetails()
{
    using var temp = new TempHome();
    var paths = new AppPaths("diagnostics-test");
    DiagnosticsState.Initialize(paths, "diagnostics-test");
    DiagnosticsState.RecordException("unit-test", new FileNotFoundException("missing file", "missing-runtime.dll"));

    var summary = DiagnosticsState.Summary(paths);

    Assert(summary.Contains("last_exception_hresult: 0x80070002", StringComparison.OrdinalIgnoreCase), "summary should include HResult");
    Assert(summary.Contains("last_exception_file: missing-runtime.dll", StringComparison.OrdinalIgnoreCase), "summary should include missing file name");
}

static void DiagnosticsLogWriteIsBestEffortWhenFileLocked()
{
    using var temp = new TempHome();
    var paths = new AppPaths("diagnostics-lock-test");
    DiagnosticsState.Initialize(paths, "diagnostics-lock-test");
    var startupLogPath = StartupLogPath(DiagnosticsState.Summary(paths));

    using var locked = new FileStream(startupLogPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);

    DiagnosticsState.WriteLine("unit-test", "this write should be skipped instead of crashing");
    DiagnosticsState.RecordException("locked-log-test", new InvalidOperationException("expected test exception"));
}

static void RuntimeLogWriteIsBestEffortWhenFileLocked()
{
    using var temp = new TempHome();
    var paths = new AppPaths("runtime-log-lock-test");
    var log = new RuntimeLog(paths);
    var path = Path.Combine(paths.LogDirectory, $"runtime-{DateTime.Now:yyyy-MM-dd}.log");

    Directory.CreateDirectory(paths.LogDirectory);
    using var locked = new FileStream(path, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);

    log.Info("Runtime log should keep updating the in-memory UI state.");

    Assert(log.Text.Contains("Runtime log should keep updating", StringComparison.Ordinal), "in-memory log should still update");
}

static string StartupLogPath(string summary)
{
    var line = summary
        .Split(Environment.NewLine, StringSplitOptions.RemoveEmptyEntries)
        .FirstOrDefault(value => value.StartsWith("startup_log: ", StringComparison.OrdinalIgnoreCase));
    if (string.IsNullOrWhiteSpace(line))
        throw new InvalidOperationException("summary should include startup log path");
    return line["startup_log: ".Length..].Trim();
}

static void AutoMaterialDefaultsOff()
{
    Assert(!new AppSettings().Paint.AutoMaterial, "auto material should default off");
}

static void FrontRegionDefaultsToFill()
{
    Assert(new AppSettings().Paint.FrontRegionMode == RegionMode.Fill, "front should default to fill");
}

static void BridgeMessagesAreUserFriendly()
{
    var alreadyRunning = HostSession.FriendlyBridgeMessage("mesh-first paint is already running");
    var completed = HostSession.FriendlyBridgeMessage("mesh-first paint completed");
    var alreadyFriendlyCompleted = HostSession.FriendlyBridgeMessage("Paint completed.");
    var preview = HostSession.FriendlyBridgeMessage("local preview material texture imported");
    var noPreview = HostSession.FriendlyBridgeMessage("mesh_unpreview_snapshot_unavailable");
    var contextChanged = HostSession.FriendlyBridgeMessage("mesh_paint_context_changed");
    var componentUnavailable = HostSession.FriendlyBridgeMessage("ServerPackedPaintBatch failed: paint_component_unavailable");
    var pawnUnavailable = HostSession.FriendlyBridgeMessage("Paint stopped because the local pawn is no longer available");
    var unsupportedNoResendRoute = HostSession.FriendlyBridgeMessage(
        "internal no-resend route validation failed for this game build: main_module_build_identity_mismatch");
    var partialLocalFailure = HostSession.FriendlyBridgeMessage(
        "Paint strokes were submitted, but local rendering failed: internal_common_no_resend_exception. Do not retry automatically.");
    var cancelledAfterSubmission = HostSession.FriendlyBridgeMessage(
        "paint cancellation arrived after submission; the committed local queue drained");
    var cancelledWithBoundedTail = HostSession.FriendlyBridgeMessage(
        "paint cancellation stopped further submission; the committed local queue drained");
    var unsafeSampling = HostSession.FriendlyBridgeMessage("planner found unsafe color-transfer candidates in enabled regions; replay was blocked instead of skipping samples");
    var localOnlyCompletion = HostSession.DescribePaintCompletion(completed, serverPaint: false);
    var replicatedCompletion = HostSession.DescribePaintCompletion(completed, serverPaint: true);

    Assert(alreadyRunning == "Paint: already running.", "already-running message should be friendly");
    Assert(completed == "Paint: completed.", "completed message should be friendly");
    Assert(alreadyFriendlyCompleted == "Paint: completed.", "already-friendly completed message should be normalized");
    Assert(localOnlyCompletion == "Paint: completed.", "non-replicated completion should retain the simple local message");
    Assert(replicatedCompletion.Contains("other clients may still be rendering", StringComparison.Ordinal),
        "replicated completion must not claim that another client has already presented its final pixels");
    Assert(preview == "Preview: applied.", "preview message should be friendly");
    Assert(noPreview == "Preview: no active preview to restore.", "missing preview snapshot should be a guard warning");
    Assert(contextChanged == "Paint: stopped because the game paint component changed.", "paint context change should be friendly");
    Assert(componentUnavailable == "Paint: stopped because the game paint component is unavailable.", "paint component unavailable should be friendly");
    Assert(pawnUnavailable == "Paint: stopped because the local pawn is no longer available.", "pawn unavailable should be friendly");
    Assert(unsupportedNoResendRoute == "Paint: this game build is not supported.", "unsupported no-resend route should be friendly");
    Assert(partialLocalFailure == "Paint: strokes were sent, but local rendering failed. Do not retry automatically.", "partial local failure should warn against retry");
    Assert(cancelledAfterSubmission == "Paint: canceled.",
        "a late cancel that waited for the committed queue must remain a concise cancellation");
    Assert(cancelledWithBoundedTail == "Paint: canceled.",
        "a bounded local queue cancel should remain a concise cancellation");
    Assert(unsafeSampling == "Paint: blocked because the current mesh sampling was unsafe.", "unsafe mesh sampling should be friendly");
    Assert(!alreadyRunning.Contains("mesh", StringComparison.OrdinalIgnoreCase), "internal mesh wording should be hidden");
}

static void PaintFallbackWarningPreservesNativeReasonAndFixedPacing()
{
    const string reason =
        "the exact local receiver queue could not be measured before any server batch was submitted";
    var reply = new BridgeReply(
        true,
        true,
        "mesh_first_paint_done",
        "mesh-first paint completed",
        $$"""
        {
          "success": true,
          "metadata": {
            "local_route_mode": "server_packed_fallback",
            "fallback_reason": "{{reason}}",
            "fallback_batch_limit": 20,
            "fallback_pacing_ms": 50
          }
        }
        """);

    var warning = HostSession.PaintFallbackWarning(reply);

    Assert(warning == reason + " (server packed fallback: 20 strokes / 50 ms)",
        "fallback warning should preserve the native error text and append fixed pacing");
    Assert(HostSession.PaintFallbackWarning(reply with { Raw = "{\"success\":true}" }) is null,
        "normal paint replies should not emit a fallback warning");
}

static void ManualDirectFallbackWarningPreservesNativeReasonAndEffectivePacing()
{
    const string reason = "manual direct local route unavailable: signature mismatch";
    var reply = new BridgeReply(
        true,
        true,
        "mesh_first_paint_done",
        "mesh-first paint completed",
        $$"""
        {
          "success": true,
          "metadata": {
            "local_route_mode": "packed_receiver_fallback",
            "fallback_reason": "{{reason}}",
            "fallback_batch_limit": 20,
            "fallback_pacing_ms": 50
          }
        }
        """);

    var warning = HostSession.PaintFallbackWarning(reply);

    Assert(warning == reason + " (packed receiver fallback: 20 strokes / 50 ms)",
        "manual direct fallback warning should preserve the reason and effective safe pacing");
}

static void SettingsDetectSupportedSystemLanguage()
{
    var previous = System.Globalization.CultureInfo.CurrentUICulture;
    try
    {
        System.Globalization.CultureInfo.CurrentUICulture = new System.Globalization.CultureInfo("ja-JP");
        var settings = SettingsStore.Clamp(new AppSettings());
        Assert(settings.Language == "ja", "blank language should detect supported UI culture");
    }
    finally
    {
        System.Globalization.CultureInfo.CurrentUICulture = previous;
    }
}

static void UiSnapshotExposesTwoPassBrushesAndBatchSliders()
{
    var snapshot = new PaintSnapshot(
        true,
        17.5,
        false,
        7.5,
        true,
        20,
        50,
        false,
        0.0,
        1.0,
        "fill",
        "paint",
        "paint",
        "#FFFFFF",
        1.0,
        0.0,
        true);
    var json = JsonSerializer.Serialize(snapshot, new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    });
    using var doc = JsonDocument.Parse(json);

    Assert(doc.RootElement.GetProperty("brush1Enabled").GetBoolean(), "snapshot should expose brush 1 enabled");
    Assert(Math.Abs(doc.RootElement.GetProperty("brush1SizeTexels").GetDouble() - 17.5) < 0.000001, "snapshot should expose brush 1");
    Assert(!doc.RootElement.GetProperty("brush2Enabled").GetBoolean(), "snapshot should expose brush 2 enabled");
    Assert(Math.Abs(doc.RootElement.GetProperty("brush2SizeTexels").GetDouble() - 7.5) < 0.000001, "snapshot should expose brush 2");
    Assert(doc.RootElement.GetProperty("batchAutoAdapt").GetBoolean(), "snapshot should expose batch auto adapt");
    Assert(doc.RootElement.GetProperty("packedBatchLimit").GetInt32() == 20, "snapshot should expose packedBatchLimit for editing");
    Assert(doc.RootElement.GetProperty("packedBatchPacingMs").GetInt32() == 50, "snapshot should expose packedBatchPacingMs for editing");
    Assert(!doc.RootElement.TryGetProperty("brushSizeTexels", out _), "snapshot should not expose the removed single-brush field");
    Assert(!doc.RootElement.TryGetProperty("coverageStepTexels", out _), "coverage compatibility should stay internal");
    Assert(!doc.RootElement.TryGetProperty("pacingMode", out _), "snapshot should not expose removed pacingMode");
    Assert(!doc.RootElement.TryGetProperty("packedBatchDelayMs", out _), "snapshot should not expose legacy delay wording");
    Assert(!doc.RootElement.TryGetProperty("adaptiveBatching", out _), "snapshot should not expose adaptiveBatching for editing");
    Assert(!doc.RootElement.TryGetProperty("strokeDelayMs", out _), "snapshot should not expose strokeDelayMs for editing");
    Assert(!doc.RootElement.TryGetProperty("batchSize", out _), "snapshot should not expose renamed batchSize");
}

static void WebUiExposesTwoPassBrushSliders()
{
    var repository = FindRepositoryRoot();
    var index = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));

    Assert(index.Contains("id=\"brush-1-size\"", StringComparison.Ordinal), "web UI should include the coarse brush slider");
    Assert(index.Contains("id=\"brush-2-size\"", StringComparison.Ordinal), "web UI should include the detail brush slider");
    Assert(index.Contains("id=\"brush-1-enabled\"", StringComparison.Ordinal), "web UI should include the brush 1 checkbox");
    Assert(index.Contains("id=\"brush-2-enabled\"", StringComparison.Ordinal), "web UI should include the brush 2 checkbox");
    Assert(index.IndexOf("id=\"brush-1-size\"", StringComparison.Ordinal) < index.IndexOf("id=\"brush-2-size\"", StringComparison.Ordinal), "brush 1 should appear above brush 2");
    Assert(index.Contains("min=\"10\" max=\"50\" step=\"0.5\"", StringComparison.Ordinal), "brush 1 should expose the 10-50 range");
    Assert(index.Contains("min=\"1\" max=\"10\" step=\"0.5\"", StringComparison.Ordinal), "brush 2 should expose the 1-10 range");
    Assert(index.Contains("id=\"batch-auto-adapt\"", StringComparison.Ordinal),
        "geometry controls should expose batch auto adapt");
    Assert(index.Contains("id=\"packed-batch-limit\" class=\"setting-control\" disabled type=\"range\" min=\"1\" max=\"500\" step=\"1\"", StringComparison.Ordinal),
        "manual batch limit should expose the 1-500 range");
    Assert(index.Contains("id=\"packed-batch-pacing\" class=\"setting-control\" disabled type=\"range\" min=\"1\" max=\"500\" step=\"1\"", StringComparison.Ordinal),
        "manual batch pacing should expose the 1-500 ms range");
    Assert(app.Contains("paint.batchAutoAdapt", StringComparison.Ordinal) &&
           app.Contains("!editing || paint.batchAutoAdapt", StringComparison.Ordinal),
        "manual batch controls should lock while auto adapt is on");
    Assert(app.Contains("paint.brush1Enabled", StringComparison.Ordinal), "web UI should bind brush 1 enabled");
    Assert(app.Contains("paint.brush1SizeTexels", StringComparison.Ordinal), "web UI should bind brush 1");
    Assert(app.Contains("paint.brush2Enabled", StringComparison.Ordinal), "web UI should bind brush 2 enabled");
    Assert(app.Contains("paint.brush2SizeTexels", StringComparison.Ordinal), "web UI should bind brush 2");
    Assert(app.Contains("if (!paint.brush1Enabled && !paint.brush2Enabled)", StringComparison.Ordinal) &&
           app.Contains("showError(\"At least one brush must be enabled.\")", StringComparison.Ordinal),
        "web UI should retain editing and show an error instead of saving with both brushes off");
    Assert(app.Contains("!editing || !paint.brush1Enabled", StringComparison.Ordinal) &&
           app.Contains("!editing || !paint.brush2Enabled", StringComparison.Ordinal),
        "web UI should disable each brush slider while its checkbox is off");
    Assert(!app.Contains("paint.brushSizeTexels", StringComparison.Ordinal), "web UI should not send the removed single-brush key");
    Assert(!app.Contains("coverageStepTexels", StringComparison.Ordinal), "web UI should not expose internal coverage compatibility");
    Assert(styles.Contains(".brush-toggle > span", StringComparison.Ordinal) &&
           styles.Contains("font: 700 12px/14px var(--font-mono)", StringComparison.Ordinal),
        "brush labels should use the same typography as the other Geometry labels");
}

static void WebUiKeepsThemeColorOnReadonlyControls()
{
    var repository = FindRepositoryRoot();
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));

    Assert(app.Contains("isThemeVisibleReadOnlyControl", StringComparison.Ordinal),
        "the UI should distinguish passive themed controls from ordinary disabled inputs");
    Assert(app.Contains("control.disabled = disabled && !themeVisibleReadonly", StringComparison.Ordinal),
        "readonly range and checkbox controls should remain paint-enabled for Chromium accent rendering");
    Assert(styles.Contains("input.theme-visible-readonly[type=\"range\"]", StringComparison.Ordinal),
        "readonly sliders need a dedicated themed style");
    Assert(styles.Replace("\r", "").Contains("input.theme-visible-readonly[type=\"range\"] {\n  opacity: 0.55;", StringComparison.Ordinal),
        "readonly sliders should visibly dim outside Edit mode");
    Assert(styles.Contains("input.theme-visible-readonly[type=\"checkbox\"]", StringComparison.Ordinal),
        "readonly checkboxes need a dedicated themed style");
    Assert(styles.Contains("pointer-events: none", StringComparison.Ordinal),
        "passive themed controls must not become interactive outside Edit mode");
    Assert(app.Contains("function canEditControl(control = null)", StringComparison.Ordinal) &&
           app.Contains("control?.getAttribute(\"aria-disabled\")", StringComparison.Ordinal) &&
           app.Contains("if (!canEditControl(source))", StringComparison.Ordinal),
        "passive themed controls must reject keyboard and label-driven edits outside Edit mode, including dependent locks");
    Assert(app.Contains("document.activeElement === control", StringComparison.Ordinal),
        "locking a previously focused themed control must blur it before keyboard input can change its visible value");
}

static void WebUiRendersPassProgressAndTotalEta()
{
    var app = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));

    Assert(app.Contains("runtime.paintPass", StringComparison.Ordinal), "live progress should render the current pass name");
    Assert(app.Contains("runtime.paintPassProgress", StringComparison.Ordinal), "live progress should render pass-local counts and percent");
    Assert(app.Contains("runtime.paintPassEta", StringComparison.Ordinal), "live progress should render pass ETA");
    Assert(app.Contains("total ETA", StringComparison.Ordinal), "live progress should label the paint ETA as total ETA");
    Assert(app.Contains("Paint: overall", StringComparison.Ordinal), "live progress should distinguish overall progress from pass progress");
}

static void RawHotkeysSuppressRepeatUntilKeyUp()
{
    var state = new HotkeyKeyState();
    Assert(state.TryBeginPress(0x72), "the first F3 key-down should trigger");
    Assert(!state.TryBeginPress(0x72), "a repeated F3 key-down should not trigger");
    state.EndPress(0x72);
    Assert(state.TryBeginPress(0x72), "F3 should trigger again after key-up");
}

static void RawHotkeysDoNotReserveSystemKeys()
{
    var repository = FindRepositoryRoot();
    var mainForm = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));

    Assert(mainForm.Contains("RegisterRawInputDevices", StringComparison.Ordinal) &&
           mainForm.Contains("RidevInputSink", StringComparison.Ordinal),
        "hotkeys should observe background keyboard input without reserving a global key");
    Assert(!mainForm.Contains("RegisterHotKey(", StringComparison.Ordinal) &&
           !mainForm.Contains("WmHotkey", StringComparison.Ordinal),
        "hotkeys must not use the exclusive Win32 global-hotkey registry");
    Assert(mainForm.Contains("if (!session.Runtime.IsConnected)", StringComparison.Ordinal),
        "raw hotkeys should remain inactive until the game bridge is connected");
}

static void NativeProgressExposesReplayPassState()
{
    var repository = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(repository, "src", "native", "bridge", "bridge.cpp"));
    var json = File.ReadAllText(Path.Combine(repository, "src", "native", "bridge", "bridge_json.inc"));

    Assert(bridge.Contains("mesh_first_replay_pass_metadata", StringComparison.Ordinal), "native bridge should build pass metadata");
    Assert(bridge.Contains("replay_server_current", StringComparison.Ordinal), "native progress should expose the server pass");
    Assert(bridge.Contains("replay_local_current", StringComparison.Ordinal), "native progress should expose the local pass");
    Assert(bridge.Contains("g_paint_dispatch_message_pending", StringComparison.Ordinal), "scheduler wakeups should be coalesced");
    Assert(bridge.Contains("defer_user_cancel_until_receiver_drain", StringComparison.Ordinal),
        "a user cancel after paired submission should retain queue ownership until drain");
    Assert(bridge.Contains("paired_local_queue_commit_count", StringComparison.Ordinal) &&
           bridge.Contains("mesh_local_queue_capacity_wait", StringComparison.Ordinal),
        "paired packed paint should cap local commitment before each server/local batch");
    Assert(bridge.Contains("cancellation_stopped_further_submission", StringComparison.Ordinal) &&
           bridge.Contains("job->local_packed_queue_strokes_submitted", StringComparison.Ordinal),
        "a canceled bounded local tail must report only actually submitted strokes as rendered");
    Assert(bridge.Contains("force_terminal_idle_local_queue_drain", StringComparison.Ordinal),
        "shutdown and request timeout should be able to terminalize an idle drain safely");
    Assert(bridge.Contains("receiver_queue_idle_threshold_reached", StringComparison.Ordinal),
        "receiver drain should invalidate stale ETA and fail closed after an idle timeout");
    Assert(json.Contains("replay_current_pass", StringComparison.Ordinal), "compact progress metadata should retain the current pass");
    Assert(bridge.Contains("manual_batch_uses_direct_local", StringComparison.Ordinal) &&
           bridge.Contains("local_direct_submission", StringComparison.Ordinal) &&
           bridge.Contains("parallel_lane_eta_ms", StringComparison.Ordinal),
        "manual paint should expose independent direct-local progress and parallel-lane ETA");
    Assert(bridge.Contains("!manual_direct_local_requested", StringComparison.Ordinal),
        "Auto Adapt paint should retain the packed local receiver queue");
}

static void SettingsClampBatchSliders()
{
    var settings = new AppSettings();
    settings.Paint.PackedBatchLimit = 999;
    settings.Paint.PackedBatchPacingMs = 750;

    var clamped = SettingsStore.Clamp(settings);

    Assert(clamped.Paint.PackedBatchLimit == 500, "manual batch limit should clamp to the configured maximum");
    Assert(clamped.Paint.PackedBatchPacingMs == 500, "batch pacing should clamp to maximum interval");

    settings.Paint.PackedBatchLimit = 0;
    settings.Paint.PackedBatchPacingMs = 0;
    clamped = SettingsStore.Clamp(settings);

    Assert(clamped.Paint.PackedBatchLimit == 1, "batch limit should clamp to one");
    Assert(clamped.Paint.PackedBatchPacingMs == 1, "zero manual pacing must clamp to one millisecond");
}

static void HotkeyValidationRejectsDuplicates()
{
    var hotkeys = new HotkeySet("F1", "F1", "F3", "F4");
    Assert(!hotkeys.TryValidate(out var message), "duplicate hotkeys should be rejected");
    Assert(message.Contains("duplicated", StringComparison.OrdinalIgnoreCase), "duplicate message should explain the problem");

    var invalid = new HotkeySet("A", "F2", "F3", "F4");
    Assert(!invalid.TryValidate(out _), "non-function hotkeys should be rejected");
}

static void HostSessionResetRestoresDefault()
{
    using var temp = new TempHome();
    var session = new HostSession("host-reset-test");

    var update = session.UpdateSettings([
        new SettingChange("paint.brush1SizeTexels", JsonSerializer.SerializeToElement(17.5)),
        new SettingChange("paint.brush2SizeTexels", JsonSerializer.SerializeToElement(7.5)),
        new SettingChange("paint.packedBatchLimit", JsonSerializer.SerializeToElement(7)),
        new SettingChange("paint.packedBatchPacingMs", JsonSerializer.SerializeToElement(125))
    ]);
    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - 17.5) < 0.000001, "brush 1 should update");
    Assert(Math.Abs(session.Settings.Paint.Brush2SizeTexels - 7.5) < 0.000001, "brush 2 should update");

    var reset = session.ResetSetting("paint.brush1SizeTexels");
    Assert(reset.Success, reset.Message);
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - new AppSettings().Paint.Brush1SizeTexels) < 0.000001, "brush 1 should reset");
    Assert(Math.Abs(session.Settings.Paint.Brush2SizeTexels - 7.5) < 0.000001, "brush 1 reset must preserve brush 2");
    Assert(session.Settings.Paint.PackedBatchLimit == 7, "brush-only reset must preserve batch limit");
    Assert(session.Settings.Paint.PackedBatchPacingMs == 125, "brush-only reset must preserve batch pacing");
}

static void HostSessionBrushUpdatesAreIndependentAndDetailSyncsCoverage()
{
    using var temp = new TempHome();
    var session = new HostSession("host-brush-sync-test");
    var originalBrush2 = session.Settings.Paint.Brush2SizeTexels;

    var coarseUpdate = session.UpdateSetting("paint.brush1SizeTexels", JsonSerializer.SerializeToElement(17.5));
    Assert(coarseUpdate.Success, coarseUpdate.Message);
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - 17.5) < 0.000001, "brush 1 should update independently");
    Assert(Math.Abs(session.Settings.Paint.Brush2SizeTexels - originalBrush2) < 0.000001, "brush 1 should not change brush 2");

    var detailUpdate = session.UpdateSetting("paint.brush2SizeTexels", JsonSerializer.SerializeToElement(6.5));

    Assert(detailUpdate.Success, detailUpdate.Message);
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - 17.5) < 0.000001, "brush 2 should not change brush 1");
    Assert(Math.Abs(session.Settings.Paint.Brush2SizeTexels - 6.5) < 0.000001, "brush 2 should update");
    Assert(Math.Abs(session.Settings.Paint.CoverageStepTexels - 6.5) < 0.000001, "coverage compatibility should follow brush 2");

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();
    Assert(Math.Abs(snapshot.Settings.Paint.Brush1SizeTexels - 17.5) < 0.000001, "snapshot should expose brush 1");
    Assert(Math.Abs(snapshot.Settings.Paint.Brush2SizeTexels - 6.5) < 0.000001, "snapshot should expose brush 2");
}

static void HostSessionRejectsDisablingEveryBrush()
{
    using var temp = new TempHome();
    var session = new HostSession("host-brush-required-test");

    var result = session.UpdateSettings([
        new SettingChange("paint.brush1Enabled", JsonSerializer.SerializeToElement(false)),
        new SettingChange("paint.brush2Enabled", JsonSerializer.SerializeToElement(false))
    ]);

    Assert(!result.Success, "disabling every brush should be rejected");
    Assert(result.Message.Contains("At least one brush", StringComparison.Ordinal), "the rejection should explain the requirement");
    Assert(session.Settings.Paint.Brush2Enabled, "rejected settings must roll back");
}

static void HostSessionUpdatesBatchSliders()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-slider-test");

    var update = session.UpdateSettings([
        new SettingChange("paint.batchAutoAdapt", JsonSerializer.SerializeToElement(false)),
        new SettingChange("paint.packedBatchLimit", JsonSerializer.SerializeToElement(7)),
        new SettingChange("paint.packedBatchPacingMs", JsonSerializer.SerializeToElement(125))
    ]);

    Assert(update.Success, update.Message);
    Assert(!session.Settings.Paint.BatchAutoAdapt, "batch auto adapt should be disabled");
    Assert(session.Settings.Paint.PackedBatchLimit == 7, "batch limit should be applied");
    Assert(session.Settings.Paint.PackedBatchPacingMs == 125, "batch pacing should be applied");

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();
    Assert(!snapshot.Settings.Paint.BatchAutoAdapt, "snapshot should expose manual batching");
    Assert(snapshot.Settings.Paint.PackedBatchLimit == 7, "snapshot should expose the batch limit slider");
    Assert(snapshot.Settings.Paint.PackedBatchPacingMs == 125, "snapshot should expose the batch pacing slider");
}

static void HostSessionQuantizesDecimalBatchSliderUpdates()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-slider-decimal-test");

    var update = session.UpdateSettings([
        new SettingChange("paint.packedBatchLimit", JsonSerializer.SerializeToElement(6.5)),
        new SettingChange("paint.packedBatchPacingMs", JsonSerializer.SerializeToElement(125.5))
    ]);

    Assert(update.Success, update.Message);
    Assert(session.Settings.Paint.PackedBatchLimit == 7, "batch limit should round to the nearest integer step");
    Assert(session.Settings.Paint.PackedBatchPacingMs == 126, "batch pacing should round to the nearest integer step");
}

static void HostSessionRollsBackInvalidHotkeyUpdate()
{
    using var temp = new TempHome();
    var session = new HostSession("host-hotkey-rollback-test");
    var original = session.Settings.PreviewHotkey;

    var update = session.UpdateSetting("app.previewHotkey", JsonSerializer.SerializeToElement(session.Settings.StartHotkey));
    Assert(!update.Success, "duplicate hotkey update should fail");
    Assert(session.Settings.PreviewHotkey == original, "failed hotkey update should roll back in memory");
}

static void HostSessionAppliesMultipleSettingUpdatesAtomically()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-valid-test");

    var update = session.UpdateSettings([
        new SettingChange("paint.brush1SizeTexels", JsonSerializer.SerializeToElement(17.5)),
        new SettingChange("paint.fillColor", JsonSerializer.SerializeToElement("#112233")),
        new SettingChange("app.processName", JsonSerializer.SerializeToElement("Game.exe"))
    ]);

    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - 17.5) < 0.000001, "brush 1 should update");
    Assert(session.Settings.Paint.FillColor.ToHex() == "#112233", "fill color should update");
    Assert(session.Settings.GameProcessName == "Game.exe", "process name should update");
}

static void HostSessionRollsBackDuplicateHotkeyBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-hotkey-rollback-test");
    var originalBrush = session.Settings.Paint.Brush1SizeTexels;
    var originalPreview = session.Settings.PreviewHotkey;

    var update = session.UpdateSettings([
        new SettingChange("paint.brush1SizeTexels", JsonSerializer.SerializeToElement(17.5)),
        new SettingChange("app.previewHotkey", JsonSerializer.SerializeToElement(session.Settings.StartHotkey))
    ]);

    Assert(!update.Success, "duplicate hotkey batch should fail");
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - originalBrush) < 0.000001, "non-hotkey change should roll back");
    Assert(session.Settings.PreviewHotkey == originalPreview, "hotkey change should roll back");
}

static void HostSessionRollsBackInvalidFillColorBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-color-rollback-test");
    var originalBrush = session.Settings.Paint.Brush1SizeTexels;
    var originalColor = session.Settings.Paint.FillColor;

    var update = session.UpdateSettings([
        new SettingChange("paint.brush1SizeTexels", JsonSerializer.SerializeToElement(17.5)),
        new SettingChange("paint.fillColor", JsonSerializer.SerializeToElement("not-a-color"))
    ]);

    Assert(!update.Success, "invalid color batch should fail");
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - originalBrush) < 0.000001, "brush 1 should roll back");
    Assert(session.Settings.Paint.FillColor == originalColor, "fill color should roll back");
}

static void HostSessionRollsBackInvalidThemeColorBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-theme-rollback-test");
    var originalBrush = session.Settings.Paint.Brush1SizeTexels;
    var originalTheme = session.Settings.ThemeColor;

    var update = session.UpdateSettings([
        new SettingChange("paint.brush1SizeTexels", JsonSerializer.SerializeToElement(17.5)),
        new SettingChange("app.themeColor", JsonSerializer.SerializeToElement("not-a-color"))
    ]);

    Assert(!update.Success, "invalid theme color batch should fail");
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - originalBrush) < 0.000001, "brush 1 should roll back");
    Assert(session.Settings.ThemeColor == originalTheme, "theme color should roll back");
}

static void HostSessionRollsBackInvalidRegionModeBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-region-rollback-test");
    var originalBrush = session.Settings.Paint.Brush1SizeTexels;
    var originalMode = session.Settings.Paint.FrontRegionMode;

    var update = session.UpdateSettings([
        new SettingChange("paint.brush1SizeTexels", JsonSerializer.SerializeToElement(17.5)),
        new SettingChange("paint.frontRegionMode", JsonSerializer.SerializeToElement("invalid"))
    ]);

    Assert(!update.Success, "invalid region mode batch should fail");
    Assert(Math.Abs(session.Settings.Paint.Brush1SizeTexels - originalBrush) < 0.000001, "brush 1 should roll back");
    Assert(session.Settings.Paint.FrontRegionMode == originalMode, "region mode should roll back");
}

static void HostSessionSnapshotIgnoresPrePaintProgress()
{
    using var temp = new TempHome();
    var session = new HostSession("host-pre-paint-progress-test");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    File.WriteAllText(Path.Combine(session.Paths.BridgeProgressDirectory, "stale.progress.json"), """
    {"stage":"mesh_paint_done","message":"done","step":1,"total_steps":1,"progress":1.0,"elapsed_ms":1.0}
    """);

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(!snapshot.Runtime.ProgressVisible, "pre-paint progress should not be visible");
}

static void HostSessionProgressCandidatesUseBridgeState()
{
    using var temp = new TempHome();
    var paths = new AppPaths("host-progress-candidates-test");
    var bridgeProgress = Path.Combine(paths.BridgeProgressDirectory, "bridge.progress.json");
    var versionProgressDirectory = Path.Combine(paths.VersionRoot, "runtime", "progress");
    var versionProgress = Path.Combine(versionProgressDirectory, "version.progress.json");
    Directory.CreateDirectory(paths.BridgeProgressDirectory);
    Directory.CreateDirectory(versionProgressDirectory);
    File.WriteAllText(bridgeProgress, "{}");
    File.WriteAllText(versionProgress, "{}");

    var candidates = HostSession.ProgressSnapshotCandidatePaths(paths);

    Assert(candidates.Contains(Path.GetFullPath(bridgeProgress), StringComparer.OrdinalIgnoreCase), "bridge-state progress should be considered");
    Assert(!candidates.Contains(Path.GetFullPath(versionProgress), StringComparer.OrdinalIgnoreCase), "version runtime progress should not be scanned");
}

static void HostSessionDoesNotFallbackWhenPreferredProgressIsMalformed()
{
    using var temp = new TempHome();
    var session = new HostSession("host-preferred-progress-write-test");
    var preferred = Path.Combine(session.Paths.BridgeProgressDirectory, "preferred.progress.json");
    var fallback = Path.Combine(session.Paths.BridgeProgressDirectory, "other-instance.progress.json");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    File.WriteAllText(preferred, "{");
    File.WriteAllText(fallback, """
    {"stage":"mesh_server_batch","phase":"server_batch","terminal":false,"result":"running","step":50,"total_steps":100,"progress":0.5,"paint_eta_ms":1000,"paint_elapsed_ms":1000}
    """);
    ConfigureLiveProgressSession(session, preferred);

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(!snapshot.Runtime.ProgressVisible,
        "an existing preferred progress file that is momentarily malformed must not expose another bridge instance's progress");
}

static void HostSessionDoesNotFallbackWhenPreferredProgressIsMissing()
{
    using var temp = new TempHome();
    var session = new HostSession("host-missing-preferred-progress-test");
    var preferred = Path.Combine(session.Paths.BridgeProgressDirectory, "not-created-yet.progress.json");
    var fallback = Path.Combine(session.Paths.BridgeProgressDirectory, "other-instance.progress.json");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    File.WriteAllText(fallback, """
    {"stage":"mesh_server_batch","phase":"server_batch","terminal":false,"result":"running","step":50,"total_steps":100,"progress":0.5,"paint_eta_ms":1000,"paint_elapsed_ms":1000}
    """);
    ConfigureLiveProgressSession(session, preferred);

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(!snapshot.Runtime.ProgressVisible,
        "a configured bridge path must remain authoritative before its first atomic snapshot is created");
}

static void HostSessionDoesNotFallbackWhenPreferredProgressIsStale()
{
    using var temp = new TempHome();
    var session = new HostSession("host-stale-preferred-progress-test");
    var preferred = Path.Combine(session.Paths.BridgeProgressDirectory, "preferred.progress.json");
    var fallback = Path.Combine(session.Paths.BridgeProgressDirectory, "other-instance.progress.json");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    const string validProgress =
        "{\"stage\":\"mesh_server_batch\",\"phase\":\"server_batch\",\"terminal\":false,\"result\":\"running\",\"step\":50,\"total_steps\":100,\"progress\":0.5,\"paint_eta_ms\":1000,\"paint_elapsed_ms\":1000}";
    File.WriteAllText(preferred, validProgress);
    File.SetLastWriteTimeUtc(preferred, DateTime.UtcNow.AddMinutes(-1));
    File.WriteAllText(fallback, validProgress);
    ConfigureLiveProgressSession(session, preferred);

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(!snapshot.Runtime.ProgressVisible,
        "an existing but stale preferred snapshot must not be replaced by another bridge instance's fresh progress");
}

static void HostSessionPresentsNativePassProgressAndReceiverQueue()
{
    using var temp = new TempHome();
    var session = new HostSession("host-native-pass-progress-test");
    var preferred = Path.Combine(session.Paths.BridgeProgressDirectory, "preferred.progress.json");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    File.WriteAllText(preferred, """
    {
      "stage":"mesh_local_queue_drain",
      "phase":"local_queue_drain",
      "terminal":false,
      "result":"running",
      "step":813,
      "total_steps":5596,
      "progress":0.145282,
      "paint_eta_ms":58000,
      "paint_elapsed_ms":24000,
      "replication_queued_stroke_count":999,
      "local_packed_queue_drain_current_queue":620,
      "replication_pacing_queue_drain_strokes_per_sec":100,
      "local_packed_queue_drain_strokes_per_sec":125,
      "replay_progress_source":"receiver_queue_drain",
      "replay_current_pass":"coarse_paint",
      "replay_current_pass_start":109,
      "replay_current_pass_end":1442,
      "replay_current_pass_completed":704,
      "replay_current_pass_total":1333,
      "replay_current_pass_eta_ms":7000
    }
    """);
    ConfigureLiveProgressSession(session, preferred);

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(snapshot.Runtime.ProgressVisible, "valid preferred progress should be visible");
    Assert(snapshot.Runtime.PaintProgressSource == "receiver_queue_drain", "native progress source should be retained");
    Assert(snapshot.Runtime.PaintPass == "Brush 1", "coarse paint should be presented as Brush 1");
    Assert(snapshot.Runtime.PaintPassProgress == "704/1333 (53%)", "pass-local count and percent should be presented together");
    Assert(snapshot.Runtime.PaintPassEta == "7s", "pass ETA should be formatted independently");
    Assert(snapshot.Runtime.PaintEta == "58s", "paint ETA should remain the total ETA");
    Assert(snapshot.Runtime.Queue.StartsWith("620 strokes", StringComparison.Ordinal),
        "receiver-drain progress should prefer the exact local drain queue over the generic replication queue");
    Assert(snapshot.Runtime.Queue.Contains("drain 125/s", StringComparison.Ordinal),
        "receiver-drain progress should prefer the exact local observed drain rate");
}

static void HostSessionLogsEachPassTransitionOnce()
{
    using var temp = new TempHome();
    var session = new HostSession("host-pass-transition-log-test");
    var preferred = Path.Combine(session.Paths.BridgeProgressDirectory, "preferred.progress.json");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    ConfigureLiveProgressSession(session, preferred);

    WritePass("submission", "coarse_paint", 200, 1333, 5000);
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    WritePass("receiver_queue_drain", "coarse_paint", 400, 1333, 4000);
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    WritePass("receiver_queue_drain", "fine_paint", 10, 4154, 50000);
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(CountOccurrences(session.Log.Text, "Paint: pass Brush 1") == 1,
        "Brush 1 should be logged once even when the progress source changes");
    Assert(CountOccurrences(session.Log.Text, "Paint: pass Brush 2") == 1,
        "Brush 2 should be logged once despite repeated snapshots");

    void WritePass(string source, string pass, int completed, int total, double etaMs)
    {
        File.WriteAllText(preferred, $$"""
        {
          "stage":"mesh_paint_progress",
          "phase":"local_queue_drain",
          "terminal":false,
          "result":"running",
          "step":{{completed}},
          "total_steps":5596,
          "paint_eta_ms":60000,
          "paint_elapsed_ms":1000,
          "replay_progress_source":"{{source}}",
          "replay_current_pass":"{{pass}}",
          "replay_current_pass_completed":{{completed}},
          "replay_current_pass_total":{{total}},
          "replay_current_pass_eta_ms":{{etaMs}}
        }
        """);
    }
}

static void HostSessionWarnsWhenCancelHasNoActivePaint()
{
    using var temp = new TempHome();
    var session = new HostSession("host-cancel-guard-test");

    var result = session.StopPaintAsync().GetAwaiter().GetResult();

    Assert(!result.Success, "cancel without active paint should not succeed");
    Assert(result.Message == "Paint: no active paint to cancel.", "cancel guard message should be explicit");
    Assert(session.Log.Text.Contains("[WARN] Paint: no active paint to cancel.", StringComparison.OrdinalIgnoreCase), "cancel guard should be logged as warn");
    Assert(!session.Log.Text.Contains("cancel failed", StringComparison.OrdinalIgnoreCase), "cancel guard should not be logged as a failure");
}

static void HostSessionPreDispatchCancelPreventsLatePaintSend()
{
    using var temp = new TempHome();
    var session = new HostSession("host-pre-dispatch-cancel-test");
    var flags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
    var runningProperty = typeof(HostSession).GetProperty(nameof(HostSession.PaintRunning))
        ?? throw new InvalidOperationException("PaintRunning property missing");
    var activeField = typeof(HostSession).GetField("activePaintGeneration", flags)
        ?? throw new InvalidOperationException("activePaintGeneration field missing");
    var nextField = typeof(HostSession).GetField("nextPaintGeneration", flags)
        ?? throw new InvalidOperationException("nextPaintGeneration field missing");
    runningProperty.SetValue(session, true);
    activeField.SetValue(session, 1);
    nextField.SetValue(session, 1);

    var cancel = session.StopPaintAsync().GetAwaiter().GetResult();
    Assert(cancel.Success && cancel.Message == "Paint: canceled.",
        "cancel during bridge attach must latch locally instead of asking native to cancel a nonexistent job");

    var tryBeginDispatch = typeof(HostSession).GetMethod("TryBeginPaintDispatch", flags)
        ?? throw new InvalidOperationException("TryBeginPaintDispatch method missing");
    var maySend = (bool)(tryBeginDispatch.Invoke(session, [1])
        ?? throw new InvalidOperationException("TryBeginPaintDispatch returned null"));
    Assert(!maySend, "a pre-dispatch cancel must forbid the later paint request from being sent");
}

static void HostSessionRetriesCancelAcrossNativeAdmission()
{
    HostSessionRetriesCancelAcrossNativeAdmissionAsync().GetAwaiter().GetResult();
}

static async Task HostSessionRetriesCancelAcrossNativeAdmissionAsync()
{
    using var temp = new TempHome();
    var session = new HostSession("host-cancel-admission-race-test");
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var instanceId = Guid.Parse("10234567-89ab-cdef-0123-456789abcdef");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("cd", 32));
    var target = TargetProcessIdentity.Create(4243, 1, Path.Combine(Path.GetTempPath(), "cancel-admission-game.exe"));
    var instance = new BridgeInstance(target, instanceId, token, hash, "bridge.dll", "injector.exe", "progress.json");
    instance.SetPort(port);
    SetActiveBridge(session.Runtime, instance, connected: true);
    SetHostPaintState(session, running: true, nativeMayBeRunning: false, activeGeneration: 1);

    var server = Task.Run(async () =>
    {
        for (var request = 0; request < 2; ++request)
        {
            using var accepted = await listener.AcceptTcpClientAsync();
            await using var stream = accepted.GetStream();
            using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
            await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
            _ = await reader.ReadLineAsync();
            await writer.WriteLineAsync(JsonSerializer.Serialize(new
            {
                success = true,
                stage = "hello",
                message = "ok",
                metadata = new { pid = 4243, instance_id = instanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
            }));
            var command = await reader.ReadLineAsync();
            Assert(command == "{\"type\":\"cancel_paint\"}", "admission retry must preserve the authenticated cancel command");
            var metadata = request == 0
                ? "\"cancelled_active_paint_jobs\":0,\"cancelled_queued_paint_jobs\":0"
                : "\"cancelled_active_paint_jobs\":1,\"cancelled_queued_paint_jobs\":0";
            await writer.WriteLineAsync("{\"success\":true,\"stage\":\"paint_cancel_requested\",\"message\":\"paint cancel requested\",\"metadata\":{" + metadata + "}}");
        }
    });

    var result = await session.StopPaintAsync().WaitAsync(TimeSpan.FromSeconds(3));
    Assert(result.Success && result.Message == "Paint: cancel requested.",
        "a zero-job early ACK must retry and return the concise pending cancellation state");
    Assert(!session.Log.Text.Contains("no active paint", StringComparison.OrdinalIgnoreCase),
        "the admission race must not produce a misleading no-active warning");
    await server.WaitAsync(TimeSpan.FromSeconds(3));

    static void SetHostPaintState(HostSession targetSession, bool running, bool nativeMayBeRunning, int activeGeneration)
    {
        var flags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
        var runningProperty = typeof(HostSession).GetProperty(nameof(HostSession.PaintRunning))
            ?? throw new InvalidOperationException("PaintRunning property missing");
        var nativeField = typeof(HostSession).GetField("nativePaintMayBeRunning", flags)
            ?? throw new InvalidOperationException("nativePaintMayBeRunning field missing");
        var activeField = typeof(HostSession).GetField("activePaintGeneration", flags)
            ?? throw new InvalidOperationException("activePaintGeneration field missing");
        var nextField = typeof(HostSession).GetField("nextPaintGeneration", flags)
            ?? throw new InvalidOperationException("nextPaintGeneration field missing");
        var dispatchGenerationField = typeof(HostSession).GetField("paintRequestDispatchGeneration", flags)
            ?? throw new InvalidOperationException("paintRequestDispatchGeneration field missing");
        runningProperty.SetValue(targetSession, running);
        nativeField.SetValue(targetSession, nativeMayBeRunning);
        activeField.SetValue(targetSession, activeGeneration);
        nextField.SetValue(targetSession, activeGeneration);
        dispatchGenerationField.SetValue(targetSession, activeGeneration);
    }
}

static void HostSessionCountsNativeCancelJobs()
{
    var none = new BridgeReply(
        true,
        true,
        "paint_cancel_requested",
        "paint cancel requested",
        """{"success":true,"metadata":{"cancelled_active_paint_jobs":0,"cancelled_queued_paint_jobs":0}}""");
    var active = new BridgeReply(
        true,
        true,
        "paint_cancel_requested",
        "paint cancel requested",
        """{"success":true,"metadata":{"cancelled_active_paint_jobs":1,"cancelled_queued_paint_jobs":2}}""");
    var latched = new BridgeReply(
        true,
        true,
        "paint_cancel_requested",
        "paint cancel requested",
        """{"success":true,"metadata":{"cancelled_active_paint_jobs":0,"cancelled_queued_paint_jobs":0,"cancel_latched_paint_request":true}}""");

    Assert(HostSession.CancelledPaintJobCount(none) == 0, "zero cancel counts should remain zero");
    Assert(HostSession.CancelledPaintJobCount(active) == 3, "active and queued cancel counts should be summed");
    Assert(HostSession.NativePaintRequestCancellationLatched(latched),
        "a native admission latch must distinguish an in-flight request from no active paint");
}

static void HostSessionKeepsCancellationPendingUntilNativeTerminalReply()
{
    HostSessionKeepsCancellationPendingUntilNativeTerminalReplyAsync().GetAwaiter().GetResult();
}

static async Task HostSessionKeepsCancellationPendingUntilNativeTerminalReplyAsync()
{
    using var temp = new TempHome();
    var session = new HostSession("host-cancel-state-test");
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var instanceId = Guid.Parse("01234567-89ab-cdef-0123-456789abcdef");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("ab", 32));
    var target = TargetProcessIdentity.Create(4242, 1, Path.Combine(Path.GetTempPath(), "cancel-state-game.exe"));
    var instance = new BridgeInstance(target, instanceId, token, hash, "bridge.dll", "injector.exe", "progress.json");
    instance.SetPort(port);
    SetActiveBridge(session.Runtime, instance, connected: true);

    var firstReceived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var secondReceived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var releaseFirstReply = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var releaseSecondReply = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var server = Task.Run(async () =>
    {
        for (var request = 0; request < 2; ++request)
        {
            using var accepted = await listener.AcceptTcpClientAsync();
            await using var stream = accepted.GetStream();
            using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
            await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
            _ = await reader.ReadLineAsync();
            await writer.WriteLineAsync(JsonSerializer.Serialize(new
            {
                success = true,
                stage = "hello",
                message = "ok",
                metadata = new { pid = 4242, instance_id = instanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
            }));
            var command = await reader.ReadLineAsync();
            Assert(command == "{\"type\":\"cancel_paint\"}", "cancel command should be authenticated before its reply");
            if (request == 0)
            {
                firstReceived.TrySetResult();
                await releaseFirstReply.Task;
            }
            else
            {
                secondReceived.TrySetResult();
                await releaseSecondReply.Task;
            }
            await writer.WriteLineAsync("{\"success\":true,\"stage\":\"paint_cancel_requested\",\"message\":\"paint cancel requested\",\"metadata\":{\"cancelled_active_paint_jobs\":1,\"cancelled_queued_paint_jobs\":0}}");
        }
    });

    SetHostPaintState(session, running: true, nativeMayBeRunning: false, activeGeneration: 1);
    var firstCancel = session.StopPaintAsync();
    await firstReceived.Task.WaitAsync(TimeSpan.FromSeconds(2));
    var secondCancel = await session.StopPaintAsync();
    Assert(secondCancel.Success && secondCancel.Message == "Paint: cancel requested.",
        "a second stop while the ACK is in flight should remain local");
    releaseFirstReply.SetResult();
    var firstResult = await firstCancel;
    Assert(firstResult.Success && firstResult.Message == "Paint: cancel requested.",
        "a controller-owned ACK must retain the concise pending cancellation state until its terminal reply");

    // Model the original paint request terminalizing before a later independently-sent cancel
    // ACK returns. The late ACK must not revive the pending state or block the next paint.
    SetHostPaintState(session, running: true, nativeMayBeRunning: false, activeGeneration: 2);
    var racedCancel = session.StopPaintAsync();
    await secondReceived.Task.WaitAsync(TimeSpan.FromSeconds(2));
    SetHostPaintState(session, running: false, nativeMayBeRunning: false, activeGeneration: 0);
    releaseSecondReply.SetResult();
    var racedResult = await racedCancel;
    Assert(racedResult.Success && racedResult.Message.Contains("terminalized", StringComparison.Ordinal),
        "a late cancel ACK must report the already-terminal paint without reviving it");
    var noActive = await session.StopPaintAsync();
    Assert(!noActive.Success && noActive.Message == "Paint: no active paint to cancel.",
        "a late ACK must leave the next paint/cancel state unblocked");
    await server;

    static void SetHostPaintState(HostSession targetSession, bool running, bool nativeMayBeRunning, int activeGeneration)
    {
        var flags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
        var runningProperty = typeof(HostSession).GetProperty(nameof(HostSession.PaintRunning))
            ?? throw new InvalidOperationException("PaintRunning property missing");
        var nativeField = typeof(HostSession).GetField("nativePaintMayBeRunning", flags)
            ?? throw new InvalidOperationException("nativePaintMayBeRunning field missing");
        var activeField = typeof(HostSession).GetField("activePaintGeneration", flags)
            ?? throw new InvalidOperationException("activePaintGeneration field missing");
        var nextField = typeof(HostSession).GetField("nextPaintGeneration", flags)
            ?? throw new InvalidOperationException("nextPaintGeneration field missing");
        var cancelStateField = typeof(HostSession).GetField("cancelState", flags)
            ?? throw new InvalidOperationException("cancelState field missing");
        var cancelGenerationField = typeof(HostSession).GetField("cancelPaintGeneration", flags)
            ?? throw new InvalidOperationException("cancelPaintGeneration field missing");
        var dispatchGenerationField = typeof(HostSession).GetField("paintRequestDispatchGeneration", flags)
            ?? throw new InvalidOperationException("paintRequestDispatchGeneration field missing");
        runningProperty.SetValue(targetSession, running);
        nativeField.SetValue(targetSession, nativeMayBeRunning);
        activeField.SetValue(targetSession, activeGeneration);
        nextField.SetValue(targetSession, activeGeneration);
        cancelStateField.SetValue(targetSession, Enum.ToObject(cancelStateField.FieldType, 0));
        cancelGenerationField.SetValue(targetSession, 0);
        dispatchGenerationField.SetValue(targetSession, activeGeneration);
    }
}

static void BridgeStartBlockHasFixedPortableLayout()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var token = Enumerable.Range(0, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = Enumerable.Range(0, BridgeStartBlockV1.HashLength).Select(value => (byte)(255 - value)).ToArray();
    var source = BridgeStartBlockV1.Create(4242, instanceId, token, hash);

    var bytes = source.Serialize();

    Assert(bytes.Length == BridgeStartBlockV1.Size, "start block size changed");
    Assert(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0, 4)) == BridgeStartBlockV1.Magic, "magic offset changed");
    Assert(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(4, 4)) == BridgeStartBlockV1.Size, "size offset changed");
    Assert(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(12, 4)) == 4242, "pid offset changed");
    Assert(Convert.ToHexString(bytes.AsSpan(16, 16)).ToLowerInvariant() == "00112233445566778899aabbccddeeff", "GUID must use RFC byte order");
    Assert(bytes.AsSpan(32, 32).SequenceEqual(token), "token offset changed");
    Assert(bytes.AsSpan(64, 32).SequenceEqual(hash), "hash offset changed");
    Assert(BridgeStartBlockV1.TryDeserialize(bytes, out var parsed, out var error), error);
    Assert(parsed.ExpectedPid == 4242 && parsed.InstanceId == instanceId, "start block did not round-trip");
    Assert(parsed.ConnectionToken.SequenceEqual(token), "token did not round-trip");
    Assert(parsed.ExpectedBridgeHash.SequenceEqual(hash), "hash did not round-trip");
}

static void InjectorResultRequiresMatchingBridgeIdentity()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var hash = string.Concat(Enumerable.Repeat("ab", 32));
    var raw = $$"""
    {"phase":"open_process","success":true,"pid":4242}
    {"event":"result","protocol":1,"success":true,"state":"listening","pid":4242,"instance_id":"{{instanceId:N}}","bridge_hash":"{{hash}}","port":54321,"win32":0,"winsock":0}
    """;

    Assert(InjectorResultV1.TryParseFinal(raw, out var result, out var error), error);
    Assert(result.Matches(4242, instanceId, hash), "matching result should be accepted");
    Assert(!result.Matches(4243, instanceId, hash), "wrong PID must be rejected");
    Assert(!result.Matches(4242, Guid.NewGuid(), hash), "wrong instance GUID must be rejected");
    Assert(!result.Matches(4242, instanceId, string.Concat(Enumerable.Repeat("cd", 32))), "wrong hash must be rejected");
}

static void BridgeHelloSerializesAndValidatesIdentity()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var token = Enumerable.Range(0, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("ab", 32));
    var endpoint = new BridgeEndpoint("127.0.0.1", 54321, instanceId, token, hash, BridgeProtocolV1.Version);
    var hello = BridgeProtocolV1.CreateHello(endpoint);
    using var request = JsonDocument.Parse(hello);
    Assert(request.RootElement.GetProperty("type").GetString() == "hello", "hello type missing");
    Assert(request.RootElement.GetProperty("instance_id").GetString() == instanceId.ToString("N"), "hello GUID missing");
    Assert(request.RootElement.GetProperty("token").GetString() == Convert.ToHexString(token).ToLowerInvariant(), "hello token missing");

    var reply = JsonSerializer.Serialize(new
    {
        success = true,
        stage = "hello",
        message = "ok",
        metadata = new { pid = 4242, instance_id = instanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
    });
    Assert(BridgeProtocolV1.TryValidateHelloReply(reply, endpoint, 4242, out _, out var error), error);
    Assert(!BridgeProtocolV1.TryValidateHelloReply(reply, endpoint, 4243, out _, out _), "wrong PID hello must be rejected");
}

static void BridgeClientSendsHelloBeforeCommand() => BridgeClientSendsHelloBeforeCommandAsync().GetAwaiter().GetResult();

static async Task BridgeClientSendsHelloBeforeCommandAsync()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var token = Enumerable.Range(0, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("ab", 32));
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var endpoint = new BridgeEndpoint("127.0.0.1", port, instanceId, token, hash, BridgeProtocolV1.Version);
    var commandAfterHello = "";
    var server = Task.Run(async () =>
    {
        using var accepted = await listener.AcceptTcpClientAsync();
        await using var stream = accepted.GetStream();
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
        var hello = await reader.ReadLineAsync();
        using var helloDocument = JsonDocument.Parse(hello ?? "");
        Assert(helloDocument.RootElement.GetProperty("type").GetString() == "hello", "client must send hello first");
        Assert(helloDocument.RootElement.GetProperty("token").GetString() == Convert.ToHexString(token).ToLowerInvariant(), "client hello token mismatch");
        await writer.WriteLineAsync(JsonSerializer.Serialize(new
        {
            success = true,
            stage = "hello",
            message = "ok",
            metadata = new { pid = 4242, instance_id = instanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
        }));
        commandAfterHello = await reader.ReadLineAsync() ?? "";
        await writer.WriteLineAsync("{\"success\":true,\"stage\":\"ping\",\"message\":\"pong\",\"metadata\":{\"pid\":4242}}");
    });

    var reply = await new BridgeClient(endpoint, 4242, TimeSpan.FromSeconds(2)).PingAsync();
    await server;
    Assert(reply.Ok && reply.Success, "authenticated ping should succeed");
    Assert(commandAfterHello == "{\"type\":\"ping\"}", "application command must follow hello on the same connection");

    using var rejectingListener = new TcpListener(IPAddress.Loopback, 0);
    rejectingListener.Start();
    var rejectingPort = ((IPEndPoint)rejectingListener.LocalEndpoint).Port;
    var rejectingEndpoint = new BridgeEndpoint("127.0.0.1", rejectingPort, instanceId, token, hash, BridgeProtocolV1.Version);
    var rejectedCommand = "sent";
    var rejectingServer = Task.Run(async () =>
    {
        using var accepted = await rejectingListener.AcceptTcpClientAsync();
        await using var stream = accepted.GetStream();
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
        _ = await reader.ReadLineAsync();
        await writer.WriteLineAsync("{\"success\":false,\"stage\":\"hello_rejected\",\"message\":\"token rejected\"}");
        rejectedCommand = await reader.ReadLineAsync() ?? "";
    });

    var rejected = await new BridgeClient(rejectingEndpoint, 4242, TimeSpan.FromSeconds(2)).PingAsync();
    await rejectingServer;
    Assert(!rejected.Ok && rejected.Stage == "hello_error", "rejected hello must stop the request");
    Assert(rejectedCommand.Length == 0, "client must not send an application command after a rejected token");
}

static void BridgeShutdownClientOutlivesNativeQuiescenceBudget() =>
    BridgeShutdownClientOutlivesNativeQuiescenceBudgetAsync().GetAwaiter().GetResult();

static void NativeStopPathsLatchInFlightPaintAdmission()
{
    var native = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    const string stopMarker = "auto request_bridge_stop() -> void";
    var start = native.IndexOf(stopMarker, StringComparison.Ordinal);
    Assert(start >= 0, "native stop path is missing");
    var end = native.IndexOf("auto handle_request", start, StringComparison.Ordinal);
    Assert(end > start, "native stop path must precede request dispatch");
    var stopPath = native[start..end];
    var closeAdmission = stopPath.IndexOf("g_accepting_bridge_commands.store(false", StringComparison.Ordinal);
    var latchAdmission = stopPath.IndexOf("latch_active_paint_request_cancel();", StringComparison.Ordinal);
    Assert(closeAdmission >= 0 && latchAdmission > closeAdmission,
        "shutdown must latch a paint handler already inside admission before sweeping jobs");

    const string listenerMarker = "auto bridge_thread(SOCKET listener, int bridge_port) -> void";
    var listenerStart = native.IndexOf(listenerMarker, StringComparison.Ordinal);
    Assert(listenerStart >= 0, "native listener loop is missing");
    var listenerEnd = native.IndexOf("namespace", listenerStart + listenerMarker.Length, StringComparison.Ordinal);
    Assert(listenerEnd > listenerStart, "native listener loop must have a bounded stop path");
    var listenerStop = native[listenerStart..listenerEnd];
    var listenerCloseAdmission = listenerStop.LastIndexOf("g_accepting_bridge_commands.store(false", StringComparison.Ordinal);
    var listenerLatchAdmission = listenerStop.LastIndexOf("latch_active_paint_request_cancel();", StringComparison.Ordinal);
    Assert(listenerCloseAdmission >= 0 && listenerLatchAdmission > listenerCloseAdmission,
        "listener failure must close and latch admission before its shutdown sweep");
}

static async Task BridgeShutdownClientOutlivesNativeQuiescenceBudgetAsync()
{
    var instanceId = Guid.Parse("22334455-6677-8899-aabb-ccddeeff0011");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("ef", 32));
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var endpoint = new BridgeEndpoint("127.0.0.1", port, instanceId, token, hash, BridgeProtocolV1.Version);
    var server = Task.Run(async () =>
    {
        using var accepted = await listener.AcceptTcpClientAsync();
        await using var stream = accepted.GetStream();
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
        _ = await reader.ReadLineAsync();
        await writer.WriteLineAsync(JsonSerializer.Serialize(new
        {
            success = true,
            stage = "hello",
            message = "ok",
            metadata = new { pid = 4242, instance_id = instanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
        }));
        _ = await reader.ReadLineAsync();
        await Task.Delay(TimeSpan.FromMilliseconds(5500));
        try
        {
            await writer.WriteLineAsync("{\"success\":true,\"stage\":\"shutdown\",\"message\":\"ok\",\"metadata\":{\"pid\":4242}}");
        }
        catch (IOException)
        {
            // The pre-fix five-second client timeout closes the connection before this response.
        }
    });

    var reply = await new BridgeClient(endpoint, 4242).ShutdownAsync();
    await server;
    Assert(reply.Ok && reply.Success,
        "the managed shutdown budget must exceed the native five-second paint-quiescence budget");
}

static void BridgeShutdownPermitsFreshInstance() => BridgeShutdownPermitsFreshInstanceAsync().GetAwaiter().GetResult();

static async Task BridgeShutdownPermitsFreshInstanceAsync()
{
    using var temp = new TempHome();
    var paths = new AppPaths("bridge-shutdown-reinjection-test");
    var service = new RuntimeBridgeService(paths, new RuntimeLog(paths));
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("ab", 32));
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var target = TargetProcessIdentity.Create(4242, 1, Path.Combine(Path.GetTempPath(), "game.exe"));
    var instance = new BridgeInstance(target, instanceId, token, hash, "bridge.dll", "injector.exe", "progress.json");
    instance.SetPort(port);

    var activeField = typeof(RuntimeBridgeService).GetField(
        "activeInstance",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("activeInstance field missing");
    var connectedField = typeof(RuntimeBridgeService).GetField(
        "bridgeConnected",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("bridgeConnected field missing");
    activeField.SetValue(service, instance);
    connectedField.SetValue(service, true);

    var command = "";
    var server = Task.Run(async () =>
    {
        using var accepted = await listener.AcceptTcpClientAsync();
        await using var stream = accepted.GetStream();
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
        _ = await reader.ReadLineAsync();
        await writer.WriteLineAsync(JsonSerializer.Serialize(new
        {
            success = true,
            stage = "hello",
            message = "ok",
            metadata = new { pid = 4242, instance_id = instanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
        }));
        command = await reader.ReadLineAsync() ?? "";
        await writer.WriteLineAsync("{\"success\":true,\"stage\":\"shutdown\",\"message\":\"ok\",\"metadata\":{\"pid\":4242}}");
    });

    var reply = await service.ShutdownAsync();
    await server;
    Assert(reply.Ok && reply.Success, "authenticated shutdown should succeed");
    Assert(command == "{\"type\":\"shutdown\"}", "shutdown command missing");
    Assert(!service.HasActiveBridgeInstance, "successful shutdown must release the controller instance for reinjection");
}

static void StaleBridgeShutdownPreservesReplacementInstance() =>
    StaleBridgeShutdownPreservesReplacementInstanceAsync().GetAwaiter().GetResult();

static async Task StaleBridgeShutdownPreservesReplacementInstanceAsync()
{
    using var temp = new TempHome();
    var paths = new AppPaths("bridge-stale-shutdown-test");
    var service = new RuntimeBridgeService(paths, new RuntimeLog(paths));
    var oldInstanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var replacementInstanceId = Guid.Parse("10213243-5465-7687-98a9-bacbdcedfe0f");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("ab", 32));
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var target = TargetProcessIdentity.Create(4242, 1, Path.Combine(Path.GetTempPath(), "game.exe"));
    var oldInstance = new BridgeInstance(target, oldInstanceId, token, hash, "old-bridge.dll", "injector.exe", "old-progress.json");
    oldInstance.SetPort(port);
    var replacementInstance = new BridgeInstance(target, replacementInstanceId, token, hash, "new-bridge.dll", "injector.exe", "new-progress.json");
    replacementInstance.SetPort(port == 65535 ? 65534 : port + 1);

    var activeField = typeof(RuntimeBridgeService).GetField(
        "activeInstance",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("activeInstance field missing");
    var connectedField = typeof(RuntimeBridgeService).GetField(
        "bridgeConnected",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("bridgeConnected field missing");
    activeField.SetValue(service, oldInstance);
    connectedField.SetValue(service, true);

    var shutdownReceived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var releaseShutdownResponse = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var server = Task.Run(async () =>
    {
        using var accepted = await listener.AcceptTcpClientAsync();
        await using var stream = accepted.GetStream();
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
        _ = await reader.ReadLineAsync();
        await writer.WriteLineAsync(JsonSerializer.Serialize(new
        {
            success = true,
            stage = "hello",
            message = "ok",
            metadata = new { pid = 4242, instance_id = oldInstanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
        }));
        _ = await reader.ReadLineAsync();
        shutdownReceived.SetResult();
        await releaseShutdownResponse.Task;
        await writer.WriteLineAsync("{\"success\":true,\"stage\":\"shutdown\",\"message\":\"ok\",\"metadata\":{\"pid\":4242}}");
    });

    var shutdownTask = service.ShutdownAsync();
    await shutdownReceived.Task.WaitAsync(TimeSpan.FromSeconds(2));
    activeField.SetValue(service, replacementInstance);
    connectedField.SetValue(service, true);
    releaseShutdownResponse.SetResult();

    var reply = await shutdownTask;
    await server;
    Assert(reply.Ok && reply.Success, "the old bridge shutdown response should still be returned to its caller");
    Assert(service.ActiveResearchBridgeIdentity?.InstanceId == replacementInstanceId,
        "an old shutdown response must not release a newer active bridge instance");
    Assert(service.IsConnected, "an old shutdown response must not disconnect a newer bridge instance");
}

static void StaleBridgeRequestPreservesReplacementConnectionState() =>
    StaleBridgeRequestPreservesReplacementConnectionStateAsync().GetAwaiter().GetResult();

static async Task StaleBridgeRequestPreservesReplacementConnectionStateAsync()
{
    using var temp = new TempHome();
    var paths = new AppPaths("bridge-stale-request-test");
    var service = new RuntimeBridgeService(paths, new RuntimeLog(paths));
    var oldInstanceId = Guid.Parse("11223344-5566-7788-99aa-bbccddeeff00");
    var replacementInstanceId = Guid.Parse("21324354-6576-8798-a9ba-cbdcedfe0f10");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("cd", 32));
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var target = TargetProcessIdentity.Create(4242, 1, Path.Combine(Path.GetTempPath(), "game.exe"));
    var oldInstance = new BridgeInstance(target, oldInstanceId, token, hash, "old-bridge.dll", "injector.exe", "old-progress.json");
    oldInstance.SetPort(port);
    var replacementInstance = new BridgeInstance(target, replacementInstanceId, token, hash, "new-bridge.dll", "injector.exe", "new-progress.json");
    replacementInstance.SetPort(port == 65535 ? 65534 : port + 1);

    var activeField = typeof(RuntimeBridgeService).GetField(
        "activeInstance",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("activeInstance field missing");
    var connectedField = typeof(RuntimeBridgeService).GetField(
        "bridgeConnected",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("bridgeConnected field missing");
    activeField.SetValue(service, oldInstance);
    connectedField.SetValue(service, true);

    var pingReceived = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var closeOldConnection = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var server = Task.Run(async () =>
    {
        using var accepted = await listener.AcceptTcpClientAsync();
        await using var stream = accepted.GetStream();
        using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
        await using var writer = new StreamWriter(stream, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true };
        _ = await reader.ReadLineAsync();
        await writer.WriteLineAsync(JsonSerializer.Serialize(new
        {
            success = true,
            stage = "hello",
            message = "ok",
            metadata = new { pid = 4242, instance_id = oldInstanceId.ToString("N"), bridge_hash = hash, protocol_version = 1 }
        }));
        _ = await reader.ReadLineAsync();
        pingReceived.SetResult();
        await closeOldConnection.Task;
    });

    var pingTask = service.PingAsync();
    await pingReceived.Task.WaitAsync(TimeSpan.FromSeconds(2));
    activeField.SetValue(service, replacementInstance);
    connectedField.SetValue(service, true);
    closeOldConnection.SetResult();

    var reply = await pingTask;
    await server;
    Assert(!reply.Ok && reply.Stage == "empty_response", "the stale request should report the old bridge's transport failure");
    Assert(service.ActiveResearchBridgeIdentity?.InstanceId == replacementInstanceId,
        "a stale request must not replace the newer active bridge identity");
    Assert(service.IsConnected, "a stale request completion must not disconnect a newer bridge instance");
}

static void RuntimeExposesExactPidBridgeStartup()
{
    var byPid = typeof(RuntimeBridgeService).GetMethod(
        nameof(RuntimeBridgeService.EnsureReadyAsync),
        [typeof(int), typeof(CancellationToken)]);
    var byProcess = typeof(RuntimeBridgeService).GetMethod(
        nameof(RuntimeBridgeService.EnsureReadyAsync),
        [typeof(System.Diagnostics.Process), typeof(CancellationToken)]);
    var researchByPid = typeof(RuntimeBridgeService).GetMethod(
        nameof(RuntimeBridgeService.EnsureResearchReadyAsync),
        [typeof(int), typeof(ResearchBridgeOptions), typeof(CancellationToken)]);

    Assert(byPid is not null, "runtime must expose exact PID startup");
    Assert(byProcess is not null, "runtime must accept a caller-selected Process");
    Assert(researchByPid is not null, "research runtime must expose exact PID startup");
}

static void ResearchTextureProbeIsExplicitlyDispatched()
{
    var root = FindRepositoryRoot();
    var native = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    const string command = "paint_replication_texture_probe";
    var serializedCommand = "\\\"type\\\":\\\"" + command + "\\\"";

    Assert(native.Contains(serializedCommand, StringComparison.Ordinal),
        "native probe recognition must include the texture command");
    Assert(native.Contains("line.find(\"" + serializedCommand + "\")", StringComparison.Ordinal),
        "bridge request dispatch must include the texture command");
    Assert(native.Contains("matches_texture_export_target", StringComparison.Ordinal),
        "texture command must retain the component selected for its export");
    Assert(native.Contains("eventwatch_multicast_packed_receiver", StringComparison.Ordinal),
        "texture command must be able to pin the watched multicast receiver rather than the local pawn");
    Assert(native.Contains("research_texture_target_unavailable", StringComparison.Ordinal),
        "an unobserved or stale multicast receiver must fail closed");
}

static void NativeResearchReplayPlanPreservesActualPassStrokes()
{
    var native = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(native.Contains("research_uv_replay_atlas", StringComparison.Ordinal),
        "native paint requests should explicitly gate UV replay sidecars to research mode");
    Assert(native.Contains("mesh_first_write_uv_replay_plan_artifact", StringComparison.Ordinal),
        "native bridge should write the actual replay-plan sidecar after planning");
    Assert(native.Contains("research_uv_replay_plan_written", StringComparison.Ordinal),
        "native reply must say whether the replay-plan sidecar was written");
    Assert(native.Contains("effective_fill_end", StringComparison.Ordinal) && native.Contains("effective_coarse_end", StringComparison.Ordinal),
        "native replay sidecar must use the post-truncation pass boundaries");
}

static void ResearchRunnerCanIsolateOnePlannedReplayStroke()
{
    var root = FindRepositoryRoot();
    var runner = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "ResearchRunner.cs"));
    var native = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(runner.Contains("--replay-stroke-index", StringComparison.Ordinal),
        "research runner should expose an explicit one-stroke replay selector");
    Assert(runner.Contains("research_replay_stroke_index", StringComparison.Ordinal),
        "research runner should serialize the one-stroke selector only into research payloads");
    Assert(native.Contains("research_replay_stroke_index", StringComparison.Ordinal),
        "native planner should read the research-only replay selector");
    Assert(native.Contains("research_replay_stroke_index_invalid", StringComparison.Ordinal),
        "native planner should reject an out-of-range selected replay stroke before dispatch");
    Assert(native.Contains("replay_plan.entries = {selected_entry};", StringComparison.Ordinal),
        "native selector should rebuild pass boundaries from exactly the selected entry");
}

static void ResearchRunnerRecordsTwoPassBrushesAndPackedLocalQueueMode()
{
    var root = FindRepositoryRoot();
    var source = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "ResearchRunner.cs"));
    var runtime = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.Controller", "RuntimeBridgeService.cs"));
    var native = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(source.Contains("brush_1_enabled = paint.Brush1Enabled", StringComparison.Ordinal), "research artifacts should record brush 1 enabled");
    Assert(source.Contains("brush_1_size_texels = paint.Brush1SizeTexels", StringComparison.Ordinal), "research artifacts should record brush 1");
    Assert(source.Contains("brush_2_enabled = paint.Brush2Enabled", StringComparison.Ordinal), "research artifacts should record brush 2 enabled");
    Assert(source.Contains("brush_2_size_texels = paint.Brush2SizeTexels", StringComparison.Ordinal), "research artifacts should record brush 2");
    Assert(!source.Contains("brush_pipeline_version = 2", StringComparison.Ordinal), "research artifacts should not version the brush pipeline");
    Assert(source.Contains("paintMode != \"packed-local-queue\"", StringComparison.Ordinal), "research runner should accept packed-local-queue mode");
    Assert(source.Contains("GetValueOrDefault(\"--paint-mode\", \"packed-local-queue\")", StringComparison.Ordinal), "research runner should default to the production-shaped packed local queue route");
    Assert(source.Contains("research_uv_replay_atlas", StringComparison.Ordinal), "research paint should explicitly request a pass-aware UV replay atlas");
    Assert(source.Contains("ResearchUvReplayArtifacts.StageAndRender", StringComparison.Ordinal), "research runs should retain the native replay plan and render its PNG atlas");
    Assert(source.Contains("ResearchTextureDeltaArtifacts.StageAndRender", StringComparison.Ordinal), "texture snapshot runs should retain an actual changed-pixel PNG mask");
    Assert(source.Contains("--texture-discovery-seconds", StringComparison.Ordinal) &&
           source.Contains("WaitForMulticastPackedReceiverAsync", StringComparison.Ordinal),
        "joining texture evidence should wait for and pin an observed packed multicast receiver");
    Assert(runtime.Contains("research_texture_expected_component", StringComparison.Ordinal) &&
           native.Contains("research_texture_target_pin_mismatch", StringComparison.Ordinal),
        "joining texture probes must send the discovered receiver address back to the native bridge as a fail-closed pin");
    Assert(source.Contains("TryNormalizeNonZeroHexAddress", StringComparison.Ordinal) &&
           source.Contains("ulong.TryParse", StringComparison.Ordinal),
        "joining receiver discovery must require a strict non-zero hexadecimal component address");
    Assert(native.Contains("selected_texture_target_only", StringComparison.Ordinal),
        "texture diagnostics must avoid unrelated component readbacks that perturb joining-client timing");
    Assert(source.Contains("CancelPaintAfterDelayAsync(session.Runtime, cancelAfterMs, paintTask)", StringComparison.Ordinal) &&
           source.Contains("cancel_admission_latched", StringComparison.Ordinal) &&
           native.Contains("cancel_latched_paint_request", StringComparison.Ordinal),
        "research cancellation must retain an admission-time cancel latch instead of misreporting no active job");
    Assert(source.Contains("textureSnapshot && shutdownAfterMs is not null", StringComparison.Ordinal),
        "a texture snapshot must reject scheduled shutdown because it cannot safely produce an after image");
}

static void UvReplayAtlasSeparatesPassesAndPackedRadii()
{
    var plan = new UvReplayPlan(
        TextureSize: 128,
        Strokes:
        [
            new UvReplayStroke(0.25, 0.25, 0.10, 0.20, UvReplayPass.Fill, "front", "torso"),
            new UvReplayStroke(0.50, 0.50, 0.08, 0.16, UvReplayPass.CoarsePaint, "side", "arm"),
            new UvReplayStroke(0.75, 0.75, 0.04, 0.08, UvReplayPass.FinePaint, "back", "arm")
        ]);

    var atlas = UvReplayAtlasRasterizer.Render(plan, tileSize: 64);
    Assert(atlas.Width == 192 && atlas.Height == 128, "the atlas should be a three-pass by two-radius grid");

    var plannerFillCenter = atlas.RgbaAt(16, 47);
    var packedFillCenter = atlas.RgbaAt(16, 111);
    Assert(plannerFillCenter.SequenceEqual(UvReplayAtlasRasterizer.FillColor), "fill must occupy the planner row");
    Assert(packedFillCenter.SequenceEqual(UvReplayAtlasRasterizer.FillColor), "fill must occupy the packed row");
    Assert(atlas.RgbaAt(26, 47).SequenceEqual(UvReplayAtlasRasterizer.BackgroundColor),
        "planner radius should remain independent from the packed radius");
    Assert(atlas.RgbaAt(26, 111).SequenceEqual(UvReplayAtlasRasterizer.FillColor),
        "packed row should render the packed wire radius rather than the planner radius");
    Assert(atlas.RgbaAt(175, 16).SequenceEqual(UvReplayAtlasRasterizer.FineColor),
        "brush 2 must occupy the fine-pass column rather than the coarse column");

    var directOnly = UvReplayAtlasRasterizer.Render(
        new UvReplayPlan(
            65_536,
            [new UvReplayStroke(0.5, 0.5, 0.1, 0.2, UvReplayPass.FinePaint, "front", "arm", false)]),
        tileSize: 64);
    Assert(directOnly.RgbaAt(170, 96).SequenceEqual(UvReplayAtlasRasterizer.BackgroundColor),
        "an unencoded direct route must not fabricate a packed-wire brush footprint");
    var bounded = UvReplayAtlasRasterizer.Render(new UvReplayPlan(65_536, []));
    Assert(bounded.Width == 3_072 && bounded.Height == 2_048,
        "a large game texture should produce a bounded proportional atlas rather than fail or allocate at source size");

    var directory = Path.Combine(Path.GetTempPath(), "meccha-uv-atlas-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(directory);
        var output = Path.Combine(directory, "uv-replay-atlas.png");
        UvReplayAtlasPng.Write(output, atlas);
        var bytes = File.ReadAllBytes(output);
        Assert(bytes.Length > 24, "PNG output should not be empty");
        Assert(bytes.AsSpan(0, 8).SequenceEqual(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }),
            "UV replay artifact must be a PNG rather than a screenshot or BMP");
    }
    finally
    {
        try { Directory.Delete(directory, recursive: true); } catch { }
    }
}

static void ResearchReplaySidecarIsStagedAsUvPng()
{
    var directory = Path.Combine(Path.GetTempPath(), "meccha-uv-sidecar-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(directory);
        var source = Path.Combine(directory, "native-plan.json");
        File.WriteAllText(source, """
        {
          "schema": "meccha_uv_replay_plan_v1",
          "texture_size": 64,
          "strokes": [
            { "u": 0.5, "v": 0.5, "planner_radius_uv": 0.1, "packed_wire_radius_uv": 0.2, "pass": "fine_paint", "region": "front", "body_region": "arm" }
          ]
        }
        """);
        var raw = JsonSerializer.Serialize(new
        {
            metadata = new
            {
                research_uv_replay_plan_written = true,
                research_uv_replay_plan_path = source
            }
        });

        var artifact = ResearchUvReplayArtifacts.StageAndRender(
            new BridgeReply(true, true, "mesh_replay_complete", "ok", raw),
            Path.Combine(directory, "run"));

        Assert(artifact.Success, "a valid native sidecar should be copied and rendered");
        Assert(File.Exists(artifact.PlanPath), "the native stroke plan should be retained with the run artifacts");
        Assert(File.Exists(artifact.AtlasPath), "the staged run should include a PNG UV replay atlas");
    }
    finally
    {
        try { Directory.Delete(directory, recursive: true); } catch { }
    }
}

static void ResearchReplaySidecarRefusesNonSuccessfulPaint()
{
    using var temp = new TempHome();
    var raw = JsonSerializer.Serialize(new
    {
        metadata = new
        {
            research_uv_replay_plan_written = true,
            research_uv_replay_plan_path = Path.Combine(Path.GetTempPath(), "must-not-be-staged.json")
        }
    });

    var artifact = ResearchUvReplayArtifacts.StageAndRender(
        new BridgeReply(true, false, "mesh_paint_cancelled", "paint cancelled", raw),
        Path.Combine(Path.GetTempPath(), "meccha-uv-non-success-" + Guid.NewGuid().ToString("N")));

    Assert(!artifact.Success && artifact.Error.Contains("intentionally not staged", StringComparison.Ordinal),
        "a cancellation must not retain a planning-time UV sidecar as rendered evidence");
}

static void ResearchTextureProbesStageActualDeltaPng()
{
    var directory = Path.Combine(Path.GetTempPath(), "meccha-texture-delta-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(directory);
        var beforeRaw = Path.Combine(directory, "before.rgba");
        var afterRaw = Path.Combine(directory, "after.rgba");
        File.WriteAllBytes(beforeRaw,
        [
            0, 0, 0, 255, 0, 0, 0, 255,
            0, 0, 0, 255, 0, 0, 0, 255
        ]);
        File.WriteAllBytes(afterRaw,
        [
            0, 0, 0, 255, 0, 0, 0, 255,
            0, 0, 0, 255, 255, 0, 255, 255
        ]);
        var beforeArtifact = Path.Combine(directory, "texture-before.json");
        var afterArtifact = Path.Combine(directory, "texture-after.json");
        File.WriteAllText(beforeArtifact, TextureProbeArtifact(beforeRaw, baselineComponentMatch: false));
        File.WriteAllText(afterArtifact, TextureProbeArtifact(afterRaw, baselineComponentMatch: true));

        var result = ResearchTextureDeltaArtifacts.StageAndRender(beforeArtifact, afterArtifact, Path.Combine(directory, "run"));

        Assert(result.Success, result.Error);
        Assert(result.TextureSize == 2 && result.ChangedPixels == 1, "the texture delta should preserve its dimensions and changed pixel count");
        Assert(File.Exists(result.BeforePngPath) && File.Exists(result.AfterPngPath) && File.Exists(result.DeltaMaskPath),
            "research texture probes should retain before, after, and changed-pixel PNGs");
    }
    finally
    {
        try { Directory.Delete(directory, recursive: true); } catch { }
    }

    static string TextureProbeArtifact(string path, bool baselineComponentMatch)
    {
        var native = JsonSerializer.Serialize(new
        {
            metadata = new
            {
                research_texture_export_target_component = "0x0000000000000042",
                research_texture_export_target_source = "resolved_component",
                runtime_paint_component_inventory = new[]
                {
                    new
                    {
                        component = "0x0000000000000042",
                        outer = "0x0000000000000007",
                        matches_resolved_component = true,
                        matches_texture_export_target = true,
                        texture_delta = new
                        {
                            albedo_dump_written = true,
                            albedo_dump_texture_size = 2,
                            albedo_dump_path = path,
                            baseline_component_match = baselineComponentMatch
                        }
                    }
                }
            }
        });
        return JsonSerializer.Serialize(new { Reply = new { Raw = native } });
    }
}

static void ResearchTextureProbesRejectComponentSwitch()
{
    var directory = Path.Combine(Path.GetTempPath(), "meccha-texture-switch-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(directory);
        var beforeRaw = Path.Combine(directory, "before.rgba");
        var afterRaw = Path.Combine(directory, "after.rgba");
        File.WriteAllBytes(beforeRaw, [0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]);
        File.WriteAllBytes(afterRaw, [255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]);
        var beforeNative = JsonSerializer.Serialize(new
        {
            metadata = new
            {
                research_texture_export_target_component = "0x42",
                runtime_paint_component_inventory = new[]
                {
                    new
                    {
                        component = "0x42",
                        outer = "0x7",
                        matches_texture_export_target = true,
                        texture_delta = new
                        {
                            albedo_dump_written = true,
                            albedo_dump_texture_size = 2,
                            albedo_dump_path = beforeRaw,
                            baseline_component_match = false
                        }
                    }
                }
            }
        });
        var afterNative = JsonSerializer.Serialize(new
        {
            metadata = new
            {
                research_texture_export_target_component = "0x43",
                runtime_paint_component_inventory = new[]
                {
                    new
                    {
                        component = "0x43",
                        outer = "0x8",
                        matches_texture_export_target = true,
                        texture_delta = new
                        {
                            albedo_dump_written = true,
                            albedo_dump_texture_size = 2,
                            albedo_dump_path = afterRaw,
                            baseline_component_match = true
                        }
                    }
                }
            }
        });
        var beforeArtifact = Path.Combine(directory, "before.json");
        var afterArtifact = Path.Combine(directory, "after.json");
        File.WriteAllText(beforeArtifact, JsonSerializer.Serialize(new { Reply = new { Raw = beforeNative } }));
        File.WriteAllText(afterArtifact, JsonSerializer.Serialize(new { Reply = new { Raw = afterNative } }));

        var result = ResearchTextureDeltaArtifacts.StageAndRender(beforeArtifact, afterArtifact, Path.Combine(directory, "run"));

        Assert(!result.Success && result.Error.Contains("different component addresses", StringComparison.Ordinal),
            "a pointer switch must not be rendered as a texture delta");
    }
    finally
    {
        try { Directory.Delete(directory, recursive: true); } catch { }
    }
}

static void ResearchTextureProbesRejectUnexpectedDiscoveryReceiver()
{
    var directory = Path.Combine(Path.GetTempPath(), "meccha-texture-discovery-pin-" + Guid.NewGuid().ToString("N"));
    try
    {
        Directory.CreateDirectory(directory);
        var beforeRaw = Path.Combine(directory, "before.rgba");
        var afterRaw = Path.Combine(directory, "after.rgba");
        File.WriteAllBytes(beforeRaw, [0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]);
        File.WriteAllBytes(afterRaw, [255, 0, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255]);
        var native = JsonSerializer.Serialize(new
        {
            metadata = new
            {
                research_texture_export_target_component = "0x42",
                runtime_paint_component_inventory = new[]
                {
                    new
                    {
                        component = "0x42",
                        outer = "0x7",
                        matches_texture_export_target = true,
                        texture_delta = new
                        {
                            albedo_dump_written = true,
                            albedo_dump_texture_size = 2,
                            albedo_dump_path = beforeRaw,
                            baseline_component_match = false
                        }
                    }
                }
            }
        });
        var afterNative = native.Replace(beforeRaw, afterRaw, StringComparison.Ordinal);
        var beforeArtifact = Path.Combine(directory, "before.json");
        var afterArtifact = Path.Combine(directory, "after.json");
        File.WriteAllText(beforeArtifact, JsonSerializer.Serialize(new { Reply = new { Raw = native } }));
        File.WriteAllText(afterArtifact, JsonSerializer.Serialize(new { Reply = new { Raw = afterNative } }));

        var result = ResearchTextureDeltaArtifacts.StageAndRender(
            beforeArtifact,
            afterArtifact,
            Path.Combine(directory, "run"),
            expectedComponent: "0x43");

        Assert(!result.Success && result.Error.Contains("pinned discovery receiver", StringComparison.Ordinal),
            "a probe must not render a delta when it differs from the receiver chosen during discovery");
    }
    finally
    {
        try { Directory.Delete(directory, recursive: true); } catch { }
    }
}

static void WebStartupLifecycleStabilizesAfterNavigationAndUiReady()
{
    var lifecycle = new WebViewStartupLifecycle();
    var first = lifecycle.Begin();

    Assert(lifecycle.RegisterInitialNavigation(first, 101), "initial navigation must be registered");
    Assert(!lifecycle.IsInitialNavigation(first, 102), "later navigations must not be treated as startup");
    Assert(!lifecycle.MarkNavigationSucceeded(first, 102), "a non-startup navigation must not stabilize the window");
    Assert(!lifecycle.MarkNavigationSucceeded(first, 101), "navigation alone must not stabilize the window");
    Assert(lifecycle.MarkUiReady(first), "uiReady after successful navigation must request one stabilization");
    Assert(!lifecycle.MarkUiReady(first), "duplicate uiReady must not queue another stabilization");

    var second = lifecycle.Begin();
    Assert(!lifecycle.RegisterInitialNavigation(first, 303), "stale WebView generations must not register navigation");
    Assert(lifecycle.RegisterInitialNavigation(second, 202), "replacement WebView must register its own startup navigation");
    Assert(!lifecycle.MarkUiReady(first), "stale WebView generations must be ignored");
    Assert(!lifecycle.MarkUiReady(second), "uiReady alone must not stabilize the window");
    Assert(lifecycle.MarkNavigationSucceeded(second, 202), "navigation after uiReady must request one stabilization");
}

static void DirectBridgeNamesAvoidHistoricalLoaderPattern()
{
    var hash = string.Concat(Enumerable.Repeat("0123456789abcdef", 4));
    var name = BridgeInstanceNaming.CreateBridgeFileName(hash, Guid.Parse("00112233-4455-6677-8899-aabbccddeeff"));
    Assert(name.StartsWith("meccha-direct-bridge-v1-", StringComparison.Ordinal), "direct bridge prefix missing");
    Assert(name.Contains(hash, StringComparison.Ordinal), "direct bridge must include its full build hash");
    Assert(!name.Contains("runtime-bridge", StringComparison.OrdinalIgnoreCase), "historical loader pattern must not be used");
}

static void ReleasePackagingContainsOnlyDirectBridge()
{
    var root = FindRepositoryRoot();
    var build = File.ReadAllText(Path.Combine(root, "scripts", "build.ps1"));
    var project = File.ReadAllText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "MecchaCamouflage.WebHost.csproj"));
    var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "ci.yml"));
    var directDoc = Path.Combine(root, "docs", "runtime-direct-bridge.md");
    var oldLoaderSource = Path.Combine(root, "src", "native", "loader", "loader.cpp");
    var oldLoaderAbi = Path.Combine(root, "src", "native", "include", "bridge_loader_abi.hpp");

    Assert(!build.Contains("bridge-loader.dll", StringComparison.OrdinalIgnoreCase), "build must not produce a loader DLL");
    Assert(!project.Contains("bridge-loader.dll", StringComparison.OrdinalIgnoreCase), "single EXE must not embed a loader DLL");
    Assert(workflow.Contains("bridge loader must not be packaged", StringComparison.OrdinalIgnoreCase), "CI must reject a packaged loader DLL");
    Assert(!File.Exists(oldLoaderSource), "obsolete loader source must be removed");
    Assert(!File.Exists(oldLoaderAbi), "obsolete loader ABI must be removed");
    Assert(File.Exists(directDoc), "direct bridge injection must have one authoritative design document");
    Assert(!build.Contains("FixedVersionRuntime", StringComparison.OrdinalIgnoreCase), "build must not download a Fixed WebView2 Runtime");
    Assert(!project.Contains("MecchaWebView2RuntimeDir", StringComparison.OrdinalIgnoreCase), "single EXE must not embed a Fixed WebView2 Runtime");
    Assert(project.Contains("webview2-bootstrapper", StringComparison.OrdinalIgnoreCase), "single EXE must embed the Evergreen bootstrapper");
    Assert(build.Contains("/p:DebugSymbols=false", StringComparison.Ordinal) &&
           build.Contains("/p:DebugType=None", StringComparison.Ordinal) &&
           build.Contains("/p:CopyOutputSymbolsToPublishDirectory=false", StringComparison.Ordinal) &&
           build.Contains("ReleaseSingleFile output contains debug artifacts", StringComparison.Ordinal),
        "ReleaseSingleFile builds must suppress and reject debug symbol sidecars");
    var release = File.ReadAllText(Path.Combine(root, "scripts", "release.ps1"));
    Assert(release.Contains("Release output directory contains debug artifacts", StringComparison.Ordinal),
        "release packaging must reject a package directory containing debug sidecars");
}

static void ReleaseBuildExcludesResearchRunnerAndDevTools()
{
    var root = FindRepositoryRoot();
    var project = File.ReadAllText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "MecchaCamouflage.WebHost.csproj"));
    var program = File.ReadAllText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "Program.cs"));
    var form = File.ReadAllText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var researchBuild = File.ReadAllText(Path.Combine(root, "scripts", "research", "build-replication-runner.ps1"));

    Assert(project.Contains("<Compile Remove=\"ResearchRunner.cs\" />", StringComparison.Ordinal) &&
           project.Contains("MecchaResearchBuild", StringComparison.Ordinal),
        "the normal WebHost build must omit the research runner source");
    Assert(program.Contains("#if MECCHA_RESEARCH_BUILD", StringComparison.Ordinal) &&
           form.Contains("BuildFeatures.ResearchArtifactsEnabled", StringComparison.Ordinal),
        "research command dispatch and DevTools must be unavailable in a normal release build");
    Assert(researchBuild.Contains("/p:MecchaResearchBuild=true", StringComparison.Ordinal),
        "the explicit research build must opt in to the runner");
}

static string FindRepositoryRoot()
{
    for (var directory = new DirectoryInfo(Directory.GetCurrentDirectory()); directory is not null; directory = directory.Parent)
    {
        if (File.Exists(Path.Combine(directory.FullName, "scripts", "build.ps1")))
            return directory.FullName;
    }
    throw new DirectoryNotFoundException("Repository root could not be found.");
}

static void ConfigureLiveProgressSession(HostSession session, string preferredProgressPath)
{
    var target = TargetProcessIdentity.Create(
        Environment.ProcessId,
        1,
        Path.Combine(Path.GetTempPath(), "progress-test-game.exe"));
    var instance = new BridgeInstance(
        target,
        Guid.NewGuid(),
        Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray(),
        string.Concat(Enumerable.Repeat("ab", 32)),
        "bridge.dll",
        "injector.exe",
        preferredProgressPath);
    var activeField = typeof(RuntimeBridgeService).GetField(
        "activeInstance",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("activeInstance field missing");
    activeField.SetValue(session.Runtime, instance);

    var startedField = typeof(HostSession).GetField(
        "currentPaintStartedAt",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("currentPaintStartedAt field missing");
    var serverProgressField = typeof(HostSession).GetField(
        "currentProgressIsServerPaint",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("currentProgressIsServerPaint field missing");
    var paintRunningProperty = typeof(HostSession).GetProperty(nameof(HostSession.PaintRunning))
        ?? throw new InvalidOperationException("PaintRunning property missing");
    startedField.SetValue(session, DateTimeOffset.UtcNow.AddSeconds(-1));
    serverProgressField.SetValue(session, true);
    paintRunningProperty.SetValue(session, true);
}

static void SetActiveBridge(RuntimeBridgeService service, BridgeInstance instance, bool connected)
{
    var flags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
    var activeField = typeof(RuntimeBridgeService).GetField("activeInstance", flags)
        ?? throw new InvalidOperationException("activeInstance field missing");
    var connectedField = typeof(RuntimeBridgeService).GetField("bridgeConnected", flags)
        ?? throw new InvalidOperationException("bridgeConnected field missing");
    activeField.SetValue(service, instance);
    connectedField.SetValue(service, connected);
}

static int CountOccurrences(string text, string value)
{
    var count = 0;
    var offset = 0;
    while ((offset = text.IndexOf(value, offset, StringComparison.Ordinal)) >= 0)
    {
        ++count;
        offset += value.Length;
    }
    return count;
}

static void Assert(bool condition, string message)
{
    if (!condition)
        throw new InvalidOperationException(message);
}

sealed class TempHome : IDisposable
{
    private readonly string oldLocalAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
    private readonly string temp = Path.Combine(Path.GetTempPath(), "meccha-tests-" + Guid.NewGuid().ToString("N"));

    public TempHome()
    {
        Directory.CreateDirectory(temp);
        Environment.SetEnvironmentVariable("LOCALAPPDATA", temp);
    }

    public void Dispose()
    {
        Environment.SetEnvironmentVariable("LOCALAPPDATA", oldLocalAppData);
        try { Directory.Delete(temp, true); } catch { }
    }
}
