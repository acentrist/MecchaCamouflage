using System.Buffers.Binary;
using System.Diagnostics;
using System.Globalization;
using System.IO.MemoryMappedFiles;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;

namespace MecchaCamouflage.LiveDiagnostics;

/// <summary>
/// Connects only to a process-resident bridge published by the running game.  It never injects a
/// DLL, never prints the bridge token, and makes a local material preview only with explicit
/// opt-in.  Preview commands are paired with a same-bridge restore unless the caller chooses the
/// explicit hold command so the normal UI can inspect and restore the result.
/// </summary>
internal static class Program
{
    private const uint ResidentCoreMagicV1 = 0x3152434D; // "MCR1"
    private const uint ResidentCoreMagicV2 = 0x3252434D; // "MCR2"
    private const int ResidentCoreSizeV1 = 104;
    private const int ResidentCoreSizeV2 = 136;
    private const string TextureProbePayload =
        "{\"type\":\"paint_replication_texture_probe\",\"research_texture_target\":\"resolved\",\"research_compact\":true}";
    private const string TextureProbePreservePayload =
        "{\"type\":\"paint_replication_texture_probe\",\"research_texture_target\":\"resolved\",\"research_compact\":true,\"research_texture_preserve_baseline\":true}";
    private const string Usage =
        "Usage: meccha-live-diagnostics --pid <game-pid> --command <ping|capabilities|esp-status|replication|replication-pressure|texture|material-memory|material-memory-inventory|appearance-emission-isolation-probe|appearance-emission-target-differential|appearance-preview|appearance-color-differential|appearance-preview-hold|manual-preview-hold|appearance-candidate-hold|appearance-candidate-differential|appearance-emissive-zero|appearance-restore|appearance-present-probe|emissive-texture-probe|environment-direct-stroke|direct-emissive-channel-probe|bridge-shutdown> [--allow-local-preview] [--allow-direct-stroke] [--allow-bridge-shutdown] [--preview-color-compression <0-10>] [--artifacts <directory>]";

    private sealed record Options(
        int ProcessId,
        string Command,
        bool AllowLocalPreview,
        bool AllowDirectStroke,
        bool AllowBridgeShutdown,
        string ArtifactRoot,
        double PreviewBrushSize,
        double PreviewRoughness,
        double PreviewColorCompression);
    private sealed record ReplySummary(bool TransportOk, bool Success, string Stage, string Message, object? Metadata);
    private sealed record TargetProcess(int ProcessId, long CreationTimeUtcFileTime, string ExecutablePath);
    private sealed record BridgeReply(bool Ok, bool Success, string Stage, string Message, string Raw);

    private sealed class AuthenticatedBridgeClient
    {
        private readonly int expectedProcessId;
        private readonly int port;
        private readonly Guid instanceId;
        private readonly byte[] token;
        private readonly string bridgeHash;
        private readonly string? runtimeBundleId;
        private readonly uint protocol;

        public AuthenticatedBridgeClient(
            int expectedProcessId,
            int port,
            Guid instanceId,
            ReadOnlySpan<byte> token,
            string bridgeHash,
            string? runtimeBundleId,
            uint protocol)
        {
            this.expectedProcessId = expectedProcessId;
            this.port = port;
            this.instanceId = instanceId;
            this.token = token.ToArray();
            this.bridgeHash = bridgeHash;
            this.runtimeBundleId = runtimeBundleId;
            this.protocol = protocol;
        }

        public async Task<BridgeReply> RequestAsync(string payload, TimeSpan timeout)
        {
            try
            {
                using var timeoutSource = new CancellationTokenSource(timeout);
                var requestToken = timeoutSource.Token;
                using var tcp = new TcpClient();
                await tcp.ConnectAsync("127.0.0.1", port, requestToken);
                await using var stream = tcp.GetStream();
                using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
                var hello = JsonSerializer.Serialize(new
                {
                    type = "hello",
                    bootstrap_protocol = protocol,
                    instance_id = instanceId.ToString("N"),
                    token = Convert.ToHexString(token).ToLowerInvariant()
                });
                await WriteLineAsync(stream, hello, requestToken);
                var helloReply = await reader.ReadLineAsync(requestToken);
                if (!ValidateHello(helloReply))
                    return new BridgeReply(false, false, "hello_error", "Resident bridge authentication failed.", "");
                await WriteLineAsync(stream, payload, requestToken);
                return Parse(await reader.ReadToEndAsync(requestToken));
            }
            catch (OperationCanceledException)
            {
                return new BridgeReply(false, false, "transport_error", "Bridge request timed out.", "");
            }
            catch (Exception exception)
            {
                return new BridgeReply(false, false, "transport_error", exception.Message, "");
            }
        }

        private bool ValidateHello(string? raw)
        {
            if (string.IsNullOrWhiteSpace(raw))
                return false;
            try
            {
                using var document = JsonDocument.Parse(raw);
                var root = document.RootElement;
                return root.TryGetProperty("success", out var success) && success.GetBoolean() &&
                       root.TryGetProperty("stage", out var stage) && stage.GetString() == "hello" &&
                       root.TryGetProperty("metadata", out var metadata) &&
                       metadata.TryGetProperty("pid", out var pid) && pid.GetInt32() == expectedProcessId &&
                       metadata.TryGetProperty("instance_id", out var receivedInstance) &&
                       Guid.TryParseExact(receivedInstance.GetString(), "N", out var parsedInstance) &&
                       parsedInstance == instanceId &&
                       metadata.TryGetProperty("bridge_hash", out var hash) &&
                       string.Equals(hash.GetString(), bridgeHash, StringComparison.OrdinalIgnoreCase) &&
                       metadata.TryGetProperty("protocol_version", out var receivedProtocol) &&
                       receivedProtocol.GetUInt32() == protocol &&
                       (runtimeBundleId is null ||
                        metadata.TryGetProperty("runtime_bundle_id", out var receivedBundle) &&
                        string.Equals(
                            receivedBundle.GetString(),
                            runtimeBundleId,
                            StringComparison.OrdinalIgnoreCase));
            }
            catch (JsonException)
            {
                return false;
            }
        }

