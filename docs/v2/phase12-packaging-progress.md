# Phase 12 Packaging Progress

## Current status

Canonical payload-manifest generation is implemented and registered in the
secret-free test graph. Deterministic CAB assembly, Win32 resource embedding,
the final payload reader, and exact release-artifact verification remain open.
No launcher executable is considered releasable at this checkpoint.

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

The test runs in the normal Linux graph, the Linux ASan/UBSan graph, and the
Windows MSVC Release graph.

## Remaining packaging work

- Assemble and freeze the minimal trusted UE4SS runtime layout.
- Produce a deterministic CAB from canonical manifest order.
- Extract the CAB into a hostile temporary tree and verify exact file set,
  sizes, hashes, generated-path policy, and absence of reparse entries.
- Embed CAB, manifest, layout identity, localization, profiles, fonts, icon,
  project/UE4SS/dependency licenses, and notices as Win32 resources.
- Implement the read-only embedded resource source and bind it to launcher
  preparation.
- Add final binary, provenance, import/export, license, forbidden-artifact, and
  one-EXE checks.
