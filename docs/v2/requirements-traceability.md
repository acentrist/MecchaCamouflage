# MecchaCamouflage v2 Requirements Traceability

This document maps the accepted v1 product behavior to the v2 architecture and
required evidence. It is subordinate to [`PLAN.md`](../../PLAN.md) and uses the
test tiers defined there.

## Source aliases

- `Models`: `src/csharp/MecchaCamouflage.Core/Models.cs`
- `Settings`: `src/csharp/MecchaCamouflage.Core/SettingsStore.cs`
- `Presets`: `src/csharp/MecchaCamouflage.Core/ImagePresetStore.cs`
- `Projects`: `src/csharp/MecchaCamouflage.Core/ImageDesignLibrary.cs`
- `Locales`: `src/csharp/MecchaCamouflage.Core/Localization/LocalizationCatalog.cs`
- `Session`: `src/csharp/MecchaCamouflage.Controller/HostSession.cs`
- `UI contracts`: `src/csharp/MecchaCamouflage.Controller/UiContracts.cs`
- `Web UI`: `src/csharp/MecchaCamouflage.WebHost/web/app.js`
- `Web host`: `src/csharp/MecchaCamouflage.WebHost/MainForm.cs`
- `Bridge`: `src/native/bridge/bridge.cpp`
- `Native contracts`: `src/native/include/runtime_contract.hpp`
- `SDK`: `src/native/include/sdk.hpp`

Evidence tiers are `T0` static, `T1` unit, `T2` contract, `T3` isolated
integration, `T4` single-client live, `T5` OS/installation live, and `T6`
multi-client live.

## Traceability interpretation

Each retained row is complete only when read with
[`test-migration.md`](test-migration.md):

- `v1 authority` identifies the owning source and any concise existing
  test/checklist evidence. The migration ledger maps all 175 exact v1 tests to
  the retained requirement or approved retirement.
- `Required evidence` is split by tier: T0-T3 are automated/static evidence;
  T4-T6 are live evidence. Pure domain and file-format contracts do not require
  a live tier. Runtime, visual, OS, and multiplayer contracts do.
- `v2 owner` is the only production module allowed to own the behavior.
- `Replaces/deletes` is the eventual deletion target. A deletion remains
  blocked until the retained requirement has its required evidence.

The Phase 1 verifier rejects missing owners, evidence tiers, requirement
sequence gaps, unclassified tests, fixture/profile drift, or localization
inventory drift.

## Frozen defaults, enums, and limits

| Domain | Frozen v2 contract |
| --- | --- |
| Paint | Brush `5.0` in `[1.0,10.0]`; modes Paint/Fill/Skip; Front=Skip, Side=Paint, Back=Paint; normal Paint source is always `environment_capture`; correction lattice is fixed at `4.0` texels; Paint M/R/E floors=`0/1/0`; Fill `#FFFFFF`, M/R/E=`1/0/0`; compression `5.0` in `[0.0,10.0]`. No Auto Material, scene-lighting, or source-UV tuning setting exists. |
| Image Paint | Disabled until a valid project is active; body `round` from round/cube/fukuyoka; alpha `skip`; Front/Right/Back/Left base modes all Skip from Fill/Skip; placement `fit`; brush `5.0` in `[1.0,10.0]`; compression `0.0` in `[0.0,10.0]`; image M/R/E=`0/1/0`; Fill `#FFFFFF`, M/R/E=`1/0/0`; each new layer center=`0.5,0.5`, size=`1.0,1.0`, full normalized crop, wrap off, mirror off. |
| Image resources | PNG/JPEG/WebP; `1..12 MiB` decoded source bytes per layer; at most `64 MiB` source bytes per project; exact 1024×512 RGBA canonical atlas; checked arithmetic and decoder pixel/dimension bounds are additionally required in Phase 9. |
| ESP | Enabled; scope `all` from all/hider/hunter; boxes, skeletons, names, distance, and snaplines enabled; hider `#00FF88`; hunter `#FF0000`. |
| Input | F9 panel; Paint F1/F2/F3/F4 = Start/Preview/Restore/Cancel; Image F5/F6/F7/F8 = Start/Preview/Restore/Cancel; every mapping is F1-F24, all nine mappings are unique, and repeat is suppressed until key-up. |
| UI | Language is the supported Windows language or English fallback; explicit scale multiplier defaults `1.0` and validates `[0.75,2.0]` before viewport/DPI scaling; theme color defaults `#FFFFFF`. No external-window geometry, opacity, always-on-top, process-name, WebView, bridge, or injector setting exists. |
| Persistence | Config schema starts at `1` under the isolated v2 root. Project/preset schema starts at `1`. Neither reader probes v1 paths or accepts v1 containers. |
| Profiles | round, cube, and user-facing `fukuyoka`; the runtime adapter alone owns the historical `paintman_hukuyoka` asset alias. |
| Localization | en, id, de, es, fr, it, nl, pl, pt-BR, vi, tr, ru, ja, ko, zh-Hans, zh-Hant. |

