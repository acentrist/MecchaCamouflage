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

Review artifacts:

- [`architecture.md`](architecture.md)
- [`requirements-traceability.md`](requirements-traceability.md)
- [`compatibility-contracts.md`](compatibility-contracts.md)
- [`live-test-checklist.md`](live-test-checklist.md)
- [`risk-register.md`](risk-register.md)
- [`decisions/`](decisions/)

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
  - [x] Carry the verified immutable UE4SS source-stage root through dependency
    evidence and notice assembly so UE4SS licenses cannot be read from the
    pristine gitlink or another checkout.
  - [x] Generate an exact evidence-bound, deliberately unapproved audit
    template whose empty review fields cannot pass notice assembly, and make
    the protected workflow upload it as evidence only.
  - [x] Run the collector against the locally initialized restricted graph:
    39 closed CMake targets and 75 exact components produced canonical
    dependency evidence and a deliberately unapproved audit template.
  - [ ] Re-run the collector in the protected workflow, upload its evidence,
    and approve every corresponding license file.
- [x] Add secret-free fork CI with `MECCHA_WITH_UE4SS=OFF`.
  - [x] Run the complete Linux graph under AddressSanitizer and
    UndefinedBehaviorSanitizer in addition to the Windows MSVC Release graph.
- [x] Add the maintainer-approved protected-ref full-build workflow.
- [x] Build UE4SS and the minimal mod from the same source/configuration.
  - [x] Reach `UE4SS.dll`, `dwmapi.dll`, and `main.dll` in a diagnostic build
    and inspect their x64 Shipping binary shape.
  - [x] Resolve the reviewed Cargo lock defect through a project-owned
    canonical one-file overlay in an independent immutable build stage,
    preserving the accepted gitlink and Cargo `--locked`; verify real-source
    preparation, reuse, offline locked metadata, and post-command identity.
  - [x] Require the pinned proxy target and make every `meccha_mod` build depend
    on that exact configured proxy target.
  - [x] Run the native immutable-stage build from a clean exact checkout and
    bind its binaries to that exact source-stage manifest.
  - [ ] Re-run and upload the same evidence through the protected GitHub
    environment.
- [x] Verify x64 Release ABI, imports, exports, runtime library, and provenance.
  - [x] Verify the binary architecture, imports, exports, and dynamic MSVC
    runtime from the diagnostic build.
  - [x] Bind protected provenance input to an explicit lowercase project commit
    and require the configured CMake source root, checkout HEAD, and tracked
    project/submodule state to match before reporting it.
  - [x] Bind those binaries to a clean, immutable source checkout and reject a
    verifier shell whose MSVC path/version differs from the configured CMake
    compiler.
- [x] Keep the accepted UE4SS gitlink and nested checkouts pristine; pin and
  enforce the explicitly approved build-stage Cargo lock as the only diff.
- [x] Confirm a clean full source build with the required Epic-linked GitHub
  access.

Evidence:

- [`dependency-lock.md`](dependency-lock.md)
- [`phase2-build-gate.md`](phase2-build-gate.md)
- `.github/workflows/v2-ci.yml`
- `.github/workflows/v2-full-build.yml`
- `tools/v2/verify_source_graph.py`
- `tools/v2/verify-full-build.ps1`
- `tools/v2/collect_dependency_evidence.py`
- `tools/v2/generate_dependency_audit_template.py`

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
  - [x] Compile a production adapter against the pinned UE4SS generic hook API,
    retain its exact callback ID pair, and validate the complete
    `ReceiveDrawHUD` reflected signature before registration.
  - [x] Own the adapter and application/UI graph from the exported mod
    composition root before any HUD callback registration.
  - [ ] Live-prove exact callback removal plus in-flight drain before
    destruction.
- [x] Implement the bounded game-thread scheduler.
- [x] Keep `on_update()` free of UObject and ProcessEvent access.
- [x] Implement project-owned generation-checked
  World/controller/HUD/Canvas rebinding.
  - [x] Derive each production identity member from a validated live
    `FWeakObjectPtr` serial and object index on the UE4SS game thread.
- [x] Implement structured compatibility failures and bounded diagnostics.
- [x] Implement representative retained Paint and Image Paint runtime
  operations on the game-thread path.
  - [x] Connect typed Paint capture, planning, dispatch, queue observation, and
    completion through `ApplicationRoot` with a fake game-thread runtime.
  - [x] Split frame, Paint-stroke, and transient-state scheduler execution
    ports, while keeping editor texture lifetime behind its single
    game-thread coordinator port, so no partial adapter can claim unsupported
    operations through no-op implementations.
  - [x] Validate the exact game-owned `PaintAtUVWithBrush`, `Vector2D`,
    `PaintChannelData`, and `RuntimeBrushSettings` reflected layouts; encode
    the captured texture dimension into the reviewed 0x68-byte call ABI; and
    compile the UE4SS `ProcessEvent` route under MSVC `/W4 /WX`.
  - [x] Validate the exact recorded/component/global queue and pressure
    schemas; require one exact non-default replication manager owned by the
    current World; and compile the generation/component-bound observer through
    the pinned UE4SS `ProcessEvent` API.
  - [x] Validate the exact preview export/import byte-array schemas and compile
    a generation-bound game-thread capture/apply/restore adapter with bounded
    Unreal-owned output cleanup and byte-for-byte post-import readback.
  - [x] Connect production Paint capture and representative Image Paint
    texture operations.
