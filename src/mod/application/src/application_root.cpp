#include <meccha/application/application_root.hpp>

#include <chrono>
#include <expected>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>

namespace meccha::application
{
namespace
{
constexpr auto GenericFailureMessage = "error.operation.failed";
constexpr auto CommandBudget = std::size_t{16U};

auto active_job(JobPhase phase) -> bool
{
    return phase == JobPhase::Planning ||
           phase == JobPhase::Dispatching ||
           phase == JobPhase::Cancelling ||
           phase == JobPhase::Draining;
}

auto command_id(const ApplicationCommand& command) -> CommandId
{
    return std::visit(
        [](const auto& request) { return request.id; },
        command);
}

auto monotonic_milliseconds() -> std::uint64_t
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

auto lifecycle_failure(RuntimeLifecycleError error)
    -> CompatibilityFailure
{
    switch (error)
    {
    case RuntimeLifecycleError::InvalidFrameIdentity:
        return CompatibilityFailure{
            RuntimeContractId::Canvas,
            ContractFailureKind::StaleObject,
            GenericFailureMessage,
        };
    case RuntimeLifecycleError::CallbackRegistration:
    case RuntimeLifecycleError::CallbackUnregistration:
        return CompatibilityFailure{
            RuntimeContractId::HudCallback,
            ContractFailureKind::CallbackFailure,
            GenericFailureMessage,
        };
    case RuntimeLifecycleError::WrongThread:
        return CompatibilityFailure{
            RuntimeContractId::HudCallback,
            ContractFailureKind::ExecutionFailure,
            GenericFailureMessage,
        };
    case RuntimeLifecycleError::ExecutionFailed:
        return CompatibilityFailure{
            RuntimeContractId::RuntimeInitialization,
            ContractFailureKind::ExecutionFailure,
            GenericFailureMessage,
        };
    case RuntimeLifecycleError::InvalidState:
    case RuntimeLifecycleError::PendingGameThreadRestore:
        return CompatibilityFailure{
            RuntimeContractId::RuntimeInitialization,
            ContractFailureKind::ExecutionFailure,
            GenericFailureMessage,
        };
    }
    return CompatibilityFailure{
        RuntimeContractId::RuntimeInitialization,
        ContractFailureKind::ExecutionFailure,
        GenericFailureMessage,
    };
}
} // namespace

ApplicationRoot::ApplicationRoot(
    RuntimeCallbackPort& callbacks,
    GameThreadExecutor& executor,
    AtomicTextStorage& config_storage,
    std::size_t queue_capacity,
    std::size_t diagnostic_capacity)
    : config_store_{config_storage},
      diagnostics_{diagnostic_capacity},
      lifecycle_{callbacks, executor, queue_capacity, this},
      paint_planner_{paint_plan_builder_},
      image_paint_planner_{image_paint_plan_builder_},
      paint_preview_builder_{paint_preview_plan_builder_},
      paint_dispatcher_{lifecycle_, jobs_},
      paint_jobs_{jobs_, paint_planner_, paint_dispatcher_},
      image_paint_jobs_{
          jobs_,
          image_paint_planner_,
          paint_dispatcher_}
{
}

ApplicationRoot::ApplicationRoot(
    RuntimeCallbackPort& callbacks,
    GameThreadExecutor& executor,
    AtomicTextStorage& config_storage,
    PaintGameRuntimePort& paint_runtime,
    GameThreadContext& game_thread_context,
    PaintPreviewRuntimePort& paint_preview_runtime,
    std::size_t queue_capacity,
    std::size_t command_capacity,
    std::size_t diagnostic_capacity)
    : ApplicationRoot{
          callbacks,
          executor,
          config_storage,
          queue_capacity,
          diagnostic_capacity}
{
    paint_runtime_ = &paint_runtime;
    command_queue_ =
        std::make_unique<ApplicationCommandQueue>(
            command_capacity);
    paint_previews_ = std::make_unique<PaintPreviewController>(
        game_thread_context,
        paint_preview_runtime,
        preview_);
    const auto lock = std::scoped_lock{state_mutex_};
    publish_locked();
}

ApplicationRoot::ApplicationRoot(
    RuntimeCallbackPort& callbacks,
    GameThreadExecutor& executor,
    AtomicTextStorage& config_storage,
    PaintGameRuntimePort& paint_runtime,
    GameThreadContext& game_thread_context,
    PaintPreviewRuntimePort& paint_preview_runtime,
    ImagePaintGameRuntimePort& image_runtime,
    ImageProjectReadinessPort& image_projects,
    std::size_t queue_capacity,
    std::size_t command_capacity,
    std::size_t diagnostic_capacity)
    : ApplicationRoot{
          callbacks,
          executor,
          config_storage,
          paint_runtime,
          game_thread_context,
          paint_preview_runtime,
          queue_capacity,
          command_capacity,
          diagnostic_capacity}
{
    image_paint_runtime_ = &image_runtime;
    image_projects_ = &image_projects;
}

auto ApplicationRoot::initialize()
    -> std::expected<void, ApplicationRootError>
{
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (phase_ != ApplicationRuntimePhase::Cold)
        {
            return std::unexpected(
                ApplicationRootError::InvalidState);
        }
        phase_ = ApplicationRuntimePhase::Initializing;
        publish_locked();
    }

