#pragma once

#include <meccha/launcher/execution.hpp>
#include <meccha/launcher/managed_loader.hpp>
#include <meccha/launcher/shared_mod.hpp>
#include <meccha/launcher/transaction.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace meccha::launcher
{
#ifdef _WIN32
inline constexpr std::wstring_view TargetSteamGameUri{
    L"steam://rungameid/4704690"};

class ElevatedLoaderBroker
{
public:
    ElevatedLoaderBroker() = default;
    ElevatedLoaderBroker(const ElevatedLoaderBroker&) = delete;
    auto operator=(const ElevatedLoaderBroker&)
        -> ElevatedLoaderBroker& = delete;
    ElevatedLoaderBroker(ElevatedLoaderBroker&&) = delete;
    auto operator=(ElevatedLoaderBroker&&)
        -> ElevatedLoaderBroker& = delete;
    virtual ~ElevatedLoaderBroker() = default;

    virtual auto apply(
        const ManagedLoaderPlan& plan,
        const std::filesystem::path& game_directory,
        const std::filesystem::path& ownership_directory,
        const ManagedLoaderMaterial& material)
        -> std::expected<void, LauncherEffectError> = 0;

    virtual auto remove(
        const RemovalPlan& plan,
        const std::filesystem::path& game_directory,
        const std::filesystem::path& ownership_directory)
        -> std::expected<void, LauncherEffectError> = 0;
};

class SteamGameLauncher
{
public:
    SteamGameLauncher() = default;
    SteamGameLauncher(const SteamGameLauncher&) = delete;
    auto operator=(const SteamGameLauncher&)
        -> SteamGameLauncher& = delete;
    SteamGameLauncher(SteamGameLauncher&&) = delete;
    auto operator=(SteamGameLauncher&&)
        -> SteamGameLauncher& = delete;
    virtual ~SteamGameLauncher() = default;

    virtual auto launch()
        -> std::expected<void, LauncherEffectError> = 0;
};

class Win32SteamGameLauncher final : public SteamGameLauncher
{
public:
    auto launch()
        -> std::expected<void, LauncherEffectError> override;
};

class LauncherMaterialProvider
{
public:
    LauncherMaterialProvider() = default;
    LauncherMaterialProvider(const LauncherMaterialProvider&) = delete;
    auto operator=(const LauncherMaterialProvider&)
        -> LauncherMaterialProvider& = delete;
    LauncherMaterialProvider(LauncherMaterialProvider&&) = delete;
    auto operator=(LauncherMaterialProvider&&)
        -> LauncherMaterialProvider& = delete;
    virtual ~LauncherMaterialProvider() = default;

    virtual auto managed_loader()
        -> std::expected<
            const ManagedLoaderMaterial*,
            LauncherEffectError> = 0;

    virtual auto shared_mod()
        -> std::expected<
            const SharedModMaterial*,
            LauncherEffectError> = 0;
};

class Win32LauncherMaterialProvider final
    : public LauncherMaterialProvider
{
public:
    Win32LauncherMaterialProvider(
        const PayloadManifest& manifest,
        Sha256Digest manifest_sha256,
        std::filesystem::path active_runtime_directory,
        RuntimePayloadSource& payload_source);

    auto managed_loader()
        -> std::expected<
            const ManagedLoaderMaterial*,
            LauncherEffectError> override;

    auto shared_mod()
        -> std::expected<
            const SharedModMaterial*,
            LauncherEffectError> override;

private:
    const PayloadManifest& manifest_;
    Sha256Digest manifest_sha256_{};
    std::filesystem::path active_runtime_directory_{};
    RuntimePayloadSource& payload_source_;
    std::optional<ManagedLoaderMaterial> managed_loader_{};
    std::optional<SharedModMaterial> shared_mod_{};
};

class Win32LauncherExecutionBackend final
    : public LauncherExecutionBackend
{
public:
    Win32LauncherExecutionBackend(
        RuntimeStorage& runtime_storage,
        Sha256Digest manifest_sha256,
        std::string runtime_nonce,
        std::filesystem::path game_directory,
        std::filesystem::path ownership_directory,
        std::filesystem::path shared_runtime_directory,
        LauncherMaterialProvider& material_provider,
        ElevatedLoaderBroker& elevated_loader_broker,
        SteamGameLauncher& steam_launcher);

    auto prepare_runtime_cache(RuntimeCacheAction action)
        -> std::expected<void, LauncherEffectError> override;

    auto apply_managed_loader(const ManagedLoaderPlan& plan)
        -> std::expected<void, LauncherEffectError> override;

    auto apply_shared_mod(SharedModAction action)
        -> std::expected<void, LauncherEffectError> override;

    auto remove_managed_loader(const RemovalPlan& plan)
        -> std::expected<void, LauncherEffectError> override;

    auto remove_runtime_cache()
        -> std::expected<void, LauncherEffectError> override;

    auto remove_shared_mod(const RemovalPlan& plan)
        -> std::expected<void, LauncherEffectError> override;

    auto launch_steam()
        -> std::expected<void, LauncherEffectError> override;

private:
    RuntimeStorage& runtime_storage_;
    Sha256Digest manifest_sha256_{};
    std::string runtime_nonce_{};
    std::filesystem::path game_directory_{};
    std::filesystem::path ownership_directory_{};
    std::filesystem::path shared_runtime_directory_{};
    LauncherMaterialProvider& material_provider_;
    ElevatedLoaderBroker& elevated_loader_broker_;
    SteamGameLauncher& steam_launcher_;
};
#endif
} // namespace meccha::launcher
