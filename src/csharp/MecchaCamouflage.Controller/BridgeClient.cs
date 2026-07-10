using System.Net.Sockets;
using System.Text;
using System.Text.Json;

namespace MecchaCamouflage.Controller;

public sealed record BridgeReply(bool Ok, bool Success, string Stage, string Message, string Raw, int? ProcessId = null);

public sealed class BridgeClient
{
    private readonly string host;
    private readonly int port;
    private readonly TimeSpan? timeout;

    public BridgeClient(string host = "127.0.0.1", int port = 47800, TimeSpan? timeout = null)
    {
        this.host = host;
        this.port = port;
        this.timeout = timeout;
    }

    public async Task<BridgeReply> RequestAsync(string jsonLine, CancellationToken cancellationToken = default, TimeSpan? timeoutOverride = null)
    {
        try
        {
            using var timeoutCts = CreateTimeoutToken(cancellationToken, timeoutOverride ?? timeout);
            var token = timeoutCts?.Token ?? cancellationToken;
            using var client = new TcpClient();
            await client.ConnectAsync(host, port, token);
            await using var stream = client.GetStream();
            var request = Encoding.UTF8.GetBytes(jsonLine.EndsWith('\n') ? jsonLine : jsonLine + "\n");
            await stream.WriteAsync(request, token);
            await stream.FlushAsync(token);
            using var reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
            var response = await reader.ReadToEndAsync(token);
            return Parse(response);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return new BridgeReply(false, false, "transport_error", "Bridge request timed out.", "");
        }
        catch (OperationCanceledException)
        {
            return new BridgeReply(false, false, "transport_error", "Bridge request canceled.", "");
        }
        catch (Exception ex)
        {
            return new BridgeReply(false, false, "transport_error", ex.Message, "");
        }
    }

    public Task<BridgeReply> PingAsync(CancellationToken cancellationToken = default, TimeSpan? timeoutOverride = null) =>
        RequestAsync("{\"type\":\"ping\"}", cancellationToken, timeoutOverride ?? TimeSpan.FromSeconds(2.5));

    public Task<BridgeReply> CancelPaintAsync(CancellationToken cancellationToken = default) =>
        RequestAsync("{\"type\":\"cancel_paint\"}", cancellationToken, TimeSpan.FromSeconds(5));

    public Task<BridgeReply> ShutdownAsync(CancellationToken cancellationToken = default) =>
        RequestAsync("{\"type\":\"shutdown\"}", cancellationToken, TimeSpan.FromSeconds(5));

    private static CancellationTokenSource? CreateTimeoutToken(CancellationToken cancellationToken, TimeSpan? timeout)
    {
        if (timeout is null || timeout.Value <= TimeSpan.Zero)
            return null;
        var cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        cts.CancelAfter(timeout.Value);
        return cts;
    }

    private static BridgeReply Parse(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
            return new BridgeReply(false, false, "empty_response", "Bridge returned no response.", raw);
        try
        {
            using var doc = JsonDocument.Parse(raw);
            var root = doc.RootElement;
            var success = root.TryGetProperty("success", out var successProp) && successProp.GetBoolean();
            var stage = root.TryGetProperty("stage", out var stageProp) ? stageProp.GetString() ?? "" : "";
            var message = root.TryGetProperty("message", out var messageProp) ? messageProp.GetString() ?? "" : "";
            int? processId = null;
            if (root.TryGetProperty("metadata", out var metadata) &&
                metadata.ValueKind == JsonValueKind.Object &&
                metadata.TryGetProperty("pid", out var pidProp) &&
                pidProp.TryGetInt32(out var pid))
            {
                processId = pid;
            }
            return new BridgeReply(true, success, stage, message, raw, processId);
        }
        catch (Exception ex)
        {
            return new BridgeReply(false, false, "parse_error", ex.Message, raw);
        }
    }
}
