# Phase 9 Image Paint Progress

## Current status

The canonical atlas compositor and bounded native image decoders are
implemented and covered by focused contract tests. The compositor consumes
validated immutable project/layer values and decoded RGBA buffers. Owned
application workers run decode and composition off-thread, and an editor
pipeline connects them by exact project identity and revision. An editor
session now coordinates the derived pipeline, active draft, typed project
operations, and application root. These modules do not create Unreal textures
or drive the complete in-game editor lifecycle.
Phase 9 therefore remains open.

## Portable Canvas editor

The project-owned UI layer now consumes the existing immutable `ImageLayer`
values directly. Its bounded pointer transition preserves the retained
topmost-layer and handle-first hit order, move, four-corner resize, 24-pixel
minimum size, layer reorder, crop/zoom/move/apply, release commit, and exact
gesture-field restoration without overwriting unrelated layer fields. Results
are replacement layer values rather than direct
project or runtime mutation, so a later binding can enqueue typed editor
commands against an immutable application snapshot.

The product-panel binding now completes the retained Crop interaction without
moving decoded pixels or texture ownership into application state. Each frame
may carry bounded opaque source-texture handles tagged by exact asset ID and
decoded dimensions. Crop opens only for the selected layer's matching source,
renders that original source instead of the composed atlas, and keeps zoom and
center movement local until Apply emits one revision- and asset-guarded layer
replacement. Button/keyboard cancellation, lost edit ownership, missing
sources, and project/revision replacement discard the local session without a
command. Invalid, duplicate, oversized, zero, or unrelated source bindings
fail before Canvas composition.

The portable renderer displays the ready canonical atlas as one texture,
validates an optional guide against its overlay schema and the complete frozen
round/cube/fukuyoka ImageReference identity, and submits that guide as a
separate later Canvas texture. Selection outlines and handles remain UI
primitives. The guide can therefore never enter the canonical RGBA atlas,
`.mcpreset`, preview planning, or Paint dispatch. The producer and lifetime
owner for the three native guide textures is intentionally not guessed before
the protected UE4SS/UCanvas adapter is available.

## Native decoder boundary

`NativeImageSourceDecoder` accepts only a validated 64-character content
identity, declared PNG/JPEG/WebP codec, and `1..12 MiB` immutable encoded byte
span. It checks the encoded signature against the declared codec before
entering either native decoder.

- Windows PNG/JPEG uses WIC from an in-memory bounded stream, verifies the WIC
  container GUID and single frame, reads dimensions before allocation,
  converts only to straight `32bppRGBA`, and copies into a project-owned
  buffer.
- WebP uses libwebp `v1.6.0` at exact commit
  `4fa21912338357f89e4fd51cf2368325b59e9bd9`. Only the static decoder target is
  linked. Features and dimensions are parsed before allocation, animated
  content is rejected, and `WebPDecodeRGBAInto` writes only to the exact
  caller-owned buffer.
- Both paths cap either dimension at `8192`, cap one decoded RGBA image at
  `64 MiB`, use checked multiplication, and contain allocation/native
  exceptions as typed failures.

`image_decoder_test` covers shared decoded bounds, content identity, declared
container mismatch, truncation, encoded-size limits, and an exact lossless WebP
decode on Linux and Windows. The Windows run additionally decodes real PNG and
baseline JPEG fixtures through WIC. The upstream license is preserved at
`resources/licenses/libwebp-COPYING.txt`.

`ImageProjectDecodeWorker` keeps all codec work off the frame callback. One
request owns copied immutable encoded-source descriptors and is tagged with
generation, project ID, and project revision. Sources decode in deterministic
project order. Every adapter result is revalidated for identity, dimensions,
exact RGBA size, and the `256 MiB` aggregate decoded-project limit before an
immutable collection is published. Stop requests are checked before and
between codecs and passed through the decoder boundary; failures and escaping
exceptions become typed terminal results.

