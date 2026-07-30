# Phase 2 Build and UE4SS Source Gate

## Current status

The secret-free source/build boundary is implemented and verified on Windows
and on Linux with AddressSanitizer plus UndefinedBehaviorSanitizer. The full
UE4SS build is correctly blocked before dependency
configuration because this workspace does not have the maintainer's
Epic-linked GitHub credential. This is expected, explicit, and not counted as
a pass.

## Target graph

| Target | Kind in Phase 2 | Dependency rule |
| --- | --- | --- |
| `meccha_build_options` | Interface build policy | Project warnings and C++23 only |
| `meccha_build_identity` | Generated immutable contract | Product/schema/UE4SS identity; no platform headers |
| `meccha_core` | Interface boundary pending Phase 6 sources | No UE4SS, Unreal, Windows UI, graphics, or launcher dependency |
| `meccha_ui` | Static project-owned Canvas/layout/widget/text/editor/input protocol | Depends only on core values; exposes no Unreal or graphics API type |
| `meccha_launcher_core` | Static Phase 3 deployment-policy module | Depends on core/build identity and pinned Glaze; contains no persistent UI |
| `meccha_mod` | Windows x64 shared library when full build is enabled | Links the `UE4SS` target from the same configure graph and outputs `main.dll` |
| `meccha_build_identity_test` | Secret-free executable test | Exercises the generated public build identity |

The Phase 2 mod contains only metadata, empty `on_unreal_init`/`on_update`
lifecycle boundaries, and the two approved exports. It installs no hook,
touches no UObject, and is not a product release candidate.

## Source policy

- Root UE4SS gitlink:
  `6c26f038751b3d96059d4a9148f5d093012d55ad`.
- Public CI initializes only that root gitlink.
- `MECCHA_WITH_UE4SS=OFF` never reads the restricted nested source.
- The full build requires Windows x64, MSVC, UEPseudo, and patternsleuth.
- Direct FetchContent inputs are predeclared by immutable commit.
- UE4SS is added `EXCLUDE_FROM_ALL`; only UE4SS and the project mod are
  requested by the full preset.
- UE4SS and the project both select the dynamic MSVC runtime.
- No UE4SS source edit or project-authored patch is present.

See [`dependency-lock.md`](dependency-lock.md) for commits and license state.

## Verified commands

Linux secret-free build:

```text
cmake -S . -B .build/v2/sanitize-linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DMECCHA_WITH_UE4SS=OFF \
  -DMECCHA_BUILD_TESTS=ON -DMECCHA_WARNINGS_AS_ERRORS=ON \
  -DMECCHA_ENABLE_SANITIZERS=ON
cmake --build .build/v2/sanitize-linux
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir .build/v2/sanitize-linux --output-on-failure
```

Result: all 48 registered secret-free tests passed with GCC 13.3.0,
CMake 3.28.3, Ninja 1.13.2, ASan, UBSan, and leak detection.

The first sanitized run exposed a timing assumption in
`image_paint_job_coordinator_test`: entering the planning function was treated
as equivalent to publishing its worker completion. The production coordinator
already retained the correct asynchronous cancellation/drain behavior. The
fixture now deterministically blocks until cancellation and waits for the
typed stale-project terminal result; ten repeated normal and sanitized runs
pass.

Windows secret-free build:

```text
cmake -S . -B .build/v2/windows-vs -G "Visual Studio 17 2022" -A x64 \
  -DMECCHA_WITH_UE4SS=OFF -DMECCHA_BUILD_TESTS=ON \
  -DMECCHA_WARNINGS_AS_ERRORS=ON
cmake --build .build/v2/windows-vs --config Release
ctest --test-dir .build/v2/windows-vs -C Release --output-on-failure
```

Result: all registered secret-free tests passed with MSVC 19.44.35228 and
CMake 4.4.0.
UNC-only MSBuild emitted path/case incremental-build warnings in this WSL
workspace, so those warnings are recorded as host-path noise rather than
product compiler warnings. GitHub CI uses a native local Windows workspace.

Full-build access probe:

```text
cmake -S . -B .build/v2/full-probe -G "Visual Studio 17 2022" -A x64 \
  -DMECCHA_WITH_UE4SS=ON -DMECCHA_BUILD_TESTS=OFF
```

Expected result in this workspace:

```text
The restricted UEPseudo dependency is missing. A complete build requires
maintainer-approved Epic-linked GitHub access.
```

No fetch, source modification, or partial binary occurs after that failure.

## Protected full-build evidence

`.github/workflows/v2-full-build.yml` is manual-only, repository/ref guarded,
and protected by the `ue4ss-full-build` environment. The maintainer must
configure that environment with required reviewers and the
`UE4SS_GITHUB_TOKEN` secret.

After approval, it must:

1. recursively initialize the exact nested graph;
2. configure `Game__Shipping__Win64` once;
3. build `UE4SS`, its `proxy`, `meccha_mod`, and every project contract
   together without selecting unrelated UE4SS tools or bundled mods;
4. run `tools/v2/verify-full-build.ps1`;
5. prove x64 PE format, only `start_mod`/`uninstall_mod` exports, a direct
   `UE4SS.dll` import, matching proxy/mod/runtime dynamic MSVC runtime, clean
   gitlink, and the exact pinned commit;
6. collect the closed production CMake target graph, exact git revisions and
   tracked diffs, and target-filtered locked Cargo package/checksum/features;
7. generate a canonical evidence-bound audit template with deliberately empty
   license-expression/file review fields;
8. run all registered contracts;
9. upload provenance, dependency evidence, and that unapproved template, not a
   distributable runtime.

## Open exit evidence

Phase 2 remains externally gated until the protected run provides:

- the complete recursive graph and license/notice report;
- a clean full configure/build without a UE4SS source patch;
- matching x64 Shipping ABI/import/export/runtime evidence;
- the provenance JSON from the exact build.

Independent secret-free launcher, payload, domain, persistence, and fake
runtime work may continue. Live UE4SS loading and runtime phases cannot be
accepted from this partial gate.
