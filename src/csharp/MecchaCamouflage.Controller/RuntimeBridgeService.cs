using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using MecchaCamouflage.Core;

namespace MecchaCamouflage.Controller;

/// <summary>Read-only native research requests accepted by the current bridge protocol.</summary>
public enum ResearchProbeKind
{
    Replication,
    ReplicationPressure,
    ReplicationTexture
}

/// <summary>Explicit target selection for a read-only research texture export.</summary>
public enum ResearchTextureTarget
{
    ResolvedComponent,
    EventwatchDirectReceiver,
    AllRuntimePaintComponents
}

/// <summary>
/// Explicit opt-in for a fresh, controller-owned research bridge. The event-watch sidecar must
/// exist before the injector loads the staged DLL, because the native bridge reads it only at
/// listener startup.
/// </summary>
public sealed record ResearchBridgeOptions(string EventWatchOutputPath);

/// <summary>Non-secret identity exported to research artifacts for correlation with event-watch output.</summary>
public sealed record ResearchBridgeIdentity(int ProcessId, Guid InstanceId, string BridgeHash, string BridgePath);

/// <summary>Creates the pre-injection, local-only sidecar used by the native event watcher.</summary>
public static class ResearchBridgeArtifacts
{
    public static string StageEventWatchSidecar(string bridgePath, string outputPath)
    {
        if (string.IsNullOrWhiteSpace(bridgePath))
            throw new ArgumentException("A staged bridge path is required.", nameof(bridgePath));
        if (string.IsNullOrWhiteSpace(outputPath))
            throw new ArgumentException("An event-watch output path is required.", nameof(outputPath));

        var normalizedBridgePath = Path.GetFullPath(bridgePath);
        if (!File.Exists(normalizedBridgePath))
            throw new FileNotFoundException("The staged bridge DLL is missing.", normalizedBridgePath);

        var normalizedOutputPath = Path.GetFullPath(outputPath);
        var outputDirectory = Path.GetDirectoryName(normalizedOutputPath);
        if (string.IsNullOrWhiteSpace(outputDirectory))
            throw new ArgumentException("The event-watch output path must have a parent directory.", nameof(outputPath));

        Directory.CreateDirectory(outputDirectory);
        var sidecarPath = normalizedBridgePath + ".eventwatch.path";
        File.WriteAllText(sidecarPath, normalizedOutputPath + Environment.NewLine, new System.Text.UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        return sidecarPath;
    }
}

/// <summary>
/// Stages and starts one uniquely named direct bridge per attempt. Existing bridge modules are
/// intentionally outside this service's control: they are neither enumerated, switched,
/// unloaded, nor used as a reason to require a game restart.
/// </summary>
public sealed class RuntimeBridgeService
{
    public static readonly TimeSpan BridgeProbeTimeout = TimeSpan.FromMilliseconds(300);

    private readonly AppPaths paths;
    private readonly RuntimeLog log;
    private readonly object bridgeStateGate = new();
    private BridgeInstance? activeInstance;
    private string waitingForProcessName = "";
    private string lastBridgeReadyLogKey = "";
    private bool bridgeReadyTimeoutLogged;
    private bool bridgeConnected;

    public RuntimeBridgeService(AppPaths paths, RuntimeLog log)
    {
        this.paths = paths;
        this.log = log;
    }

    public string BridgePath
    {
        get
        {
            lock (bridgeStateGate)
                return activeInstance?.BridgePath ?? "";
        }
    }

    public string ProgressPath
    {
        get
        {
            lock (bridgeStateGate)
                return activeInstance?.ProgressPath ?? "";
        }
    }

    public bool IsConnected
    {
        get
        {
            lock (bridgeStateGate)
                return bridgeConnected;
        }
    }

    public bool HasActiveBridgeInstance
    {
        get
        {
            lock (bridgeStateGate)
                return activeInstance is not null;
        }
    }