    auto loaded = config_store_.load();
    if (!loaded)
    {
        const auto lock = std::scoped_lock{state_mutex_};
        fail_locked(CompatibilityFailure{
            RuntimeContractId::RuntimeInitialization,
            ContractFailureKind::ExecutionFailure,
            GenericFailureMessage,
        });
        publish_locked();
        return std::unexpected(
            ApplicationRootError::ConfigurationLoad);
    }
    {
        const auto lock = std::scoped_lock{state_mutex_};
        settings_ = std::move(loaded->config);
        publish_locked();
    }

    const auto initialized = lifecycle_.initialize();
    if (!initialized)
    {
        const auto lock = std::scoped_lock{state_mutex_};
        fail_locked(lifecycle_failure(initialized.error()));
        publish_locked();
        return std::unexpected(
            ApplicationRootError::RuntimeInitialization);
    }

    {
        const auto lock = std::scoped_lock{state_mutex_};
        runtime_initialized_ = true;
        publish_locked();
    }
    return {};
}

auto ApplicationRoot::on_update() noexcept -> void
{
    lifecycle_.on_update();
}

auto ApplicationRoot::enqueue_command(ApplicationCommand command)
    -> CommandEnqueueResult
{
    if (!command_queue_)
    {
        return CommandEnqueueResult::Closed;
    }
    const auto result =
        command_queue_->enqueue(std::move(command));
    if (result == CommandEnqueueResult::Accepted)
    {
        const auto lock = std::scoped_lock{state_mutex_};
        publish_locked();
    }
    return result;
}

auto ApplicationRoot::request_shutdown(
    std::uint64_t shutdown_generation)
    -> std::expected<void, ApplicationRootError>
{
    auto initialized = false;
    auto defer_for_preview = false;
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (phase_ != ApplicationRuntimePhase::Initializing &&
            phase_ != ApplicationRuntimePhase::Compatible &&
            phase_ != ApplicationRuntimePhase::Incompatible)
        {
            return std::unexpected(
                ApplicationRootError::InvalidState);
        }
        initialized = runtime_initialized_;
        defer_for_preview = paint_previews_ != nullptr;
    }

    if (initialized && !defer_for_preview)
    {
        const auto requested =
            lifecycle_.request_shutdown(shutdown_generation);
        if (!requested)
        {
            return std::unexpected(
                ApplicationRootError::RuntimeShutdown);
        }
    }

    if (command_queue_)
    {
        command_queue_->close();
        static_cast<void>(command_queue_->discard());
    }
    const auto preview_generation =
        active_paint_preview_generation_.load(
            std::memory_order_acquire);
    if (preview_generation != 0U)
    {
        static_cast<void>(
            paint_preview_builder_.request_cancel(
                preview_generation));
    }

    const auto lock = std::scoped_lock{state_mutex_};
    phase_ = ApplicationRuntimePhase::ShuttingDown;
    pending_shutdown_generation_ = shutdown_generation;
    lifecycle_shutdown_requested_ =
        initialized && !defer_for_preview;
    publish_locked();
    return {};
}

