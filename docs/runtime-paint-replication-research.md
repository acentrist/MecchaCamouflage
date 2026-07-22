# Runtime Paint Replication Research

## Production contract

Production paint has one route: the bridge invokes the game's reflected
`PaintAtUVWithBrush` once for each planned stroke. The game owns its recorded
stroke queue, rendering, and replication. The bridge must not construct or
send a custom multiplayer paint payload.

Preview and Unpreview may use texture export/import to preserve a local material
snapshot. Normal Paint must never use texture import as a fallback.

The current material-properties target is AMRE: Metallic, Roughness, and
Emissive are written atomically through paint channel 7. Explicit Emissive
writes prevent a previous glowing value from persisting in a newly painted
surface.

## Queue telemetry and completion

The visible dotted frontier was caused by the bridge submitting strokes much
faster than the game drained its own recorded-paint queue. Submission success
is therefore not completion.

For every async job, the bridge samples the game-owned recorded/component queue
when available and reports:

- `local_strokes_submitted`
- `local_strokes_synced` (confirmed rendered strokes)
- `native_queue_*` observations, target, peak, and wait count
- `paint_eta_ms` calculated from confirmed progress

Admission also observes the replication manager's global queued-stroke and
pressure counters. Component-scoped counters are still used for owned progress,
but ignoring a saturated shared queue lets a joining client continue feeding
the two-hop network path and can stall the session. When no queue counter is
readable, the bridge falls back to one stroke every 16 milliseconds instead of
assuming an empty queue and dispatching every millisecond.

The direct scheduler preserves a one-millisecond wake-up floor and a bounded
CPU slice. It may hold a small queue window, but must wait for the queue to
drain before reporting terminal completion. Do not bypass this with immediate
reposts, queue-memory writes, or zero-delay loops.

## Multiplayer validation

Validate host-painter and joining-client-painter separately. For each run,
record:

1. `PaintAtUVWithBrush` activity and direct-route metadata.
2. Submitted versus confirmed strokes and native queue depth over time.
3. Painter completion time and joining-client visible completion time.
4. Cancellation behavior: no additional submission after cancel, followed by a
   bounded natural drain of already recorded work.
5. A changed-pixel/material export from the selected painter component and, if
   possible, the joining receiver component.

The test fails if the game crashes, the queue grows without bounded progress,
terminal completion occurs while the queue remains nonzero, or a joining client
remains visibly behind after the queue reaches zero.

## Game-update investigation

Build a small numeric loop before editing the runtime route:

1. Use one region and a small stroke limit.
2. Confirm reflection resolves `PaintAtUVWithBrush`, the paint component, and
   `FPaintChannelData` including Emissive.
3. Use manual sentinel material values and compare exported M/R/E values.
4. Repeat with Auto Detect on; compare the selected dominant material pattern,
   not the manual values.
5. Run the same controlled job in host and joining-client roles.

Never carry old RVAs, payload layouts, queue offsets, or alternate transports
into a new game version. Missing or ambiguous direct reflection is an explicit
failure that needs investigation.