- [x] Implement restore-before-unregister explicit unload ordering.
  - [x] Cancel the active Paint generation and wait for local plus observed
    visual/outgoing queue drain before lifecycle quiescing.
  - [x] Stop and discard Paint preview builds, restore the exact project-owned
    texture snapshot on the game thread, then enter lifecycle quiescing.
  - [x] Drive one attached project-owned frame extension from the exact HUD
    identity, retry its UI/input/texture restoration on the game thread, and
    refuse lifecycle quiescing until that extension reports terminal stop.
  - [x] Bind the lifecycle's final transient-state operation to the production
    adapter's exact input-lease restoration and rooted Canvas-texture cleanup,
    rejecting wrong-thread and invalid-generation calls.
- [x] Pass fake-runtime, thread-affinity, lifecycle fault, and concurrent
  uninstall tests.
- [ ] DEFERRED — maintainer interaction required: verify live load, travel,
  HUD replacement, freecam, spectator, explicit unload, and game shutdown.

Evidence:

- [`phase4-runtime-gate.md`](phase4-runtime-gate.md)
- `src/mod/runtime/`

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
    - [x] Split frame input capture from Canvas rendering so a partial
      production adapter cannot claim input support; freeze exact UE 5.6
      `K2_DrawLine`, `K2_DrawTexture`, and `K2_DrawText` reflection/ABI
      contracts; and compile a generation/viewport-bound, complete-frame
      preflighted line/filled-box adapter against the pinned UE4SS graph.
    - [x] Inventory `/Game/UI/NotoFonts/MainFont.MainFont` as the exact cooked
      `Font` asset, resolve/load it only on the game thread, retain a weak
      generation identity, own strict UTF-8-to-terminated-UTF-16 storage for
      every preflighted call, and compile `K2_DrawText` dispatch against the
      pinned UE4SS graph.
    - [x] Freeze the exact UE 5.6
      `KismetRenderingLibrary.ImportBufferAsTexture2D` reflection/ABI contract
      and implement bounded deterministic cancellable RGBA-to-PNG input for
      the game-thread texture registry.
    - [x] Bind the texture coordinator to a game-thread-only reflected import
      port with project-owned non-reused handles, exact UObject generation
      checks, bounded rooted ownership, complete-frame texture preflight,
      partial-create rollback, retryable release, and forced teardown cleanup.
    - [x] Bind the statically proven printable-ASCII range to the exact game
      font and route unproven/missing or partially clipped glyphs through the
      strictly loaded, adapter-owned fallback atlas with complete-frame
      expansion/encoding before the first Canvas draw mutation.
    - [ ] DEFERRED — maintainer interaction required: live-prove complete
      line/box/text/texture output, localized visual metrics, and atlas
      lifetime without a partial frame mutation.
- [ ] Implement exact production cursor/look/movement lease restoration while
  preserving the game's current input mode unchanged.
  - [x] Implement a transactional lease controller that captures the complete
    prior input state once, avoids repeated mutation, restores exactly on
    close/shutdown, rolls back failed acquisition, and retains failed restore
    state for bounded retry.
  - [x] Validate the exact UE 5.6 `Controller` query/mutation reflection
    records and `PlayerController.bShowMouseCursor` property; implement the
    game-thread production port with balanced stack mutations, exact cursor
    restoration, controller-identity rebinding, retryable rollback, and
    restore-before-unregister refusal; compile and link it against the pinned
    UE4SS graph under MSVC `/W4 /WX`.
  - [ ] Live-verify reflection resolution, controller replacement, travel,
    shutdown restoration, and unchanged game input-mode behavior.
- [x] Implement responsive viewport/DPI scaling.
- [x] Implement localized game-font and packaged OFL fallback-glyph paths.
  - [x] Generate and package a pinned Noto-derived RGBA fallback atlas with an
    exact source/hash/OFL manifest and exact all-catalog codepoint inventory.
  - [x] Resolve the exact localized game font first through a validated soft
    object path and reject a stale/wrong-class font before frame mutation.
  - [x] Bind unproven/missing glyphs through the production UCanvas texture
    adapter and route absent atlas entries to the frozen replacement cell.
- [x] Render representative ESP primitives while the panel is closed.
- [x] Complete the retained portable two-layer Image Paint editor vertical
  slice with topmost selection, retained move/resize/crop/order transitions,
  immutable layer edits, Canvas atlas display, and selection handles.
- [x] Keep body guides above layers and outside the canonical atlas through a
  separate exact-profile/version-bound Canvas texture overlay.
