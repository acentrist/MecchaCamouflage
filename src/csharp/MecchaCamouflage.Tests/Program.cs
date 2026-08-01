using System.Text.Json;
using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using MecchaCamouflage.Controller;
using MecchaCamouflage.Core;

var tests = new List<(string Name, Action Run)>
{
    ("paint defaults expose a single brush", PaintDefaultsExposeSingleBrush),
    ("single brush persists and migrates legacy detail settings", SingleBrushPersistsAndMigratesLegacyDetailSettings),
    ("single brush settings clamp to supported range", SingleBrushSettingsClampToSupportedRange),
    ("app defaults use 99 percent opacity", AppDefaultsUse99PercentOpacity),
    ("app defaults use the minimum window size", AppDefaultsUseMinimumWindowSize),
    ("ESP persists role colors and migrates legacy team-relative settings", EspRoleColorsPersistAndMigrateLegacySettings),
    ("payload sends a single brush and compression tolerance", PayloadSendsSingleBrushPipeline),
    ("normal paint payload uses the intrinsic material color policy", NormalPaintPayloadUsesIntrinsicMaterialColorPolicy),
    ("image payload carries a full canonical canvas", ImagePayloadCarriesFullCanonicalCanvas),
    ("image transparency fills regions before painting opaque pixels", ImageTransparencyFillsRegionsBeforePaintingOpaquePixels),
    ("image region skip suppresses only Fill", ImageRegionSkipSuppressesOnlyFill),
    ("native warms an unavailable triangle cache before it blocks paint", NativeWarmsUnavailableTriangleCache),
    ("native accepts verified direct triangle order despite duplicate UV islands", NativeAcceptsVerifiedDirectTriangleOrder),
    ("native image paint resolves its derived reference profile from the raw profile", NativeImagePaintResolvesDerivedReferenceProfile),
    ("native image paint exposes deterministic planner telemetry", NativeImagePaintExposesDeterministicPlannerTelemetry),
    ("native image paint uses the live profile when the selected body differs", NativeImagePaintUsesLiveProfileWhenSelectedBodyDiffers),
    ("image unpreview omits paint-plan diagnostics", ImageUnpreviewOmitsPaintPlanDiagnostics),
    ("image unpreview bypasses image design guards", ImageUnpreviewBypassesImageDesignGuards),
    ("custom freecam surface is absent", CustomFreecamSurfaceIsAbsent),
    ("misc configures the native Present ESP renderer", MiscUsesNativePresentEspRenderer),
    ("native Present ESP command uses role-fixed scope and colors", NativePresentEspCommandUsesRoleFixedScopeAndColors),
    ("latest async request queue preserves the final ESP scope", LatestAsyncRequestQueuePreservesFinalEspScope),
    ("native Present status logging ignores healthy counter churn", NativePresentStatusLoggingIgnoresHealthyCounterChurn),
    ("native Present ESP has no external frame-render fallback", NativePresentEspHasNoExternalFallback),
    ("native Present ESP hooks queue, owns wrapped backbuffers, and reports lifecycle state", NativePresentEspOwnsRendererLifecycle),
    ("runtime safely replaces obsolete resident bridge generations", RuntimeSafelyReplacesObsoleteResidentBridgeGenerations),
    ("resident bridge reconnect retains its progress path", ResidentBridgeReconnectRetainsProgressPath),
    ("research hot reload quiesces and serializes bridge generations", ResearchHotReloadQuiescesAndSerializesBridgeGenerations),
    ("connected game process remains pinned", ConnectedGameProcessRemainsPinned),
    ("live game identity blocks target replacement", LiveGameIdentityBlocksTargetReplacement),
    ("unresponsive matching bridge is not reinjected", UnresponsiveMatchingBridgeIsNotReinjected),
    ("native Present reports the failing direct backbuffer setup step", NativePresentReportsBackbufferSetupStep),
    ("native Present pipeline initializes valid D3D12 defaults", NativePresentPipelineInitializesValidD3d12Defaults),
    ("native Present has an opaque R10 PSO fallback", NativePresentHasOpaqueR10PipelineFallback),
    ("native Present pipeline uses explicit shader linkage semantics", NativePresentPipelineUsesExplicitShaderLinkageSemantics),
    ("native Present ESP restores its own hook without chaining unknown hooks", NativePresentEspRestoresOwnHookWithoutChaining),
    ("native Present capsule projection honors UE camera axes and constrained aspect", NativePresentCapsuleProjectionHonorsCameraAxes),
    ("native Present box uses validated pose or root capsule bounds", NativePresentBoxUsesPoseBoundsWithCapsuleFallback),
    ("native Present snaplines reach the viewport edge for distant targets", NativePresentSnaplinesReachViewportEdge),
    ("native Present requires an exact pawn SDK contract", NativePresentRequiresExactPawnSdkContract),
    ("native Present excludes spectator pawns before geometry", NativePresentExcludesSpectatorPawns),
    ("native Present can select a skeleton profile from bone topology", NativePresentSelectsSkeletonProfileFromBoneTopology),
    ("native Present snapshot double buffer never publishes a partial frame", NativePresentSnapshotDoubleBufferIsAtomic),
    ("spectator paint resolution requires local controller identity", SpectatorPaintResolutionRequiresLocalControllerIdentity),
    ("diagnostic stroke limit requires explicit option", DiagnosticStrokeLimitRequiresExplicitOption),
    ("native accepts the single brush configured range", NativeAcceptsSingleBrushConfiguredRange),
    ("native direct radius uses game defaults and fill stays fixed", NativeDirectRadiusUsesGameDefaultsAndFillStaysFixed),
    ("native spatial replay follows the current pose and camera", NativeSpatialReplayFollowsCurrentPoseAndCamera),
    ("native async paint retains captured component identity", NativeAsyncPaintRetainsCapturedComponentIdentity),
    ("native production local sync uses per-stroke paint", NativeProductionLocalSyncUsesPerStrokePaint),
    ("native preview applies PBR and emissive channels", NativePreviewAppliesPbrAndEmissiveChannels),
    ("native preview expires snapshots after a component change", NativePreviewExpiresSnapshotsAfterComponentChange),
    ("native preview returns before recorded-stroke dispatch", NativePreviewReturnsBeforeRecordedStrokeDispatch),
    ("native normal paint closes scene-lit preview feedback", NativeNormalPaintUsesEnvironmentProjection),
    ("intrinsic emission probe is diagnostic-only and observable", IntrinsicEmissionProbeIsDiagnosticOnlyAndObservable),
    ("appearance capture hides only the live brush visual", AppearanceCaptureHidesOnlyLiveBrushVisual),
    ("payload uses native paint route and includes fill material", PayloadUsesNativePaintRouteAndFillMaterial),
    ("legacy mirror-like Fill PBR defaults migrate to manual material", LegacyFillPbrDefaultsMigrateToManualMaterial),
    ("locales have complete keys", LocalesHaveCompleteKeys),
    ("color parser accepts rrggbb", ColorParserAcceptsHex),
    ("runtime log keeps repeated guard messages", RuntimeLogKeepsRepeatedGuardMessages),
    ("asset validation rejects stale ready cache", AssetValidationRejectsStaleReadyCache),
    ("copy if invalid repairs corrupt target", CopyIfInvalidRepairsCorruptTarget),
    ("runtime bundle identity tracks native behavior", RuntimeBundleIdentityTracksNativeBehavior),
    ("bridge generation policy replaces only safe mismatches", BridgeGenerationPolicyReplacesOnlySafeMismatches),
    ("resident core parser distinguishes legacy and V2 generations", ResidentCoreParserDistinguishesLegacyAndV2Generations),
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
    ("startup diagnostics expose OS and native instruction support", StartupDiagnosticsExposeOsAndNativeInstructionSupport),
    ("diagnostic summary includes file not found details", DiagnosticSummaryIncludesFileNotFoundDetails),
    ("diagnostics log write is best effort when file is locked", DiagnosticsLogWriteIsBestEffortWhenFileLocked),
    ("runtime log write is best effort when file is locked", RuntimeLogWriteIsBestEffortWhenFileLocked),
    ("auto material surface is absent", AutoMaterialSurfaceIsAbsent),
    ("scene lighting surface is absent", SceneLightingSurfaceIsAbsent),
    ("ESP snaplines default on", EspSnaplinesDefaultOn),
    ("regions default to side and back paint", RegionsDefaultToSideAndBackPaint),
    ("image design defaults are safe and persist", ImageDesignDefaultsAreSafeAndPersist),
    ("image design preserves the fukuyoka body type", ImageDesignPreservesFukuyokaBodyType),
    ("web Image Fill payload uses an RGB object", WebImageFillPayloadUsesRgbObject),
    ("image layer crop validates normalized bounds", ImageLayerCropValidatesNormalizedBounds),
    ("legacy image transforms migrate to individual layers", LegacyImageTransformsMigrateToIndividualLayers),
    ("image preset round-trips an uncompressed container", ImagePresetRoundTripsUncompressedContainer),
    ("v1 image preset expands global transforms to layers", V1ImagePresetExpandsGlobalTransformsToLayers),
    ("GUI image save persists the active preset state", GuiImageSavePersistsActivePresetState),
    ("legacy image config migrates to active image state", LegacyImageConfigMigratesToDiskLibrary),
    ("legacy image-design active state migrates once", LegacyImageDesignActiveStateMigratesOnce),
    ("image paint rejects an unsaved image design draft", ImagePaintRejectsUnsavedImageDesignDraft),
    ("image and normal settings save atomically", ImageAndNormalSettingsSaveAtomically),
    ("bridge messages are user friendly", BridgeMessagesAreUserFriendly),
    ("structured paint faults report actionable details", StructuredPaintFaultsReportActionableDetails),
    ("settings detect supported system language", SettingsDetectSupportedSystemLanguage),
    ("ui snapshot exposes a single brush", UiSnapshotExposesSingleBrush),
    ("web ui exposes one brush slider and compression tolerance", WebUiExposesSingleBrushSliderAndCompressionTolerance),
    ("web ui persists image designs through the tabbed editor", WebUiImagePaintEditorUsesSavedTransaction),
    ("web ui requires explicit Edit while paint is running", WebUiRequiresExplicitEditWhilePaintRunning),
    ("web ui preserves image actions during paint snapshots", WebUiPreservesImageActionsDuringPaintSnapshots),
    ("web ui keeps mesh guides visible with imported images", WebUiKeepsMeshGuidesVisibleWithImportedImages),
    ("web ui rebuilds image guides when the language changes", WebUiRebuildsImageGuidesWhenLanguageChanges),
    ("web ui keeps setting and log tabs distinct without visual noise", WebUiSeparatesSettingAndLogTabs),
    ("web ui scopes edit actions to the active settings tab", WebUiScopesEditActionsToActiveSettingsTab),
    ("web ui keeps manual paint controls inside logs", WebUiOffersManualPaintControls),
    ("runtime snapshot reports manual paint state", RuntimeSnapshotReportsManualPaintState),
    ("paint feedback uses one severity path for buttons and hotkeys", PaintFeedbackUsesOneSeverityPath),
    ("web ui reports the WebView zoom factor in the footer", WebUiReportsWebViewZoomFactorInFooter),
    ("web ui localizes every settings tab", WebUiLocalizesEverySettingsTab),
    ("web ui localizes every ESP control", WebUiLocalizesEveryEspControl),
    ("web ui localizes image editor controls and crop dialog", WebUiLocalizesImageEditorControlsAndCropDialog),
    ("web ui localizes operation errors", WebUiLocalizesOperationErrors),
    ("web ui exposes fukuyoka directly after cube", WebUiExposesFukuyokaDirectlyAfterCube),
    ("host and capture tool preserve fukuyoka", HostAndCaptureToolPreserveFukuyoka),
    ("profile refresh supports fukuyoka", ProfileRefreshSupportsFukuyoka),
    ("native maps the hukuyoka asset identity to fukuyoka", NativeMapsHukuyokaAssetIdentityToFukuyoka),
    ("native startup and WebView recovery dialogs are localized", NativeStartupAndWebViewRecoveryDialogsAreLocalized),
    ("native preset file dialogs are localized", NativePresetFileDialogsAreLocalized),
    ("every declared web localization key resolves in every locale", EveryDeclaredWebLocalizationKeyResolvesInEveryLocale),
    ("web ui uses packaged reference guides without a game connection", WebUiUsesPackagedReferenceGuides),
    ("web UI keeps theme color on readonly range and checkbox controls", WebUiKeepsThemeColorOnReadonlyControls),
    ("web ui renders pass progress and total eta", WebUiRendersPassProgressAndTotalEta),
    ("raw hotkeys suppress repeat until key-up", RawHotkeysSuppressRepeatUntilKeyUp),
    ("raw hotkeys do not reserve system keys", RawHotkeysDoNotReserveSystemKeys),
    ("native progress exposes replay pass state", NativeProgressExposesReplayPassState),
    ("hotkey validation rejects duplicates", HotkeyValidationRejectsDuplicates),
    ("host session reset restores setting default", HostSessionResetRestoresDefault),
    ("host session resets the brush section with current vocabulary", HostSessionResetsBrushSectionWithCurrentVocabulary),
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
    ("bridge V2 start block carries runtime bundle identity", BridgeV2StartBlockCarriesRuntimeBundleIdentity),
    ("bridge V2 hello validates runtime bundle identity", BridgeV2HelloValidatesRuntimeBundleIdentity),
    ("injector result requires matching bridge identity", InjectorResultRequiresMatchingBridgeIdentity),
    ("injector distinguishes identity and load failures", InjectorDistinguishesIdentityAndLoadFailures),
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
    ("release publish rebuilds embedded runtime assets", ReleasePublishRebuildsEmbeddedRuntimeAssets),
    ("release verifies embedded runtime bundle identity", ReleaseVerifiesEmbeddedRuntimeBundleIdentity),
    ("required submodules are declared and checked out", RequiredSubmodulesAreDeclaredAndCheckedOut),
    ("missing Defender exclusion is added with elevation", MissingDefenderExclusionIsAddedWithElevation),
    ("Defender exclusion marker prevents repeated elevation", DefenderExclusionMarkerPreventsRepeatedElevation),
    ("cancelling Defender elevation returns a nonfatal result", CancellingDefenderElevationReturnsNonfatalResult),
    ("configured Defender exclusion does not request elevation", ConfiguredDefenderExclusionDoesNotRequestElevation),
    ("desktop startup ensures the Defender exclusion before the GUI", DesktopStartupEnsuresDefenderExclusionBeforeGui),
    ("Defender elevation uses trusted system PowerShell", DefenderElevationUsesTrustedSystemPowerShell),
    ("release build excludes research runner and devtools", ReleaseBuildExcludesResearchRunnerAndDevTools),
    ("development builds use isolated version scopes", DevelopmentBuildsUseIsolatedVersionScopes)
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

static void ImageDesignDefaultsAreSafeAndPersist()
{
    using var temp = new TempHome();
    var paths = new AppPaths("image-design-persistence-test");
    var settings = new AppSettings();

    Assert(!settings.Image.Enabled, "image paint should be opt-in by default");
    Assert(settings.Image.BodyType == "round", "round should be the default image body");
    Assert(Math.Abs(settings.Image.Roughness - 1.0) < 0.000001,
        "image paint should default to a non-mirrored material");
    Assert(Math.Abs(settings.Image.BrushSizeTexels - 5.0) < 0.000001,
        "image paint should have its own default brush size");
    Assert(Math.Abs(settings.Image.ColorCompressionTolerance) < 0.000001 &&
           settings.Image.FillColor == RgbColor.White &&
           Math.Abs(settings.Image.FillMetallic - 1.0) < 0.000001 &&
           Math.Abs(settings.Image.FillRoughness) < 0.000001 &&
           Math.Abs(settings.Image.FillEmissive) < 0.000001 &&
           settings.Image.CanvasEncodingVersion == 0 &&
           settings.Image.FrontRegionMode == "skip" &&
           settings.Image.RightRegionMode == "skip" &&
           settings.Image.BackRegionMode == "skip" &&
           settings.Image.LeftRegionMode == "skip",
        "image paint should preserve full source detail, own safe Fill defaults, and Skip all four atlas faces by default");

    settings.ActiveImageDesignId = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    new SettingsStore(paths).Save(settings);
    var loaded = new SettingsStore(paths).Load();

    Assert(loaded.ActiveImageDesignId == settings.ActiveImageDesignId,
        "normal settings should persist only the active design reference");
    Assert(!File.ReadAllText(paths.ConfigPath).Contains("canvas_rgba_base64", StringComparison.Ordinal),
        "normal settings must not embed the canonical image canvas");

    var oversizedLayer = new ImagePaintLayer
    {
        FileName = "oversized.png",
        MimeType = "image/png",
        DataBase64 = Convert.ToBase64String(new byte[ImagePaintLayer.MaximumSourceBytes + 1])
    };
    Assert(!oversizedLayer.TryValidate(out _), "image source layers larger than 12 MiB should be rejected");

    var webpLayer = new ImagePaintLayer
    {
        FileName = "sample.webp",
        MimeType = "image/webp",
        DataBase64 = Convert.ToBase64String([1, 2, 3])
    };
    Assert(webpLayer.TryValidate(out _), "image source layers with WebP format should be accepted");
}

static void ImageDesignPreservesFukuyokaBodyType()
{
    var design = new ImagePaintSettings
    {
        Enabled = true,
        Revision = 1,
        CanvasEncodingVersion = ImagePaintSettings.BackgroundPbrCanvasEncodingVersion,
        BodyType = "fukuyoka",
        CanvasRgbaBase64 = Convert.ToBase64String(new byte[ImagePaintSettings.CanvasByteLength]),
        Layers =
        [
            new ImagePaintLayer
            {
                FileName = "fukuyoka.png",
                MimeType = "image/png",
                DataBase64 = Convert.ToBase64String([1])
            }
        ]
    };

    design.ClampDraft();

    Assert(design.BodyType == "fukuyoka",
        "normalizing an Image draft must preserve the selected fukuyoka body");
    Assert(design.TryValidate(out var message),
        "a fukuyoka Image draft must pass the same validation as round and cube: " + message);
}

static void WebImageFillPayloadUsesRgbObject()
{
    var app = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(), "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));

    Assert(app.Contains("function imageFillColorPayload(color)", StringComparison.Ordinal) &&
           app.Contains("fillColor: imageFillColorPayload(imageEditor.fillColor)", StringComparison.Ordinal) &&
           app.Contains("function normalizeImageFillColor(value)", StringComparison.Ordinal) &&
           app.Contains("frontRegionMode: \"skip\"", StringComparison.Ordinal) &&
           app.Contains("rightRegionMode: \"skip\"", StringComparison.Ordinal) &&
           app.Contains("backRegionMode: \"skip\"", StringComparison.Ordinal) &&
           app.Contains("leftRegionMode: \"skip\"", StringComparison.Ordinal),
        "a new web Image design must default every Fill face to Skip and serialize its Fill color as an RGB object");
}

static void ImageLayerCropValidatesNormalizedBounds()
{
    var layer = new ImagePaintLayer
    {
        FileName = "crop.png",
        MimeType = "image/png",
        DataBase64 = Convert.ToBase64String([1]),
        CropX = 0.25,
        CropY = 0.25,
        CropWidth = 0.5,
        CropHeight = 0.5
    };
    Assert(layer.TryValidate(out _), "a crop fully inside the source image should be accepted");

    layer.CropX = -0.01;
    Assert(!layer.TryValidate(out _), "a crop cannot begin outside the source image");
    layer.CropX = 0.75;
    Assert(!layer.TryValidate(out _), "a crop cannot extend past the source image edge");
}

static void LegacyImageTransformsMigrateToIndividualLayers()
{
    var legacy = new ImagePaintSettings
    {
        WrapFaces = true,
        MirrorFrontBack = true,
        Layers =
        [
            new ImagePaintLayer { FileName = "first.png", MimeType = "image/png", DataBase64 = Convert.ToBase64String([1]) },
            new ImagePaintLayer { FileName = "second.jpg", MimeType = "image/jpeg", DataBase64 = Convert.ToBase64String([2]) }
        ]
    };

    legacy.MigrateLegacyLayerTransforms();

    Assert(legacy.Layers.All(layer => layer.WrapAtlasSeam && layer.MirrorFrontBack),
        "a legacy global Wrap/Mirror state must be expanded to every editable source layer");
    Assert(!legacy.WrapFaces && !legacy.MirrorFrontBack,
        "the migrated design must no longer retain global layer transform state");
}

static void ImagePresetRoundTripsUncompressedContainer()
{
    using var temp = new TempHome();
    var paths = new AppPaths("image-preset-container-test-" + Guid.NewGuid().ToString("N"));
    var store = new ImagePresetStore(paths);
    var path = Path.Combine(paths.ImagePresetsDirectory, "example.mcpreset");
    var design = new ImagePaintSettings
    {
        Enabled = true,
        Revision = 1,
        CanvasEncodingVersion = ImagePaintSettings.BackgroundPbrCanvasEncodingVersion,
        BodyType = "cube",
        AlphaMode = "skip",
        FrontRegionMode = "skip",
        RightRegionMode = "fill",
        BackRegionMode = "skip",
        LeftRegionMode = "fill",
        FillColor = new RgbColor(12, 34, 56),
        FillMetallic = 0.25,
        FillRoughness = 0.75,
        FillEmissive = 0.5,
        BrushSizeTexels = 7.5,
        ColorCompressionTolerance = 2.5,
        Metallic = 0.3,
        Roughness = 0.4,
        Emissive = 0.6,
        CanvasRgbaBase64 = Convert.ToBase64String(new byte[ImagePaintSettings.CanvasByteLength]),
        Layers = [new ImagePaintLayer
        {
            FileName = "source.png",
            MimeType = "image/png",
            DataBase64 = Convert.ToBase64String([1, 2, 3]),
            WrapAtlasSeam = true,
            MirrorFrontBack = true
        }]
    };

    Assert(store.SavePreset(path, design).Success, "a valid Image draft should save as a .mcpreset container");
    Assert(File.Exists(path) && Path.GetExtension(path) == ImagePresetStore.PresetExtension,
        "the preset should be one file with the application extension");
    Assert(store.TryLoadPreset(path, out var loaded, out var message) && loaded.Enabled &&
           loaded.Layers.Count == 1 && loaded.Layers[0].WrapAtlasSeam && loaded.Layers[0].MirrorFrontBack &&
           loaded.CanvasRgbaBase64 == design.CanvasRgbaBase64 &&
           loaded.BodyType == design.BodyType && loaded.AlphaMode == design.AlphaMode &&
           loaded.FrontRegionMode == design.FrontRegionMode && loaded.RightRegionMode == design.RightRegionMode &&
           loaded.BackRegionMode == design.BackRegionMode && loaded.LeftRegionMode == design.LeftRegionMode &&
           loaded.FillColor == design.FillColor &&
           Math.Abs(loaded.FillMetallic - design.FillMetallic) < 0.000001 &&
           Math.Abs(loaded.FillRoughness - design.FillRoughness) < 0.000001 &&
           Math.Abs(loaded.FillEmissive - design.FillEmissive) < 0.000001 &&
           Math.Abs(loaded.BrushSizeTexels - design.BrushSizeTexels) < 0.000001 &&
           Math.Abs(loaded.ColorCompressionTolerance - design.ColorCompressionTolerance) < 0.000001 &&
           Math.Abs(loaded.Metallic - design.Metallic) < 0.000001 &&
           Math.Abs(loaded.Roughness - design.Roughness) < 0.000001 &&
           Math.Abs(loaded.Emissive - design.Emissive) < 0.000001,
        "the preset should restore editable sources, the canonical canvas, and every Image setting: " + message);

    var bytes = File.ReadAllBytes(path);
    Assert(bytes.Length > 8 && bytes[0] == (byte)'M' && bytes[1] == (byte)'C' &&
           !(bytes[0] == (byte)'P' && bytes[1] == (byte)'K'),
        "an Image preset must use the application container rather than ZIP compression");
    bytes[^1] ^= 0x01;
    File.WriteAllBytes(path, bytes);
    Assert(!store.TryLoadPreset(path, out _, out _), "a preset with a changed entry must be rejected by its hash");
}

static void V1ImagePresetExpandsGlobalTransformsToLayers()
{
    using var temp = new TempHome();
    var paths = new AppPaths("image-preset-v1-migration-test-" + Guid.NewGuid().ToString("N"));
    var store = new ImagePresetStore(paths);
    var path = Path.Combine(paths.ImagePresetsDirectory, "legacy.mcpreset");
    var canvas = new byte[ImagePaintSettings.CanvasByteLength];
    var source = new byte[] { 1, 2, 3 };
    var manifest = JsonSerializer.SerializeToUtf8Bytes(new
    {
        schema_version = 1,
        image = new
        {
            enabled = true,
            revision = 1,
            canvas_encoding_version = 0,
            body_type = "round",
            alpha_mode = "skip",
            background_color = new { r = 188, g = 188, b = 188 },
            placement = "fit",
            wrap_faces = true,
            mirror_front_back = true,
            brush_size_texels = 5.0,
            color_compression_tolerance = 0.0,
            metallic = 0.0,
            roughness = 1.0,
            emissive = 0.0,
            background_metallic = 0.0,
            background_roughness = 1.0,
            background_emissive = 0.0,
            canvas_rgba_base64 = "",
            layers = new[]
            {
                new
                {
                    asset_id = "",
                    file_name = "legacy.png",
                    mime_type = "image/png",
                    data_base64 = "",
                    center_x = 0.5,
                    center_y = 0.5,
                    width = 1.0,
                    height = 1.0,
                    crop_x = 0.0,
                    crop_y = 0.0,
                    crop_width = 1.0,
                    crop_height = 1.0
                }
            }
        }
    });

    Directory.CreateDirectory(paths.ImagePresetsDirectory);
    using (var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
    using (var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: false))
    {
        writer.Write("MCIPRST1"u8.ToArray());
        writer.Write(1);
        writer.Write(manifest.Length);
        writer.Write(2);
        writer.Write(manifest);
        writer.Write("canvas.rgba");
        writer.Write((long)canvas.Length);
        writer.Write(SHA256.HashData(canvas));
        writer.Write("layers/0.png");
        writer.Write((long)source.Length);
        writer.Write(SHA256.HashData(source));
        writer.Write(canvas);
        writer.Write(source);
    }

    Assert(store.TryLoadPreset(path, out var loaded, out var message) &&
           loaded.Layers.Count == 1 &&
           loaded.Layers[0].WrapAtlasSeam && loaded.Layers[0].MirrorFrontBack &&
           !loaded.WrapFaces && !loaded.MirrorFrontBack,
        "a v1 preset must migrate global Wrap/Mirror to its source layer: " + message);
}

static void GuiImageSavePersistsActivePresetState()
{
    using var temp = new TempHome();
    var version = "image-preset-active-state-test-" + Guid.NewGuid().ToString("N");
    var session = new HostSession(version);
    var design = new ImagePaintSettings
    {
        Enabled = true,
        CanvasEncodingVersion = ImagePaintSettings.BackgroundPbrCanvasEncodingVersion,
        CanvasRgbaBase64 = Convert.ToBase64String(new byte[ImagePaintSettings.CanvasByteLength]),
        Layers = [new ImagePaintLayer { FileName = "source.png", MimeType = "image/png", DataBase64 = Convert.ToBase64String([1, 2, 3]) }]
    };

    var result = session.CommitSettingsWithImage([], design);
    Assert(result.Success && File.Exists(session.Paths.ActiveImageStatePath) && session.Settings.Image.Enabled,
        "GUI Save should atomically arm Image Paint and persist an internal active state");

    var restarted = new HostSession(version);
    Assert(restarted.Settings.Image.Enabled && restarted.Settings.Image.Layers.Count == 1 &&
           restarted.Settings.Image.CanvasRgbaBase64 == design.CanvasRgbaBase64,
        "the active Image state should restore after restarting the tool");

    Assert(restarted.CommitSettingsWithImage([], new ImagePaintSettings()).Success,
        "Save should allow all Image layers to be removed and disable Image Paint");
    var disabled = new HostSession(version);
    Assert(!disabled.Settings.Image.Enabled && disabled.Settings.Image.Layers.Count == 0,
        "a disabled GUI Save must replace the active Image state rather than retain old layers");
}

static void LegacyImageConfigMigratesToDiskLibrary()
{
    using var temp = new TempHome();
    var version = "image-design-legacy-migration-test-" + Guid.NewGuid().ToString("N");
    var paths = new AppPaths(version);
    Directory.CreateDirectory(paths.ConfigDirectory);
    var legacy = new ImagePaintSettings
    {
        Enabled = true,
        Revision = 2,
        CanvasEncodingVersion = ImagePaintSettings.BackgroundPbrCanvasEncodingVersion,
        CanvasRgbaBase64 = Convert.ToBase64String(new byte[ImagePaintSettings.CanvasByteLength]),
        Layers = [new ImagePaintLayer { FileName = "legacy.png", MimeType = "image/png", DataBase64 = Convert.ToBase64String([1, 2, 3]) }]
    };
    var options = new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower };
    File.WriteAllText(paths.ConfigPath, JsonSerializer.Serialize(new { layout_version = 44, image = legacy }, options));

    var session = new HostSession(version);

    Assert(File.Exists(paths.ActiveImageStatePath) && session.Settings.Image.Enabled,
        "a valid embedded image should receive a private active image state");
    Assert(!File.ReadAllText(paths.ConfigPath).Contains("canvas_rgba_base64", StringComparison.Ordinal),
        "the legacy Base64 payload should be removed only after the active state import succeeds");
}

