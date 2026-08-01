# MecchaCamouflage v2 Architecture

This document records the implemented v2 architecture. [`PLAN.md`](../../PLAN.md)
remains the acceptance contract; this document explains how the current source
tree realizes it without weakening any live, protected-build, or release gate.

## Product boundary

MecchaCamouflage v2 is one native Windows launcher containing one verified
runtime payload. The prepared game loads one C++ UE4SS mod. Paint, Image Paint,
ESP, Settings, Diagnostics, and all product interaction live inside the game's
HUD Canvas path. There is no production C#, WinForms, WebView, TCP/JSON bridge,
custom injector, DXGI/D3D/Present hook, MinHook renderer, or UE4SS debug-tab
product UI.

The v1 tree remains only as migration evidence until the Phase 14 deletion
gate. It is not linked into a v2 target or packaged by the v2 assembler.

## Dependency direction

```text
common
  ↑
core                         launcher_core
  ↑                               ↑
application  ← persistence/codecs  native launcher shell
  ↑
feature workers/coordinators
  ↑
product_ui
  ↑
runtime adapter + mod composition root
```

- `src/common` owns dependency-free checked helpers.
- `src/core` owns validated immutable domain values and deterministic
  Paint/Image Paint/ESP algorithms. It imports no UE4SS, Unreal, Win32 UI,
  graphics, launcher, or persistence implementation.
- `src/mod/application` owns typed commands, immutable snapshots, arbitration,
  generations, workers, persistence coordination, progress, and diagnostics.
- `src/mod/ui` owns the project Canvas protocol, retained interaction state,
  product presentation, input queue, and frame coordinator. Its portable
  targets expose no UE4SS or graphics type.
- `src/mod/runtime` is the sole production translation boundary for UE4SS,
  Unreal reflection, UObject lifetime, UFunction calls, Canvas dispatch, input
  leasing, transient textures, Paint capture/dispatch, and ESP capture.
- `src/mod/entry.cpp` is the composition root. It owns resources, Windows
  editor services, the runtime adapter, application root, frame extension,
  texture coordinator, and one-shot input bindings before registration.
- `src/launcher` owns deployment observation, policy, transactions, the
  original-user application, and the minimal elevated two-file broker.
- `tools/v2` and `cmake` own trusted source staging, dependency evidence,
  runtime assembly, deterministic payload construction, and artifact checks.

The build graph and `source_graph` test enforce the intended target boundary;
source inspection remains a second defense rather than the only control.

## Runtime flow and thread ownership

```text
UE4SS lifecycle
  ├─ on_unreal_init: attach graph, register keys, initialize HUD callback
  ├─ on_update: advance application workers only; never touch UObjects
  └─ uninstall: close admission, restore, drain, unregister, destroy

ReceiveDrawHUD (game thread)
  ├─ validate World/controller/HUD/Canvas generation
  ├─ advance bounded control/frame/Paint work
  ├─ capture focused pointer/keyboard batch
  ├─ compose immutable product frame
  ├─ render complete preflighted UCanvas frame
  ├─ publish typed UI actions after successful render
  └─ capture/build/draw ESP even while the panel is closed

Owned workers
  ├─ decode and compose image projects
  ├─ persist config/projects/drafts
  ├─ prepare Paint geometry/appearance/plans/previews
  └─ return immutable generation-tagged results only
```

All UObject discovery, validation, reads, writes, UFunction calls, texture
ownership, Paint dispatch, and ESP capture occur on the game thread. Workers
receive copied immutable data. A late result must match its project, revision,
job generation, and object identity before publication.

## Frame and publication atomicity

The HUD identity binds World, controller, HUD, and Canvas weak generations to
one frame. Runtime capture and Canvas rendering reject stale or incoherent
identities. The Canvas adapter validates and expands the complete primitive
frame, including fallback glyphs and texture generations, before its first
draw mutation.

The callback-to-frame input queue is bounded and thread-safe. It discards a
complete overflowing or focus-lost batch. Keyboard modes are selected by the
retained frame state: `Disabled` while closed, `Navigation` while open, and
`TextEdit` only for the active project-name editor. Mode changes clear stale
navigation/text events without losing F1--F24 hotkeys. Only the first accepted
Commit/Cancel is admitted in one text frame.

Projective Paint follows the same publication rule at a larger scale: no
captured job is published until every target-visible feedback mutation has
been byte-restored and verified. The best validated Albedo-only state is
retained locally; physical Emissive is added only after dual source/target
evidence and final component validation. There is no selectable Auto Material,
cluster/SPSA branch, or global appearance fallback.

## Feature ownership

### Paint

