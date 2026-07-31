# Phase 4 Runtime Lifecycle Gate

## Current status

The secret-free application runtime foundation is implemented and tested. A
production `UnrealRuntimeAdapter` now compiles against the pinned recursive
UE4SS graph and implements the validated HUD callback/game-thread boundary plus
the exact game-owned Paint stroke, queue observation, preview channel, Canvas
texture, Image Paint mesh-identity capture, input, and transient-cleanup
operations. The adapter is not yet owned by the exported mod composition root,
production Paint mesh/sample capture remains, and no live callback has been
registered in the game, so Phase 4 remains open.

## Implemented contracts

- `meccha_application` is built independently from UE4SS and depends only on
  project-owned core interfaces.
- `meccha_runtime` is the only production target that includes UE4SS/Unreal
  headers. Its public PIMPL header exposes only project-owned callback and
  game-thread interfaces. The pinned UE4SS include roots are compiler-system
  boundaries while project-owned source remains under `/W4 /WX`.
- `UnrealRuntimeAdapter` resolves the exact
  `/Script/Engine.HUD:ReceiveDrawHUD` function and validates its owning class,
  eight-byte parameter layout, ordered `SizeX`/`SizeY` integer parameters,
  and lack of additional parameters before registering a hook.
- The adapter also validates the exact HUD, World, PlayerController, and Canvas
  classes plus the HUD `PlayerOwner` and `Canvas` object-property kinds,
  classes, array dimensions, and container layouts. Any mismatch fails closed
  before callback registration.
- Hook registration uses the pinned UE4SS generic reflected-function API,
  retains the returned pre/post ID pair, and supplies the same function and ID
  pair to exact unregistration. The adapter makes new callbacks inert before
  removal, waits for its own admitted callbacks before clearing reflected
  state, and keeps a second application-level callback barrier around the
  complete root notification. The post-hook exception boundary never lets an
  exception cross into UE4SS.
- A HUD frame is admitted only when UE4SS reports the actual game thread and
  every object is real, of the validated class, and resolves through a
  nonzero-serial `FWeakObjectPtr`. The project identity combines that serial
  with the object index for World, controller, HUD, and Canvas. A stale,
  incomplete, or wrong-thread frame is delivered as an invalid identity and
  fails closed in the existing lifecycle.
- `ApplicationRoot` owns the runtime lifecycle, loaded configuration, bounded
  typed command queue, Paint planner/worker/coordinator, job and preview state
  machines, compatibility state, bounded diagnostics, and immutable snapshot
  publisher. Platform adapters remain injected ports.
- `ApplicationRoot` also owns the single admission point for an optional
  project-owned `RuntimeFrameExtensionPort`. It passes the exact validated HUD
  identity only after publishing the current runtime snapshot; the production
  UI coordinator implements this port without exposing UI or Unreal types to
  the application layer.
- The root runtime state is explicit:
  `Cold -> Initializing -> Compatible | Incompatible -> ShuttingDown ->
  Stopped`.
- Configuration failures stop before callback registration. Callback failures
  publish a structured incompatibility, and a later valid frame can recover a
  transient frame-identity failure without deleting its diagnostic history.
- `CallbackBarrier` stops admission atomically, tracks RAII callback leases,
  and waits until the complete admitted callback has returned, including its
  non-throwing observer/composition-root notification.
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
- The scheduler contains only immutable project-owned contract resolution,
  frame rebinding, `PaintAtUvWithBrush`, and transient-state restoration
  requests. Opaque Paint handles carry identity plus generation, each Paint
  request carries the captured validated texture dimension required to
  convert texel radius into Unreal UV radius, and all dimensions/ranges are
  checked before a runtime port is called. Image editor texture ownership uses
  its dedicated game-thread coordinator/runtime port instead of a second
  generic scheduler operation.
- `RuntimeOperationExecutor` is the sole typed dispatcher from scheduled
  operations to separate frame, Paint-stroke, and transient-state ports. It
  independently rejects direct off-game-thread calls and preserves adapter
  failures unchanged. The separate texture coordinator remains the only
  editor-texture lifetime owner, preventing a partial production adapter from
  using no-op implementations to claim unsupported Image/UI responsibilities.
- The runtime contract module compares reflected records exactly: owner, name,
  total byte size, property set, kind, referenced struct/class/enum, offset,
  element size, array dimension, and parameter direction. It freezes
  `Vector2D` at 0x10 bytes, `PaintChannelData` at 0x24,
  `RuntimeBrushSettings` at 0x28, and
  `PaintAtUVWithBrush` parameters at 0x68. Near-match names, extra or duplicate
  properties, and any layout drift fail closed before `ProcessEvent`.