- [x] Pass layout, hit-test, input-lease, glyph, editor, worker-result, and
  texture-lifetime tests.
  - [x] Pass portable Canvas clipping/resource and input-lease rollback/retry/
    shutdown contract tests.
  - [x] Pass strict atlas construction/coverage, mixed game-font/fallback,
    replacement-cell, glyph-level UV clipping, and expansion-overflow tests.
  - [x] Pass portable safe-area/DPI layout, clipped hit-test, exclusive pointer
    capture, focus, disabled-control, duplicate-ID, and same-frame click tests.
  - [x] Pass portable scroll clamp/hit tests and Canvas widget drawing,
    activation, value-mapping, duplicate-ID, and high-DPI scale tests.
  - [x] Pass portable keyboard navigation/activation/cancel tests and ordered
    bounded single-line UTF-8 edit/cursor/commit/cancel tests.
  - [x] Pass portable two-layer selection, move, resize, reorder, crop,
    cancellation, guide-ordering, identity, limit, and clip-unwind tests.
  - [x] Pass deterministic canonical PNG integrity/cancellation and exact
    texture-import ABI contract tests.
  - [x] Pass the complete 112-test Windows x64 Shipping graph from the exact
    synchronized project source tree while building UE4SS and `main.dll` from
    the manifest-verified canonical source stage; reverify that the stage
    remains the pinned upstream commit plus only the approved Cargo lock
    overlay after the build.
- [ ] DEFERRED — maintainer interaction required: verify the complete Canvas
  viability checklist in the live game.

Evidence:

- [`phase5-canvas-progress.md`](phase5-canvas-progress.md)

## Phase 6 — Pure core and application state machines

- [x] Port retained value types and validation to focused C++ modules.
- [x] Port profile, region, color/PBR, atlas-mapping, pacing, compression, ESP,
  and timing algorithms.
  - [x] Port bounded ESP role/spectator filtering, avatar-cache policy,
    perspective projection, capsule/pose bounds, viewport clipping, and
    frame-primitive construction.
- [x] Implement typed commands and immutable revisioned snapshots.
- [x] Implement bounded concurrent typed-command admission, FIFO frame drains,
  close/discard shutdown, and invalid command-ID rejection.
- [x] Implement job arbitration, cancellation generations, preview ownership,
  queue pressure, terminal drain, and shutdown coordination.
- [x] Add checked arithmetic and resource limits.
  - [x] Bound decoded Image Paint dimensions/bytes, layer/source counts, and
    canonical-atlas composition work before allocation or pixel traversal.
  - [x] Complete the remaining cross-feature checked-arithmetic and
    resource-limit audit.
- [x] Pass unit, property, golden, state-machine, dependency-boundary, and
  supported sanitizer/static-analysis checks.
  - [x] Pass all registered secret-free tests under Linux ASan/UBSan and keep
    the sanitized job mandatory in public PR CI.
  - [x] Complete remaining property/golden and supported static-analysis
    evidence.

Evidence:

- [`phase6-core-progress.md`](phase6-core-progress.md)
- [`resource-limit-audit.md`](resource-limit-audit.md)
- [`static-analysis.md`](static-analysis.md)

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
  - [x] Verify the packaged fallback atlas exactly covers every codepoint in
    all 16 catalogs and localized locale names.
  - [ ] Verify localized game-font-first selection in the live runtime.
- [x] Specify the canonical v2-only `.mcpreset` container.
- [x] Implement project save/load/rename/delete and content-addressed sources.
- [x] Run named project load/save/rename/delete on one owned I/O worker with
  copied immutable requests, typed results, exception containment, reuse, and
  terminal shutdown.
- [x] Coordinate editor decode/composition, active-draft debounce, optimistic
  named project operations, typed completions, and terminal shutdown through
  one project-owned session wired to `ApplicationRoot`.
- [x] Construct the production Windows Image Paint service graph from the
  isolated v2 config/project stores, native preset hasher, WIC/libwebp decoder,
  core atlas composer, persistence coordinator, and one owned editor session.
- [x] Run startup recovery before runtime callback registration, submit the
  recovered named/draft project to the derived pipeline exactly once, publish
  its source/diagnostics in the immutable session snapshot, and fail root
  initialization closed when recovery cannot start.
- [x] Reject v1 presets with the explicit non-destructive legacy result.
- [x] Pass schema, corruption, hostile-container, fault-injection, recovery,
  determinism, and resource-limit tests.
- [ ] DEFERRED — maintainer interaction required: verify native file-picker and
  non-ASCII Windows path behavior.

Evidence:

- [`phase7-persistence-progress.md`](phase7-persistence-progress.md)

## Phase 8 — Paint