auto ApplicationRoot::finalize_shutdown()
    -> std::expected<void, ApplicationRootError>
{
    auto initialized = false;
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (phase_ != ApplicationRuntimePhase::ShuttingDown)
        {
            return std::unexpected(
                ApplicationRootError::InvalidState);
        }
        initialized = runtime_initialized_;
        if (initialized && paint_previews_ &&
            !lifecycle_shutdown_requested_)
        {
            return std::unexpected(
                ApplicationRootError::PendingGameThreadRestore);
        }
    }

    if (initialized)
    {
        const auto finalized = lifecycle_.finalize_shutdown();
        if (!finalized)
        {
            if (finalized.error() ==
                RuntimeLifecycleError::PendingGameThreadRestore)
            {
                return std::unexpected(
                    ApplicationRootError::PendingGameThreadRestore);
            }
            return std::unexpected(
                ApplicationRootError::RuntimeShutdown);
        }
    }

    const auto lock = std::scoped_lock{state_mutex_};
    runtime_initialized_ = false;
    phase_ = ApplicationRuntimePhase::Stopped;
    publish_locked();
    return {};
}

auto ApplicationRoot::snapshot() const
    -> std::shared_ptr<const ApplicationSnapshot>
{
    return snapshots_.read();
}

auto ApplicationRoot::on_hud_frame_complete(
    const std::expected<std::size_t, RuntimeLifecycleError>& result,
    const RuntimeLifecycleSnapshot& runtime_snapshot) noexcept -> void
{
    try
    {
        auto advance = false;
        auto advance_root_shutdown = false;
        if (!result)
        {
            const auto lock = std::scoped_lock{state_mutex_};
            fail_locked(
                runtime_snapshot.last_compatibility_failure
                    ? *runtime_snapshot.last_compatibility_failure
                    : lifecycle_failure(result.error()));
        }
        else if (
            runtime_snapshot.frame_identity)
        {
            const auto lock = std::scoped_lock{state_mutex_};
            if (phase_ == ApplicationRuntimePhase::ShuttingDown)
            {
                advance_root_shutdown =
                    runtime_snapshot.phase ==
                        RuntimePhase::Running &&
                    !lifecycle_shutdown_requested_;
            }
            else if (
                phase_ != ApplicationRuntimePhase::Stopped)
            {
                compatibility_.mark_compatible();
                phase_ = ApplicationRuntimePhase::Compatible;
                advance =
                    paint_runtime_ != nullptr &&
                    command_queue_ != nullptr;
            }
        }
        if (advance)
        {
            process_commands(monotonic_milliseconds());
        }
        if (advance_root_shutdown)
        {
            advance_shutdown(monotonic_milliseconds());
        }
        const auto lock = std::scoped_lock{state_mutex_};
        publish_locked(runtime_snapshot);
    }
    catch (...)
    {
    }
}

auto ApplicationRoot::record_runtime_error(
    const RuntimeExecutionError& error,
    std::optional<CommandId> command_id) -> void
{
    const auto lock = std::scoped_lock{state_mutex_};
    if (error.compatibility_failure)
    {
        fail_locked(*error.compatibility_failure);
        return;
    }
    diagnostics_.push(
        DiagnosticSeverity::Error,
        GenericFailureMessage,
        command_id);
}

auto ApplicationRoot::record_preview_error(
    const PaintPreviewError& error,
    std::optional<CommandId> command_id) -> void
{
    auto recorded = false;
    if (error.runtime_error)
    {
        record_runtime_error(*error.runtime_error, command_id);
        recorded = true;
    }
    if (error.recovery_error)
    {
        record_runtime_error(*error.recovery_error, command_id);
        recorded = true;
    }
    if (!recorded)
    {
        const auto lock = std::scoped_lock{state_mutex_};
        diagnostics_.push(
            DiagnosticSeverity::Warning,
            GenericFailureMessage,
            command_id);
    }
}

auto ApplicationRoot::record_command_error(
    CommandId command_id) -> void
{
    const auto lock = std::scoped_lock{state_mutex_};
    diagnostics_.push(
        DiagnosticSeverity::Warning,
        GenericFailureMessage,
        command_id);
}

