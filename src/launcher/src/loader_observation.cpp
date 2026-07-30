#include <meccha/launcher/loader_observation.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;
constexpr std::uintmax_t MaxOverrideBytes = 32U * 1024U;

auto error(LoaderObservationErrorCode code, std::string detail)
    -> std::unexpected<LoaderObservationError>
{
    return std::unexpected(LoaderObservationError{
        code,
        std::move(detail),
    });
}

auto utf8_path(std::string_view value) -> fs::path
{
    auto encoded = std::u8string{};
    encoded.reserve(value.size());
    for (const auto character : value)
    {
        encoded.push_back(static_cast<char8_t>(
            static_cast<unsigned char>(character)));
    }
    return fs::path{encoded};
}

auto resolve_relative(
    const fs::path& game_directory,
    const fs::path& candidate) -> fs::path
{
    if (candidate.is_absolute())
    {
        return candidate.lexically_normal();
    }
    return (game_directory / candidate).lexically_normal();
}

auto candidate_identity(
    const fs::path& path,
    const Sha256Digest& expected) -> CandidateIdentity
{
    std::error_code status_error{};
    const auto status = fs::status(path, status_error);
    if (status_error)
    {
        if (status_error == std::errc::no_such_file_or_directory)
        {
            return CandidateIdentity::Missing;
        }
        return CandidateIdentity::Unreadable;
    }
    if (!fs::exists(status))
    {
        return CandidateIdentity::Missing;
    }
    if (!fs::is_regular_file(status))
    {
        return CandidateIdentity::Incompatible;
    }
    const auto hash = sha256_file(path);
    if (!hash)
    {
        return CandidateIdentity::Unreadable;
    }
    return hash->sha256 == expected
               ? CandidateIdentity::Pinned
               : CandidateIdentity::Incompatible;
}

auto read_override(const fs::path& path)
    -> std::expected<std::string, LoaderObservationError>
{
    std::error_code size_error{};
    const auto size = fs::file_size(path, size_error);
    if (size_error)
    {
        return error(
            LoaderObservationErrorCode::Io,
            "override.txt size could not be read");
    }
    if (size > MaxOverrideBytes)
    {
        return error(
            LoaderObservationErrorCode::Override,
            "override.txt exceeds the size limit");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input)
    {
        return error(
            LoaderObservationErrorCode::Io,
            "override.txt could not be opened");
    }
    auto contents = std::string(static_cast<std::size_t>(size), '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!input || input.peek() != std::char_traits<char>::eof())
    {
        return error(
            LoaderObservationErrorCode::Io,
            "override.txt could not be read completely");
    }
    return contents;
}
} // namespace

auto analyze_ue4ss_arguments(
    std::span<const std::string_view> arguments)
    -> std::expected<
        std::optional<Ue4ssLaunchArgument>,
        LoaderObservationError>
{
    auto result = std::optional<Ue4ssLaunchArgument>{};
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        if (arguments[index] == "--disable-ue4ss")
        {
            return error(
                LoaderObservationErrorCode::Arguments,
                "--disable-ue4ss prevents a valid MecchaCamouflage launch");
        }
        if (arguments[index] != "--ue4ss-path")
        {
            continue;
        }
        if (result)
        {
            return error(
                LoaderObservationErrorCode::Arguments,
                "--ue4ss-path appears more than once");
        }
        if (index + 1U == arguments.size() ||
            arguments[index + 1U].empty())
        {
            return error(
                LoaderObservationErrorCode::Arguments,
                "--ue4ss-path has no path argument");
        }
        result = Ue4ssLaunchArgument{
            std::string{arguments[++index]},
        };
    }
    return result;
}

auto resolve_command_line_target(
    const fs::path& game_directory,
    std::string_view argument)
    -> std::expected<fs::path, LoaderObservationError>
{
    if (argument.empty() ||
        argument.find('\0') != std::string_view::npos)
    {
        return error(
            LoaderObservationErrorCode::Arguments,
            "--ue4ss-path has an empty or invalid path");
    }
    const auto path = utf8_path(argument);
    if (path.filename().empty())
    {
        return error(
            LoaderObservationErrorCode::Arguments,
            "--ue4ss-path must identify a DLL file");
    }
    return resolve_relative(game_directory, path);
}

