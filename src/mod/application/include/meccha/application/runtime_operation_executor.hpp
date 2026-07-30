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

class UnrealRuntimePort
{
public:
    UnrealRuntimePort() = default;
    UnrealRuntimePort(const UnrealRuntimePort&) = delete;
    auto operator=(const UnrealRuntimePort&) -> UnrealRuntimePort& = delete;
    UnrealRuntimePort(UnrealRuntimePort&&) = default;
    auto operator=(UnrealRuntimePort&&) -> UnrealRuntimePort& = default;
    virtual ~UnrealRuntimePort() = default;

    virtual auto resolve_initial_contracts()
        -> std::expected<void, RuntimeExecutionError> = 0;
    virtual auto rebind_hud_frame(const HudFrameIdentity& identity)
        -> std::expected<void, RuntimeExecutionError> = 0;
    virtual auto paint_at_uv_with_brush(
        const PaintAtUvWithBrush& request)
        -> std::expected<void, RuntimeExecutionError> = 0;
    virtual auto update_image_preview_texture(
        const UpdateImagePreviewTexture& request)
        -> std::expected<void, RuntimeExecutionError> = 0;
    virtual auto restore_transient_state(std::uint64_t generation)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

class RuntimeOperationExecutor final : public GameThreadExecutor
{
public:
    RuntimeOperationExecutor(
        GameThreadContext& thread_context,
        UnrealRuntimePort& runtime);

    [[nodiscard]] auto is_game_thread() const noexcept -> bool override;

    auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> override;

private:
    GameThreadContext& thread_context_;
    UnrealRuntimePort& runtime_;
};
} // namespace meccha::application
