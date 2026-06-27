param(
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutDir = "",
    [string]$ExeName = "meccha-camouflage"
)

$ErrorActionPreference = "Stop"

if (-not $OutDir) {
    $OutDir = Join-Path $RuntimeRoot ".build\bin"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$ObjDir = Join-Path $RuntimeRoot ".build\obj"
New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null

$BridgeSource = Join-Path $RuntimeRoot "runtime\src\bridge.cpp"
$InjectorSource = Join-Path $RuntimeRoot "runtime\src\injector.cpp"
$ControllerSources = @(
    (Join-Path $RuntimeRoot "runtime\src\controller.cpp"),
    (Join-Path $RuntimeRoot "runtime\src\controller_settings.cpp"),
    (Join-Path $RuntimeRoot "runtime\src\controller_events.cpp"),
    (Join-Path $RuntimeRoot "runtime\src\controller_hotkeys.cpp"),
    (Join-Path $RuntimeRoot "runtime\src\controller_ui.cpp")
)
$ImguiRoot = Join-Path $RuntimeRoot "third_party\imgui"
$ImguiBackendRoot = Join-Path $ImguiRoot "backends"
$IconSource = Join-Path $RuntimeRoot "assets\icon.ico"
$FontArchive = Join-Path $RuntimeRoot "assets\fonts\d-din.zip"
$FontExtractDir = Join-Path $ObjDir "fonts"
$FontRegularPath = Join-Path $FontExtractDir "D-DIN.otf"
$FontBoldPath = Join-Path $FontExtractDir "D-DIN-Bold.otf"
$FontCondensedPath = Join-Path $FontExtractDir "D-DINCondensed.otf"
$ImguiSources = @(
    (Join-Path $ImguiRoot "imgui.cpp"),
    (Join-Path $ImguiRoot "imgui_draw.cpp"),
    (Join-Path $ImguiRoot "imgui_tables.cpp"),
    (Join-Path $ImguiRoot "imgui_widgets.cpp"),
    (Join-Path $ImguiBackendRoot "imgui_impl_win32.cpp"),
    (Join-Path $ImguiBackendRoot "imgui_impl_dx11.cpp")
)
foreach ($source in @($BridgeSource, $InjectorSource) + $ControllerSources + $ImguiSources) {
    if (-not (Test-Path $source)) {
        throw "Source not found: $source"
    }
}
if (-not (Test-Path $IconSource)) {
    throw "Application icon not found: $IconSource"
}
if (-not (Test-Path $FontArchive)) {
    throw "Application font archive not found: $FontArchive"
}

function Quote-CmdArg([string]$Value) {
    if ($Value -match '^[A-Za-z0-9_./:=+\-\\]+$') {
        return $Value
    }
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Get-VsDevCmd {
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $VsWhere)) { return "" }
    $VsInstall = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VsInstall) { return "" }
    $VsDevCmd = Join-Path $VsInstall "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $VsDevCmd) { return $VsDevCmd }
    return ""
}

function Invoke-VsToolCommand {
    param(
        [Parameter(Mandatory = $true)][string]$ToolName,
        [Parameter(Mandatory = $true)][string[]]$ToolArgs
    )
    if (Get-Command $ToolName -ErrorAction SilentlyContinue) {
        & $ToolName @ToolArgs
        if ($LASTEXITCODE -ne 0) { throw "$ToolName failed with exit code $LASTEXITCODE" }
        return
    }
    $VsDevCmd = Get-VsDevCmd
    if (-not $VsDevCmd) {
        throw "$ToolName was not found. Install Visual Studio 2022 Build Tools or run from a VS Developer PowerShell."
    }
    $ArgText = ($ToolArgs | ForEach-Object { Quote-CmdArg $_ }) -join " "
    $CommandLine = "$(Quote-CmdArg $VsDevCmd) -arch=x64 -host_arch=x64 >nul && $ToolName $ArgText"
    cmd /d /c $CommandLine
    if ($LASTEXITCODE -ne 0) { throw "$ToolName failed with exit code $LASTEXITCODE" }
}

function Get-ExeBaseName {
    param([string]$Name)
    $candidate = (New-Object System.IO.FileInfo($Name)).BaseName
    if ([string]::IsNullOrWhiteSpace($candidate)) { return "meccha-camouflage" }
    return $candidate
}

