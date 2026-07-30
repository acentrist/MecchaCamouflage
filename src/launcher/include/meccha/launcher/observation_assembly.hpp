#pragma once

#include <meccha/launcher/loader.hpp>
#include <meccha/launcher/managed_loader.hpp>
#include <meccha/launcher/transaction.hpp>
#include <meccha/launcher/workflow.hpp>

namespace meccha::launcher
{
struct LauncherObservationEvidence
{
    bool game_running{};
    bool payload_valid{};
    bool user_cache_writable{};
    bool game_directory_writable{};
    bool shared_runtime_writable{};
    LoaderChainObservation loader{};
    ManagedLoaderObservation managed_loader{};
    RuntimeCacheIdentity runtime_cache{};
    SettingsState runtime_settings{};
    ArtifactState shared_mod{};

    auto operator==(const LauncherObservationEvidence&) const
        -> bool = default;
};

[[nodiscard]] auto assemble_preparation_environment(
    const LauncherObservationEvidence& evidence)
    -> PreparationEnvironment;

[[nodiscard]] auto assemble_removal_observation(
    const LauncherObservationEvidence& evidence)
    -> RemovalObservation;
} // namespace meccha::launcher