auto ApplicationRoot::process_commands(std::uint64_t now_ms) noexcept
    -> void
{
    try
    {
        for (auto& command :
             command_queue_->drain(CommandBudget))
        {
            process_command(std::move(command), now_ms);
        }
        advance_paint(now_ms);
        advance_image_paint(now_ms);
        advance_paint_preview(now_ms);
    }
    catch (...)
    {
        const auto lock = std::scoped_lock{state_mutex_};
        diagnostics_.push(
            DiagnosticSeverity::Error,
            GenericFailureMessage);
    }
}

auto ApplicationRoot::process_command(
    ApplicationCommand command,
    std::uint64_t now_ms) -> void
{
    std::visit(
        [this, now_ms](auto&& request) {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<Request, StartPaint>)
            {
                begin_paint(std::move(request), now_ms);
            }
            else if constexpr (
                std::is_same_v<Request, StartImagePaint>)
            {
                begin_image_paint(std::move(request), now_ms);
            }
            else if constexpr (
                std::is_same_v<Request, PreviewPaint>)
            {
                begin_paint_preview(std::move(request));
            }
            else if constexpr (std::is_same_v<Request, CancelPaint>)
            {
                const auto job = jobs_.snapshot();
                if (!job.feature ||
                    *job.feature != Feature::Paint ||
                    !active_paint_component_)
                {
                    record_command_error(request.id);
                    return;
                }
                auto observation = PaintQueueObservation{};
                if (paint_jobs_.requires_queue_observation())
                {
                    const auto observed =
                        paint_runtime_->observe_queues(
                            *active_paint_component_,
                            job.generation);
                    if (!observed)
                    {
                        record_runtime_error(
                            observed.error(),
                            request.id);
                        return;
                    }
                    observation = *observed;
                }
                if (!paint_jobs_.request_cancel(
                        job.generation,
                        now_ms,
                        observation))
                {
                    record_command_error(request.id);
                }
            }
            else if constexpr (
                std::is_same_v<Request, RestorePaintPreview>)
            {
                restore_paint_preview(std::move(request));
            }
            else if constexpr (
                std::is_same_v<Request, CancelImagePaint>)
            {
                const auto job = jobs_.snapshot();
                if (!job.feature ||
                    *job.feature != Feature::ImagePaint ||
                    !active_image_paint_component_ ||
                    !image_paint_runtime_)
                {
                    record_command_error(request.id);
                    return;
                }
                auto observation = PaintQueueObservation{};
                if (image_paint_jobs_.requires_queue_observation())
                {
                    const auto observed =
                        image_paint_runtime_->observe_queues(
                            *active_image_paint_component_,
                            job.generation);
                    if (!observed)
                    {
                        record_runtime_error(
                            observed.error(),
                            request.id);
                        return;
                    }
                    observation = *observed;
                }
                if (!image_paint_jobs_.request_cancel(
                        job.generation,
                        now_ms,
                        observation))
                {
                    record_command_error(request.id);
                }
            }
            else if constexpr (std::is_same_v<Request, ToggleUi>)
            {
                const auto lock = std::scoped_lock{state_mutex_};
                ui_open_ = !ui_open_;
            }
            else if constexpr (std::is_same_v<Request, ToggleEsp>)
            {
                const auto lock = std::scoped_lock{state_mutex_};
                esp_enabled_ = !esp_enabled_;
            }
            else if constexpr (
                std::is_same_v<Request, ApplyValidatedSettings>)
            {
                if (!core::validate(request.settings).empty() ||
                    !config_store_.save(request.settings))
                {
                    record_command_error(request.id);
                    return;
                }
                const auto lock = std::scoped_lock{state_mutex_};
                settings_ = std::move(request.settings);
            }
            else
            {
                record_command_error(request.id);
            }
        },
        std::move(command));
}

