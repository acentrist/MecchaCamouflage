# Phase 3 Safe Deployment Gate

## Current status

The manifest, ownership, loader-policy, recoverable transaction, and native
Windows managed-runtime storage foundations are implemented. The managed
runtime is not yet connected to Steam discovery, the game-directory loader
files, elevation, or the final embedded payload, so Phase 3 remains open.

## Implemented contracts

- Payload manifests use a dedicated schema identity and strict JSON parsing.
- Immutable files carry canonical relative paths, roles, sizes, and lowercase
  SHA-256 values. Case-colliding, traversal, ADS, device, and reserved Windows
  paths are rejected.
- `generated_paths` explicitly names generated directory roots. They cannot
  overlap immutable files or one another.
- Windows hashing uses CNG/BCrypt for byte and streaming file hashes.
- Ownership requires the expected product version, payload-manifest hash,
  relative path, role, size, recorded file hash, and current file hash.
- Deployment policy selects managed, exact shared, or conflict without
  performing mutations. Unknown proxies, overrides, runtimes, settings, and
  mod payloads fail closed.
- Loader resolution models the pinned proxy priority exactly:
  `--ue4ss-path`, `override.txt`, `ue4ss/UE4SS.dll`, then root `UE4SS.dll`.
  Configured but unresolved higher-priority paths do not silently fall
  through.
- Managed publication uses one `active`, one `rollback`, one named staging
  generation, and an atomically replaced journal.
- Recovery handles fresh publication, no-op reuse, update, failure between
  the two renames, promotion before commit, committed cleanup, and owned
  partial extraction.
- The Windows directory adapter uses write-through handles, flushes payload
  and metadata files, rejects reparse points while walking managed paths,
  hashes every immutable file, allows only manifest-declared generated roots,
  and enumerates validated entries before deletion.
- Unknown or modified content is preserved and reported as a conflict. It is
  never repaired or removed.

## Automated evidence

Linux secret-free tests exercise the portable parser, policy, loader, and
transaction state machine. Windows tests additionally exercise CNG and real
temporary directory trees.

Covered Windows directory cases:

- fresh publication;
- unchanged no-op reuse without payload reads;
- old-to-new publication with one final `active` generation;
- generated log content;
- immutable payload tampering;
- interrupted partial extraction and retry;
- unknown content inside `active`;
- unknown content at the managed runtime root.

The semantic transaction fake injects failures at every rename boundary
without adding a test-only branch to production coordination.

## Remaining Phase 3 work

- Parse and validate Steam launch options and filesystem paths into the loader
  resolver observations.
- Discover App ID `4704690` across Steam libraries and validate the exact game
  executable shape, with a folder-picker fallback.
- Reject prepare/remove while the game is running.
- Implement compatible shared-tree observation and mod-only installation.
- Implement the managed `dwmapi.dll` and `override.txt` mutation set,
  ownership persistence, exact-match reuse, and removal.
- Add the minimal elevated two-file broker and alternate-credential contract.
- Add explicit junction/reparse fixtures to the Windows temporary-tree suite.
- Connect the embedded payload source and final runtime generated-path
  allowlist.
- Run the deferred live managed/shared/UAC/launch checks after the game is
  stopped and a complete trusted UE4SS build exists.

No launcher executable is considered releasable at this checkpoint.