- The production Paint-stroke port resolves only
  `/Script/PenguinHotel.RuntimePaintableComponent:PaintAtUVWithBrush`, proves
  the receiver belongs to the acknowledged local body (or retains the same
  previously proven body during a same-world/same-controller freecam
  transition), and checks the request's weak-object identity plus generation.
  Its reviewed ABI encoder converts sRGB bytes to linear color, normalizes the
  brush radius by the captured texture dimension, fixes Override/Spherical/
  Normal brush behavior, and selects the combined albedo/metallic/roughness/
  emissive channel. No alternate production Paint sender exists.
- The independent Paint-queue port validates exact schemas for
  `GetRecordedStrokeCount`, `GetQueuedStrokeCountForComponent`,
  `GetQueuedStrokeCount`, `GetReplicationPressure`, and
  `RuntimePaintReplicationPressure`. It enumerates only exact-class,
  non-default live manager objects and accepts the manager only when exactly
  one resolves to the active HUD World. Every observation revalidates the
  bound component handle, weak objects, World, and job generation before
  issuing game-thread `ProcessEvent` calls. Completion uses only the owned
  component counters; global pressure is validated without attributing another
  player's work to the current job.
- The preview runtime port validates exact 0x20-byte
  `ExportChannelToBytes`/`ImportChannelFromBytes` schemas, including their
  byte-array inner type and direction. It captures only the
  generation-bound acknowledged-body component on the game thread, releases
  Unreal-owned export buffers through the pinned allocator, and verifies
  Albedo and packed-PBR imports by exact immediate readback. It remains a
  preview/restore route only and is never a multiplayer Paint sender.
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
  restoration on the next game-thread frame. The attached frame extension
  must then restore and stop its input lease and texture ownership; typed
  failures remain retryable on later HUD frames. The root refuses callback
  unregistration until the job and all restoration layers are terminal.
- The lifecycle's final `RestoreTransientState` call is implemented by the
  production adapter. It refuses non-game-thread or zero-generation requests,
  retries exact captured input restoration, and performs a final release of
  every project-rooted Canvas texture before reporting success.
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
- exact HUD-identity delivery to the attached frame extension, typed Canvas
  failure publication, rejection of late/duplicate attachment, and retryable
  extension shutdown before lifecycle restoration and unregistration;
- callback-unregistration failure publication and exact destructor recovery;
- concurrent finalization after exact unregistration, proving it remains
  blocked until an already-admitted observer/composition-root notification
  returns;
- 128 consecutive initialize/resolve/restore/unregister cycles;
- typed frame/Paint/transient runtime dispatch, invalid-request rejection, and
  direct off-thread port isolation;
- cancellation of queued work during shutdown;
- restore-before-unregister ordering.

`runtime_reflection_contract` additionally covers exact accepted schemas,
near-match function and owner rejection, missing/extra/duplicate property
rejection, every property kind/type/offset/size/array/direction mismatch,
reviewed ABI sizes, texel-to-UV radius conversion, sRGB-to-linear conversion,
material encoding, AMRE selection, invalid dimension rejection, exact queue
function/pressure sizes, generation-isolated queue activity, invalid counter
rejection, exact preview byte-array records, bounded dimension inference, and
preview import ABI encoding.

`application_root_paint_test` additionally covers bounded command
backpressure, immutable snapshot queue pressure, typed Paint capture,
off-thread planning, lifecycle-owned dispatch, runtime queue observation,
terminal completion, frame-owned UI/ESP toggles, and project-preview restore
before lifecycle transient restore and callback finalization end to end. A
second root fixture holds authoritative visual/outgoing queues nonempty and
proves lifecycle quiescing cannot overtake active Paint cancellation/drain.

All 84 registered secret-free tests pass in the Linux graph. At project commit
`b774afa`, all 102 Windows tests also pass and the changed runtime graph
compiles in the MSVC `Game__Shipping__Win64` build with project sources under
`/W4 /WX`.

The production adapter additionally compiles and links with the
manifest-verified canonical UE4SS source stage, UEPseudo, and patternsleuth
graph in MSVC x64 `Game__Shipping__Win64`. The stage passes exact post-build
one-diff verification. `main.dll` remains PE32+ x64, exports exactly
`start_mod` and `uninstall_mod`, and imports the UE4SS DLL built by that graph.
This is build evidence only; it does not count as a live callback registration
or teardown pass.

## Remaining Phase 4 work

- Make the exported mod composition root own the production
  `UnrealRuntimeAdapter`, application root, and remaining runtime services.
- Register and unregister the implemented HUD hook in the live game, then
  prove that UE4SS removes the exact recorded callback pair and that all
  admitted callbacks drain before adapter destruction.
- Implement production Paint capture, connect the completed Paint-stroke,
  queue, and preview ports to the composition root, then run the controlled
  single-/two-client calls.
- Connect the implemented Image texture coordinator and production runtime
  port through the exported composition root, then prove create/render/release
  behavior in the live transition matrix.
- Prove the implemented generation-checked World/controller/HUD/Canvas
  identity invalidates and rebinds correctly in the live UE 5.6 game.
- Run the deferred live load, travel, HUD replacement, freecam, spectator,
  explicit unload, and game-shutdown matrix.
