# Phase 2 Build and UE4SS Source Gate

## Current status

The secret-free source/build boundary is implemented and verified on Windows
and on Linux with AddressSanitizer plus UndefinedBehaviorSanitizer. The exact
restricted graph was initialized with an already configured
maintainer-authorized credential and reached UE4SS, proxy, and mod binaries on
a native Windows source path.

That diagnostic build exposed a reproducibility defect in the candidate:
Corrosion's Cargo invocation rewrites the tracked
`deps/first/patternsleuth_bind/Cargo.lock`. The architecture review explicitly
approved one project-owned canonical lock overlay in an independent immutable
build-only source stage. The accepted gitlink remains pristine, Cargo remains
`--locked`, and the stage is bound to exact source, nested-commit, upstream
lock, overlay, Git binary-diff, and manifest hashes.

The staging tool, real accepted checkout, repeated reuse, offline locked Cargo
metadata, and post-command verification now pass locally. Phase 2 is still
open until a protected native-Windows build produces same-stage binary
provenance and the resolved dependency evidence receives license approval.
The earlier diagnostic binaries remain rejected.

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
- `tools/v2/prepare_ue4ss_source_stage.py` clones the accepted initialized
  graph without hardlinks or network access, applies only the approved build
  overlay, and atomically publishes one reusable stage plus its manifest.
- `cmake/ue4ss-source-overlay.json` pins the upstream lock SHA-256
  `19292c3e0a74c851eb11ad09a3b3ac5e5d8e9b80eebe34dd705df10e09dc7e50`,
  canonical lock SHA-256
  `88c3718c03492cdc2650217a9d8bb2a8dbdecdbde1b4ea79e3e529e838b49bbe`,
  and resulting raw Git binary-diff SHA-256
  `0dac25e7c79d430aca62411cddf66c17d95340e2bee174f453f17f610a839f8f`.
- The patternsleuth Corrosion target receives Cargo `--locked`; a compiler or
  feature selection that would rewrite the accepted lock fails the build.
- UE4SS is added `EXCLUDE_FROM_ALL`; only UE4SS and the project mod are
  requested by the full preset.
- UE4SS and the project both select the dynamic MSVC runtime.
- Dependency evidence, the approved audit, and notice assembly share the
  manifest-verified staged UE4SS root. License files for staged UE4SS
  components cannot be substituted from the pristine gitlink or another
  checkout.
- The accepted UE4SS gitlink has no source edit. The only approved build-stage
  diff is the manifest-bound canonical Cargo lock; any other tracked or
  untracked entry fails pre/post-build verification.

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

Result: all 76 registered secret-free tests passed with GCC 13.3.0,
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

Full-build source-path probe:

```text
cmake -S . -B .build/v2/full-probe -G "Visual Studio 17 2022" -A x64 \
  -DMECCHA_WITH_UE4SS=ON -DMECCHA_BUILD_TESTS=OFF
```

The first attempt configured successfully from the WSL workspace but failed
when Corrosion invoked Cargo:

```text
CMD does not support UNC paths as current directories.
```

This is a Windows `cmd.exe` host-path limitation, not a source or compiler
failure. The same commit and recursive submodules were therefore checked out
under `C:\Temp` and configured without changing UE4SS.

Native-path full build:

```text
cmake -S C:\Temp\MecchaCamouflage-v2-full-src \
  -B C:\Temp\MecchaCamouflage-v2-full-local \
  -G "Visual Studio 17 2022" -A x64 \
  -DMECCHA_WITH_UE4SS=ON -DMECCHA_BUILD_TESTS=OFF \
  -DMECCHA_WARNINGS_AS_ERRORS=ON
cmake --build C:\Temp\MecchaCamouflage-v2-full-local \
  --config Game__Shipping__Win64 --target UE4SS
cmake --build C:\Temp\MecchaCamouflage-v2-full-local\third_party\RE-UE4SS\UE4SS\proxy_generator\proxy \
  --config Game__Shipping__Win64 --target proxy
cmake --build C:\Temp\MecchaCamouflage-v2-full-local \
  --config Game__Shipping__Win64 --target meccha_mod
```

