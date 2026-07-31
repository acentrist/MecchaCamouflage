# Phase 10 ESP Progress

This checkpoint establishes the project-owned, graphics-API-independent ESP
frame boundary and the production UCanvas line/text draw route. It does not
claim that production UE4SS target capture exists, and it does not satisfy the
live architecture gate.

## Pure frame model

`meccha_core` now owns bounded values for:

- the current view and viewport;
- opaque player and avatar identities;
- roster and current-pawn roles;
- UTF-8 display names;
- origin, capsule samples, skeleton poses, and indexed skeleton edges;
- line primitives for boxes, skeletons, and snaplines; and
- text primitives for names and distances.

The core has no UE4SS, Unreal, Win32, graphics, launcher, or UI dependency.
Captured values are copied before projection and remain independent from
UObject lifetimes.

Hard limits cover targets, display-name bytes, bones, skeleton edges, line
primitives, and text primitives. Invalid viewport, view, settings, non-finite
geometry, and resource-limit inputs fail closed or produce bounded per-frame
diagnostics as appropriate.

## Filtering, projection, and primitives

The pure builder now:

- excludes spectators before geometry processing;
- resolves role changes from the current pawn without trusting a stale roster
  role;
- applies all, hider, and hunter scope filtering;
- selects the configured hider or hunter color;
- projects with the captured field of view, aspect constraint, aspect ratio,
  and calibrated projection scale;
- constructs capsule or valid pose bounds;
- falls back from invalid skeleton topology to valid capsule geometry;
- clips line segments to the viewport;
- directs behind-camera snaplines to the correct viewport edge; and
- emits independently controlled boxes, skeletons, names, distances, and
  snaplines.

The avatar-cache policy is also a pure contract. It defines refresh conditions
and requires the world, player presence, object lifetime, verification state,
and resolved role to remain compatible before a cached binding can be reused.
The production runtime adapter must apply that policy to its weak Unreal
handles; the core never caches UObjects.

## Game-thread application boundary

`EspFrameCoordinator` accepts one exact `HudFrameIdentity`, asks the runtime
port for a copied target frame, rejects a stale identity before drawing, builds
the immutable primitive frame, and returns it to the runtime port for
frame-scoped drawing. Capture, frame construction, draw, port exceptions, and
generation overflow retain distinct failure kinds.

`ApplicationRoot` advances this coordinator only from the validated HUD frame
callback. ESP capture and draw continue while the product panel is closed.
The typed ESP toggle stops capture and drawing, while immutable application
snapshots publish the ESP phase, generation, primitive counts, bounded
diagnostics, and failure kind. A repeated failed state does not append one
diagnostic every frame.

The runtime port deliberately exposes project-owned values only. The
production adapter remains responsible for:

- resolving live UE4SS world, controller, player-state, pawn, HUD, Canvas,
  capsule, and skeletal-mesh contracts;
- rebuilding bindings after travel, HUD replacement, role changes, avatar
  replacement, freecam, and spectator transitions;
- copying coherent target data on the game thread;
- selecting and validating game-specific skeleton topology.

## Production Canvas draw route

`UnrealRuntimeAdapter` implements the draw side of `EspGameRuntimePort`. It
requires the UE4SS game thread, the exact active `HudFrameIdentity`, and a
positive viewport before accepting a primitive frame. A stale World,
controller, HUD, or Canvas identity fails closed under the typed `EspFrame`
runtime contract.

`encode_esp_canvas_frame` converts the immutable ESP line and text values into
one bounded `CanvasFrame`. It preserves line geometry, RGB color, thickness,
UTF-8 text, and anchors; supplies opaque alpha and the fixed text scale; and
uses the existing Canvas builder for finite geometry, clipping, UTF-8, and
resource validation. The adapter then delegates the complete frame to the
single production Canvas renderer, whose validated `K2_DrawLine` and
`K2_DrawText` contracts are already owned by `UnrealRuntimeAdapter`.

The capture side deliberately returns a typed fail-closed `EspFrame` contract
failure. No GameState roster, role, pawn, camera, pose, or skeleton property ABI
is guessed before current-build reflection and live evidence are frozen. The
exported composition root also remains inert, so this partial port cannot
silently activate ESP runtime access.

No DXGI, D3D11, D3D12, Present, ProcessEvent-vtable, or MinHook renderer was
introduced.

## Evidence

`esp_frame_test` covers projection, viewport clipping, scopes, spectator
exclusion, role colors, role changes, cache policy, capsule boxes, skeletons,
UTF-8 name/distance labels, behind-camera snaplines, invalid topology fallback,
non-finite geometry, and resource bounds.

`esp_frame_coordinator_test` covers disabled no-touch behavior, exact-frame
capture/build/draw, stale-frame refusal, typed runtime draw failure, and
exception containment.

`application_root_image_paint_test` additionally proves that the composition
root renders ESP while the panel is closed, keeps it independent across panel
open/close transitions, stops runtime access when disabled, and publishes the
immutable frame result.

`esp_canvas_frame_test` proves that a representative skeleton line and
Japanese UTF-8 name become one Canvas frame without changing geometry, color,
thickness, text, anchor, alpha, or scale.

All 85 registered secret-free tests pass in the normal Linux graph and in a
fresh ASan/UBSan graph. At project commit `319d6cf`, all 103 Windows tests pass
after a complete MSVC x64 `Game__Shipping__Win64` build, including the new ESP
Canvas test.

The full-build verifier binds that commit to canonical UE4SS source commit
`6c26f038751b3d96059d4a9148f5d093012d55ad`, verifies the source stage after
the build, and proves that `main.dll`, `UE4SS.dll`, and `dwmapi.dll` are x64
PE binaries built with the configured MSVC 19.44.35228.0 toolchain.
`main.dll` exports only `start_mod` and `uninstall_mod` and imports the
UE4SS DLL produced by the same graph. This is build evidence, not a live ESP
capture or draw pass.

## Remaining work

- Implement the production UE4SS capture adapter and validated weak-handle
  invalidation.
- Add topology/profile selection owned by the runtime adapter.
- Exercise lobby, match, travel, HUD replacement, freecam, spectator, role,
  avatar, and unload transitions in the live game.
- Add complete production-adapter, invalidation, stress, and forbidden-hook
  evidence before closing Phase 10.
