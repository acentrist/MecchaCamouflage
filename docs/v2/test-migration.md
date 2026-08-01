# MecchaCamouflage v2 Test Migration Ledger

This ledger classifies every test registered by the v1 test executable at
`src/csharp/MecchaCamouflage.Tests/Program.cs`. The ordered registry contains
175 tests. Its canonical fingerprint is the SHA-256 of the UTF-8 test names,
one name per line with a final newline:

`e92b43e7e943956251d77ac9c3745e5122b09cfea88e9169f4f27e44db0c6ac7`

The Phase 1 verifier expands every inclusive range below and rejects gaps,
overlaps, unknown dispositions, a changed source registry, or an unclassified
test.

## Dispositions

- `PORT`: preserve the behavior as a focused C++ unit test.
- `CHARACTERIZE`: preserve inputs and externally observable outputs as a
  golden/contract fixture, without copying the v1 implementation structure.
- `REWRITE`: preserve the requirement but test it at the v2 architecture
  boundary.
- `RETIRE`: delete an assertion that exists only for an explicitly removed v1
  architecture or migration path.

When one v1 test combines retained behavior with v1 migration or transport
details, `REWRITE` means the retained behavior is split into a v2 test while
the legacy-only half is not reproduced.

## Complete registry classification

The `Source test(s)` column is an audit anchor. Registry number is the durable
classifier within this baseline; exact names and the fingerprint prevent a
range from silently changing meaning.

