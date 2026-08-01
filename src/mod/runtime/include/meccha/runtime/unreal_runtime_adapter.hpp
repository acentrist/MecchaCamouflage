#pragma once

#include <meccha/application/esp_frame_coordinator.hpp>
#include <meccha/application/image_paint_game_runtime.hpp>
#include <meccha/application/image_paint_profile_catalog.hpp>
#include <meccha/application/paint_game_runtime.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/application/runtime_lifecycle.hpp>
#include <meccha/application/runtime_operation_executor.hpp>
#include <meccha/core/fallback_glyph_atlas.hpp>
#include <meccha/product_ui/image_editor_texture_coordinator.hpp>
#include <meccha/product_ui/product_ui_frame_coordinator.hpp>
#include <meccha/product_ui/product_ui_input_queue.hpp>

#include <memory>

namespace meccha::runtime
{
class UnrealRuntimeAdapter final
    : public application::RuntimeCallbackPort,
      public application::GameThreadContext,
      public application::UnrealFrameRuntimePort,
      public application::PaintStrokeRuntimePort,
      public application::TransientStateRuntimePort,
      public application::PaintQueueRuntimePort,
      public application::PaintPreviewRuntimePort,
      public application::PaintGameRuntimePort,
      public application::ImagePaintGameRuntimePort,
      public application::EspGameRuntimePort,
      public product_ui::ImageEditorTextureRuntimePort,
      public product_ui::ProductUiFrameCapturePort,
      public product_ui::ProductUiCanvasRenderPort,
      public ui::InputLeasePort
{
public:
    explicit UnrealRuntimeAdapter(
        std::shared_ptr<
            product_ui::ProductUiInputQueue> input_queue,
        std::shared_ptr<
            const application::ImagePaintProfileCatalog>
            image_paint_profiles,
        std::shared_ptr<const core::FallbackGlyphAtlas>
            fallback_glyph_atlas);
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

    auto restore_transient_state(std::uint64_t generation)
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

    auto begin_projective_capture(
        const core::PaintSettings& settings,
        application::JobGeneration generation)
        -> std::expected<
            void,
            application::RuntimeExecutionError> override;

    auto advance_projective_capture(
        application::JobGeneration generation)
        -> std::expected<
            std::optional<application::CapturedPaintJob>,
            application::RuntimeExecutionError> override;

    auto cancel_projective_capture(
        application::JobGeneration generation)
        -> std::expected<
            bool,
            application::RuntimeExecutionError> override;

    auto capture(core::BodyProfile body)
        -> std::expected<
            application::CapturedImagePaintJob,
            application::RuntimeExecutionError> override;

    auto observe_queues(
        application::RuntimeObjectHandle component,
        application::JobGeneration generation)
        -> std::expected<
            application::PaintQueueObservation,
            application::RuntimeExecutionError> override;

    [[nodiscard]] auto capture_esp_frame()
        -> std::expected<
            application::CapturedEspFrame,
            application::RuntimeExecutionError> override;

    auto draw_esp_frame(
        const application::HudFrameIdentity& frame_identity,
        const core::EspPrimitiveFrame& frame)
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

    [[nodiscard]] auto capture(
        const application::HudFrameIdentity& identity,
        product_ui::ProductUiKeyboardInputMode keyboard_mode)
        -> std::expected<
            product_ui::ProductUiFrameInput,
            product_ui::ProductUiFrameRuntimeError> override;

    [[nodiscard]] auto render(
        const application::HudFrameIdentity& identity,
        const ui::CanvasFrame& frame)
        -> std::expected<
            void,
            product_ui::ProductUiFrameRuntimeError> override;

    [[nodiscard]] auto capture()
        -> std::expected<
            ui::RuntimeInputState,
            ui::InputPortError> override;

    [[nodiscard]] auto apply_panel_controls()
        -> std::expected<void, ui::InputPortError> override;

    [[nodiscard]] auto current_owner()
        -> std::expected<
            std::uint64_t,
            ui::InputPortError> override;

    [[nodiscard]] auto restore(
        const ui::RuntimeInputState& state)
        -> std::expected<void, ui::InputPortError> override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace meccha::runtime
