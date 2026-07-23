param(
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).ProviderPath,
    [string]$OutDir = "",
    [string]$ExeName = "meccha-camouflage",
    [string]$Version = "",
    [ValidateSet("ReleaseSingleFile", "DevLooseSelfContained")]
    [string]$BuildMode = "ReleaseSingleFile",
    [switch]$ShowTimings
)

$ErrorActionPreference = "Stop"

# VsDevCmd.bat resolves vswhere by bare name. When the Visual Studio Installer
# directory (which contains vswhere.exe) is not on PATH, VsDevCmd writes a
# benign "vswhere.exe is not recognized" line to stderr. Under the Stop
# preference above, PowerShell can promote that stderr line to a terminating
# NativeCommandError and fail an otherwise successful build. Put the Installer
# directory on PATH so the notice is never emitted, making the build succeed
# from a plain shell as well as from a VS Developer PowerShell.
$vsInstallerDir = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer"
if ((Test-Path $vsInstallerDir) -and ($env:PATH -notlike "*$vsInstallerDir*")) {
    $env:PATH = "$vsInstallerDir;$env:PATH"
}

function Resolve-ProjectVersion {
    param(
        [string]$Requested,
        [string]$Root
    )
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        return $Requested
    }
    if (Get-Command git -ErrorAction SilentlyContinue) {
        try {
            $exact = & git -C $Root describe --tags --exact-match 2>$null
        } catch {
            $exact = $null
        }
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($exact)) {
            return $exact.Trim()
        }
        try {
            $described = & git -C $Root describe --tags --dirty --always 2>$null
        } catch {
            $described = $null
        }
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($described)) {
            return $described.Trim()
        }
    }
    return "unversioned"
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

function Push-NativeToolWorkingDirectory {
    $location = Get-Location
    $providerPath = $location.ProviderPath
    $isWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
    if ($isWindows -and $providerPath.StartsWith("\\")) {
        Push-Location ([System.IO.Path]::GetTempPath())
        return $true
    }
    return $false
}

function Invoke-VsToolCommand {
    param(
        [Parameter(Mandatory = $true)][string]$ToolName,
        [Parameter(Mandatory = $true)][string[]]$ToolArgs
    )
    $pushed = Push-NativeToolWorkingDirectory
    # See Invoke-VsToolCapture: keep benign native-tool stderr from being
    # promoted to a terminating error under the Stop preference. The exit code
    # remains the success signal.
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
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
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
        if ($pushed) { Pop-Location }
    }
}

function Invoke-VsToolCapture {
    param(
        [Parameter(Mandatory = $true)][string]$ToolName,
        [Parameter(Mandatory = $true)][string[]]$ToolArgs
    )
    $pushed = Push-NativeToolWorkingDirectory
    # Native tools (VsDevCmd, dumpbin) write benign notices to stderr. Under the
    # script-level $ErrorActionPreference='Stop', merging that stderr with 2>&1
    # promotes each line to a terminating NativeCommandError before the exit code
    # is ever checked, failing an otherwise successful build (for example
    # VsDevCmd's "vswhere.exe is not recognized" notice when the Installer
    # directory is not on PATH). Capture without that promotion and rely on the
    # tool's exit code, which is the real success signal.
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if (Get-Command $ToolName -ErrorAction SilentlyContinue) {
            $output = & $ToolName @ToolArgs 2>&1
            if ($LASTEXITCODE -ne 0) { throw "$ToolName failed with exit code $LASTEXITCODE`n$output" }
            return $output
        }
        $VsDevCmd = Get-VsDevCmd
        if (-not $VsDevCmd) {
            throw "$ToolName was not found. Install Visual Studio 2022 Build Tools or run from a VS Developer PowerShell."
        }
        $ArgText = ($ToolArgs | ForEach-Object { Quote-CmdArg $_ }) -join " "
        $CommandLine = "$(Quote-CmdArg $VsDevCmd) -arch=x64 -host_arch=x64 >nul && $ToolName $ArgText"
        $output = cmd /d /c $CommandLine 2>&1
        if ($LASTEXITCODE -ne 0) { throw "$ToolName failed with exit code $LASTEXITCODE`n$output" }
        return $output
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
        if ($pushed) { Pop-Location }
    }
}

