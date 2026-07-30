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

`manifest.json` records the v1 source commit and immutable profile hashes from
which the fixtures were characterized. The raw and derived profile JSON files
remain the authoritative large geometry inputs and are not duplicated here.
