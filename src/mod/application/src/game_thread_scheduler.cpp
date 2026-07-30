#include <meccha/application/game_thread_scheduler.hpp>

#include <algorithm>
#include <mutex>
#include <utility>

namespace meccha::application
{
GameThreadScheduler::GameThreadScheduler(std::size_t capacity)
    : capacity_{capacity}
{
}

auto GameThreadScheduler::schedule(GameThreadOperation operation)
    -> ScheduleResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (!accepting_)
    {
        return ScheduleResult::Closed;
    }
    if (queue_.size() >= capacity_)
    {
        return ScheduleResult::Full;
    }
    queue_.push_back(std::move(operation));
    return ScheduleResult::Accepted;
}

auto GameThreadScheduler::drain(
    GameThreadExecutor& executor,
    std::size_t maximum_operations)
    -> std::expected<std::size_t, RuntimeExecutionError>
{
    if (!executor.is_game_thread())
    {
        return std::unexpected(RuntimeExecutionError{
            RuntimeExecutionErrorCode::WrongThread,
            std::nullopt,
        });
    }

    const auto drain_lock = std::scoped_lock{drain_mutex_};
    auto completed = std::size_t{};
    while (completed < maximum_operations)
    {
        auto operation = GameThreadOperation{ResolveInitialContracts{}};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (queue_.empty())
            {
                break;
            }
            operation = queue_.front();
        }

        const auto executed = executor.execute(operation);
        if (!executed)
        {
            return std::unexpected(executed.error());
        }
        {
            const auto lock = std::scoped_lock{mutex_};
            queue_.pop_front();
        }
        ++completed;
    }
    return completed;
}

auto GameThreadScheduler::close() -> void
{
    const auto lock = std::scoped_lock{mutex_};
    accepting_ = false;
}

auto GameThreadScheduler::discard() -> std::size_t
{
    const auto drain_lock = std::scoped_lock{drain_mutex_};
    const auto lock = std::scoped_lock{mutex_};
    const auto count = queue_.size();
    queue_.clear();
    return count;
}

auto GameThreadScheduler::snapshot() const -> QueueSnapshot
{
    const auto lock = std::scoped_lock{mutex_};
    return QueueSnapshot{
        queue_.size(),
        capacity_,
        accepting_,
    };
}
} // namespace meccha::application
