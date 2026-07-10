# Local Image Auto Paint and Safe Capture Bypass

## Research conclusion

The supported path is WebView UI -> `MainForm` command dispatch -> `HostSession` settings and paint lifecycle -> `BridgePayloadBuilder` -> injected native bridge -> mesh-first planner -> packed replication. The loader deliberately excludes stealth and anti-cheat bypass behavior. Therefore this feature implements a **capture-source bypass only**: it replaces live camera color capture with a validated local image while retaining process, loader, mesh-safety, cancellation, pacing, and replication checks.

## Flow

1. The WinForms host opens PNG/JPEG/BMP with `OpenFileDialog`.
2. `System.Drawing` validates dimensions/file size, normalizes alpha/compression, downsizes to at most 2048 px, and writes a content-addressed 24-bit BMP under the version-local `local-sources` cache.
3. Settings persist `UseLocalImageSource`, normalized `LocalImagePath`, and `BypassLiveCapture`.
4. Payload tuning sends `local_image_enabled`, `local_image_path`, and `bypass_live_capture`.
5. Native code reads at most 32 MB, accepts only uncompressed 24/32-bit BMP up to 4096 px, normalizes bottom-up/top-down rows, and bilinearly samples the image by mesh UV.
6. With capture bypass enabled, live SceneCapture is skipped. The existing planner, unsafe-coordinate guards, stroke generation, adaptive packed batching, cancellation, and replication remain unchanged.

## Security boundary

This does not hide injection, evade anti-cheat, disable authorization, skip replication pressure, or remove planner safety checks. The word “bypass” refers only to bypassing the live camera color source.

## Tests

`src/native/tests/quality_algorithms_test.cpp` validates deterministic BMP decoding and UV sampling in the same native build step as the quality algorithms. C# tests validate payload emission and settings round-trip. The normal build still compiles the complete native bridge with MSVC.
