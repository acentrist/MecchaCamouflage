# Phase 3 Safe Deployment Gate

## Current status

The manifest, ownership, loader-policy, recoverable transaction, native
Windows managed-runtime storage, side-effect-free preparation/removal
planning, portable observation-to-plan workflow, concrete normal/shared
execution backend, read-only runtime/shared-state classification, and Steam
launch effect are implemented. The native original-user observation source is
also implemented and bound to the execution workflow through a synchronous
native composition root. A native application coordinator now owns the public
CLI-to-composition order. The final GUI entry point/error UI, production
elevated broker, and embedded-resource binding remain open, so Phase 3 remains
open.

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
- The same immutable loader observation retains the exact successfully parsed
  command-line and override targets, so later composition selects the runtime
  root from the bytes that were actually classified instead of re-reading
  `override.txt`.
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
- Shared-mod observation is strictly read-only. It classifies a compatible
  root as missing, exact owned, exact unowned, safely owned previous, or
  conflict from the payload, root-scoped ownership receipts, and installed
  ledger without transaction recovery or ownership-directory creation.
  Malformed ledgers, mixed ownership, changed current files, and changed stale
  ledger-only files are conflicts.
- Runtime/config compatibility has an independent read-only classifier over
  the material's canonical `runtime` and `config` entries. Missing or
  hash-mismatched bytes are incompatible; malformed material, reparse paths,
  and I/O failures remain typed errors.
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
- The portable launcher workflow obtains exactly one immutable preparation or
  removal observation, derives deployment policy and the minimal mutation plan
  before invoking the execution backend, and preserves the typed observation,
  planning, removal, or execution failure. Observation and planning failures
  cannot reach a mutation or Steam-launch effect, and prepare-only cannot
  launch Steam.
- A portable observation assembler converts the immutable loader chain,
  managed-file ownership, recovered-cache identity, runtime-settings state,
  and shared-mod state into the workflow contracts. It keeps preparation
  selection separate from removal ownership: a verified previous managed
  generation remains removable even when it is incompatible with the current
  payload, while mixed managed ownership and an active shared runtime fail
  closed.
- The native original-user source binds active-user Steam launch options,
  retained loader targets, managed proxy/override ownership, post-recovery
  cache identity, and shared runtime/config/mod classification into that
  assembler. It resolves the selected shared runtime root once for the
  execution composition root and never attaches to the game process.
- Shared execution consumes that exact observed runtime root through a
  composition-owned provider. The backend has no inferred or fallback shared
  path, and both install and removal fail before material access when no
  compatible root was selected.
- The native execution composition root rejects a running game before startup
  recovery or payload access, revalidates the manifest bytes, hash, and parsed
  identity, recovers the bounded runtime transaction, then constructs the
  material, original-user observation, and execution adapters for exactly one
  typed workflow invocation. Invalid manifest identity cannot create the data
  root or read payload bytes.
- The native application coordinator holds the fixed launcher mutex for the
  full synchronous invocation, parses only the public switches, selects an
  explicitly validated UTF-8 game path or automatic Steam discovery with the
  native folder-picker fallback, derives the stable v2 paths from the invoking
  user's LocalAppData known folder, and rejects a running game before nonce or
  package acquisition. Composition rechecks the game immediately before
  recovery to close the package-loading race.
- Runtime staging nonces come from a Windows GUID encoded as exactly 32
  lowercase hexadecimal characters. Package sources receive the invocation
  mode and bounded runtime scratch parent, while the application rejects a
  missing payload-source object before entering composition.
- Owned-file transactions expose a narrow external-mutation split for the
  elevated path. The original-user process revalidates state and durably writes
  an `installing` or `removing` receipt intent in LocalAppData; a privileged
  process may then change only the game-directory target; the original-user
  process recovers/finalizes the receipt from the measured result. An absent
  privileged mutation rolls the intent back, while a completed mutation is
  finalized after interruption.
- The privileged mutation executor has no ownership-directory or arbitrary
  target-path input. It can address only `dwmapi.dll` and `override.txt` below
  a validated game binaries directory. It rejects a running game, invalid
  schema/nonce/manifest identity, mixed apply/remove actions, independently
  rebuilt material mismatches, reparses, staging residue, and changed
  preconditions before changing either target. Verify-only actions never
  rewrite exact files.
- The original-user elevated coordinator rechecks both planned artifact states
  before creating either receipt intent, sends only the fixed-file mutation
  request to an injected privileged client, verifies the returned mutation set,
  and finalizes both receipts from measured targets. Client failure, partial
  privileged success, false success, nonce failure, and finalization failure
  all run deterministic receipt recovery. Exact reuse never acquires a nonce or
  calls the privileged client.
- The internal broker wire format is bounded canonical binary rather than JSON
  or public CLI fields. Its fixed schema carries one nonce, one manifest hash,
  one validated UTF-16 game directory, and the two implicit loader-file
  action/measurement tuples. Responses echo the nonce and carry either the
  exact mutation flags or one bounded UTF-8 typed error. Truncation, trailing
  bytes, unknown enums, invalid UTF-16/UTF-8, embedded NULs, and oversized
  content are rejected.
