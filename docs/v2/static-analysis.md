# MecchaCamouflage v2 Phase 6 Verification Inventory

## Result

The Phase 6 unit, invariant/property, golden, state-machine,
dependency-boundary, sanitizer, and supported static-analysis categories all
have repeatable evidence. This inventory does not substitute for the live and
measured release gates in Phase 13.

## Behavioral evidence

| Category | Repeatable evidence |
| --- | --- |
| Unit and boundary | The registered portable graph exercises every focused core/application/UI/launcher library and rejects invalid enums, non-finite values, limits, stale generations, malformed resources, and publication failures. |
| Invariant/property | Deterministic value sweeps cover color transfer round trips, projection/clipping, profile sampling, image transforms, codec round trips, bounded collection mutations, reflection-record mutations, and queue/state invariants. These are fixed reproducible property cases rather than randomized fuzz results. |
| Golden | Frozen v1 domain fixtures cover Paint routing/pacing, Image Paint mapping, and ESP semantics. Packaged profile identities, canonical PNG bytes, fallback glyph atlas metadata, localization catalogs, and payload/dependency manifests have exact expected-output checks. |
| State machine | Application jobs, preview ownership, workers, command queues, launcher transactions, recovery journals, and UI input modes cover success, cancellation, staleness, overflow, exception containment, shutdown, and retry/restore transitions. |
| Dependency boundary | `phase1_contracts`, `source_graph`, and `ci_policy` reject v1 production edges, restricted public-CI checkout, secret use, recursive public submodules, and release-policy drift. |

The current complete portable graph is 94 tests under both normal Linux and
ASan/UBSan builds. The exact synchronized Windows x64 Shipping graph is 112
tests under MSVC `/W4 /WX`. Feature progress documents retain the focused test
names and scenarios for their own acceptance criteria.

## Supported static analysis

Public CI has a separate `linux-static-analysis` job. It configures the
secret-free graph with:

```text
MECCHA_WITH_UE4SS=OFF
MECCHA_BUILD_TESTS=OFF
MECCHA_WARNINGS_AS_ERRORS=ON
MECCHA_ENABLE_GCC_ANALYZER=ON
```

The job builds `meccha_product_ui`, `meccha_runtime_contracts`, and
`meccha_launcher_core`. Their transitive target closures cover the portable
production common, core, application, runtime-contract, UI, and launcher
sources without spending analyzer time on test translation units or requiring
the restricted Unreal dependency. CI limits this analyzer build to two
parallel compile jobs: the analyzer has a materially higher per-translation
unit memory cost, and an unbounded Ninja fan-out can be terminated by a hosted
runner before GCC emits a diagnostic.

Every public v2 job checks out the exact pull-request head commit (or the exact
push commit), rather than GitHub's synthetic pull-request merge ref. This keeps
the rewrite's frozen v1 comparison evidence and profile identities bound to the
commit under review even while the target branch changes. The legacy v1
workflow remains enabled for its normal branches and pull requests but skips
only the `rewrite/ue4ss-v2` pull request, whose restricted recursive checkout
is outside the public v2 trust boundary.

The sanitized Image Paint composition-root test pumps asynchronous work to a
30-second steady-clock deadline and reports a timeout as a failed assertion.
This preserves a bounded failure while avoiding the former assumption that a
sanitized hosted runner would finish worker setup within 1,000 one-millisecond
sleeps.

`MECCHA_ENABLE_GCC_ANALYZER` requires GCC 13 or newer and enables `-fanalyzer`
under the normal `-Wall -Wextra -Wpedantic -Werror` policy. Analyzer and
ASan/UBSan builds must use separate build trees so each gate has an unambiguous
diagnostic model.

Two GCC analyzer diagnostics are disabled:

- `-Wanalyzer-malloc-leak`
- `-Wanalyzer-use-of-uninitialized-value`

GCC 13 reports both while moving libstdc++ `std::expected<T, HashError>` values
whose error object owns a `std::string`, in ordinary project return paths. The
reproducing diagnostics point into the standard-library expected/string
implementation rather than an unmatched project allocation or read. All other
compiler and analyzer diagnostics remain fatal. The exclusions are narrow and
must be re-evaluated when the minimum supported GCC analyzer changes.

## Local reproduction

```bash
cmake -S . -B .build/v2/gcc-analyzer -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMECCHA_WITH_UE4SS=OFF \
  -DMECCHA_BUILD_TESTS=OFF \
  -DMECCHA_WARNINGS_AS_ERRORS=ON \
  -DMECCHA_ENABLE_GCC_ANALYZER=ON
cmake --build .build/v2/gcc-analyzer \
  --target meccha_product_ui meccha_runtime_contracts meccha_launcher_core \
  --parallel 2
```
