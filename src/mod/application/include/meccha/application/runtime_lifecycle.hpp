#pragma once

#include <meccha/application/callback_barrier.hpp>
#include <meccha/application/game_thread_scheduler.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>

namespace meccha::application
{
using CallbackId = std::uint64_t;
using HudCallback = void (*)(
    void* context,
    const HudFrameIdentity& identity);

enum class CallbackPortError : std::uint8_t
{
    Registration,
    Unregistration,
};

class RuntimeCallbackPort
{
public:
    RuntimeCallbackPort() = default;
    RuntimeCallbackPort(const RuntimeCallbackPort&) = delete;
    auto operator=(const RuntimeCallbackPort&)
        -> RuntimeCallbackPort& = delete;
    RuntimeCallbackPort(RuntimeCallbackPort&&) = default;
    auto operator=(RuntimeCallbackPort&&) -> RuntimeCallbackPort& = default;
    virtual ~RuntimeCallbackPort() = default;

    virtual auto register_hud_callback(
        void* context,
        HudCallback callback)
        -> std::expected<CallbackId, CallbackPortError> = 0;

    virtual auto unregister_hud_callback(CallbackId id)
        -> std::expected<void, CallbackPortError> = 0;
};

enum class RuntimePhase : std::uint8_t
{
    Cold,
    Running,
    Quiescing,
    Stopped,
    Failed,
};

enum class RuntimeLifecycleError : std::uint8_t
{
    InvalidState,
    InvalidFrameIdentity,
    CallbackRegistration,
    CallbackUnregistration,
    WrongThread,
    ExecutionFailed,
    PendingGameThreadRestore,
};

struct RuntimeLifecycleSnapshot
{
    RuntimePhase phase{};
    std::uint64_t update_ticks{};
    std::optional<HudFrameIdentity> frame_identity{};
    QueueSnapshot queue{};
    std::optional<RuntimeLifecycleError> last_error{};

    auto operator==(const RuntimeLifecycleSnapshot&) const -> bool = default;
};

class RuntimeLifecycle
{
public:
    RuntimeLifecycle(
        RuntimeCallbackPort& callbacks,
        GameThreadExecutor& executor,
        std::size_t queue_capacity);
    RuntimeLifecycle(const RuntimeLifecycle&) = delete;
    auto operator=(const RuntimeLifecycle&) -> RuntimeLifecycle& = delete;
    ~RuntimeLifecycle();

    auto initialize()
        -> std::expected<void, RuntimeLifecycleError>;

    auto on_update() noexcept -> void;

    auto on_hud_frame(
        const HudFrameIdentity& identity,
        std::size_t operation_budget)
        -> std::expected<std::size_t, RuntimeLifecycleError>;

    [[nodiscard]] auto schedule(GameThreadOperation operation)
        -> ScheduleResult;

    auto request_shutdown(std::uint64_t shutdown_generation)
        -> std::expected<void, RuntimeLifecycleError>;

    auto finalize_shutdown()
        -> std::expected<void, RuntimeLifecycleError>;

    [[nodiscard]] auto snapshot() const -> RuntimeLifecycleSnapshot;

private:
    static auto hud_trampoline(
        void* context,
        const HudFrameIdentity& identity) -> void;

    auto remember_error(RuntimeLifecycleError error) -> void;

    RuntimeCallbackPort& callbacks_;
    GameThreadExecutor& executor_;
    GameThreadScheduler scheduler_;
    CallbackBarrier callback_barrier_{};
    mutable std::mutex state_mutex_{};
    RuntimePhase phase_{RuntimePhase::Cold};
    std::optional<CallbackId> hud_callback_{};
    std::optional<HudFrameIdentity> frame_identity_{};
    std::optional<RuntimeLifecycleError> last_error_{};
    std::uint64_t shutdown_generation_{};
    bool initial_contracts_resolved_{};
    bool transient_state_restored_{};
    std::atomic<std::uint64_t> update_ticks_{};
};
} // namespace meccha::application
