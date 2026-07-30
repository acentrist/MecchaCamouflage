#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace meccha::launcher
{
enum class SingleInstanceErrorCode : std::uint8_t
{
    AlreadyRunning,
    System,
};

struct SingleInstanceError
{
    SingleInstanceErrorCode code{};
    std::string detail{};

    auto operator==(const SingleInstanceError&) const -> bool = default;
};

#ifdef _WIN32
class LauncherInstanceGuard
{
public:
    LauncherInstanceGuard(const LauncherInstanceGuard&) = delete;
    auto operator=(const LauncherInstanceGuard&)
        -> LauncherInstanceGuard& = delete;

    LauncherInstanceGuard(LauncherInstanceGuard&& other) noexcept;
    auto operator=(LauncherInstanceGuard&& other) noexcept
        -> LauncherInstanceGuard&;

    ~LauncherInstanceGuard();

private:
    friend auto acquire_launcher_instance()
        -> std::expected<
            LauncherInstanceGuard,
            SingleInstanceError>;

    explicit LauncherInstanceGuard(void* handle);

    void* handle_{};
};

[[nodiscard]] auto acquire_launcher_instance()
    -> std::expected<LauncherInstanceGuard, SingleInstanceError>;
#endif
} // namespace meccha::launcher
