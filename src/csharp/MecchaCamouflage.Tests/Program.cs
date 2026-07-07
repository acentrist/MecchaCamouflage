using System.Text.Json;
using MecchaCamouflage.Controller;
using MecchaCamouflage.Core;

var tests = new List<(string Name, Action Run)>
{
    ("legacy false region migrates to fill", LegacyFalseRegionMigratesToFill),
    ("payload includes packed route and fill material", PayloadIncludesPackedRouteAndFillMaterial),
    ("settings clamp syncs packed batch delay", SettingsClampSyncsPackedBatchDelay),
    ("locales have complete keys", LocalesHaveCompleteKeys),
    ("color parser accepts rrggbb", ColorParserAcceptsHex),
    ("runtime cleanup removes old hash dirs", RuntimeCleanupRemovesOldHashDirs),
    ("runtime cleanup keeps loader and bridge dirs", RuntimeCleanupKeepsLoaderAndBridgeDirs),
    ("runtime log keeps repeated guard messages", RuntimeLogKeepsRepeatedGuardMessages),
    ("asset validation rejects stale ready cache", AssetValidationRejectsStaleReadyCache),
    ("copy if invalid repairs corrupt target", CopyIfInvalidRepairsCorruptTarget),
    ("diagnostic summary includes file not found details", DiagnosticSummaryIncludesFileNotFoundDetails),
    ("diagnostics log write is best effort when file is locked", DiagnosticsLogWriteIsBestEffortWhenFileLocked),
    ("runtime log write is best effort when file is locked", RuntimeLogWriteIsBestEffortWhenFileLocked),
    ("auto material defaults off", AutoMaterialDefaultsOff),
    ("front region defaults to paint", FrontRegionDefaultsToPaint),
    ("bridge messages are user friendly", BridgeMessagesAreUserFriendly),
    ("settings clamp syncs coverage step to brush size", SettingsClampSyncsCoverageToBrush),
    ("settings detect supported system language", SettingsDetectSupportedSystemLanguage),
    ("ui snapshot hides legacy batch tuning", UiSnapshotHidesLegacyBatchTuning),
    ("hotkey validation rejects duplicates", HotkeyValidationRejectsDuplicates),
    ("host session reset restores setting default", HostSessionResetRestoresDefault),
    ("host session brush update syncs coverage step", HostSessionBrushUpdateSyncsCoverageStep),
    ("host session rolls back invalid hotkey update", HostSessionRollsBackInvalidHotkeyUpdate),
    ("host session applies multiple setting updates atomically", HostSessionAppliesMultipleSettingUpdatesAtomically),
    ("host session rolls back duplicate hotkey batch", HostSessionRollsBackDuplicateHotkeyBatch),
    ("host session rolls back invalid fill color batch", HostSessionRollsBackInvalidFillColorBatch),
    ("host session rolls back invalid theme color batch", HostSessionRollsBackInvalidThemeColorBatch),
    ("host session rolls back invalid region mode batch", HostSessionRollsBackInvalidRegionModeBatch),
    ("host session progress candidates use bridge state", HostSessionProgressCandidatesUseBridgeState),
    ("host session snapshot ignores pre-paint progress", HostSessionSnapshotIgnoresPrePaintProgress),
    ("host session warns when cancel has no active paint", HostSessionWarnsWhenCancelHasNoActivePaint),
    ("host session counts native cancel jobs", HostSessionCountsNativeCancelJobs)
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

static void LegacyFalseRegionMigratesToFill()
{
    using var temp = new TempHome();
    var paths = new AppPaths("test-version");
    Directory.CreateDirectory(paths.VersionRoot);
    File.WriteAllText(paths.LegacyConfigPath, """
    {
      "layout_version": 23,
      "enable_front_paint": false,
      "enable_side_paint": true,
      "enable_back_paint": false
    }
    """);

    var settings = new SettingsStore(paths).Load();
    Assert(settings.Paint.FrontRegionMode == RegionMode.Fill, "front should migrate to fill");
    Assert(settings.Paint.SideRegionMode == RegionMode.Paint, "side should migrate to paint");
    Assert(settings.Paint.BackRegionMode == RegionMode.Fill, "back should migrate to fill");
}

static void PayloadIncludesPackedRouteAndFillMaterial()
{
    var settings = new AppSettings();
    settings.Paint.PackedBatchDelayMs = 88;
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
    Assert(tuning.GetProperty("server_batch_delay_ms").GetInt32() == 88, "server batch delay should be sent");
    Assert(tuning.GetProperty("fill_color").GetString() == "#F11111", "fill color missing");
    Assert(Math.Abs(tuning.GetProperty("fill_color_r").GetDouble() - (241.0 / 255.0)) < 0.00001, "fill red not normalized");
    Assert(tuning.GetProperty("enable_front_paint").GetBoolean() == false, "compat front bool wrong");
    Assert(tuning.GetProperty("enable_back_paint").GetBoolean(), "compat back bool wrong");
    Assert(!tuning.TryGetProperty("adaptive_batch_enabled", out _), "payload should not send legacy adaptive tuning");
    Assert(!tuning.TryGetProperty("server_batch_limit", out _), "payload should not send legacy batch limit tuning");
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

static void RuntimeCleanupRemovesOldHashDirs()
{
    using var temp = new TempHome();
    var paths = new AppPaths("cleanup-test");
    var keep = Path.Combine(paths.RuntimeBinDirectory, "keep");
    var recent = Path.Combine(paths.RuntimeBinDirectory, "recent");
    var old = Path.Combine(paths.RuntimeBinDirectory, "old");
    Directory.CreateDirectory(keep);
    Directory.CreateDirectory(recent);
    Directory.CreateDirectory(old);
    File.WriteAllText(Path.Combine(keep, "current.dll"), "");
    File.WriteAllText(Path.Combine(recent, "bridge.dll"), "");
    File.WriteAllText(Path.Combine(old, "bridge.dll"), "");
    Directory.SetLastWriteTimeUtc(old, DateTime.UtcNow - TimeSpan.FromDays(30));

    paths.CleanupRuntimeBinDirectories(keep, TimeSpan.FromDays(14), keepNewest: 3);

    Assert(Directory.Exists(keep), "current hash dir should be kept");
    Assert(Directory.Exists(recent), "recent hash dir should be kept");
    Assert(!Directory.Exists(old), "old hash dir should be removed");
}

static void RuntimeCleanupKeepsLoaderAndBridgeDirs()
{
    using var temp = new TempHome();
    var paths = new AppPaths("cleanup-multi-keep-test");
    var loader = Path.Combine(paths.RuntimeBinDirectory, "loader-current");
    var bridge = Path.Combine(paths.RuntimeBinDirectory, "bridge-current");
    var old = Path.Combine(paths.RuntimeBinDirectory, "bridge-old");
    Directory.CreateDirectory(loader);
    Directory.CreateDirectory(bridge);
    Directory.CreateDirectory(old);
    File.WriteAllText(Path.Combine(loader, "bridge-loader.dll"), "");
    File.WriteAllText(Path.Combine(bridge, "runtime-bridge.dll"), "");
    File.WriteAllText(Path.Combine(old, "runtime-bridge.dll"), "");
    Directory.SetLastWriteTimeUtc(old, DateTime.UtcNow - TimeSpan.FromDays(30));

    paths.CleanupRuntimeBinDirectories([loader, bridge], TimeSpan.FromDays(14), keepNewest: 2);

    Assert(Directory.Exists(loader), "loader runtime dir should be kept");
    Assert(Directory.Exists(bridge), "bridge runtime dir should be kept");
    Assert(!Directory.Exists(old), "old bridge runtime dir should be removed");
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

static void FrontRegionDefaultsToPaint()
{
    Assert(new AppSettings().Paint.FrontRegionMode == RegionMode.Paint, "front should default to paint");
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
    var unsafeSampling = HostSession.FriendlyBridgeMessage("planner found unsafe color-transfer candidates in enabled regions; replay was blocked instead of skipping samples");

    Assert(alreadyRunning == "Paint: already running.", "already-running message should be friendly");
    Assert(completed == "Paint: completed.", "completed message should be friendly");
    Assert(alreadyFriendlyCompleted == "Paint: completed.", "already-friendly completed message should be normalized");
    Assert(preview == "Preview: applied.", "preview message should be friendly");
    Assert(noPreview == "Preview: no active preview to restore.", "missing preview snapshot should be a guard warning");
    Assert(contextChanged == "Paint: stopped because the game paint component changed.", "paint context change should be friendly");
    Assert(componentUnavailable == "Paint: stopped because the game paint component is unavailable.", "paint component unavailable should be friendly");
    Assert(pawnUnavailable == "Paint: stopped because the local pawn is no longer available.", "pawn unavailable should be friendly");
    Assert(unsafeSampling == "Paint: blocked because the current mesh sampling was unsafe.", "unsafe mesh sampling should be friendly");
    Assert(!alreadyRunning.Contains("mesh", StringComparison.OrdinalIgnoreCase), "internal mesh wording should be hidden");
}

static void SettingsClampSyncsCoverageToBrush()
{
    var settings = new AppSettings();
    settings.Paint.StrokeSizeTexels = 12.0;
    settings.Paint.CoverageStepTexels = 2.0;

    var clamped = SettingsStore.Clamp(settings);

    Assert(Math.Abs(clamped.Paint.StrokeSizeTexels - 10.0) < 0.000001, "brush size should clamp to max");
    Assert(Math.Abs(clamped.Paint.CoverageStepTexels - clamped.Paint.StrokeSizeTexels) < 0.000001, "coverage step should follow brush size");

    settings.Paint.StrokeSizeTexels = -1.0;
    settings.Paint.CoverageStepTexels = 2.0;
    clamped = SettingsStore.Clamp(settings);

    Assert(Math.Abs(clamped.Paint.StrokeSizeTexels - 1.0) < 0.000001, "brush size should clamp to min");
    Assert(Math.Abs(clamped.Paint.CoverageStepTexels - clamped.Paint.StrokeSizeTexels) < 0.000001, "coverage step should follow clamped min brush size");
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

static void UiSnapshotHidesLegacyBatchTuning()
{
    var snapshot = new PaintSnapshot(
        6.0,
        75,
        6.0,
        false,
        0.0,
        1.0,
        "paint",
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

    Assert(!doc.RootElement.TryGetProperty("serverBatchLimit", out _), "snapshot should not expose serverBatchLimit for editing");
    Assert(!doc.RootElement.TryGetProperty("adaptiveBatching", out _), "snapshot should not expose adaptiveBatching for editing");
    Assert(!doc.RootElement.TryGetProperty("strokeDelayMs", out _), "snapshot should not expose strokeDelayMs for editing");
    Assert(!doc.RootElement.TryGetProperty("batchSize", out _), "snapshot should not expose renamed batchSize");
}

static void SettingsClampSyncsPackedBatchDelay()
{
    var settings = new AppSettings();
    settings.Paint.PackedBatchDelayMs = 150;

    var clamped = SettingsStore.Clamp(settings);

    Assert(clamped.Paint.PackedBatchDelayMs == 100, "pack batch delay should clamp to max");

    settings.Paint.PackedBatchDelayMs = 12;
    clamped = SettingsStore.Clamp(settings);

    Assert(clamped.Paint.PackedBatchDelayMs == 50, "pack batch delay should clamp to min");
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

    var update = session.UpdateSetting("paint.brushSizeTexels", JsonSerializer.SerializeToElement(10.0));
    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - 10.0) < 0.000001, "setting should update");

    var reset = session.ResetSetting("paint.brushSizeTexels");
    Assert(reset.Success, reset.Message);
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - new AppSettings().Paint.StrokeSizeTexels) < 0.000001, "setting should reset");
}

static void HostSessionBrushUpdateSyncsCoverageStep()
{
    using var temp = new TempHome();
    var session = new HostSession("host-brush-sync-test");

    var update = session.UpdateSetting("paint.brushSizeTexels", JsonSerializer.SerializeToElement(6.5));

    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - 6.5) < 0.000001, "brush size should update");
    Assert(Math.Abs(session.Settings.Paint.CoverageStepTexels - 6.5) < 0.000001, "coverage step should follow brush size");
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
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(10.0)),
        new SettingChange("paint.fillColor", JsonSerializer.SerializeToElement("#112233")),
        new SettingChange("app.processName", JsonSerializer.SerializeToElement("Game.exe"))
    ]);

    Assert(update.Success, update.Message);
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - 10.0) < 0.000001, "brush size should update");
    Assert(session.Settings.Paint.FillColor.ToHex() == "#112233", "fill color should update");
    Assert(session.Settings.GameProcessName == "Game.exe", "process name should update");
}

