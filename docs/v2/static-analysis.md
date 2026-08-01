# MecchaCamouflage v2 Windows Verification Inventory

## Result

The Phase 6 unit, invariant/property, golden, state-machine,
dependency-boundary, sanitizer, and native static-analysis categories have
repeatable Windows evidence. This inventory does not substitute for the live
and measured release gates in Phase 13.

The public hosted graph registers 94 secret-free tests. A native Windows
workspace with the available local/private inputs registers 112 tests. The
current native graph passes all 112 tests under normal MSVC Release, MSVC
AddressSanitizer, and clang-cl UndefinedBehaviorSanitizer. MSVC `/analyze`
passes the production target closure with warnings treated as errors.

## Behavioral evidence

| Category | Repeatable evidence |
| --- | --- |
| Unit and boundary | The registered graph exercises every focused core/application/UI/launcher library and rejects invalid enums, non-finite values, limits, stale generations, malformed resources, and publication failures. |
| Invariant/property | Deterministic value sweeps cover color transfer round trips, projection/clipping, profile sampling, image transforms, codec round trips, bounded collection mutations, reflection-record mutations, and queue/state invariants. |
| Golden | Frozen v1 domain fixtures cover Paint routing/pacing, Image Paint mapping, and ESP semantics. Packaged profile identities, canonical PNG bytes, fallback glyph atlas metadata, localization catalogs, and payload/dependency manifests have exact expected-output checks. |
| State machine | Application jobs, preview ownership, workers, command queues, launcher transactions, recovery journals, and UI input modes cover success, cancellation, staleness, overflow, exception containment, shutdown, and retry/restore transitions. |
| Dependency boundary | `phase1_contracts`, `source_graph`, and `ci_policy` reject v1 production edges, restricted public-CI checkout, secret use, recursive public submodules, non-Windows v2 runners, and release-policy drift. |

## Executable CI policy

`.github/workflows/v2-ci.yml` uses `windows-2022` exclusively:

- `Policy Contracts` always runs.
- `Windows MSVC Tests` is the required secret-free PR test signal for every
  code-bearing change.
- `Windows MSVC ASan`, `Windows clang-cl UBSan`, and `Windows MSVC Code
  Analysis` run on `workflow_dispatch` and are required by the stable
  `Public CI Gate` for that manual deep-validation run.
- A synchronized Markdown/latest-v1-checkpoint-only change may skip the
  compiler jobs only when the previous head already has a successful
  `Public CI Gate`. Missing or malformed evidence fails closed.

`.github/workflows/v2-release-candidate.yml` repeats all three deep Windows
jobs as hard predecessors of the protected release-candidate job. A release
candidate therefore cannot be assembled when ASan, UBSan, or `/analyze`
fails. The workflow remains manual, protected, and non-publishing.

## Toolchain modes

The three CMake deep-validation switches are mutually exclusive and require
their exact native toolchain:

- `MECCHA_ENABLE_MSVC_ADDRESS_SANITIZER=ON` adds `/fsanitize=address`, `/Zi`,
  `/DEBUG`, and `/INCREMENTAL:NO` under MSVC.
- `MECCHA_ENABLE_CLANG_CL_UBSAN=ON` adds `-fsanitize=undefined`, fatal recovery
  behavior, frame pointers, and debug information under clang-cl. LLVM's
  Windows UBSan runtime is static, so this isolated graph uses the matching
  static MSVC runtime.
- `MECCHA_ENABLE_MSVC_CODE_ANALYSIS=ON` adds `/analyze` and
  `/analyze:external-`. Normal `/WX` policy makes project analyzer diagnostics
  fatal.

CTest prepends the selected compiler directory to each sanitizer test's
`PATH`, so the MSVC ASan runtime DLL is resolved without relying on a developer
shell. The elevated transport and child test executables embed the same Common
Controls v6 activation contract as the launcher, preventing loader failures in
`TaskDialog` imports.

The Windows SDK UCRT implements inline `wmemcmp` with deliberate unaligned
64-bit loads on x64. clang-cl instruments those SDK-header loads as alignment
violations. `cmake/clang-cl-ubsan-ignorelist.txt` excludes only the alignment
check originating in that SDK header; all other UBSan checks and project-source
alignment checks remain enabled.

Leak detection is not a v2 acceptance criterion. The supported MSVC sanitizer
mode is AddressSanitizer, and the Windows toolchain does not provide equivalent
supported LeakSanitizer coverage.

## Native reproduction

Run from an x64 Visual Studio developer environment. Use a separate build tree
for every mode.

```powershell
cmake -S . -B .build/v2/windows-msvc -G "Visual Studio 17 2022" -A x64 `
  -DMECCHA_WITH_UE4SS=OFF -DMECCHA_BUILD_TESTS=ON `
  -DMECCHA_WARNINGS_AS_ERRORS=ON
cmake --build .build/v2/windows-msvc --config Release
ctest --test-dir .build/v2/windows-msvc -C Release --output-on-failure

cmake -S . -B .build/v2/windows-msvc-asan -G "Visual Studio 17 2022" -A x64 `
  -DMECCHA_WITH_UE4SS=OFF -DMECCHA_BUILD_TESTS=ON `
  -DMECCHA_WARNINGS_AS_ERRORS=ON `
  -DMECCHA_ENABLE_MSVC_ADDRESS_SANITIZER=ON
cmake --build .build/v2/windows-msvc-asan --config Release
$env:ASAN_OPTIONS = "halt_on_error=1"
ctest --test-dir .build/v2/windows-msvc-asan -C Release --output-on-failure

cmake -S . -B .build/v2/windows-clang-ubsan -G Ninja `
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl `
  -DCMAKE_BUILD_TYPE=Release -DMECCHA_WITH_UE4SS=OFF `
  -DMECCHA_BUILD_TESTS=ON -DMECCHA_WARNINGS_AS_ERRORS=ON `
  -DMECCHA_ENABLE_CLANG_CL_UBSAN=ON
cmake --build .build/v2/windows-clang-ubsan --parallel 2
$env:UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1"
ctest --test-dir .build/v2/windows-clang-ubsan --output-on-failure

cmake -S . -B .build/v2/windows-msvc-analysis `
  -G "Visual Studio 17 2022" -A x64 `
  -DMECCHA_WITH_UE4SS=OFF -DMECCHA_BUILD_TESTS=OFF `
  -DMECCHA_WARNINGS_AS_ERRORS=ON `
  -DMECCHA_ENABLE_MSVC_CODE_ANALYSIS=ON
cmake --build .build/v2/windows-msvc-analysis --config Release `
  --target meccha_product_ui meccha_runtime_contracts meccha_launcher_core `
  --parallel 2
```

Native evidence on 2026-08-01 used MSVC 19.44.35228.0, clang-cl 20.1.8,
CMake 4.4.0, and Windows SDK 10.0.26100.0. Results were 112/112 for each test
configuration and a clean three-target `/analyze` production closure.