- [ ] Implement typed game-thread capture and exact runtime contracts.
  - [x] Implement dependency-free exact reflection-schema validation and the
    reviewed Paint call encoder, including sRGB-to-linear color, normalized
    brush radius, AMRE channel selection, and ABI offset/size assertions.
  - [x] Isolate UE4SS record description and exact schema validation in a
    private runtime translation unit so lifecycle, hook, and object-ownership
    coordination do not own reflection traversal details.
  - [x] Implement the production queue observer with exact function/struct
    schemas, unique current-World manager discovery, owned component counters,
    sticky per-job activity, and negative/non-finite counter rejection.
  - [x] Decode the three packaged deformation profiles, capture exact
    world-space bone transforms through `GetSocketTransform`, and build
    cancellable project-owned skinning/projection/sample materialization
    entirely after the game-thread capture boundary.
  - [x] Freeze typed current-build `CreateRenderTarget2D` 0x38 and
    `ReadRenderTargetRaw` 0x28 reflection/parameter contracts, including the
    `Slices` input and `TArray<LinearColor>` output, bounded dimensions,
    finite readback validation, and struct-array reflection description.
  - [ ] Implement production mesh/sample/appearance capture.
    - [x] Freeze the exact 0x10-byte `InitializePaint` reflected schema as
      `MeshComponent` object input at 0x00 and bool return at 0x08; the v1
      zero-filled no-argument call is not accepted.
    - [x] Resolve and validate the exact SceneCapture actor/component class
      graph, typed capture/projection/render-target properties, and
      `CaptureScene`, `HideComponent`, and `K2_DestroyActor` schemas.
    - [x] Materialize the manual-color capture seed from the shared calibrated
      camera, profile-bound initialized mesh, complete packaged-bone transform
      set, BaseColor background, and optional FinalColor-HDR background, with
      bounded readback and local actor/array/root cleanup on every exit.
    - [x] Freeze the current cooked
      `/Game/BluePrints/cLeon/BP_BrushPlane.BP_BrushPlane_C` visual contract
      and hide exact current-World `Plane`, `Plane1`, and `Niagara` components
      from every capture pass, rejecting missing, duplicate, wrong-class,
      wrong-owner, or stale matches.
    - [ ] Add Auto Material intrinsic-emission plus trial-preview
      feedback/restoration, prove bounded multi-frame capture cost, and record
      live brush-plane/readback orientation/color/cleanup evidence before
      connecting the composition root.
      - [x] Route Auto Material admission through a generation-tagged
        multi-HUD-frame runtime session instead of the synchronous manual
        capture port; publish no capture before runtime-confirmed restoration,
        and retain ownership through cancellation, failure cleanup, and
        shutdown.
      - [x] Port the bounded HDR/emission/calibration/SPSA/acceptance
        algorithms, surface-aware emission-halo rejection, and immutable
        candidate/evaluation worker without runtime types.
      - [x] Define immutable BaseColor, FinalColor HDR, tone-curve HDR,
        intrinsic-emission HDR, normal, depth, and FinalColor LDR capture
        evidence with per-pass camera fingerprints, exact projected-raster
        observation construction, and changed-camera rejection.
      - [x] Split cancellable appearance preparation into owned deformation/
        projection plus a deduplicated nearest-depth source-query plan,
        followed by immutable evidence-to-model preparation, without
        full-raster game-thread object queries.
      - [x] Preserve finite raw HDR and scene-depth readback channels in the
        runtime codec without display normalization or clamping.
      - [x] Distinguish background-only source passes from target-visible
        feedback passes, build immutable preview-byte readback references on
        the worker, and require stable source/feedback cameras plus calibrated
        identity or red/blue-swapped linear response before evaluation.
      - [x] Admit the exact profile, current bone transforms, and calibrated
        camera into the production generation-tagged session; prepare geometry
        on the worker, capture at most one background-only source pass per
        later HUD frame, prepare the immutable model on the worker, and drain
        generation-owned cancellation without publishing or mutating a
        preview. Retain the real viewport in every camera fingerprint when the
        bounded capture raster is scaled.
      - [x] Freeze the exact 0x10
        `SceneCaptureComponent.SetShowFlagSettings` array schema and require
        exact ordered readback of the reviewed 33 intrinsic-emission flags.
      - [x] Capture the exact original preview channels, compose an immutable
        zero-emission E0 candidate on the worker, apply and verify both trial
        channels, wait across later HUD frames, capture at most one of the
        three target-visible feedback passes per frame, restore and verify the
        original channels before worker analysis, and retain restoration
        ownership through failure, cancellation, and game-thread teardown.
      - [x] Run the deterministic three-iteration, six-candidate cluster-local
        SPSA session across later HUD frames, evaluate each candidate only
        after verified exact restoration, enforce stable-camera/calibrated-
        readback/minimum-pair response gates, and freeze the response-gated
        Emissive values throughout refinement.
      - [x] Freeze the exact `HitTestAtScreenPosition` 0x70 parameter and
        `FScreenSpacePaintResult` 0x48 result contracts; retain at most the
        nearest packaged sample per projected raster pixel, cap the evenly
        selected query plan at 8192 entries, execute at most 32 cached hit
        tests per later HUD frame, and admit a non-zero topology triangle
        surface key only when the game-owned hit returns to that sample within
        the retained one-centimetre same-surface tolerance and its returned UV
        lies inside the same packaged topology triangle. Camera drift, invalid
        hit vectors, and unqueried/occluded samples remain fail-closed.
      - [x] Retain the production endpoint policy as an ordered worker/runtime
        transaction: evaluate the safe baseline and exact E=1 response
        endpoint, calibrate and response-gate Emissive per cluster, evaluate
        the exact A=0 endpoint, calibrate bounded Albedo plus robust
        per-channel chromaticity gains, and evaluate the non-emission candidate
        before refinement. Admit RGB feedback only after its median/tail
        chromaticity and fit epoch gates; otherwise replay the parameter
        baseline without RGB feedback before SPSA.
      - [x] Re-export and byte-compare the complete game-owned packed AMRE
        channel for every candidate, aggregate that proof across the session,
        apply the response-gated effective emission ROI recall/stability rules,
        and resolve only the strictly accepted candidate or the exact safe
        fallback on the worker. Publish the immutable captured job only after
        verified exact restoration; cancellation, candidate failure, and
        fallback selection cannot publish a mutated or partially verified
        preview.
- [x] Validate round, cube, and fukuyoka profiles.
  - [x] Refresh the Round raw/image-reference pair to the game 3.3.0 identity
    shipped by v1.7.1: 1,668 vertices, 8,352 indices, and profile hash
    `cd469e35ad0cbd1e483bd82b2406849429d24037807bd7a294534fb79633f55b`.
