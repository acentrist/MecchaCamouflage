#pragma once

#include <meccha/launcher/ownership.hpp>

#include <cstdint>

namespace meccha::launcher
{
enum class LaunchOptionState : std::uint8_t
{
    Absent,
    PinnedRuntime,
    OtherRuntime,
    Unresolved,
};

enum class RuntimeState : std::uint8_t
{
    Missing,
    ManagedCompatible,
    SharedCompatible,
    Incompatible,
    Ambiguous,
};

enum class SettingsState : std::uint8_t
{
    Missing,
    Compatible,
    Incompatible,
    Ambiguous,
};

enum class DeploymentMode : std::uint8_t
{
    Managed,
    Shared,
    Conflict,
};

enum class ArtifactDisposition : std::uint8_t
{
    None,
    CreateOwned,
    ReuseOwned,
    ReuseUnowned,
    ReplaceOwned,
};

enum class ConflictReason : std::uint8_t
{
    None,
    LaunchOption,
    Proxy,
    Override,
    Runtime,
    Settings,
    Mod,
    UnownedOverride,
};

struct DeploymentObservation
{
    LaunchOptionState launch_option{};
    ArtifactState proxy{};
    ArtifactState override_file{};
    RuntimeState runtime{};
    SettingsState settings{};
    ArtifactState mod{};

    auto operator==(const DeploymentObservation&) const -> bool = default;
};

struct DeploymentDecision
{
    DeploymentMode mode{DeploymentMode::Conflict};
    ConflictReason conflict{ConflictReason::None};
    ArtifactDisposition proxy{ArtifactDisposition::None};
    ArtifactDisposition override_file{ArtifactDisposition::None};
    ArtifactDisposition mod{ArtifactDisposition::None};

    auto operator==(const DeploymentDecision&) const -> bool = default;
};

[[nodiscard]] auto select_deployment(const DeploymentObservation& observation)
    -> DeploymentDecision;
} // namespace meccha::launcher
