#include <meccha/application/application_root.hpp>

#include <chrono>
#include <expected>
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
      paint_dispatcher_{lifecycle_, jobs_},
      paint_jobs_{jobs_, paint_planner_, paint_dispatcher_}
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
    }

    if (initialized)
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

    const auto lock = std::scoped_lock{state_mutex_};
    phase_ = ApplicationRuntimePhase::ShuttingDown;
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
        if (!result)
        {
            const auto lock = std::scoped_lock{state_mutex_};
            fail_locked(
                runtime_snapshot.last_compatibility_failure
                    ? *runtime_snapshot.last_compatibility_failure
                    : lifecycle_failure(result.error()));
        }
        else if (
            runtime_snapshot.frame_identity &&
            phase_ != ApplicationRuntimePhase::ShuttingDown &&
            phase_ != ApplicationRuntimePhase::Stopped)
        {
            const auto lock = std::scoped_lock{state_mutex_};
            compatibility_.mark_compatible();
            phase_ = ApplicationRuntimePhase::Compatible;
            advance =
                paint_runtime_ != nullptr &&
                command_queue_ != nullptr;
        }
        if (advance)
        {
            process_commands(monotonic_milliseconds());
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
                if (paint_previews_ &&
                    preview_.snapshot().feature)
                {
                    const auto restored =
                        paint_previews_->restore_active();
                    if (!restored)
                    {
                        record_command_error(request.id);
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
                const auto started = paint_jobs_.start(
                    request.id,
                    captured->component,
                    std::move(captured->plan),
                    captured->pacing,
                    now_ms);
                if (!started)
                {
                    record_command_error(request.id);
                    return;
                }
                active_paint_component_ = captured->component;
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
                if (!paint_previews_ ||
                    !paint_previews_->restore_active())
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
    }
}

auto ApplicationRoot::fail_locked(CompatibilityFailure failure) -> void
{
    phase_ = ApplicationRuntimePhase::Incompatible;
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
