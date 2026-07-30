# Phase 10 ESP Progress

This checkpoint establishes the project-owned, graphics-API-independent ESP
frame boundary. It does not claim that the production UE4SS capture or UCanvas
draw adapters exist, and it does not satisfy the live architecture gate.

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
- selecting and validating game-specific skeleton topology; and
- translating the resulting line and text values to UCanvas calls on the same
  validated frame.

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

The secret-free suite passes all 48 Linux tests and all 56 Windows x64 Release
tests. The Windows build retains only the known WSL UNC/case-sensitive
incremental-build `MSB8064` warnings; compilation still uses `/W4 /WX`.

## Remaining work

- Implement the production UE4SS capture adapter and validated weak-handle
  invalidation.
- Implement the production UCanvas line/text draw adapter.
- Add topology/profile selection owned by the runtime adapter.
- Exercise lobby, match, travel, HUD replacement, freecam, spectator, role,
  avatar, and unload transitions in the live game.
- Add complete production-adapter, invalidation, stress, and forbidden-hook
  evidence before closing Phase 10.