auto ApplicationRoot::begin_paint(
    StartPaint request,
    std::uint64_t now_ms) -> void
{
    if (active_paint_preview_build_)
    {
        defer_for_paint_preview(
            ApplicationCommand{std::move(request)});
        return;
    }
    if (active_job(jobs_.snapshot().phase))
    {
        record_command_error(request.id);
        return;
    }
    if (paint_previews_ && preview_.snapshot().feature)
    {
        const auto restored =
            paint_previews_->restore_active();
        if (!restored)
        {
            record_preview_error(
                restored.error(),
                request.id);
            return;
        }
    }

    auto captured =
        paint_runtime_->capture(request.settings);
    if (!captured)
    {
        record_runtime_error(
            captured.error(),
            request.id);
        return;
    }
    if (!captured->component.valid() ||
        captured->plan.settings != request.settings)
    {
        record_command_error(request.id);
        return;
    }
    const auto component = captured->component;
    const auto started = paint_jobs_.start(
        request.id,
        component,
        std::move(captured->plan),
        captured->pacing,
        now_ms);
    if (!started)
    {
        record_command_error(request.id);
        return;
    }
    active_paint_component_ = component;
    paint_shutdown_cancel_requested_ = false;
}

auto ApplicationRoot::begin_image_paint(
    StartImagePaint request,
    std::uint64_t now_ms) -> void
{
    if (active_paint_preview_build_)
    {
        defer_for_paint_preview(
            ApplicationCommand{std::move(request)});
        return;
    }
    if (active_job(jobs_.snapshot().phase) ||
        !image_paint_runtime_ || !image_projects_)
    {
        record_command_error(request.id);
        return;
    }
    if (paint_previews_ && preview_.snapshot().feature)
    {
        const auto restored =
            paint_previews_->restore_active();
        if (!restored)
        {
            record_preview_error(
                restored.error(),
                request.id);
            return;
        }
    }

    const auto project = image_projects_->ready_project(
        request.project_id,
        request.project_revision);
    if (!project)
    {
        record_command_error(request.id);
        return;
    }

    auto captured =
        image_paint_runtime_->capture(project->settings.body);
    if (!captured)
    {
        record_runtime_error(
            captured.error(),
            request.id);
        return;
    }
    if (!captured->component.valid() ||
        captured->raw_profile.body !=
            project->settings.body ||
        captured->image_profile.geometry.identity.body !=
            project->settings.body)
    {
        record_command_error(request.id);
        return;
    }

    const auto component = captured->component;
    auto plan = core::ImagePaintPlanRequest{
        std::move(captured->raw_profile),
        std::move(captured->image_profile),
        project->settings,
        project->canonical_atlas,
        std::move(captured->samples),
    };
    const auto started = image_paint_jobs_.start(
        request.id,
        std::move(request.project_id),
        request.project_revision,
        component,
        std::move(plan),
        captured->pacing,
        now_ms);
    if (!started)
    {
        record_command_error(request.id);
        return;
    }
    active_image_paint_component_ = component;
    paint_shutdown_cancel_requested_ = false;
}

auto ApplicationRoot::begin_paint_preview(
    PreviewPaint request) -> void
{
    if (active_job(jobs_.snapshot().phase) ||
        !paint_previews_)
    {
        record_command_error(request.id);
        return;
    }
    if (active_paint_preview_build_)
    {
        defer_for_paint_preview(
            ApplicationCommand{std::move(request)});
        return;
    }
    if (preview_.snapshot().feature)
    {
        const auto restored =
            paint_previews_->restore_active();
        if (!restored)
        {
            record_preview_error(
                restored.error(),
                request.id);
            return;
        }
    }

    auto captured =
        paint_runtime_->capture(request.settings);
    if (!captured)
    {
        record_runtime_error(
            captured.error(),
            request.id);
        return;
    }
    if (!captured->component.valid() ||
        captured->plan.settings != request.settings)
    {
        record_command_error(request.id);
        return;
    }

    const auto component = captured->component;
    const auto acquired =
        paint_previews_->begin(Feature::Paint, component);
    if (!acquired)
    {
        record_preview_error(
            acquired.error(),
            request.id);
        return;
    }
    auto original =
        paint_previews_->source(Feature::Paint, component);
    if (!original)
    {
        record_preview_error(
            original.error(),
            request.id);
        static_cast<void>(
            paint_previews_->restore_active());
        return;
    }
    if (paint_preview_generation_ ==
        std::numeric_limits<JobGeneration>::max())
    {
        record_command_error(request.id);
        static_cast<void>(
            paint_previews_->restore_active());
        return;
    }

    const auto generation = ++paint_preview_generation_;
    const auto started = paint_preview_builder_.start(
        generation,
        PaintPreviewBuildRequest{
            std::move(captured->plan),
            std::move(*original),
        });
    if (!started)
    {
        record_command_error(request.id);
        static_cast<void>(
            paint_previews_->restore_active());
        return;
    }
    active_paint_preview_build_ = ActivePaintPreviewBuild{
        generation,
        request.id,
        component,
    };
    active_paint_preview_generation_.store(
        generation,
        std::memory_order_release);
}

