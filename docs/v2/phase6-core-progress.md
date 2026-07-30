# Phase 6 Pure Core Progress

## Current status

The frozen domain values and validation contracts have moved from the v1 mixed
runtime header and C# models into focused, dependency-free C++ modules. Typed
application commands, immutable snapshots, the job/preview state machines, and
the first complete immutable Paint plan are also in place. Phase 6 remains
open: properties, sanitizers, and the remainder of the retained algorithms are
not yet complete.

## Implemented modules

### Paint

- Frozen Paint defaults and finite/range validation.
- Independent Paint and Fill material values.
- Fill/Paint/Skip region values.
- Fill-first replay planning, spatial ordering, deduplication, pass boundaries,
  and current-view fallback diagnostics.
- Conservative replication pacing and terminal visual/outgoing queue drain.
- Deterministic adaptive Paint compression with fixed radius candidates,
  region/UV-island/material isolation, color tolerance, edge margin, and hard
  sample/replay limits. Fill entries are never compressed.
- Replay and adaptive planning validate all finite/range/enum/index inputs,
  reject more than 600,000 samples or entries, and honor `std::stop_token`
  before and during bounded loops.
- `PaintPlan` validates the exact frozen raw round, cube, or fukuyoka profile,
  removes unsafe captured samples, selects manual or adapter-resolved Auto
  Material appearance, optionally selects captured scene color, routes a
  fixed 100-texel Fill base, applies adaptive Paint radii, and publishes one
  immutable Fill-first stroke vector.
- The pure Paint preview compositor copies validated original albedo and
  packed-PBR channels, applies the immutable Fill-first plan in order, and
  enforces checked texture sizes, a 600,000-stroke bound, a 200,000,000 pixel
  candidate budget, and row-level cancellation.
- The preview build worker owns one immutable plan request and original-channel
  pair, serializes generations, forwards cancellation through planning and
  composition, publishes immutable channel buffers, and contains exceptions.
- The Image Paint composition worker similarly owns copied settings, layer
  collections, and decoded-source descriptors; permits one active generation;
  publishes immutable atlases tagged with job generation, project identity,
  and project revision; and contains cancellation, typed compositor failures,
  exceptions, reuse, and terminal shutdown.
- The project decode worker owns copied encoded-source descriptors, invokes
  only the bounded native decoder, preserves source order, validates every
  returned identity/dimension/buffer, and enforces the aggregate decoded
  project limit before publishing an immutable collection.
- The Image Paint planning worker applies the same ownership rules to copied
  triangle captures, canonical profile/atlas values, and project identity. It
  publishes the immutable Image Paint plan only with job generation and
  project revision tags, and contains typed planner failure, cancellation,
  exceptions, reuse, and terminal shutdown.
- `ImagePaintJobCoordinator` shares the normal `JobStateMachine`,
  `PaintDispatchController`, scheduler, queue observation, pacing, drain, and
  cancellation path. It checks project identity/revision before consuming a
  plan, aliases the immutable inner `PaintPlan` without copying strokes, and
  cancels/discards/drains the active generation if the project changes during
  dispatch.

### Image Paint

- Exact 1024×512 four-tile atlas constants.
- Normalized body atlas mapping.
- Canonical cube and round/fukuyoka projection primitives.
- Layer transform and normalized crop validation.
- Per-source 12 MiB and per-project 64 MiB limits with checked accumulation.
- Frozen body, placement, face, brush, compression, material, and layer
  defaults.
- Deterministic 1024×512 RGBA composition with explicit bottom-to-top layer
  order, premultiplied bilinear sampling, source-over alpha, normalized crop,
  Fit/Fill placement, seam copies, front/back mirroring, and the reserved
  background Fill marker.
- Decoded inputs are immutable and validated for identity, dimensions, exact
  RGBA length, per-image/project memory, and exact layer/source membership.
  Candidate pixel work is preflighted against a hard budget before allocating
  or traversing the atlas, and composition honors cancellation per layer and
  row.
- The Image Paint planner accepts only exact matching raw/image-reference
  profiles, validated triangle/barycentric capture samples, and one canonical
  atlas. The application codec retains immutable image-reference vertices and
  indices from the strict packaged profile. Core derives the frozen canonical
  bounds/scale, maps every captured triangle into a Front/Right/Back/Left
  coordinate, samples atlas pixels deterministically, treats the reserved
  Background marker as non-paintable, keeps each face's Fill/Skip choice
  independent, emits Fill before opaque image Paint, isolates Fill and image
  materials, and returns the same bounded `PaintPlan` consumed by Paint.