    public ResearchBridgeIdentity? ActiveResearchBridgeIdentity
    {
        get
        {
            lock (bridgeStateGate)
            {
                var instance = activeInstance;
                return instance is null
                    ? null
                    : new ResearchBridgeIdentity(
                        instance.Target.ProcessId,
                        instance.InstanceId,
                        instance.ExpectedBridgeHash,
                        instance.BridgePath);
            }
        }
    }

    /// <summary>
    /// Resolves the configured process name for the current UI flow. Once selected, the exact
    /// <see cref="Process"/> identity is captured and passed to the injector; the injector never
    /// receives this name and never performs its own process-name lookup.
    /// </summary>
    public Process? FindGameProcess(string processName)
    {
        var name = Path.GetFileNameWithoutExtension(processName);
        return Process.GetProcessesByName(name).OrderBy(process => process.Id).FirstOrDefault();
    }

    public Task<BridgeReply> PingAsync(CancellationToken cancellationToken = default, TimeSpan? timeoutOverride = null) =>
        RequestActiveAsync(client => client.PingAsync(cancellationToken, timeoutOverride));

    public Task<BridgeReply> CancelPaintAsync(CancellationToken cancellationToken = default) =>
        RequestActiveAsync(client => client.CancelPaintAsync(cancellationToken));

    public Task<BridgeReply> SendPaintAsync(string payload, CancellationToken cancellationToken = default) =>
        RequestActiveAsync(client => client.RequestAsync(payload, cancellationToken));

    /// <summary>
    /// Sends one whitelisted, authenticated research request through the controller-owned bridge.
    /// This does not create a second listener or bypass the per-instance HELLO handshake.
    /// </summary>
    public Task<BridgeReply> SendResearchProbeAsync(
        ResearchProbeKind kind,
        CancellationToken cancellationToken = default) =>
        SendResearchProbeAsync(kind, ResearchTextureTarget.ResolvedComponent, null, cancellationToken);

    public Task<BridgeReply> SendResearchProbeAsync(
        ResearchProbeKind kind,
        ResearchTextureTarget textureTarget,
        CancellationToken cancellationToken = default) =>
        SendResearchProbeAsync(kind, textureTarget, null, cancellationToken);

    /// <summary>
    /// Sends a research probe with an optional exact component pin. The event-watch receiver
    /// target requires this pin so a later multicast cannot silently replace the receiver that
    /// discovery observed.
    /// </summary>
    public Task<BridgeReply> SendResearchProbeAsync(
        ResearchProbeKind kind,
        ResearchTextureTarget textureTarget,
        string? expectedTextureComponent,
        CancellationToken cancellationToken = default) =>
        SendResearchProbeAsync(kind, textureTarget, expectedTextureComponent, false, cancellationToken);

    /// <summary>
    /// Sends a research texture probe that can retain its initial baseline for a compact
    /// time-series. This remains inside the authenticated research bridge protocol.
    /// </summary>
    public Task<BridgeReply> SendResearchProbeAsync(
        ResearchProbeKind kind,
        ResearchTextureTarget textureTarget,
        string? expectedTextureComponent,
        bool preserveTextureBaseline,
        CancellationToken cancellationToken = default) =>
        RequestActiveAsync(client => client.RequestAsync(
            ResearchProbePayload(kind, textureTarget, expectedTextureComponent, preserveTextureBaseline),
            cancellationToken));

    public async Task<BridgeReply> ShutdownAsync(CancellationToken cancellationToken = default)
    {
        var result = await RequestActiveWithInstanceAsync(client => client.ShutdownAsync(cancellationToken));
        if (result.Reply.Ok && result.Reply.Success)
        {
            lock (bridgeStateGate)
            {
                if (ReferenceEquals(activeInstance, result.Instance))
                {
                    bridgeConnected = false;
                    activeInstance = null;
                }
            }
        }
        return result.Reply;
    }

