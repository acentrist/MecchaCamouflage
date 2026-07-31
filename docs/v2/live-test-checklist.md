# MecchaCamouflage v2 Live and Protected Validation Checklist

This checklist is the source of truth for checks that deterministic local tests
cannot satisfy. None of the current rows is a pass. [`PLAN.md`](../../PLAN.md)
defines the acceptance contract and result vocabulary.

## Evidence header required for every run

Record all of the following before assigning `PASS` or `FAIL`:

```text
check_id:
result: PASS | FAIL | BLOCKED_ENVIRONMENT | BLOCKED_EXTERNAL_VALIDATION
product_commit:
payload_manifest_sha256:
ue4ss_commit:
game_version:
operating_system:
procedure_revision:
started_utc:
finished_utc:
evidence_files_and_sha256:
redactions:
observer:
notes:
```

A run against a game process started before runtime preparation is invalid.
Any build-affecting change invalidates later evidence for the older candidate.
Raw screenshots/logs may remain protected artifacts; repository evidence must
be compact and redacted.

## Session A — architecture viability and lifecycle

Required environment: one supported Windows game host, current supported game
build, pinned prepared UE4SS runtime, and maintainer observation.

| ID | Procedure and acceptance | Owner | Current result |
| --- | --- | --- | --- |
| LIVE-A01 | Prepare a writable installation, launch normally, verify the C++ mod loads through `start_mod`, and confirm no custom injector or resident launcher remains. | Maintainer + agent-operated host | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-A02 | Open/close F9 repeatedly in lobby and match; cursor, look, movement, and the prior input mode restore exactly after every close. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-A03 | Exercise pointer, keyboard navigation, project-name text editing, hotkey remapping, native picker cancellation, and focus loss. No stale or repeated action may publish. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-A04 | Verify line, filled box, localized game-font text, fallback-atlas text, clipping, atlas/source/guide textures, and representative ESP while the panel is closed. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-A05 | Transition lobby → match → travel → HUD replacement → freecam → spectator → lobby while opening UI and observing ESP. No stale Canvas/UObject use or lost callback is allowed. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-A06 | Explicitly unload after opening UI and creating preview/textures. Restore input and preview bytes, release rooted textures, unregister exact HUD callbacks, drain in-flight calls, and observe no later callback. | Maintainer + agent-operated host | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-A07 | Repeat normal game shutdown with active and inactive features. Teardown may record unavailable World state but must not dereference it, hang, or crash. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |

## Session B — single-client features and UI

Required environment: one supported host with capture logs/screenshots and
representative round, cube, and fukuyoka avatars.

| ID | Procedure and acceptance | Owner | Current result |
| --- | --- | --- | --- |
| LIVE-B01 | Run manual Paint for all profiles, regions, Fill/Skip routing, PBR, scene-lighting modes, progress, cancellation, drain, preview, exact restore, freecam, and travel. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-B02 | Run Auto Material source capture, E0, both endpoints, bounded trial frames, accepted/fallback resolution, cancellation, and teardown. Record readback orientation/color and prove every trial restores exact bytes before publication. | Maintainer + agent-operated host | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-B03 | Import PNG/JPEG/WebP, manipulate at least two layers, crop/resize/reorder/wrap/mirror, switch all guides, refresh textures, save/load/rename/delete, restart recovery, preview/restore, and Paint. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-B04 | Exercise ESP all/hider/hunter scopes, every primitive, colors, spectator exclusion, role/avatar changes, skeletons, distant snaplines, travel, freecam, and HUD replacement. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-B05 | Walk every UI section in all 16 languages at representative 100%, 150%, and 200% DPI plus common 16:9, ultrawide, and constrained resolutions. Text, focus, scrolling, clipping, status, and diagnostics must remain usable. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-B06 | Use native dialogs and v2 storage below non-ASCII Windows paths. Cancellation is non-mutating; valid save/load/recovery succeeds; malformed input stays untouched and actionable. | Maintainer | `BLOCKED_EXTERNAL_VALIDATION` |

## Session C — multiplayer

Required environment: two real compatible game clients and observers able to
verify both directions. Mock transport evidence is not accepted.

