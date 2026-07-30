#include <meccha/launcher/preparation.hpp>

#include <meccha/launcher/policy.hpp>

#include <iostream>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL preparation_plan: " << message << '\n';
        return false;
    }
    return true;
}

auto clean_managed_decision()
    -> meccha::launcher::DeploymentDecision
{
    using namespace meccha::launcher;
    return DeploymentDecision{
        DeploymentMode::Managed,
        ConflictReason::None,
        ArtifactDisposition::CreateOwned,
        ArtifactDisposition::CreateOwned,
        ArtifactDisposition::CreateOwned,
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    const auto clean = PreparationObservation{
        PreparationCommand::PrepareAndLaunch,
        false,
        true,
        true,
        true,
        true,
        RuntimeCacheState::PublishRequired,
        clean_managed_decision(),
    };
    const auto clean_plan = plan_preparation(clean);
    passed &= expect(
        clean_plan &&
            clean_plan->runtime_cache ==
                RuntimeCacheAction::Publish &&
            clean_plan->loader &&
            clean_plan->loader->proxy ==
                ArtifactDisposition::CreateOwned &&
            clean_plan->loader->override_file ==
                ArtifactDisposition::CreateOwned &&
            !clean_plan->loader->elevated &&
            !clean_plan->shared_mod &&
            clean_plan->launch_steam &&
            !clean_plan->requires_elevation(),
        "clean managed preparation plan is incomplete");

    auto protected_game = clean;
    protected_game.game_directory_writable = false;
    const auto protected_plan = plan_preparation(protected_game);
    passed &= expect(
        protected_plan && protected_plan->loader &&
            protected_plan->loader->elevated &&
            protected_plan->requires_elevation(),
        "managed loader mutation did not request minimal elevation");

    auto prepare_only = protected_game;
    prepare_only.command = PreparationCommand::PrepareOnly;
    const auto prepare_only_plan = plan_preparation(prepare_only);
    passed &= expect(
        prepare_only_plan &&
            !prepare_only_plan->launch_steam &&
            prepare_only_plan->requires_elevation(),
        "prepare-only changed the required mutation or launched Steam");

    auto exact = clean;
    exact.game_directory_writable = false;
    exact.runtime_cache = RuntimeCacheState::Exact;
    exact.deployment = DeploymentDecision{
        DeploymentMode::Managed,
        ConflictReason::None,
        ArtifactDisposition::ReuseUnowned,
        ArtifactDisposition::ReuseOwned,
        ArtifactDisposition::ReuseOwned,
    };
    const auto exact_plan = plan_preparation(exact);
    passed &= expect(
        exact_plan &&
            exact_plan->runtime_cache ==
                RuntimeCacheAction::Reuse &&
            !exact_plan->loader &&
            !exact_plan->requires_elevation() &&
            exact_plan->launch_steam,
        "exact preparation requested unnecessary UAC or mutation");

    auto shared = clean;
    shared.runtime_cache = RuntimeCacheState::Conflict;
    shared.deployment = DeploymentDecision{
        DeploymentMode::Shared,
        ConflictReason::None,
        ArtifactDisposition::None,
        ArtifactDisposition::None,
        ArtifactDisposition::CreateOwned,
    };
    const auto shared_plan = plan_preparation(shared);
    passed &= expect(
        shared_plan &&
            shared_plan->runtime_cache ==
                RuntimeCacheAction::None &&
            !shared_plan->loader &&
            shared_plan->shared_mod ==
                SharedModAction::Install &&
            !shared_plan->requires_elevation(),
        "shared preparation attempted to mutate the managed loader/cache");

    shared.shared_runtime_writable = false;
    const auto protected_shared = plan_preparation(shared);
    passed &= expect(
        !protected_shared &&
            protected_shared.error().code ==
                PreparationErrorCode::SharedRuntimeAccess,
        "unwritable shared mod requested elevation");

    auto running = clean;
    running.game_running = true;
    const auto running_result = plan_preparation(running);
    passed &= expect(
        !running_result &&
            running_result.error().code ==
                PreparationErrorCode::GameRunning,
        "running game did not block preparation");

    auto invalid_payload = clean;
    invalid_payload.payload_valid = false;
    const auto invalid_payload_result =
        plan_preparation(invalid_payload);
    passed &= expect(
        !invalid_payload_result &&
            invalid_payload_result.error().code ==
                PreparationErrorCode::Payload,
        "invalid embedded payload was planned");

    auto conflict = clean;
    conflict.deployment = DeploymentDecision{
        DeploymentMode::Conflict,
        ConflictReason::Proxy,
        ArtifactDisposition::None,
        ArtifactDisposition::None,
        ArtifactDisposition::None,
    };
    const auto conflict_result = plan_preparation(conflict);
    passed &= expect(
        !conflict_result &&
            conflict_result.error().code ==
                PreparationErrorCode::DeploymentConflict &&
            conflict_result.error().conflict ==
                ConflictReason::Proxy,
        "deployment conflict lost its actionable reason");

    auto cache_conflict = clean;
    cache_conflict.runtime_cache = RuntimeCacheState::Conflict;
    const auto cache_conflict_result =
        plan_preparation(cache_conflict);
    passed &= expect(
        !cache_conflict_result &&
            cache_conflict_result.error().code ==
                PreparationErrorCode::RuntimeCacheConflict,
        "managed runtime conflict was ignored");

    auto cache_denied = clean;
    cache_denied.user_cache_writable = false;
    const auto cache_denied_result =
        plan_preparation(cache_denied);
    passed &= expect(
        !cache_denied_result &&
            cache_denied_result.error().code ==
                PreparationErrorCode::UserCacheAccess,
        "LocalAppData failure was incorrectly delegated to UAC");

    auto managed_update = clean;
    managed_update.runtime_cache =
        RuntimeCacheState::PublishRequired;
    managed_update.deployment = DeploymentDecision{
        DeploymentMode::Managed,
        ConflictReason::None,
        ArtifactDisposition::ReplaceOwned,
        ArtifactDisposition::ReuseOwned,
        ArtifactDisposition::ReplaceOwned,
    };
    managed_update.game_directory_writable = false;
    const auto update_plan = plan_preparation(managed_update);
    passed &= expect(
        update_plan && update_plan->loader &&
            update_plan->loader->proxy ==
                ArtifactDisposition::ReplaceOwned &&
            update_plan->loader->override_file ==
                ArtifactDisposition::ReuseOwned &&
            update_plan->loader->elevated,
        "owned update did not minimize the elevated loader set");

    const auto managed_remove = plan_removal(RemovalObservation{
        false,
        true,
        false,
        true,
        RemovalMode::Managed,
        RemovalCacheState::ExactOwned,
        ArtifactState::ExactOwned,
        ArtifactState::OwnedPrevious,
        ArtifactState::ExactOwned,
    });
    passed &= expect(
        managed_remove &&
            managed_remove->runtime_cache ==
                RemovalAction::RemoveOwned &&
            managed_remove->proxy ==
                RemovalAction::RemoveOwned &&
            managed_remove->override_file ==
                RemovalAction::RemoveOwned &&
            managed_remove->mod == RemovalAction::None &&
            managed_remove->requires_elevation(),
        "managed removal did not limit itself to owned artifacts");

    const auto unowned_remove = plan_removal(RemovalObservation{
        false,
        true,
        false,
        true,
        RemovalMode::Managed,
        RemovalCacheState::Missing,
        ArtifactState::ExactUnowned,
        ArtifactState::Missing,
        ArtifactState::Missing,
    });
    passed &= expect(
        unowned_remove &&
            unowned_remove->proxy == RemovalAction::None &&
            !unowned_remove->requires_elevation(),
        "unowned exact proxy was selected for removal");

    const auto shared_remove = plan_removal(RemovalObservation{
        false,
        true,
        false,
        true,
        RemovalMode::Shared,
        RemovalCacheState::ExactOwned,
        ArtifactState::ExactOwned,
        ArtifactState::ExactOwned,
        ArtifactState::OwnedPrevious,
    });
    passed &= expect(
        shared_remove &&
            shared_remove->runtime_cache == RemovalAction::None &&
            shared_remove->proxy == RemovalAction::None &&
            shared_remove->override_file == RemovalAction::None &&
            shared_remove->mod == RemovalAction::RemoveOwned &&
            !shared_remove->requires_elevation(),
        "shared removal touched the loader/runtime");

    auto protected_shared_remove = RemovalObservation{
        false,
        true,
        true,
        false,
        RemovalMode::Shared,
        RemovalCacheState::Missing,
        ArtifactState::ExactUnowned,
        ArtifactState::ExactUnowned,
        ArtifactState::ExactOwned,
    };
    const auto protected_shared_remove_result =
        plan_removal(protected_shared_remove);
    passed &= expect(
        !protected_shared_remove_result &&
            protected_shared_remove_result.error().code ==
                RemovalErrorCode::SharedRuntimeAccess,
        "shared mod removal incorrectly requested elevation");

    auto conflicting_remove = protected_shared_remove;
    conflicting_remove.shared_runtime_writable = true;
    conflicting_remove.mod = ArtifactState::Conflict;
    const auto conflicting_remove_result =
        plan_removal(conflicting_remove);
    passed &= expect(
        !conflicting_remove_result &&
            conflicting_remove_result.error().code ==
                RemovalErrorCode::OwnershipConflict,
        "conflicting mod ownership was ignored during removal");

    auto running_remove = protected_shared_remove;
    running_remove.game_running = true;
    const auto running_remove_result = plan_removal(running_remove);
    passed &= expect(
        !running_remove_result &&
            running_remove_result.error().code ==
                RemovalErrorCode::GameRunning,
        "running game did not block removal");

    if (passed)
    {
        std::cout << "PASS preparation_plan\n";
    }
    return passed ? 0 : 1;
}
