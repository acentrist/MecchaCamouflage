param(
    [int[]]$Ports = @(47800, 47801, 47802, 47803),
    [int]$Samples = 60,
    [int]$IntervalMs = 250,
    [string]$HostName = "127.0.0.1"
)

$ErrorActionPreference = "Stop"

function Send-BridgeRequest {
    param(
        [int]$Port,
        [string]$Json
    )
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connect = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $connect.AsyncWaitHandle.WaitOne(750)) {
            return $null
        }
        $client.EndConnect($connect)
        $stream = $client.GetStream()
        $bytes = [Text.Encoding]::UTF8.GetBytes($Json + [Environment]::NewLine)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
        return $reader.ReadToEnd()
    }
    catch {
        return $null
    }
    finally {
        $client.Close()
    }
}

function Find-BridgePort {
    foreach ($port in $Ports) {
        $raw = Send-BridgeRequest -Port $port -Json '{"type":"ping"}'
        if ([string]::IsNullOrWhiteSpace($raw)) {
            continue
        }
        try {
            $parsed = $raw | ConvertFrom-Json
            if ($parsed.success) {
                return $port
            }
        }
        catch {
        }
    }
    return $null
}

function MetadataValue {
    param(
        [object]$Metadata,
        [string]$Name,
        [object]$Fallback = $null
    )
    if ($null -eq $Metadata) {
        return $Fallback
    }
    $property = $Metadata.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $Fallback
    }
    return $property.Value
}

$port = Find-BridgePort
if ($null -eq $port) {
    Write-Error "Bridge is not reachable on ports: $($Ports -join ', '). Start the app, inject the bridge, then run again."
}

Write-Host "Sampling paint replication pressure on port $port ($Samples samples, ${IntervalMs}ms interval)."
Write-Host "time,queue,component_queue,global_queue,pressure_queue,batches,max_per_tick,reported_ticks,delta_queue,observed_rate_per_s,drain_rate_per_s,tick_equiv_per_s"

$previousQueue = $null
$previousAt = $null
for ($i = 0; $i -lt $Samples; ++$i) {
    $now = Get-Date
    $raw = Send-BridgeRequest -Port $port -Json '{"type":"paint_replication_pressure_probe"}'
    if ([string]::IsNullOrWhiteSpace($raw)) {
        Write-Host "$($now.ToString('HH:mm:ss.fff')),probe_failed"
        Start-Sleep -Milliseconds $IntervalMs
        continue
    }

    try {
        $parsed = $raw | ConvertFrom-Json
        $meta = $parsed.metadata
        $componentQueue = [int](MetadataValue $meta "replication_manager_component_queued_count" (MetadataValue $meta "global_replication_manager_component_queued_count" -1))
        $globalQueue = [int](MetadataValue $meta "replication_manager_queued_count" (MetadataValue $meta "global_replication_manager_queued_count" -1))
        $pressureQueue = [int](MetadataValue $meta "replication_queued_stroke_count" (MetadataValue $meta "global_replication_queued_stroke_count" -1))
        $queuedBatches = [int](MetadataValue $meta "replication_queued_batch_count" (MetadataValue $meta "global_replication_queued_batch_count" -1))
        $maxPerTick = [int](MetadataValue $meta "replication_max_strokes_per_tick" (MetadataValue $meta "global_replication_max_strokes_per_tick" -1))
        $reportedTicks = [double](MetadataValue $meta "replication_estimated_ticks_to_drain" (MetadataValue $meta "global_replication_estimated_ticks_to_drain" -1))

        $queue = $componentQueue
        if ($queue -lt 0) { $queue = $pressureQueue }
        if ($queue -lt 0) { $queue = $globalQueue }

        $deltaQueue = ""
        $observedRate = ""
        $drainRate = ""
        $tickEquivalent = ""
        if ($null -ne $previousQueue -and $queue -ge 0 -and $null -ne $previousAt) {
            $elapsedSeconds = [Math]::Max(0.001, ($now - $previousAt).TotalSeconds)
            $delta = $queue - $previousQueue
            $deltaQueue = $delta
            $observedRateValue = $delta / $elapsedSeconds
            $observedRate = [Math]::Round($observedRateValue, 2)
            if ($delta -lt 0) {
                $drainRateValue = (-$delta) / $elapsedSeconds
                $drainRate = [Math]::Round($drainRateValue, 2)
                if ($maxPerTick -gt 0) {
                    $tickEquivalent = [Math]::Round($drainRateValue / $maxPerTick, 3)
                }
            }
        }

        Write-Host "$($now.ToString('HH:mm:ss.fff')),$queue,$componentQueue,$globalQueue,$pressureQueue,$queuedBatches,$maxPerTick,$reportedTicks,$deltaQueue,$observedRate,$drainRate,$tickEquivalent"
        if ($queue -ge 0) {
            $previousQueue = $queue
            $previousAt = $now
        }
    }
    catch {
        Write-Host "$($now.ToString('HH:mm:ss.fff')),parse_failed,$($_.Exception.Message)"
    }

    Start-Sleep -Milliseconds $IntervalMs
}
