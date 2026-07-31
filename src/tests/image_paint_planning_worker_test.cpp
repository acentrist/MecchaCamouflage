#include <meccha/application/image_paint_planning_worker.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <expected>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_paint_planning_worker: "
                  << message << '\n';
    }
    return condition;
}

auto request() -> ImagePaintPlanningRequest
{
    auto result = ImagePaintPlanningRequest{};
    result.project_id = "0123456789abcdef0123456789abcdef";
    result.project_revision = 7U;
    result.plan.sampling_profile.vertices =
        std::make_shared<
            const std::vector<core::PaintSamplingVertex>>(
            5U);
    return result;
}

class ControlledBuilder final : public ImagePaintPlanBuilder
{
public:
    auto build(
        const core::ImagePaintProfilePlanRequest& request,
        std::stop_token cancellation)
        -> std::expected<
            core::ImagePaintPlan,
            core::ImagePaintPlanError> override
    {
        auto call = std::size_t{};
        {
            const auto lock = std::scoped_lock{mutex_};
            call = ++calls_;
            sample_count_ =
                request.sampling_profile.vertices
                    ? request.sampling_profile.vertices->size()
                    : 0U;
            entered_ = true;
        }
        condition_.notify_all();
        if (call == 1U)
        {
            auto lock = std::unique_lock{mutex_};
            while (!cancellation.stop_requested())
            {
                condition_.wait_for(lock, 1ms);
            }
            return std::unexpected(
                core::ImagePaintPlanError::Cancelled);
        }
        if (call == 3U)
        {
            throw std::runtime_error{"image planner failure"};
        }
        auto result = core::ImagePaintPlan{};
        result.opaque_samples = sample_count();
        return result;
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

    [[nodiscard]] auto sample_count() const -> std::size_t
    {
        const auto lock = std::scoped_lock{mutex_};
        return sample_count_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    std::size_t calls_{};
    std::size_t sample_count_{};
    bool entered_{};
};

auto wait_for_completion(ImagePaintPlanningWorker& worker)
    -> std::optional<ImagePaintPlanningCompletion>
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
    auto worker = ImagePaintPlanningWorker{builder};
    auto input = request();

    passed &= expect(
        worker.start(0U, input) ==
            std::unexpected(
                ImagePaintPlanningStartError::InvalidGeneration),
        "generation zero was accepted");
    auto invalid_identity = input;
    invalid_identity.project_id = "not-an-id";
    passed &= expect(
        worker.start(1U, std::move(invalid_identity)) ==
            std::unexpected(
                ImagePaintPlanningStartError::InvalidProjectIdentity),
        "an invalid project identity was accepted");
    auto invalid_revision = input;
    invalid_revision.project_revision = 0U;
    passed &= expect(
        worker.start(1U, std::move(invalid_revision)) ==
            std::unexpected(
                ImagePaintPlanningStartError::InvalidProjectRevision),
        "revision zero was accepted");

    passed &= expect(
        worker.start(11U, input).has_value() &&
            builder.wait_until_entered(),
        "the first immutable planning request did not start");
    input.plan.sampling_profile.vertices.reset();
    passed &= expect(
        builder.sample_count() == 5U,
        "the worker retained caller-owned mutable capture samples");
    passed &= expect(
        worker.start(12U, request()) ==
            std::unexpected(ImagePaintPlanningStartError::Busy),
        "a second concurrent Image Paint plan was accepted");
    passed &= expect(
        worker.request_cancel(12U) ==
                ImagePaintPlanningCancelResult::StaleGeneration &&
            worker.request_cancel(11U) ==
                ImagePaintPlanningCancelResult::Requested,
        "cancellation did not validate and stop the active generation");

    const auto cancelled = wait_for_completion(worker);
    passed &= expect(
        cancelled &&
            cancelled->generation == 11U &&
            cancelled->project_id ==
                "0123456789abcdef0123456789abcdef" &&
            cancelled->project_revision == 7U &&
            !cancelled->result &&
            cancelled->result.error().kind ==
                ImagePaintPlanningFailureKind::Planner &&
            cancelled->result.error().planner_error ==
                core::ImagePaintPlanError::Cancelled,
        "cancelled planning did not publish a fully tagged failure");

    builder.reset_entered();
    auto second = request();
    second.project_revision = 8U;
    passed &= expect(
        worker.start(12U, std::move(second)).has_value() &&
            builder.wait_until_entered(),
        "the worker could not be reused after collection");
    const auto completed = wait_for_completion(worker);
    passed &= expect(
        completed &&
            completed->generation == 12U &&
            completed->project_revision == 8U &&
            completed->result &&
            completed->result.value()->opaque_samples == 5U,
        "successful planning did not publish an immutable tagged plan");

    builder.reset_entered();
    auto third = request();
    third.project_revision = 9U;
    passed &= expect(
        worker.start(13U, std::move(third)).has_value() &&
            builder.wait_until_entered(),
        "the exception fixture did not start");
    const auto failed = wait_for_completion(worker);
    passed &= expect(
        failed &&
            failed->generation == 13U &&
            failed->project_revision == 9U &&
            !failed->result &&
            failed->result.error().kind ==
                ImagePaintPlanningFailureKind::WorkerException &&
            !failed->result.error().planner_error,
        "an exception crossed the Image Paint worker boundary");

    worker.shutdown();
    passed &= expect(
        worker.start(14U, request()) ==
            std::unexpected(ImagePaintPlanningStartError::Stopped),
        "a stopped Image Paint worker accepted new work");

    if (passed)
    {
        std::cout << "PASS image_paint_planning_worker\n";
    }
    return passed ? 0 : 1;
}
