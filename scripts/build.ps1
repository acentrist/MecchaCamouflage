param(
    [string]$UE4SSRoot = $env:UE4SS_ROOT,
    [string]$BuildDir = "",
    [string]$BuildType = "Game__Shipping__Win64",
    [string]$CMake = "cmake",
    [string]$Ninja = "",
    [string]$RustCompiler = "",
    [string]$RustCargo = "",
    [string]$Generator = "Ninja"
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot "build"
}
if (-not $UE4SSRoot) {
    $UE4SSRoot = Join-Path $RepoRoot "external\RE-UE4SS"
}
if (-not (Test-Path (Join-Path $UE4SSRoot "CMakeLists.txt"))) {
    throw "RE-UE4SS was not found. Clone it to external\RE-UE4SS or set UE4SS_ROOT."
}

$ConfigureArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DUE4SS_ROOT=$UE4SSRoot",
    "-DUE4SS_VERSION_CHECK=OFF"
)
if ($Ninja) {
    $ConfigureArgs += "-DCMAKE_MAKE_PROGRAM=$Ninja"
}

function Resolve-ToolPath([string]$ExplicitPath, [string]$CommandName, [string[]]$Candidates) {
    if ($ExplicitPath) {
        return $ExplicitPath
    }
    $Command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }
    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path $Candidate)) {
            return $Candidate
        }
    }
    return ""
}

$RustupBin = Join-Path $env:USERPROFILE ".rustup\toolchains\stable-x86_64-pc-windows-msvc\bin"
$CargoBin = Join-Path $env:USERPROFILE ".cargo\bin"
$RustCompiler = Resolve-ToolPath $RustCompiler "rustc.exe" @(
    (Join-Path $RustupBin "rustc.exe"),
    (Join-Path $CargoBin "rustc.exe")
)
$RustCargo = Resolve-ToolPath $RustCargo "cargo.exe" @(
    (Join-Path $RustupBin "cargo.exe"),
    (Join-Path $CargoBin "cargo.exe")
)
if ($RustCompiler) {
    $ConfigureArgs += "-DRust_COMPILER=$RustCompiler"
}
if ($RustCargo) {
    $ConfigureArgs += "-DRust_CARGO=$RustCargo"
}

function Quote-CmdArg([string]$Value) {
    if ($Value -match '^[A-Za-z0-9_./:=+\-\\]+$') {
        return $Value
    }
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Get-VsDevCmd {
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $VsWhere)) {
        return ""
    }
    $VsInstall = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VsInstall) {
        return ""
    }
    $VsDevCmd = Join-Path $VsInstall "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $VsDevCmd) {
        return $VsDevCmd
    }
    return ""
}

function Invoke-BuildCommand([string]$Executable, [string[]]$Arguments) {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Executable failed with exit code $LASTEXITCODE"
        }
        return
    }

    $VsDevCmd = Get-VsDevCmd
    if (-not $VsDevCmd) {
        throw "MSVC was not found. Install Visual Studio 2022 Build Tools or run this script from a VS Developer PowerShell."
    }

    $ArgText = ($Arguments | ForEach-Object { Quote-CmdArg $_ }) -join " "
    $CommandLine = "$(Quote-CmdArg $VsDevCmd) -arch=x64 -host_arch=x64 >nul && $(Quote-CmdArg $Executable) $ArgText"
    cmd /d /c $CommandLine
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE"
    }
}

Invoke-BuildCommand $CMake $ConfigureArgs
Invoke-BuildCommand $CMake @("--build", $BuildDir, "--target", "MecchaCamouflage")
