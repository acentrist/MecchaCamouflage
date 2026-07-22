using System.Text.Json;
using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Text;
using MecchaCamouflage.Controller;
using MecchaCamouflage.Core;

var tests = new List<(string Name, Action Run)>
{
    ("paint defaults expose a single brush", PaintDefaultsExposeSingleBrush),
    ("single brush persists and migrates legacy detail settings", SingleBrushPersistsAndMigratesLegacyDetailSettings),
    ("single brush settings clamp to supported range", SingleBrushSettingsClampToSupportedRange),
    ("app defaults use 99 percent opacity", AppDefaultsUse99PercentOpacity),
    ("payload sends a single brush and compression tolerance", PayloadSendsSingleBrushPipeline),
    ("custom freecam surface is absent", CustomFreecamSurfaceIsAbsent),
    ("spectator paint resolution requires local controller identity", SpectatorPaintResolutionRequiresLocalControllerIdentity),
    ("diagnostic stroke limit requires explicit option", DiagnosticStrokeLimitRequiresExplicitOption),
    ("native accepts the single brush configured range", NativeAcceptsSingleBrushConfiguredRange),
    ("native direct radius uses game defaults and fill stays fixed", NativeDirectRadiusUsesGameDefaultsAndFillStaysFixed),
    ("native spatial replay follows the current pose and camera", NativeSpatialReplayFollowsCurrentPoseAndCamera),
    ("native async paint retains captured component identity", NativeAsyncPaintRetainsCapturedComponentIdentity),
    ("native production local sync uses per-stroke paint", NativeProductionLocalSyncUsesPerStrokePaint),
    ("native preview applies PBR and emissive channels", NativePreviewAppliesPbrAndEmissiveChannels),
    ("native preview returns before recorded-stroke dispatch", NativePreviewReturnsBeforeRecordedStrokeDispatch),
    ("native auto material detects emissive and reports local pacing", NativeAutoMaterialDetectsEmissiveAndReportsLocalPacing),
    ("payload uses native paint route and includes fill material", PayloadUsesNativePaintRouteAndFillMaterial),
    ("legacy mirror-like Fill PBR defaults migrate to manual material", LegacyFillPbrDefaultsMigrateToManualMaterial),
    ("locales have complete keys", LocalesHaveCompleteKeys),
    ("color parser accepts rrggbb", ColorParserAcceptsHex),
    ("runtime log keeps repeated guard messages", RuntimeLogKeepsRepeatedGuardMessages),
    ("asset validation rejects stale ready cache", AssetValidationRejectsStaleReadyCache),
    ("copy if invalid repairs corrupt target", CopyIfInvalidRepairsCorruptTarget),
    ("research event-watch sidecar uses exact staged bridge path", ResearchEventWatchSidecarUsesExactStagedBridgePath),
    ("research texture probe is explicitly dispatched", ResearchTextureProbeIsExplicitlyDispatched),
    ("research runner can isolate one planned replay stroke", ResearchRunnerCanIsolateOnePlannedReplayStroke),
    ("research runner records a single brush and direct queue mode", ResearchRunnerRecordsSingleBrushAndDirectQueueMode),
    ("UV replay atlas separates Fill and Paint", UvReplayAtlasSeparatesFillAndPaint),
    ("research replay sidecar is staged as a UV PNG", ResearchReplaySidecarIsStagedAsUvPng),
    ("research replay sidecar refuses a non-successful paint", ResearchReplaySidecarRefusesNonSuccessfulPaint),
    ("research texture probes stage an actual delta PNG", ResearchTextureProbesStageActualDeltaPng),
    ("research texture probes reject a component switch", ResearchTextureProbesRejectComponentSwitch),
    ("research texture probes reject an unexpected discovery receiver", ResearchTextureProbesRejectUnexpectedDiscoveryReceiver),
    ("diagnostic summary includes file not found details", DiagnosticSummaryIncludesFileNotFoundDetails),
    ("diagnostics log write is best effort when file is locked", DiagnosticsLogWriteIsBestEffortWhenFileLocked),
    ("runtime log write is best effort when file is locked", RuntimeLogWriteIsBestEffortWhenFileLocked),
    ("auto material defaults off", AutoMaterialDefaultsOff),
    ("regions default to side and back paint", RegionsDefaultToSideAndBackPaint),
    ("bridge messages are user friendly", BridgeMessagesAreUserFriendly),
    ("settings detect supported system language", SettingsDetectSupportedSystemLanguage),
    ("ui snapshot exposes a single brush", UiSnapshotExposesSingleBrush),
    ("web ui exposes one brush slider and compression tolerance", WebUiExposesSingleBrushSliderAndCompressionTolerance),
    ("web ui image picker previews fit crop and transparency", WebUiImagePickerPreviewsFitCropAndTransparency),
    ("web UI keeps theme color on readonly range and checkbox controls", WebUiKeepsThemeColorOnReadonlyControls),
    ("web ui renders pass progress and total eta", WebUiRendersPassProgressAndTotalEta),
    ("raw hotkeys suppress repeat until key-up", RawHotkeysSuppressRepeatUntilKeyUp),
    ("raw hotkeys do not reserve system keys", RawHotkeysDoNotReserveSystemKeys),
    ("native progress exposes replay pass state", NativeProgressExposesReplayPassState),
    ("hotkey validation rejects duplicates", HotkeyValidationRejectsDuplicates),
    ("host session reset restores setting default", HostSessionResetRestoresDefault),
    ("host session updates a single brush", HostSessionUpdatesSingleBrush),
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
    ("host session presents native pass progress and queue backpressure", HostSessionPresentsNativePassProgressAndQueueBackpressure),
    ("host session logs each pass transition once per job", HostSessionLogsEachPassTransitionOnce),
    ("paint diagnostics report direct-stroke PBR values", PaintDiagnosticsReportDirectStrokePbrValues),
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
    ("app close shuts down the active bridge", AppCloseShutsDownActiveBridge),
    ("native process event accepts a resident direct bridge hook", NativeProcessEventAcceptsResidentDirectBridgeHook),
    ("runtime launch stages a local Windows copy", RuntimeLaunchStagesLocalWindowsCopy),
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

static void PaintDefaultsExposeSingleBrush()
{
    var paint = new AppSettings().Paint;

    Assert(Math.Abs(paint.BrushSizeTexels - 5.0) < 0.000001, "the single brush should default to 5 texels");
    Assert(Math.Abs(paint.ColorCompressionTolerance - 5.0) < 0.000001,
        "color compression should default to 5");
    Assert(paint.FrontRegionMode == RegionMode.Skip, "front should default to skip");
    Assert(paint.SideRegionMode == RegionMode.Paint, "side should default to paint");
    Assert(paint.BackRegionMode == RegionMode.Paint, "back should default to paint");
}

static void SingleBrushPersistsAndMigratesLegacyDetailSettings()
{
    using var temp = new TempHome();
    var paths = new AppPaths("brush-selection-persistence-test");
    var settings = new AppSettings();
    settings.Paint.BrushSizeTexels = 2.5;

    new SettingsStore(paths).Save(settings);
    var loaded = new SettingsStore(paths).Load();
    Assert(Math.Abs(loaded.Paint.BrushSizeTexels - 2.5) < 0.000001, "single brush size should round-trip");
    using var saved = JsonDocument.Parse(File.ReadAllText(paths.ConfigPath));
    Assert(Math.Abs(saved.RootElement.GetProperty("brush_size_texels").GetDouble() - 2.5) < 0.000001,
        "single brush size should persist");
    Assert(!saved.RootElement.TryGetProperty("brush_1_size_texels", out _) &&
           !saved.RootElement.TryGetProperty("brush_2_size_texels", out _),
        "legacy two-brush keys should not persist");

    File.WriteAllText(paths.ConfigPath, """
    { "layout_version": 40, "brush_1_enabled": false, "brush_1_size_texels": 25, "brush_2_enabled": true, "brush_2_size_texels": 3.5 }
    """);
    var migrated = new SettingsStore(paths).Load();
    Assert(Math.Abs(migrated.Paint.BrushSizeTexels - 3.5) < 0.000001,
        "legacy detail brush should migrate to the single brush");
}

static void SingleBrushSettingsClampToSupportedRange()
{
    var settings = new AppSettings();
    settings.Paint.BrushSizeTexels = 15.0;

    var clamped = SettingsStore.Clamp(settings);
    Assert(Math.Abs(clamped.Paint.BrushSizeTexels - 10.0) < 0.000001, "single brush should clamp to 10");
    settings.Paint.BrushSizeTexels = 0.5;
    clamped = SettingsStore.Clamp(settings);
    Assert(Math.Abs(clamped.Paint.BrushSizeTexels - 1.0) < 0.000001, "single brush should clamp to 1");

    settings.Paint.ColorCompressionTolerance = 100.0;
    clamped = SettingsStore.Clamp(settings);
    Assert(Math.Abs(clamped.Paint.ColorCompressionTolerance - 10.0) < 0.000001,
        "color compression tolerance should clamp to 10");
}

static void AppDefaultsUse99PercentOpacity()
{
    using var temp = new TempHome();
    var defaults = new AppSettings();
    var loaded = new SettingsStore(new AppPaths("opacity-default-test")).Load();

    Assert(Math.Abs(defaults.Opacity - 0.99) < 0.000001, "a new app settings instance should default to 99 percent opacity");
    Assert(Math.Abs(loaded.Opacity - 0.99) < 0.000001, "a new persisted settings file should inherit the 99 percent opacity default");
}

static void PayloadSendsSingleBrushPipeline()
{
    var settings = new AppSettings();
    settings.Paint.BrushSizeTexels = 7.5;
    settings.Paint.ColorCompressionTolerance = 4.0;

    var payload = BridgePayloadBuilder.BuildPaintPayload(settings, 42, "Game.exe", new PaintRequestOptions());
    using var doc = JsonDocument.Parse(payload);
    var tuning = doc.RootElement.GetProperty("tuning");

    Assert(Math.Abs(tuning.GetProperty("brush_size_texels").GetDouble() - 7.5) < 0.000001, "payload should send the single brush");
    Assert(Math.Abs(tuning.GetProperty("color_compression_tolerance").GetDouble() - 4.0) < 0.000001,
        "payload should send the compression tolerance");
    Assert(!tuning.TryGetProperty("brush_1_size_texels", out _) && !tuning.TryGetProperty("brush_2_size_texels", out _),
        "payload should not send retired two-brush keys");
}

static void CustomFreecamSurfaceIsAbsent()
{
    var root = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var productSources = Directory
        .EnumerateFiles(Path.Combine(root, "src", "csharp"), "*.cs", SearchOption.AllDirectories)
        .Where(path => !path.Contains("MecchaCamouflage.Tests", StringComparison.Ordinal))
        .Select(File.ReadAllText)
        .ToArray();

    Assert(productSources.All(source => !source.Contains("Freecam", StringComparison.OrdinalIgnoreCase)) &&
           !bridge.Contains("freecam_toggle", StringComparison.Ordinal) &&
           !bridge.Contains("ToggleDebugCamera", StringComparison.Ordinal),
        "the application must not expose or invoke its own freecam implementation");
}

static void SpectatorPaintResolutionRequiresLocalControllerIdentity()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    var start = bridge.IndexOf("auto find_component", StringComparison.Ordinal);
    var end = bridge.IndexOf("struct SdkContext", start, StringComparison.Ordinal);
    Assert(start >= 0 && end > start, "native local-body component resolver must exist");
    var resolver = bridge[start..end];

    Assert(resolver.Contains("AcknowledgedPawn", StringComparison.Ordinal) &&
           resolver.Contains("local_body_unavailable", StringComparison.Ordinal) &&
           resolver.Contains("owner_controller == controller", StringComparison.Ordinal) &&
           resolver.Contains("owner_player_state == local_player_state", StringComparison.Ordinal) &&
           !resolver.Contains("controller_view_target", StringComparison.Ordinal) &&
           !resolver.Contains("camera_view_target", StringComparison.Ordinal) &&
           !resolver.Contains("playercontroller\"", StringComparison.Ordinal),
        "spectator paint must require the local controller and player-state identity, never a camera target or another controller");
}

