#include <meccha/launcher/execution.hpp>

#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
auto error(
    LauncherExecutionStage stage,
    std::string detail)
    -> std::unexpected<LauncherExecutionError>
{
    return std::unexpected(LauncherExecutionError{
        stage,
        std::move(detail),
    });
}

auto effect_error(
    LauncherExecutionStage stage,
    const LauncherEffectError& cause)
    -> std::unexpected<LauncherExecutionError>
{
    return error(stage, cause.detail);
}

auto mutates(ArtifactDisposition disposition) -> bool
{
    return disposition == ArtifactDisposition::CreateOwned ||
           disposition == ArtifactDisposition::ReplaceOwned;
}

auto valid_loader_plan(const ManagedLoaderPlan& plan) -> bool
{
    const auto proxy_valid =
        plan.proxy == ArtifactDisposition::CreateOwned ||
        plan.proxy == ArtifactDisposition::ReuseOwned ||
        plan.proxy == ArtifactDisposition::ReuseUnowned ||
        plan.proxy == ArtifactDisposition::ReplaceOwned;
    const auto override_valid =
        plan.override_file == ArtifactDisposition::CreateOwned ||
        plan.override_file == ArtifactDisposition::ReuseOwned ||
        plan.override_file == ArtifactDisposition::ReplaceOwned;
    return proxy_valid && override_valid &&
           (mutates(plan.proxy) ||
            mutates(plan.override_file));
}
} // namespace

auto execute_preparation(
    const PreparationPlan& plan,
    LauncherExecutionBackend& backend)
    -> std::expected<
        PreparationExecutionResult,
        LauncherExecutionError>
{
    auto result = PreparationExecutionResult{};
    if (plan.mode == DeploymentMode::Managed)
    {
        if (plan.runtime_cache == RuntimeCacheAction::None ||
            plan.shared_mod ||
            (plan.loader && !valid_loader_plan(*plan.loader)))
        {
            return error(
                LauncherExecutionStage::Plan,
                "The managed preparation execution plan is "
                "invalid.");
        }
        auto runtime = backend.prepare_runtime_cache(
            plan.runtime_cache);
        if (!runtime)
        {
            return effect_error(
                LauncherExecutionStage::RuntimeCache,
                runtime.error());
        }
        result.runtime_cache = plan.runtime_cache;

        if (plan.loader)
        {
            auto loader =
                backend.apply_managed_loader(*plan.loader);
            if (!loader)
            {
                return effect_error(
                    LauncherExecutionStage::ManagedLoader,
                    loader.error());
            }
            result.managed_loader_applied = true;
        }
    }
    else if (plan.mode == DeploymentMode::Shared)
    {
        if (plan.runtime_cache != RuntimeCacheAction::None ||
            plan.loader || !plan.shared_mod)
        {
            return error(
                LauncherExecutionStage::Plan,
                "The shared preparation execution plan is "
                "invalid.");
        }
        auto shared = backend.apply_shared_mod(*plan.shared_mod);
        if (!shared)
        {
            return effect_error(
                LauncherExecutionStage::SharedMod,
                shared.error());
        }
        result.shared_mod_applied = true;
    }
    else
    {
        return error(
            LauncherExecutionStage::Plan,
            "The preparation execution mode is not actionable.");
    }

    if (plan.launch_steam)
    {
        auto launched = backend.launch_steam();
        if (!launched)
        {
            return effect_error(
                LauncherExecutionStage::Steam,
                launched.error());
        }
        result.steam_launched = true;
    }
    return result;
}

auto execute_removal(
    RemovalMode mode,
    const RemovalPlan& plan,
    LauncherExecutionBackend& backend)
    -> std::expected<
        RemovalExecutionResult,
        LauncherExecutionError>
{
    const auto has_loader_action =
        plan.proxy == RemovalAction::RemoveOwned ||
        plan.override_file == RemovalAction::RemoveOwned;
    auto result = RemovalExecutionResult{};
    if (mode == RemovalMode::Managed)
    {
        if (plan.mod != RemovalAction::None ||
            (plan.elevated_loader && !has_loader_action))
        {
            return error(
                LauncherExecutionStage::Plan,
                "The managed removal execution plan is invalid.");
        }
        if (has_loader_action)
        {
            auto loader = backend.remove_managed_loader(plan);
            if (!loader)
            {
                return effect_error(
                    LauncherExecutionStage::ManagedLoader,
                    loader.error());
            }
            result.managed_loader_removed = true;
        }
        if (plan.runtime_cache == RemovalAction::RemoveOwned)
        {
            auto runtime = backend.remove_runtime_cache();
            if (!runtime)
            {
                return effect_error(
                    LauncherExecutionStage::RuntimeCache,
                    runtime.error());
            }
            result.runtime_cache_removed = true;
        }
    }
    else if (mode == RemovalMode::Shared)
    {
        if (plan.runtime_cache != RemovalAction::None ||
            has_loader_action || plan.elevated_loader)
        {
            return error(
                LauncherExecutionStage::Plan,
                "The shared removal execution plan is invalid.");
        }
        if (plan.mod == RemovalAction::RemoveOwned)
        {
            auto shared = backend.remove_shared_mod(plan);
            if (!shared)
            {
                return effect_error(
                    LauncherExecutionStage::SharedMod,
                    shared.error());
            }
            result.shared_mod_removed = true;
        }
    }
    else if (mode == RemovalMode::None)
    {
        if (plan.runtime_cache != RemovalAction::None ||
            has_loader_action ||
            plan.mod != RemovalAction::None ||
            plan.elevated_loader)
        {
            return error(
                LauncherExecutionStage::Plan,
                "The empty removal execution plan is invalid.");
        }
    }
    else
    {
        return error(
            LauncherExecutionStage::Plan,
            "The removal execution mode is not actionable.");
    }
    return result;
}
} // namespace meccha::launcher