        private static async Task WriteLineAsync(NetworkStream stream, string line, CancellationToken cancellationToken)
        {
            var bytes = Encoding.UTF8.GetBytes(line + "\n");
            await stream.WriteAsync(bytes, cancellationToken);
            await stream.FlushAsync(cancellationToken);
        }

        private static BridgeReply Parse(string raw)
        {
            if (string.IsNullOrWhiteSpace(raw))
                return new BridgeReply(false, false, "empty_response", "Bridge returned no response.", raw);
            try
            {
                using var document = JsonDocument.Parse(raw);
                var root = document.RootElement;
                return new BridgeReply(
                    true,
                    root.TryGetProperty("success", out var success) && success.GetBoolean(),
                    root.TryGetProperty("stage", out var stage) ? stage.GetString() ?? "" : "",
                    root.TryGetProperty("message", out var message) ? message.GetString() ?? "" : "",
                    raw);
            }
            catch (JsonException exception)
            {
                return new BridgeReply(false, false, "parse_error", exception.Message, raw);
            }
        }
    }

    private static async Task<int> Main(string[] args)
    {
        Options options;
        try
        {
            options = Parse(args);
        }
        catch (ArgumentException exception)
        {
            Console.Error.WriteLine(exception.Message);
            Console.Error.WriteLine(Usage);
            return 2;
        }

        var runDirectory = Path.Combine(
            Path.GetFullPath(options.ArtifactRoot),
            "run-" + DateTimeOffset.UtcNow.ToString("yyyyMMddTHHmmssfffZ") + "-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(runDirectory);
        var summary = new Dictionary<string, object?>
        {
            ["tool"] = "meccha-live-diagnostics",
            ["started_utc"] = DateTimeOffset.UtcNow,
            ["pid"] = options.ProcessId,
            ["command"] = options.Command,
            ["local_preview_allowed"] = options.AllowLocalPreview,
            ["direct_stroke_allowed"] = options.AllowDirectStroke,
            ["run_directory"] = runDirectory,
            ["success"] = false
        };

        var previewStarted = false;
        AuthenticatedBridgeClient? client = null;
        try
        {
            using var process = Process.GetProcessById(options.ProcessId);
            if (process.HasExited)
                throw new InvalidOperationException("The requested game process has already exited.");
            var executablePath = process.MainModule?.FileName;
            if (string.IsNullOrWhiteSpace(executablePath))
                throw new InvalidOperationException("The requested game executable path is unavailable.");
            var target = new TargetProcess(
                process.Id,
                process.StartTime.ToUniversalTime().ToFileTimeUtc(),
                Path.GetFullPath(executablePath));
            client = OpenResidentClient(target, options.Command);

            summary["game_executable"] = executablePath;
            summary["bridge_attached"] = true;

            switch (options.Command)
            {
                case "ping":
                    summary["reply"] = await SendAndStoreAsync(client, "ping", "{\"type\":\"ping\"}", runDirectory);
                    break;
                case "bridge-shutdown":
                    RequireBridgeShutdownOptIn(options);
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "bridge-shutdown",
                        "{\"type\":\"shutdown\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(10));
                    break;
                case "capabilities":
                    summary["reply"] = await SendAndStoreAsync(client, "capabilities", "{\"type\":\"capabilities\"}", runDirectory);
                    break;
                case "esp-status":
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "esp-status",
                        "{\"type\":\"esp_native_present_status\"}",
                        runDirectory);
                    break;
                case "replication":
                    summary["reply"] = await SendAndStoreAsync(client, "replication", "{\"type\":\"paint_replication_probe\"}", runDirectory);
                    break;
                case "replication-pressure":
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "replication-pressure",
                        "{\"type\":\"paint_replication_pressure_probe\"}",
                        runDirectory);
                    break;
                case "texture":
                    summary["reply"] = await SendAndStoreAsync(client, "texture", TextureProbePayload, runDirectory);
                    break;
                case "appearance-present-probe":
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "appearance-present-probe",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    break;
                case "material-memory":
                {
                    var reply = await RequestAndStoreRawAsync(client, "material-memory-texture", TextureProbePayload, runDirectory);
                    if (!reply.Ok || !reply.Success)
                        throw new InvalidOperationException($"material texture discovery failed at {reply.Stage}: {reply.Message}");
                    summary["texture"] = Summarize(reply);
                    summary["material_memory"] = MaterialMemoryProbe.Capture(
                        target.ProcessId,
                        ExtractResolvedTextureComponent(reply));
                    break;
                }
                case "material-memory-inventory":
                {
                    // This is deliberately the pressure probe rather than the texture probe: it inventories every
                    // RuntimePaintableComponent without exporting or importing a texture.  The only subsequent
                    // operation is PROCESS_VM_READ inspection of the returned component addresses.
                    var reply = await RequestAndStoreRawAsync(
                        client,
                        "material-memory-inventory",
                        "{\"type\":\"paint_replication_pressure_probe\"}",
                        runDirectory);
                    if (!reply.Ok || !reply.Success)
                        throw new InvalidOperationException($"material inventory failed at {reply.Stage}: {reply.Message}");
                    summary["inventory"] = Summarize(reply);
                    summary["material_memory_inventory"] = CaptureMaterialInventory(target.ProcessId, reply);
                    break;
                }
                case "environment-direct-stroke":
                {
                    RequireDirectStrokeOptIn(options);
                    var before = await RequestAndStoreRawAsync(
                        client,
                        "environment-direct-before-texture",
                        TextureProbePayload,
                        runDirectory);
                    if (!before.Ok || !before.Success)
                        throw new InvalidOperationException($"pre-stroke texture discovery failed at {before.Stage}: {before.Message}");
                    summary["before_texture"] = Summarize(before);
                    summary["direct_stroke"] = await SendAndStoreAsync(
                        client,
                        "environment-direct-stroke",
                        BuildEnvironmentDirectStrokePayload(target),
                        runDirectory,
                        TimeSpan.FromSeconds(30));
                    var after = await RequestAndStoreRawAsync(
                        client,
                        "environment-direct-after-texture",
                        TextureProbePayload,
                        runDirectory);
                    if (!after.Ok || !after.Success)
                        throw new InvalidOperationException($"post-stroke texture discovery failed at {after.Stage}: {after.Message}");
                    summary["after_texture"] = Summarize(after);
                    break;
                }
                case "direct-emissive-channel-probe":
                {
                    RequireDirectStrokeOptIn(options);
                    var before = await RequestAndStoreRawAsync(client, "direct-emissive-before-texture", TextureProbePayload, runDirectory);
                    if (!before.Ok || !before.Success)
                        throw new InvalidOperationException($"pre-stroke texture discovery failed at {before.Stage}: {before.Message}");
                    var component = ExtractResolvedTextureComponent(before);
                    summary["before_texture"] = Summarize(before);
                    summary["before_material_memory"] = MaterialMemoryProbe.Capture(target.ProcessId, component);
                    summary["direct_stroke"] = await SendAndStoreAsync(
                        client,
                        "direct-emissive-channel",
                        BuildDirectEmissiveChannelProbePayload(target),
                        runDirectory,
                        TimeSpan.FromSeconds(30));
                    var after = await RequestAndStoreRawAsync(client, "direct-emissive-after-texture", TextureProbePayload, runDirectory);
                    if (!after.Ok || !after.Success)
                        throw new InvalidOperationException($"post-stroke texture discovery failed at {after.Stage}: {after.Message}");
                    summary["after_texture"] = Summarize(after);
                    summary["after_material_memory"] = MaterialMemoryProbe.Capture(
                        target.ProcessId,
                        ExtractResolvedTextureComponent(after));
                    break;
                }
                case "appearance-emission-isolation-probe":
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "appearance-emission-isolation-probe",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            researchArtifacts: true,
                            appearanceEmissionIsolationProbe: true),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    break;
                case "appearance-emission-target-differential":
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    previewStarted = true;
                    summary["manual_e0_preview"] = await SendAndStoreAsync(
                        client,
                        "manual-e0-preview",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["manual_e0_isolation"] = await SendAndStoreAsync(
                        client,
                        "manual-e0-isolation",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            researchArtifacts: true,
                            appearanceEmissionIsolationProbe: true,
                            appearanceEmissionIsolationTargetVisible: true),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["manual_e0_restore"] = await SendAndStoreAsync(
                        client,
                        "manual-e0-restore",
                        BuildRestorePayload(target.ProcessId),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    previewStarted = false;

                    previewStarted = true;
                    summary["manual_e1_preview"] = await SendAndStoreAsync(
                        client,
                        "manual-e1-preview",
                        BuildPreviewPayload(
                            target,
                            emissive: 1.0),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["manual_e1_isolation"] = await SendAndStoreAsync(
                        client,
                        "manual-e1-isolation",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            researchArtifacts: true,
                            appearanceEmissionIsolationProbe: true,
                            appearanceEmissionIsolationTargetVisible: true),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["manual_e1_restore"] = await SendAndStoreAsync(
                        client,
                        "manual-e1-restore",
                        BuildRestorePayload(target.ProcessId),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    previewStarted = false;
                    break;
                case "appearance-preview":
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    previewStarted = true;
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "appearance-preview",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    break;
                case "appearance-color-differential":
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    previewStarted = true;
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "appearance-color-differential",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            appearanceCaptureArtifacts: true,
                            appearanceDiagnosticSamples: true),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    break;
                case "appearance-preview-hold":
                {
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    previewStarted = true;
                    var reply = await SendAndStoreAsync(
                        client,
                        "appearance-preview-hold",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["reply"] = reply;
                    if (reply.TransportOk && reply.Success)
                    {
                        previewStarted = false;
                        summary["preview_left_active"] = true;
                    }
                    break;
                }
                case "manual-preview-hold":
                {
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    previewStarted = true;
                    var reply = await SendAndStoreAsync(
                        client,
                        "manual-preview-hold",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            researchArtifacts: true,
                            appearanceDiagnosticSamples: true,
                            brushSize: options.PreviewBrushSize,
                            roughness: options.PreviewRoughness,
                            colorCompressionTolerance:
                                options.PreviewColorCompression),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["reply"] = reply;
                    if (reply.TransportOk && reply.Success)
                    {
                        previewStarted = false;
                        summary["preview_left_active"] = true;
                    }
                    break;
                }
                case "appearance-candidate-hold":
                {
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    previewStarted = true;
                    var reply = await SendAndStoreAsync(
                        client,
                        "appearance-candidate-hold",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            diagnosticHoldBestCandidate: true),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["reply"] = reply;
                    if (reply.TransportOk && reply.Success)
                    {
                        previewStarted = false;
                        summary["preview_left_active"] = true;
                    }
                    break;
                }
                case "appearance-candidate-differential":
                {
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    previewStarted = true;
                    var hold = await SendAndStoreAsync(
                        client,
                        "appearance-candidate-hold",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            diagnosticHoldBestCandidate: true),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["candidate_hold"] = hold;
                    if (!hold.TransportOk || !hold.Success)
                        throw new InvalidOperationException($"candidate hold failed at {hold.Stage}: {hold.Message}");
                    summary["candidate_present_before"] = await SendAndStoreAsync(
                        client,
                        "candidate-present-before",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    summary["candidate_e0_override"] = await SendAndStoreAsync(
                        client,
                        "candidate-e0-override",
                        "{\"type\":\"appearance_preview_emissive_override\",\"emissive\":0.0}",
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    await Task.Delay(TimeSpan.FromMilliseconds(35));
                    summary["candidate_e0_present"] = await SendAndStoreAsync(
                        client,
                        "candidate-e0-present",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    summary["candidate_restore"] = await SendAndStoreAsync(
                        client,
                        "candidate-restore",
                        "{\"type\":\"appearance_preview_emissive_override\",\"restore_candidate\":true}",
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    await Task.Delay(TimeSpan.FromMilliseconds(35));
                    summary["candidate_present_after"] = await SendAndStoreAsync(
                        client,
                        "candidate-present-after",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    break;
                }
                case "appearance-restore":
                    RequirePreviewOptIn(options);
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "appearance-restore",
                        BuildRestorePayload(target.ProcessId),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    break;
                case "appearance-emissive-zero":
                    RequirePreviewOptIn(options);
                    summary["reply"] = await SendAndStoreAsync(
                        client,
                        "appearance-emissive-zero",
                        "{\"type\":\"appearance_preview_emissive_override\",\"emissive\":0.0}",
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    break;
                case "emissive-texture-probe":
                    RequirePreviewOptIn(options);
                    await RestoreStalePreviewAsync(client, target, runDirectory, summary);
                    summary["e0_preview"] = await SendAndStoreAsync(
                        client,
                        "e0-preview",
                        BuildPreviewPayload(target, emissive: 0.0),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    previewStarted = true;
                    // Establish the texture baseline after E=0, before applying E=1.
                    summary["e0_texture"] = await SendAndStoreAsync(client, "e0-texture", TextureProbePayload, runDirectory);
                    summary["e1_preview"] = await SendAndStoreAsync(
                        client,
                        "e1-preview",
                        BuildPreviewPayload(target, emissive: 1.0),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["e1_texture"] = await SendAndStoreAsync(client, "e1-texture", TextureProbePreservePayload, runDirectory);
                    summary["auto_preview"] = await SendAndStoreAsync(
                        client,
                        "auto-preview",
                        BuildPreviewPayload(
                            target,
                            emissive: 0.0,
                            researchArtifacts: true,
                            researchKeepSourceSeed: true,
                            researchSkipPreviewFit: true,
                            researchSourceEmissivePower: 0.05,
                            researchEmissiveRadiusMultiplier: 1.0,
                            researchUseScreenHitUv: true),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["auto_texture"] = await SendAndStoreAsync(
                        client,
                        "auto-texture",
                        TextureProbePreservePayload,
                        runDirectory);
                    // The paint request itself owns the game thread, so a
                    // Present readback must be requested only after that
                    // request returns. Reuse the exact Appearance Match
                    // projection points for E=0/E=1 positive controls.
                    await Task.Delay(TimeSpan.FromMilliseconds(50));
                    summary["auto_present"] = await SendAndStoreAsync(
                        client,
                        "auto-present",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    summary["auto_e0_override"] = await SendAndStoreAsync(
                        client,
                        "auto-e0-override",
                        JsonSerializer.Serialize(new
                        {
                            type = "appearance_preview_emissive_override",
                            emissive = 0.0
                        }),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    await Task.Delay(TimeSpan.FromMilliseconds(50));
                    summary["auto_e0_texture"] = await SendAndStoreAsync(
                        client,
                        "auto-e0-texture",
                        TextureProbePreservePayload,
                        runDirectory);
                    summary["auto_e0_present"] = await SendAndStoreAsync(
                        client,
                        "auto-e0-present",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    summary["auto_e1_override"] = await SendAndStoreAsync(
                        client,
                        "auto-e1-override",
                        JsonSerializer.Serialize(new
                        {
                            type = "appearance_preview_emissive_override",
                            emissive = 1.0
                        }),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    await Task.Delay(TimeSpan.FromMilliseconds(50));
                    summary["auto_e1_texture"] = await SendAndStoreAsync(
                        client,
                        "auto-e1-texture",
                        TextureProbePreservePayload,
                        runDirectory);
                    summary["auto_e1_present"] = await SendAndStoreAsync(
                        client,
                        "auto-e1-present",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    summary["auto_e0_repeat_override"] = await SendAndStoreAsync(
                        client,
                        "auto-e0-repeat-override",
                        JsonSerializer.Serialize(new
                        {
                            type = "appearance_preview_emissive_override",
                            emissive = 0.0
                        }),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    await Task.Delay(TimeSpan.FromMilliseconds(50));
                    summary["auto_e0_repeat_present"] = await SendAndStoreAsync(
                        client,
                        "auto-e0-repeat-present",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    foreach (var quadrant in Enumerable.Range(0, 4))
                    {
                        summary[$"auto_quadrant_{quadrant}_e1_override"] = await SendAndStoreAsync(
                            client,
                            $"auto-quadrant-{quadrant}-e1-override",
                            JsonSerializer.Serialize(new
                            {
                                type = "appearance_preview_emissive_override",
                                emissive = 1.0,
                                scope = "quadrant",
                                quadrant
                            }),
                            runDirectory,
                            TimeSpan.FromSeconds(20));
                        await Task.Delay(TimeSpan.FromMilliseconds(50));
                        summary[$"auto_quadrant_{quadrant}_e1_present"] = await SendAndStoreAsync(
                            client,
                            $"auto-quadrant-{quadrant}-e1-present",
                            "{\"type\":\"appearance_present_probe\"}",
                            runDirectory,
                            TimeSpan.FromSeconds(5));
                        summary[$"auto_quadrant_{quadrant}_e0_override"] = await SendAndStoreAsync(
                            client,
                            $"auto-quadrant-{quadrant}-e0-override",
                            JsonSerializer.Serialize(new
                            {
                                type = "appearance_preview_emissive_override",
                                emissive = 0.0,
                                scope = "painted"
                            }),
                            runDirectory,
                            TimeSpan.FromSeconds(20));
                        await Task.Delay(TimeSpan.FromMilliseconds(50));
                        summary[$"auto_quadrant_{quadrant}_e0_present"] = await SendAndStoreAsync(
                            client,
                            $"auto-quadrant-{quadrant}-e0-present",
                            "{\"type\":\"appearance_present_probe\"}",
                            runDirectory,
                            TimeSpan.FromSeconds(5));
                    }
                    summary["auto_all_e1_override"] = await SendAndStoreAsync(
                        client,
                        "auto-all-e1-override",
                        JsonSerializer.Serialize(new
                        {
                            type = "appearance_preview_emissive_override",
                            emissive = 1.0,
                            scope = "painted"
                        }),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    await Task.Delay(TimeSpan.FromMilliseconds(50));
                    summary["auto_all_e1_present"] = await SendAndStoreAsync(
                        client,
                        "auto-all-e1-present",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    summary["auto_all_e0_override"] = await SendAndStoreAsync(
                        client,
                        "auto-all-e0-override",
                        JsonSerializer.Serialize(new
                        {
                            type = "appearance_preview_emissive_override",
                            emissive = 0.0,
                            scope = "painted"
                        }),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    await Task.Delay(TimeSpan.FromMilliseconds(50));
                    summary["auto_all_e0_present"] = await SendAndStoreAsync(
                        client,
                        "auto-all-e0-present",
                        "{\"type\":\"appearance_present_probe\"}",
                        runDirectory,
                        TimeSpan.FromSeconds(5));
                    break;
                default:
                    throw new ArgumentException("Unsupported command: " + options.Command);
            }

            summary["success"] = true;
        }
        catch (Exception exception)
        {
            summary["error"] = exception.Message;
            summary["success"] = false;
        }
        finally
        {
            if (previewStarted && client is not null)
            {
                try
                {
                    summary["preview_restore"] = await SendAndStoreAsync(
                        client,
                        "preview-restore",
                        BuildRestorePayload(options.ProcessId),
                        runDirectory,
                        TimeSpan.FromSeconds(20));
                    summary["preview_restore_attempted"] = true;
                }
                catch (Exception exception)
                {
                    summary["preview_restore_attempted"] = true;
                    summary["preview_restore_error"] = exception.Message;
                    summary["success"] = false;
                }
            }

            summary["completed_utc"] = DateTimeOffset.UtcNow;
            WriteJson(Path.Combine(runDirectory, "summary.json"), summary);
        }

        // Do not print raw bridge replies: they are retained as local artifacts and can be large.
        Console.Out.WriteLine(JsonSerializer.Serialize(summary, new JsonSerializerOptions { WriteIndented = true }));
        return summary["success"] is true ? 0 : 1;
    }

    private static Options Parse(string[] args)
    {
        int? processId = null;
        string? command = null;
        var allowLocalPreview = false;
        var allowDirectStroke = false;
        var allowBridgeShutdown = false;
        var artifacts = Path.Combine(Environment.CurrentDirectory, "artifacts", "live-diagnostics");
        var previewBrushSize = 4.0;
        var previewRoughness = 0.65;
        var previewColorCompression = 5.0;
        for (var index = 0; index < args.Length; index++)
        {
            switch (args[index])
            {
                case "--pid" when ++index < args.Length && int.TryParse(args[index], out var parsedPid) && parsedPid > 0:
                    processId = parsedPid;
                    break;
                case "--command" when ++index < args.Length:
                    command = args[index].Trim().ToLowerInvariant();
                    break;
                case "--allow-local-preview":
                    allowLocalPreview = true;
                    break;
                case "--allow-direct-stroke":
                    allowDirectStroke = true;
                    break;
                case "--allow-bridge-shutdown":
                    allowBridgeShutdown = true;
                    break;
                case "--artifacts" when ++index < args.Length && !string.IsNullOrWhiteSpace(args[index]):
                    artifacts = args[index];
                    break;
                case "--preview-brush-size" when ++index < args.Length &&
                                                  double.TryParse(
                                                      args[index],
                                                      System.Globalization.NumberStyles.Float,
                                                      System.Globalization.CultureInfo.InvariantCulture,
                                                      out var parsedBrushSize) &&
                                                  parsedBrushSize > 0.0:
                    previewBrushSize = parsedBrushSize;
                    break;
                case "--preview-roughness" when ++index < args.Length &&
                                                 double.TryParse(
                                                     args[index],
                                                     System.Globalization.NumberStyles.Float,
                                                     System.Globalization.CultureInfo.InvariantCulture,
                                                     out var parsedRoughness) &&
                                                 parsedRoughness is >= 0.0 and <= 1.0:
                    previewRoughness = parsedRoughness;
                    break;
                case "--preview-color-compression" when ++index < args.Length &&
                                                         double.TryParse(
                                                             args[index],
                                                             System.Globalization.NumberStyles.Float,
                                                             System.Globalization.CultureInfo.InvariantCulture,
                                                             out var parsedColorCompression) &&
                                                         parsedColorCompression is >= 0.0 and <= 10.0:
                    previewColorCompression =
                        parsedColorCompression;
                    break;
                default:
                    throw new ArgumentException("Invalid argument: " + args[index]);
            }
        }
        if (processId is null)
            throw new ArgumentException("--pid is required.");
        if (string.IsNullOrWhiteSpace(command))
            throw new ArgumentException("--command is required.");
        return new Options(
            processId.Value,
            command,
            allowLocalPreview,
            allowDirectStroke,
            allowBridgeShutdown,
            artifacts,
            previewBrushSize,
            previewRoughness,
            previewColorCompression);
    }

    private static AuthenticatedBridgeClient OpenResidentClient(TargetProcess target, string command)
    {
        if (!OperatingSystem.IsWindows())
            throw new PlatformNotSupportedException("Live resident bridge diagnostics require Windows.");
        using var mapping = MemoryMappedFile.OpenExisting(
            $@"Local\MecchaCamouflage.ResidentCore.{target.ProcessId}",
            MemoryMappedFileRights.Read);
        var header = ReadMappedBytes(mapping, 12);
        var mappedSize = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(4, 4));
        if (mappedSize is not (ResidentCoreSizeV1 or ResidentCoreSizeV2))
            throw new InvalidOperationException("The resident bridge rendezvous has an unsupported size.");
        var bytes = ReadMappedBytes(mapping, checked((int)mappedSize));
        var span = bytes.AsSpan();
        var magic = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(0, 4));
        var size = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(4, 4));
        var abi = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(8, 4));
        var mappedPid = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(12, 4));
        var port = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(16, 4));
        var protocol = BinaryPrimitives.ReadUInt32LittleEndian(span.Slice(20, 4));
        var legacy = magic == ResidentCoreMagicV1 && size == ResidentCoreSizeV1 && abi == 1;
        var current = magic == ResidentCoreMagicV2 && size == ResidentCoreSizeV2 && abi == 2;
        if ((!legacy && !current) ||
            mappedPid != target.ProcessId || protocol != (legacy ? 1U : 2U) || port is 0 or > 65535 ||
            !HasEntropy(span.Slice(40, 32)))
        {
            throw new InvalidOperationException("The resident bridge rendezvous was invalid or belongs to another game instance.");
        }
        if (legacy && !string.Equals(command, "bridge-shutdown", StringComparison.Ordinal))
        {
            throw new InvalidOperationException(
                "A legacy resident bridge may only be opened for authenticated shutdown and generation replacement.");
        }

        var runtimeBundleId = current
            ? Convert.ToHexString(span.Slice(104, 32)).ToLowerInvariant()
            : null;
        if (current && !HasEntropy(span.Slice(104, 32)))
            throw new InvalidOperationException("The resident bridge has no runtime bundle identity.");

        // The token is kept only in the authenticated client and is never written to an artifact or console.
        return new AuthenticatedBridgeClient(
            target.ProcessId,
            (int)port,
            new Guid(span.Slice(24, 16), bigEndian: true),
            span.Slice(40, 32),
            Convert.ToHexString(span.Slice(72, 32)).ToLowerInvariant(),
            runtimeBundleId,
            protocol);
    }

    private static byte[] ReadMappedBytes(MemoryMappedFile mapping, int size)
    {
        using var view = mapping.CreateViewStream(0, size, MemoryMappedFileAccess.Read);
        var bytes = new byte[size];
        view.ReadExactly(bytes);
        return bytes;
    }

    private static bool HasEntropy(ReadOnlySpan<byte> value)
    {
        if (value.IsEmpty)
            return false;
        var first = value[0];
        foreach (var next in value)
        {
            if (next != first)
                return true;
        }
        return false;
    }

    private static async Task<ReplySummary> SendAndStoreAsync(
        AuthenticatedBridgeClient client,
        string name,
        string payload,
        string runDirectory,
        TimeSpan? timeout = null)
    {
        var reply = await RequestAndStoreRawAsync(client, name, payload, runDirectory, timeout);
        if (!reply.Ok || !reply.Success)
            throw new InvalidOperationException($"{name} failed at {reply.Stage}: {reply.Message}");
        return Summarize(reply);
    }

    private static async Task<BridgeReply> RequestAndStoreRawAsync(
        AuthenticatedBridgeClient client,
        string name,
        string payload,
        string runDirectory,
        TimeSpan? timeout = null)
    {
        var reply = await client.RequestAsync(payload, timeout ?? TimeSpan.FromSeconds(8));
        WriteRawReply(Path.Combine(runDirectory, name + ".json"), reply);
        return reply;
    }

    private static async Task RestoreStalePreviewAsync(
        AuthenticatedBridgeClient client,
        TargetProcess target,
        string runDirectory,
        IDictionary<string, object?> summary)
    {
        var reply = await client.RequestAsync(BuildRestorePayload(target.ProcessId), TimeSpan.FromSeconds(20));
        WriteRawReply(Path.Combine(runDirectory, "stale-preview-restore.json"), reply);
        // No snapshot is the normal clean state. Any other restore failure must stop before a probe.
        if (!reply.Ok || (!reply.Success && !string.Equals(reply.Stage, "mesh_unpreview_snapshot_unavailable", StringComparison.Ordinal)))
        {
            throw new InvalidOperationException($"stale preview restore failed at {reply.Stage}: {reply.Message}");
        }
        summary["stale_preview_restore"] = Summarize(reply);
    }

    private static string BuildPreviewPayload(
        TargetProcess target,
        double emissive,
        bool researchArtifacts = false,
        bool appearanceCaptureArtifacts = false,
        bool researchKeepSourceSeed = false,
        bool researchSkipPreviewFit = false,
        double researchSourceEmissivePower = 1.0,
        double researchSourceEmissiveOverride = -1.0,
        double researchEmissiveRadiusMultiplier = 1.0,
        bool researchUseScreenHitUv = false,
        bool diagnosticHoldBestCandidate = false,
        bool appearanceDiagnosticSamples = false,
        bool appearanceEmissionIsolationProbe = false,
        bool appearanceEmissionIsolationTargetVisible = false,
        double brushSize = 4.0,
        double roughness = 0.65,
        double colorCompressionTolerance = 5.0) =>
        JsonSerializer.Serialize(new
        {
            type = appearanceEmissionIsolationProbe
                ? "appearance_emission_isolation_probe"
                : "paint_full_route",
            native_apply_mode = "native_recorded_paint",
            route = "native_recorded_paint",
            preview_only = true,
            unpreview_only = false,
            diagnostic_hold_best_candidate = diagnosticHoldBestCandidate,
            appearance_diagnostic_samples =
                !appearanceEmissionIsolationProbe &&
                appearanceDiagnosticSamples,
            research_artifacts = researchArtifacts,
            appearance_capture_artifacts =
                appearanceCaptureArtifacts,
            appearance_emission_isolation_probe =
                appearanceEmissionIsolationProbe,
            appearance_emission_isolation_target_visible =
                appearanceEmissionIsolationTargetVisible,
            research_keep_source_seed = researchKeepSourceSeed,
            research_skip_preview_fit = researchSkipPreviewFit,
            research_source_emissive_power = researchSourceEmissivePower,
            research_source_emissive_override = researchSourceEmissiveOverride,
            research_emissive_radius_multiplier = researchEmissiveRadiusMultiplier,
            research_use_screen_hit_uv = researchUseScreenHitUv,
            process = new { pid = target.ProcessId, name = Path.GetFileName(target.ExecutablePath) },
            paint_source = new { kind = "environment_capture" },
            tuning = new
            {
                brush_size_texels = brushSize,
                metallic = 0.0,
                roughness,
                emissive,
                front_region_mode = "paint",
                side_region_mode = "paint",
                back_region_mode = "paint",
                fill_color_r = 1.0,
                fill_color_g = 1.0,
                fill_color_b = 1.0,
                fill_metallic = 0.0,
                fill_roughness = 0.65,
                fill_emissive = 0.0,
                color_compression_tolerance =
                    colorCompressionTolerance
            }
        });

    private static string BuildRestorePayload(int processId) =>
        JsonSerializer.Serialize(new
        {
            type = "paint_full_route",
            native_apply_mode = "native_recorded_paint",
            route = "native_recorded_paint",
            preview_only = false,
            unpreview_only = true,
            process = new { pid = processId, name = "" },
            paint_source = new { kind = "environment_capture" },
            tuning = new { }
        });

    private static string BuildDirectEmissiveChannelProbePayload(TargetProcess target) =>
        JsonSerializer.Serialize(new
        {
            type = "paint_full_route",
            native_apply_mode = "native_recorded_paint",
            route = "native_recorded_paint",
            preview_only = false,
            unpreview_only = false,
            research_artifacts = true,
            research_route_mode = "combined",
            research_stroke_limit = 1,
            research_target_channel = 6,
            research_apply_mode = 0,
            research_force_paint_color = true,
            research_paint_color_r = 0.0,
            research_paint_color_g = 0.0,
            research_paint_color_b = 0.0,
            process = new { pid = target.ProcessId, name = Path.GetFileName(target.ExecutablePath) },
            paint_source = new { kind = "environment_capture" },
            tuning = new
            {
                brush_size_texels = 1.0,
                metallic = 0.0,
                roughness = 0.65,
                emissive = 1.0,
                front_region_mode = "paint",
                side_region_mode = "skip",
                back_region_mode = "skip",
                color_compression_tolerance = 0.0
            }
        });

    private static string BuildEnvironmentDirectStrokePayload(TargetProcess target) =>
        JsonSerializer.Serialize(new
        {
            type = "paint_full_route",
            native_apply_mode = "native_recorded_paint",
            route = "native_recorded_paint",
            preview_only = false,
            unpreview_only = false,
            diagnostic_stroke_limit = 1,
            process = new
            {
                pid = target.ProcessId,
                name = Path.GetFileName(target.ExecutablePath)
            },
            paint_source = new { kind = "environment_capture" },
            tuning = new
            {
                brush_size_texels = 10.0,
                metallic = 0.0,
                roughness = 0.65,
                emissive = 0.0,
                front_region_mode = "paint",
                side_region_mode = "skip",
                back_region_mode = "skip",
                fill_color_r = 1.0,
                fill_color_g = 1.0,
                fill_color_b = 1.0,
                fill_metallic = 0.0,
                fill_roughness = 0.65,
                fill_emissive = 0.0,
                color_compression_tolerance = 10.0
            }
        });

    private static void RequirePreviewOptIn(Options options)
    {
        if (!options.AllowLocalPreview)
            throw new ArgumentException(options.Command + " requires --allow-local-preview.");
    }

    private static void RequireDirectStrokeOptIn(Options options)
    {
        if (!options.AllowDirectStroke)
            throw new ArgumentException(options.Command + " requires --allow-direct-stroke.");
    }

    private static void RequireBridgeShutdownOptIn(Options options)
    {
        if (!options.AllowBridgeShutdown)
            throw new ArgumentException(options.Command + " requires --allow-bridge-shutdown.");
    }

    private static ulong ExtractResolvedTextureComponent(BridgeReply reply)
    {
        try
        {
            using var document = JsonDocument.Parse(reply.Raw);
            var metadata = document.RootElement.GetProperty("metadata");
            foreach (var item in metadata.GetProperty("runtime_paint_component_inventory").EnumerateArray())
            {
                if (!item.TryGetProperty("matches_texture_export_target", out var selected) || !selected.GetBoolean() ||
                    !item.TryGetProperty("component", out var component))
                {
                    continue;
                }
                var text = component.GetString();
                if (text is null || !text.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ||
                    !ulong.TryParse(text[2..], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var address) ||
                    address == 0)
                {
                    break;
                }
                return address;
            }
        }
        catch (JsonException)
        {
            // Fall through to the same evidence failure below.
        }
        throw new InvalidOperationException("The texture probe did not identify one readable target paint component.");
    }

    private static object CaptureMaterialInventory(int processId, BridgeReply reply)
    {
        var components = new List<object>();
        try
        {
            using var document = JsonDocument.Parse(reply.Raw);
            var metadata = document.RootElement.GetProperty("metadata");
            foreach (var item in metadata.GetProperty("runtime_paint_component_inventory").EnumerateArray())
            {
                if (!TryReadHexAddress(item, "component", out var address))
                    continue;
                try
                {
                    components.Add(new
                    {
                        component = "0x" + address.ToString("x", CultureInfo.InvariantCulture),
                        inspection = MaterialMemoryProbe.Capture(processId, address)
                    });
                }
                catch (Exception exception)
                {
                    // An unrelated blueprint variable may have been destroyed while the read-only scan ran.
                    // Preserve that fact instead of retrying against a potentially different object.
                    components.Add(new
                    {
                        component = "0x" + address.ToString("x", CultureInfo.InvariantCulture),
                        inspection_error = exception.Message
                    });
                }
            }
        }
        catch (JsonException exception)
        {
            throw new InvalidOperationException("The material inventory reply had invalid JSON.", exception);
        }
        return new { read_only = true, component_count = components.Count, components };
    }

    private static bool TryReadHexAddress(JsonElement element, string propertyName, out ulong address)
    {
        address = 0;
        if (!element.TryGetProperty(propertyName, out var value))
            return false;
        var text = value.GetString();
        return text is not null && text.StartsWith("0x", StringComparison.OrdinalIgnoreCase) &&
               ulong.TryParse(text[2..], NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out address) &&
               address != 0;
    }

    private static ReplySummary Summarize(BridgeReply reply)
    {
        object? metadata = null;
        try
        {
            using var document = JsonDocument.Parse(reply.Raw);
            if (document.RootElement.TryGetProperty("metadata", out var value))
                metadata = SummarizeMetadata(value);
        }
        catch (JsonException)
        {
            metadata = new { parse_error = true };
        }
        return new ReplySummary(reply.Ok, reply.Success, reply.Stage, reply.Message, metadata);
    }

    private static object? SummarizeMetadata(JsonElement metadata)
    {
        if (metadata.ValueKind != JsonValueKind.Object)
            return null;
        var selected = new SortedDictionary<string, object?>();
        foreach (var property in metadata.EnumerateObject())
        {
            if (property.Name.StartsWith("appearance_", StringComparison.Ordinal) ||
                property.Name.StartsWith("environment_", StringComparison.Ordinal) ||
                property.Name.StartsWith("preview_", StringComparison.Ordinal) ||
                property.Name.StartsWith("unpreview_", StringComparison.Ordinal) ||
                property.Name is "material_properties_mode" or "direct_route_only" or
                    "local_paint_rpc" or "local_paint_target_channel" or
                    "paint_target_channel" or "function_paint_at_uv_with_brush_available" or
                    "runtime_paint_component_inventory_detected_count" or
                    "runtime_paint_component_inventory_returned_count" or
                    "research_texture_export_target_found")
            {
                selected[property.Name] = property.Value.Clone();
            }
        }
        if (metadata.TryGetProperty("runtime_paint_component_inventory", out var inventory) &&
            inventory.ValueKind == JsonValueKind.Array)
        {
            selected["texture_components"] = SummarizeTextureInventory(inventory);
        }
        return selected;
    }

    private static object[] SummarizeTextureInventory(JsonElement inventory) =>
        inventory.EnumerateArray().Select(item => new
        {
            resolved = ReadBool(item, "matches_resolved_component"),
            target = ReadBool(item, "matches_texture_export_target"),
            albedo = Checksum(item, "albedo_export"),
            metallic = Checksum(item, "metallic_export"),
            roughness = Checksum(item, "roughness_export"),
            emissive = Checksum(item, "emissive_export"),
            delta = item.TryGetProperty("texture_delta", out var delta) ? delta.Clone() : default(JsonElement?)
        }).ToArray();

    private static object? Checksum(JsonElement item, string name) =>
        item.TryGetProperty(name, out var value) ? value.Clone() : null;

    private static bool? ReadBool(JsonElement item, string name) =>
        item.TryGetProperty(name, out var value) && value.ValueKind is JsonValueKind.True or JsonValueKind.False
            ? value.GetBoolean()
            : null;

    private static void WriteRawReply(string path, BridgeReply reply) =>
        File.WriteAllText(path, string.IsNullOrWhiteSpace(reply.Raw)
            ? JsonSerializer.Serialize(new { reply.Ok, reply.Success, reply.Stage, reply.Message })
            : reply.Raw);

    private static void WriteJson(string path, object value) =>
        File.WriteAllText(path, JsonSerializer.Serialize(value, new JsonSerializerOptions { WriteIndented = true }));
}
