#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>

namespace meccha::application
{
class CallbackBarrier
{
public:
    class Lease
    {
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        auto operator=(const Lease&) -> Lease& = delete;
        Lease(Lease&& other) noexcept;
        auto operator=(Lease&& other) noexcept -> Lease&;
        ~Lease();

    private:
        friend class CallbackBarrier;
        explicit Lease(CallbackBarrier& owner) noexcept;
        auto release() noexcept -> void;

        CallbackBarrier* owner_{};
    };

    CallbackBarrier() = default;
    CallbackBarrier(const CallbackBarrier&) = delete;
    auto operator=(const CallbackBarrier&) -> CallbackBarrier& = delete;

    [[nodiscard]] auto try_enter() -> std::optional<Lease>;
    auto begin_close() -> void;
    auto wait_for_idle() -> void;

    [[nodiscard]] auto accepting() const -> bool;
    [[nodiscard]] auto in_flight() const -> std::size_t;

private:
    auto leave() noexcept -> void;

    mutable std::mutex mutex_{};
    std::condition_variable idle_{};
    bool accepting_{true};
    std::size_t in_flight_{};
};
} // namespace meccha::application
