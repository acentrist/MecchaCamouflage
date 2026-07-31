# Phase 8 Paint Progress

Phase 8 is open. Its pure immutable planning, owned planning worker, and
generation-tagged dispatch boundaries are implemented and verified. Its exact
reflected `PaintAtUVWithBrush` contract and production game-thread sender now
compile against the pinned UE4SS graph. The exact production queue observer
and preview channel adapter also compile against that graph; production
mesh/sample/appearance capture, composition-root ownership, and live evidence
remain.

## Immutable capture-to-plan contract

`PaintPlanRequest` owns copied game-thread capture values only:

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

`PaintPlanningWorker` receives a complete copied `PaintPlanRequest` and permits
one active generation. It:

- runs the project-owned core planner on one owned `std::jthread`;
- propagates cancellation through the planner's `std::stop_token`;
- refuses generation zero, concurrent work, and work after shutdown;
- publishes exactly one immutable plan or typed planner failure tagged with the
  originating job generation;
- contains all planner exceptions at the worker boundary; and
- requires completion collection before the worker can be reused, preventing a
  late result from being confused with a newer generation.

The worker never touches UObjects, UE4SS, the scheduler, job state, or the
runtime adapter.

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

`PaintPreviewBuildWorker` owns the asynchronous boundary around planning and
composition. It accepts copied plan input plus shared immutable original
channels, permits only one active nonzero generation, forwards one stop token
through both algorithms, and publishes an immutable texture or a typed
planner/composer failure tagged with the originating generation. Collection is
required before reuse, and no planner exception can cross the worker boundary.
It has no runtime adapter, UObject, scheduler, or preview-lease access.

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
`application_root_paint` covers the end-to-end real-Paint and preview commands,
capture, workers, game-thread preview apply/restore, restore-before-real-Paint,
dispatch, execution, observation, completion, command backpressure, and
frame-owned UI/ESP path. It also holds fake runtime queues nonempty during
shutdown and proves the active Paint generation reaches `Cancelled` before
quiescing. `paint_preview_composer` adds Fill/Paint overwrite
ordering, packed-PBR quantization, edge clipping, original immutability,
invalid plan/buffer rejection, cancellation, and resource-limit evidence. The
secret-free Linux normal and ASan/UBSan suites currently pass all 78 registered
tests from isolated fresh build directories. The production adapter, exact
sender, queue observer, and preview channel adapter also compile in the pinned
Windows MSVC `Game__Shipping__Win64` graph; this is build evidence, not a live
Paint pass.

## Remaining gate

- Capture the live component/profile/source appearance through validated
  reflected contracts on the game thread. `InitializePaint` is known to have a
  0x10-byte parameter buffer, but its exact property schema is not frozen; v2
  must not reproduce the v1 zero-filled call.
- Implement the remaining production UE4SS capture contracts, connect the
  completed sender and queue observer through the exported composition root,
  and connect the completed preview adapter through that same root.
- Complete fake-runtime failures and the deferred single-/two-client live
  matrix before Phase 8 can close.
