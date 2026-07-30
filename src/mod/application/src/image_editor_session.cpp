#include <meccha/application/image_editor_session.hpp>

#include <algorithm>
#include <limits>
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

auto mutation_error(ImageEditorSubmitError error)
    -> ImageEditorMutationError
{
    switch (error)
    {
    case ImageEditorSubmitError::InvalidProject:
        return ImageEditorMutationError::InvalidProject;
    case ImageEditorSubmitError::StaleRevision:
        return ImageEditorMutationError::StaleRevision;
    case ImageEditorSubmitError::Stopped:
        return ImageEditorMutationError::Stopped;
    case ImageEditorSubmitError::GenerationOverflow:
        return ImageEditorMutationError::GenerationOverflow;
    case ImageEditorSubmitError::DecodeStart:
        return ImageEditorMutationError::PipelineStart;
    }
    return ImageEditorMutationError::PipelineStart;
}
} // namespace

ImageEditorSession::ImageEditorSession(
    ImageSourceDecoder& decoder,
    ImageAtlasComposer& composer,
    ImageProjectStore& projects,
    ImageProjectPersistenceCoordinator& persistence,
    std::chrono::milliseconds draft_debounce)
    : pipeline_{decoder, composer},
      persistence_{persistence},
      persistence_worker_{projects, persistence},
      draft_worker_{projects, draft_debounce}
{
}

ImageEditorSession::~ImageEditorSession()
{
    shutdown(true);
}

auto ImageEditorSession::recover_startup(
    const core::ApplicationConfig& config)
    -> std::expected<
        ImageEditorStartupSnapshot,
        ImageEditorStartupError>
{
    if (stopped_)
    {
        return std::unexpected(
            ImageEditorStartupError::Stopped);
    }
    if (startup_.attempted)
    {
        return std::unexpected(
            ImageEditorStartupError::AlreadyAttempted);
    }
    if (active_operation_ || completion_ ||
        pipeline_.snapshot().phase !=
            ImageEditorPipelinePhase::Empty)
    {
        return std::unexpected(
            ImageEditorStartupError::Busy);
    }

    startup_.attempted = true;
    auto recovered = ImageProjectRecoveryResult{};
    try
    {
        recovered = persistence_.recover(config);
    }
    catch (...)
    {
        startup_.failure =
            ImageEditorStartupError::PersistenceException;
        return std::unexpected(*startup_.failure);
    }

    startup_.source = recovered.source;
    startup_.diagnostics =
        std::move(recovered.diagnostics);
    if (!recovered.project)
    {
        return startup_;
    }

    auto submitted = admit_project(
        std::move(*recovered.project),
        true);
    if (!submitted)
    {
        startup_.failure =
            ImageEditorStartupError::Pipeline;
        startup_.pipeline_error = submitted.error();
        return std::unexpected(*startup_.failure);
    }
    startup_.pipeline_generation = *submitted;
    return startup_;
}

auto ImageEditorSession::submit_edit(core::ImageProject project)
    -> std::expected<JobGeneration, ImageEditorSubmitError>
{
    if (stopped_)
    {
        return std::unexpected(ImageEditorSubmitError::Stopped);
    }
    return admit_project(std::move(project), false);
}

auto ImageEditorSession::admit_project(
    core::ImageProject project,
    bool replace)
    -> std::expected<JobGeneration, ImageEditorSubmitError>
{
    auto retained =
        std::make_shared<const core::ImageProject>(
            std::move(project));
    auto submitted = replace
                         ? pipeline_.replace(*retained)
                         : pipeline_.submit(*retained);
    if (submitted)
    {
        current_project_ = std::move(retained);
    }
    return submitted;
}

