# Phase 12 Packaging Progress

## Current status

Canonical payload-manifest generation, deterministic CAB assembly, bounded
Win32 `RCDATA` loading, and manifest-exact CAB consumption are implemented.
Final release-resource binding and exact release-artifact verification remain
open. No launcher executable is considered releasable at this checkpoint.

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

Portable target-validation coverage runs in the normal Linux graph and the
Linux ASan/UBSan graph. The MakeCab determinism and round-trip case runs in the
Windows MSVC Release graph.

## Remaining packaging work

- Assemble and freeze the minimal trusted UE4SS runtime layout.
- Embed CAB, manifest, layout identity, localization, profiles, fonts, icon,
  project/UE4SS/dependency licenses, and notices as Win32 resources.
- Bind the read-only resource/CAB source to final launcher material
  construction.
- Add final binary, provenance, import/export, license, forbidden-artifact, and
  one-EXE checks.
