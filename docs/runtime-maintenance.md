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

## Direct Bridge Startup

The production lifecycle entry point is `BridgeStartV1` in the directly injected
bridge DLL. The injector targets the selected game PID, waits for both remote
calls, and connects only to the authenticated loopback endpoint returned by
that bridge instance. The complete contract is documented in
[`runtime-direct-bridge.md`](runtime-direct-bridge.md).

Do not introduce loader switching, unloading, old-module compatibility checks,
or restart-required states merely because older bridge DLLs remain loaded.
There is no loader or compatibility path to preserve: each attempt stages and
authenticates its own direct bridge instance.

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

Auto Adapt paint uses the packed component route plus the dynamically
validated native packed receiver queue for painter-local submission. Manual
batching explicitly selects the independently scheduled, dynamically validated
internal-common no-resend local route; it must never call reflected
`PaintAtUVWithBrush`, which can re-enter replication. Do not invoke the
reflected multicast UFunction for local application, and do not reintroduce
automatic fallback to per-stroke internal-common, compact/adaptive/send-custom,
or reflected `PaintAtUVWithBrush` from Auto mode. Manual direct scheduling may
immediately repost one additional 4-ms slice, but must then use a deferred
wakeup so the game message pump cannot be held continuously.

The server schema, packed payload, and source ID remain fatal requirements. If
only the local route or exact local queue is unavailable, stop local calls and
continue `ServerPackedPaintBatch` at 20 strokes / 50 ms. Preserve
`local_route_mode`, `fallback_reason`, `fallback_batch_limit`, and
`fallback_pacing_ms` metadata. A readable nonzero queue from a previous job is
still a blocking condition.

When changing replication behavior, verify host and joining-client behavior
separately. Painter-side completion is not enough; a normal other client must
also receive the final result without returning to the old multi-minute drain
path.

### Game-update revalidation

The native packed receiver route is not exact-build gated. After a game update,
record the PE and `.text` identity for diagnosis, then verify the reflected
`MulticastPackedPaintBatch` payload layout and the unique masked-signature chain
from UFunction thunk through vtable implementation, decoder, manager resolver,
enqueue, and coalescer. Never copy old RVAs forward as acceptance criteria. A
missing, changed-ABI, or ambiguous candidate must disable local calls and select
server packed fallback before the first RPC. Then repeat both multiplayer
directions with event-watch, pressure/queue samples, and painter/receiver
texture checksums.

The packed source id is not a reflected property; its offset
(`RuntimePaintableComponentPackedSourceIdOffset` in
`src/native/bridge/bridge.cpp`) must be re-derived from each new SDK dump.
Compare the old and new `URuntimePaintableComponent` layouts and apply the
struct shift to the old offset — the id sits inside the private padding that
follows `TargetMeshComponent`. History: v2.8.0 `0x2A8`; v2.9.0 `0x2E0`
(layout shifted `0x38` when `FPaintTextureOptions` grew and
MaterialProperties/Emissive fields were added). A wrong offset fails closed as
`source_id_zero`/`source_id_read_failed` and stops paint with explicit
metadata.

## Bridge File Structure

`src/native/bridge/bridge.cpp` remains a single translation unit unless there
is a focused reason to split further.

Low-risk helpers may move to `.inc` files when that reduces local complexity and
does not change behavior. Full `.cpp/.h` splitting should wait until the moved
section has focused build and live verification.

Existing `.inc` files:

- `src/native/bridge/bridge_json.inc`
- `src/native/bridge/bridge_sidecar.inc` (progress and research sidecar paths)

## Research Tool Policy

Prefer maintained source under `scripts/research/` or `third_party/` over
untracked binary output.

`tools/asset_probe/` is currently ignored local output from an old research
tool. Only local `bin/` and `obj/` output remains. Keep it only when a local
investigation still needs it.
