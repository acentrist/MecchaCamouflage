#include <meccha/launcher/preparation.hpp>

#include <expected>
#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
auto error(
    PreparationErrorCode code,
    std::string detail,
    ConflictReason conflict = ConflictReason::None)
    -> std::unexpected<PreparationError>
{
    return std::unexpected(PreparationError{
        code,
        conflict,
        std::move(detail),
    });
}

auto mutates(ArtifactDisposition disposition) -> bool
{
    return disposition == ArtifactDisposition::CreateOwned ||
           disposition == ArtifactDisposition::ReplaceOwned;
}

auto managed_proxy_valid(ArtifactDisposition disposition) -> bool
{
    return disposition == ArtifactDisposition::CreateOwned ||
           disposition == ArtifactDisposition::ReuseOwned ||
           disposition == ArtifactDisposition::ReuseUnowned ||
           disposition == ArtifactDisposition::ReplaceOwned;
}

auto managed_override_valid(ArtifactDisposition disposition) -> bool
{
    return disposition == ArtifactDisposition::CreateOwned ||
           disposition == ArtifactDisposition::ReuseOwned ||
           disposition == ArtifactDisposition::ReplaceOwned;
}

auto managed_mod_valid(ArtifactDisposition disposition) -> bool
{
    return disposition == ArtifactDisposition::CreateOwned ||
           disposition == ArtifactDisposition::ReuseOwned ||
           disposition == ArtifactDisposition::ReplaceOwned;
}

auto owned(ArtifactState state) -> bool
{
    return state == ArtifactState::ExactOwned ||
           state == ArtifactState::OwnedPrevious;
}

auto removal_error(
    RemovalErrorCode code,
    std::string detail) -> std::unexpected<RemovalError>
{
    return std::unexpected(RemovalError{
        code,
        std::move(detail),
    });
}
} // namespace

auto PreparationPlan::requires_elevation() const -> bool
{
    return loader && loader->elevated;
}

auto plan_preparation(const PreparationObservation& observation)
    -> std::expected<PreparationPlan, PreparationError>
{
    if (observation.game_running)
    {
        return error(
            PreparationErrorCode::GameRunning,
            "Preparation is blocked while the game is running.");
    }
    if (!observation.payload_valid)
    {
        return error(
            PreparationErrorCode::Payload,
            "The embedded payload manifest or bytes are invalid.");
    }
    if (observation.deployment.mode == DeploymentMode::Conflict)
    {
        return error(
            PreparationErrorCode::DeploymentConflict,
            "The existing loader deployment is incompatible.",
            observation.deployment.conflict);
    }

    auto plan = PreparationPlan{
        observation.deployment.mode,
        RuntimeCacheAction::None,
        std::nullopt,
        std::nullopt,
        observation.command ==
            PreparationCommand::PrepareAndLaunch,
    };

    if (observation.deployment.mode == DeploymentMode::Managed)
    {
        if (!managed_proxy_valid(observation.deployment.proxy) ||
            !managed_override_valid(
                observation.deployment.override_file) ||
            !managed_mod_valid(observation.deployment.mod))
        {
            return error(
                PreparationErrorCode::InvalidDecision,
                "Managed deployment decision is internally invalid.");
        }
        if (observation.runtime_cache ==
            RuntimeCacheState::Conflict)
        {
            return error(
                PreparationErrorCode::RuntimeCacheConflict,
                "The managed runtime cache is not safely replaceable.");
        }
        if (observation.runtime_cache ==
            RuntimeCacheState::PublishRequired)
        {
            if (!observation.user_cache_writable)
            {
                return error(
                    PreparationErrorCode::UserCacheAccess,
                    "The original user cannot write the LocalAppData "
                    "runtime cache.");
            }
            plan.runtime_cache = RuntimeCacheAction::Publish;
        }
        else
        {
            plan.runtime_cache = RuntimeCacheAction::Reuse;
        }

        if (mutates(observation.deployment.proxy) ||
            mutates(observation.deployment.override_file))
        {
            plan.loader = ManagedLoaderPlan{
                observation.deployment.proxy,
                observation.deployment.override_file,
                !observation.game_directory_writable,
            };
        }
        return plan;
    }

    if (observation.deployment.mode == DeploymentMode::Shared)
    {
        if (observation.deployment.proxy !=
                ArtifactDisposition::None ||
            observation.deployment.override_file !=
                ArtifactDisposition::None)
        {
            return error(
                PreparationErrorCode::InvalidDecision,
                "Shared deployment must not mutate loader files.");
        }

        switch (observation.deployment.mod)
        {
        case ArtifactDisposition::CreateOwned:
        case ArtifactDisposition::ReplaceOwned:
            if (!observation.shared_runtime_writable)
            {
                return error(
                    PreparationErrorCode::SharedRuntimeAccess,
                    "The shared UE4SS mod directory is not writable "
                    "without elevation.");
            }
            plan.shared_mod = SharedModAction::Install;
            break;
        case ArtifactDisposition::ReuseOwned:
        case ArtifactDisposition::ReuseUnowned:
            plan.shared_mod = SharedModAction::Reuse;
            break;
        case ArtifactDisposition::None:
            return error(
                PreparationErrorCode::InvalidDecision,
                "Shared deployment has no valid mod disposition.");
        }
        return plan;
    }

    return error(
        PreparationErrorCode::InvalidDecision,
        "Deployment mode is not actionable.");
}

