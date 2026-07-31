# Phase 11 Product UI Progress

Phase 11 is open. The portable Canvas protocol, layout, interaction, widget,
scroll, text-edit, Image Paint editor, input-lease, and typed editor-mutation
boundaries are implemented. The current milestones add the application-owned
hotkey and product-action boundaries plus a portable five-tab product-panel
shell, native file-selection boundary, latest-snapshot effect executor, and
v2-only preset import/activation path. The three exact body guides, portable
game-thread texture lifetime boundary, and complete project-owned HUD-frame
coordinator are also implemented and attached to `ApplicationRoot` through one
project-owned frame-extension contract. Production UCanvas rendering, Unreal
input registration/lease, callback composition, game-font/fallback selection,
and live verification remain open. The reflected Unreal texture port is
implemented but still requires live travel and teardown evidence.

## Ownership and dependency direction

The project-owned `meccha_ui` library remains dependent on `src/core` only. It
does not import application state, UE4SS, Unreal, Windows, or graphics APIs.
The new `InputCommandRouter` lives in `src/mod/application`, where it can read
an immutable `ApplicationSnapshot` and emit only the existing typed
`ApplicationCommand` variant.

The new `meccha_product_ui` target is the explicit composition boundary between
`meccha_ui` and `meccha_application`. This preserves the direction of the
primitive UI library while allowing the panel composer to consume immutable
`ProductUiModel` values and emit `ProductUiActionEnvelope` values. It does not
link UE4SS, Unreal, Windows UI, or a graphics API.

`ProductUiInputQueue` is the thread-safe boundary between registered input
callbacks and one HUD game-thread frame. It accepts only F1–F24, validated
navigation values, and bounded single-line UTF-8 text-edit events. Each
debounced function-key edge becomes one adjacent Pressed/Released pair because
the pinned UE4SS Win32 source emits a new callback only after observing the
physical release. At most 32 such edges can therefore become the router's
64-event frame limit. Text input retains the existing 64-event/4096-byte
bounds. Any overflow discards the complete pending frame; focus loss can
discard it explicitly; terminal stop clears all storage and makes later
callbacks inert.

`ProductUiFunctionKeyBinding` now owns one-shot F1–F24 registration through a
project-owned registrar interface. Every registered callback retains only the
thread-safe queue, and terminal stop makes already registered or partially
registered callbacks inert before derived mod members are destroyed. Duplicate
start, registration failure/exception, partial registration, callback order,
post-stop invocation, and callback lifetime beyond the binding object are
covered without UE4SS types in the portable target.

The remaining production input connection has one narrow responsibility:
start that binding only after the exported composition root owns the matching
HUD-frame queue consumer, translate callbacks into the queue, and drain it
through `ProductUiFrameCapturePort`. While a Settings capture is armed, the
coordinator delivers the next press only as
`ProductPanelInput::function_key_pressed`; otherwise the same immutable batch
goes through `InputCommandRouter`. The adapter does not choose feature
settings, project revisions, or command identities.

## Hotkey command contract

The router implements the locked default mapping:

- F9 toggles the in-game panel.
- F1/F2/F3/F4 start, preview, restore, and cancel Paint.
- F5/F6/F7/F8 start, preview, restore, and cancel Image Paint.

Every action follows the current validated F1–F24 mapping in
`ApplicationConfig`. Pressed physical keys remain held until an exact release
event or explicit input-loss reset, so operating-system/key-callback repeat
cannot enqueue duplicate work. Multiple real press/release transitions in one
frame preserve their order.

Paint Start and Preview copy the exact Paint settings from the immutable
snapshot. Image Paint Start and Preview copy the exact editor project ID and
revision; when no valid editor document exists, the press becomes one bounded
typed rejection and remains held until release. Restore and Cancel commands do
not invent readiness checks that belong to `ApplicationRoot`.

