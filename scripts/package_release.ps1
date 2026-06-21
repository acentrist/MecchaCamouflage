param(
    [string]$BuildDir = "",
    [string]$Version = "1.0.0",
    [string]$OutDir = "",
    [string]$UE4SSRuntimeRoot = $env:UE4SS_RUNTIME_ROOT,
    [string]$UE4SSSourceRoot = $env:UE4SS_ROOT,
    [string]$UE4SSLicensePath = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ModName = "MecchaCamouflage"
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot "dist"
}
if (-not $UE4SSRuntimeRoot) {
    $UE4SSRuntimeRoot = Join-Path $RepoRoot "external\UE4SS-runtime"
}
if (-not $UE4SSSourceRoot) {
    $UE4SSSourceRoot = Join-Path $RepoRoot "external\RE-UE4SS"
}

function Resolve-FileFromRoot([string]$Root, [string[]]$RelativePaths, [string]$Name) {
    foreach ($RelativePath in $RelativePaths) {
        $Candidate = Join-Path $Root $RelativePath
        if (Test-Path $Candidate -PathType Leaf) {
            return (Resolve-Path $Candidate).Path
        }
    }
    throw "$Name was not found under $Root"
}

$DllFile = Get-ChildItem $BuildDir -Recurse -Filter "$ModName.dll" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $DllFile) {
    throw "$ModName.dll was not found under $BuildDir"
}

if (-not (Test-Path $UE4SSRuntimeRoot -PathType Container)) {
    throw "UE4SS runtime was not found. Extract UE4SS_v3.0.1.zip to external\UE4SS-runtime or pass -UE4SSRuntimeRoot."
}