- [x] Implement immutable planning for Paint/Fill/Skip, lighting, Auto Material,
  PBR, compression, and Fill-first ordering.
  - [x] Preserve Back, Side, then Front dispatch within both Fill/Paint replay
    passes and adaptive-compression output, with scanline/radius ordering only
    inside each region.
  - [x] Share direct projected material evidence across Front and Back while
    retaining topology-visible, camera-facing admission for Side samples.
- [x] Run Paint deformation, projection, sample materialization, and planning
  on one owned cancellable worker from an immutable capture seed, with
  generation-tagged results and an exception boundary.
- [x] Coordinate planning, dispatch, drain, planning cancellation, typed
  failure, and stale-result rejection through the shared job generation.
- [x] Implement the bounded typed albedo/packed-PBR preview snapshot controller,
  ownership guards, apply recovery, replacement restore, and shutdown restore.
- [x] Implement cancellable bounded Paint-plan composition over immutable
  original albedo/packed-PBR channels without mutating the restore snapshot.
- [x] Run Paint preview deformation, projection, sample materialization,
  planning, and composition on one owned cancellable worker with immutable
  input, generation-tagged results, typed failures, and an exception boundary.
- [x] Connect typed Start/Cancel Paint, capture, planning, dispatch, queue
  observation, progress, and terminal completion through `ApplicationRoot`.
- [x] Connect typed Preview/Restore Paint through `ApplicationRoot`, including
  restore-before-real-Paint ordering and game-thread-only capture/apply.
- [x] Implement exact preview capture/apply/restore ownership.
  - [x] Bind exact 0x20-byte export/import schemas to the acknowledged-body
    component; capture Albedo plus packed PBR on the game thread; free
    Unreal-owned output through the pinned allocator; and verify every import
    through exact byte readback before reporting success.
  - [x] Connect the production adapter to the exported composition root.
  - [ ] Complete live preview/restore evidence.
- [x] Dispatch only through game-owned `PaintAtUVWithBrush`.
  - [x] Make the production Paint-stroke port resolve only
    `/Script/PenguinHotel.RuntimePaintableComponent:PaintAtUVWithBrush`,
    reject schema/owner/object-generation drift, and invoke it on the UE4SS
    game thread without a second sender.
  - [x] Connect that port to the exported production composition root.
  - [ ] Complete single-/two-client live evidence.
- [x] Implement bounded per-frame pacing, progress, cancellation, and terminal
  queue drain.
- [ ] Preserve valid jobs through freecam/controller-pawn changes.
  - [x] Retain the same generation-bound acknowledged-body component for the
    sender and queue observer only during a same-World/same-controller
    freecam transition.
- [x] Pass all Paint unit, contract, fake-runtime, stress, and failure tests.
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
- [x] Implement the full editable layer and project model.
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
  - [x] Connect selected-layer removal through an index/asset-guarded mutation
    that preserves the non-empty project invariant and removes newly orphaned
    encoded source ownership in the same application transaction.
  - [x] Connect those values to the complete in-game editor lifecycle.
- [x] Implement deterministic cancellable 1024×512 RGBA composition.
  - [x] Run composition on one owned worker with copied immutable inputs,
    one active generation, typed cancellation/failures, revision-tagged
    results, exception containment, and terminal shutdown.
  - [x] Reject stale project revisions in the application/editor owner.
- [ ] Implement versioned round/cube/fukuyoka guide overlays.
  - [x] Define an exact image-profile/schema-bound guide texture contract and
    render it separately after the canonical atlas.
  - [x] Generate deterministic locale-independent 1024×512 guide bitmaps from
    the exact three packaged reference profiles, including validated component
    transforms, body silhouettes, reference-pose skeletons, and face bounds.
  - [x] Retain the matching immutable decoded source pixels with each ready
    atlas and own atlas/source/guide opaque handles through a game-thread-only,
    rollback-safe, bounded, retryable texture coordinator.
  - [x] Bind the texture coordinator's narrow port to exact reflected
    `ImportBufferAsTexture2D` creation, rooted project ownership, generation-
    validated Canvas lookup, and release/teardown cleanup.
  - [ ] Prove production texture creation/release through HUD replacement,
    travel, and unload.