Input is capped at 64 events per frame. Invalid keys, event kinds, settings,
and a zero initial command ID fail transactionally without changing held-key
or command-sequence state. Successful command IDs are monotonic and the final
`uint64_t` ID is usable exactly once before terminal overflow. Shutdown clears
all held keys and permanently closes admission.

`input_command_router` covers defaults, ordered repeated press/release,
snapshot-bound settings and project identity, unavailable-project rejection,
held-key behavior across snapshot publication, complete remapping, duplicate
mapping refusal, invalid/event-limit refusal, exact command-ID exhaustion,
input-loss release, and terminal shutdown.

The current complete Linux and Linux ASan/UBSan graphs pass all 80 registered
tests. The last exact clean full Windows MSVC Release graph passes 97 tests
with `UE4SS.dll`, `main.dll`, and the launcher bound to the
manifest-verified immutable UE4SS source stage; the added key-binding target
also passes a targeted Windows MSVC Release build and execution. A new
complete clean Windows count is not claimed until that full graph is rerun.
Post-build verification confirms that the immutable stage still contains only
the pinned source plus the project-owned canonical Cargo lock overlay.

## Immutable product presentation

`ProductUiModel` is the bounded application-owned read boundary for the five
locked Paint, Image Paint, ESP, Settings, and Diagnostics sections. It is
derived from one immutable `ApplicationSnapshot` and contains no Unreal,
graphics, input, or mutable runtime reference.

The model exposes:

- Exact current Paint, Image Paint, ESP, and UI settings.
- Feature Start, Preview, Restore, and Cancel availability derived from
  runtime compatibility, queue admission, active job ownership, preview
  ownership, and exact editor project readiness.
- Separate Image Paint edit/load/save/rename/delete availability. Coalescible
  edits remain available during composition, while ready-only persistence and
  deletion operations fail closed.
- ESP enabled/frame state even while the product panel is closed.
- Exact job and preview identity, bounded progress fractions, elapsed time,
  ETA, and queue pressure.
- Command/runtime queue counts and utilization, compatibility status, and the
  newest 64 ordered diagnostics.

Invalid configuration, queue counts, progress arithmetic/pressure, unordered
diagnostic sequences, oversized diagnostic keys, and invalid diagnostic UTF-8
are rejected before a model is published. Closing the panel changes only the
published `ui_open` value; it does not erase or pause ESP, preview, job, or
queue state.

## Shared typed product actions

`InputCommandRouter` now owns one synchronized command-ID sequence for both
function-key events and Canvas-originated product actions. This prevents
hotkey and pointer/keyboard UI callbacks from issuing duplicate command IDs
when they arrive concurrently.

The product-action variant covers:

- Paint and Image Paint Start, Preview, Restore, and Cancel.
- Product-panel and ESP toggles.
- Validated complete settings application.
- Project load and current-project save, rename, delete, and editor mutation.

Each Canvas action carries the exact `ApplicationSnapshot::revision` from
which its control was rendered. A stale action is rejected transactionally.
At most one Canvas action is admitted per immutable frame, matching the
exclusive interaction contract and preventing two state-changing commands
from consuming the same snapshot. Disabled actions return a typed bounded
rejection without consuming an ID.

Commands bind Paint settings and current project ID/revision from the
immutable snapshot rather than trusting caller-owned identities. Settings
cannot replace the application-owned active-project reference. Names, project
IDs, editor mutations, layer/asset guards, action enums, and batch bounds are
validated before command construction. Start/Preview require exact readiness;
Restore/Cancel remain available from preview/job ownership without requiring a
currently published editor document.

The shared router serializes concurrent callers, preserves physical hotkey
repeat state, uses the final `uint64_t` command ID exactly once, clears held
keys on input loss/shutdown, and closes both hotkey and Canvas admission on
terminal shutdown.

## Portable product-panel composition

`compose_product_panel` owns only deterministic frame composition. It receives
one immutable `ProductUiModel`, retained pointer/focus and selected-tab state,
viewport/safe-area/input values, and copied localized labels. It returns a
bounded `CanvasFrame`, the responsive layout, the next local UI state, and at
most one revision-bound product action.

