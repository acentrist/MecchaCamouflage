#include <meccha/runtime/paint_queue_codec.hpp>

#include <cmath>
#include <cstddef>

namespace meccha::runtime
{
namespace
{
auto valid_counter(const std::optional<std::int32_t>& counter)
    -> bool
{
    return !counter || *counter >= 0;
}

auto valid_pressure(
    const std::optional<RuntimePaintReplicationPressureAbi>& pressure)
    -> bool
{
    return !pressure ||
           (pressure->queued_batch_count >= 0 &&
            pressure->queued_stroke_count >= 0 &&
            pressure->max_strokes_per_tick >= 0 &&
            std::isfinite(pressure->estimated_ticks_to_drain) &&
            pressure->estimated_ticks_to_drain >= 0.0F);
}
} // namespace

auto PaintQueueObservationTracker::observe(
    application::RuntimeObjectHandle component,
    application::JobGeneration generation,
    const PaintQueueCounters& counters)
    -> std::expected<
        application::PaintQueueObservation,
        PaintQueueCodecError>
{
    if (!component.valid() || generation == 0U)
    {
        return std::unexpected(
            PaintQueueCodecError::InvalidIdentity);
    }
    if (!counters.recorded_strokes ||
        !counters.component_queued_strokes)
    {
        return std::unexpected(
            PaintQueueCodecError::MissingOwnedObserver);
    }
    if (!valid_counter(counters.recorded_strokes) ||
        !valid_counter(counters.component_queued_strokes) ||
        !valid_counter(counters.manager_queued_strokes))
    {
        return std::unexpected(
            PaintQueueCodecError::InvalidCounter);
    }
    if (!valid_pressure(counters.pressure))
    {
        return std::unexpected(
            PaintQueueCodecError::InvalidPressure);
    }

    if (!component_ || *component_ != component ||
        generation_ != generation)
    {
        component_ = component;
        generation_ = generation;
        visual_observed_activity_ = false;
    }
    visual_observed_activity_ =
        visual_observed_activity_ ||
        *counters.recorded_strokes > 0;

    return application::PaintQueueObservation{
        true,
        visual_observed_activity_,
        static_cast<std::size_t>(*counters.recorded_strokes),
        true,
        static_cast<std::size_t>(
            *counters.component_queued_strokes),
    };
}

auto PaintQueueObservationTracker::reset() noexcept -> void
{
    component_.reset();
    generation_ = 0U;
    visual_observed_activity_ = false;
}
} // namespace meccha::runtime
