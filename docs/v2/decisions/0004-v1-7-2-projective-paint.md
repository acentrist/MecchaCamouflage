# Decision 0004: v1.7.2 Projective Paint Is the v2 Paint Contract

## Status

Accepted by maintainer direction on 2026-08-01.

## Context

MecchaCamouflage v1.7.2, published from PR #256 at merge commit
`87de3fdcf83e4a39d9bc0f47ed5e45987ec62636`, replaces the selectable Auto
Material path with an always-on hidden-environment projection pipeline for
normal Paint. The previous v2 implementation tracked v1.7.1 and had already
ported the retired Auto Material, cluster-fit, and SPSA transaction.

Until v2 stabilizes, the maintainer-requested latest v1 release is the behavior
source of truth. Compatibility with the retired v1.7.1 appearance path is not a
product requirement.

## Decision

- v1.7.2 is the latest-v1 tracking checkpoint.
- Normal Paint always uses `environment_capture`; Image Paint remains
  `imported_image`.
- Remove Auto Material, scene-lighting, and source-UV tuning settings and their
  old runtime branches.
- Use a fixed four-texel correction lattice, Front/Back anchors, and
  harmonic/one-boundary Side propagation.
- Retain the best validated albedo-only feedback result locally. Do not use a
  global appearance fallback.
- Admit automatic Emissive only with repeatable source separation and a
  calibrated target response. Preserve source chromaticity in bounded Albedo
  and treat manual Emissive as a floor.
- Replace proximity coalescing with brush-aligned coverage compression,
  deterministic circle covering, and minimax representative colors.
- Pace local non-preemptible stroke calls from measured game-thread slice time.
- Require an explicit Edit session before targets or settings/editor state can
  mutate, including while Paint is active.

## Consequences

The v1.7.1 Auto Material/SPSA tests remain historical evidence only and are
removed or rewritten as v1.7.2 projective-Paint contracts. Phase 8 automated
completion is reopened until the replacement core, runtime transaction, UI
guard, compression, pacing, and Windows validation evidence pass. Protected
single-/multi-client visual validation remains a later release blocker.
