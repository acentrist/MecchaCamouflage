#include <meccha/launcher/workflow.hpp>

#include <utility>

namespace meccha::launcher
{
namespace
{
auto preparation_command(LauncherInvocationMode mode)
    -> PreparationCommand
{
    return mode == LauncherInvocationMode::PrepareAndLaunch
               ? PreparationCommand::PrepareAndLaunch
               : PreparationCommand::PrepareOnly;
}
} // namespace

auto run_launcher_workflow(
    LauncherInvocationMode mode,
    LauncherObservationSource& observation_source,
    LauncherExecutionBackend& execution_backend)
    -> std::expected<LauncherWorkflowResult, LauncherWorkflowError>
{
    if (mode == LauncherInvocationMode::Remove)
    {
        const auto observation =
            observation_source.observe_removal();
        if (!observation)
        {
            return std::unexpected(LauncherWorkflowError{
                std::in_place_type<LauncherObservationError>,
                observation.error(),
            });
        }
        const auto plan = plan_removal(*observation);
        if (!plan)
        {
            return std::unexpected(LauncherWorkflowError{
                std::in_place_type<RemovalError>,
                plan.error(),
            });
        }
        const auto executed = execute_removal(
            observation->mode,
            *plan,
            execution_backend);
        if (!executed)
        {
            return std::unexpected(LauncherWorkflowError{
                std::in_place_type<LauncherExecutionError>,
                executed.error(),
            });
        }
        return LauncherWorkflowResult{
            std::in_place_type<RemovalExecutionResult>,
            std::move(*executed),
        };
    }

    const auto environment =
        observation_source.observe_preparation();
    if (!environment)
    {
        return std::unexpected(LauncherWorkflowError{
            std::in_place_type<LauncherObservationError>,
            environment.error(),
        });
    }
    const auto decision = select_deployment(
        environment->deployment);
    const auto plan = plan_preparation(PreparationObservation{
        preparation_command(mode),
        environment->game_running,
        environment->payload_valid,
        environment->user_cache_writable,
        environment->game_directory_writable,
        environment->shared_runtime_writable,
        environment->runtime_cache,
        decision,
    });
    if (!plan)
    {
        return std::unexpected(LauncherWorkflowError{
            std::in_place_type<PreparationError>,
            plan.error(),
        });
    }
    const auto executed = execute_preparation(
        *plan,
        execution_backend);
    if (!executed)
    {
        return std::unexpected(LauncherWorkflowError{
            std::in_place_type<LauncherExecutionError>,
            executed.error(),
        });
    }
    return LauncherWorkflowResult{
        std::in_place_type<PreparationExecutionResult>,
        std::move(*executed),
    };
}
} // namespace meccha::launcher
