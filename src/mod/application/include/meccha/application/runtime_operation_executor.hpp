#pragma once

#include <meccha/application/game_thread_scheduler.hpp>

#include <cstdint>
#include <expected>

namespace meccha::application
{
class GameThreadContext
{
public:
    GameThreadContext() = default;
    GameThreadContext(const GameThreadContext&) = delete;
    auto operator=(const GameThreadContext&) -> GameThreadContext& = delete;
    GameThreadContext(GameThreadContext&&) = default;
    auto operator=(GameThreadContext&&) -> GameThreadContext& = default;
    virtual ~GameThreadContext() = default;

    [[nodiscard]] virtual auto is_game_thread() const noexcept -> bool = 0;
};

class UnrealFrameRuntimePort
{
public:
    UnrealFrameRuntimePort() = default;
    UnrealFrameRuntimePort(const UnrealFrameRuntimePort&) = delete;
    auto operator=(const UnrealFrameRuntimePort&)
        -> UnrealFrameRuntimePort& = delete;
    virtual ~UnrealFrameRuntimePort() = default;

    virtual auto resolve_initial_contracts()
        -> std::expected<void, RuntimeExecutionError> = 0;
    virtual auto rebind_hud_frame(const HudFrameIdentity& identity)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

class PaintStrokeRuntimePort
{
public:
    PaintStrokeRuntimePort() = default;
    PaintStrokeRuntimePort(const PaintStrokeRuntimePort&) = delete;
    auto operator=(const PaintStrokeRuntimePort&)
        -> PaintStrokeRuntimePort& = delete;
    virtual ~PaintStrokeRuntimePort() = default;

    virtual auto paint_at_uv_with_brush(
        const PaintAtUvWithBrush& request)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

class ImagePreviewTextureRuntimePort
{
public:
    ImagePreviewTextureRuntimePort() = default;
    ImagePreviewTextureRuntimePort(
        const ImagePreviewTextureRuntimePort&) = delete;
    auto operator=(const ImagePreviewTextureRuntimePort&)
        -> ImagePreviewTextureRuntimePort& = delete;
    virtual ~ImagePreviewTextureRuntimePort() = default;

    virtual auto update_image_preview_texture(
        const UpdateImagePreviewTexture& request)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

class TransientStateRuntimePort
{
public:
    TransientStateRuntimePort() = default;
    TransientStateRuntimePort(
        const TransientStateRuntimePort&) = delete;
    auto operator=(const TransientStateRuntimePort&)
        -> TransientStateRuntimePort& = delete;
    virtual ~TransientStateRuntimePort() = default;

    virtual auto restore_transient_state(std::uint64_t generation)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

class RuntimeOperationExecutor final : public GameThreadExecutor
{
public:
    RuntimeOperationExecutor(
        GameThreadContext& thread_context,
        UnrealFrameRuntimePort& frame_runtime,
        PaintStrokeRuntimePort& paint_runtime,
        ImagePreviewTextureRuntimePort& texture_runtime,
        TransientStateRuntimePort& transient_runtime);

    [[nodiscard]] auto is_game_thread() const noexcept -> bool override;

    auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> override;

private:
    GameThreadContext& thread_context_;
    UnrealFrameRuntimePort& frame_runtime_;
    PaintStrokeRuntimePort& paint_runtime_;
    ImagePreviewTextureRuntimePort& texture_runtime_;
    TransientStateRuntimePort& transient_runtime_;
};
} // namespace meccha::application
