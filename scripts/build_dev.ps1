param(
    [string]$UE4SSRoot = $env:UE4SS_ROOT,
    [string]$CMake = "cmake",
    [string]$Ninja = "",
    [string]$RustCompiler = "",
    [string]$RustCargo = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build-dev"

& (Join-Path $PSScriptRoot "build.ps1") `
    -UE4SSRoot $UE4SSRoot `
    -BuildDir $BuildDir `
    -CMake $CMake `
    -Ninja $Ninja `
    -RustCompiler $RustCompiler `
    -RustCargo $RustCargo

Write-Host "Dev build output: $BuildDir"
