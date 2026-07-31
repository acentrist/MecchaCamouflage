# Phase 8 Paint Progress

Phase 8 is open. Its pure immutable planning, packaged deformation,
projection, sample materialization, owned planning worker, and
generation-tagged dispatch boundaries are implemented and verified. Its exact
reflected `PaintAtUVWithBrush` contract and production game-thread sender now
compile against the pinned UE4SS graph. The exact production queue observer
and preview channel adapter also compile against that graph. A fail-closed
manual-color scene-capture seed now compiles against the same graph. Auto
Material appearance feedback, composition-root ownership, and live evidence
remain.

The Auto Material numerical boundary is now project-owned and independently
testable. It preserves finite HDR values up to the reviewed 64.0 rejection
ceiling, exact sRGB/linear and Oklab loss transforms, source-plus-target E0
emission noise floors, the 8-cluster/3-iteration bounded SPSA constants,
emission quantization floor, response calibration, strict sample/improvement/
DeltaE acceptance gates, and safe non-emission fallback. Source emission
regions pass both the reviewed four-pixel spatial-component filter and the
surface-key-aware strong-core/weak-halo filter; an unknown surface key never
authorizes that correction. A generation-tagged `PaintAppearanceWorker` owns
model preparation, immutable trial-preview composition, response evaluation,
cancellation, and exception containment without any UObject or runtime type.

The source-capture boundary is also project-owned. One immutable evidence
value carries BaseColor, FinalColor HDR, tone-curve HDR, intrinsic-emission
HDR, world normal, scene depth, and FinalColor LDR passes with a camera
fingerprint on every pass. Observation construction rejects changed capture
dimensions, viewport, FOV, camera position, or direction; maps the projected
deformed geometry to exact raster pixels; and admits only visible,
front-facing samples. Raw HDR conversion preserves finite negative and
greater-than-one channels, while depth conversion preserves the finite raw
red channel rather than applying display normalization.

Appearance preparation is split into two cancellable worker stages. The first
owns deformation and projection and returns immutable geometry plus a sorted,
deduplicated set of only the raster pixels the runtime needs to query. The
second consumes immutable multi-pass evidence and that geometry to build the
appearance model. This avoids a game-thread full-raster object query and
keeps all deformation, observation, clustering, and fitting work outside the
HUD callback.

## Immutable capture-to-plan contract

The game-thread boundary may now return either a complete
`PaintPlanRequest` or an immutable `PaintCaptureInput`. The latter owns:

- one complete packaged reference mesh, sampling topology, skinning influence,
  skeleton, and inverse-reference pose;
- copied current world-space bone transforms;
- one validated camera view and viewport;
- bounded immutable intrinsic/scene rasters and optional already-resolved
  automatic appearances; and
- one complete validated `PaintSettings` value.

No UObject, UE4SS, Windows, graphics, callback, or launcher type crosses this
boundary. `PaintPlanningWorker` and `PaintPreviewBuildWorker` perform
skinning, projection, raster lookup, sample construction, and planning on
their owned cancellable worker before planning or preview composition.

The resulting `PaintPlanRequest` owns copied capture values only:

- one exact validated raw round, cube, or fukuyoka profile identity;
- one complete validated `PaintSettings` value;
- bounded source samples containing destination UV/island/region, spatial
  ordering coordinates, intrinsic and captured-scene colors, safety state,
  and an optional already-resolved automatic appearance.

It contains no UObject, UE4SS, Windows, graphics, callback, or launcher type.
`UnrealRuntimeAdapter` remains responsible for resolving the source sample and
Auto Material appearance on the game thread. When Auto Material is requested,
the planner fails closed if an enabled Paint sample lacks that resolved value.
Fill never consumes automatic appearance and always retains its independent
manual color and material.

`build_paint_plan`:

1. validates the profile, settings, every finite coordinate/material, and the
   600,000-sample bound;
2. drops unsafe capture samples;
3. maps the three region modes;
4. emits the fixed 100-texel Fill base first when any region uses Fill;
5. emits only Paint regions as the overwrite pass;
6. selects manual intrinsic/captured-scene color or the resolved Auto Material
   tuple;
7. applies deterministic color/material-isolated adaptive compression;
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

