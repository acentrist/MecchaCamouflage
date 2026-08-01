#include <meccha/application/paint_preview_build_worker.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
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
        std::cerr << "FAIL paint_preview_build_worker: "
                  << message << '\n';
    }
    return condition;
}

auto image(std::uint32_t dimension = 8U) -> PaintTextureImage
{
    const auto bytes = static_cast<std::size_t>(
        dimension * dimension * 4U);
    return PaintTextureImage{
        dimension,
        std::make_shared<const std::vector<std::byte>>(
            bytes,
            std::byte{0x11}),
        std::make_shared<const std::vector<std::byte>>(
            bytes,
            std::byte{0x22}),
    };
}

auto plan() -> core::PaintPlan
{
    auto result = core::PaintPlan{};
    result.strokes.push_back(core::PaintStroke{
        0U,
        core::ReplayPass::Paint,
        core::Region::Side,
        0.5,
        0.5,
        128.0,
        core::Rgb8{10U, 20U, 30U},
        core::Material{0.25, 0.5, 0.75},
    });
    result.paint_count = 1U;
    result.source_paint_count = 1U;
    return result;
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
            while (!cancellation.stop_requested())
            {
                condition_.wait_for(lock, 1ms);
            }
            return std::unexpected(core::PaintPlanError::Cancelled);
        }
        if (call == 3U)
        {
            throw std::runtime_error{"preview planner failure"};
        }
        return plan();
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
};

class ImmediateBuilder final : public PaintPlanBuilder
{
public:
    auto build(
        const core::PaintPlanRequest&,
        std::stop_token)
        -> std::expected<core::PaintPlan, core::PaintPlanError> override
    {
        return plan();
    }
};

auto wait_for_completion(PaintPreviewBuildWorker& worker)
    -> std::optional<PaintPreviewBuildCompletion>
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
    auto worker = PaintPreviewBuildWorker{builder};
    auto request = PaintPreviewBuildRequest{
        core::PaintPlanRequest{},
        image(),
    };
    std::get<core::PaintPlanRequest>(
        request.planning.value)
        .samples.resize(7U);

    const auto invalid_start = worker.start(0U, request);
    passed &= expect(
        !invalid_start &&
            invalid_start.error() ==
                PaintPreviewBuildStartError::InvalidGeneration,
        "generation zero was accepted");
    passed &= expect(
        worker.start(11U, request).has_value() &&
            builder.wait_until_entered(),
        "the first immutable preview request did not start");
    std::get<core::PaintPlanRequest>(
        request.planning.value)
        .samples.clear();
    passed &= expect(
        builder.last_sample_count() == 7U,
        "the worker retained caller-owned mutable planning state");
    const auto busy_start = worker.start(12U, request);
    passed &= expect(
        !busy_start &&
            busy_start.error() ==
                PaintPreviewBuildStartError::Busy,
        "a second concurrent preview build was accepted");
    passed &= expect(
        worker.request_cancel(12U) ==
                PaintPreviewBuildCancelResult::StaleGeneration &&
            worker.request_cancel(11U) ==
                PaintPreviewBuildCancelResult::Requested,
        "cancellation did not validate and stop the active generation");

    const auto cancelled = wait_for_completion(worker);
    passed &= expect(
        cancelled &&
            cancelled->generation == 11U &&
            !cancelled->result &&
            cancelled->result.error().kind ==
                PaintPreviewBuildFailureKind::Planner &&
            cancelled->result.error().planner_error ==
                core::PaintPlanError::Cancelled,
        "cancelled planning did not publish a tagged failure");

    builder.reset_entered();
    passed &= expect(
        worker.start(
                  12U,
                  PaintPreviewBuildRequest{
                      core::PaintPlanRequest{},
                      image()})
                .has_value() &&
            builder.wait_until_entered(),
        "the worker could not be reused after collection");
    const auto completed = wait_for_completion(worker);
    passed &= expect(
        completed &&
            completed->generation == 12U &&
            completed->result &&
            completed->result.value()->dimension == 8U &&
            completed->result.value()->albedo_rgba &&
            std::to_integer<std::uint8_t>(
                completed->result.value()->albedo_rgba->at(
                    (4U * 8U + 4U) * 4U)) == 10U,
        "successful planning and composition did not publish an immutable "
        "texture");

    builder.reset_entered();
    passed &= expect(
        worker.start(
                  13U,
                  PaintPreviewBuildRequest{
                      core::PaintPlanRequest{},
                      image()})
                .has_value() &&
            builder.wait_until_entered(),
        "the exception fixture did not start");
    const auto failed = wait_for_completion(worker);
    passed &= expect(
        failed &&
            failed->generation == 13U &&
            !failed->result &&
            failed->result.error().kind ==
                PaintPreviewBuildFailureKind::WorkerException &&
            !failed->result.error().planner_error &&
            !failed->result.error().compose_error &&
            !failed->result.error().capture_error,
        "a planner exception crossed the preview worker boundary");

    auto invalid_builder = ImmediateBuilder{};
    auto invalid_worker = PaintPreviewBuildWorker{invalid_builder};
    const auto malformed = PaintTextureImage{
        8U,
        nullptr,
        nullptr,
    };
    passed &= expect(
        invalid_worker
            .start(
                21U,
                PaintPreviewBuildRequest{
                    core::PaintPlanRequest{},
                    malformed})
            .has_value(),
        "the malformed-buffer fixture did not enter the worker");
    const auto invalid = wait_for_completion(invalid_worker);
    passed &= expect(
        invalid &&
            !invalid->result &&
            invalid->result.error().kind ==
                PaintPreviewBuildFailureKind::Composer &&
            invalid->result.error().compose_error ==
                core::PaintPreviewComposeError::InvalidBuffer,
        "malformed immutable channels were not a typed composition failure");

    passed &= expect(
        invalid_worker
            .start(
                22U,
                PaintPreviewBuildRequest{
                    core::PaintCaptureInput{},
                    image()})
            .has_value(),
        "an immutable preview capture input did not start");
    const auto capture_failed =
        wait_for_completion(invalid_worker);
    passed &= expect(
        capture_failed &&
            !capture_failed->result &&
            capture_failed->result.error().kind ==
                PaintPreviewBuildFailureKind::Capture &&
            capture_failed->result.error().capture_error ==
                core::PaintCaptureRequestError::InvalidRaster &&
            !capture_failed->result.error().planner_error &&
            !capture_failed->result.error().compose_error,
        "preview capture geometry was not validated inside the worker");

    worker.shutdown();
    const auto stopped_start = worker.start(14U, request);
    passed &= expect(
        !stopped_start &&
            stopped_start.error() ==
                PaintPreviewBuildStartError::Stopped,
        "a stopped preview worker accepted new work");

    if (passed)
    {
        std::cout << "PASS paint_preview_build_worker\n";
    }
    return passed ? 0 : 1;
}
