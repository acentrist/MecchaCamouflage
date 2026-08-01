# MecchaCamouflage v2 Risk Register

This register tracks release-relevant uncertainty. Automated mitigation is not
a substitute for a required live or protected-build result. Evidence uses only
the result vocabulary frozen in [`PLAN.md`](../../PLAN.md).

| ID | Risk | Current control | Remaining evidence / owner | Release state |
| --- | --- | --- | --- | --- |
| R-001 | Pinned UE4SS fails to load or bind the supported live game build. | Exact source-stage identity, reflection schemas, x64 Shipping build, binary provenance. | Architecture session on the supported build; maintainer + agent-operated live host. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-002 | HUD callback survives after unload or restoration occurs after callback removal. | Exact callback IDs, in-flight barrier, restore-before-unregister lifecycle, fake fault tests. | Explicit live unload, callback-inert log, and resource/input restoration; maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-003 | Canvas text, clipping, texture, pointer, or input lease differs from portable contracts. | Complete-frame preflight, glyph atlas, retained interaction, transactional lease, portable/Windows tests. | Visual and interaction matrix across representative resolution/DPI/language cases; maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-004 | Projective Paint capture orientation, correction-field response, physical emission, or feedback cleanup differs in the live scene. | Exact pass/camera contracts, fixed four-texel lattice, dual source/target emission evidence, source-hit surface gate, component validation, and byte-verified restore. | Supported-body live capture session with redacted readback evidence and bounded frame cost; maintainer + agent-operated host. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-005 | Game-owned Paint queue or replication semantics differ for host/joining clients. | Exact `PaintAtUVWithBrush` sender, queue observer, bounded pacing, cancellation and drain tests. | Host-painter and joining-client-painter two-client matrix; maintainer/multi-client. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-006 | Runtime object/cache invalidation fails across lobby, travel, freecam, spectator, role, avatar, or HUD replacement. | Weak generations, exact HUD identity, role/avatar policy, profile asset gates, fake lifecycle tests. | Single-client transition matrix for Paint, Image Paint, ESP, and UI; maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-007 | Alternate-credential UAC resolves the wrong user's LocalAppData or broadens broker authority. | Parent SID/token validation, original-user ownership, local authenticated pipe, two-file broker, Win32 tests. | Writable and elevation-required hosts with alternate administrator credentials; maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-008 | Managed/shared UE4SS preparation overwrites or removes unrelated content. | Read-only observation, exact identity modes, ownership receipts, journal recovery, no-change conflicts, hostile temporary-tree tests. | Existing exact and incompatible shared installations on supported hosts; maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-009 | Native picker or persistence fails on non-ASCII Windows paths or after interrupted I/O. | Strict UTF-8/UTF-16 boundaries, atomic storage, worker fault/recovery tests. | Native picker and project lifecycle on non-ASCII paths; maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-010 | Protected dependency graph contains an unreviewed license or source component. | Exact CMake/git/Cargo evidence, immutable source-stage binding, empty-field audit refusal, notice assembly checks. | Protected collector run and human approval of every component/license hash; release maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-011 | Final EXE differs from the tested runtime, payload, notices, or provenance. | Deterministic manifest/CAB, exact RCDATA identity, PE verifier, SHA-256 evidence, no rebuild-after-acceptance rule. | Protected release-candidate run and retained final bytes; release maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-012 | Performance, queue depth, memory, threads, textures, logs, preparation time, or storage accumulates under repetition. | Hard code-level limits, bounded queues/workers/textures/diagnostics, cancellation and cleanup tests. | Measured baselines plus repeated live lifecycle and 25 prepare/launch cycles on Windows 10 and 11; maintainer. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-013 | Legacy v1 paths are deleted before replacement evidence is complete. | v1 tag/maintenance branch, requirement/test migration ledgers, Phase 14 deletion gate. | Delete only after Phase 13 passes and verify every retirement against traceability. | `BLOCKED_EXTERNAL_VALIDATION` |
| R-014 | Required Windows 10/11, multi-client, or protected environments remain unavailable. | All unavailable checks are explicit in the live checklist; no assumed pass or fallback architecture. | Environment owners must provide the required hosts/credentials/participants. | `BLOCKED_ENVIRONMENT` |

## Review rules

- A risk closes only when its required evidence is attached to the exact
  product commit, payload manifest hash, UE4SS commit, game version, OS, and
  procedure.
- Any `FAIL`, `NOT_RUN`, or release-critical `BLOCKED_*` result prevents Phase
  13 exit and release.
- A failed architecture risk reopens the owning phase. It does not authorize a
  forbidden fallback.
- Secrets, user names, machine paths, account identifiers, unrelated mod
  inventories, and raw private logs must be redacted before evidence is
  committed.
