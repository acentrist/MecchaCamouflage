<p align="center">
  <img src="assets/meccha-camouflage-banner.png" alt="Meccha Camouflage banner" width="100%" />
</p>

# Meccha Camouflage

Native Xenos-injected runtime for MECCHA CHAMELEON.

## Download

Download the latest `meccha-camouflage.exe` from GitHub Releases:

- https://github.com/acentrist/MecchaCamouflage/releases/latest

The EXE is self-contained and can be placed anywhere, including Downloads; it finds `PenguinHotel-Win64-Shipping.exe` by process name and injects the embedded bridge DLL from `%LOCALAPPDATA%\MecchaCamouflage\runtime\native\`.

## Use

1. Start MECCHA CHAMELEON.
2. Start `meccha-camouflage.exe`.
3. Wait for `sdk_probe` and `sdk_deep_probe` to complete.
4. Press `F10` in game.

Runtime diagnostics are written to:

```text
%LOCALAPPDATA%\MecchaCamouflage\runtime\events.jsonl
%LOCALAPPDATA%\MecchaCamouflage\runtime\last_status.json
%LOCALAPPDATA%\MecchaCamouflage\runtime\runtime.log
```

## Current route policy

The default F10 route is `texture_sync_strict_probe`.

- It imports the generated texture through the SDK and dispatches texture sync RPC candidates.
- It is the current fast multiplayer candidate, but remote peer verification is still required.
- `front_atlas_paint_stream` remains an explicit fallback for replicated paint API streaming, but it is slow.
- Material swap, synthetic UV placement, local-only import routes, and memory-scan fallback are forbidden.

Details are in `native/README.md`.

## Development

Windows/MSVC is the primary build environment. WSL can drive the PowerShell scripts when Windows tooling is installed.

Common commands:

```bash
make build
make copy-to-game GAME_ROOT='C:\Program Files (x86)\Steam\steamapps\common\MECCHA CHAMELEON'
make service
make probe
make apply RUNTIME_ARGS='--native-apply-mode front_atlas_paint_stream'
make package VERSION=0.1.0
```

Script equivalents remain available under `scripts/`, but the Makefile is the preferred entrypoint for local development.

Build output:

```text
.build/native/bin/meccha-camouflage.exe       # self-contained runtime EXE
.build/native/bin/meccha-xenos-bridge.dll     # embedded into the runtime EXE at build time
.build/native/bin/meccha-xenos-injector.exe   # development helper for Dumper7/tooling
.build/native/obj/                            # native object/resource files
```

Repository layout:

- `native/`: C++ controller, injected bridge, and SDK-backed paint runtime.
- `scripts/`: PowerShell build/package/tooling scripts.
- `dumper-sdk/`: managed Dumper7 SDK output for the target game build.
- `tools/Dumper-7/`: local Dumper-7 tool source.