The runtime captures one exact profile-bound component and immutable geometry,
appearance, and queue evidence. Owned workers deform, project, calibrate,
resolve appearance, and build the immutable plan. The game thread dispatches
only through the validated game-owned `PaintAtUVWithBrush` function. Completion
requires submitted work plus drained visual/outgoing queues and confirmation.
Preview uses exact exported Albedo and packed-PBR bytes under one guarded lease.

### Image Paint

The editor session owns one validated project, content-addressed encoded
sources, decoded immutable pixels, canonical 1024x512 composition, active-draft
persistence, and named project transactions. The texture coordinator alone
owns rooted atlas/source/guide handles. The Image Paint planner maps exact
profile topology and the canonical atlas into the same Paint plan and dispatcher.

### ESP

The runtime captures the current GameState roster, role, spectator, avatar,
camera, capsule, and exact-profile skeleton state. Core performs bounded
filtering, projection, clipping, and primitive construction. The shared Canvas
renderer draws the immutable frame without a graphics hook. Weak generation
and role/avatar cache policy prevent stale reuse across lifecycle transitions.

## Lifetime and shutdown

Shutdown closes product command and worker admission first. It cancels owned
workers and keeps the HUD callback available while game-thread restoration and
queue drain remain necessary. Preview bytes, input lease state, and rooted
textures must restore or release before lifecycle quiescing. Registered input
callbacks become inert before derived mod members are destroyed. The HUD hook
then unregisters its exact callback IDs and waits for in-flight callbacks.

If the World is already unavailable during process exit, teardown records that
condition and does not dereference stale UObjects. Explicit live unload remains
a mandatory gate and may not assume this fallback.

## Persistence and deployment

v2 uses an isolated schema-1 data root and never reads v1 settings or presets.
Config, active drafts, and named project publications are atomic and preserve
the prior valid state on failure. `.mcpreset` remains the product extension,
but only the v2 container is accepted.

The launcher observes before mutation, rejects a running game, resolves the
effective UE4SS loader chain, and selects managed, exact-shared, or no-change
conflict policy. Managed updates use a recoverable journal. The elevated child
can mutate only the preflighted `dwmapi.dll` and `override.txt`; invoking-user
LocalAppData state remains owned by the unelevated process.

## Trusted build and release boundary

The accepted UE4SS gitlink remains pristine. Trusted builds create an
independent source stage at the pinned commit and apply exactly the approved
`patternsleuth_bind/Cargo.lock` overlay under manifest verification. UE4SS and
`main.dll` build from that same compiler/source graph with Cargo `--locked`.

Runtime assembly accepts explicit binaries and reviewed resources only. A
canonical manifest covers every payload byte; the deterministic CAB must round
trip to it. The release verifier requires exactly one native x64 GUI EXE with
the reviewed imports/resources, embedded manifest/CAB identity, no legacy or
debug artifacts, and provenance matching the protected build. The current
unsigned policy rejects an unexpected certificate table.

## Forbidden production paths

The following remain architecture violations, not fallback options:

- C#, WinForms, WebView2, JavaScript, or a resident external UI;
- TCP/JSON bridges, sidecar command channels, or a custom injector;
- DXGI, D3D11, D3D12, Present, MinHook, or ProcessEvent-vtable hooks;
- non-game Paint senders, direct texture mutation for multiplayer Paint, or
  permissive reflection;
- UObject access from `on_update()` or worker threads;
- v1 config/preset migration readers;
- Defender exclusions, silent elevation, or broad privileged file access;
- unmanifested payloads, debug/probe controls, or rebuild-after-acceptance.

## Evidence map

- Build/source gate: [`phase2-build-gate.md`](phase2-build-gate.md)
- Deployment: [`phase3-deployment-gate.md`](phase3-deployment-gate.md)
- Runtime lifecycle: [`phase4-runtime-gate.md`](phase4-runtime-gate.md)
- Canvas/input/textures: [`phase5-canvas-progress.md`](phase5-canvas-progress.md)
- Core and persistence: [`phase6-core-progress.md`](phase6-core-progress.md),
  [`phase7-persistence-progress.md`](phase7-persistence-progress.md)
- Features: [`phase8-paint-progress.md`](phase8-paint-progress.md),
  [`phase9-image-paint-progress.md`](phase9-image-paint-progress.md),
  [`phase10-esp-progress.md`](phase10-esp-progress.md)
- Product integration: [`phase11-ui-progress.md`](phase11-ui-progress.md)
- Packaging: [`phase12-packaging-progress.md`](phase12-packaging-progress.md)
- Open external gates: [`live-test-checklist.md`](live-test-checklist.md)
- Release risks: [`risk-register.md`](risk-register.md)
