#include <meccha/application/image_paint_job_coordinator.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::application
{
namespace
{
auto active(JobPhase phase) -> bool
{
    return phase == JobPhase::Planning ||
           phase == JobPhase::Dispatching ||
           phase == JobPhase::Cancelling ||
           phase == JobPhase::Draining;
}
} // namespace

ImagePaintJobCoordinator::ImagePaintJobCoordinator(
    JobStateMachine& jobs,
    ImagePaintPlanningWorker& planner,
    PaintDispatchController& dispatcher)
    : jobs_{jobs},
      planner_{planner},
      dispatcher_{dispatcher}
{
}

auto ImagePaintJobCoordinator::start(
    CommandId command_id,
    std::string project_id,
    std::uint64_t project_revision,
    RuntimeObjectHandle component,
    core::ImagePaintProfilePlanRequest request,
    core::ReplicationPacingPlan pacing,
    std::uint64_t start_ms)
    -> std::expected<
        JobGeneration,
        ImagePaintJobCoordinatorError>
{
    if (command_id == 0U)
    {
        return std::unexpected(
            ImagePaintJobCoordinatorError::InvalidCommand);
    }
    if (!core::valid_image_project_id(project_id) ||
        project_revision == 0U)
    {
        return std::unexpected(
            ImagePaintJobCoordinatorError::InvalidProject);
    }
    if (!component.valid())
    {
        return std::unexpected(
            ImagePaintJobCoordinatorError::InvalidComponent);
    }
    if (stage_ != Stage::Idle)
    {
        return std::unexpected(
            ImagePaintJobCoordinatorError::Busy);
    }

    const auto started =
        jobs_.start(Feature::ImagePaint, command_id);
    if (started != JobMutationResult::Applied)
    {
        return std::unexpected(
            started == JobMutationResult::Busy
                ? ImagePaintJobCoordinatorError::Busy
                : ImagePaintJobCoordinatorError::InvalidState);
    }

    generation_ = jobs_.snapshot().generation;
    project_id_ = std::move(project_id);
    project_revision_ = project_revision;
    component_ = component;
    pacing_ = pacing;
    start_ms_ = start_ms;
    stale_project_ = false;
    const auto planning = planner_.start(
        generation_,
        ImagePaintPlanningRequest{
            project_id_,
            project_revision_,
            std::move(request),
        });
    if (!planning)
    {
        static_cast<void>(jobs_.fail(generation_));
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::PlanningStart);
    }
    stage_ = Stage::Planning;
    return generation_;
}

auto ImagePaintJobCoordinator::tick(
    std::uint64_t now_ms,
    const PaintQueueObservation& observation,
    std::string_view current_project_id,
    std::uint64_t current_project_revision)
    -> std::expected<
        JobSnapshot,
        ImagePaintJobCoordinatorError>
{
    if (stage_ == Stage::Idle)
    {
        return std::unexpected(
            ImagePaintJobCoordinatorError::InvalidState);
    }
    if (!project_matches(
            current_project_id,
            current_project_revision))
    {
        const auto stale =
            mark_stale_project(now_ms, observation);
        if (!stale)
        {
            return std::unexpected(stale.error());
        }
        const auto phase = jobs_.snapshot().phase;
        if (stage_ == Stage::Dispatch &&
            (phase == JobPhase::Completed ||
             phase == JobPhase::Cancelled ||
             phase == JobPhase::Failed))
        {
            clear_active();
            return std::unexpected(
                ImagePaintJobCoordinatorError::StaleProject);
        }
    }
    if (stage_ == Stage::Dispatch)
    {
        return tick_dispatch(now_ms, observation);
    }

    auto completion = planner_.poll();
    if (!completion)
    {
        return jobs_.snapshot();
    }
    return consume_planning(
        std::move(*completion),
        now_ms,
        observation);
}

auto ImagePaintJobCoordinator::request_cancel(
    JobGeneration generation,
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<void, ImagePaintJobCoordinatorError>
{
    if (stage_ == Stage::Idle)
    {
        return std::unexpected(
            ImagePaintJobCoordinatorError::InvalidState);
    }
    if (generation == 0U || generation != generation_ ||
        generation != jobs_.snapshot().generation)
    {
        return std::unexpected(
            ImagePaintJobCoordinatorError::StaleCompletion);
    }

    if (stage_ == Stage::Planning)
    {
        const auto requested = jobs_.request_cancel(generation_);
        if (requested != JobMutationResult::Applied)
        {
            return std::unexpected(
                ImagePaintJobCoordinatorError::InvalidState);
        }
        const auto stopped =
            planner_.request_cancel(generation_);
        if (stopped == ImagePaintPlanningCancelResult::Idle ||
            stopped ==
                ImagePaintPlanningCancelResult::StaleGeneration)
        {
            static_cast<void>(jobs_.fail(generation_));
            clear_active();
            return std::unexpected(
                ImagePaintJobCoordinatorError::PlanningFailure);
        }
        return {};
    }

    const auto cancelled = dispatcher_.request_cancel(
        generation_,
        now_ms,
        observation);
    if (!cancelled)
    {
        if (active(jobs_.snapshot().phase))
        {
            static_cast<void>(jobs_.fail(generation_));
        }
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::DispatchFailure);
    }
    if (jobs_.snapshot().phase == JobPhase::Cancelled)
    {
        clear_active();
    }
    return {};
}