static void LegacyImageDesignActiveStateMigratesOnce()
{
    using var temp = new TempHome();
    var version = "image-design-library-migration-test-" + Guid.NewGuid().ToString("N");
    var paths = new AppPaths(version);
    var legacyLibrary = new ImageDesignLibrary(paths);
    var legacy = new ImagePaintSettings
    {
        Enabled = true,
        Revision = 3,
        CanvasEncodingVersion = ImagePaintSettings.BackgroundPbrCanvasEncodingVersion,
        CanvasRgbaBase64 = Convert.ToBase64String(new byte[ImagePaintSettings.CanvasByteLength]),
        Layers = [new ImagePaintLayer { FileName = "legacy.png", MimeType = "image/png", DataBase64 = Convert.ToBase64String([1, 2, 3]) }]
    };
    var saved = legacyLibrary.Save("", "legacy", legacy);
    Assert(saved.Success && saved.Design is not null, "the legacy active design fixture should save");
    new SettingsStore(paths).Save(new AppSettings { ActiveImageDesignId = saved.Design!.Id });

    var session = new HostSession(version);

    Assert(File.Exists(paths.ActiveImageStatePath) && session.Settings.Image.Enabled &&
           string.IsNullOrEmpty(session.Settings.ActiveImageDesignId),
        "the same-version legacy active design should migrate to private active state");
    Assert(File.Exists(Path.Combine(paths.ImageDesignsDirectory, saved.Design.Id, "design.json")),
        "migration must not delete the legacy image-design library");
    var restarted = new HostSession(version);
    Assert(restarted.Settings.Image.Revision == session.Settings.Image.Revision && restarted.Settings.Image.Enabled,
        "the migration should be one-time; later starts use active state directly");
}

static void ImagePaintRejectsUnsavedImageDesignDraft()
{
    using var temp = new TempHome();
    var session = new HostSession("image-design-draft-guard-test-" + Guid.NewGuid().ToString("N"));

    session.SetImageDesignDraftDirty(true);
    var result = session.RunImagePaintAsync(previewOnly: false, unpreviewOnly: false).GetAwaiter().GetResult();

    Assert(!result.Success && result.Level == CommandResultLevel.Warn &&
           result.Message == "Image Paint: save or cancel the image design before starting.",
        "F5-F8 must not silently run the last saved design while an Image draft is unsaved");
}

static void ImageAndNormalSettingsSaveAtomically()
{
    using var temp = new TempHome();
    var session = new HostSession("image-transaction-test");
    var image = new ImagePaintSettings
    {
        Enabled = true,
        CanvasRgbaBase64 = Convert.ToBase64String(new byte[ImagePaintSettings.CanvasByteLength]),
        Layers =
        [
            new ImagePaintLayer
            {
                FileName = "front.png",
                MimeType = "image/png",
                DataBase64 = Convert.ToBase64String([1, 2, 3, 4])
            }
        ]
    };
    var result = session.CommitSettingsWithImage(
        [new SettingChange("app.startHotkey", JsonSerializer.SerializeToElement("F2"))], image);

    Assert(!result.Success, "a normal settings validation error should reject the combined save");
    Assert(!session.Settings.Image.Enabled, "a failed combined save must not arm the staged image design");
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

static void AppDefaultsUseMinimumWindowSize()
{
    using var temp = new TempHome();
    var defaults = new AppSettings();
    var loaded = new SettingsStore(new AppPaths("window-size-default-test")).Load();

    Assert(defaults.PanelWidth == 960.0 && defaults.PanelHeight == 640.0,
        "a new app settings instance should default to the minimum window size");
    Assert(loaded.PanelWidth == 960.0 && loaded.PanelHeight == 640.0,
        "a new persisted settings file should inherit the minimum window size");
}

static void PayloadSendsSingleBrushPipeline()
{
    var settings = new AppSettings();
    settings.Paint.BrushSizeTexels = 7.5;
    settings.Paint.ColorCompressionTolerance = 4.0;

    var payload = BridgePayloadBuilder.BuildPaintPayload(settings, 42, "Game.exe", new PaintRequestOptions());
    using var doc = JsonDocument.Parse(payload);
    var tuning = doc.RootElement.GetProperty("tuning");
    var source = doc.RootElement.GetProperty("paint_source");

    Assert(Math.Abs(tuning.GetProperty("brush_size_texels").GetDouble() - 7.5) < 0.000001, "payload should send the single brush");
    Assert(Math.Abs(tuning.GetProperty("color_compression_tolerance").GetDouble() - 4.0) < 0.000001,
        "payload should send the compression tolerance");
    Assert(source.GetProperty("kind").GetString() == "environment_capture" &&
           !doc.RootElement.TryGetProperty("image_paint_enabled", out _) &&
           !tuning.TryGetProperty("side_source_max_uv", out _),
        "normal paint should use the typed environment-capture source without legacy source-transfer controls");
    Assert(!tuning.TryGetProperty("brush_1_size_texels", out _) && !tuning.TryGetProperty("brush_2_size_texels", out _),
        "payload should not send retired two-brush keys");
}

static void NormalPaintPayloadUsesIntrinsicMaterialColorPolicy()
{
    var settings = new AppSettings();

    var payload = BridgePayloadBuilder.BuildPaintPayload(
        settings,
        42,
        "Game.exe",
        new PaintRequestOptions());
    using var doc = JsonDocument.Parse(payload);
    var tuning = doc.RootElement.GetProperty("tuning");

    Assert(!tuning.TryGetProperty("auto_material", out _) &&
           !tuning.TryGetProperty("include_shadows", out _) &&
           Math.Abs(tuning.GetProperty("metallic").GetDouble()) < 0.000001 &&
           Math.Abs(tuning.GetProperty("roughness").GetDouble() - 1.0) < 0.000001 &&
           Math.Abs(tuning.GetProperty("emissive").GetDouble()) < 0.000001,
        "normal Paint should omit Scene Lighting and use the fixed-safe manual material defaults");
}

static void ImagePayloadCarriesFullCanonicalCanvas()
{
    var rgba = new byte[ImagePaintSettings.CanvasByteLength];
    for (var index = 0; index < rgba.Length; index++)
        rgba[index] = (byte)(index % 251);
    var image = new ImagePaintOptions(
        ImagePaintSettings.CanvasWidth,
        ImagePaintSettings.CanvasHeight,
        Convert.ToBase64String(rgba),
        "skip",
        new RgbColor(12, 34, 56),
        "fit",
        "round",
        "fill",
        "fill",
        "fill",
        "fill",
        RgbColor.White,
        1.0,
        0.0,
        0.0,
        5.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        1.0,
        0.0,
        9);

    var payload = BridgePayloadBuilder.BuildPaintPayload(
        new AppSettings(), 42, "Game.exe", new PaintRequestOptions(Image: image));
    using var document = JsonDocument.Parse(payload);
    var root = document.RootElement;
    var source = root.GetProperty("paint_source");
    var encoded = source.GetProperty("image_paint_rgba_base64").GetString();
    Assert(source.GetProperty("kind").GetString() == "imported_image" &&
           !root.TryGetProperty("image_paint_enabled", out _) &&
           source.GetProperty("image_paint_width").GetInt32() == ImagePaintSettings.CanvasWidth &&
           source.GetProperty("image_paint_height").GetInt32() == ImagePaintSettings.CanvasHeight &&
           Math.Abs(source.GetProperty("image_paint_fill_color_r").GetDouble() - 1.0) < 0.000001 &&
           Math.Abs(source.GetProperty("image_paint_fill_color_g").GetDouble() - 1.0) < 0.000001 &&
           Math.Abs(source.GetProperty("image_paint_fill_color_b").GetDouble() - 1.0) < 0.000001 &&
           Math.Abs(source.GetProperty("image_paint_fill_metallic").GetDouble() - 1.0) < 0.000001 &&
           Math.Abs(source.GetProperty("image_paint_fill_roughness").GetDouble()) < 0.000001 &&
           Math.Abs(source.GetProperty("image_paint_fill_emissive").GetDouble()) < 0.000001 &&
           source.GetProperty("image_paint_front_region_mode").GetString() == "fill" &&
           source.GetProperty("image_paint_right_region_mode").GetString() == "fill" &&
           source.GetProperty("image_paint_back_region_mode").GetString() == "fill" &&
           source.GetProperty("image_paint_left_region_mode").GetString() == "fill" &&
           encoded is not null &&
           Convert.FromBase64String(encoded).AsSpan().SequenceEqual(rgba) &&
           !source.TryGetProperty("image_paint_wrap_faces", out _) &&
           !source.TryGetProperty("image_paint_mirror_front_back", out _) &&
           !payload.Contains("\\u002B", StringComparison.Ordinal) &&
           Encoding.UTF8.GetByteCount(payload) < 8 * 1024 * 1024,
        "the full 1024x512 RGBA image payload and Image-owned Fill must reach native unescaped and below the bridge request limit");

    var reply = new BridgeReply(
        true,
        false,
        "image_paint_invalid",
        "Imported image data is invalid; painting was not started",
        "{\"success\":false,\"metadata\":{\"image_decode_failure\":\"base64_invalid_length\",\"image_base64_characters\":17,\"image_decoded_bytes\":0}}");
    var detail = HostSession.PaintFailureDetail(reply);
    Assert(detail is not null && detail.Contains("image_decode_failure=base64_invalid_length", StringComparison.Ordinal) &&
           detail.Contains("image_base64_characters=17", StringComparison.Ordinal),
        "image payload rejections should report the native decode boundary in the runtime log");

    var referenceReply = new BridgeReply(
        true,
        false,
        "image_paint_reference_profile_unavailable",
        "The fixed image reference pose is unavailable for the verified mesh profile.",
        "{\"success\":false,\"metadata\":{\"image_paint_reference_profile_catalog_count\":2,\"image_paint_reference_profile_failure\":\"no complete derived Image reference profile matches the verified raw mesh\"}}");
    var referenceDetail = HostSession.PaintFailureDetail(referenceReply);
    Assert(referenceDetail is not null &&
           referenceDetail.Contains("image_paint_reference_profile_catalog_count=2", StringComparison.Ordinal) &&
           referenceDetail.Contains("image_paint_reference_profile_failure=no complete derived Image reference profile", StringComparison.Ordinal),
        "derived Image reference-profile failures should identify the catalog and rejected identity in the runtime log");

    var cacheReply = new BridgeReply(
        true,
        false,
        "runtime_triangle_cache_unavailable",
        "RuntimePaintable cached current triangles are unavailable; mesh-first paint cannot plan safely",
        "{\"success\":false,\"metadata\":{\"runtime_triangle_cache_mode\":\"profile_verified_failed\",\"runtime_triangle_cache_failure\":\"runtime_triangle_cache_unavailable\",\"runtime_triangle_cache_warmup_reason\":\"runtime_triangle_cache_unavailable\",\"runtime_triangle_cache_warmup_hit_test_uncached_called\":true}}");
    var cacheDetail = HostSession.PaintFailureDetail(cacheReply);
    Assert(cacheDetail is not null &&
           cacheDetail.Contains("runtime_triangle_cache_mode=profile_verified_failed", StringComparison.Ordinal) &&
           cacheDetail.Contains("runtime_triangle_cache_warmup_hit_test_uncached_called=true", StringComparison.Ordinal),
        "triangle-cache failures should log the warm-up boundary needed to diagnose a game-layout change");
}

static void StructuredPaintFaultsReportActionableDetails()
{
    var reply = new BridgeReply(
        true,
        false,
        "game_thread_dispatch_exception",
        "paint planning stopped after a structured exception on the game thread",
        """
        {
          "success": false,
          "metadata": {
            "paint_dispatch_exception_stage": "resolve_runtime_triangles",
            "paint_dispatch_exception_kind": "access_violation",
            "paint_dispatch_exception_code": "0xc0000005",
            "paint_dispatch_exception_access_operation": "read",
            "paint_dispatch_exception_fault_address": "0x10",
            "paint_dispatch_exception_instruction_module": "bridge",
            "paint_dispatch_exception_module_offset": "0x1234",
            "paint_dispatch_exception_in_bridge": true,
            "paint_dispatch_exception_restore_verified": false,
            "paint_dispatch_exception_restore_failure": "emergency_preview_session_unavailable"
          }
        }
        """);

    var detail = HostSession.PaintFailureDetail(reply);
    Assert(detail is not null &&
           detail.Contains("paint_dispatch_exception_stage=resolve_runtime_triangles", StringComparison.Ordinal) &&
           detail.Contains("paint_dispatch_exception_kind=access_violation", StringComparison.Ordinal) &&
           detail.Contains("paint_dispatch_exception_access_operation=read", StringComparison.Ordinal) &&
           detail.Contains("paint_dispatch_exception_fault_address=0x10", StringComparison.Ordinal) &&
           detail.Contains("paint_dispatch_exception_instruction_module=bridge", StringComparison.Ordinal) &&
           detail.Contains("paint_dispatch_exception_module_offset=0x1234", StringComparison.Ordinal) &&
           detail.Contains("paint_dispatch_exception_restore_verified=false", StringComparison.Ordinal),
        "a production fault report must distinguish its stage, exception, access, module, offset, and restore result");
}

static void ImageTransparencyFillsRegionsBeforePaintingOpaquePixels()
{
    var repository = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(
        repository, "src", "native", "bridge", "bridge.cpp"));
    var app = File.ReadAllText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var session = File.ReadAllText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.Controller", "HostSession.cs"));

    Assert(bridge.Contains("if (mode == MeshFirstRegionMode::Fill)\n                {\n                    append_candidate(MeshFirstRegionMode::Fill);\n                }\n                if (!sample.image_transparent_skip)\n                {\n                    append_candidate(MeshFirstRegionMode::Paint);", StringComparison.Ordinal) &&
           !bridge.Contains("if (mode == MeshFirstRegionMode::Fill && !sample.image_transparent_skip)", StringComparison.Ordinal),
        "Image region Fill must cover a face even where its imported image is transparent; only Paint may skip transparent pixels");
    Assert(app.Contains("const IMAGE_ALPHA_THRESHOLD = 128;", StringComparison.Ordinal) &&
           app.Contains("if (pixels[index + 3] < IMAGE_ALPHA_THRESHOLD)", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_alpha_mode == \"skip\" && alpha < 128", StringComparison.Ordinal),
        "transparent imported pixels must preserve the region Fill base while ordinary antialiased source pixels retain their image color");
    Assert(session.Contains("image_fill_regions={Field(metadata, \"image_fill_region_count\")}", StringComparison.Ordinal),
        "the Image Paint log must report its own four-face Fill count, not the unrelated normal Paint setting count");
}

static void ImageRegionSkipSuppressesOnlyFill()
{
    var repository = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(repository, "src", "native", "bridge", "bridge.cpp"));
    var markup = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));

    Assert(bridge.Contains("const bool replay_front_enabled = image_paint_enabled ||", StringComparison.Ordinal) &&
           !bridge.Contains("if (image_paint_enabled && any_image_fill_region)", StringComparison.Ordinal) &&
           bridge.Contains("if (mode == MeshFirstRegionMode::Fill)\n                {\n                    append_candidate(MeshFirstRegionMode::Fill);\n                }\n                if (!sample.image_transparent_skip)\n                {\n                    append_candidate(MeshFirstRegionMode::Paint);", StringComparison.Ordinal),
        "an Image design must Paint its opaque pixels even when every Fill region is skipped; Fill and image Paint are separate passes");
    var fillSection = markup.IndexOf("id=\"image-fill-section\"", StringComparison.Ordinal);
    var fillRegion = markup.IndexOf("data-image-region=\"frontRegionMode\"", StringComparison.Ordinal);
    var fillColor = markup.IndexOf("id=\"image-fill-color-picker\"", StringComparison.Ordinal);
    Assert(fillSection >= 0 && fillRegion > fillSection && fillColor > fillRegion,
        "Image Fill regions must appear directly below the Fill heading, before its material controls");
}

static void NativeWarmsUnavailableTriangleCache()
{
    var root = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var bridgeJson = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge_json.inc"));

    Assert(bridge.Contains("const bool runtime_cache_missing_before_warmup = !runtime_triangle_cache.ok;", StringComparison.Ordinal) &&
           bridge.Contains("if (runtime_cache_missing_before_warmup || runtime_cache_unstable_before_warmup)", StringComparison.Ordinal) &&
           bridge.Contains("const auto initialized = sdk_call_no_params_detail(ref, ctx.component, \"InitializePaint\");", StringComparison.Ordinal) &&
           bridge.Contains("else if (!out.is_initialized_before_ok)", StringComparison.Ordinal) &&
           bridge.Contains("\"runtime_triangle_cache_unavailable\"", StringComparison.Ordinal) &&
           bridgeJson.Contains("\"runtime_triangle_cache_warmup_initialize_called\"", StringComparison.Ordinal) &&
           bridgeJson.Contains("\"runtime_triangle_cache_failure\"", StringComparison.Ordinal) &&
           bridgeJson.Contains("\"runtime_triangle_cache_uv_rejections\"", StringComparison.Ordinal) &&
           bridge.Contains("runtime_triangle_cache_matching_count_arrays_seen", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_order_runtime_triangles_by_profile_uv", StringComparison.Ordinal),
        "a missing RuntimePaintable triangle cache must initialize only an explicitly uninitialized component, then return the cache diagnostics needed to investigate a game-layout change");
}

static void NativeAcceptsVerifiedDirectTriangleOrder()
{
    var root = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var contract = File.ReadAllText(Path.Combine(root, "src", "native", "include", "runtime_contract.hpp"));

    var mappingStart = bridge.IndexOf("auto mesh_first_order_runtime_triangles_by_profile_uv", StringComparison.Ordinal);
    var ambiguityGuard = bridge.IndexOf("A duplicated UV island cannot safely identify geometry", StringComparison.Ordinal);
    var directIndexCall = bridge.IndexOf("order_runtime_triangles_by_direct_profile_index", StringComparison.Ordinal);
    Assert(mappingStart >= 0 && directIndexCall > mappingStart && ambiguityGuard > directIndexCall &&
           contract.Contains("every runtime triangle can still be verified against its profile index", StringComparison.Ordinal) &&
           contract.Contains("order_runtime_triangles_by_direct_profile_index", StringComparison.Ordinal),
        "a fully verified runtime/profile index mapping must be accepted before duplicate UV ambiguity is considered");
}

static void NativeImagePaintResolvesDerivedReferenceProfile()
{
    var root = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    using var raw = JsonDocument.Parse(File.ReadAllText(Path.Combine(
        root, "resources", "mesh-profiles", "paintman_cube.mesh-profile-v2.json")));
    using var image = JsonDocument.Parse(File.ReadAllText(Path.Combine(
        root, "resources", "mesh-profiles", "paintman_cube.image-profile-v2.json")));

    Assert(!raw.RootElement.TryGetProperty("ImageReferencePose", out _) &&
           image.RootElement.TryGetProperty("BaseProfileId", out var baseProfileId) &&
           baseProfileId.GetString() == raw.RootElement.GetProperty("ProfileId").GetString() &&
           image.RootElement.TryGetProperty("BaseProfileHash", out var baseProfileHash) &&
           baseProfileHash.GetString() == raw.RootElement.GetProperty("ProfileHash").GetString() &&
           bridge.Contains("load_mesh_first_image_reference_profile_catalog", StringComparison.Ordinal) &&
           bridge.Contains("*.image-profile-v2.json", StringComparison.Ordinal) &&
           bridge.Contains("select_mesh_first_image_reference_profile", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_reference_profile_id", StringComparison.Ordinal),
        "Image Paint must retain the raw profile for live-mesh identity and select its matching derived profile for the fixed natural-standing reference pose");
}

static void NativeImagePaintExposesDeterministicPlannerTelemetry()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var contract = ReadRepositoryText(Path.Combine(root, "src", "native", "include", "runtime_contract.hpp"));
    var bridgeJson = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge_json.inc"));

    Assert(bridge.Contains("image_paint_mapping_ms", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_mapping_workers", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_candidate_ms", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_mapping_parallel", StringComparison.Ordinal),
        "Image Paint must report its canonical mapping and game-thread candidate phases separately");
    Assert(contract.Contains("adaptive_plan_avx2_available", StringComparison.Ordinal) &&
           contract.Contains("adaptive_plan_worker_count", StringComparison.Ordinal) &&
           contract.Contains("coverage_grid_size", StringComparison.Ordinal) &&
           contract.Contains("representative_error_max", StringComparison.Ordinal),
        "the shared adaptive planner must expose its CPU-safe execution mode to Image Paint");
    Assert(bridgeJson.Contains("image_paint_mapping_ms", StringComparison.Ordinal) &&
           bridgeJson.Contains("image_paint_candidate_ms", StringComparison.Ordinal) &&
           bridgeJson.Contains("adaptive_plan_worker_count", StringComparison.Ordinal) &&
           bridgeJson.Contains("color_compression_strategy", StringComparison.Ordinal) &&
           bridgeJson.Contains("color_compression_representative_error_max", StringComparison.Ordinal),
        "the compact production bridge response must retain Image Paint planner telemetry");
}

static void NativeImagePaintUsesLiveProfileWhenSelectedBodyDiffers()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var bridgeJson = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge_json.inc"));

    Assert(!bridge.Contains("The selected image body does not match the live mesh profile", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_profile_body_type", StringComparison.Ordinal) &&
           bridge.Contains("\"fukuyoka\"", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_requested_body_type", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_body_type_source", StringComparison.Ordinal) &&
           bridge.Contains("live_profile", StringComparison.Ordinal) &&
           bridgeJson.Contains("image_paint_requested_body_type", StringComparison.Ordinal) &&
           bridgeJson.Contains("image_paint_body_type_source", StringComparison.Ordinal),
        "Image Paint must treat the selected body as a UI preference and use the verified live mesh profile for Paint and Preview");
}

static void ImageUnpreviewOmitsPaintPlanDiagnostics()
{
    var root = FindRepositoryRoot();
    var session = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.Controller", "HostSession.cs"));

    Assert(session.Contains("kind == PaintKind.Image && !unpreviewOnly && selectedImage is not null", StringComparison.Ordinal) &&
           session.Contains("kind == PaintKind.Image && !unpreviewOnly)\n                LogImagePaintNativeSummary(response);", StringComparison.Ordinal),
        "Image UnPreview must restore its snapshot without reporting unrelated Image Paint planning diagnostics");
}

static void ImageUnpreviewBypassesImageDesignGuards()
{
    var root = FindRepositoryRoot();
    var session = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.Controller", "HostSession.cs"));
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(session.Contains("kind == PaintKind.Image && !unpreviewOnly && imageDraftDirty", StringComparison.Ordinal) &&
           session.Contains("kind == PaintKind.Image && !unpreviewOnly && selectedImage is null", StringComparison.Ordinal) &&
           session.Contains("Image: unpreviewOnly ? null : selectedImage", StringComparison.Ordinal) &&
           bridge.Contains("if (image_paint_enabled && !unpreview_only)", StringComparison.Ordinal),
        "Image UnPreview must remain available after the saved image is edited, disabled, or removed");
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

static void MiscUsesNativePresentEspRenderer()
{
    var root = FindRepositoryRoot();
    var bridge = File.ReadAllText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var runtime = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.Controller", "RuntimeBridgeService.cs"));
    var session = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.Controller", "HostSession.cs"));
    var mainForm = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var markup = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));

    Assert(markup.Contains("data-settings-panel=\"misc\"", StringComparison.Ordinal) &&
           markup.Contains("data-settings-actions=\"misc\"", StringComparison.Ordinal) &&
           markup.Contains("id=\"esp-enabled\"", StringComparison.Ordinal) &&
           markup.Contains("id=\"esp-boxes\"", StringComparison.Ordinal) &&
           markup.Contains("id=\"esp-skeletons\"", StringComparison.Ordinal) &&
           markup.Contains("id=\"esp-hider-color\"", StringComparison.Ordinal) &&
           markup.Contains("data-esp-scope", StringComparison.Ordinal) &&
           markup.Contains("id=\"esp-hunter-color\"", StringComparison.Ordinal) &&
           markup.Contains("Hider color", StringComparison.Ordinal) &&
           markup.Contains("Hunter color", StringComparison.Ordinal) &&
           app.Contains("bindCheckbox(\"esp-enabled\", \"esp.enabled\")", StringComparison.Ordinal) &&
           app.Contains("for (const scope of [\"all\", \"hider\", \"hunter\"])", StringComparison.Ordinal) &&
           app.Contains("bindColorPair(\"esp-hider-color-picker\", \"esp-hider-color\", \"esp.hiderColor\")", StringComparison.Ordinal) &&
           app.Contains("bindColorPair(\"esp-hunter-color-picker\", \"esp-hunter-color\", \"esp.hunterColor\")", StringComparison.Ordinal) &&
           !app.Contains("esp.enemyColor", StringComparison.Ordinal) &&
           mainForm.Contains("QueueNativePresentEspConfiguration", StringComparison.Ordinal) &&
           mainForm.Contains("nativeEspConfigurationQueue.Enqueue", StringComparison.Ordinal) &&
           mainForm.Contains("ApplyNativePresentEspConfigurationAsync", StringComparison.Ordinal) &&
           !mainForm.Contains("nativeEspConfigurationSignature", StringComparison.Ordinal) &&
           mainForm.Contains("\"hiderColor\"", StringComparison.Ordinal) &&
           mainForm.Contains("\"hunterColor\"", StringComparison.Ordinal) &&
           !mainForm.Contains("EspOverlayForm", StringComparison.Ordinal) &&
           runtime.Contains("ConfigureNativePresentEspAsync", StringComparison.Ordinal) &&
           session.Contains("esp.enabled", StringComparison.Ordinal) &&
           !File.Exists(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "EspOverlayForm.cs")) &&
           bridge.Contains("esp_native_present", StringComparison.Ordinal) &&
           bridge.Contains("d3d12_direct", StringComparison.Ordinal) &&
           !bridge.Contains("misc_apply", StringComparison.Ordinal),
        "Misc must configure only the in-process Native Present ESP renderer; the WinForms/RPM overlay is not a production path");
}

