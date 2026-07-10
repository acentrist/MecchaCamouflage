# Runtime Paint Replication Research

This document tracks the reverse-engineering surface for multiplayer paint
replication. It is separate from normal runtime docs because these entrypoints
are investigative and may be useful even when they are not part of production
paint behavior.

## Current Production Route

Normal paint uses the direct component route:

- `RuntimePaintableComponent.ServerPackedPaintBatch`
- packed batch size fixed by the bridge
- no fallback to old compact/adaptive `SendCustom` path
- failure stops paint with explicit metadata

The production route must remain small and deterministic. Research helpers
should not be wired into normal paint decisions.

## Research Entry Points

The bridge currently exposes these investigation-only command types:

- `paint_replication_probe`
  - resolves the current paint component and replication functions/properties
  - reports reflected schema and queue metadata when available
- `paint_replication_pressure_probe`
  - samples global/component replication pressure and drain-related values
  - used to compare old queue/drain behavior with packed route behavior
- `paint_packed_replay_probe`
  - submits a caller-provided packed payload through the selected packed route
  - dangerous enough that scripts should require an explicit replay opt-in
- event-watch sidecar
  - samples selected `ProcessEvent` calls when enabled by sidecar/config
  - useful for discovering host/client route differences

These commands are classified as `RESEARCH_ONLY` in
`docs/runtime-bridge-map.md`.

## Script Surface

Research scripts live under `scripts/research/` when they talk to an injected
bridge or collect multiplayer/runtime data.

Current commands:

```bash
make research-probe
make research-probe RESEARCH_PROBE_TYPE=paint_replication_pressure_probe
make research-pressure
```

For custom bridge JSON:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/research/bridge-probe.ps1 `
  -Json '{"type":"paint_replication_probe"}'
```

Keep generated output under `artifacts/research/` or a local temp directory.

## Multiplayer Verification Questions

When a route changes, collect these facts separately for host and joining
client:

- who initiated paint: host or joining client
- whether other players see the paint
- painter-side completion time
- other-client visible completion time
- delay between painter completion and other-client completion
- crashes, disconnects, lobby returns, freezes, missing paint, or partial paint
- event-watch counts for old send path, packed component route, and relay route

The important distinction is not just whether the painter finishes quickly. The
other normal client must also receive the result without falling back to the old
2-3 minute replication drain path.

## Cleanup Rules

- Do not delete research helpers only because static analysis shows few
  references.
- Do not expose research probes in normal UI unless they become supported user
  behavior.
- Do not let probe output change production paint decisions.
- Keep `ProcessEvent`, RPC payload layout, and SDK padding changes behind live
  verification.
- Move repeatable one-off experiments into `scripts/research/` before adding
  more bridge command strings.

## Artifacts Not To Commit

- event-watch output
- raw replay payloads
- game archives or cooked assets
- generated mappings
- dumps, traces, injected DLLs, and local diagnostics bundles
