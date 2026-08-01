# Phase 8 Paint Progress

Phase 8 is open. The portable v1.7.2 projective Paint pipeline, immutable
planning, packaged deformation, projection, sample materialization, owned
workers, preview restoration, dispatch, queue drain, and exact reflected
`PaintAtUVWithBrush` contracts are implemented. The production runtime source
also uses the bounded projective state machine. The superseded v1.7.1 Auto
Material/cluster/SPSA transaction has been deleted from the v2 graph and is not
acceptance evidence for this phase.

Every normal Paint now projects a hidden environment capture through a fixed
four-texel correction lattice. Front and Back anchor one correction field;
Side uses deterministic harmonic or one-boundary propagation and rejects
unanchored components. Albedo retains its best validated local feedback state.
Physical Emissive requires repeatable source separation plus a calibrated
target E=1 response and carries source chromaticity in bounded Albedo. The
replacement core/application/runtime/UI path, coverage-preserving compression,
and measured adaptive dispatch pacing pass the native Windows automated gates.
Private UE4SS-backed production compilation and live capture/multiplayer
evidence remain before Phase 8 can close.

The source-capture boundary is also project-owned. One immutable evidence
value carries BaseColor, FinalColor HDR, tone-curve HDR, intrinsic-emission
HDR, world normal, scene depth, and FinalColor LDR passes with a camera
fingerprint on every pass. Observation construction rejects changed capture
dimensions, viewport, FOV, camera position, or direction; maps the projected
deformed geometry to exact raster pixels. Front and Back share the directly
captured face-material path even when one destination is projection-occluded;
Side continues to require exact topology visibility and camera-facing
evidence. Raw HDR conversion preserves finite negative and greater-than-one
channels, while depth conversion preserves the finite raw red channel rather
than applying display normalization.

## Latest-v1 tracking checkpoint: v1.7.2

Until v2 is stable, maintainer-requested checkpoints advance this rewrite to
the latest stable v1 release. A new checkpoint supersedes the previous v1
behavior and resource target; historical v1 releases are not simultaneous v2
compatibility targets. The current checkpoint is v1.7.2 at merge commit
`87de3fdcf83e4a39d9bc0f47ed5e45987ec62636` from PR #256.

The packaged Round raw and image-reference profiles now use the game 3.3.0
identity introduced on the v1.7.1 line and unchanged in v1.7.2: 1,668
vertices, 8,352 indices, and profile
hash
`cd469e35ad0cbd1e483bd82b2406849429d24037807bd7a294534fb79633f55b`.
The regenerated pair still produces 39,214 default four-texel samples and
passes exact raw/image topology pairing, index bounds, deformation, guide, and
production-resource validation.

Replay partitions each Fill and Paint pass as Back, Side, then Front. The
v1.7.2 compressor preserves that partition while using a brush-aligned coverage
field, hard hole/unsafe/island/region/material boundaries, deterministic
circle covering, and per-channel minimax representative colors. Normal Paint
has one environment-capture source path; Image Paint remains direct imported
image sampling. No dynamic runtime-topology fallback is introduced; mismatched
live assets and packaged profiles continue to fail closed.

Appearance preparation is split into two cancellable worker stages. The first
owns deformation and projection and returns immutable geometry plus a sorted,
deduplicated set of only the raster pixels the runtime needs to query. The
second consumes immutable multi-pass evidence and that geometry to build the
projective model. This avoids a game-thread full-raster object query and keeps
all deformation, observation construction, correction calibration, and final
component validation outside the HUD callback.

Scene-capture subject visibility is explicit rather than inferred. All eight
source-evidence passes are `BackgroundOnly` and hide the target mesh, including
two separate intrinsic-emission HDR passes required for source repeatability.
The bounded feedback plan contains target-visible FinalColor HDR, BaseColor
E0, and intrinsic-emission E0 passes while continuing to hide the brush-plane
visuals. The candidate worker samples the composed preview albedo bytes at the
exact model UVs, drops ambiguous duplicate raster references, and returns
immutable readback references. Target feedback must match the source and
feedback cameras and calibrates identity versus red/blue-swapped linear
readback before evaluation can report a calibrated response.

