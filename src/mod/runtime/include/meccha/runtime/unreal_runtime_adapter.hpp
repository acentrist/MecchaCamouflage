#pragma once

#include <meccha/application/paint_game_runtime.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/application/runtime_lifecycle.hpp>
#include <meccha/application/runtime_operation_executor.hpp>
#include <meccha/product_ui/image_editor_texture_coordinator.hpp>
#include <meccha/product_ui/product_ui_frame_coordinator.hpp>

#include <memory>

namespace meccha::runtime
{
class UnrealRuntimeAdapter final
    : public application::RuntimeCallbackPort,
      public application::GameThreadContext,
      public application::UnrealFrameRuntimePort,
      public application::PaintStrokeRuntimePort,
      public application::PaintQueueRuntimePort,
      public application::PaintPreviewRuntimePort,
      public product_ui::ImageEditorTextureRuntimePort,
      public product_ui::ProductUiCanvasRenderPort
{
public:
    UnrealRuntimeAdapter();
    UnrealRuntimeAdapter(const UnrealRuntimeAdapter&) = delete;
    auto operator=(const UnrealRuntimeAdapter&)
        -> UnrealRuntimeAdapter& = delete;
    ~UnrealRuntimeAdapter() override;

    auto register_hud_callback(
        void* context,
        application::HudCallback callback)
        -> std::expected<
            application::CallbackId,
            application::CallbackPortError> override;

    auto unregister_hud_callback(application::CallbackId id)
        -> std::expected<
            void,
            application::CallbackPortError> override;

    [[nodiscard]] auto is_game_thread() const noexcept
        -> bool override;

    auto resolve_initial_contracts()
        -> std::expected<
            void,
            application::RuntimeExecutionError> override;

    auto rebind_hud_frame(
        const application::HudFrameIdentity& identity)
        -> std::expected<
            void,
            application::RuntimeExecutionError> override;

    auto paint_at_uv_with_brush(
        const application::PaintAtUvWithBrush& request)
        -> std::expected<
            void,
            application::RuntimeExecutionError> override;

    auto observe_paint_queues(
        application::RuntimeObjectHandle component,
        application::JobGeneration generation)
        -> std::expected<
            application::PaintQueueObservation,
            application::RuntimeExecutionError> override;

    auto capture(application::RuntimeObjectHandle component)
        -> std::expected<
            application::PaintPreviewSnapshot,
            application::RuntimeExecutionError> override;

    auto apply(
        application::RuntimeObjectHandle component,
        const application::PaintTextureImage& image)
        -> std::expected<
            void,
            application::RuntimeExecutionError> override;

    auto restore(
        const application::PaintPreviewSnapshot& snapshot)
        -> std::expected<
            void,
            application::RuntimeExecutionError> override;

    [[nodiscard]] auto create_texture(
        const product_ui::ImageEditorTextureUpload& upload)
        -> std::expected<
            ui::CanvasTextureHandle,
            product_ui::ImageEditorTextureRuntimeError> override;

    auto release_texture(ui::CanvasTextureHandle handle)
        -> std::expected<
            void,
            product_ui::ImageEditorTextureRuntimeError> override;

    [[nodiscard]] auto render(
        const application::HudFrameIdentity& identity,
        const ui::CanvasFrame& frame)
        -> std::expected<
            void,
            product_ui::ProductUiFrameRuntimeError> override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace meccha::runtime
