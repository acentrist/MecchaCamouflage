param(
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$OutDir = "",
    [string]$ExeName = "meccha-camouflage",
    [string]$Version = "",
    [ValidateSet("ReleaseSingleFile", "DevLooseSelfContained")]
    [string]$BuildMode = "ReleaseSingleFile",
    [switch]$ShowTimings
)

$ErrorActionPreference = "Stop"

function Resolve-ProjectVersion {
    param(
        [string]$Requested,
        [string]$Root
    )
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        return $Requested
    }
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $exact = & git -C $Root describe --tags --exact-match 2>$null
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($exact)) {
            return $exact.Trim()
        }
        $described = & git -C $Root describe --tags --dirty --always 2>$null
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

function Invoke-VsToolCapture {
    param(
        [Parameter(Mandatory = $true)][string]$ToolName,
        [Parameter(Mandatory = $true)][string[]]$ToolArgs
    )
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
    $full = (Resolve-Path $Path).Path.TrimEnd("\", "/")
    $root = [System.IO.Path]::GetPathRoot($full).TrimEnd("\", "/")
    if ($full -eq $root) {
        throw "Refusing to clear filesystem root: $full"
    }
    Get-ChildItem -Force -LiteralPath $full | Remove-Item -Recurse -Force
}

function Ensure-WebView2FixedRuntime {
    param(
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$Url,
        [Parameter(Mandatory = $true)][string]$CacheRoot
    )
    $RuntimeDir = Join-Path $CacheRoot "runtime"
    $RuntimeExe = Join-Path $RuntimeDir "msedgewebview2.exe"
    if (Test-Path $RuntimeExe -PathType Leaf) {
        return $RuntimeDir
    }

    New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
    $CabPath = Join-Path $CacheRoot "Microsoft.WebView2.FixedVersionRuntime.$Version.x64.cab"
    if (-not (Test-Path $CabPath -PathType Leaf)) {
        Write-Host "Downloading WebView2 Fixed Runtime $Version..."
        Invoke-WebRequest -Uri $Url -OutFile $CabPath
    }

    $ExtractDir = Join-Path $CacheRoot ("extract." + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $ExtractDir | Out-Null
    try {
        & expand.exe $CabPath -F:* $ExtractDir | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "expand.exe failed with exit code $LASTEXITCODE"
        }
        $Exe = Get-ChildItem -Path $ExtractDir -Recurse -Filter "msedgewebview2.exe" -File | Select-Object -First 1
        if (-not $Exe) {
            throw "WebView2 Fixed Runtime cab did not contain msedgewebview2.exe"
        }
        if (Test-Path $RuntimeDir) {
            Remove-Item -Recurse -Force $RuntimeDir
        }
        New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null
        Copy-Item -Recurse -Force -Path (Join-Path $Exe.Directory.FullName "*") -Destination $RuntimeDir
    }
    finally {
        if (Test-Path $ExtractDir) {
            Remove-Item -Recurse -Force $ExtractDir
        }
    }
    if (-not (Test-Path $RuntimeExe -PathType Leaf)) {
        throw "WebView2 Fixed Runtime was not prepared: $RuntimeExe"
    }
    return $RuntimeDir
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
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
$ObjDir = Join-Path $RuntimeRoot ".build\obj"
$NativePackageDir = Join-Path $ObjDir "package-native"
$WebView2FixedVersion = "150.0.4078.48"
$WebView2FixedRuntimeUrl = "https://msedge.sf.dl.delivery.mp.microsoft.com/filestreamingservice/files/60926d99-f201-46bb-91a0-d868dc06b275/Microsoft.WebView2.FixedVersionRuntime.150.0.4078.48.x64.cab"
$WebView2CacheRoot = Join-Path $RuntimeRoot ".build\cache\webview2\$WebView2FixedVersion\win-x64"

$BridgeSource = Join-Path $RuntimeRoot "src\native\bridge\bridge.cpp"
$NativeQualityTestSource = Join-Path $RuntimeRoot "src\native\tests\quality_algorithms_test.cpp"
$LoaderSource = Join-Path $RuntimeRoot "src\native\loader\loader.cpp"
$InjectorSource = Join-Path $RuntimeRoot "src\native\injector\injector.cpp"
$WebHostProject = Join-Path $RuntimeRoot "src\csharp\MecchaCamouflage.WebHost\MecchaCamouflage.WebHost.csproj"
$TestsProject = Join-Path $RuntimeRoot "src\csharp\MecchaCamouflage.Tests\MecchaCamouflage.Tests.csproj"
$MeshProfilesSourceDir = Join-Path $RuntimeRoot "resources\mesh-profiles"

foreach ($path in @($BridgeSource, $NativeQualityTestSource, $LoaderSource, $InjectorSource, $WebHostProject, $TestsProject)) {
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
$WebView2RuntimeDir = Invoke-BuildStep -Name "prepare WebView2 fixed runtime" -ScriptBlock {
    Ensure-WebView2FixedRuntime -Version $WebView2FixedVersion -Url $WebView2FixedRuntimeUrl -CacheRoot $WebView2CacheRoot
}

Push-Location $RuntimeRoot
try {
    Invoke-BuildStep -Name "run C# tests" -ScriptBlock {
        Invoke-DotNet -Arguments @("run", "--project", $TestsProject, "-c", "Release")
    }

    Invoke-BuildStep -Name "run native quality algorithm tests" -ScriptBlock {
        $NativeQualityTestOutput = Join-Path $ObjDir "quality-algorithms-test.exe"
        Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
            "/nologo", "/std:c++17", "/EHsc", "/O2", $NativeQualityTestSource,
            "/Fo:$(Join-Path $ObjDir 'quality-algorithms-test.obj')",
            "/Fe:$NativeQualityTestOutput",
            "/link",
            "/Brepro"
        )
        & $NativeQualityTestOutput
        if ($LASTEXITCODE -ne 0) {
            throw "native quality algorithm tests failed with exit code $LASTEXITCODE"
        }
    }

    $BridgeOutput = Join-Path $NativePackageDir "runtime-bridge.dll"
    $LoaderOutput = Join-Path $NativePackageDir "bridge-loader.dll"
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
    Invoke-BuildStep -Name "compile native loader" -ScriptBlock {
        Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
            "/nologo", "/std:c++17", "/EHsc", "/O2", "/LD", $LoaderSource,
            "/Fo:$(Join-Path $ObjDir 'loader.obj')",
            "/Fe:$LoaderOutput",
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
    if (-not (Test-Path $LoaderOutput -PathType Leaf)) {
        throw "Bridge loader DLL was not produced: $LoaderOutput"
    }
    if (-not (Test-Path $InjectorOutput -PathType Leaf)) {
        throw "Injector EXE was not produced: $InjectorOutput"
    }
    Invoke-BuildStep -Name "check native dependencies" -ScriptBlock {
        Assert-NativeDependencyAllowList -Path $BridgeOutput -Allowed @("KERNEL32.dll", "USER32.dll", "WS2_32.dll") -Label "runtime-bridge.dll"
        Assert-NativeDependencyAllowList -Path $LoaderOutput -Allowed @("KERNEL32.dll") -Label "bridge-loader.dll"
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
            "/p:MecchaNativeRuntimeDir=$NativePackageDir",
            "/p:MecchaMeshProfilesDir=$MeshProfilesSourceDir",
            "/p:MecchaWebView2RuntimeDir=$WebView2RuntimeDir",
            "/p:MecchaWebView2Version=$WebView2FixedVersion"
        )
        if ($BuildMode -eq "DevLooseSelfContained") {
            $publishArgs += "/p:PublishSingleFile=false"
        }
        else {
            $publishArgs += @(
                "/p:PublishSingleFile=true",
                "/p:IncludeAllContentForSelfExtract=true",
                "/p:IncludeNativeLibrariesForSelfExtract=true",
                "/p:EnableCompressionInSingleFile=true"
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
Write-Host "  WebView2 Fixed Runtime $WebView2FixedVersion embedded from $WebView2RuntimeDir"
