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
- [x] Add secret-free fork CI with `MECCHA_WITH_UE4SS=OFF`.
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
- [ ] Implement managed, exact shared, and conflict modes.
- [ ] Implement original-user preparation and minimal elevated broker.
- [ ] Implement safe reuse, repair, update, cleanup, and `--remove`.
- [ ] Pass temporary-tree, hostile-path, reparse, interrupted-transaction,
  shared-isolation, and alternate-credential tests.
- [ ] DEFERRED — maintainer interaction required: prepare and launch on a
  supported live Windows host after stopping the currently running game.
- [ ] DEFERRED — maintainer interaction required: observe writable and
  elevation-required managed preparation.
- [ ] DEFERRED — maintainer interaction required: verify exact shared UE4SS
  coexistence without modifying unrelated content.

Evidence:

- [`phase3-deployment-gate.md`](phase3-deployment-gate.md)

## Phase 4 — Runtime lifecycle and teardown gate

- [ ] Implement the composition root and runtime state machine.
- [x] Implement callback registration, in-flight barrier, and exact
  unregistration.
- [x] Implement the bounded game-thread scheduler.
- [x] Keep `on_update()` free of UObject and ProcessEvent access.
- [ ] Implement generation-checked World/controller/HUD/Canvas rebinding.
- [ ] Implement structured compatibility failures and bounded diagnostics.
- [ ] Implement representative retained Paint and Image Paint runtime
  operations on the game-thread path.
- [x] Implement restore-before-unregister explicit unload ordering.
- [ ] Pass fake-runtime, thread-affinity, lifecycle fault, and concurrent
  uninstall tests.
- [ ] DEFERRED — maintainer interaction required: verify live load, travel,
  HUD replacement, freecam, spectator, explicit unload, and game shutdown.

Evidence:

- [`phase4-runtime-gate.md`](phase4-runtime-gate.md)

## Phase 5 — UCanvas feasibility gate

- [ ] Implement retained immediate-mode Canvas controls and layout.
- [ ] Implement frame-scoped lines, boxes, text, clipping, and textures.
- [ ] Implement exact cursor/look/movement/input-mode lease restoration.
- [ ] Implement responsive viewport/DPI scaling.
- [ ] Implement localized game-font and packaged OFL fallback-glyph paths.
- [ ] Render representative ESP primitives while the panel is closed.
- [ ] Complete the retained two-layer Image Paint editor vertical slice.
- [ ] Keep body guides above layers and outside the canonical atlas.
- [ ] Pass layout, hit-test, input-lease, glyph, editor, worker-result, and
  texture-lifetime tests.
- [ ] DEFERRED — maintainer interaction required: verify the complete Canvas
  viability checklist in the live game.

## Phase 6 — Pure core and application state machines

- [ ] Port retained value types and validation to focused C++ modules.
- [ ] Port profile, region, color/PBR, atlas-mapping, pacing, compression, ESP,
  and timing algorithms.
- [ ] Implement typed commands and immutable revisioned snapshots.
- [ ] Implement job arbitration, cancellation generations, preview ownership,
  queue pressure, terminal drain, and shutdown coordination.
- [ ] Add checked arithmetic and resource limits.
- [ ] Pass unit, property, golden, state-machine, dependency-boundary, and
  supported sanitizer/static-analysis checks.

## Phase 7 — Configuration, localization, and persistence

- [ ] Specify and implement the strict v2 config schema.
- [ ] Keep image bytes and layer collections out of `config.json`.
- [ ] Implement atomic config and active-draft persistence.
- [ ] Port all 16 localization catalogs and validate placeholders/glyphs.
- [ ] Specify the canonical v2-only `.mcpreset` container.
- [ ] Implement project save/load/rename/delete and content-addressed sources.
- [ ] Reject v1 presets with the explicit non-destructive legacy result.
- [ ] Pass schema, corruption, hostile-container, fault-injection, recovery,
  determinism, and resource-limit tests.