static void NativePresentEspCommandUsesRoleFixedScopeAndColors() =>
    NativePresentEspCommandUsesRoleFixedScopeAndColorsAsync().GetAwaiter().GetResult();

static async Task NativePresentEspCommandUsesRoleFixedScopeAndColorsAsync()
{
    using var temp = new TempHome();
    var paths = new AppPaths("esp-role-command-test");
    var service = new RuntimeBridgeService(paths, new RuntimeLog(paths));
    var instanceId = Guid.Parse("10213243-5465-7687-98a9-bacbdcedfe0f");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("cd", 32));
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var target = TargetProcessIdentity.Create(4242, 1, Path.Combine(Path.GetTempPath(), "game.exe"));
    var instance = new BridgeInstance(target, instanceId, token, hash, "bridge.dll", "injector.exe", "progress.json");
    instance.SetPort(port);
    typeof(RuntimeBridgeService)
        .GetField("activeInstance", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!
        .SetValue(service, instance);
    typeof(RuntimeBridgeService)
        .GetField("bridgeConnected", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!
        .SetValue(service, true);

    string command = "";
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
        await writer.WriteLineAsync("{\"success\":true,\"stage\":\"esp_native_present\",\"message\":\"ok\"}");
    });

    var settings = new EspSettings
    {
        TargetScope = "hunter",
        HiderColor = new RgbColor(1, 2, 3),
        HunterColor = new RgbColor(4, 5, 6)
    };
    var reply = await service.ConfigureNativePresentEspAsync(settings);
    await server;
    using var document = JsonDocument.Parse(command);
    var root = document.RootElement;
    Assert(reply.Ok && reply.Success &&
           root.GetProperty("scope").GetString() == "hunter" &&
           root.GetProperty("hider_r").GetInt32() == 1 &&
           root.GetProperty("hider_g").GetInt32() == 2 &&
           root.GetProperty("hider_b").GetInt32() == 3 &&
           root.GetProperty("hunter_r").GetInt32() == 4 &&
           root.GetProperty("hunter_g").GetInt32() == 5 &&
           root.GetProperty("hunter_b").GetInt32() == 6 &&
           !root.TryGetProperty("ally_r", out _) &&
           !root.TryGetProperty("enemy_r", out _),
        "native Present ESP command should express fixed Hider/Hunter scope and colors");
}

static void LatestAsyncRequestQueuePreservesFinalEspScope() =>
    LatestAsyncRequestQueuePreservesFinalEspScopeAsync().GetAwaiter().GetResult();

static async Task LatestAsyncRequestQueuePreservesFinalEspScopeAsync()
{
    var firstStarted = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var releaseFirst = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    var applied = new List<string>();
    var queue = new LatestAsyncRequestQueue<string>(async scope =>
    {
        if (scope == "all")
        {
            firstStarted.SetResult();
            await releaseFirst.Task;
        }
        lock (applied)
            applied.Add(scope);
        return true;
    });

    Assert(queue.Enqueue("all"), "the initial ESP scope should start a configuration request");
    await firstStarted.Task.WaitAsync(TimeSpan.FromSeconds(2));
    Assert(queue.Enqueue("hider") && queue.Enqueue("hunter"),
        "new ESP scopes should replace pending configuration");
    releaseFirst.SetResult();
    await queue.WhenIdleAsync().WaitAsync(TimeSpan.FromSeconds(2));

    string[] observed;
    lock (applied)
        observed = applied.ToArray();
    Assert(observed.SequenceEqual(["all", "hunter"]),
        "an in-flight request may finish, but only the latest pending ESP scope may run next");
    Assert(!queue.Enqueue("hunter"),
        "the last successfully applied ESP scope should not be resent as unchanged");

    var attempt = 0;
    var retryQueue = new LatestAsyncRequestQueue<string>(_ =>
        Task.FromResult(++attempt > 1));
    Assert(retryQueue.Enqueue("hider"), "a failed ESP scope should be attempted");
    await retryQueue.WhenIdleAsync().WaitAsync(TimeSpan.FromSeconds(2));
    Assert(retryQueue.Enqueue("hider"),
        "a failed ESP scope must remain eligible for a later bridge retry");
    await retryQueue.WhenIdleAsync().WaitAsync(TimeSpan.FromSeconds(2));
    Assert(attempt == 2, "the same ESP scope should retry exactly once after failure");
}

static void NativePresentStatusLoggingIgnoresHealthyCounterChurn()
{
    var first = new NativePresentStatusLogSample
    {
        Status = "ready",
        Reason = "native D3D12 Present compositor active",
        Format = "R10G10B10A2_UNORM",
        ConfiguredScope = "all",
        CaptureStatus = "active",
        CaptureAgeMs = 16,
        HudRebinds = 0,
        HiderRosterCount = 5,
        HunterRosterCount = 5,
        RosterSource = "player_array",
        RosterCount = 10,
        ValidPawns = 10,
        FilteredLocal = 1,
        CapsuleComponents = 9,
        CapsuleTransforms = 9,
        CapsuleSizes = 9,
        CapsuleProjected = 9,
        MeshComponents = 9,
        MeshTransforms = 9,
        SkeletonContracts = 2,
        PoseProfileMatches = 9,
        PoseComponentSpace = 9,
        PoseBones = 252,
        PoseEdges = 243,
        Poses = 9,
        Players = 9,
        Lines = 324,
        Texts = 9,
        Vertices = 14694,
        GlyphQuads = 2132,
        ProjectionCalibrations = 2,
        SnapshotSequence = 71,
        SubmittedFrames = 71,
        CompletedFences = 68,
        RenderedFrames = 71
    };
    var later = first with
    {
        CaptureAgeMs = 0,
        RosterCount = 9,
        ValidPawns = 9,
        CapsuleComponents = 8,
        CapsuleTransforms = 8,
        CapsuleSizes = 8,
        CapsuleProjected = 8,
        MeshComponents = 8,
        MeshTransforms = 8,
        PoseProfileMatches = 8,
        PoseComponentSpace = 8,
        PoseBones = 224,
        PoseEdges = 216,
        Poses = 8,
        Players = 8,
        Lines = 288,
        Texts = 8,
        Vertices = 13434,
        GlyphQuads = 1956,
        ProjectionCalibrations = 6,
        SnapshotSequence = 311,
        SubmittedFrames = 311,
        CompletedFences = 308,
        RenderedFrames = 311
    };

    Assert(
        NativePresentStatusLogPolicy.Signature(first, verbose: false) ==
        NativePresentStatusLogPolicy.Signature(later, verbose: false),
        "normal ESP logging must ignore healthy roster and progress counter churn");
    Assert(
        NativePresentStatusLogPolicy.Signature(first, verbose: true) !=
        NativePresentStatusLogPolicy.Signature(later, verbose: true),
        "explicit verbose ESP logging must retain raw diagnostic counter changes");

    Assert(
        NativePresentStatusLogPolicy.Format(later, verbose: false) ==
        "ESP: status=ready | scope=all | capture=active | roster=player_array:9 | drawable=8/8 | filtered=local:1 spectator:0 scope:0 | miss=capsule:0 mesh:0 pose:0 spatial:0 fault:0",
        "normal ESP status output must remain compact while preserving pipeline coverage");
    Assert(
        NativePresentStatusLogPolicy.Format(later, verbose: true).Contains(
            "snapshot_sequence=311", StringComparison.Ordinal),
        "explicit verbose ESP status output must retain raw diagnostics");

    var scopeChanged = later with { ConfiguredScope = "hunter" };
    Assert(
        NativePresentStatusLogPolicy.Signature(later, verbose: false) !=
        NativePresentStatusLogPolicy.Signature(scopeChanged, verbose: false),
        "normal ESP status logging must expose the native scope that was actually applied");

    var stalled = later with { CaptureStatus = "stalled", CaptureAgeMs = 1500 };
    Assert(
        NativePresentStatusLogPolicy.Signature(later, verbose: false) !=
        NativePresentStatusLogPolicy.Signature(stalled, verbose: false) &&
        NativePresentStatusLogPolicy.ShouldWarn(stalled) &&
        NativePresentStatusLogPolicy.Format(stalled, verbose: false).Contains(
            "snapshot_sequence=311", StringComparison.Ordinal),
        "capture stalls must remain visible as a warning state transition");

    var paintBusy = later with { CaptureStatus = "busy", CaptureAgeMs = 1500 };
    Assert(
        NativePresentStatusLogPolicy.Signature(later, verbose: false) !=
        NativePresentStatusLogPolicy.Signature(paintBusy, verbose: false) &&
        !NativePresentStatusLogPolicy.ShouldWarn(paintBusy),
        "paint-owned capture pauses must remain visible without being misreported as ESP failure");

    var offscreen = later with
    {
        Reason = "native D3D12 Present compositor active (no geometry in current snapshot)",
        Players = 0,
        Lines = 0,
        Texts = 0,
        Vertices = 0,
        GlyphQuads = 0
    };
    Assert(
        NativePresentStatusLogPolicy.Signature(later, verbose: true) !=
        NativePresentStatusLogPolicy.Signature(offscreen, verbose: true) &&
        NativePresentStatusLogPolicy.Signature(later, verbose: false) ==
        NativePresentStatusLogPolicy.Signature(offscreen, verbose: false),
        "normal ESP logging must ignore visibility-only geometry churn while verbose mode retains it");

    var brokenTransform = later with { CapsuleTransforms = 0, CapsuleSizes = 0 };
    Assert(
        NativePresentStatusLogPolicy.Signature(later, verbose: false) !=
        NativePresentStatusLogPolicy.Signature(brokenTransform, verbose: false),
        "normal ESP logging must retain upstream capture-stage failures");

    var missingMeshContract = later with { MissingMeshContracts = 2 };
    Assert(
        NativePresentStatusLogPolicy.Signature(later, verbose: false) !=
        NativePresentStatusLogPolicy.Signature(missingMeshContract, verbose: false) &&
        NativePresentStatusLogPolicy.ShouldWarn(missingMeshContract),
        "normal ESP logging must surface a missing Mesh capability without discarding independent Box geometry");

    var isolatedTargetFault = later with { TargetFaults = 1 };
    Assert(
        NativePresentStatusLogPolicy.Signature(later, verbose: false) !=
        NativePresentStatusLogPolicy.Signature(isolatedTargetFault, verbose: false) &&
        NativePresentStatusLogPolicy.ShouldWarn(isolatedTargetFault),
        "normal ESP logging must surface a target-local fault without requiring the renderer to become unavailable");

    var initializing = later with
    {
        Status = "initializing",
        Reason = "waiting for an exact or uniquely observed swapchain command queue",
        Format = "",
        InitializationAgeMs = 2_000,
        InitializationStage = "queue_match",
        PresentHookReady = true,
        ExecuteHookReady = true,
        ResizeHookReady = true,
        FactoryHooksReady = true,
        GameSwapchainSeen = true,
        PresentCalls = 120,
        ExecuteCalls = 5_000,
        SwapchainQueueRecords = 0,
        RecentDirectQueueRecords = 1,
        SubmittedFrames = 0,
        CompletedFences = 0,
        RenderedFrames = 0
    };
    Assert(
        !NativePresentStatusLogPolicy.ShouldWarn(initializing),
        "native Present initialization must retain a short startup grace period");

    var stuckInitializing = initializing with { InitializationAgeMs = 12_000 };
    var stuckInitializationLog =
        NativePresentStatusLogPolicy.Format(stuckInitializing, verbose: false);
    Assert(
        NativePresentStatusLogPolicy.ShouldWarn(stuckInitializing) &&
        stuckInitializationLog.Contains(
            "init=queue_match age_ms=12000", StringComparison.Ordinal) &&
        stuckInitializationLog.Contains(
            "hooks=present:True execute:True resize:True factory:True",
            StringComparison.Ordinal) &&
        stuckInitializationLog.Contains(
            "traffic=present:120 execute:5000", StringComparison.Ordinal) &&
        stuckInitializationLog.Contains(
            "association=swapchain:True exact:0 recent:1",
            StringComparison.Ordinal) &&
        stuckInitializationLog.Contains(
            "waiting for an exact or uniquely observed swapchain command queue",
            StringComparison.Ordinal),
        "stalled native Present initialization must identify the hook, traffic, swapchain, and queue-association boundary");
    Assert(
        NativePresentStatusLogPolicy.Signature(initializing, verbose: false) !=
        NativePresentStatusLogPolicy.Signature(stuckInitializing, verbose: false),
        "crossing the initialization grace period must produce one warning transition");
    var stuckCounterChurn = stuckInitializing with
    {
        InitializationAgeMs = 24_000,
        PresentCalls = 240,
        ExecuteCalls = 10_000
    };
    Assert(
        NativePresentStatusLogPolicy.Signature(stuckInitializing, verbose: false) ==
        NativePresentStatusLogPolicy.Signature(stuckCounterChurn, verbose: false),
        "stalled initialization diagnostics must not repeat for healthy counter churn");

    var previousFlag = Environment.GetEnvironmentVariable("MECCHA_ESP_VERBOSE_STATUS");
    try
    {
        Environment.SetEnvironmentVariable("MECCHA_ESP_VERBOSE_STATUS", null);
        Assert(!BuildFeatures.NativeEspVerboseStatusEnabled,
            "verbose ESP logging must default off");
        Environment.SetEnvironmentVariable("MECCHA_ESP_VERBOSE_STATUS", "1");
        Assert(BuildFeatures.NativeEspVerboseStatusEnabled,
            "verbose ESP logging must require its explicit environment flag");
    }
    finally
    {
        Environment.SetEnvironmentVariable("MECCHA_ESP_VERBOSE_STATUS", previousFlag);
    }
}

static void NativePresentEspHasNoExternalFallback()
{
    var root = FindRepositoryRoot();
    var mainForm = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(!mainForm.Contains("EspOverlayForm", StringComparison.Ordinal) &&
           !mainForm.Contains("EspMemoryReader", StringComparison.Ordinal) &&
           !File.Exists(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "EspOverlayForm.cs")) &&
           !File.Exists(Path.Combine(root, "src", "csharp", "MecchaCamouflage.WebHost", "EspMemoryReader.cs")) &&
           bridge.Contains("external_overlay\\\":false", StringComparison.Ordinal) &&
           !bridge.Contains("SetEvent(signal);", StringComparison.Ordinal),
        "native Present ESP must fail closed instead of signalling or starting an external overlay frame renderer");
}

static void NativePresentEspOwnsRendererLifecycle()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var runtime = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.Controller", "RuntimeBridgeService.cs"));

    Assert(bridge.Contains("esp_execute_command_lists_detour", StringComparison.Ordinal) &&
           bridge.Contains("esp_resize_buffers_detour", StringComparison.Ordinal) &&
           bridge.Contains("esp_create_swapchain_for_hwnd_detour", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_track_swapchain_queue", StringComparison.Ordinal) &&
           bridge.Contains("BridgeResidentCoreV2", StringComparison.Ordinal) &&
           bridge.Contains("bridge host detached; resident core remains available", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_queue_for_late_present", StringComparison.Ordinal) &&
           bridge.Contains("esp_unique_queue_identity", StringComparison.Ordinal) &&
           bridge.Contains(
               "waiting for an exact or uniquely observed swapchain command queue",
               StringComparison.Ordinal) &&
           !bridge.Contains(
               "swapchain queue has no factory record or same-thread Present submission",
               StringComparison.Ordinal) &&
           bridge.Contains("DXGI_FORMAT_R10G10B10A2_UNORM", StringComparison.Ordinal) &&
           bridge.Contains("D3D12_RESOURCE_STATE_PRESENT", StringComparison.Ordinal) &&
           bridge.Contains("D3D12_RESOURCE_STATE_RENDER_TARGET", StringComparison.Ordinal) &&
           bridge.Contains("CreateRenderTargetView", StringComparison.Ordinal) &&
           bridge.Contains("CreateCommandAllocator", StringComparison.Ordinal) &&
           bridge.Contains("CreateFence", StringComparison.Ordinal) &&
           bridge.Contains("ExecuteCommandLists(1, lists)", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_render_snapshot_guarded", StringComparison.Ordinal) &&
           bridge.Contains("native Present compositor faulted", StringComparison.Ordinal) &&
           bridge.Contains("GetCurrentBackBufferIndex", StringComparison.Ordinal) &&
           bridge.Contains("g_esp_native_snapshot_readers", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_present_status_native", StringComparison.Ordinal) &&
           bridge.Contains("snapshot_sequence", StringComparison.Ordinal) &&
           bridge.Contains("capture_age_ms", StringComparison.Ordinal) &&
           bridge.Contains("hud_rebinds", StringComparison.Ordinal) &&
           bridge.Contains("valid_pawns", StringComparison.Ordinal) &&
           bridge.Contains("capsule_projected", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_skeleton_contract_for_mesh", StringComparison.Ordinal) &&
           bridge.Contains(
               "SkeletalMeshComponent_CachedComponentSpaceTransforms",
               StringComparison.Ordinal) &&
           bridge.Contains("esp_native_pose_topology_error", StringComparison.Ordinal) &&
           bridge.Contains("pose_component_space", StringComparison.Ordinal) &&
           !bridge.Contains("esp_native_compose_local_pose", StringComparison.Ordinal) &&
           bridge.Contains("runtime_contract::esp_ascii_glyph_rows", StringComparison.Ordinal) &&
           bridge.Contains("glyph_quads", StringComparison.Ordinal) &&
           bridge.Contains("snapshot.text_count", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_calibrate_projection", StringComparison.Ordinal) &&
           bridge.Contains("sdk_project_world_to_screen", StringComparison.Ordinal) &&
           bridge.Contains("projection_scale_x", StringComparison.Ordinal) &&
           bridge.Contains("submitted_frames", StringComparison.Ordinal) &&
           bridge.Contains("completed_fences", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_present_probe", StringComparison.Ordinal) &&
           bridge.Contains("runtime_contract::esp_hud_callback_matches", StringComparison.Ordinal) &&
           !bridge.Contains("reinterpret_cast<std::uintptr_t>(object) ==\n                g_esp_hud_callback_target", StringComparison.Ordinal) &&
           bridge.Contains("rendered_frames", StringComparison.Ordinal) &&
           runtime.Contains("ConfigureNativePresentEspAsync", StringComparison.Ordinal) &&
           runtime.Contains("force_rebind", StringComparison.Ordinal) &&
           runtime.Contains("ArmNativePresentEspProbeAsync", StringComparison.Ordinal) &&
           runtime.Contains("TryAttachResidentCore", StringComparison.Ordinal) &&
           runtime.Contains("GetNativePresentEspStatusAsync", StringComparison.Ordinal),
        "native Present ESP must capture the exact swapchain queue, use the validated component-space pose contract and direct glyph geometry, retain one resident renderer across host reconnects, publish completed snapshots, and expose lifecycle status");
}

static void RuntimeSafelyReplacesObsoleteResidentBridgeGenerations()
{
    var root = FindRepositoryRoot();
    var runtime = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.Controller", "RuntimeBridgeService.cs"));

    Assert(runtime.Contains("TryAttachResidentCore", StringComparison.Ordinal) &&
           runtime.Contains("BridgeGenerationPolicy.Decide", StringComparison.Ordinal) &&
           runtime.Contains("ShutdownProvedQuiescence", StringComparison.Ordinal) &&
           runtime.Contains("WaitForResidentCoreAbsent", StringComparison.Ordinal) &&
           runtime.Contains("RuntimeBundleMatches", StringComparison.Ordinal) &&
           runtime.Contains("FailGenerationUpgrade", StringComparison.Ordinal) &&
           runtime.Contains("replacementAttempts", StringComparison.Ordinal) &&
           !runtime.Contains("LogResidentBridgeBinaryState", StringComparison.Ordinal),
        "a GUI update must authenticate the complete native bundle, quiesce an obsolete resident, and fail closed instead of reusing stale code");
}

static void ResidentBridgeReconnectRetainsProgressPath() =>
    ResidentBridgeReconnectRetainsProgressPathAsync().GetAwaiter().GetResult();

static async Task ResidentBridgeReconnectRetainsProgressPathAsync()
{
    using var temp = new TempHome();
    var paths = new AppPaths("resident-progress-reconnect-test");
    var service = new RuntimeBridgeService(paths, new RuntimeLog(paths));
    var instanceId = Guid.Parse("31415926-5358-9793-2384-626433832795");
    var token = Enumerable.Range(1, BridgeStartBlockV1.TokenLength).Select(value => (byte)value).ToArray();
    var hash = string.Concat(Enumerable.Repeat("ef", 32));
    var progressPath = Path.Combine(paths.BridgeProgressDirectory, $"{instanceId:N}.progress.json");
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    var target = TargetProcessIdentity.Create(4242, 1, Path.Combine(Path.GetTempPath(), "game.exe"));
    var resident = new BridgeInstance(target, instanceId, token, hash, "", "", "");
    resident.SetPort(port);

    var activeField = typeof(RuntimeBridgeService).GetField(
        "activeInstance",
        System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)
        ?? throw new InvalidOperationException("activeInstance field missing");
    activeField.SetValue(service, resident);

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
            metadata = new
            {
                pid = 4242,
                instance_id = instanceId.ToString("N"),
                bridge_hash = hash,
                protocol_version = 1,
                progress_path = progressPath
            }
        }));
        _ = await reader.ReadLineAsync();
        await writer.WriteLineAsync("{\"success\":true,\"stage\":\"ping\",\"message\":\"ok\"}");
    });

    var reply = await service.PingAsync();
    await server;

    Assert(reply.Ok && reply.Success &&
           string.Equals(service.ProgressPath, Path.GetFullPath(progressPath), StringComparison.Ordinal),
        "an authenticated resident reconnect must retain the exact native progress path");

    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    Assert(bridge.Contains("\\\"progress_path\\\"", StringComparison.Ordinal) &&
           bridge.Contains("bridge_progress_path()", StringComparison.Ordinal),
        "the native authenticated hello must publish its actual progress sidecar path");
}

static void ResearchHotReloadQuiescesAndSerializesBridgeGenerations()
{
    var root = FindRepositoryRoot();
    var script = ReadRepositoryText(Path.Combine(
        root,
        "scripts",
        "research",
        "hot-reload-bridge.ps1"));

    Assert(script.Contains(@"Local\MecchaCamouflage.Inject.$TargetPid", StringComparison.Ordinal) &&
           script.Contains("active_paint_quiescent", StringComparison.Ordinal) &&
           script.Contains("hook_callbacks_quiescent", StringComparison.Ordinal) &&
           script.Contains("Wait-ResidentCoreAbsent", StringComparison.Ordinal) &&
           script.Contains("runtime-bridge-hot-$($bundle.Id).dll", StringComparison.Ordinal) &&
           script.Contains("MaxGenerations", StringComparison.Ordinal) &&
           script.Contains("ProfileDirectory", StringComparison.Ordinal) &&
           script.Contains(@"mesh-profiles", StringComparison.Ordinal) &&
           script.Contains("Get-NativeRuntimeBundle", StringComparison.Ordinal) &&
           script.Contains("runtime_bundle_id", StringComparison.Ordinal) &&
           script.Contains("Get-FileHash -Algorithm SHA256", StringComparison.Ordinal) &&
           script.Contains("Invoke-Injector", StringComparison.Ordinal) &&
           script.Contains("redacted", StringComparison.Ordinal),
        "research hot reload must hold the production injection mutex, verify native quiescence, inject a content-addressed DLL, cap inert generations, and never expose bridge credentials");
}

static void ConnectedGameProcessRemainsPinned()
{
    var selected = GameProcessSelectionPolicy.SelectProcessId(
        [3180, 8860],
        pinnedProcessId: 8860);
    var ambiguous = GameProcessSelectionPolicy.SelectProcessId(
        [3180, 8860],
        pinnedProcessId: null);
    var unique = GameProcessSelectionPolicy.SelectProcessId(
        [3180],
        pinnedProcessId: null);

    Assert(selected == 8860 &&
           ambiguous is null &&
           unique == 3180,
        "the connected PID must remain pinned, and an unpinned target must be unambiguous");
}

static void LiveGameIdentityBlocksTargetReplacement()
{
    Assert(!GameProcessSelectionPolicy.MayReplacePinnedTarget(
               hasPinnedTarget: true,
               pinnedTargetIsAlive: true) &&
           GameProcessSelectionPolicy.MayReplacePinnedTarget(
               hasPinnedTarget: true,
               pinnedTargetIsAlive: false) &&
           GameProcessSelectionPolicy.MayReplacePinnedTarget(
               hasPinnedTarget: false,
               pinnedTargetIsAlive: false),
        "a new target may be selected only after the pinned game identity exits");
}

static void UnresponsiveMatchingBridgeIsNotReinjected()
{
    var root = FindRepositoryRoot();
    var runtime = ReadRepositoryText(Path.Combine(
        root, "src", "csharp", "MecchaCamouflage.Controller", "RuntimeBridgeService.cs"));
    var injectionMethod = runtime.IndexOf(
        "private bool InjectDirectInstanceOnMutexThread(",
        StringComparison.Ordinal);
    var mutexAcquired = runtime.IndexOf(
        "if (!ownsMutex)",
        injectionMethod,
        StringComparison.Ordinal);
    var residentRecheck = runtime.IndexOf(
        "TryAttachResidentCore(target)",
        mutexAcquired,
        StringComparison.Ordinal);
    var staging = runtime.IndexOf(
        "PrepareDirectBridgeInstance(target, researchOptions, desiredBundle)",
        mutexAcquired,
        StringComparison.Ordinal);

    Assert(injectionMethod >= 0 &&
           mutexAcquired > injectionMethod &&
           residentRecheck > mutexAcquired &&
           staging > residentRecheck &&
           runtime.Contains("matching resident bridge is unresponsive", StringComparison.Ordinal),
        "automatic warmup must recheck under the mutex and fail closed when a matching generation cannot authenticate");
}

static void NativePresentReportsBackbufferSetupStep()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var host = ReadRepositoryText(Path.Combine(
        root,
        "src",
        "csharp",
        "MecchaCamouflage.WebHost",
        "MainForm.cs"));

    Assert(bridge.Contains("native D3D12 backbuffer setup failed at ", StringComparison.Ordinal) &&
           bridge.Contains("CheckFeatureSupport(format)", StringComparison.Ordinal) &&
           bridge.Contains("CreateGraphicsPipelineState(pipeline)", StringComparison.Ordinal) &&
           bridge.Contains("failure_step", StringComparison.Ordinal) &&
           bridge.Contains("\\\"initialization_stage\\\"", StringComparison.Ordinal) &&
           bridge.Contains("\\\"present_hook_ready\\\"", StringComparison.Ordinal) &&
           bridge.Contains("\\\"execute_calls\\\"", StringComparison.Ordinal) &&
           bridge.Contains("\\\"swapchain_queue_records\\\"", StringComparison.Ordinal) &&
           host.Contains("\"initialization_stage\"", StringComparison.Ordinal) &&
           host.Contains("\"present_hook_ready\"", StringComparison.Ordinal) &&
           host.Contains("\"execute_calls\"", StringComparison.Ordinal) &&
           host.Contains("\"swapchain_queue_records\"", StringComparison.Ordinal),
        "native Present diagnostics must identify the exact D3D12 initialization and queue-association boundary and preserve it in host logs");
}

