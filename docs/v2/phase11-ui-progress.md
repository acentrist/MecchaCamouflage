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

The complete Linux, Linux ASan/UBSan, and Windows MSVC Release graphs pass 66,
66, and 83 tests respectively.

## Remaining work

- Compose the five complete Paint, Image Paint, ESP, Settings, and Diagnostics
  Canvas sections from immutable presentation values.
- Convert Canvas interactions into typed commands without direct runtime
  mutation.
- Connect the production UE4SS key callbacks and input lease.
- Implement game-font/OFL fallback selection and runtime texture lifetime.
- Complete fake-runtime and live UI verification across all languages,
  resolutions, and DPI settings.

No external window, WebView, debug tab, Present hook, or speculative UE4SS
adapter is introduced by this milestone.