The shell currently renders all five localized section tabs, Paint and Image
Paint Start/Preview/Restore/Cancel rows, the complete portable Paint, Image
Paint project-settings, Settings, and ESP controls, and a language-neutral
completed/total and command-queue summary. It applies
viewport/DPI/configured scale, safe-area fitting, clipping, and the configured
theme accent. Disabled presentation actions cannot emit commands. Tab
switching changes only local panel state.

Closing the panel emits an empty frame, releases pointer capture and keyboard
focus, preserves the selected section, and does not mutate the model's ESP,
job, preview, or queue state. Labels are bounded and UTF-8 validated before
composition, and unknown section inventories or selected values fail closed.
The panel test composes every shipped locale and covers pointer section
switching, typed revision-bound actions, disabled controls, panel close, and
invalid input refusal.

## Complete portable Settings section

`SettingsPanelModel` now carries the exact validated complete
`ApplicationConfig`, not a partial UI subset. This prevents the Canvas layer
from inventing Paint, Image Paint, ESP, active-project, schema, or hotkey
values when it submits a settings edit.

The section exposes the 16-language selection, 75–200 percent UI scale, RGB
theme color, panel-toggle mapping, and all eight Paint/Image Paint mappings.
Each ordinary activation starts from the immutable complete config, changes
exactly one field, validates the result, and emits one `UiApplySettings` action
bound to the rendered snapshot revision.

Activating a function-key row now arms a retained capture state instead of
cycling to an arbitrary available key. The next exact F1–F24 press changes only
that mapping and publishes only after complete-config validation. A key owned
by another mapping publishes nothing, keeps capture armed, and renders the
catalog's localized duplicate message. Esc, explicit function-key input loss,
loss of settings admission, leaving Settings, and closing the panel clear the
capture without changing configuration. Capturing the existing mapping is a
local no-op, and out-of-range or internally inconsistent key input is rejected
before frame composition.

Settings rows use retained section-local wheel scrolling on constrained
viewports. Fully clipped controls are removed from focus and action admission.
Invalid model/config divergence fails closed before drawing. Portable tests
cover exact config copying, language, scale bounds, isolated RGB edits, direct
F1–F24 capture, localized duplicate refusal, cancellation/input-loss/tab/close
reset, unavailable actions, compact scrolling, all catalogs, out-of-range
input, and divergent-model refusal.

## Complete portable Paint settings section

The Paint section retains its four job/preview actions and adds every current
configuration field below them: brush size, side and front/back source bounds,
independent Front/Side/Back Paint/Fill/Skip routing, Auto Material, scene
lighting, Paint metallic/roughness/emissive, Fill color and independent
metallic/roughness/emissive, and color-compression tolerance.

Controls read the exact immutable Paint presentation and start edits from the
exact complete `ApplicationConfig`. Each interaction changes only its owned
field, validates the full result, and emits at most one revision-bound
`UiApplySettings` action. The section has retained bounded scrolling, clipped
control exclusion, localized labels and region-mode values, and finite
validation ranges shared with the core contract. Core validation now also
rejects unknown Front/Side/Back enum values instead of treating an invalid
persisted routing value as valid.

Portable tests exercise all three sampling/size values, all three region
controls, both toggles, all six material values, Fill color, compression,
field isolation, full-config validity, and the pre-existing action row.

## Complete portable ESP section

The ESP section preserves the dedicated master toggle action and adds the
All/Hider/Hunter scope, independent Boxes/Skeletons/Names/Distance/Snaplines
toggles, and Hider/Hunter RGB controls. Non-master edits copy the exact
immutable complete config, change one field, validate it, and publish one
revision-bound settings action. The master toggle retains its dedicated
application command because the application root owns its atomic
persisted/runtime transition.