function Get-ExeBaseName {
    param([string]$Name)
    $candidate = (New-Object System.IO.FileInfo($Name)).BaseName
    if ([string]::IsNullOrWhiteSpace($candidate)) { return "meccha-camouflage" }
    return $candidate
}

function Invoke-DotNet {
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    & dotnet @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "dotnet $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

function Clear-DirectoryContents {
    param([Parameter(Mandatory = $true)][string]$Path)
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    $full = (Resolve-Path -LiteralPath $Path).ProviderPath.TrimEnd("\", "/")
    $root = [System.IO.Path]::GetPathRoot($full).TrimEnd("\", "/")
    if ($full -eq $root) {
        throw "Refusing to clear filesystem root: $full"
    }
    Get-ChildItem -Force -LiteralPath $full | Remove-Item -Recurse -Force
}

function Test-WebView2EvergreenBootstrapper {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path $Path -PathType Leaf)) {
        return $false
    }
    $signature = Get-AuthenticodeSignature -FilePath $Path
    return (
        $signature.Status -eq "Valid" -and
        $null -ne $signature.SignerCertificate -and
        $signature.SignerCertificate.Subject -like "*Microsoft Corporation*"
    )
}

function Ensure-WebView2EvergreenBootstrapper {
    param(
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$CacheRoot
    )
    New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
    $BootstrapperPath = Join-Path $CacheRoot "MicrosoftEdgeWebview2Setup.exe"
    if (Test-WebView2EvergreenBootstrapper -Path $BootstrapperPath) {
        return $BootstrapperPath
    }

    if (Test-Path $BootstrapperPath) {
        Remove-Item -Force $BootstrapperPath
    }
    Write-Host "Downloading Microsoft Edge WebView2 Evergreen Bootstrapper..."
    Invoke-WebRequest -Uri $Url -OutFile $BootstrapperPath
    if (-not (Test-WebView2EvergreenBootstrapper -Path $BootstrapperPath)) {
        Remove-Item -Force $BootstrapperPath -ErrorAction SilentlyContinue
        throw "WebView2 Evergreen Bootstrapper was not signed by Microsoft."
    }
    return $BootstrapperPath
}

function Assert-NativeDependencyAllowList {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Allowed,
        [Parameter(Mandatory = $true)][string]$Label
    )
    $Output = Invoke-VsToolCapture -ToolName "dumpbin.exe" -ToolArgs @("/nologo", "/dependents", $Path)
    $Dlls = @($Output | ForEach-Object {
        $line = $_.ToString().Trim()
        if ($line -match '^[A-Za-z0-9_.-]+\.dll$') { $line }
    } | Sort-Object -Unique)
    $Unexpected = @($Dlls | Where-Object { $Allowed -notcontains $_ })
    if ($Unexpected.Count -gt 0) {
        throw "$Label has unexpected dependencies: $($Unexpected -join ', ')"
    }
}

$BuildTimings = New-Object System.Collections.Generic.List[object]

function Invoke-BuildStep {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][scriptblock]$ScriptBlock
    )
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        & $ScriptBlock
    }
    finally {
        $timer.Stop()
        if ($ShowTimings) {
            $BuildTimings.Add([pscustomobject]@{
                Step = $Name
                Seconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
            }) | Out-Null
        }
    }
}

$BuildTotalTimer = [System.Diagnostics.Stopwatch]::StartNew()
$RuntimeRoot = [System.IO.Path]::GetFullPath(
    (Resolve-Path -LiteralPath $RuntimeRoot).ProviderPath
)
$Version = Resolve-ProjectVersion -Requested $Version -Root $RuntimeRoot
$ExeName = Get-ExeBaseName -Name $ExeName
Write-Host "Build version: $Version"
Write-Host "Build mode: $BuildMode"

