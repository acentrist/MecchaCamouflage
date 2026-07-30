#pragma once

#include <meccha/launcher/command_line.hpp>
#include <meccha/launcher/execution.hpp>
#include <meccha/launcher/policy.hpp>
#include <meccha/launcher/preparation.hpp>

#include <expected>
#include <string>
#include <variant>

namespace meccha::launcher
{
struct PreparationEnvironment
{
    bool game_running{};
    bool payload_valid{};
    bool user_cache_writable{};
    bool game_directory_writable{};
    bool shared_runtime_writable{};
    RuntimeCacheState runtime_cache{};
    DeploymentObservation deployment{};

    auto operator==(const PreparationEnvironment&) const -> bool = default;
};

struct LauncherObservationError
{
    std::string detail{};

    auto operator==(const LauncherObservationError&) const -> bool = default;
};

class LauncherObservationSource
{
public:
    LauncherObservationSource() = default;
    LauncherObservationSource(const LauncherObservationSource&) = delete;
    auto operator=(const LauncherObservationSource&)
        -> LauncherObservationSource& = delete;
    LauncherObservationSource(LauncherObservationSource&&) = delete;
    auto operator=(LauncherObservationSource&&)
        -> LauncherObservationSource& = delete;
    virtual ~LauncherObservationSource() = default;

    virtual auto observe_preparation()
        -> std::expected<
            PreparationEnvironment,
            LauncherObservationError> = 0;

    virtual auto observe_removal()
        -> std::expected<
            RemovalObservation,
            LauncherObservationError> = 0;
};

using LauncherWorkflowResult = std::variant<
    PreparationExecutionResult,
    RemovalExecutionResult>;

using LauncherWorkflowError = std::variant<
    LauncherObservationError,
    PreparationError,
    RemovalError,
    LauncherExecutionError>;

[[nodiscard]] auto run_launcher_workflow(
    LauncherInvocationMode mode,
    LauncherObservationSource& observation_source,
    LauncherExecutionBackend& execution_backend)
    -> std::expected<LauncherWorkflowResult, LauncherWorkflowError>;
} // namespace meccha::launcher
