#include <meccha/application/paint_dispatch.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>

namespace meccha::application
{
namespace
{
auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto valid_plan(const core::PaintPlan& plan) -> bool
{
    if (plan.texture_dimension == 0U ||
        plan.texture_dimension > 4096U ||
        plan.fill_end != plan.fill_count ||
        plan.fill_end > plan.strokes.size() ||
        plan.paint_count !=
            plan.strokes.size() - plan.fill_end ||
        plan.source_paint_count < plan.paint_count ||
        plan.compressed_paint_count !=
            plan.source_paint_count - plan.paint_count)
    {
        return false;
    }
    for (auto index = std::size_t{};
         index < plan.strokes.size();
         ++index)
    {
        const auto& stroke = plan.strokes[index];
        const auto expected_pass =
            index < plan.fill_end
                ? core::ReplayPass::Fill
                : core::ReplayPass::Paint;
        if (stroke.pass != expected_pass ||
            !unit(stroke.u) || !unit(stroke.v) ||
            !std::isfinite(stroke.radius_texels) ||
            stroke.radius_texels < 1.0 ||
            stroke.radius_texels >
                static_cast<double>(plan.texture_dimension) ||
            !unit(stroke.material.metallic) ||
            !unit(stroke.material.roughness) ||
            !unit(stroke.material.emissive) ||
            (expected_pass == core::ReplayPass::Fill &&
             stroke.radius_texels !=
                 core::PaintFillRadiusTexels))
        {
            return false;
        }
    }
    return true;
}

auto valid_pacing(const core::ReplicationPacingPlan& pacing) -> bool
{
    return pacing.effective_strokes_per_second > 0 &&
           pacing.calls_per_tick > 0 &&
           pacing.calls_per_tick <= 3 &&
           pacing.cadence_ms > 0 &&
           pacing.final_confirmation_ms >= 0;
}

auto mutation_error(JobMutationResult result)
    -> PaintDispatchError
{
    return result == JobMutationResult::StaleGeneration
               ? PaintDispatchError::StaleGeneration
               : PaintDispatchError::InvalidState;
}
} // namespace

PaintDispatchController::PaintDispatchController(
    PaintDispatchQueue& scheduler,
    JobStateMachine& jobs)
    : scheduler_{scheduler},
      jobs_{jobs}
{
}

auto PaintDispatchController::begin(
    JobGeneration generation,
    RuntimeObjectHandle component,
    std::shared_ptr<const core::PaintPlan> plan,
    core::ReplicationPacingPlan pacing,
    std::uint64_t start_ms)
    -> std::expected<void, PaintDispatchError>
{
    const auto snapshot = jobs_.snapshot();
    if (generation == 0U ||
        generation != snapshot.generation)
    {
        return std::unexpected(
            PaintDispatchError::StaleGeneration);
    }
    if (snapshot.phase != JobPhase::Planning)
    {
        return std::unexpected(PaintDispatchError::InvalidState);
    }
    if (!component.valid())
    {
        return std::unexpected(
            PaintDispatchError::InvalidComponent);
    }
    if (!plan || !valid_plan(*plan))
    {
        return std::unexpected(PaintDispatchError::InvalidPlan);
    }
    if (!valid_pacing(pacing))
    {
        return std::unexpected(PaintDispatchError::InvalidPacing);
    }

    const auto ready = jobs_.planning_ready(
        generation,
        plan->fill_count,
        plan->paint_count);
    if (ready != JobMutationResult::Applied)
    {
        return std::unexpected(mutation_error(ready));
    }
    generation_ = generation;
    component_ = component;
    plan_ = std::move(plan);
    pacing_ = pacing;
    cursor_ = 0U;
    start_ms_ = start_ms;
    next_admission_ms_ = start_ms;
    all_admitted_ms_ = 0U;
    cancel_requested_ms_.reset();
    return {};
}

auto PaintDispatchController::tick(
    JobGeneration generation,
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<PaintDispatchTick, PaintDispatchError>
{
    const auto active = validate_active(generation);
    if (!active)
    {
        return std::unexpected(active.error());
    }
    if (now_ms < start_ms_)
    {
        return std::unexpected(
            PaintDispatchError::ClockRegression);
    }

    auto admitted = std::size_t{};
    const auto phase = jobs_.snapshot().phase;
    if (phase == JobPhase::Dispatching &&
        now_ms >= next_admission_ms_)
    {
        const auto budget =
            static_cast<std::size_t>(pacing_.calls_per_tick);
        while (cursor_ < plan_->strokes.size() &&
               admitted < budget)
        {
            if (next_request_id_ ==
                std::numeric_limits<std::uint64_t>::max())
            {
                static_cast<void>(jobs_.fail(generation_));
                return std::unexpected(
                    PaintDispatchError::RequestIdOverflow);
            }
            const auto& stroke = plan_->strokes[cursor_];
            const auto scheduled = scheduler_.schedule(
                PaintAtUvWithBrush{
                    next_request_id_,
                    generation_,
                    component_,
                    stroke.u,
                    stroke.v,
                    stroke.radius_texels,
                    plan_->texture_dimension,
                    stroke.color,
                    stroke.material,
                });
            if (scheduled == ScheduleResult::Full)
            {
                break;
            }
            if (scheduled == ScheduleResult::Closed)
            {
                static_cast<void>(jobs_.fail(generation_));
                return std::unexpected(
                    PaintDispatchError::SchedulerClosed);
            }
            ++next_request_id_;
            ++cursor_;
            ++admitted;
        }
        if (admitted > 0U)
        {
            const auto cadence = static_cast<std::uint64_t>(
                core::local_dispatch_adaptive_delay_ms(
                    pacing_.cadence_ms,
                    scheduler_.last_paint_dispatch_us(generation_)));
            next_admission_ms_ =
                now_ms >
                        std::numeric_limits<std::uint64_t>::max() -
                            cadence
                    ? std::numeric_limits<std::uint64_t>::max()
                    : now_ms + cadence;
        }
    }

    const auto published =
        publish_progress(now_ms, observation);
    if (!published)
    {
        return std::unexpected(published.error());
    }

    if (jobs_.snapshot().phase == JobPhase::Dispatching &&
        cursor_ == plan_->strokes.size())
    {
        all_admitted_ms_ = now_ms;
        const auto draining = jobs_.begin_drain(generation_);
        if (draining != JobMutationResult::Applied)
        {
            return std::unexpected(
                mutation_error(draining));
        }
    }

    if (jobs_.snapshot().phase == JobPhase::Cancelling)
    {
        const auto elapsed_after_cancel =
            cancel_requested_ms_ &&
                    now_ms >= *cancel_requested_ms_
                ? now_ms - *cancel_requested_ms_
                : 0U;
        const auto confirmed =
            scheduler_.queued_paint_generation(generation_) == 0U &&
            effective_visual_pending(observation) == 0U &&
            effective_outgoing_pending(observation) == 0U &&
            elapsed_after_cancel >=
                static_cast<std::uint64_t>(
                    pacing_.final_confirmation_ms);
        if (confirmed)
        {
            const auto cancelled = jobs_.acknowledge_cancel(
                generation_,
                false,
                0U,
                0U);
            if (cancelled != JobMutationResult::Applied)
            {
                return std::unexpected(
                    mutation_error(cancelled));
            }
        }
    }
    else if (jobs_.snapshot().phase == JobPhase::Draining)
    {
        const auto elapsed_after_admission =
            now_ms >= all_admitted_ms_
                ? now_ms - all_admitted_ms_
                : 0U;
        const auto confirmed =
            scheduler_.queued_paint_generation(generation_) == 0U &&
            effective_visual_pending(observation) == 0U &&
            effective_outgoing_pending(observation) == 0U &&
            elapsed_after_admission >=
                static_cast<std::uint64_t>(
                    pacing_.final_confirmation_ms);
        const auto completed =
            jobs_.complete_if_drained(generation_, confirmed);
        if (completed != JobMutationResult::Applied &&
            completed != JobMutationResult::PendingQueueDrain)
        {
            return std::unexpected(
                mutation_error(completed));
        }
    }

    return PaintDispatchTick{state(), admitted};
}

auto PaintDispatchController::request_cancel(
    JobGeneration generation,
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<
        PaintDispatchMutation,
        PaintDispatchError>
{
    const auto active = validate_active(generation);
    if (!active)
    {
        return std::unexpected(active.error());
    }
    if (now_ms < start_ms_)
    {
        return std::unexpected(
            PaintDispatchError::ClockRegression);
    }
    const auto requested = jobs_.request_cancel(generation);
    if (requested != JobMutationResult::Applied)
    {
        return std::unexpected(mutation_error(requested));
    }
    if (!cancel_requested_ms_)
    {
        cancel_requested_ms_ = now_ms;
    }
    static_cast<void>(
        scheduler_.discard_paint_generation(generation));
    const auto immediately_confirmed =
        pacing_.final_confirmation_ms == 0 &&
        effective_visual_pending(observation) == 0U &&
        effective_outgoing_pending(observation) == 0U;
    if (!immediately_confirmed)
    {
        return PaintDispatchMutation::PendingDrain;
    }
    const auto acknowledged =
        jobs_.acknowledge_cancel(generation, false, 0U, 0U);
    if (acknowledged != JobMutationResult::Applied)
    {
        return std::unexpected(mutation_error(acknowledged));
    }
    return PaintDispatchMutation::Applied;
}

auto PaintDispatchController::validate_active(
    JobGeneration generation) const
    -> std::expected<void, PaintDispatchError>
{
    if (generation == 0U ||
        generation != generation_ ||
        generation != jobs_.snapshot().generation)
    {
        return std::unexpected(
            PaintDispatchError::StaleGeneration);
    }
    if (!plan_)
    {
        return std::unexpected(PaintDispatchError::InvalidState);
    }
    return {};
}

auto PaintDispatchController::publish_progress(
    std::uint64_t now_ms,
    const PaintQueueObservation& observation)
    -> std::expected<void, PaintDispatchError>
{
    const auto queue = scheduler_.queue_snapshot();
    const auto pressure =
        queue.capacity == 0U
            ? 0.0
            : static_cast<double>(queue.queued) /
                  static_cast<double>(queue.capacity);
    const auto remaining =
        plan_->strokes.size() - cursor_;
    auto eta = std::optional<std::uint64_t>{};
    if (remaining > 0U)
    {
        const auto rate = static_cast<std::uint64_t>(
            pacing_.effective_strokes_per_second);
        const auto numerator =
            static_cast<std::uint64_t>(remaining) * 1000U;
        eta = (numerator + rate - 1U) / rate;
    }
    const auto result = jobs_.dispatch_progress(
        generation_,
        cursor_,
        effective_visual_pending(observation),
        effective_outgoing_pending(observation),
        std::clamp(pressure, 0.0, 1.0),
        now_ms - start_ms_,
        eta);
    if (result != JobMutationResult::Applied)
    {
        return std::unexpected(mutation_error(result));
    }
    return {};
}

auto PaintDispatchController::effective_visual_pending(
    const PaintQueueObservation& observation) const
    -> std::size_t
{
    return observation.visual_observer_available &&
                   observation.visual_observed_activity
               ? observation.visual_pending
               : 0U;
}

auto PaintDispatchController::effective_outgoing_pending(
    const PaintQueueObservation& observation) const
    -> std::size_t
{
    return observation.outgoing_observer_available
               ? observation.outgoing_pending
               : 0U;
}

auto PaintDispatchController::state() const -> PaintDispatchState
{
    switch (jobs_.snapshot().phase)
    {
    case JobPhase::Dispatching:
        return PaintDispatchState::Dispatching;
    case JobPhase::Draining:
        return PaintDispatchState::Draining;
    case JobPhase::Cancelling:
        return PaintDispatchState::Cancelling;
    case JobPhase::Completed:
        return PaintDispatchState::Completed;
    case JobPhase::Cancelled:
        return PaintDispatchState::Cancelled;
    case JobPhase::Failed:
        return PaintDispatchState::Failed;
    case JobPhase::Idle:
    case JobPhase::Planning:
        return PaintDispatchState::Failed;
    }
    return PaintDispatchState::Failed;
}
} // namespace meccha::application
