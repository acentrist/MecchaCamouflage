# Runtime Paint Replication Validation

This checklist validates the direct `PaintAtUVWithBrush` route. It supersedes
historical payload, receiver-queue, and custom batching experiments.

## Preconditions

- Use a current game build and a fresh direct bridge instance.
- Capture the selected game PID, bridge instance ID, and game module identity.
- Keep the normal scheduler floor at one millisecond.
- Use a small controlled region before a full Fill/Brush run.

## Manual material validation

1. With Auto Detect off, use distinguishable Paint and Fill M/R/E values.
2. Verify the first-stroke metadata contains the requested values and channel 7.
3. Export the selected component before and after. Confirm Metallic, Roughness,
   and Emissive values change together as one material-properties target.
4. Run Preview Paint and Preview Fill separately, then Unpreview on the same
   bridge instance. Preview is not evidence for normal paint replication.

## Auto Detect validation

1. Enable Auto Detect for Paint with manual values that differ from the expected
   dominant material.
2. Record candidates, selection, and first-stroke M/R/E.
3. Confirm Fill still uses manual values.
4. Treat a global `M=0/R=1/E=0` result as valid only when it is the selected
   game pattern, not because the bridge silently overwrote the request.

## Queue and cancellation validation

1. Record `local_strokes_submitted`, `local_strokes_synced`, and
   `native_queue_target_strokes` throughout the job. Also record
   `native_shared_queue_waits` and `native_unobserved_fallback_ticks`.
2. Confirm ETA uses confirmed progress rather than submission count.
3. Confirm terminal completion happens only after the observed queue is idle.
4. Cancel during active paint. The job must stop submitting new strokes and end
   as cancelled after already recorded work drains naturally.
5. Verify the game remains responsive; do not reduce the scheduler below one
   millisecond or write into internal queues to make this faster.
6. If the shared manager reports saturation, confirm new stroke admission pauses
   until pressure falls. If all queue telemetry is unavailable, confirm only one
   stroke is admitted per 16-millisecond fallback tick.

## Multiplayer validation

Run the same controlled paint with the painter as host and as joining client.
For both roles, record painter/receiver completion times and direct queue data.
The result is invalid if the game crashes, the receiver displays a long dotted
frontier while the queue is unbounded, or a terminal result precedes queue
drain.