static void DiagnosticStrokeLimitRequiresExplicitOption()
{
    var settings = new AppSettings();
    var normal = BridgePayloadBuilder.BuildPaintPayload(settings, 42, "Game.exe", new PaintRequestOptions());
    using var normalDocument = JsonDocument.Parse(normal);
    Assert(!normalDocument.RootElement.TryGetProperty("diagnostic_stroke_limit", out _),
        "normal paint must not carry a diagnostic stroke limit");

    var diagnostic = BridgePayloadBuilder.BuildPaintPayload(
        settings,
        42,
        "Game.exe",
        new PaintRequestOptions(DiagnosticStrokeLimit: 100));
    using var diagnosticDocument = JsonDocument.Parse(diagnostic);
    Assert(diagnosticDocument.RootElement.GetProperty("diagnostic_stroke_limit").GetInt32() == 100,
        "the explicitly requested diagnostic limit must reach native paint");
}

static void NativeAcceptsSingleBrushConfiguredRange()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("json_number_field(request, \"brush_size_texels\", 4.0)", StringComparison.Ordinal) &&
           bridge.Contains("1.0, 10.0", StringComparison.Ordinal),
        "native paint payload parsing must preserve the configured 1-10 single-brush range");
    Assert(bridge.Contains("json_number_field(request, \"color_compression_tolerance\", 4.0)", StringComparison.Ordinal) &&
           bridge.Contains("0.0, 10.0", StringComparison.Ordinal),
        "native paint payload parsing must cap color compression at 10");
}

static void NativeDirectRadiusUsesGameDefaultsAndFillStaysFixed()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("const double fill_stroke_radius_texels = 100.0;", StringComparison.Ordinal) &&
           bridge.Contains("\\\"fill_stroke_radius_source\\\":\\\"fixed_100_texels\\\"", StringComparison.Ordinal),
        "fill radius should be independent from either brush");
    Assert(bridge.Contains("\\\"replay_world_radius_policy\\\":\\\"game_default\\\"", StringComparison.Ordinal) &&
           bridge.Contains("sdk_make_mesh_anchor_stroke", StringComparison.Ordinal) &&
           bridge.Contains("GamePaintMeshAnchorWorldRadiusAuto", StringComparison.Ordinal),
        "direct paint should leave world-radius interpretation to the game defaults");
}