static void NativePresentPipelineInitializesValidD3d12Defaults()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("D3D12_COMPARISON_FUNC_LESS", StringComparison.Ordinal) &&
           bridge.Contains("D3D12_DEFAULT_STENCIL_READ_MASK", StringComparison.Ordinal) &&
           bridge.Contains("D3D12_BLEND_ONE", StringComparison.Ordinal) &&
           bridge.Contains("D3D12_BLEND_ZERO", StringComparison.Ordinal),
        "the direct PSO must explicitly initialize all depth-stencil and blend fields that D3D12 validates");
}

static void NativePresentHasOpaqueR10PipelineFallback()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("opaque R10 fallback", StringComparison.Ordinal) &&
           bridge.Contains("blend.BlendEnable = FALSE", StringComparison.Ordinal),
        "the R10 Present path must retain a non-blended PSO candidate when a driver rejects alpha blending");
}

static void NativePresentPipelineUsesExplicitShaderLinkageSemantics()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("struct EspDirectPixelInput", StringComparison.Ordinal) &&
           bridge.Contains("float4 color : COLOR0", StringComparison.Ordinal) &&
           bridge.Contains(": SV_TARGET0", StringComparison.Ordinal),
        "the production vertex and pixel shaders must use an explicit matching COLOR0 linkage");
}

static void NativePresentEspRestoresOwnHookWithoutChaining()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var build = ReadRepositoryText(Path.Combine(root, "scripts", "build.ps1"));

    Assert(bridge.Contains("MH_CreateHook", StringComparison.Ordinal) &&
           bridge.Contains("MH_ApplyQueued", StringComparison.Ordinal) &&
           bridge.Contains("MH_RemoveHook", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_uninstall_present_sync", StringComparison.Ordinal) &&
           bridge.Contains("not a raw vtable", StringComparison.Ordinal) &&
           !bridge.Contains("*present_slot = reinterpret_cast<void*>(&esp_present_detour)", StringComparison.Ordinal) &&
           build.Contains("third_party\\minhook", StringComparison.Ordinal),
        "the bridge must use trampoline detours and remove only its own hooks instead of replacing a shared graphics vtable");
}

static void NativePresentCapsuleProjectionHonorsCameraAxes()
{
    var view = new NativePresentCamera(
        new NativePresentVector3(0, 0, 0),
        new NativePresentRotator(0, 0, 0),
        90,
        16.0 / 9.0,
        NativePresentFovAxis.MaintainY);

    Assert(NativePresentProjection.TryProject(
               view,
               new NativePresentVector3(1000, 0, 0),
               3440,
               1440,
               out var centre) &&
           Math.Abs(centre.X - 1720) < 0.001 && Math.Abs(centre.Y - 720) < 0.001,
        "a MaintainY 90 degree UE camera must put its forward vector at the constrained viewport centre");

    Assert(NativePresentProjection.TryProject(
               view,
               new NativePresentVector3(1000, 100, 0),
               3440,
               1440,
               out var right) &&
           Math.Abs(right.X - 1848) < 0.001 && Math.Abs(right.Y - 720) < 0.001,
        "MaintainY must use height × aspect ratio as its horizontal focal length");

    var rolled = view with { Rotation = new NativePresentRotator(0, 0, 90) };
    Assert(NativePresentProjection.TryProject(
               rolled,
               new NativePresentVector3(1000, 0, 100),
               3440,
               1440,
               out var rollPoint) &&
           Math.Abs(rollPoint.X - 1592) < 0.001 && Math.Abs(rollPoint.Y - 720) < 0.001,
        "roll must be part of the UE camera rotation matrix rather than an ignored overlay approximation");
}

static void NativePresentBoxUsesPoseBoundsWithCapsuleFallback()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var view = new NativePresentCamera(
        new NativePresentVector3(0, 0, 0),
        new NativePresentRotator(0, 0, 0),
        90,
        16.0 / 9.0,
        NativePresentFovAxis.MaintainY);
    var capsule = new NativePresentCapsule(
        new NativePresentVector3(1000, 0, 0),
        new NativePresentRotator(0, 0, 0),
        Radius: 50,
        HalfHeight: 100);

    Assert(NativePresentProjection.TryProject(view, new NativePresentVector3(1000, 0, 100), 3440, 1440, out var apex) &&
           Math.Abs(apex.Y - 648) < 0.001,
        $"the collision capsule apex itself must project before building its bounds (actual={apex})");
    Assert(NativePresentProjection.TryProjectCapsuleBounds(view, capsule, 3440, 1440, out var box) &&
           box.Left <= 1656 && box.Right >= 1784 && box.Top <= 648 && box.Bottom >= 792 &&
           box.Width > 100 && box.Height > 100 &&
           bridge.Contains("esp_native_component_pose", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_project_pose_bounds", StringComparison.Ordinal) &&
           bridge.Contains("esp_expand_screen_bounds", StringComparison.Ordinal),
        $"the renderer must prefer validated animated pose bounds while retaining the authoritative root-capsule bounds (actual={box})");
}

static void NativePresentRequiresExactPawnSdkContract()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains("esp_native_pawn_sdk_contract", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_resolve_pawn_components", StringComparison.Ordinal) &&
           bridge.Contains("esp_select_target_pawn_source(", StringComparison.Ordinal) &&
           bridge.Contains("esp_should_refresh_avatar_directory(", StringComparison.Ordinal) &&
           bridge.Contains("esp_cached_avatar_binding_is_usable(", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_cached_avatar_matches", StringComparison.Ordinal) &&
           bridge.Contains("UWorld_PersistentLevel", StringComparison.Ordinal) &&
           bridge.Contains("ULevel_Actors", StringComparison.Ordinal) &&
           bridge.Contains("avatar_actor_scans", StringComparison.Ordinal) &&
           bridge.Contains("avatar_actor_candidates", StringComparison.Ordinal) &&
           bridge.Contains("avatar_pawn_recoveries", StringComparison.Ordinal) &&
           bridge.Contains("esp_native_capture_target_guarded", StringComparison.Ordinal) &&
           bridge.Contains("missing_capsule_contracts", StringComparison.Ordinal) &&
           bridge.Contains("missing_mesh_contracts", StringComparison.Ordinal) &&
           bridge.Contains("target_faults", StringComparison.Ordinal) &&
           bridge.Contains("\"RootComponent\"", StringComparison.Ordinal) &&
           bridge.Contains("find_object_property(ref, pawn, \"Mesh\")", StringComparison.Ordinal) &&
           bridge.Contains(
               "native snapshot is waiting for PlayerArray",
               StringComparison.Ordinal) &&
           !bridge.Contains("EspPawnGeometryContract", StringComparison.Ordinal) &&
           !bridge.Contains("unsupported_contracts", StringComparison.Ordinal) &&
           !bridge.Contains("esp_native_actor_bounds", StringComparison.Ordinal) &&
           !bridge.Contains("esp_native_project_world_bounds", StringComparison.Ordinal) &&
           !bridge.Contains("target.pawn = found->second.pawn", StringComparison.Ordinal) &&
           !bridge.Contains("role_roster_fallback", StringComparison.Ordinal),
        "ESP must join PlayerArray identity to a verified role-roster avatar, resolve RootComponent and Mesh independently, and isolate a broken target without promoting an unrelated pawn or guessed bounds");
}

static void NativePresentSnaplinesReachViewportEdge()
{
    var start = new NativePresentPoint(1720, 1438);

    Assert(NativePresentProjection.TryClipLineEndpointToViewport(
               start,
               new NativePresentPoint(-4000, 300),
               3440,
               1440,
               out var left) &&
           Math.Abs(left.X) < 0.001 &&
           left.Y is >= 0 and <= 1440,
        $"a projected target beyond the left edge must retain a clipped snapline endpoint (actual={left})");
    Assert(NativePresentProjection.TryClipLineEndpointToViewport(
               start,
               new NativePresentPoint(1900, 700),
               3440,
               1440,
               out var inside) &&
           Math.Abs(inside.X - 1900) < 0.001 &&
           Math.Abs(inside.Y - 700) < 0.001,
        $"an on-screen target must keep its exact projected endpoint (actual={inside})");

    var camera = new NativePresentCamera(
        new NativePresentVector3(0, 0, 0),
        new NativePresentRotator(0, 0, 0),
        90,
        16.0 / 9.0,
        NativePresentFovAxis.MaintainY);
    var behind = default(NativePresentPoint);
    var behindEdge = default(NativePresentPoint);
    Assert(
        NativePresentProjection.TryProjectSnaplineTarget(
            camera,
            new NativePresentVector3(-1000, 100, 0),
            3440,
            1440,
            out behind) &&
        NativePresentProjection.TryClipLineEndpointToViewport(
            start,
            behind,
            3440,
            1440,
            out behindEdge) &&
        (Math.Abs(behindEdge.X) < 0.001 ||
         Math.Abs(behindEdge.X - 3440) < 0.001 ||
         Math.Abs(behindEdge.Y) < 0.001),
        $"a target behind the camera must retain a directional snapline at the viewport edge (actual={behindEdge})");

    var bridge = ReadRepositoryText(Path.Combine(
        FindRepositoryRoot(), "src", "native", "bridge", "bridge.cpp"));
    Assert(
        bridge.Contains(
            "esp_native_clip_line_endpoint_to_viewport(",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "esp_native_project_snapline_target(",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "else if (target_origin_valid)",
            StringComparison.Ordinal) &&
        !bridge.Contains(
            "if (draw_snaplines && box_valid)",
            StringComparison.Ordinal),
        "snapline generation must use the current projected target origin when distant pose bounds collapse below box size");
}

static void NativePresentExcludesSpectatorPawns()
{
    var bridge = ReadRepositoryText(Path.Combine(
        FindRepositoryRoot(), "src", "native", "bridge", "bridge.cpp"));

    Assert(
        bridge.Contains(
            "target_role == runtime_contract::EspRole::Spectator",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "contains_text(class_name, \"spectate\")",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "contains_text(class_name, \"spectator\")",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "contains_text(class_name, \"hunter\")",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "contains_text(class_name, \"survivor\")",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "esp_resolve_target_role(",
            StringComparison.Ordinal) &&
        bridge.Contains(
            "++snapshot.filtered_spectators;",
            StringComparison.Ordinal) &&
        !bridge.Contains(
            "\"bIsSpectator\"",
            StringComparison.Ordinal) &&
        !bridge.Contains(
            "esp_snapshot_pawn_is_spectator",
            StringComparison.Ordinal),
        "the current Pawn class must replace stale role-roster state across Hider, Hunter, and Spectator transitions");
}

static void NativePresentSelectsSkeletonProfileFromBoneTopology()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));

    Assert(bridge.Contains(
               "contract = esp_native_skeleton_contract_for_mesh(ref, mesh);",
               StringComparison.Ordinal) &&
           bridge.Contains(
               "SkeletalMeshComponent_CachedComponentSpaceTransforms",
               StringComparison.Ordinal) &&
           bridge.Contains(
               "sdk::FieldOffsets::SkeletalMeshComponent_CachedComponentSpaceTransforms",
               StringComparison.Ordinal) &&
           bridge.Contains(
               "esp_native_pose_topology_error(",
               StringComparison.Ordinal) &&
           bridge.Contains(
               "esp_native_resolve_pawn_components(",
               StringComparison.Ordinal) &&
           bridge.Contains(
               "esp_native_component_pose(",
               StringComparison.Ordinal) &&
           !bridge.Contains("verified_offsets", StringComparison.Ordinal) &&
           !bridge.Contains("candidate_contracts", StringComparison.Ordinal) &&
           !bridge.Contains("accessor.last_probe_ms", StringComparison.Ordinal) &&
           !bridge.Contains(
               "resolve_pose_candidate(candidate)",
               StringComparison.Ordinal),
        "an exact Mesh must use its current asset profile and validated CachedComponentSpaceTransforms contract without scanning alternative arrays or profiles");
}

static void NativePresentSnapshotDoubleBufferIsAtomic()
{
    var publisher = new NativePresentSnapshotBuffer<int>(capacity: 4);
    Assert(!publisher.TryRead(out _), "an empty native snapshot buffer must not expose an incomplete frame");

    var writer = publisher.BeginWrite();
    writer.Add(17);
    Assert(!publisher.TryRead(out _), "the reader must not observe a writer-owned frame before publication");
    publisher.Publish(writer);

    Assert(publisher.TryRead(out var frame) && frame.Sequence == 1 && frame.Items.Span.SequenceEqual([17]),
        "publication must atomically hand off the complete snapshot and sequence number");
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

    Assert(bridge.Contains("json_number_field(request, \"brush_size_texels\", 5.0)", StringComparison.Ordinal) &&
           bridge.Contains("1.0, 10.0", StringComparison.Ordinal),
        "native paint payload parsing must preserve the configured 1-10 single-brush range");
    Assert(bridge.Contains("json_number_field(request, \"color_compression_tolerance\", 5.0)", StringComparison.Ordinal) &&
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

    Assert(bridge.Contains("appearance_normalized_screen_position_valid(", StringComparison.Ordinal) &&
           bridge.Contains("sample.appearance_screen_nx *", StringComparison.Ordinal) &&
           bridge.Contains("\"appearance_capture_coordinates\"", StringComparison.Ordinal) &&
           bridge.Contains("else if (!appearance_replay_uses_capture_projection)", StringComparison.Ordinal) &&
           bridge.Contains("sdk_project_world_to_screen(", StringComparison.Ordinal) &&
           bridge.Contains("current_pose_camera_scanline_before_adaptive_radius_order", StringComparison.Ordinal),
        "appearance replay should reuse its stable capture-camera projection while other modes retain current-pose live projection");
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
    Assert(bridge.Contains("const int local_sample_batch_limit =", StringComparison.Ordinal) &&
           bridge.Contains("async_job->replication_pacing.calls_per_tick;", StringComparison.Ordinal),
        "production paint must schedule only the reflected, bounded game-native route");
    var contract = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(),
        "src", "native", "include", "runtime_contract.hpp"));
    Assert(contract.Contains("constexpr int NativeRecordedPaintMaxCallsPerTick = 4;", StringComparison.Ordinal) &&
           contract.Contains("constexpr int NativeRecordedPaintQueueTargetStrokes = 2;", StringComparison.Ordinal) &&
           contract.Contains("ConservativeReplicationCapacityNumerator = 4;", StringComparison.Ordinal) &&
           contract.Contains("ConservativeReplicationCapacityDenominator = 5;", StringComparison.Ordinal) &&
           contract.Contains("ConservativeReplicationBurstCalls = 3;", StringComparison.Ordinal) &&
           contract.Contains("AssumedRemotePaintFps = 60;", StringComparison.Ordinal) &&
           contract.Contains("paint_replication_pacing_plan(", StringComparison.Ordinal) &&
           contract.Contains("local_dispatch_adaptive_delay_ms(", StringComparison.Ordinal),
        "native paint must retain a conservative bounded dispatch derived from game-owned network and receiver limits");
    Assert(bridge.Contains("job->local_dispatch_last_slice_us", StringComparison.Ordinal) &&
           bridge.Contains("runtime_contract::local_dispatch_adaptive_delay_ms(", StringComparison.Ordinal) &&
           bridge.Contains("job->local_dispatch_delay_ms", StringComparison.Ordinal) &&
           bridge.Contains("local_dispatch_throttle_events", StringComparison.Ordinal),
        "an over-budget PaintAtUVWithBrush call must defer the next game-thread slice and expose the throttle decision");
    Assert(bridge.Contains("direct_paint_capture_queue_snapshot", StringComparison.Ordinal) &&
           bridge.Contains("GetQueuedStrokeCountForComponent", StringComparison.Ordinal) &&
           bridge.Contains("\"QueuedOutgoingBatches\"", StringComparison.Ordinal) &&
           bridge.Contains("direct_paint_queue_array_remaining", StringComparison.Ordinal) &&
           bridge.Contains("native_queue_backpressure", StringComparison.Ordinal) &&
           bridge.Contains("direct_paint_queue_target_strokes", StringComparison.Ordinal) &&
           bridge.Contains("mesh_direct_paint_cancel_drain", StringComparison.Ordinal) &&
           bridge.Contains("waiting for the game's recorded-paint queue", StringComparison.Ordinal),
        "native paint must use game-owned component and outgoing queues for backpressure, completion, and cancel drain");
    Assert(bridge.Contains("paint_queue_observer_authoritative(", StringComparison.Ordinal) &&
           bridge.Contains("paint_final_queue_ready(", StringComparison.Ordinal) &&
           bridge.Contains("paint_visual_drain_complete(", StringComparison.Ordinal) &&
           bridge.Contains("final_queue_ready_at", StringComparison.Ordinal) &&
           bridge.Contains("replication_conservative_receiver_strokes_per_second", StringComparison.Ordinal) &&
           bridge.Contains("replay_visual_offset", StringComparison.Ordinal) &&
           bridge.Contains("final_visual_completion_confirmed", StringComparison.Ordinal),
        "submission must remain incomplete until sender drain and a stable receiver confirmation window both finish");
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
           bridge.Contains("mesh_unpreview_packed_pbr_mismatch", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_restore_appearance_preview_session(", StringComparison.Ordinal) &&
           bridge.Contains("unpreview_restore_verified", StringComparison.Ordinal),
        "preview and unpreview must preserve packed Metallic/Roughness/Emissive data without successive imports overwriting it");
}