    public async Task<bool> EnsureReadyAsync(string processName, CancellationToken cancellationToken = default)
    {
        var process = FindGameProcess(processName);
        if (process is null)
        {
            MarkDisconnected();
            if (!string.Equals(waitingForProcessName, processName, StringComparison.OrdinalIgnoreCase))
            {
                log.Warn($"Game process: waiting for {processName}.");
                waitingForProcessName = processName;
            }
            return false;
        }

        using (process)
            return await EnsureReadyAsync(process, cancellationToken);
    }

    /// <summary>Starts a bridge for an explicitly selected PID after recapturing its identity.</summary>
    public async Task<bool> EnsureReadyAsync(int selectedProcessId, CancellationToken cancellationToken = default)
    {
        try
        {
            using var process = Process.GetProcessById(selectedProcessId);
            return await EnsureReadyAsync(process, cancellationToken);
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
        {
            MarkDisconnected();
            DiagnosticsState.SetLastCode("MC-INJ-102", ex.Message);
            log.Warn($"Game process: selected pid={selectedProcessId} is no longer available.");
            return false;
        }
    }

    /// <summary>
    /// Starts one fresh, exact-PID bridge with event-watch armed before injection. This is
    /// deliberately separate from normal warmup so the research runner can validate its own
    /// event-watch identity and lifecycle.
    /// </summary>
    public async Task<bool> EnsureResearchReadyAsync(
        int selectedProcessId,
        ResearchBridgeOptions options,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(options);
        lock (bridgeStateGate)
        {
            if (activeInstance is not null && bridgeConnected)
                throw new InvalidOperationException("A controller bridge is already active in this runner. Shut it down before starting another research bridge.");
        }

        try
        {
            using var process = Process.GetProcessById(selectedProcessId);
            var target = CaptureTargetIdentity(process);
            waitingForProcessName = "";
            return await InjectDirectInstanceAsync(target, options, cancellationToken);
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or Win32Exception or UnauthorizedAccessException)
        {
            MarkDisconnected();
            DiagnosticsState.SetLastCode("MC-INJ-102", ex.Message);
            log.Error("Research bridge: could not read the selected game process identity: " + FriendlyAccessFailure(ex.Message));
            return false;
        }
    }

    /// <summary>
    /// Starts a bridge for the exact process selected by the caller. The process instance is not
    /// retained; its PID, creation FILETIME, and executable path are captured once and passed to
    /// the injector for independent verification.
    /// </summary>
    public async Task<bool> EnsureReadyAsync(Process selectedProcess, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(selectedProcess);
        TargetProcessIdentity target;
        try
        {
            target = CaptureTargetIdentity(selectedProcess);
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException or UnauthorizedAccessException)
        {
            MarkDisconnected();
            DiagnosticsState.SetLastCode("MC-INJ-102", ex.Message);
            log.Error("Bridge: could not read the selected game process identity: " + FriendlyAccessFailure(ex.Message));
            return false;
        }

        return await EnsureReadyAsync(target, cancellationToken);
    }

    /// <summary>Uses an already captured exact identity; the injector verifies it again before injection.</summary>
    public async Task<bool> EnsureReadyAsync(TargetProcessIdentity target, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(target);
        waitingForProcessName = "";
        var matchingInstance = MatchingActiveInstance(target);
        if (matchingInstance is not null)
        {
            var ping = await PingAsync(cancellationToken, BridgeProbeTimeout);
            if (IsBridgeReadyForInstance(ping, matchingInstance))
            {
                bridgeReadyTimeoutLogged = false;
                if (RestoreConnectedState(matchingInstance))
                    return true;
            }
            MarkDisconnectedIfCurrent(matchingInstance);
        }

        return await InjectDirectInstanceAsync(target, null, cancellationToken);
    }

    private Task<bool> InjectDirectInstanceAsync(
        TargetProcessIdentity target,
        ResearchBridgeOptions? researchOptions,
        CancellationToken cancellationToken) =>
        // Mutex ownership is thread-affine. Keep the complete critical section on one worker
        // thread so asynchronous injector/network continuations cannot release it elsewhere.
        Task.Run(() => InjectDirectInstanceOnMutexThread(target, researchOptions, cancellationToken), CancellationToken.None);

    private bool InjectDirectInstanceOnMutexThread(
        TargetProcessIdentity target,
        ResearchBridgeOptions? researchOptions,
        CancellationToken cancellationToken)
    {
        var mutexName = $@"Local\MecchaCamouflage.Inject.{target.ProcessId}";
        using var mutex = new Mutex(false, mutexName);
        var ownsMutex = false;
        try
        {
            try
            {
                ownsMutex = mutex.WaitOne(TimeSpan.FromSeconds(1));
            }
            catch (AbandonedMutexException)
            {
                ownsMutex = true;
            }
            if (!ownsMutex)
            {
                DiagnosticsState.SetBridgeInjection($"waiting for another direct injection pid={target.ProcessId}");
                return false;
            }

            BridgeInstance instance;
            try
            {
                instance = PrepareDirectBridgeInstance(target, researchOptions);
            }
            catch (Exception ex) when (ex is ArgumentException or UnauthorizedAccessException or IOException or DirectoryNotFoundException or FileNotFoundException)
            {
                MarkDisconnected();
                DiagnosticsState.SetLastCode("MC-RT-001", ex.Message);
                DiagnosticsState.SetBridgeInjection("direct bridge staging failed");
                log.Error("Bridge: runtime files could not be staged: " + FriendlyAccessFailure(ex.Message));
                return false;
            }

            LogTargetProcess(target);
            DiagnosticsState.SetBridgeInjection($"direct-injecting pid={target.ProcessId} instance={instance.InstanceId:N}");
            var invocation = InvokeDirectInjectorAsync(instance, cancellationToken).GetAwaiter().GetResult();
            if (invocation.Canceled)
            {
                MarkDisconnected();
                DiagnosticsState.SetLastCode("MC-INJ-131", "injector wait was canceled; target memory ownership is indeterminate");
                DiagnosticsState.SetBridgeInjection($"direct injector canceled pid={target.ProcessId}");
                log.Warn("Bridge: injection was canceled while the target operation may still be running. Retry explicitly when ready.");
                return false;
            }
            if (!invocation.Parsed || invocation.Result is null || !invocation.Result.Matches(target.ProcessId, instance.InstanceId, instance.ExpectedBridgeHash))
            {
                MarkDisconnected();
                var result = invocation.Result;
                var detail = result?.Detail;
                if (string.IsNullOrWhiteSpace(detail))
                    detail = invocation.ParseError;
                var code = result?.State.Equals("indeterminate_timeout", StringComparison.OrdinalIgnoreCase) == true ? "MC-INJ-132" : "MC-INJ-130";
                DiagnosticsState.SetLastCode(code, detail ?? "injector did not return a matching direct bridge result");
                DiagnosticsState.SetBridgeInjection($"direct injection failed pid={target.ProcessId} state={result?.State ?? "protocol_error"}");
                log.Error("Bridge: direct injection failed: " + FriendlyInjectorFailure(invocation));
                return false;
            }

            instance.SetPort(invocation.Result.BoundPort!.Value);
            lock (bridgeStateGate)
            {
                activeInstance = instance;
                bridgeConnected = false;
            }
            var ping = PingAsync(cancellationToken, TimeSpan.FromSeconds(3)).GetAwaiter().GetResult();
            if (IsBridgeReadyForInstance(ping, instance))
            {
                bridgeReadyTimeoutLogged = false;
                return RestoreConnectedState(instance);
            }

            MarkDisconnectedIfCurrent(instance);
            DiagnosticsState.SetLastCode("MC-INJ-145", ping.Message);
            DiagnosticsState.SetBridgeInjection($"direct bridge hello failed pid={target.ProcessId} instance={instance.InstanceId:N}");
            if (!bridgeReadyTimeoutLogged)
            {
                bridgeReadyTimeoutLogged = true;
                log.Error("Bridge: direct bridge started but its authenticated hello did not complete.");
            }
            return false;
        }
        finally
        {
            if (ownsMutex)
                mutex.ReleaseMutex();
        }
    }

    private BridgeInstance PrepareDirectBridgeInstance(TargetProcessIdentity target, ResearchBridgeOptions? researchOptions)
    {
        paths.EnsureBaseDirectories();
        var nativeRoot = PackagedAssets.ResolveRequiredAssetRoot(paths, "native", log);
        // Mesh profiles live beneath the packaged Web root so the Image editor
        // and the native bridge read one identical, versioned copy.
        var profilesRoot = PackagedAssets.ResolveRequiredAssetRoot(paths, "web", log);
        var bridgeSource = ResolvePackagedNativeAsset(nativeRoot, "runtime-bridge.dll");
        var injectorSource = ResolvePackagedNativeAsset(nativeRoot, "runtime-injector.exe");
        if (!File.Exists(bridgeSource) || !File.Exists(injectorSource))
            throw new FileNotFoundException("Packaged direct bridge or injector is missing.");

        var instanceId = Guid.NewGuid();
        var token = RandomNumberGenerator.GetBytes(BridgeStartBlockV1.TokenLength);
        var hash = PackagedAssets.Sha256File(bridgeSource).ToLowerInvariant();
        var instanceDirectory = Path.Combine(paths.BridgeInstancesDirectory, BridgeInstanceNaming.CreateDirectoryName(instanceId));
        Directory.CreateDirectory(instanceDirectory);

        var bridgePath = Path.Combine(instanceDirectory, BridgeInstanceNaming.CreateBridgeFileName(hash, instanceId));
        var injectorPath = Path.Combine(instanceDirectory, "runtime-injector.exe");
        PackagedAssets.CopyIfInvalid(bridgeSource, bridgePath);
        PackagedAssets.CopyIfInvalid(injectorSource, injectorPath);
        CopyMeshProfiles(profilesRoot, instanceDirectory);
        if (researchOptions is not null)
            ResearchBridgeArtifacts.StageEventWatchSidecar(bridgePath, researchOptions.EventWatchOutputPath);

        var progressPath = bridgePath + ".progress.json";
        var instance = new BridgeInstance(target, instanceId, token, hash, bridgePath, injectorPath, progressPath);
        LogRuntimeFilesPrepared(instance);
        return instance;
    }

    private async Task<InjectorInvocation> InvokeDirectInjectorAsync(BridgeInstance instance, CancellationToken cancellationToken)
    {
        var start = new ProcessStartInfo(instance.InjectorPath)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardError = true,
            RedirectStandardOutput = true
        };
        start.ArgumentList.Add("--direct");
        start.ArgumentList.Add(instance.Target.ProcessId.ToString(System.Globalization.CultureInfo.InvariantCulture));
        start.ArgumentList.Add(instance.Target.CreationTimeUtcFileTime.ToString(System.Globalization.CultureInfo.InvariantCulture));
        start.ArgumentList.Add(instance.Target.ExecutablePath);
        start.ArgumentList.Add(instance.BridgePath);

        var startBlock = BridgeStartBlockV1.Create(
            instance.Target.ProcessId,
            instance.InstanceId,
            instance.ConnectionToken,
            Convert.FromHexString(instance.ExpectedBridgeHash));
        Process? injector;
        try
        {
            injector = Process.Start(start);
        }
        catch (Exception ex) when (ex is Win32Exception or UnauthorizedAccessException or InvalidOperationException)
        {
            return new InjectorInvocation(false, false, null, ex.Message, "");
        }
        if (injector is null)
            return new InjectorInvocation(false, false, null, "could not start the direct injector", "");
        using (injector)
        {
            var stdoutTask = injector.StandardOutput.ReadToEndAsync();
            var stderrTask = injector.StandardError.ReadToEndAsync();

            try
            {
                await injector.StandardInput.BaseStream.WriteAsync(startBlock.Serialize(), cancellationToken);
                await injector.StandardInput.BaseStream.FlushAsync(cancellationToken);
                injector.StandardInput.Close();
                await injector.WaitForExitAsync(cancellationToken);
            }
            catch (OperationCanceledException)
            {
                TryCloseStandardInput(injector);
                _ = ObserveInjectorOutputAsync(stdoutTask, stderrTask);
                return new InjectorInvocation(true, false, null, "injector operation canceled", "");
            }
            catch (Exception ex) when (ex is IOException or Win32Exception or InvalidOperationException)
            {
                TryCloseStandardInput(injector);
                _ = ObserveInjectorOutputAsync(stdoutTask, stderrTask);
                return new InjectorInvocation(false, false, null, ex.Message, "");
            }

            var stdout = await stdoutTask;
            var stderr = await stderrTask;
            LogInjectorOutput(stdout, stderr);
            if (!InjectorResultV1.TryParseFinal(stdout, out var result, out var parseError))
                return new InjectorInvocation(false, false, null, parseError, stderr);
            return new InjectorInvocation(false, true, result, "", stderr);
        }
    }

