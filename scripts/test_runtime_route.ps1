$ErrorActionPreference = "Stop"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SourcePath = Join-Path $RepoRoot "cppmods\MecchaCamouflage\src\dllmain.cpp"
$SourceText = Get-Content -Raw $SourcePath

Assert-True ($SourceText.Contains("route=f10_full_body_metallic_then_front")) "full-body metallic then front route marker is missing"
Assert-True ($SourceText.Contains("backend=tick_bounded_full_body_uv_grid_then_front_streaming")) "full-body/front tick backend marker is missing"
Assert-True ($SourceText.Contains("full_body_metallic=1")) "full-body metallic marker is missing"
Assert-True ($SourceText.Contains("full_body_metallic_base=1")) "full-body metallic base marker is missing"
Assert-True ($SourceText.Contains("front_after_full_body=1")) "front-after-full-body marker is missing"
Assert-True ($SourceText.Contains("front_material_reset=1")) "front material reset marker is missing"
Assert-True ($SourceText.Contains("front_material_channels_sent=2")) "front material channel marker is missing"
Assert-True ($SourceText.Contains("front_metallic=0")) "front metallic reset marker is missing"
Assert-True ($SourceText.Contains("front_roughness=0.65")) "front roughness reset marker is missing"
Assert-True ($SourceText.Contains("front_material_source=front_non_metallic_after_full_body_base")) "front material source marker is missing"
Assert-True ($SourceText.Contains("color_space=sRGB_to_linear")) "sRGB-to-linear color-space marker is missing"
Assert-True ($SourceText.Contains("rgb=(255,255,255)")) "white RGB marker is missing"
Assert-True ($SourceText.Contains("linear_rgb=(1,1,1)")) "linear white marker is missing"
Assert-True ($SourceText.Contains("metallic=1")) "metallic=1 marker is missing"
Assert-True ($SourceText.Contains("roughness=0")) "roughness=0 marker is missing"
Assert-True ($SourceText.Contains("apply_mode=Override")) "Override apply mode marker is missing"
Assert-True ($SourceText.Contains("PaintChannelAlbedoMetallicRoughness")) "combined material channel is missing"
Assert-True ($SourceText.Contains("replicated_apply=1")) "replicated apply marker is missing"
Assert-True ($SourceText.Contains("texture_import_used=0")) "texture import disabled marker is missing"
Assert-True ($SourceText.Contains("import_backend=0")) "import backend disabled marker is missing"
Assert-True ($SourceText.Contains("fallback_used=0")) "fallback disabled marker is missing"
Assert-True ($SourceText.Contains("legacy_splat_success=0")) "legacy splat disabled marker is missing"
Assert-True ($SourceText.Contains("full_body_grid=")) "full-body grid marker is missing"
Assert-True ($SourceText.Contains("brush_radius=")) "brush radius marker is missing"
Assert-True ($SourceText.Contains("brush_footprint_texels=")) "brush footprint marker is missing"
Assert-True ($SourceText.Contains("replicated_batches_sent=")) "replicated batch count marker is missing"
Assert-True ($SourceText.Contains("batch_strokes_sent=")) "batch stroke count marker is missing"
Assert-True ($SourceText.Contains("ServerPaintBatch")) "ServerPaintBatch probe is missing"
Assert-True ($SourceText.Contains('stroke_server_rpcs{STR("ServerSendPaint"), STR("ServerPaint")}')) "single-stroke server fallback is missing"
Assert-True ($SourceText.Contains('local_rpcs{STR("PaintAtUVWithBrush")}')) "PaintAtUVWithBrush local echo is missing"
Assert-True ($SourceText.Contains("color_source=hidden_character_capture")) "front hidden capture color source is missing"
Assert-True ($SourceText.Contains("readback_backend=sampled_pixel_tick")) "front sampled readback marker is missing"

Assert-True (-not $SourceText.Contains("auto run_play() -> void")) "synchronous run_play route remains"
Assert-True (-not $SourceText.Contains("run_play();")) "F10 path still calls synchronous run_play"
Assert-True ($SourceText.Contains("start_pipeline_job")) "tick pipeline start is missing"
Assert-True ($SourceText.Contains("advance_pipeline_job")) "tick pipeline advance is missing"
Assert-True ($SourceText.Contains("prepare_full_body_metallic_apply")) "full-body preparation function is missing"
Assert-True ($SourceText.Contains("continue_to_front_after_full_body_metallic")) "front continuation after full-body base is missing"

