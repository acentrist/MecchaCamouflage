#include <meccha/launcher/observation_assembly.hpp>

#include <iostream>
#include <string_view>

namespace
{
using namespace meccha::launcher;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_observation_assembly: "
                  << message << '\n';
    }
    return condition;
}

auto clean_evidence() -> LauncherObservationEvidence
{
    return LauncherObservationEvidence{
        false,
        true,
        true,
        true,
        false,
        LoaderChainObservation{},
        ManagedLoaderObservation{
            ArtifactState::Missing,
            ArtifactState::Missing,
        },
        RuntimeCacheIdentity::Missing,
        SettingsState::Missing,
        ArtifactState::Missing,
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    const auto clean = clean_evidence();
    const auto clean_preparation =
        assemble_preparation_environment(clean);
    const auto clean_removal =
        assemble_removal_observation(clean);
    auto passed = expect(
        clean_preparation ==
            PreparationEnvironment{
                false,
                true,
                true,
                true,
                false,
                RuntimeCacheState::PublishRequired,
                DeploymentObservation{
                    LaunchOptionState::Absent,
                    ArtifactState::Missing,
                    ArtifactState::Missing,
                    RuntimeState::Missing,
                    SettingsState::Missing,
                    ArtifactState::Missing,
                },
            } &&
            clean_removal ==
                RemovalObservation{
                    false,
                    true,
                    true,
                    false,
                    RemovalMode::None,
                    RemovalCacheState::Missing,
                    ArtifactState::Missing,
                    ArtifactState::Missing,
                    ArtifactState::Missing,
                },
        "a clean tree was not assembled as managed publication/no-op removal");

    auto managed = clean;
    managed.loader.override_file = DirectiveState::Owned;
    managed.loader.override_target = CandidateIdentity::Pinned;
    managed.managed_loader = ManagedLoaderObservation{
        ArtifactState::ExactOwned,
        ArtifactState::ExactOwned,
    };
    managed.runtime_cache = RuntimeCacheIdentity::Exact;
    managed.runtime_settings = SettingsState::Compatible;
    const auto managed_preparation =
        assemble_preparation_environment(managed);
    const auto managed_removal =
        assemble_removal_observation(managed);
    passed &= expect(
        managed_preparation.runtime_cache ==
                RuntimeCacheState::Exact &&
            managed_preparation.deployment ==
                DeploymentObservation{
                    LaunchOptionState::Absent,
                    ArtifactState::ExactOwned,
                    ArtifactState::ExactOwned,
                    RuntimeState::ManagedCompatible,
                    SettingsState::Compatible,
                    ArtifactState::ExactOwned,
                } &&
            managed_removal.mode == RemovalMode::Managed &&
            managed_removal.runtime_cache ==
                RemovalCacheState::ExactOwned &&
            managed_removal.proxy == ArtifactState::ExactOwned &&
            managed_removal.override_file ==
                ArtifactState::ExactOwned,
        "an exact managed deployment lost loader/cache ownership");

    auto managed_previous = managed;
    managed_previous.loader.override_target =
        CandidateIdentity::Incompatible;
    managed_previous.managed_loader = ManagedLoaderObservation{
        ArtifactState::OwnedPrevious,
        ArtifactState::OwnedPrevious,
    };
    managed_previous.runtime_cache =
        RuntimeCacheIdentity::OwnedPrevious;
    const auto previous_preparation =
        assemble_preparation_environment(managed_previous);
    const auto previous_removal =
        assemble_removal_observation(managed_previous);
    passed &= expect(
        previous_preparation.runtime_cache ==
                RuntimeCacheState::PublishRequired &&
            previous_preparation.deployment.runtime ==
                RuntimeState::Incompatible &&
            previous_preparation.deployment.mod ==
                ArtifactState::OwnedPrevious &&
            previous_removal.mode == RemovalMode::Managed &&
            previous_removal.runtime_cache ==
                RemovalCacheState::OwnedPartial,
        "verified previous managed ownership was not removable after an "
        "incompatible current-version loader observation");

    auto shared = clean;
    shared.shared_runtime_writable = true;
    shared.loader.command_line = DirectiveState::Unowned;
    shared.loader.command_target = CandidateIdentity::Pinned;
    shared.managed_loader.proxy = ArtifactState::ExactUnowned;
    shared.runtime_settings = SettingsState::Compatible;
    shared.shared_mod = ArtifactState::ExactUnowned;
    const auto shared_preparation =
        assemble_preparation_environment(shared);
    const auto shared_removal =
        assemble_removal_observation(shared);
    passed &= expect(
        shared_preparation.deployment ==
                DeploymentObservation{
                    LaunchOptionState::PinnedRuntime,
                    ArtifactState::ExactUnowned,
                    ArtifactState::Missing,
                    RuntimeState::SharedCompatible,
                    SettingsState::Compatible,
                    ArtifactState::ExactUnowned,
                } &&
            shared_removal.mode == RemovalMode::Shared &&
            shared_removal.mod == ArtifactState::ExactUnowned,
        "an exact shared launch-option deployment was not preserved");

    auto mixed = shared;
    mixed.runtime_cache = RuntimeCacheIdentity::Exact;
    const auto mixed_removal =
        assemble_removal_observation(mixed);
    passed &= expect(
        mixed_removal.mode == RemovalMode::Conflict,
        "mixed shared and managed ownership was accepted for removal");

    auto invalid = clean;
    invalid.loader.command_line = DirectiveState::Invalid;
    invalid.managed_loader.proxy = ArtifactState::Conflict;
    const auto invalid_preparation =
        assemble_preparation_environment(invalid);
    const auto invalid_removal =
        assemble_removal_observation(invalid);
    passed &= expect(
        invalid_preparation.deployment.launch_option ==
                LaunchOptionState::Unresolved &&
            invalid_preparation.deployment.runtime ==
                RuntimeState::Incompatible &&
            invalid_removal.mode == RemovalMode::Conflict,
        "invalid loader evidence did not fail closed");

    if (passed)
    {
        std::cout << "PASS launcher_observation_assembly\n";
        return 0;
    }
    return 1;
}
