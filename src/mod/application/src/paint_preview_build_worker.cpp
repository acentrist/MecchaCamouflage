#include <meccha/application/paint_preview_build_worker.hpp>

#include <cstddef>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
auto composer_failure(core::PaintPreviewComposeError error)
    -> PaintPreviewBuildResult
{
    return std::unexpected(PaintPreviewBuildFailure{
        PaintPreviewBuildFailureKind::Composer,
        std::nullopt,
        error,
        std::nullopt,
    });
}
} // namespace

PaintPreviewBuildWorker::PaintPreviewBuildWorker(
    PaintPlanBuilder& builder)
    : builder_{builder}
{
}

PaintPreviewBuildWorker::~PaintPreviewBuildWorker()
{
    shutdown();
}

auto PaintPreviewBuildWorker::start(
    JobGeneration generation,
    PaintPreviewBuildRequest request)
    -> std::expected<void, PaintPreviewBuildStartError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(
            PaintPreviewBuildStartError::Stopped);
    }
    if (generation == 0U)
    {
        return std::unexpected(
            PaintPreviewBuildStartError::InvalidGeneration);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(
            PaintPreviewBuildStartError::Busy);
    }

    active_generation_ = generation;
    state_ = State::Running;
    try
    {
        worker_ = std::jthread{
            [this,
             generation,
             request = std::move(request)](
                std::stop_token cancellation) mutable {
                run(
                    generation,
                    std::move(request),
                    cancellation);
            }};
    }
    catch (...)
    {
        active_generation_ = 0U;
        state_ = State::Idle;
        return std::unexpected(
            PaintPreviewBuildStartError::ThreadStart);
    }
    return {};
}

auto PaintPreviewBuildWorker::request_cancel(
    JobGeneration generation) -> PaintPreviewBuildCancelResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Idle)
    {
        return PaintPreviewBuildCancelResult::Idle;
    }
    if (generation == 0U || generation != active_generation_)
    {
        return PaintPreviewBuildCancelResult::StaleGeneration;
    }
    if (state_ == State::Completed)
    {
        return PaintPreviewBuildCancelResult::AlreadyCompleted;
    }
    static_cast<void>(worker_.request_stop());
    return PaintPreviewBuildCancelResult::Requested;
}

auto PaintPreviewBuildWorker::poll()
    -> std::optional<PaintPreviewBuildCompletion>
{
    auto completed_thread = std::jthread{};
    auto result = std::optional<PaintPreviewBuildCompletion>{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (state_ != State::Completed || !completion_)
        {
            return std::nullopt;
        }
        result = std::move(completion_);
        completion_.reset();
        completed_thread = std::move(worker_);
        active_generation_ = 0U;
        state_ = State::Idle;
    }
    if (completed_thread.joinable())
    {
        completed_thread.join();
    }
    return result;
}

auto PaintPreviewBuildWorker::shutdown() noexcept -> void
{
    auto stopped_thread = std::jthread{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (stopped_ && !worker_.joinable())
        {
            return;
        }
        stopped_ = true;
        if (worker_.joinable())
        {
            static_cast<void>(worker_.request_stop());
            stopped_thread = std::move(worker_);
        }
    }
    if (stopped_thread.joinable())
    {
        stopped_thread.join();
    }
    const auto lock = std::scoped_lock{mutex_};
    completion_.reset();
    active_generation_ = 0U;
    state_ = State::Idle;
}

auto PaintPreviewBuildWorker::run(
    JobGeneration generation,
    PaintPreviewBuildRequest request,
    std::stop_token cancellation) noexcept -> void
{
    auto completion =
        std::optional<PaintPreviewBuildCompletion>{};
    try
    {
        auto plan_request =
            std::optional<core::PaintPlanRequest>{};
        if (auto* captured =
                std::get_if<core::PaintCaptureInput>(
                    &request.planning.value))
        {
            auto built = core::build_paint_capture_request(
                *captured,
                cancellation);
            if (!built)
            {
                completion = PaintPreviewBuildCompletion{
                    generation,
                    std::unexpected(PaintPreviewBuildFailure{
                        PaintPreviewBuildFailureKind::Capture,
                        std::nullopt,
                        std::nullopt,
                        built.error(),
                    }),
                };
            }
            else
            {
                plan_request.emplace(std::move(*built));
            }
        }
        else
        {
            plan_request.emplace(std::move(
                std::get<core::PaintPlanRequest>(
                    request.planning.value)));
        }
        if (completion)
        {
            const auto lock = std::scoped_lock{mutex_};
            if (state_ == State::Running &&
                active_generation_ == generation)
            {
                completion_ = std::move(completion);
                state_ = State::Completed;
            }
            return;
        }

        auto planned =
            builder_.build(*plan_request, cancellation);
        if (!planned)
        {
            completion = PaintPreviewBuildCompletion{
                generation,
                std::unexpected(PaintPreviewBuildFailure{
                    PaintPreviewBuildFailureKind::Planner,
                    planned.error(),
                    std::nullopt,
                    std::nullopt,
                }),
            };
        }
        else if (
            request.original.albedo_rgba == nullptr ||
            request.original.packed_pbr_rgba == nullptr)
        {
            completion = PaintPreviewBuildCompletion{
                generation,
                composer_failure(
                    core::PaintPreviewComposeError::InvalidBuffer),
            };
        }
        else
        {
            auto composed = core::compose_paint_preview(
                request.original.dimension,
                std::span<const std::byte>{
                    *request.original.albedo_rgba},
                std::span<const std::byte>{
                    *request.original.packed_pbr_rgba},
                *planned,
                cancellation);
            if (!composed)
            {
                completion = PaintPreviewBuildCompletion{
                    generation,
                    composer_failure(composed.error()),
                };
            }
            else
            {
                auto image = PaintTextureImage{
                    composed->dimension,
                    std::make_shared<const std::vector<std::byte>>(
                        std::move(composed->albedo_rgba)),
                    std::make_shared<const std::vector<std::byte>>(
                        std::move(composed->packed_pbr_rgba)),
                };
                completion = PaintPreviewBuildCompletion{
                    generation,
                    std::make_shared<const PaintTextureImage>(
                        std::move(image)),
                };
            }
        }
    }
    catch (...)
    {
        completion = PaintPreviewBuildCompletion{
            generation,
            std::unexpected(PaintPreviewBuildFailure{
                PaintPreviewBuildFailureKind::WorkerException,
                std::nullopt,
                std::nullopt,
                std::nullopt,
            }),
        };
    }

    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Running &&
        active_generation_ == generation)
    {
        completion_ = std::move(completion);
        state_ = State::Completed;
    }
}
} // namespace meccha::application