The production source state machine snapshots exact albedo and packed-AMRE
bytes, composes the baseline zero-emission candidate on a worker, begins
restoration ownership before the first import can mutate, and byte-verifies
both imported channels. It waits across later HUD frames and captures at most
one of the three target-visible feedback passes per frame, restores and
byte-verifies the original channels, calibrates the fixed correction field,
then applies and verifies one E=1 endpoint candidate. One later FinalColor HDR
capture supplies the endpoint response; the original is restored again before
component validation and immutable resolved-appearance publication.

## Immutable capture-to-plan contract

The game-thread boundary may now return either a complete
`PaintPlanRequest` or an immutable `PaintCaptureInput`. The latter owns:

- one complete packaged reference mesh, sampling topology, skinning influence,
  skeleton, and inverse-reference pose;
- copied current world-space bone transforms;
- one validated camera view and viewport;
- bounded immutable environment-capture rasters and resolved projective
  appearances; and
- one complete validated `PaintSettings` value.

No UObject, UE4SS, Windows, graphics, callback, or launcher type crosses this
boundary. `PaintPlanningWorker` and `PaintPreviewBuildWorker` perform
skinning, projection, raster lookup, sample construction, and planning on
their owned cancellable worker before planning or preview composition.

The resulting `PaintPlanRequest` owns copied capture values only:

- one exact validated raw round, cube, or fukuyoka profile identity;
- one complete validated `PaintSettings` value;
- bounded source samples containing destination UV/island/region, spatial
  ordering coordinates, projective color/material, correction-field identity,
  and safety state.

It contains no UObject, UE4SS, Windows, graphics, callback, or launcher type.
`UnrealRuntimeAdapter` remains responsible for capturing runtime evidence on
the game thread. Workers resolve projective appearance from copied values. The
planner fails closed if an enabled Paint sample lacks its validated projective
value. Fill never consumes projective appearance and retains its independent
manual color and material.

`build_paint_plan`:

1. validates the profile, settings, every finite coordinate/material, and the
   600,000-sample bound;
2. drops unsafe capture samples;
3. maps the three region modes;
4. emits the fixed 100-texel Fill base first when any region uses Fill;
5. emits only Paint regions as the overwrite pass;
6. selects the resolved projective tuple for Paint;
7. applies deterministic coverage-preserving adaptive compression;
8. returns one project-owned stroke vector with the validated source texture
   dimension, pass counts, and diagnostics.

Planning accepts a `std::stop_token`. Replay construction, candidate
validation, adaptive search, and final publication check cancellation without
publishing a partial plan.

## Owned planning worker

`PaintPlanningWorker` receives a copied `PaintPlanRequest` or
`PaintCaptureInput` and permits one active generation. It:

- materializes deformation/projection/capture samples on its owned thread when
  given a capture input;
- runs the project-owned core planner on one owned `std::jthread`;
- propagates cancellation through the planner's `std::stop_token`;
- refuses generation zero, concurrent work, and work after shutdown;
- publishes exactly one immutable plan or typed planner failure tagged with the
  originating job generation;
- contains all planner exceptions at the worker boundary; and
- requires completion collection before the worker can be reused, preventing a
  late result from being confused with a newer generation.

The worker never touches UObjects, UE4SS, the scheduler, job state, or the
runtime adapter. Capture-seed failures are distinct from planner failures.

## Current capture contracts

The current shipping executable and source contracts now freeze:

- `InitializePaint` at 0x10: `MeshComponent` object input at 0x00 and bool
  return at 0x08;
- `IsInitialized` at 0x01 and `GetInitializedPaintMesh` at 0x08;
- `SceneComponent.GetSocketTransform` at 0x70, including the UE5 0x60
  `Transform` return;
- `KismetRenderingLibrary.CreateRenderTarget2D` at 0x38, including the current
  `Slices` input at 0x10 and return object at 0x30; and
- `KismetRenderingLibrary.ReadRenderTargetRaw` at 0x28, including its output
  `TArray<LinearColor>` at 0x10 and bool return at 0x21;