- [x] Implement game-thread preview texture lifetime.
- [ ] Implement all profile mappings and reuse the accepted Paint dispatch.
  - [x] Decode immutable reference vertices/indices from each exact packaged
    image profile, validate canonical bounds/topology, and map
    triangle+barycentric captures for every round/cube/fukuyoka triangle into
    the four atlas faces.
  - [x] Convert exact-profile UV samples, barycentric image anchors, and
    canonical atlas pixels into the shared immutable `PaintPlan`, with the
    canonical mapping performed inside the planner, independent face Fill/Skip,
    alpha/background routing, Fill-first ordering, material isolation,
    compression, cancellation, and resource bounds.
  - [x] Run Image Paint planning on one owned worker with copied immutable
    profile/atlas input, one active generation, typed cancellation and
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
  - [x] Implement the shared production UE4SS queue-observation adapter.
  - [x] Implement the production UE4SS profile-bound Image Paint capture
    adapter.
    - [x] Decode a project-owned immutable Paint sampling profile from each
      exact raw packaged profile, retaining only validated UV/topology/island
      values required by planning.
    - [x] Generate bounded brush-spacing UV samples and triangle barycentric
      anchors inside the owned planning worker, with cancellation and exact
      raw/image topology matching; never scan a runtime component for an
      unreflected triangle cache.
    - [x] Expose that deterministic expansion through one project-owned core
      sampler and make the Image Paint planner consume it, so future
      profile-derived Paint capture cannot fork the topology walk, limits, or
      cancellation contract.
    - [x] Implement an immutable catalog loader that validates all three
      raw/image profile pairs and require that catalog at runtime-adapter
      construction, with no profile I/O or parsing in HUD callbacks.
    - [x] Preconstruct one immutable production resource bundle from an
      absolute resource root, including the exact profile catalog, all 16
      localization catalogs, and the three generated guide bitmaps, with
      bounded reads and typed fail-closed construction errors.
    - [x] Derive the exact resource root from the loaded
      `Mods/MecchaCamouflage/dlls/main.dll` module path and make the exported
      mod owner construct and retain that bundle before any callback or key
      registration.
    - [x] Inject the retained profile catalog and shared input queue into one
      exported-mod-owned runtime adapter without registering callbacks or
      touching UObjects during construction.
    - [x] Complete exported composition-root ownership, consume the retained
      localization and guides in the UI graph, and initialize the whole graph
      before callback registration.
    - [x] On the game thread, validate the live RuntimePaintable target mesh
      and exact `SkinnedAsset` path/export against the selected catalog entry
      before returning the acknowledged component and pacing.
    - [ ] Prove the reflected target-mesh and asset-identity gate against the
      supported live game through replacement, travel, freecam, spectator, and
      unload transitions.
- [x] Integrate v2 project/preset management.
  - [x] Connect the v2-only project store and session transaction boundary to
    the application command variant and composition root.
  - [x] Connect persisted startup recovery to the root initialization boundary
    before UE4SS callback registration.
  - [x] Compose the production Windows persistence, decoder, compositor, and
    editor-session services behind one lifetime owner, with typed construction
    failure and terminal worker shutdown.
  - [x] Connect current-project Save/Rename/Delete to the portable UCanvas
    toolbar with typed actions, bounded name editing, and explicit delete
    confirmation.
  - [x] Connect native picker/import/load and the portable UCanvas project
    controls through latest-snapshot effect execution.
  - [x] Bind those effects and controls to the production callback boundary.
- [x] Pass decoder, layer, guide, atlas, mapping, preset, fake-runtime, and
  resource-limit tests.
  - [x] Pass portable editor gesture/crop/order and guide overlay isolation/
    identity/ordering/limit tests.
  - [x] Pass exact-profile guide generation, deterministic rasterization,
    strict reference-transform parsing, ready-content retention, and
    game-thread texture rollback/retry/shutdown tests.
- [ ] DEFERRED — maintainer interaction required: complete the full Image Paint
  visual/editor/live-paint checklist.

Evidence:

- [`phase9-image-paint-progress.md`](phase9-image-paint-progress.md)

## Phase 10 — ESP

- [x] Implement coherent game-thread target/role/avatar/pose capture.
- [x] Implement project-owned projection and Canvas primitive rendering.
  - [x] Implement bounded project-owned target values, role/spectator
    filtering, projection, clipping, capsule/pose bounds, and line/text
    primitive construction without graphics or Unreal dependencies.
  - [x] Coordinate exact-HUD-frame capture/build/draw through a typed runtime
    port and publish immutable frame diagnostics.
  - [x] Implement production UCanvas line/text drawing through the one
    validated, complete-frame Canvas renderer with exact HUD-frame identity.
  - [x] Implement the production UE4SS target capture adapter.
    - [x] Capture the exact current-build cLeon GameState roster, role,
      spectator state, PlayerState/Pawn identity, bounded UTF-8 name, camera
      view, and character capsule samples on the game thread through validated
      reflection contracts.
    - [x] Calibrate independent projection axes through the exact reflected
      engine projection function and fail closed on inconsistent results.
    - [x] Capture and validate the game-specific skeletal pose and topology
      selected by exact packaged-profile asset path.
- [x] Implement every scope, primitive, role color, and clipping rule.
  - [x] Cover every configured scope, primitive toggle, role color, and
    viewport/behind-camera clipping rule in the pure builder.
- [x] Invalidate/rebuild on role, avatar, spectator, travel, HUD, and lifetime
  changes.
  - [x] Specify and test the project-owned role/avatar cache policy and reject
    stale HUD frame identities before draw.
  - [x] Apply the policy to validated production weak Unreal handles across
    travel, HUD, freecam, spectator, role, and avatar transitions.
- [x] Pass projection, filtering, skeleton, invalidation, fake-runtime, and
  forbidden-hook tests.
  - [x] Pass pure projection/filtering/skeleton/resource tests, coordinator
    failure tests, and root integration proving ESP remains active while the
    panel is closed.
  - [x] Pass production capture/projection/skeleton ABI, topology,
    weak-directory refresh-policy tests, the forbidden-hook source audit, and
    the Windows UE4SS production-adapter build.
- [ ] DEFERRED — maintainer interaction required: complete all live ESP and
  lifecycle checks.

Evidence:

