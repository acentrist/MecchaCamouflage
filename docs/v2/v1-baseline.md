# MecchaCamouflage v1.6.7 Baseline

This document records the rewrite baseline required by Phase 0 of
[`PLAN.md`](../../PLAN.md). It describes the accepted v1 source at commit
`caac4da` and tag `v1.6.7`; it is not a v2 design.

## Repository identity

- Accepted v1 commit: `caac4da`
- Accepted v1 tag: `v1.6.7`
- Maintenance branch start: `release/v1.x`
- Rewrite branch start: `rewrite/ue4ss-v2`
- Existing submodules:
  - `third_party/CUE4Parse` at
    `c43d6707cbde0305e2970d7c527e2ccd26f393c7`
  - `third_party/UnrealMappingsDumper` at
    `4da8c66c23ce66ef86d75962d66b12cf39185092`
  - `third_party/minhook` at
    `c3fcafdc10146beb5919319d0683e44e3c30d537`

## Deterministic build baseline

Command:

```text
make build VERSION=v1.6.7-baseline
```

Result on 2026-07-30:

- PASS: C# Release build
- PASS: all 175 registered C# behavioral/source-contract cases
- PASS: native `transform-validation-test.exe`
- PASS: native bridge build
- PASS: native injector build
- PASS: Windows x64 self-contained single-EXE publish
- Build warnings: 0
- Build errors: 0

The baseline is reproducible through the repository's unmodified
`scripts/build.ps1` from native Windows PowerShell.

## Baseline artifact inventory

| Role | Path | Size | SHA-256 |
| --- | --- | ---: | --- |
| Distributed EXE | `.build/bin/meccha-camouflage.exe` | 56,974,302 bytes | `23031a3d52fa7e7471b9e3bd07ec32935df2f4a7b53e1e8834d92af21b395517` |
| Injected bridge | `.build/obj/v1.6.7-baseline/package-native/runtime-bridge.dll` | 2,649,088 bytes | `085f844180b9d899229662410eff6de6aa968a515b2a47827bb9cde4e74f0cda` |
| Custom injector | `.build/obj/v1.6.7-baseline/package-native/runtime-injector.exe` | 287,232 bytes | `383169a1bdaaceb5396a1b5550dcd9fdae4d81d60c2f93fe47b362b5cda3f6ba` |

`.build/` is ignored and is evidence local to this baseline run. Release v2
must not reproduce the bridge or injector artifacts.

## Source and resource inventory

- C# projects: 5
- C# source files: 31
- Native maintained `.cpp` files: 3
- Native maintained headers: 3
- `src/native/bridge/bridge.cpp`: 36,554 lines
- `src/native/include/runtime_contract.hpp`: 3,730 lines
- `src/csharp/MecchaCamouflage.Tests/Program.cs`: 5,637 lines
- Mesh profile files: 6
- Mesh profile bytes: 19,636,567
- Packaged font files: 8 plus OFL license
- Font resource bytes: 488,495
- Supported UI locales: 16
- Body profiles: round, cube, and fukuyoka

## v1 runtime map

The v1 process is split across:

1. `MecchaCamouflage.WebHost`: WinForms, WebView2, JavaScript command routing,
   hotkeys, dialogs, and external UI.
2. `MecchaCamouflage.Controller`: session orchestration, runtime attachment,
   TCP/JSON bridge client, image state, progress, and packaged assets.
3. `MecchaCamouflage.Core`: settings, models, localization, presets, projects,
   atlas helpers, and projection helpers.
4. `runtime-injector.exe`: process selection support and remote `LoadLibraryW`.
5. `runtime-bridge.dll`: custom reflection, ProcessEvent access, Paint,
   Image Paint, ESP capture/rendering, TCP/JSON server, DXGI/D3D Present hooks,
   and lifecycle.

The behavioral contracts are retained through Phase 1 traceability. These v1
transport, UI, injection, and rendering boundaries are deletion targets only
after their v2 replacements pass the required evidence.

## CI and release baseline

- `.github/workflows/ci.yml` uses `windows-2022`, recursive submodules, MSVC
  x64, .NET 10, `scripts/build.ps1`, native artifact checks, and a package dry
  run.
- `.github/workflows/release.yml` builds on Windows and publishes one
  `meccha-camouflage-*.exe`.
- The v1 CI explicitly requires `runtime-bridge.dll` and
  `runtime-injector.exe`; Phase 2/12 must replace those assertions rather than
  carry them forward.

## Baseline limitations

- No live-game behavior was claimed by this deterministic baseline.
- Windows 10/11, UAC, Steam, visual UI, travel, freecam, spectator, and
  multiplayer checks remain maintainer/live evidence.
- Existing tests include source-text assertions tied to v1 architecture. Phase
  1 must classify them and preserve only the underlying behavior.