auto RemovalPlan::requires_elevation() const -> bool
{
    return elevated_loader;
}

auto plan_removal(const RemovalObservation& observation)
    -> std::expected<RemovalPlan, RemovalError>
{
    if (observation.game_running)
    {
        return removal_error(
            RemovalErrorCode::GameRunning,
            "Removal is blocked while the game is running.");
    }
    if (observation.mode == RemovalMode::Conflict)
    {
        return removal_error(
            RemovalErrorCode::OwnershipConflict,
            "The installed deployment cannot be identified safely.");
    }

    auto plan = RemovalPlan{};
    if (observation.mode == RemovalMode::None)
    {
        if (observation.runtime_cache !=
                RemovalCacheState::Missing ||
            observation.proxy != ArtifactState::Missing ||
            observation.override_file != ArtifactState::Missing ||
            observation.mod != ArtifactState::Missing)
        {
            return removal_error(
                RemovalErrorCode::InvalidObservation,
                "No-install removal observation contains artifacts.");
        }
        return plan;
    }

    if (observation.mode == RemovalMode::Managed)
    {
        if (observation.runtime_cache ==
                RemovalCacheState::Conflict ||
            observation.proxy == ArtifactState::Conflict ||
            observation.override_file ==
                ArtifactState::Conflict)
        {
            return removal_error(
                RemovalErrorCode::OwnershipConflict,
                "Managed removal found modified or unknown content.");
        }
        if (observation.runtime_cache ==
                RemovalCacheState::ExactOwned ||
            observation.runtime_cache ==
                RemovalCacheState::OwnedPartial)
        {
            if (!observation.user_cache_writable)
            {
                return removal_error(
                    RemovalErrorCode::UserCacheAccess,
                    "The original user cannot remove the owned "
                    "LocalAppData runtime cache.");
            }
            plan.runtime_cache = RemovalAction::RemoveOwned;
        }
        if (owned(observation.proxy))
        {
            plan.proxy = RemovalAction::RemoveOwned;
        }
        if (owned(observation.override_file))
        {
            plan.override_file = RemovalAction::RemoveOwned;
        }
        plan.elevated_loader =
            (plan.proxy == RemovalAction::RemoveOwned ||
             plan.override_file == RemovalAction::RemoveOwned) &&
            !observation.game_directory_writable;
        return plan;
    }

    if (observation.mode == RemovalMode::Shared)
    {
        if (observation.mod == ArtifactState::Conflict)
        {
            return removal_error(
                RemovalErrorCode::OwnershipConflict,
                "Shared MecchaCamouflage mod content was modified.");
        }
        if (owned(observation.mod))
        {
            if (!observation.shared_runtime_writable)
            {
                return removal_error(
                    RemovalErrorCode::SharedRuntimeAccess,
                    "The shared UE4SS mod directory is not writable "
                    "without elevation.");
            }
            plan.mod = RemovalAction::RemoveOwned;
        }
        return plan;
    }

    return removal_error(
        RemovalErrorCode::InvalidObservation,
        "Removal mode is not actionable.");
}
} // namespace meccha::launcher
