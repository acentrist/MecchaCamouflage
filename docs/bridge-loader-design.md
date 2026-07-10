# Bridge Loader Design

`bridge-loader.dll` is the stable lifecycle supervisor injected into the game
process. It exists to make bridge startup, diagnostics, restart requirements,
and safe reload decisions explicit.

The loader is not a second bridge. It must not know UE reflection, paint
payloads, `ProcessEvent`, preview/unpreview state, or multiplayer replication
rules.

## Goals

- Inject a stable loader DLL instead of injecting the versioned bridge directly.
- Load exactly one versioned `runtime-bridge.dll` through the loader.
- Report lifecycle state before the bridge TCP listener exists.
- Allow retry after bridge load/start failures when no unsafe bridge state
  exists.
- Allow bridge reload only when the bridge proves it is quiescent.
- Keep normal paint behavior on the existing bridge TCP IPC and packed
  `ServerPackedPaintBatch` route.

## Non-Goals

- No manual mapping or reflective loading.
- No stealth or anti-cheat bypass behavior.
- No in-place overwrite of a loaded DLL.
- No multiple bridge DLLs side by side.
- No adoption of old direct-injected bridges.
- No forced unload when blockers remain.
- No `TerminateThread` shutdown path.
- No production paint command handling in the loader.
- No UE reflection or `ProcessEvent` code in the loader.

## Old Direct Bridge Policy

Old direct-injected bridge modules are unsupported after loader migration.

If module enumeration finds a bridge-like module that was not loaded by the
current loader lifecycle, classify it as `LegacyDirectBridgePresent` and require
game restart. The loader must not adopt, unload, or inject beside it.

## Process Model

```text
C# app
  -> runtime-injector.exe
      -> LoadLibraryW(bridge-loader.dll)
      -> McLoader_RemoteMain(configPath)
          -> named pipe lifecycle control
          -> LoadLibraryExW(runtime-bridge.dll)
          -> McBridge_GetApi(...)
          -> bridge Create/Start
              -> existing TCP bridge IPC
              -> existing packed paint route
```

## Runtime Cache Layout

The loader cache and bridge cache are intentionally separate.

- `%LOCALAPPDATA%/MecchaCamouflage/bridge-loaders/<loader-hash>/bridge-loader.dll`
- `%LOCALAPPDATA%/MecchaCamouflage/bridge-loaders/<loader-hash>/runtime-injector.exe`
- `%LOCALAPPDATA%/MecchaCamouflage/versions/<version>/runtime/bin/bridge-<bridge-hash>/runtime-bridge-<bridge-hash>-<port>.dll`

Do not include the app version or bridge hash in the loader path. Native loader
builds should use reproducible linker output so no-op rebuilds keep the same
loader hash. During development and reverse engineering, bridge-only rebuilds
must be switchable through the already loaded stable loader.

If the loader DLL path differs but the named pipe protocol responds, C# may use
that already loaded compatible loader to switch the bridge. If the pipe does not
respond and re-running that loaded loader's `RemoteMain` also fails, game restart
is required.

## Loader Control Channel

The loader exposes a local named pipe:

```text
\\.\pipe\MecchaCamouflage.Loader.<pid>.v1
```

The pipe is for lifecycle only. Once the bridge is `RunningListening`, C# uses
the existing bridge TCP IPC for `ping`, `paint_full_route`, `cancel_paint`,
`shutdown`, and research commands.

Messages are length-prefixed UTF-8 JSON:

```text
uint32 little-endian byteLength
UTF-8 JSON payload
```

Required commands:

- `hello`
- `status`
- `loadAndStartBridge`
- `stopBridge`
- `unloadBridge`
- `switchBridge`

Every response currently includes:

- `ok`
- `result`
- `restartRequired`
- `loaderState`
- `bridgeState`
- `generation`
- `tcpPort`
- `lastError`

The loader also writes a status sidecar for diagnostics and crash/postmortem
inspection. Sidecars are not the command plane.

## Loader Config

`McLoader_RemoteMain` receives a UTF-16 path to a JSON config file.

The config file contains:

