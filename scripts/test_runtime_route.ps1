$ErrorActionPreference = "Stop"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SourcePath = Join-Path $RepoRoot "cppmods\MecchaCamouflage\src\dllmain.cpp"
$SourceText = Get-Content -Raw $SourcePath
$CoreFiles = Get-ChildItem (Join-Path $RepoRoot "cppmods\MecchaCamouflage\include\MecchaCamouflage\core"), (Join-Path $RepoRoot "cppmods\MecchaCamouflage\src\core") -Recurse -File
$CoreText = ($CoreFiles | ForEach-Object { Get-Content -Raw $_.FullName }) -join "`n"

Assert-True ($SourceText.Contains("route=f10_front_paint")) "F10 front paint route marker is missing"
Assert-True ($SourceText.Contains("backend=validated_bulk_only")) "validated bulk backend marker is missing"
Assert-True ($SourceText.Contains('STR("validated_bulk")')) "validated bulk result marker is missing"
Assert-True ($SourceText.Contains("fallback_used=0")) "fallback disabled marker is missing"
Assert-True ($SourceText.Contains("side_enabled=0")) "side disabled marker is missing"
Assert-True ($SourceText.Contains("auto run_play() -> void")) "synchronous run_play is missing"
Assert-True ($SourceText.Contains("MecchaCamouflage::Core::validate_capture_quality")) "core capture validation is not used"
Assert-True ($SourceText.Contains("MecchaCamouflage::Core::assemble_direct_texture")) "core texture assembly is not used"
Assert-True ($SourceText.Contains("corrected_deproject_hfov")) "F10 capture FOV diagnostic/source marker is missing"

$RunPendingStart = $SourceText.IndexOf("auto run_pending_ui_camouflage()")
$RunPlayStart = $SourceText.IndexOf("auto run_play() -> void", $RunPendingStart)
Assert-True ($RunPendingStart -ge 0 -and $RunPlayStart -gt $RunPendingStart) "run_pending_ui_camouflage section not found"
$RunPendingText = $SourceText.Substring($RunPendingStart, $RunPlayStart - $RunPendingStart)
Assert-True ($RunPendingText.Contains("run_play();")) "F10 path does not call synchronous run_play"
Assert-True (-not $RunPendingText.Contains("run_play_deferred_import")) "F10 path still uses deferred import wrapper"
Assert-True (-not $RunPendingText.Contains("start_progressive_paint_job")) "F10 path still starts ProgressivePaintJob"
Assert-True (-not $RunPendingText.Contains("advance_progressive_paint_job")) "F10 path still advances ProgressivePaintJob"

Assert-True (-not $SourceText.Contains("pixel_api_after_bulk_unverified")) "unverified pixel fallback marker remains"
Assert-True (-not $SourceText.Contains("aux_surface_copy_v1")) "broken aux_surface_copy_v1 route remains"
Assert-True (-not $SourceText.Contains("screen_silhouette_short_band_v1")) "broken screen_silhouette_short_band_v1 route remains"
Assert-True (-not $SourceText.Contains("progressive_screen_body")) "progressive_screen_body route remains"
Assert-True (-not $SourceText.Contains("tps_camera_manager_identity")) "tps_camera_manager_identity route remains"
Assert-True (-not $SourceText.Contains("camera_manager_fov_for_capture")) "camera-manager FOV capture regression remains"
Assert-True (-not $SourceText.Contains("pixel_api_batched")) "pixel_api_batched route remains"
Assert-True (-not $SourceText.Contains("auto run_projection_audit")) "projection audit command route remains"
Assert-True (-not $SourceText.Contains("fallback_camo_palette")) "procedural palette fallback remains"
Assert-True (-not $SourceText.Contains("apply_background_palette_import_fallback")) "background palette fallback remains"
Assert-True (-not $SourceText.Contains("apply_view_projection_cloak")) "old view projection route remains"
Assert-True (-not $SourceText.Contains("run_lab_probe")) "old lab probe route remains"
Assert-True (-not $CoreText.Contains("Unreal")) "core contains Unreal dependency"
Assert-True (-not $CoreText.Contains("UObject")) "core contains UObject dependency"
Assert-True (-not $CoreText.Contains("RC::")) "core contains RC dependency"
Assert-True (-not $CoreText.Contains("UE4SS")) "core contains UE4SS dependency"

Write-Host "rollback route checks passed"
