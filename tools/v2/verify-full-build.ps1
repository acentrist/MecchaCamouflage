param(
    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,

    [Parameter(Mandatory = $true)]
    [string]$ReportPath,

    [Parameter(Mandatory = $true)]
    [string]$Ue4ssSourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$Ue4ssSourceManifest,

    [Parameter(Mandatory = $true)]
    [string]$ProjectCommit
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$AcceptedUe4ssCommit = "6c26f038751b3d96059d4a9148f5d093012d55ad"
if ($ProjectCommit -cnotmatch "^[0-9a-f]{40}$") {
    throw "ProjectCommit must be a lowercase 40-character Git commit."
}
$ResolvedBuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
$ResolvedUe4ssSourceRoot = (
    Resolve-Path -LiteralPath $Ue4ssSourceRoot
).Path
$ResolvedUe4ssSourceManifest = (
    Resolve-Path -LiteralPath $Ue4ssSourceManifest
).Path
$ProjectRoot = (
    Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")
).Path

$StageOutput = & python `
    (Join-Path $ProjectRoot "tools\v2\prepare_ue4ss_source_stage.py") `
    --verify-only `
    --policy (Join-Path $ProjectRoot "cmake\ue4ss-source-overlay.json") `
    --output-root $ResolvedUe4ssSourceRoot `
    --manifest $ResolvedUe4ssSourceManifest 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "UE4SS source-stage verification failed.`n$($StageOutput -join [Environment]::NewLine)"
}
$StageManifest = Get-Content `
    -LiteralPath $ResolvedUe4ssSourceManifest `
    -Raw |
    ConvertFrom-Json
