# Phase 12 Packaging Progress

## Current status

Canonical payload-manifest generation, deterministic CAB assembly, bounded
Win32 `RCDATA` loading, manifest-exact CAB consumption, and the native GUI
launcher resource boundary are implemented. Final trusted runtime/resource
assembly and exact release-artifact verification remain open. No launcher
executable is considered releasable at this checkpoint.

## Canonical manifest generator

`tools/v2/generate_payload_manifest.py` consumes an exact payload directory and
a separate declarative layout. The layout contains only:

- `schema_version`;
- `generated_paths`;
- `files`, where each file has a canonical relative `path` and `role`.

The build supplies product version and pinned UE4SS commit separately. Size and
SHA-256 are always measured from the payload bytes; neither value can be
provided by the layout.

Generation fails closed when:

- the layout is empty, oversized, malformed UTF-8 JSON, contains duplicate or
  unknown keys, uses an unsupported schema, role, version, or commit;
- a path is absolute, non-ASCII, traversal-capable, case-colliding,
  Windows-reserved, alternate-data-stream capable, or otherwise non-canonical;
- generated paths overlap each other or immutable files;
- the declared and actual payload trees differ in either direction; or
- the payload root or any entry is a symbolic link, junction, or other Windows
  reparse point.

Output files are sorted by case-insensitive canonical path and generated paths
are sorted the same way. JSON field order and formatting are fixed, all strings
are ASCII escaped, and the file ends with one newline. Publication uses a
flushed sibling temporary file followed by atomic replacement. A validation
failure occurs before publication and preserves any previous output.

Example:

```text
python tools/v2/generate_payload_manifest.py \
  --payload-root <assembled-runtime> \
  --layout <payload-layout.json> \
  --output <payload-manifest.json> \
  --product-version 2.0.0 \
  --ue4ss-commit 6c26f038751b3d96059d4a9148f5d093012d55ad
```

The final payload layout is deliberately not committed yet. It must be
generated from the trusted full-build output so it cannot disguise missing
UE4SS files or speculative runtime contents.

## Deterministic CAB assembly

