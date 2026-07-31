# Phase 5 UCanvas Feasibility Progress

## Current status

The project-owned, secret-free Canvas command boundary, responsive layout,
pointer interaction frame, and exact input-lease state machine are
implemented. They do not prove that the pinned UE4SS/runtime can register the
required live HUD callback or that the game's UCanvas and input objects
satisfy the complete product interaction contract. Phase 5 therefore remains
open and the approved architecture stop gate is unchanged.

## Canvas frame boundary

`meccha_ui` owns a platform-independent immediate-frame protocol. It contains
only project values:

- finite viewport dimensions and DPI scale;
- points, rectangles, normalized UV rectangles, and RGBA colors;
- line, filled-box, strict UTF-8 text, and opaque texture-handle primitives;
- the exact active clip rectangle carried by every emitted primitive; and
- a hard-bounded immutable frame returned to the future UCanvas adapter.

`CanvasFrameBuilder` starts with the viewport clip and supports at most 16
nested clips. It intersects nested rectangles, clips crossing lines, clips
filled boxes, and adjusts both destination geometry and source UVs when a
texture is partially visible. Text is admitted only when its anchor is inside
the active clip; the primitive retains that clip so the production adapter
must also clip glyph output. The builder rejects:

- non-finite or out-of-contract viewports, points, rectangles, thicknesses,
  scales, and UVs;
- zero/unknown texture handles;
- empty, invalid UTF-8, or oversized text;
- more than 8,192 primitives or 1 MiB of text per frame; and
- clip overflow, underflow, or an unbalanced final frame.

An out-of-clip primitive is a successful no-op. A rejected primitive never
partially changes the frame. Texture handles are project-owned integers; no
UE4SS, UObject, UCanvas, D3D, DXGI, or custom-render type crosses this
boundary.

## Responsive layout and pointer interaction

`build_panel_layout` validates the Canvas viewport, platform-provided safe-area
insets, and the product's `[0.75, 2.0]` user-scale contract. Its effective
pixel scale combines the user multiplier with DPI and caps that result only
when required to keep the minimum logical interaction surface inside the safe
area. The deterministic result contains:

- safe, panel, tab-strip, content, and status rectangles;
- five exact section-tab partitions for Paint, Image Paint, ESP, Settings, and
  Diagnostics;
- compact-tab state derived from the available panel width; and
- positive clipped content at the minimum accepted safe-area size.

`InteractionFrame` consumes one copied prior state and one frame of pointer
edges. Controls use nonzero project-owned IDs and explicit geometry plus clip
rectangles. A press is captured by at most one enabled control, a captured
release activates only when it remains inside the clipped control, and focus
changes only on successful activation or an unclaimed background press.
Same-frame press/release is deterministic. Closing the panel releases capture
and focus. Duplicate/zero IDs, invalid geometry, non-finite input, and more
than 2,048 controls fail the complete interaction frame; the caller never
receives partially committed state.

The same interaction frame collects the enabled focusable IDs actually
submitted during the frame. Forward/reverse navigation wraps deterministically,
activation reaches only the focused control, cancel clears focus, and
conflicting navigation edges fail the frame. Pointer focus and keyboard focus
share one state instead of maintaining competing input paths.

`update_scroll_container` is a pure retained scroll transition. It clamps
existing state whenever content shrinks, consumes finite wheel input only
inside the intersection of the container viewport and active clip, and
returns the exact visible clip, content origin, maximum offset, and change
state. Fully clipped containers cannot consume input.

`WidgetPainter` composes that interaction protocol with the Canvas builder. It
provides buttons, toggles, continuous bounded sliders, and an RGBA-preserving
three-channel RGB control. Every control:

- has one explicit ID and one explicit interaction clip;
- renders only through filled-box, line, and strict UTF-8 text primitives;
- distinguishes disabled, hovered, held, selected, and accent states;
- maps a captured slider pointer deterministically and clamps outside drags;
  and
- propagates Canvas, interaction, and validation failures so the caller can
  discard both frame-local builders rather than publish partial UI state.

The maximum Canvas text scale is eight, matching the largest validated
user-scale/DPI product combination. The same frame protocol rejects any larger
text request.

`TextEditState` owns a bounded single-line edit lease with an exact original
value and a cursor that is always on a UTF-8 byte boundary. It consumes at most
64 ordered events and 4 KiB of inserted bytes per frame. Insert, code-point
left/right, home/end, backspace, forward-delete, commit, and exact cancel
restore are supported. Invalid UTF-8, control characters, cursor splits,
payloads on non-insert events, terminal-event suffixes, and size/event
overflows fail without publishing an update.

The Canvas text-field control enters editing only through pointer or focused
keyboard activation, commits on focus loss/disable, clears focus after commit
or cancel, draws its value and bounded approximate caret through the Canvas
protocol, and preserves the text state outside runtime objects. The future
input adapter supplies only committed UTF-8 event sequences; OS/IME composition
state does not leak into the UI or application model.

## Image Paint editor boundary

`update_image_editor_interaction` retains the v1 editor behavior without
retaining DOM state. A bounded ordered pointer stream selects the topmost layer,
but checks all corner handles before any rectangle body. Move and four-corner
resize gestures capture the exact original placement. They emit one
normalized replacement value, preserve the retained 24-canonical-pixel minimum
size, commit only on release, and restore the exact captured placement on
cancel without overwriting unrelated current layer fields. The active gesture
is rejected if its asset is replaced or removed.

