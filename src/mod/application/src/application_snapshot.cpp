#include <meccha/application/application_snapshot.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace meccha::application
{
BoundedDiagnostics::BoundedDiagnostics(std::size_t capacity)
    : capacity_{capacity}
{
    entries_.reserve(capacity);
}

auto BoundedDiagnostics::push(
    DiagnosticSeverity severity,
    std::string message_key,
    std::optional<CommandId> command_id) -> void
{
    if (capacity_ == 0U)
    {
        return;
    }
    if (entries_.size() == capacity_)
    {
        entries_.erase(entries_.begin());
    }
    entries_.push_back(DiagnosticEntry{
        next_sequence_,
        severity,
        std::move(message_key),
        command_id,
    });
    if (next_sequence_ !=
        std::numeric_limits<std::uint64_t>::max())
    {
        ++next_sequence_;
    }
}

auto BoundedDiagnostics::entries() const
    -> std::vector<DiagnosticEntry>
{
    return entries_;
}

SnapshotPublisher::SnapshotPublisher()
    : current_{std::make_shared<const ApplicationSnapshot>()}
{
}

auto SnapshotPublisher::publish(ApplicationSnapshot snapshot) -> void
{
    const auto lock = std::scoped_lock{publish_mutex_};
    const auto previous = current_.load(std::memory_order_acquire);
    snapshot.revision =
        previous->revision ==
                std::numeric_limits<std::uint64_t>::max()
            ? previous->revision
            : previous->revision + 1U;
    current_.store(
        std::make_shared<const ApplicationSnapshot>(
            std::move(snapshot)),
        std::memory_order_release);
}

auto SnapshotPublisher::read() const
    -> std::shared_ptr<const ApplicationSnapshot>
{
    return current_.load(std::memory_order_acquire);
}
} // namespace meccha::application