auto ImageEditorSession::mutate(
    std::string_view project_id,
    std::uint64_t expected_revision,
    ImageEditorMutation mutation)
    -> std::expected<JobGeneration, ImageEditorMutationError>
{
    if (stopped_)
    {
        return std::unexpected(
            ImageEditorMutationError::Stopped);
    }
    if (active_operation_ || completion_)
    {
        return std::unexpected(
            ImageEditorMutationError::Busy);
    }
    if (!current_project_ ||
        current_project_->project_id != project_id)
    {
        return std::unexpected(
            ImageEditorMutationError::NoProject);
    }
    if (current_project_->revision != expected_revision)
    {
        return std::unexpected(
            ImageEditorMutationError::StaleRevision);
    }

    auto edited = *current_project_;
    const auto changed = std::visit(
        [&edited](auto&& request)
            -> std::expected<bool, ImageEditorMutationError>
        {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (
                std::is_same_v<Request, ReplaceImageLayerMutation>)
            {
                if (request.layer_index >= edited.layers.size() ||
                    request.expected_asset_id.empty())
                {
                    return std::unexpected(
                        ImageEditorMutationError::InvalidLayer);
                }
                auto& layer = edited.layers[request.layer_index];
                if (layer.asset_id != request.expected_asset_id ||
                    request.layer.asset_id !=
                        request.expected_asset_id)
                {
                    return std::unexpected(
                        ImageEditorMutationError::InvalidLayer);
                }

                auto replacement = layer;
                replacement.center_x = request.layer.center_x;
                replacement.center_y = request.layer.center_y;
                replacement.width = request.layer.width;
                replacement.height = request.layer.height;
                replacement.crop = request.layer.crop;
                replacement.wrap_atlas_seam =
                    request.layer.wrap_atlas_seam;
                replacement.mirror_front_back =
                    request.layer.mirror_front_back;
                if (!core::validate(replacement).empty())
                {
                    return std::unexpected(
                        ImageEditorMutationError::InvalidLayer);
                }
                if (replacement == layer)
                {
                    return false;
                }
                layer = std::move(replacement);
                return true;
            }
            else if constexpr (
                std::is_same_v<Request, ReorderImageLayerMutation>)
            {
                if (request.layer_index >= edited.layers.size() ||
                    request.destination_index >=
                        edited.layers.size() ||
                    request.expected_asset_id.empty() ||
                    edited.layers[request.layer_index].asset_id !=
                        request.expected_asset_id)
                {
                    return std::unexpected(
                        ImageEditorMutationError::InvalidLayer);
                }
                if (request.layer_index ==
                    request.destination_index)
                {
                    return false;
                }

                auto first = edited.layers.begin();
                if (request.layer_index <
                    request.destination_index)
                {
                    std::rotate(
                        first +
                            static_cast<std::ptrdiff_t>(
                                request.layer_index),
                        first +
                            static_cast<std::ptrdiff_t>(
                                request.layer_index + 1U),
                        first +
                            static_cast<std::ptrdiff_t>(
                                request.destination_index + 1U));
                }
                else
                {
                    std::rotate(
                        first +
                            static_cast<std::ptrdiff_t>(
                                request.destination_index),
                        first +
                            static_cast<std::ptrdiff_t>(
                                request.layer_index),
                        first +
                            static_cast<std::ptrdiff_t>(
                                request.layer_index + 1U));
                }
                return true;
            }
            else if constexpr (
                std::is_same_v<Request, RemoveImageLayerMutation>)
            {
                if (edited.layers.size() <= 1U ||
                    request.layer_index >=
                        edited.layers.size() ||
                    request.expected_asset_id.empty() ||
                    edited.layers[request.layer_index].asset_id !=
                        request.expected_asset_id)
                {
                    return std::unexpected(
                        ImageEditorMutationError::InvalidLayer);
                }

                auto still_referenced = false;
                for (auto index = std::size_t{};
                     index < edited.layers.size();
                     ++index)
                {
                    if (index != request.layer_index &&
                        edited.layers[index].asset_id ==
                            request.expected_asset_id)
                    {
                        still_referenced = true;
                        break;
                    }
                }
                auto source = edited.sources.end();
                if (!still_referenced)
                {
                    source = std::ranges::find_if(
                        edited.sources,
                        [&request](
                            const core::ImageSourceAsset& candidate)
                        {
                            return candidate.asset_id ==
                                   request.expected_asset_id;
                        });
                    if (source == edited.sources.end())
                    {
                        return std::unexpected(
                            ImageEditorMutationError::InvalidProject);
                    }
                }

                edited.layers.erase(
                    edited.layers.begin() +
                    static_cast<std::ptrdiff_t>(
                        request.layer_index));
                if (!still_referenced)
                {
                    edited.sources.erase(source);
                }
                if (!core::validate(edited).empty())
                {
                    return std::unexpected(
                        ImageEditorMutationError::InvalidProject);
                }
                return true;
            }
            else
            {
                if (!core::validate(request.settings).empty())
                {
                    return std::unexpected(
                        ImageEditorMutationError::InvalidSettings);
                }
                if (request.settings == edited.settings)
                {
                    return false;
                }
                edited.settings = std::move(request.settings);
                return true;
            }
        },
        std::move(mutation));
    if (!changed)
    {
        return std::unexpected(changed.error());
    }
    if (!*changed)
    {
        return std::unexpected(
            ImageEditorMutationError::NoChange);
    }
    if (edited.revision ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return std::unexpected(
            ImageEditorMutationError::RevisionOverflow);
    }
    ++edited.revision;

    const auto submitted = submit_edit(std::move(edited));
    if (!submitted)
    {
        return std::unexpected(
            mutation_error(submitted.error()));
    }
    return *submitted;
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
    if (current_project_ &&
        current_project_->project_id == project_id)
    {
        const auto editor = pipeline_.snapshot();
        if (editor.phase == ImageEditorPipelinePhase::Decoding ||
            editor.phase == ImageEditorPipelinePhase::Composing ||
            editor.pending)
        {
            return std::unexpected(
                ImageEditorSessionStartError::NotReady);
        }
    }
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
    current_project_ = project;
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
                auto submitted = admit_project(
                    std::move(value.project),
                    true);
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
                auto submitted = admit_project(
                    std::move(value),
                    true);
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
                if (current_project_ &&
                    current_project_->project_id ==
                        expected_project_id)
                {
                    current_project_.reset();
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
    current_project_.reset();
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
    auto document =
        std::optional<ImageEditorDocumentSnapshot>{};
    if (current_project_)
    {
        document = ImageEditorDocumentSnapshot{
            current_project_->project_id,
            current_project_->display_name,
            current_project_->revision,
            current_project_->settings,
            current_project_->layers,
        };
    }
    return ImageEditorSessionSnapshot{
        pipeline_.snapshot(),
        startup_,
        command,
        operation,
        completion_.has_value(),
        draft_worker_.snapshot(),
        draft_schedule_error_,
        std::move(document),
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
