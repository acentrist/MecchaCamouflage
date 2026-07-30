#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::launcher
{
inline constexpr std::string_view TargetSteamAppId{"4704690"};
inline constexpr std::string_view TargetGameExecutable{
    "PenguinHotel-Win64-Shipping.exe"};

enum class GameDiscoveryErrorCode : std::uint8_t
{
    Io,
    MalformedVdf,
    InvalidAppManifest,
    InvalidInstallDirectory,
    MissingGame,
    AmbiguousGame,
    Registry,
    ProcessEnumeration,
};

struct GameDiscoveryError
{
    GameDiscoveryErrorCode code{};
    std::string detail{};

    auto operator==(const GameDiscoveryError&) const -> bool = default;
};

struct GameInstallation
{
    std::filesystem::path steam_library{};
    std::filesystem::path install_directory{};
    std::filesystem::path binaries_directory{};
    std::filesystem::path executable{};

    auto operator==(const GameInstallation&) const -> bool = default;
};

[[nodiscard]] auto parse_steam_library_folders(std::string_view vdf)
    -> std::expected<std::vector<std::string>, GameDiscoveryError>;

[[nodiscard]] auto parse_app_install_directory(std::string_view vdf)
    -> std::expected<std::string, GameDiscoveryError>;

[[nodiscard]] auto discover_game_installation(
    std::span<const std::filesystem::path> steam_roots)
    -> std::expected<GameInstallation, GameDiscoveryError>;

[[nodiscard]] auto validate_game_directory(
    const std::filesystem::path& selected_directory)
    -> std::expected<GameInstallation, GameDiscoveryError>;

#ifdef _WIN32
[[nodiscard]] auto discover_windows_steam_roots()
    -> std::expected<std::vector<std::filesystem::path>, GameDiscoveryError>;

[[nodiscard]] auto is_process_running_by_image_name(
    std::wstring_view image_name)
    -> std::expected<bool, GameDiscoveryError>;

[[nodiscard]] auto is_target_game_running()
    -> std::expected<bool, GameDiscoveryError>;
#endif
} // namespace meccha::launcher