if (-not $OutDir) {
    $OutDir = if ($BuildMode -eq "DevLooseSelfContained") {
        Join-Path $RuntimeRoot ".build\bin-dev"
    }
    else {
        Join-Path $RuntimeRoot ".build\bin"
    }
}
elseif (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $RuntimeRoot $OutDir
}
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
$ObjDir = Join-Path $RuntimeRoot ".build\obj"
$DotNetArtifactRoot = Join-Path $RuntimeRoot ".build\dotnet-windows"
$NativePackageDir = Join-Path $ObjDir "package-native"
$WebView2EvergreenBootstrapperUrl = "https://go.microsoft.com/fwlink/p/?LinkId=2124703"
$WebView2BootstrapperCacheRoot = Join-Path $RuntimeRoot ".build\cache\webview2\evergreen"

$BridgeSource = Join-Path $RuntimeRoot "src\native\bridge\bridge.cpp"
$InjectorSource = Join-Path $RuntimeRoot "src\native\injector\injector.cpp"
$TransformValidationTestSource = Join-Path $RuntimeRoot "src\native\tests\transform_validation_test.cpp"
$WebHostProject = Join-Path $RuntimeRoot "src\csharp\MecchaCamouflage.WebHost\MecchaCamouflage.WebHost.csproj"
$TestsProject = Join-Path $RuntimeRoot "src\csharp\MecchaCamouflage.Tests\MecchaCamouflage.Tests.csproj"
$MeshProfilesSourceDir = Join-Path $RuntimeRoot "resources\mesh-profiles"

foreach ($path in @($BridgeSource, $InjectorSource, $TransformValidationTestSource, $WebHostProject, $TestsProject)) {
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "Required source not found: $path"
    }
}
if (-not (Test-Path $MeshProfilesSourceDir -PathType Container)) {
    throw "Mesh profile asset directory not found: $MeshProfilesSourceDir"
}

Invoke-BuildStep -Name "prepare output directories" -ScriptBlock {
    Clear-DirectoryContents -Path $OutDir
    New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
    Clear-DirectoryContents -Path $NativePackageDir
}
$WebView2BootstrapperPath = Invoke-BuildStep -Name "prepare WebView2 Evergreen bootstrapper" -ScriptBlock {
    Ensure-WebView2EvergreenBootstrapper -Url $WebView2EvergreenBootstrapperUrl -CacheRoot $WebView2BootstrapperCacheRoot
}

