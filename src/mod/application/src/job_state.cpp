#include <meccha/application/job_state.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace meccha::application
{
namespace
{
auto is_active(JobPhase phase) -> bool
{
    return phase == JobPhase::Planning ||
           phase == JobPhase::Dispatching ||
           phase == JobPhase::Cancelling ||
           phase == JobPhase::Draining;
}
} // namespace

auto JobStateMachine::start(Feature feature, CommandId command_id)
    -> JobMutationResult
{
    if (is_active(snapshot_.phase))
    {
        return JobMutationResult::Busy;
    }
    if (snapshot_.generation ==
        std::numeric_limits<JobGeneration>::max())
    {
        return JobMutationResult::InvalidState;
    }
    const auto revision = snapshot_.revision;
    const auto generation = snapshot_.generation + 1U;
    snapshot_ = JobSnapshot{
        revision,
        JobPhase::Planning,
        feature,
        command_id,
        generation,
        {},
    };
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::planning_ready(
    JobGeneration generation,
    std::size_t fill_count,
    std::size_t paint_count) -> JobMutationResult
{
    if (!generation_matches(generation))
    {
        return JobMutationResult::StaleGeneration;
    }
    if (snapshot_.phase != JobPhase::Planning)
    {
        return JobMutationResult::InvalidState;
    }
    if (paint_count >
        std::numeric_limits<std::size_t>::max() - fill_count)
    {
        return JobMutationResult::InvalidProgress;
    }
    snapshot_.progress.fill_count = fill_count;
    snapshot_.progress.paint_count = paint_count;
    snapshot_.progress.total = fill_count + paint_count;
    snapshot_.phase = JobPhase::Dispatching;
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::dispatch_progress(
    JobGeneration generation,
    std::size_t submitted,
    std::size_t visual_pending,
    std::size_t outgoing_pending,
    double queue_pressure,
    std::uint64_t elapsed_ms,
    std::optional<std::uint64_t> eta_ms)
    -> JobMutationResult
{
    if (!generation_matches(generation))
    {
        return JobMutationResult::StaleGeneration;
    }
    if (snapshot_.phase != JobPhase::Dispatching &&
        snapshot_.phase != JobPhase::Draining &&
        snapshot_.phase != JobPhase::Cancelling)
    {
        return JobMutationResult::InvalidState;
    }
    if (submitted > snapshot_.progress.total ||
        submitted < snapshot_.progress.submitted ||
        elapsed_ms < snapshot_.progress.elapsed_ms ||
        !std::isfinite(queue_pressure) ||
        queue_pressure < 0.0 || queue_pressure > 1.0)
    {
        return JobMutationResult::InvalidProgress;
    }
    snapshot_.progress.submitted = submitted;
    snapshot_.progress.visual_pending = visual_pending;
    snapshot_.progress.outgoing_pending = outgoing_pending;
    snapshot_.progress.queue_pressure = queue_pressure;
    snapshot_.progress.elapsed_ms = elapsed_ms;
    snapshot_.progress.eta_ms = eta_ms;
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::begin_drain(JobGeneration generation)
    -> JobMutationResult
{
    if (!generation_matches(generation))
    {
        return JobMutationResult::StaleGeneration;
    }
    if (snapshot_.phase != JobPhase::Dispatching)
    {
        return JobMutationResult::InvalidState;
    }
    if (snapshot_.progress.submitted != snapshot_.progress.total)
    {
        return JobMutationResult::InvalidProgress;
    }
    snapshot_.phase = JobPhase::Draining;
    snapshot_.progress.eta_ms.reset();
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::complete_if_drained(
    JobGeneration generation,
    bool visual_confirmation_complete) -> JobMutationResult
{
    if (!generation_matches(generation))
    {
        return JobMutationResult::StaleGeneration;
    }
    if (snapshot_.phase != JobPhase::Draining)
    {
        return JobMutationResult::InvalidState;
    }
    if (snapshot_.progress.visual_pending != 0U ||
        snapshot_.progress.outgoing_pending != 0U ||
        !visual_confirmation_complete)
    {
        return JobMutationResult::PendingQueueDrain;
    }
    snapshot_.phase = JobPhase::Completed;
    snapshot_.progress.queue_pressure = 0.0;
    snapshot_.progress.eta_ms = 0U;
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::request_cancel(JobGeneration generation)
    -> JobMutationResult
{
    if (!generation_matches(generation))
    {
        return JobMutationResult::StaleGeneration;
    }
    if (snapshot_.phase == JobPhase::Cancelling)
    {
        return JobMutationResult::Applied;
    }
    if (snapshot_.phase != JobPhase::Planning &&
        snapshot_.phase != JobPhase::Dispatching &&
        snapshot_.phase != JobPhase::Draining)
    {
        return JobMutationResult::InvalidState;
    }
    snapshot_.phase = JobPhase::Cancelling;
    snapshot_.progress.eta_ms.reset();
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::acknowledge_cancel(
    JobGeneration generation,
    bool native_admission_active,
    std::size_t visual_pending,
    std::size_t outgoing_pending) -> JobMutationResult
{
    if (!generation_matches(generation))
    {
        return JobMutationResult::StaleGeneration;
    }
    if (snapshot_.phase != JobPhase::Cancelling)
    {
        return JobMutationResult::InvalidState;
    }
    snapshot_.progress.visual_pending = visual_pending;
    snapshot_.progress.outgoing_pending = outgoing_pending;
    if (native_admission_active)
    {
        return JobMutationResult::PendingAdmission;
    }
    if (visual_pending != 0U || outgoing_pending != 0U)
    {
        return JobMutationResult::PendingQueueDrain;
    }
    snapshot_.phase = JobPhase::Cancelled;
    snapshot_.progress.queue_pressure = 0.0;
    snapshot_.progress.eta_ms.reset();
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::fail(JobGeneration generation)
    -> JobMutationResult
{
    if (!generation_matches(generation))
    {
        return JobMutationResult::StaleGeneration;
    }
    if (!is_active(snapshot_.phase))
    {
        return JobMutationResult::InvalidState;
    }
    snapshot_.phase = JobPhase::Failed;
    snapshot_.progress.eta_ms.reset();
    mutate();
    return JobMutationResult::Applied;
}

auto JobStateMachine::snapshot() const -> JobSnapshot
{
    return snapshot_;
}

auto JobStateMachine::generation_matches(
    JobGeneration generation) const -> bool
{
    return generation != 0U && generation == snapshot_.generation;
}

auto JobStateMachine::mutate() -> void
{
    if (snapshot_.revision !=
        std::numeric_limits<std::uint64_t>::max())
    {
        ++snapshot_.revision;
    }
}

auto PreviewStateMachine::acquire(
    Feature feature,
    std::uint64_t component_identity)
    -> PreviewAcquireResult
{
    if (component_identity == 0U)
    {
        return PreviewAcquireResult::InvalidComponent;
    }
    if (snapshot_.feature &&
        *snapshot_.feature == feature &&
        snapshot_.component_identity == component_identity)
    {
        return PreviewAcquireResult::Reused;
    }

    const auto replacing = snapshot_.feature.has_value();
    snapshot_.feature = feature;
    snapshot_.component_identity = component_identity;
    if (snapshot_.lease_generation !=
        std::numeric_limits<std::uint64_t>::max())
    {
        ++snapshot_.lease_generation;
    }
    if (snapshot_.revision !=
        std::numeric_limits<std::uint64_t>::max())
    {
        ++snapshot_.revision;
    }
    return replacing
               ? PreviewAcquireResult::Replaced
               : PreviewAcquireResult::Created;
}

auto PreviewStateMachine::restore(std::uint64_t component_identity)
    -> PreviewRestoreResult
{
    if (!snapshot_.feature)
    {
        return PreviewRestoreResult::NoLease;
    }
    if (component_identity == 0U ||
        snapshot_.component_identity != component_identity)
    {
        return PreviewRestoreResult::WrongComponent;
    }
    snapshot_.feature.reset();
    snapshot_.component_identity = 0U;
    if (snapshot_.revision !=
        std::numeric_limits<std::uint64_t>::max())
    {
        ++snapshot_.revision;
    }
    return PreviewRestoreResult::Restored;
}

auto PreviewStateMachine::invalidate_component(
    std::uint64_t component_identity) -> bool
{
    if (!snapshot_.feature ||
        component_identity == 0U ||
        snapshot_.component_identity != component_identity)
    {
        return false;
    }
    snapshot_.feature.reset();
    snapshot_.component_identity = 0U;
    if (snapshot_.revision !=
        std::numeric_limits<std::uint64_t>::max())
    {
        ++snapshot_.revision;
    }
    return true;
}

auto PreviewStateMachine::snapshot() const -> PreviewLeaseSnapshot
{
    return snapshot_;
}
} // namespace meccha::application
