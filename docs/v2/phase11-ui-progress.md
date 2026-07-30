# Phase 11 Product UI Progress

Phase 11 is open. The portable Canvas protocol, layout, interaction, widget,
scroll, text-edit, Image Paint editor, input-lease, and typed editor-mutation
boundaries are implemented. This milestone adds the application-owned hotkey
command boundary. Production UCanvas rendering, Unreal input registration,
font/texture lifetime, complete section composition, and live verification
remain intentionally unimplemented until the protected UE4SS graph exposes
the exact accepted interfaces.

## Ownership and dependency direction

The project-owned `meccha_ui` library remains dependent on `src/core` only. It
does not import application state, UE4SS, Unreal, Windows, or graphics APIs.
The new `InputCommandRouter` lives in `src/mod/application`, where it can read
an immutable `ApplicationSnapshot` and emit only the existing typed
`ApplicationCommand` variant.

The future production input adapter therefore has one narrow responsibility:
register F1–F24 once, translate physical press/release callbacks into bounded
`FunctionKeyEvent` values, pass the current immutable snapshot to the router,
and enqueue the returned commands. It does not choose feature settings,
project revisions, or command identities.

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

The complete Linux, Linux ASan/UBSan, and Windows MSVC Release graphs pass 68,
68, and 85 tests respectively.

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

## Remaining work

- Compose the five complete Paint, Image Paint, ESP, Settings, and Diagnostics
  Canvas sections from the implemented immutable presentation values.
- Connect Canvas widget/editor activations to the implemented typed product
  action router without direct runtime mutation.
- Connect the production UE4SS key callbacks and input lease.
- Implement game-font/OFL fallback selection and runtime texture lifetime.
- Complete fake-runtime and live UI verification across all languages,
  resolutions, and DPI settings.

No external window, WebView, debug tab, Present hook, or speculative UE4SS
adapter is introduced by this milestone.