    private async Task<BridgeReply> RequestActiveAsync(Func<BridgeClient, Task<BridgeReply>> request) =>
        (await RequestActiveWithInstanceAsync(request)).Reply;

    private async Task<ActiveBridgeRequest> RequestActiveWithInstanceAsync(Func<BridgeClient, Task<BridgeReply>> request)
    {
        BridgeInstance? instance;
        lock (bridgeStateGate)
            instance = activeInstance;
        if (instance?.Port is not int)
            return new ActiveBridgeRequest(instance, new BridgeReply(false, false, "not_connected", "Bridge is not connected.", ""));
        var reply = await request(new BridgeClient(instance.Endpoint, instance.Target.ProcessId));
        lock (bridgeStateGate)
        {
            if (ReferenceEquals(activeInstance, instance))
                bridgeConnected = IsBridgeReadyForInstance(reply, instance) || (reply.Ok && reply.InstanceId == instance.InstanceId);
        }
        return new ActiveBridgeRequest(instance, reply);
    }

    private static string ResearchProbePayload(
        ResearchProbeKind kind,
        ResearchTextureTarget textureTarget,
        string? expectedTextureComponent,
        bool preserveTextureBaseline = false) => kind switch
    {
        ResearchProbeKind.Replication => "{\"type\":\"paint_replication_probe\"}",
        ResearchProbeKind.ReplicationPressure => "{\"type\":\"paint_replication_pressure_probe\"}",
        ResearchProbeKind.ReplicationTexture => ResearchTextureProbePayload(
            textureTarget,
            expectedTextureComponent,
            preserveTextureBaseline),
        _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unsupported research probe.")
    };