static void NativePreviewExpiresSnapshotsAfterComponentChange()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(root, "src", "native", "bridge", "bridge.cpp"));
    var session = ReadRepositoryText(Path.Combine(root, "src", "csharp", "MecchaCamouflage.Controller", "HostSession.cs"));

    Assert(!bridge.Contains("\"mesh_preview_component_mismatch\"", StringComparison.Ordinal) &&
           !bridge.Contains("\"mesh_unpreview_component_mismatch\"", StringComparison.Ordinal) &&
           bridge.Contains("\"mesh_unpreview_expired\"", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_clear_preview_snapshot();", StringComparison.Ordinal) &&
           session.Contains("lower is \"mesh_unpreview_expired\"", StringComparison.Ordinal),
        "component changes must expire an old Preview session without writing its bytes into the new component");
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

static void NativeNormalPaintUsesEnvironmentProjection()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(
        root,
        "src", "native", "bridge", "bridge.cpp"));
    var contract = ReadRepositoryText(Path.Combine(
        root,
        "src", "native", "include", "runtime_contract.hpp"));

    Assert(bridge.Contains("PaintSourceKind::EnvironmentCapture", StringComparison.Ordinal) &&
           bridge.Contains("PaintSourceKind::ImportedImage", StringComparison.Ordinal) &&
           bridge.Contains("\"paint_source_kind_invalid\"", StringComparison.Ordinal),
        "native paint should require the typed paint source contract");
    Assert(contract.Contains("environment_projected_capture_coordinate", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_assign_environment_projection_colors", StringComparison.Ordinal) &&
           bridge.Contains("environment_projection_schema", StringComparison.Ordinal) &&
           bridge.Contains("current_view_projective_v1", StringComparison.Ordinal) &&
           !bridge.Contains("map_environment_atlas_coordinate", StringComparison.Ordinal),
        "normal paint should sample every mesh point from its exact hidden-capture projection without atlas normalization");
    var projectionAssignmentStart = bridge.IndexOf(
        "auto mesh_first_assign_environment_projection_colors(",
        StringComparison.Ordinal);
    var projectionAssignmentEnd = bridge.IndexOf(
        "struct MeshFirstAppearanceCluster",
        projectionAssignmentStart,
        StringComparison.Ordinal);
    Assert(projectionAssignmentStart >= 0 && projectionAssignmentEnd > projectionAssignmentStart,
        "environment projection assignment must remain inspectable as one bounded native stage");
    var projectionAssignment = bridge[projectionAssignmentStart..projectionAssignmentEnd];
    Assert(!projectionAssignment.Contains("mesh_first_region_enabled", StringComparison.Ordinal) &&
           !projectionAssignment.Contains("enable_front", StringComparison.Ordinal) &&
           !projectionAssignment.Contains("appearance_rescue_emission_color", StringComparison.Ordinal) &&
           projectionAssignment.Contains("const SdkFrontCaptureResult* emission_capture", StringComparison.Ordinal) &&
           bridge.Contains("kAppearanceCalibrationStepTexels = 4.0", StringComparison.Ordinal) &&
           bridge.Contains("appearance_preview_replay_candidates.push_back", StringComparison.Ordinal) &&
           bridge.Contains("runtime_contract::ReplayRegionMode::Paint", StringComparison.Ordinal),
        "capture, source assignment, and local calibration preview must be region-mask and selected-brush independent");
    Assert(bridge.Contains("\"environment_base_color\"", StringComparison.Ordinal) &&
           bridge.Contains("base_capture_request.hide_target_component = true", StringComparison.Ordinal) &&
           bridge.Contains("base_capture_request.retain_capture_pixels = true", StringComparison.Ordinal) &&
           bridge.Contains("\"environment_intrinsic_emission_hdr\"", StringComparison.Ordinal) &&
           bridge.Contains("emission_capture_request.profile =", StringComparison.Ordinal) &&
           bridge.Contains("SdkSceneCaptureProfile::IntrinsicEmission", StringComparison.Ordinal) &&
           bridge.Contains("\"environment_scene_lit_reference_hdr\"", StringComparison.Ordinal) &&
           bridge.Contains("appearance_albedo_closed_loop_correction", StringComparison.Ordinal) &&
           bridge.Contains("appearance_physical_emission_evidence", StringComparison.Ordinal) &&
           bridge.Contains("appearance_compose_physical_emission_material", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_assign_physical_emission_components", StringComparison.Ordinal) &&
           contract.Contains("appearance_validate_physical_emission_component", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_build_emission_response_field", StringComparison.Ordinal) &&
           bridge.Contains("sample.appearance_metallic =\n                            resolved_paint_metallic", StringComparison.Ordinal) &&
           bridge.Contains("sample.appearance_roughness =\n                            resolved_paint_roughness", StringComparison.Ordinal) &&
           bridge.Contains("sample.appearance_emissive =\n                            resolved_paint_emissive", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_fixed_four_texel_correction_lattice", StringComparison.Ordinal) &&
           bridge.Contains("environment_albedo_feedback_physical_emission_v2", StringComparison.Ordinal) &&
           bridge.Contains("appearance_albedo_only_improvement", StringComparison.Ordinal) &&
           bridge.Contains("appearance_final_painted_nonzero_emissive_pixels", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_apply_local_material_import_preview", StringComparison.Ordinal) &&
           CountOccurrences(
               bridge,
               "appearance_emission_chromaticity_albedo(") == 1 &&
           !bridge.Contains("appearance_rescue_emission_color(", StringComparison.Ordinal) &&
           !bridge.Contains("\"mesh_emission_capture_failed\"", StringComparison.Ordinal) &&
           !bridge.Contains("\"mesh_emission_classification_failed\"", StringComparison.Ordinal) &&
           contract.Contains("source_noise_floor_first", StringComparison.Ordinal) &&
           contract.Contains("source_noise_floor_second", StringComparison.Ordinal) &&
           !bridge.Contains("tuning_include_shadows", StringComparison.Ordinal) &&
           !bridge.Contains("json_bool_field(request, \"include_shadows\"", StringComparison.Ordinal),
        "normal paint should preserve raw BaseColor, correct Albedo on a fixed shared field, and infer Emissive only from repeatable source evidence plus measured target response");
    Assert(!bridge.Contains("tuning_auto_material", StringComparison.Ordinal) &&
           !bridge.Contains("mesh_first_get_dominant_material_properties", StringComparison.Ordinal) &&
           !bridge.Contains("auto_material_resolution_failed", StringComparison.Ordinal) &&
           !bridge.Contains("appearance_match_requested", StringComparison.Ordinal) &&
           !bridge.Contains("appearance_make_calibrated_emissive_parameters", StringComparison.Ordinal) &&
           !bridge.Contains("appearance_make_cluster_local_candidate", StringComparison.Ordinal) &&
           !contract.Contains("appearance_should_resolve_screen_hit_uv", StringComparison.Ordinal),
        "normal Paint should have one manual material route without retired Auto Material fitting branches");
    Assert(!bridge.Contains("appearance_fallback_stroke_samples", StringComparison.Ordinal) &&
           !bridge.Contains("mesh_first_assign_colors(profile_available", StringComparison.Ordinal),
        "the production bridge should not retain per-sample Appearance Match fallback or source-mesh transfer");
}

static void IntrinsicEmissionProbeIsDiagnosticOnlyAndObservable()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(
        root, "src", "native", "bridge", "bridge.cpp"));
    var bridgeJson = ReadRepositoryText(Path.Combine(
        root, "src", "native", "bridge", "bridge_json.inc"));
    var diagnostics = ReadRepositoryText(Path.Combine(
        root,
        "src",
        "csharp",
        "MecchaCamouflage.LiveDiagnostics",
        "Program.cs"));

    Assert(bridge.Contains("SdkSceneCaptureProfile::IntrinsicEmission", StringComparison.Ordinal) &&
           bridge.Contains("SetShowFlagSettings", StringComparison.Ordinal) &&
           bridge.Contains("appearance_intrinsic_emission_show_flags", StringComparison.Ordinal) &&
           bridge.Contains("appearance_show_flags_readback_ok", StringComparison.Ordinal) &&
           bridge.Contains("appearance_emission_isolation_probe_complete", StringComparison.Ordinal) &&
           bridge.Contains("appearance_preview_count\\\":0", StringComparison.Ordinal) &&
           bridge.Contains("appearance_direct_stroke_count\\\":0", StringComparison.Ordinal) &&
           bridge.Contains("appearance_production_connected\\\":false", StringComparison.Ordinal) &&
           bridgeJson.Contains("\"appearance_isolation_energy_mean\"", StringComparison.Ordinal) &&
           bridgeJson.Contains("\"appearance_show_flags_requested_names\"", StringComparison.Ordinal) &&
           diagnostics.Contains("appearance-emission-isolation-probe", StringComparison.Ordinal) &&
           diagnostics.Contains("appearance-emission-target-differential", StringComparison.Ordinal) &&
           diagnostics.Contains("appearance-color-differential", StringComparison.Ordinal) &&
           diagnostics.Contains("manual-preview-hold", StringComparison.Ordinal) &&
           diagnostics.Contains("appearanceCaptureArtifacts: true", StringComparison.Ordinal) &&
           diagnostics.Contains("property.Name.StartsWith(\"environment_\"", StringComparison.Ordinal) &&
           diagnostics.Contains("appearance_emission_isolation_target_visible", StringComparison.Ordinal) &&
           diagnostics.Contains("ResidentCoreMagicV2", StringComparison.Ordinal) &&
           diagnostics.Contains("runtime_bundle_id", StringComparison.Ordinal),
        "the intrinsic emission backend must expose applied ShowFlags and E0/E1 research evidence without applying Preview or direct production strokes");
}

static void AppearanceCaptureHidesOnlyLiveBrushVisual()
{
    var root = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(
        root, "src", "native", "bridge", "bridge.cpp"));
    var bridgeJson = ReadRepositoryText(Path.Combine(
        root, "src", "native", "bridge", "bridge_json.inc"));
    var hostSession = ReadRepositoryText(Path.Combine(
        root,
        "src",
        "csharp",
        "MecchaCamouflage.Controller",
        "HostSession.cs"));

    Assert(bridge.Contains("sdk_collect_brush_plane_visual_components", StringComparison.Ordinal) &&
           bridge.Contains("sdk_brush_plane_actor_for_live_component", StringComparison.Ordinal) &&
           bridge.Contains("\"persistentlevel\"", StringComparison.Ordinal) &&
           bridge.Contains("\"bp_brushplane\"", StringComparison.Ordinal) &&
           bridge.Contains("request.extra_hidden_components", StringComparison.Ordinal) &&
           bridge.Contains("\"HideComponent\"", StringComparison.Ordinal) &&
           bridge.Contains("appearance_brush_visual_hidden_count", StringComparison.Ordinal) &&
           !bridge.Contains("SetActorHiddenInGame", StringComparison.Ordinal) &&
           !bridge.Contains("appearance_intrinsic_emission_show_flags_with_decals_disabled", StringComparison.Ordinal) &&
           bridgeJson.Contains("\"appearance_brush_visual_hidden_count\"", StringComparison.Ordinal) &&
           bridgeJson.Contains("\"appearance_brush_visual_hide_failures\"", StringComparison.Ordinal) &&
           hostSession.Contains("appearance_brush_visual_component_count", StringComparison.Ordinal) &&
           hostSession.Contains("appearance_brush_visual_hidden_count", StringComparison.Ordinal) &&
           hostSession.Contains("appearance_brush_visual_hide_failures", StringComparison.Ordinal),
        "Appearance Match must hide only the live BP_BrushPlane visual from each capture without mutating the world or suppressing map decals");
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

static void RuntimeBundleIdentityTracksNativeBehavior()
{
    var root = Path.Combine(Path.GetTempPath(), "meccha-runtime-bundle-" + Guid.NewGuid().ToString("N"));
    var profiles = Path.Combine(root, "mesh-profiles");
    Directory.CreateDirectory(profiles);
    try
    {
        var bridge = Path.Combine(root, "runtime-bridge.dll");
        File.WriteAllBytes(bridge, [1, 2, 3, 4]);
        File.WriteAllText(Path.Combine(profiles, "z.image-profile-v2.json"), "{\"kind\":\"image\"}");
        File.WriteAllText(Path.Combine(profiles, "a.mesh-profile-v2.json"), "{\"kind\":\"mesh\"}");

        var first = NativeRuntimeBundle.Create(bridge, profiles);

        File.Delete(Path.Combine(profiles, "z.image-profile-v2.json"));
        File.WriteAllText(Path.Combine(profiles, "z.image-profile-v2.json"), "{\"kind\":\"image\"}");
        var reordered = NativeRuntimeBundle.Create(bridge, profiles);
        Assert(first.Id == reordered.Id, "bundle identity must not depend on filesystem enumeration order");

        File.WriteAllText(Path.Combine(profiles, "z.image-profile-v2.json"), "{\"kind\":\"image\",\"revision\":2}");
        var changedProfile = NativeRuntimeBundle.Create(bridge, profiles);
        Assert(first.Id != changedProfile.Id, "changing any profile must change the runtime bundle identity");

        File.WriteAllBytes(bridge, [4, 3, 2, 1]);
        var changedBridge = NativeRuntimeBundle.Create(bridge, profiles);
        Assert(changedProfile.Id != changedBridge.Id, "changing the native bridge must change the runtime bundle identity");
        Assert(first.CanonicalManifest.Contains("start_block_abi=2\n", StringComparison.Ordinal) &&
               first.CanonicalManifest.Contains("resident_core_abi=2\n", StringComparison.Ordinal) &&
               first.CanonicalManifest.Contains("protocol=2\n", StringComparison.Ordinal),
            "bundle identity must include every native compatibility boundary");
        Assert(!first.CanonicalManifest.Contains("app_version", StringComparison.Ordinal) &&
               !first.CanonicalManifest.Contains("injector", StringComparison.Ordinal),
            "application version and injector metadata must not change resident behavior identity");
        static string ManifestId(string manifest) =>
            Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(manifest))).ToLowerInvariant();
        Assert(ManifestId(first.CanonicalManifest) != ManifestId(
                   first.CanonicalManifest.Replace("start_block_abi=2\n", "start_block_abi=3\n", StringComparison.Ordinal)) &&
               ManifestId(first.CanonicalManifest) != ManifestId(
                   first.CanonicalManifest.Replace("resident_core_abi=2\n", "resident_core_abi=3\n", StringComparison.Ordinal)) &&
               ManifestId(first.CanonicalManifest) != ManifestId(
                   first.CanonicalManifest.Replace("protocol=2\n", "protocol=3\n", StringComparison.Ordinal)),
            "changing any compatibility ABI or protocol must change the runtime bundle identity");
    }
    finally
    {
        Directory.Delete(root, recursive: true);
    }
}

static void BridgeGenerationPolicyReplacesOnlySafeMismatches()
{
    var desired = string.Concat(Enumerable.Repeat("ab", 32));
    var stale = string.Concat(Enumerable.Repeat("cd", 32));

    Assert(BridgeGenerationPolicy.Decide(
               hasResident: true,
               residentRuntimeBundleId: desired,
               desiredRuntimeBundleId: desired,
               replacementAlreadyAttempted: false,
               generationCount: 1,
               generationLimit: 3) == BridgeGenerationAction.Reconnect,
        "an identical resident generation should reconnect");
    Assert(BridgeGenerationPolicy.Decide(true, stale, desired, false, 1, 3) == BridgeGenerationAction.Replace,
        "a stale V2 resident should be replaced once");
    Assert(BridgeGenerationPolicy.Decide(true, null, desired, false, 1, 3) == BridgeGenerationAction.Replace,
        "a legacy resident should be upgraded rather than accepted");
    Assert(BridgeGenerationPolicy.Decide(false, null, desired, false, 0, 3) == BridgeGenerationAction.Inject,
        "a process without a resident should receive its first generation");
    Assert(BridgeGenerationPolicy.Decide(true, stale, desired, true, 1, 3) == BridgeGenerationAction.RestartRequired,
        "a failed replacement must not loop");
    Assert(BridgeGenerationPolicy.Decide(true, stale, desired, false, 3, 3) == BridgeGenerationAction.RestartRequired,
        "the production generation cap must fail closed");
    Assert(!BridgeGenerationPolicy.MayDeleteInstanceDirectory(
               ownerIsCurrentTarget: true,
               ownerProcessIsAlive: true,
               moduleIsLoaded: true) &&
           !BridgeGenerationPolicy.MayDeleteInstanceDirectory(
               ownerIsCurrentTarget: false,
               ownerProcessIsAlive: true,
               moduleIsLoaded: false) &&
           BridgeGenerationPolicy.MayDeleteInstanceDirectory(
               ownerIsCurrentTarget: true,
               ownerProcessIsAlive: true,
               moduleIsLoaded: false) &&
           BridgeGenerationPolicy.MayDeleteInstanceDirectory(
               ownerIsCurrentTarget: false,
               ownerProcessIsAlive: false,
               moduleIsLoaded: false),
        "cleanup must preserve every loaded generation and every directory owned by another live process");
}

static void ResidentCoreParserDistinguishesLegacyAndV2Generations()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var token = Enumerable.Range(1, 32).Select(value => (byte)value).ToArray();
    var bridgeHash = Enumerable.Repeat((byte)0xab, 32).ToArray();
    var bundleHash = Enumerable.Repeat((byte)0xcd, 32).ToArray();

    var legacy = new byte[104];
    BinaryPrimitives.WriteUInt32LittleEndian(legacy.AsSpan(0, 4), 0x3152434D);
    BinaryPrimitives.WriteUInt32LittleEndian(legacy.AsSpan(4, 4), 104);
    BinaryPrimitives.WriteUInt32LittleEndian(legacy.AsSpan(8, 4), 1);
    BinaryPrimitives.WriteUInt32LittleEndian(legacy.AsSpan(12, 4), 4242);
    BinaryPrimitives.WriteUInt32LittleEndian(legacy.AsSpan(16, 4), 54321);
    BinaryPrimitives.WriteUInt32LittleEndian(legacy.AsSpan(20, 4), 1);
    instanceId.TryWriteBytes(legacy.AsSpan(24, 16), bigEndian: true, out _);
    token.CopyTo(legacy, 40);
    bridgeHash.CopyTo(legacy, 72);
    Assert(BridgeResidentCore.TryParse(legacy, 4242, out var parsedLegacy) &&
           parsedLegacy.IsLegacy &&
           parsedLegacy.RuntimeBundleId is null,
        "V1 resident must remain readable only as an upgrade source");

    var current = new byte[136];
    BinaryPrimitives.WriteUInt32LittleEndian(current.AsSpan(0, 4), 0x3252434D);
    BinaryPrimitives.WriteUInt32LittleEndian(current.AsSpan(4, 4), 136);
    BinaryPrimitives.WriteUInt32LittleEndian(current.AsSpan(8, 4), 2);
    BinaryPrimitives.WriteUInt32LittleEndian(current.AsSpan(12, 4), 4242);
    BinaryPrimitives.WriteUInt32LittleEndian(current.AsSpan(16, 4), 54321);
    BinaryPrimitives.WriteUInt32LittleEndian(current.AsSpan(20, 4), 2);
    instanceId.TryWriteBytes(current.AsSpan(24, 16), bigEndian: true, out _);
    token.CopyTo(current, 40);
    bridgeHash.CopyTo(current, 72);
    bundleHash.CopyTo(current, 104);
    Assert(BridgeResidentCore.TryParse(current, 4242, out var parsedCurrent) &&
           !parsedCurrent.IsLegacy &&
           parsedCurrent.RuntimeBundleId == Convert.ToHexString(bundleHash).ToLowerInvariant(),
        "V2 resident must expose the full runtime bundle identity");

    current[104] = 0;
    Array.Clear(current, 104, 32);
    Assert(!BridgeResidentCore.TryParse(current, 4242, out _),
        "V2 resident with a zero bundle identity must fail closed");
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

static void StartupDiagnosticsExposeOsAndNativeInstructionSupport()
{
    var capabilities = new RuntimePlatformCapabilities(
        "Microsoft Windows",
        new Version(10, 0, 26100, 4652),
        System.Runtime.InteropServices.Architecture.X64,
        System.Runtime.InteropServices.Architecture.X64,
        Sse2Supported: true,
        AvxSupported: true,
        Avx2Supported: true);

    Assert(
        RuntimePlatformDiagnostics.Format(capabilities) ==
        "System: os=Microsoft Windows 10.0.26100.4652 | arch=process:x64 os:x64 | cpu_isa=sse2:on avx:on avx2:on | native=x64/sse2 adaptive_avx2:on",
        "startup diagnostics must report the OS build, architecture, required SSE2 baseline, and optional AVX2 path in one stable line");

    var unsupportedOptionalPath = capabilities with
    {
        AvxSupported = false,
        Avx2Supported = false
    };
    Assert(
        RuntimePlatformDiagnostics.Format(unsupportedOptionalPath).Contains(
            "native=x64/sse2 adaptive_avx2:off",
            StringComparison.Ordinal),
        "unsupported optional AVX2 must be reported as a safe scalar fallback rather than a startup failure");

    var injectedText = capabilities with
    {
        OsDescription = "Windows\r\n[ERROR] fake | field"
    };
    var sanitized = RuntimePlatformDiagnostics.Format(injectedText);
    Assert(
        !sanitized.Contains('\r') &&
        !sanitized.Contains('\n') &&
        sanitized.Count(character => character == '|') == 3,
        "platform descriptions must not inject extra runtime-log records or fields");
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

static void AutoMaterialSurfaceIsAbsent()
{
    var root = FindRepositoryRoot();
    var index = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var researchRunner = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "ResearchRunner.cs"));
    var liveDiagnostics = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.LiveDiagnostics", "Program.cs"));
    var localization = File.ReadAllText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.Core", "Localization", "Strings.json"));
    var bridgeJson = File.ReadAllText(Path.Combine(
        root,
        "src", "native", "bridge", "bridge_json.inc"));

    Assert(typeof(PaintSettings).GetProperty("AutoMaterial") is null &&
           typeof(PaintSnapshot).GetProperty("AutoMaterial") is null &&
           !index.Contains("auto-material", StringComparison.Ordinal) &&
           !app.Contains("autoMaterial", StringComparison.Ordinal) &&
           !researchRunner.Contains("AutoMaterial", StringComparison.Ordinal) &&
           !researchRunner.Contains("auto_material", StringComparison.Ordinal) &&
           !researchRunner.Contains("--auto-material", StringComparison.Ordinal) &&
           !liveDiagnostics.Contains("auto_material", StringComparison.Ordinal) &&
           !localization.Contains("\"auto.material\"", StringComparison.Ordinal) &&
           !bridgeJson.Contains("\"auto_material\"", StringComparison.Ordinal),
        "Auto Material should be removed from settings, UI, diagnostics, and native metadata");
}

static void SceneLightingSurfaceIsAbsent()
{
    var root = FindRepositoryRoot();
    var index = ReadRepositoryText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = ReadRepositoryText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var localization = ReadRepositoryText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.Core", "Localization", "Strings.json"));
    var liveDiagnostics = ReadRepositoryText(Path.Combine(
        root,
        "src", "csharp", "MecchaCamouflage.LiveDiagnostics", "Program.cs"));

    Assert(typeof(PaintSettings).GetProperty("IncludeShadows") is null &&
           typeof(PaintSnapshot).GetProperty("IncludeShadows") is null &&
           !index.Contains("include-shadows", StringComparison.Ordinal) &&
           !app.Contains("paint.includeShadows", StringComparison.Ordinal) &&
           !localization.Contains("\"include.shadows\"", StringComparison.Ordinal) &&
           !liveDiagnostics.Contains("--include-shadows", StringComparison.Ordinal) &&
           !liveDiagnostics.Contains("include_shadows", StringComparison.Ordinal),
        "Scene Lighting should be removed from settings, snapshots, UI, localization, and diagnostics");
}

static void EspSnaplinesDefaultOn()
{
    var esp = new AppSettings().Esp;
    Assert(esp.Enabled && esp.Snaplines && esp.TargetScope == "all" &&
           esp.HiderColor == new RgbColor(0, 255, 136) &&
           esp.HunterColor == new RgbColor(255, 0, 0),
        "ESP should start enabled for all targets, with green Hiders, red Hunters, and visible snaplines");
}

static void EspRoleColorsPersistAndMigrateLegacySettings()
{
    using var temp = new TempHome();
    var paths = new AppPaths("esp-role-color-persistence-test");
    var store = new SettingsStore(paths);
    var settings = new AppSettings();
    settings.Esp.TargetScope = "hider";
    settings.Esp.HiderColor = new RgbColor(1, 2, 3);
    settings.Esp.HunterColor = new RgbColor(4, 5, 6);

    store.Save(settings);
    var saved = File.ReadAllText(paths.ConfigPath);
    var loaded = store.Load();
    Assert(saved.Contains("\"esp_hider_color\"", StringComparison.Ordinal) &&
           saved.Contains("\"esp_hunter_color\"", StringComparison.Ordinal) &&
           !saved.Contains("\"esp_enemy_color\"", StringComparison.Ordinal) &&
           loaded.Esp.TargetScope == "hider" &&
           loaded.Esp.HiderColor == settings.Esp.HiderColor &&
           loaded.Esp.HunterColor == settings.Esp.HunterColor,
        "ESP settings should persist role-fixed scope and colors");

    File.WriteAllText(paths.ConfigPath,
        """
        {
          "esp_target_scope": "enemy",
          "esp_color": "#112233",
          "esp_enemy_color": "#445566"
        }
        """);
    var migrated = store.Load();
    Assert(migrated.Esp.TargetScope == "all" &&
           migrated.Esp.HiderColor == new RgbColor(0x11, 0x22, 0x33) &&
           migrated.Esp.HunterColor == new RgbColor(0x44, 0x55, 0x66),
        "legacy ally/enemy settings should migrate colors by their historical defaults and reset relative scope to all");
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
    var styles = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));
    Assert(index.Contains("id=\"brush-size\"", StringComparison.Ordinal), "web UI should include the single brush slider");
    Assert(index.Contains("id=\"color-compression-tolerance\"", StringComparison.Ordinal), "web UI should include compression tolerance");
    Assert(index.Contains("min=\"1\" max=\"10\" step=\"0.5\"", StringComparison.Ordinal), "single brush should expose the 1-10 range");
    Assert(index.Contains("id=\"color-compression-tolerance\" class=\"setting-control\" disabled type=\"range\" min=\"0\" max=\"10\" step=\"0.5\"", StringComparison.Ordinal),
        "compression tolerance should expose the 0-10 range in half-step increments");
    Assert(app.Contains("paint.brushSizeTexels", StringComparison.Ordinal), "web UI should bind the single brush");
    Assert(app.Contains("paint.colorCompressionTolerance", StringComparison.Ordinal), "web UI should bind compression tolerance");
    Assert(app.Contains("function initializeRangeScales()", StringComparison.Ordinal) &&
           app.Contains("addRangeScale(byId(\"crop-editor-zoom\"), \"crop-range-scale\");", StringComparison.Ordinal) &&
           styles.Contains(".range-scale-bounds", StringComparison.Ordinal) &&
           styles.Contains("input[type=\"range\"]::-webkit-slider-runnable-track", StringComparison.Ordinal),
        "sliders should expose subdued minimum and maximum values without noisy intermediate ticks");
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