auto ApplicationRoot::restore_paint_preview(
    RestorePaintPreview request) -> void
{
    if (!paint_previews_)
    {
        record_command_error(request.id);
        return;
    }
    if (active_paint_preview_build_)
    {
        defer_for_paint_preview(
            ApplicationCommand{std::move(request)});
        return;
    }

    const auto restored = paint_previews_->restore_active();
    if (!restored)
    {
        record_preview_error(
            restored.error(),
            request.id);
    }
    else if (*restored == PaintPreviewRestore::NoPreview)
    {
        record_command_error(request.id);
    }
}

auto ApplicationRoot::defer_for_paint_preview(
    ApplicationCommand command) -> void
{
    if (!active_paint_preview_build_)
    {
        record_command_error(command_id(command));
        return;
    }
    deferred_paint_preview_command_ = std::move(command);
    const auto cancelled = paint_preview_builder_.request_cancel(
        active_paint_preview_build_->generation);
    if (cancelled ==
            PaintPreviewBuildCancelResult::StaleGeneration ||
        cancelled == PaintPreviewBuildCancelResult::Idle)
    {
        const auto deferred_id =
            command_id(*deferred_paint_preview_command_);
        deferred_paint_preview_command_.reset();
        active_paint_preview_build_.reset();
        active_paint_preview_generation_.store(
            0U,
            std::memory_order_release);
        const auto restored =
            paint_previews_->restore_active();
        if (!restored)
        {
            record_preview_error(
                restored.error(),
                deferred_id);
        }
        record_command_error(deferred_id);
    }
}

auto ApplicationRoot::advance_paint(std::uint64_t now_ms) -> void
{
    const auto job = jobs_.snapshot();
    if (!job.feature || *job.feature != Feature::Paint ||
        (job.phase != JobPhase::Planning &&
         job.phase != JobPhase::Dispatching &&
         job.phase != JobPhase::Cancelling &&
         job.phase != JobPhase::Draining))
    {
        return;
    }

    auto observation = PaintQueueObservation{};
    if (paint_jobs_.requires_queue_observation())
    {
        if (!active_paint_component_)
        {
            record_command_error(job.command_id.value_or(0U));
            return;
        }
        const auto observed = paint_runtime_->observe_queues(
            *active_paint_component_,
            job.generation);
        if (!observed)
        {
            record_runtime_error(
                observed.error(),
                job.command_id);
            return;
        }
        observation = *observed;
    }

    const auto ticked =
        paint_jobs_.tick(now_ms, observation);
    if (!ticked)
    {
        record_command_error(job.command_id.value_or(0U));
        if (jobs_.snapshot().phase == JobPhase::Failed)
        {
            active_paint_component_.reset();
        }
        return;
    }
    if (ticked->phase == JobPhase::Completed ||
        ticked->phase == JobPhase::Cancelled ||
        ticked->phase == JobPhase::Failed)
    {
        active_paint_component_.reset();
        paint_shutdown_cancel_requested_ = false;
    }
}

