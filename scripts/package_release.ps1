param(
    [string]$Version = "1.0.0",
    [string]$OutDir = "",
    [string]$ExePath = "",
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [switch]$IncludeRuntimeSource = $false
)

$ErrorActionPreference = "Stop"

if (-not $OutDir) { $OutDir = Join-Path $RuntimeRoot ".build\package" }
$ArtifactName = "meccha-camouflage-$Version"
if (-not $ExePath) { $ExePath = Join-Path $RuntimeRoot ".build\bin\meccha-camouflage.exe" }
if (-not (Test-Path $ExePath -PathType Leaf)) { throw "Executable not found: $ExePath. Run scripts/build_runtime.ps1 first." }

$TmpRoot = Join-Path $OutDir "tmp-release"
Remove-Item -Recurse -Force $TmpRoot -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $TmpRoot | Out-Null

Copy-Item -Force $ExePath (Join-Path $TmpRoot $([System.IO.Path]::GetFileName($ExePath)))
Copy-Item -Force (Join-Path $RuntimeRoot "README.md") (Join-Path $TmpRoot "README.md")

Set-Content -Encoding ASCII -Path (Join-Path $TmpRoot "runtime-config.json") -Value @'
{
  "version": "%VERSION%",
  "runtime": "cpp",
  "mode": "service",
  "game_process_name": "PenguinHotel-Win64-Shipping.exe",
  "log_dir": "%LOCALAPPDATA%\\MecchaCamouflage\\runtime"
}
'@.Replace("%VERSION%", $Version)

if ($IncludeRuntimeSource) {
    Copy-Item -Recurse -Force (Join-Path $RuntimeRoot "runtime") (Join-Path $TmpRoot "runtime")
    Copy-Item -Recurse -Force (Join-Path $RuntimeRoot "scripts") (Join-Path $TmpRoot "scripts")
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ZipPath = Join-Path $OutDir "$ArtifactName.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
$Zip = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $Root = (Resolve-Path $TmpRoot).Path.TrimEnd("\", "/") + [System.IO.Path]::DirectorySeparatorChar
    Get-ChildItem $TmpRoot -Recurse -File | ForEach-Object {
        $FullPath = (Resolve-Path $_.FullName).Path
        $RelativePath = $FullPath.Substring($Root.Length).Replace("\", "/")
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($Zip, $_.FullName, $RelativePath, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $Zip.Dispose()
}

Remove-Item -Recurse -Force $TmpRoot
Write-Host "Wrote $ZipPath"
