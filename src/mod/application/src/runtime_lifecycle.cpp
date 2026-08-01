#include <meccha/application/runtime_lifecycle.hpp>

#include <expected>
#include <mutex>
#include <utility>

namespace meccha::application
{
namespace
{
auto map_execution_error(const RuntimeExecutionError& error)
    -> RuntimeLifecycleError
{
    return error.code == RuntimeExecutionErrorCode::WrongThread
               ? RuntimeLifecycleError::WrongThread
               : RuntimeLifecycleError::ExecutionFailed;
}
} // namespace

RuntimeLifecycle::RuntimeLifecycle(
    RuntimeCallbackPort& callbacks,
    GameThreadExecutor& executor,
    std::size_t queue_capacity,
    RuntimeLifecycleObserver* observer)
    : callbacks_{callbacks},
      executor_{executor},
      scheduler_{queue_capacity},
      observer_{observer}
{
}

RuntimeLifecycle::~RuntimeLifecycle()
{
    callback_barrier_.begin_close();
    auto callback = std::optional<CallbackId>{};
    {
        const auto lock = std::scoped_lock{state_mutex_};
        callback = hud_callback_;
        hud_callback_.reset();
    }
    if (callback)
    {
        static_cast<void>(
            callbacks_.unregister_hud_callback(*callback));
    }
    callback_barrier_.wait_for_idle();
}

auto RuntimeLifecycle::initialize()
    -> std::expected<void, RuntimeLifecycleError>
{
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (phase_ != RuntimePhase::Cold)
        {
            return std::unexpected(RuntimeLifecycleError::InvalidState);
        }
    }

    const auto callback = callbacks_.register_hud_callback(
        this,
        &RuntimeLifecycle::hud_trampoline);
    if (!callback)
    {
        const auto lock = std::scoped_lock{state_mutex_};
        phase_ = RuntimePhase::Failed;
        last_error_ = RuntimeLifecycleError::CallbackRegistration;
        return std::unexpected(
            RuntimeLifecycleError::CallbackRegistration);
    }

    const auto lock = std::scoped_lock{state_mutex_};
    hud_callback_ = *callback;
    phase_ = RuntimePhase::Running;
    return {};
}

auto RuntimeLifecycle::on_update() noexcept -> void
{
    update_ticks_.fetch_add(1U, std::memory_order_relaxed);
}

auto RuntimeLifecycle::on_hud_frame(
    const HudFrameIdentity& identity,
    std::size_t operation_budget)
    -> std::expected<std::size_t, RuntimeLifecycleError>
{
    auto lease = callback_barrier_.try_enter();
    if (!lease)
    {
        return std::size_t{};
    }
    return on_hud_frame_admitted(identity, operation_budget);
}

auto RuntimeLifecycle::on_hud_frame_admitted(
    const HudFrameIdentity& identity,
    std::size_t operation_budget)
    -> std::expected<std::size_t, RuntimeLifecycleError>
{
    if (!identity.valid())
    {
        remember_error(RuntimeLifecycleError::InvalidFrameIdentity);
        return std::unexpected(
            RuntimeLifecycleError::InvalidFrameIdentity);
    }
    if (!executor_.is_game_thread())
    {
        remember_error(RuntimeLifecycleError::WrongThread);
        return std::unexpected(RuntimeLifecycleError::WrongThread);
    }

    auto phase = RuntimePhase::Cold;
    auto resolve_contracts = false;
    auto rebind_frame = false;
    auto restore = false;
    auto shutdown_generation = std::uint64_t{};
    {
        const auto lock = std::scoped_lock{state_mutex_};
        phase = phase_;
        if (phase == RuntimePhase::Running)
        {
            resolve_contracts = !initial_contracts_resolved_;
            rebind_frame =
                resolve_contracts || !frame_identity_ ||
                *frame_identity_ != identity;
        }
        else if (phase == RuntimePhase::Quiescing)
        {
            restore = !transient_state_restored_;
            shutdown_generation = shutdown_generation_;
        }
        else
        {
            return std::size_t{};
        }
    }

    if (resolve_contracts)
    {
        const auto result =
            executor_.execute(ResolveInitialContracts{});
        if (!result)
        {
            remember_error(
                RuntimeLifecycleError::ExecutionFailed,
                result.error().compatibility_failure);
            return std::unexpected(
                RuntimeLifecycleError::ExecutionFailed);
        }
        const auto lock = std::scoped_lock{state_mutex_};
        initial_contracts_resolved_ = true;
    }
    if (rebind_frame)
    {
        const auto result =
            executor_.execute(RebindHudFrame{identity});
        if (!result)
        {
            remember_error(
                RuntimeLifecycleError::ExecutionFailed,
                result.error().compatibility_failure);
            return std::unexpected(
                RuntimeLifecycleError::ExecutionFailed);
        }
        const auto lock = std::scoped_lock{state_mutex_};
        frame_identity_ = identity;
    }
    if (restore)
    {
        const auto result = executor_.execute(
            RestoreTransientState{shutdown_generation});
        if (!result)
        {
            remember_error(RuntimeLifecycleError::ExecutionFailed);
            return std::unexpected(
                RuntimeLifecycleError::ExecutionFailed);
        }
        const auto lock = std::scoped_lock{state_mutex_};
        transient_state_restored_ = true;
        return std::size_t{1U};
    }

    const auto drained = scheduler_.drain(
        executor_,
        operation_budget);
    if (!drained)
    {
        const auto mapped = map_execution_error(drained.error());
        remember_error(
            mapped,
            drained.error().compatibility_failure);
        return std::unexpected(mapped);
    }
    return *drained;
}

