# MecchaCamouflage v2 Image Preset Container

## Identity and limits

The v2 `.mcpreset` format is a deterministic, uncompressed, little-endian
container. It is not ZIP, CAB, JSON-with-Base64, or the v1 `MCIPRST1` format.

- Magic: eight ASCII bytes `MCV2PR01`.
- Container version: unsigned 32-bit integer `1`.
- Manifest schema: unsigned 32-bit integer `1`.
- Manifest limit: 1 MiB of strict UTF-8 JSON.
- Entry limit: 257 (one atlas plus at most 256 unique sources).
- Source limit: 12 MiB each and 64 MiB total.
- Atlas: exactly `1024 * 512 * 4` bytes in RGBA8 order.
- Container limit: 68 MiB.

Recognized `MCIPRST1` input returns the dedicated unsupported-legacy result and
is never interpreted as v2.

## Canonical byte layout

```text
magic[8]
container_version:u32
manifest_length:u32
entry_count:u32
reserved:u32 = 0
manifest_sha256[32]
manifest[manifest_length]
entry_descriptor[entry_count]
entry_data[entry_count]
```

Each descriptor is:

```text
name_length:u16
role:u8                 # 1 = atlas, 2 = original source
reserved:u8 = 0
byte_length:u64
sha256[32]
name[name_length]       # strict ASCII relative entry name
```

Descriptors and their data are ordered by bytewise ascending entry name.
The only atlas entry is `atlas.rgba`. Source entries are
`sources/<lowercase-sha256>.<png|jpg|webp>`. No compression, links, absolute
paths, traversal, alternate data streams, backslashes, unknown entries,
duplicate names, or ASCII-case collisions are representable.

The manifest hash covers its exact UTF-8 JSON bytes. The manifest uses stable
snake-case aggregate field names. It contains project identity, display name,
revision, all Image Paint settings, ordered layer transforms, a source table,
and the atlas descriptor. Each source asset ID is the SHA-256 of its original
bytes. The atlas and every source also carry a descriptor SHA-256. Save sorts
the source table by asset ID; layer order remains unchanged.

## Publication and decoding

Encoding validates the complete project and recomputes every content hash
before producing bytes. Decoding verifies the manifest hash before parsing,
then validates the entire header, strict manifest, descriptor table, bounds,
canonical order, hashes, source references, and domain model in temporary
project-owned memory before returning a project. Trailing bytes and unused
entries are rejected.

Filesystem publication is a separate store responsibility and must use
same-directory atomic replacement. No Canvas callback serializes this format.
