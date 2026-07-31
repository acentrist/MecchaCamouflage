# Decision 0001: Canonical UE4SS Cargo Lock Overlay

Status: Accepted

## Context

The pinned UE4SS commit is the reviewed runtime source, but its restricted
recursive graph contains a `patternsleuth_bind/Cargo.lock` state that cannot be
reproduced with the required Cargo `--locked` build. Mutating the accepted
gitlink or allowing an unlocked dependency resolve would break source identity.

## Decision

Keep the accepted UE4SS gitlink and nested checkouts pristine. A trusted build
creates an independent source stage, verifies the pinned upstream identity,
and replaces exactly `deps/first/patternsleuth_bind/Cargo.lock` with the
project-owned canonical overlay declared by
`cmake/ue4ss-source-overlay.json`. The stage manifest binds the upstream,
overlay, and resulting hashes. Every configure/build/evidence operation uses
Cargo `--locked` and verifies stage identity before and after execution.

No second overlay, hard-linked stage, in-place submodule edit, or unlocked
fallback is permitted. Any future UE4SS upgrade requires a dedicated
compatibility decision and evidence run.

## Consequences

- Source provenance remains explicit and reproducible.
- Protected dependency evidence must name the staged root, not the pristine
  gitlink or another checkout.
- The approved Cargo lock is release input and is covered by review and tests.

