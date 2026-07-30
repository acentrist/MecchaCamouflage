#include <meccha/application/image_paint_planning_worker.hpp>

#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace meccha::application
{
auto CoreImagePaintPlanBuilder::build(
    const core::ImagePaintPlanRequest& request,
    std::stop_token cancellation)
    -> std::expected<
        core::ImagePaintPlan,
        core::ImagePaintPlanError>
{
    return core::build_image_paint_plan(request, cancellation);
}

ImagePaintPlanningWorker::ImagePaintPlanningWorker(
    ImagePaintPlanBuilder& builder)
    : builder_{builder}
{
}

ImagePaintPlanningWorker::~ImagePaintPlanningWorker()
{
    shutdown();
}

auto ImagePaintPlanningWorker::start(
    JobGeneration generation,
    ImagePaintPlanningRequest request)
    -> std::expected<void, ImagePaintPlanningStartError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(
            ImagePaintPlanningStartError::Stopped);
    }
    if (generation == 0U)
    {
        return std::unexpected(
            ImagePaintPlanningStartError::InvalidGeneration);
    }
    if (!core::valid_image_project_id(request.project_id))
    {
        return std::unexpected(
            ImagePaintPlanningStartError::InvalidProjectIdentity);
    }
    if (request.project_revision == 0U)
    {
        return std::unexpected(
            ImagePaintPlanningStartError::InvalidProjectRevision);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(ImagePaintPlanningStartError::Busy);
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
            ImagePaintPlanningStartError::ThreadStart);
    }
    return {};
}

auto ImagePaintPlanningWorker::request_cancel(
    JobGeneration generation) -> ImagePaintPlanningCancelResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Idle)
    {
        return ImagePaintPlanningCancelResult::Idle;
    }
    if (generation == 0U || generation != active_generation_)
    {
        return ImagePaintPlanningCancelResult::StaleGeneration;
    }
    if (state_ == State::Completed)
    {
        return ImagePaintPlanningCancelResult::AlreadyCompleted;
    }
    static_cast<void>(worker_.request_stop());
    return ImagePaintPlanningCancelResult::Requested;
}

auto ImagePaintPlanningWorker::poll()
    -> std::optional<ImagePaintPlanningCompletion>
{
    auto completed_thread = std::jthread{};
    auto result = std::optional<ImagePaintPlanningCompletion>{};
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

auto ImagePaintPlanningWorker::shutdown() noexcept -> void
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

auto ImagePaintPlanningWorker::run(
    JobGeneration generation,
    ImagePaintPlanningRequest request,
    std::stop_token cancellation) noexcept -> void
{
    auto result = std::optional<ImagePaintPlanningCompletion>{};
    try
    {
        auto planned = builder_.build(request.plan, cancellation);
        if (!planned)
        {
            result = ImagePaintPlanningCompletion{
                generation,
                request.project_id,
                request.project_revision,
                std::unexpected(ImagePaintPlanningFailure{
                    ImagePaintPlanningFailureKind::Planner,
                    planned.error(),
                }),
            };
        }
        else
        {
            result = ImagePaintPlanningCompletion{
                generation,
                request.project_id,
                request.project_revision,
                std::make_shared<const core::ImagePaintPlan>(
                    std::move(*planned)),
            };
        }
    }
    catch (...)
    {
        result = ImagePaintPlanningCompletion{
            generation,
            request.project_id,
            request.project_revision,
            std::unexpected(ImagePaintPlanningFailure{
                ImagePaintPlanningFailureKind::WorkerException,
                std::nullopt,
            }),
        };
    }

    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Running &&
        active_generation_ == generation)
    {
        completion_ = std::move(result);
        state_ = State::Completed;
    }
}
} // namespace meccha::application
