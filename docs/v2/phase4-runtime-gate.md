# Phase 4 Runtime Lifecycle Gate

## Current status

The secret-free application runtime foundation is implemented and tested. The
production UE4SS composition root and live callback adapter remain blocked on
the trusted recursive UE4SS build and live architecture gate, so Phase 4 is
open.

## Implemented contracts

- `meccha_application` is built independently from UE4SS and depends only on
  project-owned core interfaces.
- `ApplicationRoot` owns the runtime lifecycle, loaded configuration, bounded
  typed command queue, Paint planner/worker/coordinator, job and preview state
  machines, compatibility state, bounded diagnostics, and immutable snapshot
  publisher. Platform adapters remain injected ports.
- The root runtime state is explicit:
  `Cold -> Initializing -> Compatible | Incompatible -> ShuttingDown ->
  Stopped`.
- Configuration failures stop before callback registration. Callback failures
  publish a structured incompatibility, and a later valid frame can recover a
  transient frame-identity failure without deleting its diagnostic history.
- `CallbackBarrier` stops admission atomically, tracks RAII callback leases,
  and waits until all admitted callbacks have returned.
- `GameThreadScheduler` is bounded, FIFO, serialized for drain/discard, and
  retains the front operation when execution fails.
- A non-game thread cannot execute or consume scheduled Unreal work.
- `RuntimeLifecycle::on_update()` changes only an atomic heartbeat and has no
  runtime-adapter reference or UObject operation.
- The first valid HUD frame resolves initial contracts and then binds one
  project-owned identity containing World, controller, HUD, and Canvas
  generations through the game-thread executor.
- A change to any identity member is rebound before queued feature work. A
  stable identity does not repeat the bind.
- A zero or incomplete frame identity fails closed before contract resolution,
  rebinding, or queued Unreal execution.
- Compatibility failures use project-owned contract and failure-kind enums.
  Unsupported game builds are classified separately from runtime contract
  failures, and successful validation clears stale failure context.
- Game-thread executor failures carry optional structured compatibility
  context through the scheduler and lifecycle into the immutable application
  snapshot without stringly typed contract IDs.
- Diagnostic entries preserve optional structured compatibility context in a
  fixed-capacity oldest-first eviction history.
- The HUD callback trampoline is non-throwing. Unexpected adapter exceptions
  become a closed runtime execution failure instead of crossing the UE4SS
  callback boundary.
- The scheduler's former representative IDs are replaced by immutable
  project-owned `PaintAtUvWithBrush` and Image preview-texture upload
  requests. Opaque handles carry identity plus generation, RGBA payloads are
  immutable shared buffers, and all dimensions/ranges are checked before the
  runtime port is called.
- `RuntimeOperationExecutor` is the sole typed dispatcher from scheduled
  operations to `UnrealRuntimePort`. It independently rejects direct
  off-game-thread calls and preserves adapter failures unchanged.
- `PaintGameRuntimePort` is the only root-facing capture/queue-observation
  boundary. A Start Paint command is captured on the HUD callback, planned
  from copied values off-thread, admitted through the lifecycle-owned queue,
  executed through `PaintAtUvWithBrush`, and completed only after observed
  runtime drain.
- Shutdown closes scheduling, deterministically discards pending feature work,
  first requests generation-checked cancellation of active Paint and waits for
  its local and observed visual/outgoing queues to drain. It then cancels and
  collects any root-owned Paint preview build, restores its exact project-owned
  texture snapshot on the game thread, and requires transient UI/input
  restoration on the next game-thread frame. It refuses callback
  unregistration until the job and both restoration layers are terminal.
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
- invalid-identity rejection, initial resolution, controller replacement
  rebinding, and stable-frame dispatch;
- compatibility classification, stale-failure clearing, and bounded structured
  diagnostic eviction;
- composition-root initialization, configuration/callback fault paths,
  snapshot publication, recovery, and restore-before-finalize shutdown;
- structured executor failure propagation and exception containment at the HUD
  callback boundary;
- failed HUD rebinding without publishing the replacement identity, followed by
  deterministic retry on the next valid frame;
- failed transient-state restoration without unregistering the callback,
  followed by deterministic retry;
- callback-unregistration failure publication and exact destructor recovery;
- 128 consecutive initialize/resolve/restore/unregister cycles;
- typed Paint/Image runtime dispatch, invalid-request rejection, and direct
  off-thread port isolation;
- cancellation of queued work during shutdown;
- restore-before-unregister ordering.

`application_root_paint_test` additionally covers bounded command
backpressure, immutable snapshot queue pressure, typed Paint capture,
off-thread planning, lifecycle-owned dispatch, runtime queue observation,
terminal completion, frame-owned UI/ESP toggles, and project-preview restore
before lifecycle transient restore and callback finalization end to end. A
second root fixture holds authoritative visual/outgoing queues nonempty and
proves lifecycle quiescing cannot overtake active Paint cancellation/drain.

The test runs in both the Linux secret-free build and the Windows MSVC
`/W4 /WX` build.

## Remaining Phase 4 work

- Add production reflected Paint preview import/export, Image Paint, ESP, full
  UI and persistence lifecycle, and the production `UnrealRuntimeAdapter` to
  the existing owned root as those modules are implemented.
- Implement the pinned UE4SS callback registration adapter and prove exact
  callback unregistration against its real APIs.
- Bind the typed Paint/Image requests to validated reflected UFunction and
  texture contracts in the production adapter and run the controlled live
  calls.
- Bind the project-owned World/controller/HUD/Canvas identity to validated
  UE4SS object generations and prove invalidation in the live UE 5.6 game.
- Add complete lifecycle fault injection, concurrent uninstall, and repeated
  initialize/unload tests.
- Run the deferred live load, travel, HUD replacement, freecam, spectator,
  explicit unload, and game-shutdown matrix.
