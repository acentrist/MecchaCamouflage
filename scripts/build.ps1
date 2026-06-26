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
$ControllerSource = Join-Path $RuntimeRoot "runtime\src\controller.cpp"
foreach ($source in @($BridgeSource, $InjectorSource, $ControllerSource)) {
    if (-not (Test-Path $source)) {
        throw "Source not found: $source"
    }
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
    Set-Content -Encoding ASCII -Path $ResourceRc -Value "101 RCDATA `"$BridgeResourcePath`"`r`n"
    Invoke-VsToolCommand -ToolName "rc.exe" -ToolArgs @("/nologo", "/fo", $ResourceRes, $ResourceRc)

    Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
        "/nologo", "/std:c++17", "/EHsc", "/O2", $ControllerSource, $ResourceRes,
        "/Fo:$(Join-Path $ObjDir 'controller.obj')",
        "/Fe:$ControllerOutput",
        "Ws2_32.lib",
        "User32.lib"
    )

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
