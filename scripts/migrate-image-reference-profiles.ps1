[CmdletBinding()]
param(
    [ValidateSet("round", "cube", "all")]
    [string]$BodyType = "all"
)

$ErrorActionPreference = "Stop"

function Write-Utf8File {
    param([string]$Path, [string]$Json)
    [System.IO.File]::WriteAllText(
        $Path,
        $Json + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding($false))
    )
}

function Add-OrReplaceProperty {
    param(
        [object]$Target,
        [string]$Name,
        [object]$Value
    )
    if ($Target.PSObject.Properties[$Name]) {
        $Target.$Name = $Value
    } else {
        $Target | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Migrate-Profile {
    param(
        [string]$RawProfilePath,
        [string]$ImageProfilePath
    )

    $rawText = [System.IO.File]::ReadAllText($RawProfilePath)
    $legacyProfile = $rawText | ConvertFrom-Json
    $referencePose = $legacyProfile.ImageReferencePose
    if ($null -eq $referencePose) {
        throw "Raw profile has no embedded ImageReferencePose to migrate: $RawProfilePath"
    }

    # Construct the derived editor asset from the full legacy profile first.
    # It retains the exact captured pose while the dump is restored to raw-only.
    $imageProfile = $rawText | ConvertFrom-Json
    Add-OrReplaceProperty $imageProfile "ProfileRole" "image_reference"
    Add-OrReplaceProperty $imageProfile "BaseProfileId" $legacyProfile.ProfileId
    Add-OrReplaceProperty $imageProfile "BaseProfileHash" $legacyProfile.ProfileHash
    Add-OrReplaceProperty $imageProfile "ImageReferencePose" $referencePose
    Write-Utf8File $ImageProfilePath ($imageProfile | ConvertTo-Json -Depth 16)

    $legacyProfile.PSObject.Properties.Remove("ImageReferencePose")
    $legacyProfile.PSObject.Properties.Remove("ProfileRole")
    $legacyProfile.PSObject.Properties.Remove("BaseProfileId")
    $legacyProfile.PSObject.Properties.Remove("BaseProfileHash")
    Write-Utf8File $RawProfilePath ($legacyProfile | ConvertTo-Json -Depth 16)
    Write-Host "Migrated raw dump and derived Image profile: $RawProfilePath -> $ImageProfilePath"
}

$repoRoot = [System.IO.Path]::GetFullPath(
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).ProviderPath
)
$profiles = @(
    [PSCustomObject]@{
        BodyType = "round"
        RawFile = "paintman.mesh-profile-v2.json"
        ImageFile = "paintman.image-profile-v2.json"
    },
    [PSCustomObject]@{
        BodyType = "cube"
        RawFile = "paintman_cube.mesh-profile-v2.json"
        ImageFile = "paintman_cube.image-profile-v2.json"
    }
)

foreach ($profile in $profiles) {
    if ($BodyType -ne "all" -and $BodyType -ne $profile.BodyType) {
        continue
    }
    $rawPath = Join-Path $repoRoot (Join-Path "resources\mesh-profiles" $profile.RawFile)
    $imagePath = Join-Path $repoRoot (Join-Path "resources\mesh-profiles" $profile.ImageFile)
    if (-not (Test-Path -LiteralPath $rawPath)) {
        throw "Raw mesh profile is missing: $rawPath"
    }
    Migrate-Profile -RawProfilePath $rawPath -ImageProfilePath $imagePath
}
