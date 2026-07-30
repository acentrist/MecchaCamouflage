#include <meccha/launcher/execution.hpp>

#include <expected>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::launcher;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_execution: " << message << '\n';
    }
    return condition;
}

class RecordingBackend final : public LauncherExecutionBackend
{
public:
    auto prepare_runtime_cache(RuntimeCacheAction action)
        -> std::expected<void, LauncherEffectError> override
    {
        return record(
            action == RuntimeCacheAction::Publish
                ? "runtime publish"
                : "runtime reuse");
    }

    auto apply_managed_loader(const ManagedLoaderPlan& plan)
        -> std::expected<void, LauncherEffectError> override
    {
        return record(
            plan.elevated ? "loader elevated" : "loader");
    }

    auto apply_shared_mod(SharedModAction action)
        -> std::expected<void, LauncherEffectError> override
    {
        return record(
            action == SharedModAction::Install
                ? "shared install"
                : "shared reuse");
    }

    auto remove_managed_loader(const RemovalPlan&)
        -> std::expected<void, LauncherEffectError> override
    {
        return record("loader remove");
    }

    auto remove_runtime_cache()
        -> std::expected<void, LauncherEffectError> override
    {
        return record("runtime remove");
    }

    auto remove_shared_mod(const RemovalPlan&)
        -> std::expected<void, LauncherEffectError> override
    {
        return record("shared remove");
    }

    auto launch_steam()
        -> std::expected<void, LauncherEffectError> override
    {
        return record("steam");
    }

