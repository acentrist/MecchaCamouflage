# MecchaCamouflage v2 Dependency Lock

This document records the public portion of the Phase 2 source graph. It is
both a build input and a release audit record. The protected full-build gate
must extend it with the restricted UEPseudo graph and the resolved transitive
Cargo/FetchContent license manifest before Phase 2 can be closed.

## Root gitlinks

| Path | Repository | Commit | Access | License evidence |
| --- | --- | --- | --- | --- |
| `third_party/RE-UE4SS` | `https://github.com/UE4SS-RE/RE-UE4SS.git` | `6c26f038751b3d96059d4a9148f5d093012d55ad` | Public | Root `LICENSE`: MIT License |
| `deps/first/Unreal` inside UE4SS | `git@github.com:Re-UE4SS/UEPseudo.git` | `b2e876da82b17254c04304746341c8fde0ddb37c` | **restricted**; Epic-linked GitHub access required | Deferred to protected full-build audit |
| `deps/first/patternsleuth` inside UE4SS | `git@github.com:trumank/patternsleuth.git` | `da8bfe4c5a464be0ef225c2c9a6ccaa2d9284018` | Public | Workspace declaration: `MIT OR Apache-2.0`; exact license texts must be collected for packaging |

The public checkout initializes only the RE-UE4SS root gitlink. It does not
attempt a recursive checkout and therefore does not request UEPseudo access.
The secret-free build configures with `MECCHA_WITH_UE4SS=OFF`.

## Direct UE4SS FetchContent locks

UE4SS identifies most direct fetches by release tag, but identifies
IconFontCppHeaders by the moving `main` branch. MecchaCamouflage declares every
direct fetch first with an exact commit in
`cmake/Ue4ssDependencyPins.cmake`. CMake's first-declaration-wins behavior
keeps the accepted graph immutable without changing the UE4SS gitlink.

| Component | UE4SS reference | Accepted commit | Declared license |
| --- | --- | --- | --- |
| glaze | `v6.4.0` | `3a850807501d98d23bab4bdc5af64d8d4e83e6bc` | MIT |
| GLFW | `3.3.9` | `e2c92645460f680fd272fd2eed591efb2be7dc31` | Zlib |
| Dear ImGui | `v1.92.1` | `5d4126876bc10396d4c6511853ff10964414c776` | MIT |
| ImGuiColorTextEdit | `v1.2.0` | `6d943aba9f7cef05da80b86dbb0253b63818f95c` | MIT |
| IconFontCppHeaders | `main` | `210b5a399a64270674560d633638952d1e8d804d` | Zlib |
| Zydis | `v4.1.1` | `a2278f1d254e492f6a6b39f6cb5d1f5d515659dc` | MIT |
| PolyHook2 | exact commit | `298d56210b9d9e66cde8f96481d6053925c6ae15` | MIT |
| raw_pdb | exact commit | `8c6a7146393c83d27fa101e8bc8017f2a7f151df` | BSD-2-Clause |
| Corrosion | exact commit | `52844733e14f095c947577627e367ee5f6458af7` | MIT |
| {fmt} | `11.2.0` | `40626af88bd7df9a5fb80be7b25ac85b122d6c21` | MIT |
| Tracy | `v0.10` | `37aff70dfa50cf6307b3fee6074d627dc2929143` | BSD-3-Clause |

The exact upstream {fmt} patch command contained in the accepted UE4SS source
is preserved by the parent declaration. No new patch is introduced.

## Vendored UE4SS sources

The accepted UE4SS tree includes first-party modules and license texts for
ASMHelper, Constructs, DynamicOutput, File, Function, Helpers, IniParser,
Input, JSON, LuaMadeSimple, LuaRaw, MProgram, ParserBase, ScopedTimer, and
SinglePassSigScanner. Those texts are MIT License. UE4SS also carries separate
MIT texts for UVTD and USMapGenerator, plus font notices under
`deps/fonts/droid` and `UE4SS/include/fonts`.

The final payload license tool must discover what is actually linked and
packaged; this list is not a license-pruning decision.

## Patch and provenance policy

No UE4SS source patch is present. MecchaCamouflage may:

- select documented UE4SS cache options;
- add UE4SS with `EXCLUDE_FROM_ALL`;
- predeclare exact direct FetchContent commits;
- link `main.dll` against the `UE4SS` target from that same build.

Any edit inside the UE4SS gitlink, change to a locked commit, or additional
patch command stops the architecture gate for review.

## Project-native decoder lock

| Component | Upstream release | Accepted commit | Linked surface | License evidence |
| --- | --- | --- | --- | --- |
| libwebp | `v1.6.0` | `4fa21912338357f89e4fd51cf2368325b59e9bd9` | static `webpdecoder` only | BSD-3-Clause-compatible upstream `COPYING`, preserved at `resources/licenses/libwebp-COPYING.txt` |

The build disables libwebp tools, animation utilities, muxers, viewers,
JavaScript, fuzz targets, extras, and decoder threading. Encoder, demux, and
SharpYUV targets are excluded from the default build graph; only the static
decoder and its decoder-only object targets remain. No libwebp source patch is
applied. The runtime accepts still WebP only; an animation feature bit fails
closed before decoded allocation.

## Protected full-build completion

The following evidence is intentionally deferred because it requires a
maintainer-approved credential with Epic organization access:

1. Initialize UEPseudo and its complete recursive source graph.
2. Configure the exact `Game__Shipping__Win64` graph with MSVC x64.
3. Capture the resolved commit and content hash of every nested git,
   FetchContent, and Cargo input.
4. Collect and classify every corresponding license/notice text.
5. Prove UE4SS and `main.dll` share compiler, configuration, architecture,
   C++ runtime, import ABI, and source provenance.
6. Confirm the UE4SS gitlink remains clean and no source patch is required.

Until that report exists, the checklist item for the complete recursive graph
and the full-build ABI gate remains open. Secret-free work may continue only
on targets that do not link UE4SS.