## Runtime and application

| ID | Retained contract | v1 authority | v2 owner | Required evidence | Replaces/deletes |
| --- | --- | --- | --- | --- | --- |
| RUNTIME-001 | The mod loads through the pinned UE4SS runtime and exports only `start_mod`/`uninstall_mod`. | `RuntimeBridgeService`, injector, `BridgeStartV1` | `mod` composition root | T0, T2, T4 | Custom injector/bootstrap ABI |
| RUNTIME-002 | Every UObject discovery/read/write/UFunction call occurs on the game thread. | Bridge tick/ProcessEvent paths | `ApplicationRoot`, `PaintGameRuntimePort`, `GameThreadScheduler`, `UnrealRuntimeAdapter` | T1, T2, T4 | Bridge worker UObject access |
| RUNTIME-003 | World, controller, pawn, HUD, and Canvas bindings recover after travel/replacement/freecam/spectator changes. | Bridge context and ESP rebinding | `HudFrameAdapter`, runtime adapter | T2, T4 | Resident bridge context globals |
| RUNTIME-004 | Explicit unload restores leases, unregisters callbacks, drains in-flight calls, and leaves no callback into unloaded code. | Bridge quiescence logic | Lifecycle state machine | T1, T2, T4 | Bridge shutdown/reinjection lifecycle |
| RUNTIME-005 | Every game-specific class/property/UFunction/layout is validated and failure is actionable/fail-closed. | Reflection helpers, `SDK` assertions | Compatibility table | T0, T2, T4 | Custom permissive reflection scanners |
| RUNTIME-006 | Worker threads receive immutable copied data and late/cancelled results cannot mutate current state. | Async Paint planning | `PaintPlanningWorker` + `PaintJobCoordinator` generations | T1, T2 | Bridge job globals |
| RUNTIME-007 | Game-thread work is bounded, observable, and control work cannot starve behind Paint dispatch. | Native batch scheduler | Scheduler and job arbiter | T1, T2, T4 | Bridge recurring scheduler |
| RUNTIME-008 | Diagnostics expose compatibility, command IDs, phases, queue pressure, timing, and terminal reasons without unbounded logs. | Runtime log/progress sidecars | Diagnostics/snapshots | T1, T4 | Sidecars/TCP status |
| RUNTIME-009 | Paint and Image Paint are mutually exclusive and exactly one preview lease exists. | `Session.RunPaintAsync` | `ApplicationRoot` shared job/preview state machines | T1, T2, T4 | `HostSession` ownership |
| RUNTIME-010 | Cancellation remains pending through native admission and reaches a true terminal state. | Session cancellation tests and Bridge jobs | Application state machine | T1, T2, T4 | Bridge cancel command |
| RUNTIME-011 | UI reads immutable revisioned snapshots and emits typed commands only. | `UI contracts`, WebHost command router | Application/UI boundary | T1, T2 | JavaScript/JSON commands |
| RUNTIME-012 | Production state is owned by one composition root, not process-wide mutable feature globals. | v1 global bridge state is the negative baseline | Composition root | T0, T2 | Bridge globals |

## Paint

