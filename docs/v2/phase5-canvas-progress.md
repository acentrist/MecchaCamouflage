# Phase 5 UCanvas Feasibility Progress

## Current status

The project-owned, secret-free Canvas command boundary and exact input-lease
state machine are implemented. They do not prove that the pinned UE4SS/runtime
can register the required live HUD callback or that the game's UCanvas and
input objects satisfy the complete product interaction contract. Phase 5
therefore remains open and the approved architecture stop gate is unchanged.

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

Both tests run in the normal Linux graph, the mandatory ASan/UBSan graph, and
the Windows MSVC x64 Release graph.

## Remaining feasibility work

- Implement responsive layout, hit testing, controls, editor gestures, body
  guides, and immutable command/snapshot binding above this protocol.
- Implement the validated UCanvas and Unreal input adapters only after the
  protected UE4SS graph compiles the exact interfaces.
- Prove localized font/text, texture creation/lifetime, clipping, mouse input,
  travel/HUD replacement, and teardown in the live UE 5.6 game.