function Extract-ZipEntry {
    param(
        [Parameter(Mandatory = $true)]$Zip,
        [Parameter(Mandatory = $true)][string]$EntryName,
        [Parameter(Mandatory = $true)][string]$OutPath
    )
    $Entry = $Zip.Entries | Where-Object { $_.FullName -eq $EntryName } | Select-Object -First 1
    if (-not $Entry) { throw "Font entry not found in archive: $EntryName" }
    if (Test-Path $OutPath) { Remove-Item -Force $OutPath }
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($Entry, $OutPath)
}

$ExeName = Get-ExeBaseName -Name $ExeName

Push-Location $RuntimeRoot
try {
    $BridgeOutput = Join-Path $OutDir "runtime-bridge.dll"
    $InjectorOutput = Join-Path $OutDir "runtime-injector.exe"
    $ControllerOutput = Join-Path $OutDir "$ExeName.exe"

    Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
        "/nologo", "/std:c++17", "/EHsc", "/O2", "/LD", $BridgeSource,
        "/Fo:$(Join-Path $ObjDir 'bridge.obj')",
        "/Fe:$BridgeOutput",
        "Ws2_32.lib",
        "User32.lib"
    )
    Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
        "/nologo", "/EHsc", "/O2", $InjectorSource,
        "/Fo:$(Join-Path $ObjDir 'injector.obj')",
        "/Fe:$InjectorOutput"
    )

    if (-not (Test-Path $BridgeOutput)) { throw "Bridge DLL was not produced: $BridgeOutput" }

    $ResourceRc = Join-Path $ObjDir "controller.rc"
    $ResourceRes = Join-Path $ObjDir "controller.res"
    $BridgeResourcePath = ((Resolve-Path $BridgeOutput).Path -replace '\\', '\\')
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Force -Path $FontExtractDir | Out-Null
    $FontZip = [System.IO.Compression.ZipFile]::OpenRead($FontArchive)
    try {
        Extract-ZipEntry -Zip $FontZip -EntryName "D-DIN.otf" -OutPath $FontRegularPath
        Extract-ZipEntry -Zip $FontZip -EntryName "D-DIN-Bold.otf" -OutPath $FontBoldPath
        Extract-ZipEntry -Zip $FontZip -EntryName "D-DINCondensed.otf" -OutPath $FontCondensedPath
    } finally {
        $FontZip.Dispose()
    }
    $IconResourcePath = ((Resolve-Path $IconSource).Path -replace '\\', '\\')
    $FontRegularResourcePath = ((Resolve-Path $FontRegularPath).Path -replace '\\', '\\')
    $FontBoldResourcePath = ((Resolve-Path $FontBoldPath).Path -replace '\\', '\\')
    $FontCondensedResourcePath = ((Resolve-Path $FontCondensedPath).Path -replace '\\', '\\')
    Set-Content -Encoding ASCII -Path $ResourceRc -Value @"
101 RCDATA "$BridgeResourcePath"
201 ICON "$IconResourcePath"
202 RCDATA "$FontRegularResourcePath"
203 RCDATA "$FontBoldResourcePath"
204 RCDATA "$FontCondensedResourcePath"
"@
    Invoke-VsToolCommand -ToolName "rc.exe" -ToolArgs @("/nologo", "/fo", $ResourceRes, $ResourceRc)

    $ControllerToolArgs = @(
        "/nologo", "/std:c++17", "/EHsc", "/O2",
        "/I$ImguiRoot", "/I$ImguiBackendRoot",
        $ResourceRes
    ) + $ControllerSources + $ImguiSources + @(
        "/Fo:$ObjDir\",
        "/Fe:$ControllerOutput",
        "Ws2_32.lib",
        "User32.lib",
        "Gdi32.lib",
        "D3d11.lib",
        "Shell32.lib",
        "Dwmapi.lib",
        "/link",
        "/SUBSYSTEM:WINDOWS"
    )
    Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs $ControllerToolArgs

    if (-not (Test-Path $ControllerOutput)) { throw "Controller EXE was not produced: $ControllerOutput" }
    if (-not (Test-Path $InjectorOutput)) { throw "Injector EXE was not produced: $InjectorOutput" }
}
finally {
    Pop-Location
}

Write-Host "Built runtime artifacts:"
Write-Host "  $(Join-Path $OutDir "$ExeName.exe")"
Write-Host "  $(Join-Path $OutDir 'runtime-bridge.dll')"
Write-Host "  $(Join-Path $OutDir 'runtime-injector.exe')"
