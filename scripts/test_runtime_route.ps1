$ErrorActionPreference = "Stop"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SourcePath = Join-Path $RepoRoot "cppmods\MecchaCamouflage\src\dllmain.cpp"
$SourceText = Get-Content -Raw $SourcePath

Assert-True ($SourceText.Contains("backend=screen_body_mask_validated_scene_capture")) "old validated scene-capture route is missing"
Assert-True ($SourceText.Contains("capture_alignment=show_only_body_mask")) "body-mask capture alignment marker is missing"
Assert-True ($SourceText.Contains("mask_validated_scene_capture_bulk_image")) "bulk scene-capture readback marker is missing"
Assert-True ($SourceText.Contains("mask_validated_scene_capture_pixel_api_after_bulk_unverified")) "old pixel fallback marker is missing"
Assert-True ($SourceText.Contains("auto run_play() -> void")) "synchronous run_play is missing"

$RunPendingStart = $SourceText.IndexOf("auto run_pending_ui_camouflage()")
$ProjectionAuditStart = $SourceText.IndexOf("auto run_projection_audit", $RunPendingStart)
Assert-True ($RunPendingStart -ge 0 -and $ProjectionAuditStart -gt $RunPendingStart) "run_pending_ui_camouflage section not found"
$RunPendingText = $SourceText.Substring($RunPendingStart, $ProjectionAuditStart - $RunPendingStart)
Assert-True ($RunPendingText.Contains("run_play();")) "F10 path does not call synchronous run_play"
Assert-True (-not $RunPendingText.Contains("run_play_deferred_import")) "F10 path still uses deferred import wrapper"
Assert-True (-not $RunPendingText.Contains("start_progressive_paint_job")) "F10 path still starts ProgressivePaintJob"
Assert-True (-not $RunPendingText.Contains("advance_progressive_paint_job")) "F10 path still advances ProgressivePaintJob"

Assert-True (-not $SourceText.Contains("aux_surface_copy_v1")) "broken aux_surface_copy_v1 route remains"
Assert-True (-not $SourceText.Contains("screen_silhouette_short_band_v1")) "broken screen_silhouette_short_band_v1 route remains"
Assert-True (-not $SourceText.Contains("progressive_screen_body")) "progressive_screen_body route remains"
Assert-True (-not $SourceText.Contains("tps_camera_manager_identity")) "tps_camera_manager_identity route remains"
Assert-True (-not $SourceText.Contains("pixel_api_batched")) "pixel_api_batched route remains"

Write-Host "rollback route checks passed"