| ID | Retained contract | v1 authority | v2 owner | Required evidence | Replaces/deletes |
| --- | --- | --- | --- | --- | --- |
| PAINT-001 | Brush size is one value in `[1,10]`, default `5`. | `Models.PaintSettings`, `Settings.Clamp` | `core/paint` | T1 | C# model |
| PAINT-002 | Region modes are Paint/Fill/Skip with defaults Front=Skip, Side=Paint, Back=Paint. | `Models`, passing `regions default to side and back paint` test | `core/paint` | T1, T4 | Stale release-checklist wording |
| PAINT-003 | Normal Paint always projects a hidden environment capture; the source projection is not user-selectable. | v1.7.2 PR #256 payload/runtime contract | runtime adapter + appearance worker | T1, T2, T4 | Auto Material/manual capture split |
| PAINT-004 | Appearance calibration uses a fixed four-texel lattice independent of replay brush size and final region filters. | v1.7.2 PR #256 native contract | capture geometry + appearance resolver | T1, T2, T4 | Source-UV tuning settings |
| PAINT-005 | Manual Paint PBR defaults M=0/R=1/E=0 and validates each `[0,1]`. | `Models`, Bridge channel packing | `core/paint` | T1, T2, T4 | C#/Bridge packing |
| PAINT-006 | Fill color defaults white; Fill PBR defaults M=1/R=0/E=0 and is independent of Paint PBR. | `Models`, payload tests | `core/paint` | T1, T2, T4 | C#/Bridge packing |
| PAINT-007 | Front/Back anchor one deterministic correction field; Side uses harmonic interpolation or one-boundary extension and fails closed when unanchored. | v1.7.2 PR #256 correction-field contract | `core/paint_appearance` | T1, T2, T4 | Cluster-local appearance fit |
| PAINT-008 | Automatic Emissive requires repeatable source separation plus calibrated target response; validated source chromaticity is carried by bounded Albedo and manual Emissive is a floor. | v1.7.2 PR #256 physical-emission contract | `core/paint_appearance` + runtime capture | T1, T2, T4 | SPSA/cluster Emissive fit |
| PAINT-009 | Color compression tolerance defaults `5`, range `[0,10]`, and uses brush-aligned coverage with holes, unsafe samples, UV islands, regions, and material changes as hard boundaries plus deterministic circle covering/minimax representative colors. | v1.7.2 PR #256 native contract | `core/paint` | T1, T2 | Legacy proximity coalescing |
| PAINT-010 | Round, cube, and fukuyoka profile identity/dimensions are validated before planning. | profile JSON, Bridge profile catalog | profile repository/runtime adapter | T0, T1, T4 | Bridge profile parser |
| PAINT-011 | Fill dispatch covers the base first; Paint overwrites only Paint regions; Skip receives no overwrite. | `Native contracts`, release checklist | `core/paint_plan` + dispatcher | T1, T2, T4 | Bridge replay routing |
| PAINT-012 | Preview captures all changed channels and exact restore is guarded against wrong component/repeat use. | Bridge preview export/import | core preview compositor + `PaintPreviewBuildWorker` + `PaintPreviewController` + runtime adapter | T1, T2, T4 | Import/export bridge commands |
| PAINT-013 | Normal production painting calls game-owned `PaintAtUVWithBrush` for every accepted stroke and no alternative sender. | `SDK`, Bridge direct route, maintenance docs | Runtime adapter/dispatcher | T0, T2, T4, T6 | Bridge direct RPC |
| PAINT-014 | Planning and dispatch are cancellable and bounded per frame. | `Native contracts`, async Bridge job | `PaintJobCoordinator` + stop-token planner + dispatcher | T1, T2, T4 | Bridge async globals |
| PAINT-015 | Progress distinguishes planning, pass, submitted, queued, drained, elapsed, ETA, cancel, and failure. | `UI contracts`, Session, Bridge progress | `PaintDispatchController` + snapshots | T1, T2, T4 | Progress sidecars |
| PAINT-016 | Terminal completion is impossible while the game-owned queue is nonzero. | Bridge queue drain, release checklist | generation-tagged dispatcher/job arbiter | T1, T2, T4, T6 | Bridge completion heuristic |
| PAINT-017 | A valid captured paint component survives controller-pawn/freecam changes and fails safely when invalidated. | Bridge captured-component tests | Runtime handle/job | T2, T4 | Bridge process context |
| PAINT-018 | Host-painter and joining-client-painter both replicate completely without crash/disconnect. | multiplayer checklist | Game-owned dispatcher | T6 | No mock replacement |
| PAINT-019 | Local non-preemptible stroke dispatch derives its bounded delay from measured game-thread slice duration. | v1.7.2 PR #256 dispatch contract | scheduler/runtime adapter | T1, T2, T4 | Fixed-delay local dispatch |