- `SceneCaptureComponent2D.CaptureScene` at 0x00,
  `SceneCaptureComponent.HideComponent` at 0x08 with an exact
  `PrimitiveComponent` input, and `Actor.K2_DestroyActor` at 0x00; and
- exact SceneCapture class ancestry plus reflected
  `CaptureComponent2D`, `CaptureSource`, `bCaptureEveryFrame`,
  `bCaptureOnMovement`, `bAlwaysPersistRenderingState`, `ProjectionType`,
  `FOVAngle`, and `TextureTarget` property kinds, owners, sizes, enum types,
  and object classes.
- `SceneCaptureComponent.SetShowFlagSettings` at 0x10 with one exact
  `TArray<EngineShowFlagsSetting>` input, plus the component-side
  `ShowFlagSettings` array and exact `ShowFlagName` FString/`Enabled` bool
  element schema.

The typed render-target codec admits only non-null game-thread objects,
single-image dimensions at or below 2048, and the reviewed RGBA8-sRGB or
RGBA16f formats. Readback requires the exact expected pixel count, a plausible
array header, a true function return, and finite RGBA values. The runtime
reflection bridge now describes byte arrays and struct arrays without
weakening exact inner-type validation.

The production adapter now reuses the calibrated ESP camera, validates and
initializes the profile-bound target mesh, copies every packaged bone's current
world transform, and materializes a bounded immutable manual-color capture
seed. For each accepted pass it creates and roots one transient render target,
spawns one exact `SceneCapture2D`, configures and reads back every reflected
property, hides the target mesh and the exact live brush-plane visuals,
captures once, copies finite raw linear samples into project-owned storage,
and releases the Unreal array, actor, and root on every exit. The current
cooked package fixes the brush-plane class as
`/Game/BluePrints/cLeon/BP_BrushPlane.BP_BrushPlane_C` and its visual
components as `Plane` and `Plane1` (`StaticMeshComponent`) plus `Niagara`
(`NiagaraComponent`). Runtime discovery requires exactly one actor in the
current World and exactly one live component of each exact name/class under
that actor; absence, duplication, wrong ownership, or a stale object rejects
capture before readback. Audio is not hidden. Manual unlit capture uses
BaseColor; manual lit capture also uses FinalColor HDR. Deterministic
linear-to-sRGB conversion occurs after the game-thread readback.

This remains contract/build evidence, not a live capture claim. The current
code admits the exact profile, current bone transforms, and calibrated camera;
prepares deformation/projection on a worker; captures at most one pass per
later HUD callback; retains real viewport identity; applies the reviewed 33
intrinsic-emission show flags; and restores exact preview channels before
publication. The fixed four-texel correction field, local Albedo feedback,
dual-evidence physical Emissive, endpoint response, and final component
validation have replaced the old model/endpoint/SPSA resolver. Exact
projected-pixel visibility plus live brush-plane hiding, readback orientation,
color semantics, cleanup, and measured in-game frame-budget behavior remain
unverified.

The application boundary is generation-tagged. `ApplicationRoot` routes every
normal Paint and Paint Preview command through the projective session; there is
no synchronous manual capture or optional Auto Material branch.
Admission does not advance the session in the same HUD callback. Each later
HUD callback performs at most one runtime advance; only a completed result
whose runtime contract promises exact restoration can enter the existing
planning or preview pipeline. Explicit cancellation, a failed advance, and
shutdown keep ownership and repeatedly request cleanup until the runtime
reports restoration complete. Fake-runtime coverage proves that no stroke or
partial plan is published during those paths and that projective restoration
precedes generic transient restoration at shutdown.

`PaintJobCoordinator` is the sole planning-to-dispatch transition. It compares
every completion with both its owned generation and the current shared job
generation before it can mutate job state or begin dispatch. It also:

- atomically claims the shared Paint job before starting worker work;
- carries the captured component, pacing plan, and start time with that
  generation;
- converts planner rejection or dispatch admission failure into a terminal
  typed job failure;
- cancels planning without admitting a stroke and acknowledges cancellation
  only after the worker has stopped;
- delegates dispatch cancellation to the generation-selective dispatcher; and
- collects but ignores a late result when the shared job has already been
  replaced, leaving the replacement snapshot byte-for-byte unchanged.

