# Phase 8 Paint Progress

Phase 8 is open. Its pure immutable planning, owned planning worker, and
generation-tagged dispatch boundaries are implemented and verified;
production capture, preview, and UE4SS runtime integration remain.

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
8. returns one project-owned stroke vector with pass counts and diagnostics.

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
runtime adapter. The application coordinator still must compare the tagged
completion with the active job before beginning dispatch.

## Dispatch prerequisites now enforced

- Effective runtime brush radii are validated separately from the user setting
  range. The game-thread operation accepts the fixed 100-texel Fill radius and
  compressed Paint radii while the persisted setting remains `[1,10]`.
- The bounded scheduler has control and frame lanes. Contract resolution, HUD
  rebinding, and transient-state restore drain ahead of Paint strokes.
- The only represented production stroke operation remains
  `PaintAtUvWithBrush`; no texture import, bridge sender, or custom multiplayer
  transport exists in v2.

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
admission and oversized-radius rejection. `application_runtime` covers control
priority and reserved control capacity over queued Paint work.
`paint_dispatch` covers cadence, frame admission, backpressure, exact
generation-tagged stroke conversion, queue observations, confirmation,
completion, cancellation, selective discard, progress preservation, and stale
generation rejection. `paint_planning_worker` covers immutable request
ownership, bounded concurrency, cancellation, generation tagging, worker
reuse, exception containment, and terminal shutdown. All related tests pass on
GCC and MSVC Release. The secret-free Linux suite currently passes all 28
registered tests.

## Remaining gate

- Capture the live component/profile/source appearance and preview snapshot
  through validated reflected contracts on the game thread.
- Connect worker completion to the active application job and reject stale
  command/generation results.
- Connect the dispatcher to `ApplicationRoot`, live game queue observers, and
  exact preview restoration.
- Complete fake-runtime failures and the deferred single-/two-client live
  matrix before Phase 8 can close.