- The broker transport creates exactly one local-only byte-mode named-pipe
  instance derived from the private 128-bit request nonce. Its protected DACL
  admits only the invoking user, Administrators, and LocalSystem; remote
  clients and a second server instance are rejected. The original process
  binds the connected client PID to the exact `ShellExecuteExW("runas")`
  process, then validates the same executable, interactive session, and
  elevated token before sending any request. The child independently binds the
  pipe server to the supplied parent PID, same executable, and session and
  retains the validated original-parent SID/session identity. Bounded
  length-prefixed overlapped I/O observes both timeout and peer-process exit.
  Only the nonce and parent PID cross the internal command line; the mutation
  request, game path, measurements, and payload never do, and the internal
  grammar is separate from the three public launcher switches.
- The original-user application binds the parsed embedded manifest hash into
  the broker provider only after package validation. The elevated child
  authenticates its parent before independently reopening the same embedded
  manifest and CAB, comparing the exact manifest hash from the request, and
  constructing loader material. For installation it reopens the authenticated
  parent process token, verifies the retained SID and session, and resolves
  that user's LocalAppData so alternate UAC credentials cannot redirect
  `override.txt` to the elevated account. Removal does not resolve or access
  original-user runtime data because the restricted executor needs no payload
  bytes for deletion.
- The native `wWinMain` composition selects the nonce/PID-only internal child
  mode before public argument conversion. Normal operation runs as the
  invoking user, holds the launcher mutex, composes the embedded package,
  observer, manifest-bound broker, and Steam effects, and reports bounded
  failures through TaskDialog plus
  `%LOCALAPPDATA%\MecchaCamouflage\v2\logs\launcher.log`. Before `runas`, a
  cancellable TaskDialog names the exact game directory and the planned action
  for only `dwmapi.dll` and `override.txt`.
- Native writability observation walks only absolute normalized plain
  directory components, rejects reparses, opens the nearest existing directory
  with the exact create/delete-child access needed by publication, and treats
  access denied as a planning input. It creates no probe file or directory.
- Runtime reuse now has a strictly read-only transaction check: `active` must
  match the expected manifest and no journal, staging, or rollback generation
  may exist. Runtime removal first completes the existing recovery state
  machine, then removes only the ownership-identified `active` generation; a
  missing cache is an idempotent no-op and conflicting content is untouched.
- After the explicit startup-recovery boundary, runtime observation is also
  strictly read-only and classifies only a clean missing cache, exact current
  `active`, or exact owned previous `active` as actionable. Any remaining
  journal, staging, rollback, partial, or unknown generation is a conflict and
  cannot reach preparation/removal effects.
- The Win32 execution backend binds that transaction contract to the existing
  managed-loader and shared-mod adapters. Elevated managed operations are
  handed only to the broker interface, with cache/mod actions stripped from a
  removal request. Normal managed and shared integration tests operate on real
  temporary Windows trees; Steam launch remains an injected final effect and
  is not invoked by the tests.
- Managed-loader and shared-mod material is requested lazily by the execution
  backend. Managed material therefore cannot be constructed until runtime
  publication/reuse has succeeded and the stable `active` directory can be
  validated. Successful material is cached for the invocation; a failed
  precondition is not cached and remains explicitly retryable.
- Managed proxy/override expectations are independently derivable before the
  runtime exists, so the original-user observer can classify both loader files
  before planning without reading payload bytes or creating runtime/ownership
  directories. Existing path components must be plain directories; the
  generated override uses the direct ASCII path or an equivalent existing
  short-path prefix plus ASCII unpublished suffix. Full material construction
  still revalidates that `active` exists after publication.
- The source-graph gate locks the pinned proxy's exact override contract:
  narrow `std::ifstream`/`std::string` line input is converted to its native
  filesystem path, `UE4SS.dll` is appended, and the resulting wide native path
  is passed to `LoadLibrary`. The launcher therefore emits ASCII only, proves
  that an existing non-ASCII directory resolves through an equivalent ASCII
  short path when available, and otherwise fails closed.
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
  Deterministic MakeCab assembly normalizes an isolated staging snapshot,
  expands the result, and requires exact manifest equivalence before
  publication.
- The Win32 embedded-payload boundary reads exact `RCDATA` bytes from the
  current executable and accepts only a bounded single CAB whose file set,
  canonical paths, declared sizes, and SHA-256 values exactly match the
  already-validated manifest. CAB extraction uses a GUID-named private
  workspace below a caller-approved plain directory, rejects multi-cabinet,
  undeclared, duplicate, non-canonical, oversized, and corrupt input, copies
  verified files into memory, and removes the workspace before returning.
  The Windows GUI target conditionally embeds resource IDs 101 and 102 only
  when both a verified manifest and CAB are configured, refuses a partial
  pair, and uses one explicit `asInvoker` manifest. The final trusted runtime,
  profiles, localization, fonts, icon, licenses, and notices remain open.

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
The portable runtime-transaction suite additionally proves read-only
post-recovery classification for missing, exact current, owned previous, and
non-clean conflict states.

