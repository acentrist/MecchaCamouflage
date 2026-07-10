using System.Runtime.InteropServices;
using System.Text.Json;
using System.Diagnostics;
using System.Drawing.Imaging;
using System.Security.Cryptography;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;
using MecchaCamouflage.Controller;

namespace MecchaCamouflage.WebHost;

public sealed class MainForm : Form
{
    private const int HotkeyStart = 1;
    private const int HotkeyPreview = 2;
    private const int HotkeyUnPreview = 3;
    private const int HotkeyStop = 4;
    private const int WmHotkey = 0x0312;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = false
    };

    private readonly HostSession session;
    private readonly WebView2 webView = new() { Dock = DockStyle.Fill };
    private readonly System.Windows.Forms.Timer statusTimer = new() { Interval = 2000 };
    private bool webReady;
    private bool guiInitializedLogged;
    private bool settingsEditing;
    private bool hotkeyRecording;

    public MainForm(HostSession session)
    {
        this.session = session;
        Text = "Meccha Camouflage";
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
        Controls.Add(webView);

        Shown += async (_, _) =>
        {
            try
            {
                StartBridgeWarmup();
                await InitializeWebViewAsync();
            }
            catch (Exception ex)
            {
                DiagnosticsState.RecordException("webview2_initialize_failed", ex);
                session.Log.Error(ex.Message);
                MessageBox.Show(
                    "Meccha Camouflage failed to start. Diagnostic logs were written to:" +
                    Environment.NewLine + session.Paths.DiagnosticsDirectory +
                    Environment.NewLine + Environment.NewLine + ex.Message,
                    "Meccha Camouflage",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                Close();
            }
        };
        FormClosing += (_, _) =>
        {
            PersistWindowSnapshot();
            UnregisterHotkeys();
        };
        ResizeEnd += (_, _) => PersistWindowSnapshot();
        Move += (_, _) => PersistWindowSnapshot();
        statusTimer.Tick += async (_, _) =>
        {
            statusTimer.Interval = session.PaintRunning ? 500 : 2000;
            StartBridgeWarmup();
            await PushSnapshotAsync();
        };
        session.Log.Changed += (_, _) => PushSnapshotFromAnyThread();
        statusTimer.Start();
    }

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        ApplyDarkTitleBar();
        ApplyWindowSettings();
        if (!TryRegisterHotkeys(out var message))
            session.Log.Warn(message);
    }

    protected override void WndProc(ref Message message)
    {
        if (message.Msg == WmHotkey)
        {
            if (settingsEditing)
            {
                if (!hotkeyRecording)
                    SendToast(session.Localization.Text(session.Settings.Language, "toast.editing.hotkey.blocked"), "warn");
                return;
            }
            _ = HandleHotkeyAsync(message.WParam.ToInt32());
            return;
        }
        base.WndProc(ref message);
    }

    private async Task InitializeWebViewAsync()
    {
        DiagnosticsState.SetStartupPhase("webview2_prepare");
        var runtime = await Task.Run(PrepareFixedWebRuntime);
        session.Log.Info("WebView2 runtime: initializing.");
        var environment = await CoreWebView2Environment.CreateAsync(runtime.BrowserExecutableFolder, runtime.UserDataFolder);
        await webView.EnsureCoreWebView2Async(environment);
        DiagnosticsState.SetStartupPhase("webview2_ready");
        session.Log.Info($"WebView2 runtime: initialized ({runtime.Version}).");
        if (!Directory.Exists(runtime.WebRoot))
            throw new DirectoryNotFoundException("Packaged web assets are missing: " + runtime.WebRoot);

        webView.CoreWebView2.WebMessageReceived += async (_, args) => await HandleWebMessageAsync(args);
        webView.CoreWebView2.NavigationCompleted += HandleNavigationCompleted;
        webView.CoreWebView2.NavigationStarting += (_, args) => HandleNavigationStarting(args);
        webView.CoreWebView2.NewWindowRequested += (_, args) => HandleNewWindowRequested(args);
        webView.CoreWebView2.SetVirtualHostNameToFolderMapping(
            "meccha.localhost",
            runtime.WebRoot,
            CoreWebView2HostResourceAccessKind.DenyCors);
        webView.CoreWebView2.Settings.AreDefaultScriptDialogsEnabled = true;
        webView.CoreWebView2.Settings.AreDefaultContextMenusEnabled = false;
        webView.CoreWebView2.Settings.AreBrowserAcceleratorKeysEnabled = false;
        webView.CoreWebView2.Settings.AreDevToolsEnabled =
            string.Equals(Environment.GetEnvironmentVariable("MECCHA_RESEARCH_ARTIFACTS"), "1", StringComparison.Ordinal);
        webView.CoreWebView2.Navigate("https://meccha.localhost/index.html");
        webReady = true;
        StartBridgeWarmup();
    }

    private async void HandleNavigationCompleted(object? sender, CoreWebView2NavigationCompletedEventArgs args)
    {
        try
        {
            ApplyWindowSettings();
            if (!args.IsSuccess)
            {
                session.Log.Error($"GUI: failed to load web assets ({args.WebErrorStatus}).");
                return;
            }
            if (guiInitializedLogged)
                return;
            guiInitializedLogged = true;
            session.Log.Info("GUI: initialized.");
            await PushSnapshotAsync();
        }
        catch (Exception ex)
        {
            DiagnosticsState.RecordException("gui_navigation_completed_failed", ex);
            session.Log.Error(ex.Message);
        }
    }

    private WebRuntimeInfo PrepareFixedWebRuntime()
    {
        var assetRoot = PackagedAssets.ResolveRequiredAssetRoot(session.Paths, "webview2", session.Log);
        var webView2Root = Path.Combine(assetRoot, "webview2");
        var executable = Directory.EnumerateFiles(webView2Root, "msedgewebview2.exe", SearchOption.AllDirectories)
            .Order(StringComparer.OrdinalIgnoreCase)
            .FirstOrDefault();
        if (string.IsNullOrWhiteSpace(executable))
        {
            DiagnosticsState.SetLastCode("MC-WV-001", "msedgewebview2.exe missing from fixed runtime");
            throw new FileNotFoundException("MC-WV-001 Fixed WebView2 runtime is missing msedgewebview2.exe.");
        }

        var browserExecutableFolder = Path.GetDirectoryName(executable)!;
        ApplyWindows10WebView2Acl(browserExecutableFolder);
        var version = CoreWebView2Environment.GetAvailableBrowserVersionString(browserExecutableFolder);
        var userDataFolder = Path.Combine(session.Paths.RuntimeDirectory, "webview2-user-data");
        Directory.CreateDirectory(userDataFolder);
        var webRoot = Path.Combine(PackagedAssets.ResolveRequiredAssetRoot(session.Paths, "web", session.Log), "web");
        DiagnosticsState.SetWebView2Runtime($"fixed version={version} path={browserExecutableFolder}");
        return new WebRuntimeInfo(browserExecutableFolder, userDataFolder, webRoot, version);
    }

    private sealed record WebRuntimeInfo(string BrowserExecutableFolder, string UserDataFolder, string WebRoot, string Version);

    private static void ApplyWindows10WebView2Acl(string browserExecutableFolder)
    {
        if (!OperatingSystem.IsWindows())
            return;
        var version = Environment.OSVersion.Version;
        if (version.Major != 10 || version.Build >= 22000)
            return;

        RunIcaclsGrant(browserExecutableFolder, "*S-1-15-2-2:(OI)(CI)(RX)");
        RunIcaclsGrant(browserExecutableFolder, "*S-1-15-2-1:(OI)(CI)(RX)");
    }

    private static void RunIcaclsGrant(string path, string grant)
    {
        var start = new ProcessStartInfo("icacls.exe")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        start.ArgumentList.Add(path);
        start.ArgumentList.Add("/grant");
        start.ArgumentList.Add(grant);
        using var process = Process.Start(start);
        if (process is null)
            throw new IOException("MC-WV-ACL-001 icacls.exe could not be started.");
        process.WaitForExit();
        var stdout = process.StandardOutput.ReadToEnd();
        var stderr = process.StandardError.ReadToEnd();
        if (process.ExitCode != 0)
        {
            DiagnosticsState.SetLastCode("MC-WV-ACL-001", stderr.Trim().Length == 0 ? stdout.Trim() : stderr.Trim());
            throw new IOException("MC-WV-ACL-001 Windows denied Fixed WebView2 runtime permissions.");
        }
    }

    private void StartBridgeWarmup()
    {
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

    private static void HandleNavigationStarting(CoreWebView2NavigationStartingEventArgs args)
    {
        if (IsInternalUri(args.Uri))
            return;
        args.Cancel = true;
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
        string.Equals(parsed.Host, "meccha.localhost", StringComparison.OrdinalIgnoreCase);

    private static void OpenExternal(string uri)
    {
        if (!Uri.TryCreate(uri, UriKind.Absolute, out var parsed) ||
            (parsed.Scheme != Uri.UriSchemeHttp && parsed.Scheme != Uri.UriSchemeHttps))
        {
            return;
        }
        Process.Start(new ProcessStartInfo(parsed.ToString()) { UseShellExecute = true });
    }

    private async Task HandleWebMessageAsync(CoreWebView2WebMessageReceivedEventArgs args)
    {
        HostWebCommand? command = null;
        try
        {
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
                return ApplyResult(session.ResetAllSettings());
            case "openLogs":
                session.OpenLogs();
                return new { success = true };
            case "copyLogs":
                Clipboard.SetText(session.ClipboardLogText());
                return new { success = true };
            case "selectLocalImage":
                return SelectAndNormalizeLocalImage();
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
            case "previewWindow":
                HandlePreviewWindow(command.Payload);
                return new { success = true };
            case "setWindowState":
                PersistWindowSnapshot();
                return new { success = true };
            case "paint":
                return ApplyResult(await RunPaintCommandAsync(previewOnly: false, unpreviewOnly: false));
            case "preview":
                return ApplyResult(await RunPaintCommandAsync(previewOnly: true, unpreviewOnly: false));
            case "unpreview":
                return ApplyResult(await RunPaintCommandAsync(previewOnly: false, unpreviewOnly: true));
            case "stop":
                return ApplyResult(await session.StopPaintAsync());
            default:
                return new { success = false, message = "Unknown command: " + command.Command };
        }
    }

    private async Task<HostCommandResult> RunPaintCommandAsync(bool previewOnly, bool unpreviewOnly)
    {
        var previousInterval = statusTimer.Interval;
        statusTimer.Interval = 250;
        using var refresh = new CancellationTokenSource();
        var refreshTask = RefreshSnapshotsUntilCancelledAsync(refresh.Token);
        try
        {
            return await session.RunPaintAsync(previewOnly, unpreviewOnly);
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
        var oldHotkeys = HotkeySet.From(session.Settings);
        var result = session.UpdateSetting(key, payload.GetProperty("value"));
        if (result.Success && key.Contains("Hotkey", StringComparison.OrdinalIgnoreCase) && !TryRegisterHotkeys(out var message))
        {
            oldHotkeys.ApplyTo(session.Settings);
            session.UpdateSetting("app.startHotkey", JsonSerializer.SerializeToElement(oldHotkeys.Start));
            session.UpdateSetting("app.previewHotkey", JsonSerializer.SerializeToElement(oldHotkeys.Preview));
            session.UpdateSetting("app.unpreviewHotkey", JsonSerializer.SerializeToElement(oldHotkeys.UnPreview));
            session.UpdateSetting("app.stopHotkey", JsonSerializer.SerializeToElement(oldHotkeys.Stop));
            TryRegisterHotkeys(out _);
            result = new HostCommandResult(false, message);
        }
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private object SelectAndNormalizeLocalImage()
    {
        using var dialog = new OpenFileDialog { Title = "Select camouflage source image", Filter = "Image files|*.png;*.jpg;*.jpeg;*.bmp", CheckFileExists = true };
        if (dialog.ShowDialog(this) != DialogResult.OK) return new { success = false, cancelled = true };
        var info = new FileInfo(dialog.FileName);
        if (info.Length <= 0 || info.Length > 32L * 1024L * 1024L) return new { success = false, cancelled = false, message = "Image must be between 1 byte and 32 MB." };
        using var source = new Bitmap(dialog.FileName);
        if (source.Width <= 0 || source.Height <= 0 || source.Width > 4096 || source.Height > 4096) return new { success = false, cancelled = false, message = "Image dimensions must be 1..4096 pixels." };
        var cache = Path.Combine(session.Paths.VersionRoot, "local-sources"); Directory.CreateDirectory(cache);
        var identity = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(dialog.FileName))).ToLowerInvariant();
        var output = Path.Combine(cache, identity + ".bmp");
        var scale = Math.Min(1.0, 2048.0 / Math.Max(source.Width, source.Height));
        var width = Math.Max(1, (int)Math.Round(source.Width * scale)); var height = Math.Max(1, (int)Math.Round(source.Height * scale));
        if (!File.Exists(output)) { using var normalized = new Bitmap(width, height, PixelFormat.Format24bppRgb); using var graphics = Graphics.FromImage(normalized); graphics.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy; graphics.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic; graphics.DrawImage(source, 0, 0, width, height); normalized.Save(output, ImageFormat.Bmp); }
        return new { success = true, cancelled = false, path = output, name = Path.GetFileName(dialog.FileName), width, height };
    }

    private object HandleUpdateSettings(JsonElement payload)
    {
        var oldHotkeys = HotkeySet.From(session.Settings);
        var changes = new List<SettingChange>();
        var hasHotkeyChange = false;
        foreach (var item in payload.GetProperty("changes").EnumerateArray())
        {
            var key = item.GetProperty("key").GetString() ?? "";
            changes.Add(new SettingChange(key, item.GetProperty("value")));
            hasHotkeyChange |= key.Contains("Hotkey", StringComparison.OrdinalIgnoreCase);
        }

        var result = session.UpdateSettings(changes);
        if (result.Success && hasHotkeyChange && !TryRegisterHotkeys(out var message))
        {
            session.UpdateSettings(HotkeyRevertChanges(oldHotkeys));
            TryRegisterHotkeys(out _);
            result = new HostCommandResult(false, message);
        }
        else if (result.Success)
        {
            TryRegisterHotkeys(out _);
        }
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private object HandleResetSetting(JsonElement payload)
    {
        var result = session.ResetSetting(payload.GetProperty("key").GetString() ?? "");
        TryRegisterHotkeys(out _);
        ApplyWindowSettings();
        return ApplyResult(result);
    }

    private object HandleResetSection(JsonElement payload)
    {
        var result = session.ResetSection(payload.GetProperty("section").GetString() ?? "");
        TryRegisterHotkeys(out _);
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
        }
        await PushSnapshotAsync();
    }

    private void ApplyWindowSettings()
    {
        Opacity = session.Settings.Opacity;
        var topMost = session.Settings.AlwaysOnTop;
        TopMost = topMost;
        if (IsHandleCreated)
        {
            _ = SetWindowPos(
                Handle,
                topMost ? HwndTopMost : HwndNoTopMost,
                0,
                0,
                0,
                0,
                SwpNoMove | SwpNoSize | SwpNoActivate | SwpShowWindow);
        }
        ApplyDarkTitleBar();
    }

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
        if (!webReady || webView.CoreWebView2 is null)
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
        if (!webReady || webView.CoreWebView2 is null)
            return;
        var json = JsonSerializer.Serialize(new { type = "response", id, ok, data }, JsonOptions);
        webView.CoreWebView2.PostWebMessageAsJson(json);
    }

    private void PostEvent(string name, object data)
    {
        if (!webReady || webView.CoreWebView2 is null)
            return;
        var json = JsonSerializer.Serialize(new { type = "event", name, data }, JsonOptions);
        webView.CoreWebView2.PostWebMessageAsJson(json);
    }

    private void SendToast(string message, string level)
    {
        if (webReady && webView.CoreWebView2 is not null)
            PostEvent("toast", new { message, level });
    }

    private void PersistWindowSnapshot()
    {
        if (!IsHandleCreated)
            return;
        session.SetWindowSnapshot(Width, Height, Left, Top);
    }

    private bool TryRegisterHotkeys(out string message)
    {
        message = "";
        UnregisterHotkeys();
        var keys = new[]
        {
            (HotkeyStart, session.Settings.StartHotkey),
            (HotkeyPreview, session.Settings.PreviewHotkey),
            (HotkeyUnPreview, session.Settings.UnPreviewHotkey),
            (HotkeyStop, session.Settings.StopHotkey)
        };
        foreach (var (id, key) in keys)
        {
            var virtualKey = ParseVirtualKey(key);
            if (virtualKey == 0 || !RegisterHotKey(Handle, id, 0, virtualKey))
            {
                message = $"Hotkey registration failed: {key}";
                UnregisterHotkeys();
                return false;
            }
        }
        return true;
    }

    private void UnregisterHotkeys()
    {
        if (!IsHandleCreated)
            return;
        for (var id = 1; id <= 4; ++id)
            UnregisterHotKey(Handle, id);
    }

    private static uint ParseVirtualKey(string text)
    {
        var value = HotkeySet.Normalize(text);
        if (value.Length >= 2 && value[0] == 'F' && int.TryParse(value[1..], out var f) && f is >= 1 and <= 24)
            return (uint)(0x70 + f - 1);
        return 0;
    }

    private static IEnumerable<SettingChange> HotkeyRevertChanges(HotkeySet oldHotkeys)
    {
        yield return new SettingChange("app.startHotkey", JsonSerializer.SerializeToElement(oldHotkeys.Start, JsonOptions));
        yield return new SettingChange("app.previewHotkey", JsonSerializer.SerializeToElement(oldHotkeys.Preview, JsonOptions));
        yield return new SettingChange("app.unpreviewHotkey", JsonSerializer.SerializeToElement(oldHotkeys.UnPreview, JsonOptions));
        yield return new SettingChange("app.stopHotkey", JsonSerializer.SerializeToElement(oldHotkeys.Stop, JsonOptions));
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    private const int DwmwaUseImmersiveDarkMode = 20;
    private const int DwmwaCaptionColor = 35;
    private const int DwmwaTextColor = 36;

    [DllImport("dwmapi.dll", PreserveSig = true)]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int attributeValue, int attributeSize);

    private static readonly IntPtr HwndTopMost = new(-1);
    private static readonly IntPtr HwndNoTopMost = new(-2);
    private const uint SwpNoSize = 0x0001;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpShowWindow = 0x0040;

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
