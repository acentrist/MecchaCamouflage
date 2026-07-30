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
- Canonical payload roles are also destination boundaries. Proxy and generated
  override entries are never extracted into `runtime/active`; the managed
  runtime transaction publishes only runtime-cache roles.
- The pinned UE4SS source confirms that `override.txt` contains the directory
  holding `UE4SS.dll`, while the product mod is self-enabled by
  `Mods/MecchaCamouflage/enabled.txt` and loads
  `Mods/MecchaCamouflage/dlls/main.dll`. The launcher will not edit a shared
  `Mods/mods.txt`.
- Managed loader material accepts exactly one manifest-owned `dwmapi.dll` and
  generates `override.txt` from the already-published `active` directory. The
  file has no BOM or trailing newline. Because the pinned proxy reads a narrow
  string, the path must be ASCII or resolve to an ASCII Windows short path;
  otherwise preparation fails closed instead of writing lossy text.
- Non-elevated loader application recovers both ownership transactions and
  revalidates both dispositions before changing either file. A stale plan
  performs no mutation. Exact unowned proxy reuse remains unowned.
- A plan marked for elevation performs no local mutation and returns an
  explicit broker-required result. The broker protocol remains separate and
  is not emulated by an unsafe fallback.
- Managed removal preflights both ownership receipts and current file hashes
  before deletion. It removes the proxy first so that any later interruption
  leaves a harmless override rather than an active proxy, and exact unowned
  proxy content is never removed.
- Owned-file installation creates previously absent plain parent directories
  only after read-only preflight succeeds. Observation and no-op removal keep
  absent target and receipt trees absent. An owned-version update whose bytes
  are unchanged refreshes only the recoverable receipt instead of rewriting
  the target.
- Shared-mod material accepts only canonical `mod` entries below
  `Mods/MecchaCamouflage`, requires `dlls/main.dll` plus an empty
  `enabled.txt`, and verifies every byte against the payload manifest before
  inspecting the destination.
- Shared installation requires an existing plain shared-runtime root and
  `Mods` directory. It verifies the pinned runtime/config hashes before and
  after target preflight, creates directories only below
  `Mods/MecchaCamouflage`, and keeps its ownership receipts in the original
  user's LocalAppData ownership tree.
- Shared ownership receipts are scoped by the exact absolute shared-runtime
  root. A receipt created for one compatible UE4SS tree cannot authorize
  replacement or removal in a different tree, even when its bytes happen to
  match.
- Each owned shared install publishes a strict, root-scoped installed-file
  ledger through the same recoverable owned-file transaction. The ledger is
  canonicalized independently of manifest ordering and records the exact
  per-file product version, manifest hash, path, role, size, and content hash.
- Updates preflight both current and ledger-only files, durably publish an
  old/new union transition ledger before touching the mod, install the new
  payload, remove only stale receipt-backed files, and publish the compact
  final ledger last. Files newly added by an interrupted intermediate release
  therefore remain discoverable even if a later release has already removed
  them from its manifest.
- Removal uses the union of current payload entries and the installed ledger,
  so a newer launcher can remove an older complete or partially updated mod.
  A missing, malformed, modified, unowned, or cross-root ledger never grants
  deletion authority.
- Exact owned and exact unowned shared mods are reusable without rewriting or
  claiming content. Unknown, modified, stale-plan, and reparse targets fail
  closed before any mod payload is installed.
- Shared removal preflights every current manifest file and deletes only
  receipt-backed, hash-matched MecchaCamouflage files. `UE4SS.dll`,
  `Mods/mods.txt`, loader files, settings, unrelated mods, and exact unowned
  MecchaCamouflage files are never changed.
- Before the first deletion, shared removal takes bounded, reparse-refusing
  snapshots of both `Mods/MecchaCamouflage` and its root-scoped ownership
  metadata. Every observed file and directory must be derivable from the
  current/installed ledger union. Unknown content aborts the whole operation;
  a successful removal then prunes only the now-empty Meccha-owned directory
  tree and leaves shared parent directories intact.
- The portable execution coordinator rejects malformed or mixed-mode plans
  before calling an effect. Managed preparation validates/publishes the
  runtime before the normal or elevated loader operation; shared preparation
  invokes only the shared-mod operation. Steam is always last and is omitted
  for prepare-only. Managed removal deactivates the loader before deleting the
  runtime cache, while shared removal cannot reach either managed operation.
  Every effect failure terminates the sequence immediately.
- Runtime reuse now has a strictly read-only transaction check: `active` must
  match the expected manifest and no journal, staging, or rollback generation
  may exist. Runtime removal first completes the existing recovery state
  machine, then removes only the ownership-identified `active` generation; a
  missing cache is an idempotent no-op and conflicting content is untouched.
