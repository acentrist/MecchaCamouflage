#include <meccha/application/game_thread_scheduler.hpp>

#include <algorithm>
#include <mutex>
#include <type_traits>
#include <utility>
#include <variant>

namespace meccha::application
{
namespace
{
auto is_control_operation(const GameThreadOperation& operation) -> bool
{
    return std::visit(
        [](const auto& request)
        {
            using Request = std::decay_t<decltype(request)>;
            return std::is_same_v<Request, ResolveInitialContracts> ||
                   std::is_same_v<Request, RebindHudFrame> ||
                   std::is_same_v<Request, RestoreTransientState>;
        },
        operation);
}
} // namespace

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
    const auto control = is_control_operation(operation);
    const auto total =
        control_queue_.size() + frame_queue_.size();
    const auto frame_capacity =
        capacity_ > 0U ? capacity_ - 1U : 0U;
    if (total >= capacity_ ||
        (!control && frame_queue_.size() >= frame_capacity))
    {
        return ScheduleResult::Full;
    }
    auto& queue =
        control ? control_queue_ : frame_queue_;
    queue.push_back(std::move(operation));
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
        auto control = false;
        {
            const auto lock = std::scoped_lock{mutex_};
            if (control_queue_.empty() && frame_queue_.empty())
            {
                break;
            }
            control = !control_queue_.empty();
            operation =
                control
                    ? control_queue_.front()
                    : frame_queue_.front();
        }

        const auto executed = executor.execute(operation);
        if (!executed)
        {
            return std::unexpected(executed.error());
        }
        {
            const auto lock = std::scoped_lock{mutex_};
            auto& queue =
                control ? control_queue_ : frame_queue_;
            queue.pop_front();
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
    const auto count =
        control_queue_.size() + frame_queue_.size();
    control_queue_.clear();
    frame_queue_.clear();
    return count;
}

auto GameThreadScheduler::discard_paint_generation(
    JobGeneration generation) -> std::size_t
{
    if (generation == 0U)
    {
        return 0U;
    }
    const auto drain_lock = std::scoped_lock{drain_mutex_};
    const auto lock = std::scoped_lock{mutex_};
    const auto previous = frame_queue_.size();
    std::erase_if(
        frame_queue_,
        [generation](const GameThreadOperation& operation)
        {
            const auto* paint =
                std::get_if<PaintAtUvWithBrush>(&operation);
            return paint != nullptr &&
                   paint->job_generation == generation;
        });
    return previous - frame_queue_.size();
}

auto GameThreadScheduler::snapshot() const -> QueueSnapshot
{
    const auto lock = std::scoped_lock{mutex_};
    return QueueSnapshot{
        control_queue_.size() + frame_queue_.size(),
        capacity_,
        accepting_,
    };
}

auto GameThreadScheduler::queue_snapshot() const -> QueueSnapshot
{
    return snapshot();
}

auto GameThreadScheduler::queued_paint_generation(
    JobGeneration generation) const -> std::size_t
{
    if (generation == 0U)
    {
        return 0U;
    }
    const auto lock = std::scoped_lock{mutex_};
    return static_cast<std::size_t>(std::ranges::count_if(
        frame_queue_,
        [generation](const GameThreadOperation& operation)
        {
            const auto* paint =
                std::get_if<PaintAtUvWithBrush>(&operation);
            return paint != nullptr &&
                   paint->job_generation == generation;
        }));
}
} // namespace meccha::application
