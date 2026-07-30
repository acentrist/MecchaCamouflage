#include <meccha/launcher/workflow.hpp>

#include <expected>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::launcher;

auto expect(bool condition, const char* message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

class ObservationSource final : public LauncherObservationSource
{
public:
    auto observe_preparation()
        -> std::expected<
            PreparationEnvironment,
            LauncherObservationError> override
    {
        events.emplace_back("observe preparation");
        if (preparation_error)
        {
            return std::unexpected(*preparation_error);
        }
        return preparation;
    }

    auto observe_removal()
        -> std::expected<
            RemovalObservation,
            LauncherObservationError> override
    {
        events.emplace_back("observe removal");
        return removal;
    }

    PreparationEnvironment preparation{
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
    };
    RemovalObservation removal{};
    std::optional<LauncherObservationError> preparation_error{};
    std::vector<std::string> events{};
};

class EffectBackend final : public LauncherExecutionBackend
{
public:
    explicit EffectBackend(std::vector<std::string>& events)
        : events_{events}
    {
    }

    auto prepare_runtime_cache(RuntimeCacheAction)
        -> std::expected<void, LauncherEffectError> override
    {
        events_.emplace_back("prepare runtime");
        return {};
    }

    auto apply_managed_loader(const ManagedLoaderPlan&)
        -> std::expected<void, LauncherEffectError> override
    {
        events_.emplace_back("apply loader");
        return {};
    }

    auto apply_shared_mod(SharedModAction)
        -> std::expected<void, LauncherEffectError> override
    {
        events_.emplace_back("apply shared mod");
        return {};
    }

    auto remove_managed_loader(const RemovalPlan&)
        -> std::expected<void, LauncherEffectError> override
    {
        events_.emplace_back("remove loader");
        return {};
    }

    auto remove_runtime_cache()
        -> std::expected<void, LauncherEffectError> override
    {
        events_.emplace_back("remove runtime");
        return {};
    }

    auto remove_shared_mod(const RemovalPlan&)
        -> std::expected<void, LauncherEffectError> override
    {
        events_.emplace_back("remove shared mod");
        return {};
    }

    auto launch_steam()
        -> std::expected<void, LauncherEffectError> override
    {
        events_.emplace_back("launch Steam");
        return {};
    }

private:
    std::vector<std::string>& events_;
};
} // namespace

auto main() -> int
{
    ObservationSource source{};
    EffectBackend effects{source.events};

    const auto prepared = run_launcher_workflow(
        LauncherInvocationMode::PrepareAndLaunch,
        source,
        effects);
    auto passed = expect(
        prepared &&
            std::holds_alternative<PreparationExecutionResult>(
                *prepared) &&
            source.events ==
                std::vector<std::string>{
                    "observe preparation",
                    "prepare runtime",
                    "apply loader",
                    "launch Steam",
                },
        "prepare-and-launch did not observe, plan, and execute in order");

    source.events.clear();
    source.preparation.game_running = true;
    const auto blocked = run_launcher_workflow(
        LauncherInvocationMode::PrepareOnly,
        source,
        effects);
    passed &= expect(
        !blocked &&
            std::holds_alternative<PreparationError>(
                blocked.error()) &&
            std::get<PreparationError>(blocked.error()).code ==
                PreparationErrorCode::GameRunning &&
            source.events ==
                std::vector<std::string>{"observe preparation"},
        "a planning failure reached launcher effects");

    source.events.clear();
    source.preparation.game_running = false;
    const auto prepared_only = run_launcher_workflow(
        LauncherInvocationMode::PrepareOnly,
        source,
        effects);
    passed &= expect(
        prepared_only &&
            source.events ==
                std::vector<std::string>{
                    "observe preparation",
                    "prepare runtime",
                    "apply loader",
                },
        "prepare-only reached the Steam launch effect");

    source.events.clear();
    source.preparation_error = LauncherObservationError{
        "read-only probe failed",
    };
    const auto observation_failed = run_launcher_workflow(
        LauncherInvocationMode::PrepareAndLaunch,
        source,
        effects);
    passed &= expect(
        !observation_failed &&
            std::holds_alternative<LauncherObservationError>(
                observation_failed.error()) &&
            std::get<LauncherObservationError>(
                observation_failed.error()).detail ==
                "read-only probe failed" &&
            source.events ==
                std::vector<std::string>{"observe preparation"},
        "an observation failure did not stop before planning effects");
    source.preparation_error.reset();

    source.events.clear();
    source.removal = RemovalObservation{
        false,
        true,
        true,
        false,
        RemovalMode::Managed,
        RemovalCacheState::ExactOwned,
        ArtifactState::ExactOwned,
        ArtifactState::ExactOwned,
        ArtifactState::Missing,
    };
    const auto removed = run_launcher_workflow(
        LauncherInvocationMode::Remove,
        source,
        effects);
    passed &= expect(
        removed &&
            std::holds_alternative<RemovalExecutionResult>(*removed) &&
            source.events ==
                std::vector<std::string>{
                    "observe removal",
                    "remove loader",
                    "remove runtime",
                },
        "managed removal did not use the removal observation and plan");

    if (passed)
    {
        std::cout << "PASS launcher_workflow\n";
        return 0;
    }
    return 1;
}