static void WebUiImagePaintEditorUsesSavedTransaction()
{
    var repository = FindRepositoryRoot();
    var index = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));
    var mainForm = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var bridge = ReadRepositoryText(Path.Combine(repository, "src", "native", "bridge", "bridge.cpp"));

    Assert(index.Contains("data-settings-tab=\"paint\" data-i18n=\"settings.paint\">Paint</button>", StringComparison.Ordinal) &&
           index.Contains("data-settings-tab=\"image\" data-i18n=\"settings.image\">Image</button>", StringComparison.Ordinal) &&
           index.Contains("data-settings-tab=\"application\" data-i18n=\"settings.app\">App</button>", StringComparison.Ordinal) &&
           index.Contains("data-settings-panel=\"application\" hidden", StringComparison.Ordinal) &&
           index.Contains("class=\"image-editor-action-grid\"", StringComparison.Ordinal) &&
           !index.Contains("id=\"image-preset-open\"", StringComparison.Ordinal) &&
           index.Contains("id=\"image-preset-load\"", StringComparison.Ordinal) &&
           index.Contains("id=\"image-preset-save\"", StringComparison.Ordinal) &&
           index.Contains("<div class=\"group-title\" data-i18n=\"group.editor\">Editor</div>", StringComparison.Ordinal) &&
           !index.Contains("<div class=\"group-title\">Presets</div>", StringComparison.Ordinal) &&
           !index.Contains("<div class=\"group-title\">Layers</div>", StringComparison.Ordinal) &&
           !index.Contains("image-design-list", StringComparison.Ordinal) &&
           !index.Contains("image-design-name", StringComparison.Ordinal),
        "Image uses the Paint/Image tabs and file presets within one named editor workspace");
    var applicationPanelStart = index.IndexOf("data-settings-panel=\"application\"", StringComparison.Ordinal);
    var applicationPanelEnd = index.IndexOf("</section>", applicationPanelStart, StringComparison.Ordinal);
    Assert(applicationPanelStart >= 0 &&
           applicationPanelEnd > applicationPanelStart &&
           index.IndexOf("id=\"language\"", applicationPanelStart, StringComparison.Ordinal) < applicationPanelEnd &&
           index.IndexOf("id=\"theme-color\"", applicationPanelStart, StringComparison.Ordinal) < applicationPanelEnd &&
           index.IndexOf("id=\"always-on-top\"", applicationPanelStart, StringComparison.Ordinal) < applicationPanelEnd &&
           index.IndexOf("id=\"opacity\"", applicationPanelStart, StringComparison.Ordinal) < applicationPanelEnd &&
           index.IndexOf("class=\"hotkey-list", applicationPanelStart, StringComparison.Ordinal) < applicationPanelEnd &&
           index.Contains("class=\"settings-group app-general-section\"", StringComparison.Ordinal) &&
           index.Contains("class=\"settings-group app-appearance-section\"", StringComparison.Ordinal) &&
           index.Contains("class=\"settings-group app-hotkeys-section\"", StringComparison.Ordinal) &&
           !index.Contains("class=\"sub-panel\"", StringComparison.Ordinal),
        "application controls must use General, Appearance, and Hotkeys groups instead of a separate scrolling sub-panel");
    Assert(index.Contains("id=\"image-file-input\"", StringComparison.Ordinal) &&
           index.Contains("multiple hidden", StringComparison.Ordinal) &&
           index.Contains("id=\"image-upload\"", StringComparison.Ordinal) &&
           index.Contains("id=\"image-layer-list\"", StringComparison.Ordinal) &&
           index.Contains("id=\"crop-editor-dialog\"", StringComparison.Ordinal) &&
           index.Contains("Body type", StringComparison.Ordinal) &&
           index.Contains("id=\"image-fill-section\"", StringComparison.Ordinal) &&
           index.Contains("id=\"image-fill-color-picker\"", StringComparison.Ordinal) &&
           index.Contains("data-image-region=\"frontRegionMode\"", StringComparison.Ordinal) &&
           index.Contains("data-image-region=\"rightRegionMode\"", StringComparison.Ordinal) &&
           index.Contains("data-image-region=\"backRegionMode\"", StringComparison.Ordinal) &&
           index.Contains("data-image-region=\"leftRegionMode\"", StringComparison.Ordinal) &&
           index.IndexOf("data-image-region=\"frontRegionMode\"", StringComparison.Ordinal) <
               index.IndexOf("data-image-region=\"rightRegionMode\"", StringComparison.Ordinal) &&
           index.IndexOf("data-image-region=\"rightRegionMode\"", StringComparison.Ordinal) <
               index.IndexOf("data-image-region=\"backRegionMode\"", StringComparison.Ordinal) &&
           index.IndexOf("data-image-region=\"backRegionMode\"", StringComparison.Ordinal) <
               index.IndexOf("data-image-region=\"leftRegionMode\"", StringComparison.Ordinal) &&
           !index.Contains("image-paint-background", StringComparison.Ordinal) &&
           !index.Contains("image-background-section", StringComparison.Ordinal) &&
           !index.Contains("image-fit-canvas", StringComparison.Ordinal) &&
           !index.Contains("image-crop-layer", StringComparison.Ordinal) &&
           !index.Contains("image-wrap", StringComparison.Ordinal) &&
           !index.Contains("image-mirror", StringComparison.Ordinal) &&
           !index.Contains("image-status", StringComparison.Ordinal),
        "Image keeps Upload global while per-layer controls own Wrap, Mirror, Fit, Crop, and Delete, and owns four Fill/Skip faces while reusing Fill material");
    Assert(!index.Contains("Paint background", StringComparison.Ordinal) &&
           !index.Contains("Background material", StringComparison.Ordinal) &&
           !index.Contains("Transparent pixels", StringComparison.Ordinal) &&
           index.Contains("image-brush-size", StringComparison.Ordinal) &&
           index.Contains("image-color-compression-tolerance", StringComparison.Ordinal) &&
           index.Contains("image-metallic", StringComparison.Ordinal) &&
           index.Contains("image-fill-metallic", StringComparison.Ordinal) &&
           index.IndexOf("class=\"settings-group image-editor-section\"", StringComparison.Ordinal) <
               index.IndexOf("id=\"image-brush-section\"", StringComparison.Ordinal),
        "Image owns its Fill material without a background-material UI, and Editor appears before Brush");
    Assert(app.Contains("function defaultImageCropForLayer(layer)", StringComparison.Ordinal) &&
           app.Contains("const targetAspect = layer.width / layer.height;", StringComparison.Ordinal) &&
           app.Contains("const width = base.width / factor;", StringComparison.Ordinal) &&
           app.Contains("const height = base.height / factor;", StringComparison.Ordinal),
        "Crop must preserve each selected layer's aspect ratio when it opens and when its zoom changes");
    Assert(app.Contains("async function stageImageDesign(design)", StringComparison.Ordinal) &&
           app.Contains("send(\"commitSettingsWithImage\"", StringComparison.Ordinal) &&
           app.Contains("send(\"saveImagePreset\"", StringComparison.Ordinal) &&
           app.Contains("send(\"loadImagePreset\"", StringComparison.Ordinal) &&
           !app.Contains("openImagePresetsFolder", StringComparison.Ordinal) &&
           app.Contains("toast(i18n(\"toast.image.preset.saved\"));", StringComparison.Ordinal) &&
           app.Contains("toast(i18n(\"toast.image.preset.loaded\"));", StringComparison.Ordinal) &&
           !app.Contains("image-paint-background", StringComparison.Ordinal) &&
           app.Contains("getLoadedImagePresetChunk", StringComparison.Ordinal) &&
           app.Contains("getImageAssetChunk", StringComparison.Ordinal) &&
           !app.Contains("commitImageDesign", StringComparison.Ordinal) &&
           !app.Contains("listImageDesigns", StringComparison.Ordinal) &&
           app.Contains("function imageResizeHandleAt", StringComparison.Ordinal) &&
           app.Contains("beginImagePointerInteraction", StringComparison.Ordinal) &&
           app.Contains("wrapAtlasSeam", StringComparison.Ordinal) &&
           app.Contains("image-layer-tools", StringComparison.Ordinal) &&
           app.Contains("function compactImageLayerLabel(layer, index)", StringComparison.Ordinal) &&
           app.Contains("const displayName = stem || i18n(\"image.layer.default\", index + 1);", StringComparison.Ordinal) &&
           !app.Contains("Array.from(stem || i18n(\"image.layer.default\", index + 1)).slice(0, 8)", StringComparison.Ordinal) &&
           app.Contains("openImageCropEditor(index)", StringComparison.Ordinal) &&
           app.Contains("bindImageColorPair(\"image-fill-color-picker\", \"image-fill-color\", \"fillColor\")", StringComparison.Ordinal) &&
           app.Contains("fillColor: imageFillColorPayload(imageEditor.fillColor)", StringComparison.Ordinal) &&
           app.Contains("next.fillColor = normalizeImageFillColor(design?.fillColor)", StringComparison.Ordinal) &&
           app.Contains("const silhouette = document.createElement(\"canvas\")", StringComparison.Ordinal) &&
           !app.Contains("rgba(255,255,255,0.32)", StringComparison.Ordinal) &&
           !app.Contains("drawTransparentPixelChecker", StringComparison.Ordinal) &&
           !app.Contains("Unsaved Image Paint changes", StringComparison.Ordinal) &&
           !app.Contains("Guide unavailable", StringComparison.Ordinal),
        "GUI Save persists all Image settings; guide triangles form one neutral silhouette rather than a checkerboard-like overlay");
    Assert(app.Contains("function renderImageRegionButtons", StringComparison.Ordinal) &&
           app.Contains("for (const mode of [\"fill\", \"skip\"])", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_parse_image_region_mode", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_image_region_mode_for_tile", StringComparison.Ordinal) &&
           bridge.Contains("sample.image_face_tile", StringComparison.Ordinal) &&
           bridge.Contains("append_candidate(MeshFirstRegionMode::Fill);", StringComparison.Ordinal) &&
           bridge.Contains("append_candidate(MeshFirstRegionMode::Paint);", StringComparison.Ordinal),
        "Image must choose Fill or Skip per canonical face and overlay opaque image pixels on the shared Fill pass");
    Assert(mainForm.Contains("case \"commitSettingsWithImage\"", StringComparison.Ordinal) &&
           mainForm.Contains("case \"loadImagePreset\"", StringComparison.Ordinal) &&
           mainForm.Contains("case \"saveImagePreset\"", StringComparison.Ordinal) &&
           !mainForm.Contains("case \"openImagePresetsFolder\"", StringComparison.Ordinal) &&
           mainForm.Contains("OpenFileDialog", StringComparison.Ordinal) &&
           mainForm.Contains("SaveFileDialog", StringComparison.Ordinal) &&
           !mainForm.Contains("case \"commitImageDesign\"", StringComparison.Ordinal) &&
           !mainForm.Contains("case \"listImageDesigns\"", StringComparison.Ordinal),
        "native file dialogs own preset paths and the old library commands are absent");
    Assert(styles.Contains(".image-editor-action-grid", StringComparison.Ordinal) &&
           styles.Contains(".image-editor-action-grid button", StringComparison.Ordinal) &&
           styles.Contains(".region-choice[data-image-region]", StringComparison.Ordinal) &&
           styles.Contains(".image-settings-stack {\n  display: grid;\n  gap: 0;", StringComparison.Ordinal) &&
           styles.Contains(".image-layer-row {\n  display: grid;\n  grid-template-columns: 72px minmax(0, 1fr);", StringComparison.Ordinal) &&
           styles.Contains(".image-layer-tools {\n  display: grid;\n  grid-template-columns: repeat(4, minmax(0, 1fr)) 32px;", StringComparison.Ordinal) &&
           styles.Contains(".image-layer-remove {\n  width: 32px;", StringComparison.Ordinal) &&
           styles.Contains(".image-layer-remove:hover:not(:disabled)", StringComparison.Ordinal) &&
           styles.Contains(".image-layer-item {\n  min-height: 32px;\n  border: 0;", StringComparison.Ordinal) &&
           styles.Contains("text-overflow: ellipsis;", StringComparison.Ordinal) &&
           styles.Contains(".image-layer-action", StringComparison.Ordinal) &&
           styles.Contains("#image-fill-section.disabled", StringComparison.Ordinal) &&
           styles.Contains("touch-action: none", StringComparison.Ordinal) &&
           bridge.Contains("guide_atlas_triangles", StringComparison.Ordinal) &&
           bridge.Contains("guide_runtime_triangles", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_validate_runtime_guide_topology", StringComparison.Ordinal) &&
           bridge.Contains("guide_seam_rejections", StringComparison.Ordinal) &&
           bridge.Contains("GuideCubeEdgeBand", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_order_runtime_triangles_by_profile_uv", StringComparison.Ordinal) &&
           !bridge.Contains("image_guide_runtime_coordinates_untrusted", StringComparison.Ordinal),
        "the compact editor keeps per-layer controls and native guide emission rejects unresolved atlas seams");
}

static void WebUiRequiresExplicitEditWhilePaintRunning()
{
    var repository = FindRepositoryRoot();
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var mainForm = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));

    Assert(app.Contains("function canEditDraft()", StringComparison.Ordinal) &&
           app.Contains("return Boolean(editing && draftSnapshot);", StringComparison.Ordinal) &&
           !app.Contains("return Boolean(editing || liveSnapshot?.runtime?.paintRunning);", StringComparison.Ordinal) &&
           !app.Contains("ensureLiveDraftEdit", StringComparison.Ordinal),
        "Paint progress must not make settings editable before the user presses Edit");
    Assert(app.Contains("const editable = canEditDraft();", StringComparison.Ordinal) &&
           app.Contains("const editable = canEditDraft() && !imageEditor.restoring;", StringComparison.Ordinal) &&
           app.Contains("function canEditImage()", StringComparison.Ordinal) &&
           app.Contains("canEditDraft() && imageEditor", StringComparison.Ordinal),
        "Paint settings and the Image canvas must share the explicit Edit gate");
    Assert(mainForm.Contains("if (settingsEditing && !IsStopHotkey(hotkeyId))", StringComparison.Ordinal) &&
           mainForm.Contains("private static bool IsStopHotkey", StringComparison.Ordinal),
        "Edit mode must never prevent stopping the currently running Paint");
    foreach (var handler in new[]
             {
                 "private object HandleUpdateSetting(JsonElement payload)",
                 "private object HandleUpdateSettings(JsonElement payload)",
                 "private object HandleResetSetting(JsonElement payload)",
                 "private object HandleResetSection(JsonElement payload)",
                 "private object HandleResetAllSettings()"
             })
    {
        var handlerStart = mainForm.IndexOf(handler, StringComparison.Ordinal);
        var handlerEnd = mainForm.IndexOf(
            "\n    private ",
            handlerStart + handler.Length,
            StringComparison.Ordinal);
        Assert(handlerStart >= 0 &&
               handlerEnd > handlerStart &&
               mainForm[handlerStart..handlerEnd].Contains(
                   "if (!settingsEditing)",
                   StringComparison.Ordinal),
            $"{handler} must reject mutations unless Edit is active");
    }
}

static void WebUiPreservesImageActionsDuringPaintSnapshots()
{
    var app = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(), "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));

    Assert(app.Contains("const previousPaintRunning = Boolean(liveSnapshot?.runtime?.paintRunning);", StringComparison.Ordinal) &&
           app.Contains("const paintStillRunning = previousPaintRunning && Boolean(liveSnapshot.runtime?.paintRunning);", StringComparison.Ordinal) &&
           app.Contains("render({ runtimeOnly: editing || paintStillRunning });", StringComparison.Ordinal),
        "periodic snapshots must distinguish a running Paint refresh from a UI-state refresh");
    Assert(app.Contains("function render({ runtimeOnly = false } = {})", StringComparison.Ordinal) &&
           app.Contains("if (runtimeOnly) return;", StringComparison.Ordinal) &&
           app.Contains("list.replaceChildren();", StringComparison.Ordinal),
        "a running Paint snapshot must update progress without replacing Image action buttons between pointer-down and pointer-up");
}

static void WebUiKeepsMeshGuidesVisibleWithImportedImages()
{
    var app = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(), "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));

    Assert(app.Contains("if (imageEditor.guideCanvas) {", StringComparison.Ordinal) &&
           app.Contains("context.drawImage(imageEditor.guideCanvas, 0, 0);", StringComparison.Ordinal) &&
           !app.Contains("const hasImageLayer = imageEditor.layers.some(layer => layer.image);", StringComparison.Ordinal),
        "the fixed mesh-and-skeleton guide must remain visible while an image is being placed");
}

static void WebUiRebuildsImageGuidesWhenLanguageChanges()
{
    var app = File.ReadAllText(Path.Combine(
        FindRepositoryRoot(), "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));

    Assert(app.Contains("let imageGuideCanvasLocale = \"\";", StringComparison.Ordinal) &&
           app.Contains("imageGuideCanvasCache.clear();", StringComparison.Ordinal) &&
           app.Contains("loadImageGuideProfile(imageEditor.bodyType).catch", StringComparison.Ordinal),
        "a language change must rebuild the rasterized canvas guide so its face labels use the selected locale");
}

static void WebUiSeparatesSettingAndLogTabs()
{
    var repository = FindRepositoryRoot();
    var markup = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));

    Assert(markup.Contains("<div class=\"group-title\" data-i18n=\"group.editor\">Editor</div>", StringComparison.Ordinal) &&
           !markup.Contains("<div class=\"group-title\">Images</div>", StringComparison.Ordinal),
        "the Upload, Load preset, and Save preset controls must use the Editor group title");
    Assert(markup.Contains("class=\"settings-group\"", StringComparison.Ordinal) &&
           markup.Contains("class=\"field-label\"", StringComparison.Ordinal) &&
           styles.Contains(".settings-group {\n  position: relative;\n  border: 1px solid var(--hairline);\n  border-top: 0;\n  padding: 20px 12px 16px;", StringComparison.Ordinal) &&
           styles.Contains(".group-title {\n  position: absolute;\n  top: -9px;\n  left: 8px;\n  right: -1px;\n  display: flex;", StringComparison.Ordinal) &&
           styles.Contains(".settings-group::before {\n  position: absolute;\n  top: 0;\n  left: 0;\n  width: 4px;", StringComparison.Ordinal) &&
           styles.Contains(".group-title::after {\n  height: 1px;\n  flex: 1 1 auto;\n  margin-left: 4px;", StringComparison.Ordinal) &&
           styles.Contains(".field-label,", StringComparison.Ordinal),
        "settings must use semantic bordered sections with titles whose top rule reaches the right border and shared field-label typography");
    Assert(styles.Contains(".settings-tab + .settings-tab {\n  border-left: 0;\n}", StringComparison.Ordinal) &&
           styles.Contains(".tab + .tab {\n  border-left: 0;\n}", StringComparison.Ordinal) &&
           styles.Contains(".settings-tabs {\n  display: grid;", StringComparison.Ordinal) &&
           styles.Contains("grid-template-columns: repeat(4, minmax(0, 1fr));", StringComparison.Ordinal),
        "the four settings tabs and log filters must keep equal widths without unnecessary dividers");
    Assert(styles.Contains(".tabs {\n  display: grid;", StringComparison.Ordinal) &&
           styles.Contains("grid-template-columns: repeat(4, minmax(0, 1fr));", StringComparison.Ordinal) &&
           styles.Contains("border-right: 0;", StringComparison.Ordinal) &&
           styles.Contains(".tab {\n  width: 100%;", StringComparison.Ordinal),
        "the four log filters must share the available width equally without an extra terminal divider");
    Assert(styles.Contains(".log-tabs {\n  display: grid;", StringComparison.Ordinal) &&
           styles.Contains("grid-template-columns: minmax(0, 1fr) auto;", StringComparison.Ordinal) &&
           styles.Contains(".log-actions {\n  display: grid;", StringComparison.Ordinal) &&
           styles.Contains("grid-template-columns: repeat(2, minmax(0, 1fr));", StringComparison.Ordinal) &&
           styles.Contains(".log-actions {\n  display: grid;\n  grid-template-columns: repeat(2, minmax(0, 1fr));\n  width: max-content;", StringComparison.Ordinal) &&
           styles.Contains(".manual-paint-grid {\n  grid-template-columns: repeat(4, minmax(0, 1fr));", StringComparison.Ordinal) &&
           styles.Contains(".hotkey-row {\n  display: grid;\n  grid-template-columns: minmax(0, 1fr) 104px;", StringComparison.Ordinal) &&
           styles.Contains(".image-editor-action-grid {\n  display: grid;\n  grid-template-columns: repeat(3, minmax(0, 1fr));", StringComparison.Ordinal) &&
           styles.Contains(".log-actions button {\n  width: auto;\n  min-width: max-content;", StringComparison.Ordinal) &&
           app.Contains("if (element instanceof HTMLButtonElement)", StringComparison.Ordinal) &&
           app.Contains("element.title = localized;", StringComparison.Ordinal),
        "localized controls must retain the two-pane layout with compact single-row actions and full-text tooltips");
}

static void WebUiScopesEditActionsToActiveSettingsTab()
{
    var repository = FindRepositoryRoot();
    var markup = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));
    var catalog = LocalizationCatalog.Load();

    Assert(CountOccurrences(markup, "data-settings-actions=") == 4 &&
           markup.Contains("data-settings-actions=\"paint\"", StringComparison.Ordinal) &&
           markup.Contains("data-settings-actions=\"image\"", StringComparison.Ordinal) &&
           markup.Contains("data-settings-actions=\"misc\"", StringComparison.Ordinal) &&
           markup.Contains("data-settings-actions=\"application\"", StringComparison.Ordinal),
        "each editable settings tab must own its Edit, Reset, Cancel, and Save action bar");
    Assert(app.Contains("if (editing && tab.dataset.settingsTab !== activeSettingsTab)", StringComparison.Ordinal) &&
           app.Contains("tab.disabled = editing && tab.dataset.settingsTab !== activeSettingsTab;", StringComparison.Ordinal),
        "an editing settings tab must be saved or cancelled before another tab can be selected");
    Assert(app.Contains("if (activeSettingsTab === \"paint\")", StringComparison.Ordinal) &&
           app.Contains("draftSnapshot.settings.paint = clone(liveSnapshot.defaults.paint);", StringComparison.Ordinal) &&
           app.Contains("if (activeSettingsTab === \"image\")", StringComparison.Ordinal) &&
           app.Contains("if (activeSettingsTab === \"misc\")", StringComparison.Ordinal) &&
           app.Contains("draftSnapshot.settings.esp = clone(liveSnapshot.defaults.esp);", StringComparison.Ordinal) &&
           app.Contains("draftSnapshot.settings.app = clone(liveSnapshot.defaults.app);", StringComparison.Ordinal),
        "Reset must restore only the settings belonging to the active tab");
    Assert(styles.Contains(".action-bar[hidden] {\n  display: none;\n}", StringComparison.Ordinal),
        "inactive settings action bars must not remain visible");
    Assert(markup.Contains("class=\"danger-action\" type=\"button\" disabled data-settings-action=\"reset\"", StringComparison.Ordinal) &&
           markup.IndexOf("data-settings-action=\"reset\"", StringComparison.Ordinal) <
               markup.IndexOf("data-settings-action=\"edit\"", StringComparison.Ordinal) &&
           styles.Contains(".action-bar {\n  display: grid;\n  z-index: 3;\n  flex: 0 0 auto;\n  grid-template-columns: repeat(4, minmax(0, 1fr));", StringComparison.Ordinal) &&
           styles.Contains(".danger-action", StringComparison.Ordinal) &&
           app.Contains("toast(i18n(\"toast.settings.reset\", i18n(settingsTabTitleKey())));", StringComparison.Ordinal),
        "Reset must remain the leftmost equal-width danger action and confirm the active tab reset with a toast");
    var resetStart = app.IndexOf("async function resetDraft()", StringComparison.Ordinal);
    var resetEnd = app.IndexOf("async function saveDraft()", resetStart, StringComparison.Ordinal);
    var reset = app[resetStart..resetEnd];
    Assert(!reset.Contains("updateSettings", StringComparison.Ordinal) &&
           !reset.Contains("commitSettingsWithImage", StringComparison.Ordinal) &&
           string.Equals(catalog.Text("en", "toast.settings.reset"), "{0} reset in draft. Save to apply.", StringComparison.Ordinal),
        "Reset must remain a draft-only operation and say that Save is required to apply it");
    var cancelStart = app.IndexOf("function cancelEdit()", StringComparison.Ordinal);
    var cancelEnd = app.IndexOf("async function resetDraft()", cancelStart, StringComparison.Ordinal);
    var cancel = app[cancelStart..cancelEnd];
    Assert(cancel.Contains("const imageEditorBeforeEdit = imageEditorAtEditStart;", StringComparison.Ordinal) &&
           cancel.Contains("imageEditor = imageEditorBeforeEdit;", StringComparison.Ordinal) &&
           !cancel.Contains("updateSettings", StringComparison.Ordinal) &&
           !cancel.Contains("commitSettingsWithImage", StringComparison.Ordinal),
        "Cancel must immediately restore the pre-edit Image state without persisting the reset draft");
}

static void WebUiOffersManualPaintControls()
{
    var repository = FindRepositoryRoot();
    var markup = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));
    var controlsIndex = markup.IndexOf("id=\"manual-paint-controls\"", StringComparison.Ordinal);
    var logsIndex = markup.IndexOf("class=\"log-panel\"", StringComparison.Ordinal);
    var commands = new[]
    {
        "paint", "preview", "unpreview", "stop",
        "imagePaint", "imagePreview", "imageUnpreview", "imageStop"
    };

    Assert(controlsIndex > logsIndex &&
           !markup[logsIndex..controlsIndex].Contains("</section>", StringComparison.Ordinal),
        "manual Paint controls must live inside the Logs panel");
    foreach (var command in commands)
    {
        Assert(markup.Contains($"data-manual-paint-command=\"{command}\"", StringComparison.Ordinal),
            $"manual Paint controls must expose the {command} command");
    }
    Assert(CountOccurrences(markup, "data-manual-paint-command=") == commands.Length,
        "manual Paint controls must expose exactly the eight existing Paint commands");
    Assert(markup.IndexOf("data-manual-paint-command=\"paint\"", StringComparison.Ordinal) <
           markup.IndexOf("data-manual-paint-command=\"stop\"", StringComparison.Ordinal) &&
           markup.IndexOf("data-manual-paint-command=\"stop\"", StringComparison.Ordinal) <
           markup.IndexOf("data-manual-paint-command=\"preview\"", StringComparison.Ordinal) &&
           markup.IndexOf("data-manual-paint-command=\"preview\"", StringComparison.Ordinal) <
           markup.IndexOf("data-manual-paint-command=\"unpreview\"", StringComparison.Ordinal) &&
           markup.IndexOf("data-manual-paint-command=\"imagePaint\"", StringComparison.Ordinal) <
           markup.IndexOf("data-manual-paint-command=\"imageStop\"", StringComparison.Ordinal) &&
           markup.IndexOf("data-manual-paint-command=\"imageStop\"", StringComparison.Ordinal) <
           markup.IndexOf("data-manual-paint-command=\"imagePreview\"", StringComparison.Ordinal) &&
           markup.IndexOf("data-manual-paint-command=\"imagePreview\"", StringComparison.Ordinal) <
           markup.IndexOf("data-manual-paint-command=\"imageUnpreview\"", StringComparison.Ordinal),
        "manual Paint controls must order actions as Start, Stop, Preview, then Unpreview");
    Assert(app.Contains("function renderManualPaintControls()", StringComparison.Ordinal) &&
           app.Contains("function runManualPaintCommand(command)", StringComparison.Ordinal) &&
           app.Contains("button.dataset.manualPaintCommand", StringComparison.Ordinal) &&
           app.Contains("runtime?.activePaintKind", StringComparison.Ordinal) &&
           app.Contains("runtime?.activePreviewKind", StringComparison.Ordinal),
        "manual Paint controls must distinguish the running and previewed Paint kind before enabling actions");
    Assert(styles.Contains(".manual-paint-controls", StringComparison.Ordinal) &&
           styles.Contains("border-top: 1px solid var(--hairline);", StringComparison.Ordinal) &&
           CountOccurrences(markup, "class=\"action-bar manual-paint-grid\"") == 2 &&
           styles.Contains(".manual-paint-group + .manual-paint-group", StringComparison.Ordinal),
        "manual Paint controls must reuse the same four-button action bar as the settings tabs");
}