- [`phase10-esp-progress.md`](phase10-esp-progress.md)

## Phase 11 — Full product UI and integration

- [x] Complete Paint, Image Paint, ESP, Settings, and Diagnostics sections.
  - [x] Define a bounded application-owned five-section presentation model
    containing exact current settings, editor/project readiness, feature and
    project action availability, ESP state, progress, queue utilization,
    compatibility, and newest bounded diagnostics.
  - [x] Compose a responsive localized five-tab Canvas shell with Paint/Image
    action rows, ESP toggle, and bounded status output in a separate
    application-to-UI target without changing the Core-only UI primitives.
  - [x] Compose the complete portable Settings section for language, UI scale,
    RGB theme color, and all nine F1–F24 mappings with section-local scrolling,
    direct key capture, duplicate refusal, cancellation, and input-loss reset.
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
  - [x] Compose a separate Image Paint project toolbar with a bounded UTF-8
    display-name editor, named-project Save, and two-step current-project
    Delete with keyboard cancellation and exact availability gating.
  - [x] Render and interact with every complete section through the production
    UCanvas adapter.
- [x] Bind UI only to typed commands and immutable snapshots.
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
  - [x] Route current-project Save, Rename, and confirmed Delete through their
    existing snapshot-bound typed product actions without exposing project
    identity or persistence objects to the Canvas layer.
  - [x] Connect every portable Canvas widget/editor activation and native
    picker effect through one game-thread frame coordinator that enqueues
    typed output only after successful frame rendering.
  - [x] Attach that coordinator to `ApplicationRoot` through one project-owned
    HUD-frame extension contract with typed compatibility failures and
    restore-before-callback-unregistration ordering.
  - [x] Invoke the frame coordinator from the production UE4SS HUD callback.
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
  - [x] Connect localized selected-layer removal through the product panel,
    disable removal for the final layer, and publish only one guarded typed
    mutation.
  - [ ] Connect import and body-guide generation/lifetime through the product
    panel.
    - [x] Add separate revision-bound Load-project and Add-images picker
      effects, keep Load available without an active document, and capture the
      exact project ID/revision for Add-images.
    - [x] Implement the native Windows `IFileOpenDialog` boundary for
      PNG/JPEG/WebP multi-selection and one `.mcpreset`, returning only bounded
      validated bytes and file names to the application layer.
    - [x] Implement hash-derived source identity, existing/new source
      deduplication, layer/source/byte-limit refusal, and one atomic
      `AddImageLayersMutation` through the router/session/pipeline boundary.
    - [x] Execute picker effects outside frame composition, revalidate the
      latest immutable snapshot after the modal dialog, and enqueue the
      resulting typed image/preset operation.
    - [x] Decode, publish, and activate selected v2 presets on the persistence
      worker, reuse an exact existing project, and refuse a differing project
      with the same ID without overwriting it.
    - [x] Generate all three exact packaged-profile guide bitmaps, draw the
      localized Front/Right/Back/Left labels separately through Canvas, retain
      decoded crop-source content, and own atlas/source/guide handle
      generations through the game-thread texture coordinator.
    - [x] Bind the texture coordinator to the production Unreal runtime port
      and require resource-free normal callback teardown.
    - [ ] Live-verify reflected texture import, travel persistence, and
      validated callback teardown.
- [ ] Complete F9 and configurable F1–F8 hotkey behavior.
  - [x] Implement a bounded application-owned F1–F24 command router with the
    F9/F1–F8 defaults, validated remapping, physical-key repeat suppression
    until release, immutable snapshot capture, unavailable-project rejection,
    one command-ID sequence shared with Canvas actions, concurrent admission,
    input-loss release, and terminal shutdown.
  - [x] Expose all nine current mappings as localized, duplicate-free portable
    Settings controls while preserving a complete validated config.
  - [x] Replace click-to-cycle remapping with an explicit localized capture
    state that accepts the next exact F1–F24 press, refuses duplicate or
    out-of-range input without publishing settings, treats the current mapping
    as a no-op, and clears on Esc, input loss, unavailable application state,
    tab departure, or panel close.
  - [x] Consume bounded registered-key event batches through the frame
    coordinator, hand the next press to Settings capture when armed, suppress
    repeats through the shared router, and release held state on input loss.
  - [x] Implement a bounded thread-safe callback-to-HUD-frame input queue that
    validates F1–F24 and single-line UTF-8 text events, converts each
    edge-only function-key callback into one ordered Pressed/Released pair,
    rejects an overflowing frame without partial publication, discards input
    on focus loss, and makes callbacks inert on stop.
  - [x] Register all supported keys once and connect the production UE4SS input
    callbacks to the router.
    - [x] Implement a one-shot F1–F24 registration owner with bounded typed
      failure, partial-registration fail closure, callback-safe shared queue
      lifetime, explicit stop-before-derived-destruction, and portable,
      sanitizer, and Windows MSVC Release tests.
    - [x] Inject the same callback queue into the production runtime adapter
      and drain it only from a validated, focused HUD frame after mapping a
      current-process game-window pointer into Canvas coordinates. Suppress
      initially held, focus-return, and replacement-window button state,
      publish exact press/hold/release edges, discard pending input on focus
      loss, and fail closed on stale frame, invalid window/client/DPI, stopped
      queue, or bounded overflow.
    - [x] Start registration only after the exported composition root owns the
      matching HUD-frame queue consumer.
    - [x] Register the frozen navigation/edit/printable key set once through
      the same mod-owned UE4SS input API; translate the current Windows
      keyboard layout to bounded strict UTF-8 only while the project-name
      field is editing, and discard stale keyboard/text edges on panel,
      section, focus, or edit-mode transitions without installing a window
      hook.
    - [ ] Live-prove UE4SS callback removal.
