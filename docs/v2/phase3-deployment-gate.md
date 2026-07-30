# Phase 3 Safe Deployment Gate

## Current status

The manifest, ownership, loader-policy, recoverable transaction, native
Windows managed-runtime storage, and side-effect-free preparation/removal
planning foundations are implemented. The managed runtime is not yet connected
to game-directory mutation, elevation, Steam launch, or the final embedded
payload, so Phase 3 remains open.

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
- Steam discovery reads bounded Valve VDF data, resolves App ID `4704690`
  across default and non-default libraries, validates the app manifest and
  exact `Chameleon/Binaries/Win64` executable shape, and rejects ambiguous
  installs.
- Explicit game-directory selection accepts only the validated installation
  root or its exact `Win64` directory. Windows registry discovery is isolated
  behind the native adapter.
- Running-game detection enumerates process image names through Toolhelp and
  never opens or attaches to the game process.
- Steam `LaunchOptions` are read through the same bounded VDF parser. The
  Windows command line is tokenized with the platform parser; missing,
  duplicate, disabled, or malformed UE4SS arguments fail closed.
- `override.txt` is a bounded, single-line, no-BOM directory directive matching
  the pinned proxy implementation. Relative command and override paths resolve
  from the game executable directory.
- The filesystem observer hashes the resolved command-line, override,
  `ue4ss/UE4SS.dll`, and root `UE4SS.dll` candidates. A configured missing
  target or any incompatible fallback remains a conflict instead of silently
  falling through.
- Preparation observations are converted into a minimal, side-effect-free
  mutation plan before any file is changed. Invalid payloads, a running game,
  deployment conflicts, managed-cache conflicts, and inaccessible original-user
  cache storage all fail closed.
- Managed preparation publishes or reuses the LocalAppData runtime and changes
  only loader files classified as create or owned replacement. Elevation is
  requested only when one of those loader changes is necessary and the game
  directory is not writable.
- LocalAppData runtime publication is never delegated to an elevated process.
  Exact shared-runtime mode never changes the managed cache or loader files and
  never elevates a mod-directory write.
- Removal planning is ownership-aware. Managed removal deletes only the owned
  cache, proxy, and override; exact shared-runtime removal deletes only the
  owned MecchaCamouflage mod. Exact unowned files remain untouched, and
  conflicting observations abort the entire plan.
- The native owned-file store binds each mutable target to a separate,
  path-and-role-specific LocalAppData ownership receipt. Matching unowned
  content can be reused by policy but is never silently claimed, replaced, or
  removed.
- Owned-file publication uses write-through staging plus
  `installing`/`complete`/`removing` receipt phases. Recovery either confirms
  the published bytes, restores the prior ownership record, or completes a
  verified removal; any changed target, staging file, receipt, or reparse path
  fails closed.
- Read-only observation and no-op removal do not create ownership directories.
  Mutation remeasures the target immediately before atomic publication, and
  removal remeasures it after recording intent.

## Automated evidence

Linux secret-free tests exercise the portable parser, policy, loader, and
transaction state machine. Windows tests additionally exercise CNG and real
temporary directory trees.

The preparation/removal planner is exercised on Linux and MSVC Release. Its
tests cover clean managed preparation, exact reuse, minimal owned update,
prepare-only operation, managed UAC decisions, shared mod installation,
running-game rejection, invalid payloads, inaccessible cache storage, every
deployment conflict, ownership-safe managed/shared removal, and removal
conflicts.

The MSVC Release owned-file suite covers create, no-op reuse, owned update,
idempotent removal, exact-unowned refusal, post-install tampering, payload
hash mismatch, read-only observation, interrupted receipt publication,
post-publish install recovery, post-delete removal recovery, and a
privilege-free NTFS junction fixture. The optional file-symbolic-link variant
is skipped on hosts where Windows developer-mode link creation is unavailable;
the junction fixture remains mandatory and passes.

Covered Windows directory cases:

- fresh publication;
- unchanged no-op reuse without payload reads;
- old-to-new publication with one final `active` generation;
- generated log content;
- immutable payload tampering;
- interrupted partial extraction and retry;
- unknown content inside `active`;
- unknown content at the managed runtime root.
- default and non-default Steam libraries;
- current and legacy library VDF forms;
- malformed manifests, traversal install names, missing folder shapes, and
  ambiguous installations;
- Windows process enumeration against the calling test executable.
- Steam launch-option extraction and Windows command-line tokenization;
- relative override resolution, exact runtime hashing, missing candidates, and
  incompatible lower-priority candidates.

The semantic transaction fake injects failures at every rename boundary
without adding a test-only branch to production coordination.

## Remaining Phase 3 work

- Connect active Steam-user `localconfig.vdf` lookup to launcher orchestration.
- Connect the native folder-picker fallback to the validated explicit-directory
  path.
- Connect the validated preparation/removal plans to native side-effect
  orchestration.
- Implement compatible shared-tree observation and mod-only installation.
- Implement the managed `dwmapi.dll` and `override.txt` mutation set,
  ownership persistence, exact-match reuse, and removal.
- Add the minimal elevated two-file broker and alternate-credential contract.
- Extend the passing owned-file junction fixture to the managed runtime
  generation adapter.
- Connect the embedded payload source and final runtime generated-path
  allowlist.
- Run the deferred live managed/shared/UAC/launch checks after the game is
  stopped and a complete trusted UE4SS build exists.

No launcher executable is considered releasable at this checkpoint.
