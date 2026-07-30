# Phase 9 Image Paint Progress

## Current status

The dependency-free canonical atlas compositor is implemented and covered by a
focused contract test. It consumes validated immutable project/layer values and
decoded RGBA buffers. An owned one-generation application worker now runs that
composition off-thread. Neither module decodes files, creates Unreal textures,
drives the full application state machine, or dispatches paint. Phase 9
therefore remains open.

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

The worker does not own the current editable project revision. The future
application/editor owner must compare all three completion tags and discard a
result when its job generation, project ID, or revision is no longer current.

`image_composition_worker_test` covers copied collections, single-generation
admission, stale-generation cancellation, cancellation propagation, completion
tags, immutable publication, worker reuse, exception containment, and terminal
shutdown.

## Paint-plan boundary

`core/image_paint_plan` converts a canonical atlas and already mapped runtime
capture samples into the same immutable `PaintPlan` used by normal Paint. It
does not own reflection, UObjects, dispatch, or profile geometry loading.

- Both raw and image-reference identities must be the frozen matching
  round/cube/fukuyoka pair selected by the project body type.
- Every captured sample carries validated paint UV/spatial data plus its
  canonical atlas coordinate and Front/Right/Back/Left face.
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

`image_paint_planner_test` covers independent four-face routing, transparent
and Background-marker handling, Fill-first overwrite, color/material/radius
selection, all three accepted profile identity pairs, profile mismatch,
truncated atlas, malformed mapping values, unsafe-sample exclusion, and
cancellation.

The runtime adapter still needs to derive the canonical coordinates from the
verified reference-profile geometry. Until that mapping and dispatcher
integration are complete, this is a tested pure boundary rather than a
production Image Paint path.

## Remaining work

- Implement bounded WIC PNG/JPEG adapters.
- Select, pin, license, and implement the bounded native WebP adapter.
- Connect the worker and reject stale revisions in the application/editor
  owner.
- Connect project persistence and editing commands to the application root.
- Derive and version all three editor-only guide overlays.
- Implement game-thread texture creation/update/release through the accepted
  runtime adapter.
- Map captured triangles through all three canonical image-reference profiles
  and connect the resulting shared Paint plan to the accepted dispatcher.
- Build the complete UCanvas editor and pass fake-runtime and live checks.

No decoder, runtime, or UI fallback is implied by this core milestone.