$Dwmapi = Resolve-FileFromRoot $UE4SSRuntimeRoot @(
    "dwmapi.dll",
    "Chameleon\Binaries\Win64\dwmapi.dll"
) "dwmapi.dll"
$BuiltUE4SSDll = Get-ChildItem $BuildDir -Recurse -Filter "UE4SS.dll" |
    Where-Object { $_.FullName -match "\\Game__Shipping__Win64\\bin\\UE4SS\.dll$" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($BuiltUE4SSDll) {
    $UE4SSDll = $BuiltUE4SSDll.FullName
}
else {
    $UE4SSDll = Resolve-FileFromRoot $UE4SSRuntimeRoot @(
        "UE4SS.dll",
        "ue4ss\UE4SS.dll",
        "Chameleon\Binaries\Win64\ue4ss\UE4SS.dll"
    ) "UE4SS.dll"
}
$UE4SSSettings = $null
foreach ($RelativePath in @(
    "assets\UE4SS-settings.ini"
)) {
    $Candidate = Join-Path $UE4SSSourceRoot $RelativePath
    if (Test-Path $Candidate -PathType Leaf) {
        $UE4SSSettings = (Resolve-Path $Candidate).Path
        break
    }
}
if (-not $UE4SSSettings) {
    foreach ($RelativePath in @(
        "UE4SS-settings.ini",
        "ue4ss\UE4SS-settings.ini",
        "Chameleon\Binaries\Win64\ue4ss\UE4SS-settings.ini"
    )) {
        $Candidate = Join-Path $UE4SSRuntimeRoot $RelativePath
        if (Test-Path $Candidate -PathType Leaf) {
            $UE4SSSettings = (Resolve-Path $Candidate).Path
            break
        }
    }
}

$PackageName = "meccha-camouflage-$Version"
$Stage = Join-Path $OutDir $PackageName
$Win64Root = $Stage
$UE4SSRoot = $Win64Root
$ModsRoot = Join-Path $Win64Root "Mods"
$ModDllDir = Join-Path $ModsRoot "$ModName\dlls"
$SignaturesRoot = Join-Path $UE4SSRoot "UE4SS_Signatures"

Remove-Item -Recurse -Force $Stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $ModDllDir | Out-Null
New-Item -ItemType Directory -Force -Path $SignaturesRoot | Out-Null

Copy-Item -Force $DllFile.FullName (Join-Path $ModDllDir "main.dll")
Copy-Item -Force $Dwmapi (Join-Path $Win64Root "dwmapi.dll")
Copy-Item -Force $UE4SSDll (Join-Path $Win64Root "UE4SS.dll")
Copy-Item -Force (Join-Path $RepoRoot "README.md") (Join-Path $Stage "README.md")
Copy-Item -Force (Join-Path $RepoRoot "LICENSE.txt") (Join-Path $Stage "LICENSE.txt")
Copy-Item -Force (Join-Path $RepoRoot "UE4SS_Signatures\StaticConstructObject.lua") (Join-Path $SignaturesRoot "StaticConstructObject.lua")

@"
ConsoleEnabled = 0
GuiConsoleEnabled = 0
GuiConsoleVisible = 0
"@ | Set-Content -Encoding ASCII -Path (Join-Path $UE4SSRoot "UE4SS-settings.ini")
if ($UE4SSSettings) {
    $SettingsText = Get-Content -Raw $UE4SSSettings
    foreach ($Line in @(
        "MajorVersion = 5",
        "MinorVersion = 6",
        "ConsoleEnabled = 0",
        "GuiConsoleEnabled = 0",
        "GuiConsoleVisible = 0"
    )) {
        $Name = ($Line -split '=')[0].Trim()
        if ($SettingsText -match "(?m)^\s*$([regex]::Escape($Name))\s*=") {
            $SettingsText = $SettingsText -replace "(?m)^\s*$([regex]::Escape($Name))\s*=.*$", $Line
        }
        else {
            $SettingsText += "`r`n$Line"
        }
    }
    Set-Content -Encoding ASCII -Path (Join-Path $UE4SSRoot "UE4SS-settings.ini") -Value $SettingsText
}

Set-Content -Encoding ASCII -Path (Join-Path $ModsRoot "mods.txt") -Value "$ModName : 1"

$NoticePath = Join-Path $Stage "THIRD_PARTY_NOTICES.md"
@"
# Third Party Notices

This package bundles RE-UE4SS runtime files so the mod can be loaded by the game.

RE-UE4SS: https://github.com/UE4SS-RE/RE-UE4SS
"@ | Set-Content -Encoding ASCII -Path $NoticePath

if (-not $UE4SSLicensePath) {
    foreach ($Candidate in @(
        (Join-Path $UE4SSRuntimeRoot "LICENSE"),
        (Join-Path $UE4SSRuntimeRoot "LICENSE.txt"),
        (Join-Path $RepoRoot "external\RE-UE4SS\LICENSE"),
        (Join-Path $RepoRoot "external\RE-UE4SS\LICENSE.txt")
    )) {
        if (Test-Path $Candidate -PathType Leaf) {
            $UE4SSLicensePath = $Candidate
            break
        }
    }
}
if ($UE4SSLicensePath -and (Test-Path $UE4SSLicensePath -PathType Leaf)) {
    Copy-Item -Force $UE4SSLicensePath (Join-Path $Stage "UE4SS_LICENSE.txt")
}
else {
    Set-Content -Encoding ASCII -Path (Join-Path $Stage "UE4SS_LICENSE.txt") -Value "See https://github.com/UE4SS-RE/RE-UE4SS for RE-UE4SS license terms."
}

$ZipPath = Join-Path $OutDir "$PackageName.zip"
Remove-Item -Force $ZipPath -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$Zip = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $StageRoot = (Resolve-Path $Stage).Path.TrimEnd("\", "/") + [System.IO.Path]::DirectorySeparatorChar
    Get-ChildItem $Stage -Recurse -File | ForEach-Object {
        $FullPath = (Resolve-Path $_.FullName).Path
        $RelativePath = $FullPath.Substring($StageRoot.Length).Replace("\", "/")
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $Zip,
            $_.FullName,
            $RelativePath,
            [System.IO.Compression.CompressionLevel]::Optimal
        ) | Out-Null
    }
}
finally {
    $Zip.Dispose()
}
Write-Host "Wrote $ZipPath"
