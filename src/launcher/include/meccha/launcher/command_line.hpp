#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace meccha::launcher
{
enum class LauncherInvocationMode : std::uint8_t
{
    PrepareAndLaunch,
    PrepareOnly,
    Remove,
};

struct LauncherArguments
{
    LauncherInvocationMode mode{
        LauncherInvocationMode::PrepareAndLaunch};
    std::optional<std::string> game_directory_utf8{};

    auto operator==(const LauncherArguments&) const -> bool = default;
};

enum class LauncherArgumentErrorCode : std::uint8_t
{
    UnknownArgument,
    MissingValue,
    DuplicateArgument,
    ConflictingMode,
    InvalidValue,
};

struct LauncherArgumentError
{
    LauncherArgumentErrorCode code{};
    std::string detail{};

    auto operator==(const LauncherArgumentError&) const -> bool = default;
};

[[nodiscard]] auto parse_launcher_arguments(
    std::span<const std::string_view> arguments)
    -> std::expected<LauncherArguments, LauncherArgumentError>;
} // namespace meccha::launcher
