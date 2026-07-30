#include <meccha/launcher/execution_win32.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::launcher
{
namespace
{
auto effect_error(
    std::string_view operation,
    std::string_view detail)
    -> std::unexpected<LauncherEffectError>
{
    return std::unexpected(LauncherEffectError{
        std::string{operation} + ": " + std::string{detail},
    });
}
} // namespace

auto Win32SteamGameLauncher::launch()
    -> std::expected<void, LauncherEffectError>
{
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask =
        SEE_MASK_NOCLOSEPROCESS |
        SEE_MASK_NOASYNC |
        SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"open";
    request.lpFile = TargetSteamGameUri.data();
    request.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&request))
    {
        return effect_error(
            "Steam launch",
            "ShellExecuteExW failed with Windows error " +
                std::to_string(GetLastError()));
    }
    if (request.hProcess != nullptr)
    {
        static_cast<void>(CloseHandle(request.hProcess));
    }
    return {};
}

Win32LauncherMaterialProvider::Win32LauncherMaterialProvider(
    const PayloadManifest& manifest,
    Sha256Digest manifest_sha256,
    std::filesystem::path active_runtime_directory,
    RuntimePayloadSource& payload_source)
    : manifest_(manifest),
      manifest_sha256_(manifest_sha256),
      active_runtime_directory_(
          std::move(active_runtime_directory)),
      payload_source_(payload_source)
{
}

auto Win32LauncherMaterialProvider::managed_loader()
    -> std::expected<
        const ManagedLoaderMaterial*,
        LauncherEffectError>
{
    if (!managed_loader_)
    {
        auto built = build_managed_loader_material(
            manifest_,
            manifest_sha256_,
            active_runtime_directory_,
            payload_source_);
        if (!built)
        {
            return effect_error(
                "Managed loader material",
                built.error().detail);
        }
        managed_loader_ = std::move(*built);
    }
    return std::addressof(*managed_loader_);
}

auto Win32LauncherMaterialProvider::shared_mod()
    -> std::expected<
        const SharedModMaterial*,
        LauncherEffectError>
{
    if (!shared_mod_)
    {
        auto built = build_shared_mod_material(
            manifest_,
            manifest_sha256_,
            payload_source_);
        if (!built)
        {
            return effect_error(
                "Shared mod material",
                built.error().detail);
        }
        shared_mod_ = std::move(*built);
    }
    return std::addressof(*shared_mod_);
}

Win32LauncherExecutionBackend::Win32LauncherExecutionBackend(
    RuntimeStorage& runtime_storage,
    Sha256Digest manifest_sha256,
    std::string runtime_nonce,
    std::filesystem::path game_directory,
    std::filesystem::path ownership_directory,
    std::filesystem::path shared_runtime_directory,
    LauncherMaterialProvider& material_provider,
    ElevatedLoaderBroker& elevated_loader_broker,
    SteamGameLauncher& steam_launcher)
    : runtime_storage_(runtime_storage),
      manifest_sha256_(manifest_sha256),
      runtime_nonce_(std::move(runtime_nonce)),
      game_directory_(std::move(game_directory)),
      ownership_directory_(std::move(ownership_directory)),
      shared_runtime_directory_(
          std::move(shared_runtime_directory)),
      material_provider_(material_provider),
      elevated_loader_broker_(elevated_loader_broker),
      steam_launcher_(steam_launcher)
{
}

auto Win32LauncherExecutionBackend::prepare_runtime_cache(
    RuntimeCacheAction action)
    -> std::expected<void, LauncherEffectError>
{
    if (action == RuntimeCacheAction::Reuse)
    {
        auto validated = validate_runtime_reuse(
            runtime_storage_,
            manifest_sha256_);
        if (!validated)
        {
            return effect_error(
                "Managed runtime reuse",
                validated.error().detail);
        }
        return {};
    }
    if (action == RuntimeCacheAction::Publish)
    {
        auto prepared = prepare_runtime(
            runtime_storage_,
            manifest_sha256_,
            runtime_nonce_);
        if (!prepared)
        {
            return effect_error(
                "Managed runtime publication",
                prepared.error().detail);
        }
        return {};
    }
    return effect_error(
        "Managed runtime",
        "the execution action is invalid");
}

auto Win32LauncherExecutionBackend::apply_managed_loader(
    const ManagedLoaderPlan& plan)
    -> std::expected<void, LauncherEffectError>
{
    const auto material = material_provider_.managed_loader();
    if (!material)
    {
        return std::unexpected(material.error());
    }
    if (plan.elevated)
    {
        return elevated_loader_broker_.apply(
            plan,
            game_directory_,
            ownership_directory_,
            **material);
    }
    auto applied = apply_managed_loader_plan(
        plan,
        game_directory_,
        ownership_directory_,
        **material);
    if (!applied)
    {
        return effect_error(
            "Managed loader application",
            applied.error().detail);
    }
    return {};
}

auto Win32LauncherExecutionBackend::apply_shared_mod(
    SharedModAction action)
    -> std::expected<void, LauncherEffectError>
{
    const auto material = material_provider_.shared_mod();
    if (!material)
    {
        return std::unexpected(material.error());
    }
    auto applied = apply_shared_mod_plan(
        action,
        shared_runtime_directory_,
        ownership_directory_,
        **material);
    if (!applied)
    {
        return effect_error(
            "Shared mod application",
            applied.error().detail);
    }
    return {};
}

auto Win32LauncherExecutionBackend::remove_managed_loader(
    const RemovalPlan& plan)
    -> std::expected<void, LauncherEffectError>
{
    auto loader_plan = plan;
    loader_plan.runtime_cache = RemovalAction::None;
    loader_plan.mod = RemovalAction::None;
    if (loader_plan.elevated_loader)
    {
        return elevated_loader_broker_.remove(
            loader_plan,
            game_directory_,
            ownership_directory_);
    }
    auto removed = apply_managed_loader_removal(
        loader_plan,
        game_directory_,
        ownership_directory_);
    if (!removed)
    {
        return effect_error(
            "Managed loader removal",
            removed.error().detail);
    }
    return {};
}

auto Win32LauncherExecutionBackend::remove_runtime_cache()
    -> std::expected<void, LauncherEffectError>
{
    auto removed = remove_runtime(runtime_storage_);
    if (!removed)
    {
        return effect_error(
            "Managed runtime removal",
            removed.error().detail);
    }
    return {};
}

auto Win32LauncherExecutionBackend::remove_shared_mod(
    const RemovalPlan& plan)
    -> std::expected<void, LauncherEffectError>
{
    const auto material = material_provider_.shared_mod();
    if (!material)
    {
        return std::unexpected(material.error());
    }
    auto removed = apply_shared_mod_removal(
        plan,
        shared_runtime_directory_,
        ownership_directory_,
        **material);
    if (!removed)
    {
        return effect_error(
            "Shared mod removal",
            removed.error().detail);
    }
    return {};
}

auto Win32LauncherExecutionBackend::launch_steam()
    -> std::expected<void, LauncherEffectError>
{
    return steam_launcher_.launch();
}
} // namespace meccha::launcher
