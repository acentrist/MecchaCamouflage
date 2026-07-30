#include <meccha/application/image_project_io_worker.hpp>

#include <expected>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace meccha::application
{
namespace
{
auto command_id(const ImageProjectIoRequest& request) -> CommandId
{
    return std::visit(
        [](const auto& operation)
        {
            return operation.command_id;
        },
        request);
}

auto operation(const ImageProjectIoRequest& request)
    -> ImageProjectIoOperation
{
    return std::visit(
        [](const auto& value)
        {
            using Request = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<Request, ImageProjectLoadRequest>)
            {
                return ImageProjectIoOperation::Load;
            }
            else if constexpr (
                std::is_same_v<Request, ImageProjectSaveRequest>)
            {
                return ImageProjectIoOperation::Save;
            }
            else if constexpr (
                std::is_same_v<Request, ImageProjectRenameRequest>)
            {
                return ImageProjectIoOperation::Rename;
            }
            else
            {
                return ImageProjectIoOperation::Delete;
            }
        },
        request);
}

auto valid(const ImageProjectIoRequest& request) -> bool
{
    return std::visit(
        [](const auto& value)
        {
            using Request = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<Request, ImageProjectLoadRequest> ||
                std::is_same_v<Request, ImageProjectDeleteRequest>)
            {
                return core::valid_image_project_id(
                           value.project_id) &&
                       core::validate(value.current_config).empty();
            }
            else if constexpr (
                std::is_same_v<Request, ImageProjectSaveRequest>)
            {
                return value.project &&
                       core::validate(*value.project).empty() &&
                       core::validate(value.current_config).empty();
            }
            else
            {
                return core::valid_image_project_id(
                           value.project_id) &&
                       !value.new_name.empty();
            }
        },
        request);
}

auto persistence_failure(
    ImageProjectPersistenceError error)
    -> std::unexpected<ImageProjectIoFailure>
{
    return std::unexpected(ImageProjectIoFailure{
        ImageProjectIoFailureKind::Persistence,
        std::move(error),
        std::nullopt,
    });
}

auto project_failure(ImageProjectStoreError error)
    -> std::unexpected<ImageProjectIoFailure>
{
    return std::unexpected(ImageProjectIoFailure{
        ImageProjectIoFailureKind::ProjectStore,
        std::nullopt,
        std::move(error),
    });
}

auto execute(
    ImageProjectStore& projects,
    ImageProjectPersistenceCoordinator& persistence,
    ImageProjectIoRequest request) -> ImageProjectIoResult
{
    return std::visit(
        [&projects, &persistence](auto&& value)
            -> ImageProjectIoResult
        {
            using Request = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<Request, ImageProjectLoadRequest>)
            {
                auto loaded =
                    persistence.load_named_and_activate(
                        value.project_id,
                        value.current_config);
                if (!loaded)
                {
                    return persistence_failure(
                        std::move(loaded.error()));
                }
                return ImageProjectIoValue{
                    std::move(*loaded),
                };
            }
            else if constexpr (
                std::is_same_v<Request, ImageProjectSaveRequest>)
            {
                auto configured =
                    persistence.save_named_and_activate(
                        *value.project,
                        value.expected_revision,
                        value.current_config);
                if (!configured)
                {
                    return persistence_failure(
                        std::move(configured.error()));
                }
                return ImageProjectIoValue{
                    SavedImageProject{
                        std::move(value.project),
                        std::move(*configured),
                    },
                };
            }
            else if constexpr (
                std::is_same_v<Request, ImageProjectRenameRequest>)
            {
                auto renamed = projects.rename_named(
                    value.project_id,
                    std::move(value.new_name));
                if (!renamed)
                {
                    return project_failure(
                        std::move(renamed.error()));
                }
                return ImageProjectIoValue{
                    std::move(*renamed),
                };
            }
            else
            {
                auto deleted =
                    persistence.delete_named_and_deactivate(
                        value.project_id,
                        value.current_config);
                if (!deleted)
                {
                    return persistence_failure(
                        std::move(deleted.error()));
                }
                return ImageProjectIoValue{
                    std::move(*deleted),
                };
            }
        },
        std::move(request));
}
} // namespace

ImageProjectIoWorker::ImageProjectIoWorker(
    ImageProjectStore& projects,
    ImageProjectPersistenceCoordinator& persistence)
    : projects_{projects},
      persistence_{persistence}
{
}

ImageProjectIoWorker::~ImageProjectIoWorker()
{
    shutdown();
}

auto ImageProjectIoWorker::start(ImageProjectIoRequest request)
    -> std::expected<void, ImageProjectIoStartError>
{
    if (command_id(request) == 0U)
    {
        return std::unexpected(
            ImageProjectIoStartError::InvalidCommand);
    }
    if (!valid(request))
    {
        return std::unexpected(
            ImageProjectIoStartError::InvalidRequest);
    }

    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(ImageProjectIoStartError::Stopped);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(ImageProjectIoStartError::Busy);
    }

    state_ = State::Running;
    try
    {
        worker_ = std::jthread{
            [this, request = std::move(request)]() mutable
            {
                run(std::move(request));
            }};
    }
    catch (...)
    {
        state_ = State::Idle;
        return std::unexpected(
            ImageProjectIoStartError::ThreadStart);
    }
    return {};
}

auto ImageProjectIoWorker::poll()
    -> std::optional<ImageProjectIoCompletion>
{
    auto completed_thread = std::jthread{};
    auto result = std::optional<ImageProjectIoCompletion>{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (state_ != State::Completed || !completion_)
        {
            return std::nullopt;
        }
        result = std::move(completion_);
        completion_.reset();
        completed_thread = std::move(worker_);
        state_ = State::Idle;
    }
    if (completed_thread.joinable())
    {
        completed_thread.join();
    }
    return result;
}

auto ImageProjectIoWorker::shutdown() noexcept -> void
{
    auto active_thread = std::jthread{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (stopped_ && !worker_.joinable())
        {
            return;
        }
        stopped_ = true;
        if (worker_.joinable())
        {
            active_thread = std::move(worker_);
        }
    }
    if (active_thread.joinable())
    {
        active_thread.join();
    }
    const auto lock = std::scoped_lock{mutex_};
    completion_.reset();
    state_ = State::Idle;
}

auto ImageProjectIoWorker::run(
    ImageProjectIoRequest request) noexcept -> void
{
    const auto id = command_id(request);
    const auto kind = operation(request);
    auto result = ImageProjectIoResult{
        std::unexpected(ImageProjectIoFailure{
            ImageProjectIoFailureKind::WorkerException,
        })};
    try
    {
        result = execute(
            projects_,
            persistence_,
            std::move(request));
    }
    catch (...)
    {
    }

    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Running)
    {
        completion_ = ImageProjectIoCompletion{
            id,
            kind,
            std::move(result),
        };
        state_ = State::Completed;
    }
}
} // namespace meccha::application
