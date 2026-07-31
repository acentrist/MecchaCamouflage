param(
    [Parameter(Mandatory = $false)]
    [string]$ProjectRoot = ".",

    [Parameter(Mandatory = $true)]
    [string]$BuildRoot,

    [Parameter(Mandatory = $true)]
    [string]$Ue4ssSourceRoot,

    [Parameter(Mandatory = $true)]
    [string]$Ue4ssSourceManifest,

    [Parameter(Mandatory = $true)]
    [string]$ApprovedLicenseAudit,

    [Parameter(Mandatory = $true)]
    [string]$ReleaseRoot,

    [Parameter(Mandatory = $false)]
    [string]$CargoRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProductVersion = "2.0.0"
$Ue4ssCommit = "6c26f038751b3d96059d4a9148f5d093012d55ad"
$ExecutableName = "meccha-camouflage-v$ProductVersion.exe"

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Resolve-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Resolve-ProvenanceBinary {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Record,

        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if (-not ($Record.path -is [string]) -or
        [string]::IsNullOrWhiteSpace($Record.path)) {
        throw "$Label provenance path is invalid."
    }
    $Candidate = Join-Path $ResolvedBuildRoot $Record.path
    return Resolve-RequiredFile -Path $Candidate -Label $Label
}

$ResolvedProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
$ResolvedBuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
if (-not (Test-Path -LiteralPath $Ue4ssSourceRoot -PathType Container)) {
    throw "UE4SS source stage is missing: $Ue4ssSourceRoot"
}
$ResolvedUe4ssSourceRoot = (
    Resolve-Path -LiteralPath $Ue4ssSourceRoot
).Path
$ResolvedUe4ssSourceManifest = Resolve-RequiredFile `
    -Path $Ue4ssSourceManifest `
    -Label "UE4SS source-stage manifest"
$ResolvedAudit = Resolve-RequiredFile `
    -Path $ApprovedLicenseAudit `
    -Label "Approved dependency license audit"

if ([string]::IsNullOrWhiteSpace($CargoRoot)) {
    $CargoRoot = if ($env:CARGO_HOME) {
        $env:CARGO_HOME
    } else {
        Join-Path $env:USERPROFILE ".cargo"
    }
}
$ResolvedCargoRoot = (Resolve-Path -LiteralPath $CargoRoot).Path

$ReleasePath = [System.IO.Path]::GetFullPath($ReleaseRoot)
if (Test-Path -LiteralPath $ReleasePath) {
    throw "Release candidate root already exists: $ReleasePath"
}
$ReleaseParent = Split-Path -Parent $ReleasePath
if ($ReleaseParent) {
    New-Item -ItemType Directory -Path $ReleaseParent -Force | Out-Null
}
New-Item -ItemType Directory -Path $ReleasePath | Out-Null

$Intermediate = Join-Path $ReleasePath "intermediate"
$Evidence = Join-Path $ReleasePath "evidence"
$Artifact = Join-Path $ReleasePath "artifact"
New-Item -ItemType Directory -Path $Intermediate | Out-Null
New-Item -ItemType Directory -Path $Evidence | Out-Null
New-Item -ItemType Directory -Path $Artifact | Out-Null

$Provenance = Join-Path $Evidence "phase2-provenance.json"
$CargoMetadata = Join-Path $Intermediate "patternsleuth-cargo-metadata.json"
$DependencyEvidence = Join-Path $Evidence "dependency-evidence.json"
$DependencyNotices = Join-Path $Evidence "THIRD-PARTY-NOTICES.txt"
$DependencyReport = Join-Path $Evidence "dependency-license-report.json"
$ApprovedAuditCopy = Join-Path $Evidence "approved-dependency-license-audit.json"
$RuntimeRoot = Join-Path $Intermediate "runtime"
$PayloadLayout = Join-Path $Evidence "payload-layout.json"
$PayloadManifest = Join-Path $Evidence "payload-manifest.json"
$PayloadCab = Join-Path $Intermediate "payload.cab"
$ReleaseReport = Join-Path $Evidence "release-report.json"
$ReleaseChecksum = Join-Path $Evidence "$ExecutableName.sha256"
$SourceStageEvidence = Join-Path $Evidence "ue4ss-source-stage.json"

Push-Location $ResolvedProjectRoot
try {
    Copy-Item -LiteralPath $ResolvedAudit -Destination $ApprovedAuditCopy
    Copy-Item `
        -LiteralPath $ResolvedUe4ssSourceManifest `
        -Destination $SourceStageEvidence

    Invoke-Checked -FilePath "python" -Arguments @(
        "tools\v2\prepare_ue4ss_source_stage.py",
        "--verify-only",
        "--policy", "cmake\ue4ss-source-overlay.json",
        "--output-root", $ResolvedUe4ssSourceRoot,
        "--manifest", $ResolvedUe4ssSourceManifest
    )

    Invoke-Checked -FilePath "powershell" -Arguments @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $ResolvedProjectRoot "tools\v2\verify-full-build.ps1"),
        "-BuildRoot", $ResolvedBuildRoot,
        "-ReportPath", $Provenance,
        "-Ue4ssSourceRoot", $ResolvedUe4ssSourceRoot,
        "-Ue4ssSourceManifest", $ResolvedUe4ssSourceManifest
    )

    $CargoJson = & cargo metadata `
        --locked `
        --offline `
        --filter-platform x86_64-pc-windows-msvc `
        --format-version 1 `
        --manifest-path (Join-Path $ResolvedUe4ssSourceRoot "deps\first\patternsleuth_bind\Cargo.toml")
    if ($LASTEXITCODE -ne 0) {
        throw "cargo metadata failed with exit code $LASTEXITCODE."
    }
    [System.IO.File]::WriteAllText(
        $CargoMetadata,
        ($CargoJson -join [Environment]::NewLine),
        [System.Text.UTF8Encoding]::new($false)
    )

    Invoke-Checked -FilePath "python" -Arguments @(
        "tools\v2\collect_dependency_evidence.py",
        "--project-root", $ResolvedProjectRoot,
        "--build-root", $ResolvedBuildRoot,
        "--cargo-root", $ResolvedCargoRoot,
        "--reply-directory",
        (Join-Path $ResolvedBuildRoot ".cmake\api\v1\reply"),
        "--configuration", "Game__Shipping__Win64",
        "--root-target", "meccha_mod",
        "--root-target", "proxy",
        "--root-target", "UE4SS",
        "--cargo-metadata", $CargoMetadata,
        "--cargo-lock",
        (Join-Path $ResolvedUe4ssSourceRoot "deps\first\patternsleuth_bind\Cargo.lock"),
        "--ue4ss-source-root", $ResolvedUe4ssSourceRoot,
        "--ue4ss-source-manifest", $ResolvedUe4ssSourceManifest,
        "--cargo-root-package", "patternsleuth_bind",
        "--ue4ss-commit", $Ue4ssCommit,
        "--output", $DependencyEvidence
    )

    Invoke-Checked -FilePath "python" -Arguments @(
        "tools\v2\build_dependency_notices.py",
        "--evidence", $DependencyEvidence,
        "--approved-audit", $ResolvedAudit,
        "--ue4ss-source-root", $ResolvedUe4ssSourceRoot,
        "--project-root", $ResolvedProjectRoot,
        "--build-root", $ResolvedBuildRoot,
        "--cargo-root", $ResolvedCargoRoot,
        "--notice-output", $DependencyNotices,
        "--report-output", $DependencyReport,
        "--ue4ss-commit", $Ue4ssCommit
    )

    $ProvenanceObject = Get-Content `
        -LiteralPath $Provenance `
        -Raw |
        ConvertFrom-Json
    $MainDll = Resolve-ProvenanceBinary `
        -Record $ProvenanceObject.main `
        -Label "MecchaCamouflage mod"
    $Ue4ssDll = Resolve-ProvenanceBinary `
        -Record $ProvenanceObject.ue4ss `
        -Label "UE4SS runtime"
    $ProxyDll = Resolve-ProvenanceBinary `
        -Record $ProvenanceObject.proxy `
        -Label "UE4SS proxy"

    Invoke-Checked -FilePath "python" -Arguments @(
        "tools\v2\assemble_runtime.py",
        "--project-root", $ResolvedProjectRoot,
        "--ue4ss-dll", $Ue4ssDll,
        "--proxy-dll", $ProxyDll,
        "--mod-dll", $MainDll,
        "--ue4ss-settings",
        (Join-Path $ResolvedProjectRoot "third_party\RE-UE4SS\assets\UE4SS-settings.ini"),
        "--member-variable-layout",
        (Join-Path $ResolvedProjectRoot "third_party\RE-UE4SS\assets\MemberVarLayoutTemplates\MemberVariableLayout.ini"),
        "--ue4ss-license",
        (Join-Path $ResolvedProjectRoot "third_party\RE-UE4SS\LICENSE"),
        "--dependency-notices", $DependencyNotices,
        "--output-root", $RuntimeRoot,
        "--layout-output", $PayloadLayout
    )

    Invoke-Checked -FilePath "python" -Arguments @(
        "tools\v2\build_payload_cab.py",
        "--payload-root", $RuntimeRoot,
        "--layout", $PayloadLayout,
        "--manifest-output", $PayloadManifest,
        "--cab-output", $PayloadCab,
        "--product-version", $ProductVersion,
        "--ue4ss-commit", $Ue4ssCommit
    )

    Invoke-Checked -FilePath "cmake" -Arguments @(
        "-S", $ResolvedProjectRoot,
        "-B", $ResolvedBuildRoot,
        "-DMECCHA_PAYLOAD_MANIFEST=$PayloadManifest",
        "-DMECCHA_PAYLOAD_CAB=$PayloadCab"
    )
    Invoke-Checked -FilePath "cmake" -Arguments @(
        "--build", $ResolvedBuildRoot,
        "--target", "meccha_camouflage_launcher"
    )
    Invoke-Checked -FilePath "ctest" -Arguments @(
        "--test-dir", $ResolvedBuildRoot,
        "--output-on-failure"
    )

    $LauncherMatches = @(
        Get-ChildItem `
            -LiteralPath $ResolvedBuildRoot `
            -Recurse `
            -File `
            -Filter $ExecutableName |
            Where-Object { $_.FullName -notmatch "[\\/]_deps[\\/]" }
    )
    if ($LauncherMatches.Count -ne 1) {
        throw "Expected exactly one $ExecutableName; found $($LauncherMatches.Count)."
    }
    Copy-Item `
        -LiteralPath $LauncherMatches[0].FullName `
        -Destination (Join-Path $Artifact $ExecutableName)

    Invoke-Checked -FilePath "python" -Arguments @(
        "tools\v2\verify_release_artifact.py",
        "--artifact-directory", $Artifact,
        "--executable-name", $ExecutableName,
        "--payload-manifest", $PayloadManifest,
        "--payload-cab", $PayloadCab,
        "--payload-layout", $PayloadLayout,
        "--provenance-report", $Provenance,
        "--report-output", $ReleaseReport,
        "--checksum-output", $ReleaseChecksum,
        "--product-version", $ProductVersion,
        "--ue4ss-commit", $Ue4ssCommit,
        "--signing-policy", "unsigned"
    )
} catch {
    if (Test-Path -LiteralPath $Artifact) {
        Get-ChildItem -LiteralPath $Artifact -Force |
            Remove-Item -Force -Recurse
    }
    throw
} finally {
    Pop-Location
}

Write-Host "PASS release candidate: $Artifact\$ExecutableName"
Write-Host "Evidence: $Evidence"