The section has retained bounded scrolling and clipped control exclusion.
Portable tests cover exact scope cycling, every primitive, both role colors,
full-config field isolation, and master-enable preservation. Core and complete
config validation now reject unknown `EspScope` enum values before the panel
or frame builder can consume them. ESP-specific English role and primitive
terms match the v1 product vocabulary; translation expansion remains part of
the still-open full localization pass.

## Complete portable Image Paint project settings

The Image Paint section retains its four job/preview actions and adds all 16
persisted project controls: body profile, placement mode, transparent-pixel
behavior, independent Front/Right/Back/Left Fill/Skip bases, brush size,
compression tolerance, independent Image and Fill material values, and Fill
color. Labels and option values reuse the shipped localization catalogs.

Unlike application settings, these controls copy the exact immutable current
document settings and emit a `ReplaceImageProjectSettingsMutation`. The action
is bound to the rendered application snapshot revision, while the application
router/session continue to own current project identity and revision checks.
An absent document, unavailable edit ownership, an invalid value, or divergent
document/presentation state fails closed. No Canvas callback mutates an editor
document or runtime object directly.

The section has bounded local scrolling and excludes clipped controls from
interaction. Portable tests exercise every mapping/face/brush/material/color
field, exact field isolation, validation, revision binding, unavailable edits,
compact scrolling, and divergent-document refusal.

## Revision-safe Image Paint atlas integration

The product panel accepts an optional opaque atlas texture and guide overlay
tagged with the exact current project ID and revision. The binding is accepted
only while the immutable editor pipeline reports the same ready revision.
Texture creation, lifetime, and destruction remain outside the portable
application/UI libraries; stale, zero, pending, failed, or mismatched bindings
fail before Canvas composition.

The retained 2:1 atlas view now participates in the Image Paint section's
bounded scroll content. Pointer press/move/resize updates only a panel-local
draft, freezes wheel scrolling for the gesture, and renders that draft over the
current atlas. Release emits exactly one snapshot-revision-bound,
index/asset-guarded `ReplaceImageLayerMutation`; it does not restart worker
composition on every pointer move. Cancel discards the draft without a
command. The panel suppresses another gesture until a new immutable project
revision acknowledges the edit, then resets the transient draft.

Changing edit availability, losing the exact texture binding, switching
project/revision, or closing the panel clears transient gesture state without
mutating the project. Portable tests cover exact drawing, move/release/cancel,
scroll exclusion, stale-asset refusal, edit refusal, duplicate suppression,
and revision acknowledgement.

The selected-layer toolbar additionally exposes universal backward/forward
order controls plus localized seam-wrap, front/back-mirror, and Remove
controls. Ordering emits one `ReorderImageLayerMutation`; wrap and mirror copy
the immutable layer and emit one `ReplaceImageLayerMutation`; Remove emits one
`RemoveImageLayerMutation`. Every operation retains the selected index and
expected asset identity, enters the same revision-wait state, and is disabled
without an exact ready atlas, edit ownership, valid selection, or available
destination. Remove is also disabled for the final layer so the project remains
valid and non-empty. The application owner removes newly orphaned encoded
source bytes in the same transaction. Tests cover both order directions,
isolated wrap/mirror fields, guarded removal, orphan-source cleanup, and
final-layer refusal across two layers.

Crop is a separate source-bound local session rather than an atlas mutation
gesture. The frame binding carries bounded opaque source textures with exact
asset IDs and decoded dimensions; duplicate, zero, oversized, unrelated, or
invalid source bindings fail before Canvas composition. Entering Crop preserves
the selected layer index and asset identity, fits the original source into the
atlas viewport, and replaces the composed-atlas view with that source plus a
clipped selection border. Zoom and center dragging update only the local
session, and wheel scrolling remains frozen during a source drag.

Localized Zoom, Apply, and Cancel controls share the selected-layer toolbar.
Apply copies the immutable layer, changes only its normalized crop, and emits
one index/asset-guarded `ReplaceImageLayerMutation`; Cancel or the keyboard
cancel action discards the session without a command. Missing source bindings,
lost edit ownership, project/revision replacement, and panel close all fail
closed without persisting a draft. Portable tests cover source-versus-atlas
drawing, the four-edge crop indicator, zoom, press/move/release, scroll
exclusion, isolated Apply, both cancellation paths, missing-source refusal,
and malformed-source rejection.

