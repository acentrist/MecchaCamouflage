#pragma once

#include <meccha/application/paint_dispatch.hpp>
#include <meccha/application/paint_planning_request.hpp>
#include <meccha/application/runtime_operation_executor.hpp>

#include <expected>
#include <optional>

namespace meccha::application
{
struct CapturedPaintJob
{
    RuntimeObjectHandle component{};
    PaintPlanningRequest planning{};
    core::ReplicationPacingPlan pacing{};
};

class PaintQueueRuntimePort
{
public:
    PaintQueueRuntimePort() = default;
    PaintQueueRuntimePort(const PaintQueueRuntimePort&) = delete;
    auto operator=(const PaintQueueRuntimePort&)
        -> PaintQueueRuntimePort& = delete;
    virtual ~PaintQueueRuntimePort() = default;

    virtual auto observe_paint_queues(
        RuntimeObjectHandle component,
        JobGeneration generation)
        -> std::expected<
            PaintQueueObservation,
            RuntimeExecutionError> = 0;
};

class PaintGameRuntimePort
{
public:
    PaintGameRuntimePort() = default;
    PaintGameRuntimePort(const PaintGameRuntimePort&) = delete;
    auto operator=(const PaintGameRuntimePort&)
        -> PaintGameRuntimePort& = delete;
    PaintGameRuntimePort(PaintGameRuntimePort&&) = default;
    auto operator=(PaintGameRuntimePort&&)
        -> PaintGameRuntimePort& = default;
    virtual ~PaintGameRuntimePort() = default;

    // Projective Paint is a bounded multi-HUD-frame transaction. A successful
    // begin owns any preview snapshot until advance publishes a completed job
    // after exact restoration, or cancel reports true after exact restoration.
    // Pending advance results and false cancellation results retain ownership.
    virtual auto begin_automatic_capture(
        const core::PaintSettings& settings,
        JobGeneration generation)
        -> std::expected<
            void,
            RuntimeExecutionError> = 0;

    virtual auto advance_automatic_capture(
        JobGeneration generation)
        -> std::expected<
            std::optional<CapturedPaintJob>,
            RuntimeExecutionError> = 0;

    virtual auto cancel_automatic_capture(
        JobGeneration generation)
        -> std::expected<
            bool,
            RuntimeExecutionError> = 0;

    virtual auto observe_queues(
        RuntimeObjectHandle component,
        JobGeneration generation)
        -> std::expected<
            PaintQueueObservation,
            RuntimeExecutionError> = 0;
};
} // namespace meccha::application