`image_decode_worker_test` covers synchronous input rejection, copied source
ownership, one-generation admission, stale cancellation, ordered immutable
success, malformed adapter output, aggregate decoded-memory rejection,
exception containment, worker reuse, and terminal shutdown.

## Editor pipeline

`ImageEditorPipeline` is the single owner of derived-atlas readiness. A valid
project submission receives a monotonic generation and enters decode, then
composition. The pipeline publishes a ready project only when both worker
results match the active generation, project ID, and revision and the resulting
atlas passes the full project contract. Consumers must request readiness with
the expected project ID and revision.

A newer revision is accepted while decode or composition is active. Only the
latest pending revision is retained, the active worker is cancelled, and its
terminal result is collected before the replacement begins. Same-project
revisions cannot move backward or repeat. Invalid submissions leave an
existing ready project untouched. Decode/composition failures remain typed and
no stale atlas is exposed. Shutdown cancels and joins both workers, discards
pending and ready values, and permanently closes admission.

An explicit load may replace a newer in-memory revision because it represents
a deliberate user project selection rather than an edit. Clear cancels active
derived work and returns to an empty editor only after the cancelled result is
collected; it cannot publish a late atlas.

`image_editor_pipeline_test` covers end-to-end decode/composition publication,
exact ready lookup, invalid and stale submission isolation, replacement during
decode and composition, explicit replacement, clear during active work, typed
decode failure, and terminal shutdown.

## Canonical composition contract

`core/image_compositor` produces exactly one 1024×512 RGBA atlas and owns no
thread, UObject, file, or decoder state.

- Layers are composed in explicit input order from bottom to top.
- Sampling is bilinear in premultiplied-alpha space and composition uses
  deterministic source-over alpha.
- Normalized source crops are validated by the project model before sampling.
- Fit preserves the cropped source aspect ratio with transparent letterboxing;
  Fill preserves the ratio while cropping the overflowing dimension.
- Seam wrapping emits clipped copies one atlas-width to either side.
- Front/back mirroring emits a horizontally reflected copy offset by half the
  atlas width.
- Pixels below the fixed opacity threshold either remain transparent for Skip
  or receive the project Fill color and reserved alpha marker for Background.
  The later image planner, not the compositor, owns face routing and material
  selection.
- No guide overlay enters this atlas. Guides remain editor-only content.

The result reports the number of composed layers and rectangles, candidate
pixel operations, and pixels that actually blended. These values are bounded
diagnostics; they are not progress or dispatch state.

## Validation and resource boundaries

Before allocating or traversing the output atlas, composition rejects:

- invalid project settings or layer values;
- empty projects, excessive layer/source counts, duplicate/empty source IDs,
  missing sources, and decoded sources that no layer owns;
- zero, excessive, or overflowing decoded dimensions;
- null, truncated, or oversized RGBA buffers;
- decoded per-image or aggregate memory above the compositor limits; and
- candidate atlas work above the fixed 200,000,000-pixel-operation budget.

The persisted source-byte limits remain 12 MiB per source and 64 MiB per
project. Decoded RGBA additionally uses an 8192 dimension cap, 64 MiB
per-image cap, and 256 MiB aggregate cap. All source buffers are shared as
immutable values. Cancellation is checked before validation, between layers,
on every composed row, and while finalizing the atlas.

## Automated evidence

`image_compositor_test` covers:

- exact atlas size, layer order, and source-over alpha;
- normalized crop;
- distinct Fit and Fill behavior;
- seam copies and horizontally reflected front/back copies;
- Skip transparency and the reserved Background Fill marker;
- malformed and mismatched decoded sources;
- pre-cancelled composition; and
- resource rejection during the constant-time preflight rather than after
  expensive partial drawing.

The test is part of the secret-free Linux and Windows MSVC suites.

## Worker boundary

