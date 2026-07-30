# Phase 4 Runtime Lifecycle Gate

## Current status

The secret-free application runtime foundation is implemented and tested. The
production UE4SS composition root and live callback adapter remain blocked on
the trusted recursive UE4SS build and live architecture gate, so Phase 4 is
open.

## Implemented contracts

- `meccha_application` is built independently from UE4SS and depends only on
  project-owned core interfaces.
- `CallbackBarrier` stops admission atomically, tracks RAII callback leases,
  and waits until all admitted callbacks have returned.
- `GameThreadScheduler` is bounded, FIFO, serialized for drain/discard, and
  retains the front operation when execution fails.
- A non-game thread cannot execute or consume scheduled Unreal work.
- `RuntimeLifecycle::on_update()` changes only an atomic heartbeat and has no
  runtime-adapter reference or UObject operation.
- The first HUD frame resolves initial contracts and then binds the current
  world generation through the game-thread executor.
- A changed generation is rebound before queued feature work. A stable
  generation does not repeat the bind.
- Shutdown closes scheduling, deterministically discards pending feature work,
  requires transient UI/preview/input restoration on the game thread, and
  refuses callback unregistration until restoration succeeds.
- Finalization closes callback admission, unregisters the exact recorded
  callback ID, drains in-flight leases, and then reaches `Stopped`.

## Automated evidence

`application_runtime_test` covers:

- queue capacity and backpressure;
- wrong-thread rejection without queue consumption;
- FIFO budgets and retry after executor failure;
- scheduler close and deterministic discard;
- concurrent callback close/drain;
- registration and exact callback ID removal;
- an `on_update()` path with no executor calls;
- initial resolution, generation rebind, and stable-frame dispatch;
- cancellation of queued work during shutdown;
- restore-before-unregister ordering.

The test runs in both the Linux secret-free build and the Windows MSVC
`/W4 /WX` build.

## Remaining Phase 4 work

- Compose application, Paint, Image Paint, ESP, UI, diagnostics, persistence,
  and the production `UnrealRuntimeAdapter` in one owned root.
- Replace representative operation markers with the final typed runtime
  request/result contracts.
- Add structured contract IDs and bounded diagnostic history.
- Implement the pinned UE4SS callback registration adapter and prove exact
  callback unregistration against its real APIs.
- Prove World/controller/HUD/Canvas object-generation invalidation in the live
  UE 5.6 game.
- Add complete lifecycle fault injection, concurrent uninstall, and repeated
  initialize/unload tests.
- Run the deferred live load, travel, HUD replacement, freecam, spectator,
  explicit unload, and game-shutdown matrix.