## Image Paint

| ID | Retained contract | v1 authority | v2 owner | Required evidence | Replaces/deletes |
| --- | --- | --- | --- | --- | --- |
| IMAGE-001 | Import PNG, JPEG, and WebP through bounded native decoders. | `Models.ImagePaintLayer`, Web UI decoder | WIC PNG/JPEG + pinned static libwebp adapter + `ImageProjectDecodeWorker` | T1, T3, T4 | Browser decoding/Base64 transport |
| IMAGE-002 | Each source is `1..12 MiB`; all sources total at most `64 MiB`. | `Models` | Project model/codecs + `core/image_compositor` decoded bounds | T1 | C# validation |
| IMAGE-003 | The canonical output is exactly 1024×512 RGBA. | `Models`, payload test | `core/image_compositor` + core mapping | T1, T2 | Browser canvas/Base64 |
| IMAGE-004 | Multiple images preserve editable source bytes and explicit layer order. | `Web UI`, `Presets`, `Projects` | Project model + `ImageEditorSession`/pipeline + UI | T1, T4 | Browser layer state |
| IMAGE-005 | Each layer preserves center, size, normalized crop, seam wrap, and front/back mirror. | `Models.ImagePaintLayer`, Web UI | Project model + `core/image_compositor` | T1, T4 | JavaScript transforms |
| IMAGE-006 | Crop is finite, positive, normalized, and remains inside the source. | `Models.TryValidate` | `core/image_project` + `core/image_compositor` | T1 | C# validation |
| IMAGE-007 | Placement preserves the existing fit/fill semantics. | `Models.Placement`, Web UI compositor | `core/image_compositor` + UI | T1, T4 | Browser canvas |
| IMAGE-008 | Body type is round/cube/fukuyoka and profile selection follows the chosen body. | `Models`, Bridge image planner | `decode_canonical_image_profile` + `core/image_profile_mapping` + planner identity gate | T1, T2, T4 | Bridge mapping |
| IMAGE-009 | Four faces independently select Fill or Skip beneath opaque image pixels. | `Models`, payload/routing tests | `core/image_paint_plan` | T1, T2, T4 | Bridge image routing |
| IMAGE-010 | Image material defaults M=0/R=1/E=0; brush defaults `5` in `[1,10]`; compression defaults `0` in `[0,10]`. | `Models` | Project model + `core/image_paint_plan` | T1, T4 | C# model |
| IMAGE-011 | Image Fill color/PBR belongs to the saved project and does not inherit mutable Paint-tab state. | `Models`, preset tests | Project model + `core/image_paint_plan` | T1, T4 | C# active state |
| IMAGE-012 | Alpha skip/background behavior and background material encoding remain deterministic. | `Models`, image transparency tests | `core/image_compositor` + `core/image_paint_plan` | T1, T2, T4 | Browser/Bridge encoding |
| IMAGE-013 | Layer composition is cancellable off-thread and stale results cannot replace a newer edit. | Web UI behavior, PLAN concurrency contract | `ImageEditorPipeline` + composition/planning worker tags + `ImagePaintJobCoordinator` revision gate | T1, T2 | Browser rendering loop |
| IMAGE-014 | Preview texture creation/mutation/destruction occurs only on the game thread. | Bridge preview path | Runtime adapter | T2, T4 | Texture import/chunk bridge |
| IMAGE-015 | Preview/restore/cancel reuse the single preview/job ownership rules. | Session image commands | Application state machine | T1, T2, T4 | HostSession |
| IMAGE-016 | The three body guides align with the accepted profiles, remain above layers, and are excluded from the atlas. | Web UI guide tests, profile JSON | Profile guide/UI | T1, T2, T4 | Packaged browser guides |
| IMAGE-017 | Save/load/rename/delete preserves project metadata, sources, transforms, atlas, and material settings. | `Presets`, `Projects`, Session | `ImageEditorSession` + project store/coordinator/I/O worker | T1, T3, T4 | C# stores |
| IMAGE-018 | v2 active draft survives restart without placing image bytes/layers in `config.json`. | v1 active-state behavior; v2 PLAN ownership | `ImageEditorSession` + `ActiveDraftPersistenceWorker` + recovery | T1, T3 | v1 config migration |
| IMAGE-019 | `.mcpreset` remains a v2 product capability but v1 containers are rejected non-destructively. | `Presets` is format baseline; locked v2 break | Project store | T1, T3 | v1 preset migration |
| IMAGE-020 | Image Paint uses the accepted `PaintAtUVWithBrush` dispatcher and satisfies representative multi-client visibility. | Bridge image planning/direct route | `ApplicationRoot` + `ImagePaintGameRuntimePort` + `ImagePaintJobCoordinator` + shared Paint dispatcher | T2, T4, T6 | Bridge image sender |