static void RuntimeSnapshotReportsManualPaintState()
{
    using var temp = new TempHome();
    var session = new HostSession("manual-paint-state-test-" + Guid.NewGuid().ToString("N"));
    var flags = System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic;
    var activePaintField = typeof(HostSession).GetField("activePaintKind", flags)
        ?? throw new InvalidOperationException("activePaintKind field missing");
    var activePreviewField = typeof(HostSession).GetField("activePreviewKind", flags)
        ?? throw new InvalidOperationException("activePreviewKind field missing");
    var createSnapshot = typeof(HostSession).GetMethod("CreateSnapshot", flags)
        ?? throw new InvalidOperationException("CreateSnapshot method missing");
    activePaintField.SetValue(session, PaintKind.Image);
    activePreviewField.SetValue(session, PaintKind.Standard);

    var snapshot = (UiSnapshot)(createSnapshot.Invoke(session, new object?[] { "waiting", "waiting", "stopped", null })
        ?? throw new InvalidOperationException("CreateSnapshot returned null"));

    Assert(snapshot.Runtime.ActivePaintKind == "image" && snapshot.Runtime.ActivePreviewKind == "standard",
        "the runtime snapshot must identify which manual Stop and Unpreview actions are valid");
}

static void PaintFeedbackUsesOneSeverityPath()
{
    var repository = FindRepositoryRoot();
    var app = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var mainForm = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));

    Assert(!app.Contains("toast(result?.message || i18n(\"error.operation.failed\"), \"error\");", StringComparison.Ordinal),
        "manual Paint commands must not turn every rejected result into a red toast in the browser");
    Assert(mainForm.Contains("ApplyPaintResult(", StringComparison.Ordinal) &&
           mainForm.Contains("NotifyPaintResult(result);", StringComparison.Ordinal) &&
           mainForm.Contains("private void NotifyPaintResult", StringComparison.Ordinal),
        "manual Paint commands and hotkeys must share one host-side result notification path");
}

static void WebUiReportsWebViewZoomFactorInFooter()
{
    var repository = FindRepositoryRoot();
    var markup = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var mainForm = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));

    Assert(markup.Contains("id=\"footer-zoom\">100%</span>", StringComparison.Ordinal) &&
           !markup.Contains("blob/main/LICENSE.txt", StringComparison.Ordinal) &&
           markup.Contains("https://github.com/acentrist/MecchaCamouflage", StringComparison.Ordinal),
        "the footer must replace the redundant License link with the current WebView zoom percentage");
    Assert(app.Contains("message.name === \"zoomChanged\"", StringComparison.Ordinal) &&
           app.Contains("renderFooterZoom(message.data?.percent);", StringComparison.Ordinal),
        "the page must render zoom updates received from the host");
    Assert(mainForm.Contains("ZoomFactorChanged +=", StringComparison.Ordinal) &&
           mainForm.Contains("PostEvent(\"zoomChanged\", new { percent", StringComparison.Ordinal),
        "the host must send its actual WebView2 ZoomFactor to the footer");
}

static void WebUiLocalizesEverySettingsTab()
{
    var repository = FindRepositoryRoot();
    var markup = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var catalog = LocalizationCatalog.Load();

    Assert(markup.Contains("data-i18n=\"settings.paint\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"settings.image\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"settings.misc\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"settings.app\"", StringComparison.Ordinal) &&
           app.Contains("document.documentElement.lang = locale;", StringComparison.Ordinal),
        "the Paint, Image, Misc, and Application tabs must update with the selected UI language");
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        foreach (var key in new[] { "settings.paint", "settings.image", "settings.misc", "settings.app" })
        {
            Assert(!string.Equals(catalog.Text(locale.Code, key), key, StringComparison.Ordinal),
                $"{locale.Code} must translate {key}");
        }
    }
}

static void WebUiLocalizesEveryEspControl()
{
    var repository = FindRepositoryRoot();
    var markup = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var catalog = LocalizationCatalog.Load();
    var keys = new[]
    {
        "group.esp", "esp.enabled", "esp.boxes", "esp.skeletons",
        "esp.names", "esp.distance", "esp.snaplines", "esp.targets",
        "esp.hider.color", "esp.hunter.color", "esp.scope.all",
        "esp.scope.hider", "esp.scope.hunter"
    };

    foreach (var key in keys.Take(10))
    {
        Assert(markup.Contains($"data-i18n=\"{key}\"", StringComparison.Ordinal),
            $"ESP markup must localize {key}");
    }
    Assert(app.Contains("i18n(`esp.scope.${scope}`)", StringComparison.Ordinal),
        "dynamic ESP target buttons must use localized scope labels");
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        foreach (var key in keys)
        {
            Assert(!string.Equals(catalog.Text(locale.Code, key), key, StringComparison.Ordinal),
                $"{locale.Code} must translate {key}");
        }
    }
}

static void WebUiLocalizesImageEditorControlsAndCropDialog()
{
    var repository = FindRepositoryRoot();
    var markup = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var catalog = LocalizationCatalog.Load();
    var keys = new[]
    {
        "group.editor", "group.brush", "group.general", "group.appearance", "button.upload", "button.load.preset", "button.save.preset",
        "image.body.type", "body.round", "body.cube", "region.right", "region.left",
        "group.hotkeys", "hotkey.image.paint", "hotkey.image.preview", "hotkey.image.unpreview", "hotkey.image.stop",
        "image.action.wrap", "image.action.mirror", "image.action.fit", "image.action.crop", "image.action.remove",
        "image.action.wrap.title", "image.action.mirror.title", "image.action.fit.title", "image.action.crop.title",
        "image.layer.default", "toast.image.preset.saved", "toast.image.preset.loaded",
        "dialog.crop.title", "dialog.crop.help", "dialog.crop.zoom", "dialog.crop.reset", "dialog.crop.apply",
        "dialog.crop.image.alt", "aria.settings.sections", "aria.image.body.type", "aria.image.canvas"
    };

    Assert(markup.Contains("data-i18n=\"group.editor\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"group.brush\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"group.general\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"group.appearance\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"button.upload\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"group.hotkeys\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n=\"dialog.crop.title\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n-title=\"aria.image.body.type\"", StringComparison.Ordinal) &&
           markup.Contains("data-i18n-alt=\"dialog.crop.image.alt\"", StringComparison.Ordinal) &&
           app.Contains("action(i18n(\"image.action.wrap\"), i18n(\"image.action.wrap.title\")", StringComparison.Ordinal) &&
           app.Contains("toast(i18n(\"toast.image.preset.saved\"));", StringComparison.Ordinal) &&
           app.Contains("toast(i18n(\"toast.image.preset.loaded\"));", StringComparison.Ordinal),
        "all image editor controls, crop dialog text, and dynamic layer actions must use localization keys");
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        foreach (var key in keys)
        {
            Assert(!string.Equals(catalog.Text(locale.Code, key), key, StringComparison.Ordinal),
                $"{locale.Code} must translate {key}");
        }
    }
}

static void WebUiLocalizesOperationErrors()
{
    var repository = FindRepositoryRoot();
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var catalog = LocalizationCatalog.Load();

    Assert(app.Contains("toast(i18n(\"error.operation.failed\"), \"error\");", StringComparison.Ordinal),
        "operation failures must show a localized user-facing message instead of a raw native error");
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        Assert(!string.Equals(catalog.Text(locale.Code, "error.operation.failed"), "error.operation.failed", StringComparison.Ordinal),
            $"{locale.Code} must translate the generic operation failure message");
    }
}

static void NativeStartupAndWebViewRecoveryDialogsAreLocalized()
{
    var repository = FindRepositoryRoot();
    var program = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "Program.cs"));
    var mainForm = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var dialog = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "WebViewFailureDialog.cs"));
    var catalog = LocalizationCatalog.Load();
    var keys = new[]
    {
        "dialog.startup.failed", "dialog.webview.runtime.required", "dialog.webview.failed.navigation",
        "dialog.webview.failed.initialize", "dialog.webview.failed.ready.timeout", "dialog.webview.failed.retry",
        "dialog.webview.failed.browser.process", "dialog.webview.failed.starting", "dialog.webview.details",
        "dialog.webview.download", "button.close", "button.retry.once"
    };

    Assert(program.Contains("startupCatalog.Text(startupLocale, \"dialog.startup.failed\")", StringComparison.Ordinal) &&
           mainForm.Contains("UiText(\"dialog.webview.runtime.required\")", StringComparison.Ordinal) &&
           mainForm.Contains("\"dialog.webview.failed.navigation\"", StringComparison.Ordinal) &&
           dialog.Contains("LocalizationCatalog localization", StringComparison.Ordinal) &&
           dialog.Contains("localization.Text(locale, \"dialog.webview.details\")", StringComparison.Ordinal) &&
           dialog.Contains("localization.Text(locale, \"button.retry.once\")", StringComparison.Ordinal),
        "startup, WebView runtime, and recovery dialogs must resolve their visible text from the selected localization catalog");
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        foreach (var key in keys)
        {
            Assert(!string.Equals(catalog.Text(locale.Code, key), key, StringComparison.Ordinal),
                $"{locale.Code} must translate {key}");
        }
    }
}

static void NativePresetFileDialogsAreLocalized()
{
    var repository = FindRepositoryRoot();
    var mainForm = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var catalog = LocalizationCatalog.Load();
    var keys = new[] { "dialog.preset.load.title", "dialog.preset.save.title", "dialog.preset.filter" };

    Assert(mainForm.Contains("Title = UiText(\"dialog.preset.load.title\")", StringComparison.Ordinal) &&
           mainForm.Contains("Title = UiText(\"dialog.preset.save.title\")", StringComparison.Ordinal) &&
           mainForm.Contains("Filter = UiText(\"dialog.preset.filter\")", StringComparison.Ordinal),
        "native preset dialogs must obtain titles and filters from the selected localization catalog");
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        foreach (var key in keys)
        {
            Assert(!string.Equals(catalog.Text(locale.Code, key), key, StringComparison.Ordinal),
                $"{locale.Code} must translate {key}");
        }
    }
}

static void EveryDeclaredWebLocalizationKeyResolvesInEveryLocale()
{
    var repository = FindRepositoryRoot();
    var markup = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = File.ReadAllText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var catalog = LocalizationCatalog.Load();
    var keys = new HashSet<string>(StringComparer.Ordinal);

    foreach (System.Text.RegularExpressions.Match match in System.Text.RegularExpressions.Regex.Matches(
                 markup, "data-i18n(?:-[^=]+)?=\\\"(?<key>[^\\\"]+)\\\""))
    {
        keys.Add(match.Groups["key"].Value);
    }
    foreach (System.Text.RegularExpressions.Match match in System.Text.RegularExpressions.Regex.Matches(
                 app, "i18n\\(\\\"(?<key>[^\\\"]+)\\\""))
    {
        keys.Add(match.Groups["key"].Value);
    }
    keys.UnionWith(["mode.paint", "mode.fill", "mode.skip", "state.attached", "state.waiting", "state.connected", "state.ready", "state.running", "state.stopped", "state.failed"]);

    Assert(keys.Count > 0, "the Web UI must declare localization keys");
    foreach (var locale in LocalizationCatalog.SupportedLocales)
    {
        foreach (var key in keys)
        {
            Assert(!string.Equals(catalog.Text(locale.Code, key), key, StringComparison.Ordinal),
                $"{locale.Code} must resolve web localization key {key}");
        }
    }
}

static void WebUiUsesPackagedReferenceGuides()
{
    var repository = FindRepositoryRoot();
    var app = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var mainForm = ReadRepositoryText(Path.Combine(repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var bridge = ReadRepositoryText(Path.Combine(repository, "src", "native", "bridge", "bridge.cpp"));
    var contract = ReadRepositoryText(Path.Combine(repository, "src", "native", "include", "runtime_contract.hpp"));
    var refreshScript = ReadRepositoryText(Path.Combine(repository, "scripts", "refresh-image-reference-profile.ps1"));
    var migrationScript = ReadRepositoryText(Path.Combine(repository, "scripts", "migrate-image-reference-profiles.ps1"));
    using var roundRawProfile = JsonDocument.Parse(File.ReadAllText(Path.Combine(
        repository, "resources", "mesh-profiles", "paintman.mesh-profile-v2.json")));
    using var roundImageProfile = JsonDocument.Parse(File.ReadAllText(Path.Combine(
        repository, "resources", "mesh-profiles", "paintman.image-profile-v2.json")));

    Assert(app.Contains("const IMAGE_GUIDE_PROFILE_FILES", StringComparison.Ordinal) &&
           app.Contains("fetch(profilePath", StringComparison.Ordinal) &&
           app.Contains("paintman.image-profile-v2.json", StringComparison.Ordinal) &&
           app.Contains("paintman_cube.image-profile-v2.json", StringComparison.Ordinal) &&
           app.Contains("buildCubeCanonicalImageGuideCanvas", StringComparison.Ordinal) &&
           app.Contains("cubeCanonicalNaturalStandPositions", StringComparison.Ordinal) &&
           app.Contains("drawCubeCanonicalSkeleton", StringComparison.Ordinal) &&
           app.Contains("roundCanonicalNaturalStandPositions", StringComparison.Ordinal) &&
           app.Contains("drawRoundCanonicalSkeleton", StringComparison.Ordinal) &&
           !app.Contains("send(\"getImageGuide\"", StringComparison.Ordinal) &&
           !app.Contains("Live pose guide", StringComparison.Ordinal),
        "the Image editor must render both fixed guides from packaged profiles, with their canonical natural-standing poses and skeletons independent of bridge or game state");
    Assert(mainForm.Contains("mesh-profiles", StringComparison.Ordinal),
        "the Web host must continue serving the packaged mesh profiles to the Image editor");
    Assert(!roundRawProfile.RootElement.TryGetProperty("ImageReferencePose", out _) &&
           roundRawProfile.RootElement.GetProperty("VertexCount").GetInt32() == 1668 &&
           roundRawProfile.RootElement.GetProperty("ProfileHash").GetString() ==
               "cd469e35ad0cbd1e483bd82b2406849429d24037807bd7a294534fb79633f55b" &&
           roundImageProfile.RootElement.TryGetProperty("ProfileRole", out var roundProfileRole) &&
           roundProfileRole.GetString() == "image_reference" &&
           roundImageProfile.RootElement.TryGetProperty("BaseProfileId", out var roundBaseProfileId) &&
           roundBaseProfileId.GetString() == roundRawProfile.RootElement.GetProperty("ProfileId").GetString() &&
           roundImageProfile.RootElement.TryGetProperty("BaseProfileHash", out var roundBaseProfileHash) &&
           roundBaseProfileHash.GetString() == roundRawProfile.RootElement.GetProperty("ProfileHash").GetString() &&
           roundImageProfile.RootElement.TryGetProperty("ImageReferencePose", out var roundReferencePose) &&
           roundReferencePose.TryGetProperty("ComponentTransforms", out var roundTransforms) &&
           roundReferencePose.TryGetProperty("Vertices", out var roundVertices) &&
           roundTransforms.ValueKind == JsonValueKind.Array &&
           roundVertices.ValueKind == JsonValueKind.Array &&
           roundTransforms.GetArrayLength() == 28 &&
           roundVertices.GetArrayLength() == 1668 &&
           refreshScript.Contains("ExpectedVertices = 1668", StringComparison.Ordinal),
        "the raw round dump must match game 3.3.0 while its derived Image profile ships one complete captured natural-standing reference pose for the same topology");
    Assert(app.Contains("for (const face of [\"front\", \"right\", \"back\", \"left\"])", StringComparison.Ordinal) &&
           !app.Contains("const region = referenceGuideRegion(normal, depthIsY);", StringComparison.Ordinal) &&
           app.Contains("projection: {\n      depthIsY,", StringComparison.Ordinal),
        "the round guide must orthographically project the full fixed mesh into every view with its recorded depth axis, rather than splitting fragments by surface normal or swapping the front and side views");
    Assert(bridge.Contains("mesh_first_build_cube_canonical_image_atlas", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_map_cube_canonical_sample", StringComparison.Ordinal) &&
           bridge.Contains("canonical_natural_stand_v1", StringComparison.Ordinal) &&
           contract.Contains("map_cube_canonical_image_coordinate", StringComparison.Ordinal),
        "Cube image paint must sample the same canonical natural-standing projection as its editor guide");
    Assert(bridge.Contains("mesh_first_build_round_canonical_image_atlas", StringComparison.Ordinal) &&
           bridge.Contains("mesh_first_map_round_canonical_sample", StringComparison.Ordinal) &&
           bridge.Contains("image_paint_round_atlas", StringComparison.Ordinal),
        "Round image paint must sample the same fixed natural-standing reference as its editor guide");
    Assert(app.Contains("cubeReferenceComponentTransforms", StringComparison.Ordinal) &&
           app.Contains("cubeReferenceVertices", StringComparison.Ordinal) &&
           app.Contains("roundReferenceComponentTransforms", StringComparison.Ordinal) &&
           app.Contains("roundReferenceVertices", StringComparison.Ordinal) &&
           app.Contains("ImageReferencePose", StringComparison.Ordinal) &&
           bridge.Contains("image_reference_component_transforms", StringComparison.Ordinal) &&
           bridge.Contains("image_reference_vertices", StringComparison.Ordinal) &&
           bridge.Contains("capture_reference_pose", StringComparison.Ordinal) &&
           !app.Contains("CUBE_CANONICAL_ARM_LOWERING_DEGREES", StringComparison.Ordinal) &&
           !bridge.Contains("CubeCanonicalArmLoweringDegrees", StringComparison.Ordinal),
        "the editor and native mappers must consume fixed captured profile poses, never arbitrary arm angles");
    Assert(refreshScript.Contains("CaptureNeutralPose", StringComparison.Ordinal) &&
           refreshScript.Contains("scripts\\mesh.ps1", StringComparison.Ordinal) &&
           refreshScript.Contains("--capture-$BodyType-reference-pose", StringComparison.Ordinal) &&
           refreshScript.Contains("ImageProfileFile", StringComparison.Ordinal) &&
           refreshScript.Contains("ProfileRole", StringComparison.Ordinal) &&
           refreshScript.Contains("ImageReferencePose", StringComparison.Ordinal) &&
           refreshScript.Contains("Restored the previous derived Image profile", StringComparison.Ordinal),
        "the update workflow must keep raw dumps immutable while safely restoring a derived Image profile on failure and baking one explicitly confirmed neutral-pose capture");
    Assert(refreshScript.Contains("$meshArguments = @{", StringComparison.Ordinal) &&
           !refreshScript.Contains("$meshArguments = @(\n", StringComparison.Ordinal) &&
           refreshScript.Contains("[System.IO.File]::ReadAllBytes($imageProfilePath)", StringComparison.Ordinal) &&
           refreshScript.Contains("[System.IO.File]::WriteAllBytes($imageProfilePath, $previousImageProfile)", StringComparison.Ordinal),
        "the profile refresh must pass mesh options by name and restore the exact derived Image bytes when a refresh fails");
    Assert(migrationScript.Contains("$legacyProfile.PSObject.Properties.Remove(\"ImageReferencePose\")", StringComparison.Ordinal) &&
           migrationScript.Contains("ProfileRole", StringComparison.Ordinal) &&
           migrationScript.Contains("paintman_cube.image-profile-v2.json", StringComparison.Ordinal),
        "one-time migration must split legacy embedded reference poses into raw dump and derived Image profile files without manual JSON editing");
}

static void WebUiExposesFukuyokaDirectlyAfterCube()
{
    var repository = FindRepositoryRoot();
    var index = ReadRepositoryText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "index.html"));
    var app = ReadRepositoryText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "app.js"));
    var styles = ReadRepositoryText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.WebHost", "web", "styles.css"));
    var cube = index.IndexOf("id=\"image-guide-cube\"", StringComparison.Ordinal);
    var fukuyoka = index.IndexOf("id=\"image-guide-fukuyoka\"", StringComparison.Ordinal);

    Assert(cube >= 0 && fukuyoka > cube,
        "the Fukuyoka Image body tile must appear directly after Cube");
    Assert(app.Contains("fukuyoka: \"mesh-profiles/paintman_hukuyoka.image-profile-v2.json\"", StringComparison.Ordinal) &&
           app.Contains("setImageBodyType(\"fukuyoka\")", StringComparison.Ordinal),
        "the Fukuyoka tile must load its packaged image profile and preserve its body identity");
    Assert(styles.Contains(".image-choice {\n  grid-template-columns: repeat(3, minmax(0, 1fr));", StringComparison.Ordinal),
        "Round, Cube, and Fukuyoka must remain on one row in that order");
}

static void HostAndCaptureToolPreserveFukuyoka()
{
    var repository = FindRepositoryRoot();
    var hostSession = ReadRepositoryText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.Controller", "HostSession.cs"));
    var program = ReadRepositoryText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.WebHost", "Program.cs"));
    var mainForm = ReadRepositoryText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.WebHost", "MainForm.cs"));
    var imageLibrary = ReadRepositoryText(Path.Combine(
        repository, "src", "csharp", "MecchaCamouflage.Core", "ImageDesignLibrary.cs"));

    Assert(hostSession.Contains("ImagePaintSettings.NormalizeBodyType", StringComparison.Ordinal),
        "guide and reference capture requests must preserve the fukuyoka body identity");
    Assert(program.Contains("--capture-fukuyoka-reference-pose", StringComparison.Ordinal),
        "the development capture tool must support the fukuyoka neutral-pose command");
    Assert(mainForm.Contains("paintman_hukuyoka.image-profile-v2.json", StringComparison.Ordinal),
        "the Web host must serve the packaged Fukuyoka image profile");
    Assert(mainForm.Contains("ImagePaintSettings.NormalizeBodyType", StringComparison.Ordinal) &&
           hostSession.Contains("ImagePaintSettings.NormalizeBodyType", StringComparison.Ordinal) &&
           imageLibrary.Contains("ImagePaintSettings.IsSupportedBodyType", StringComparison.Ordinal) &&
           !mainForm.Contains("private static string NormalizeImageBodyType", StringComparison.Ordinal) &&
           !hostSession.Contains("private static string NormalizeImageBodyType", StringComparison.Ordinal),
        "guide requests, artifacts, and saved design manifests must share one body-type contract");
}

static void ProfileRefreshSupportsFukuyoka()
{
    var repository = FindRepositoryRoot();
    var refresh = ReadRepositoryText(Path.Combine(
        repository, "scripts", "refresh-image-reference-profile.ps1"));
    using var profile = JsonDocument.Parse(File.ReadAllText(Path.Combine(
        repository, "resources", "mesh-profiles", "paintman_hukuyoka.mesh-profile-v2.json")));
    var root = profile.RootElement;
    using var imageProfile = JsonDocument.Parse(File.ReadAllText(Path.Combine(
        repository, "resources", "mesh-profiles", "paintman_hukuyoka.image-profile-v2.json")));
    var imageRoot = imageProfile.RootElement;

    Assert(refresh.Contains("[ValidateSet(\"round\", \"cube\", \"fukuyoka\")]", StringComparison.Ordinal) &&
           refresh.Contains("paintman_hukuyoka.uasset", StringComparison.Ordinal) &&
           refresh.Contains("paintman_hukuyoka.mesh-profile-v2.json", StringComparison.Ordinal) &&
           refresh.Contains("paintman_hukuyoka.image-profile-v2.json", StringComparison.Ordinal) &&
           refresh.Contains("ExpectedVertices = 1508", StringComparison.Ordinal) &&
           refresh.Contains("ExpectedIndices = 7584", StringComparison.Ordinal) &&
           refresh.Contains("RedirectStandardOutput = $true", StringComparison.Ordinal) &&
           refresh.Contains("RedirectStandardError = $true", StringComparison.Ordinal) &&
           refresh.Contains("$process.WaitForExit()", StringComparison.Ordinal) &&
           refresh.Contains("$process.ExitCode", StringComparison.Ordinal),
        "the documented profile refresh command must preserve the measured Fukuyoka topology");
    Assert(root.GetProperty("ProfileSchemaVersion").GetInt32() == 2 &&
           root.GetProperty("SourcePath").GetString() ==
               "Chameleon/Content/3Dmodel/cLeon/charactor/paintman/skeltal/paintman_hukuyoka.uasset" &&
           root.GetProperty("Export").GetString() == "paintman_hukuyoka" &&
           root.GetProperty("VertexCount").GetInt32() == 1508 &&
           root.GetProperty("IndexCount").GetInt32() == 7584 &&
           root.GetProperty("TriangleCount").GetInt32() == 2528 &&
           root.GetProperty("Bones").GetArrayLength() == 28 &&
           root.GetProperty("Lod0").GetProperty("Vertices").GetArrayLength() == 1508 &&
           root.GetProperty("Lod0").GetProperty("Indices").GetArrayLength() == 7584 &&
           root.GetProperty("Triangles").GetArrayLength() == 2528 &&
           !root.TryGetProperty("ImageReferencePose", out _),
        "the packaged Fukuyoka raw profile must match the current 3.3.1 asset and remain free of captured editor pose data");
    Assert(imageRoot.GetProperty("ProfileRole").GetString() == "image_reference" &&
           imageRoot.GetProperty("BaseProfileId").GetString() == root.GetProperty("ProfileId").GetString() &&
           imageRoot.GetProperty("BaseProfileHash").GetString() == root.GetProperty("ProfileHash").GetString() &&
           imageRoot.GetProperty("ImageReferencePose").GetProperty("ComponentTransforms").GetArrayLength() == 28 &&
           imageRoot.GetProperty("ImageReferencePose").GetProperty("Vertices").GetArrayLength() == 1508,
        "the packaged Fukuyoka Image profile must derive from the current raw profile and contain one complete neutral-pose capture");
}

