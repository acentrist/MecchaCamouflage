#pragma once

#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/loader.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace meccha::launcher
{
enum class LoaderObservationErrorCode : std::uint8_t
{
    Arguments,
    Override,
    Io,
};

struct LoaderObservationError
{
    LoaderObservationErrorCode code{};
    std::string detail{};

    auto operator==(const LoaderObservationError&) const -> bool = default;
};

struct Ue4ssLaunchArgument
{
    std::string path{};

    auto operator==(const Ue4ssLaunchArgument&) const -> bool = default;
};

struct LoaderFilesystemRequest
{
    std::filesystem::path game_directory{};
    DirectiveState command_line{DirectiveState::Absent};
    std::optional<std::filesystem::path> command_line_target{};
    DirectiveState override_file{DirectiveState::Absent};
    Sha256Digest pinned_runtime_sha256{};
};

struct LoaderFilesystemObservation
{
    LoaderChainObservation chain{};
    std::optional<std::filesystem::path> command_line_target{};
    std::optional<std::filesystem::path> override_target{};

    auto operator==(const LoaderFilesystemObservation&) const
        -> bool = default;
};

[[nodiscard]] auto analyze_ue4ss_arguments(
    std::span<const std::string_view> arguments)
    -> std::expected<std::optional<Ue4ssLaunchArgument>,
                     LoaderObservationError>;

[[nodiscard]] auto resolve_command_line_target(
    const std::filesystem::path& game_directory,
    std::string_view argument)
    -> std::expected<std::filesystem::path, LoaderObservationError>;

[[nodiscard]] auto parse_override_target(
    std::string_view contents,
    const std::filesystem::path& game_directory)
    -> std::expected<std::filesystem::path, LoaderObservationError>;

[[nodiscard]] auto observe_loader_filesystem(
    const LoaderFilesystemRequest& request)
    -> std::expected<LoaderChainObservation, LoaderObservationError>;

[[nodiscard]] auto observe_loader_filesystem_details(
    const LoaderFilesystemRequest& request)
    -> std::expected<
        LoaderFilesystemObservation,
        LoaderObservationError>;

#ifdef _WIN32
[[nodiscard]] auto analyze_windows_launch_options(
    std::string_view utf8_options,
    const std::filesystem::path& game_directory)
    -> std::expected<
        std::optional<std::filesystem::path>,
        LoaderObservationError>;
#endif
} // namespace meccha::launcher