if ($StageManifest.ue4ss_commit -ne $AcceptedUe4ssCommit -or
    $StageManifest.owner -ne "MecchaCamouflage") {
    throw "UE4SS source-stage manifest identity is invalid."
}
$CMakeCache = Join-Path $ResolvedBuildRoot "CMakeCache.txt"
if (-not (Test-Path -LiteralPath $CMakeCache -PathType Leaf)) {
    throw "The full-build CMake cache is missing."
}
$CacheText = Get-Content -LiteralPath $CMakeCache -Raw
$NormalizedSourceRoot = $ResolvedUe4ssSourceRoot.Replace("\", "/")
$NormalizedSourceManifest = $ResolvedUe4ssSourceManifest.Replace("\", "/")
$NormalizedProjectRoot = $ProjectRoot.Replace("\", "/")
$ExpectedSourceCache = "MECCHA_UE4SS_SOURCE_ROOT:PATH=$NormalizedSourceRoot"
$ExpectedManifestCache = "MECCHA_UE4SS_SOURCE_MANIFEST:FILEPATH=$NormalizedSourceManifest"
$ExpectedProjectCache = "CMAKE_HOME_DIRECTORY:INTERNAL=$NormalizedProjectRoot"
if (-not $CacheText.Contains($ExpectedSourceCache) -or
    -not $CacheText.Contains($ExpectedManifestCache) -or
    -not $CacheText.Contains($ExpectedProjectCache)) {
    throw "The full-build CMake graph is not bound to the verified project and UE4SS source stage."
}

function Get-CMakeSetValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $Pattern = (
        '(?m)^set\(' +
        [Regex]::Escape($Name) +
        ' "([^"]+)"\)\r?$'
    )
    $Found = [Regex]::Matches($Text, $Pattern)
    if ($Found.Count -ne 1) {
        throw "CMake compiler metadata is missing or ambiguous: $Name."
    }
    return $Found[0].Groups[1].Value
}

$CompilerMetadata = @(
    Get-ChildItem `
        -LiteralPath (Join-Path $ResolvedBuildRoot "CMakeFiles") `
        -Directory |
        ForEach-Object {
            Join-Path $_.FullName "CMakeCXXCompiler.cmake"
        } |
        Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        }
)
if ($CompilerMetadata.Count -ne 1) {
    throw "The full-build CMake compiler metadata is missing or ambiguous."
}
$CompilerMetadataText = Get-Content `
    -LiteralPath $CompilerMetadata[0] `
    -Raw
$ConfiguredCompilerId = Get-CMakeSetValue `
    -Text $CompilerMetadataText `
    -Name "CMAKE_CXX_COMPILER_ID"
$ConfiguredCompilerArchitecture = Get-CMakeSetValue `
    -Text $CompilerMetadataText `
    -Name "CMAKE_CXX_COMPILER_ARCHITECTURE_ID"
$ConfiguredCompilerVersion = Get-CMakeSetValue `
    -Text $CompilerMetadataText `
    -Name "CMAKE_CXX_COMPILER_VERSION"
$ReportedCompilerPath = (
    Resolve-Path -LiteralPath (
        Get-CMakeSetValue `
            -Text $CompilerMetadataText `
            -Name "CMAKE_CXX_COMPILER"
    )
).Path
$ConfiguredCompilerPath = $ReportedCompilerPath.ToLowerInvariant()
$CompilerPath = (
    Resolve-Path -LiteralPath (
        Get-Command cl -ErrorAction Stop
    ).Source
).Path.ToLowerInvariant()
$CompilerVersion = (
    Get-Item -LiteralPath $ReportedCompilerPath
).VersionInfo.FileVersion
if ($ConfiguredCompilerId -cne "MSVC" -or
    $ConfiguredCompilerArchitecture -cne "x64" -or
    $ConfiguredCompilerPath -cne $CompilerPath -or
    $ConfiguredCompilerVersion -cne $CompilerVersion) {
    throw (
        "The verification compiler does not match the configured " +
        "x64 MSVC compiler."
    )
}

$ProjectHead = (
    & git -C $ProjectRoot rev-parse HEAD
).Trim()
if ($LASTEXITCODE -ne 0 -or $ProjectHead -cne $ProjectCommit) {
    throw "The full-build project checkout does not match ProjectCommit."
}
$ProjectStatus = @(
    & git -C $ProjectRoot `
        -c core.filemode=false `
        status `
        --porcelain `
        --untracked-files=no `
        --ignore-submodules=none
)
if ($LASTEXITCODE -ne 0 -or $ProjectStatus.Count -ne 0) {
    throw "The full-build project checkout contains a tracked modification."
}

function Get-SingleBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    $Matches = @(
        Get-ChildItem -LiteralPath $ResolvedBuildRoot -Recurse -File -Filter $Name |
            Where-Object { $_.FullName -notmatch "[\\/]_deps[\\/]" }
    )
    if ($Matches.Count -ne 1) {
        throw "Expected exactly one $Name under $ResolvedBuildRoot; found $($Matches.Count)."
    }
    return $Matches[0]
}

function Invoke-Dumpbin {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Mode,

        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $Output = & dumpbin $Mode $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin $Mode failed for $Path.`n$($Output -join [Environment]::NewLine)"
    }
    return @($Output)
}

function Assert-X64Pe {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo]$Binary
    )

    $Headers = Invoke-Dumpbin -Mode "/headers" -Path $Binary.FullName
    if (($Headers -join "`n") -notmatch "(?im)^\s*8664 machine \(x64\)\s*$") {
        throw "$($Binary.Name) is not an x64 PE binary."
    }
}

function Get-Dependents {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.FileInfo]$Binary
    )

    $Output = Invoke-Dumpbin -Mode "/dependents" -Path $Binary.FullName
    return @(
        $Output |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -match "^[A-Za-z0-9._-]+\.dll$" } |
            ForEach-Object { $_.ToLowerInvariant() } |
            Sort-Object -Unique
    )
}

