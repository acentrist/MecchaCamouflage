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

## Reflected UCanvas boundary

The product UI now separates frame input capture from Canvas rendering through
`ProductUiFrameCapturePort` and `ProductUiCanvasRenderPort`. A production
adapter can therefore prove one responsibility without claiming that mouse,
keyboard, IME, cursor, or input-mode capture is implemented. The coordinator
still requires both ports and preserves render-failure input rollback.

The project-owned runtime contract layer freezes the UE 5.6 x64 reflected
records and call ABIs used by the renderer:

| Function | Parameter bytes | Required reflected properties |
| --- | ---: | --- |
| `Canvas.K2_DrawLine` | `0x38` | two `Vector2D` inputs, thickness, `LinearColor` |
| `Canvas.K2_DrawTexture` | `0x70` | texture, destination/UV vectors, color, blend, rotation, pivot |
| `Canvas.K2_DrawText` | `0x88` | font, `FString`, position/scale/color, shadow, centering, outline |

`Vector2D`, `LinearColor`, every function owner, property name, kind, type,
offset, size, array dimension, direction, and total record size are validated
before the adapter stores the contracts. The reflection descriptor recognizes
`FStrProperty` explicitly; an unknown string-like property does not pass as
text. The dependency-free encoders validate finite geometry, bounded line/text
scales, normalized UVs, nonempty terminated UTF-16 input, sRGB-to-linear color,
alpha, translucent blending, and the exact x64 parameter layout. The text
boundary converts at most 4 KiB of strict UTF-8 into owned, explicitly
terminated UTF-16, including correct surrogate pairs; malformed UTF-8,
embedded NUL, unterminated storage, and oversized input fail before dispatch.

`UnrealRuntimeAdapter` now consumes only the render port. It records the exact
`ReceiveDrawHUD` viewport values with the generation-scoped World/controller/
HUD/Canvas identity, preflights the complete bounded frame, and dispatches
validated lines, filled boxes, and game-font text through `K2_DrawLine`, a
null-texture white `K2_DrawTexture` tile, and `K2_DrawText`. Opt-in CUE4Parse
inventory of the exact cooked game files identified
`Chameleon/Content/UI/NotoFonts/MainFont.uasset` as a `Font` export; the
corresponding Unreal path is frozen as
`/Game/UI/NotoFonts/MainFont.MainFont`. Contract resolution loads that soft
path only on the game thread, requires the exact `Font` class and path, stores
a weak generation identity, and revalidates it before admitting any text.
Every text call owns its UTF-16 buffer through the complete dispatch.

Any stale identity, viewport mismatch, invalid primitive, stale/wrong-class
font, or texture without adapter-owned generation tracking rejects the
complete frame before the first `ProcessEvent`. Missing-glyph fallback and
glyph-level clipping are not claimed: the packaged fallback atlas still
requires the production texture registry. There is no default-font guess,
pointer-shaped texture handle, external window, or Present-hook fallback.

The runtime contract layer additionally freezes
`KismetRenderingLibrary.ImportBufferAsTexture2D` at `0x20` parameter bytes:
one World context object, one `TArray<Byte>` input, and one `Texture2D` return
value. Core now produces a bounded deterministic RGBA8 PNG using canonical
filter-zero scanlines and stored DEFLATE blocks, validates chunk CRCs, Adler-32,
dimensions, resource limits, and cancellation, and exposes the encoded bytes
without a UObject.

The composition worker publishes canonical PNGs for the atlas and decoded
source pixels; profile guide generation publishes the same bounded encoding.
The game-thread coordinator validates those bytes and dimensions before
requesting a runtime texture. `UnrealRuntimeAdapter` validates the exact
Kismet library CDO/function/return class, imports against the active World,
rejects an already-rooted or wrong-class result, roots only the returned
transient `Texture2D`, and publishes one monotonic nonzero project handle.
The bounded registry stores a weak UObject generation plus the exact object
identity. Every texture primitive must resolve that registered, live, rooted
generation before the complete frame can dispatch. Raw UObject addresses
never become Canvas handles.

Partial project/guide replacement retires every newly created handle without
disturbing the last complete frame assets. Release clears only a matching
project-rooted generation and retry state remains explicit. Normal callback
unregistration refuses to discard a nonempty registry; terminal emergency
teardown waits for in-flight callbacks, clears every still-live owned root on
the game thread, then drops runtime contracts. These compile and portable
lifetime tests do not yet prove that imported textures display or survive
travel in the live game.

The exact contract and PNG tests pass on Linux and Windows. The modified
adapter compiles and links under `/W4 /WX` in the pinned MSVC
`Game__Shipping__Win64` graph against the manifest-verified canonical UE4SS
source stage. The exact clean project checkout passes all 97 registered
Windows tests after building `UE4SS.dll`, `main.dll`, and the native launcher
from that graph. Post-build verification confirms that the source stage still
contains the pinned upstream commit plus only the approved project-owned Cargo
lock overlay. This remains compile and contract evidence only; live reflection
resolution and visible Canvas output are mandatory.

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
creation/release is now bound through the exact game-thread runtime port with
bounded rooting, generation validation, rollback, retry, and teardown cleanup.
Visible live-game output and lifetime across travel/HUD replacement remain
open work.