## Portable Image Paint project controls

The Image Paint editor now owns a separate project toolbar rather than mixing
persistence controls into layer manipulation. Its single-line project-name
field accepts bounded UTF-8 text events and publishes
`UiRenameCurrentImageProject` only after a valid changed value is committed.
Empty, control-character, malformed, or oversized names never cross the
product-action boundary. The toolbar's localized Save control publishes
`UiSaveCurrentImageProject`, while Delete requires two distinct activations;
the first arms a visible selected state and the second publishes
`UiDeleteCurrentImageProject`. Keyboard cancel disarms deletion without an
action.

All three operations are enabled solely from the immutable
`ImageProjectActionAvailability` snapshot. The Canvas layer does not supply a
project ID or revision: the application router binds those identities from the
same snapshot used to compose the frame. Project/revision replacement and
panel close discard the local name/delete state. The toolbar is implemented in
its own composition unit so project persistence UX does not enlarge the layer
gesture/crop owner.

Portable tests cover UTF-8 name-field activation and commit, empty-name
refusal, snapshot revision binding, Save, two-step Delete, keyboard
cancellation, unavailable operations, invalid snapshot names, and the shifted
atlas/settings scroll geometry.

## Native picker and image-import boundary

The project toolbar now has a separate first row for localized Load-project and
Add-images controls. These controls emit at most one
`ProductUiEffectEnvelope`; they do not pretend a modal operating-system dialog
is an `ApplicationCommand` and do not perform file I/O during deterministic
Canvas composition. Load remains available when no project is active.
Add-images is available only with current edit ownership and captures the exact
project ID and revision rendered by the frame. An effect also carries the
application snapshot revision, and a picker effect wins over any coincident
state-changing action so a frame cannot publish both.

The Windows picker adapter uses `IFileOpenDialog`. Image selection is restricted
to PNG, JPEG, and WebP and permits multiple files; project selection accepts
one `.mcpreset`. Cancellation is represented as an empty successful selection.
Selected filesystem paths remain inside the Windows adapter. It opens each file
without following its final reparse point, refuses directories and reparse
points, enforces per-file and aggregate bounds, performs exact complete reads,
converts only the base file name to strict UTF-8, and returns immutable shared
bytes to the portable application boundary.

Portable import preparation derives every source identity from the selected
bytes, reuses matching source metadata already in the active document,
deduplicates repeated new sources, and creates one default full-atlas layer per
selected file. It refuses invalid documents/files, layer/source/aggregate-byte
overflow, unavailable or failed hashing, and identity collisions. The
product-action router validates the complete immutable add mutation, and the
editor session appends its sources and layers transactionally, validates the
resulting complete project, advances exactly one revision, and submits the
normal decode/composition pipeline while retaining active-draft persistence
ownership.

Portable tests cover picker-effect isolation, exact snapshot/project/revision
capture, no-document Load, unavailable controls, source reuse and
deduplication, limits, hash failure/collision, immutable source bytes, router
admission, transaction failure, draft ownership, and revision publication. The
native picker is compiled in the Windows MSVC Release graph; opening and
cancelling the real dialog remains a live/manual check.

`ProductUiEffectExecutor` owns the non-frame modal boundary. It refuses an
effect whose initial snapshot revision is already stale, opens only the
requested native picker, and reads a fresh immutable snapshot after the dialog
returns. Add-images requires the exact project identity and revision to remain
editable before it prepares one mutation; the emitted action is rebound to the
latest application snapshot revision. Preset Load similarly rechecks current
availability and returns immutable bounded bytes in a typed import action.
Cancellation is a successful non-mutating result, while picker and import
preparation failures retain separate structured error domains.

