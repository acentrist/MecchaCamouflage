#pragma once

#include <meccha/application/application_snapshot.hpp>
#include <meccha/application/config_store.hpp>
#include <meccha/application/runtime_lifecycle.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>

namespace meccha::application
{
enum class ApplicationRootError : std::uint8_t
{
    InvalidState,
    ConfigurationLoad,
    RuntimeInitialization,
    PendingGameThreadRestore,
    RuntimeShutdown,
};

class ApplicationRoot final : private RuntimeLifecycleObserver
{
public:
    ApplicationRoot(
        RuntimeCallbackPort& callbacks,
        GameThreadExecutor& executor,
        AtomicTextStorage& config_storage,
        std::size_t queue_capacity,
        std::size_t diagnostic_capacity);
    ApplicationRoot(const ApplicationRoot&) = delete;
    auto operator=(const ApplicationRoot&) -> ApplicationRoot& = delete;
    ~ApplicationRoot() override = default;

    auto initialize() -> std::expected<void, ApplicationRootError>;
    auto on_update() noexcept -> void;

    auto request_shutdown(std::uint64_t shutdown_generation)
        -> std::expected<void, ApplicationRootError>;
    auto finalize_shutdown()
        -> std::expected<void, ApplicationRootError>;

    [[nodiscard]] auto snapshot() const
        -> std::shared_ptr<const ApplicationSnapshot>;

private:
    auto on_hud_frame_complete(
        const std::expected<std::size_t, RuntimeLifecycleError>& result,
        const RuntimeLifecycleSnapshot& runtime_snapshot) noexcept
        -> void override;

    auto fail_locked(CompatibilityFailure failure) -> void;
    auto publish_locked(
        const RuntimeLifecycleSnapshot& runtime_snapshot) -> void;
    auto publish_locked() -> void;

    mutable std::mutex state_mutex_{};
    ConfigStore config_store_;
    ApplicationRuntimePhase phase_{ApplicationRuntimePhase::Cold};
    core::ApplicationConfig settings_{};
    JobStateMachine jobs_{};
    PreviewStateMachine preview_{};
    CompatibilityState compatibility_{};
    BoundedDiagnostics diagnostics_;
    SnapshotPublisher snapshots_{};
    bool runtime_initialized_{};
    RuntimeLifecycle lifecycle_;
};
} // namespace meccha::application