Push-Location $RuntimeRoot
try {
    Invoke-BuildStep -Name "run C# tests" -ScriptBlock {
        Invoke-DotNet -Arguments @(
            "build", $TestsProject, "-c", "Release", "--no-incremental",
            "/p:MecchaDotNetArtifactRoot=$DotNetArtifactRoot"
        )
        Invoke-DotNet -Arguments @(
            "run", "--project", $TestsProject, "-c", "Release", "--no-build",
            "/p:MecchaDotNetArtifactRoot=$DotNetArtifactRoot"
        )
    }

    $TransformValidationTestOutput = Join-Path $ObjDir "transform-validation-test.exe"
    Invoke-BuildStep -Name "run native transform validation test" -ScriptBlock {
        Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
            "/nologo", "/std:c++17", "/EHsc", "/O2", $TransformValidationTestSource,
            "/Fo:$(Join-Path $ObjDir 'transform-validation-test.obj')",
            "/Fe:$TransformValidationTestOutput"
        )
        & $TransformValidationTestOutput
        if ($LASTEXITCODE -ne 0) {
            throw "Native transform validation test failed with exit code $LASTEXITCODE"
        }
    }

    $BridgeOutput = Join-Path $NativePackageDir "runtime-bridge.dll"
    $InjectorOutput = Join-Path $NativePackageDir "runtime-injector.exe"
    Invoke-BuildStep -Name "compile native bridge" -ScriptBlock {
        Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
            "/nologo", "/std:c++17", "/EHsc", "/O2", "/LD", $BridgeSource,
            "/Fo:$(Join-Path $ObjDir 'bridge.obj')",
            "/Fe:$BridgeOutput",
            "Ws2_32.lib",
            "User32.lib",
            "/link",
            "/Brepro"
        )
    }
    Invoke-BuildStep -Name "compile native injector" -ScriptBlock {
        Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
            "/nologo", "/EHsc", "/O2", $InjectorSource,
            "/Fo:$(Join-Path $ObjDir 'injector.obj')",
            "/Fe:$InjectorOutput",
            "/link",
            "/Brepro"
        )
    }

    if (-not (Test-Path $BridgeOutput -PathType Leaf)) {
        throw "Bridge DLL was not produced: $BridgeOutput"
    }
    if (-not (Test-Path $InjectorOutput -PathType Leaf)) {
        throw "Injector EXE was not produced: $InjectorOutput"
    }
    Invoke-BuildStep -Name "check native dependencies" -ScriptBlock {
        Assert-NativeDependencyAllowList -Path $BridgeOutput -Allowed @("KERNEL32.dll", "USER32.dll", "WS2_32.dll") -Label "runtime-bridge.dll"
    }

    $MeshProfiles = @(Get-ChildItem -Path $MeshProfilesSourceDir -Filter "*.json" -File)
    if ($MeshProfiles.Count -le 0) {
        throw "No mesh profile JSON assets found in: $MeshProfilesSourceDir"
    }

    Invoke-BuildStep -Name $(if ($BuildMode -eq "DevLooseSelfContained") { "publish WebHost loose dev" } else { "publish WebHost single-file" }) -ScriptBlock {
        $publishArgs = @(
            "publish", $WebHostProject,
            "-c", "Release",
            "-r", "win-x64",
            "--self-contained", "true",
            "-o", $OutDir,
            "/p:MecchaAppVersion=$Version",
            "/p:MecchaDotNetArtifactRoot=$DotNetArtifactRoot",
            "/p:MecchaNativeRuntimeDir=$NativePackageDir",
            "/p:MecchaMeshProfilesDir=$MeshProfilesSourceDir",
            "/p:MecchaWebView2BootstrapperPath=$WebView2BootstrapperPath"
        )
        if ($BuildMode -eq "DevLooseSelfContained") {
            $publishArgs += "/p:PublishSingleFile=false"
        }
        else {
            $publishArgs += @(
                "/p:PublishSingleFile=true",
                "/p:IncludeAllContentForSelfExtract=true",
                "/p:IncludeNativeLibrariesForSelfExtract=true",
                "/p:EnableCompressionInSingleFile=true",
                "/p:DebugSymbols=false",
                "/p:DebugType=None",
                "/p:CopyOutputSymbolsToPublishDirectory=false"
            )
        }
        Invoke-DotNet -Arguments $publishArgs
    }

    Invoke-BuildStep -Name "verify build output" -ScriptBlock {
        $DefaultControllerOutput = Join-Path $OutDir "meccha-camouflage.exe"
        $ControllerOutput = Join-Path $OutDir "$ExeName.exe"
        if ($DefaultControllerOutput -ne $ControllerOutput -and (Test-Path $DefaultControllerOutput -PathType Leaf)) {
            Move-Item -Force $DefaultControllerOutput $ControllerOutput
        }

        if (-not (Test-Path $ControllerOutput -PathType Leaf)) {
            throw "WebView2 controller EXE was not produced: $ControllerOutput"
        }
        if ($BuildMode -eq "ReleaseSingleFile") {
            $debugArtifacts = @(
                Get-ChildItem -Path $OutDir -File -Recurse |
                    Where-Object { $_.Extension -in @(".pdb", ".dbg", ".ilk") }
            )
            if ($debugArtifacts.Count -gt 0) {
                throw "ReleaseSingleFile output contains debug artifacts: $($debugArtifacts.FullName -join ', ')"
            }
        }
    }
}
finally {
    Pop-Location
}

$BuildTotalTimer.Stop()
if ($ShowTimings) {
    Write-Host ""
    Write-Host "Build timings:"
    foreach ($entry in $BuildTimings) {
        Write-Host ("  {0,-34} {1,8:N3}s" -f $entry.Step, $entry.Seconds)
    }
    Write-Host ("  {0,-34} {1,8:N3}s" -f "total", [math]::Round($BuildTotalTimer.Elapsed.TotalSeconds, 3))
}

Write-Host "Built runtime artifacts:"
Write-Host "  $(Join-Path $OutDir "$ExeName.exe")"
Write-Host "  native runtime embedded from $NativePackageDir"
Write-Host "  Microsoft Edge WebView2 Evergreen Bootstrapper embedded from $WebView2BootstrapperPath"