## ESP

| ID | Retained contract | v1 authority | v2 owner | Required evidence | Replaces/deletes |
| --- | --- | --- | --- | --- | --- |
| ESP-001 | ESP defaults enabled and remains active while the panel is closed. | `Models`, native Present behavior | `ApplicationRoot` + `EspFrameCoordinator` | T1, T4 | External UI/native Present coupling |
| ESP-002 | Scope supports all/hider/hunter. | `Models`, Session/Web UI | ESP core frame builder | T1, T4 | JSON config command |
| ESP-003 | Boxes, skeletons, names, distance, and snaplines are independently toggleable and default on. | `Models` | ESP core frame builder + Canvas renderer | T1, T4 | D3D primitive config |
| ESP-004 | Hider color defaults `#00FF88`; hunter color defaults `#FF0000`. | `Models` | ESP core frame builder | T1, T4 | C# settings |
| ESP-005 | Spectators are excluded before target geometry. | Bridge ESP capture | ESP core frame builder + runtime capture | T1, T2, T4 | Bridge snapshot |
| ESP-006 | Role changes and avatar replacement invalidate cached identity/geometry. | Bridge avatar directory/capture | ESP core cache policy + runtime capture cache | T1, T2, T4 | Bridge globals |
| ESP-007 | Skeleton topology is selected safely and pose/root fallback never uses partial invalid data. | Bridge pose/capture, projection tests | ESP core frame builder + runtime capture | T1, T2, T4 | Bridge pose reader |
| ESP-008 | Projection/clipping honors viewport/aspect and distant snaplines reach the edge. | `Native contracts`, projection tests | ESP core frame builder | T1, T2, T4 | D3D renderer math |
| ESP-009 | Lobby/match/travel/HUD/freecam/spectator transitions rebind without stale UObjects. | Bridge DrawHUD rebinding | Runtime/ESP | T2, T4 | ProcessEvent-vtable path |
| ESP-010 | ESP renders only through frame-scoped UCanvas primitives. | v1 Canvas capture feasibility; PLAN | `EspFrameCoordinator` + Canvas renderer | T0, T4 | DXGI/D3D/Present/MinHook |

## UI, input, localization, and diagnostics

