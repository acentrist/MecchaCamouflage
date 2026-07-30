# Phase 9 Image Paint Progress

## Current status

The dependency-free canonical atlas compositor is implemented and covered by a
focused contract test. It consumes validated immutable project/layer values and
decoded RGBA buffers, but it does not decode files, create Unreal textures,
drive the application state machine, or dispatch paint. Phase 9 therefore
remains open.

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

## Remaining work

- Implement bounded WIC PNG/JPEG adapters.
- Select, pin, license, and implement the bounded native WebP adapter.
- Add the owned cancellable composition worker and stale-revision rejection.
- Connect project persistence and editing commands to the application root.
- Derive and version all three editor-only guide overlays.
- Implement game-thread texture creation/update/release through the accepted
  runtime adapter.
- Convert the atlas through all three image profiles into the accepted Paint
  planner/dispatcher with face Fill/Skip and material routing.
- Build the complete UCanvas editor and pass fake-runtime and live checks.

No decoder, runtime, or UI fallback is implied by this core milestone.