`tools/v2/build_payload_cab.py` uses the Windows inbox
[`makecab.exe`](https://learn.microsoft.com/windows-server/administration/windows-commands/makecab)
and `expand.exe`; it does not add a packaging runtime or network dependency.

The tool:

1. validates the original payload against the exact layout;
2. copies only declared files into a private sibling staging directory;
3. remeasures the staged tree and refuses a changed source snapshot;
4. normalizes staged timestamps and attributes without modifying original
   build outputs;
5. writes a fixed MakeCab DDF in canonical manifest order with fixed LZX
   settings, cabinet thresholds, destination directories, names, and explicit
   per-file CAB date/time/attribute metadata;
6. builds the CAB;
7. expands the CAB into a second private tree and regenerates its manifest;
8. requires exact manifest identity before atomically publishing each CAB and
   manifest output; and
9. removes all private staging and extraction trees.

Manifest and CAB outputs must use distinct paths outside the payload root.
This prevents a previous output or transient workspace from becoming a
self-referential payload input.

Example:

```text
python tools/v2/build_payload_cab.py \
  --payload-root <assembled-runtime> \
  --layout <payload-layout.json> \
  --manifest-output <payload-manifest.json> \
  --cab-output <payload.cab> \
  --product-version 2.0.0 \
  --ue4ss-commit 6c26f038751b3d96059d4a9148f5d093012d55ad
```

## Win32 embedded payload source

`read_current_module_rcdata` uses the documented
[`FindResource`/`LoadResource` resource path](https://learn.microsoft.com/windows/win32/menurc/finding-and-loading-resources)
to copy an exact bounded resource from the current executable.
`Win32CabPayloadSource` then stages those CAB bytes only in a GUID-named
workspace below a caller-approved plain directory and enumerates the cabinet
with
[`SetupIterateCabinetW`](https://learn.microsoft.com/windows/win32/api/setupapi/nf-setupapi-setupiteratecabinetw).

The source fails closed for:

- empty or oversized resources, payloads, and individual files;
- multi-cabinet input;
- non-canonical, undeclared, duplicate, missing, or overlong paths;
- manifest total-size inconsistencies;
- file-size or SHA-256 mismatches;
- corrupt/truncated CAB data; and
- a reparse or non-directory scratch root.

Every extracted file is reopened without following a reparse point, measured,
hashed, and copied into source-owned memory. The extraction tree and staged CAB
must be removed before a successful `open()` returns. Later `read_file()` calls
accept only canonical manifest paths and return caller-owned byte copies. No
extracted packaging workspace survives a successful source; rejected inputs
use the same RAII cleanup path.

## Native launcher resource boundary

The Windows build produces one native x64 GUI target named
`meccha-camouflage-v2.0.0.exe`. Its explicit manifest is `asInvoker`, declares
Windows 10 compatibility, and opts into Common Controls v6. The root build
accepts `MECCHA_PAYLOAD_MANIFEST` and `MECCHA_PAYLOAD_CAB` only as an exact
pair. Supplying one without the other or naming a missing file is a configure
error; supplying both adds them as `RCDATA` resource IDs 101 and 102.

The normal entry point loads and validates that resource pair before binding
the accepted manifest hash to the original-user elevated broker. The internal
nonce/PID-only entry point independently reloads the same resources after
authenticating its pipe parent. It cannot accept payload bytes, game paths, or
loader material through the command line. Development builds may omit the
resource pair to compile and test the native shell, but such an executable
fails closed at package loading and is not a release artifact.

## Trusted runtime assembler

`tools/v2/assemble_runtime.py` accepts only explicit trusted-build
`UE4SS.dll`, `dwmapi.dll`, and `main.dll` paths plus the pinned UE4SS settings,
member layout, UE4SS license, and a separately audited dependency-notice
bundle. It copies no build directory by wildcard and does not run or modify
the upstream UE4SS release script.

The output inventory is limited to:

- the pinned proxy and runtime DLL;
- a project-owned C++ mod directory with `main.dll` and an empty
  `enabled.txt`;
- the upstream member-variable layout and a copied release settings file;
- the exact six profile files, one 16-locale catalog, and eight D-DIN font
  files; and
- project, UE4SS, libwebp, D-DIN, and audited dependency notices.

The settings copy keeps scanning and input support but disables UE4SS
hot-reload paths, UObject-array caching, simple/debug consoles, debug UI, and
crash-dump output. The source settings file is never edited. `UE4SS.log` and
`cache` are the only current generated-path entries; the trusted/live gate
must confirm that exact allowlist before it is frozen.

Assembly refuses missing, empty, linked/reparse, unexpected-resource,
changed-settings-schema, overlapping-output, and pre-existing-output inputs.
It builds in a private sibling directory and publishes the payload and
canonical layout only after complete validation. The release icon is a native
Win32 resource and is not duplicated inside the runtime CAB.

## Automated evidence

`payload_manifest_tool` covers:

- deterministic canonical generation from differently ordered input objects;
- exact file coverage in both directions;
- hostile and malformed layouts, unknown keys/roles, case collisions,
  traversal, and generated-path overlap;
- symbolic-link/reparse refusal;
- duplicate JSON-key refusal; and
- atomic publication plus preservation of the previous manifest after a later
  validation failure.

`payload_cab_tool` additionally proves on Windows that:

- changing original source mtimes and output directories does not change the
  CAB or manifest bytes;
- root and nested files expand with their exact canonical relative paths and
  bytes;
- a truncated CAB fails verification; and
- unsafe overlapping output targets are rejected before tool execution.

`embedded_payload` additionally proves exact `RCDATA` reads, root and nested
CAB files including an empty UE4SS enable marker, workspace cleanup, and
refusal of missing resources, wrong hashes, undeclared files, duplicate
manifest paths, hostile lookups, and truncated CAB data.

`runtime_assembly_tool` additionally proves exact resource/role inventory,
release-only settings without upstream mutation, required notice inputs,
linked-input refusal, pre-existing-output preservation, and no partial
publication after validation failure.

## Exact release artifact verifier

`tools/v2/verify_release_artifact.py` is a dependency-free bounded parser for
the final PE32+ executable. It requires an artifact directory containing
exactly one correctly versioned EXE and independently supplied canonical
payload manifest, CAB, layout, and protected full-build provenance report.

The verifier requires:

- x64 PE32+, the Windows GUI subsystem, no COFF symbol table, no exports, no
  .NET COM descriptor, and high-entropy ASLR, dynamic-base, and NX flags;
- the reviewed exact native DLL import allowlist and absence of WebView2,
  WinForms, bridge, MinHook, D3D/DXGI, networking, and CodeView/PDB artifacts;
- an `asInvoker` application manifest with Common Controls v6 and Windows 10
  compatibility, native icon group 201, and exactly RCDATA 101/102;
- byte-for-byte identity between those RCDATA resources and the independently
  verified canonical manifest/CAB;
- exact layout-to-manifest path/role identity, required product/runtime/mod/
  config/resource/license roles, no build artifacts, and the frozen product
  and UE4SS identities; and
- size/SHA-256 identity for the proxy, UE4SS runtime, and mod against the
  protected x64 Shipping provenance report.

Only after every check passes does it publish a deterministic JSON evidence
report and the canonical `sha256  filename` sidecar, each through atomic
replacement. Tests construct PE/resource fixtures directly and cover extra
files, forbidden imports, CodeView, console binaries, substituted CAB bytes,
mismatched provenance, and signing-policy violations. The parser has also
validated a real MSVC Release launcher with fixture manifest/CAB resources,
including the linker-generated resource and debug-directory layout; this is
development evidence, not final release acceptance.

## Code-signing policy

The v2 pipeline currently produces an explicitly **unsigned** artifact because
the project has no reviewed code-signing identity or protected signing
credential. The verifier rejects a certificate table under this policy so a
partially or unexpectedly signed binary cannot be mislabeled. A future signed
release requires a dedicated security/CI review, protected timestamping and
certificate configuration, Authenticode chain verification, and verification
of the exact post-signing bytes. It must not add Defender exclusions, suppress
SmartScreen, weaken UAC, or rebuild after acceptance. GitHub Releases publish
the verified SHA-256 regardless of signing state.

Portable target-validation coverage runs in the normal Linux graph and the
Linux ASan/UBSan graph. The MakeCab determinism and round-trip case runs in the
Windows MSVC Release graph.

## Remaining packaging work

- Assemble and freeze the minimal trusted UE4SS runtime layout.
- Audit the complete linked dependency notice bundle in the protected build.
- Embed CAB, manifest, layout identity, localization, profiles, fonts, icon,
  project/UE4SS/dependency licenses, and notices as Win32 resources.
- Run the exact artifact verifier against the protected full-build EXE and
  retain its report/checksum beside the release evidence.