| ID | Retained contract | v1 authority | v2 owner | Required evidence | Replaces/deletes |
| --- | --- | --- | --- | --- | --- |
| UI-001 | The in-game panel contains Paint, Image Paint, ESP, Settings, and Diagnostics sections. | Web UI tabs/actions | Canvas UI | T1, T4 | WinForms/WebView window |
| UI-002 | Panel toggle defaults F9 and is configurable. | v2 locked requirement | Input/UI/settings | T1, T4 | No v1 equivalent |
| UI-003 | Paint Start/Preview/Restore/Cancel default F1–F4. | `AppSettings`, `UI contracts` | Input/application | T1, T4 | Raw Win32 hotkeys |
| UI-004 | Image Start/Preview/Restore/Cancel default F5–F8. | `AppSettings`, `UI contracts` | Input/application | T1, T4 | Raw Win32 hotkeys |
| UI-005 | Action hotkeys accept F1–F24, are unique, suppress repeat until key-up, and do not reserve system keys. | `UI contracts`, WebHost tests | Input/settings | T1, T4 | Win32 raw input |
| UI-006 | Opening the panel captures cursor/look/movement/input mode and closing restores exact prior state. | v2 locked requirement | Input lease | T1, T2, T4 | External window focus |
| UI-007 | Targets and every settings/editor mutation require an explicit Edit session, including while Paint runs; controls emit typed commands and an active job remains immutable. | v1.7.2 PR #256 WebHost guards | UI/application | T1, T2, T4 | Implicit live-draft mutation |
| UI-008 | UI displays pass progress, total progress, queue pressure, elapsed, ETA, compatibility, and bounded diagnostics. | `UI contracts`, Web UI progress | Snapshots/Canvas UI | T1, T4 | Sidecar/WebView display |
| UI-009 | UI uses a validated `[0.75,2.0]` scale multiplier defaulting to `1.0`, then scales to viewport/DPI and clips scrollable/editor content. | Web UI behavior; v2 Canvas gate | Canvas layout | T1, T4 | CSS layout |
| UI-010 | Native Windows picker is used only for image/preset selection and does not remain open. | WebHost dialogs | Windows dialog adapter | T3, T4 | Web/WinForms dialog host |
| UI-011 | Theme color remains configurable without external opacity/always-on-top/window-position fields. | `AppSettings`, locked removals | Settings/Canvas UI | T1, T4 | External-window fields |
| UI-012 | Guard/error feedback uses one severity/localization path for buttons and hotkeys. | Session/Web UI tests | Application snapshots/UI | T1, T4 | Web message router |
| I18N-001 | Exactly 16 locale codes remain: en, id, de, es, fr, it, nl, pl, pt-BR, vi, tr, ru, ja, ko, zh-Hans, zh-Hant. | `Locales` | Localization service | T0, T1 | Embedded C# catalog |
| I18N-002 | Every required key and placeholder exists in every locale with English fallback. | `Locales`, Web localization tests | Localization service | T0, T1 | JS/C# split catalogs |
| I18N-003 | Shipped fonts cover every catalog glyph; game localized font is preferred and OFL fallback is packaged. | packaged fonts, PLAN | Font resolver/Canvas UI | T0, T1, T4 | WebView font rendering |
| I18N-004 | Settings, editor, dialogs, progress, guard, operation, and compatibility errors are localized. | Web/native dialog tests | UI/application error keys | T1, T4 | Web/WinForms strings |

## Persistence and launcher