    std::vector<std::string> events{};
    std::optional<std::string> fail_on{};

private:
    auto record(std::string event)
        -> std::expected<void, LauncherEffectError>
    {
        events.push_back(std::move(event));
        if (fail_on && events.back() == *fail_on)
        {
            return std::unexpected(
                LauncherEffectError{"injected failure"});
        }
        return {};
    }
};
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    RecordingBackend backend{};
    const auto result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Managed,
            RuntimeCacheAction::Publish,
            ManagedLoaderPlan{
                ArtifactDisposition::CreateOwned,
                ArtifactDisposition::CreateOwned,
                false,
            },
            std::nullopt,
            true,
        },
        backend);
    auto passed = expect(
        result &&
            result->runtime_cache ==
                RuntimeCacheAction::Publish &&
            result->managed_loader_applied &&
            !result->shared_mod_applied &&
            result->steam_launched &&
            backend.events ==
                std::vector<std::string>{
                    "runtime publish",
                    "loader",
                    "steam",
                },
        "managed preparation did not run runtime, loader, then Steam");

    RecordingBackend shared_backend{};
    const auto shared_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Shared,
            RuntimeCacheAction::None,
            std::nullopt,
            SharedModAction::Install,
            true,
        },
        shared_backend);
    passed &= expect(
        shared_result &&
            shared_result->runtime_cache ==
                RuntimeCacheAction::None &&
            !shared_result->managed_loader_applied &&
            shared_result->shared_mod_applied &&
            shared_result->steam_launched &&
            shared_backend.events ==
                std::vector<std::string>{
                    "shared install",
                    "steam",
                },
        "shared preparation touched managed state or launched early");

    RecordingBackend prepare_only_backend{};
    const auto prepare_only_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Managed,
            RuntimeCacheAction::Reuse,
            std::nullopt,
            std::nullopt,
            false,
        },
        prepare_only_backend);
    passed &= expect(
        prepare_only_result &&
            !prepare_only_result->steam_launched &&
            prepare_only_backend.events ==
                std::vector<std::string>{"runtime reuse"},
        "prepare-only launched Steam or skipped runtime validation");

    RecordingBackend failed_runtime_backend{};
    failed_runtime_backend.fail_on = "runtime publish";
    const auto failed_runtime_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Managed,
            RuntimeCacheAction::Publish,
            ManagedLoaderPlan{
                ArtifactDisposition::CreateOwned,
                ArtifactDisposition::CreateOwned,
                false,
            },
            std::nullopt,
            true,
        },
        failed_runtime_backend);
    passed &= expect(
        !failed_runtime_result &&
            failed_runtime_result.error().stage ==
                LauncherExecutionStage::RuntimeCache &&
            failed_runtime_backend.events ==
                std::vector<std::string>{"runtime publish"},
        "runtime failure allowed loader or Steam side effects");

    RecordingBackend failed_loader_backend{};
    failed_loader_backend.fail_on = "loader elevated";
    const auto failed_loader_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Managed,
            RuntimeCacheAction::Publish,
            ManagedLoaderPlan{
                ArtifactDisposition::CreateOwned,
                ArtifactDisposition::CreateOwned,
                true,
            },
            std::nullopt,
            true,
        },
        failed_loader_backend);
    passed &= expect(
        !failed_loader_result &&
            failed_loader_result.error().stage ==
                LauncherExecutionStage::ManagedLoader &&
            failed_loader_backend.events ==
                std::vector<std::string>{
                    "runtime publish",
                    "loader elevated",
                },
        "loader failure allowed an early Steam launch");

    RecordingBackend invalid_backend{};
    const auto invalid_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Shared,
            RuntimeCacheAction::Publish,
            std::nullopt,
            SharedModAction::Install,
            true,
        },
        invalid_backend);
    passed &= expect(
        !invalid_result &&
            invalid_result.error().stage ==
                LauncherExecutionStage::Plan &&
            invalid_backend.events.empty(),
        "an invalid mixed-mode plan reached launcher effects");

    RecordingBackend malformed_loader_backend{};
    const auto malformed_loader_result = execute_preparation(
        PreparationPlan{
            DeploymentMode::Managed,
            RuntimeCacheAction::Reuse,
            ManagedLoaderPlan{
                ArtifactDisposition::None,
                ArtifactDisposition::CreateOwned,
                false,
            },
            std::nullopt,
            true,
        },
        malformed_loader_backend);
    passed &= expect(
        !malformed_loader_result &&
            malformed_loader_result.error().stage ==
                LauncherExecutionStage::Plan &&
            malformed_loader_backend.events.empty(),
        "a malformed managed loader plan reached launcher effects");

    RecordingBackend removal_backend{};
    const auto removal_result = execute_removal(
        RemovalMode::Managed,
        RemovalPlan{
            RemovalAction::RemoveOwned,
            RemovalAction::RemoveOwned,
            RemovalAction::RemoveOwned,
            RemovalAction::None,
            false,
        },
        removal_backend);
    passed &= expect(
        removal_result &&
            removal_result->managed_loader_removed &&
            removal_result->runtime_cache_removed &&
            !removal_result->shared_mod_removed &&
            removal_backend.events ==
                std::vector<std::string>{
                    "loader remove",
                    "runtime remove",
                },
        "managed removal did not deactivate the loader before cache "
        "cleanup");

    RecordingBackend shared_removal_backend{};
    const auto shared_removal_result = execute_removal(
        RemovalMode::Shared,
        RemovalPlan{
            RemovalAction::None,
            RemovalAction::None,
            RemovalAction::None,
            RemovalAction::RemoveOwned,
            false,
        },
        shared_removal_backend);
    passed &= expect(
        shared_removal_result &&
            !shared_removal_result->managed_loader_removed &&
            !shared_removal_result->runtime_cache_removed &&
            shared_removal_result->shared_mod_removed &&
            shared_removal_backend.events ==
                std::vector<std::string>{"shared remove"},
        "shared removal touched managed loader or cache state");

    RecordingBackend empty_removal_backend{};
    const auto empty_removal_result = execute_removal(
        RemovalMode::None,
        RemovalPlan{},
        empty_removal_backend);
    passed &= expect(
        empty_removal_result &&
            !empty_removal_result->managed_loader_removed &&
            !empty_removal_result->runtime_cache_removed &&
            !empty_removal_result->shared_mod_removed &&
            empty_removal_backend.events.empty(),
        "an already-absent deployment was not an idempotent no-op");

    RecordingBackend failed_removal_backend{};
    failed_removal_backend.fail_on = "loader remove";
    const auto failed_removal_result = execute_removal(
        RemovalMode::Managed,
        RemovalPlan{
            RemovalAction::RemoveOwned,
            RemovalAction::RemoveOwned,
            RemovalAction::RemoveOwned,
            RemovalAction::None,
            true,
        },
        failed_removal_backend);
    passed &= expect(
        !failed_removal_result &&
            failed_removal_result.error().stage ==
                LauncherExecutionStage::ManagedLoader &&
            failed_removal_backend.events ==
                std::vector<std::string>{"loader remove"},
        "failed loader removal allowed runtime-cache deletion");

    RecordingBackend invalid_removal_backend{};
    const auto invalid_removal_result = execute_removal(
        RemovalMode::Shared,
        RemovalPlan{
            RemovalAction::RemoveOwned,
            RemovalAction::None,
            RemovalAction::None,
            RemovalAction::RemoveOwned,
            false,
        },
        invalid_removal_backend);
    passed &= expect(
        !invalid_removal_result &&
            invalid_removal_result.error().stage ==
                LauncherExecutionStage::Plan &&
            invalid_removal_backend.events.empty(),
        "an invalid shared removal reached launcher effects");

    if (passed)
    {
        std::cout << "PASS launcher_execution\n";
        return 0;
    }
    return 1;
}
