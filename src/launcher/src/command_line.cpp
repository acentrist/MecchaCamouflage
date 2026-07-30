#include <meccha/launcher/command_line.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
auto error(
    LauncherArgumentErrorCode code,
    std::string detail)
    -> std::unexpected<LauncherArgumentError>
{
    return std::unexpected(LauncherArgumentError{
        code,
        std::move(detail),
    });
}
} // namespace

auto parse_launcher_arguments(
    std::span<const std::string_view> arguments)
    -> std::expected<LauncherArguments, LauncherArgumentError>
{
    auto result = LauncherArguments{};
    auto prepare_only = false;
    auto remove = false;
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const auto argument = arguments[index];
        if (argument == "--game-dir")
        {
            if (result.game_directory_utf8)
            {
                return error(
                    LauncherArgumentErrorCode::DuplicateArgument,
                    "--game-dir was specified more than once.");
            }
            if (index + 1U >= arguments.size() ||
                arguments[index + 1U].starts_with("--"))
            {
                return error(
                    LauncherArgumentErrorCode::MissingValue,
                    "--game-dir requires a directory value.");
            }
            const auto value = arguments[++index];
            if (value.empty() ||
                value.find('\0') != std::string_view::npos)
            {
                return error(
                    LauncherArgumentErrorCode::InvalidValue,
                    "--game-dir contains an invalid value.");
            }
            result.game_directory_utf8 = std::string{value};
            continue;
        }
        if (argument == "--prepare-only")
        {
            if (prepare_only)
            {
                return error(
                    LauncherArgumentErrorCode::DuplicateArgument,
                    "--prepare-only was specified more than once.");
            }
            prepare_only = true;
            continue;
        }
        if (argument == "--remove")
        {
            if (remove)
            {
                return error(
                    LauncherArgumentErrorCode::DuplicateArgument,
                    "--remove was specified more than once.");
            }
            remove = true;
            continue;
        }
        return error(
            LauncherArgumentErrorCode::UnknownArgument,
            "Unknown launcher argument: " +
                std::string{argument});
    }
    if (prepare_only && remove)
    {
        return error(
            LauncherArgumentErrorCode::ConflictingMode,
            "--prepare-only and --remove cannot be combined.");
    }
    if (prepare_only)
    {
        result.mode = LauncherInvocationMode::PrepareOnly;
    }
    else if (remove)
    {
        result.mode = LauncherInvocationMode::Remove;
    }
    return result;
}
} // namespace meccha::launcher