| ID | Retained contract | v1 authority | v2 owner | Required evidence | Replaces/deletes |
| --- | --- | --- | --- | --- | --- |
| PERSIST-001 | v2 uses `%LOCALAPPDATA%\\MecchaCamouflage\\v2` and never scans/reads/rewrites v1 data. | `AppPaths` is v1 baseline; locked break | Settings/project store | T0, T1, T3 | Version-scoped v1 roots |
| PERSIST-002 | Missing config loads documented defaults; malformed config is reported and left untouched. | `Settings` baseline, PLAN stricter behavior | Settings store | T1, T3 | C# permissive migration |
| PERSIST-003 | Applying settings validates one complete candidate before publication. | Session atomic batch tests | Application/settings | T1 | HostSession mutation |
| PERSIST-004 | Config/project/draft writes preserve the previous valid file on failure. | v1 atomic tests, PLAN | Stores/filesystem adapter | T1, T3 | Delete-then-move C# path |
| PERSIST-005 | Project containers reject traversal, duplicate/case-collision, links, unknown schema, invalid sizes/hashes/UTF-8/transforms, and excessive expansion. | Preset corruption baseline, PLAN | Project store | T1, T3 | `ZipArchive` assumptions |
| PERSIST-006 | Active draft is debounced off-thread and never serialized in a Canvas callback. | v2 ownership decision | `ImageEditorSession` + `ActiveDraftPersistenceWorker` | T1, T2 | Browser active state |
| PERSIST-007 | Named project publishes before the active project reference; load validates before activation; delete drains draft writes and clears an active reference before removal; partial failure is recoverable. | v2 transaction decision | `ImageEditorSession` + persistence coordinator/I/O worker | T1, T3 | Cross-file HostSession save |
| LAUNCH-001 | Distributed artifact is one native x64 EXE and runtime has no network/update/service/resident process. | v1 single EXE baseline, locked v2 behavior | Launcher/payload | T0, T3, T5 | .NET host/updater absence |
| LAUNCH-002 | Launcher resolves App ID 4704690 and validates `Chameleon\\Binaries\\Win64`, with picker fallback. | v2 locked behavior | Launcher discovery | T1, T3, T5 | Process-name targeting |
| LAUNCH-003 | Running game blocks every runtime/proxy update/removal. | v2 locked behavior | Launcher | T1, T3, T5 | Live injection |
| LAUNCH-004 | Embedded manifest is the path/role/size/SHA-256 allowlist and extraction rejects hostile paths. | `PackagedAssets` baseline, PLAN | Payload/launcher core | T1, T3 | Embedded C# assets |
| LAUNCH-005 | Managed runtime update is journaled and recovers every staging/rename interruption with one final active generation. | v2 transaction contract | Launcher transaction | T1, T3, T5 | Version/GUID caches |
| LAUNCH-006 | Effective UE4SS resolution includes higher-priority `--ue4ss-path`; ambiguity is a no-change conflict. | UE4SS installation behavior | Loader resolver | T1, T3, T5 | Assumed proxy target |
| LAUNCH-007 | Managed mode owns/reuses only exact allowed loader files and uses stable `runtime\\active`. | PLAN | Launcher | T1, T3, T5 | Custom injector |
| LAUNCH-008 | Exact shared mode validates the loaded UE4SS ABI/config, adds only Meccha's mod, and leaves unrelated content untouched. | PLAN | Launcher | T1, T3, T5 | No v1 equivalent |
| LAUNCH-009 | Unknown/incompatible loader/runtime/mod/settings cause a clear no-change conflict. | PLAN | Launcher | T1, T3, T5 | v1 forced staging/injection |
| LAUNCH-010 | UAC is requested only for needed managed proxy/override writes; unelevated original-user cache remains authoritative. | v2 security contract | Launcher broker | T1, T3, T5 | Defender elevation service |
| LAUNCH-011 | `--remove` removes only currently matching owned files; shared loader/runtime remains. | PLAN | Launcher | T1, T3, T5 | Broad cache deletion |
| LAUNCH-012 | Runtime preparation and launch work offline. | PLAN | Launcher | T1, T3, T5 | WebView bootstrap/network |

## Build, OSS, and release

| ID | Retained contract | v1 authority | v2 owner | Required evidence | Replaces/deletes |
| --- | --- | --- | --- | --- | --- |
| RELEASE-001 | UE4SS and `main.dll` come from the same exact pinned source graph/compiler/architecture/Release/runtime-library configuration. | Historical UE4SS lessons; PLAN | CMake/trusted CI | T0, T2 | Independently staged binaries |
| RELEASE-002 | Default/fork checkout and CI do not require or expose restricted UE4SS dependencies/secrets. | Current public OSS repo | CI | T0, T2 | Recursive public CI assumption |
| RELEASE-003 | Full build runs only for reviewed protected refs with maintainer-approved Epic access. | PLAN | Trusted CI | T2 | Automatic secret build |
| RELEASE-004 | Release configuration excludes UE4SS debug UI/consoles/research tools/unused mods. | v1 release-build exclusion tests | CMake/payload config | T0, T2 | v1 research runner |
| RELEASE-005 | Canonical manifest records schema, product, UE4SS commit, path, role, size, and SHA-256 for every packaged file. | PLAN | Payload tool | T1, T3 | Ad hoc embedded resources |
| RELEASE-006 | CAB, manifest, profiles, localization, fonts, project/UE4SS/dependency notices are Win32 resources in the one EXE. | v1 packaging baseline | Payload/launcher | T0, T3 | .NET resources/bootstrapper |
| RELEASE-007 | Binary checks prove x64 Release, required exports/imports, allowed dependencies, and no legacy artifacts/debug files. | v1 CI baseline | CI/payload tests | T0, T2 | v1 artifact assertions |
| RELEASE-008 | Code signing policy is truthful; missing credentials never trigger Defender changes. | PLAN | Release docs/CI | T0, T5 | Defender exclusion |
| RELEASE-009 | `main` remains v1 until the exact normal merge commit and its artifact pass all required evidence. | Branch policy | Integration/release | T0, T4-T6 | Merge-before-test |
| RELEASE-010 | `v2.0.0` tags the exact tested merge commit; published EXE/SHA-256 are the already tested trusted-CI bytes. | PLAN | Release workflow | T0, T2, T5-T6 | Rebuild-after-test |