The MSVC Release owned-file suite covers create, no-op reuse, owned update,
idempotent removal, exact-unowned refusal, post-install tampering, payload
hash mismatch, read-only observation, interrupted receipt publication,
post-publish install recovery, post-delete removal recovery, external
create/replace/remove intent finalization, no-target-change preparation, and
aborted external-intent rollback, plus a
privilege-free NTFS junction fixture. The optional file-symbolic-link variant
is skipped on hosts where Windows developer-mode link creation is unavailable;
the junction fixture remains mandatory and passes.

The runtime-storage suite additionally replaces the managed `active`
generation with a privilege-free NTFS junction to an external empty
directory. Preparation classifies it as a conflict and neither follows nor
modifies the external target.

The managed-loader suite covers payload and generated-override material,
read-only prepublication expectations, missing/exact-owned observation without
directory creation, two-file creation, exact reuse, stale-plan preflight,
exact-unowned proxy
coexistence, payload tampering, Unicode-path short-path-or-refusal behavior,
stale removal, ownership-safe removal, and zero-mutation elevated handoff.

The shared-mod suite covers canonical material construction, read-only
missing/exact-owned/exact-unowned/owned-previous/conflict classification,
fresh nested installation, exact-owned and exact-unowned reuse, all-file
conflict preflight, receipt-only unchanged-file updates, changed-file
replacement, hash-safe removal after a runtime-owner update, manifest-removed
file cleanup, new-launcher removal of an old install, ledger tampering,
cross-root receipt rejection, payload tampering, foreign mod-path rejection,
preservation of `Mods/mods.txt`, complete owned-directory cleanup,
pre-mutation refusal of unknown mod or ownership content, and a privilege-free
target-directory junction fixture.
The portable ledger suite additionally proves canonical ordering, strict
round trips, duplicate rejection, per-file ownership retention, and old/new
transition union semantics.
The portable execution suite proves managed/shared isolation, prepare-only,
minimal elevated-loader routing, effect ordering, plan rejection, idempotent
empty removal, and the absence of later mutations or Steam launch after a
failed effect.
The portable workflow suite additionally proves the full observation-policy-
plan-execution order for prepare-and-launch, prepare-only, and managed removal,
including typed fail-closed observation/planning behavior before any effect.
The portable observation-assembly suite proves clean, exact managed, previous
managed, exact shared, mixed-ownership, and invalid-loader mappings for both
preparation and removal.
The Win32 execution suite additionally proves read-only exact runtime reuse,
normal owned-loader publication, elevated loader-only handoff, loader-before-
cache removal, shared-mod installation/removal, and preservation of the shared
UE4SS runtime. It also proves material-provider ordering before normal/elevated
effects and retry-once/cache-after-success behavior across runtime publication.
The Win32 observation-source suite proves read-only clean-managed and
launch-option shared-runtime assembly, exact resolved-root retention,
no ownership-directory creation, one platform read per observation, and a
non-mutating current-user access probe for a not-yet-created cache path. It
also runs the observed shared root through the portable workflow and Win32
backend to a complete prepare-only mod installation without touching the
managed cache, elevated broker, or Steam.
The Win32 composition suite proves clean managed publication of the complete
runtime and two loader files, unchanged no-op reuse with exactly one runtime
generation, ownership-safe managed removal, and zero recovery/payload effects
for a running game or mismatched manifest identity.
The Win32 application suite proves explicit UTF-8 selection, automatic
discovery-to-picker fallback, invoking-user LocalAppData path derivation,
native GUID nonce shape, full prepare-only composition, second-instance
refusal before bootstrap, and running-game refusal before package acquisition
or filesystem mutation.
The privileged loader-executor suite proves two-file-only fresh publication,
exact proxy verification without rewrite, owned replacement, exact removal,
full preflight before either mutation, changed-target refusal, running-game
refusal, malformed-request refusal, and absence of any ownership-directory
effect.
The original-user broker-coordinator suite proves end-to-end apply/remove
receipt coordination with the restricted executor, rollback after a
pre-mutation client failure, deterministic ownership after proxy-only partial
success, refusal of false client success, and zero privileged-client/nonce
calls for exact reuse.
The broker-protocol suite proves Unicode request and typed success/error
round trips, rejection at every truncated request length, strict trailing-data
and unknown-action refusal, embedded-NUL path refusal, response truncation
refusal, and bounded error detail.
The broker-transport suite proves an end-to-end authenticated named-pipe
exchange into the restricted two-file executor, original-parent SID/session
capture, strict internal invocation grammar, canonical `runas` parameters,
private GUID nonce generation, launched/connected PID mismatch refusal,
response-nonce mismatch refusal, and native same-executable/session parent
identity checks without displaying UAC. A successful real elevated-token and
alternate-credential exchange remains a live gate.
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

- Bind the final trusted CAB/manifest resources through the verified payload
  source, freeze the runtime generated-path allowlist, and complete
  alternate-credential/UAC live verification.
- Run the deferred live managed/shared/UAC/launch checks after the game is
  stopped and a complete trusted UE4SS build exists.

No launcher executable is considered releasable at this checkpoint.