`ImageCompositionWorker` copies the settings, ordered layers, and decoded-source
descriptors into one owned `std::jthread`. The referenced decoded RGBA vectors
remain shared and immutable. It:

- rejects generation zero, malformed project IDs, revision zero, concurrent
  starts, and starts after shutdown;
- forwards its stop token to the compositor;
- tags every completion with job generation, project ID, and project revision;
- publishes only an immutable atlas value;
- converts compositor failures and escaping exceptions into typed failures;
  and
- joins the completed thread before accepting another request, and cancels and
  joins on shutdown.

The worker does not own the current editable project revision.
`ImageEditorPipeline` compares all three completion tags and discards a result
when its job generation, project ID, or revision is no longer current.

`image_composition_worker_test` covers copied collections, single-generation
admission, stale-generation cancellation, cancellation propagation, completion
tags, immutable publication, worker reuse, exception containment, and terminal
shutdown.

`ui_image_editor_test` covers the portable two-layer editor, including
selection and handle precedence, normalized move/resize, retained minimum size,
release/cancel behavior, reorder, aspect-derived bounded crop sessions, stale
asset refusal, event bounds, exact guide identity, guide-after-atlas primitive
ordering, and clip-stack recovery after a bounded Canvas failure.

## Paint-plan boundary

`core/image_paint_plan` converts a canonical atlas and triangle-anchored runtime
capture samples into the same immutable `PaintPlan` used by normal Paint. It
does not own reflection, UObjects, or dispatch.

- Both raw and image-reference identities must be the frozen matching
  round/cube/fukuyoka pair selected by the project body type.
- `decode_canonical_image_profile` extends the strict profile decoder to retain
  immutable image-reference positions and indices. It validates exact vertex
  order and finite coordinates before core accepts the geometry.
- Core computes the frozen canonical center, orientation, and pixels-per-unit
  scale from those reference vertices. Every captured sample carries validated
  paint UV/spatial data plus a triangle index and barycentric weights; core
  derives its Front/Right/Back/Left face and atlas coordinate.
- Atlas sampling uses the frozen v1 nearest-pixel orientation, including the
  vertical flip between projection coordinates and image rows.
- Transparent pixels and the reserved Background marker do not emit image
  strokes. The marker remains non-paintable even if the project alpha mode is
  changed before the atlas is recomposed.
- Fill candidates are built separately per face, so a face configured Skip
  cannot acquire Fill merely because another face uses Fill.
- The Fill and opaque-image candidate sets are planned separately and then
  combined as one Fill-first adaptive plan. Fill keeps the project's Fill
  color/material and fixed radius; image strokes keep the image material,
  brush size, and optional color compression.
- Output uses no scene-lighting or Auto Material path and is directly
  compatible with the shared bounded Paint dispatcher.

`image_profile_mapping_test` covers immutable geometry validation, all three
body contracts, front/back face projection, invalid identity/counts/anchors,
degenerate triangles, and cancellation. `mesh_profile_codec_test` now decodes
all three packaged image profiles, maps the barycentric center of every shipped
triangle, proves every profile reaches all four faces, and rejects a corrupted
reference-vertex order.

`image_paint_planner_test` covers the resulting end-to-end pure mapping and
independent four-face routing, transparent and Background-marker handling,
Fill-first overwrite, color/material/radius selection, all three accepted
profile pairs, profile mismatch, truncated atlas, malformed anchors,
unsafe-sample exclusion, and cancellation.

`ImagePaintPlanningWorker` copies the complete plan request into one owned
generation and publishes an immutable `ImagePaintPlan` tagged with job
generation, project ID, and project revision. Its contract mirrors the
composition worker: no concurrent start, stop-token propagation, typed planner
failure, exception containment, collection before reuse, and cancel/join on
shutdown. `image_paint_planning_worker_test` directly covers those rules.

