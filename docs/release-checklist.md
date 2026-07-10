# Release Checklist

This checklist is version-independent. Keep release decisions in GitHub issues
or release notes; keep this file focused on the repeatable process.

Do not tag a release until maintainer-owned game checks are complete.

## Local Checks

Run these before preparing a tag:

```bash
git status --branch --short
make review-dead-code
make clean
make package
git diff --check
```

`make clean` removes `.build/` only. It does not remove `artifacts/`, because
review and research reports are often useful during investigation.

Use `make clean-artifacts` only when ignored review/research reports should be
discarded. Use `make clean-all` to remove both `.build/` and `artifacts/`.

Confirm:

- release artifact is a single EXE under `.build/package/`
- no loose WebView2 runtime is required
- root directory has no generated `*.dll` or `*.exe`
- source tree has no generated `bin/` or `obj`
- shipped app resources are under `resources/`
- source code is under `src/`

## Runtime Packaging Checks

The packaged EXE must include:

- native bridge loader DLL
- native bridge DLL
- native injector EXE
- WebView2 Fixed Runtime
- Web UI assets
- mesh profile resources
- app icon resources

The app must extract these into LocalAppData and repair missing or corrupt
runtime cache files automatically.

## Maintainer Game Checks

These require MECCHA CHAMELEON.

- Start the app with no game running.
  - GUI initializes.
  - error state is clear and diagnostic.
  - no unhandled .NET dialog appears.
- Start the app with the game in menu or lobby.
  - bridge state is clear.
  - paint in lobby fails as a paint-time pawn/component error, not startup
    failure.
- Start the app in a valid paintable match.
  - preview applies.
  - unpreview restores.
  - repeated unpreview shows a guard warning.
  - cancel with no active paint shows a guard warning.
  - normal paint completes.
  - progress shows packed pacing and queue/drain data.
- Delete the LocalAppData runtime cache and restart.
  - cache rebuilds automatically.
  - WebView2 starts from the fixed runtime cache.
- Restart the controller against the same game process.
  - bridge does not double-inject unnecessarily.
  - loaded-but-not-ready reports a diagnostic code instead of looping.

## Multiplayer Checks

Collect these separately for painter-as-host and painter-as-joining-client:

- whether other players see the paint
- painter-side completion time
- other-client visible completion time
- delay between painter completion and other-client completion
- crashes, disconnects, lobby returns, freezes, missing paint, or partial paint
- event-watch counts if available:
  - `ServerPackedPaintBatch > 0`
  - `SendCustomStrokeBatchToServer == 0`
  - `ServerRelayPackedStrokeBatch == 0`

Do not release if joining-client paint crashes the server, or if other clients
finish closer to the old multi-minute replication drain path than to the new
packed route.