- [x] Complete progress, backpressure, compatibility, and diagnostics display.
  - [x] Publish validated progress fractions, queue pressure/utilization,
    compatibility state, and newest bounded ordered diagnostics as portable
    presentation values.
  - [x] Draw localized completed/total/percentage, elapsed time, optional ETA,
    and command-queue counts in a clipped portable status strip, without
    inventing an unavailable ETA.
  - [x] Draw localized runtime/compatibility/queue summaries and bounded
    actionable diagnostic entries through the portable Canvas panel.
  - [x] Bind those values through the production UCanvas adapter.
- [x] Complete responsive themes, DPI, clipping, fonts, and all 16 languages.
  - [x] Apply safe-area, viewport, DPI, configured scale, clipping, and theme
    accent to the portable panel shell and compose its labels for every one of
    the 16 shipped catalogs.
  - [x] Retain bounded per-section scroll state and exclude clipped Settings,
    Paint, Image Paint, and ESP controls from keyboard focus/action admission.
  - [x] Bind a deterministic packaged fallback-atlas inventory to every
    shipped localized codepoint without adding a runtime font library.
  - [x] Strictly load that inventory into the production composition root and
    bind its generation-tracked texture through the central Product UI/ESP
    Canvas render path.
- [x] Ensure panel close preserves ESP and active jobs.
- [x] Pass UI action/state, gesture, hotkey, layout, and localization tests.
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
  - [x] Prove selected-layer removal routing, project/revision/index/asset
    guards, orphan-source cleanup, revision publication, and final-layer
    refusal across the session, action router, and product panel.
  - [x] Prove bounded current-project name editing, empty-name refusal,
    snapshot-bound Save/Rename, two-step Delete, keyboard cancellation,
    invalid snapshot-name refusal, and unavailable-control suppression.
  - [x] Prove localized progress/elapsed/ETA/queue formatting, exact
    millisecond duration rendering, absent-ETA presentation, status clipping,
    and fail-closed progress arithmetic validation.
  - [x] Prove direct F1–F24 Settings capture, exact isolated remapping,
    localized duplicate refusal, Esc cancellation, input-loss reset,
    unavailable-state reset, tab/panel-close reset, and out-of-range refusal.
  - [x] Prove callback-thread input validation, exact edge ordering, event and
    byte bounds, overflow recovery without partial publication, focus-loss
    discard, guarded navigation/text modes, unique bounded key registration,
    modifier-aware translation admission, one-terminal-event-per-frame
    enforcement, and terminal callback refusal.
  - [x] Prove Load/Add-images picker-effect isolation, no-document Load,
    unavailable-control suppression, exact project/revision capture, source
    deduplication, source-byte/layer limits, hash/collision failure, immutable
    mutation bytes, and atomic editor revision publication.
  - [x] Prove pre-dialog and post-dialog snapshot validation, cancellation,
    latest-revision action binding, immutable preset bytes, off-thread preset
    import/activation, exact-project reuse, same-ID conflict refusal, busy
    exclusion, and root command routing.
  - [x] Prove exact-profile guide decoding/generation for all three body types,
    deterministic four-face output, strict reference-transform validation,
    matching ready atlas/source ownership, localized guide-label ordering,
    game-thread texture publication, partial-create rollback, bounded release
    retry, clear, and terminal shutdown.
  - [x] Prove full-frame immutable snapshot/model/localization, bounded
    hotkey/capture routing, render-before-action ordering, modal-effect
    rebinding, exact input rollback, texture-generation synchronization, and
    retryable terminal shutdown through a fake runtime port.
- [ ] DEFERRED — maintainer interaction required: complete the full in-game UI
  walkthrough across languages/resolutions/DPI.

Evidence:

- [`phase11-ui-progress.md`](phase11-ui-progress.md)

## Phase 12 — Launcher, payload, and CI hardening

- [x] Complete launcher switches and every managed/shared lifecycle.
- [ ] Assemble the minimal release UE4SS configuration.
  - [x] Implement an exact trusted-build runtime assembler that disables
    UE4SS consoles/debug UI/hot reload/crash dumps, packages only the product
    C++ mod and retained resources/licenses, and emits the canonical layout
    input without modifying the pinned UE4SS source.
  - [x] Replace the unused v1 D-DIN font family with the exact reviewed
    fallback atlas, manifest, and Noto CJK OFL license; refuse assembly if
    atlas provenance, bytes, geometry, or catalog coverage drift.
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
  - [x] Bind every public v2 job to the exact pull-request head/push commit,
    keep the legacy recursive v1 job off the rewrite pull request only, bound
    GCC analyzer parallelism, and cover hosted-runner timing plus Windows 8.3
    dependency-root aliases without weakening reparse rejection.
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

- [x] Inventory every live/protected check with its required environment,
  owner, procedure, evidence header, and current non-pass result; map the
  release-critical uncertainty into the risk register.
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
