# Phase 6 Pure Core Progress

## Current status

The first frozen domain contracts have moved from the v1 mixed runtime header
and C# models into focused, dependency-free C++ modules. Phase 6 remains open:
profile validation, adaptive color compression, application state machines,
properties, sanitizers, and the remainder of the retained algorithms are not
yet complete.

## Implemented modules

### Paint

- Frozen Paint defaults and finite/range validation.
- Independent Paint and Fill material values.
- Fill/Paint/Skip region values.
- Fill-first replay planning, spatial ordering, deduplication, pass boundaries,
  and current-view fallback diagnostics.
- Conservative replication pacing and terminal visual/outgoing queue drain.

### Image Paint

- Exact 1024×512 four-tile atlas constants.
- Normalized body atlas mapping.
- Canonical cube and round/fukuyoka projection primitives.
- Layer transform and normalized crop validation.
- Per-source 12 MiB and per-project 64 MiB limits with checked accumulation.
- Frozen body, placement, face, brush, compression, material, and layer
  defaults.

### ESP

- Scope/role filtering with spectator exclusion.
- Role-roster avatar replacement selection.
- Spectator-safe geometry capabilities.
- Bounds expansion and projection-scale calibration.

## Evidence

`core_contract_test` ports representative cases from:

- `src/tests/fixtures/v1/paint-domain.json`;
- `src/tests/fixtures/v1/runtime-pacing.json`;
- `src/tests/fixtures/v1/image-mapping.json`;
- `src/tests/fixtures/v1/esp-domain.json`.

The focused core library includes no UE4SS, Unreal, Win32, graphics, launcher,
JSON, or UI header. Its test passes with both GCC and MSVC `/W4 /WX`.

## Deliberate non-port

The complete 3,700-line v1 `runtime_contract.hpp` is not copied as a unit.
Each retained algorithm must receive a project-owned type boundary and direct
golden/property evidence. Research helpers, raw UObject flags/layouts, custom
renderer behavior, and unaccepted appearance heuristics remain outside v2
until their retained requirement and production boundary are explicit.