$AdvanceStart = $SourceText.IndexOf("auto advance_pipeline_job() -> void")
$ExecuteOnceStart = $SourceText.IndexOf("auto execute_pipeline_once() -> void", $AdvanceStart)
Assert-True ($AdvanceStart -ge 0 -and $ExecuteOnceStart -gt $AdvanceStart) "advance_pipeline_job section not found"
$AdvanceText = $SourceText.Substring($AdvanceStart, $ExecuteOnceStart - $AdvanceStart)
Assert-True ($AdvanceText.Contains("prepare_full_body_metallic_apply()")) "ResolveTarget does not jump to full-body preparation"
Assert-True ($AdvanceText.Contains("if (m_pipeline_job.full_body_metallic_only)")) "Apply stage lacks full-body branch"
Assert-True ($AdvanceText.Contains("call_replicated_paint_batch")) "full-body branch does not use batch paint"
Assert-True ($AdvanceText.Contains("call_replicated_paint_at_uv")) "full-body branch lacks single-stroke fallback"
Assert-True ($AdvanceText.Contains("continue_to_front_after_full_body_metallic();")) "full-body branch does not continue to front"
Assert-True ($AdvanceText.Contains("hidden_capture=front_phase")) "route does not mark hidden capture as front-only"
Assert-True ($AdvanceText.Contains("side_back_query_skipped=1")) "full-body result does not explicitly skip side/back query"
Assert-True ($AdvanceText.Contains("native_virtual_screen_paint=0")) "full-body result does not explicitly disable native virtual side/back paint"
Assert-True ($AdvanceText.Contains("force_front_material_after_metallic_base(m_pipeline_job.material_evidence);")) "front phase does not reset material after metallic base"
Assert-True ($SourceText.Contains("FrontMetallicAfterMetallicBase = 0.0")) "front material reset does not set metallic=0"
Assert-True ($SourceText.Contains("FrontRoughnessAfterMetallicBase = 0.65")) "front material reset does not set roughness=0.65"
Assert-True ($AdvanceText.Contains("m_pipeline_job.include_material_channels = true;")) "front phase does not send material channels after metallic base"

$FullBodyStart = $SourceText.IndexOf("auto prepare_full_body_metallic_apply")
$SideStart = $SourceText.IndexOf("auto prepare_side_back_query", $FullBodyStart)
Assert-True ($FullBodyStart -ge 0 -and $SideStart -gt $FullBodyStart) "full-body preparation block not found"
$FullBodyPrepareText = $SourceText.Substring($FullBodyStart, $SideStart - $FullBodyStart)
Assert-True (-not $FullBodyPrepareText.Contains("begin_sampled_scene_capture")) "full-body preparation starts scene capture"
Assert-True (-not $FullBodyPrepareText.Contains("hidden_background_capture")) "full-body preparation references hidden capture"
Assert-True (-not $FullBodyPrepareText.Contains("prepare_side_back_query")) "full-body preparation references side/back query"
Assert-True (-not $FullBodyPrepareText.Contains("PaintAtWorldPosition")) "full-body preparation uses PaintAtWorldPosition"
Assert-True ($FullBodyPrepareText.Contains("srgb_to_linear_component(1.0)")) "white sRGB input is not converted through the linear boundary"
Assert-True ($FullBodyPrepareText.Contains("white_metallic.roughness = 0.0")) "full-body roughness is not fixed to 0"
Assert-True ($FullBodyPrepareText.Contains("white_metallic.metallic = 1.0")) "full-body metallic is not fixed to 1"
Assert-True ($FullBodyPrepareText.Contains("white_metallic.apply_mode = 0")) "full-body apply mode is not Override"

$SidePrepareCall = $AdvanceText.IndexOf("prepare_side_back_query(front_samples)")
$SideSkipGuard = $AdvanceText.LastIndexOf("if (m_pipeline_job.full_body_metallic_base_done)", [Math]::Max(0, $SidePrepareCall))
Assert-True ($SidePrepareCall -gt 0 -and $SideSkipGuard -gt 0 -and $SideSkipGuard -lt $SidePrepareCall) "side/back query is not guarded off for the full-body metallic then front route"

Write-Host "runtime route checks passed"
