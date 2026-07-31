# MecchaCamouflage v2 Dependency Lock

This document records the locked Phase 2 source graph. It is both a build input
and a release audit record. The restricted graph has now been initialized and
built from the exact commits below. The protected full-build gate must still
produce the resolved transitive Cargo/FetchContent evidence and a separately
approved license manifest before Phase 2 can be closed.

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
- pass Cargo `--locked` to the imported patternsleuth target so the accepted
  lock cannot be silently pruned or regenerated;
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

## Project fallback glyph-atlas lock

The fallback is a pre-rendered RGBA atlas rather than a runtime font engine or
an additional font parser. It is generated from the unmodified Regular OTC at
the signed Noto Sans CJK `Sans2.004` tag and packages only the atlas, its exact
geometry/codepoint manifest, and the upstream OFL text.

| Component | Upstream tag | Accepted commit | Source SHA-256 | Packaged evidence |
| --- | --- | --- | --- | --- |
| Noto Sans CJK Regular OTC | `Sans2.004` | `523d033d6cb47f4a80c58a35753646f5c3608a78` | `b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a` | `resources/fonts/fallback/fallback-glyph-atlas.{png,json}` and `Noto-CJK-OFL.txt` |

`verify_fallback_glyph_atlas.py` binds the PNG hash and RGBA dimensions to the
manifest, binds the source commit/font hash and OFL hash, rejects duplicate or
malformed inventory, and requires exact sorted coverage of every codepoint in
all 16 shipped translations and locale display names plus the replacement
character. The trusted runtime assembler reruns that verification before
copying either atlas file. The original 18.6 MiB OTC and ImageMagick are
generation inputs only and are not linked, loaded, or distributed.

## Protected full-build completion

The available maintainer-authorized checkout completed the following direct
evidence:

1. UEPseudo and patternsleuth resolved to the exact nested gitlinks above.
2. The complete graph configured as `Game__Shipping__Win64` with MSVC x64.
3. UE4SS, its proxy, and `main.dll` built from that one graph.
4. Binary inspection proved the expected architecture, two-export mod ABI,
   direct UE4SS import, dynamic MSVC runtime, and binary hashes.
5. The UE4SS gitlink remained clean; no UE4SS patch was required.

The remaining protected gate is review work, not a compilation assumption:

1. Capture the closed production-target identities and target-filtered Cargo
   closure from the protected build.
2. Review every corresponding license and notice file.
3. Approve the audit only when its component identities and evidence hash
   exactly match.

Until that review exists, the checklist item for the complete recursive graph
and licenses remains open. The successful build does not make its unapproved
dependency audit distributable.

`tools/v2/build_dependency_notices.py` is the fail-closed consumer for that
future report. It requires the resolved evidence and a separate approved
component/license/hash audit to match exactly; it does not treat this public
dependency summary as approval or as a complete packaging inventory.

`tools/v2/collect_dependency_evidence.py` produces the machine side of that
boundary from the closed production-target CMake File API codemodel, actual git
roots and tracked diffs, and the target-filtered Cargo resolve closure. Registry
packages are bound to both `Cargo.lock` and Cargo's package checksum evidence;
resolved feature sets are part of each source identity. The protected workflow
uploads this evidence for separate license review.

`tools/v2/generate_dependency_audit_template.py` copies only those validated
component identities and the evidence SHA-256 into a canonical review
template. Every license expression and file list is deliberately empty. The
template is not approval and is rejected by the notice builder until a
maintainer verifies and fills every component against the protected graph.