auto ApplicationRoot::advance_image_paint(
    std::uint64_t now_ms) -> void
{
    const auto job = jobs_.snapshot();
    if (!job.feature ||
        *job.feature != Feature::ImagePaint ||
        !active_job(job.phase))
    {
        return;
    }
    if (!image_paint_runtime_ || !image_projects_ ||
        !active_image_paint_component_)
    {
        record_command_error(job.command_id.value_or(0U));
        return;
    }

    auto observation = PaintQueueObservation{};
    if (image_paint_jobs_.requires_queue_observation())
    {
        const auto observed =
            image_paint_runtime_->observe_queues(
                *active_image_paint_component_,
                job.generation);
        if (!observed)
        {
            record_runtime_error(
                observed.error(),
                job.command_id);
            return;
        }
        observation = *observed;
    }

    const auto project = image_projects_->snapshot();
    const auto ticked = image_paint_jobs_.tick(
        now_ms,
        observation,
        project.project_id,
        project.project_revision);
    if (!ticked)
    {
        record_command_error(job.command_id.value_or(0U));
        if (jobs_.snapshot().phase == JobPhase::Failed ||
            !active_job(jobs_.snapshot().phase))
        {
            active_image_paint_component_.reset();
        }
        return;
    }
    if (ticked->phase == JobPhase::Completed ||
        ticked->phase == JobPhase::Cancelled ||
        ticked->phase == JobPhase::Failed)
    {
        active_image_paint_component_.reset();
        paint_shutdown_cancel_requested_ = false;
    }
}

auto ApplicationRoot::advance_paint_preview(
    std::uint64_t now_ms) -> void
{
    auto completed = paint_preview_builder_.poll();
    if (!completed)
    {
        return;
    }
    if (!active_paint_preview_build_ ||
        completed->generation !=
            active_paint_preview_build_->generation)
    {
        const auto id = active_paint_preview_build_
                            ? active_paint_preview_build_->command_id
                            : CommandId{};
        active_paint_preview_build_.reset();
        active_paint_preview_generation_.store(
            0U,
            std::memory_order_release);
        const auto restored =
            paint_previews_->restore_active();
        if (!restored && id != 0U)
        {
            record_preview_error(restored.error(), id);
        }
        record_command_error(id);
        return;
    }

    const auto active = *active_paint_preview_build_;
    active_paint_preview_build_.reset();
    active_paint_preview_generation_.store(
        0U,
        std::memory_order_release);
    if (deferred_paint_preview_command_)
    {
        auto deferred =
            std::move(*deferred_paint_preview_command_);
        deferred_paint_preview_command_.reset();
        const auto restored =
            paint_previews_->restore_active();
        if (!restored)
        {
            record_preview_error(
                restored.error(),
                command_id(deferred));
            return;
        }
        process_command(std::move(deferred), now_ms);
        return;
    }

    if (!completed->result)
    {
        record_command_error(active.command_id);
        const auto restored =
            paint_previews_->restore_active();
        if (!restored)
        {
            record_preview_error(
                restored.error(),
                active.command_id);
        }
        return;
    }
    const auto applied = paint_previews_->apply(
        Feature::Paint,
        active.component,
        **completed->result);
    if (!applied)
    {
        record_preview_error(
            applied.error(),
            active.command_id);
    }
}

