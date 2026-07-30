#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <mutex>
#include <variant>

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

struct RepresentativePaintCall
{
    std::uint64_t request_id{};

    auto operator==(const RepresentativePaintCall&) const -> bool = default;
};

struct RepresentativeImagePaintCall
{
    std::uint64_t request_id{};

    auto operator==(const RepresentativeImagePaintCall&) const
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
    RepresentativePaintCall,
    RepresentativeImagePaintCall,
    RestoreTransientState>;

enum class ScheduleResult : std::uint8_t
{
    Accepted,
    Full,
    Closed,
};

enum class DrainError : std::uint8_t
{
    WrongThread,
    ExecutionFailed,
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
        -> std::expected<void, DrainError> = 0;
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
        -> std::expected<std::size_t, DrainError>;

    auto close() -> void;
    auto discard() -> std::size_t;

    [[nodiscard]] auto snapshot() const -> QueueSnapshot;

private:
    const std::size_t capacity_{};
    mutable std::mutex drain_mutex_{};
    mutable std::mutex mutex_{};
    std::deque<GameThreadOperation> queue_{};
    bool accepting_{true};
};
} // namespace meccha::application