static void HostSessionRollsBackDuplicateHotkeyBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-hotkey-rollback-test");
    var originalBrush = session.Settings.Paint.StrokeSizeTexels;
    var originalPreview = session.Settings.PreviewHotkey;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(10.0)),
        new SettingChange("app.previewHotkey", JsonSerializer.SerializeToElement(session.Settings.StartHotkey))
    ]);

    Assert(!update.Success, "duplicate hotkey batch should fail");
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - originalBrush) < 0.000001, "non-hotkey change should roll back");
    Assert(session.Settings.PreviewHotkey == originalPreview, "hotkey change should roll back");
}

static void HostSessionRollsBackInvalidFillColorBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-color-rollback-test");
    var originalBrush = session.Settings.Paint.StrokeSizeTexels;
    var originalColor = session.Settings.Paint.FillColor;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(10.0)),
        new SettingChange("paint.fillColor", JsonSerializer.SerializeToElement("not-a-color"))
    ]);

    Assert(!update.Success, "invalid color batch should fail");
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - originalBrush) < 0.000001, "brush size should roll back");
    Assert(session.Settings.Paint.FillColor == originalColor, "fill color should roll back");
}

static void HostSessionRollsBackInvalidThemeColorBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-theme-rollback-test");
    var originalBrush = session.Settings.Paint.StrokeSizeTexels;
    var originalTheme = session.Settings.ThemeColor;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(10.0)),
        new SettingChange("app.themeColor", JsonSerializer.SerializeToElement("not-a-color"))
    ]);

    Assert(!update.Success, "invalid theme color batch should fail");
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - originalBrush) < 0.000001, "brush size should roll back");
    Assert(session.Settings.ThemeColor == originalTheme, "theme color should roll back");
}

static void HostSessionRollsBackInvalidRegionModeBatch()
{
    using var temp = new TempHome();
    var session = new HostSession("host-batch-region-rollback-test");
    var originalBrush = session.Settings.Paint.StrokeSizeTexels;
    var originalMode = session.Settings.Paint.FrontRegionMode;

    var update = session.UpdateSettings([
        new SettingChange("paint.brushSizeTexels", JsonSerializer.SerializeToElement(10.0)),
        new SettingChange("paint.frontRegionMode", JsonSerializer.SerializeToElement("invalid"))
    ]);

    Assert(!update.Success, "invalid region mode batch should fail");
    Assert(Math.Abs(session.Settings.Paint.StrokeSizeTexels - originalBrush) < 0.000001, "brush size should roll back");
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

    Assert(HostSession.CancelledPaintJobCount(none) == 0, "zero cancel counts should remain zero");
    Assert(HostSession.CancelledPaintJobCount(active) == 3, "active and queued cancel counts should be summed");
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