`ImagePaintJobCoordinator` consumes only an exactly matching generation,
project ID, and project revision. It aliases the plan's inner immutable
`PaintPlan` into the existing `PaintDispatchController`, so Image Paint has no
second sender, pacing implementation, scheduler, or drain heuristic. A project
revision change during planning cancels and collects the worker before any
stroke is admitted. A change during dispatch cancels admission, discards the
queued generation, waits through the normal queue/confirmation drain, and
terminates with the typed `StaleProject` result.

`image_paint_job_coordinator_test` covers the shared
`PaintAtUvWithBrush`-operation queue, generation preservation, terminal drain,
planning cancellation, stale-before-dispatch rejection, stale-during-dispatch
discard/drain, and typed planner failure.

## Application root boundary

`ApplicationRoot` now consumes the `ImageEditorSessionPort` and a narrow
game-thread `ImagePaintGameRuntimePort`. A typed Start command cannot
reach game capture until the requested derived atlas is ready. The runtime
port supplies only the validated raw/reference profiles, triangle/barycentric
samples, component handle, and pacing; project settings and the atlas remain
owned by the editor value.

The root starts `ImagePaintJobCoordinator`, advances it on HUD frames, observes
game queues through the Image Paint port, publishes editor and job state in the
immutable application snapshot, and shares the existing job state and
dispatcher with Paint. Typed Cancel, a newer editor revision, and shutdown all
stop admission and retain ownership until the observed queue drain is
terminal. No second sender or dispatch policy was introduced.

The session routes typed Load, Save, Rename, and Delete commands to one
off-thread persistence worker. Ready revisions are debounced to the active
draft, rename preserves the current editor value rather than reloading older
stored content, and delete waits for draft publication to become idle before
clearing the draft and named file. Persistence command identity, operation,
completion pressure, draft pressure/error, and the derived pipeline are
published together as immutable application state. Final root shutdown closes
the session only after active derived work and draft persistence settle and
runtime callbacks are unregistered.

Canvas layer and settings results now enter that same owner through a typed
`MutateImageProject` command. The command is guarded by exact project revision;
layer changes are additionally guarded by selected index and content-addressed
asset ID. The session applies only editable placement, crop, seam-wrap, mirror,
order, and validated settings fields, advances the revision once, and retains
the latest immutable draft while older decode/composition work is cancelled or
coalesced. UI snapshots copy only bounded project metadata, settings, and layer
values; encoded sources and the canonical atlas never cross the UI boundary.
Load/save/rename/delete persistence ownership also blocks editor mutation, so
an asynchronous project replacement cannot discard a concurrent Canvas edit.
Deleting the current project is not admitted while its decode/composition
generation is active; after it becomes terminal, delete owns the session,
drains draft persistence, and prevents a newer edit from recreating the draft.

`application_root_image_paint_test` covers exact ready lookup, body/profile
capture, planning through final drain, the shared `PaintAtUvWithBrush`
operation, stale command rejection before capture, typed cancellation under
queue pressure, edit invalidation during dispatch, immutable snapshot state,
cancel-before-lifecycle shutdown, all four project command routes, config
publication, and editor close ordering. `image_editor_session_test` covers the
full fake-store load/edit/draft/save/rename/delete lifecycle plus consecutive
in-flight mutations, stale revision and asset/range rejection, source-metadata
isolation, and ordered draft publication.

The production runtime adapter still needs to capture validated
triangle/barycentric anchors and real queue observations from reflected game
contracts. Until that adapter is complete, this is a tested root boundary, not
a live production path.

## Remaining work

- Construct the production session from the Win32 stores; root-owned startup
  recovery is already connected.
- Derive and version all three editor-only guide overlays.
- Implement game-thread texture creation/update/release through the accepted
  runtime adapter.
- Capture triangle/barycentric anchors through the production runtime adapter.
- Build the complete UCanvas editor and pass fake-runtime and live checks.

No decoder, runtime, or UI fallback is implied by this core milestone.
