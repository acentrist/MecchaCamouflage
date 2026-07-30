#include <meccha/application/paint_job_coordinator.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <iostream>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <thread>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_job_coordinator: "
                  << message << '\n';
    }
    return condition;
}

auto one_stroke_plan() -> core::PaintPlan
{
    auto plan = core::PaintPlan{};
    plan.strokes.push_back(core::PaintStroke{
        0U,
        core::ReplayPass::Paint,
        core::Region::Side,
        0.25,
        0.75,
        4.0,
        core::Rgb8{10U, 20U, 30U},
        core::Material{0.1, 0.8, 0.0},
        false,
    });
    plan.paint_count = 1U;
    plan.source_paint_count = 1U;
    return plan;
}

class CoordinatedBuilder final : public PaintPlanBuilder
{
public:
    auto build(
        const core::PaintPlanRequest&,
        std::stop_token cancellation)
        -> std::expected<core::PaintPlan, core::PaintPlanError> override
    {
        auto mode = Mode::Success;
        {
            const auto lock = std::scoped_lock{mutex_};
            mode = mode_;
            entered_ = true;
        }
        condition_.notify_all();

        if (mode == Mode::BlockUntilCancelled)
        {
            auto lock = std::unique_lock{mutex_};
            while (!cancellation.stop_requested())
            {
                condition_.wait_for(lock, 1ms);
            }
            return std::unexpected(
                core::PaintPlanError::Cancelled);
        }
        if (mode == Mode::Failure)
        {
            return std::unexpected(
                core::PaintPlanError::InvalidSample);
        }
        return one_stroke_plan();
    }

    enum class Mode
    {
        Success,
        BlockUntilCancelled,
        Failure,
    };

    auto set_mode(Mode mode) -> void
    {
        const auto lock = std::scoped_lock{mutex_};
        mode_ = mode;
        entered_ = false;
    }

    auto wait_until_entered() -> bool
    {
        auto lock = std::unique_lock{mutex_};
        return condition_.wait_for(
            lock,
            1s,
            [&] { return entered_; });
    }

private:
    std::mutex mutex_{};
    std::condition_variable condition_{};
    Mode mode_{Mode::Success};
    bool entered_{};
};

class RecordingExecutor final : public GameThreadExecutor
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return true;
    }

    auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> override
    {
        last_operation =
            std::make_shared<GameThreadOperation>(operation);
        return {};
    }

    std::shared_ptr<GameThreadOperation> last_operation{};
};

auto wait_for_phase(
    PaintJobCoordinator& coordinator,
    JobPhase phase,
    std::uint64_t now_ms)
    -> std::expected<JobSnapshot, PaintJobCoordinatorError>
{
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        auto ticked = coordinator.tick(now_ms, {});
        if (!ticked || ticked->phase == phase)
        {
            return ticked;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::unexpected(
        PaintJobCoordinatorError::PlanningFailure);
}

constexpr auto Pacing = core::ReplicationPacingPlan{
    100,
    10,
    100,
    100,
    100,
    1,
    1,
    10,
    10,
    20,
};
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto builder = CoordinatedBuilder{};
    auto worker = PaintPlanningWorker{builder};
    auto jobs = JobStateMachine{};
    auto scheduler = GameThreadScheduler{3U};
    auto dispatcher = PaintDispatchController{scheduler, jobs};
    auto coordinator =
        PaintJobCoordinator{jobs, worker, dispatcher};

    const auto started = coordinator.start(
        101U,
        RuntimeObjectHandle{55U, 3U},
        core::PaintPlanRequest{},
        Pacing,
        100U);
    passed &= expect(
        started.has_value() &&
            *started == jobs.snapshot().generation &&
            jobs.snapshot().phase == JobPhase::Planning &&
            builder.wait_until_entered(),
        "a valid Paint command did not enter planning");

    const auto dispatching =
        wait_for_phase(coordinator, JobPhase::Draining, 100U);
    passed &= expect(
        dispatching &&
            dispatching->phase == JobPhase::Draining &&
            dispatching->progress.total == 1U &&
            dispatching->progress.submitted == 1U &&
            scheduler.queued_paint_generation(*started) == 1U,
        "planning completion did not begin bounded dispatch");
    auto executor = RecordingExecutor{};
    passed &= expect(
        scheduler.drain(executor, 1U).value() == 1U &&
            executor.last_operation &&
            std::get<PaintAtUvWithBrush>(
                *executor.last_operation)
                    .job_generation == *started,
        "the coordinator lost the job generation at execution");
    const auto completed =
        coordinator.tick(120U, {});
    passed &= expect(
        completed &&
            completed->phase == JobPhase::Completed,
        "the coordinator did not complete after confirmed drain");

    builder.set_mode(
        CoordinatedBuilder::Mode::BlockUntilCancelled);
    const auto cancelling = coordinator.start(
        102U,
        RuntimeObjectHandle{66U, 4U},
        core::PaintPlanRequest{},
        Pacing,
        200U);
    passed &= expect(
        cancelling && builder.wait_until_entered() &&
            coordinator
                .request_cancel(*cancelling, 200U, {})
                .has_value() &&
            jobs.snapshot().phase == JobPhase::Cancelling,
        "planning cancellation was not requested");
    const auto cancelled =
        wait_for_phase(coordinator, JobPhase::Cancelled, 220U);
    passed &= expect(
        cancelled &&
            cancelled->phase == JobPhase::Cancelled &&
            cancelled->progress.submitted == 0U,
        "a cancelled planner mutated dispatch state");

    builder.set_mode(CoordinatedBuilder::Mode::Failure);
    const auto failing = coordinator.start(
        103U,
        RuntimeObjectHandle{77U, 5U},
        core::PaintPlanRequest{},
        Pacing,
        300U);
    passed &= expect(
        failing && builder.wait_until_entered(),
        "the planner-failure fixture did not start");
    const auto failed =
        wait_for_phase(coordinator, JobPhase::Failed, 300U);
    passed &= expect(
        !failed &&
            failed.error() ==
                PaintJobCoordinatorError::PlanningFailure &&
            jobs.snapshot().phase == JobPhase::Failed,
        "a typed planner failure did not fail the active job");

    builder.set_mode(CoordinatedBuilder::Mode::Success);
    const auto stale = coordinator.start(
        104U,
        RuntimeObjectHandle{88U, 6U},
        core::PaintPlanRequest{},
        Pacing,
        400U);
    passed &= expect(
        stale && builder.wait_until_entered() &&
            jobs.fail(*stale) == JobMutationResult::Applied &&
            jobs.start(Feature::Paint, 999U) ==
                JobMutationResult::Applied,
        "the stale-completion fixture could not replace the job");
    const auto replacement = jobs.snapshot();
    const auto ignored =
        wait_for_phase(coordinator, JobPhase::Planning, 400U);
    passed &= expect(
        !ignored &&
            ignored.error() ==
                PaintJobCoordinatorError::StaleCompletion &&
            jobs.snapshot() == replacement,
        "a late planning result mutated the replacement job");

    if (passed)
    {
        std::cout << "PASS paint_job_coordinator\n";
    }
    return passed ? 0 : 1;
}