The preset command enters the existing bounded image-project I/O worker.
Decoding, project-store publication, configuration activation, and pipeline
submission remain outside the Canvas callback. Import accepts the v2 container
only, atomically publishes a new named project, reuses an exactly equal
existing project, and refuses a differing project with the same ID without
changing stored bytes. The editor's persistence busy guard prevents overlap
with Save, Load, Rename, Delete, and another Import. The application root
routes the typed command without reading preset structure or accessing Unreal.

Portable tests cover stale effects both before and after the modal boundary,
latest-snapshot rebinding, cancellation, picker error preservation, immutable
preset ownership, store reuse and conflict refusal, worker-thread import,
configuration activation, editor busy exclusion, pipeline publication, and
root routing.

## Body-guide and texture ownership boundary

`core/image_guide` derives one deterministic, locale-independent 1024×512 RGBA
overlay from each exact packaged round, cube, and fukuyoka ImageReference
profile. Profile decoding now validates the complete ordered reference-pose
component-transform collection and its skeleton parent hierarchy. Generation
projects the bounded reference geometry and skeleton into all four canonical
faces, draws only translucent white guide content, checks cancellation and a
fixed raster-work ceiling, and never touches a UObject, runtime texture,
project atlas, or persisted preset.

Front/Right/Back/Left remain localized Canvas text rather than pixels baked
into the overlay. The product panel supplies all four labels only with an
exact-profile guide, and the portable editor draws them after the guide
texture and before selection primitives. This keeps one reusable guide bitmap
per body type while retaining every shipped locale.

`ImageEditorPipeline` now publishes a matching immutable ready-content bundle:
the exact composed project/atlas and the decoded source pixels used for that
revision. Encoded and decoded bytes remain outside application snapshots.
This bundle gives Crop and the runtime texture owner the source content they
need without decoding again or accepting mismatched revisions.

`ImageEditorTextureCoordinator` is the game-thread-only owner of opaque
atlas/source/guide texture generations. It installs the exact three guide
textures once, reuses them across project revisions, publishes a replacement
frame only after every new texture succeeds, rolls back partial creation,
retires the previous generation after publication, bounds failed releases,
and retries release during later synchronization or shutdown. Clear releases
only project textures; shutdown is retryable until all project-owned handles
are released. Its narrow runtime port exposes no UE4SS or Unreal type. The
production reflected texture implementation and live travel/unload evidence
remain open.

Tests cover all packaged profiles, deterministic four-face output, strict
reference-transform validation, cancellation, ready-content matching and
invalidation, localized guide-label ordering, wrong-thread refusal, unchanged
generation reuse, partial-create rollback, failed-release retry, clear, and
resource-free terminal shutdown.

## Project-owned HUD-frame coordination

`ProductUiFrameCoordinator` is the portable production composition boundary
for one validated HUD/Canvas frame. It owns no UE4SS or Unreal type. A narrow
runtime port captures project-owned viewport, pointer, keyboard, text-edit,
function-key, input-availability, and owner-window values and renders one
bounded `CanvasFrame`.

Each admitted frame reads one immutable `ApplicationSnapshot`, builds the
validated presentation model, captures bounded runtime input, routes physical
hotkeys or hands the next press to Settings capture, synchronizes an exact
ready Image Paint texture generation, composes localized panel output,
reconciles the exact input lease, and renders. The coordinator commits retained
panel state and routes a Canvas action only after rendering succeeds. A render
failure immediately restores the previously captured game input state and
publishes no Canvas action.

Modal picker effects run only after deterministic composition and successful
rendering. The effect executor re-reads the newest immutable snapshot after the
dialog, and only its revision-rebound typed action can enter the shared command
sink. Cancellation remains successful and non-mutating. Physical hotkeys keep
their independent bounded command path, including repeat suppression and input
loss release.

