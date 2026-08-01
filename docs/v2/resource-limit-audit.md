# MecchaCamouflage v2 Checked-Arithmetic and Resource-Limit Audit

## Scope and result

This audit covers the current v2 production graph from external bytes and game
counts through parsing, allocation, traversal, queueing, rendering, persistence,
deployment, and embedded-payload extraction. It excludes v1 retirement code and
research-only tooling from the production result.

Result: no production allocation or traversal derived from an untrusted size is
admitted without a prior hard bound and, where the bound alone is not a complete
proof, checked addition/multiplication. Bounded operations that fail discard the
whole candidate/frame/transaction rather than publishing a prefix.

This closes the Phase 6 source-level cross-feature arithmetic audit. It does not
close the Phase 13 measured memory/performance/storage baseline or any live game
gate.

## Audit rules

The reviewed code follows these rules:

1. Bound encoded bytes and collection counts before parsing or allocation.
2. Reject zero, non-finite, negative, stale, or enum-invalid values before
   converting them to indices, dimensions, or progress arithmetic.
3. Promote dimensions to `uint64_t` and division-check multiplication before
   narrowing to `size_t`, `UINT`, `DWORD`, or reflected `int32` fields.
4. Accumulate aggregate byte/work counts with `value > maximum - total`, or an
   equivalent checked helper, before addition.
5. Preflight complete work and output capacity before the first state, file,
   texture, preview, or Canvas mutation.
6. Use hard-capacity queues and bounded drains; overflow closes or rejects the
   complete unit of work rather than silently dropping an interior element.
7. Catch allocation/platform exceptions at the owning public boundary and
   return a typed failure without publishing mutable state.

## Domain inventory