static void NativeSpatialReplayFollowsCurrentPoseAndCamera()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("sdk_project_world_to_screen(ref, ctx, sample.world_position", StringComparison.Ordinal) &&
           bridge.Contains("current_pose_camera_scanline_before_adaptive_radius_order", StringComparison.Ordinal),
        "replay order should be derived from each current-pose world sample in the current camera");
    Assert(!bridge.Contains("profile_reference_z_desc_rows_camera_right_asc", StringComparison.Ordinal) &&
           !bridge.Contains("sample.reference_position.Z", StringComparison.Ordinal),
        "replay order must not use the mesh profile reference pose");
}

static void NativeAsyncPaintRetainsCapturedComponentIdentity()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    Assert(bridge.Contains("safe_read<std::uintptr_t>(job->component + OffClass)", StringComparison.Ordinal) &&
           !bridge.Contains("current_pawn != job->pawn", StringComparison.Ordinal),
        "a valid captured paint component must remain paintable while its own queued job drains");
}

static void NativeProductionLocalSyncUsesPerStrokePaint()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("\\\"local_route_mode\\\":\\\"native_recorded_paint\\\"", StringComparison.Ordinal) &&
           bridge.Contains("PaintAtUVWithBrush", StringComparison.Ordinal) &&
           bridge.Contains("paint_at_uv_with_brush_native_replication", StringComparison.Ordinal) &&
           bridge.Contains("sdk_call_paint_at_uv_with_brush", StringComparison.Ordinal),
        "production paint must use the game-native recorded per-stroke route");
    Assert(bridge.Contains("const int local_sample_batch_limit = runtime_contract::NativeRecordedPaintMaxCallsPerTick;", StringComparison.Ordinal),
        "production paint must schedule only the bounded game-native route");
    var contract = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "include", "runtime_contract.hpp"));
    Assert(contract.Contains("constexpr int NativeRecordedPaintMaxCallsPerTick = 6;", StringComparison.Ordinal) &&
           contract.Contains("constexpr int NativeRecordedPaintQueueTargetStrokes = 4;", StringComparison.Ordinal) &&
           contract.Contains("constexpr int FastLocalCadenceMs = 1;", StringComparison.Ordinal) &&
           contract.Contains("constexpr int NativeRecordedPaintBackpressureCadenceMs = 8;", StringComparison.Ordinal) &&
           contract.Contains("constexpr int NativeRecordedPaintUnobservedCadenceMs = 16;", StringComparison.Ordinal),
        "native paint must retain bounded direct dispatch and a small game-owned queue window");
    Assert(bridge.Contains("direct_paint_capture_queue_snapshot", StringComparison.Ordinal) &&
           bridge.Contains("GetQueuedStrokeCountForComponent", StringComparison.Ordinal) &&
           bridge.Contains("native_queue_backpressure", StringComparison.Ordinal) &&
           bridge.Contains("direct_paint_queue_target_strokes", StringComparison.Ordinal) &&
           bridge.Contains("direct_paint_admission_queue_strokes", StringComparison.Ordinal) &&
           bridge.Contains("direct_paint_admission_queue_observed", StringComparison.Ordinal) &&
           bridge.Contains("native_shared_queue_waits", StringComparison.Ordinal) &&
           bridge.Contains("native_unobserved_fallback_ticks", StringComparison.Ordinal) &&
           bridge.Contains("mesh_direct_paint_cancel_drain", StringComparison.Ordinal) &&
           bridge.Contains("waiting for the game's recorded-paint queue", StringComparison.Ordinal),
        "native paint must use component and shared queue pressure with a conservative unobserved fallback");
    Assert(bridge.Contains("json_int_field(request, \"diagnostic_stroke_limit\", 0, 0, 10000)", StringComparison.Ordinal) &&
           bridge.Contains("diagnostic_stroke_limit_applied", StringComparison.Ordinal),
        "diagnostic runs must report their explicit stroke limit without changing normal paint");
    Assert(bridge.Contains("json_int_field(request, \"research_direct_queue_target_strokes\", 0, 0, 16)", StringComparison.Ordinal) &&
           bridge.Contains("direct_queue_requested_target_strokes", StringComparison.Ordinal),
        "research runs must vary the direct queue high-water mark without changing production defaults");
    Assert(bridge.Contains("compact_texture_research", StringComparison.Ordinal) &&
           bridge.Contains("research_compact", StringComparison.Ordinal),
        "research texture probes must return compact, complete evidence instead of truncating diagnostics");
    Assert(bridge.Contains("g_mesh_first_research_texture_snapshots", StringComparison.Ordinal) &&
           bridge.Contains("g_mesh_first_research_texture_snapshots.find(component)", StringComparison.Ordinal),
        "research texture inventories must retain one baseline per component");
    Assert(bridge.Contains("research_texture_preserve_baseline", StringComparison.Ordinal),
        "research texture time-series probes must preserve their initial component baselines");
}

static void NativePreviewAppliesPbrAndEmissiveChannels()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("paint_albedo_metallic_roughness", StringComparison.Ordinal) &&
           bridge.Contains("paint_emissive", StringComparison.Ordinal) &&
           bridge.Contains("packed_pbr_export_mismatch", StringComparison.Ordinal) &&
           bridge.Contains("sdk::EPaintChannel::AlbedoMetallicRoughnessEmissive", StringComparison.Ordinal) &&
           bridge.Contains("unpreview_snapshot_emissive_bytes", StringComparison.Ordinal) &&
           bridge.Contains("mesh_unpreview_packed_pbr_mismatch", StringComparison.Ordinal),
        "preview and unpreview must preserve packed Metallic/Roughness/Emissive data without successive imports overwriting it");
}

static void NativePreviewReturnsBeforeRecordedStrokeDispatch()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));
    var asyncDispatch = bridge.IndexOf("auto async_job = std::make_shared<MeshFirstServerBatchAsyncJob>", StringComparison.Ordinal);
    var previewBranch = bridge.LastIndexOf("if (preview_only)", asyncDispatch, StringComparison.Ordinal);

    Assert(asyncDispatch >= 0 && previewBranch >= 0, "preview and direct dispatch branches must exist");
    var previewRoute = bridge[previewBranch..asyncDispatch];
    Assert(previewRoute.Contains("mesh_first_apply_local_material_import_preview", StringComparison.Ordinal) &&
           previewRoute.Contains("return response_json", StringComparison.Ordinal),
        "preview must complete via local texture import before a recorded-stroke async job is created");
}