    private static string ResearchTextureProbePayload(
        ResearchTextureTarget textureTarget,
        string? expectedTextureComponent,
        bool preserveTextureBaseline)
    {
        var preserve = preserveTextureBaseline ? ",\"research_texture_preserve_baseline\":true" : "";
        return textureTarget switch
    {
        ResearchTextureTarget.ResolvedComponent when string.IsNullOrWhiteSpace(expectedTextureComponent) =>
            "{\"type\":\"paint_replication_texture_probe\",\"research_texture_target\":\"resolved\",\"research_compact\":true" + preserve + "}",
        ResearchTextureTarget.ResolvedComponent => throw new ArgumentException(
            "A texture component pin is only supported for an event-watch direct receiver.",
            nameof(expectedTextureComponent)),
        ResearchTextureTarget.EventwatchDirectReceiver =>
            "{\"type\":\"paint_replication_texture_probe\",\"research_texture_target\":\"eventwatch_direct_receiver\",\"research_compact\":true" + preserve + ",\"research_texture_expected_component\":\"" +
            NormalizeNonZeroHexAddress(expectedTextureComponent) + "\"}",
        ResearchTextureTarget.AllRuntimePaintComponents when string.IsNullOrWhiteSpace(expectedTextureComponent) =>
            "{\"type\":\"paint_replication_texture_probe\",\"research_texture_target\":\"inventory_all\",\"research_compact\":true" + preserve + "}",
        ResearchTextureTarget.AllRuntimePaintComponents => throw new ArgumentException(
            "An all-components texture probe does not accept a component pin.",
            nameof(expectedTextureComponent)),
        _ => throw new ArgumentOutOfRangeException(nameof(textureTarget), textureTarget, "Unsupported research texture target.")
    };
    }

