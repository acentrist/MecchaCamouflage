#include <meccha/launcher/single_instance.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
constexpr auto LauncherMutexName =
    L"Local\\MecchaCamouflage.v2.Launcher";

auto error(
    SingleInstanceErrorCode code,
    std::string detail)
    -> std::unexpected<SingleInstanceError>
{
    return std::unexpected(SingleInstanceError{
        code,
        std::move(detail),
    });
}
} // namespace

LauncherInstanceGuard::LauncherInstanceGuard(void* handle)
    : handle_(handle)
{
}

LauncherInstanceGuard::LauncherInstanceGuard(
    LauncherInstanceGuard&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
{
}

auto LauncherInstanceGuard::operator=(
    LauncherInstanceGuard&& other) noexcept
    -> LauncherInstanceGuard&
{
    if (this != &other)
    {
        if (handle_ != nullptr)
        {
            static_cast<void>(
                CloseHandle(static_cast<HANDLE>(handle_)));
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

LauncherInstanceGuard::~LauncherInstanceGuard()
{
    if (handle_ != nullptr)
    {
        static_cast<void>(
            CloseHandle(static_cast<HANDLE>(handle_)));
    }
}

auto acquire_launcher_instance()
    -> std::expected<LauncherInstanceGuard, SingleInstanceError>
{
    const auto handle = CreateMutexW(
        nullptr,
        FALSE,
        LauncherMutexName);
    if (handle == nullptr)
    {
        return error(
            SingleInstanceErrorCode::System,
            "The launcher mutex could not be created (Windows error " +
                std::to_string(GetLastError()) + ").");
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        static_cast<void>(CloseHandle(handle));
        return error(
            SingleInstanceErrorCode::AlreadyRunning,
            "Another MecchaCamouflage launcher is already running.");
    }
    return LauncherInstanceGuard{handle};
}
} // namespace meccha::launcher