## Exact input lease

`InputLeaseController` owns the panel input transition through the narrow
`InputLeasePort`. On the closed-to-open edge it captures the exact
PlayerController generation, cursor visibility, look-input suppression, and
movement-input suppression once, then asks the adapter to apply the panel
controls. Unreal exposes no validated reflected getter for the current Slate
input mode, so the product never changes that mode and explicitly records the
`PreserveUnchanged` policy instead of inventing a value to restore.

Closing or shutting down restores the complete captured value. A failed apply
immediately attempts rollback. If rollback or a later restore fails, the
captured value remains owned in the `Restoring` state so the next frame or
shutdown step can retry; it is never discarded as if restoration succeeded.
Port exceptions are contained and failure details are bounded. While held,
each frame validates the current controller identity. Replacement restores the
old owner before capturing and applying to the new owner; a failed old-owner
restore blocks reacquisition and remains retryable.

`UnrealRuntimeAdapter` now implements that port on the validated game-thread
HUD boundary. It freezes the exact one-byte UE 5.6 reflection records for
`IsLookInputIgnored`, `IsMoveInputIgnored`, `SetIgnoreLookInput`, and
`SetIgnoreMoveInput`, validates their exact `Controller` owner, and validates
`PlayerController.bShowMouseCursor` as an in-container `FBoolProperty`.
Look/movement calls are issued only when the captured value was not already
ignored and are paired with exactly one release call. Cursor mutation is
tracked separately and restored to the captured bit value. Partial apply and
restore progress remains adapter-owned so retries do not repeat already
completed releases. Normal callback removal refuses a live input mutation;
the terminal emergency path makes a bounded game-thread restoration attempt
after admitted callbacks drain.

The reflection contracts, controller replacement state machine, rollback, and
retry paths pass portable Linux, ASan/UBSan, and targeted Windows MSVC Release
tests. The production adapter compiles and links under `/W4 /WX` against the
pinned manifest-verified UE4SS graph. Live reflection resolution and behavior
through travel, HUD/controller replacement, and unload remain mandatory.

## Automated evidence

`ui_canvas` covers line/box/texture intersection, matching UV adjustment,
localized UTF-8 text, active clip retention, out-of-clip no-op behavior,
primitive limits without partial mutation, invalid geometry/text/viewport
rejection, and clip-balance refusal.

`ui_input_lease` covers exact owner/state capture and restoration,
stable-frame owner validation, restore-before-rebind ordering, failed
owner-validation containment, apply rollback, failed rollback/restore
retention and retry, same-frame reacquisition after retry, invalid captured
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

The canonical PNG test additionally covers deterministic output, strict
metadata/CRC inspection, mismatched RGBA input, corruption, and cancellation;
the runtime contract test covers the exact texture-import ABI and owned
localized UTF-16 conversion. Composition/pipeline tests prove immutable PNG
publication, while the texture coordinator/frame tests cover complete
generation replacement, unchanged-revision reuse, partial-create rollback,
bounded retry, clear, and resource-free shutdown. The complete 80-test graph
passes in normal Linux and mandatory ASan/UBSan configurations. The complete
97-test Windows MSVC x64 Shipping graph passes from the same exact project
commit and immutable UE4SS source-stage identity.

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
fake runtime port. The current normal Linux and ASan/UBSan graphs pass all 80
registered tests. The current exact clean project checkpoint passes all 97
registered Windows x64 Shipping tests and links the production mod against the
same manifest-verified UE4SS source graph.

## Packaged fallback glyph atlas

The production adapter will prefer the game's localized UI font, but it no
longer depends on that font covering every shipped translation. A reviewed
1536×1440 RGBA atlas now contains the exact 957 drawable/spacing codepoints
required by all 16 catalogs, localized locale names, and the replacement
character. Each entry uses a fixed 48×48 cell, sorted codepoint index, and
bounded advance so the future UCanvas adapter can emit texture primitives
without loading a native font parser or introducing a graphics hook.

The atlas is generated from the exact Noto Sans CJK `Sans2.004` Regular OTC
commit and source hash recorded in `dependency-lock.md`. Its JSON manifest
binds source provenance, OFL hash, atlas hash, RGBA geometry, and every
codepoint/index. `fallback_glyph_atlas` rejects manifest, inventory, PNG hash,
format, geometry, license, and catalog drift. The runtime assembler reruns the
same verification before packaging. The eight v1 D-DIN files are removed from
the v2 payload because no v2 runtime path consumes them.

## Remaining feasibility work

- Resolve the game-localized font first and bind missing text through the
  fallback atlas in the production UCanvas adapter.
- Complete glyph clipping/fallback and the separate Unreal input adapter.
- Prove localized font/text, texture creation/lifetime, clipping, mouse input,
  travel/HUD replacement, and teardown in the live UE 5.6 game.