static void NativeAutoMaterialDetectsEmissiveAndReportsLocalPacing()
{
    var bridge = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("mesh_first_get_dominant_emissive_properties", StringComparison.Ordinal) &&
           bridge.Contains("sdk::EPaintChannel::Emissive", StringComparison.Ordinal) &&
           bridge.Contains("material_properties_emissive_source", StringComparison.Ordinal),
        "Auto Detect must derive Emissive from the game channel and report its source or fallback");
    Assert(bridge.Contains("sizeof(MeshFirstPaintMaterialPattern) == 0x30", StringComparison.Ordinal) &&
           bridge.Contains("offsetof(MeshFirstPaintMaterialPattern, emissive_color) == 0x18", StringComparison.Ordinal) &&
           bridge.Contains("offsetof(MeshFirstPaintMaterialPattern, sample_count) == 0x2C", StringComparison.Ordinal) &&
           bridge.Contains("material_properties_candidates", StringComparison.Ordinal) &&
           bridge.Contains("packed_pbr_emissive_blue_mode", StringComparison.Ordinal) &&
           bridge.Contains("PreferredSurfaceCoverageFloor = 0.01", StringComparison.Ordinal) &&
           bridge.Contains("auto_material_fill_policy", StringComparison.Ordinal) &&
           bridge.Contains("manual_fill_tuning", StringComparison.Ordinal) &&
           bridge.Contains("material_properties_fill_manual_samples", StringComparison.Ordinal) &&
           bridge.Contains("first_stroke_emissive", StringComparison.Ordinal),
        "Auto Detect must cover Paint, preserve an explicit Fill material, use the UE5.6 Emissive-aware pattern layout, and expose numeric candidates for runtime verification");
    Assert(bridge.Contains("tuning_auto_material && any_paint_region", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_enabled ? 0.0 : fill_metallic", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_enabled ? 1.0 : fill_roughness", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_enabled ? 0.0 : fill_emissive", StringComparison.Ordinal),
        "normal Fill must retain manual PBR values while imported-image reset strokes explicitly clear emissive");
    Assert(bridge.Contains("local_cpu_budget_us", StringComparison.Ordinal) &&
           bridge.Contains("local_render_target_write_budget", StringComparison.Ordinal) &&
           bridge.Contains("local_logical_sample_batch_limit", StringComparison.Ordinal),
        "normal local paint must report its CPU and write-budget pacing for live performance checks");
}

static void PayloadUsesNativePaintRouteAndFillMaterial()
{
    var settings = new AppSettings();
    settings.Paint.FrontRegionMode = RegionMode.Fill;
    settings.Paint.SideRegionMode = RegionMode.Skip;
    settings.Paint.BackRegionMode = RegionMode.Paint;
    settings.Paint.FillColor = new RgbColor(241, 17, 17);
    settings.Paint.FillMetallic = 1.0;
    settings.Paint.FillRoughness = 0.0;
    settings.Paint.Emissive = 0.35;
    settings.Paint.FillEmissive = 0.7;

    var payload = BridgePayloadBuilder.BuildPaintPayload(settings, 42, "Game.exe", new PaintRequestOptions());
    using var doc = JsonDocument.Parse(payload);
    Assert(doc.RootElement.GetProperty("native_apply_mode").GetString() == "native_recorded_paint",
        "payload should request the game-native recorded paint route");
    var tuning = doc.RootElement.GetProperty("tuning");
    Assert(tuning.GetProperty("front_region_mode").GetString() == "fill", "front mode missing");
    Assert(tuning.GetProperty("side_region_mode").GetString() == "skip", "side mode missing");
    Assert(tuning.GetProperty("back_region_mode").GetString() == "paint", "back mode missing");
    Assert(tuning.GetProperty("fill_color").GetString() == "#F11111", "fill color missing");
    Assert(Math.Abs(tuning.GetProperty("fill_color_r").GetDouble() - (241.0 / 255.0)) < 0.00001, "fill red not normalized");
    Assert(Math.Abs(tuning.GetProperty("emissive").GetDouble() - 0.35) < 0.00001, "paint emissive missing");
    Assert(Math.Abs(tuning.GetProperty("fill_emissive").GetDouble() - 0.7) < 0.00001, "fill emissive missing");
    Assert(!tuning.TryGetProperty("enable_front_paint", out _), "legacy front bool must not be sent");
    Assert(!tuning.TryGetProperty("enable_side_paint", out _), "legacy side bool must not be sent");
    Assert(!tuning.TryGetProperty("enable_back_paint", out _), "legacy back bool must not be sent");
    Assert(!tuning.TryGetProperty("auto_material_properties", out _), "legacy material key must not be sent");
}

