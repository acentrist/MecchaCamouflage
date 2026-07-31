#pragma once

#include <meccha/application/image_paint_planning_worker.hpp>
#include <meccha/application/paint_dispatch.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace meccha::application
{
enum class ImagePaintJobCoordinatorError : std::uint8_t
{
    InvalidCommand,
    InvalidProject,
    InvalidComponent,
    Busy,
    PlanningStart,
    PlanningFailure,
    StaleCompletion,
    StaleProject,
    InvalidState,
    DispatchFailure,
};

class ImagePaintJobCoordinator
{
public:
    ImagePaintJobCoordinator(
        JobStateMachine& jobs,
        ImagePaintPlanningWorker& planner,
        PaintDispatchController& dispatcher);

    [[nodiscard]] auto start(
        CommandId command_id,
        std::string project_id,
        std::uint64_t project_revision,
        RuntimeObjectHandle component,
        core::ImagePaintProfilePlanRequest request,
        core::ReplicationPacingPlan pacing,
        std::uint64_t start_ms)
        -> std::expected<
            JobGeneration,
            ImagePaintJobCoordinatorError>;

    [[nodiscard]] auto tick(
        std::uint64_t now_ms,
        const PaintQueueObservation& observation,
        std::string_view current_project_id,
        std::uint64_t current_project_revision)
        -> std::expected<
            JobSnapshot,
            ImagePaintJobCoordinatorError>;

    [[nodiscard]] auto request_cancel(
        JobGeneration generation,
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<void, ImagePaintJobCoordinatorError>;

    [[nodiscard]] auto requires_queue_observation() const noexcept
        -> bool;

private:
    enum class Stage : std::uint8_t
    {
        Idle,
        Planning,
        Dispatch,
    };

    [[nodiscard]] auto project_matches(
        std::string_view project_id,
        std::uint64_t project_revision) const -> bool;
    [[nodiscard]] auto mark_stale_project(
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<void, ImagePaintJobCoordinatorError>;
    [[nodiscard]] auto consume_planning(
        ImagePaintPlanningCompletion completion,
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<
            JobSnapshot,
            ImagePaintJobCoordinatorError>;
    [[nodiscard]] auto tick_dispatch(
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<
            JobSnapshot,
            ImagePaintJobCoordinatorError>;
    auto clear_active() -> void;

    JobStateMachine& jobs_;
    ImagePaintPlanningWorker& planner_;
    PaintDispatchController& dispatcher_;
    Stage stage_{Stage::Idle};
    JobGeneration generation_{};
    std::string project_id_{};
    std::uint64_t project_revision_{};
    RuntimeObjectHandle component_{};
    core::ReplicationPacingPlan pacing_{};
    std::uint64_t start_ms_{};
    bool stale_project_{};
};
} // namespace meccha::application
