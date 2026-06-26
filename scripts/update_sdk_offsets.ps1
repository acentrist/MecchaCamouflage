param(
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$DumperOutputRoot = "",
    [string]$SdkHeader = "",
    [switch]$Check
)

$ErrorActionPreference = "Stop"

if (-not $DumperOutputRoot) {
    $DumperOutputRoot = Join-Path $RuntimeRoot "dumper-sdk"
}
if (-not $SdkHeader) {
    $SdkHeader = Join-Path $RuntimeRoot "runtime\sdk\meccha_sdk_min.hpp"
}

$DumperOutputRoot = (Resolve-Path $DumperOutputRoot).Path
$SdkHeader = (Resolve-Path $SdkHeader).Path

$offsetsFile = Get-ChildItem -Path $DumperOutputRoot -Recurse -File -Filter "OffsetsInfo.json" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $offsetsFile) {
    throw "OffsetsInfo.json not found under $DumperOutputRoot"
}

$offsetsJson = Get-Content -Raw -Path $offsetsFile.FullName | ConvertFrom-Json
$offsets = @{}
foreach ($entry in $offsetsJson.data) {
    if ($entry.Count -ge 2) {
        $offsets[[string]$entry[0]] = [int64]$entry[1]
    }
}

$required = @(
    "OFFSET_GOBJECTS",
    "OFFSET_GWORLD",
    "OFFSET_PROCESSEVENT",
    "INDEX_PROCESSEVENT"
)
foreach ($key in $required) {
    if (-not $offsets.ContainsKey($key)) {
        throw "$key not found in $($offsetsFile.FullName)"
    }
}

function Format-CppHex([int64]$value) {
    return "0x{0:X8}" -f $value
}

$replacements = @{
    'GObjects' = Format-CppHex $offsets["OFFSET_GOBJECTS"]
    'GWorld' = Format-CppHex $offsets["OFFSET_GWORLD"]
    'ProcessEvent' = Format-CppHex $offsets["OFFSET_PROCESSEVENT"]
    'ProcessEventIdx' = Format-CppHex $offsets["INDEX_PROCESSEVENT"]
}

$original = Get-Content -Raw -Path $SdkHeader
$updated = $original
$updated = [regex]::Replace($updated, '(constexpr std::uintptr_t GObjects = )0x[0-9A-Fa-f]+;', "`${1}$($replacements.GObjects);")
$updated = [regex]::Replace($updated, '(constexpr std::uintptr_t GWorld = )0x[0-9A-Fa-f]+;', "`${1}$($replacements.GWorld);")
$updated = [regex]::Replace($updated, '(constexpr std::uintptr_t ProcessEvent = )0x[0-9A-Fa-f]+;', "`${1}$($replacements.ProcessEvent);")
$updated = [regex]::Replace($updated, '(constexpr int ProcessEventIdx = )0x[0-9A-Fa-f]+;', "`${1}$($replacements.ProcessEventIdx);")

Write-Host "SDK offsets source:"
Write-Host "  $($offsetsFile.FullName)"
Write-Host "SDK header:"
Write-Host "  $SdkHeader"
Write-Host "Resolved offsets:"
Write-Host "  GObjects       = $($replacements.GObjects)"
Write-Host "  GWorld         = $($replacements.GWorld)"
Write-Host "  ProcessEvent   = $($replacements.ProcessEvent)"
Write-Host "  ProcessEventIdx= $($replacements.ProcessEventIdx)"

if ($updated -eq $original) {
    Write-Host "SDK offsets already up to date."
    exit 0
}

if ($Check) {
    Write-Host "SDK offsets are out of date. Run make sdk-sync to update."
    exit 1
}

Set-Content -Encoding UTF8 -NoNewline -Path $SdkHeader -Value $updated
Write-Host "Updated runtime SDK offsets."