static void LegacyFillPbrDefaultsMigrateToManualMaterial()
{
    using var temp = new TempHome();
    var paths = new AppPaths("fill-pbr-defaults-migration-test");
    Directory.CreateDirectory(paths.ConfigDirectory);
    File.WriteAllText(paths.ConfigPath, """
    {
      "layout_version": 38,
      "metallic": 0,
      "roughness": 1,
      "emissive": 0,
      "fill_metallic": 1,
      "fill_roughness": 0,
      "fill_emissive": 0
    }
    """);

    var migrated = new SettingsStore(paths).Load();
    Assert(Math.Abs(migrated.Paint.FillMetallic) < 0.000001,
        "the old mirror-like Fill metallic default should migrate to the manual material value");
    Assert(Math.Abs(migrated.Paint.FillRoughness - 1.0) < 0.000001,
        "the old mirror-like Fill roughness default should migrate to the manual material value");
    Assert(Math.Abs(migrated.Paint.FillEmissive) < 0.000001,
        "the Fill emissive default should migrate with the manual material value");

    File.WriteAllText(paths.ConfigPath, """
    {
      "layout_version": 38,
      "metallic": 0,
      "roughness": 1,
      "emissive": 0,
      "fill_metallic": 0.7,
      "fill_roughness": 0.2,
      "fill_emissive": 0.1
    }
    """);
    var custom = new SettingsStore(paths).Load();
    Assert(Math.Abs(custom.Paint.FillMetallic - 0.7) < 0.000001 &&
           Math.Abs(custom.Paint.FillRoughness - 0.2) < 0.000001 &&
           Math.Abs(custom.Paint.FillEmissive - 0.1) < 0.000001,
        "a non-default Fill PBR choice must not be changed by the migration");
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

static void RegionsDefaultToSideAndBackPaint()
{
    var paint = new AppSettings().Paint;
    Assert(paint.FrontRegionMode == RegionMode.Skip, "front should default to skip");
    Assert(paint.SideRegionMode == RegionMode.Paint, "side should default to paint");
    Assert(paint.BackRegionMode == RegionMode.Paint, "back should default to paint");
}

static void BridgeMessagesAreUserFriendly()
{
    var alreadyRunning = HostSession.FriendlyBridgeMessage("mesh-first paint is already running");
    var completed = HostSession.FriendlyBridgeMessage("mesh-first paint completed");
    var alreadyFriendlyCompleted = HostSession.FriendlyBridgeMessage("Paint completed.");
    var preview = HostSession.FriendlyBridgeMessage("local preview material texture imported");
    var noPreview = HostSession.FriendlyBridgeMessage("mesh_unpreview_snapshot_unavailable");
    var contextChanged = HostSession.FriendlyBridgeMessage("mesh_paint_context_changed");
    var componentUnavailable = HostSession.FriendlyBridgeMessage("PaintAtUVWithBrush failed: paint_component_unavailable");
    var pawnUnavailable = HostSession.FriendlyBridgeMessage("Paint stopped because the local pawn is no longer available");
    var unprovenSpectatorBody = HostSession.FriendlyBridgeMessage("paintable_body_unavailable: local_body_unavailable");
    var nativeRouteUnavailable = HostSession.FriendlyBridgeMessage("mesh_native_paint_unavailable");
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
    Assert(unprovenSpectatorBody == "Paint: blocked because the current spectator state cannot prove a local paint body.",
        "an unproven spectator body must produce an explicit safe error");
    Assert(nativeRouteUnavailable == "Paint: the game-native paint route is unavailable.", "missing native route should be friendly");
    Assert(cancelledAfterSubmission == "Paint: canceled.",
        "a late cancel that waited for the committed queue must remain a concise cancellation");
    Assert(cancelledWithBoundedTail == "Paint: canceled.",
        "a bounded local queue cancel should remain a concise cancellation");
    Assert(unsafeSampling == "Paint: blocked because the current mesh sampling was unsafe.", "unsafe mesh sampling should be friendly");
    Assert(!alreadyRunning.Contains("mesh", StringComparison.OrdinalIgnoreCase), "internal mesh wording should be hidden");
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

static void UiSnapshotExposesSingleBrush()
{
    var snapshot = new PaintSnapshot(
        7.5,
        false,
        0.0,
        1.0,
        0.0,
        "fill",
        "paint",
        "paint",
        "#FFFFFF",
        1.0,
        0.0,
        0.0,
        true);
    var json = JsonSerializer.Serialize(snapshot, new JsonSerializerOptions
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    });
    using var doc = JsonDocument.Parse(json);

    Assert(Math.Abs(doc.RootElement.GetProperty("brushSizeTexels").GetDouble() - 7.5) < 0.000001,
        "snapshot should expose the single brush");
    Assert(!doc.RootElement.TryGetProperty("brush1SizeTexels", out _) &&
           !doc.RootElement.TryGetProperty("brush2SizeTexels", out _),
        "snapshot should not expose retired two-brush fields");
}

static void WebUiExposesSingleBrushSliderAndCompressionTolerance()
{
    var repository = FindRepositoryRoot();
    var index = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    Assert(index.Contains("id=\"brush-size\"", StringComparison.Ordinal), "web UI should include the single brush slider");
    Assert(index.Contains("id=\"color-compression-tolerance\"", StringComparison.Ordinal), "web UI should include compression tolerance");
    Assert(index.Contains("min=\"1\" max=\"10\" step=\"0.5\"", StringComparison.Ordinal), "single brush should expose the 1-10 range");
    Assert(index.Contains("id=\"color-compression-tolerance\" class=\"setting-control\" disabled type=\"range\" min=\"0\" max=\"10\" step=\"0.5\"", StringComparison.Ordinal),
        "compression tolerance should expose the 0-10 range in half-step increments");
    Assert(app.Contains("paint.brushSizeTexels", StringComparison.Ordinal), "web UI should bind the single brush");
    Assert(app.Contains("paint.colorCompressionTolerance", StringComparison.Ordinal), "web UI should bind compression tolerance");
    Assert(!app.Contains("paint.brush1", StringComparison.Ordinal) && !app.Contains("paint.brush2", StringComparison.Ordinal),
        "web UI should not retain two-brush bindings");
}

static void WebUiKeepsThemeColorOnReadonlyControls()
{
    var repository = FindRepositoryRoot();
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));
    var normalizedStyles = styles.ReplaceLineEndings("\n");

    Assert(app.Contains("isThemeVisibleReadOnlyControl", StringComparison.Ordinal),
        "the UI should distinguish passive themed controls from ordinary disabled inputs");
    Assert(app.Contains("control.disabled = disabled && !themeVisibleReadonly", StringComparison.Ordinal),
        "readonly range and checkbox controls should remain paint-enabled for Chromium accent rendering");
    Assert(styles.Contains("input.theme-visible-readonly[type=\"range\"]", StringComparison.Ordinal),
        "readonly sliders need a dedicated themed style");
    Assert(normalizedStyles.Contains("input.theme-visible-readonly[type=\"range\"] {\n  opacity: 0.55;", StringComparison.Ordinal),
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

static void WebUiImagePickerPreviewsFitCropAndTransparency()
{
    var repository = FindRepositoryRoot();
    var index = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));

    Assert(index.Contains("id=\"image-picker-input\"", StringComparison.Ordinal) &&
           index.Contains("accept=\".png,.jpg,.jpeg,image/png,image/jpeg\"", StringComparison.Ordinal),
        "image picker should accept local PNG and JPEG files");
    Assert(index.Contains("data-image-placement=\"fit\"", StringComparison.Ordinal) &&
           index.Contains("data-image-placement=\"crop\"", StringComparison.Ordinal),
        "image picker should expose Fit and Crop placement");
    Assert(index.Contains("data-alpha-mode=\"skip\"", StringComparison.Ordinal) &&
           index.Contains("data-alpha-mode=\"background\"", StringComparison.Ordinal),
        "image picker should expose unpainted and background transparency choices");
    Assert(index.IndexOf("id=\"image-picker-section\"", StringComparison.Ordinal) >
           index.IndexOf("id=\"fill-section\"", StringComparison.Ordinal),
        "image picker should appear below the Fill controls");
    Assert(index.Contains(">Leave unpainted<", StringComparison.Ordinal) &&
           !index.Contains("PNG only", StringComparison.Ordinal) &&
           !index.Contains("How it works", StringComparison.Ordinal) &&
           !index.Contains("Nothing leaves your computer", StringComparison.Ordinal) &&
           index.Contains("transparent areas at the default body color", StringComparison.Ordinal) &&
           index.IndexOf("image-picker-disclaimer", StringComparison.Ordinal) >
           index.IndexOf("image-picker-background-row", StringComparison.Ordinal),
        "image picker should use compact labels and explain repeat whole-body painting");
    Assert(index.Contains("data-wrap-mode=\"base\"", StringComparison.Ordinal) &&
           index.Contains("data-wrap-mode=\"meet\"", StringComparison.Ordinal) &&
           index.Contains("data-wrap-mode=\"full\"", StringComparison.Ordinal) &&
           index.Contains("data-image-count=\"1\"", StringComparison.Ordinal) &&
           index.Contains("data-image-count=\"2\"", StringComparison.Ordinal) &&
           index.Contains("data-mirror-mode=\"none\"", StringComparison.Ordinal) &&
           index.Contains("data-mirror-mode=\"mirror\"", StringComparison.Ordinal) &&
           index.Contains(">Multiple images<", StringComparison.Ordinal) &&
           index.Contains("Choose image", StringComparison.Ordinal) &&
           index.Contains("id=\"image-picker-reset-placement\"", StringComparison.Ordinal) &&
           index.Contains("id=\"image-design-save\"", StringComparison.Ordinal) &&
           index.Contains("id=\"image-design-load\"", StringComparison.Ordinal) &&
           index.Contains("id=\"image-layer-list\"", StringComparison.Ordinal) &&
           index.Contains("id=\"crop-editor-dialog\"", StringComparison.Ordinal) &&
           index.Contains("id=\"crop-editor-selection\"", StringComparison.Ordinal) &&
           index.Contains("multiple", StringComparison.Ordinal) &&
           index.Contains("(or drop)", StringComparison.Ordinal) &&
           !index.Contains("data-body-type=\"cube\"", StringComparison.Ordinal) &&
           index.Contains("value=\"#BCBCBC\"", StringComparison.Ordinal),
        "image picker should expose drag/drop, placement, wrapping, saved designs, and the default body gray");
    Assert(app.Contains("URL.createObjectURL(file)", StringComparison.Ordinal) &&
           app.Contains("URL.revokeObjectURL", StringComparison.Ordinal) &&
           app.Contains("imagePickerMaximumBytes", StringComparison.Ordinal) &&
           app.Contains("imagePickerMaximumEdge", StringComparison.Ordinal),
        "image picker should preview locally and validate resource limits");
    Assert(app.Contains("function armImagePaint()", StringComparison.Ordinal) &&
           app.Contains("send(\"setImagePaint\"", StringComparison.Ordinal) &&
           app.Contains("imagePaintMapWidth = 512", StringComparison.Ordinal) &&
           app.Contains("function drawPlacedImage", StringComparison.Ordinal) &&
           app.Contains("function buildWrapAtlas", StringComparison.Ordinal) &&
           app.Contains("function renderWrapEditor", StringComparison.Ordinal) &&
           app.Contains("function beginImageEditorDrag", StringComparison.Ordinal) &&
           app.Contains("function bindImageDropTarget", StringComparison.Ordinal) &&
           app.Contains("function syncMirroredLayer()", StringComparison.Ordinal) &&
           app.Contains("MecchaCamouflageImageDesigns", StringComparison.Ordinal) &&
           app.Contains("function saveCurrentImageDesign()", StringComparison.Ordinal) &&
           app.Contains("function loadSelectedImageDesign()", StringComparison.Ordinal) &&
           app.Contains("function addImagePickerFiles", StringComparison.Ordinal) &&
           app.Contains("function openCropEditor", StringComparison.Ordinal) &&
           app.Contains("function updateCropEditorZoom", StringComparison.Ordinal) &&
           app.Contains("crop: image.crop ? clone(image.crop) : null", StringComparison.Ordinal) &&
           app.Contains("generation !== imagePaintArmGeneration", StringComparison.Ordinal),
        "image picker should arm the bounded four-panel atlas shown by its editor");
    Assert(styles.Contains(".image-picker-preview canvas", StringComparison.Ordinal) &&
           styles.Contains("touch-action: none", StringComparison.Ordinal) &&
           styles.Contains(".image-drop-button.drag-over", StringComparison.Ordinal) &&
           styles.Contains(".crop-editor-selection", StringComparison.Ordinal) &&
           styles.Contains(".image-picker-preview.checkerboard", StringComparison.Ordinal),
        "image preview should expose the four-view canvas editor and visible transparency");
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
    Assert(!bridge.Contains("replay_server_current", StringComparison.Ordinal), "direct paint must not retain a server cursor");
    Assert(bridge.Contains("replay_local_offset", StringComparison.Ordinal), "native progress should expose the local pass");
    Assert(bridge.Contains("g_paint_dispatch_message_pending", StringComparison.Ordinal), "scheduler wakeups should be coalesced");
    Assert(bridge.Contains("cancellation_stopped_further_submission", StringComparison.Ordinal),
        "a canceled native route must report only actually submitted strokes as rendered");
    Assert(json.Contains("replay_current_pass", StringComparison.Ordinal), "compact progress metadata should retain the current pass");
    Assert(bridge.Contains("\\\"local_route_mode\\\":\\\"native_recorded_paint\\\"", StringComparison.Ordinal) &&
           bridge.Contains("paint_at_uv_with_brush_native_replication", StringComparison.Ordinal),
        "production paint should use the game-native recorded paint route");
    Assert(bridge.Contains("mesh_first_apply_local_material_import_preview", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_apply_local_material_import_increment", StringComparison.Ordinal) &&
           bridge.Contains("sdk_call_paint_at_uv_with_brush", StringComparison.Ordinal) &&
           !bridge.Contains("production_direct_local_requested", StringComparison.Ordinal) &&
           !bridge.Contains("completed_before_server_submission", StringComparison.Ordinal),
        "preview/import tooling must remain separate from the production per-stroke local paint route");
    Assert(json.Contains("local_texture_import_ok", StringComparison.Ordinal) &&
           json.Contains("local_texture_import_calls", StringComparison.Ordinal) &&
           json.Contains("local_texture_import_strokes_painted", StringComparison.Ordinal) &&
           json.Contains("local_texture_import_compose_elapsed_ms", StringComparison.Ordinal) &&
           json.Contains("local_texture_import_channel_elapsed_ms", StringComparison.Ordinal) &&
           json.Contains("local_texture_import_elapsed_ms", StringComparison.Ordinal),
        "compact preview/import progress and replies should retain texture-import evidence");
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

    var update = session.UpdateSetting("paint.brushSizeTexels", JsonSerializer.SerializeToElement(7.5));
    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - 7.5) < 0.000001, "single brush should update");

    var reset = session.ResetSetting("paint.brushSizeTexels");
    Assert(reset.Success, reset.Message);
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - new AppSettings().Paint.BrushSizeTexels) < 0.000001,
        "single brush should reset");
}