- The Win32 execution backend binds that transaction contract to the existing
  managed-loader and shared-mod adapters. Elevated managed operations are
  handed only to the broker interface, with cache/mod actions stripped from a
  removal request. Normal managed and shared integration tests operate on real
  temporary Windows trees; Steam launch remains an injected final effect and
  is not invoked by the tests.
- Active Steam launch options are resolved from the calling user's HKCU
  `SteamPath` and `ActiveProcess/ActiveUser`, then read from that exact
  `userdata/<account>/config/localconfig.vdf` through the bounded VDF reader.
  A missing/zero active user or unreadable file fails closed. The native folder
  picker returns only a filesystem directory and routes it through the same
  strict game-shape validation as `--game-dir`.
- The concrete Steam launcher opens only
  `steam://rungameid/4704690`. It is an injected final execution effect, closes
  any returned shell process handle, and is never exercised by automated
  tests or preparation validation.
- The public command-line parser accepts only `--game-dir <path>`,
  `--prepare-only`, and `--remove`. Duplicate switches, missing or
  embedded-NUL values, conflicting modes, positional values, and unknown
  operations are rejected before discovery or mutation. Internal broker
  inputs are not part of this parser.
- Normal launcher invocations hold the fixed per-session
  `MecchaCamouflage.v2.Launcher` mutex for their complete lifetime. A second
  invocation fails immediately, and the RAII guard releases the kernel object
  on every exit path. The child broker protocol remains a distinct internal
  mode so the elevated process cannot deadlock against its parent.
- Canonical manifest generation consumes a separately declared exact layout,
  measures every payload file, rejects extra/missing/hostile/reparse entries,
  orders paths deterministically, and publishes through atomic replacement.
  CAB creation and embedded payload binding remain open.

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

The managed-loader suite covers payload and generated-override material,
two-file creation, exact reuse, stale-plan preflight, exact-unowned proxy
coexistence, payload tampering, Unicode-path short-path-or-refusal behavior,
stale removal, ownership-safe removal, and zero-mutation elevated handoff.

The shared-mod suite covers canonical material construction, fresh nested
installation, exact-owned and exact-unowned reuse, all-file conflict preflight,
receipt-only unchanged-file updates, changed-file replacement, hash-safe
removal after a runtime-owner update, manifest-removed file cleanup,
new-launcher removal of an old install, ledger tampering, cross-root receipt
rejection, payload tampering, foreign mod-path rejection, preservation of
`Mods/mods.txt`, complete owned-directory cleanup, pre-mutation refusal of
unknown mod or ownership content, and a privilege-free target-directory
junction fixture.
The portable ledger suite additionally proves canonical ordering, strict
round trips, duplicate rejection, per-file ownership retention, and old/new
transition union semantics.
The portable execution suite proves managed/shared isolation, prepare-only,
minimal elevated-loader routing, effect ordering, plan rejection, idempotent
empty removal, and the absence of later mutations or Steam launch after a
failed effect.
The Win32 execution suite additionally proves read-only exact runtime reuse,
normal owned-loader publication, elevated loader-only handoff, loader-before-
cache removal, shared-mod installation/removal, and preservation of the shared
UE4SS runtime.
The command-line and Windows single-instance suites prove the exact public
switch grammar, conflicting/hostile input rejection, concurrent-instance
refusal, and deterministic guard release.

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
- bounded active-user `localconfig.vdf` launch-option reads;
- malformed manifests, traversal install names, missing folder shapes, and
  ambiguous installations;
- Windows process enumeration against the calling test executable.
- Steam launch-option extraction and Windows command-line tokenization;
- relative override resolution, exact runtime hashing, missing candidates, and
  incompatible lower-priority candidates.

The semantic transaction fake injects failures at every rename boundary
without adding a test-only branch to production coordination.

## Remaining Phase 3 work

- Consume the active-user launch-option lookup in the native composition root.
- Invoke the native folder-picker fallback from automatic discovery failures.
- Build the native observation/material composition root around the Win32
  execution backend and connect the concrete Steam URI launcher.
- Validate Unicode and ANSI-round-trip behavior of the pinned proxy's
  narrow-string `override.txt` reader. Use a verified short path when
  available and fail closed if the stable runtime path cannot be represented;
  do not patch the proxy without architecture review.
- Add the minimal elevated two-file broker and alternate-credential contract.
- Extend the passing owned-file junction fixture to the managed runtime
  generation adapter.
- Connect the embedded payload source and final runtime generated-path
  allowlist.
- Run the deferred live managed/shared/UAC/launch checks after the game is
  stopped and a complete trusted UE4SS build exists.

No launcher executable is considered releasable at this checkpoint.
