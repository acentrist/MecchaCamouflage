#include <meccha/launcher/composition_win32.hpp>

#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/manifest.hpp>

#include <span>
#include <utility>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

auto observation_error(std::string detail)
    -> Win32LauncherCompositionError
{
    return Win32LauncherCompositionError{
        std::in_place_type<LauncherObservationError>,
        LauncherObservationError{std::move(detail)},
    };
}

auto running_error(LauncherInvocationMode mode)
    -> LauncherWorkflowError
{
    if (mode == LauncherInvocationMode::Remove)
    {
        return LauncherWorkflowError{
            std::in_place_type<RemovalError>,
            RemovalError{
                RemovalErrorCode::GameRunning,
                "Removal is blocked while the game is running.",
            },
        };
    }
    return LauncherWorkflowError{
        std::in_place_type<PreparationError>,
        PreparationError{
            PreparationErrorCode::GameRunning,
            ConflictReason::None,
            "Preparation is blocked while the game is running.",
        },
    };
}

auto valid_directory_path(const fs::path& path) -> bool
{
    return path.is_absolute() &&
           path.lexically_normal() == path &&
           !path.filename().empty();
}

auto validate_inputs(
    const Win32LauncherCompositionInputs& inputs)
    -> std::expected<void, Win32LauncherCompositionError>
{
    if (!valid_directory_path(inputs.game_directory) ||
        !valid_directory_path(inputs.runtime_root) ||
        !valid_directory_path(inputs.ownership_directory))
    {
        return std::unexpected(observation_error(
            "Launcher composition paths must be absolute and "
            "normalized."));
    }
    if (inputs.manifest_json.empty())
    {
        return std::unexpected(observation_error(
            "The payload manifest resource is empty."));
    }
    const auto manifest_sha256 = sha256_bytes(
        std::as_bytes(std::span{inputs.manifest_json}));
    const auto manifest =
        parse_payload_manifest(inputs.manifest_json);
    if (!manifest_sha256 || !manifest ||
        *manifest_sha256 != inputs.manifest_sha256 ||
        *manifest != inputs.manifest)
    {
        return std::unexpected(observation_error(
            "The payload manifest bytes, hash, and parsed identity "
            "do not match."));
    }
    return {};
}
} // namespace

auto run_win32_launcher_composition(
    const Win32LauncherCompositionInputs& inputs)
    -> std::expected<
        Win32LauncherCompositionResult,
        Win32LauncherCompositionError>
{
    const auto running =
        inputs.observation_platform.game_running();
    if (!running)
    {
        return std::unexpected(Win32LauncherCompositionError{
            std::in_place_type<LauncherObservationError>,
            running.error(),
        });
    }
    if (*running)
    {
        return std::unexpected(Win32LauncherCompositionError{
            std::in_place_type<LauncherWorkflowError>,
            running_error(inputs.mode),
        });
    }
    const auto valid = validate_inputs(inputs);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }

    Win32RuntimeStorage runtime_storage{
        inputs.runtime_root,
        std::string{inputs.manifest_json},
        inputs.manifest_sha256,
        inputs.payload_source,
    };
    const auto recovery = recover_runtime(runtime_storage);
    if (!recovery)
    {
        return std::unexpected(Win32LauncherCompositionError{
            std::in_place_type<RuntimeTransactionError>,
            recovery.error(),
        });
    }

    Win32LauncherMaterialProvider material_provider{
        inputs.manifest,
        inputs.manifest_sha256,
        inputs.runtime_root / "active",
        inputs.payload_source,
    };
    Win32LauncherObservationSource observation_source{
        inputs.manifest,
        inputs.manifest_sha256,
        runtime_storage,
        Win32LauncherObservationPaths{
            inputs.game_directory,
            inputs.runtime_root,
            inputs.runtime_root / "active",
            inputs.ownership_directory,
        },
        material_provider,
        inputs.observation_platform,
    };
    Win32LauncherExecutionBackend execution_backend{
        runtime_storage,
        inputs.manifest_sha256,
        inputs.runtime_nonce,
        inputs.game_directory,
        inputs.ownership_directory,
        observation_source,
        material_provider,
        inputs.elevated_loader_broker,
        inputs.steam_launcher,
    };
    auto workflow = run_launcher_workflow(
        inputs.mode,
        observation_source,
        execution_backend);
    if (!workflow)
    {
        return std::unexpected(Win32LauncherCompositionError{
            std::in_place_type<LauncherWorkflowError>,
            std::move(workflow.error()),
        });
    }
    return Win32LauncherCompositionResult{
        *recovery,
        std::move(*workflow),
    };
}
} // namespace meccha::launcher
