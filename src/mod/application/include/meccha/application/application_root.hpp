#pragma once

#include <meccha/application/application_command_queue.hpp>
#include <meccha/application/application_snapshot.hpp>
#include <meccha/application/config_store.hpp>
#include <meccha/application/esp_frame_coordinator.hpp>
#include <meccha/application/image_editor_session.hpp>
#include <meccha/application/image_paint_game_runtime.hpp>
#include <meccha/application/image_paint_job_coordinator.hpp>
#include <meccha/application/paint_game_runtime.hpp>
#include <meccha/application/paint_job_coordinator.hpp>
#include <meccha/application/paint_preview_build_worker.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/application/runtime_frame_extension.hpp>
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
    ImageEditorRecovery,
    RuntimeInitialization,
    PendingGameThreadRestore,
    RuntimeShutdown,
};

class ApplicationRoot final
    : public ApplicationSnapshotPort,
      public ApplicationCommandSink,
      private RuntimeLifecycleObserver
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
        ImageEditorSessionPort& image_editor,
        EspGameRuntimePort& esp_runtime,
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
        ImageEditorSessionPort& image_editor,
        std::size_t queue_capacity,
        std::size_t command_capacity,
        std::size_t diagnostic_capacity);
    ApplicationRoot(const ApplicationRoot&) = delete;
    auto operator=(const ApplicationRoot&) -> ApplicationRoot& = delete;
    ~ApplicationRoot() override = default;

    auto initialize() -> std::expected<void, ApplicationRootError>;
    auto attach_frame_extension(RuntimeFrameExtensionPort& extension)
        -> std::expected<void, ApplicationRootError>;
    auto on_update() noexcept -> void;

    [[nodiscard]] auto enqueue_command(ApplicationCommand command)
        -> CommandEnqueueResult override;

    auto request_shutdown(std::uint64_t shutdown_generation)
        -> std::expected<void, ApplicationRootError>;
    auto finalize_shutdown()
        -> std::expected<void, ApplicationRootError>;

    [[nodiscard]] auto snapshot() const
        -> std::shared_ptr<const ApplicationSnapshot> override;

private:
    struct ActivePaintPreviewBuild
    {
        JobGeneration generation{};
        CommandId command_id{};
        RuntimeObjectHandle component{};
    };

    struct ActiveAutomaticPaintCapture
    {
        JobGeneration generation{};
        CommandId command_id{};
        core::PaintSettings settings{};
        bool preview{};
        std::uint64_t admitted_hud_epoch{};
        bool cancel_requested{};
        bool failure_recorded{};
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
    auto record_frame_extension_error(
        const RuntimeFrameExtensionError& error) -> void;
    auto record_command_error(CommandId command_id) -> void;
    auto advance_image_editor() -> void;
    auto advance_esp(
        const HudFrameIdentity& frame_identity) -> void;
    auto process_commands(
        std::uint64_t now_ms,
        const HudFrameIdentity& frame_identity) noexcept -> void;
    auto process_command(
        ApplicationCommand command,
        std::uint64_t now_ms) -> void;
    auto begin_paint(
        StartPaint request,
        std::uint64_t now_ms) -> void;
    auto accept_captured_paint(
        CommandId command_id,
        const core::PaintSettings& settings,
        CapturedPaintJob captured,
        std::uint64_t now_ms) -> void;
    auto begin_image_paint(
        StartImagePaint request,
        std::uint64_t now_ms) -> void;
    auto begin_paint_preview(PreviewPaint request) -> void;
    auto accept_captured_paint_preview(
        CommandId command_id,
        const core::PaintSettings& settings,
        CapturedPaintJob captured) -> void;
    auto begin_automatic_paint_capture(
        CommandId command_id,
        const core::PaintSettings& settings,
        bool preview) -> bool;
    auto advance_automatic_paint_capture(
        std::uint64_t now_ms) -> void;
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
    ImageEditorSessionPort* image_editor_{};
    std::unique_ptr<EspFrameCoordinator> esp_frames_{};
    std::unique_ptr<ApplicationCommandQueue> command_queue_{};
    std::unique_ptr<PaintPreviewController> paint_previews_{};
    std::optional<RuntimeObjectHandle> active_paint_component_{};
    std::optional<RuntimeObjectHandle>
        active_image_paint_component_{};
    std::optional<ActivePaintPreviewBuild>
        active_paint_preview_build_{};
    std::optional<ActiveAutomaticPaintCapture>
        active_automatic_paint_capture_{};
    std::optional<ApplicationCommand>
        deferred_paint_preview_command_{};
    JobGeneration paint_preview_generation_{};
    JobGeneration paint_capture_generation_{};
    std::uint64_t hud_frame_epoch_{};
    std::atomic<JobGeneration>
        active_paint_preview_generation_{};
    std::optional<std::uint64_t>
        pending_shutdown_generation_{};
    RuntimeFrameExtensionPort* frame_extension_{};
    bool lifecycle_shutdown_requested_{};
    bool frame_extension_stopped_{};
    bool paint_shutdown_cancel_requested_{};
    bool ui_open_{};
};
} // namespace meccha::application
