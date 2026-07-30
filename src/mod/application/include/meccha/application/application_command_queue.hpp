#pragma once

#include <meccha/application/commands.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace meccha::application
{
enum class CommandEnqueueResult : std::uint8_t
{
    Accepted,
    InvalidCommand,
    Full,
    Closed,
};

struct CommandQueueSnapshot
{
    std::size_t queued{};
    std::size_t capacity{};
    bool accepting{};

    auto operator==(const CommandQueueSnapshot&) const
        -> bool = default;
};

class ApplicationCommandQueue
{
public:
    explicit ApplicationCommandQueue(std::size_t capacity);
    ApplicationCommandQueue(const ApplicationCommandQueue&) = delete;
    auto operator=(const ApplicationCommandQueue&)
        -> ApplicationCommandQueue& = delete;

    [[nodiscard]] auto enqueue(ApplicationCommand command)
        -> CommandEnqueueResult;

    [[nodiscard]] auto drain(std::size_t maximum_commands)
        -> std::vector<ApplicationCommand>;

    auto close() -> void;
    auto discard() -> std::size_t;

    [[nodiscard]] auto snapshot() const -> CommandQueueSnapshot;

private:
    const std::size_t capacity_{};
    mutable std::mutex mutex_{};
    std::deque<ApplicationCommand> queue_{};
    bool accepting_{true};
};
} // namespace meccha::application