auto parse_override_target(
    std::string_view contents,
    const fs::path& game_directory)
    -> std::expected<fs::path, LoaderObservationError>
{
    if (contents.empty() || contents.size() > MaxOverrideBytes ||
        contents.find('\0') != std::string_view::npos)
    {
        return error(
            LoaderObservationErrorCode::Override,
            "override.txt is empty, oversized, or contains NUL");
    }

    if (contents.ends_with('\n'))
    {
        contents.remove_suffix(1U);
        if (contents.ends_with('\r'))
        {
            contents.remove_suffix(1U);
        }
    }
    if (contents.empty() ||
        contents.find_first_of("\r\n") != std::string_view::npos)
    {
        return error(
            LoaderObservationErrorCode::Override,
            "override.txt must contain exactly one path line");
    }
    if (contents.starts_with("\xEF\xBB\xBF"))
    {
        return error(
            LoaderObservationErrorCode::Override,
            "override.txt must not contain a UTF-8 BOM");
    }

    const auto directory = utf8_path(contents);
    if (directory.empty())
    {
        return error(
            LoaderObservationErrorCode::Override,
            "override.txt path is empty");
    }
    return resolve_relative(
        game_directory,
        directory) /
           "UE4SS.dll";
}

auto observe_loader_filesystem_details(
    const LoaderFilesystemRequest& request)
    -> std::expected<
        LoaderFilesystemObservation,
        LoaderObservationError>
{
    std::error_code directory_error{};
    if (!fs::is_directory(request.game_directory, directory_error) ||
        directory_error)
    {
        return error(
            LoaderObservationErrorCode::Io,
            "the game executable directory is unavailable");
    }

    auto result = LoaderFilesystemObservation{};
    auto& observation = result.chain;
    observation.command_line = request.command_line;
    if (request.command_line == DirectiveState::Absent)
    {
        if (request.command_line_target)
        {
            observation.command_line = DirectiveState::Invalid;
        }
    }
    else if (request.command_line == DirectiveState::Unowned &&
             request.command_line_target)
    {
        result.command_line_target =
            request.command_line_target;
        observation.command_target = candidate_identity(
            *request.command_line_target,
            request.pinned_runtime_sha256);
    }
    else
    {
        observation.command_line = DirectiveState::Invalid;
    }

    const auto override_path = request.game_directory / "override.txt";
    std::error_code override_error{};
    const auto override_exists =
        fs::exists(override_path, override_error);
    if (override_error)
    {
        observation.override_file = DirectiveState::Invalid;
    }
    else if (!override_exists)
    {
        observation.override_file =
            request.override_file == DirectiveState::Absent
                ? DirectiveState::Absent
                : DirectiveState::Invalid;
    }
    else if (
        request.override_file == DirectiveState::Owned ||
        request.override_file == DirectiveState::Unowned)
    {
        observation.override_file = request.override_file;
        const auto contents = read_override(override_path);
        if (!contents)
        {
            observation.override_file = DirectiveState::Invalid;
        }
        else
        {
            const auto target =
                parse_override_target(*contents, request.game_directory);
            if (!target)
            {
                observation.override_file = DirectiveState::Invalid;
            }
            else
            {
                result.override_target = *target;
                observation.override_target = candidate_identity(
                    *target,
                    request.pinned_runtime_sha256);
            }
        }
    }
    else
    {
        observation.override_file = DirectiveState::Invalid;
    }

    observation.conventional_subdirectory = candidate_identity(
        request.game_directory / "ue4ss" / "UE4SS.dll",
        request.pinned_runtime_sha256);
    observation.conventional_root = candidate_identity(
        request.game_directory / "UE4SS.dll",
        request.pinned_runtime_sha256);
    return result;
}

auto observe_loader_filesystem(const LoaderFilesystemRequest& request)
    -> std::expected<LoaderChainObservation, LoaderObservationError>
{
    const auto result =
        observe_loader_filesystem_details(request);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    return result->chain;
}
} // namespace meccha::launcher