$Main = Get-SingleBinary -Name "main.dll"
$Ue4ss = Get-SingleBinary -Name "UE4SS.dll"
$Proxy = Get-SingleBinary -Name "dwmapi.dll"
Assert-X64Pe -Binary $Main
Assert-X64Pe -Binary $Ue4ss
Assert-X64Pe -Binary $Proxy

$ExportOutput = Invoke-Dumpbin -Mode "/exports" -Path $Main.FullName
$Exports = @(
    $ExportOutput |
        ForEach-Object {
            if ($_ -match "^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\S+)(?:\s+=\s+\S+)?\s*$") {
                $Matches[1]
            }
        } |
        Where-Object { $_ } |
        Sort-Object -Unique
)
$ExpectedExports = @("start_mod", "uninstall_mod")
if (Compare-Object -ReferenceObject $ExpectedExports -DifferenceObject $Exports) {
    throw "main.dll exports '$($Exports -join ", ")'; expected only '$($ExpectedExports -join ", ")'."
}

$MainDependents = Get-Dependents -Binary $Main
$Ue4ssDependents = Get-Dependents -Binary $Ue4ss
$ProxyDependents = Get-Dependents -Binary $Proxy
if ($MainDependents -notcontains "ue4ss.dll") {
    throw "main.dll does not import UE4SS.dll from the same source graph."
}
if ($MainDependents -notcontains "vcruntime140.dll") {
    throw "main.dll is not linked to the expected dynamic MSVC runtime."
}
if ($Ue4ssDependents -notcontains "vcruntime140.dll") {
    throw "UE4SS.dll is not linked to the expected dynamic MSVC runtime."
}
if ($ProxyDependents -notcontains "vcruntime140.dll") {
    throw "dwmapi.dll is not linked to the expected dynamic MSVC runtime."
}

$Ue4ssHead = $StageManifest.ue4ss_commit

$Compiler = "$ReportedCompilerPath $CompilerVersion"
$Report = [ordered]@{
    schema_version = 1
    product_version = "2.0.0"
    source_commit = $ProjectHead
    ue4ss_commit = $Ue4ssHead
    ue4ss_source_stage = [ordered]@{
        manifest_sha256 = (
            Get-FileHash `
                -LiteralPath $ResolvedUe4ssSourceManifest `
                -Algorithm SHA256
        ).Hash.ToLowerInvariant()
        policy_sha256 = $StageManifest.policy_sha256
        overlay = $StageManifest.overlay
        nested_gitlinks = $StageManifest.nested_gitlinks
    }
    configuration = "Game__Shipping__Win64"
    architecture = "x64"
    msvc_runtime = "MultiThreadedDLL"
    compiler = $Compiler
    cmake_version = (cmake --version | Select-Object -First 1).Trim()
    main = [ordered]@{
        path = $Main.FullName.Substring($ResolvedBuildRoot.Length).TrimStart("\", "/")
        size = $Main.Length
        sha256 = (Get-FileHash -LiteralPath $Main.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        exports = $Exports
        dependents = $MainDependents
    }
    ue4ss = [ordered]@{
        path = $Ue4ss.FullName.Substring($ResolvedBuildRoot.Length).TrimStart("\", "/")
        size = $Ue4ss.Length
        sha256 = (Get-FileHash -LiteralPath $Ue4ss.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        dependents = $Ue4ssDependents
    }
    proxy = [ordered]@{
        path = $Proxy.FullName.Substring($ResolvedBuildRoot.Length).TrimStart("\", "/")
        size = $Proxy.Length
        sha256 = (Get-FileHash -LiteralPath $Proxy.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        dependents = $ProxyDependents
    }
}

$ReportDirectory = Split-Path -Parent $ReportPath
if ($ReportDirectory) {
    New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
}
$EncodedReport = $Report | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText(
    $ReportPath,
    $EncodedReport,
    [System.Text.UTF8Encoding]::new($false)
)
Write-Host "PASS full-build: main.dll and UE4SS.dll share the pinned x64 Shipping graph."
