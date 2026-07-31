#pragma once

#include <meccha/application/paint_dispatch.hpp>
#include <meccha/application/paint_planning_request.hpp>
#include <meccha/application/runtime_operation_executor.hpp>

#include <expected>

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

    virtual auto capture(const core::PaintSettings& settings)
        -> std::expected<
            CapturedPaintJob,
            RuntimeExecutionError> = 0;

    virtual auto observe_queues(
        RuntimeObjectHandle component,
        JobGeneration generation)
        -> std::expected<
            PaintQueueObservation,
            RuntimeExecutionError> = 0;
};
} // namespace meccha::application
