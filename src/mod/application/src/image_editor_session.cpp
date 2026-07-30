#include <meccha/application/image_editor_session.hpp>

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace meccha::application
{
namespace
{
auto map_start_error(ImageProjectIoStartError error)
    -> ImageEditorSessionStartError
{
    switch (error)
    {
    case ImageProjectIoStartError::InvalidCommand:
        return ImageEditorSessionStartError::InvalidCommand;
    case ImageProjectIoStartError::InvalidRequest:
        return ImageEditorSessionStartError::InvalidProject;
    case ImageProjectIoStartError::Busy:
        return ImageEditorSessionStartError::Busy;
    case ImageProjectIoStartError::Stopped:
        return ImageEditorSessionStartError::Stopped;
    case ImageProjectIoStartError::ThreadStart:
        return ImageEditorSessionStartError::PersistenceStart;
    }
    return ImageEditorSessionStartError::PersistenceStart;
}

auto pipeline_failure(ImageEditorSubmitError error)
    -> ImageEditorSessionFailure
{
    return ImageEditorSessionFailure{
        ImageEditorSessionFailureKind::Pipeline,
        std::nullopt,
        error,
        std::nullopt,
    };
}

auto invalid_completion() -> ImageEditorSessionFailure
{
    return ImageEditorSessionFailure{
        ImageEditorSessionFailureKind::InvalidCompletion,
    };
}
} // namespace

ImageEditorSession::ImageEditorSession(
    ImageSourceDecoder& decoder,
    ImageAtlasComposer& composer,
    ImageProjectStore& projects,
    ImageProjectPersistenceCoordinator& persistence,
    std::chrono::milliseconds draft_debounce)
    : pipeline_{decoder, composer},
      persistence_worker_{projects, persistence},
      draft_worker_{projects, draft_debounce}
{
}

ImageEditorSession::~ImageEditorSession()
{
    shutdown(true);
}

auto ImageEditorSession::submit_edit(core::ImageProject project)
    -> std::expected<JobGeneration, ImageEditorSubmitError>
{
    if (stopped_)
    {
        return std::unexpected(ImageEditorSubmitError::Stopped);
    }
    return pipeline_.submit(std::move(project));
}

auto ImageEditorSession::load(
    CommandId command_id,
    std::string project_id,
    core::ApplicationConfig current_config)
    -> std::expected<void, ImageEditorSessionStartError>
{
    auto request = ImageProjectIoRequest{
        ImageProjectLoadRequest{
            command_id,
            project_id,
            std::move(current_config),
        }};
    return admit(ActivePersistenceOperation{
        command_id,
        ImageProjectIoOperation::Load,
        std::move(project_id),
        false,
        std::move(request),
    });
}

auto ImageEditorSession::save(
    CommandId command_id,
    std::string_view project_id,
    std::uint64_t expected_revision,
    core::ApplicationConfig current_config)
    -> std::expected<void, ImageEditorSessionStartError>
{
    const auto editor = pipeline_.snapshot();
    const auto project = pipeline_.ready_project(
        project_id,
        editor.project_revision);
    if (!project)
    {
        return std::unexpected(
            ImageEditorSessionStartError::NotReady);
    }

    auto request = ImageProjectIoRequest{
        ImageProjectSaveRequest{
            command_id,
            project,
            expected_revision,
            std::move(current_config),
        }};
    return admit(ActivePersistenceOperation{
        command_id,
        ImageProjectIoOperation::Save,
        std::string{project_id},
        false,
        std::move(request),
    });
}

auto ImageEditorSession::rename(
    CommandId command_id,
    std::string_view project_id,
    std::uint64_t expected_revision,
    std::string new_name)
    -> std::expected<void, ImageEditorSessionStartError>
{
    const auto editor = pipeline_.snapshot();
    const auto project = pipeline_.ready_project(
        project_id,
        editor.project_revision);
    if (!project)
    {
        return std::unexpected(
            ImageEditorSessionStartError::NotReady);
    }

    auto request = ImageProjectIoRequest{
        ImageProjectRenameRequest{
            command_id,
            project,
            expected_revision,
            std::move(new_name),
        }};
    return admit(ActivePersistenceOperation{
        command_id,
        ImageProjectIoOperation::Rename,
        std::string{project_id},
        false,
        std::move(request),
    });
}

auto ImageEditorSession::remove(
    CommandId command_id,
    std::string project_id,
    core::ApplicationConfig current_config)
    -> std::expected<void, ImageEditorSessionStartError>
{
    auto request = ImageProjectIoRequest{
        ImageProjectDeleteRequest{
            command_id,
            project_id,
            std::move(current_config),
        }};
    return admit(ActivePersistenceOperation{
        command_id,
        ImageProjectIoOperation::Delete,
        std::move(project_id),
        true,
        std::move(request),
    });
}

auto ImageEditorSession::admit(
    ActivePersistenceOperation operation)
    -> std::expected<void, ImageEditorSessionStartError>
{
    if (stopped_)
    {
        return std::unexpected(
            ImageEditorSessionStartError::Stopped);
    }
    if (operation.command_id == 0U)
    {
        return std::unexpected(
            ImageEditorSessionStartError::InvalidCommand);
    }
    if (active_operation_ || completion_)
    {
        return std::unexpected(
            ImageEditorSessionStartError::Busy);
    }

    if (!operation.waiting_for_draft)
    {
        auto request = std::move(*operation.request);
        operation.request.reset();
        const auto started =
            persistence_worker_.start(std::move(request));
        if (!started)
        {
            return std::unexpected(
                map_start_error(started.error()));
        }
    }
    active_operation_ = std::move(operation);
    return {};
}

auto ImageEditorSession::update() -> void
{
    if (stopped_)
    {
        return;
    }

    pipeline_.update();
    observe_ready_project();
    start_waiting_operation();
    if (auto completed = persistence_worker_.poll())
    {
        complete(std::move(*completed));
    }
}

auto ImageEditorSession::start_waiting_operation() -> void
{
    if (!active_operation_ ||
        !active_operation_->waiting_for_draft ||
        !active_operation_->request)
    {
        return;
    }
    const auto draft = draft_worker_.snapshot();
    if (draft.pending || draft.in_flight)
    {
        return;
    }

    auto request = std::move(*active_operation_->request);
    active_operation_->request.reset();
    active_operation_->waiting_for_draft = false;
    const auto started =
        persistence_worker_.start(std::move(request));
    if (!started)
    {
        const auto command = active_operation_->command_id;
        const auto operation = active_operation_->operation;
        active_operation_.reset();
        fail_completion(
            command,
            operation,
            ImageEditorSessionFailure{
                ImageEditorSessionFailureKind::Persistence,
                ImageProjectIoFailure{
                    ImageProjectIoFailureKind::WorkerException,
                },
                std::nullopt,
                std::nullopt,
            });
    }
}

auto ImageEditorSession::observe_ready_project() -> void
{
    const auto editor = pipeline_.snapshot();
    if (editor.phase != ImageEditorPipelinePhase::Ready ||
        editor.generation == scheduled_draft_generation_)
    {
        return;
    }
    const auto project = pipeline_.ready_project(
        editor.project_id,
        editor.project_revision);
    if (!project)
    {
        draft_schedule_error_ =
            ActiveDraftScheduleError::InvalidProject;
        return;
    }
    const auto scheduled = draft_worker_.schedule(project);
    if (!scheduled)
    {
        draft_schedule_error_ = scheduled.error();
        return;
    }
    scheduled_draft_generation_ = editor.generation;
    draft_schedule_error_.reset();
}

auto ImageEditorSession::complete(
    ImageProjectIoCompletion completed) -> void
{
    if (!active_operation_ ||
        completed.command_id !=
            active_operation_->command_id ||
        completed.operation !=
            active_operation_->operation)
    {
        const auto command = active_operation_
                                 ? active_operation_->command_id
                                 : completed.command_id;
        const auto operation = active_operation_
                                   ? active_operation_->operation
                                   : completed.operation;
        active_operation_.reset();
        fail_completion(
            command,
            operation,
            invalid_completion());
        return;
    }

    const auto expected_project_id =
        active_operation_->project_id;
    active_operation_.reset();
    if (!completed.result)
    {
        fail_completion(
            completed.command_id,
            completed.operation,
            ImageEditorSessionFailure{
                ImageEditorSessionFailureKind::Persistence,
                std::move(completed.result.error()),
                std::nullopt,
                std::nullopt,
            });
        return;
    }

    auto success = ImageEditorSessionSuccess{};
    success.project_id = expected_project_id;
    const auto accepted = std::visit(
        [this, &success, &expected_project_id](
            auto&& value)
            -> std::optional<ImageEditorSessionFailure>
        {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<Value, ActivatedImageProject>)
            {
                if (value.project.project_id !=
                    expected_project_id)
                {
                    return invalid_completion();
                }
                success.project_revision =
                    value.project.revision;
                success.config = std::move(value.config);
                auto submitted =
                    pipeline_.replace(
                        std::move(value.project));
                if (!submitted)
                {
                    auto failure = pipeline_failure(
                        submitted.error());
                    failure.published_config =
                        success.config;
                    return failure;
                }
                success.pipeline_generation = *submitted;
            }
            else if constexpr (
                std::is_same_v<Value, SavedImageProject>)
            {
                if (!value.project ||
                    value.project->project_id !=
                        expected_project_id)
                {
                    return invalid_completion();
                }
                success.project_revision =
                    value.project->revision;
                success.config = std::move(value.config);
            }
            else if constexpr (
                std::is_same_v<Value, core::ImageProject>)
            {
                if (value.project_id != expected_project_id)
                {
                    return invalid_completion();
                }
                success.project_revision = value.revision;
                auto submitted =
                    pipeline_.replace(std::move(value));
                if (!submitted)
                {
                    return pipeline_failure(
                        submitted.error());
                }
                success.pipeline_generation = *submitted;
            }
            else
            {
                success.deleted = value.deleted;
                success.config = std::move(value.config);
                const auto editor = pipeline_.snapshot();
                if (editor.project_id == expected_project_id)
                {
                    pipeline_.clear();
                }
            }
            return std::nullopt;
        },
        std::move(*completed.result));

    if (accepted)
    {
        fail_completion(
            completed.command_id,
            completed.operation,
            *accepted);
        return;
    }
    completion_ = ImageEditorSessionCompletion{
        completed.command_id,
        completed.operation,
        std::move(success),
    };
}

