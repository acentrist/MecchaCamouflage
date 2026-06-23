param(
    [string]$ProcessName = "PenguinHotel-Win64-Shipping.exe",
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$DumperRoot = "",
    [string]$OutputRoot = "",
    [int]$SleepTimeoutSeconds = 3,
    [switch]$BuildDumper,
    [switch]$WaitForProcess,
    [int]$WaitTimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"

if (-not $DumperRoot) {
    $DumperRoot = Join-Path $RuntimeRoot "Dumper-7"
}
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $RuntimeRoot "dumper-sdk"
}

$DumperRoot = (Resolve-Path $DumperRoot).Path
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$OutputRoot = (Resolve-Path $OutputRoot).Path

$DumperDll = Join-Path $DumperRoot "build\bin\Release\Dumper-7.dll"
$Injector = Join-Path $RuntimeRoot "native\bin\meccha-xenos-injector.exe"
$GlobalConfigDir = "C:\Dumper-7"
$GlobalConfigPath = Join-Path $GlobalConfigDir "Dumper-7.ini"

function Get-CMakeExe {
    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    throw "cmake.exe not found"
}

if ($BuildDumper -or -not (Test-Path $DumperDll)) {
    $cmake = Get-CMakeExe
    Write-Host "Building Dumper-7..."
    & $cmake -S $DumperRoot -B (Join-Path $DumperRoot "build") -A x64
    if ($LASTEXITCODE -ne 0) {
        throw "Dumper-7 configure failed"
    }
    & $cmake --build (Join-Path $DumperRoot "build") --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Dumper-7 build failed"
    }
}

if (-not (Test-Path $DumperDll)) {
    throw "Dumper-7 DLL not found: $DumperDll"
}
if (-not (Test-Path $Injector)) {
    throw "Injector not found. Run ./scripts/dev_flow.sh -Action build first. Expected: $Injector"
}

New-Item -ItemType Directory -Force -Path $GlobalConfigDir | Out-Null
$config = @(
    "[Settings]",
    "SDKGenerationPath=$($OutputRoot.Replace('\', '/'))",
    "SleepTimeout=$SleepTimeoutSeconds"
)
Set-Content -Encoding ASCII -Path $GlobalConfigPath -Value $config
Write-Host "Configured Dumper-7:"
Write-Host "  dll=$DumperDll"
Write-Host "  output=$OutputRoot"
Write-Host "  config=$GlobalConfigPath"

$deadline = (Get-Date).AddSeconds($WaitTimeoutSeconds)
do {
    $proc = Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($ProcessName)) -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) {
        break
    }
    if (-not $WaitForProcess) {
        throw "Process not found: $ProcessName"
    }
    if ((Get-Date) -gt $deadline) {
        throw "Timed out waiting for process: $ProcessName"
    }
    Write-Host "Waiting for $ProcessName..."
    Start-Sleep -Seconds 2
} while ($true)

Write-Host "Injecting Dumper-7..."
& $Injector $ProcessName $DumperDll
if ($LASTEXITCODE -ne 0) {
    throw "Dumper-7 injection failed with exit code $LASTEXITCODE"
}

Write-Host "Waiting for generated CppSDK..."
$sdkDeadline = (Get-Date).AddSeconds([Math]::Max(120, $WaitTimeoutSeconds))
do {
    $cppSdk = Get-ChildItem -Path $OutputRoot -Recurse -Directory -Filter "CppSDK" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($cppSdk) {
        Write-Host "Generated CppSDK:"
        Write-Host "  $($cppSdk.FullName)"
        exit 0
    }
    if ((Get-Date) -gt $sdkDeadline) {
        throw "Timed out waiting for generated CppSDK under $OutputRoot"
    }
    Start-Sleep -Seconds 3
} while ($true)
