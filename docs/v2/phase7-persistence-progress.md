# Phase 7 Configuration and Persistence Progress

Phase 7 is open. The strict configuration codec, atomic Windows `config.json`
path, localization catalog boundary, immutable project model, and v2-only
preset codec, project coordinator, and shared atomic Win32 file adapter are
implemented. Runtime active-draft integration and final glyph coverage are not
yet complete.

## Configuration contract

- The only top-level keys are `schema_version`, `ui`, `paint`, `image_paint`,
  and `esp`.
- `schema_version` is exactly `1`; v1 and unknown schemas are rejected without
  migration or mutation.
- Every field is required and unknown fields are rejected.
- Duplicate object keys are rejected before typed publication, including
  escaped spellings that decode to the same key.
- JSON comments and trailing non-whitespace content are rejected.
- The complete candidate is parsed into a temporary value and then passed
  through the project-owned domain validator.
- Function keys and domain enums use explicit stable string spellings rather
  than compiler-dependent numeric representations.
- `config.json` has a 64 KiB hard limit.
- Image sources, layer collections, canonical atlas bytes, draft state, window
  geometry, WebView, bridge, injector, and process fields have no
  representation in the configuration type.

## Store boundary

`ConfigStore` depends on the project-owned `AtomicTextStorage` interface. A
missing file returns validated defaults without creating a directory or file.
A malformed file returns an error and is left byte-for-byte untouched.
Invalid candidates never reach storage.

The Windows config and project adapters share one managed-file primitive. It:

- resolves LocalAppData through `SHGetKnownFolderPath`;
- uses `%LOCALAPPDATA%\MecchaCamouflage\v2\config.json` and
  `%LOCALAPPDATA%\MecchaCamouflage\v2\image-projects\*.mcpreset`;
- creates only the required managed directory components;
- serializes publication/cleanup through a bounded cross-process Windows
  mutex;
- rejects managed directories, targets, and owned staging paths that are
  directories or reparse points;
- accepts only one safe internal file-name component;
- performs bounded handle-based reads and rechecks the opened file attributes;
- writes a unique same-directory GUID staging name with `CREATE_NEW`,
  `FILE_FLAG_WRITE_THROUGH`, and `FlushFileBuffers`;
- replaces the destination with
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`;
- removes only exact product-owned interrupted staging names, preserves
  staging-like unknown files, and fails closed on an unsafe owned conflict.

The previous valid destination remains intact when validation, staging,
writing, flushing, or publication fails.

## Automated evidence

`config_persistence` covers deterministic round trips, stable enum spellings,
retired-field absence, unknown/missing/duplicate fields, old schemas, invalid
values, trailing data, size limits, default loading, failure preservation, and
the isolated v2 data root.

`config_storage` and `image_project_storage` are Windows-only and cover missing
reads without directory creation, initial publication, replacement, bounded
reads/writes, traversal rejection, interrupted unique staging recovery,
unknown staging preservation, fail-closed unsafe conflicts, idempotent removal,
and non-ASCII LocalAppData paths.

## Localization boundary

The 16 v1 catalogs and their 149 translated keys are preserved under
`resources/localization/catalog.json` while the v2 UI key inventory is being
defined. Retired architecture-only keys must be removed when Phase 11 freezes
the final UI catalog; they are data only and have no runtime behavior.

`LocalizationCatalog`:

- requires exactly the 16 locked locale codes in their product display order;
- requires every locale to have exactly the English key set;
- rejects duplicate JSON keys, malformed JSON, comments, invalid UTF-8, empty
  values, invalid key syntax, oversized catalogs/values, and resource
  overflows;
- requires each translation to preserve the English numeric placeholder
  multiset, while allowing language-specific placeholder order;
- exposes English fallback and key fallback without mutating the catalog;
- performs bounded indexed placeholder formatting;
- produces the unique Unicode codepoint inventory for deterministic font
  coverage checks, including localized locale display names.

`localization` verifies the shipped resource, representative Japanese/Korean/
Cyrillic glyph inventory, English fallback, formatting, placeholder failures,
locale-set failures, semantic duplicate keys, invalid UTF-8, and size limits.

Verified in both the Linux secret-free build and the Windows MSVC Release
build. The repeated MSBuild `MSB8064`/`MSB8065` messages are caused by building
the Windows tree through a WSL UNC path and are not project compiler warnings.

## v2-only project container

[`image-preset-container.md`](image-preset-container.md) freezes the
uncompressed little-endian `MCV2PR01` format before filesystem publication is
implemented.

- Project identity, display name, revision, all Image Paint settings, ordered
  layers, content-addressed original sources, and the exact 1024×512 RGBA atlas
  are validated as one immutable domain value.
- Alpha `skip`/`background` is now an explicit retained setting rather than an
  untyped transport string.
- Source objects use immutable shared bytes and semantic equality; source
  storage order is canonicalized without changing layer order.
- Every source asset ID is its lowercase SHA-256. The atlas and every source
  descriptor carry a binary SHA-256 and must match before publication. The
  exact manifest bytes are also hashed and verified before JSON parsing.
- Header, manifest, entry-count, per-source, total-source, atlas, and complete
  container bounds are checked before allocation or copying.
- Strict JSON rejects missing, unknown, duplicate, malformed, non-finite, and
  trailing manifest content.
- Entry names are sorted, strict lowercase ASCII relative paths. Traversal,
  alternate separators, unknown roles, duplicate names, case collisions,
  unused entries, truncation, and trailing data fail closed.
- PNG, JPEG, and WebP are the only manifest codecs. The codec does not decode
  pixels; WIC/WebP remain separate bounded Image Paint adapters.
- `MCIPRST1` is recognized only to return the dedicated non-destructive legacy
  result. No v1 field or migration path is interpreted.
- SHA-256 is shared through the narrow `meccha_common` boundary. Production
Windows uses BCrypt; secret-free portable codec tests inject a deterministic
test hasher without adding a selectable production fake.

`ImageProjectStore` coordinates named projects and the active recovery
draft through a project-owned atomic-storage port. It keeps project IDs out of
filesystem path parsing, preserves structured storage/codec failures, rejects
embedded-ID/file-name mismatches, requires consecutive revisions for saves and
renames, and makes delete/clear idempotent. The portable contract is verified
with missing, stale, corrupt, legacy, mismatched, overflow, and injected I/O
cases. `Win32AtomicProjectStorage` publishes those containers only inside the
managed `image-projects` area through the shared atomic-file primitive.

## Remaining gate

- The application composition root must wire the project store and implement
  active-draft lifecycle/debounce behavior.
- The final v2-only UI key set and game/OFL fallback glyph coverage remain.
- Reparse-point, power-loss, antivirus-lock, non-ASCII path, and native file
  picker coverage must be completed before Phase 7 closes.
