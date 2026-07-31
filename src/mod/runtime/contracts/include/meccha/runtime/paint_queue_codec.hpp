#pragma once

#include <meccha/application/paint_dispatch.hpp>

#include <cstdint>
#include <expected>
#include <optional>

namespace meccha::runtime
{
struct RuntimePaintReplicationPressureAbi
{
    std::int32_t queued_batch_count{};
    std::int32_t queued_stroke_count{};
    std::int32_t max_strokes_per_tick{};
    float estimated_ticks_to_drain{};
};
static_assert(sizeof(RuntimePaintReplicationPressureAbi) == 0x10U);

struct RecordedStrokeCountParamsAbi
{
    std::int32_t return_value{};
};
static_assert(sizeof(RecordedStrokeCountParamsAbi) == 0x04U);

struct QueuedStrokeCountParamsAbi
{
    std::int32_t return_value{};
};
static_assert(sizeof(QueuedStrokeCountParamsAbi) == 0x04U);

struct QueuedStrokeCountForComponentParamsAbi
{
    void* paint_component{};
    std::int32_t return_value{};
    std::uint32_t padding{};
};
static_assert(sizeof(QueuedStrokeCountForComponentParamsAbi) == 0x10U);

struct ReplicationPressureParamsAbi
{
    RuntimePaintReplicationPressureAbi return_value{};
};
static_assert(sizeof(ReplicationPressureParamsAbi) == 0x10U);

struct PaintQueueCounters
{
    std::optional<std::int32_t> recorded_strokes{};
    std::optional<std::int32_t> component_queued_strokes{};
    std::optional<std::int32_t> manager_queued_strokes{};
    std::optional<RuntimePaintReplicationPressureAbi> pressure{};
};

enum class PaintQueueCodecError : std::uint8_t
{
    InvalidIdentity,
    MissingOwnedObserver,
    InvalidCounter,
    InvalidPressure,
};

class PaintQueueObservationTracker
{
public:
    [[nodiscard]] auto observe(
        application::RuntimeObjectHandle component,
        application::JobGeneration generation,
        const PaintQueueCounters& counters)
        -> std::expected<
            application::PaintQueueObservation,
            PaintQueueCodecError>;

    auto reset() noexcept -> void;

private:
    std::optional<application::RuntimeObjectHandle> component_{};
    application::JobGeneration generation_{};
    bool visual_observed_activity_{};
};
} // namespace meccha::runtime
