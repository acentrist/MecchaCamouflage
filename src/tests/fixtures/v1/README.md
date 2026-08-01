# v1 Behavioral Fixtures

These fixtures freeze deterministic, user-visible v1 behavior for the v2
rewrite. They contain semantic inputs and expected outputs only. They do not
freeze v1 object layouts, global state, source-text shape, TCP messages,
renderer hooks, or implementation-specific helper calls.

The fixtures are seeds for the Phase 6 C++ golden tests. Property and boundary
tests must extend them; passing only these examples is not sufficient.

- `paint-domain.json`: settings, pass ordering, preview ownership, and adaptive
  compression behavior.
- `image-mapping.json`: canonical four-tile atlas mapping for round and cube.
- `runtime-pacing.json`: conservative dispatch and terminal queue-drain
  behavior.
- `esp-domain.json`: scope, spectator, avatar, bounds, and projection behavior.

`manifest.json` records the current maintainer-requested latest-v1 tracking
checkpoint, v1.7.2. This checkpoint replaces the earlier v1.7.1
characterization as the v2 behavior and resource target; older v1 releases are
not concurrent compatibility targets. The manifest records the packaged
profile-generating commit separately because the Round raw and image-reference
profiles were regenerated for game 3.3.0 earlier on the v1.7.1 line and are
unchanged in v1.7.2. Profile
hashes use the repository-canonical LF form declared by `.gitattributes`, so
the inventory is independent of the checkout host's line-ending mode. The raw
and derived profile JSON files remain the authoritative large geometry inputs
and are not duplicated here.
