#include <meccha/application/application_root.hpp>

#include <expected>
#include <mutex>
#include <utility>

namespace meccha::application
{
namespace
{
constexpr auto GenericFailureMessage = "error.operation.failed";

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
      lifecycle_{callbacks, executor, queue_capacity, this}
{
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
        const auto lock = std::scoped_lock{state_mutex_};
        if (!result)
        {
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
            compatibility_.mark_compatible();
            phase_ = ApplicationRuntimePhase::Compatible;
        }
        publish_locked(runtime_snapshot);
    }
    catch (...)
    {
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
    snapshot.settings = settings_;
    snapshot.job = jobs_.snapshot();
    snapshot.preview = preview_.snapshot();
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
