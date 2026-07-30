#include <meccha/application/image_composition_worker.hpp>

#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace meccha::application
{
auto CoreImageAtlasComposer::compose(
    const core::ImageProjectSettings& settings,
    std::span<const core::ImageLayer> layers,
    std::span<const core::DecodedImageSource> sources,
    std::stop_token cancellation)
    -> std::expected<
        core::ImageAtlasComposition,
        core::ImageComposeError>
{
    return core::compose_image_atlas(
        settings,
        layers,
        sources,
        cancellation);
}

ImageCompositionWorker::ImageCompositionWorker(
    ImageAtlasComposer& composer)
    : composer_{composer}
{
}

ImageCompositionWorker::~ImageCompositionWorker()
{
    shutdown();
}

auto ImageCompositionWorker::start(
    JobGeneration generation,
    ImageCompositionRequest request)
    -> std::expected<void, ImageCompositionStartError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(ImageCompositionStartError::Stopped);
    }
    if (generation == 0U)
    {
        return std::unexpected(
            ImageCompositionStartError::InvalidGeneration);
    }
    if (!core::valid_image_project_id(request.project_id))
    {
        return std::unexpected(
            ImageCompositionStartError::InvalidProjectIdentity);
    }
    if (request.project_revision == 0U)
    {
        return std::unexpected(
            ImageCompositionStartError::InvalidProjectRevision);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(ImageCompositionStartError::Busy);
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
            ImageCompositionStartError::ThreadStart);
    }
    return {};
}

auto ImageCompositionWorker::request_cancel(
    JobGeneration generation) -> ImageCompositionCancelResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Idle)
    {
        return ImageCompositionCancelResult::Idle;
    }
    if (generation == 0U || generation != active_generation_)
    {
        return ImageCompositionCancelResult::StaleGeneration;
    }
    if (state_ == State::Completed)
    {
        return ImageCompositionCancelResult::AlreadyCompleted;
    }
    static_cast<void>(worker_.request_stop());
    return ImageCompositionCancelResult::Requested;
}

auto ImageCompositionWorker::poll()
    -> std::optional<ImageCompositionCompletion>
{
    auto completed_thread = std::jthread{};
    auto result = std::optional<ImageCompositionCompletion>{};
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

auto ImageCompositionWorker::shutdown() noexcept -> void
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

auto ImageCompositionWorker::run(
    JobGeneration generation,
    ImageCompositionRequest request,
    std::stop_token cancellation) noexcept -> void
{
    auto result = std::optional<ImageCompositionCompletion>{};
    try
    {
        auto composed = composer_.compose(
            request.settings,
            request.layers,
            request.sources,
            cancellation);
        if (!composed)
        {
            result = ImageCompositionCompletion{
                generation,
                request.project_id,
                request.project_revision,
                std::unexpected(ImageCompositionFailure{
                    ImageCompositionFailureKind::Composer,
                    composed.error(),
                }),
            };
        }
        else
        {
            result = ImageCompositionCompletion{
                generation,
                request.project_id,
                request.project_revision,
                std::make_shared<
                    const core::ImageAtlasComposition>(
                    std::move(*composed)),
            };
        }
    }
    catch (...)
    {
        result = ImageCompositionCompletion{
            generation,
            request.project_id,
            request.project_revision,
            std::unexpected(ImageCompositionFailure{
                ImageCompositionFailureKind::WorkerException,
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