static void HostSessionUpdatesSingleBrush()
{
    using var temp = new TempHome();
    var session = new HostSession("host-brush-sync-test");
    var update = session.UpdateSetting("paint.brushSizeTexels", JsonSerializer.SerializeToElement(6.5));

    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - 6.5) < 0.000001, "single brush should update");

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();
    Assert(Math.Abs(snapshot.Settings.Paint.BrushSizeTexels - 6.5) < 0.000001, "snapshot should expose the single brush");
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
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(7.5)),
        new SettingChange("paint.fillColor", JsonSerializer.SerializeToElement("#112233")),
        new SettingChange("app.processName", JsonSerializer.SerializeToElement("Game.exe"))
    ]);

    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - 7.5) < 0.000001, "single brush should update");
    Assert(session.Settings.Paint.FillColor.ToHex() == "#112233", "fill color should update");
    Assert(session.Settings.GameProcessName == "Game.exe", "process name should update");
}

static void HostSessionRollsBackDuplicateHotkeyBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-hotkey-rollback-test");
    var originalBrush = session.Settings.Paint.BrushSizeTexels;
    var originalPreview = session.Settings.PreviewHotkey;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(7.5)),
        new SettingChange("app.previewHotkey", JsonSerializer.SerializeToElement(session.Settings.StartHotkey))
    ]);

    Assert(!update.Success, "duplicate hotkey batch should fail");
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - originalBrush) < 0.000001, "single brush change should roll back");
    Assert(session.Settings.PreviewHotkey == originalPreview, "hotkey change should roll back");
}

static void HostSessionRollsBackInvalidFillColorBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-color-rollback-test");
    var originalBrush = session.Settings.Paint.BrushSizeTexels;
    var originalColor = session.Settings.Paint.FillColor;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(7.5)),
        new SettingChange("paint.fillColor", JsonSerializer.SerializeToElement("not-a-color"))
    ]);

    Assert(!update.Success, "invalid color batch should fail");
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - originalBrush) < 0.000001, "single brush should roll back");
    Assert(session.Settings.Paint.FillColor == originalColor, "fill color should roll back");
}

static void HostSessionRollsBackInvalidThemeColorBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-theme-rollback-test");
    var originalBrush = session.Settings.Paint.BrushSizeTexels;
    var originalTheme = session.Settings.ThemeColor;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(7.5)),
        new SettingChange("app.themeColor", JsonSerializer.SerializeToElement("not-a-color"))
    ]);

    Assert(!update.Success, "invalid theme color batch should fail");
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - originalBrush) < 0.000001, "single brush should roll back");
    Assert(session.Settings.ThemeColor == originalTheme, "theme color should roll back");
}

static void HostSessionRollsBackInvalidRegionModeBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-region-rollback-test");
    var originalBrush = session.Settings.Paint.BrushSizeTexels;
    var originalMode = session.Settings.Paint.FrontRegionMode;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(7.5)),
        new SettingChange("paint.frontRegionMode", JsonSerializer.SerializeToElement("invalid"))
    ]);

    Assert(!update.Success, "invalid region mode batch should fail");
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - originalBrush) < 0.000001, "single brush should roll back");
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
    {"stage":"mesh_direct_paint","phase":"local_paint","terminal":false,"result":"running","step":50,"total_steps":100,"progress":0.5,"paint_eta_ms":1000,"paint_elapsed_ms":1000}
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
    {"stage":"mesh_direct_paint","phase":"local_paint","terminal":false,"result":"running","step":50,"total_steps":100,"progress":0.5,"paint_eta_ms":1000,"paint_elapsed_ms":1000}
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
        "{\"stage\":\"mesh_direct_paint\",\"phase\":\"local_paint\",\"terminal\":false,\"result\":\"running\",\"step\":50,\"total_steps\":100,\"progress\":0.5,\"paint_eta_ms\":1000,\"paint_elapsed_ms\":1000}";
    File.WriteAllText(preferred, validProgress);
    File.SetLastWriteTimeUtc(preferred, DateTime.UtcNow.AddMinutes(-1));
    File.WriteAllText(fallback, validProgress);
    ConfigureLiveProgressSession(session, preferred);

    var snapshot = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(!snapshot.Runtime.ProgressVisible,
        "an existing but stale preferred snapshot must not be replaced by another bridge instance's fresh progress");
}

