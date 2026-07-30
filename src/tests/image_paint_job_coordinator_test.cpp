#include <meccha/application/image_paint_job_coordinator.hpp>

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

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_paint_job_coordinator: "
                  << message << '\n';
    }
    return condition;
}

auto one_stroke_plan() -> core::ImagePaintPlan
{
    auto result = core::ImagePaintPlan{};
    result.paint.strokes.push_back(core::PaintStroke{
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
    result.paint.paint_count = 1U;
    result.paint.source_paint_count = 1U;
    result.opaque_samples = 1U;
    return result;
}

class CoordinatedBuilder final : public ImagePaintPlanBuilder
{
public:
    auto build(
        const core::ImagePaintPlanRequest&,
        std::stop_token cancellation)
        -> std::expected<
            core::ImagePaintPlan,
            core::ImagePaintPlanError> override
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
                core::ImagePaintPlanError::Cancelled);
        }
        if (mode == Mode::Failure)
        {
            return std::unexpected(
                core::ImagePaintPlanError::InvalidSample);
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
    ImagePaintJobCoordinator& coordinator,
    JobPhase phase,
    std::uint64_t now_ms,
    std::string_view project_id = ProjectId,
    std::uint64_t project_revision = 7U)
    -> std::expected<JobSnapshot, ImagePaintJobCoordinatorError>
{
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        auto ticked = coordinator.tick(
            now_ms,
            {},
            project_id,
            project_revision);
        if (!ticked || ticked->phase == phase)
        {
            return ticked;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::unexpected(
        ImagePaintJobCoordinatorError::PlanningFailure);
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
    auto worker = ImagePaintPlanningWorker{builder};
    auto jobs = JobStateMachine{};
    auto scheduler = GameThreadScheduler{3U};
    auto dispatcher = PaintDispatchController{scheduler, jobs};
    auto coordinator =
        ImagePaintJobCoordinator{jobs, worker, dispatcher};

    const auto started = coordinator.start(
        101U,
        std::string{ProjectId},
        7U,
        RuntimeObjectHandle{55U, 3U},
        core::ImagePaintPlanRequest{},
        Pacing,
        100U);
    passed &= expect(
        started &&
            *started == jobs.snapshot().generation &&
            jobs.snapshot().feature == Feature::ImagePaint &&
            jobs.snapshot().phase == JobPhase::Planning &&
            builder.wait_until_entered(),
        "a valid Image Paint command did not enter planning");

    const auto draining = wait_for_phase(
        coordinator,
        JobPhase::Draining,
        100U);
    passed &= expect(
        draining &&
            draining->progress.total == 1U &&
            draining->progress.submitted == 1U &&
            scheduler.queued_paint_generation(*started) == 1U,
        "planning completion did not enter the shared bounded dispatcher");
    auto executor = RecordingExecutor{};
    passed &= expect(
        scheduler.drain(executor, 1U).value() == 1U &&
            executor.last_operation &&
            std::get<PaintAtUvWithBrush>(
                *executor.last_operation)
                    .job_generation == *started,
        "Image Paint did not retain the shared dispatch generation");
    const auto completed = coordinator.tick(
        120U,
        {},
        ProjectId,
        7U);
    passed &= expect(
        completed &&
            completed->phase == JobPhase::Completed,
        "Image Paint did not complete after confirmed queue drain");

    builder.set_mode(
        CoordinatedBuilder::Mode::BlockUntilCancelled);
    const auto cancelling = coordinator.start(
        102U,
        std::string{ProjectId},
        8U,
        RuntimeObjectHandle{66U, 4U},
        core::ImagePaintPlanRequest{},
        Pacing,
        200U);
    passed &= expect(
        cancelling && builder.wait_until_entered() &&
            coordinator
                .request_cancel(*cancelling, 200U, {})
                .has_value() &&
            jobs.snapshot().phase == JobPhase::Cancelling,
        "Image Paint planning cancellation was not requested");
    const auto cancelled = wait_for_phase(
        coordinator,
        JobPhase::Cancelled,
        220U,
        ProjectId,
        8U);
    passed &= expect(
        cancelled &&
            cancelled->progress.submitted == 0U,
        "a cancelled Image Paint planner mutated dispatch state");

    builder.set_mode(
        CoordinatedBuilder::Mode::BlockUntilCancelled);
    const auto stale = coordinator.start(
        103U,
        std::string{ProjectId},
        9U,
        RuntimeObjectHandle{77U, 5U},
        core::ImagePaintPlanRequest{},
        Pacing,
        300U);
    passed &= expect(
        stale && builder.wait_until_entered(),
        "the stale-project fixture did not start");
    const auto rejected = wait_for_phase(
        coordinator,
        JobPhase::Cancelled,
        300U,
        ProjectId,
        10U);
    passed &= expect(
        !rejected &&
            rejected.error() ==
                ImagePaintJobCoordinatorError::StaleProject &&
            jobs.snapshot().phase == JobPhase::Cancelled &&
            scheduler.queued_paint_generation(*stale) == 0U,
        "a stale project revision reached the shared dispatcher");

    builder.set_mode(CoordinatedBuilder::Mode::Success);
    const auto stale_dispatch = coordinator.start(
        104U,
        std::string{ProjectId},
        11U,
        RuntimeObjectHandle{88U, 6U},
        core::ImagePaintPlanRequest{},
        Pacing,
        400U);
    passed &= expect(
        stale_dispatch && builder.wait_until_entered(),
        "the stale-dispatch fixture did not start");
    const auto dispatch_started = wait_for_phase(
        coordinator,
        JobPhase::Draining,
        400U,
        ProjectId,
        11U);
    const auto dispatch_stale = coordinator.tick(
        400U,
        {},
        ProjectId,
        12U);
    passed &= expect(
        dispatch_started &&
            dispatch_started->progress.submitted == 1U &&
            dispatch_stale &&
            dispatch_stale->phase == JobPhase::Cancelling &&
            scheduler.queued_paint_generation(*stale_dispatch) == 0U,
        "a revision change during dispatch did not discard queued strokes");
    const auto dispatch_rejected = coordinator.tick(
        420U,
        {},
        ProjectId,
        12U);
    passed &= expect(
        !dispatch_rejected &&
            dispatch_rejected.error() ==
                ImagePaintJobCoordinatorError::StaleProject &&
            jobs.snapshot().phase == JobPhase::Cancelled,
        "stale dispatch did not terminate after its drain interval");

    builder.set_mode(CoordinatedBuilder::Mode::Failure);
    const auto failing = coordinator.start(
        105U,
        std::string{ProjectId},
        13U,
        RuntimeObjectHandle{99U, 7U},
        core::ImagePaintPlanRequest{},
        Pacing,
        500U);
    passed &= expect(
        failing && builder.wait_until_entered(),
        "the Image Paint planner-failure fixture did not start");
    const auto failed = wait_for_phase(
        coordinator,
        JobPhase::Failed,
        500U,
        ProjectId,
        13U);
    passed &= expect(
        !failed &&
            failed.error() ==
                ImagePaintJobCoordinatorError::PlanningFailure &&
            jobs.snapshot().phase == JobPhase::Failed,
        "a typed Image Paint planner failure did not fail the job");

    if (passed)
    {
        std::cout << "PASS image_paint_job_coordinator\n";
    }
    return passed ? 0 : 1;
}
