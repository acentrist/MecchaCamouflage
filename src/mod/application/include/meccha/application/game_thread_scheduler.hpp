#pragma once

#include <meccha/application/compatibility.hpp>
#include <meccha/core/paint.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

namespace meccha::application
{
struct HudFrameIdentity
{
    std::uint64_t world{};
    std::uint64_t controller{};
    std::uint64_t hud{};
    std::uint64_t canvas{};

    [[nodiscard]] constexpr auto valid() const noexcept -> bool
    {
        return world != 0U && controller != 0U && hud != 0U &&
               canvas != 0U;
    }

    auto operator==(const HudFrameIdentity&) const -> bool = default;
};

struct ResolveInitialContracts
{
    auto operator==(const ResolveInitialContracts&) const -> bool = default;
};

struct RebindHudFrame
{
    HudFrameIdentity identity{};

    auto operator==(const RebindHudFrame&) const -> bool = default;
};

struct RuntimeObjectHandle
{
    std::uint64_t identity{};
    std::uint64_t generation{};

    [[nodiscard]] constexpr auto valid() const noexcept -> bool
    {
        return identity != 0U && generation != 0U;
    }

    auto operator==(const RuntimeObjectHandle&) const -> bool = default;
};

struct PaintAtUvWithBrush
{
    std::uint64_t request_id{};
    RuntimeObjectHandle component{};
    double u{};
    double v{};
    double brush_size_texels{};
    core::Rgb8 color{};
    core::Material material{};
    bool include_scene_lighting{};

    auto operator==(const PaintAtUvWithBrush&) const -> bool = default;
};

struct UpdateImagePreviewTexture
{
    std::uint64_t request_id{};
    RuntimeObjectHandle texture{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::shared_ptr<const std::vector<std::byte>> rgba{};

    auto operator==(const UpdateImagePreviewTexture&) const
        -> bool = default;
};

struct RestoreTransientState
{
    std::uint64_t shutdown_generation{};

    auto operator==(const RestoreTransientState&) const -> bool = default;
};

using GameThreadOperation = std::variant<
    ResolveInitialContracts,
    RebindHudFrame,
    PaintAtUvWithBrush,
    UpdateImagePreviewTexture,
    RestoreTransientState>;

enum class ScheduleResult : std::uint8_t
{
    Accepted,
    Full,
    Closed,
};

enum class RuntimeExecutionErrorCode : std::uint8_t
{
    WrongThread,
    InvalidRequest,
    OperationFailure,
};

struct RuntimeExecutionError
{
    RuntimeExecutionErrorCode code{};
    std::optional<CompatibilityFailure> compatibility_failure{};

    auto operator==(const RuntimeExecutionError&) const -> bool = default;
};

struct QueueSnapshot
{
    std::size_t queued{};
    std::size_t capacity{};
    bool accepting{};

    auto operator==(const QueueSnapshot&) const -> bool = default;
};

class GameThreadExecutor
{
public:
    GameThreadExecutor() = default;
    GameThreadExecutor(const GameThreadExecutor&) = delete;
    auto operator=(const GameThreadExecutor&) -> GameThreadExecutor& = delete;
    GameThreadExecutor(GameThreadExecutor&&) = default;
    auto operator=(GameThreadExecutor&&) -> GameThreadExecutor& = default;
    virtual ~GameThreadExecutor() = default;

    [[nodiscard]] virtual auto is_game_thread() const noexcept -> bool = 0;
    virtual auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

class GameThreadScheduler
{
public:
    explicit GameThreadScheduler(std::size_t capacity);
    GameThreadScheduler(const GameThreadScheduler&) = delete;
    auto operator=(const GameThreadScheduler&)
        -> GameThreadScheduler& = delete;

    [[nodiscard]] auto schedule(GameThreadOperation operation)
        -> ScheduleResult;

    [[nodiscard]] auto drain(
        GameThreadExecutor& executor,
        std::size_t maximum_operations)
        -> std::expected<std::size_t, RuntimeExecutionError>;

    auto close() -> void;
    auto discard() -> std::size_t;

    [[nodiscard]] auto snapshot() const -> QueueSnapshot;

private:
    const std::size_t capacity_{};
    mutable std::mutex drain_mutex_{};
    mutable std::mutex mutex_{};
    std::deque<GameThreadOperation> control_queue_{};
    std::deque<GameThreadOperation> frame_queue_{};
    bool accepting_{true};
};
} // namespace meccha::application