    private static string NormalizeNonZeroHexAddress(string? value)
    {
        if (string.IsNullOrWhiteSpace(value) || value.Length < 3 ||
            value[0] != '0' || (value[1] != 'x' && value[1] != 'X') ||
            !ulong.TryParse(value[2..], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var address) ||
            address == 0)
        {
            throw new ArgumentException(
                "The event-watch texture target requires a non-zero hexadecimal component address.",
                nameof(value));
        }
        return "0x" + address.ToString("x", CultureInfo.InvariantCulture);
    }

    private static bool IsBridgeReadyForInstance(BridgeReply reply, BridgeInstance instance) =>
        reply.Ok &&
        reply.Success &&
        reply.ProcessId == instance.Target.ProcessId &&
        reply.InstanceId == instance.InstanceId &&
        string.Equals(reply.BridgeHash, instance.ExpectedBridgeHash, StringComparison.OrdinalIgnoreCase) &&
        reply.ProtocolVersion == BridgeProtocolV1.Version;

    private BridgeInstance? MatchingActiveInstance(TargetProcessIdentity target)
    {
        lock (bridgeStateGate)
        {
            var instance = activeInstance;
            return instance is not null &&
                   instance.Target.ProcessId == target.ProcessId &&
                   instance.Target.CreationTimeUtcFileTime == target.CreationTimeUtcFileTime &&
                   string.Equals(instance.Target.ExecutablePath, target.ExecutablePath, StringComparison.OrdinalIgnoreCase) &&
                   instance.Port is not null
                ? instance
                : null;
        }
    }