## Dispatch prerequisites now enforced

- Effective runtime brush radii are validated separately from the user setting
  range. The game-thread operation accepts the fixed 100-texel Fill radius and
  compressed Paint radii while the persisted setting remains `[1,10]`. Every
  immutable plan and scheduled stroke also carries the captured texture
  dimension, so the runtime never guesses the texel-to-UV conversion.
- The bounded scheduler has control and frame lanes. Contract resolution, HUD
  rebinding, and transient-state restore drain ahead of Paint strokes.
- The lifecycle exposes only the project-owned `PaintDispatchQueue` contract
  to feature coordination; its concrete scheduler remains lifecycle-owned.
- The only represented production stroke operation remains
  `PaintAtUvWithBrush`; no texture import, bridge sender, or custom multiplayer
  transport exists in v2.
- A dependency-free exact reflection validator rejects owner/name/total-size,
  property-set, kind/type, offset/size, array-dimension, and parameter-direction
  drift. The accepted records are `Vector2D` 0x10,
  `PaintChannelData` 0x24, `RuntimeBrushSettings` 0x28, and the
  `PaintAtUVWithBrush` 0x68 parameter buffer. The UE4SS record traversal and
  validation bridge is isolated in a private runtime translation unit; the
  lifecycle/ownership adapter consumes only its fail-closed validation result.
- The production `PaintStrokeRuntimePort` resolves only the exact PenguinHotel
  function, checks a generation-bound weak component owned by the acknowledged
  local body, converts sRGB bytes to linear albedo, normalizes radius using the
  captured dimension, fixes the reviewed brush/apply modes, selects the AMRE
  channel, and invokes UE4SS `ProcessEvent` only on the game thread.
- The separate production `PaintQueueRuntimePort` freezes
  `GetRecordedStrokeCount`, both replication-manager count functions,
  `GetReplicationPressure`, and `RuntimePaintReplicationPressure` by exact
  owner/name/property/ABI schema. It accepts exactly one live, exact-class,
  non-CDO/non-archetype manager whose `GetWorld()` is the active HUD World.
  The component handle must match the bound weak-object identity and generation
  before any call. Owned visual and per-component outgoing counters drive
  completion; global manager count/pressure are validated but never hold this
  job open for another player's work. Negative or non-finite results fail
  closed. Visual activity is sticky only within the same component/job
  generation and resets on replacement.

## Bounded dispatch contract

`PaintDispatchController` consumes an immutable plan only after the shared job
state has entered `Planning`. It then:

- tags every scheduled stroke with the active job generation and a monotonic
  request ID;
- admits no more than the validated pacing `calls_per_tick` after each cadence
  window;
- leaves the remaining plan untouched when the frame lane is full;
- reserves scheduler capacity for control work and drains control before
  frame work;
- publishes submitted counts, bounded queue pressure, elapsed time, and ETA;
- enters `Draining` only after every stroke has been admitted;
- requires the same generation to have no scheduled strokes, the authoritative
  visual/outgoing observations to be empty, and the final confirmation window
  to elapse before completion;
- cancels admission, removes only queued strokes from the cancelled
  generation, preserves control/other-generation work, retains the actual
  submitted count, and uses the same confirmation window before terminal
  cancellation.

Unavailable game queue observers are treated as zero only after the
confirmation window; they do not permit immediate completion or cancellation.
The captured component handle belongs to the job and does not change when a
controller/freecam pawn changes.

`ApplicationRoot` now owns the complete fake-runtime Paint path. On each HUD
callback it drains a bounded number of typed commands, captures through
`PaintGameRuntimePort`, starts the immutable worker/coordinator generation,
observes authoritative queue state only after dispatch begins, and publishes
the resulting job and command-queue state. The root never captures or observes
Paint from `on_update()`. Typed `PreviewPaint` also restores any prior lease
before a fresh capture, acquires one immutable original snapshot, starts the
preview-build worker, and applies only the matching completed generation on a
later game-thread frame. `RestorePaintPreview` cancels an in-flight build
before exact restoration, and `StartPaint` defers until the same restoration
has completed so preview pixels cannot contaminate real Paint capture.
Shutdown uses the same cancellation boundary: it collects and discards any
late worker result, restores the controller-owned original on a HUD frame, and
only then lets `RuntimeLifecycle` enter its generic transient-state restore
frame and callback-unregistration barrier.
If real Paint is active, the root first requests cancellation of that exact
generation and continues observing its local, visual, and outgoing queues on
HUD frames. Preview restoration and lifecycle quiescing cannot begin until the
job reaches `Cancelled` or another terminal state.

