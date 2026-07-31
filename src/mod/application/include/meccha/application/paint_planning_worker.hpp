#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/application/paint_planning_request.hpp>
#include <meccha/core/paint_plan.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace meccha::application
{
class PaintPlanBuilder
{
public:
    PaintPlanBuilder() = default;
    PaintPlanBuilder(const PaintPlanBuilder&) = delete;
    auto operator=(const PaintPlanBuilder&) -> PaintPlanBuilder& = delete;
    PaintPlanBuilder(PaintPlanBuilder&&) = default;
    auto operator=(PaintPlanBuilder&&) -> PaintPlanBuilder& = default;
    virtual ~PaintPlanBuilder() = default;

    virtual auto build(
        const core::PaintPlanRequest& request,
        std::stop_token cancellation)
        -> std::expected<core::PaintPlan, core::PaintPlanError> = 0;
};

class CorePaintPlanBuilder final : public PaintPlanBuilder
{
public:
    auto build(
        const core::PaintPlanRequest& request,
        std::stop_token cancellation)
        -> std::expected<core::PaintPlan, core::PaintPlanError> override;
};

enum class PaintPlanningFailureKind : std::uint8_t
{
    Capture,
    Planner,
    WorkerException,
};

struct PaintPlanningFailure
{
    PaintPlanningFailureKind kind{PaintPlanningFailureKind::Planner};
    std::optional<core::PaintPlanError> planner_error{};
    std::optional<core::PaintCaptureRequestError> capture_error{};

    auto operator==(const PaintPlanningFailure&) const -> bool = default;
};

struct PaintPlanningCompletion
{
    JobGeneration generation{};
    std::expected<
        std::shared_ptr<const core::PaintPlan>,
        PaintPlanningFailure>
        result;
};

enum class PaintPlanningStartError : std::uint8_t
{
    InvalidGeneration,
    Busy,
    Stopped,
    ThreadStart,
};

enum class PaintPlanningCancelResult : std::uint8_t
{
    Requested,
    AlreadyCompleted,
    Idle,
    StaleGeneration,
};

class PaintPlanningWorker
{
public:
    explicit PaintPlanningWorker(PaintPlanBuilder& builder);
    PaintPlanningWorker(const PaintPlanningWorker&) = delete;
    auto operator=(const PaintPlanningWorker&)
        -> PaintPlanningWorker& = delete;
    ~PaintPlanningWorker();

    [[nodiscard]] auto start(
        JobGeneration generation,
        PaintPlanningRequest request)
        -> std::expected<void, PaintPlanningStartError>;

    [[nodiscard]] auto request_cancel(JobGeneration generation)
        -> PaintPlanningCancelResult;

    [[nodiscard]] auto poll()
        -> std::optional<PaintPlanningCompletion>;

    auto shutdown() noexcept -> void;

private:
    enum class State : std::uint8_t
    {
        Idle,
        Running,
        Completed,
    };

    auto run(
        JobGeneration generation,
        PaintPlanningRequest request,
        std::stop_token cancellation) noexcept -> void;

    PaintPlanBuilder& builder_;
    std::mutex mutex_{};
    std::jthread worker_{};
    std::optional<PaintPlanningCompletion> completion_{};
    JobGeneration active_generation_{};
    State state_{State::Idle};
    bool stopped_{};
};
} // namespace meccha::application
