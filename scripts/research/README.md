# Runtime Research Scripts

This directory is for report-only or explicitly invoked runtime investigation
helpers. These scripts are not part of normal app startup, normal paint, or
release packaging.

Use this area for multiplayer replication, bridge IPC, event-watch, and
game-update recovery experiments that need a running injected bridge.

## Current Entrypoints

Run the default replication probe:

```bash
make research-probe
```

Run a specific safe probe:

```bash
make research-probe RESEARCH_PROBE_TYPE=paint_replication_pressure_probe
```

Sample replication pressure repeatedly:

```bash
make research-pressure
```

Send a custom research request directly:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/research/bridge-probe.ps1 `
  -Json '{"type":"paint_replication_probe"}'
```

`paint_packed_replay_probe` can submit a packed replay request and is blocked
unless `-AllowReplay` is passed.

## Policy

- Keep production paint behavior out of `scripts/research/`.
- Keep generated research output under `artifacts/research/` or a local temp
  directory.
- Do not commit game archives, mappings, dumps, event-watch output, or bridge
  replay payloads.
- Prefer adding a documented research script over adding one-off command
  strings to user-facing UI.