auto ImageEditorSession::fail_completion(
    CommandId command_id,
    ImageProjectIoOperation operation,
    ImageEditorSessionFailure failure) -> void
{
    completion_ = ImageEditorSessionCompletion{
        command_id,
        operation,
        std::unexpected(std::move(failure)),
    };
}

auto ImageEditorSession::poll_completion()
    -> std::optional<ImageEditorSessionCompletion>
{
    auto completed = std::move(completion_);
    completion_.reset();
    return completed;
}

auto ImageEditorSession::shutdown(
    bool flush_active_draft) noexcept -> void
{
    if (stopped_)
    {
        return;
    }
    stopped_ = true;
    draft_worker_.shutdown(flush_active_draft);
    if (active_operation_ &&
        active_operation_->waiting_for_draft &&
        active_operation_->request)
    {
        auto request =
            std::move(*active_operation_->request);
        active_operation_->request.reset();
        active_operation_->waiting_for_draft = false;
        static_cast<void>(
            persistence_worker_.start(std::move(request)));
    }
    persistence_worker_.shutdown();
    pipeline_.shutdown();
    active_operation_.reset();
    completion_.reset();
}

auto ImageEditorSession::session_snapshot() const
    -> ImageEditorSessionSnapshot
{
    const auto command =
        active_operation_
            ? std::optional<CommandId>{
                  active_operation_->command_id}
            : completion_
                  ? std::optional<CommandId>{
                        completion_->command_id}
                  : std::nullopt;
    const auto operation =
        active_operation_
            ? std::optional<ImageProjectIoOperation>{
                  active_operation_->operation}
            : completion_
                  ? std::optional<ImageProjectIoOperation>{
                        completion_->operation}
                  : std::nullopt;
    return ImageEditorSessionSnapshot{
        pipeline_.snapshot(),
        command,
        operation,
        completion_.has_value(),
        draft_worker_.snapshot(),
        draft_schedule_error_,
        stopped_,
    };
}

auto ImageEditorSession::snapshot() const
    -> ImageEditorPipelineSnapshot
{
    return pipeline_.snapshot();
}

auto ImageEditorSession::ready_project(
    std::string_view project_id,
    std::uint64_t project_revision) const
    -> std::shared_ptr<const core::ImageProject>
{
    return pipeline_.ready_project(
        project_id,
        project_revision);
}
} // namespace meccha::application