## Preview ownership and recovery boundary

`PaintPreviewController` owns the only project-side original preview snapshot.
The snapshot is bounded to a validated square texture no larger than 4096 and
stores immutable albedo RGBA plus the packed metallic/roughness/emissive RGBA
channel required for exact restoration.

The controller runs only on the game thread and:

- captures the original channels once per feature/component lease;
- reuses that original for repeated preview updates;
- restores the old component before another feature or component can acquire
  preview ownership;
- rejects wrong-feature, wrong-component, malformed, oversized, or
  dimension-changing preview updates before calling the runtime;
- attempts immediate exact restore when preview application fails;
- releases the lease only after verified runtime restoration;
- retains the original snapshot when recovery fails so a later explicit or
  shutdown restore can retry;
- guards repeated and wrong-component restores; and
- explicitly expires a snapshot only after its captured runtime handle has
  been invalidated.

`PaintPreviewRuntimePort` is the narrow production boundary for reflected
channel export, import, and post-import verification. Its UE4SS implementation
now freezes the two 0x20-byte UFunction records, validates byte-array inner
type/direction/offset/size, binds the acknowledged-body component generation,
captures Albedo and the packed PBR channel on the game thread, and releases
Unreal-owned export storage through the pinned allocator. Apply and restore
import Albedo plus combined AMRE and immediately export the corresponding
Albedo/Emissive views for byte-for-byte verification. Any false return,
malformed array, stale binding, dimension mismatch, or readback mismatch fails
closed so `PaintPreviewController` can retain or restore its original lease.
Composition-root ownership and live preview/restore evidence remain open.

`compose_paint_preview` is the dependency-free worker algorithm between capture
and that port. It copies the immutable original channels, applies every
Fill-first stroke as a clipped circular write, quantizes metallic, roughness,
and emissive into the packed RGBA channel, and never mutates the restoration
snapshot. It validates plan structure and channel dimensions before copying,
uses checked arithmetic, stops above the hard stroke/pixel-operation budgets,
and honors cancellation before and during scanline composition.

`PaintPreviewBuildWorker` owns the asynchronous boundary around capture
materialization, planning, and composition. It accepts copied plan/capture
input plus shared immutable original channels, permits only one active nonzero
generation, forwards one stop token through every algorithm, and publishes an
immutable texture or a typed capture/planner/composer failure tagged with the
originating generation. Collection is required before reuse, and no exception
can cross the worker boundary. It has no runtime adapter, UObject, scheduler,
or preview-lease access.

## Automated evidence

`paint_planner` passes under MSVC Release and clang-cl UBSan and covers:

- Fill-first/Paint-overwrite/Skip routing;
- independent Fill color, PBR, and radius;
- projective Paint appearance without changing Fill;
- unsafe sample exclusion;
- invalid profile/sample rejection;
- missing projective appearance rejection;
- cancellation.