- [ ] DEFERRED — maintainer interaction required: verify native file-picker and
  non-ASCII Windows path behavior.

## Phase 8 — Paint

- [ ] Implement typed game-thread capture and exact runtime contracts.
- [ ] Validate round, cube, and fukuyoka profiles.
- [ ] Implement immutable planning for Paint/Fill/Skip, lighting, Auto Material,
  PBR, compression, and Fill-first ordering.
- [ ] Implement exact preview capture/apply/restore ownership.
- [ ] Dispatch only through game-owned `PaintAtUVWithBrush`.
- [ ] Implement bounded per-frame pacing, progress, cancellation, and terminal
  queue drain.
- [ ] Preserve valid jobs through freecam/controller-pawn changes.
- [ ] Pass all Paint unit, contract, fake-runtime, stress, and failure tests.
- [ ] DEFERRED — maintainer interaction required: complete single-client Paint
  live checks.
- [ ] DEFERRED — maintainer interaction required: complete host-painter and
  joining-client-painter two-client checks.

## Phase 9 — Image Paint

- [ ] Implement bounded WIC PNG/JPEG decoding.
- [ ] Select, pin, license, and implement a bounded WebP decoder.
- [ ] Implement the full editable layer and project model.
- [ ] Implement deterministic cancellable 1024×512 RGBA composition.
- [ ] Implement versioned round/cube/fukuyoka guide overlays.
- [ ] Implement game-thread preview texture lifetime.
- [ ] Implement all profile mappings and reuse the accepted Paint dispatch.
- [ ] Integrate v2 project/preset management.
- [ ] Pass decoder, layer, guide, atlas, mapping, preset, fake-runtime, and
  resource-limit tests.
- [ ] DEFERRED — maintainer interaction required: complete the full Image Paint
  visual/editor/live-paint checklist.

## Phase 10 — ESP

- [ ] Implement coherent game-thread target/role/avatar/pose capture.
- [ ] Implement project-owned projection and Canvas primitive rendering.
- [ ] Implement every scope, primitive, role color, and clipping rule.
- [ ] Invalidate/rebuild on role, avatar, spectator, travel, HUD, and lifetime
  changes.
- [ ] Pass projection, filtering, skeleton, invalidation, fake-runtime, and
  forbidden-hook tests.
- [ ] DEFERRED — maintainer interaction required: complete all live ESP and
  lifecycle checks.

## Phase 11 — Full product UI and integration

- [ ] Complete Paint, Image Paint, ESP, Settings, and Diagnostics sections.
- [ ] Bind UI only to typed commands and immutable snapshots.
- [ ] Complete all editor interactions and body-guide behavior.
- [ ] Complete F9 and configurable F1–F8 hotkey behavior.
- [ ] Complete progress, backpressure, compatibility, and diagnostics display.
- [ ] Complete responsive themes, DPI, clipping, fonts, and all 16 languages.
- [ ] Ensure panel close preserves ESP and active jobs.
- [ ] Pass UI action/state, gesture, hotkey, layout, and localization tests.
- [ ] DEFERRED — maintainer interaction required: complete the full in-game UI
  walkthrough across languages/resolutions/DPI.

## Phase 12 — Launcher, payload, and CI hardening

- [ ] Complete launcher switches and every managed/shared lifecycle.
- [ ] Assemble the minimal release UE4SS configuration.
- [ ] Generate canonical manifest and deterministic CAB.
- [ ] Embed payload, resources, licenses, and notices.
- [ ] Build runtime and mod from the same trusted graph.
- [ ] Complete secret-free PR CI and protected full-build/release CI.
- [ ] Add binary, provenance, payload, license, and forbidden-artifact checks.
- [ ] Decide and document code-signing policy without modifying Defender.
- [ ] Produce exactly one EXE and its SHA-256.
- [ ] DEFERRED — maintainer interaction required: complete real Steam/UAC/
  coexistence/update/remove checks.

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
