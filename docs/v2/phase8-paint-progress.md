# Phase 8 Paint Progress

Phase 8 is open. Its pure, immutable planning boundary is implemented and
verified; production capture, preview, dispatch, and queue-drain integration
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
8. returns one project-owned stroke vector with pass counts and diagnostics.

Planning accepts a `std::stop_token`. Replay construction, candidate
validation, adaptive search, and final publication check cancellation without
publishing a partial plan.

## Dispatch prerequisites now enforced

- Effective runtime brush radii are validated separately from the user setting
  range. The game-thread operation accepts the fixed 100-texel Fill radius and
  compressed Paint radii while the persisted setting remains `[1,10]`.
- The bounded scheduler has control and frame lanes. Contract resolution, HUD
  rebinding, and transient-state restore drain ahead of Paint strokes.
- The only represented production stroke operation remains
  `PaintAtUvWithBrush`; no texture import, bridge sender, or custom multiplayer
  transport exists in v2.

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
priority over queued Paint work.

## Remaining gate

- Capture the live component/profile/source appearance and preview snapshot
  through validated reflected contracts on the game thread.
- Run planning on the owned worker and reject stale command/generation
  completions.
- Add generation-tagged bounded per-frame admission, cancellation, game queue
  observations, terminal confirmation, elapsed/ETA publication, and exact
  preview restoration.
- Complete fake-runtime failures and the deferred single-/two-client live
  matrix before Phase 8 can close.