auto RuntimeLifecycle::schedule(GameThreadOperation operation)
    -> ScheduleResult
{
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (phase_ != RuntimePhase::Running)
        {
            return ScheduleResult::Closed;
        }
    }
    return scheduler_.schedule(std::move(operation));
}

auto RuntimeLifecycle::discard_paint_generation(
    JobGeneration generation) -> std::size_t
{
    return scheduler_.discard_paint_generation(generation);
}

auto RuntimeLifecycle::queued_paint_generation(
    JobGeneration generation) const -> std::size_t
{
    return scheduler_.queued_paint_generation(generation);
}

auto RuntimeLifecycle::queue_snapshot() const -> QueueSnapshot
{
    return scheduler_.snapshot();
}

auto RuntimeLifecycle::last_paint_dispatch_us(
    JobGeneration generation) const -> std::uint64_t
{
    return scheduler_.last_paint_dispatch_us(generation);
}

auto RuntimeLifecycle::request_shutdown(std::uint64_t shutdown_generation)
    -> std::expected<void, RuntimeLifecycleError>
{
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (phase_ == RuntimePhase::Quiescing)
        {
            return {};
        }
        if (phase_ != RuntimePhase::Running)
        {
            return std::unexpected(RuntimeLifecycleError::InvalidState);
        }
        phase_ = RuntimePhase::Quiescing;
        shutdown_generation_ = shutdown_generation;
        transient_state_restored_ = false;
    }
    scheduler_.close();
    static_cast<void>(scheduler_.discard());
    return {};
}

auto RuntimeLifecycle::finalize_shutdown()
    -> std::expected<void, RuntimeLifecycleError>
{
    std::optional<CallbackId> callback{};
    {
        const auto lock = std::scoped_lock{state_mutex_};
        if (phase_ != RuntimePhase::Quiescing)
        {
            return std::unexpected(RuntimeLifecycleError::InvalidState);
        }
        if (!transient_state_restored_)
        {
            return std::unexpected(
                RuntimeLifecycleError::PendingGameThreadRestore);
        }
        callback = hud_callback_;
    }

    callback_barrier_.begin_close();
    if (callback)
    {
        const auto unregistered =
            callbacks_.unregister_hud_callback(*callback);
        if (!unregistered)
        {
            const auto lock = std::scoped_lock{state_mutex_};
            phase_ = RuntimePhase::Failed;
            last_error_ =
                RuntimeLifecycleError::CallbackUnregistration;
            return std::unexpected(
                RuntimeLifecycleError::CallbackUnregistration);
        }
    }
    callback_barrier_.wait_for_idle();

    const auto lock = std::scoped_lock{state_mutex_};
    hud_callback_.reset();
    phase_ = RuntimePhase::Stopped;
    return {};
}

auto RuntimeLifecycle::snapshot() const -> RuntimeLifecycleSnapshot
{
    const auto lock = std::scoped_lock{state_mutex_};
    return RuntimeLifecycleSnapshot{
        phase_,
        update_ticks_.load(std::memory_order_relaxed),
        frame_identity_,
        scheduler_.snapshot(),
        last_error_,
        last_compatibility_failure_,
    };
}

auto RuntimeLifecycle::hud_trampoline(
    void* context,
    const HudFrameIdentity& identity) noexcept -> void
{
    if (context == nullptr)
    {
        return;
    }
    auto& lifecycle = *static_cast<RuntimeLifecycle*>(context);
    auto lease = lifecycle.callback_barrier_.try_enter();
    if (!lease)
    {
        return;
    }
    try
    {
        const auto result =
            lifecycle.on_hud_frame_admitted(identity, 64U);
        if (lifecycle.observer_ != nullptr)
        {
            lifecycle.observer_->on_hud_frame_complete(
                result,
                lifecycle.snapshot());
        }
    }
    catch (...)
    {
        lifecycle.remember_error(
            RuntimeLifecycleError::ExecutionFailed);
        if (lifecycle.observer_ != nullptr)
        {
            try
            {
                lifecycle.observer_->on_hud_frame_complete(
                    std::unexpected(
                        RuntimeLifecycleError::ExecutionFailed),
                    lifecycle.snapshot());
            }
            catch (...)
            {
            }
        }
    }
}

auto RuntimeLifecycle::remember_error(
    RuntimeLifecycleError error,
    std::optional<CompatibilityFailure> compatibility_failure) -> void
{
    const auto lock = std::scoped_lock{state_mutex_};
    last_error_ = error;
    last_compatibility_failure_ =
        std::move(compatibility_failure);
}
} // namespace meccha::application
