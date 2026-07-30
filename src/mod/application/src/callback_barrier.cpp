#include <meccha/application/callback_barrier.hpp>

#include <utility>

namespace meccha::application
{
CallbackBarrier::Lease::Lease(CallbackBarrier& owner) noexcept
    : owner_{&owner}
{
}

CallbackBarrier::Lease::Lease(Lease&& other) noexcept
    : owner_{std::exchange(other.owner_, nullptr)}
{
}

auto CallbackBarrier::Lease::operator=(Lease&& other) noexcept -> Lease&
{
    if (this != &other)
    {
        release();
        owner_ = std::exchange(other.owner_, nullptr);
    }
    return *this;
}

CallbackBarrier::Lease::~Lease()
{
    release();
}

auto CallbackBarrier::Lease::release() noexcept -> void
{
    if (owner_ != nullptr)
    {
        owner_->leave();
        owner_ = nullptr;
    }
}

auto CallbackBarrier::try_enter() -> std::optional<Lease>
{
    const auto lock = std::scoped_lock{mutex_};
    if (!accepting_)
    {
        return std::nullopt;
    }
    ++in_flight_;
    return Lease{*this};
}

auto CallbackBarrier::begin_close() -> void
{
    const auto lock = std::scoped_lock{mutex_};
    accepting_ = false;
    if (in_flight_ == 0U)
    {
        idle_.notify_all();
    }
}

auto CallbackBarrier::wait_for_idle() -> void
{
    auto lock = std::unique_lock{mutex_};
    idle_.wait(lock, [this] { return in_flight_ == 0U; });
}

auto CallbackBarrier::accepting() const -> bool
{
    const auto lock = std::scoped_lock{mutex_};
    return accepting_;
}

auto CallbackBarrier::in_flight() const -> std::size_t
{
    const auto lock = std::scoped_lock{mutex_};
    return in_flight_;
}

auto CallbackBarrier::leave() noexcept -> void
{
    const auto lock = std::scoped_lock{mutex_};
    if (in_flight_ > 0U)
    {
        --in_flight_;
    }
    if (!accepting_ && in_flight_ == 0U)
    {
        idle_.notify_all();
    }
}
} // namespace meccha::application
