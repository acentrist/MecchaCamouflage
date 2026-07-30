#include <meccha/launcher/policy.hpp>

namespace meccha::launcher
{
namespace
{
auto conflict(ConflictReason reason) -> DeploymentDecision
{
    return DeploymentDecision{
        DeploymentMode::Conflict,
        reason,
        ArtifactDisposition::None,
        ArtifactDisposition::None,
        ArtifactDisposition::None,
    };
}

auto managed_disposition(ArtifactState state) -> ArtifactDisposition
{
    switch (state)
    {
    case ArtifactState::Missing:
        return ArtifactDisposition::CreateOwned;
    case ArtifactState::ExactOwned:
        return ArtifactDisposition::ReuseOwned;
    case ArtifactState::ExactUnowned:
        return ArtifactDisposition::ReuseUnowned;
    case ArtifactState::Conflict:
        return ArtifactDisposition::None;
    }
    return ArtifactDisposition::None;
}

auto shared_mod_disposition(ArtifactState state) -> ArtifactDisposition
{
    switch (state)
    {
    case ArtifactState::Missing:
        return ArtifactDisposition::CreateOwned;
    case ArtifactState::ExactOwned:
        return ArtifactDisposition::ReuseOwned;
    case ArtifactState::ExactUnowned:
        return ArtifactDisposition::ReuseUnowned;
    case ArtifactState::Conflict:
        return ArtifactDisposition::None;
    }
    return ArtifactDisposition::None;
}
} // namespace

auto select_deployment(const DeploymentObservation& observation)
    -> DeploymentDecision
{
    if (observation.launch_option == LaunchOptionState::OtherRuntime ||
        observation.launch_option == LaunchOptionState::Unresolved)
    {
        return conflict(ConflictReason::LaunchOption);
    }
    if (observation.proxy == ArtifactState::Conflict)
    {
        return conflict(ConflictReason::Proxy);
    }
    if (observation.override_file == ArtifactState::Conflict)
    {
        return conflict(ConflictReason::Override);
    }
    if (observation.mod == ArtifactState::Conflict)
    {
        return conflict(ConflictReason::Mod);
    }
    if (observation.runtime == RuntimeState::Incompatible ||
        observation.runtime == RuntimeState::Ambiguous)
    {
        return conflict(ConflictReason::Runtime);
    }
    if (observation.settings == SettingsState::Incompatible ||
        observation.settings == SettingsState::Ambiguous)
    {
        return conflict(ConflictReason::Settings);
    }

    if (observation.runtime == RuntimeState::Missing)
    {
        if (observation.launch_option != LaunchOptionState::Absent)
        {
            return conflict(ConflictReason::Runtime);
        }
        if (observation.settings != SettingsState::Missing)
        {
            return conflict(ConflictReason::Settings);
        }
        if (observation.override_file == ArtifactState::ExactUnowned)
        {
            return conflict(ConflictReason::UnownedOverride);
        }
        if (observation.mod == ArtifactState::ExactUnowned)
        {
            return conflict(ConflictReason::Mod);
        }
        return DeploymentDecision{
            DeploymentMode::Managed,
            ConflictReason::None,
            managed_disposition(observation.proxy),
            managed_disposition(observation.override_file),
            managed_disposition(observation.mod),
        };
    }

    if (observation.runtime == RuntimeState::ManagedCompatible)
    {
        if (observation.launch_option != LaunchOptionState::Absent ||
            observation.settings != SettingsState::Compatible ||
            observation.override_file != ArtifactState::ExactOwned ||
            observation.mod == ArtifactState::ExactUnowned)
        {
            return conflict(ConflictReason::Runtime);
        }
        return DeploymentDecision{
            DeploymentMode::Managed,
            ConflictReason::None,
            managed_disposition(observation.proxy),
            managed_disposition(observation.override_file),
            managed_disposition(observation.mod),
        };
    }

    if (observation.runtime == RuntimeState::SharedCompatible)
    {
        if (observation.settings != SettingsState::Compatible)
        {
            return conflict(ConflictReason::Settings);
        }
        if (observation.proxy == ArtifactState::Missing)
        {
            return conflict(ConflictReason::Proxy);
        }
        return DeploymentDecision{
            DeploymentMode::Shared,
            ConflictReason::None,
            ArtifactDisposition::None,
            ArtifactDisposition::None,
            shared_mod_disposition(observation.mod),
        };
    }

    return conflict(ConflictReason::Runtime);
}
} // namespace meccha::launcher
