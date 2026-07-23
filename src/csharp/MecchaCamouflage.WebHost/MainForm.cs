using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Diagnostics;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;
using MecchaCamouflage.Controller;
using MecchaCamouflage.Core;

namespace MecchaCamouflage.WebHost;

public sealed class MainForm : Form
{
    private const string EvergreenBootstrapperFileName = "MicrosoftEdgeWebview2Setup.exe";
    private const string ManualWebView2RuntimeUrl = "https://developer.microsoft.com/microsoft-edge/webview2/";
    private const string WebUiHostName = "meccha.localhost";
    private const string WebUiStartUri = "https://meccha.localhost/index.html";
    private static readonly TimeSpan UiReadyTimeout = TimeSpan.FromSeconds(15);
    private const int HotkeyStart = 1;
    private const int HotkeyPreview = 2;
    private const int HotkeyUnPreview = 3;
    private const int HotkeyStop = 4;
    private const int HotkeyImageStart = 5;
    private const int HotkeyImagePreview = 6;
    private const int HotkeyImageUnPreview = 7;
    private const int HotkeyImageStop = 8;
    // Keep WebView messages comfortably below the sizes where Windows message
    // transport or JSON allocation becomes unreliable. Image assets are never
    // included in a regular settings snapshot.
    private const int ImageTransferChunkCharacters = 128 * 1024;
    private const int MaximumImageTransferCharacters = 40 * 1024 * 1024;
    private static readonly TimeSpan ImageTransferLifetime = TimeSpan.FromMinutes(5);
    private const int WmInput = 0x00FF;
    private const uint RidInput = 0x10000003;
    private const uint RimTypeKeyboard = 1;
    private const uint RidevRemove = 0x00000001;
    private const uint RidevInputSink = 0x00000100;
    private const ushort HidUsagePageGeneric = 0x01;
    private const ushort HidUsageGenericKeyboard = 0x06;
    private const uint WmKeyDown = 0x0100;
    private const uint WmKeyUp = 0x0101;
    private const uint WmSystemKeyDown = 0x0104;
    private const uint WmSystemKeyUp = 0x0105;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false
    };

    private readonly HostSession session;
    private readonly WebViewStartupLifecycle webViewStartup = new();
    private WebView2? webView;
    private readonly System.Windows.Forms.Timer statusTimer = new() { Interval = 2000 };
    private CancellationTokenSource? uiReadyTimeoutCancellation;
    private bool webReady;
    private bool guiInitializedLogged;
    private bool settingsEditing;
    private bool hotkeyRecording;
    private readonly HotkeyKeyState hotkeyKeyState = new();
    private bool rawKeyboardRegistered;
    private bool webViewRetryUsed;
    private bool webViewRecoveryInProgress;
    private bool bridgeShutdownInProgress;
    private bool bridgeShutdownCompleted;
    private readonly Dictionary<string, StagedImageDesignTransfer> stagedImageDesigns = new(StringComparer.Ordinal);
    // Presets are loaded through a native dialog and held only long enough for
    // the WebView to request their assets in bounded chunks. They are drafts:
    // loading never changes the active game state.
    private readonly Dictionary<string, LoadedImagePresetTransfer> loadedImagePresets = new(StringComparer.Ordinal);

    private string UiText(string key) => session.Localization.Text(session.Settings.Language, key);

    private sealed class StagedImageDesignTransfer
    {
        public DateTimeOffset LastUpdated { get; set; } = DateTimeOffset.UtcNow;
        public Dictionary<string, StagedImageAsset> Assets { get; } = new(StringComparer.Ordinal);
    }

    private sealed class StagedImageAsset
    {
        public int NextChunkIndex { get; set; }
        public StringBuilder Data { get; } = new();
    }

    private sealed class LoadedImagePresetTransfer
    {
        public DateTimeOffset LastUpdated { get; set; } = DateTimeOffset.UtcNow;
        public required ImagePaintSettings Design { get; init; }
    }

    public MainForm(HostSession session)
    {
        this.session = session;
        Text = UiText("app.title");
        Icon = LoadWindowIcon();
        MinimumSize = new Size(960, 640);
        Width = (int)Math.Round(session.Settings.PanelWidth);
        Height = (int)Math.Round(session.Settings.PanelHeight);
        if (session.Settings.PanelX >= 0 && session.Settings.PanelY >= 0)
        {
            StartPosition = FormStartPosition.Manual;
            Left = (int)Math.Round(session.Settings.PanelX);
            Top = (int)Math.Round(session.Settings.PanelY);
        }
        TopMost = session.Settings.AlwaysOnTop;
        Opacity = session.Settings.Opacity;
        BackColor = Color.FromArgb(32, 32, 32);
        webView = CreateWebViewControl();
        Controls.Add(webView);

        Shown += async (_, _) =>
        {
            try
            {
                ApplyWindowSettings("shown");
                await InitializeWebViewAsync();
            }
            catch (Exception ex)
            {
                await HandleWebViewInitializationFailureAsync(ex);
            }
        };
        FormClosing += HandleFormClosingAsync;
        ResizeEnd += (_, _) => PersistWindowSnapshot();
        Move += (_, _) => PersistWindowSnapshot();
        statusTimer.Tick += async (_, _) =>
        {
            statusTimer.Interval = session.PaintRunning ? 500 : 2000;
            if (webReady)
                StartBridgeWarmup();
            await PushSnapshotAsync();
        };
        session.Log.Changed += (_, _) => PushSnapshotFromAnyThread();
        statusTimer.Start();
    }

    private async void HandleFormClosingAsync(object? sender, FormClosingEventArgs eventArgs)
    {
        if (!bridgeShutdownCompleted)
        {
            eventArgs.Cancel = true;
            if (bridgeShutdownInProgress)
                return;

            bridgeShutdownInProgress = true;
            webReady = false;
            CancelUiReadyTimeout();
            statusTimer.Stop();
            try
            {
                await session.ShutdownBridgeAsync();
            }
            catch (Exception exception)
            {
                DiagnosticsState.RecordException("bridge_shutdown_on_form_close", exception);
            }
            finally
            {
                bridgeShutdownInProgress = false;
                bridgeShutdownCompleted = true;
                if (!IsDisposed)
                    Close();
            }
            return;
        }

        webReady = false;
        CancelUiReadyTimeout();
        statusTimer.Stop();
        PersistWindowSnapshot();
        UnregisterRawKeyboardInput();
        hotkeyKeyState.Clear();
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        ApplyDarkTitleBar();
        ApplyWindowSettings("handle-created");
        if (!TryRegisterRawKeyboardInput(out var message))
            session.Log.Warn(message);
    }

    protected override void WndProc(ref Message message)
    {
        if (message.Msg == WmInput)
        {
            HandleRawKeyboardInput(message.LParam);
            base.WndProc(ref message);
            return;
        }
        base.WndProc(ref message);
    }

    private async Task InitializeWebViewAsync()
    {
        var generation = webViewStartup.Begin();
        webReady = false;
        guiInitializedLogged = false;
        CancelUiReadyTimeout();
        DiagnosticsState.SetStartupPhase("webview2_prepare");
        var runtime = await PrepareEvergreenWebRuntimeAsync();
        var view = webView ?? throw new InvalidOperationException("MC-WV-201 WebView2 control is unavailable.");
        session.Log.Info("WebView2 runtime: creating Evergreen environment.");
        DiagnosticsState.SetStartupPhase("webview2_environment_create");
        var environment = await CoreWebView2Environment.CreateAsync(
            null,
            runtime.UserDataFolder,
            CreateEvergreenEnvironmentOptions());
        await view.EnsureCoreWebView2Async(environment);
        var core = view.CoreWebView2 ?? throw new InvalidOperationException("MC-WV-202 WebView2 did not create a CoreWebView2 instance.");
        DiagnosticsState.SetStartupPhase("webview2_environment_ready");
        session.Log.Info($"WebView2 runtime: initialized ({runtime.Version}).");
        VerifyPackagedWebAssets(runtime.WebRoot);

        core.WebMessageReceived += async (sender, args) => await HandleWebMessageAsync(generation, sender, args);
        core.NavigationCompleted += (sender, args) => HandleNavigationCompleted(generation, sender, args);
        core.NavigationStarting += (sender, args) => HandleNavigationStarting(generation, sender, args);
        core.ContentLoading += (sender, args) => HandleContentLoading(generation, sender, args);
        core.DOMContentLoaded += (sender, args) => HandleDomContentLoaded(generation, sender, args);
        core.NewWindowRequested += (_, args) => HandleNewWindowRequested(args);
        core.ProcessFailed += HandleWebViewProcessFailed;
        view.ZoomFactorChanged += (sender, _) =>
        {
            if (ReferenceEquals(sender, webView) && webReady && !webViewRecoveryInProgress)
                PostWebViewZoomFactor();
        };
        core.SetVirtualHostNameToFolderMapping(
            WebUiHostName,
            runtime.WebRoot,
            CoreWebView2HostResourceAccessKind.DenyCors);
        core.Settings.AreDefaultScriptDialogsEnabled = true;
        core.Settings.AreDefaultContextMenusEnabled = false;
        core.Settings.AreBrowserAcceleratorKeysEnabled = false;
        core.Settings.AreDevToolsEnabled = BuildFeatures.ResearchArtifactsEnabled;
        DiagnosticsState.SetStartupPhase("webview2_navigate");
        session.Log.Info("GUI: navigating packaged index.html.");
        DiagnosticsState.WriteLine("webview2", $"navigation_requested generation={generation} uri={WebUiStartUri}");
        core.Navigate(WebUiStartUri);
        StartUiReadyTimeout();
    }

    private void VerifyPackagedWebAssets(string webRoot)
    {
        if (!Directory.Exists(webRoot))
            throw new DirectoryNotFoundException("MC-WV-105 Packaged web assets are missing: " + webRoot);

        foreach (var fileName in new[]
                 {
                     "index.html",
                     "app.js",
                     "styles.css",
                     Path.Combine("mesh-profiles", "paintman.image-profile-v2.json"),
                     Path.Combine("mesh-profiles", "paintman_cube.image-profile-v2.json")
                 })
        {
            var path = Path.Combine(webRoot, fileName);
            if (!File.Exists(path))
                throw new FileNotFoundException($"MC-WV-105 Packaged web asset is missing: {fileName}", path);

            var length = new FileInfo(path).Length;
            if (length == 0)
                throw new IOException($"MC-WV-105 Packaged web asset is empty: {fileName}");
            DiagnosticsState.WriteLine("webview2", $"asset_verified name={fileName} bytes={length} path={path}");
        }
    }

    private async void HandleNavigationCompleted(long generation, object? sender, CoreWebView2NavigationCompletedEventArgs args)
    {
        if (!IsActiveWebView(generation, sender) || webViewRecoveryInProgress)
            return;

        try
        {
            var source = webView?.CoreWebView2?.Source ?? WebUiStartUri;
            var startupNavigation = webViewStartup.IsInitialNavigation(generation, args.NavigationId);
            DiagnosticsState.WriteLine(
                "webview2",
                $"navigation_completed generation={generation} id={args.NavigationId} startup={startupNavigation} success={args.IsSuccess} status={args.HttpStatusCode} error={args.WebErrorStatus} uri={source}");
            if (!startupNavigation)
                return;
            DiagnosticsState.SetStartupPhase("webview2_navigation_completed");
            if (!args.IsSuccess)
            {
                await RecoverWebViewAsync(
                    "dialog.webview.failed.navigation",
                    new InvalidOperationException($"MC-WV-203 WebView2 navigation failed: {args.WebErrorStatus}"));
                return;
            }
            if (guiInitializedLogged)
                return;
            guiInitializedLogged = true;
            session.Log.Info("GUI: web assets loaded; waiting for uiReady.");
            if (webViewStartup.MarkNavigationSucceeded(generation, args.NavigationId))
                QueueWindowSettingsStabilization(generation);
        }
        catch (Exception ex)
        {
            DiagnosticsState.RecordException("gui_navigation_completed_failed", ex);
            session.Log.Error(ex.Message);
        }
    }

    private void HandleContentLoading(long generation, object? sender, CoreWebView2ContentLoadingEventArgs args)
    {
        if (!IsActiveWebView(generation, sender) || webViewRecoveryInProgress)
            return;

        var source = webView?.CoreWebView2?.Source ?? WebUiStartUri;
        DiagnosticsState.SetStartupPhase("webview2_content_loading");
        DiagnosticsState.WriteLine(
            "webview2",
            $"content_loading generation={generation} id={args.NavigationId} error_page={args.IsErrorPage} uri={source}");
    }

    private void HandleDomContentLoaded(long generation, object? sender, CoreWebView2DOMContentLoadedEventArgs args)
    {
        if (!IsActiveWebView(generation, sender) || webViewRecoveryInProgress)
            return;

        var source = webView?.CoreWebView2?.Source ?? WebUiStartUri;
        DiagnosticsState.SetStartupPhase("webview2_dom_content_loaded");
        session.Log.Info("GUI: DOM content loaded.");
        DiagnosticsState.WriteLine("webview2", $"dom_content_loaded generation={generation} id={args.NavigationId} uri={source}");
    }

    private async Task<WebRuntimeInfo> PrepareEvergreenWebRuntimeAsync()
    {
        DiagnosticsState.SetStartupPhase("webview2_runtime_detect");
        var version = await Task.Run(TryGetEvergreenRuntimeVersion);
        if (string.IsNullOrWhiteSpace(version))
        {
            DiagnosticsState.SetLastCode("MC-WV-101", "Evergreen WebView2 Runtime is not installed");
            var install = MessageBox.Show(
                this,
                UiText("dialog.webview.runtime.required"),
                UiText("app.title"),
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Information);
            if (install != DialogResult.Yes)
                throw new InvalidOperationException("MC-WV-101 Microsoft Edge WebView2 Runtime is required.");

            DiagnosticsState.SetStartupPhase("webview2_runtime_install");
            await InstallEvergreenRuntimeAsync();
            version = await WaitForEvergreenRuntimeAsync();
            if (string.IsNullOrWhiteSpace(version))
            {
                DiagnosticsState.SetLastCode("MC-WV-102", "Evergreen bootstrapper completed but no compatible runtime was detected");
                throw new InvalidOperationException("MC-WV-102 The WebView2 Runtime was not available after installation.");
            }
        }

        var userDataFolder = Path.Combine(session.Paths.RootDirectory, "webview2-user-data");
        Directory.CreateDirectory(userDataFolder);
        var webRoot = await Task.Run(() => Path.Combine(PackagedAssets.ResolveRequiredAssetRoot(session.Paths, "web", session.Log), "web"));
        DiagnosticsState.SetWebView2Runtime($"evergreen version={version}");
        return new WebRuntimeInfo(userDataFolder, webRoot, version);
    }

    private static CoreWebView2EnvironmentOptions CreateEvergreenEnvironmentOptions() => new()
    {
        ReleaseChannels = CoreWebView2ReleaseChannels.Stable,
        ExclusiveUserDataFolderAccess = false
    };

    private static string? TryGetEvergreenRuntimeVersion()
    {
        try
        {
            var version = CoreWebView2Environment.GetAvailableBrowserVersionString(
                null,
                CreateEvergreenEnvironmentOptions());
            return string.IsNullOrWhiteSpace(version) ? null : version;
        }
        catch (WebView2RuntimeNotFoundException)
        {
            return null;
        }
    }

    private static async Task<string?> WaitForEvergreenRuntimeAsync()
    {
        for (var attempt = 0; attempt != 10; ++attempt)
        {
            var version = await Task.Run(TryGetEvergreenRuntimeVersion);
            if (!string.IsNullOrWhiteSpace(version))
                return version;
            await Task.Delay(TimeSpan.FromSeconds(1));
        }
        return null;
    }

    private async Task InstallEvergreenRuntimeAsync()
    {
        var assetRoot = await Task.Run(() => PackagedAssets.ResolveRequiredAssetRoot(session.Paths, "webview2-bootstrapper", session.Log));
        var source = Path.Combine(assetRoot, "webview2-bootstrapper", EvergreenBootstrapperFileName);
        if (!File.Exists(source))
        {
            DiagnosticsState.SetLastCode("MC-WV-103", "embedded Evergreen bootstrapper is missing");
            throw new FileNotFoundException("MC-WV-103 The embedded WebView2 Evergreen bootstrapper is missing.", source);
        }

        var temporaryDirectory = Path.Combine(
            Path.GetTempPath(),
            "MecchaCamouflage",
            "webview2-bootstrapper",
            Guid.NewGuid().ToString("N"));
        var bootstrapperPath = Path.Combine(temporaryDirectory, EvergreenBootstrapperFileName);
        try
        {
            Directory.CreateDirectory(temporaryDirectory);
            File.Copy(source, bootstrapperPath, overwrite: true);
            var start = new ProcessStartInfo(bootstrapperPath)
            {
                UseShellExecute = false,
                CreateNoWindow = true
            };
            start.ArgumentList.Add("/silent");
            start.ArgumentList.Add("/install");
            using var process = Process.Start(start)
                ?? throw new IOException("MC-WV-104 The WebView2 Evergreen bootstrapper could not be started.");
            await process.WaitForExitAsync();
            if (process.ExitCode != 0)
            {
                DiagnosticsState.SetLastCode("MC-WV-104", $"Evergreen bootstrapper exit code {process.ExitCode}");
                throw new IOException($"MC-WV-104 The WebView2 Evergreen bootstrapper failed with exit code {process.ExitCode}.");
            }
        }
        finally
        {
            TryDeleteDirectory(temporaryDirectory);
        }
    }

    private static void TryDeleteDirectory(string path)
    {
        try
        {
            if (Directory.Exists(path))
                Directory.Delete(path, recursive: true);
        }
        catch (IOException)
        {
            // A security product can briefly retain the installer; the per-run directory is safe to clean later.
        }
        catch (UnauthorizedAccessException)
        {
            // Diagnostics should describe the startup error, not turn temporary-file cleanup into a new failure.
        }
    }

    private sealed record WebRuntimeInfo(string UserDataFolder, string WebRoot, string Version);

    private WebView2 CreateWebViewControl() => new() { Dock = DockStyle.Fill };

    private void StartBridgeWarmup()
    {
        if (!webReady)
            return;
        _ = Task.Run(async () =>
        {
            try
            {
                await session.WarmupBridgeAsync();
            }
            catch (OperationCanceledException)
            {
            }
        });
    }

    private void StartUiReadyTimeout()
    {
        CancelUiReadyTimeout();
        var cancellation = new CancellationTokenSource();
        uiReadyTimeoutCancellation = cancellation;
        _ = WaitForUiReadyAsync(cancellation);
    }

    private void CancelUiReadyTimeout()
    {
        var cancellation = uiReadyTimeoutCancellation;
        uiReadyTimeoutCancellation = null;
        cancellation?.Cancel();
    }

    private async Task WaitForUiReadyAsync(CancellationTokenSource cancellation)
    {
        try
        {
            await Task.Delay(UiReadyTimeout, cancellation.Token);
            if (IsDisposed || Disposing || webReady || !ReferenceEquals(uiReadyTimeoutCancellation, cancellation))
                return;
            uiReadyTimeoutCancellation = null;
            await RecoverWebViewAsync(
                "dialog.webview.failed.ready.timeout",
                new TimeoutException("MC-WV-204 Timed out waiting for the web interface uiReady signal."));
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            cancellation.Dispose();
        }
    }

    private async Task MarkUiReadyAsync(long generation)
    {
        if (!webViewStartup.IsCurrent(generation) || webViewRecoveryInProgress || webReady || IsDisposed || Disposing)
            return;
        webReady = true;
        CancelUiReadyTimeout();
        DiagnosticsState.SetStartupPhase("webview2_ui_ready");
        session.Log.Info("GUI: initialized.");
        DiagnosticsState.WriteLine("webview2", $"ui_ready generation={generation}");
        if (webViewStartup.MarkUiReady(generation))
            QueueWindowSettingsStabilization(generation);
        PostWebViewZoomFactor();
        StartBridgeWarmup();
        await PushSnapshotAsync();
    }

    private void PostWebViewZoomFactor()
    {
        var zoomFactor = webView?.ZoomFactor ?? 1.0;
        var percent = (int)Math.Round(zoomFactor * 100, MidpointRounding.AwayFromZero);
        PostEvent("zoomChanged", new { percent });
    }

    private void QueueWindowSettingsStabilization(long generation)
    {
        if (!IsHandleCreated || IsDisposed || Disposing)
            return;
        try
        {
            BeginInvoke((MethodInvoker)(() =>
            {
                if (IsDisposed || Disposing || !webReady || !webViewStartup.IsCurrent(generation))
                    return;
                ApplyWindowSettings("webview-ready-posted");
            }));
        }
        catch (InvalidOperationException) when (IsDisposed || Disposing || !IsHandleCreated)
        {
            // The form closed between the guard and BeginInvoke.
        }
    }

    private bool IsActiveWebView(long generation, object? sender) =>
        webViewStartup.IsCurrent(generation) && ReferenceEquals(sender, webView?.CoreWebView2);

    private async Task HandleWebViewInitializationFailureAsync(Exception exception)
    {
        await RecoverWebViewAsync("dialog.webview.failed.initialize", exception);
    }

    private async Task RecoverWebViewAsync(string userMessageKey, Exception exception)
    {
        if (webViewRecoveryInProgress || IsDisposed || Disposing)
            return;

        webViewRecoveryInProgress = true;
        webReady = false;
        CancelUiReadyTimeout();
        DiagnosticsState.RecordException("webview2_failed", exception);
        DiagnosticsState.SetLastCode(WebViewFailureCode(exception), exception.Message);
        session.Log.Error("WebView2: " + exception.Message);
        try
        {
            var retryAvailable = !webViewRetryUsed;
            var action = WebViewFailureDialog.Show(
                this,
                session.Localization,
                session.Settings.Language,
                UiText(userMessageKey),
                DiagnosticsState.Summary(session.Paths),
                ManualWebView2RuntimeUrl,
                retryAvailable);
            if (action != WebViewRecoveryAction.Retry)
            {
                Close();
                return;
            }

            webViewRetryUsed = true;
            try
            {
                await RecreateWebViewAsync();
            }
            catch (Exception retryException)
            {
                DiagnosticsState.RecordException("webview2_retry_failed", retryException);
                session.Log.Error("WebView2 retry: " + retryException.Message);
                WebViewFailureDialog.Show(
                    this,
                    session.Localization,
                    session.Settings.Language,
                    UiText("dialog.webview.failed.retry"),
                    DiagnosticsState.Summary(session.Paths),
                    ManualWebView2RuntimeUrl,
                    retryAvailable: false);
                Close();
            }
        }
        finally
        {
            webViewRecoveryInProgress = false;
        }
    }

    private static string WebViewFailureCode(Exception exception)
    {
        var firstToken = exception.Message
            .Split([' ', ':', '\r', '\n'], StringSplitOptions.RemoveEmptyEntries)
            .FirstOrDefault(token => token.StartsWith("MC-WV-", StringComparison.OrdinalIgnoreCase));
        return string.IsNullOrWhiteSpace(firstToken) ? "MC-WV-999" : firstToken;
    }

    private async Task RecreateWebViewAsync()
    {
        webReady = false;
        guiInitializedLogged = false;
        CancelUiReadyTimeout();
        var previous = webView;
        webView = null;
        if (previous is not null)
        {
            Controls.Remove(previous);
            previous.Dispose();
        }

        var replacement = CreateWebViewControl();
        webView = replacement;
        Controls.Add(replacement);
        replacement.BringToFront();
        await InitializeWebViewAsync();
    }

    private void HandleWebViewProcessFailed(object? sender, CoreWebView2ProcessFailedEventArgs args)
    {
        if (!ReferenceEquals(sender, webView?.CoreWebView2))
            return;

        var detail = BuildProcessFailureDetail(args);
        DiagnosticsState.SetStartupPhase("webview2_process_failed");
        DiagnosticsState.SetLastCode("MC-WV-301", detail);
        session.Log.Error("WebView2 process failed: " + detail);
        // RenderProcessUnresponsive can recover on its own, so it is diagnostic-only here.
        var requiresRecreate = args.ProcessFailedKind is
            CoreWebView2ProcessFailedKind.BrowserProcessExited or
            CoreWebView2ProcessFailedKind.RenderProcessExited or
            CoreWebView2ProcessFailedKind.UnknownProcessExited;
        if (!requiresRecreate || webViewRecoveryInProgress)
            return;

        if (InvokeRequired)
        {
            if (IsDisposed || Disposing || !IsHandleCreated)
                return;
            try
            {
                BeginInvoke((MethodInvoker)(() => HandleWebViewProcessFailed(sender, args)));
            }
            catch (InvalidOperationException) when (IsDisposed || Disposing || !IsHandleCreated)
            {
                // The form closed between the guard and BeginInvoke.
            }
            return;
        }
        _ = RecoverWebViewAsync(
            "dialog.webview.failed.browser.process",
            new InvalidOperationException("MC-WV-301 " + detail));
    }

    private static string BuildProcessFailureDetail(CoreWebView2ProcessFailedEventArgs args)
    {
        var detail = $"{args.ProcessFailedKind} exit={args.ExitCode}";
        try
        {
            detail += $" reason={args.Reason}";
            if (!string.IsNullOrWhiteSpace(args.ProcessDescription))
                detail += $" description={args.ProcessDescription}";
            if (!string.IsNullOrWhiteSpace(args.FailureSourceModulePath))
                detail += $" module={args.FailureSourceModulePath}";
        }
        catch
        {
            // These extended diagnostics are optional on older Evergreen runtimes.
        }
        return detail;
    }

    private void HandleNavigationStarting(long generation, object? sender, CoreWebView2NavigationStartingEventArgs args)
    {
        if (!IsActiveWebView(generation, sender))
            return;
        if (webViewRecoveryInProgress)
        {
            args.Cancel = true;
            return;
        }
        if (IsStartupUri(args.Uri))
        {
            var registered = webViewStartup.RegisterInitialNavigation(generation, args.NavigationId);
            DiagnosticsState.SetStartupPhase("webview2_navigation_starting");
            DiagnosticsState.WriteLine("webview2", $"navigation_starting generation={generation} id={args.NavigationId} startup={registered} uri={args.Uri}");
            return;
        }
        if (IsInternalUri(args.Uri))
        {
            DiagnosticsState.WriteLine("webview2", $"navigation_starting generation={generation} id={args.NavigationId} startup=false uri={args.Uri}");
            return;
        }
        args.Cancel = true;
        DiagnosticsState.WriteLine("webview2", $"navigation_blocked generation={generation} id={args.NavigationId} uri={args.Uri}");
        OpenExternal(args.Uri);
    }

    private static void HandleNewWindowRequested(CoreWebView2NewWindowRequestedEventArgs args)
    {
        args.Handled = true;
        OpenExternal(args.Uri);
    }

    private static bool IsInternalUri(string uri) =>
        Uri.TryCreate(uri, UriKind.Absolute, out var parsed) &&
        parsed.Scheme == Uri.UriSchemeHttps &&
        string.Equals(parsed.Host, WebUiHostName, StringComparison.OrdinalIgnoreCase);

    private static bool IsStartupUri(string uri) =>
        string.Equals(uri, WebUiStartUri, StringComparison.OrdinalIgnoreCase);

    private static void OpenExternal(string uri)
    {
        if (!Uri.TryCreate(uri, UriKind.Absolute, out var parsed) ||
            (parsed.Scheme != Uri.UriSchemeHttp && parsed.Scheme != Uri.UriSchemeHttps))
        {
            return;
        }
        Process.Start(new ProcessStartInfo(parsed.ToString()) { UseShellExecute = true });
    }

    private async Task HandleWebMessageAsync(long generation, object? sender, CoreWebView2WebMessageReceivedEventArgs args)
    {
        if (!IsActiveWebView(generation, sender) || webViewRecoveryInProgress)
            return;
        if (!IsInternalUri(args.Source))
        {
            session.Log.Warn("GUI: ignored a web message from an untrusted origin.");
            DiagnosticsState.WriteLine("webview2", $"web_message_rejected generation={generation} source={args.Source}");
            return;
        }
        HostWebCommand? command = null;
        try
        {
            if (TryGetUiStartupFailure(args.WebMessageAsJson, out var startupFailure))
            {
                await HandleUiStartupFailureAsync(generation, startupFailure);
                return;
            }
            if (IsUiReadyMessage(args.WebMessageAsJson))
            {
                await MarkUiReadyAsync(generation);
                return;
            }
            command = JsonSerializer.Deserialize<HostWebCommand>(args.WebMessageAsJson, JsonOptions);
            if (command is null)
                return;
            var result = await ExecuteCommandAsync(command);
            PostResponse(command.Id, true, result);
        }
        catch (Exception ex)
        {
            PostResponse(command?.Id ?? "", false, new { message = ex.Message });
        }
    }

    private static bool IsUiReadyMessage(string message)
    {
        using var document = JsonDocument.Parse(message);
        return document.RootElement.ValueKind == JsonValueKind.Object &&
            document.RootElement.TryGetProperty("type", out var type) &&
            string.Equals(type.GetString(), "uiReady", StringComparison.Ordinal);
    }

    private static bool TryGetUiStartupFailure(string message, out string detail)
    {
        detail = "";
        using var document = JsonDocument.Parse(message);
        if (document.RootElement.ValueKind != JsonValueKind.Object ||
            !document.RootElement.TryGetProperty("type", out var type) ||
            !string.Equals(type.GetString(), "uiStartupFailure", StringComparison.Ordinal))
        {
            return false;
        }

        var kind = document.RootElement.TryGetProperty("kind", out var kindValue) ? kindValue.GetString() : "javascript";
        var messageValue = document.RootElement.TryGetProperty("message", out var detailValue) ? detailValue.GetString() : "unknown JavaScript startup error";
        detail = $"{kind}: {messageValue}";
        return true;
    }

    private async Task HandleUiStartupFailureAsync(long generation, string detail)
    {
        DiagnosticsState.SetLastCode("MC-WV-205", detail);
        session.Log.Error("GUI startup JavaScript failure: " + detail);
        DiagnosticsState.WriteLine("webview2", $"ui_startup_failure generation={generation} detail={detail}");
        if (webReady)
            return;
        await RecoverWebViewAsync(
            "dialog.webview.failed.starting",
            new InvalidOperationException("MC-WV-205 " + detail));
    }

    private async Task<object?> ExecuteCommandAsync(HostWebCommand command)
    {
        switch (command.Command)
        {
            case "getSnapshot":
                return await session.GetSnapshotAsync();
            case "updateSetting":
                return HandleUpdateSetting(command.Payload);
            case "updateSettings":
                return HandleUpdateSettings(command.Payload);
            case "resetSetting":
                return HandleResetSetting(command.Payload);
            case "resetSection":
                return HandleResetSection(command.Payload);
            case "resetAllSettings":
                return HandleResetAllSettings();
            case "openLogs":
                session.OpenLogs();
                return new { success = true };
            case "copyLogs":
                Clipboard.SetText(session.ClipboardLogText());
                return new { success = true };
            case "setEditing":
                settingsEditing = command.Payload.GetProperty("editing").GetBoolean();
                if (!settingsEditing)
                {
                    hotkeyRecording = false;
                    ApplyWindowSettings();
                }
                return new { success = true };
            case "setHotkeyRecording":
                hotkeyRecording = command.Payload.GetProperty("recording").GetBoolean();
                return new { success = true };
            case "setImageDesignDraftState":
                if (!command.Payload.TryGetProperty("dirty", out var dirtyPayload) ||
                    dirtyPayload.ValueKind is not (JsonValueKind.True or JsonValueKind.False))
                {
                    return new { success = false, message = "Image design draft state is invalid." };
                }
                session.SetImageDesignDraftDirty(dirtyPayload.GetBoolean());
                return new { success = true };
            case "previewWindow":
                HandlePreviewWindow(command.Payload);
                return new { success = true };
            case "setWindowState":
                PersistWindowSnapshot();
                return new { success = true };
            case "getActiveImage":
                return new { success = true, design = session.GetImageDesignMetadata() };
            case "getImageGuide":
                return await HandleGetImageGuideAsync(command.Payload);
            case "getImageAssetChunk":
                return HandleGetImageAssetChunk(command.Payload);
            case "getLoadedImagePresetChunk":
                return HandleGetLoadedImagePresetChunk(command.Payload);
            case "stageImageDesignChunk":
                return HandleStageImageDesignChunk(command.Payload);
            case "commitSettingsWithImage":
                return HandleCommitSettingsWithImage(command.Payload);
            case "loadImagePreset":
                return HandleLoadImagePreset();
            case "saveImagePreset":
                return HandleSaveImagePreset(command.Payload);
            case "paint":
                return ApplyResult(await RunPaintCommandAsync(previewOnly: false, unpreviewOnly: false));
            case "preview":
                return ApplyResult(await RunPaintCommandAsync(previewOnly: true, unpreviewOnly: false));
            case "unpreview":
                return ApplyResult(await RunPaintCommandAsync(previewOnly: false, unpreviewOnly: true));
            case "stop":
                return ApplyResult(await session.StopPaintAsync());
            case "imagePaint":
                return ApplyResult(await RunPaintCommandAsync(PaintKind.Image, previewOnly: false, unpreviewOnly: false));
            case "imagePreview":
                return ApplyResult(await RunPaintCommandAsync(PaintKind.Image, previewOnly: true, unpreviewOnly: false));
            case "imageUnpreview":
                return ApplyResult(await RunPaintCommandAsync(PaintKind.Image, previewOnly: false, unpreviewOnly: true));
            case "imageStop":
                return ApplyResult(await session.StopPaintAsync());
            default:
                return new { success = false, message = "Unknown command: " + command.Command };
        }
    }

    private async Task<object> HandleGetImageGuideAsync(JsonElement payload)
    {
        var bodyType = "round";
        if (payload.TryGetProperty("bodyType", out var bodyTypePayload) &&
            bodyTypePayload.ValueKind == JsonValueKind.String &&
            string.Equals(bodyTypePayload.GetString(), "cube", StringComparison.OrdinalIgnoreCase))
        {
            bodyType = "cube";
        }
        return await session.GetImageGuideAsync(bodyType);
    }

    private object HandleCommitSettingsWithImage(JsonElement payload)
    {
        if (!settingsEditing)
            return new { success = false, message = "Open Edit before changing Image Paint." };
        if (!TryReadSettingChanges(payload, out var changes, out var settingsMessage))
            return new { success = false, message = settingsMessage };
        if (!TryReadImageDesign(payload, out var design, out var imageMessage))
            return new { success = false, message = imageMessage };
        var result = session.CommitSettingsWithImage(changes, design);
        return new
        {
            success = result.Success,
            message = result.Message,
            revision = result.Success ? session.Settings.Image.Revision : 0
        };
    }

    private object HandleLoadImagePreset()
    {
        if (!settingsEditing)
            return new { success = false, message = "Open Edit before loading a preset." };

        Directory.CreateDirectory(session.Paths.ImagePresetsDirectory);
        using var dialog = new OpenFileDialog
        {
            Title = UiText("dialog.preset.load.title"),
            InitialDirectory = session.Paths.ImagePresetsDirectory,
            Filter = UiText("dialog.preset.filter"),
            DefaultExt = ImagePresetStore.PresetExtension.TrimStart('.'),
            Multiselect = false,
            CheckFileExists = true,
            CheckPathExists = true
        };
        if (dialog.ShowDialog(this) != DialogResult.OK)
            return new { success = true, cancelled = true };
        if (!session.TryLoadImagePreset(dialog.FileName, out var design, out var message))
            return new { success = false, message };

        PruneExpiredImageTransfers();
        var transferId = Guid.NewGuid().ToString("D");
        loadedImagePresets[transferId] = new LoadedImagePresetTransfer { Design = design };
        return new { success = true, transferId, design = WithoutImageAssets(design) };
    }

    private object HandleSaveImagePreset(JsonElement payload)
    {
        if (!settingsEditing)
            return new { success = false, message = "Open Edit before saving a preset." };
        if (!TryReadImageDesign(payload, out var design, out var message))
            return new { success = false, message };

        Directory.CreateDirectory(session.Paths.ImagePresetsDirectory);
        using var dialog = new SaveFileDialog
        {
            Title = UiText("dialog.preset.save.title"),
            InitialDirectory = session.Paths.ImagePresetsDirectory,
            Filter = UiText("dialog.preset.filter"),
            DefaultExt = ImagePresetStore.PresetExtension.TrimStart('.'),
            AddExtension = true,
            OverwritePrompt = true,
            CheckPathExists = true
        };
        if (dialog.ShowDialog(this) != DialogResult.OK)
            return new { success = true, cancelled = true };
        var result = session.SaveImagePreset(dialog.FileName, design);
        return new { success = result.Success, message = result.Message, path = result.Success ? result.Message : "" };
    }

    private bool TryReadImageDesign(JsonElement payload, out ImagePaintSettings design, out string message)
    {
        design = new ImagePaintSettings();
        message = "";
        if (!payload.TryGetProperty("design", out var designPayload))
        {
            message = "The image design is missing.";
            return false;
        }
        try
        {
            design = designPayload.Deserialize<ImagePaintSettings>(JsonOptions) ?? new ImagePaintSettings();
        }
        catch (JsonException)
        {
            message = "The image design is invalid.";
            return false;
        }
        if (design.Enabled && !TryTakeStagedImageDesign(payload, design, out message))
            return false;
        return true;
    }

    private object HandleGetImageAssetChunk(JsonElement payload)
    {
        if (!TryReadImageAssetRequest(payload, out var asset, out var index, out var message))
            return new { success = false, message };
        var data = "";
        var available = session.TryGetImageDesignAsset(asset, out data);
        if (!available)
            return new { success = false, message = "The requested active image asset is unavailable." };
        return ImageAssetChunk(data, index);
    }

    private object HandleGetLoadedImagePresetChunk(JsonElement payload)
    {
        if (!TryReadImageAssetRequest(payload, out var asset, out var index, out var message) ||
            !TryReadTransferId(payload, out var transferId, out message))
        {
            return new { success = false, message = message ?? "The preset asset request is invalid." };
        }
        PruneExpiredImageTransfers();
        if (!loadedImagePresets.TryGetValue(transferId, out var transfer) ||
            !TryGetImageAssetBase64(transfer.Design, asset, out var data))
        {
            return new { success = false, message = "The loaded preset is no longer available." };
        }
        transfer.LastUpdated = DateTimeOffset.UtcNow;
        return ImageAssetChunk(data, index);
    }

    private static object ImageAssetChunk(string data, int index)
    {
        if (index > data.Length / ImageTransferChunkCharacters + 1)
            return new { success = false, message = "The requested image chunk is unavailable." };
        var offset = index * ImageTransferChunkCharacters;
        if (offset >= data.Length)
            return new { success = false, message = "The requested image chunk is unavailable." };
        var length = Math.Min(ImageTransferChunkCharacters, data.Length - offset);
        return new { success = true, index, data = data.Substring(offset, length), complete = offset + length >= data.Length };
    }

    private object HandleStageImageDesignChunk(JsonElement payload)
    {
        if (!settingsEditing)
            return new { success = false, message = "Open Edit before uploading an image design." };
        if (!TryReadImageAssetRequest(payload, out var asset, out var index, out var message) ||
            !TryReadTransferId(payload, out var transferId, out message) ||
            !payload.TryGetProperty("data", out var dataPayload) ||
            dataPayload.ValueKind != JsonValueKind.String)
        {
            return new { success = false, message = message ?? "The image chunk is invalid." };
        }
        var data = dataPayload.GetString() ?? "";
        if (data.Length == 0 || data.Length > ImageTransferChunkCharacters)
            return new { success = false, message = "The image chunk has an invalid size." };

        PruneExpiredImageTransfers();
        if (!stagedImageDesigns.TryGetValue(transferId, out var transfer))
        {
            if (stagedImageDesigns.Count >= 16)
                return new { success = false, message = "Too many pending image uploads. Try saving again." };
            transfer = new StagedImageDesignTransfer();
            stagedImageDesigns.Add(transferId, transfer);
        }
        if (!transfer.Assets.TryGetValue(asset, out var staged))
        {
            staged = new StagedImageAsset();
            transfer.Assets.Add(asset, staged);
        }
        if (index != staged.NextChunkIndex)
            return new { success = false, message = "The image chunks arrived out of order." };
        if (staged.Data.Length > MaximumImageTransferCharacters - data.Length)
            return new { success = false, message = "The staged image is too large." };
        staged.Data.Append(data);
        staged.NextChunkIndex++;
        transfer.LastUpdated = DateTimeOffset.UtcNow;
        return new { success = true, index };
    }

    private bool TryTakeStagedImageDesign(JsonElement payload, ImagePaintSettings design, out string message)
    {
        message = "";
        if (!TryReadTransferId(payload, out var transferId, out message) ||
            !stagedImageDesigns.Remove(transferId, out var transfer))
        {
            message = string.IsNullOrWhiteSpace(message) ? "The image upload was not found. Save the design again." : message;
            return false;
        }
        if (!transfer.Assets.TryGetValue("canvas", out var canvas))
        {
            message = "The canonical image canvas is missing.";
            return false;
        }
        design.CanvasRgbaBase64 = canvas.Data.ToString();
        for (var index = 0; index < design.Layers.Count; index++)
        {
            if (!transfer.Assets.TryGetValue($"layer{index}", out var layer))
            {
                message = $"Image layer {index + 1} is missing.";
                return false;
            }
            design.Layers[index].DataBase64 = layer.Data.ToString();
        }
        return true;
    }

    private static bool TryReadImageAssetRequest(JsonElement payload, out string asset, out int index, out string? message)
    {
        asset = "";
        index = -1;
        message = null;
        if (!payload.TryGetProperty("asset", out var assetPayload) || assetPayload.ValueKind != JsonValueKind.String)
        {
            message = "The image asset is missing.";
            return false;
        }
        asset = assetPayload.GetString() ?? "";
        if (asset != "canvas" &&
            (!asset.StartsWith("layer", StringComparison.Ordinal) ||
             !int.TryParse(asset["layer".Length..], out var layerIndex) ||
             layerIndex < 0 || layerIndex > 4095))
        {
            message = "The requested image asset is invalid.";
            return false;
        }
        if (!payload.TryGetProperty("index", out var indexPayload) || !indexPayload.TryGetInt32(out index) || index < 0)
        {
            message = "The image chunk index is invalid.";
            return false;
        }
        return true;
    }

    private static bool TryReadTransferId(JsonElement payload, out string transferId, out string message)
    {
        transferId = "";
        message = "";
        if (!payload.TryGetProperty("transferId", out var transferPayload) ||
            transferPayload.ValueKind != JsonValueKind.String ||
            !Guid.TryParse(transferPayload.GetString(), out var transfer))
        {
            message = "The image upload id is invalid.";
            return false;
        }
        transferId = transfer.ToString("D");
        return true;
    }

    private void PruneExpiredImageTransfers()
    {
        var cutoff = DateTimeOffset.UtcNow - ImageTransferLifetime;
        foreach (var transferId in stagedImageDesigns
                     .Where(entry => entry.Value.LastUpdated < cutoff)
                     .Select(entry => entry.Key)
                     .ToArray())
        {
            stagedImageDesigns.Remove(transferId);
        }
        foreach (var transferId in loadedImagePresets
                     .Where(entry => entry.Value.LastUpdated < cutoff)
                     .Select(entry => entry.Key)
                     .ToArray())
        {
            loadedImagePresets.Remove(transferId);
        }
    }

    private static ImagePaintSettings WithoutImageAssets(ImagePaintSettings source)
    {
        var clone = JsonSerializer.Deserialize<ImagePaintSettings>(JsonSerializer.Serialize(source, JsonOptions), JsonOptions)
                    ?? new ImagePaintSettings();
        clone.CanvasRgbaBase64 = "";
        foreach (var layer in clone.Layers)
            layer.DataBase64 = "";
        return clone;
    }

    private static bool TryGetImageAssetBase64(ImagePaintSettings design, string asset, out string data)
    {
        data = "";
        if (asset == "canvas")
        {
            data = design.CanvasRgbaBase64;
            return data.Length > 0;
        }
        if (!asset.StartsWith("layer", StringComparison.Ordinal) ||
            !int.TryParse(asset["layer".Length..], out var index) ||
            index < 0 || index >= design.Layers.Count)
        {
            return false;
        }
        data = design.Layers[index].DataBase64;
        return data.Length > 0;
    }

    private async Task<HostCommandResult> RunPaintCommandAsync(PaintKind kind = PaintKind.Standard, bool previewOnly = false, bool unpreviewOnly = false)
    {
        var previousInterval = statusTimer.Interval;
        statusTimer.Interval = 250;
        using var refresh = new CancellationTokenSource();
        var refreshTask = RefreshSnapshotsUntilCancelledAsync(refresh.Token);
        try
        {
            return await session.RunPaintAsync(kind, previewOnly, unpreviewOnly);
        }
        finally
        {
            refresh.Cancel();
            try
            {
                await refreshTask;
            }
            catch (OperationCanceledException)
            {
            }
            statusTimer.Interval = session.PaintRunning ? 500 : previousInterval;
            await PushSnapshotAsync();
        }
    }

    private async Task RefreshSnapshotsUntilCancelledAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            await Task.Delay(250, cancellationToken);
            await PushSnapshotAsync();
        }
    }

    private object HandleUpdateSetting(JsonElement payload)
    {
        var key = payload.GetProperty("key").GetString() ?? "";
        var result = session.UpdateSetting(key, payload.GetProperty("value"));
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private object HandleUpdateSettings(JsonElement payload)
    {
        if (!TryReadSettingChanges(payload, out var changes, out var message))
            return new { success = false, message };
        var result = session.UpdateSettings(changes);
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private static bool TryReadSettingChanges(JsonElement payload, out List<SettingChange> changes, out string message)
    {
        changes = [];
        message = "";
        if (!payload.TryGetProperty("changes", out var entries) || entries.ValueKind != JsonValueKind.Array)
        {
            message = "The setting changes are missing.";
            return false;
        }
        try
        {
            foreach (var item in entries.EnumerateArray())
            {
                var key = item.GetProperty("key").GetString() ?? "";
                changes.Add(new SettingChange(key, item.GetProperty("value")));
            }
            return true;
        }
        catch (InvalidOperationException)
        {
            message = "A setting change is invalid.";
            return false;
        }
    }

    private object HandleResetSetting(JsonElement payload)
    {
        var key = payload.GetProperty("key").GetString() ?? "";
        var result = session.ResetSetting(key);
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private object HandleResetSection(JsonElement payload)
    {
        var section = payload.GetProperty("section").GetString() ?? "";
        var result = session.ResetSection(section);
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private object HandleResetAllSettings()
    {
        var result = session.ResetAllSettings();
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private object ApplyResult(HostCommandResult result)
    {
        ApplyWindowSettings();
        _ = PushSnapshotAsync();
        return new { success = result.Success, message = result.Message };
    }

    private void HandlePreviewWindow(JsonElement payload)
    {
        if (payload.TryGetProperty("opacity", out var opacityValue) && opacityValue.TryGetDouble(out var opacity))
            Opacity = Math.Clamp(opacity, 0.35, 1.0);
    }

    private async Task HandleHotkeyAsync(int id)
    {
        switch (id)
        {
            case HotkeyStart:
                _ = await RunPaintCommandAsync(previewOnly: false, unpreviewOnly: false);
                break;
            case HotkeyPreview:
                _ = await RunPaintCommandAsync(previewOnly: true, unpreviewOnly: false);
                break;
            case HotkeyUnPreview:
                _ = await RunPaintCommandAsync(previewOnly: false, unpreviewOnly: true);
                break;
            case HotkeyStop:
                _ = await session.StopPaintAsync();
                break;
            case HotkeyImageStart:
                _ = await RunPaintCommandAsync(PaintKind.Image, previewOnly: false, unpreviewOnly: false);
                break;
            case HotkeyImagePreview:
                _ = await RunPaintCommandAsync(PaintKind.Image, previewOnly: true, unpreviewOnly: false);
                break;
            case HotkeyImageUnPreview:
                _ = await RunPaintCommandAsync(PaintKind.Image, previewOnly: false, unpreviewOnly: true);
                break;
            case HotkeyImageStop:
                _ = await session.StopPaintAsync();
                break;
        }
        await PushSnapshotAsync();
    }

    private void ApplyWindowSettings(string? stage = null)
    {
        Opacity = session.Settings.Opacity;
        var topMost = session.Settings.AlwaysOnTop;
        bool? nativeTopMostBefore = IsHandleCreated ? IsNativeTopMost() : null;
        TopMost = topMost;
        var setWindowPosSucceeded = true;
        var setWindowPosError = 0;
        if (IsHandleCreated)
        {
            setWindowPosSucceeded = SetWindowPos(
                Handle,
                topMost ? HwndTopMost : HwndNoTopMost,
                0,
                0,
                0,
                0,
                SwpNoMove | SwpNoSize | SwpNoActivate | SwpNoOwnerZOrder);
            if (!setWindowPosSucceeded)
                setWindowPosError = Marshal.GetLastWin32Error();
        }
        ApplyDarkTitleBar();
        if (!string.IsNullOrWhiteSpace(stage))
            RecordWindowSettingsState(stage, topMost, nativeTopMostBefore, setWindowPosSucceeded, setWindowPosError);
    }

    private void RecordWindowSettingsState(string stage, bool requestedTopMost, bool? nativeTopMostBefore, bool setWindowPosSucceeded, int setWindowPosError)
    {
        if (!IsHandleCreated || IsDisposed || Disposing)
            return;

        var nativeTopMost = IsNativeTopMost();
        var state =
            $"stage={stage} requested_topmost={requestedTopMost} managed_topmost={TopMost} native_topmost_before={nativeTopMostBefore?.ToString() ?? "n/a"} native_topmost={nativeTopMost} " +
            $"visible={Visible} setwindowpos={setWindowPosSucceeded}";
        if (!setWindowPosSucceeded)
        {
            state += $" win32={setWindowPosError}";
            DiagnosticsState.SetLastCode("MC-WIN-101", state);
            session.Log.Warn("Window: " + state);
            return;
        }

        DiagnosticsState.WriteLine("window", state);
        if (nativeTopMost != requestedTopMost)
            session.Log.Warn("Window: " + state);
        else
            session.Log.Info("Window: " + state);
    }

    private bool IsNativeTopMost() => (GetWindowLong(Handle, GwlExStyle) & WsExTopMost) != 0;

    private void ApplyDarkTitleBar()
    {
        if (!IsHandleCreated || !OperatingSystem.IsWindowsVersionAtLeast(10, 0, 17763))
            return;
        try
        {
            var dark = 1;
            _ = DwmSetWindowAttribute(Handle, DwmwaUseImmersiveDarkMode, ref dark, sizeof(int));
            if (OperatingSystem.IsWindowsVersionAtLeast(10, 0, 22000))
            {
                var caption = ColorRef(Color.FromArgb(32, 32, 32));
                var text = ColorRef(Color.White);
                _ = DwmSetWindowAttribute(Handle, DwmwaCaptionColor, ref caption, sizeof(int));
                _ = DwmSetWindowAttribute(Handle, DwmwaTextColor, ref text, sizeof(int));
            }
        }
        catch
        {
            // Non-client dark mode is best-effort and may be unavailable on older Windows builds.
        }
    }

    private static int ColorRef(Color color) => color.R | (color.G << 8) | (color.B << 16);

    private static Icon? LoadWindowIcon()
    {
        var packagedIcon = Path.Combine(AppContext.BaseDirectory, "web", "icon.ico");
        if (File.Exists(packagedIcon))
            return new Icon(packagedIcon);
        return Icon.ExtractAssociatedIcon(Application.ExecutablePath);
    }

    private async Task PushSnapshotAsync()
    {
        var core = webView?.CoreWebView2;
        if (!webReady || core is null)
            return;
        var snapshot = await session.GetSnapshotAsync();
        PostEvent("snapshotChanged", snapshot);
    }

    private void PushSnapshotFromAnyThread()
    {
        if (IsDisposed || !IsHandleCreated)
            return;
        try
        {
            if (InvokeRequired)
            {
                BeginInvoke((MethodInvoker)(async () =>
                {
                    await PushSnapshotAsync();
                }));
                return;
            }
            _ = PushSnapshotAsync();
        }
        catch (InvalidOperationException)
        {
        }
    }

    private void PostResponse(string id, bool ok, object? data)
    {
        var core = webView?.CoreWebView2;
        if (!webReady || core is null)
            return;
        var json = JsonSerializer.Serialize(new { type = "response", id, ok, data }, JsonOptions);
        core.PostWebMessageAsJson(json);
    }

    private void PostEvent(string name, object data)
    {
        var core = webView?.CoreWebView2;
        if (!webReady || core is null)
            return;
        var json = JsonSerializer.Serialize(new { type = "event", name, data }, JsonOptions);
        core.PostWebMessageAsJson(json);
    }

    private void SendToast(string message, string level)
    {
        if (webReady && webView?.CoreWebView2 is not null)
            PostEvent("toast", new { message, level });
    }

    private void PersistWindowSnapshot()
    {
        if (!IsHandleCreated)
            return;
        session.SetWindowSnapshot(Width, Height, Left, Top);
    }

    private void HandleRawKeyboardInput(IntPtr rawInputHandle)
    {
        var size = (uint)Marshal.SizeOf<RawInput>();
        var headerSize = (uint)Marshal.SizeOf<RawInputHeader>();
        if (GetRawInputData(rawInputHandle, RidInput, out var input, ref size, headerSize) == uint.MaxValue ||
            input.Header.Type != RimTypeKeyboard)
        {
            return;
        }

        var virtualKey = (uint)input.Keyboard.VirtualKey;
        if (input.Keyboard.Message is WmKeyUp or WmSystemKeyUp)
        {
            hotkeyKeyState.EndPress(virtualKey);
            return;
        }
        if (input.Keyboard.Message is not (WmKeyDown or WmSystemKeyDown) ||
            !hotkeyKeyState.TryBeginPress(virtualKey))
        {
            return;
        }
        if (!session.Runtime.IsConnected)
            return;

        var hotkeyId = ResolveHotkeyId(virtualKey);
        if (hotkeyId == 0)
            return;
        if (settingsEditing && !IsStopHotkey(hotkeyId))
        {
            if (!hotkeyRecording)
                SendToast(session.Localization.Text(session.Settings.Language, "toast.editing.hotkey.blocked"), "warn");
            return;
        }
        _ = HandleHotkeyAsync(hotkeyId);
    }

    private static bool IsStopHotkey(int hotkeyId) =>
        hotkeyId is HotkeyStop or HotkeyImageStop;

    private int ResolveHotkeyId(uint virtualKey)
    {
        var keys = new[]
        {
            (HotkeyStart, session.Settings.StartHotkey),
            (HotkeyPreview, session.Settings.PreviewHotkey),
            (HotkeyUnPreview, session.Settings.UnPreviewHotkey),
            (HotkeyStop, session.Settings.StopHotkey),
            (HotkeyImageStart, session.Settings.ImageStartHotkey),
            (HotkeyImagePreview, session.Settings.ImagePreviewHotkey),
            (HotkeyImageUnPreview, session.Settings.ImageUnPreviewHotkey),
            (HotkeyImageStop, session.Settings.ImageStopHotkey)
        };
        foreach (var (id, key) in keys)
        {
            if (ParseVirtualKey(key) == virtualKey)
                return id;
        }
        return 0;
    }

    private bool TryRegisterRawKeyboardInput(out string message)
    {
        message = "";
        if (!IsHandleCreated)
        {
            message = "Raw keyboard registration failed: form handle is unavailable.";
            return false;
        }
        var devices = new[]
        {
            new RawInputDevice
            {
                UsagePage = HidUsagePageGeneric,
                Usage = HidUsageGenericKeyboard,
                Flags = RidevInputSink,
                Target = Handle
            }
        };
        if (!RegisterRawInputDevices(devices, 1, (uint)Marshal.SizeOf<RawInputDevice>()))
        {
            message = $"Raw keyboard registration failed (Win32 error {Marshal.GetLastWin32Error()}).";
            rawKeyboardRegistered = false;
            return false;
        }
        rawKeyboardRegistered = true;
        return true;
    }

    private void UnregisterRawKeyboardInput()
    {
        if (!rawKeyboardRegistered)
            return;
        var devices = new[]
        {
            new RawInputDevice
            {
                UsagePage = HidUsagePageGeneric,
                Usage = HidUsageGenericKeyboard,
                Flags = RidevRemove,
                Target = IntPtr.Zero
            }
        };
        RegisterRawInputDevices(devices, 1, (uint)Marshal.SizeOf<RawInputDevice>());
        rawKeyboardRegistered = false;
    }

    private static uint ParseVirtualKey(string text)
    {
        var value = HotkeySet.Normalize(text);
        if (value.Length >= 2 && value[0] == 'F' && int.TryParse(value[1..], out var f) && f is >= 1 and <= 24)
            return (uint)(0x70 + f - 1);
        return 0;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RawInputDevice
    {
        public ushort UsagePage;
        public ushort Usage;
        public uint Flags;
        public IntPtr Target;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RawInputHeader
    {
        public uint Type;
        public uint Size;
        public IntPtr Device;
        public IntPtr WParam;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RawKeyboard
    {
        public ushort MakeCode;
        public ushort Flags;
        public ushort Reserved;
        public ushort VirtualKey;
        public uint Message;
        public uint ExtraInformation;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RawInput
    {
        public RawInputHeader Header;
        public RawKeyboard Keyboard;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterRawInputDevices(
        [In] RawInputDevice[] devices,
        uint deviceCount,
        uint deviceSize);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetRawInputData(
        IntPtr rawInput,
        uint command,
        out RawInput data,
        ref uint size,
        uint headerSize);

    private const int DwmwaUseImmersiveDarkMode = 20;
    private const int DwmwaCaptionColor = 35;
    private const int DwmwaTextColor = 36;

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int attributeValue, int attributeSize);

    private static readonly IntPtr HwndTopMost = new(-1);
    private static readonly IntPtr HwndNoTopMost = new(-2);
    private const int GwlExStyle = -20;
    private const int WsExTopMost = 0x00000008;
    private const uint SwpNoSize = 0x0001;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpNoOwnerZOrder = 0x0200;

    [DllImport("user32.dll", EntryPoint = "GetWindowLongW", SetLastError = true)]
    private static extern int GetWindowLong(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool SetWindowPos(
        IntPtr hWnd,
        IntPtr hWndInsertAfter,
        int x,
        int y,
        int cx,
        int cy,
        uint flags);

    private sealed record HostWebCommand(string Id, string Command, JsonElement Payload);
}
