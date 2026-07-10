# Runtime Maintenance

This document defines how to change runtime, bridge, and reverse-engineering
code without growing accidental complexity.

## Default Workflow

1. Start from the source layout in `docs/repository-layout.md`.
2. Classify the change:
   - app/runtime infrastructure
   - bridge injection or IPC
   - WebView2 GUI
   - mesh profile generation
   - paint replication
   - research-only reverse engineering
3. Run the smallest useful static check.
4. Make a focused change.
5. Run `make build`.
6. Run maintainer game smoke tests when the change touches game behavior.
7. Run `make package` before release work.

Do not mix unrelated runtime, GUI, research, and packaging changes in one diff
unless the change is a deliberate architecture migration.

## Bridge Loader

The injected lifecycle entry point is `bridge-loader.dll`. The loader supervises
loading, starting, stopping, status reporting, and conditional unloading of the
versioned bridge DLL.

The loader must remain small. It must not contain UE reflection, paint routing,
texture preview logic, or `ProcessEvent` behavior. Those stay in
`runtime-bridge.dll`.

Loader design rules live in `docs/bridge-loader-design.md`.

## Dead-Code Review

Generate a report-only inventory:

```bash
make review-dead-code
```

Review output is ignored under `artifacts/review/runtime-dead-code/`.

Static search is evidence, not proof. Do not delete code only because an
analyzer or `rg` shows few references.

Deletion requires:

- inventory evidence
- category review
- command/reflection/layout review
- focused diff
- `make build`
- live smoke coverage when the code is near paint, injection, startup, or
  multiplayer behavior

## Keep Categories

### Dynamic Runtime Entries

Keep sparse-looking entry points reached by runtime mechanisms:

- `DllMain`
- Win32 callbacks and message hooks
- bridge listener command handlers
- WebView2/C# command strings
- exported or `GetProcAddress`-reachable functions

### Unreal Reflection And SDK Layout

Keep code that preserves binary or reflection compatibility:

- `ProcessEvent` wrappers
- `FName`, `UObject`, `UFunction`, and `FProperty` helpers
- reflected function and property name strings
- RPC parameter structs
- padding fields
- `static_assert` layout checks

Do not rename, reorder, or delete these without live verification.

### Research-Only Reverse Engineering

Keep research helpers out of normal UI and normal paint decisions, but do not
delete them just because production code does not call them.

Research-only code includes:

- paint replication probes
- pressure probes
- packed replay probes
- event-watch sidecars
- dump and trace helpers
- `MECCHA_RESEARCH_ARTIFACTS` paths

Repeatable research entry points belong under `scripts/research/`. Generated
research output belongs under `artifacts/research/` or another ignored local
directory.

## Paint Replication Rules

Normal paint uses the packed component route. Do not reintroduce automatic
fallback to old compact/adaptive/send-custom routes.

If the packed route cannot be prepared, fail with diagnostic metadata instead
of silently changing routes.

When changing replication behavior, verify host and joining-client behavior
separately. Painter-side completion is not enough; a normal other client must
also receive the final result without returning to the old multi-minute drain
path.

## Bridge File Structure

`src/native/bridge/bridge.cpp` remains a single translation unit unless there
is a focused reason to split further.

Low-risk helpers may move to `.inc` files when that reduces local complexity and
does not change behavior. Full `.cpp/.h` splitting should wait until the moved
section has focused build and live verification.

Existing `.inc` files:

- `src/native/bridge/bridge_json.inc`
- `src/native/bridge/bridge_sidecar.inc`

## Research Tool Policy

Prefer maintained source under `scripts/research/` or `third_party/` over
untracked binary output.

`tools/asset_probe/` is currently ignored local output from an old research
tool. Only local `bin/` and `obj/` output remains. Keep it only when a local
investigation still needs it.