`core_contract` additionally covers replay/adaptive resource limits and
cancellation. `runtime_operation_executor` covers effective Fill radius
admission, captured-dimension validation, and oversized-radius rejection.
`runtime_reflection_contract` covers every exact reflected-record mismatch,
the reviewed ABI/color/material/radius/channel encoding, the two exact preview
byte-array records, bounded square-channel inference, import-array encoding,
all exact queue record sizes, owned-counter mapping, generation isolation,
sticky drain activity, and invalid counter rejection.
`application_runtime` covers control priority and reserved control capacity
over queued Paint work.
`paint_dispatch` covers cadence, frame admission, backpressure, exact
generation-tagged stroke conversion, queue observations, confirmation,
completion, cancellation, selective discard, progress preservation, and stale
generation rejection. `paint_planning_worker` covers immutable request
ownership, bounded concurrency, cancellation, generation tagging, worker
reuse, exception containment, and terminal shutdown. `paint_job_coordinator`
covers planning-to-dispatch transition, planning cancellation, typed failure,
confirmed terminal completion, and replacement-job isolation. All related
tests pass under MSVC Release, MSVC ASan, and clang-cl UBSan. `paint_preview_build_worker` covers copied
request ownership, bounded concurrency, cancellation, generation tagging,
typed planner/composer failures, immutable output, reuse, exception
containment, and terminal shutdown. `paint_preview_controller` covers
game-thread enforcement, capture reuse, replacement ordering, strict image
bounds, wrong-component and repeat guards, apply recovery, retained recovery
failure, shutdown restoration, malformed capture, and invalid-handle expiry.
`paint_deformation`, `paint_capture_geometry`, and `paint_capture_request`
cover exact hierarchy reconstruction, weighted skinning, current-view
projection, finite/bounded raster materialization, topology/calibration
classification, resolved-appearance presence, and cancellation.
`paint_projective_pipeline` covers projective model preparation, fixed
four-texel trials independent of replay brush and final filters, Front/Back
anchors, deterministic Side propagation and unanchored rejection, local
Albedo retention, dual-evidence physical Emissive, packed-AMRE verification,
component-level validation, invalid inputs, and cancellation.
`application_root_paint` covers the end-to-end real-Paint and preview commands,
capture, workers, game-thread preview apply/restore, restore-before-real-Paint,
dispatch, execution, observation, completion, command backpressure, and
frame-owned UI/ESP path. It proves every normal Paint uses the deferred
projective session, cannot progress in its admission frame, publishes only
after a later restored completion, retains restoration ownership after a stage
failure or explicit cancellation, and restores before shutdown quiescing. It
also holds fake runtime queues nonempty during shutdown and proves the active
Paint generation reaches `Cancelled` before quiescing.
`paint_preview_composer` adds Fill/Paint overwrite
ordering, packed-PBR quantization, edge clipping, original immutability,
invalid plan/buffer rejection, cancellation, and resource-limit evidence.
`paint_appearance_capture` covers exact eight-pass source-value preservation,
two independent intrinsic-emission captures, deduplicated projected-pixel
queries, shared Front/Back material evidence, Side topology/facing visibility,
camera movement and viewport-resize rejection, and calibrated target-E0 noise
from 256 paired samples. `paint_appearance_worker` additionally proves owned
profile/transform/evidence lifetimes, immutable baseline/endpoint raster
ownership, baseline calibration, final component validation, and typed
failures. `core_contract` covers the brush-aligned compressor's hard
boundaries, deterministic covering/minimax choices, and sample/replay bounds;
the application scheduler tests cover measured dispatch cost and the finite
adaptive-delay cap.

The public hosted Windows graph contains 112 secret-free tests. The synchronized
native Windows graph passes all 112 tests under normal MSVC Release, MSVC ASan,
and clang-cl UBSan. MSVC `/analyze` passes the three-target production closure
(`meccha_product_ui`, `meccha_runtime_contracts`, and
`meccha_launcher_core`). The latest projective runtime source graph and exact
reflection/capture contracts pass, but this environment does not provide the
private UE4SS inputs needed to compile the UE4SS-backed mod target. This is
portable build/source-contract evidence, not a live Paint pass.

## Remaining gate

- Compile the new projective runtime state machine against the pinned private
  UE4SS `Game__Shipping__Win64` inputs. The source graph already contains the
  exact initialization, eight background passes, bounded projected-pixel
  queries, baseline/endpoint preview transactions, repeated exact restoration,
  fixed-lattice calibration, component validation, and immutable publication,
  but the current secret-free checkout cannot produce that private target.
- Measure the bounded in-game frame cost and record brush-plane hiding,
  projected-pixel visibility, readback orientation/color semantics, cleanup,
  cancellation, travel, and teardown evidence.
- Live-prove the connected production capture, sender, queue observer, and
  preview adapter without weakening their fail-closed contracts.
- Complete the deferred single-/two-client live matrix before Phase 8 can
  close. Portable unit, contract, fake-runtime, failure, and sanitizer evidence
  is complete.