This remains contract/build evidence, not a live capture claim. Auto Material
is rejected before mutation because its trial-preview feedback transaction is
not implemented. The intrinsic-emission SceneCapture profile now applies the
reviewed 33 all-disabled show flags through a borrowed call-lifetime array,
then requires an exact 33-entry component readback matching every ordered name
and value. This path compiles against the pinned UE4SS graph but has not yet
been admitted by the production Auto Material session. The composition root
does not expose the production scene-capture path, and live brush-plane
hiding, readback orientation, color semantics, cleanup, and frame-budget
behavior remain unverified.

The application boundary for that transaction is now fixed even though the
production appearance stages remain fail-closed. `ApplicationRoot` routes
Auto Material Paint and Paint Preview commands to a separate
generation-tagged session rather than the synchronous manual `capture()` call.
Admission does not advance the session in the same HUD callback. Each later
HUD callback performs at most one runtime advance; only a completed result
whose runtime contract promises exact restoration can enter the existing
planning or preview pipeline. Explicit cancellation, a failed advance, and
shutdown keep ownership and repeatedly request cleanup until the runtime
reports restoration complete. Fake-runtime coverage proves that no stroke or
partial plan is published during those paths and that Auto restoration
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

`paint_planner` passes on GCC and MSVC Release and covers:

- Fill-first/Paint-overwrite/Skip routing;
- independent Fill color, PBR, and radius;
- manual intrinsic and captured-scene color selection;
- resolved Auto Material selection without changing Fill;
- unsafe sample exclusion;
- invalid profile/sample rejection;
- missing Auto Material resolution;
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
tests pass on GCC and MSVC Release. `paint_preview_build_worker` covers copied
request ownership, bounded concurrency, cancellation, generation tagging,
typed planner/composer failures, immutable output, reuse, exception
containment, and terminal shutdown. `paint_preview_controller` covers
game-thread enforcement, capture reuse, replacement ordering, strict image
bounds, wrong-component and repeat guards, apply recovery, retained recovery
failure, shutdown restoration, malformed capture, and invalid-handle expiry.
`paint_deformation`, `paint_capture_geometry`, and `paint_capture_request`
cover exact hierarchy reconstruction, weighted skinning, current-view
projection, finite/bounded raster materialization, Auto Material presence,
and cancellation. `application_root_paint` covers the end-to-end real-Paint
and preview commands,
capture, workers, game-thread preview apply/restore, restore-before-real-Paint,
dispatch, execution, observation, completion, command backpressure, and
frame-owned UI/ESP path. It additionally proves that Auto Material bypasses
synchronous capture, cannot progress in its admission frame, publishes only
after a later multi-frame completion, retains restoration ownership after a
stage failure or explicit cancellation, and restores before shutdown
quiescing. It also holds fake runtime queues nonempty during shutdown and
proves the active Paint generation reaches `Cancelled` before quiescing.
`paint_preview_composer` adds Fill/Paint overwrite
ordering, packed-PBR quantization, edge clipping, original immutability,
invalid plan/buffer rejection, cancellation, and resource-limit evidence.
`paint_appearance_capture` covers exact pass-value preservation,
deduplicated projected-pixel queries, front-facing visibility, and camera
movement rejection. `paint_appearance_worker` additionally proves owned
profile/transform/evidence lifetimes across both worker stages and typed
geometry/evidence failures. The secret-free Linux normal and fresh
ASan/UBSan suites currently pass all 93 registered tests. The production
adapter, exact sender, queue observer, preview channel adapter, capture codecs,
and struct-array reflection bridge also compile with `/WX` in the pinned
Windows MSVC `Game__Shipping__Win64` graph. The exact Windows
reflection-contract, appearance-capture, and appearance-worker tests pass,
and post-build verification confirms the immutable UE4SS stage remains
unchanged. This is build evidence, not a live Paint pass.

## Remaining gate

- Finish production source-appearance capture on the game thread. The exact
  initialization, SceneCapture actor/component/property/function, bounded
  render-target/readback, target-mesh hiding, manual pass, and local cleanup
  paths now compile. Exact current-World brush-plane visual discovery/hiding
  also compiles from current cooked-package evidence. The immutable seven-pass
  evidence, camera-stability gate, bounded projected-pixel query, observation
  builder, and cancellable preparation worker are complete. The production
  multi-HUD-frame source-pass session, target-visible trial-preview feedback
  and exact restoration, a bounded frame budget, and live brush-plane/
  orientation/color/cleanup evidence remain.
- Implement the remaining production UE4SS capture contracts, connect the
  completed sender and queue observer through the exported composition root,
  and connect the completed preview adapter through that same root.
- Complete fake-runtime failures and the deferred single-/two-client live
  matrix before Phase 8 can close.
