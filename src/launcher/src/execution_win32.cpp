#include <meccha/launcher/execution_win32.hpp>

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

Win32LauncherExecutionBackend::Win32LauncherExecutionBackend(
    RuntimeStorage& runtime_storage,
    Sha256Digest manifest_sha256,
    std::string runtime_nonce,
    std::filesystem::path game_directory,
    std::filesystem::path ownership_directory,
    std::filesystem::path shared_runtime_directory,
    const ManagedLoaderMaterial& managed_loader_material,
    const SharedModMaterial& shared_mod_material,
    ElevatedLoaderBroker& elevated_loader_broker,
    SteamGameLauncher& steam_launcher)
    : runtime_storage_(runtime_storage),
      manifest_sha256_(manifest_sha256),
      runtime_nonce_(std::move(runtime_nonce)),
      game_directory_(std::move(game_directory)),
      ownership_directory_(std::move(ownership_directory)),
      shared_runtime_directory_(
          std::move(shared_runtime_directory)),
      managed_loader_material_(managed_loader_material),
      shared_mod_material_(shared_mod_material),
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
    if (plan.elevated)
    {
        return elevated_loader_broker_.apply(
            plan,
            game_directory_,
            ownership_directory_,
            managed_loader_material_);
    }
    auto applied = apply_managed_loader_plan(
        plan,
        game_directory_,
        ownership_directory_,
        managed_loader_material_);
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
    auto applied = apply_shared_mod_plan(
        action,
        shared_runtime_directory_,
        ownership_directory_,
        shared_mod_material_);
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
    auto removed = apply_shared_mod_removal(
        plan,
        shared_runtime_directory_,
        ownership_directory_,
        shared_mod_material_);
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
