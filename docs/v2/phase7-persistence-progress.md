# Phase 7 Configuration and Persistence Progress

Phase 7 is open. The strict configuration codec, atomic Windows `config.json`
path, localization catalog boundary, immutable project model, and v2-only
preset codec, project coordinator, and shared atomic Win32 file adapter are
implemented. The active-project recovery transaction and bounded off-thread
active-draft debounce worker are also implemented. Named project commands have
an owned off-thread I/O worker, and one editor session now coordinates those
operations with decode/composition, draft persistence, immutable snapshots,
and `ApplicationRoot`. Startup recovery is now part of root initialization.
Production Win32 composition construction, UCanvas controls, and glyph
coverage are not yet complete.

## Configuration contract

- The only top-level keys are `schema_version`, `ui`, `paint`, `image_paint`,
  `esp`, and optional `active_image_project`.
- `schema_version` is exactly `1`; v1 and unknown schemas are rejected without
  migration or mutation.
- Every non-optional field is required and unknown fields are rejected.
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
- `esp.enabled` is the single owner of both persisted and active ESP
  enablement. Startup uses the loaded value; a typed toggle atomically saves a
  complete validated config before publishing the new value, and a failed save
  leaves capture/draw enablement unchanged with a command diagnostic.
- `active_image_project` contains only a validated internal 32-character
  lowercase hexadecimal project ID and a `named` or `draft` discriminator.
  It never contains an editor revision, image byte, or layer.

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
embedded-ID/file-name mismatches, requires strictly monotonic editor revisions
for saves, increments revisions for renames, and makes delete/clear
idempotent. Revision gaps are valid because multiple in-memory edits can occur
between named saves. The portable contract is verified with missing, stale,
corrupt, legacy, mismatched, overflow, and injected I/O cases.
`Win32AtomicProjectStorage` publishes those containers only inside the managed
`image-projects` area through the shared atomic-file primitive.

`ImageProjectPersistenceCoordinator` implements the cross-file publication and
startup recovery contract:

- a named project or active draft is fully published before its small config
  reference;
- a named load validates and decodes the complete project before publishing
  its active reference;
- deleting an active named project clears its config reference before removing
  the project, so a config-write failure leaves the referenced project intact;
- a matching active draft is removed with its deleted named identity;
- config publication failure never rolls back or deletes the valid project;
- a matching newer active draft supersedes its named project on startup;
- a missing or corrupt named project reports a bounded diagnostic and falls
  back to the last valid active draft or a blank editor;
- a corrupt draft never partially replaces editor state and is not rewritten
  during recovery.

`ActiveDraftPersistenceWorker` owns one immutable pending project, resets its
deadline on edits, serializes only the latest generation on its dedicated
thread, and exposes immutable pending/in-flight/completion/error state. It can
flush or discard pending work during explicit shutdown, rejects work after
shutdown, and cannot run container encoding in a Canvas callback.

`ImageProjectIoWorker` serializes one copied Load, Save, Rename, or Delete
request on a dedicated thread. It validates command IDs, project identities,
complete config candidates, and save candidates before admission; preserves
the store/coordinator's typed failures; tags the immutable completion with its
command and operation; contains escaping filesystem/codec exceptions; joins
before reuse; and permanently closes on shutdown. Atomic operations are
allowed to finish during shutdown rather than being interrupted midway.

`ImageEditorSession` is the project-owned lifetime boundary over the editor
pipeline, named I/O worker, and active-draft worker. It:

- runs the persistence coordinator's bounded startup recovery exactly once,
  before normal editor or persistence admission, then submits a recovered
  named/draft project to the existing decode/composition pipeline;
- publishes the recovery source, pipeline generation, typed diagnostics, and
  terminal recovery failure in its immutable session snapshot;
- admits one persistence command at a time and exposes its command/operation
  pressure in the immutable application snapshot;
- schedules only a newly ready editor generation for debounced draft storage;
- loads an explicitly selected named project as a replacement even when its
  revision is older than the current in-memory edit;
- saves only an exact ready project, and renames by copying the current ready
  value, advancing its revision, and publishing it optimistically so unsaved
  settings/layers are not replaced by an older stored value;
- drains pending/in-flight draft publication before deleting that identity,
  preventing the draft worker from recreating a deleted project;
- returns typed command completions and applies a published config back to the
  root snapshot;
- clears deleted editor state and cancels any derived work before stale output
  can publish; and
- finishes accepted atomic persistence, flushes the active draft, cancels
  derived workers, and closes permanently during final shutdown.

`ApplicationRoot` routes the four typed project commands through this narrow
session port on HUD frames. It never performs project codec or filesystem work
on a HUD/Canvas callback. During initialization it passes the validated loaded
configuration through startup recovery before registering the runtime
callback. A recovery admission or worker-boundary failure leaves the root
incompatible and registers no callback. Recovery diagnostics remain bounded
and visible without making missing/corrupt optional editor state fatal.
Session progress is advanced while shutting down, and the root retains
callbacks until accepted persistence, active derived composition, and the
final debounced draft are settled. The session is then closed only after
lifecycle callback unregistration, so callback code cannot race destroyed
editor state.

`image_project_persistence` verifies missing-reference recovery,
newer-draft precedence, corrupt-draft isolation, the named-project-first
partial-failure case, load-before-activate, reference-before-delete,
delete refusal on config failure, monotonic revision gaps, debounce
coalescing, off-caller-thread publication, failure visibility, and shutdown
rejection. `image_project_io_worker` covers single-operation admission,
off-thread load/activate, save, rename revision advance, active delete,
missing-project failure, exception containment, reuse, and terminal shutdown.
`image_editor_session` covers exact-once named startup recovery, load/activate,
exact ready-save, draft debounce, rename without losing current edits,
delete-after-draft-drain, immutable pressure, reuse, and terminal shutdown.
`application_root_image_paint` verifies root-owned startup recovery before
callback registration, fail-closed recovery, all four typed project commands,
config publication, editor snapshot routing, and close-after-lifecycle
ordering.
The portable suite passes on Linux and Windows MSVC Release.

## Remaining gate

- Production composition must construct the accepted session with the Win32
  stores, native decoders, and final UCanvas editor.
- The final v2-only UI key set and game/OFL fallback glyph coverage remain.
- Complete per-step fault injection, power-loss/antivirus-lock simulation, and
  native file-picker coverage before Phase 7 closes.
