# MecchaCamouflage v2 Implementation Checklist

This checklist tracks execution of [`PLAN.md`](../../PLAN.md). `PLAN.md` is the
architecture and acceptance contract; this file records execution status
without weakening that contract.

Draft PR: [#233](https://github.com/acentrist/MecchaCamouflage/pull/233)

## Status rules

- `[x]` means the item has direct evidence.
- `[ ]` means the item is not complete.
- `DEFERRED — maintainer interaction required` means engineering may continue
  only where the deferred result is not a dependency. It never means pass.
- A live check is complete only when its evidence records the product commit,
  payload manifest hash, UE4SS commit, game version, operating system,
  procedure, and relevant redacted logs/screenshots.
- Any failed architecture gate stops its dependent phases.
- Any release-critical deferred item blocks Phase 13 and release.

## Phase 0 — Repository preservation and rewrite governance

- [x] Approve the detailed v2 rewrite plan.
- [x] Verify `main` and tag `v1.6.7` identify the accepted v1 release commit.
- [x] Create local branch `release/v1.x` at `v1.6.7`.
- [x] Create local branch `rewrite/ue4ss-v2` from `main`.
- [x] Commit `PLAN.md` as the first rewrite commit.
- [x] Push `release/v1.x`.
- [x] Push `rewrite/ue4ss-v2`.
- [x] Open the public Draft PR with the locked title.
- [x] Add this phase checklist to the Draft PR.
- [x] Record the v1 source, test, submodule, build, and payload baseline.
- [x] Run every available v1 deterministic build/test check and record
  pre-existing failures separately.

## Phase 1 — v1 behavioral baseline and traceability

- [x] Assign stable requirement IDs for Paint, Image Paint, ESP, UI,
  persistence, runtime, launcher, localization, and release behavior.
- [x] Map each retained behavior to v1 source, v1 evidence, v2 owner, planned
  automated evidence, planned live evidence, and eventual deletion.
- [x] Record defaults, ranges, hotkeys, profiles, locales, image limits, atlas,
  preset incompatibility, and body-guide behavior.
- [x] Classify every existing test as port, characterization, rewrite, or
  architecture-only retirement.
- [x] Create deterministic fixtures for retained pure algorithms.
- [x] Inventory all game-specific reflected contracts and profile inputs.
- [x] Resolve or explicitly block every behavior ambiguity.

Evidence:

- [`requirements-traceability.md`](requirements-traceability.md)
- [`test-migration.md`](test-migration.md)
- [`compatibility-contracts.md`](compatibility-contracts.md)
- `src/tests/fixtures/v1`
- `python3 tools/v2/verify_phase1.py`

## Phase 2 — C++ build graph and pinned UE4SS source gate

- [x] Add the root CMake target graph and enforce dependency boundaries.
- [x] Add public dependency checkout that does not require restricted recursive
  UE4SS dependencies.
- [x] Pin `third_party/RE-UE4SS` to
  `6c26f038751b3d96059d4a9148f5d093012d55ad`.
- [ ] Record the exact recursive source graph and licenses.
  - [x] Collect the closed production CMake target graph through File API
    codemodel v2, collapse its actual git roots with tracked-diff identities,
    and add the target-filtered locked Cargo resolve closure with registry
    checksum and feature identities.
  - [x] Generate and upload an exact evidence-bound, deliberately unapproved
    audit template whose empty review fields cannot pass notice assembly.
  - [ ] Run the collector on the protected recursive graph and approve every
    corresponding license file.
- [x] Add secret-free fork CI with `MECCHA_WITH_UE4SS=OFF`.
  - [x] Run the complete Linux graph under AddressSanitizer and
    UndefinedBehaviorSanitizer in addition to the Windows MSVC Release graph.
- [x] Add the maintainer-approved protected-ref full-build workflow.
- [ ] Build UE4SS and the minimal mod from the same source/configuration.
- [ ] Verify x64 Release ABI, imports, exports, runtime library, and provenance.
- [ ] Confirm no UE4SS source patch is required.
- [ ] DEFERRED — maintainer interaction required: confirm a clean full source
  build with the required Epic-linked GitHub access.

Evidence:

- [`dependency-lock.md`](dependency-lock.md)
- [`phase2-build-gate.md`](phase2-build-gate.md)
- `.github/workflows/v2-ci.yml`
- `.github/workflows/v2-full-build.yml`
- `tools/v2/verify_source_graph.py`
- `tools/v2/verify-full-build.ps1`

## Phase 3 — Safe isolated deployment and loading gate

- [x] Implement manifest, hashing, ownership, and conflict primitives.
- [x] Implement the journaled staging/rollback/recovery transaction.
- [x] Implement Steam/folder discovery and running-game rejection.
- [x] Resolve effective `--ue4ss-path`, override, conventional, and proxy
  loader identity.
- [x] Implement managed, exact shared, and conflict modes.
- [x] Implement original-user preparation and minimal elevated broker.
  - [x] Compose immutable observation, deployment policy, preparation/removal
    planning, and ordered execution behind one portable typed workflow.
  - [x] Derive and observe managed proxy/override expectations before runtime
    publication without creating cache or ownership directories.
  - [x] Classify a recovered runtime cache as missing, exact, owned previous,
    or conflict through a read-only transaction boundary.
  - [x] Observe compatible shared mod payload and installed-ledger state as
    missing, exact owned, exact unowned, owned previous, or conflict without
    creating ownership metadata or recovering transactions.
  - [x] Assemble loader, managed-file, runtime-cache, runtime-settings, and
    shared-mod evidence into one deterministic preparation/removal observation
    without platform effects.
  - [x] Retain the exact resolved command/override target from the loader
    observation and classify pinned shared runtime/config compatibility without
    mutation.
  - [x] Bind the native original-user observation source to active Steam
    options, loader targets, managed ownership, recovered cache identity,
    shared runtime/config/mod state, and non-mutating access probes.
  - [x] Bind the observed shared runtime root into the execution backend
    without re-reading loader configuration.
  - [x] Compose verified manifest identity, running-game preflight, startup
    recovery, material/observation/execution adapters, and the typed launcher
    workflow behind one synchronous native boundary.
  - [x] Compose the public CLI, single-instance guard, explicit/automatic game
    selection with folder-picker fallback, invoking-user LocalAppData paths,
    running-game-before-package preflight, nonce generation, and execution
    composition behind a testable native application boundary.
  - [x] Split owned-file receipt intent/finalization from privileged target
    mutation so the elevated broker never owns LocalAppData transaction work.
  - [x] Constrain the privileged mutation executor to preflighted
    verify/install/remove actions for only `dwmapi.dll` and `override.txt`,
    with no ownership or LocalAppData capability.
  - [x] Coordinate original-user receipt intents, privileged-client results,
    exact result verification, finalization, and partial/failure recovery
    without requesting a client for exact reuse.
  - [x] Define a bounded canonical binary broker request/response protocol with
    nonce echo, strict framing, UTF-16 path validation, and no arbitrary target
    or ownership path.
  - [x] Bind the broker protocol to one first-instance, local-only named pipe
    with a protected user/Administrators/System DACL, exact launched/connected
    PID checks, same-executable and same-session validation, elevated-child
    token validation, parent SID capture, peer-exit/timeout handling, and an
    internal nonce/PID-only `runas` child mode kept outside the public CLI.
  - [x] Implement the minimal elevated two-file broker.
    - [x] Bind one accepted manifest hash into the original-user broker only
      after the embedded package has been parsed and verified.
    - [x] Independently reload and verify the embedded package in the
      authenticated elevated child before resolving original-user data or
      mutating loader files.
    - [x] Resolve the stable runtime path from the authenticated parent's
      token, rebuild proxy/override material in the child, and expose only the
      restricted two-file mutation platform.
    - [x] Compose the public native GUI entry point, internal child entry
      point, exact UAC scope explanation, bounded TaskDialog/log reporting,
      and `asInvoker` manifest without adding an always-elevated launcher.
- [x] Implement safe reuse, repair, update, cleanup, and `--remove`.
- [x] Pass automated temporary-tree, hostile-path, reparse,
  interrupted-transaction, and shared-isolation tests.
- [ ] DEFERRED — maintainer interaction required: verify the authenticated
  original-user LocalAppData contract with alternate UAC credentials.
- [ ] DEFERRED — maintainer interaction required: prepare and launch on a
  supported live Windows host after stopping the currently running game.
- [ ] DEFERRED — maintainer interaction required: observe writable and
  elevation-required managed preparation.
- [ ] DEFERRED — maintainer interaction required: verify exact shared UE4SS
  coexistence without modifying unrelated content.

Evidence:

- [`phase3-deployment-gate.md`](phase3-deployment-gate.md)

## Phase 4 — Runtime lifecycle and teardown gate

- [x] Implement the composition root and runtime state machine.
- [x] Implement callback registration, in-flight barrier, and exact
  unregistration.
- [x] Implement the bounded game-thread scheduler.
- [x] Keep `on_update()` free of UObject and ProcessEvent access.
- [x] Implement project-owned generation-checked
  World/controller/HUD/Canvas rebinding.
- [x] Implement structured compatibility failures and bounded diagnostics.
- [ ] Implement representative retained Paint and Image Paint runtime
  operations on the game-thread path.
  - [x] Connect typed Paint capture, planning, dispatch, queue observation, and
    completion through `ApplicationRoot` with a fake game-thread runtime.
  - [ ] Connect representative Image Paint and production UE4SS operations.
- [x] Implement restore-before-unregister explicit unload ordering.
  - [x] Cancel the active Paint generation and wait for local plus observed
    visual/outgoing queue drain before lifecycle quiescing.
  - [x] Stop and discard Paint preview builds, restore the exact project-owned
    texture snapshot on the game thread, then enter lifecycle quiescing.
- [ ] Pass fake-runtime, thread-affinity, lifecycle fault, and concurrent
  uninstall tests.
- [ ] DEFERRED — maintainer interaction required: verify live load, travel,
  HUD replacement, freecam, spectator, explicit unload, and game shutdown.

Evidence:

- [`phase4-runtime-gate.md`](phase4-runtime-gate.md)

## Phase 5 — UCanvas feasibility gate

- [x] Implement retained immediate-mode Canvas controls and layout.
  - [x] Implement bounded clipped scroll containers and pointer-driven buttons,
    toggles, continuous sliders, and RGB color controls on the project-owned
    Canvas/interaction protocol.
  - [x] Implement keyboard focus/navigation and bounded text fields.
- [ ] Implement frame-scoped lines, boxes, text, clipping, and textures.
  - [x] Define a bounded project-owned Canvas frame protocol with finite
    geometry validation, nested clipping, exact line/box intersection,
    texture/UV clipping, strict UTF-8 text, opaque texture handles, and no
    Unreal or graphics API types.
  - [ ] Implement the production UCanvas primitive adapter.
- [ ] Implement exact cursor/look/movement/input-mode lease restoration.
  - [x] Implement a transactional lease controller that captures the complete
    prior input state once, avoids repeated mutation, restores exactly on
    close/shutdown, rolls back failed acquisition, and retains failed restore
    state for bounded retry.
  - [ ] Implement and live-verify the production Unreal input-state port.
- [x] Implement responsive viewport/DPI scaling.
- [ ] Implement localized game-font and packaged OFL fallback-glyph paths.
- [ ] Render representative ESP primitives while the panel is closed.
- [x] Complete the retained portable two-layer Image Paint editor vertical
  slice with topmost selection, retained move/resize/crop/order transitions,
  immutable layer edits, Canvas atlas display, and selection handles.
- [x] Keep body guides above layers and outside the canonical atlas through a
  separate exact-profile/version-bound Canvas texture overlay.
- [ ] Pass layout, hit-test, input-lease, glyph, editor, worker-result, and
  texture-lifetime tests.
  - [x] Pass portable Canvas clipping/resource and input-lease rollback/retry/
    shutdown contract tests.
  - [x] Pass portable safe-area/DPI layout, clipped hit-test, exclusive pointer
    capture, focus, disabled-control, duplicate-ID, and same-frame click tests.
  - [x] Pass portable scroll clamp/hit tests and Canvas widget drawing,
    activation, value-mapping, duplicate-ID, and high-DPI scale tests.
  - [x] Pass portable keyboard navigation/activation/cancel tests and ordered
    bounded single-line UTF-8 edit/cursor/commit/cancel tests.
  - [x] Pass portable two-layer selection, move, resize, reorder, crop,
    cancellation, guide-ordering, identity, limit, and clip-unwind tests.
- [ ] DEFERRED — maintainer interaction required: verify the complete Canvas
  viability checklist in the live game.

Evidence:

- [`phase5-canvas-progress.md`](phase5-canvas-progress.md)

## Phase 6 — Pure core and application state machines

- [x] Port retained value types and validation to focused C++ modules.
- [ ] Port profile, region, color/PBR, atlas-mapping, pacing, compression, ESP,
  and timing algorithms.
  - [x] Port bounded ESP role/spectator filtering, avatar-cache policy,
    perspective projection, capsule/pose bounds, viewport clipping, and
    frame-primitive construction.
- [x] Implement typed commands and immutable revisioned snapshots.
- [x] Implement bounded concurrent typed-command admission, FIFO frame drains,
  close/discard shutdown, and invalid command-ID rejection.
- [x] Implement job arbitration, cancellation generations, preview ownership,
  queue pressure, terminal drain, and shutdown coordination.
- [ ] Add checked arithmetic and resource limits.
  - [x] Bound decoded Image Paint dimensions/bytes, layer/source counts, and
    canonical-atlas composition work before allocation or pixel traversal.
  - [ ] Complete the remaining cross-feature checked-arithmetic and
    resource-limit audit.
- [ ] Pass unit, property, golden, state-machine, dependency-boundary, and
  supported sanitizer/static-analysis checks.
  - [x] Pass all registered secret-free tests under Linux ASan/UBSan and keep
    the sanitized job mandatory in public PR CI.
  - [ ] Complete remaining property/golden and supported static-analysis
    evidence.

Evidence:

- [`phase6-core-progress.md`](phase6-core-progress.md)

## Phase 7 — Configuration, localization, and persistence

- [x] Specify and implement the strict v2 config schema.
- [x] Use `ApplicationConfig::esp.enabled` as the single persisted and runtime
  ESP enablement owner; toggles publish only after atomic config save and fail
  closed without changing capture/draw state.
- [x] Keep image bytes and layer collections out of `config.json`.
- [x] Implement atomic config and active-draft persistence.
- [ ] Port all 16 localization catalogs and validate placeholders/glyphs.
  - [x] Preserve all 16 catalogs in the v2 resource tree.
  - [x] Validate locale/key sets, placeholders, UTF-8, and glyph inventory.
  - [ ] Verify game-font and packaged fallback glyph coverage.
- [x] Specify the canonical v2-only `.mcpreset` container.
- [x] Implement project save/load/rename/delete and content-addressed sources.
- [x] Run named project load/save/rename/delete on one owned I/O worker with
  copied immutable requests, typed results, exception containment, reuse, and
  terminal shutdown.
- [x] Coordinate editor decode/composition, active-draft debounce, optimistic
  named project operations, typed completions, and terminal shutdown through
  one project-owned session wired to `ApplicationRoot`.
- [x] Run startup recovery before runtime callback registration, submit the
  recovered named/draft project to the derived pipeline exactly once, publish
  its source/diagnostics in the immutable session snapshot, and fail root
  initialization closed when recovery cannot start.
- [x] Reject v1 presets with the explicit non-destructive legacy result.
- [ ] Pass schema, corruption, hostile-container, fault-injection, recovery,
  determinism, and resource-limit tests.
- [ ] DEFERRED — maintainer interaction required: verify native file-picker and
  non-ASCII Windows path behavior.

Evidence:

- [`phase7-persistence-progress.md`](phase7-persistence-progress.md)

## Phase 8 — Paint

- [ ] Implement typed game-thread capture and exact runtime contracts.
- [x] Validate round, cube, and fukuyoka profiles.
- [x] Implement immutable planning for Paint/Fill/Skip, lighting, Auto Material,
  PBR, compression, and Fill-first ordering.
- [x] Run Paint planning on one owned cancellable worker with immutable input,
  generation-tagged results, and an exception boundary.
- [x] Coordinate planning, dispatch, drain, planning cancellation, typed
  failure, and stale-result rejection through the shared job generation.
- [x] Implement the bounded typed albedo/packed-PBR preview snapshot controller,
  ownership guards, apply recovery, replacement restore, and shutdown restore.
- [x] Implement cancellable bounded Paint-plan composition over immutable
  original albedo/packed-PBR channels without mutating the restore snapshot.
- [x] Run Paint preview planning and composition on one owned cancellable
  worker with immutable input, generation-tagged results, typed failures, and
  an exception boundary.
- [x] Connect typed Start/Cancel Paint, capture, planning, dispatch, queue
  observation, progress, and terminal completion through `ApplicationRoot`.
- [x] Connect typed Preview/Restore Paint through `ApplicationRoot`, including
  restore-before-real-Paint ordering and game-thread-only capture/apply.
- [ ] Implement exact preview capture/apply/restore ownership.
- [ ] Dispatch only through game-owned `PaintAtUVWithBrush`.
- [x] Implement bounded per-frame pacing, progress, cancellation, and terminal
  queue drain.
- [ ] Preserve valid jobs through freecam/controller-pawn changes.
- [ ] Pass all Paint unit, contract, fake-runtime, stress, and failure tests.
- [ ] DEFERRED — maintainer interaction required: complete single-client Paint
  live checks.
- [ ] DEFERRED — maintainer interaction required: complete host-painter and
  joining-client-painter two-client checks.

## Phase 9 — Image Paint

- [x] Implement bounded WIC PNG/JPEG decoding with declared-container
  verification and caller-owned RGBA output.
- [x] Select, pin, license, and implement the static libwebp `v1.6.0`
  decoder, rejecting animation and decoding into a pre-bounded caller-owned
  RGBA buffer.
- [x] Run project-source decoding on one owned worker with copied immutable
  source descriptors, ordered output, cancellation, project/revision tags,
  aggregate decoded-memory enforcement, exception containment, reuse, and
  terminal shutdown.
- [ ] Implement the full editable layer and project model.
  - [x] Implement validated immutable project settings, ordered layer values,
    v2-only persistence, and content-addressed source ownership.
  - [x] Connect project revisions to an owned decode/composition pipeline that
    coalesces newer edits, cancels superseded work, rejects stale revisions,
    and publishes only an exact project-ID/revision match.
  - [x] Route typed load/save/rename/delete commands through the editor session,
    publish persistence/draft pressure in immutable snapshots, preserve current
    edits during rename, drain draft writes before delete, and close the
    session only after runtime callbacks are unregistered.
  - [x] Implement bounded project-owned layer selection, move, corner resize,
    reorder, crop/zoom, exact cancellation, and immutable edit results for the
    in-game Canvas editor.
  - [x] Route revision- and asset-guarded layer placement/crop/order and
    project-settings mutations through the application owner, retain the
    newest immutable draft while composition is active, and publish only
    bounded editor metadata to UI snapshots.
  - [x] Connect source-identity-bound Crop controls to the portable product
    panel with local zoom/center edits, exact cancellation, source-texture
    rendering, and one guarded layer mutation only on Apply.
  - [ ] Connect those values to the complete in-game editor lifecycle.
- [x] Implement deterministic cancellable 1024×512 RGBA composition.
  - [x] Run composition on one owned worker with copied immutable inputs,
    one active generation, typed cancellation/failures, revision-tagged
    results, exception containment, and terminal shutdown.
  - [x] Reject stale project revisions in the application/editor owner.
- [ ] Implement versioned round/cube/fukuyoka guide overlays.
  - [x] Define an exact image-profile/schema-bound guide texture contract and
    render it separately after the canonical atlas.
  - [ ] Generate and lifetime-manage the guide textures from the three exact
    packaged reference profiles through the production runtime adapter.
- [ ] Implement game-thread preview texture lifetime.
- [ ] Implement all profile mappings and reuse the accepted Paint dispatch.
  - [x] Decode immutable reference vertices/indices from each exact packaged
    image profile, validate canonical bounds/topology, and map
    triangle+barycentric captures for every round/cube/fukuyoka triangle into
    the four atlas faces.
  - [x] Convert validated triangle-anchored capture samples and canonical atlas
    pixels into the shared immutable `PaintPlan`, with the canonical mapping
    performed inside the planner, independent face Fill/Skip,
    alpha/background routing, Fill-first ordering, material isolation,
    compression, cancellation, and resource bounds.
  - [x] Run Image Paint planning on one owned worker with copied immutable
    capture/profile/atlas input, one active generation, typed cancellation and
    failures, project/revision-tagged results, exception containment, reuse,
    and terminal shutdown.
  - [x] Connect the resulting plan to the shared bounded Paint dispatcher
    through a project/revision-validating coordinator, including planning and
    dispatch cancellation, queued-generation discard, drain, and stale-result
    rejection.
  - [x] Wire the coordinator to `ApplicationRoot` through exact editor
    readiness plus game-thread capture and queue-observation ports, including
    typed Start/Cancel, edit invalidation, shutdown drain, and immutable
    snapshot publication.
  - [ ] Implement the production UE4SS triangle-anchor capture and queue
    observation adapter.
- [ ] Integrate v2 project/preset management.
  - [x] Connect the v2-only project store and session transaction boundary to
    the application command variant and composition root.
  - [x] Connect persisted startup recovery to the root initialization boundary
    before UE4SS callback registration.
  - [ ] Connect native picker/import and complete UCanvas project controls.
- [ ] Pass decoder, layer, guide, atlas, mapping, preset, fake-runtime, and
  resource-limit tests.
  - [x] Pass portable editor gesture/crop/order and guide overlay isolation/
    identity/ordering/limit tests.
- [ ] DEFERRED — maintainer interaction required: complete the full Image Paint
  visual/editor/live-paint checklist.

Evidence:

- [`phase9-image-paint-progress.md`](phase9-image-paint-progress.md)

## Phase 10 — ESP

- [ ] Implement coherent game-thread target/role/avatar/pose capture.
- [ ] Implement project-owned projection and Canvas primitive rendering.
  - [x] Implement bounded project-owned target values, role/spectator
    filtering, projection, clipping, capsule/pose bounds, and line/text
    primitive construction without graphics or Unreal dependencies.
  - [x] Coordinate exact-HUD-frame capture/build/draw through a typed runtime
    port and publish immutable frame diagnostics.
  - [ ] Implement the production UE4SS capture and UCanvas draw adapters.
- [ ] Implement every scope, primitive, role color, and clipping rule.
  - [x] Cover every configured scope, primitive toggle, role color, and
    viewport/behind-camera clipping rule in the pure builder.
- [ ] Invalidate/rebuild on role, avatar, spectator, travel, HUD, and lifetime
  changes.
  - [x] Specify and test the project-owned role/avatar cache policy and reject
    stale HUD frame identities before draw.
  - [ ] Apply the policy to validated production weak Unreal handles across
    travel, HUD, freecam, spectator, role, and avatar transitions.
- [ ] Pass projection, filtering, skeleton, invalidation, fake-runtime, and
  forbidden-hook tests.
  - [x] Pass pure projection/filtering/skeleton/resource tests, coordinator
    failure tests, and root integration proving ESP remains active while the
    panel is closed.
- [ ] DEFERRED — maintainer interaction required: complete all live ESP and
  lifecycle checks.

Evidence:

- [`phase10-esp-progress.md`](phase10-esp-progress.md)

## Phase 11 — Full product UI and integration

- [ ] Complete Paint, Image Paint, ESP, Settings, and Diagnostics sections.
  - [x] Define a bounded application-owned five-section presentation model
    containing exact current settings, editor/project readiness, feature and
    project action availability, ESP state, progress, queue utilization,
    compatibility, and newest bounded diagnostics.
  - [x] Compose a responsive localized five-tab Canvas shell with Paint/Image
    action rows, ESP toggle, and bounded status output in a separate
    application-to-UI target without changing the Core-only UI primitives.
  - [x] Compose the complete portable Settings section for language, UI scale,
    RGB theme color, and all nine F1–F24 mappings with section-local scrolling
    and duplicate-free remapping.
  - [x] Compose the complete portable Paint settings editor for sampling
    bounds, three region modes, appearance toggles, independent Paint/Fill
    materials, Fill color, compression, and bounded section-local scrolling.
  - [x] Compose the complete portable ESP section for the master toggle, target
    scope, all five primitive toggles, both role colors, and bounded
    section-local scrolling.
  - [x] Compose every persisted Image Paint project setting as a validated
    revision-bound portable control, including body/placement/alpha/face
    routing, brush/compression, Image/Fill materials, Fill color, and bounded
    section-local scrolling.
  - [x] Compose a bounded localized Diagnostics section for runtime and
    compatibility state, command/runtime queue pressure, omitted-entry counts,
    ordered severity-colored diagnostics, command IDs, and exact
    contract/failure identifiers.
  - [ ] Render and interact with every complete section through the production
    UCanvas adapter.
- [ ] Bind UI only to typed commands and immutable snapshots.
  - [x] Build presentation state solely from a validated immutable
    `ApplicationSnapshot`, without Unreal, graphics, or mutable runtime access.
  - [x] Define a revision-bound product-action variant covering every Paint,
    Image Paint, ESP, settings, and project operation, and convert it through
    the same thread-safe monotonic command-ID owner used by hotkeys.
  - [x] Convert portable Paint/Image button and ESP toggle activations into one
    revision-bound product action per immutable Canvas frame.
  - [x] Copy the exact immutable complete config for every Settings edit,
    mutate only the selected field, validate it, and emit one revision-bound
    `UiApplySettings` action without reconstructing omitted state.
  - [x] Route every portable Paint settings edit through that same exact
    full-config validation and revision-bound action boundary.
  - [x] Route every portable ESP setting except the dedicated master toggle
    through exact full-config validation and revision-bound application.
  - [x] Route every portable Image Paint setting through a project-owned
    `ReplaceImageProjectSettingsMutation` copied from the exact immutable
    document rather than changing application defaults or runtime objects.
  - [ ] Connect every production Canvas widget/editor activation to that
    product-action boundary and enqueue its typed result.
- [ ] Complete all editor interactions and body-guide behavior.
  - [x] Bind an exact project/revision-tagged opaque atlas texture and optional
    profile-tagged guide overlay into the portable Image Paint section without
    transferring texture ownership into application or UI state.
  - [x] Integrate retained topmost selection and move/resize gestures through a
    local draft that freezes section scrolling, cancels without mutation, and
    emits one asset-guarded layer replacement only on release.
  - [x] Reject stale texture bindings, unavailable edit ownership, duplicate
    gestures while awaiting the published project revision, and reset the
    local draft on exact revision replacement or panel close.
  - [x] Connect selected-layer forward/back ordering, seam wrap, and front/back
    mirror controls through index/asset-guarded revision-bound mutations.
  - [x] Connect Crop through an exact project/revision/source-bound local
    session that renders the opaque source texture, freezes scrolling during
    drag, supports localized zoom/Apply/Cancel controls, and emits one isolated
    guarded layer replacement only on Apply.
  - [ ] Connect import/removal and body-guide generation/lifetime through the
    product panel.
- [ ] Complete F9 and configurable F1–F8 hotkey behavior.
  - [x] Implement a bounded application-owned F1–F24 command router with the
    F9/F1–F8 defaults, validated remapping, physical-key repeat suppression
    until release, immutable snapshot capture, unavailable-project rejection,
    one command-ID sequence shared with Canvas actions, concurrent admission,
    input-loss release, and terminal shutdown.
  - [x] Expose all nine current mappings as localized, duplicate-free portable
    Settings controls while preserving a complete validated config.
  - [ ] Register all supported keys once and connect the production UE4SS input
    callbacks to the router.
- [ ] Complete progress, backpressure, compatibility, and diagnostics display.
  - [x] Publish validated progress fractions, queue pressure/utilization,
    compatibility state, and newest bounded ordered diagnostics as portable
    presentation values.
  - [x] Draw bounded completed/total and command-queue counts in the portable
    panel status strip.
  - [x] Draw localized runtime/compatibility/queue summaries and bounded
    actionable diagnostic entries through the portable Canvas panel.
  - [ ] Bind those values through the production UCanvas adapter.
- [ ] Complete responsive themes, DPI, clipping, fonts, and all 16 languages.
  - [x] Apply safe-area, viewport, DPI, configured scale, clipping, and theme
    accent to the portable panel shell and compose its labels for every one of
    the 16 shipped catalogs.
  - [x] Retain bounded per-section scroll state and exclude clipped Settings,
    Paint, Image Paint, and ESP controls from keyboard focus/action admission.
- [x] Ensure panel close preserves ESP and active jobs.
- [ ] Pass UI action/state, gesture, hotkey, layout, and localization tests.
  - [x] Pass portable panel tests for all shipped locales, section switching,
    enabled/disabled typed actions, revision binding, panel-close input
    release, and invalid label/state refusal.
  - [x] Prove every Paint setting control changes only its owned field in a
    complete validated config, including strict rejection of invalid region
    enum values.
  - [x] Prove every ESP scope/primitive/color control changes only its owned
    field, preserves the master enable state, and rejects invalid scope enums.
  - [x] Prove all 16 Image Paint settings controls change only their owned
    field, emit one snapshot-revision-bound project mutation, remain disabled
    without edit ownership, retain compact scrolling, and reject divergent
    document/presentation state.
  - [x] Prove Image Paint atlas binding, local move/release/cancel behavior,
    gesture-time scroll exclusion, asset-guarded mutation, stale-asset refusal,
    edit-availability refusal, revision acknowledgement, and duplicate-action
    suppression.
  - [x] Prove selected-layer forward/back ordering, seam wrap, and front/back
    mirror emit only their exact guarded mutation and enter revision wait.
  - [x] Prove Crop source binding and dimensions, source-versus-atlas drawing,
    local zoom/drag/release, scroll exclusion, Apply field isolation, button
    and keyboard cancellation, missing-source refusal, and invalid-source
    rejection.
  - [x] Prove localized Diagnostics summaries, message translation, command and
    compatibility details, omitted counts, empty state, compact scrolling, and
    rejection of incoherent queue presentation.
- [ ] DEFERRED — maintainer interaction required: complete the full in-game UI
  walkthrough across languages/resolutions/DPI.

Evidence:

- [`phase11-ui-progress.md`](phase11-ui-progress.md)

## Phase 12 — Launcher, payload, and CI hardening

- [ ] Complete launcher switches and every managed/shared lifecycle.
- [ ] Assemble the minimal release UE4SS configuration.
  - [x] Implement an exact trusted-build runtime assembler that disables
    UE4SS consoles/debug UI/hot reload/crash dumps, packages only the product
    C++ mod and retained resources/licenses, and emits the canonical layout
    input without modifying the pinned UE4SS source.
  - [ ] Confirm the assembled runtime inventory and generated-path allowlist
    through the trusted build and live architecture gate.
- [x] Generate canonical manifest and deterministic CAB.
  - [x] Generate the manifest from an exact declared layout with canonical
    ordering, SHA-256/size binding, strict hostile-path rejection, exact tree
    coverage, reparse refusal, and atomic output publication.
  - [x] Generate a deterministic CAB and verify its round trip against the
    canonical manifest.
- [ ] Embed payload, resources, licenses, and notices.
  - [x] Implement bounded Win32 `RCDATA` loading and a manifest-exact,
    in-memory CAB payload source with private extraction and cleanup.
  - [x] Bind an exact manifest/CAB pair conditionally as `RCDATA` resources
    and build the native x64 Windows GUI EXE with an explicit `asInvoker`
    manifest; configuration refuses a partial resource pair.
  - [x] Implement evidence-bound third-party notice assembly that requires an
    exact separately approved component/license/hash audit and refuses missing,
    extra, changed, empty, linked, or reparse-routed license inputs.
  - [x] Wire deterministic CMake/git/Cargo evidence collection into the
    protected full-build workflow.
  - [x] Generate an evidence-bound review template without inferring license
    expressions, files, or approval; the protected notice builder rejects that
    template until every component is reviewed.
  - [ ] Run that protected collector and approve the complete dependency
    evidence and corresponding license-file hashes.
  - [ ] Bind the final trusted CAB, manifest, profiles, localization, fonts,
    icon, licenses, and notices into the release EXE.
- [ ] Build runtime and mod from the same trusted graph.
- [ ] Complete secret-free PR CI and protected full-build/release CI.
  - [x] Implement a manual, protected, fail-closed release-candidate workflow
    that rebuilds the pinned graph, requires a separately approved dependency
    audit, collects exact evidence, assembles the runtime/CAB/resources,
    reruns all contracts, verifies one EXE, and uploads candidate bytes plus
    evidence without publishing a GitHub Release.
  - [ ] Commit the reviewed dependency audit and complete the protected
    release-candidate run.
- [ ] Add binary, provenance, payload, license, and forbidden-artifact checks.
  - [x] Implement a bounded dependency-free PE/resource verifier for exact x64
    GUI/manifest/icon/RCDATA contracts, memory protections, import allowlist,
    no exports/.NET/CodeView/PDB/legacy tokens, payload/layout/provenance
    identity, and exact single-artifact publication.
  - [ ] Run the verifier against the protected full-build artifact and audited
    dependency-notice bundle.
- [x] Decide and document code-signing policy without modifying Defender.
- [ ] Produce exactly one EXE and its SHA-256.
  - [x] Generate the canonical SHA-256 sidecar and release evidence only after
    the exact single-EXE directory passes all artifact checks.
  - [x] Retain the approved audit, generated notices, dependency report,
    source/binary provenance, payload manifest/layout, artifact report, and
    checksum as one protected candidate evidence bundle.
  - [ ] Produce and retain the protected final artifact bytes.
- [ ] DEFERRED — maintainer interaction required: complete real Steam/UAC/
  coexistence/update/remove checks.

Evidence:

- [`phase12-packaging-progress.md`](phase12-packaging-progress.md)

## Phase 13 — Stabilization and release matrix

- [ ] Freeze behavior and accept only evidence-driven fixes.
- [ ] Pass the complete automated suite repeatedly from clean trusted builds.
- [ ] Establish and enforce bounded performance/resource baselines.
- [ ] Pass fault-injection, stress, cancellation, and shutdown coverage.
- [ ] Complete 25 managed prepare/launch cycles without accumulation.
- [ ] Close every requirement and risk with evidence.
- [ ] DEFERRED — maintainer interaction required: complete Windows 10 matrix.
- [ ] DEFERRED — maintainer interaction required: complete Windows 11 matrix.
- [ ] DEFERRED — maintainer interaction required: complete the final
  single-client, UI/localization, and two-client game matrices.

## Phase 14 — Legacy deletion, exact merge candidate, and v2.0.0

- [ ] Delete all replaced C#/WebView/bridge/injector/custom-render runtime code.
- [ ] Remove obsolete dependencies only after replacement equivalence.
- [ ] Remove probes, debug controls, stale resources, and generated outputs.
- [ ] Update all v2 documentation, CI, licenses, and repository layout.
- [ ] Verify the complete deletion list against requirements traceability.
- [ ] Create a normal merge commit on a temporary protected integration ref.
- [ ] Build and test the exact proposed merge commit and artifact.
- [ ] DEFERRED — maintainer interaction required: run the final live matrix
  against that exact merge commit and manifest.
- [ ] Fast-forward `main` to the tested merge commit.
- [ ] Tag that same commit `v2.0.0`.
- [ ] Publish the already tested single EXE and matching SHA-256.

## Deferred maintainer sessions

These sessions are grouped to minimize interruptions:

1. Architecture viability session: Phase 3 managed/shared preparation, Phase 4
   lifecycle/unload, and Phase 5 Canvas/input/editor checks.
2. Feature session: single-client Paint, Image Paint, ESP, UI, travel, freecam,
   spectator, and shutdown checks.
3. Multiplayer session: host-painter and joining-client-painter checks with two
   real clients.
4. Portability session: Windows 10/11, Steam library, UAC, shared/conflict,
   repair/update/remove, and repeated-cycle checks.
5. Final candidate session: rerun the release-critical matrix against the exact
   proposed merge commit and manifest.
