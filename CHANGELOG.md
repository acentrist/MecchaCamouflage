# Changelog

## Unreleased — game v2.9.0 support (build 24280866)

### Fixed

- Packed multiplayer paint stopped with "Packed multiplayer paint source id is
  unavailable" after the game updated to v2.9.0.
  `RuntimePaintableComponentPackedSourceIdOffset` in
  `src/native/bridge/bridge.cpp` updated from `0x2A8` to `0x2E0`.
  Cause: v2.9.0 grew `FPaintTextureOptions` from `0x54` to `0x70` bytes and
  added MaterialProperties/Emissive parameter-name and render-target fields to
  `URuntimePaintableComponent`, shifting the private member section by `0x38`.
  The new offset was derived from the v2.9.0 Dumper-7 SDK dump by applying the
  same relative position inside the private padding that follows
  `TargetMeshComponent`.

### Verification

- Full `scripts/build.ps1` run: all C# tests pass, native transform validation
  passes, `runtime-bridge.dll` and `runtime-injector.exe` compile with a clean
  native dependency allow-list, and the single-file publish produces
  `.build/bin/meccha-camouflage.exe`.
- Live multiplayer revalidation per
  `docs/runtime-maintenance.md` ("Game-update revalidation") still needs to be
  performed against build 24280866 before release.

## v2.8.0 game support (prior)

- Updated offsets for game version 2.8.0.
- Fixed error on laying down / star pose.
- Added option to disable brush 1.
