# MecchaCamouflage Native Bridge

This directory is owned by the `p` runtime.

- `src/meccha_xenos_bridge.cpp`: injected bridge DLL source.
- `src/meccha_xenos_injector.cpp`: process injector source.
- `bin/`: expected compiled artifacts bundled by PyInstaller.

The current bridge implements the newline JSON protocol surface:

- `ping`
- `capabilities`
- `paint_full_route`

`paint_full_route` is currently switchable between the release-oriented replicated paint route and temporary diagnostic routes.

## Route decisions

| Route | Status | Decision |
| --- | --- | --- |
| `metallic_base_then_front_texture_import_diagnostic` | Adopted diagnostic default | Use for front texture quality, color, sRGB, metallic/roughness, and capture alignment inspection. Coverage warnings must not block diagnostic import. |
| `texture_import_diagnostic` | Diagnostic only | Albedo-only local import isolation. Not multiplayer-safe success. |
| `front_metallic_texture_paint_stream` / `texture_atlas_paint_api_stream` | On hold | Replicated paint API streaming is required for multiplayer later, but resumes only after texture/capture quality is stable and visible/export observations are separated. |
| `cpu_mesh_raycast` | On hold | May run only when explicitly selected. It requires exact Dumper7/UE5 private render-data layout for `USkeletalMesh -> FSkeletalMeshRenderData -> LODRenderData -> vertex/index/UV`; reflection memory scanning is not accepted. |
| `cpu_mesh_probe_only` | Probe only | Writes `mesh_snapshot_v2.json`, `cpu_raycast_validation.json`, `raycast_profile.json`, `uv_hit_heatmap.pgm`, and `atlas_preview.ppm`. It must not paint or import. |
| `cpu_mesh_texture_import_diagnostic` | Gate 4 candidate | May import only after exact mesh snapshot and CPU validation pass. Until then it fails closed with `sdk_layout_mismatch` or `mesh_snapshot_failed`. |
| `cpu_mesh_texture_paint_stream` | Gate 5 candidate | May stream replicated paint only after exact mesh snapshot, CPU validation, and atlas preview gates pass. |
| UObject memory scan fallback | Rejected | Pointer-table scans are unstable and must not decide the formal F10 path. |

Default F10 must not depend on `mesh_snapshot.json` or `mesh_buffer_candidates.json`. Those artifacts are research diagnostics only.

## Temporary diagnostic route

`metallic_base_then_front_texture_import_diagnostic` is the temporary F10 default used only to isolate atlas/capture/UV alignment from replicated paint API behavior.

- It may call `ExportChannelToBytes` and `ImportChannelFromBytes` for local texture diagnostics.
- It must not call `ServerPaintBatch`, `PaintAtUVWithBrush`, metallic base paint, local echo, material swap, or texture/material replacement.
- It is not a release runtime route and must not be treated as multiplayer-safe success.
- It must emit `temporary_diagnostic_only=true` and `replicated_paint_used=false` in metadata.
- Success only means the diagnostic import was locally observed by export/hash verification.
- `front_texture_quality_warning` is allowed when 38923 parity coverage is low; it must not prevent diagnostic import.

## Forbidden implementation paths

These paths are not valid release-runtime solutions for `p/native`:

- Material swap. It changes assets/material references instead of using the game's paint system, so it is not an acceptable fallback.
- `ImportChannelFromBytes` or direct texture import as F10 success. It can create local texture state but does not prove multiplayer-safe replicated paint.
- Local-only echo as success. `PaintAtUVWithBrush` may be useful for diagnostics, but F10 success requires replicated server paint RPC.
- Relay-only routing without parity evidence. The UE4SS reference path uses `RuntimePaintableComponent.ServerPaintBatch`; relay APIs are diagnostics-only until proven equivalent.
- Python-generated front UV samples without game surface/world-position mapping. Synthetic UVs do not identify the player's visible front paint surface and must not be dispatched as the formal front paint path.
- Fixed/equal-spaced front dots or nearest-payload-color fill. Front camouflage must come from native game surface sampling plus hidden/background capture/readback; arbitrary UV interpolation only creates misleading dots and is not product behavior.
- Front paint without sRGB capture evidence. If capture/readback parity is unavailable, F10 must fail closed instead of guessing colors.
- Native `CreateRenderTarget2D` / `SceneCapture2D` from the reflection bridge. This repeatedly caused UE D3D12 `CreateCommittedResource` `E_INVALIDARG` crashes even with bounded render targets and byte-width enum writes. F10 must not call this backend; capture/readback needs a typed Dumper7 SDK implementation before it can be re-enabled.
- Metallic-only side effects when the full front route is impossible. If the required front capture backend is disabled, F10 must not dispatch the metallic base.
- CPU mesh raycast based on reflection or raw memory scans. It can be reintroduced only with exact SDK/private render-data layouts; otherwise it is not a fallback.

## CPU mesh raycast gates

CPU mesh raycast is the planned replacement for slow `ScreenSpaceBrushQuery` generation, but it is gated.

- Gate 1: deterministic mesh snapshot from vendored Dumper7/UE5 layout.
- Gate 2: sparse `ScreenSpaceBrushQuery` oracle validation against CPU raycast.
- Gate 3: CPU atlas preview artifacts.
- Gate 4: texture import diagnostic using the CPU atlas.
- Gate 5: replicated `ServerPaintBatch` stream.

The current layout profile is `generated_cppsdk_no_private_render_data`; the generated Dumper-7 CppSDK is vendored at `p/dumper-sdk/5.6.1-44394996+++UE5+Release-5.6-Chameleon/CppSDK` and gives exact game UFUNCTION / reflected property layouts for paint/query APIs, but it does not expose the private UE render-data chain needed for deterministic CPU mesh raycast (`FSkeletalMeshRenderData -> FSkeletalMeshLODRenderData -> vertex/index/UV`). CPU routes therefore fail closed with `sdk_layout_mismatch` and do not fall back to UE query generation.

The generated SDK does expose `RuntimePaintableComponent::HitTestAtScreenPosition(MeshComponent, ScreenPosition, PlayerController, bUseCachedTriangles)`. The front sampling diagnostic now prefers this typed cached-triangle API over the previous `DeprojectScreenPositionToWorld -> ScreenSpaceBrushQuery::QueryFromWorldRay` chain. This is not the final CPU mesh raycast route, but it is a deterministic game API with exact Dumper parameter layout and avoids one ProcessEvent call per sample.
