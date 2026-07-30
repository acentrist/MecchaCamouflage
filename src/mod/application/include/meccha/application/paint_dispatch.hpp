#pragma once

#include <meccha/application/game_thread_scheduler.hpp>
#include <meccha/application/job_state.hpp>
#include <meccha/core/paint_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>

namespace meccha::application
{
struct PaintQueueObservation
{
    bool visual_observer_available{};
    bool visual_observed_activity{};
    std::size_t visual_pending{};
    bool outgoing_observer_available{};
    std::size_t outgoing_pending{};

    auto operator==(const PaintQueueObservation&) const
        -> bool = default;
};

enum class PaintDispatchState : std::uint8_t
{
    Dispatching,
    Draining,
    Cancelling,
    Completed,
    Cancelled,
    Failed,
};

struct PaintDispatchTick
{
    PaintDispatchState state{PaintDispatchState::Dispatching};
    std::size_t admitted{};

    auto operator==(const PaintDispatchTick&) const -> bool = default;
};

enum class PaintDispatchError : std::uint8_t
{
    InvalidState,
    StaleGeneration,
    InvalidComponent,
    InvalidPlan,
    InvalidPacing,
    ClockRegression,
    RequestIdOverflow,
    SchedulerClosed,
};

enum class PaintDispatchMutation : std::uint8_t
{
    Applied,
    PendingDrain,
};

class PaintDispatchController
{
public:
    PaintDispatchController(
        PaintDispatchQueue& scheduler,
        JobStateMachine& jobs);

    [[nodiscard]] auto begin(
        JobGeneration generation,
        RuntimeObjectHandle component,
        std::shared_ptr<const core::PaintPlan> plan,
        core::ReplicationPacingPlan pacing,
        std::uint64_t start_ms)
        -> std::expected<void, PaintDispatchError>;

    [[nodiscard]] auto tick(
        JobGeneration generation,
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<PaintDispatchTick, PaintDispatchError>;

    [[nodiscard]] auto request_cancel(
        JobGeneration generation,
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<
            PaintDispatchMutation,
            PaintDispatchError>;

private:
    [[nodiscard]] auto validate_active(
        JobGeneration generation) const
        -> std::expected<void, PaintDispatchError>;
    [[nodiscard]] auto publish_progress(
        std::uint64_t now_ms,
        const PaintQueueObservation& observation)
        -> std::expected<void, PaintDispatchError>;
    [[nodiscard]] auto effective_visual_pending(
        const PaintQueueObservation& observation) const
        -> std::size_t;
    [[nodiscard]] auto effective_outgoing_pending(
        const PaintQueueObservation& observation) const
        -> std::size_t;
    [[nodiscard]] auto state() const -> PaintDispatchState;

    PaintDispatchQueue& scheduler_;
    JobStateMachine& jobs_;
    JobGeneration generation_{};
    RuntimeObjectHandle component_{};
    std::shared_ptr<const core::PaintPlan> plan_{};
    core::ReplicationPacingPlan pacing_{};
    std::size_t cursor_{};
    std::uint64_t start_ms_{};
    std::uint64_t next_admission_ms_{};
    std::uint64_t all_admitted_ms_{};
    std::optional<std::uint64_t> cancel_requested_ms_{};
    std::uint64_t next_request_id_{1U};
};
} // namespace meccha::application