static void HostSessionPresentsNativePassProgressAndQueueBackpressure()
{
    using var temp = new TempHome();
    var session = new HostSession("host-native-pass-progress-test");
    var preferred = Path.Combine(session.Paths.BridgeProgressDirectory, "preferred.progress.json");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    File.WriteAllText(preferred, """
    {
      "stage":"mesh_direct_paint_drain",
      "phase":"local_paint",
      "terminal":false,
      "result":"running",
      "step":813,
      "total_steps":5596,
      "progress":0.145282,
      "paint_eta_ms":58000,
      "paint_elapsed_ms":24000,
      "native_queue_component_last_strokes":4,
      "native_queue_target_strokes":4,
      "replay_progress_source":"native_queue_backpressure",
      "replay_current_pass":"paint",
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
    Assert(snapshot.Runtime.PaintProgressSource == "native_queue_backpressure", "native progress source should be retained");
    Assert(snapshot.Runtime.PaintPass == "Paint", "single paint pass should be presented as Paint");
    Assert(snapshot.Runtime.PaintPassProgress == "704/1333 (53%)", "pass-local count and percent should be presented together");
    Assert(snapshot.Runtime.PaintPassEta == "7s", "pass ETA should be formatted independently");
    Assert(snapshot.Runtime.PaintEta == "58s", "paint ETA should remain the total ETA");
}

static void HostSessionLogsEachPassTransitionOnce()
{
    using var temp = new TempHome();
    var session = new HostSession("host-pass-transition-log-test");
    var preferred = Path.Combine(session.Paths.BridgeProgressDirectory, "preferred.progress.json");
    Directory.CreateDirectory(session.Paths.BridgeProgressDirectory);
    ConfigureLiveProgressSession(session, preferred);

    WritePass("submission", "paint", 200, 1333, 5000);
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    WritePass("native_queue_backpressure", "paint", 400, 1333, 4000);
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    WritePass("native_queue_backpressure", "complete", 4154, 4154, 0);
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();
    _ = session.GetSnapshotAsync().GetAwaiter().GetResult();

    Assert(CountOccurrences(session.Log.Text, "Paint: pass Paint") == 1,
        "Paint should be logged once even when the progress source changes");
    Assert(CountOccurrences(session.Log.Text, "Paint: pass Complete") == 1,
        "Complete should be logged once despite repeated snapshots");

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

static void PaintDiagnosticsReportDirectStrokePbrValues()
{
    var reply = new BridgeReply(
        true,
        true,
        "mesh_direct_paint_done",
        "ok",
        "{\"metadata\":{\"diagnostic_strokes_before_limit\":10,\"diagnostic_strokes_after_limit\":1,\"local_stroke_calls\":1,\"local_stroke_success\":1,\"first_stroke_target_channel\":7,\"first_stroke_metallic\":1,\"first_stroke_roughness\":0,\"first_stroke_emissive\":0}}");
    var summary = HostSession.PaintDiagnosticSummary(reply);

    Assert(summary is not null &&
           summary.Contains("diagnostic_strokes_after_limit=1", StringComparison.Ordinal) &&
           summary.Contains("local_stroke_calls=1", StringComparison.Ordinal) &&
           summary.Contains("first_stroke_roughness=0", StringComparison.Ordinal),
        "a one-stroke diagnostic must report the submitted direct call and PBR inputs");
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
    Assert(native.Contains("eventwatch_direct_receiver", StringComparison.Ordinal),
        "texture command must be able to pin the watched direct receiver rather than the local pawn");
    Assert(native.Contains("research_texture_target_unavailable", StringComparison.Ordinal),
        "an unobserved or stale multicast receiver must fail closed");
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

static void ResearchRunnerRecordsSingleBrushAndDirectQueueMode()
{
    var root = FindRepositoryRoot();
    var source = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "ResearchRunner.cs"));
    var runtime = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.Controller", "RuntimeBridgeService.cs"));
    var native = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(source.Contains("brush_size_texels = paint.BrushSizeTexels", StringComparison.Ordinal), "research artifacts should record the single brush");
    Assert(source.Contains("color_compression_tolerance = paint.ColorCompressionTolerance", StringComparison.Ordinal), "research artifacts should record compression tolerance");
    Assert(!source.Contains("--paint-mode", StringComparison.Ordinal), "research runner must not reintroduce an alternate paint transport");
    Assert(source.Contains("research_uv_replay_atlas", StringComparison.Ordinal), "research paint should explicitly request a pass-aware UV replay atlas");
    Assert(source.Contains("ResearchUvReplayArtifacts.StageAndRender", StringComparison.Ordinal), "research runs should retain the native replay plan and render its PNG atlas");
    Assert(source.Contains("ResearchTextureDeltaArtifacts.StageAndRender", StringComparison.Ordinal), "texture snapshot runs should retain an actual changed-pixel PNG mask");
    Assert(source.Contains("--texture-discovery-seconds", StringComparison.Ordinal) &&
           source.Contains("WaitForDirectReceiverAsync", StringComparison.Ordinal),
        "joining texture evidence should wait for and pin an observed direct receiver");
    Assert(runtime.Contains("research_texture_expected_component", StringComparison.Ordinal) &&
           native.Contains("research_texture_target_pin_mismatch", StringComparison.Ordinal),
        "joining texture probes must send the discovered receiver address back to the native bridge as a fail-closed pin");
    Assert(source.Contains("TryNormalizeNonZeroHexAddress", StringComparison.Ordinal) &&
           source.Contains("ulong.TryParse", StringComparison.Ordinal),
        "joining receiver discovery must require a strict non-zero hexadecimal component address");
    Assert(source.Contains("--target-channel", StringComparison.Ordinal) &&
           source.Contains("research_target_channel", StringComparison.Ordinal) &&
           native.Contains("research_single_channel", StringComparison.Ordinal),
        "research runs must be able to isolate one live paint-channel enum without changing production fan-out");
    Assert(source.Contains("--metallic", StringComparison.Ordinal) &&
           source.Contains("--roughness", StringComparison.Ordinal) &&
           source.Contains("--emissive", StringComparison.Ordinal) &&
           source.Contains("ParseUnitIntervalOverride", StringComparison.Ordinal),
        "research runs must support bounded PBR sentinel values for channel-contract checks");
    Assert(source.Contains("--preview-only", StringComparison.Ordinal) &&
           source.Contains("--unpreview-only", StringComparison.Ordinal) &&
           source.Contains("not_applicable_preview_operation", StringComparison.Ordinal) &&
           source.Contains("preview-cleanup-reply.json", StringComparison.Ordinal) &&
           source.Contains("new PaintRequestOptions(UnPreviewOnly: true, ResearchArtifacts: true)", StringComparison.Ordinal),
        "research preview runs must restore their material snapshot before the short-lived bridge shuts down");
    Assert(source.Contains("--auto-material", StringComparison.Ordinal) &&
           source.Contains("session.Settings.Paint.AutoMaterial = true", StringComparison.Ordinal),
        "research runs must be able to capture the live auto-material decision separately from manual PBR sentinels");
    Assert(native.Contains("selected_texture_target_only", StringComparison.Ordinal),
        "texture diagnostics must avoid unrelated component readbacks that perturb joining-client timing");
    Assert(native.Contains("emissive_export", StringComparison.Ordinal) &&
           native.Contains("emissive_after_changed_rgba", StringComparison.Ordinal) &&
           native.Contains("roughness_after_changed_rgba", StringComparison.Ordinal),
        "research texture diagnostics must record all PBR channels and their changed output values");
    Assert(native.Contains("channel_data_schema", StringComparison.Ordinal) &&
           native.Contains("channel_enum_schema", StringComparison.Ordinal) &&
           native.Contains("out_patterns_schema", StringComparison.Ordinal),
        "research paint probes must report the live channel, enum, and auto-material pattern contracts");
    Assert(source.Contains("CancelPaintAfterDelayAsync(session.Runtime, cancelAfterMs, paintTask)", StringComparison.Ordinal) &&
           source.Contains("cancel_admission_latched", StringComparison.Ordinal) &&
           native.Contains("cancel_latched_paint_request", StringComparison.Ordinal),
        "research cancellation must retain an admission-time cancel latch instead of misreporting no active job");
    Assert(source.Contains("textureSnapshot && shutdownAfterMs is not null", StringComparison.Ordinal),
        "a texture snapshot must reject scheduled shutdown because it cannot safely produce an after image");
}

static void UvReplayAtlasSeparatesFillAndPaint()
{
    var plan = new UvReplayPlan(
        TextureSize: 128,
        Strokes:
        [
            new UvReplayStroke(0.25, 0.25, 0.10, UvReplayPass.Fill, "front", "torso"),
            new UvReplayStroke(0.75, 0.75, 0.04, UvReplayPass.Paint, "back", "arm")
        ]);

    var atlas = UvReplayAtlasRasterizer.Render(plan, tileSize: 64);
    Assert(atlas.Width == 128 && atlas.Height == 64, "the atlas should be a one-row, two-pass direct-paint grid");

    var plannerFillCenter = atlas.RgbaAt(16, 47);
    Assert(plannerFillCenter.SequenceEqual(UvReplayAtlasRasterizer.FillColor), "fill must occupy the planner row");
    Assert(atlas.RgbaAt(26, 47).SequenceEqual(UvReplayAtlasRasterizer.BackgroundColor),
        "the direct planner radius should define the rendered footprint");
    Assert(atlas.RgbaAt(111, 16).SequenceEqual(UvReplayAtlasRasterizer.PaintColor),
        "paint must occupy the Paint column");

    var bounded = UvReplayAtlasRasterizer.Render(new UvReplayPlan(65_536, []));
    Assert(bounded.Width == 2_048 && bounded.Height == 1_024,
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
            { "u": 0.5, "v": 0.5, "planner_radius_uv": 0.1, "pass": "paint", "region": "front", "body_region": "arm" }
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

static void AppCloseShutsDownActiveBridge()
{
    var root = FindRepositoryRoot();
    var form = File.ReadAllText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));

    Assert(form.Contains("FormClosing += HandleFormClosingAsync", StringComparison.Ordinal),
        "the main form must own an explicit bridge shutdown close path");
    Assert(form.Contains("await session.ShutdownBridgeAsync();", StringComparison.Ordinal),
        "closing the app must await bridge shutdown before the form exits");
    Assert(form.Contains("bridgeShutdownCompleted = true;", StringComparison.Ordinal) &&
           form.Contains("if (!IsDisposed)", StringComparison.Ordinal) &&
           form.Contains("Close();", StringComparison.Ordinal),
        "the close path must resume the original close only after shutdown completion");
}

static void NativeProcessEventAcceptsResidentDirectBridgeHook()
{
    var root = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("address_in_resident_direct_bridge_module", StringComparison.Ordinal) &&
           bridge.Contains("meccha-direct-bridge-v1-", StringComparison.Ordinal),
        "the bridge must identify only a resident uniquely staged direct bridge hook");
    Assert(bridge.Contains("page.State != MEM_COMMIT", StringComparison.Ordinal) &&
           bridge.Contains("PAGE_EXECUTE_READ", StringComparison.Ordinal),
        "resident bridge reuse must require an executable committed module page");
    Assert(bridge.Contains("address_in_main_module(address) || address_in_bridge_module(address) ||", StringComparison.Ordinal) &&
           bridge.Contains("address_in_resident_direct_bridge_module(address)", StringComparison.Ordinal),
        "a new bridge must chain through one valid resident direct bridge hook rather than reject it");
}

static void RuntimeLaunchStagesLocalWindowsCopy()
{
    var root = FindRepositoryRoot();
    var makefile = File.ReadAllText(Path.Combine(root, "Makefile"));
    var start = File.ReadAllText(Path.Combine(root, "scripts", "start.ps1"));

    Assert(makefile.Contains("START_PS := scripts/start.ps1", StringComparison.Ordinal) &&
           makefile.Contains("-File \"$$PS_SCRIPT_WIN\" -SourceExe \"$$EXE_WIN\" -DiagnosticStrokeLimit", StringComparison.Ordinal),
        "make start must invoke the dedicated staged launcher");
    Assert(start.Contains("[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)", StringComparison.Ordinal) &&
           start.Contains("MecchaCamouflage\\launch", StringComparison.Ordinal) &&
           start.Contains("Get-FileHash", StringComparison.Ordinal),
        "the launcher must stage a hash-verified executable under LocalAppData");
    Assert(start.Contains("$startProcessArguments = @{ FilePath = $stagedExe; PassThru = $true }", StringComparison.Ordinal) &&
           start.Contains("if ($DiagnosticStrokeLimit -gt 0)", StringComparison.Ordinal) &&
           start.Contains("$startProcessArguments.ArgumentList", StringComparison.Ordinal) &&
           start.Contains("Start-Process @startProcessArguments", StringComparison.Ordinal),
        "an argument-free launch must omit ArgumentList while diagnostic runs pass an explicit limit");
    Assert(start.Contains("Get-Process -Name $exeBaseName", StringComparison.Ordinal) &&
           start.Contains("Close it normally before running make start", StringComparison.Ordinal),
        "the launcher must refuse a duplicate controller rather than orphaning an active bridge");
    Assert(!makefile.Contains("Start-Process", StringComparison.Ordinal),
        "make start must not directly run the build output that a later build must replace");
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
