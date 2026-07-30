# Phase 7 Configuration and Persistence Progress

Phase 7 is open. The strict configuration codec and atomic Windows
`config.json` path are implemented. Active-draft persistence, localization,
the v2-only preset container, and project/source storage are not yet complete.

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

The Windows implementation:

- resolves LocalAppData through `SHGetKnownFolderPath`;
- uses `%LOCALAPPDATA%\MecchaCamouflage\v2\config.json`;
- creates only the two managed directory components;
- rejects managed directories, target files, and staging paths that are
  directories or reparse points;
- accepts only one safe internal file-name component;
- performs bounded handle-based reads and rechecks the opened file attributes;
- writes a fixed, owned `config.json.tmp` with `CREATE_NEW`,
  `FILE_FLAG_WRITE_THROUGH`, and `FlushFileBuffers`;
- replaces the destination with
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`;
- removes only an interrupted plain staging file and fails closed on an unsafe
  staging conflict.

The previous valid destination remains intact when validation, staging,
writing, flushing, or publication fails.

## Automated evidence

`config_persistence` covers deterministic round trips, stable enum spellings,
retired-field absence, unknown/missing/duplicate fields, old schemas, invalid
values, trailing data, size limits, default loading, failure preservation, and
the isolated v2 data root.

`config_storage` is Windows-only and covers missing reads without directory
creation, initial publication, replacement, bounded reads, traversal
rejection, interrupted regular staging recovery, and fail-closed unsafe
staging conflicts.

Verified in both the Linux secret-free build and the Windows MSVC Release
build. The repeated MSBuild `MSB8064`/`MSB8065` messages are caused by building
the Windows tree through a WSL UNC path and are not project compiler warnings.

## Remaining gate

- Active Image Paint draft storage must use a separate bounded format.
- The 16 localization catalogs, placeholder checks, and glyph coverage remain.
- The canonical `.mcpreset` container and content-addressed source storage
  remain.
- Reparse-point, power-loss, antivirus-lock, non-ASCII path, and native file
  picker coverage must be completed before Phase 7 closes.
