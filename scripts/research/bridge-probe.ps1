param(
    [ValidateSet("ping", "capabilities", "paint_replication_probe", "paint_replication_pressure_probe", "paint_packed_replay_probe")]
    [string]$Type = "paint_replication_probe",
    [string]$Json = "",
    [int[]]$Ports = @(47800, 47801, 47802, 47803),
    [string]$HostName = "127.0.0.1",
    [string]$OutFile = "",
    [switch]$AllowReplay,
    [switch]$Raw
)

$ErrorActionPreference = "Stop"

function Send-BridgeRequest {
    param(
        [int]$Port,
        [string]$RequestJson
    )

    $client = [Net.Sockets.TcpClient]::new()
    try {
        $connect = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $connect.AsyncWaitHandle.WaitOne(750)) {
            return $null
        }
        $client.EndConnect($connect)

        $stream = $client.GetStream()
        $bytes = [Text.Encoding]::UTF8.GetBytes($RequestJson + [Environment]::NewLine)
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
        $raw = Send-BridgeRequest -Port $port -RequestJson '{"type":"ping"}'
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

function Get-RequestType {
    param([Parameter(Mandatory = $true)][string]$RequestJson)
    try {
        $parsed = $RequestJson | ConvertFrom-Json
    }
    catch {
        throw "Request JSON could not be parsed: $($_.Exception.Message)"
    }

    $property = $parsed.PSObject.Properties["type"]
    if ($null -eq $property -or [string]::IsNullOrWhiteSpace([string]$property.Value)) {
        throw "Request JSON must contain a non-empty type field."
    }
    return [string]$property.Value
}

$allowedTypes = @(
    "ping",
    "capabilities",
    "paint_replication_probe",
    "paint_replication_pressure_probe",
    "paint_packed_replay_probe"
)

$requestJson = $Json
if ([string]::IsNullOrWhiteSpace($requestJson)) {
    $requestJson = "{""type"":""$Type""}"
}

$requestType = Get-RequestType -RequestJson $requestJson
if ($allowedTypes -notcontains $requestType) {
    throw "Bridge request type is not a research entrypoint: $requestType"
}

if ($requestType -eq "paint_packed_replay_probe" -and -not $AllowReplay) {
    throw "paint_packed_replay_probe can submit a packed replay request. Re-run with -AllowReplay if this is intentional."
}

$port = Find-BridgePort
if ($null -eq $port) {
    throw "Bridge is not reachable on ports: $($Ports -join ', '). Start the app, inject the bridge, then run again."
}

$rawResponse = Send-BridgeRequest -Port $port -RequestJson $requestJson
if ([string]::IsNullOrWhiteSpace($rawResponse)) {
    throw "Bridge request returned no response on port $port."
}

if (-not [string]::IsNullOrWhiteSpace($OutFile)) {
    $parent = Split-Path -Parent $OutFile
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Set-Content -LiteralPath $OutFile -Value $rawResponse -Encoding UTF8
}

if ($Raw) {
    Write-Output $rawResponse
    return
}

try {
    $rawResponse | ConvertFrom-Json | ConvertTo-Json -Depth 64
}
catch {
    Write-Output $rawResponse
}
