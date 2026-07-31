# Decision 0003: Isolated v2-Only Persistence

Status: Accepted

## Context

The v1 settings, active image state, and `.mcpreset` format contain assumptions
owned by the removed C#/WebView/bridge architecture. Silent migration would
increase the trusted parser surface and make failure recovery ambiguous.

## Decision

Start config and project/preset schemas at version 1 under the isolated v2 data
root. Never scan, read, rewrite, or migrate a v1 data path. Preserve the
`.mcpreset` extension as a product capability, but require the exact v2
container magic/schema and return a specific non-destructive legacy result for
v1 containers.

Config stores only validated product settings and the small optional active
project reference. Encoded sources, layers, transforms, atlas state, and
project materials belong to the project store and active-draft transaction.
All publications are atomic and preserve the prior valid state on failure.

## Consequences

- Fresh v2 defaults are deterministic and independent of v1 installation
  history.
- v1 preservation occurs through the release tag/maintenance branch, not a v2
  compatibility reader.
- Save/load/rename/delete remain required product behavior and live evidence.