| Registry | Disposition | Source test(s) | v2 requirement/evidence |
| --- | --- | --- | --- |
| 001-001 | PORT | paint defaults expose a single brush | PAINT-001 defaults |
| 002-002 | REWRITE | single brush persists and migrates legacy detail settings | PERSIST-002 fresh-v2 persistence; omit migration |
| 003-003 | PORT | single brush settings clamp to supported range | PAINT-001 validation |
| 004-004 | RETIRE | app defaults use 99 percent opacity | RETIRE-002 |
| 005-007 | REWRITE | ESP role-color migration through Paint payload policies | ESP-004, PAINT-008/009; typed contracts, no v1 migration |
| 008-015 | CHARACTERIZE | canonical Image canvas through live-profile selection | IMAGE-003/008/009/012, PAINT-010/011 |
| 016-017 | REWRITE | Image restore command behavior | IMAGE-015, RUNTIME-009 |
| 018-019 | RETIRE | custom freecam surface and Present renderer configuration | RETIRE-007; freecam survival is tested separately |
| 020-021 | REWRITE | ESP scope/color and latest-request behavior | ESP-002/004, RUNTIME-006/011 |
| 022-022 | RETIRE | Present status counter logging | RETIRE-007 |
| 023-023 | REWRITE | no external frame-render fallback | ESP-010 forbidden-artifact check |
| 024-024 | RETIRE | Present queue/backbuffer/hook lifecycle | RETIRE-007 |
| 025-025 | REWRITE | one resident runtime across UI updates | RUNTIME-012 composition-root test |
| 026-035 | RETIRE | bridge reconnect/injection and D3D/Present implementation details | RETIRE-005/006/007/011 |
| 036-038 | CHARACTERIZE | projection, bounds fallback, and viewport-edge snaplines | ESP-007/008 |
| 039-040 | REWRITE | exact pawn contract and pre-geometry spectator exclusion | RUNTIME-005, ESP-005 |
| 041-041 | CHARACTERIZE | skeleton profile selection | ESP-007 |
| 042-043 | REWRITE | coherent ESP snapshot and spectator paint identity | ESP-005/006, PAINT-017 |
| 044-044 | RETIRE | diagnostic stroke-limit option | RETIRE-011 |
| 045-047 | CHARACTERIZE | brush range, game radius defaults, and spatial replay order | PAINT-001/011/014 |
| 048-052 | REWRITE | captured component, game sender, and preview lifetime/order | PAINT-012/013/017 |
| 053-053 | REWRITE | Projective environment feedback with independent manual Fill route | PAINT-003/006/007/008 |
| 054-054 | RETIRE | intrinsic emission probe | RETIRE-011 |
| 055-055 | CHARACTERIZE | capture hides only the live brush visual | PAINT-003 |
| 056-056 | REWRITE | typed Paint route includes independent Fill material | PAINT-006/013 |
| 057-057 | RETIRE | legacy Fill PBR migration | RETIRE-009 |
| 058-059 | PORT | locale completeness and RRGGBB parsing | I18N-002, PAINT-006, ESP-004 |
| 060-062 | REWRITE | bounded diagnostics and cache validation/repair | RUNTIME-008, LAUNCH-004/005 |
| 063-066 | RETIRE | event-watch/texture/replay research paths | RETIRE-011 |
| 067-067 | CHARACTERIZE | Fill and Paint atlas separation | PAINT-011, IMAGE-009/012 |
| 068-072 | RETIRE | research replay and texture-probe sidecars | RETIRE-005/011 |
| 073-075 | REWRITE | actionable and best-effort bounded diagnostics | RUNTIME-005/008 |
| 076-080 | PORT | Paint/ESP defaults and retained settings | PAINT-002/007/008, ESP-003 |
| 081-081 | REWRITE | Image defaults plus fresh-v2 persistence | IMAGE-010/011/018 |
| 082-082 | PORT | fukuyoka body identity | IMAGE-008 |
| 083-083 | REWRITE | typed Image Fill color | IMAGE-011 |
| 084-084 | PORT | normalized crop validation | IMAGE-006 |
| 085-085 | RETIRE | legacy global-to-layer transform migration | RETIRE-009 |
| 086-086 | REWRITE | deterministic v2 preset round trip | IMAGE-017/019, PERSIST-005 |
| 087-087 | RETIRE | v1 preset transform expansion | RETIRE-010 |
| 088-088 | REWRITE | saved project becomes active atomically | IMAGE-017, PERSIST-007 |
| 089-090 | RETIRE | legacy active-image migration | RETIRE-009 |
| 091-093 | REWRITE | draft validation, atomic stores, and user-facing errors | IMAGE-018, PERSIST-003/004, UI-012 |
| 094-096 | PORT | system locale and immutable snapshot fields | I18N-001, RUNTIME-011, PAINT-001/008 |
| 097-105 | REWRITE | Web editor behavior as Canvas controls/state tests | UI-001/007/009, IMAGE-004/016 |
| 106-106 | RETIRE | manual Paint controls in the log tab | RETIRE-011 |
| 107-108 | REWRITE | snapshot diagnostics and unified feedback | RUNTIME-008, UI-008/012 |
| 109-109 | RETIRE | WebView zoom footer | RETIRE-004 |
| 110-113 | REWRITE | localized Canvas sections/editor/errors/body selector | UI-001, I18N-004, IMAGE-008 |
| 114-116 | CHARACTERIZE | fukuyoka preservation/profile refresh/asset alias | PAINT-010, IMAGE-008/016 |
| 117-118 | REWRITE | localized native compatibility and picker dialogs | I18N-004, UI-010 |
| 119-119 | PORT | all declared localization keys resolve | I18N-002 |
| 120-125 | REWRITE | packaged guides, Canvas theme/progress, input, snapshots | IMAGE-016, UI-005/008/011 |
| 126-126 | PORT | duplicate F-key validation | UI-005 |
| 127-136 | REWRITE | transactional settings commands and reset semantics | PERSIST-003, RUNTIME-011 |
| 137-144 | REWRITE | revisioned progress, backpressure, diagnostics, stale-state rejection | RUNTIME-006/008/011, PAINT-015/016 |
| 145-149 | REWRITE | cancel guards, admission race, and terminal cancellation | RUNTIME-010, PAINT-014 |
| 150-154 | RETIRE | bridge ABI, injector identity, and TCP hello | RETIRE-005/006 |
| 155-156 | REWRITE | bounded teardown and in-flight admission barrier | RUNTIME-004/010 |
| 157-157 | RETIRE | fresh bridge instance after shutdown | RETIRE-005/006 |
| 158-159 | REWRITE | stale callback/result cannot mutate a replacement generation | RUNTIME-004/006 |
| 160-161 | RETIRE | exact-PID bridge startup and Web navigation lifecycle | RETIRE-004/005/006 |
| 162-162 | REWRITE | application close performs ordered runtime shutdown | RUNTIME-004 |
| 163-163 | RETIRE | resident ProcessEvent bridge hook | RETIRE-006/007 |
| 164-164 | REWRITE | verified local runtime staging | LAUNCH-004/005 |
| 165-165 | RETIRE | direct-bridge naming convention | RETIRE-005/006 |
| 166-167 | REWRITE | minimal v2 payload and pinned dependency graph | RELEASE-001/002/004/006/007 |
| 168-173 | RETIRE | Defender exclusion/elevation workflow | RETIRE-008 |
| 174-174 | REWRITE | release excludes research/debug content | RELEASE-004/007 |
| 175-175 | RETIRE | version-scoped development runtime directories | LAUNCH-005 replaces them with one active generation |

## Replacement rule

A `RETIRE` row authorizes removal only of the named test and its v1-only
contract. It never authorizes deleting retained behavior referenced in the last
column. A `REWRITE` row is not complete until its new test exists and the
associated requirement has the planned automated evidence.
