#include <meccha/launcher/observation_assembly.hpp>

namespace meccha::launcher
{
namespace
{
auto launch_option_state(
    const LoaderChainObservation& observation)
    -> LaunchOptionState
{
    if (observation.command_line == DirectiveState::Absent &&
        observation.command_target == CandidateIdentity::Missing)
    {
        return LaunchOptionState::Absent;
    }
    if (observation.command_line == DirectiveState::Unowned)
    {
        if (observation.command_target ==
            CandidateIdentity::Pinned)
        {
            return LaunchOptionState::PinnedRuntime;
        }
        if (observation.command_target ==
            CandidateIdentity::Incompatible)
        {
            return LaunchOptionState::OtherRuntime;
        }
    }
    return LaunchOptionState::Unresolved;
}

auto runtime_state(LoaderResolutionState state) -> RuntimeState
{
    switch (state)
    {
    case LoaderResolutionState::Missing:
        return RuntimeState::Missing;
    case LoaderResolutionState::ManagedCompatible:
        return RuntimeState::ManagedCompatible;
    case LoaderResolutionState::SharedCompatible:
        return RuntimeState::SharedCompatible;
    case LoaderResolutionState::Conflict:
        return RuntimeState::Incompatible;
    }
    return RuntimeState::Incompatible;
}

auto preparation_cache_state(RuntimeCacheIdentity identity)
    -> RuntimeCacheState
{
    switch (identity)
    {
    case RuntimeCacheIdentity::Exact:
        return RuntimeCacheState::Exact;
    case RuntimeCacheIdentity::Missing:
    case RuntimeCacheIdentity::OwnedPrevious:
        return RuntimeCacheState::PublishRequired;
    case RuntimeCacheIdentity::Conflict:
        return RuntimeCacheState::Conflict;
    }
    return RuntimeCacheState::Conflict;
}

auto removal_cache_state(RuntimeCacheIdentity identity)
    -> RemovalCacheState
{
    switch (identity)
    {
    case RuntimeCacheIdentity::Missing:
        return RemovalCacheState::Missing;
    case RuntimeCacheIdentity::Exact:
        return RemovalCacheState::ExactOwned;
    case RuntimeCacheIdentity::OwnedPrevious:
        return RemovalCacheState::OwnedPartial;
    case RuntimeCacheIdentity::Conflict:
        return RemovalCacheState::Conflict;
    }
    return RemovalCacheState::Conflict;
}

auto managed_mod_state(RuntimeCacheIdentity identity)
    -> ArtifactState
{
    switch (identity)
    {
    case RuntimeCacheIdentity::Missing:
        return ArtifactState::Missing;
    case RuntimeCacheIdentity::Exact:
        return ArtifactState::ExactOwned;
    case RuntimeCacheIdentity::OwnedPrevious:
        return ArtifactState::OwnedPrevious;
    case RuntimeCacheIdentity::Conflict:
        return ArtifactState::Conflict;
    }
    return ArtifactState::Conflict;
}

auto owned(ArtifactState state) -> bool
{
    return state == ArtifactState::ExactOwned ||
           state == ArtifactState::OwnedPrevious;
}

auto managed_owned(
    const LauncherObservationEvidence& evidence) -> bool
{
    return evidence.runtime_cache == RuntimeCacheIdentity::Exact ||
           evidence.runtime_cache ==
               RuntimeCacheIdentity::OwnedPrevious ||
           owned(evidence.managed_loader.proxy) ||
           owned(evidence.managed_loader.override_file);
}

auto managed_conflict(
    const LauncherObservationEvidence& evidence) -> bool
{
    return evidence.runtime_cache ==
               RuntimeCacheIdentity::Conflict ||
           evidence.managed_loader.proxy ==
               ArtifactState::Conflict ||
           evidence.managed_loader.override_file ==
               ArtifactState::Conflict;
}
} // namespace

auto assemble_preparation_environment(
    const LauncherObservationEvidence& evidence)
    -> PreparationEnvironment
{
    const auto resolution = resolve_loader_chain(evidence.loader);
    const auto shared =
        resolution.state ==
        LoaderResolutionState::SharedCompatible;
    auto settings = evidence.runtime_settings;
    if (resolution.state == LoaderResolutionState::Missing)
    {
        settings = SettingsState::Missing;
    }
    else if (resolution.state ==
             LoaderResolutionState::Conflict)
    {
        settings = SettingsState::Incompatible;
    }

    return PreparationEnvironment{
        evidence.game_running,
        evidence.payload_valid,
        evidence.user_cache_writable,
        evidence.game_directory_writable,
        evidence.shared_runtime_writable,
        preparation_cache_state(evidence.runtime_cache),
        DeploymentObservation{
            launch_option_state(evidence.loader),
            evidence.managed_loader.proxy,
            evidence.managed_loader.override_file,
            runtime_state(resolution.state),
            settings,
            shared
                ? evidence.shared_mod
                : managed_mod_state(evidence.runtime_cache),
        },
    };
}

auto assemble_removal_observation(
    const LauncherObservationEvidence& evidence)
    -> RemovalObservation
{
    const auto resolution = resolve_loader_chain(evidence.loader);
    const auto cache_state =
        removal_cache_state(evidence.runtime_cache);
    const auto has_managed_ownership = managed_owned(evidence);
    const auto shared =
        resolution.state ==
        LoaderResolutionState::SharedCompatible;

    auto result = RemovalObservation{
        evidence.game_running,
        evidence.user_cache_writable,
        evidence.game_directory_writable,
        evidence.shared_runtime_writable,
        RemovalMode::None,
        RemovalCacheState::Missing,
        ArtifactState::Missing,
        ArtifactState::Missing,
        ArtifactState::Missing,
    };

    if (managed_conflict(evidence) ||
        (shared && evidence.shared_mod ==
                       ArtifactState::Conflict) ||
        (shared && has_managed_ownership))
    {
        result.mode = RemovalMode::Conflict;
        return result;
    }

    if (shared)
    {
        result.mode = RemovalMode::Shared;
        result.proxy = evidence.managed_loader.proxy;
        result.override_file =
            evidence.managed_loader.override_file;
        result.mod = evidence.shared_mod;
        return result;
    }

    if (has_managed_ownership ||
        resolution.state ==
            LoaderResolutionState::ManagedCompatible)
    {
        result.mode = RemovalMode::Managed;
        result.runtime_cache = cache_state;
        result.proxy = evidence.managed_loader.proxy;
        result.override_file =
            evidence.managed_loader.override_file;
        return result;
    }

    if (resolution.state == LoaderResolutionState::Conflict ||
        evidence.shared_mod != ArtifactState::Missing)
    {
        result.mode = RemovalMode::Conflict;
        return result;
    }

    if (evidence.managed_loader.proxy !=
            ArtifactState::Missing ||
        evidence.managed_loader.override_file !=
            ArtifactState::Missing)
    {
        result.mode = RemovalMode::Managed;
        result.proxy = evidence.managed_loader.proxy;
        result.override_file =
            evidence.managed_loader.override_file;
    }
    return result;
}
} // namespace meccha::launcher