Diagnostic result:

- MSVC `19.44.35228.0`, CMake `4.4.0`, Windows x64,
  `Game__Shipping__Win64`.
- `UE4SS.dll`, `dwmapi.dll`, and `main.dll` built from the same configured
  graph.
- `main.dll` imports that `UE4SS.dll` and exports only `start_mod` and
  `uninstall_mod`.
- All three binaries use the dynamic MSVC runtime and pass x64 PE inspection.
- Cargo then leaves a tracked 368-line deletion plus one-line rewrite in
  `deps/first/patternsleuth_bind/Cargo.lock`; the binary result is rejected.
- Reconfiguring the same graph with project-enforced `--locked` fails with
  `the lock file ... needs to be updated but --locked was passed`.
- Cargo `1.88.0` and `1.97.1` both reject the committed lock for this selected
  target/features graph, so selecting an older supported stable toolchain does
  not close the gate.

The verifier accepts the current `dumpbin` export-alias display while still
requiring exactly the two approved export names. It reads the resolved
`cl.exe` file version rather than treating the compiler's expected nonzero
no-argument exit as a verification failure. It must be rerun only after a
clean source build exists; running it against another clean checkout is not
valid provenance for these diagnostic binaries.

Approved source-stage verification:

```text
python tools/v2/prepare_ue4ss_source_stage.py \
  --policy cmake/ue4ss-source-overlay.json \
  --source-root third_party/RE-UE4SS \
  --output-root .build/v2/ue4ss-source \
  --manifest .build/v2/ue4ss-source-stage.json
cargo metadata --locked --offline \
  --filter-platform x86_64-pc-windows-msvc --format-version 1 --no-deps \
  --manifest-path \
  .build/v2/ue4ss-source/deps/first/patternsleuth_bind/Cargo.toml
python tools/v2/prepare_ue4ss_source_stage.py --verify-only \
  --policy cmake/ue4ss-source-overlay.json \
  --output-root .build/v2/ue4ss-source \
  --manifest .build/v2/ue4ss-source-stage.json
```

Result: preparation, no-accumulation reuse, locked offline resolution, and
post-command one-diff verification pass against the accepted real graph.

## Protected full-build evidence

`.github/workflows/v2-full-build.yml` is manual-only, repository/ref guarded,
and protected by the `ue4ss-full-build` environment. The maintainer must
configure that environment with required reviewers and the
`UE4SS_GITHUB_TOKEN` secret.

After approval, it must:

1. recursively initialize the exact nested graph;
2. prepare and verify the approved one-overlay source stage, then configure
   `Game__Shipping__Win64` against that explicit source root once;
3. build `UE4SS`, its `proxy`, `meccha_mod`, and every project contract
   together without selecting unrelated UE4SS tools or bundled mods;
4. run `tools/v2/verify-full-build.ps1`;
5. prove x64 PE format, only `start_mod`/`uninstall_mod` exports, a direct
   `UE4SS.dll` import, matching proxy/mod/runtime dynamic MSVC runtime, clean
   pristine gitlink, exact pinned commit, source-stage manifest, and approved
   one-file staged diff;
6. collect the closed production CMake target graph, exact git revisions and
   tracked diffs, and target-filtered locked Cargo package/checksum/features;
7. generate a canonical evidence-bound audit template with deliberately empty
   license-expression/file review fields;
8. run all registered contracts;
9. upload provenance, dependency evidence, and that unapproved template, not a
   distributable runtime.

## Open exit evidence

Phase 2 remains externally gated until the protected evidence workflow
provides:

- a locked build of the accepted candidate from the exact approved immutable
  source stage, with no mutation beyond its manifest-bound Cargo lock;
- matching x64 Shipping ABI/import/export/runtime evidence and provenance from
  that same clean checkout;
- the closed production target and Cargo dependency evidence;
- a maintainer-reviewed license/notice decision for every resolved component;
- an approved audit bound to that exact evidence hash.

Independent secret-free launcher, payload, domain, persistence, and fake
runtime work may continue. Live UE4SS loading and runtime phases remain
separate gates and cannot be inferred from this successful source build.
