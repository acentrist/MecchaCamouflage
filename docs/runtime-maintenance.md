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

The production lifecycle entry point is `BridgeStartV2` in the directly
injected bridge DLL. The injector targets the selected game PID, waits for both
remote calls, and connects only after the start block, injector result,
resident mapping, and authenticated HELLO agree on the exact runtime bundle.
The complete contract is documented in
[`runtime-direct-bridge.md`](runtime-direct-bridge.md).

Never unload a resident DLL. A bundle mismatch uses authenticated shutdown,
requires paint and hook quiescence, leaves the old module inert, and starts a
new immutable generation. If shutdown, identity verification, or generation
limits fail, the controller must fail closed and require a game restart.

## Dead-Code Review

Generate a report-only inventory:

```bash
make review-dead-code
```

Review output is ignored under `artifacts/review/runtime-dead-code/`.

Static search is evidence, not proof. Do not delete code only because an
analyzer or `rg` shows few references.

The inventory intentionally reports dynamic bridge entry points, reflection
names/layouts, and research-only paths. Treat those categories as retained until
their runtime contract is disproved. A release review must record whether the
report contains a concrete, statically reachable deletion candidate; an
inventory containing only these protected categories is not authorization to
remove code.

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
- event-watch sidecars
- dump and trace helpers
- `MECCHA_RESEARCH_ARTIFACTS` paths

Repeatable research entry points belong under `scripts/research/`. Generated
research output belongs under `artifacts/research/` or another ignored local
directory.

## Paint Replication Rules

Normal paint invokes the game-owned `PaintAtUVWithBrush` entry point for every
planned stroke. This is the only production paint route; it lets the game
record and replicate the stroke using its current multiplayer implementation.
The material target remains AMRE: `R=Metallic`, `G=Roughness`, and
`B=Emissive`.

`PaintAtUVWithBrush` and UObject/render-target access remain on the game
thread. Pure planning may use workers, but moving the reflected call itself is
unsafe. The direct scheduler measures each submitted slice; when one
non-preemptible call exceeds the 4 ms CPU budget, it increases the delay before
the next slice so average paint occupancy returns to the budget. Keep
`local_dispatch_last_slice_us`, `local_dispatch_delay_ms`,
`local_dispatch_peak_delay_ms`, and `local_dispatch_throttle_events` in
performance captures.

`ImportChannelFromBytes` is strictly the Preview/Unpreview transport. It must
not become a production paint fallback, and neither a packed RPC nor the old
internal no-resend / receiver-queue routes may be reintroduced.

Auto Detect applies only to Paint regions. It obtains one global dominant
material pattern from `GetDominantPaintMaterialPatterns`, including M/R/E, and
therefore intentionally ignores the manual Paint PBR values for that request.
Fill is always an explicit manual material, even when Auto Detect is on. Record
the returned candidate list, selection, and first-stroke M/R/E before judging
an Auto Detect result.

Color compression is a planner-only optimization for the single Paint pass.
At a nonzero tolerance it may widen and coalesce strokes only within one
region, UV island, safe sample set, and final material payload; Fill is never
compressed. Its supported range is `0` through `10` (default `5`); `0`
disables widening. Nonzero compression uses a brush-aligned coverage field:
empty, skipped, unsafe, conflicting PBR, region, and UV-island cells stop
expansion. A deterministic circle-covering phase reduces overlap on flat
fields, and each emitted stroke uses the per-channel minimax colour of its
coverage instead of whichever sample happened to be visited first. Fixed
appearance-calibration samples do not participate in final replay
compression. New settings default to a 5-texel brush with Front `Skip` and
Side / Back `Paint`.

The reflected `PaintAtUVWithBrush` schema is a fatal requirement. If it is
unavailable, stop before painting; do not silently switch to texture import or
any legacy packed route.

When changing replication behavior, verify host and joining-client behavior
separately. Painter-side completion is not enough; a normal other client must
also receive the final result without returning to the old multi-minute drain
path.

## Debugging Game Updates And PBR

Build a short numeric feedback loop before changing the production route.

1. Reproduce with one region, one stroke, and a fresh research artifact
   directory. Run only one research runner while its event-watch bridge is
   active.
2. Use distinguishable manual PBR values such as `M=.21`, `R=.83`, `E=.47`.
   The material-properties texture result should be approximately `R=54`, `G=212`,
   `B=120`; quantization is expected.
3. Run the same controlled probe with Auto Detect on. Compare
   `material_properties_candidates`, `material_properties_selection`, and the
   first-stroke values. Auto Detect succeeding at a global `M=0/R=1/E=0` is
   not evidence that the manual values were lost.
4. For Preview, test Paint and Fill independently. Front defaults to Fill, so
   changing Paint PBR does not change a Front Fill preview. Preview must be
   restored on the same bridge that captured its snapshot.
5. Treat a successful server RPC, a changed hash, an eyedropper reading, or a
   screenshot as incomplete evidence on its own. Record changed-pixel values
   and the selected component, then separately verify renderer/remote-client
   completion where that claim matters.

Never lower the recurring scheduler below its 1 ms safety floor or restore
zero-delay reposts to make a benchmark look faster; this can monopolize the
game thread and freeze the game. Do not reuse old RVAs, mutate queues or
render-target memory, use `TerminateThread`, unload a bridge, or restart the
game merely because a new controller starts. The direct bridge is designed to
authenticate a fresh controller-owned instance in an already-running game.

### Game-update revalidation

After a game update, record the PE and `.text` identity for diagnosis, then
verify the reflected `PaintAtUVWithBrush`, `FPaintChannelData`, and
`FPaintStroke` layouts, including Emissive. Never copy old RVAs or queued-paint
memory offsets forward as acceptance criteria. A missing or changed direct
paint schema must fail explicitly; do not substitute a second transport. Then
repeat both multiplayer directions, recording native queue depth, submitted
and completed strokes, and painter/receiver texture evidence.

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