| Boundary | Hard limits and checked arithmetic | Fail-closed publication | Direct tests |
| --- | --- | --- | --- |
| Config and localization | Config is at most 64 KiB; localization input is bounded, each value is at most 4096 bytes, placeholder indices are checked before decimal accumulation, exactly 16 catalogs are admitted. | Parse/validation produces a complete candidate before atomic save or catalog publication. | `config_persistence`, `localization`, `core_contract` |
| Mesh/profile resources | Input bytes, declared counts, serialized counts, index/triangle/bone/influence references, profile pairing, topology hashes, and exact packaged identities are bounded before materialization. | A profile/catalog is published only after all three raw/image pairs validate. | `mesh_profile_codec`, `image_paint_profile_catalog`, `production_resources` |
| Paint planning | At most 600,000 adaptive/replay/preview entries; texture dimensions and channel lengths are exact; frame dispatch and observed queues have hard capacities; cancellation is checked during long loops. | Plans, previews, and terminal job state publish only for the matching generation; overflow/cancellation returns no partial plan. | `core_contract`, `paint_planner`, `paint_preview_composer`, `paint_dispatch`, `paint_job_coordinator` |
| Projective Paint | At most 600,000 appearance samples, 8192 source queries, 32 hit tests per later HUD frame, a fixed four-texel calibration lattice, bounded correction-field vertices/edges/iterations, bounded feedback passes, and exact raster/pass lengths. Dimension products use promoted or division-checked arithmetic. | No appearance capture publishes before byte-exact restoration and component validation; only validated local Albedo plus dual-evidence physical Emissive can publish. | `paint_appearance`, `paint_appearance_capture`, `paint_appearance_worker` |
| Encoded image input | Each source is 1..12 MiB; aggregate encoded source ownership is at most 64 MiB; layer/source counts are each at most 256. Aggregate additions use subtraction guards. | Picker/import/session mutations are immutable and revision-bound; an over-limit import publishes no layers or sources. | `image_file_import`, `product_ui_effect_executor`, `image_editor_session` |
| Decoded images | Dimensions are 1..8192; one RGBA decode is at most 64 MiB and a project at most 256 MiB. `width * height * 4` is promoted, division-checked, compared to both the domain maximum and `size_t`, then narrowed. | Decoder workers publish only a complete ordered project/revision-tagged result. | `image_decoder`, `image_decode_worker` |
| Image composition and guides | Canonical atlas is exactly 1024x512x4; composition is capped at 200,000,000 candidate pixel operations; source/layer identity and exact RGBA length are checked first. Guide generation has an independent pixel-work bound. | Atlas/guide bytes publish only after complete preflight and generation validation. | `image_compositor`, `image_composition_worker`, `image_guide`, `image_editor_pipeline` |
| Project/preset container | Manifest at most 1 MiB, at most 257 entries, container at most 68 MiB, bounded names, exact entry lengths/hashes, checked header/name/source total addition, and 64 MiB aggregate source limit. | Decode constructs a complete validated immutable project; storage publication is atomic and preserves the previous valid file. | `image_project_codec`, `image_project_store`, `image_project_persistence`, `image_project_io_worker` |
| ESP | At most 64 targets, 128 bones, 127 skeleton edges, 256 UTF-8 name bytes, and derived line/text limits fixed from those constants. Runtime roster/avatar arrays are capped before resolution. | A stale/invalid target fails or is omitted before geometry; frame coordination never draws a partial failed build. | `esp_frame`, `esp_frame_coordinator`, `esp_capture_codec` |
| Product UI and input | At most 8192 Canvas primitives, clip depth 16, 4096 bytes per text primitive, 1 MiB text per frame, 64 text-edit events/4096 inserted bytes, 32 function-key edges, and bounded diagnostic/action collections. Subtraction guards precede every frame aggregate addition. | Primitive/input overflow rejects the complete frame/batch; focus/mode changes discard stale input; fallback expansion preflights before draw. | `ui_canvas`, `ui_text_edit`, `product_ui_input_queue`, `product_panel`, `product_ui_frame_coordinator` |
| Scheduler and commands | Construction fixes queue capacities; one game-thread slot is reserved for control work; drains are caller-bounded; command IDs/generations reject zero and terminal overflow. | Full/closed queues reject admission without mutation; close/discard is explicit and observable. | `application_runtime`, `application_command_queue`, `input_command_router`, `application_state` |
| Runtime reflection/ABI | Every reflected property/function has exact size/kind/offset records; Unreal array counts, strings, texture dimensions, channel bytes, Canvas primitives, and ESP rosters are capped before copying into project storage. | Complete-frame/capture preflight precedes ProcessEvent or Canvas mutation; wrong thread/generation fails typed. | `runtime_reflection_contract`, `paint_capture_request`, `paint_preview_controller`, Windows `/W4 /WX` build |
| Launcher manifests and state | Manifest at most 4 MiB/4096 files/64 generated paths/1024-byte path; receipts, ledgers, journals, protocol messages, and pipe frames have independent bounds. Totals use overflow guards. | Observation precedes mutation; transactions, receipts, and ledgers publish atomically and recover or preserve prior state. | `launcher_manifest`, `runtime_transaction`, `shared_mod_ledger`, Windows broker/transport/storage tests |
| Embedded payload | CAB and total payload at most 512 MiB, one file at most 256 MiB, exact manifest total with subtraction-guard accumulation, canonical paths, reparse refusal, and exact hashes. | Extraction stays private and is removed before source publication; manifest/CAB mismatch publishes nothing. | `embedded_payload`, `payload_manifest_tool`, `payload_cab_tool`, `runtime_assembly_tool` |

## Arithmetic observations

- Small direct products such as cluster-parameter count (`clusters * 4`), ESP
  reserve estimates (`targets * 16`), and function-key edge expansion
  (`presses * 2`) occur only after hard constants prove the product is far
  below `size_t` on every supported x64 build.
- Canonical compile-time products use fixed values whose result is asserted by
  exact-size validation at their first public boundary.
- Reflected signed counts are rejected when non-positive, too large for the
  owning domain, or inconsistent with reflected element sizes before they are
  converted to an allocation size.
- Floating-point layout/projection/progress values are required to be finite
  and range-consistent before integer conversion or rendering.
- `std::bad_alloc` and platform exceptions are contained at decoder, worker,
  runtime, launcher, and callback ownership boundaries. They never convert an
  incomplete result into success.

## Remaining measured evidence

The following intentionally remains open in
[`live-test-checklist.md`](live-test-checklist.md): peak decoded/project memory,
frame callback time, planning time, dispatch/queue depth, rooted texture count,
worker/thread count, log size, launcher prepare time, and storage growth across
25 prepare/launch cycles and repeated live lifecycle sessions. Those are
measured baselines, not permission to relax the reviewed hard limits.
