# MecchaCamouflage v2 Complete Rewrite Plan

## 1. Summary and Locked Decisions

MecchaCamouflage v2 will completely replace the v1 runtime while preserving all user-facing Paint, Image Paint, ESP, preview, cancellation, hotkey, preset, progress, and localization capabilities.

- Keep the existing repository, issues, releases, and project identity.
- Preserve v1 at tag `v1.6.7` and branch `release/v1.x`.
- While the rewrite is active, synchronize v2 to the latest stable v1 release
  at maintainer-requested checkpoints. Each checkpoint replaces the previous
  v1 behavior and resource target; superseded v1 releases remain preserved as
  history, not as concurrent v2 compatibility targets.
- Develop v2 on `rewrite/ue4ss-v2` through one public Draft PR.
- Do not create duplicated `v1.x/` and `v2.x/` source directories.
- Keep `main` on v1 until the complete v2 acceptance matrix passes.
- Release the merged rewrite as `v2.0.0`.
- Distribute exactly one native Windows executable.
- Build the launcher and mod in C++; remove the .NET, WinForms, WebView2, JavaScript, TCP/JSON bridge, and custom injector stacks.
- Use a pinned UE4SS C++ runtime. Do not use Lua.
- Use UE4SS for loading, reflection, UObject access, lifecycle, and hook infrastructure.
- Implement the product UI and ESP through Unreal `AHUD`/`UCanvas`; UE4SS debug tabs are not the product UI.
- Remove all custom DXGI, D3D11, D3D12, Present, ProcessEvent-vtable, and MinHook rendering code.
- Start with a fresh v2 settings and preset format. Do not migrate or read v1 user data.
- Keep the `.mcpreset` filename extension for the product feature, but require
  the v2 schema/magic and reject v1 containers with a specific incompatibility
  message. “Preserve presets” means preserve create/load/save/rename/delete
  capability, not v1 file compatibility.
- Retain all 16 current UI languages.
- Never change Windows Defender configuration.
- Request UAC only when the game directory is not writable and the minimal proxy files actually need to be changed.
- Treat preparation as a persistent portable deployment: after a successful
  prepare, direct later game launches continue to load MecchaCamouflage until
  the user runs `--remove`. The launcher itself does not remain resident or
  register a traditional Windows installer/service.
- Do not merge or release an incomplete subset of the three required features.