| ID | Procedure and acceptance | Owner | Current result |
| --- | --- | --- | --- |
| LIVE-C01 | Client A paints as host while A and B observe complete final Paint, progress drain, and no disconnect/crash. | Multi-client maintainers | `BLOCKED_ENVIRONMENT` |
| LIVE-C02 | Client B paints as joining client while A and B observe the same acceptance conditions. | Multi-client maintainers | `BLOCKED_ENVIRONMENT` |
| LIVE-C03 | Repeat representative Image Paint in host and joining-client directions, including cancellation and queue drain. | Multi-client maintainers | `BLOCKED_ENVIRONMENT` |
| LIVE-C04 | Join during or after Paint and verify the joining client reaches the complete intended appearance without a partial terminal state. | Multi-client maintainers | `BLOCKED_ENVIRONMENT` |

## Session D — launcher, UAC, and coexistence

Required environments: Windows 10 and Windows 11 hosts, default and non-default
Steam libraries, writable and elevation-required game directories, an
alternate administrator credential, and exact/incompatible shared UE4SS cases.

| ID | Procedure and acceptance | Owner | Current result |
| --- | --- | --- | --- |
| LIVE-D01 | Prepare, no-op reuse, repair, update, launch offline, and `--remove` from default/non-default Steam libraries. A running game blocks mutation. | Maintainer | `BLOCKED_ENVIRONMENT` |
| LIVE-D02 | Compare writable and elevation-required managed paths. UAC appears only for required proxy/override mutation and the dialog describes the exact scope. | Maintainer | `BLOCKED_ENVIRONMENT` |
| LIVE-D03 | Approve elevation with alternate credentials and prove invoking-user LocalAppData remains authoritative while the child changes only the two preflighted loader files. | Maintainer | `BLOCKED_ENVIRONMENT` |
| LIVE-D04 | Reuse exact shared UE4SS and add/remove only Meccha's mod; reject unknown/incompatible proxy, override, runtime, config, or mod conflicts without mutation. | Maintainer | `BLOCKED_ENVIRONMENT` |
| LIVE-D05 | Interrupt managed update at each journal boundary, rerun recovery, and verify one exact active generation with no abandoned staging tree. | Maintainer | `BLOCKED_ENVIRONMENT` |
| LIVE-D06 | Complete 25 prepare/launch cycles and compare runtime cache, receipts, logs, generated paths, processes, threads, and storage against the baseline. No version/GUID accumulation is allowed. | Maintainer | `BLOCKED_ENVIRONMENT` |

## Session E — protected dependency and release candidate

Required environment: the protected GitHub environment with reviewed Epic
access, approved dependency audit, and release maintainer authorization.

| ID | Procedure and acceptance | Owner | Current result |
| --- | --- | --- | --- |
| LIVE-E01 | Run the exact dependency collector, retain CMake/git/Cargo evidence, and approve every component/license-file hash. | Release maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-E02 | Build the same clean trusted graph, reverify the immutable UE4SS stage, assemble the frozen runtime inventory, and confirm only `UE4SS.log` and `cache` are generated paths. | Release maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-E03 | Build the final manifest/CAB/notices/resources into one EXE, rerun every test and artifact check, and retain the candidate EXE, checksum, reports, and provenance without publishing. | Release maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-E04 | Establish and record frame, planning, dispatch, queue, memory, texture, worker/thread, log, prepare-time, and storage baselines without weakening limits. | Maintainer + release maintainer | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-E05 | Create the normal proposed merge commit on the protected integration ref, rebuild it, and repeat Sessions A--D against those exact bytes. | Release maintainer + maintainers | `BLOCKED_EXTERNAL_VALIDATION` |
| LIVE-E06 | After every result passes, fast-forward `main`, merge PR #233, tag the same commit `v2.0.0`, and publish only the retained EXE and matching SHA-256. | Release maintainer | `BLOCKED_EXTERNAL_VALIDATION` |

## Failure handling

- Stop the owning phase on `FAIL`; preserve the evidence and diagnose before
  another run.
- Keep `BLOCKED_*` explicit. It never becomes `PASS` through source inspection
  or a fake runtime.
- Do not start Phase 14 deletion or move `main` while any release-critical row
  is not `PASS`.
- Never add a forbidden fallback to make a live gate appear successful.

