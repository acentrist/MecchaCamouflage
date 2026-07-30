#pragma once

#include <meccha/launcher/command_line.hpp>
#include <meccha/launcher/composition_win32.hpp>
#include <meccha/launcher/game_discovery.hpp>
#include <meccha/launcher/single_instance.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace meccha::launcher
{
#ifdef _WIN32
enum class LauncherBootstrapErrorCode : std::uint8_t
{
    KnownFolder,
    InvalidPath,
    Nonce,
};

struct LauncherBootstrapError
{
    LauncherBootstrapErrorCode code{};
    std::string detail{};

    auto operator==(const LauncherBootstrapError&) const -> bool = default;
};

struct Win32LauncherDataPaths
{
    std::filesystem::path data_root{};
    std::filesystem::path runtime_root{};
    std::filesystem::path ownership_directory{};

    auto operator==(const Win32LauncherDataPaths&) const -> bool = default;
};

class LauncherBootstrapPlatform
{
public:
    LauncherBootstrapPlatform() = default;
    LauncherBootstrapPlatform(const LauncherBootstrapPlatform&) = delete;
    auto operator=(const LauncherBootstrapPlatform&)
        -> LauncherBootstrapPlatform& = delete;
    LauncherBootstrapPlatform(LauncherBootstrapPlatform&&) = delete;
    auto operator=(LauncherBootstrapPlatform&&)
        -> LauncherBootstrapPlatform& = delete;
    virtual ~LauncherBootstrapPlatform() = default;

    virtual auto validate_explicit_game_directory(
        std::string_view value)
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> = 0;

    virtual auto discover_game_installation()
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> = 0;

    virtual auto pick_game_installation()
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> = 0;

    virtual auto local_app_data_directory()
        -> std::expected<
            std::filesystem::path,
            LauncherBootstrapError> = 0;

    virtual auto runtime_nonce()
        -> std::expected<
            std::string,
            LauncherBootstrapError> = 0;
};

class Win32LauncherBootstrapPlatform final
    : public LauncherBootstrapPlatform
{
public:
    auto validate_explicit_game_directory(
        std::string_view value)
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> override;

    auto discover_game_installation()
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> override;

    auto pick_game_installation()
        -> std::expected<
            GameInstallation,
            GameDiscoveryError> override;

    auto local_app_data_directory()
        -> std::expected<
            std::filesystem::path,
            LauncherBootstrapError> override;

    auto runtime_nonce()
        -> std::expected<
            std::string,
            LauncherBootstrapError> override;
};

struct LoadedLauncherPackage
{
    std::string manifest_json{};
    PayloadManifest manifest{};
    Sha256Digest manifest_sha256{};
    std::unique_ptr<RuntimePayloadSource> payload_source{};
};

class LauncherPackageSource
{
public:
    LauncherPackageSource() = default;
    LauncherPackageSource(const LauncherPackageSource&) = delete;
    auto operator=(const LauncherPackageSource&)
        -> LauncherPackageSource& = delete;
    LauncherPackageSource(LauncherPackageSource&&) = delete;
    auto operator=(LauncherPackageSource&&)
        -> LauncherPackageSource& = delete;
    virtual ~LauncherPackageSource() = default;

    virtual auto load(
        const std::filesystem::path& scratch_parent,
        LauncherInvocationMode mode)
        -> std::expected<
            LoadedLauncherPackage,
            RuntimePayloadError> = 0;
};

struct Win32LauncherApplicationInputs
{
    std::span<const std::string_view> arguments{};
    LauncherBootstrapPlatform& bootstrap_platform;
    LauncherPackageSource& package_source;
    OriginalUserObservationPlatform& observation_platform;
    ElevatedLoaderBroker& elevated_loader_broker;
    SteamGameLauncher& steam_launcher;
};

struct Win32LauncherApplicationResult
{
    LauncherArguments arguments{};
    GameInstallation installation{};
    Win32LauncherDataPaths paths{};
    Win32LauncherCompositionResult composition{};
};

using Win32LauncherApplicationError = std::variant<
    SingleInstanceError,
    LauncherArgumentError,
    GameDiscoveryError,
    LauncherBootstrapError,
    RuntimePayloadError,
    LauncherObservationError,
    RuntimeTransactionError,
    LauncherWorkflowError>;

[[nodiscard]] auto make_launcher_data_paths(
    const std::filesystem::path& local_app_data_directory)
    -> std::expected<
        Win32LauncherDataPaths,
        LauncherBootstrapError>;

[[nodiscard]] auto resolve_launcher_game_installation(
    const LauncherArguments& arguments,
    LauncherBootstrapPlatform& platform)
    -> std::expected<GameInstallation, GameDiscoveryError>;

[[nodiscard]] auto run_win32_launcher_application(
    const Win32LauncherApplicationInputs& inputs)
    -> std::expected<
        Win32LauncherApplicationResult,
        Win32LauncherApplicationError>;
#endif
} // namespace meccha::launcher
