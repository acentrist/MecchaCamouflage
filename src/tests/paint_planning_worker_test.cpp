#include <meccha/application/paint_planning_worker.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <iostream>
#include <mutex>
#include <stdexcept>
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
        std::cerr << "FAIL paint_planning_worker: "
                  << message << '\n';
    }
    return condition;
}

class ControlledBuilder final : public PaintPlanBuilder
{
public:
    auto build(
        const core::PaintPlanRequest& request,
        std::stop_token cancellation)
        -> std::expected<core::PaintPlan, core::PaintPlanError> override
    {
        auto call = std::size_t{};
        {
            const auto lock = std::scoped_lock{mutex_};
            call = ++calls_;
            last_sample_count_ = request.samples.size();
            entered_ = true;
        }
        condition_.notify_all();

        if (call == 1U)
        {
            auto lock = std::unique_lock{mutex_};
            while (
                !cancellation.stop_requested() && !released_)
            {
                condition_.wait_for(lock, 1ms);
            }
            if (cancellation.stop_requested())
            {
                return std::unexpected(
                    core::PaintPlanError::Cancelled);
            }
        }
        if (call == 3U)
        {
            throw std::runtime_error{"planner failure"};
        }

        auto plan = core::PaintPlan{};
        plan.source_paint_count = request.samples.size();
        return plan;
    }

    auto wait_until_entered() -> bool
    {
        auto lock = std::unique_lock{mutex_};
        return condition_.wait_for(
            lock,
            1s,
            [&] { return entered_; });
    }

    auto reset_entered() -> void
    {
        const auto lock = std::scoped_lock{mutex_};
        entered_ = false;
    }

    [[nodiscard]] auto last_sample_count() const -> std::size_t
    {
        const auto lock = std::scoped_lock{mutex_};
        return last_sample_count_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    std::size_t calls_{};
    std::size_t last_sample_count_{};
    bool entered_{};
    bool released_{};
};

auto wait_for_completion(PaintPlanningWorker& worker)
    -> std::optional<PaintPlanningCompletion>
{
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        if (auto result = worker.poll())
        {
            return result;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto builder = ControlledBuilder{};
    auto worker = PaintPlanningWorker{builder};
    auto request = core::PaintPlanRequest{};
    request.samples.resize(7U);

    const auto invalid_start = worker.start(0U, request);
    passed &= expect(
        !invalid_start &&
            invalid_start.error() ==
                PaintPlanningStartError::InvalidGeneration,
        "generation zero was accepted");
    passed &= expect(
        worker.start(11U, request).has_value() &&
            builder.wait_until_entered(),
        "the first immutable planning request did not start");
    request.samples.clear();
    passed &= expect(
        builder.last_sample_count() == 7U,
        "the worker retained caller-owned mutable request state");
    const auto busy_start = worker.start(12U, request);
    passed &= expect(
        !busy_start &&
            busy_start.error() ==
                PaintPlanningStartError::Busy,
        "a second concurrent plan was accepted");
    passed &= expect(
        worker.request_cancel(12U) ==
                PaintPlanningCancelResult::StaleGeneration &&
            worker.request_cancel(11U) ==
                PaintPlanningCancelResult::Requested,
        "cancellation did not validate and stop the active generation");

    const auto cancelled = wait_for_completion(worker);
    passed &= expect(
        cancelled &&
            cancelled->generation == 11U &&
            !cancelled->result &&
            cancelled->result.error().kind ==
                PaintPlanningFailureKind::Planner &&
            cancelled->result.error().planner_error ==
                core::PaintPlanError::Cancelled,
        "cancelled planning did not publish a tagged failure");

    builder.reset_entered();
    passed &= expect(
        worker.start(12U, core::PaintPlanRequest{}).has_value() &&
            builder.wait_until_entered(),
        "the worker could not be reused after collection");
    const auto completed = wait_for_completion(worker);
    passed &= expect(
        completed &&
            completed->generation == 12U &&
            completed->result.has_value(),
        "successful planning did not publish an immutable plan");

    builder.reset_entered();
    passed &= expect(
        worker.start(13U, core::PaintPlanRequest{}).has_value() &&
            builder.wait_until_entered(),
        "the exception fixture did not start");
    const auto failed = wait_for_completion(worker);
    passed &= expect(
        failed &&
            failed->generation == 13U &&
            !failed->result &&
            failed->result.error().kind ==
                PaintPlanningFailureKind::WorkerException &&
            !failed->result.error().capture_error &&
            !failed->result.error().planner_error,
        "a planner exception crossed the worker boundary");

    builder.reset_entered();
    passed &= expect(
        worker.start(14U, core::PaintCaptureInput{}).has_value(),
        "an immutable Paint capture input did not start");
    const auto capture_failed = wait_for_completion(worker);
    passed &= expect(
        capture_failed &&
            capture_failed->generation == 14U &&
            !capture_failed->result &&
            capture_failed->result.error().kind ==
                PaintPlanningFailureKind::Capture &&
            capture_failed->result.error().capture_error ==
                core::PaintCaptureRequestError::InvalidRaster &&
            !capture_failed->result.error().planner_error &&
            !builder.wait_until_entered(),
        "capture geometry was not validated inside the worker");

    worker.shutdown();
    const auto stopped_start =
        worker.start(15U, core::PaintPlanRequest{});
    passed &= expect(
        !stopped_start &&
            stopped_start.error() ==
                PaintPlanningStartError::Stopped,
        "a stopped worker accepted new work");

    if (passed)
    {
        std::cout << "PASS paint_planning_worker\n";
    }
    return passed ? 0 : 1;
}
