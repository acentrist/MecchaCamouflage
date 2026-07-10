param(
    [string]$Version = "",
    [string]$OutDir = "",
    [string]$ExePath = "",
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
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

$Version = Resolve-ProjectVersion -Requested $Version -Root $RuntimeRoot
Write-Host "Package version: $Version"

if (-not $OutDir) { $OutDir = Join-Path $RuntimeRoot ".build\package" }
$ArtifactName = "meccha-camouflage-$Version"
if (-not $ExePath) { $ExePath = Join-Path $RuntimeRoot ".build\bin\meccha-camouflage.exe" }
if (-not (Test-Path $ExePath -PathType Leaf)) { throw "Executable not found: $ExePath. Run scripts/build.ps1 first." }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$ArtifactPath = Join-Path $OutDir "$ArtifactName.exe"
if (Test-Path $ArtifactPath) { Remove-Item -Force $ArtifactPath }
$LegacyZipPath = Join-Path $OutDir "$ArtifactName.zip"
if (Test-Path $LegacyZipPath) { Remove-Item -Force $LegacyZipPath }
Copy-Item -Force -Path $ExePath -Destination $ArtifactPath
Write-Host "Wrote $ArtifactPath"