Layer ordering returns a copied ordered collection and the new selected index.
The crop session derives the Fit-shaped base crop from decoded source dimensions
and the normalized atlas placement, preserves its aspect at a bounded 1–4×
zoom, clamps movement inside the source, binds apply/restore to the exact asset,
and retains the exact original crop for cancellation. No editor transition
mutates a project, worker, texture, or UObject.

`draw_image_editor` is the portable two-layer vertical slice. It draws one
canonical-atlas texture, then an optional guide texture whose schema, body, and
complete frozen ImageReference profile identity must match, then layer
outlines and selected resize handles. The guide is therefore a separate Canvas
primitive after the atlas; it is never passed to the compositor, persisted
atlas, preset, preview planner, or Paint dispatcher. A failed draw unwinds both
clip scopes before returning. All three packaged profiles now produce
deterministic body/skeleton guide bitmaps, localized face names remain separate
Canvas text, and the project-owned game-thread coordinator manages opaque
guide, atlas, and source handle generations. Reflected Unreal texture
creation/release and its live lifetime evidence remain open work.

## Exact input lease

`InputLeaseController` owns the panel input transition while a future Unreal
adapter implements the narrow `InputLeasePort`. On the closed-to-open edge it
captures cursor visibility, look-input suppression, movement-input
suppression, and the current game/UI input mode exactly once, then asks the
adapter to apply the panel controls. Stable open/closed frames do not repeat
runtime mutations.

Closing or shutting down restores the complete captured value. A failed apply
immediately attempts rollback. If rollback or a later restore fails, the
captured value remains owned in the `Restoring` state so the next frame or
shutdown step can retry; it is never discarded as if restoration succeeded.
Port exceptions are contained and failure details are bounded.

The production port must run on the validated game-thread/HUD boundary. It
must show the cursor, suspend look and movement, and select the reviewed UI
input mode while held. These runtime effects and their behavior through
travel/controller replacement remain part of the live feasibility gate.

## Automated evidence

`ui_canvas` covers line/box/texture intersection, matching UV adjustment,
localized UTF-8 text, active clip retention, out-of-clip no-op behavior,
primitive limits without partial mutation, invalid geometry/text/viewport
rejection, and clip-balance refusal.

`ui_input_lease` covers exact state capture/restore, stable-frame idempotence,
apply rollback, failed rollback/restore retention and retry, invalid captured
state refusal, shutdown restoration, and exception containment.

`ui_layout` covers normal, constrained high-DPI, safe-area, compact-tab, and
ultrawide geometry plus invalid scale/inset refusal. `ui_interaction` covers
exclusive capture, clipped hit testing, inside/outside release, focus,
disabled controls, duplicate IDs, panel close, same-frame clicks, and invalid
pointer refusal.

`ui_scroll` covers inside/outside/clipped wheel routing, retained offset
clamping after content shrink, and invalid-state/content refusal. `ui_widgets`
covers primitive emission, button activation/focus, stable and activated
toggles, continuous slider mapping/capture, stable alpha-preserving color
controls, text-field Canvas integration, and duplicate-ID propagation.
`ui_interaction` also covers forward/reverse focus wrapping, focused-only
activation, cancel, and conflicting-key refusal. `ui_text_edit` covers ordered
multibyte editing, UTF-8 cursor boundaries, commit/cancel, byte/event limits,
single-line enforcement, and terminal-sequence refusal. `ui_canvas` verifies
the bounded high-DPI text-scale contract. `ui_image_editor` covers two-layer
topmost selection, handle priority, move/resize, retained minimum size, exact
cancel, reorder, source-aspect crop/zoom/move/apply/restore, stale-asset and
event bounds, exact guide identity and ordering, and clip cleanup after a
primitive-limit failure.

All eight tests run in the normal Linux graph, the mandatory ASan/UBSan graph,
and the Windows MSVC x64 Release graph.

## Immutable editor binding

The Canvas editor result now crosses the UI/application boundary through one
typed `MutateImageProject` command. Every mutation carries the exact project
ID and expected revision. Layer replacement and reordering additionally carry
the selected layer index and expected asset ID, so a stale selection cannot
modify a replaced or reordered layer.

`ImageEditorSession` retains the newest accepted immutable draft even while
decode/composition is active. This allows consecutive drag, resize, crop,
order, wrap/mirror, and project-settings changes to coalesce without depending
on an older ready atlas. A successful change advances the project revision
exactly once; stale revisions, invalid layer identities/ranges, invalid
settings, no-op edits, revision overflow, and concurrent persistence ownership
fail closed.

The immutable application snapshot exposes only bounded project metadata,
settings, and ordered layer values. Encoded source bytes and the canonical
atlas remain inside the editor owner. A layer mutation can change only
placement, crop, seam wrapping, and front/back mirroring; source identity,
codec, name, and byte length are copied from the owned project rather than
trusted from UI input.

`image_editor_session` covers consecutive mutations during active composition,
metadata isolation, revision/asset/range guards, order preservation into the
debounced draft, and terminal shutdown. `application_root_image_paint` proves
the typed command route and immutable document publication. The project-owned
HUD-frame coordinator additionally proves localized composition, exact
input-lease acquisition and render-failure rollback, render-before-action
ordering, ready-texture synchronization, and resource-free shutdown through a
fake runtime port. The normal Linux, ASan/UBSan, and Windows Release graphs pass
74, 74, and 92 tests respectively.

## Remaining feasibility work

- Bind the project-owned texture coordinator to reflected Unreal texture
  creation/release.
- Implement the validated UCanvas and Unreal input adapters only after the
  protected UE4SS graph compiles the exact interfaces.
- Prove localized font/text, texture creation/lifetime, clipping, mouse input,
  travel/HUD replacement, and teardown in the live UE 5.6 game.
