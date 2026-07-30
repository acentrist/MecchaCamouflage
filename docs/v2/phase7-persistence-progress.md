# Phase 7 Configuration and Persistence Progress

Phase 7 is open. The strict configuration codec, atomic Windows `config.json`
path, and localization catalog boundary are implemented. Active-draft
persistence, final glyph coverage, the v2-only preset container, and
project/source storage are not yet complete.

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

## Remaining gate

- Active Image Paint draft storage must use a separate bounded format.
- The final v2-only UI key set and game/OFL fallback glyph coverage remain.
- The canonical `.mcpreset` container and content-addressed source storage
  remain.
- Reparse-point, power-loss, antivirus-lock, non-ASCII path, and native file
  picker coverage must be completed before Phase 7 closes.
