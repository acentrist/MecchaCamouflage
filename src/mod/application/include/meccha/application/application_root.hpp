#pragma once

#include <meccha/application/application_command_queue.hpp>
#include <meccha/application/application_snapshot.hpp>
#include <meccha/application/config_store.hpp>
#include <meccha/application/image_editor_pipeline.hpp>
#include <meccha/application/image_paint_game_runtime.hpp>
#include <meccha/application/image_paint_job_coordinator.hpp>
#include <meccha/application/paint_game_runtime.hpp>
#include <meccha/application/paint_job_coordinator.hpp>
#include <meccha/application/paint_preview_build_worker.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/application/runtime_lifecycle.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>

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
    ApplicationRoot(
        RuntimeCallbackPort& callbacks,
        GameThreadExecutor& executor,
        AtomicTextStorage& config_storage,
        PaintGameRuntimePort& paint_runtime,
        GameThreadContext& game_thread_context,
        PaintPreviewRuntimePort& paint_preview_runtime,
        std::size_t queue_capacity,
        std::size_t command_capacity,
        std::size_t diagnostic_capacity);
    ApplicationRoot(
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
        std::size_t diagnostic_capacity);
    ApplicationRoot(const ApplicationRoot&) = delete;
    auto operator=(const ApplicationRoot&) -> ApplicationRoot& = delete;
    ~ApplicationRoot() override = default;

    auto initialize() -> std::expected<void, ApplicationRootError>;
    auto on_update() noexcept -> void;

    [[nodiscard]] auto enqueue_command(ApplicationCommand command)
        -> CommandEnqueueResult;

    auto request_shutdown(std::uint64_t shutdown_generation)
        -> std::expected<void, ApplicationRootError>;
    auto finalize_shutdown()
        -> std::expected<void, ApplicationRootError>;

    [[nodiscard]] auto snapshot() const
        -> std::shared_ptr<const ApplicationSnapshot>;

private:
    struct ActivePaintPreviewBuild
    {
        JobGeneration generation{};
        CommandId command_id{};
        RuntimeObjectHandle component{};
    };

    auto on_hud_frame_complete(
        const std::expected<std::size_t, RuntimeLifecycleError>& result,
        const RuntimeLifecycleSnapshot& runtime_snapshot) noexcept
        -> void override;

    auto fail_locked(CompatibilityFailure failure) -> void;
    auto record_runtime_error(
        const RuntimeExecutionError& error,
        std::optional<CommandId> command_id) -> void;
    auto record_preview_error(
        const PaintPreviewError& error,
        std::optional<CommandId> command_id) -> void;
    auto record_command_error(CommandId command_id) -> void;
    auto process_commands(std::uint64_t now_ms) noexcept -> void;
    auto process_command(
        ApplicationCommand command,
        std::uint64_t now_ms) -> void;
    auto begin_paint(
        StartPaint request,
        std::uint64_t now_ms) -> void;
    auto begin_image_paint(
        StartImagePaint request,
        std::uint64_t now_ms) -> void;
    auto begin_paint_preview(PreviewPaint request) -> void;
    auto restore_paint_preview(
        RestorePaintPreview request) -> void;
    auto defer_for_paint_preview(
        ApplicationCommand command) -> void;
    auto advance_paint(std::uint64_t now_ms) -> void;
    auto advance_image_paint(std::uint64_t now_ms) -> void;
    auto advance_paint_preview(std::uint64_t now_ms) -> void;
    auto advance_shutdown(std::uint64_t now_ms) -> void;
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
    CorePaintPlanBuilder paint_plan_builder_{};
    CorePaintPlanBuilder paint_preview_plan_builder_{};
    CoreImagePaintPlanBuilder image_paint_plan_builder_{};
    PaintPlanningWorker paint_planner_;
    ImagePaintPlanningWorker image_paint_planner_;
    PaintPreviewBuildWorker paint_preview_builder_;
    PaintDispatchController paint_dispatcher_;
    PaintJobCoordinator paint_jobs_;
    ImagePaintJobCoordinator image_paint_jobs_;
    PaintGameRuntimePort* paint_runtime_{};
    ImagePaintGameRuntimePort* image_paint_runtime_{};
    ImageProjectReadinessPort* image_projects_{};
    std::unique_ptr<ApplicationCommandQueue> command_queue_{};
    std::unique_ptr<PaintPreviewController> paint_previews_{};
    std::optional<RuntimeObjectHandle> active_paint_component_{};
    std::optional<RuntimeObjectHandle>
        active_image_paint_component_{};
    std::optional<ActivePaintPreviewBuild>
        active_paint_preview_build_{};
    std::optional<ApplicationCommand>
        deferred_paint_preview_command_{};
    JobGeneration paint_preview_generation_{};
    std::atomic<JobGeneration>
        active_paint_preview_generation_{};
    std::optional<std::uint64_t>
        pending_shutdown_generation_{};
    bool lifecycle_shutdown_requested_{};
    bool paint_shutdown_cancel_requested_{};
    bool ui_open_{};
    bool esp_enabled_{true};
};
} // namespace meccha::application
