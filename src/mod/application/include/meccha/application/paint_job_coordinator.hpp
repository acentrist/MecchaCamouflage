#pragma once

#include <meccha/application/paint_dispatch.hpp>
#include <meccha/application/paint_planning_worker.hpp>

#include <cstdint>
#include <expected>

namespace meccha::application
{
enum class PaintJobCoordinatorError : std::uint8_t
{
    InvalidCommand,
    InvalidComponent,
    Busy,
    PlanningStart,
    PlanningFailure,
    StaleCompletion,
    InvalidState,
    DispatchFailure,
};

class PaintJobCoordinator
{
public:
    PaintJobCoordinator(
        JobStateMachine& jobs,
        PaintPlanningWorker& planner,
        PaintDispatchController& dispatcher);

    [[nodiscard]] auto start(
        CommandId command_id,
        RuntimeObjectHandle component,
        PaintPlanningRequest request,
        core::ReplicationPacingPlan pacing,
        std::uint64_t start_ms)
        -> std::expected<
            JobGeneration,
            PaintJobCoordinatorError>;

    [[nodiscard]] auto tick(
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<
            JobSnapshot,
            PaintJobCoordinatorError>;

    [[nodiscard]] auto request_cancel(
        JobGeneration generation,
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<void, PaintJobCoordinatorError>;

    [[nodiscard]] auto requires_queue_observation() const noexcept
        -> bool;

private:
    enum class Stage : std::uint8_t
    {
        Idle,
        Planning,
        Dispatch,
    };

    [[nodiscard]] auto consume_planning(
        PaintPlanningCompletion completion,
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<
            JobSnapshot,
            PaintJobCoordinatorError>;

    [[nodiscard]] auto tick_dispatch(
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<
            JobSnapshot,
            PaintJobCoordinatorError>;

    auto clear_active() -> void;

    JobStateMachine& jobs_;
    PaintPlanningWorker& planner_;
    PaintDispatchController& dispatcher_;
    Stage stage_{Stage::Idle};
    JobGeneration generation_{};
    RuntimeObjectHandle component_{};
    core::ReplicationPacingPlan pacing_{};
    std::uint64_t start_ms_{};
};
} // namespace meccha::application