The initial UE4SS candidate is commit [`6c26f038751b3d96059d4a9148f5d093012d55ad`](https://github.com/UE4SS-RE/RE-UE4SS/commit/6c26f038751b3d96059d4a9148f5d093012d55ad), which includes UE 5.6 support. It becomes the frozen v2 runtime only after the architecture compatibility gate passes. Any later UE4SS upgrade requires a dedicated compatibility PR.

### Program goal

Deliver a maintainable, fail-closed, single-EXE MecchaCamouflage v2 whose
runtime is substantially smaller and more stable than v1, without reducing the
Paint, Image Paint, or ESP product surface. The rewrite is complete only when:

- The one distributed EXE safely prepares the pinned runtime and launches the
  game without a custom injector, persistent desktop UI, or network access.
- The in-game UI, Paint, Image Paint, and ESP work through the reviewed UE4SS
  and UCanvas boundaries.
- Automated tests cover all project-owned deterministic logic and safety
  policies.
- Required live-game and Windows checks have recorded evidence, including
  checks that cannot be performed by an automated coding agent.
- Every v1 runtime path scheduled for removal has a passing v2 replacement or
  an explicitly approved removal decision.
- The final source and packaged artifact contain no temporary compatibility
  shim, hidden fallback, unreviewed hook, or partially implemented product
  mode.

### Execution rules

The following rules apply to every phase:

1. Each phase has explicit entry criteria, deliverables, automated checks,
   live/manual checks, exit criteria, and stop conditions.
2. A phase may be marked `PASSED` only when all of its required evidence
   exists. Code inspection is not a substitute for a live check.
3. A check that cannot be run in the current environment is marked
   `BLOCKED_EXTERNAL_VALIDATION`, not assumed to pass and not recreated with a
   production workaround.
4. Work may continue around an externally blocked check only when the later
   work does not depend on its result. A hard gate blocks all dependent phases.
5. Architecture probes must use the intended production boundary and
   production-quality lifetime rules. Probe-only diagnostics must be
   compile-time excluded from release payloads or deleted before the phase
   exits.
6. Do not add a second implementation path “temporarily.” In particular, do
   not add fallback injection, WebView UI, debug-tab UI, Present rendering,
   direct texture import for multiplayer paint, permissive reflection, or
   unsafe teardown to make a gate appear successful.
7. Do not optimize by weakening validation, scheduler pacing, queue drain,
   cancellation, ownership checks, or atomic persistence.
8. Do not delete a v1 implementation until its behavioral contract has been
   captured and its v2 replacement has passed the required checks.
9. Keep commits phase-coherent and reviewable. Mechanical deletion, generated
   data, dependency updates, and behavior changes must not be mixed without a
   documented reason.
10. If evidence contradicts this plan, stop the affected phase, record the
    result, revise `PLAN.md` or an architecture decision record, and obtain
    review before continuing.

### Verification ownership and evidence vocabulary

Every verification item is assigned one of these owners:

- **Automated**: runnable by local/CI commands without a live game. This
  includes unit, contract, fake-runtime, temporary-tree launcher, payload,
  static, and binary tests.
- **Agent-operated live**: runnable only when the coding environment has direct
  access to the supported Windows host, the prepared game process, and the
  resulting logs or screen. Availability must be demonstrated, not assumed.
- **Maintainer-observed**: requires a person to judge visuals, cursor/input
  behavior, UAC prompts, Steam behavior, or another physical/virtual Windows
  host.
- **Multi-client**: requires at least two real game clients and cannot be
  replaced with a mock transport test.

Use only these result states in the future evidence ledger:

```text
NOT_RUN
PASS
FAIL
BLOCKED_ENVIRONMENT
BLOCKED_EXTERNAL_VALIDATION
NOT_APPLICABLE
```

`PASS` must record the product commit, payload manifest hash, UE4SS commit,
game version, operating system, test procedure, and relevant logs/screenshots.
`BLOCKED_*` must state exactly what access or observation is missing. A failed
or blocked release-critical check prevents release.

Future implementation will create the following review artifacts:

```text
docs/v2/
  requirements-traceability.md
  architecture.md
  compatibility-contracts.md
  live-test-checklist.md
  risk-register.md
  decisions/
  evidence/
```

Evidence containing user names, machine paths, Steam identifiers, or unrelated
mod inventory must be redacted before it is committed. Large raw logs and
screenshots remain release artifacts; the repository stores a compact result
record and hash.

### Capability boundary for live testing

The coding agent can implement and run deterministic source-level checks,
inspect produced binaries, exercise fake runtime adapters, and test launcher
behavior against isolated temporary directory trees. It cannot independently
claim the following without direct environment access and observable evidence:

- Visual correctness and interaction quality inside the live game.
- Exact restoration of mouse, camera, and movement input.
- Windows 10 and Windows 11 UAC/Steam behavior on real installations.
- Compatibility with a user's unknown existing proxy or UE4SS installation.
- Host-to-client and joining-client-to-host paint replication.
- Subjective readability across all languages, DPI values, and resolutions.
- Game stability through real lobby, match, travel, freecam, spectator, and
  shutdown sequences.

A game process that was started before the pinned proxy and runtime were
prepared is not treated as a UE4SS test session. The launcher must not modify
runtime files while the game is running. The maintainer controls game exits,
restarts, multiplayer participants, and visual observations. The agent prepares
an exact checklist and diagnostics for each collaborative live session.

## 2. Target Architecture and Interfaces

### Repository structure

```text
src/
  core/                 Pure C++ domain models and algorithms
  mod/
    application/        Commands, state machines, coordination
    runtime/            UE4SS and MECCHA CHAMELEON adapter
    ui/                 UCanvas UI, input, localization
    paint/              Paint planning, preview, dispatch
    image_paint/        Image editing, compositing, persistence
    esp/                Target capture, projection, Canvas rendering
  launcher/             Native payload preparation and Steam launch
  tests/                Core, launcher, and contract tests

resources/
  localization/
  mesh-profiles/
  fonts/
  ue4ss/

third_party/
  RE-UE4SS/             Exact gitlink for the accepted commit
  CUE4Parse/            Research/profile generation only
```

`src/core` must not include UE4SS, Unreal, Windows UI, graphics, or launcher headers. Only `UnrealRuntimeAdapter` may expose UE4SS types to the rest of the mod. Feature modules consume project-owned interfaces and strongly typed domain values.

Production state is owned by a composition root. Do not introduce process-wide mutable feature globals.

### Runtime data flow

```text
UE4SS lifecycle
      ↓
UnrealRuntimeAdapter ──→ HudFrameAdapter
      ↓                        ↓
GameThreadScheduler       UCanvas frame
      ↓                    ↙           ↘
Paint/Image engines   In-game UI      ESP
      ↑
immutable plans from worker threads
```

Thread rules:

- `on_unreal_init()` installs validated callbacks and resolves initial Unreal contracts.
- UE4SS `on_update()` runs on the UE4SS update thread and must never access UObjects or invoke ProcessEvent.
- HUD/Canvas callbacks are the game-thread boundary.
- All Unreal object discovery, property access, UFunction calls, texture mutation, paint dispatch, and ESP capture occur on the game thread.
- Image decoding, atlas composition, color compression, and paint planning operate off-thread using immutable copied data.
- UI callbacks enqueue typed commands and read immutable snapshots; they do not mutate runtime objects directly.
- Shutdown rejects new product commands and cancels workers first. While the
  game-thread callback and scheduler are still active, it restores any preview
  and input lease and drains required game-thread work. It then makes callbacks
  inert, unregisters them, waits for in-flight callbacks, destroys feature
  state, and returns from `uninstall_mod`.
- If the Unreal world is already unavailable because the process is exiting,
  shutdown performs no UObject restoration against invalid state and records
  that condition. Explicit live mod unload is accepted only when preview/input
  restoration completes before callback removal.

### Layering and dependency rules

The build graph must enforce the intended architecture rather than relying only
on review:

```text
core
  ↑
application ← persistence/image codecs
  ↑
feature engines
  ↑
mod composition root
  ↑
runtime + HUD/Canvas adapters

launcher core ← Windows launcher shell
payload tooling
```

- `core` contains value types, validation, deterministic algorithms, and no
  platform dependency.
- `application` owns commands, snapshots, job arbitration, preview ownership,
  cancellation, and shutdown coordination.
- Paint, Image Paint, ESP, and UI depend on project-owned application/runtime
  interfaces, never directly on UE4SS headers.
- The runtime adapter implementation is the only production target that
  includes UE4SS or Unreal headers.
- Canvas is exposed through a frame-scoped project-owned drawing interface.
  Feature code must not retain `UCanvas`, `AHUD`, `UWorld`, `APlayerController`,
  `UObject`, or reflected property pointers beyond the validated callback.
- The launcher and mod share only narrowly scoped manifest, hashing, schema,
  and version primitives. The launcher must not link mod runtime code.
- CUE4Parse and profile-generation tools are outside the production dependency
  graph.
- Tests may use a fake runtime implementation, but production code must not
  contain a selectable fake or simulation backend.

CI will include include-graph or source-boundary checks that reject UE4SS,
Unreal, WinForms, WebView, socket/JSON command, DXGI, D3D, or MinHook headers in
disallowed modules.

### Project-owned interfaces

Exact C++ names may change during implementation, but the responsibility
boundaries are fixed:

```text
IRuntimeLifecycle
  install validated callbacks; begin inert shutdown; unregister; drain

IGameThreadScheduler
  enqueue bounded typed work; execute within frame budget; cancel pending work

IUnrealRuntime
  resolve compatibility; capture game-owned snapshots; invoke typed operations

ICanvasFrame
  frame-scoped lines, boxes, text, textures, clipping, viewport and DPI

IInputRuntime
  register supported keys; capture and restore exact controller/input state

ISettingsStore / IImageProjectStore
  validate, load, and atomically replace project-owned files

IImageDecoder
  decode bounded PNG/JPEG/WebP bytes into project-owned RGBA buffers

IClock / IWorkerExecutor
  deterministic time and background work boundaries
```

`IUnrealRuntime` accepts and returns project-owned typed request/result values;
it does not expose raw UE4SS types. Runtime-owned object handles are opaque,
generation-checked, and revalidated on every game-thread use. A handle never
extends an object's lifetime.

All public operations return a structured result with a stable project error
code, a localized user-facing key, and bounded diagnostic context. Exceptions
must not cross the DLL export, UE4SS callback, worker entry, Windows callback,
or file-dialog boundary.

### State ownership and lifecycle

The composition root owns all long-lived state. The minimum explicit state
machines are:

- Runtime: `Cold → Initializing → Compatible | Incompatible → ShuttingDown →
  Stopped`.
- Application job arbiter: `Idle → Planning → Dispatching → Cancelling →
  Draining → Completed | Failed | Cancelled → Idle`.
- Preview lease: `None → Capturing → Active → Restoring → None`, with exactly
  one owner across Paint and Image Paint.
- UI input lease: `Closed → Opening → Open → Closing → Closed`, storing the
  precise pre-open cursor, input-mode, movement, and look state.
- World binding: `Unbound → Resolving → Bound → Invalidated`, where travel,
  HUD replacement, controller replacement, or object invalidation returns to
  resolution without retaining stale pointers.

Commands carry monotonically increasing IDs. Snapshots carry a revision so the
UI can ignore stale results. Worker completions include the originating command
and cancellation generation; late results from an old job are discarded
without touching Unreal state.

Queue policy is bounded and observable:

- Control work such as shutdown, restore, and cancel cannot be starved by
  paint dispatch.
- Frame work has an explicit count/time budget and reports queue pressure.
- Terminal completion is impossible while required game-owned work is queued
  or while an in-flight callback can still mutate state.
- Shutdown rejects new commands before cancelling workers and draining
  callbacks.

### Runtime compatibility contract

`UnrealRuntimeAdapter` owns one declarative compatibility table containing all
game-specific classes, objects, properties, functions, parameter layouts,
accepted engine/game versions, and profile schema versions.

At initialization and again after travel/rebinding, it validates:

- UE4SS runtime identity and expected API/ABI.
- Engine and game build information available at runtime.
- Class inheritance and object type.
- Property existence, kind, offset/size assumptions, and owning class.
- UFunction owner, flags where relevant, total parameter size, each parameter
  kind/offset/direction, and return semantics.
- Object validity immediately before use.
- Mesh/profile identity and expected data dimensions.

Compatibility has three externally visible states:

```text
compatible
unsupported_game_build
runtime_contract_error
```

An unsupported or partially resolved contract disables the affected operation
before mutation and produces an actionable diagnostic naming the failed
contract. There is no “best effort” offset, signature, or parameter fallback.

### Diagnostics and observability

- Keep an in-memory bounded ring for the Diagnostics panel.
- Write rotating bounded logs under the v2 data root.
- Include command IDs, state transitions, callback generation, queue depth,
  planning/dispatch timing, compatibility contract names, and terminal reasons.
- Do not log original image bytes, atlas pixels, arbitrary process memory,
  credentials, Steam tokens, or unrelated mod contents.
- Paint terminal diagnostics must distinguish planning complete, local
  submission complete, game-owned queue drain, cancellation, and failure.
- Release builds may expose product diagnostics but exclude research probes,
  arbitrary object enumeration, developer consoles, and memory dumping.

### Public and persisted contracts

The only DLL exports are:

```cpp
extern "C" __declspec(dllexport)
RC::CppUserModBase* start_mod();

extern "C" __declspec(dllexport)
void uninstall_mod(RC::CppUserModBase*);
```

Replace JSON commands with an internal C++ command variant covering:

- Start, preview, restore preview, and cancel Paint.
- Start, preview, restore preview, and cancel Image Paint.
- Toggle UI and ESP.
- Apply validated settings.
- Load, save, rename, and delete Image Paint projects.

Expose immutable application snapshots to the UI containing job state, active preview, pass progress, queue pressure, elapsed time, ETA, runtime compatibility, and bounded diagnostics.

The complete v2 persisted model preserves all current settings, but ownership
is split so `config.json` never contains image bytes or the full layer graph.

`config.json` contains:

- `schema_version`.
- UI language, scale, theme, UI-toggle hotkey, and the existing eight Paint/Image Paint hotkeys.
- Every current Paint setting.
- Every current ESP scope, primitive toggle, and role color.
- Image Paint editor defaults and an optional active project/draft identifier,
  but no original image bytes, atlas bytes, or layer collection.
- No external-window position, opacity, always-on-top, process name, WebView, bridge, or injector fields.

Each v2 Image Paint project/draft owns its material, fill, layer order,
placement, crop, seam-wrap, mirror, body type, face routing, original image
bytes, and canonical atlas. Together, config plus the project store preserve
every current Image Paint setting without expanding `config.json` toward the
64 MiB project limit.

Use a stable v2 data root:

```text
%LOCALAPPDATA%\MecchaCamouflage\v2\
  config.json
  image-projects\
    active-draft.mcpreset
    <project-id>.mcpreset
  logs\
  runtime\
    active\
    prepare-transaction.json  # exists only during/recovering an update
```

Settings and project manifests use atomic temporary-file replacement. The new `.mcpreset` implementation supports the v2 format only and stores a validated manifest, original image bytes, layer transforms, and canonical 1024×512 RGBA atlas. It does not contain v1 migration code.

Persistence behavior is further constrained as follows:

- v2 never scans, imports, deletes, or rewrites the v1 data root.
- A missing config loads documented defaults. A malformed config is reported
  and left untouched; it is not silently overwritten during startup.
- Applying settings validates a complete candidate, including ranges and
  duplicate hotkeys, before publishing one new immutable configuration
  snapshot.
- Writes use a unique same-directory temporary file, flush file data, replace
  the destination atomically where the filesystem supports it, and preserve
  the previous valid file if any step fails.
- The `.mcpreset` container format and canonical byte ordering are documented
  before implementation. The decoder rejects absolute paths, traversal,
  duplicate/case-colliding entries, links/reparse targets, unknown schema
  versions, unsupported codecs, declared/actual size mismatches, excessive
  expansion, hash mismatches, invalid UTF-8, non-finite transforms, and
  resource-limit violations.
- A recognized v1 `.mcpreset` is rejected as an unsupported legacy format with
  a specific non-destructive message. It is never partially interpreted as v2.
- Preset parsing completes into project-owned memory and passes full validation
  before replacing the active editor state.
- Active-editor persistence is a debounced worker operation that atomically
  replaces `active-draft.mcpreset`; it never serializes the project from a
  Canvas callback.
- Saving a named project publishes the fully verified project container before
  updating the small active-project reference in `config.json`. If the second
  write fails, the valid project remains discoverable and the previous active
  reference remains valid.
- A missing project referenced by config produces an actionable diagnostic and
  opens the last valid active draft or a blank editor; startup does not
  overwrite either file.
- Original source images remain byte-for-byte content-addressed entries; the
  canonical atlas is reproducible from the manifest and sources and is checked
  on load.
- Project IDs are generated internally and are never used as unvalidated path
  components.
- No persistence operation runs while holding a game-thread callback or
  application state lock.

Initial resource limits to preserve from v1 are 12 MiB per source image,
64 MiB total source bytes per project, and a canonical 1024×512 RGBA atlas.
Any changed limit requires a documented memory/performance reason and tests.

### In-game UI

Implement a project-owned immediate-mode UCanvas UI with Paint, Image Paint, ESP, Settings, and Diagnostics sections.

- Toggle with a configurable key, defaulting to `F9`.
- Preserve the eight existing action hotkeys, defaulting to `F1`–`F8`.
- Register supported keys once through UE4SS and dispatch according to the current validated mapping.
- Show the mouse cursor and suspend look/movement input while the panel is open; restore the exact previous state when it closes.
- Scale against viewport size and DPI.
- Keep ESP active while the control panel is closed.
- Use UCanvas line, box, text, and texture primitives only.
- Use a Windows file picker solely for image and preset selection; no external control window remains open.
- Decode PNG/JPEG through Windows Imaging Component and WebP through a pinned, redistributable native decoder.
- Composite Image Paint layers on a worker thread and create the preview texture through validated reflected Unreal APIs.
- Preserve multi-image import, ordering, drag/resize, crop, seam wrapping, front/back mirroring, three body profiles, Fill/Skip faces, preview, restore, and preset storage.
- Preserve the body-specific Image Paint guide overlay for round, cube, and
  fukuyoka. It is derived from/versioned with the accepted profile, renders
  above editable layers, and is never composited into the painted atlas.
- Retain all 16 translation catalogs. Resolve the game’s localized UI font first and provide a packaged OFL fallback glyph atlas. CI must verify that every shipped translation has glyph coverage.

If UCanvas cannot provide the required interaction or text/texture behavior, implementation stops for architecture review. It must not silently fall back to WebView, an external UE4SS window, or custom Present rendering. Packaged UMG assets are the only reviewed fallback and require explicit approval.

### Paint and ESP boundaries

Port the proven pure algorithms from `runtime_contract.hpp` into focused C++ modules rather than carrying forward `bridge.cpp`.

Paint must preserve:

- Round, cube, and fukuyoka profile validation.
- Paint, Fill, and Skip region routing.
- Brush size and color compression.
- Metallic, roughness, emissive, and independent Fill material.
- Normal Paint always projects the hidden environment capture onto the mesh.
  It has no Auto Material or scene-lighting mode switch. Image Paint remains a
  separate imported-image source and does not enter the environment-capture
  calibration path.
- Environment projection uses a fixed four-texel calibration lattice that is
  independent of the selected replay brush size and final region filters.
- Front and Back anchor one deterministic correction field. Side vertices use
  harmonic interpolation when both boundaries are present and a one-sided
  extension when only one boundary is reachable; an unanchored Side component
  fails closed.
- Albedo feedback retains the best validated albedo-only result locally. It
  never reverts the complete Paint job to a global appearance fallback.
- Automatic Emissive requires repeatable source separation and a calibrated
  target E=1 response. Accepted source residual chromaticity is carried by
  bounded Albedo while Emissive remains a scalar with the manual value as its
  floor.
- Compression operates on a brush-aligned coverage field. Holes, unsafe
  samples, UV islands, regions, and material changes are hard boundaries;
  deterministic circle covering and per-channel minimax representatives bound
  the replay error.
- Non-preemptible `PaintAtUVWithBrush` dispatch is paced from the measured
  game-thread slice duration, with a documented finite delay cap.
- Preview snapshot and exact unpreview restoration.
- Fill-first followed by Paint overwrite.
- Bounded planning and per-frame dispatch.
- Game-owned `PaintAtUVWithBrush` as the only production multiplayer route.
- Queue drain before terminal completion.
- Cancellation during planning and dispatch.
- Continued operation through freecam and controller-pawn changes.

ESP must preserve:

- All/hider/hunter targeting.
- Boxes, skeletons, names, distance, and snaplines.
- Hider and hunter colors.
- Role changes, avatar replacement, spectator exclusion, freecam, map travel, and HUD replacement.
- Game-thread capture and UCanvas drawing without any graphics API hook.

All game-specific object names, properties, UFunctions, and parameter layouts live in `UnrealRuntimeAdapter`. Every lookup validates class, parameter size, property kind, and object lifetime and fails closed with an actionable compatibility error.

## 3. Launcher, UE4SS, Build, and Rewrite Work

### Lightweight launcher behavior

The distributed EXE is a portable prepare-and-launch tool with a persistent
minimal loader footprint. It does not register with Windows Installer, but a
successful prepare intentionally remains effective for later direct game
launches until `--remove`. Default double-click behavior is:

1. Acquire a single-instance preparation mutex.
2. Resolve Steam App ID `4704690` and locate the real `Chameleon\Binaries\Win64` directory, with a folder picker fallback.
3. Reject updates while `PenguinHotel-Win64-Shipping.exe` is running.
4. Verify the embedded payload manifest without network access.
5. Resolve the effective UE4SS loader chain in its real priority order,
   including any `--ue4ss-path`, `override.txt`, conventional location, proxy,
   required settings, and actual `UE4SS.dll`.
6. Select exactly one deployment mode: managed runtime, compatible shared
   runtime, or conflict.
7. In managed mode, reuse the verified LocalAppData `active` tree or publish a
   fully verified replacement through the recovery transaction below, then
   ensure the owned/reused proxy and owned `override.txt`.
8. In compatible shared mode, leave the user's proxy, runtime, override, and
   unrelated mods unchanged and add only the exact MecchaCamouflage mod folder.
9. Launch `steam://rungameid/4704690` and exit.

In managed mode, `override.txt` points to the stable `runtime\active`
directory. Updates do not require changing the proxy path. After a successful
transaction and cleanup, only one managed runtime generation remains.

Supported launcher switches:

```text
--game-dir <path>   Override automatic game discovery.
--prepare-only      Validate and prepare without launching Steam.
--remove            Remove only hash-matched Meccha-owned files and cache.
```

The launcher uses TaskDialog/error logs rather than a persistent desktop UI. It does not auto-update, run a service, remain resident, or contact the network.

All LocalAppData extraction, hashing, and transaction work runs unelevated as
the invoking user. When managed-mode writes fail with access denied, the
launcher explains the exact `dwmapi.dll`/`override.txt` mutation and requests
UAC for a minimal broker operation. The broker uses the canonical original-user
runtime path handed to it; it never derives the path from the elevated
account's `%LOCALAPPDATA%`. It revalidates the game path, invoking-user identity,
request nonce, source hashes, embedded manifest, current destination hashes,
and exact operation, then writes/removes only the approved two files.

Shared-mode installation does not elevate merely to modify another UE4SS
owner's mod tree. If the exact shared mod location is not normally writable, it
fails closed with the conflicting path and manual resolution guidance.

Neither process modifies Defender, firewall, Steam settings, ACLs, or unrelated
game files.

#### Managed runtime update transaction

Windows cannot atomically replace a populated `active` directory in one
operation. Managed updates therefore use a recoverable same-volume transaction:

1. Extract into a unique sibling `staging-<nonce>` directory.
2. Verify every immutable packaged file and the complete manifest.
3. Flush the staged files and atomically write a transaction journal containing
   the old/new manifest hashes and expected directory names.
4. Rename existing `active` to the single `rollback` directory.
5. Rename the verified staging directory to `active`.
6. Reverify the new `active`, atomically mark the journal committed, then
   remove `rollback` and the journal.

Startup recovery runs before a new prepare. It validates `active`, `rollback`,
staging, and the journal against explicit expected paths and hashes. If a crash
occurred between renames, it restores the last verified runtime before retrying
the update. It never guesses based on directory age or name alone. Failed work
before publication leaves `active` usable; an interrupted rename window leaves
the prior generation recoverable on the next launcher run. A successful prepare
leaves no staging, rollback, or journal generation.

The payload manifest distinguishes immutable packaged files from explicitly
allowlisted UE4SS-generated cache/log paths. Reuse hashes every immutable file.
Managed cleanup may remove generated paths only beneath the verified
Meccha-owned runtime root and only when there is no reparse point or unknown
entry. Shared-runtime cache/log/settings are never owned or removed.

Launcher safety invariants:

- Resolve the Steam library and game executable using documented registry/VDF
  inputs, then verify the expected executable and relative directory shape.
  A folder-picker result is subject to the same validation.
- Canonicalize and compare paths without following an attacker-controlled
  archive path outside an approved root. Reject reparse-point/junction
  traversal in paths that will be replaced or removed.
- Treat the embedded manifest as an allowlist. Extraction rejects undeclared,
  duplicate, absolute, alternate-data-stream, device, and parent-relative
  paths.
- Never infer ownership from a filename alone. Ownership records contain
  product version, manifest hash, relative path, role, size, and content hash.
- Replace or remove an owned file only when both its recorded identity and
  current hash match. A mismatch is a conflict requiring user action.
- A failed prepare never treats a partially verified staging tree as active;
  transaction recovery restores the previous verified generation when a crash
  interrupted the rename window.
- Cleanup operates on explicit validated paths and is idempotent. It never
  recursively deletes an unresolved environment-derived path.
- Normal execution performs all read-only checks before requesting elevation.
  Elevation is not requested when the two game-directory files already match.
- The elevated execution path accepts only the narrow managed proxy/override
  prepare/remove operation, revalidates all inputs, and cannot be used as a
  general file-copy primitive.
- Among files directly added to the game executable directory,
  MecchaCamouflage owns at most `dwmapi.dll` and `override.txt`.
- Launch occurs only after the selected managed or shared loader identity
  satisfies the accepted compatibility contract.

### Existing UE4SS policy

Follow UE4SS’s supported external-root mechanism through `override.txt`. The official lookup behavior is documented in the [UE4SS installation guide](https://docs.ue4ss.com/dev/installation-guide.html).

The resolver uses separate identities rather than requiring an unrelated shared
tree to match the product payload manifest:

- **Loader identity**: effective command-line override, proxy path/hash,
  override/conventional resolution, and the actual loaded `UE4SS.dll`.
- **Runtime compatibility identity**: UE4SS binary hash, pinned commit/ABI, and
  required configuration fields. Unrelated compatible settings may differ only
  where explicitly reviewed.
- **Meccha payload identity**: the exact mod DLL, enabled marker, profiles,
  localization, fonts, licenses, and product-owned settings.
- **Unrelated shared content**: other mods, logs, caches, and non-conflicting
  settings. These are observed only as needed for compatibility and are never
  product-owned or manifest-enumerated.

Deployment modes:

1. **Managed runtime**
   - Allowed when there is no existing loader chain or the existing game-folder
     files are exact Meccha-owned files from a prior prepare.
   - Deploy/reuse the pinned proxy, owned `override.txt`, and managed
     LocalAppData runtime.
   - An exact unowned proxy may be reused without taking removal ownership only
     when the remaining effective chain is otherwise absent and the owned
     override unambiguously wins.
   - `--remove` removes hash-matched owned files and the verified managed cache;
     it leaves a reused unowned proxy.
2. **Compatible shared runtime**
   - Allowed only when the already effective loader chain resolves to the exact
     pinned UE4SS binary/ABI and compatible required settings.
   - Do not change the proxy, `override.txt`, `UE4SS.dll`, shared settings,
     caches, logs, or unrelated mods.
   - Add MecchaCamouflage only under its own absent or exact-match mod directory.
   - `--remove` removes only hash-matched Meccha-owned mod files; it never
     removes the shared loader/runtime.
3. **Conflict**
   - Any unknown proxy, higher-priority `--ue4ss-path`, unresolved Steam launch
     option, different UE4SS binary/ABI, incompatible required setting,
     different Meccha mod, or ambiguous effective path causes a no-change
     failure naming the path and available hash/identity.

The launcher must account for UE4SS resolution priority. It must not write an
`override.txt` that is silently superseded by an unverified `--ue4ss-path`.
Never overwrite or remove an unknown `dwmapi.dll`, runtime, settings file, mod,
override, launch option, or generated shared file.

### Build and payload

Build UE4SS and `main.dll` from the same pinned source graph, compiler, architecture, Release configuration, and MSVC runtime. Never package a UE4SS binary different from the one against which the mod was linked.

The planned root CMake graph exposes at least these project targets:

```text
meccha_core
meccha_application
meccha_mod
meccha_launcher_core
meccha_launcher
meccha_payload_tool
meccha_core_tests
meccha_application_tests
meccha_launcher_tests
meccha_runtime_contract_tests
```

Project-owned targets use strict warnings and reproducible Release settings.
Third-party warnings do not get hidden by weakening warnings for project code.
The exact compiler version, Windows SDK, CMake generator, architecture, runtime
library selection, UE4SS options, recursive dependency revisions, and build
configuration are emitted into a build provenance record.

UE4SS’s restricted UEPseudo dependency means:

- Default and fork checkout initializes only public dependencies. It does not
  recursively request the restricted UE4SS dependency graph.
- Automatic fork/PR CI runs with no secrets and uses
  `MECCHA_WITH_UE4SS=OFF`. Initial, reopened, code-bearing, and unclassifiable
  updates build/test the pure core, application, launcher, payload tooling, and
  fake runtime adapter. A synchronized update limited to Markdown and the exact
  latest-v1 checkpoint manifest may rerun policy contracts under one stable
  aggregate gate without rebuilding unchanged binaries only when the previous
  head already has that aggregate gate's successful evidence. Missing prior
  evidence, missing/empty ranges, and manual runs fail closed to the heavy
  graph. It does not claim that the real mod ABI compiled.
- Every v2 workflow runs on `windows-2022`. Normal MSVC secret-free tests are
  the required compiler signal for code-bearing pull requests. Manual deep
  validation and every release-candidate run additionally require MSVC
  AddressSanitizer, clang-cl UndefinedBehaviorSanitizer, and MSVC `/analyze`
  over the production target closure. No non-Windows job is official v2
  development, review, phase-exit, or release evidence.
- Leak detection is not an acceptance criterion because the supported Windows
  toolchain has no equivalent LeakSanitizer coverage. This does not weaken the
  mandatory MSVC AddressSanitizer or clang-cl UndefinedBehaviorSanitizer gates.
- A full recursive build is required before merge, but it runs only from an
  explicitly maintainer-approved commit on a protected repository ref and
  behind an approved CI environment. Do not use `pull_request_target` or expose
  credentials automatically to fork code.
- The full-build credential is short-lived/read-only and is available only for
  dependency checkout. The workflow contains no step that uploads restricted
  source. Build logs and artifacts are reviewed/redacted so PR-controlled
  output cannot publish restricted dependency contents.
- Trusted main/release CI performs the complete recursive source build using a
  maintainer credential with the required Epic organization access.
- Contributor documentation explains the Epic-linked GitHub prerequisite for a complete local UE4SS build.
- Release artifacts always come from the trusted full-build of the exact
  pre-approved merge commit.
- The accepted UE4SS gitlink and its initialized nested checkouts remain
  byte-for-byte unmodified. The explicitly approved reproducibility exception
  is one project-owned canonical
  `deps/first/patternsleuth_bind/Cargo.lock` overlay, applied only to an
  independent build-only clone of the exact accepted graph. The policy pins
  the upstream lock hash, overlay hash, resulting Git binary-diff hash, root
  commit, and nested commits.
- Trusted builds generate that stage atomically, require it through explicit
  CMake source-root/manifest inputs, keep Cargo `--locked`, and verify the
  one-file diff before configure, after build, and during provenance and
  dependency-evidence collection. The accepted gitlink is never a build
  mutation target.
- Any second overlay, change to the approved lock bytes or hashes, different
  staged diff, or other UE4SS source patch stops the compatibility gate for a
  new architecture review.

The Windows build pipeline will:

1. Verify the pristine pinned UE4SS graph, atomically prepare the approved
   manifest-bound build-only source stage, and configure the root CMake graph
   against that stage.
2. Build UE4SS, the C++ mod, native launcher, payload tooling, and tests.
3. Run unit and contract tests.
4. Assemble the exact minimal runtime tree.
5. Disable UE4SS debug UI, consoles, research tools, and unused bundled mods through reviewed release configuration.
6. Generate a canonical manifest containing schema, product version, UE4SS commit, relative path, role, size, and SHA-256 for every file.
7. Compress the runtime tree into a CAB payload using Windows tooling.
8. Embed the CAB, manifest, profiles, localization, project license, UE4SS MIT license, and all dependency notices as Win32 resources.
9. Produce one `meccha-camouflage-vX.Y.Z.exe`.
10. Publish its SHA-256 alongside the GitHub Release.

Remove from the final v2 tree:

- All C# projects and tests.
- WebView HTML, CSS, and JavaScript.
- WebView2 bootstrapper logic.
- BridgeClient, HostSession, RuntimeBridgeService, bootstrap ABI, TCP, JSON command handling, sidecars, and bridge state directories.
- Custom injector and `BridgeStartV1`.
- Custom reflection scanners replaced by UE4SS.
- MinHook and custom D3D rendering.
- Defender exclusion service.
- Production research/probe commands.
- Source-text tests that assert the v1 architecture.

Retain CUE4Parse only as opt-in profile-generation tooling. Replace the separate UnrealMappingsDumper workflow with UE4SS’s maintained mapping facilities after equivalence is confirmed.

### Branch and commit sequence

1. Copy this approved plan into `PLAN.md` and pause.
2. Create `release/v1.x` at `v1.6.7`.
3. Create `rewrite/ue4ss-v2` from `main`.
4. Commit `PLAN.md` as the first v2 commit.
5. Open a Draft PR titled `feat!: rebuild MecchaCamouflage as an UE4SS in-game mod`.
6. Keep coherent commits for architecture gate, core, runtime, UI, Paint, Image Paint, ESP, launcher, packaging, and cleanup.
7. Delete v1 implementation files before merge rather than retaining a legacy source directory.
8. Create and test a normal merge commit on a temporary integration ref, then
   fast-forward `main` to that exact commit after approval; do not squash away
   the rewrite history or let a merge UI create a different untested commit.
9. Tag the accepted, tested merge commit as `v2.0.0`.

## 4. Detailed Phase Plan

### Phase dependency and gate model

```text
Phase 0  Repository preservation and rewrite governance
    ↓
Phase 1  v1 behavioral baseline and requirements traceability
    ↓
Phase 2  C++ build graph and pinned UE4SS source gate
    ↓
Phase 3  Safe isolated deployment and loading gate
    ↓
Phase 4  Runtime lifecycle, game-thread, and teardown gate
    ↓
Phase 5  UCanvas UI/input/texture feasibility gate
    ↓
Phase 6  Pure core and application state machines
    ├──────────────→ Phase 7  Persistence ───────┐
    ├──────────────→ Phase 8  Paint ─→ Phase 9  Image Paint
    └──────────────→ Phase 10 ESP ───────────────┤
Phase 7 + Phase 8 + Phase 9 + Phase 10 ──────────┘
                          ↓
Phase 11 Full product UI and integration
                          ↓
Phase 12 Launcher, payload, and CI hardening
                          ↓
Phase 13 Feature-complete stabilization and release matrix
                          ↓
Phase 14 Legacy deletion, final candidate, merge, and release
```

Phases 2 through 5 form the architecture gate. Each phase closes its own
sub-gate; Phase 5 closes the combined architecture viability gate. The gate has
two broader evidence closures:

- **Viability closure**: the intended runtime, teardown, Canvas, input, and
  representative game-operation paths pass on at least one supported Windows
  10/11 live host. This is required before feature runtime porting begins.
- **Portability closure**: deployment, UAC, runtime initialization, and Canvas
  behavior pass on both Windows 10 and Windows 11. This remains mandatory
  before Phase 13 can pass and before any legacy deletion or release.

This split permits deterministic core work when a second physical OS is not
immediately available. It does not convert an unrun Windows version into a pass
or weaken the release matrix.

### Phase 0 — Repository preservation and rewrite governance

**Entry criteria**

- This detailed plan is approved explicitly.
- The only intended local pre-rewrite change is `PLAN.md`.
- `v1.6.7` resolves to the accepted v1 release commit.

**Work**

1. Record the current branch, commit, tag, remotes, submodules, and clean/dirty
   status.
2. Create `release/v1.x` directly at `v1.6.7` without rewriting the tag.
3. Create `rewrite/ue4ss-v2` from the current `main`, carrying only the approved
   `PLAN.md`.
4. Commit `PLAN.md` as the first rewrite commit.
5. Open the public Draft PR with the locked title and a checklist linked to the
   phases in this document.
6. Configure the PR so `main` remains v1 until Phase 14.
7. Record baseline CI results and the exact v1 packaging inventory. Known
   pre-existing failures, if any, are documented and must not be silently
   attributed to v2.

**Automated verification**

- Branch and tag ancestry checks.
- `git diff --check`.
- Existing v1 build/test commands where the environment supports them.
- Inventory of tracked C#, web, bridge, injector, D3D/MinHook, profile, mapping,
  localization, and release files.

**Manual/external verification**

- Confirm the public branch/PR visibility and that v1 issues/releases remain
  attached to the same repository.

**Exit criteria**

- v1 has an immutable tag and maintenance branch.
- The Draft PR contains only the plan as its initial commit.
- Baseline failures and unavailable checks are visible.

**Stop conditions**

- Tag mismatch, unexpected history, unrelated dirty work, or inability to
  preserve v1 safely.
- Any repository-side policy that would force a premature merge or squash.

### Phase 1 — v1 behavioral baseline and requirements traceability

**Entry criteria**

- Phase 0 passed.
- No v2 runtime implementation has started.

**Work**

1. Create a requirement ID for every retained behavior:
   `PAINT-*`, `IMAGE-*`, `ESP-*`, `UI-*`, `PERSIST-*`, `RUNTIME-*`,
   `LAUNCH-*`, `I18N-*`, and `RELEASE-*`.
2. Map each requirement to its v1 source location, existing test/checklist,
   planned v2 owner, automated test, live test, and eventual deletion.
3. Capture the current defaults, accepted ranges, enum values, three body
   profiles, eight F1–F8 action hotkeys, F1–F24 validation, F9 panel toggle,
   all 16 locale codes, image resource limits, and the 1024×512 atlas contract.
   Capture the visible round/cube/fukuyoka guide-overlay contract separately
   from painted atlas content.
4. Classify the existing tests as:
   behavioral tests to port, useful golden-characterization tests,
   launcher/persistence safety cases to rewrite, or v1 architecture/source-text
   assertions to retire.
5. Extract deterministic fixtures for the pure algorithms in
   `runtime_contract.hpp`. Golden data must describe behavior, not copy
   implementation internals.
6. Document all reflected classes/properties/UFunctions and profile inputs
   currently required by Paint, Image Paint, and ESP.
7. Record unresolved behavior as a decision or live-observation item. Do not
   invent a v2 interpretation.
8. Mark v1 preset import as an intentional incompatibility while separately
   tracing all preset-management behaviors that v2 must preserve.

**Automated verification**

- A traceability linter ensures every locked capability has an owner and at
  least one planned verification.
- Existing deterministic tests establish the baseline for algorithm fixtures.
- Localization inventory verifies 16 catalogs and equal required key sets.

**Manual/external verification**

- Use the running v1 product only for behavior that source/tests cannot define
  unambiguously, such as exact interaction or visual semantics.
- Record observations without changing game state beyond the approved manual
  checklist.

**Exit criteria**

- The traceability table has no unclassified user-facing behavior.
- Every planned v1 deletion has a replacement requirement or explicit approved
  removal.
- Open questions that could alter architecture are resolved or block their
  dependent phase.

**Stop conditions**

- A supposedly removable v1 path is found to own required product behavior.
- Paint replication or preview behavior cannot be characterized safely.

### Phase 2 — C++ build graph and pinned UE4SS source gate

**Entry criteria**

- Phase 1 passed.
- The UE4SS candidate remains unmodified.

**Work**

1. Add the root CMake graph and empty production targets with enforced
   dependency boundaries.
2. Add `third_party/RE-UE4SS` as an exact recursive gitlink at the candidate
   commit and record the complete dependency graph.
3. Confirm the candidate's license obligations, build prerequisites,
   UEPseudo/Epic access requirements, supported compiler/SDK, configuration
   options, proxy layout, external-root behavior, and C++ mod ABI.
4. Build UE4SS and a minimal project-owned C++ mod from the same graph and
   configuration on trusted Windows CI/local infrastructure.
5. Export only `start_mod` and `uninstall_mod`; inspect imports/exports and
   runtime-library consistency.
6. Establish fork-safe CI for core, launcher, manifest, and fake-runtime targets
   without private dependencies or maintainer secrets.
7. Emit build provenance and dependency/license inventories.
8. Keep the accepted UE4SS gitlink pristine. Apply only the explicitly
   approved, manifest-bound canonical Cargo lock to an independent build-only
   clone; reject any other staged diff before and after the build.

**Automated verification**

- Clean and incremental x64 Release builds from the exact verified source
  stage.
- Recursive submodule revision check.
- Upstream lock, canonical lock, staged binary-diff, and stage-manifest hash
  checks before configure, after build, and in dependency evidence.
- ABI/import/export inspection.
- Project include-boundary checks.
- License/notices inventory.
- A reproducibility comparison for canonical manifest inputs.

**Manual/external verification**

- A maintainer with the required Epic-linked GitHub access confirms a complete
  clean source build.

**Exit criteria**

- One exact accepted graph plus the single approved canonical lock identity
  produces the UE4SS runtime and matching mod without mutating the gitlink.
- Contributor and trusted-CI build responsibilities are documented.
- The candidate remains a candidate until Phases 3–5 pass live.

**Stop conditions**

- Required source is inaccessible to trusted release CI.
- The mod and packaged runtime cannot be built with a consistent ABI.
- An unreviewed additional UE4SS patch, binary substitution, or license-incompatible
  dependency would be required.

### Phase 3 — Safe isolated deployment and loading gate

**Entry criteria**

- Phase 2 passed.
- The game is stopped before any proxy/runtime mutation.

**Work**

1. Implement the project-owned manifest parser, hashing, ownership model,
   journaled staging/promotion/recovery, cleanup, and conflict decisions with no
   game dependency.
2. Implement Steam App ID `4704690` discovery and strict folder-picker
   validation.
3. Resolve the effective loader chain, including higher-priority Steam
   `--ue4ss-path` arguments, and classify managed/shared/conflict mode before
   mutation.
4. Implement running-game rejection before modification.
5. Prepare the candidate UE4SS runtime in the stable LocalAppData `active`
   directory through the journaled transaction.
6. In managed mode, deploy only the exact proxy and `override.txt` when safe.
   In exact shared mode, leave the loader/runtime untouched and add only the
   project-owned mod.
7. Implement the original-user unelevated preparation and minimal elevated
   proxy/override broker without deriving LocalAppData from the elevated
   account.
8. Prove no-op reuse, repair, conflict refusal, interrupted staging/rename
   recovery, shared-mode isolation, and hash-safe removal.
9. Launch through Steam only after preparation succeeds.
10. Keep all gate tooling on the production manifest/ownership path. Any
   extra diagnostics are excluded from the release configuration.

**Automated verification**

- Temporary fake game-tree tests for clean, existing exact-match, corrupt,
  unknown proxy, incompatible UE4SS, mismatched Meccha mod, interrupted staging,
  interrupted rename/journal recovery, shared/managed classification,
  higher-priority path override, reparse/path traversal, alternate-credential
  elevation-decision, update, and removal cases.
- Repeated preparation produces one `active` generation.
- Mutation-set assertions prove only allowlisted files change.

**Manual/external verification**

- On a supported Windows host: clean prepare, Steam launch, runtime discovery,
  second no-op prepare, conflict display, and removal.
- Exercise writable and access-denied game directories. Observe that UAC is
  requested only for the exact managed proxy/override change and still points
  to the invoking user's verified runtime when alternate administrator
  credentials are used.
- Exercise exact compatible shared UE4SS and verify that its override, settings,
  runtime, unrelated mods, logs, and caches do not change.
- The currently running game, if any, must first be closed; this phase does not
  attach to or retrofit an already running process.

**Exit criteria**

- The Phase 3 deployment sub-gate proves UE4SS initializes through the managed
  isolated root on one supported live host without a custom injector and
  proves exact shared-mode resolution independently.
- Automated ownership/conflict tests pass.
- Windows 10 and 11 evidence is either complete or explicitly remains a
  portability blocker for Phase 13.

**Stop conditions**

- Isolated loading requires more than the reviewed proxy/override files.
- Unknown files must be overwritten, elevation scope cannot be constrained, or
  repeated preparation accumulates runtime generations.
- The only working path reintroduces direct injection.

### Phase 4 — Runtime lifecycle, game-thread, and teardown gate

**Entry criteria**

- The Phase 3 deployment sub-gate passed.
- The exact payload manifest and UE4SS candidate are recorded.

**Work**

1. Implement the composition root, runtime state machine, callback registry,
   in-flight callback barrier, bounded game-thread scheduler, and bounded
   diagnostics.
2. Register from `on_unreal_init()` and keep `on_update()` free of UObject and
   ProcessEvent access.
3. Resolve World, controller, HUD, and Canvas through validated runtime
   contracts and generation-checked bindings.
4. Rebind after lobby/match changes, travel, HUD replacement, controller/pawn
   replacement, freecam, and spectator transitions.
5. Implement the smallest retained production-path typed adapter operations
   needed to execute one controlled representative Paint call and one
   representative Image Paint/texture operation from the game-thread
   scheduler. Validate the exact parameter contracts and prove off-thread
   attempts are rejected; do not implement an alternate diagnostic route.
6. During explicit unload, reject new product commands, cancel workers, restore
   preview/input leases and drain required game-thread work while the callback
   remains active, then make callbacks inert, unregister exact callback IDs,
   drain in-flight callbacks, destroy state, and unload repeatedly.
7. Add debug-only thread-affinity assertions and fake-runtime tests; do not add
   permissive production recovery.

**Automated verification**

- Lifecycle/state-machine tests including init failure at every stage.
- Scheduler ordering, budget, cancellation generation, starvation, and shutdown
  tests.
- Fake-runtime proof that UObject operations are rejected off game thread.
- Concurrent callback/uninstall stress tests without a live game.
- Binary export and forbidden-import checks.

**Manual/external verification**

- Repeated load/unload and game shutdown.
- Lobby, match, travel, HUD replacement, controller/pawn replacement, freecam,
  and spectator transitions.
- In a maintainer-approved disposable test state, execute the representative
  Paint and Image Paint/texture operations and verify their diagnostics identify
  the HUD scheduler's game thread.
- Logs must show callback registration generations, invalidation/rebind, and
  zero in-flight callbacks at unload.

**Exit criteria**

- Safe callback teardown and game-thread ownership pass live.
- No UObject operation originates from `on_update()` or a worker.
- Runtime failures are actionable and fail closed.

**Stop conditions**

- Callback IDs cannot be reliably unregistered.
- In-flight callbacks cannot be drained before DLL teardown.
- Required lifecycle behavior needs a custom ProcessEvent-vtable or graphics
  hook.

### Phase 5 — UCanvas UI, input, text, and texture feasibility gate

**Entry criteria**

- Phase 4 passed.
- A valid frame-scoped Canvas callback is available.

**Work**

1. Implement the retained immediate-mode UI foundation: layout, IDs, focus,
   hit-testing, clipping, scroll containers, buttons, toggles, sliders, color
   controls, text fields where required, and diagnostics text.
2. Draw representative line, box, text, and image-texture content through
   UCanvas only.
3. Implement pointer/key routing and the exact input lease that captures and
   restores cursor, look, movement, and prior input mode.
4. Validate viewport/DPI scaling and safe areas.
5. Resolve the game font and packaged OFL fallback glyph atlas.
6. Render representative ESP primitives while the panel is closed.
7. Create and release a representative transient texture through validated
   reflected APIs.
8. Build an end-to-end retained Image Paint editor vertical slice with native
   file selection/cancel, at least two layers, selection, ordering, drag,
   resize, crop, clipping/scrolling, one body-specific guide overlay, worker
   composition, and game-thread texture refresh. The guide must remain above
   layers and absent from the canonical atlas.
9. Ensure test controls evolve into the production UI foundation; remove or
   release-exclude gate-only controls before exit.

**Automated verification**

- Layout, clipping, hit-testing, focus, command emission, and input-lease state
  tests.
- Localization key parity and glyph coverage for all 16 catalogs.
- Texture dimension/format/lifetime validation through the fake adapter.
- Editor selection/order/drag/resize/crop, guide-overlay separation,
  composition cancellation, stale-result rejection, and texture-refresh tests.
- Source/import checks proving no WebView, debug-tab product UI, ImGui, DXGI,
  D3D, Present, or MinHook path exists.

**Manual/external verification**

- Open/close the F9 panel repeatedly and verify exact input restoration.
- Mouse interaction, clipping, text, non-ASCII glyphs, image texture, and
  representative ESP primitives at common resolution/DPI combinations.
- Complete the retained two-layer editor vertical slice through the native file
  picker and verify guide visibility, layer ordering, crop/resize interaction,
  worker refresh, and atlas exclusion.
- Lobby, match, travel, HUD replacement, freecam, and spectator transitions.

**Exit criteria**

- Canvas supports the required product interaction, text, clipping, and texture
  behavior without an external control window.
- ESP drawing persists while the panel is closed.
- Input restoration and transient texture cleanup pass live.
- The decisive multi-layer editor vertical slice is usable enough to justify
  completing the editor on the same Canvas foundation.
- The accepted UE4SS commit is frozen only after Phases 3–5 viability closure.

**Stop conditions**

- Canvas cannot provide a usable editor or reliable text/texture rendering.
- Input state cannot be restored exactly.
- Canvas disappears across required lifecycle transitions.

Failure stops for architecture review. The only candidate fallback is reviewed
packaged UMG; no fallback implementation starts without explicit plan approval.

### Phase 6 — Pure core and application state machines

**Entry criteria**

- Architecture viability closure passed.
- Phase 1 fixtures and traceability are complete.

**Work**

1. Port value types, validation, region routing, profile identities, color/PBR
   packing, image atlas mapping, paint pacing, compression/coalescing, ESP
   projection helpers, and timing estimates into focused pure C++ modules.
2. Implement typed commands, immutable snapshots, the shared job arbiter,
   cancellation, preview lease, queue-pressure reporting, and shutdown
   coordination.
3. Preserve behavior with differential/golden tests while removing accidental
   dependencies on the monolithic `bridge.cpp`.
4. Model clock, worker execution, filesystem, and runtime boundaries explicitly
   for deterministic tests.
5. Add resource limits and checked arithmetic for image sizes, stroke counts,
   queue sizes, durations, and serialization lengths.

**Automated verification**

- Pure unit/property/boundary tests for every ported algorithm.
- Golden comparison to characterized v1 behavior.
- Command/state-machine tests for invalid transitions, mutual exclusion,
  preview ownership, late worker results, cancellation, restore, and shutdown.
- Dependency checks proving no UE4SS, Unreal, Windows UI, or launcher headers in
  `core`.
- Windows MSVC AddressSanitizer and clang-cl UndefinedBehaviorSanitizer over
  the complete registered secret-free graph, plus MSVC `/analyze` over the
  production target closure. All diagnostics are release-blocking.

**Manual/external verification**

- None required for pure logic. Visual or game-dependent interpretations remain
  mapped to their feature phase.

**Exit criteria**

- Every core requirement has deterministic coverage.
- No feature module needs to copy v1 transport, reflection, or global state.
- All public core inputs validate before allocation or arithmetic.

**Stop conditions**

- Golden behavior exposes an unresolved v1 product ambiguity.
- A supposedly pure algorithm requires live UObject state; redesign the input
  as an immutable captured value before continuing.

### Phase 7 — Configuration, localization, and Image Paint persistence

**Entry criteria**

- Phase 6 application contracts are stable.

**Work**

1. Specify the exact v2 config schema, defaults, ranges, enum values, hotkey
   rules, active project/draft reference, and error behavior. Keep all image
   bytes and the layer collection out of `config.json`.
2. Implement atomic config load/save under the v2 root without reading v1.
3. Port all 16 localization catalogs to external project resources, define the
   English fallback policy, and verify placeholders.
4. Select and document the v2 `.mcpreset` container encoding and canonical
   manifest before writing its decoder.
5. Implement bounded, hash-validated project save/load/rename/delete and
   content-addressed source images, plus debounced atomic active-draft
   persistence.
6. Test corruption, interrupted writes, hostile paths, size limits, duplicate
   entries, unsupported schemas/codecs, and atlas/source inconsistency.
7. Keep persistence independent of UCanvas and Unreal lifetime.

**Automated verification**

- Full schema range/default/round-trip tests.
- Duplicate F1–F24 hotkey rejection including the F9 UI toggle.
- Atomic-replacement fault injection at every filesystem step.
- All locale key, placeholder, encoding, and glyph-coverage tests.
- Preset determinism, corruption, archive safety, and resource-limit tests.
- Recovery tests for project-published/config-reference-write-failed, missing
  active project, and interrupted active-draft replacement.
- Tests proving v1 data roots are ignored and recognized v1 presets are rejected
  with the explicit non-destructive legacy-format result.

**Manual/external verification**

- Native file-picker selection and cancel behavior on Windows.
- Inspect localized filenames and error dialogs with non-ASCII paths.

**Exit criteria**

- Invalid data never partially publishes state.
- All 16 catalogs and the fallback glyph coverage pass.
- v2 persistence is fully specified, tested, and isolated from v1.

**Stop conditions**

- The chosen native container/codec adds an unacceptable license or binary
  dependency.
- Atomic replacement cannot preserve the previous valid state.

### Phase 8 — Paint

**Entry criteria**

- Phases 4 and 6 passed.
- Paint runtime contracts are present in the compatibility table.

**Work**

1. Implement typed game-thread capture for the active paint component, mesh
   identity/profile, source colors/material values, and preview channels.
2. Validate round, cube, and fukuyoka profiles before planning.
3. Build immutable worker inputs and port Paint/Fill/Skip routing, always-on
   hidden-environment projection, fixed four-texel correction calibration,
   dual-evidence physical Emissive, manual PBR floors, independent Fill PBR,
   coverage-preserving compression, and fill-first/paint-overwrite ordering.
   Delete the retired Auto Material, scene-lighting, SPSA, and cluster-fit
   settings and execution branches rather than retaining a compatibility mode.
4. Implement preview capture, apply, ownership, repeated-restore guard, exact
   restore, invalidation, and shutdown restoration.
5. Implement bounded per-frame dispatch through game-owned
   `PaintAtUVWithBrush` only.
6. Report planning, pass, submission, queue-pressure, drain, elapsed, ETA,
   cancellation, and terminal state accurately.
7. Continue a valid captured job through freecam/controller-pawn changes while
   failing safely on invalid component/world transitions.

**Automated verification**

- Profile, region, appearance, compression, ordering, pacing, queue-drain,
  cancellation, progress, and preview-lease tests.
- Fake-runtime verification of game-thread-only capture/dispatch/restore.
- Contract failure tests for every reflected type/layout mismatch.
- Stress tests for large plans and bounded per-frame work.

**Manual/external verification**

- Preview Paint and Fill, exact restore, repeated restore warning, projected
  environment appearance, manual PBR/Emissive floors, all region combinations,
  cancellation during capture/planning/dispatch, progress/backpressure,
  adaptive local pacing, and travel/freecam behavior.
- Multi-client host-painter and joining-client-painter runs with visible remote
  completion and both queues drained.

**Exit criteria**

- All automated Paint requirements pass.
- Single-client live behavior passes.
- Multi-client evidence is `PASS`; if unavailable, Paint remains
  `BLOCKED_EXTERNAL_VALIDATION` and blocks release but does not require a fake
  sender or transport.

**Stop conditions**

- Joining-client paint crashes/disconnects or remains incomplete after drain.
- Preview restore is not exact and repeatable.
- Production requires texture import, a custom multiplayer message, or any
  sender other than `PaintAtUVWithBrush`.

### Phase 9 — Image Paint

**Entry criteria**

- Phases 6 and 7 passed.
- The retained editor vertical slice from Phase 5 passed.
- Codec/layer/compositor/project work may proceed independently, but preview
  texture and paint-dispatch integration do not begin until Phase 8 automated
  and single-client criteria pass and the shared Paint dispatcher/preview lease
  are stable. A still-pending Phase 8 multi-client check remains a release
  blocker for both features.

**Work**

1. Implement bounded PNG/JPEG decoding through WIC and WebP through a reviewed
   pinned redistributable native decoder.
2. Implement the editable layer model: import, order, placement, resize, crop,
   seam wrap, front/back mirror, body profile, face Fill/Skip, material values,
   and project metadata.
3. Composite immutable layer snapshots off-thread into the canonical
   1024×512 RGBA atlas with deterministic output and cancellation.
4. Derive/version the round, cube, and fukuyoka guide overlays from the accepted
   profiles and expose them as editor-only content above every editable layer;
   never composite guide pixels into the atlas.
5. Create/update/release the preview texture only through the game-thread
   runtime adapter.
6. Implement round, cube, and fukuyoka atlas-to-mesh planning and reuse the
   reviewed game-owned Paint dispatch path.
7. Integrate v2 project/preset save/load/rename/delete without v1 migration.
8. Enforce per-image, total-source, decoded-dimension, atlas, layer-count, and
   worker-memory limits before allocation.

**Automated verification**

- Decoder corpus tests, malformed/truncated input, decompression-bomb/resource
  limits, and codec license inventory.
- Layer transform, crop, fit/fill placement, ordering, wrap, mirror, alpha,
  background/Fill, atlas determinism, and cancellation tests.
- Three-profile mapping and Fill/Skip routing tests.
- Three-profile guide geometry/version tests proving alignment with mapping
  fixtures and byte-for-byte exclusion from the canonical atlas.
- Preset round-trip/corruption tests and source/atlas hash verification.
- Fake-runtime preview texture lifetime and game-thread-only calls.

**Manual/external verification**

- PNG, JPEG, WebP; multiple layers; drag/resize/crop/reorder; wrapping;
  mirroring; all profiles; each face Fill/Skip; save/load/rename/delete;
  preview/restore/cancel; non-ASCII paths.
- Confirm visual mapping at seams, front/back orientation, and each body type.
- Confirm the correct body guide remains visible above imported layers while no
  guide pixel appears in preview/paint output.
- Repeat representative host/joining-client paint checks.

**Exit criteria**

- Every locked editor and painting behavior has automated or recorded live
  evidence as assigned.
- Preview textures and decoded buffers have bounded, observable lifetimes.
- No browser, Base64 transport, or sidecar state is present.

**Stop conditions**

- Native Canvas interaction cannot support the required editor semantics.
- Texture creation requires an unvalidated or off-thread Unreal operation.
- A decoder cannot be redistributed or safely bounded.

### Phase 10 — ESP

**Entry criteria**

- Phases 4–6 passed.
- The UCanvas frame adapter survived the architecture gate.

**Work**

1. Capture world, roster, role, avatar, spectator, skeletal pose, name, and
   position data coherently on the game thread.
2. Convert captures into project-owned frame data and render boxes, skeletons,
   names, distances, and snaplines through UCanvas.
3. Implement all/hider/hunter filtering, role colors, screen clipping, skeleton
   selection, projection failure behavior, and distance formatting.
4. Invalidate and rebuild caches on role change, avatar replacement, spectator
   state, map travel, HUD replacement, and object lifetime changes.
5. Keep ESP independent from panel visibility and never retain UObjects between
   frames.

**Automated verification**

- Projection/clipping, viewport scaling, role/spectator filtering, skeleton
  topology selection, color, label/distance, cache generation, and invalidation
  tests.
- Fake-runtime object-expiry and travel sequences.
- Forbidden graphics-hook/import checks.

**Manual/external verification**

- Every primitive and scope, both role colors, non-ASCII names, avatar/role
  changes, spectator/freecam, lobby/match/travel, HUD replacement, and panel
  closed.

**Exit criteria**

- ESP remains stable and visible through required transitions with no custom
  graphics API hook.
- Stale or incompatible objects fail closed for the frame without crashing.

**Stop conditions**

- Reliable persistent ESP requires Present, DXGI, D3D, MinHook, or another
  unapproved render hook.
- Game-thread capture cannot remain within an agreed frame budget.

### Phase 11 — Full product UI and feature integration

**Entry criteria**

- Feature engines expose stable typed commands and snapshots.
- The Canvas UI foundation passed Phase 5.

**Work**

1. Implement Paint, Image Paint, ESP, Settings, and Diagnostics sections using
   the retained Canvas widget foundation.
2. Bind controls only to typed commands and immutable snapshots.
3. Implement the complete Image Paint editor interaction, selection, drag,
   resize, crop, order, import, body-guide overlay, project management, and
   preview controls.
4. Implement the F9 toggle and eight configurable action hotkeys, key-down
   deduplication, invalid/duplicate mapping errors, and focus/edit guards.
5. Preserve exact preview/cancel mutual exclusion and surface progress,
   backpressure, elapsed, ETA, compatibility, and bounded diagnostics.
6. Complete responsive layout, DPI scaling, clipping, fallback fonts, themes,
   localization, and persistence integration.
7. Ensure closing the panel never disables ESP or abandons an active job.

**Automated verification**

- Widget state, navigation, hit-testing, editor gesture, command mapping,
  hotkey, focus, snapshot revision, localization, clipping, and layout tests.
- State-machine integration tests for every UI action during every job/preview
  state.
- Screenshot/golden layout tests only where deterministic Canvas abstraction
  output is meaningful; they do not replace live rendering.

**Manual/external verification**

- Full task walkthrough in all 16 languages at representative resolutions and
  DPI values.
- Cursor/look/movement restoration, file picker, image editor usability,
  diagnostic readability, hotkeys, panel-close behavior, and ESP persistence.

**Exit criteria**

- The complete user-facing v2 product surface is available in-game.
- There is no persistent external window or UE4SS debug-tab dependency.
- All UI requirements are traceable to automated and/or recorded live evidence.

**Stop conditions**

- Any locked feature exists only through a diagnostic/dev control.
- A locale or required editor interaction cannot be made usable on Canvas.

### Phase 12 — Launcher, payload, and CI hardening

**Entry criteria**

- The production mod/runtime tree and dependencies are known.
- Phase 3 launcher safety primitives passed.

**Work**

1. Complete the launcher switches, Steam discovery, mutex, running-game
   rejection, effective loader-chain resolution, TaskDialog reporting,
   original-user/minimal-broker elevation path, managed/shared mode,
   transaction recovery, update, repair, and ownership-safe removal.
2. Assemble the minimal UE4SS configuration with debug UI, consoles, research
   tools, and unused mods disabled.
3. Generate the canonical manifest and deterministic CAB; embed payload,
   manifest, profiles, fonts, localization, icons, project license, UE4SS
   license, and all dependency notices.
4. Build the mod and packaged runtime from the same trusted source graph.
5. Implement Windows-only trusted full-build/release CI and secret-free fork
   PR CI. Require MSVC AddressSanitizer, clang-cl UndefinedBehaviorSanitizer,
   and MSVC `/analyze` before release-candidate assembly.
6. Add binary allowlist checks, payload inventory checks, dependency provenance,
   and release SHA-256 generation.
7. Decide and document Windows code-signing policy before release. Signing is
   not assumed available, and absence of credentials must never lead to
   Defender changes or a misleading publisher claim.

**Automated verification**

- Full temporary-tree launcher/payload suite, including hostile manifests and
  paths, every interrupted transaction state, managed/shared isolation,
  higher-priority loader paths, and alternate-credential elevation.
- CAB round-trip and canonical manifest tests.
- Exact mod/runtime ABI and provenance match.
- One-EXE artifact count and allowed PE imports/resources.
- No PDB/debug artifact or legacy WebView/bridge/injector/D3D material.
- Fork CI proves deterministic modules need no maintainer secret.
- MSVC AddressSanitizer and clang-cl UndefinedBehaviorSanitizer pass every
  registered secret-free test, and MSVC `/analyze` passes the production
  closure, before release-candidate assembly begins.

**Manual/external verification**

- Default/non-default Steam library, writable/elevation-required directory,
  TaskDialog clarity, exact-match coexistence, incompatible/unknown conflicts,
  invoking-user cache resolution under alternate administrator credentials,
  offline operation, repair, interrupted-update recovery, update, and removal.

**Exit criteria**

- Trusted CI produces exactly one releasable EXE and SHA-256.
- The EXE can prepare, reuse, repair, launch, and remove only owned files.
- All embedded files and notices are manifest-covered.

**Stop conditions**

- Release CI packages a binary not produced by the pinned source graph.
- UAC is needed during an unchanged no-op launch.
- Unknown/shared files can be overwritten or removed.

### Phase 13 — Feature-complete stabilization and release matrix

**Entry criteria**

- Phases 8–12 are engineering-complete.
- All externally blocked checks are listed with owners and environments.

**Work**

1. Freeze feature behavior and permit only evidence-driven fixes.
2. Run the complete automated suite repeatedly on clean trusted builds.
3. Establish performance/resource baselines before setting regression
   thresholds: frame callback time, planning time, dispatch budget, queue depth,
   decoded/project memory, texture lifetime, worker/thread count, log size,
   prepare time, and storage size.
4. Exercise cancellation/shutdown at every state boundary and fault-inject
   runtime, filesystem, decoder, worker, callback, and compatibility failures.
5. Complete architecture portability evidence on Windows 10 and 11.
6. Complete the full live game, UI/localization, launcher, and multiplayer
   matrix.
7. Perform 25 repeated prepare/launch cycles and repeated lobby/match/travel/
   unload sessions while checking storage, callback, thread, and object-lifetime
   stability.
8. Close every requirement and risk with evidence or block the release.

**Automated verification**

- All unit, property, contract, fake-runtime, persistence, launcher, payload,
  static-analysis, and binary tests.
- Clean-build reproducibility and manifest comparison.
- Fault-injection, stress, and resource-bound suites.
- No forbidden dependency/source/import scan findings.

**Manual/external verification**

- The complete live matrix in Section 5 on both supported Windows versions.
- Multi-client Paint and representative Image Paint in both host/joining-client
  directions.
- All languages, common resolution/DPI combinations, and lifecycle transitions.

**Exit criteria**

- No `FAIL`, `NOT_RUN`, or release-critical `BLOCKED_*` result remains.
- Performance and storage remain bounded without weakening safety behavior.
- The exact feature-complete candidate manifest is approved for cleanup.

**Stop conditions**

- Any mandatory live environment remains unavailable.
- Intermittent crash, disconnect, partial remote paint, lost restore, lost
  Canvas callback, callback-after-unload, unsafe launcher mutation, or storage
  accumulation occurs.

### Phase 14 — Legacy deletion, final candidate, merge, and release

**Entry criteria**

- Phase 13 passed against a feature-complete candidate.
- v1 is preserved at the tag and maintenance branch.

**Work**

1. Delete v1 C#, WinForms, WebView, JavaScript, bridge, injector, custom
   reflection/render hooks, Defender service, architecture-only tests, obsolete
   build stages, and unused dependencies.
2. Remove MinHook and UnrealMappingsDumper only after their replacements and
   mapping equivalence are verified. Retain CUE4Parse only in opt-in research
   tooling.
3. Remove all excluded compatibility probes, debug controls, stale resources,
   dead code, and generated outputs.
4. Update README, contributor/build/security docs, architecture docs, release
   checklist, licenses, CI, and repository layout for v2 only.
5. Review the complete Draft PR by coherent commits and verify the v1 deletion
   list against traceability.
6. Create the proposed normal merge commit on a temporary protected integration
   ref with the current v1 `main` and final rewrite head as its two parents.
   Do not move `main` yet.
7. Build the exact proposed merge commit through trusted CI and rerun the entire
   automated and live release matrix against the artifact/manifest whose
   provenance names that merge commit.
8. If any code, dependency, configuration, resource, or documentation affecting
   the build changes, discard the candidate, create a new proposed merge commit,
   and rerun its required evidence.
9. After the exact merge commit passes, fast-forward `main` to that same commit,
   mark the Draft PR merged, tag the same commit `v2.0.0`, and publish the
   already tested EXE/SHA-256 without rebuilding.

**Automated verification**

- Clean clone, recursive dependency, build, test, package, install-tree, binary,
  license, and forbidden-artifact checks.
- Searches prove no production C#, WebView, bridge, injector, Defender,
  TCP/JSON command, custom D3D/Present, or MinHook path remains.
- Proposed merge commit parentage and tree match the approved v1/rewrite heads.
- Release artifact, embedded provenance, manifest, and published SHA-256 match
  the trusted CI output from the exact merge commit.

**Manual/external verification**

- Repeat the full release matrix on the exact proposed merge commit, final
  post-cleanup artifact, and manifest.
- Verify GitHub release contents, install/remove behavior, and v1 branch/tag
  accessibility.

**Exit criteria**

- `main` contains the complete v2 rewrite with no legacy runtime tree.
- `main`, `v2.0.0`, the tested provenance commit, and the published artifact all
  identify the same normal merge commit.
- `v2.0.0` has exactly one EXE plus its published checksum.
- v1 remains available for maintenance/reference outside `main`.

**Stop conditions**

- Cleanup removes an untraced required behavior or changes the final payload
  without rerunning acceptance.
- Final artifact differs from the tested manifest.
- Any partial-feature release is proposed.

## 5. Verification and Acceptance

### Mandatory architecture gate

Evaluate the following against the pinned UE4SS candidate and the live UE 5.6
game. Phase 4/5 viability checks must pass on at least one supported host before
runtime feature porting. Windows 10 and Windows 11 portability evidence must
both pass before Phase 13 exits:

- Minimal C++ mod loads through `start_mod` and unloads safely through `uninstall_mod`.
- UE4SS initializes reliably without a custom game-process injector.
- Game-thread HUD/Canvas callback survives lobby, match, travel, HUD replacement, freecam, and spectator transitions.
- Callback IDs can be unregistered and in-flight callbacks drained safely.
- UCanvas supports panel controls, mouse interaction, localized text, image textures, and clipping.
- Representative ESP box, skeleton, name, distance, and snapline primitives render correctly.
- Representative Paint and Image Paint UFunctions execute only on the game thread.
- The isolated runtime cache, proxy override, exact-match coexistence, conflict handling, UAC path, and cleanup work on Windows 10 and 11.
- Repeated preparation does not create version/GUID directory accumulation.

Failure of Canvas UI, safe callback teardown, UE5.6 initialization, or isolated
loading stops the rewrite for plan revision. Lack of access to a required
environment records a blocker; it does not authorize reintroducing Present
hooks, an external UI, or an assumed pass.

### Automated tests

- Pure Paint planning, region routing, appearance matching, color compression, image mapping, pacing, and queue-completion tests.
- Image layer validation, compositing, crop, wrap, mirror, canonical atlas, and v2 preset corruption tests.
- ESP projection, clipping, role filtering, spectator filtering, skeleton selection, and travel invalidation tests.
- Configuration ranges, duplicate hotkeys, atomic persistence, all 16 translations, and glyph coverage tests.
- Typed command/state-machine tests for mutual exclusion, preview ownership, cancellation, and shutdown.
- Fake-runtime tests proving that feature code cannot call Unreal outside the scheduler.
- Launcher tests using temporary fake game trees for fresh preparation, no-op
  reuse, repair, every interrupted journal/rename state, original-user
  elevation, managed/shared mode, higher-priority loader resolution, update,
  exact UE4SS coexistence, unknown proxy refusal, ownership-safe removal, and
  access-denied escalation decisions.
- Payload tests for path traversal rejection, duplicate paths, manifest mismatch, hash mismatch, missing licenses, and stale-generation cleanup.
- Binary checks for x64 Release, required exports, matching UE4SS ABI/imports, allowed dependencies, no debug symbols, and no legacy WebView/bridge/injector/D3D artifacts.

### Test tiers and responsibility

| Tier | Scope | Primary runner | Release meaning |
| --- | --- | --- | --- |
| T0 Static | Formatting, include boundaries, forbidden APIs/imports, schemas, licenses | Local/CI | Required on every PR |
| T1 Unit | Pure algorithms, validation, state machines, persistence primitives | Local/CI | Required on every PR |
| T2 Contract | Golden v1 behavior, fake runtime, thread affinity, manifest/ABI | Local/trusted CI | Required before phase exit |
| T3 Isolated integration | Temporary game trees, payload CAB, fault injection, launcher decisions | Windows CI/local | Required before live preparation |
| T4 Single-client live | UE4SS lifecycle, Canvas/UI/input, Paint/Image Paint/ESP, travel/unload | Agent when access exists plus maintainer observation | Required; never inferred |
| T5 OS/installation live | Windows 10/11, Steam libraries, UAC, coexistence/conflict/removal | Maintainer/CI lab | Required before release |
| T6 Multi-client live | Host and joining-client replication and stability | Maintainer with two real clients | Required before release |

An automated coding environment is expected to complete T0–T3. It may assist
with T4 by preparing builds, exact procedures, diagnostics, and evidence
parsing. T4–T6 remain maintainer-owned whenever the environment cannot directly
control and observe the required machines. No test is replaced with a source
assertion merely because the live environment is unavailable.

"Required on every PR" means every initial, reopened, code-bearing, or
unclassifiable PR update. A synchronize range limited to non-executable
Markdown/latest-v1 checkpoint metadata may reuse the unchanged binary evidence
only when the previous head's stable aggregate gate already passed and the new
head's policy contracts and aggregate gate pass.

### Live-session protocol

For each live session:

1. Identify the exact product commit, payload manifest hash, UE4SS commit, game
   version, OS version, GPU/API if relevant, Steam library location, and
   existing proxy/UE4SS state.
2. Stop the game before preparation. Do not modify a loaded runtime.
3. Run `--prepare-only` first and retain its bounded log.
4. Launch normally and perform only the checklist items assigned to the
   session.
5. Capture diagnostic IDs and concise screenshots/video where visual evidence
   is required.
6. Test explicit live mod unload through the pinned UE4SS-supported mechanism
   before the normal game-exit case. If no safe supported mechanism invokes and
   completes `uninstall_mod`, the architecture teardown gate fails rather than
   being marked not applicable.
7. Inspect callbacks, worker/job state, file ownership, storage generations,
   and crash logs.
8. Record `PASS`, `FAIL`, or the exact blocker. A retry never erases the failed
   run; the fix commit and new manifest identify the later result.

### Live release matrix

Run the maintained release checklist on Windows 10 and 11 with:

- Default and non-default Steam libraries.
- Writable and elevation-required game directories.
- Clean install, current cache, corrupt cache, interrupted transaction, managed
  install, existing exact shared UE4SS, incompatible UE4SS, unknown proxy, and
  higher-priority `--ue4ss-path`.
- Writable/elevated runs under both same-account consent and alternate
  administrator credentials without changing the original runtime root.
- Twenty-five repeated managed launch/prepare cycles with one active runtime
  directory only after each successful transaction.
- Lobby, active match, map travel, freecam, spectator mode, role changes, and shutdown.
- Paint preview, restore, repeated restore guard, Fill/Paint/Skip combinations,
  environment projection/correction, dual-evidence Emissive, cancellation,
  adaptive pacing, progress, and backpressure.
- Image Paint PNG/JPEG/WebP, multiple layers, crop, resize, reorder, wrap,
  mirror, all three body types and guide overlays, Fill/Skip faces, save/load,
  preview, restore, and cancellation.
- ESP every primitive, each scope, both role colors, names with non-ASCII text, role changes, and avatar replacement.
- Every shipped language at common resolutions and DPI settings.
- Host and joining-client multiplayer paint visibility, completion, replication queue drain, and crash/disconnect checks.

Do not release if joining-client paint crashes, remote paint remains incomplete after queue drain, preview restoration is unreliable, Canvas rendering disappears after travel, callbacks survive unload, the launcher overwrites unknown files, or storage grows across unchanged launches.

### Final definition of done

The rewrite is done only when all of the following are true on the exact tested
normal merge commit that will become `main` and `v2.0.0`:

- Every locked requirement is linked to passing evidence.
- All automated tests pass from a clean checkout.
- The trusted build provenance and payload manifest identify one exact UE4SS
  source graph and matching mod.
- Windows 10, Windows 11, single-client, and multi-client required checks pass.
- Paint, Image Paint, and ESP are all complete; none is a preview, beta-only, or
  hidden developer feature.
- Safe restore, cancellation, queue drain, travel/rebind, and unload pass.
- The launcher makes no unknown-file or security-configuration changes.
- The release contains one EXE and a published matching SHA-256.
- The repository contains no v1 runtime implementation or unapproved fallback,
  while v1 remains preserved at its tag and maintenance branch.

## 6. Risk Register and Decision Points

| Risk | Earliest proof | Required response if unresolved |
| --- | --- | --- |
| UE4SS candidate cannot build reproducibly with accessible dependencies | Phase 2 | Stop; select/review a different exact commit or distribution policy |
| Restricted recursive dependencies cannot be built securely before merge | Phase 2 | Block merge; use only reviewed protected-ref full builds without exposing credentials/source |
| UE4SS ABI or callback teardown is unsafe | Phase 4 | Stop; no feature port and no hot-unload claim |
| UCanvas cannot support required UI/textures/input | Phase 5 | Stop for explicit UMG architecture review |
| Reflected game contracts changed or remain ambiguous | Phases 1 and 4 | Fail closed; update compatibility data with live evidence |
| Image editor is unusable or exceeds frame/memory budgets | Phases 5, 9, and 11 | Redesign within Canvas/approved UMG; do not restore WebView |
| Multiplayer paint is incomplete or unstable | Phase 8 | Block release; investigate game-owned dispatch/pacing only |
| Existing proxy/UE4SS coexistence cannot be ownership-safe | Phases 3 and 12 | Refuse conflicting installs; do not overwrite |
| Managed runtime transaction cannot recover every rename/crash state | Phases 3 and 12 | Block launcher; retain stable path but redesign the journal/rollback protocol |
| Effective higher-priority UE4SS path cannot be resolved unambiguously | Phase 3 | Treat as conflict; do not rely on `override.txt` |
| UAC path grants broader mutation than required | Phases 3 and 12 | Stop launcher release and narrow the elevated protocol |
| Windows 10 or second-client hardware is unavailable | Phase 13 | Record external blocker; no release |
| Unsigned executable triggers trust friction | Phase 12 | Document/sign if credentials exist; never alter Defender |
| Mapping/profile replacement is not equivalent | Phases 1, 8, and 9 | Retain research tooling until equivalence is proven |
| Legacy deletion removes hidden required behavior | Phase 14 | Restore from rewrite history, add traceability/test, rerun acceptance |

Architecture decision records require explicit review for:

- Changing the UE4SS commit or applying any UE4SS patch.
- Selecting packaged UMG instead of UCanvas.
- Adding or replacing a native image/archive/font dependency.
- Changing the proxy or external-root mechanism.
- Changing the game-owned multiplayer paint route.
- Weakening any ownership, compatibility, thread, teardown, or persistence
  invariant.
- Changing the supported OS/game/architecture, language count, data limits, or
  no-v1-migration decision.
- Adding runtime network access, auto-update, a resident service, or more than
  one distributed artifact.

## 7. Assumptions and Delivery Gate

- Supported product target: Steam App ID `4704690`, Windows x64, Windows 10/11, current UE 5.6 game build.
- The v2 release does not support v1 settings, active Image Paint state, or v1 `.mcpreset` files.
- The current 16 languages remain supported.
- UE4SS is pinned and bundled; users are not expected to understand or install UE4SS manually.
- The distributed artifact remains one EXE, while the running system necessarily contains an extracted UE4SS DLL, mod DLL, proxy, settings, profiles, and licenses.
- In managed mode, the owned proxy and override remain in the game directory
  until `--remove`, and the managed runtime retains one `active` generation
  after a successful transaction. In compatible shared mode, the existing
  loader remains untouched and only the Meccha mod remains until `--remove`.
- No network access occurs at runtime.
- No partial v2 release is permitted.
- The currently running game is not modified or treated as a prepared UE4SS
  session during planning.
- The next repository mutation after this detailed planning turn requires
  explicit approval to begin Phase 0.
- This turn is limited to updating `PLAN.md` in English and setting the
  persistent rewrite goal. Work stops immediately afterward for architectural
  review.
