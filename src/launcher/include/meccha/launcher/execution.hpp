#pragma once

#include <meccha/launcher/preparation.hpp>

#include <cstdint>
#include <expected>
#include <string>

namespace meccha::launcher
{
struct LauncherEffectError
{
    std::string detail{};

    auto operator==(const LauncherEffectError&) const -> bool = default;
};

class LauncherExecutionBackend
{
public:
    LauncherExecutionBackend() = default;
    LauncherExecutionBackend(const LauncherExecutionBackend&) = delete;
    auto operator=(const LauncherExecutionBackend&)
        -> LauncherExecutionBackend& = delete;
    LauncherExecutionBackend(LauncherExecutionBackend&&) = delete;
    auto operator=(LauncherExecutionBackend&&)
        -> LauncherExecutionBackend& = delete;
    virtual ~LauncherExecutionBackend() = default;

    virtual auto prepare_runtime_cache(RuntimeCacheAction action)
        -> std::expected<void, LauncherEffectError> = 0;

    virtual auto apply_managed_loader(const ManagedLoaderPlan& plan)
        -> std::expected<void, LauncherEffectError> = 0;

    virtual auto apply_shared_mod(SharedModAction action)
        -> std::expected<void, LauncherEffectError> = 0;

    virtual auto remove_managed_loader(const RemovalPlan& plan)
        -> std::expected<void, LauncherEffectError> = 0;

    virtual auto remove_runtime_cache()
        -> std::expected<void, LauncherEffectError> = 0;

    virtual auto remove_shared_mod(const RemovalPlan& plan)
        -> std::expected<void, LauncherEffectError> = 0;

    virtual auto launch_steam()
        -> std::expected<void, LauncherEffectError> = 0;
};

enum class LauncherExecutionStage : std::uint8_t
{
    Plan,
    RuntimeCache,
    ManagedLoader,
    SharedMod,
    Steam,
};

struct LauncherExecutionError
{
    LauncherExecutionStage stage{};
    std::string detail{};

    auto operator==(const LauncherExecutionError&) const -> bool = default;
};

struct PreparationExecutionResult
{
    RuntimeCacheAction runtime_cache{RuntimeCacheAction::None};
    bool managed_loader_applied{};
    bool shared_mod_applied{};
    bool steam_launched{};

    auto operator==(const PreparationExecutionResult&) const
        -> bool = default;
};

struct RemovalExecutionResult
{
    bool managed_loader_removed{};
    bool runtime_cache_removed{};
    bool shared_mod_removed{};

    auto operator==(const RemovalExecutionResult&) const -> bool = default;
};

[[nodiscard]] auto execute_preparation(
    const PreparationPlan& plan,
    LauncherExecutionBackend& backend)
    -> std::expected<
        PreparationExecutionResult,
        LauncherExecutionError>;

[[nodiscard]] auto execute_removal(
    RemovalMode mode,
    const RemovalPlan& plan,
    LauncherExecutionBackend& backend)
    -> std::expected<
        RemovalExecutionResult,
        LauncherExecutionError>;
} // namespace meccha::launcher
