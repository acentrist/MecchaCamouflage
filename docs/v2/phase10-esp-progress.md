# Phase 10 ESP Progress

This checkpoint establishes the project-owned, graphics-API-independent ESP
frame boundary, the production UCanvas line/text draw route, and an
evidence-backed production UE4SS capture route through camera and capsule
geometry. Skeletal-pose capture, projection calibration, and the live
architecture gate remain open, so Phase 10 is not complete.

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
The production runtime adapter applies that policy to weak Unreal handles; the
core never caches UObjects.

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

- resolving the remaining skeletal-mesh and projection-calibration contracts;
- copying a coherent skeletal pose on the game thread; and
- selecting and validating game-specific skeleton topology.

## Production target capture route

`UnrealRuntimeAdapter` now implements the non-skeletal capture side of
`EspGameRuntimePort`. Capture is accepted only on the UE4SS game thread and
inside the exact active HUD frame. World, controller, HUD, Canvas, viewport,
and their opaque weak identities and ownership relationships are revalidated
before capture; the same frame is checked again before copied values are
returned.

The current-build reflection inventory fixes the capture surface to:

- the exact cLeon GameState, PlayerState, survivor-character,
  hunter-character, and spectate-pawn classes;
- `World.GameState`, `GameStateBase.PlayerArray`,
  `Controller.PlayerState`, `PlayerState.PawnPrivate`, `Pawn.PlayerState`,
  `Character.CapsuleComponent`, and
  `PlayerController.PlayerCameraManager`;
- the replicated `LiveSurvivors_PlayerState` and `HuntersPlayerState`
  cLeon arrays and the exact inherited `CustomPlayerName` string; and
- exact reflected camera, SceneComponent transform, and scaled capsule
  UFunctions with double-precision UE5 Vector/Rotator return layouts.

Class inheritance, property owner, property kind, element class, array
dimension, container size, function owner, parameter direction, offsets, and
sizes are validated before access. Roster arrays are bounded to 64 live,
unique objects and must be subsets of `PlayerArray` with no role overlap.
Cross-world values, stale weak identities, ambiguous role avatars, invalid
camera values, malformed strings, and oversized or non-finite geometry fail
closed.

Camera location, rotation, FOV, viewport aspect, and unit projection scales
become one copied `EspView`. Each active character contributes its capsule
origin plus 18 deterministic world-space samples derived from the reflected
component transform and scaled radius/half-height. This is sufficient for the
box, name, distance, and snapline paths; skeleton output remains absent until
the reviewed mesh topology and pose contract is implemented. The horizontal
FOV/aspect choice and unit projection scales are explicit provisional capture
values, not live calibration evidence.

Role-avatar fallback retains only `FWeakObjectPtr` values across frames. The
directory is scoped to World and HUD identities, verifies PlayerState
membership, object lifetime, world, role class, and `Pawn.PlayerState` on every
reuse, and refreshes immediately for a new PlayerState, role change, expired
successful binding, travel, or HUD replacement. A verified negative lookup is
rate-limited to one bounded UObject scan per second rather than rescanning
every frame. The cache is cleared on detach.

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

The exported composition root remains inert, so this partial production port
cannot silently activate ESP runtime access before the remaining gates pass.

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

`esp_capture_codec_test` proves exact UE5 Vector/Rotator/float return ABI
contracts, bounded camera decoding, rotated 18-point capsule sampling, invalid
value refusal, and production weak-directory refresh timing. The production
adapter itself compiles with warnings-as-errors in the pinned UE4SS graph.

All 86 registered secret-free tests pass in the normal Linux graph and in a
fresh ASan/UBSan graph. At project commit `6e4e367`, all 104 Windows tests pass
after a complete MSVC x64 `Game__Shipping__Win64` build, including the
production capture codec and reflection contracts.

The full-build verifier binds that commit to canonical UE4SS source commit
`6c26f038751b3d96059d4a9148f5d093012d55ad`, verifies the source stage after
the build, and proves that `main.dll`, `UE4SS.dll`, and `dwmapi.dll` are x64
PE binaries built with the configured MSVC 19.44.35228.0 toolchain.
`main.dll` exports only `start_mod` and `uninstall_mod` and imports the UE4SS
DLL produced by the same graph. This is build and adapter evidence, not a live
ESP capture or draw pass.

## Remaining work

- Add exact aspect-constraint/projection calibration evidence and production
  capture.
- Add skeletal pose capture and topology/profile selection owned by the
  runtime adapter.
- Exercise lobby, match, travel, HUD replacement, freecam, spectator, role,
  avatar, and unload transitions in the live game.
- Add complete production-adapter, invalidation, stress, and forbidden-hook
  evidence before closing Phase 10.
