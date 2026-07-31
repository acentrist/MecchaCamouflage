# MecchaCamouflage v2 Dependency Lock

This document records the locked Phase 2 source graph. It is both a build input
and a release audit record. The restricted graph has been initialized from the
exact commits below. One project-owned canonical Cargo lock overlay has been
approved for an independent build-only source stage; it resolves locally under
Cargo `--locked` without modifying the accepted gitlink. The protected
full-build must still prove the resulting native binaries and closed dependency
graph, and a separate license manifest must be approved before Phase 2 closes.

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

## Approved build-only source overlay and provenance policy

No edit is made inside the accepted UE4SS gitlink. The approved build-only
source overlay is:

| Identity | Value |
| --- | --- |
| Target | `deps/first/patternsleuth_bind/Cargo.lock` |
| Upstream lock SHA-256 | `19292c3e0a74c851eb11ad09a3b3ac5e5d8e9b80eebe34dd705df10e09dc7e50` |
| Canonical lock SHA-256 | `88c3718c03492cdc2650217a9d8bb2a8dbdecdbde1b4ea79e3e529e838b49bbe` |
| Raw staged Git binary-diff SHA-256 | `0dac25e7c79d430aca62411cddf66c17d95340e2bee174f453f17f610a839f8f` |
| Policy | `cmake/ue4ss-source-overlay.json` |
| Canonical bytes | `cmake/ue4ss-overlays/6c26f038751b3d96059d4a9148f5d093012d55ad/patternsleuth_bind.Cargo.lock` |

MecchaCamouflage may:

- select documented UE4SS cache options;
- add UE4SS with `EXCLUDE_FROM_ALL`;
- predeclare exact direct FetchContent commits;
- clone the exact initialized accepted graph into an independent no-hardlink
  build stage and replace only the tabled Cargo lock with the exact canonical
  bytes;
- pass Cargo `--locked` to the imported patternsleuth target so the accepted
  lock cannot be silently pruned or regenerated;
- link `main.dll` against the `UE4SS` target from that same build.

The staging tool checks the pristine source and nested commits, original and
overlay hashes, exact one-file Git diff, and exact canonical manifest. CMake
accepts only the verified stage and manifest. The verifier runs again after
build, and provenance plus dependency evidence bind the stage-manifest hash and
diff identity. Any edit inside the gitlink, locked commit change, additional
overlay, unexpected/untracked staged entry, or changed hash stops the
architecture gate for review.

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

The available maintainer-authorized checkout established:

1. UEPseudo and patternsleuth resolve to the exact nested gitlinks above.
2. The complete graph configures as `Game__Shipping__Win64` with MSVC x64.
3. An unlocked diagnostic build reaches UE4SS, proxy, and mod binaries with the
   expected binary shape.
4. That build rewrites the tracked
   `deps/first/patternsleuth_bind/Cargo.lock`, deleting 368 lines and changing
   one dependency label.
5. Project-enforced Cargo `--locked` fails before compilation instead of
   accepting that mutation. Cargo `1.88.0` and `1.97.1` both reproduce the
   rejection against the upstream lock.
6. The explicitly approved canonical overlay contains exactly Cargo's
   deterministic 368-line pruning and one dependency-label normalization.
   Preparation, repeated reuse, `cargo metadata --locked --offline` for
   `x86_64-pc-windows-msvc`, and post-command stage verification pass against
   the accepted real graph.

The diagnostic binary inspection is not stage-bound provenance and is not
accepted. The protected build must now produce binaries from the approved
stage before the following work can pass:

1. Capture the closed production-target identities and target-filtered Cargo
   closure.
2. Prove binary ABI and provenance from that same clean checkout.
3. Review every corresponding license and notice file.
4. Approve the audit only when its component identities and evidence hash
   exactly match.

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
