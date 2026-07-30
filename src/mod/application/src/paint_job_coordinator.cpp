#include <meccha/application/paint_job_coordinator.hpp>

#include <expected>
#include <memory>
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

PaintJobCoordinator::PaintJobCoordinator(
    JobStateMachine& jobs,
    PaintPlanningWorker& planner,
    PaintDispatchController& dispatcher)
    : jobs_{jobs},
      planner_{planner},
      dispatcher_{dispatcher}
{
}

auto PaintJobCoordinator::start(
    CommandId command_id,
    RuntimeObjectHandle component,
    core::PaintPlanRequest request,
    core::ReplicationPacingPlan pacing,
    std::uint64_t start_ms)
    -> std::expected<JobGeneration, PaintJobCoordinatorError>
{
    if (command_id == 0U)
    {
        return std::unexpected(
            PaintJobCoordinatorError::InvalidCommand);
    }
    if (!component.valid())
    {
        return std::unexpected(
            PaintJobCoordinatorError::InvalidComponent);
    }
    if (stage_ != Stage::Idle)
    {
        return std::unexpected(
            PaintJobCoordinatorError::Busy);
    }

    const auto started = jobs_.start(Feature::Paint, command_id);
    if (started != JobMutationResult::Applied)
    {
        return std::unexpected(
            started == JobMutationResult::Busy
                ? PaintJobCoordinatorError::Busy
                : PaintJobCoordinatorError::InvalidState);
    }

    generation_ = jobs_.snapshot().generation;
    component_ = component;
    pacing_ = pacing;
    start_ms_ = start_ms;
    const auto planning =
        planner_.start(generation_, std::move(request));
    if (!planning)
    {
        static_cast<void>(jobs_.fail(generation_));
        clear_active();
        return std::unexpected(
            PaintJobCoordinatorError::PlanningStart);
    }
    stage_ = Stage::Planning;
    return generation_;
}

auto PaintJobCoordinator::tick(
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<JobSnapshot, PaintJobCoordinatorError>
{
    if (stage_ == Stage::Idle)
    {
        return std::unexpected(
            PaintJobCoordinatorError::InvalidState);
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

auto PaintJobCoordinator::request_cancel(
    JobGeneration generation,
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<void, PaintJobCoordinatorError>
{
    if (stage_ == Stage::Idle)
    {
        return std::unexpected(
            PaintJobCoordinatorError::InvalidState);
    }
    if (generation == 0U || generation != generation_ ||
        generation != jobs_.snapshot().generation)
    {
        return std::unexpected(
            PaintJobCoordinatorError::StaleCompletion);
    }

    if (stage_ == Stage::Planning)
    {
        const auto requested = jobs_.request_cancel(generation_);
        if (requested != JobMutationResult::Applied)
        {
            return std::unexpected(
                PaintJobCoordinatorError::InvalidState);
        }
        const auto stopped =
            planner_.request_cancel(generation_);
        if (stopped == PaintPlanningCancelResult::Idle ||
            stopped ==
                PaintPlanningCancelResult::StaleGeneration)
        {
            static_cast<void>(jobs_.fail(generation_));
            clear_active();
            return std::unexpected(
                PaintJobCoordinatorError::PlanningFailure);
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
            PaintJobCoordinatorError::DispatchFailure);
    }
    if (jobs_.snapshot().phase == JobPhase::Cancelled)
    {
        clear_active();
    }
    return {};
}

auto PaintJobCoordinator::consume_planning(
    PaintPlanningCompletion completion,
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<JobSnapshot, PaintJobCoordinatorError>
{
    const auto snapshot = jobs_.snapshot();
    if (completion.generation == 0U ||
        completion.generation != generation_ ||
        completion.generation != snapshot.generation)
    {
        clear_active();
        return std::unexpected(
            PaintJobCoordinatorError::StaleCompletion);
    }

    if (snapshot.phase == JobPhase::Cancelling)
    {
        const auto cancelled = jobs_.acknowledge_cancel(
            generation_,
            false,
            0U,
            0U);
        if (cancelled != JobMutationResult::Applied)
        {
            static_cast<void>(jobs_.fail(generation_));
            clear_active();
            return std::unexpected(
                PaintJobCoordinatorError::PlanningFailure);
        }
        const auto result = jobs_.snapshot();
        clear_active();
        return result;
    }
    if (snapshot.phase != JobPhase::Planning)
    {
        clear_active();
        return std::unexpected(
            PaintJobCoordinatorError::StaleCompletion);
    }
    if (!completion.result)
    {
        static_cast<void>(jobs_.fail(generation_));
        clear_active();
        return std::unexpected(
            PaintJobCoordinatorError::PlanningFailure);
    }

    const auto began = dispatcher_.begin(
        generation_,
        component_,
        std::move(*completion.result),
        pacing_,
        start_ms_);
    if (!began)
    {
        static_cast<void>(jobs_.fail(generation_));
        clear_active();
        return std::unexpected(
            PaintJobCoordinatorError::DispatchFailure);
    }
    stage_ = Stage::Dispatch;
    return tick_dispatch(now_ms, observation);
}

auto PaintJobCoordinator::tick_dispatch(
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<JobSnapshot, PaintJobCoordinatorError>
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
            PaintJobCoordinatorError::DispatchFailure);
    }

    const auto result = jobs_.snapshot();
    if (result.phase == JobPhase::Completed ||
        result.phase == JobPhase::Cancelled ||
        result.phase == JobPhase::Failed)
    {
        clear_active();
    }
    return result;
}

auto PaintJobCoordinator::clear_active() -> void
{
    stage_ = Stage::Idle;
    generation_ = 0U;
    component_ = {};
    pacing_ = {};
    start_ms_ = 0U;
}
} // namespace meccha::application