Ready Image Paint revisions are synchronized through the game-thread texture
coordinator before composition. Empty, failed, stopped, replaced, or removed
documents clear only project texture generations; the shared body-guide
catalog survives until terminal shutdown. Shutdown closes both hotkey and
frame admission, restores the exact input lease, retries texture release when
needed, clears retained interaction state, and becomes terminal only after all
owned transient state is released.

The fake-runtime integration test covers the closed F9 path, open localized
render/input acquisition, render-before-typed-action ordering, tab-local state,
modal cancellation and owner-window propagation, render rollback, closed-panel
input release, ready texture publication/project clearing, guide lifetime, and
terminal shutdown without importing a UE4SS or graphics header.

## Localized status strip

The status strip is now composed by a dedicated portable owner. It renders the
selected catalog's Progress, Elapsed, ETA, and Queue labels together with exact
completed/total counts, a checked percentage, millisecond-precision duration
text, and command-queue occupancy. A missing ETA is rendered as unavailable
rather than fabricated as zero. The whole line is clipped to the status strip,
so long translations cannot draw into the panel content.

Panel validation independently rechecks completed/total arithmetic, the exact
fraction, finite bounded queue pressure, and all derived ranges before duration
or percentage formatting. Portable tests cover English and Japanese labels,
known and absent ETA, exact duration text, queue counts, and NaN refusal; all 16
catalog paths continue through composition.

## Complete portable Diagnostics section

The Diagnostics tab now consumes only the bounded immutable
`DiagnosticsPanelModel`. It renders localized runtime and compatibility state,
separate command/runtime queue counts and utilization, omitted-entry counts,
and the newest ordered diagnostic entries. Generic operation failures,
severity terms, state terms, and the empty state use the selected one of all 16
catalogs. Command IDs plus stable runtime contract/failure identifiers remain
visible so a compatibility failure is actionable without an unbounded log or
external window.

The section has retained local scrolling and emits no product action.
Presentation validation rejects oversized/unordered diagnostics, invalid
UTF-8 keys, inconsistent compatibility failures, invalid enum values, and
non-finite or arithmetically inconsistent queue utilization before drawing.
Portable tests cover localized messages, command IDs, Canvas/missing-function
details, omitted counts, the empty state, compact scrolling, and incoherent
queue refusal.

This remains a partial product UI milestone. The production UE4SS callback,
frame/pointer/text capture, live input-lease proof, and remaining UCanvas/font
adapters remain open.

`ProductUiFrameCoordinator` now implements the application-owned
`RuntimeFrameExtensionPort`. Each compatible root HUD frame supplies the same
validated World/controller/HUD/Canvas identity used by feature dispatch.
Capture/render, input-lease, and texture failures map to typed compatibility
contracts without escaping the non-throwing callback boundary. During
shutdown, the root retries UI input/texture restoration on later game-thread
frames and cannot request lifecycle restoration or unregister the callback
until the coordinator has stopped successfully. Portable tests cover identity
delivery, Canvas failure mapping, terminal admission closure, failed
restoration retry, and exact ordering before transient-state restore.

The complete localized label inventory also has a packaged, source-pinned
Noto-derived RGBA fallback atlas. Its exact codepoint set, PNG geometry/hash,
manifest provenance, and OFL text are verified in CI and again during trusted
runtime assembly. This closes portable all-language glyph coverage without a
runtime font dependency; game-font-first selection and fallback texture
drawing remain part of the production UCanvas adapter and live gate.

## Remaining work

- Bind the root-owned HUD-frame callback and attached frame coordinator to the
  production UE4SS callback registration adapter.
- Implement production UCanvas frame/pointer/text capture, live-verify the
  compiled Unreal input-lease port, and connect registered UE4SS key callbacks
  only when the frame consumer is owned by the composition root.
- Select and bind the game-font/OFL fallback path, then live-verify the already
  bound reflected texture creation/release across travel and teardown.
- Complete fake-runtime and live UI verification across all languages,
  resolutions, and DPI settings.

No external window, WebView, debug tab, Present hook, or speculative UE4SS
adapter is introduced by this milestone.