auto ImagePaintJobCoordinator::requires_queue_observation()
    const noexcept -> bool
{
    return stage_ == Stage::Dispatch;
}

auto ImagePaintJobCoordinator::project_matches(
    std::string_view project_id,
    std::uint64_t project_revision) const -> bool
{
    return project_id == project_id_ &&
           project_revision == project_revision_;
}

auto ImagePaintJobCoordinator::mark_stale_project(
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<void, ImagePaintJobCoordinatorError>
{
    if (stale_project_)
    {
        return {};
    }
    stale_project_ = true;
    if (stage_ == Stage::Planning)
    {
        const auto requested = jobs_.request_cancel(generation_);
        const auto stopped =
            planner_.request_cancel(generation_);
        if (requested != JobMutationResult::Applied ||
            stopped == ImagePaintPlanningCancelResult::Idle ||
            stopped ==
                ImagePaintPlanningCancelResult::StaleGeneration)
        {
            static_cast<void>(jobs_.fail(generation_));
            clear_active();
            return std::unexpected(
                ImagePaintJobCoordinatorError::PlanningFailure);
        }
        return {};
    }
    const auto cancelled = dispatcher_.request_cancel(
        generation_,
        now_ms,
        observation);
    if (!cancelled)
    {
        if (active(jobs_.snapshot().phase))
        {
            static_cast<void>(jobs_.fail(generation_));
        }
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::DispatchFailure);
    }
    return {};
}

auto ImagePaintJobCoordinator::consume_planning(
    ImagePaintPlanningCompletion completion,
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<
        JobSnapshot,
        ImagePaintJobCoordinatorError>
{
    const auto snapshot = jobs_.snapshot();
    if (completion.generation == 0U ||
        completion.generation != generation_ ||
        completion.generation != snapshot.generation ||
        completion.project_id != project_id_ ||
        completion.project_revision != project_revision_)
    {
        if (active(snapshot.phase))
        {
            static_cast<void>(jobs_.fail(generation_));
        }
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::StaleCompletion);
    }

    if (snapshot.phase == JobPhase::Cancelling)
    {
        const auto cancelled = jobs_.acknowledge_cancel(
            generation_,
            false,
            0U,
            0U);
        const auto stale = stale_project_;
        if (cancelled != JobMutationResult::Applied)
        {
            static_cast<void>(jobs_.fail(generation_));
            clear_active();
            return std::unexpected(
                ImagePaintJobCoordinatorError::PlanningFailure);
        }
        const auto result = jobs_.snapshot();
        clear_active();
        if (stale)
        {
            return std::unexpected(
                ImagePaintJobCoordinatorError::StaleProject);
        }
        return result;
    }
    if (snapshot.phase != JobPhase::Planning)
    {
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::StaleCompletion);
    }
    if (!completion.result)
    {
        static_cast<void>(jobs_.fail(generation_));
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::PlanningFailure);
    }

    auto image_plan = std::move(*completion.result);
    auto paint_plan = std::shared_ptr<const core::PaintPlan>{
        image_plan,
        &image_plan->paint,
    };
    const auto began = dispatcher_.begin(
        generation_,
        component_,
        std::move(paint_plan),
        pacing_,
        start_ms_);
    if (!began)
    {
        static_cast<void>(jobs_.fail(generation_));
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::DispatchFailure);
    }
    stage_ = Stage::Dispatch;
    return tick_dispatch(now_ms, observation);
}

auto ImagePaintJobCoordinator::tick_dispatch(
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<
        JobSnapshot,
        ImagePaintJobCoordinatorError>
{
    const auto ticked =
        dispatcher_.tick(generation_, now_ms, observation);
    if (!ticked)
    {
        if (active(jobs_.snapshot().phase))
        {
            static_cast<void>(jobs_.fail(generation_));
        }
        clear_active();
        return std::unexpected(
            ImagePaintJobCoordinatorError::DispatchFailure);
    }

    const auto result = jobs_.snapshot();
    if (result.phase == JobPhase::Completed ||
        result.phase == JobPhase::Cancelled ||
        result.phase == JobPhase::Failed)
    {
        const auto stale = stale_project_;
        clear_active();
        if (stale)
        {
            return std::unexpected(
                ImagePaintJobCoordinatorError::StaleProject);
        }
    }
    return result;
}

auto ImagePaintJobCoordinator::clear_active() -> void
{
    stage_ = Stage::Idle;
    generation_ = 0U;
    project_id_.clear();
    project_revision_ = 0U;
    component_ = {};
    pacing_ = {};
    start_ms_ = 0U;
    stale_project_ = false;
}
} // namespace meccha::application
