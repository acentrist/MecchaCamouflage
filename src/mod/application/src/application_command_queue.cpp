#include <meccha/application/application_command_queue.hpp>

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace meccha::application
{
namespace
{
auto command_id(const ApplicationCommand& command) -> CommandId
{
    return std::visit(
        [](const auto& value) {
            return value.id;
        },
        command);
}
} // namespace

ApplicationCommandQueue::ApplicationCommandQueue(
    std::size_t capacity)
    : capacity_{capacity}
{
}

auto ApplicationCommandQueue::enqueue(
    ApplicationCommand command) -> CommandEnqueueResult
{
    if (command_id(command) == 0U)
    {
        return CommandEnqueueResult::InvalidCommand;
    }
    const auto lock = std::scoped_lock{mutex_};
    if (!accepting_)
    {
        return CommandEnqueueResult::Closed;
    }
    if (queue_.size() >= capacity_)
    {
        return CommandEnqueueResult::Full;
    }
    queue_.push_back(std::move(command));
    return CommandEnqueueResult::Accepted;
}

auto ApplicationCommandQueue::drain(
    std::size_t maximum_commands)
    -> std::vector<ApplicationCommand>
{
    const auto lock = std::scoped_lock{mutex_};
    const auto count =
        std::min(maximum_commands, queue_.size());
    auto result = std::vector<ApplicationCommand>{};
    result.reserve(count);
    for (auto index = std::size_t{}; index < count; ++index)
    {
        result.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
    return result;
}

auto ApplicationCommandQueue::close() -> void
{
    const auto lock = std::scoped_lock{mutex_};
    accepting_ = false;
}

auto ApplicationCommandQueue::discard() -> std::size_t
{
    const auto lock = std::scoped_lock{mutex_};
    const auto count = queue_.size();
    queue_.clear();
    return count;
}

auto ApplicationCommandQueue::snapshot() const
    -> CommandQueueSnapshot
{
    const auto lock = std::scoped_lock{mutex_};
    return CommandQueueSnapshot{
        queue_.size(),
        capacity_,
        accepting_,
    };
}
} // namespace meccha::application