## Explicit v1 retirements

These are not feature losses. They are v1 implementation or external-window
contracts that the approved plan removes.

| ID | Retired v1 contract | Replacement or reason |
| --- | --- | --- |
| RETIRE-001 | External window X/Y/width/height | Responsive in-game Canvas layout |
| RETIRE-002 | External opacity and always-on-top | In-game panel/theme |
| RETIRE-003 | Configurable process name and multiple-process targeting | Fixed Steam App ID/game directory |
| RETIRE-004 | WebView zoom/footer, lifecycle, recovery, DevTools, HTML/CSS/JS | Project-owned Canvas UI |
| RETIRE-005 | TCP/JSON bridge identity, reconnect, sidecars, and bootstrap ABI | In-process typed commands/snapshots |
| RETIRE-006 | Custom injection/reinjection/hot bridge generations | UE4SS proxy/runtime lifecycle |
| RETIRE-007 | DXGI/D3D11/D3D12/Present/MinHook renderer details | UCanvas drawing |
| RETIRE-008 | Windows Defender exclusion, marker, and elevation workflow | No security-configuration mutation |
| RETIRE-009 | v1 settings/active-image migration | Fresh isolated v2 store |
| RETIRE-010 | v1 `.mcpreset` import/migration | Explicit non-destructive legacy rejection |
| RETIRE-011 | Production research/probe commands and development hot reload | Test/research-only non-production tooling |
| RETIRE-012 | WebView2 Evergreen bootstrapper | No browser runtime |

## Resolved baseline ambiguities

- Paint region defaults are **Front=Skip, Side=Paint, Back=Paint**. This is
  proven by `Models.PaintSettings` and the passing test named
  `regions default to side and back paint`. Older release-checklist wording
  that says Front defaults to Fill is stale and is not a v2 requirement.
- The source asset uses the historical spelling `paintman_hukuyoka`, while the
  user-facing body type remains `fukuyoka`. The adapter/profile layer owns this
  alias; UI and persisted v2 data use `fukuyoka`.
- “Preserve preset support” preserves v2 create/load/save/rename/delete
  behavior, not v1 container compatibility.
- Projective environment appearance applies only to Paint regions. Fill always
  uses explicit Fill material values; Image Paint remains imported-image
  sampling.
- The body guide is editor-only content. It must not alter the canonical atlas
  or painted output.
- The v2 UI-scale setting is a multiplier applied after viewport/DPI
  measurement. It defaults to `1.0` and validates `[0.75,2.0]`; it is not a
  replacement for the retired WebView zoom field.
- v2 config and project/preset schemas begin at `1` inside the isolated `v2`
  data root. Product major version and schema version are independent.

## Deferred observations, not open interpretations

The following results require a live host and remain explicit gates. They do
not leave a semantic choice for implementation:

- the exact UE4SS callback/function/property subset that passes the Phase 4
  compatibility table;
- Canvas clipping, input leasing, localized glyphs, and two-layer editor
  usability in the Phase 5 viability checklist;
- real Steam/UAC/shared-runtime behavior in Phases 3, 12, and 13;
- Paint/Image Paint replication and queue drain with both host and joining
  clients.

Failure stops the dependent phase for architecture review. It does not permit
an alternate external UI, custom injector, Present hook, v1 migration reader,
or non-game Paint sender.