    private static TargetProcessIdentity CaptureTargetIdentity(Process process)
    {
        if (process.HasExited)
            throw new InvalidOperationException("The selected game process exited.");
        var executablePath = process.MainModule?.FileName;
        if (string.IsNullOrWhiteSpace(executablePath))
            throw new InvalidOperationException("The selected game executable path is unavailable.");
        return TargetProcessIdentity.Create(process.Id, process.StartTime.ToUniversalTime().ToFileTimeUtc(), executablePath);
    }

    private static string ResolvePackagedNativeAsset(string root, string fileName)
    {
        var inNativeDirectory = Path.Combine(root, "native", fileName);
        return File.Exists(inNativeDirectory) ? inNativeDirectory : Path.Combine(root, fileName);
    }

    private static void CopyMeshProfiles(string profilesRoot, string instanceDirectory)
    {
        var source = Path.Combine(profilesRoot, "web", "mesh-profiles");
        if (!Directory.Exists(source))
            throw new DirectoryNotFoundException("Packaged mesh profiles are missing from the Web assets.");
        var target = Path.Combine(instanceDirectory, "mesh-profiles");
        Directory.CreateDirectory(target);
        foreach (var file in Directory.EnumerateFiles(source, "*.json", SearchOption.TopDirectoryOnly))
            PackagedAssets.CopyIfInvalid(file, Path.Combine(target, Path.GetFileName(file)));
    }

    private void MarkDisconnectedIfCurrent(BridgeInstance instance)
    {
        lock (bridgeStateGate)
        {
            if (ReferenceEquals(activeInstance, instance))
                bridgeConnected = false;
        }
    }

    private void MarkDisconnected()
    {
        lock (bridgeStateGate)
            bridgeConnected = false;
    }

