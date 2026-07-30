#pragma once

#include <meccha/launcher/policy.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace meccha::launcher
{
enum class PreparationCommand : std::uint8_t
{
    PrepareAndLaunch,
    PrepareOnly,
};

enum class RuntimeCacheState : std::uint8_t
{
    Exact,
    PublishRequired,
    Conflict,
};

enum class RuntimeCacheAction : std::uint8_t
{
    None,
    Reuse,
    Publish,
};

enum class SharedModAction : std::uint8_t
{
    Reuse,
    Install,
};

struct ManagedLoaderPlan
{
    ArtifactDisposition proxy{ArtifactDisposition::None};
    ArtifactDisposition override_file{ArtifactDisposition::None};
    bool elevated{};

    auto operator==(const ManagedLoaderPlan&) const -> bool = default;
};

struct PreparationObservation
{
    PreparationCommand command{};
    bool game_running{};
    bool payload_valid{};
    bool user_cache_writable{};
    bool game_directory_writable{};
    bool shared_runtime_writable{};
    RuntimeCacheState runtime_cache{};
    DeploymentDecision deployment{};

    auto operator==(const PreparationObservation&) const -> bool = default;
};

struct PreparationPlan
{
    DeploymentMode mode{DeploymentMode::Conflict};
    RuntimeCacheAction runtime_cache{RuntimeCacheAction::None};
    std::optional<ManagedLoaderPlan> loader{};
    std::optional<SharedModAction> shared_mod{};
    bool launch_steam{};

    [[nodiscard]] auto requires_elevation() const -> bool;

    auto operator==(const PreparationPlan&) const -> bool = default;
};

enum class PreparationErrorCode : std::uint8_t
{
    GameRunning,
    Payload,
    DeploymentConflict,
    RuntimeCacheConflict,
    UserCacheAccess,
    SharedRuntimeAccess,
    InvalidDecision,
};

struct PreparationError
{
    PreparationErrorCode code{};
    ConflictReason conflict{ConflictReason::None};
    std::string detail{};

    auto operator==(const PreparationError&) const -> bool = default;
};

[[nodiscard]] auto plan_preparation(
    const PreparationObservation& observation)
    -> std::expected<PreparationPlan, PreparationError>;

enum class RemovalMode : std::uint8_t
{
    None,
    Managed,
    Shared,
    Conflict,
};

enum class RemovalCacheState : std::uint8_t
{
    Missing,
    ExactOwned,
    OwnedPartial,
    Conflict,
};

enum class RemovalAction : std::uint8_t
{
    None,
    RemoveOwned,
};

struct RemovalObservation
{
    bool game_running{};
    bool user_cache_writable{};
    bool game_directory_writable{};
    bool shared_runtime_writable{};
    RemovalMode mode{};
    RemovalCacheState runtime_cache{};
    ArtifactState proxy{};
    ArtifactState override_file{};
    ArtifactState mod{};

    auto operator==(const RemovalObservation&) const -> bool = default;
};

struct RemovalPlan
{
    RemovalAction runtime_cache{RemovalAction::None};
    RemovalAction proxy{RemovalAction::None};
    RemovalAction override_file{RemovalAction::None};
    RemovalAction mod{RemovalAction::None};
    bool elevated_loader{};

    [[nodiscard]] auto requires_elevation() const -> bool;

    auto operator==(const RemovalPlan&) const -> bool = default;
};

enum class RemovalErrorCode : std::uint8_t
{
    GameRunning,
    OwnershipConflict,
    UserCacheAccess,
    SharedRuntimeAccess,
    InvalidObservation,
};

struct RemovalError
{
    RemovalErrorCode code{};
    std::string detail{};

    auto operator==(const RemovalError&) const -> bool = default;
};

[[nodiscard]] auto plan_removal(const RemovalObservation& observation)
    -> std::expected<RemovalPlan, RemovalError>;
} // namespace meccha::launcher