```json
{
  "protocol": 1,
  "gamePid": 1234,
  "pipeName": "\\\\.\\pipe\\MecchaCamouflage.Loader.1234.v1",
  "statusPath": "C:\\...\\loader-status.json",
  "path": "C:\\...\\runtime-bridge.dll",
  "sha256": "...",
  "buildId": "...",
  "port": 47800,
  "progressPath": "C:\\...\\bridge.progress.json",
  "runtimeDir": "C:\\...",
  "logDir": "C:\\..."
}
```

`RemoteMain` is idempotent for an already-running matching bridge. Re-running it
against an initialized loader refreshes config and starts or switches the bridge
only when the current bridge state is safe.

## Bridge ABI

The bridge exposes one named C ABI entry:

```cpp
extern "C" __declspec(dllexport)
McResult WINAPI McBridge_GetApi(
    uint32_t loaderAbiMajor,
    uint32_t loaderAbiMinor,
    McBridgeApi* outApi);
```

ABI rules:

- `extern "C"` named exports only.
- `WINAPI` calling convention.
- no C++ exceptions across ABI.
- no STL types across ABI.
- no heap ownership transfer across modules.
- all structs start with `size`.
- callers zero-initialize structs.
- callees ignore unknown trailing fields.
- returned strings use fixed caller-owned buffers.

Required bridge API:

- `Create`
- `Start`
- `RequestStop`
- `JoinStop`
- `GetStatus`
- `Destroy`

`LoadLibraryExW` only maps the bridge. The bridge must not start listener,
hooks, workers, or UE work from `DllMain`.

## Lifecycle States

Loader states:

- `Uninitialized`
- `Ready`
- `BridgeLoading`
- `BridgeLoaded`
- `BridgeStarting`
- `BridgeRunning`
- `BridgeStopping`
- `BridgeStopped`
- `BridgeUnloadable`
- `BridgeFailed`
- `RestartRequired`

Bridge states:

- `Created`
- `Starting`
- `RunningNotListening`
- `RunningListening`
- `Stopping`
- `Stopped`
- `Unloadable`
- `Failed`

## Unload Blockers

The bridge may be unloaded only when all blockers are clear:

- listener
- active clients
- worker threads
- hooks
- active hook callbacks
- active UE/`ProcessEvent` calls
- active preview state
- active paint queue
- unknown bridge-owned references

If a blocker remains, the loader returns `restartRequired: true` and does not
call `FreeLibrary`.

## Stop Order

1. Set bridge state to `Stopping`.
2. Reject new commands.
3. Stop TCP listener ingress.
4. Close or drain active clients.
5. Cancel queued paint work.
6. Block unload if preview snapshot state is still active.
7. Drain active UE calls and hook callbacks.
8. Uninstall hooks.
9. Join bridge-owned worker threads.
10. Release sockets and handles.
11. Report `Unloadable` only if no blockers remain.

Never use `TerminateThread`.

## Failure Classes

- `LoaderInjectionFailed`
- `LoaderLoadedNoStatus`
- `LoaderAbiIncompatible`
- `BridgeLoadFailed`
- `BridgeAbiIncompatible`
- `BridgeLoadedNotListening`
- `BridgeStartFailed`
- `BridgeShutdownFailed`
- `BridgeUnloadFailed`
- `LegacyDirectBridgePresent`
- `DuplicateBridgeModules`

These must map to short user-facing messages and detailed diagnostic metadata.

## Implementation Milestones

### Milestone 1: Loader Shell

- Build and package `bridge-loader.dll`.
- Inject loader instead of direct bridge.
- Start loader from `McLoader_RemoteMain(configPath)`.
- Expose named pipe and status sidecar.
- C# detects loader states.

### Milestone 2: Bridge Load/Start

- Add bridge C ABI.
- Move bridge startup out of `DllMain` into `Start`.
- Loader loads bridge, resolves API, and starts bridge.
- C# waits for `RunningListening`, then uses existing bridge TCP IPC.

### Milestone 3: Graceful Stop

- Add bridge lifecycle state.
- Stop listener, clients, queues, workers, hooks.
- Report unload blockers.
- Refuse unload when blockers remain.

### Milestone 4: Conditional Reload

- Loader unloads only when bridge reports `Unloadable`.
- Loader verifies module removal after `FreeLibrary`.
- `switchBridge` works only in known-safe idle states.
- Unsafe cases return `restartRequired`.

## Release Rule

Reload is allowed only when proven safe. Restart is required when safety cannot
be proven.