### Mesh profiles

- Exact round, cube, and fukuyoka schema, asset/export identity, profile ID,
  topology hash, LOD, texture, vertex/index/triangle, UV-island, and bone
  contracts.
- The user-facing `fukuyoka` name maps only to the historical
  `paintman_hukuyoka` game asset alias.
- Raw and Image-reference profiles require exact base-profile linkage and
  reference-pose bone counts.
- The bounded profile decoder validates all six packaged JSON files, including
  declared/serialized counts, maximum mesh indices, bone hierarchy, triangle
  vertex/UV/bone indices, and vertex influence bone bounds.
- A single leading UTF-8 BOM is accepted only at this inherited profile
  resource boundary; the remainder still uses strict duplicate-safe JSON
  validation.

### ESP

- Scope/role filtering with spectator exclusion.
- Role-roster avatar replacement selection.
- Spectator-safe geometry capabilities.
- Bounds expansion and projection-scale calibration.

### Configuration boundary

- The v2 configuration type contains only UI, hotkey, Paint, Image Paint, ESP,
  and the small optional active-project/draft reference.
- The exact 16-locale inventory and F1–F24 mapping are validated.
- The nine UI/action keys must be unique.
- UI scale and all Paint/Image material/range values are validated as one
  candidate.
- No window geometry, opacity, always-on-top, process, WebView, bridge, or
  injector field exists in the v2 type.

### Application state

- One typed `ApplicationCommand` variant covers all retained Paint, Image
  Paint, UI/ESP, settings, and image-project actions.
- `ApplicationCommandQueue` provides a hard-capacity concurrent publication
  boundary between UI/hotkey callbacks and bounded game-frame processing. It
  rejects command ID zero, preserves FIFO order, and closes/discards exactly
  during shutdown.
- Paint and Image Paint share one generation-counted job arbiter and cannot run
  concurrently.
- Late planning results cannot mutate a newer job.
- Cancellation remains non-terminal while native admission or either observed
  queue remains active.
- Normal completion requires all work submitted, zero visual/outgoing queue,
  and visual confirmation.
- One preview state machine owns the cross-feature lease, guards component
  identity, and makes restore exactly-once.
- Snapshots are immutable shared values with monotonic revisions. Diagnostic
  history has a hard capacity and stable sequence numbers. Command queue
  pressure is published with the same immutable snapshot.
- The bounded game-thread scheduler keeps separate control and frame lanes.
  One slot is reserved from frame admission, and resolve/rebind/restore work
  drains before Paint/texture work, so teardown and rebinding cannot be
  starved by a full Paint stream while total storage remains hard-bounded.
- `RuntimeLifecycle` implements the narrow `PaintDispatchQueue` interface, so
  the composition root can coordinate Paint without exposing or duplicating
  the owned scheduler.

## Evidence

`core_contract_test`, `paint_planner_test`, `mesh_profile_codec_test`, and
`application_state_test` port representative cases from:

- `src/tests/fixtures/v1/paint-domain.json`;
- `src/tests/fixtures/v1/runtime-pacing.json`;
- `src/tests/fixtures/v1/image-mapping.json`;
- `src/tests/fixtures/v1/esp-domain.json`.

The focused core library includes no UE4SS, Unreal, Win32, graphics, launcher,
JSON, or UI header. Its test passes with both GCC and MSVC `/W4 /WX`.

The adaptive-compression cases cover disabled exact preservation,
same-material coalescing, material isolation, deterministic expansion, and
non-finite input rejection. Paint planning additionally covers Fill-first
ordering, Skip/unsafe exclusion, independent Fill PBR, manual and resolved
automatic appearance, scene-color selection, profile rejection, resource
limits, and cancellation. `application_runtime_test` proves that control work
preempts already-queued Paint work. `paint_preview_build_worker_test` covers
copied inputs, bounded concurrency, generation-checked cancellation,
planner/composer errors, immutable publication, worker reuse, exception
containment, and terminal shutdown. `application_command_queue_test` covers
hard capacity, invalid IDs, FIFO bounded drains, concurrent publication, and
terminal close/discard. The secret-free Linux suite currently passes all 43
registered tests.

## Deliberate non-port

The complete 3,700-line v1 `runtime_contract.hpp` is not copied as a unit.
Each retained algorithm must receive a project-owned type boundary and direct
golden/property evidence. Research helpers, raw UObject flags/layouts, custom
renderer behavior, and unaccepted appearance heuristics remain outside v2
until their retained requirement and production boundary are explicit.