    private bool RestoreConnectedState(BridgeInstance instance)
    {
        var shouldLog = false;
        lock (bridgeStateGate)
        {
            if (!ReferenceEquals(activeInstance, instance))
                return false;
            bridgeConnected = true;
            var key = $"{instance.Target.ProcessId}:{instance.InstanceId:N}:{instance.Port}";
            if (!string.Equals(lastBridgeReadyLogKey, key, StringComparison.Ordinal))
            {
                lastBridgeReadyLogKey = key;
                shouldLog = true;
            }
        }
        if (!shouldLog)
            return true;
        DiagnosticsState.SetBridgeInjection($"ready pid={instance.Target.ProcessId} instance={instance.InstanceId:N} port={instance.Port}");
        log.Info($"Bridge: connected to direct instance in game process pid={instance.Target.ProcessId}.");
        return true;
    }

    private void LogRuntimeFilesPrepared(BridgeInstance instance)
    {
        log.Info("Bridge: staged direct instance.");
        if (!ResearchArtifactsEnabled())
            return;
        LogRuntimeFileDetails("bridge", instance.BridgePath);
        LogRuntimeFileDetails("injector", instance.InjectorPath);
    }

    private void LogTargetProcess(TargetProcessIdentity target)
    {
        log.Info($"Game process: selected pid={target.ProcessId}.");
        if (ResearchArtifactsEnabled())
            log.Info($"Game process detail: created={DateTimeOffset.FromFileTime(target.CreationTimeUtcFileTime):O} path={target.ExecutablePath}");
    }

    private void LogRuntimeFileDetails(string label, string path)
    {
        var info = new FileInfo(path);
        log.Info($"Bridge detail: {label} {path} ({info.Length} bytes sha256={PackagedAssets.Sha256File(path)[..16]})");
    }

    private void LogInjectorOutput(string stdout, string stderr)
    {
        if (ResearchArtifactsEnabled())
        {
            foreach (var line in SplitLines(stdout))
                log.Info("Bridge detail: injector " + line);
        }
        foreach (var line in SplitLines(stderr))
            log.Warn("Bridge detail: injector " + line);
    }

    private static async Task ObserveInjectorOutputAsync(Task<string> stdoutTask, Task<string> stderrTask)
    {
        try
        {
            await Task.WhenAll(stdoutTask, stderrTask);
        }
        catch
        {
            // The process and its remote work are deliberately not terminated on host cancellation.
        }
    }

    private static void TryCloseStandardInput(Process injector)
    {
        try { injector.StandardInput.Close(); } catch { }
    }

    private static IEnumerable<string> SplitLines(string value) =>
        value.Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries)
            .Select(line => line.Trim())
            .Where(line => line.Length > 0);

    private static string FriendlyInjectorFailure(InjectorInvocation invocation)
    {
        if (invocation.Result?.State.Equals("indeterminate_timeout", StringComparison.OrdinalIgnoreCase) == true)
            return "the remote operation did not finish. Its target memory was intentionally retained; retry explicitly after confirming the game is responsive.";
        var detail = invocation.Result?.Detail;
        if (string.IsNullOrWhiteSpace(detail))
            detail = invocation.ParseError;
        if (string.IsNullOrWhiteSpace(detail))
            detail = invocation.StandardError;
        var lower = detail.ToLowerInvariant();
        if (lower.Contains("access denied") || lower.Contains("win32=5"))
            return "access denied while opening the selected game process. Run Meccha Camouflage with the same privileges as the game, or try Run as administrator.";
        return detail;
    }

    private static string FriendlyAccessFailure(string message)
    {
        var lower = message.ToLowerInvariant();
        if (lower.Contains("access") || lower.Contains("permission") || lower.Contains("denied"))
            return message + " Run Meccha Camouflage with access to its runtime folder.";
        return message;
    }

    private static bool ResearchArtifactsEnabled() => BuildFeatures.ResearchArtifactsEnabled;

    private sealed record ActiveBridgeRequest(BridgeInstance? Instance, BridgeReply Reply);
    private sealed record InjectorInvocation(bool Canceled, bool Parsed, InjectorResultV1? Result, string ParseError, string StandardError);
}
