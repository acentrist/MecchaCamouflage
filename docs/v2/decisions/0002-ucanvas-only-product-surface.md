# Decision 0002: UCanvas-Only Product Surface

Status: Accepted

## Context

v1 combined an external WebView/WinForms UI with a custom graphics/injection
stack. The v2 plan requires a smaller in-process product surface while keeping
Paint, Image Paint, ESP, settings, diagnostics, localization, and editing.

## Decision

Render and interact only through the validated frame-scoped Unreal HUD/UCanvas
boundary. Keep all layout, widgets, interaction, text editing, scrolling, and
primitive encoding in project-owned portable code. The runtime adapter alone
translates a complete validated frame to reflected Canvas calls and owns
generation-checked transient textures, game-font resolution, and the packaged
fallback atlas.

Use the pinned UE4SS input API plus a project-owned bounded queue for hotkeys,
navigation, and text translation. Do not install WndProc, DXGI, D3D, Present,
MinHook, ProcessEvent-vtable, external-window, or UE4SS debug-tab fallbacks.

## Consequences

- Live Canvas usability and exact input restoration are architecture gates.
- A failed gate requires explicit architecture review; it does not authorize
  restoration of a forbidden v1 path.
- ESP remains a HUD-frame consumer and is independent of panel visibility.