auto ApplicationRoot::advance_shutdown(
    std::uint64_t now_ms) -> void
{
    const auto job = jobs_.snapshot();
    if (active_job(job.phase))
    {
        const auto image =
            job.feature == Feature::ImagePaint;
        const auto paint =
            job.feature == Feature::Paint;
        if ((!paint && !image) ||
            (paint &&
             (!paint_runtime_ || !active_paint_component_)) ||
            (image &&
             (!image_paint_runtime_ || !image_projects_ ||
              !active_image_paint_component_)))
        {
            record_command_error(
                job.command_id.value_or(0U));
            return;
        }

        auto observation = PaintQueueObservation{};
        const auto requires_observation =
            paint
                ? paint_jobs_.requires_queue_observation()
                : image_paint_jobs_.requires_queue_observation();
        if (requires_observation)
        {
            const auto observed =
                paint
                    ? paint_runtime_->observe_queues(
                          *active_paint_component_,
                          job.generation)
                    : image_paint_runtime_->observe_queues(
                          *active_image_paint_component_,
                          job.generation);
            if (!observed)
            {
                record_runtime_error(
                    observed.error(),
                    job.command_id);
                return;
            }
            observation = *observed;
        }

        if (!paint_shutdown_cancel_requested_)
        {
            const auto cancelled =
                paint
                    ? static_cast<bool>(
                          paint_jobs_.request_cancel(
                              job.generation,
                              now_ms,
                              observation))
                    : static_cast<bool>(
                          image_paint_jobs_.request_cancel(
                              job.generation,
                              now_ms,
                              observation));
            if (!cancelled)
            {
                record_command_error(
                    job.command_id.value_or(0U));
                if (active_job(jobs_.snapshot().phase))
                {
                    return;
                }
            }
            else
            {
                paint_shutdown_cancel_requested_ = true;
            }
        }
        else
        {
            auto ticked = false;
            if (paint)
            {
                ticked = static_cast<bool>(
                    paint_jobs_.tick(now_ms, observation));
            }
            else
            {
                const auto project =
                    image_projects_->snapshot();
                ticked = static_cast<bool>(
                    image_paint_jobs_.tick(
                        now_ms,
                        observation,
                        project.project_id,
                        project.project_revision));
            }
            if (!ticked)
            {
                record_command_error(
                    job.command_id.value_or(0U));
                if (active_job(jobs_.snapshot().phase))
                {
                    return;
                }
            }
        }

        if (active_job(jobs_.snapshot().phase))
        {
            return;
        }
        if (paint)
        {
            active_paint_component_.reset();
        }
        else
        {
            active_image_paint_component_.reset();
        }
        paint_shutdown_cancel_requested_ = false;
    }

    if (active_paint_preview_generation_.load(
            std::memory_order_acquire) != 0U)
    {
        auto completed = paint_preview_builder_.poll();
        if (!completed)
        {
            return;
        }
        active_paint_preview_build_.reset();
        active_paint_preview_generation_.store(
            0U,
            std::memory_order_release);
    }
    deferred_paint_preview_command_.reset();

    if (paint_previews_ && preview_.snapshot().feature)
    {
        const auto restored =
            paint_previews_->restore_active();
        if (!restored)
        {
            record_preview_error(
                restored.error(),
                std::nullopt);
            return;
        }
    }

    auto shutdown_generation = std::uint64_t{};
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (!pending_shutdown_generation_ ||
            lifecycle_shutdown_requested_)
        {
            return;
        }
        shutdown_generation = *pending_shutdown_generation_;
    }
    const auto requested =
        lifecycle_.request_shutdown(shutdown_generation);
    if (!requested)
    {
        const auto lock = std::scoped_lock{state_mutex_};
        fail_locked(lifecycle_failure(requested.error()));
        return;
    }
    const auto lock = std::scoped_lock{state_mutex_};
    lifecycle_shutdown_requested_ = true;
}

auto ApplicationRoot::fail_locked(CompatibilityFailure failure) -> void
{
    if (phase_ != ApplicationRuntimePhase::ShuttingDown)
    {
        phase_ = ApplicationRuntimePhase::Incompatible;
    }
    compatibility_.fail(failure);
    diagnostics_.push(
        DiagnosticSeverity::Error,
        failure.message_key,
        std::nullopt,
        std::move(failure));
}

auto ApplicationRoot::publish_locked(
    const RuntimeLifecycleSnapshot& runtime_snapshot) -> void
{
    auto snapshot = ApplicationSnapshot{};
    snapshot.runtime_phase = phase_;
    snapshot.ui_open = ui_open_;
    snapshot.esp_enabled = esp_enabled_;
    snapshot.settings = settings_;
    snapshot.image_editor =
        image_projects_
            ? image_projects_->snapshot()
            : ImageEditorPipelineSnapshot{};
    snapshot.job = jobs_.snapshot();
    snapshot.preview = preview_.snapshot();
    snapshot.command_queue =
        command_queue_
            ? command_queue_->snapshot()
            : CommandQueueSnapshot{};
    snapshot.runtime_queue = runtime_snapshot.queue;
    snapshot.frame_identity = runtime_snapshot.frame_identity;
    snapshot.compatibility = compatibility_.snapshot();
    snapshot.diagnostics = diagnostics_.entries();
    snapshots_.publish(std::move(snapshot));
}

auto ApplicationRoot::publish_locked() -> void
{
    publish_locked(lifecycle_.snapshot());
}
} // namespace meccha::application
