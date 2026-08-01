# Phase 13 Stabilization Progress

Phase 13 remains open. This document separates repeatable Windows automated
evidence from the protected and live evidence that is still mandatory. It does
not convert an unavailable protected build, game session, second client, or
supported operating-system matrix into a pass.

## Candidate identity

- Project commit: `52ce4b017636a099525e489895542f2c102d9c07`
- UE4SS commit: `6c26f038751b3d96059d4a9148f5d093012d55ad`
- Local MSVC: 19.44.35228.0
- Local clang-cl: 20.1.8
- CMake: 4.4.0
- Windows SDK: 10.0.26100.0

This is a secret-free source candidate, not the final protected artifact or
the proposed Phase 14 merge commit.

## Repeated automated evidence

The complete registered secret-free graph was repeated from the native
Windows build trees with failure-stopping semantics. Each repetition includes
the unit, property/contract, fake-runtime, persistence, launcher, payload-tool,
source-policy, cancellation, fault, shutdown, and resource-bound tests already
registered in CTest.

```powershell
ctest --test-dir .build/v2/windows-probe-normal -C Release `
  --repeat until-fail:3 --output-on-failure --parallel 4

$env:ASAN_OPTIONS = "halt_on_error=1"
ctest --test-dir .build/v2/windows-probe-msvc-asan -C Release `
  --repeat until-fail:2 --output-on-failure --parallel 4

$env:UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1"
ctest --test-dir .build/v2/windows-probe-clang-ubsan `
  --repeat until-fail:2 --output-on-failure --parallel 4
```

Results on 2026-08-01:

| Configuration | Registered tests | Repetitions | Executions | Result | Wall time |
| --- | ---: | ---: | ---: | --- | ---: |
| MSVC Release | 112 | 3 | 336 | PASS | 34.17 s |
| MSVC AddressSanitizer | 112 | 2 | 224 | PASS | 153.68 s |
| clang-cl UndefinedBehaviorSanitizer | 112 | 2 | 224 | PASS | 23.72 s |
| **Total** |  |  | **784** | **PASS** |  |

The hosted Windows-only evidence for the same commit is retained separately:

- [pull-request run 30676266477](https://github.com/acentrist/MecchaCamouflage/actions/runs/30676266477):
  policy contracts, 112/112 MSVC Release tests, and `Public CI Gate`;
- [manual deep run 30676738910](https://github.com/acentrist/MecchaCamouflage/actions/runs/30676738910):
  112/112 MSVC Release, 112/112 MSVC AddressSanitizer, 112/112 clang-cl
  UndefinedBehaviorSanitizer, the three-target MSVC `/analyze` production
  closure, and `Public CI Gate`.

These repetitions strengthen deterministic T0--T3 evidence. They are not
clean trusted UE4SS rebuild repetitions and do not establish live frame,
planning, dispatch, memory, texture, thread, preparation, log, or storage
baselines.

## Resource and failure coverage

[`resource-limit-audit.md`](resource-limit-audit.md) records the enforced
code-level memory, count, queue, file, payload, and arithmetic limits and the
tests that reject excess work without partial publication. The repeated graph
above exercised the currently registered fault-injection, cancellation,
staleness, overflow, retry, restore, transaction-recovery, worker exception,
and shutdown cases under all three supported local compiler modes.

Phase 13 still requires measured baselines from the exact feature-complete
candidate. Hard limits and fast fake-runtime timings cannot substitute for
measured game-frame cost, game-owned queue behavior, rooted texture lifetime,
process thread count, preparation time, or repeated installation storage.

## Remaining release blockers

| Required evidence | Current state | Missing authority/environment |
| --- | --- | --- |
| Repeated clean trusted UE4SS/mod builds and manifest comparison | `BLOCKED_EXTERNAL_VALIDATION` | Protected `ue4ss-full-build` environment and restricted dependency access |
| Reviewed dependency graph and license-file hashes | `BLOCKED_EXTERNAL_VALIDATION` | Release-maintainer review and approval |
| Final one-EXE protected candidate and verifier evidence | `BLOCKED_EXTERNAL_VALIDATION` | Approved audit plus protected release-candidate run |
| Architecture and full feature live sessions | `BLOCKED_EXTERNAL_VALIDATION` | Supported game build and maintainer observation |
| Windows 10 and Windows 11 installation/UAC matrix | `BLOCKED_ENVIRONMENT` | Both physical OS environments and required installation states |
| Host and joining-client Paint/Image Paint matrix | `BLOCKED_ENVIRONMENT` | Two real game clients and participants |
| Measured performance/resource/storage baselines and 25 managed cycles | `BLOCKED_EXTERNAL_VALIDATION` | Exact protected candidate plus live Windows hosts |

Phase 14 legacy deletion, proposed merge-commit construction, merge, tag, and
release remain forbidden until every Phase 13 exit criterion passes against
the same accepted candidate bytes.