static void NativeMapsHukuyokaAssetIdentityToFukuyoka()
{
    var repository = FindRepositoryRoot();
    var bridge = ReadRepositoryText(Path.Combine(
        repository, "src", "native", "bridge", "bridge.cpp"));
    using var profile = JsonDocument.Parse(File.ReadAllText(Path.Combine(
        repository, "resources", "mesh-profiles", "paintman_hukuyoka.mesh-profile-v2.json")));
    var identity = string.Join(
        "\n",
        profile.RootElement.GetProperty("ProfileId").GetString(),
        profile.RootElement.GetProperty("SourcePath").GetString(),
        profile.RootElement.GetProperty("Export").GetString());

    Assert(identity.Contains("hukuyoka", StringComparison.OrdinalIgnoreCase) &&
           !identity.Contains("fukuyoka", StringComparison.OrdinalIgnoreCase),
        "the current game asset must retain its authoritative hukuyoka spelling");
    Assert(bridge.Contains("identity.find(\"hukuyoka\")", StringComparison.Ordinal),
        "native body classification must map the game's hukuyoka asset spelling to the public fukuyoka body type");
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

    var imageDuplicate = new HotkeySet("F1", "F2", "F3", "F4", "F1", "F6", "F7", "F8");
    Assert(!imageDuplicate.TryValidate(out _), "image hotkeys should not collide with the standard F1-F4 commands");
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

static void HostSessionResetsBrushSectionWithCurrentVocabulary()
{
    using var temp = new TempHome();
    var session = new HostSession("host-brush-section-reset-test");

    Assert(session.UpdateSetting("paint.brushSizeTexels", JsonSerializer.SerializeToElement(7.5)).Success,
        "brush size should update before the section reset");
    Assert(session.UpdateSetting("paint.colorCompressionTolerance", JsonSerializer.SerializeToElement(2.0)).Success,
        "compression tolerance should update before the section reset");
    Assert(session.UpdateSetting("paint.metallic", JsonSerializer.SerializeToElement(0.65)).Success,
        "material should update before the section reset");

    var reset = session.ResetSection("paint.brush");
    var defaults = new AppSettings().Paint;
    Assert(reset.Success, reset.Message);
    Assert(Math.Abs(session.Settings.Paint.BrushSizeTexels - defaults.BrushSizeTexels) < 0.000001 &&
           Math.Abs(session.Settings.Paint.ColorCompressionTolerance - defaults.ColorCompressionTolerance) < 0.000001,
        "paint.brush should reset only brush size and compression tolerance");
    Assert(Math.Abs(session.Settings.Paint.Metallic - 0.65) < 0.000001,
        "paint.brush must not reset material settings");
    Assert(!session.ResetSection("paint.geometry").Success && !session.ResetSection("geometry").Success,
        "removed geometry section names must not remain as aliases");
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
        "{\"metadata\":{\"diagnostic_strokes_before_limit\":10,\"diagnostic_strokes_after_limit\":1,\"local_stroke_calls\":1,\"local_stroke_success\":1,\"local_dispatch_max_ms\":16.42,\"local_dispatch_delay_ms\":53,\"local_dispatch_throttle_events\":1,\"first_stroke_target_channel\":7,\"first_stroke_metallic\":1,\"first_stroke_roughness\":0,\"first_stroke_emissive\":0}}");
    var summary = HostSession.PaintDiagnosticSummary(reply);

    Assert(summary is not null &&
           summary.Contains("diagnostic_strokes_after_limit=1", StringComparison.Ordinal) &&
           summary.Contains("local_stroke_calls=1", StringComparison.Ordinal) &&
           summary.Contains("local_dispatch_delay_ms=53", StringComparison.Ordinal) &&
           summary.Contains("local_dispatch_throttle_events=1", StringComparison.Ordinal) &&
           summary.Contains("first_stroke_roughness=0", StringComparison.Ordinal),
        "a one-stroke diagnostic must report the submitted direct call, adaptive throttle, and PBR inputs");
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

static void BridgeV2StartBlockCarriesRuntimeBundleIdentity()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var token = Enumerable.Range(1, BridgeStartBlockV2.TokenLength).Select(value => (byte)value).ToArray();
    var bridgeHash = Enumerable.Range(0, BridgeStartBlockV2.HashLength).Select(value => (byte)value).ToArray();
    var bundleHash = Enumerable.Range(0, BridgeStartBlockV2.HashLength).Select(value => (byte)(255 - value)).ToArray();
    var source = BridgeStartBlockV2.Create(4242, instanceId, token, bridgeHash, bundleHash);

    var bytes = source.Serialize();

    Assert(bytes.Length == 160, "V2 start block size changed");
    Assert(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0, 4)) == 0x3253434D, "V2 magic offset changed");
    Assert(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8, 4)) == 2, "V2 ABI offset changed");
    Assert(bytes.AsSpan(64, 32).SequenceEqual(bridgeHash), "V2 bridge hash offset changed");
    Assert(bytes.AsSpan(96, 32).SequenceEqual(bundleHash), "V2 runtime bundle hash offset changed");
    Assert(BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(140, 4)) == BridgeProtocolV2.Version, "V2 protocol offset changed");
    Assert(BridgeStartBlockV2.TryDeserialize(bytes, out var parsed, out var error), error);
    Assert(parsed.ExpectedPid == 4242 && parsed.InstanceId == instanceId, "V2 start block did not round-trip");
    Assert(parsed.ExpectedBridgeHash.SequenceEqual(bridgeHash), "V2 bridge hash did not round-trip");
    Assert(parsed.ExpectedRuntimeBundleHash.SequenceEqual(bundleHash), "V2 runtime bundle hash did not round-trip");
}

static void BridgeV2HelloValidatesRuntimeBundleIdentity()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var token = Enumerable.Range(1, BridgeStartBlockV2.TokenLength).Select(value => (byte)value).ToArray();
    var bridgeHash = string.Concat(Enumerable.Repeat("ab", 32));
    var bundleId = string.Concat(Enumerable.Repeat("cd", 32));
    var endpoint = new BridgeEndpoint(
        "127.0.0.1",
        54321,
        instanceId,
        token,
        bridgeHash,
        BridgeProtocolV2.Version,
        bundleId);
    var reply = JsonSerializer.Serialize(new
    {
        success = true,
        stage = "hello",
        message = "ok",
        metadata = new
        {
            pid = 4242,
            instance_id = instanceId.ToString("N"),
            bridge_hash = bridgeHash,
            runtime_bundle_id = bundleId,
            protocol_version = BridgeProtocolV2.Version
        }
    });

    Assert(BridgeProtocolV1.TryValidateHelloReply(reply, endpoint, 4242, out var identity, out var error), error);
    Assert(identity.RuntimeBundleId == bundleId, "V2 hello must retain the authenticated runtime bundle identity");

    var wrongReply = reply.Replace(bundleId, string.Concat(Enumerable.Repeat("ef", 32)), StringComparison.Ordinal);
    Assert(!BridgeProtocolV1.TryValidateHelloReply(wrongReply, endpoint, 4242, out _, out _),
        "V2 hello with a different runtime bundle identity must be rejected");
}

static void InjectorResultRequiresMatchingBridgeIdentity()
{
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var hash = string.Concat(Enumerable.Repeat("ab", 32));
    var raw = $$"""
    {"phase":"open_process","success":true,"pid":4242}
    {"event":"result","protocol":1,"success":true,"state":"listening","pid":4242,"instance_id":"{{instanceId:N}}","bridge_hash":"{{hash}}","port":54321,"win32":0,"winsock":0}
    """;

    Assert(InjectorResult.TryParseFinal(raw, out var result, out var error), error);
    Assert(result.Matches(4242, instanceId, hash), "matching result should be accepted");
    Assert(!result.Matches(4243, instanceId, hash), "wrong PID must be rejected");
    Assert(!result.Matches(4242, Guid.NewGuid(), hash), "wrong instance GUID must be rejected");
    Assert(!result.Matches(4242, instanceId, string.Concat(Enumerable.Repeat("cd", 32))), "wrong hash must be rejected");

    var bundleId = string.Concat(Enumerable.Repeat("cd", 32));
    var v2Raw = $$"""
    {"event":"result","protocol":2,"success":true,"state":"listening","pid":4242,"instance_id":"{{instanceId:N}}","bridge_hash":"{{hash}}","runtime_bundle_id":"{{bundleId}}","port":54321,"win32":0,"winsock":0}
    """;
    Assert(InjectorResult.TryParseFinal(v2Raw, out var v2Result, out error), error);
    Assert(v2Result.Matches(4242, instanceId, hash, bundleId),
        "V2 injector result should require the complete runtime bundle identity");
    Assert(!v2Result.Matches(4242, instanceId, hash, string.Concat(Enumerable.Repeat("ef", 32))),
        "V2 injector result with the wrong runtime bundle must be rejected");
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
    Assert(native.Contains("selected_texture_target_only", StringComparison.Ordinal),
        "texture diagnostics must avoid unrelated component readbacks that perturb joining-client timing");
    Assert(native.Contains("emissive_export", StringComparison.Ordinal) &&
           native.Contains("emissive_after_changed_rgba", StringComparison.Ordinal) &&
           native.Contains("roughness_after_changed_rgba", StringComparison.Ordinal),
        "research texture diagnostics must record all PBR channels and their changed output values");
    Assert(native.Contains("channel_data_schema", StringComparison.Ordinal) &&
           native.Contains("channel_enum_schema", StringComparison.Ordinal) &&
           native.Contains("out_patterns_schema", StringComparison.Ordinal),
        "research paint probes must report the live channel, enum, and array-parameter contracts");
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
    var bundle = string.Concat(Enumerable.Repeat("fedcba9876543210", 4));
    var instanceId = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
    var name = BridgeInstanceNaming.CreateBridgeFileName(
        bundle,
        instanceId);
    Assert(name.StartsWith("meccha-direct-bridge-v2-", StringComparison.Ordinal), "direct bridge prefix missing");
    Assert(name.Contains(bundle[..16], StringComparison.Ordinal), "direct bridge must include its runtime bundle generation");
    Assert(name.Contains(instanceId.ToString("N"), StringComparison.Ordinal), "direct bridge must include its unique instance id");
    Assert(!name.Contains(hash, StringComparison.Ordinal), "direct bridge filename must not duplicate the full build hash");
    Assert(!name.Contains("runtime-bridge", StringComparison.OrdinalIgnoreCase), "historical loader pattern must not be used");

    var localRoot = @"C:\Users\abcdefghijklmnopqrst\AppData\Local\MecchaCamouflage\bridge-instances";
    var directory = BridgeInstanceNaming.CreateDirectoryName(bundle, instanceId, 12345);
    var bridgePath = Path.Combine(localRoot, directory, name);
    Assert(bridgePath.Length < 260, "direct bridge path must remain below the legacy Win32 MAX_PATH limit");
}

static void InjectorDistinguishesIdentityAndLoadFailures()
{
    var root = FindRepositoryRoot();
    var injector = ReadRepositoryText(Path.Combine(
        root, "src", "native", "injector", "injector.cpp"));
    var runtime = ReadRepositoryText(Path.Combine(
        root, "src", "csharp", "MecchaCamouflage.Controller", "RuntimeBridgeService.cs"));

    Assert(injector.Contains("\"target_creation_time_query_failed\"", StringComparison.Ordinal) &&
           injector.Contains("\"target_image_path_query_failed\"", StringComparison.Ordinal) &&
           injector.Contains("\"target_exited_during_loadlibrary\"", StringComparison.Ordinal) &&
           injector.Contains("\"remote_loadlibrary_failed\"", StringComparison.Ordinal) &&
           injector.Contains("load_exit == 0", StringComparison.Ordinal) &&
           runtime.Contains("win32=", StringComparison.Ordinal),
        "injector failures must identify the exact API stage, zero LoadLibrary result, process exit, and Win32 code");
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
           bridge.Contains("meccha-direct-bridge-v2-", StringComparison.Ordinal) &&
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
    Assert(workflow.Contains("BUILD_VERSION: ci-${{ github.run_number }}-${{ github.run_attempt }}", StringComparison.Ordinal) &&
           workflow.Contains(".build/obj/$env:BUILD_VERSION/package-native", StringComparison.Ordinal) &&
           workflow.Contains("bridge loader must not be packaged", StringComparison.OrdinalIgnoreCase),
        "CI must inspect the version-scoped native output and reject a packaged loader DLL");
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

static void ReleasePublishRebuildsEmbeddedRuntimeAssets()
{
    var root = FindRepositoryRoot();
    var build = File.ReadAllText(Path.Combine(root, "scripts", "build.ps1"));
    var publishStart = build.IndexOf("\"publish\", $WebHostProject", StringComparison.Ordinal);
    Assert(publishStart >= 0, "release WebHost publish command must remain discoverable");
    var publishBlock = build[
        build.LastIndexOf("Invoke-BuildStep", publishStart, StringComparison.Ordinal)..publishStart];
    Assert(publishBlock.Contains("bin\\MecchaCamouflage.WebHost", StringComparison.Ordinal) &&
           publishBlock.Contains("obj\\MecchaCamouflage.WebHost", StringComparison.Ordinal) &&
           publishBlock.Contains("Remove-Item -LiteralPath $artifactPath -Recurse -Force", StringComparison.Ordinal),
        "release packaging must discard only stale WebHost artifacts before embedding current mesh profiles and native assets");
}

static void ReleaseVerifiesEmbeddedRuntimeBundleIdentity()
{
    var root = FindRepositoryRoot();
    var program = ReadRepositoryText(Path.Combine(
        root, "src", "csharp", "MecchaCamouflage.WebHost", "Program.cs"));
    var verifierPath = Path.Combine(root, "scripts", "verify-runtime-bundle.ps1");
    var workflow = ReadRepositoryText(Path.Combine(root, ".github", "workflows", "release.yml"));

    Assert(File.Exists(verifierPath), "release runtime bundle verifier is missing");
    var verifier = ReadRepositoryText(verifierPath);
    Assert(program.Contains("--verify-runtime-bundle", StringComparison.Ordinal) &&
           verifier.Contains("runtime_bundle_id", StringComparison.Ordinal) &&
           verifier.Contains("Get-FileHash", StringComparison.Ordinal) &&
           verifier.Contains("-Algorithm SHA256", StringComparison.Ordinal) &&
           verifier.Contains("ExpectedVersion", StringComparison.Ordinal) &&
           verifier.Contains("GITHUB_STEP_SUMMARY", StringComparison.Ordinal) &&
           workflow.Contains("verify-runtime-bundle.ps1", StringComparison.Ordinal) &&
           workflow.Contains("concurrency:", StringComparison.Ordinal),
        "release publishing must verify the tagged executable's embedded version and runtime bundle before upload");
}

static void RequiredSubmodulesAreDeclaredAndCheckedOut()
{
    var root = FindRepositoryRoot();
    var modules = File.ReadAllText(Path.Combine(root, ".gitmodules"));
    var ci = File.ReadAllText(Path.Combine(root, ".github", "workflows", "ci.yml"));
    var release = File.ReadAllText(Path.Combine(root, ".github", "workflows", "release.yml"));
    var readme = File.ReadAllText(Path.Combine(root, "README.md"));
    var contributing = File.ReadAllText(Path.Combine(root, "CONTRIBUTING.md"));
    var thirdPartyDocs = File.ReadAllText(Path.Combine(root, "docs", "research-tools.md"));

    Assert(modules.Contains("[submodule \"third_party/minhook\"]", StringComparison.Ordinal) &&
           modules.Contains("path = third_party/minhook", StringComparison.Ordinal) &&
           modules.Contains("https://github.com/TsudaKageyu/minhook.git", StringComparison.Ordinal),
        "the required MinHook gitlink must have a public submodule mapping");
    Assert(ci.Contains("submodules: recursive", StringComparison.Ordinal) &&
           release.Contains("submodules: recursive", StringComparison.Ordinal),
        "CI and release builds must initialize required submodules");
    Assert(readme.Contains("git clone --recurse-submodules", StringComparison.Ordinal) &&
           contributing.Contains("git clone --recurse-submodules", StringComparison.Ordinal) &&
           contributing.Contains("git submodule update --init --recursive", StringComparison.Ordinal),
        "developer setup must initialize pinned dependencies");
    Assert(thirdPartyDocs.Contains("## MinHook", StringComparison.Ordinal) &&
           thirdPartyDocs.Contains("BSD 2-Clause", StringComparison.Ordinal) &&
           thirdPartyDocs.Contains("third_party/minhook", StringComparison.Ordinal),
        "third-party documentation must identify MinHook's purpose, license, and pinned path");
}

static void MissingDefenderExclusionIsAddedWithElevation()
{
    var platform = new FakeWindowsDefenderExclusionPlatform(
        elevatedExitCode: 0,
        WindowsDefenderExclusionCheck.Missing,
        WindowsDefenderExclusionCheck.Configured);
    var service = new WindowsDefenderExclusionService(platform);
    var path = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "MecchaCamouflage");

    var result = service.EnsureConfiguredAsync(path).GetAwaiter().GetResult();

    Assert(result.Outcome == WindowsDefenderExclusionOutcome.Added,
        "a successful elevated request must report that the exclusion was added");
    Assert(platform.ElevatedPaths.SequenceEqual([path]),
        "the exact MecchaCamouflage LocalAppData path must be elevated once");
}

static void DefenderExclusionMarkerPreventsRepeatedElevation()
{
    var root = Path.Combine(
        Path.GetTempPath(),
        "meccha-defender-marker-" + Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(root);
    try
    {
        var firstPlatform = new FakeWindowsDefenderExclusionPlatform(
            elevatedExitCode: 0,
            WindowsDefenderExclusionCheck.Missing);
        var first = new WindowsDefenderExclusionService(firstPlatform)
            .EnsureConfiguredAsync(root).GetAwaiter().GetResult();
        var markerPath = Path.Combine(root, "defender-exclusion-added.txt");

        Assert(first.Outcome == WindowsDefenderExclusionOutcome.Added &&
               File.ReadAllText(markerPath).Trim() == "1",
            "a successful elevated addition must write the version-independent root marker");

        var secondPlatform = new FakeWindowsDefenderExclusionPlatform(
            elevatedExitCode: 0,
            WindowsDefenderExclusionCheck.Hidden);
        var second = new WindowsDefenderExclusionService(secondPlatform)
            .EnsureConfiguredAsync(root).GetAwaiter().GetResult();

        Assert(second.Outcome == WindowsDefenderExclusionOutcome.AlreadyConfigured &&
               secondPlatform.CheckCount == 1 &&
               secondPlatform.ElevatedPaths.Count == 0,
            "the marker must bypass hidden Defender exclusions without another UAC request");

        var removedPlatform = new FakeWindowsDefenderExclusionPlatform(
            elevatedExitCode: 0,
            WindowsDefenderExclusionCheck.Missing);
        var removed = new WindowsDefenderExclusionService(removedPlatform)
            .EnsureConfiguredAsync(root).GetAwaiter().GetResult();

        Assert(removed.Outcome == WindowsDefenderExclusionOutcome.Added &&
               removedPlatform.CheckCount == 1 &&
               removedPlatform.ElevatedPaths.SequenceEqual([root]),
            "a visible missing exclusion must override a stale marker and request UAC again");
    }
    finally
    {
        try { Directory.Delete(root, true); } catch { }
    }
}

static void CancellingDefenderElevationReturnsNonfatalResult()
{
    var platform = new FakeWindowsDefenderExclusionPlatform(
        elevatedExitCode: 1223,
        WindowsDefenderExclusionCheck.Missing);
    var service = new WindowsDefenderExclusionService(platform);

    var result = service.EnsureConfiguredAsync(
        Path.Combine(Path.GetTempPath(), "MecchaCamouflage")).GetAwaiter().GetResult();

    Assert(result.Outcome == WindowsDefenderExclusionOutcome.Cancelled,
        "declining UAC must return a nonfatal cancelled result");
}

static void ConfiguredDefenderExclusionDoesNotRequestElevation()
{
    var platform = new FakeWindowsDefenderExclusionPlatform(
        elevatedExitCode: 0,
        WindowsDefenderExclusionCheck.Configured);
    var service = new WindowsDefenderExclusionService(platform);

    var result = service.EnsureConfiguredAsync(
        Path.Combine(Path.GetTempPath(), "MecchaCamouflage")).GetAwaiter().GetResult();

    Assert(result.Outcome == WindowsDefenderExclusionOutcome.AlreadyConfigured &&
           platform.ElevatedPaths.Count == 0,
        "an existing exclusion must skip the elevated process entirely");
}

static void DesktopStartupEnsuresDefenderExclusionBeforeGui()
{
    var root = FindRepositoryRoot();
    var program = File.ReadAllText(Path.Combine(
        root, "src", "csharp", "MecchaCamouflage.WebHost", "Program.cs"));
    var ensureOffset = program.IndexOf(
        "EnsureWindowsDefenderExclusion(paths);",
        StringComparison.Ordinal);
    var guiOffset = program.IndexOf("Application.Run(form)", StringComparison.Ordinal);

    Assert(ensureOffset >= 0 &&
           guiOffset > ensureOffset &&
           program.Contains("EnsureConfiguredAsync(paths.RootDirectory)", StringComparison.Ordinal),
        "every WebHost executable must ensure the Defender exclusion before starting the GUI");
}

static void DefenderElevationUsesTrustedSystemPowerShell()
{
    var root = FindRepositoryRoot();
    var service = File.ReadAllText(Path.Combine(
        root,
        "src",
        "csharp",
        "MecchaCamouflage.Controller",
        "WindowsDefenderExclusionService.cs"));

    Assert(service.Contains(
            "Path.Combine(Environment.SystemDirectory, \"WindowsPowerShell\", \"v1.0\", \"powershell.exe\")",
            StringComparison.Ordinal) &&
           service.Contains("startInfo.Verb = \"runas\"", StringComparison.Ordinal),
        "the elevated process must use Windows system PowerShell rather than PATH lookup");
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

static void DevelopmentBuildsUseIsolatedVersionScopes()
{
    var root = FindRepositoryRoot();
    var makefile = File.ReadAllText(Path.Combine(root, "Makefile"));
    var build = File.ReadAllText(Path.Combine(root, "scripts", "build.ps1"));

    Assert(makefile.Contains("VERSION := $(shell", StringComparison.Ordinal) &&
           makefile.Contains("git status --porcelain --untracked-files=normal", StringComparison.Ordinal) &&
           makefile.Contains("-build-%s", StringComparison.Ordinal),
        "make build must allocate one immutable identity for each development invocation");
    Assert(build.Contains("Only a clean, exact tag is a stable release identity", StringComparison.Ordinal) &&
           build.Contains("-build-$([DateTime]::UtcNow.ToString", StringComparison.Ordinal),
        "direct build.ps1 invocations must use the same version-isolation rule");

    using var temp = new TempHome();
    var first = new AppPaths("dev-build-a");
    var second = new AppPaths("dev-build-b");
    Directory.CreateDirectory(first.ImagePresetsDirectory);
    File.WriteAllText(Path.Combine(first.ImagePresetsDirectory, "marker"), "first build");
    Assert(!File.Exists(Path.Combine(second.ImagePresetsDirectory, "marker")),
        "distinct build identities must not share Image preset storage");
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

static string ReadRepositoryText(string path)
{
    return File.ReadAllText(path).ReplaceLineEndings("\n");
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

sealed class FakeWindowsDefenderExclusionPlatform(
    int elevatedExitCode,
    params WindowsDefenderExclusionCheck[] checks) : IWindowsDefenderExclusionPlatform
{
    private readonly Queue<WindowsDefenderExclusionCheck> checks = new(checks);
    private WindowsDefenderExclusionCheck lastCheck = checks.LastOrDefault();
    public int CheckCount { get; private set; }
    public List<string> ElevatedPaths { get; } = [];

    public Task<WindowsDefenderExclusionCheck> CheckAsync(
        string path,
        CancellationToken cancellationToken)
    {
        ++CheckCount;
        if (checks.Count > 0)
            lastCheck = checks.Dequeue();
        return Task.FromResult(lastCheck);
    }

    public Task<int> AddElevatedAsync(
        string path,
        CancellationToken cancellationToken)
    {
        ElevatedPaths.Add(path);
        return Task.FromResult(elevatedExitCode);
    }
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
