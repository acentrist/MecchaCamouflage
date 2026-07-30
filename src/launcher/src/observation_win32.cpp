#include <meccha/launcher/observation_win32.hpp>

#include <meccha/launcher/execution_win32.hpp>
#include <meccha/launcher/game_discovery.hpp>
#include <meccha/launcher/loader_observation.hpp>
#include <meccha/launcher/managed_loader.hpp>
#include <meccha/launcher/observation_assembly.hpp>
#include <meccha/launcher/shared_mod.hpp>
#include <meccha/launcher/transaction.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

class FileHandle
{
public:
    explicit FileHandle(HANDLE value)
        : value_(value)
    {
    }

    FileHandle(const FileHandle&) = delete;
    auto operator=(const FileHandle&) -> FileHandle& = delete;

    ~FileHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(value_));
        }
    }

    [[nodiscard]] auto valid() const -> bool
    {
        return value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

auto error(std::string detail)
    -> std::unexpected<LauncherObservationError>
{
    return std::unexpected(LauncherObservationError{
        std::move(detail),
    });
}

auto windows_error(
    std::string_view operation,
    DWORD code = GetLastError())
    -> std::unexpected<LauncherObservationError>
{
    return error(
        std::string{operation} + " (Windows error " +
        std::to_string(code) + ").");
}

auto valid_directory_path(const fs::path& path) -> bool
{
    return path.is_absolute() &&
           path.lexically_normal() == path &&
           !path.filename().empty();
}

auto validate_paths(const Win32LauncherObservationPaths& paths)
    -> std::expected<void, LauncherObservationError>
{
    if (!valid_directory_path(paths.game_directory) ||
        !valid_directory_path(paths.runtime_root) ||
        !valid_directory_path(paths.active_runtime_directory) ||
        !valid_directory_path(paths.ownership_directory) ||
        paths.active_runtime_directory !=
            paths.runtime_root / "active")
    {
        return error(
            "Launcher observation paths must be absolute, normalized, "
            "and bind active below the runtime root.");
    }
    return {};
}

auto nearest_plain_directory(const fs::path& path)
    -> std::expected<fs::path, LauncherObservationError>
{
    if (!valid_directory_path(path))
    {
        return error(
            "The writable-directory probe path is invalid.");
    }

    auto current = path.root_path();
    for (const auto& component : path.relative_path())
    {
        const auto candidate = current / component;
        const auto attributes =
            GetFileAttributesW(candidate.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const auto code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND ||
                code == ERROR_PATH_NOT_FOUND)
            {
                break;
            }
            return windows_error(
                "The writable-directory path could not be inspected",
                code);
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
        {
            return error(
                "The writable-directory path traverses a "
                "non-directory or reparse entry.");
        }
        current = candidate;
    }
    return current;
}

auto observe_directory_writable(const fs::path& path)
    -> std::expected<bool, LauncherObservationError>
{
    const auto directory = nearest_plain_directory(path);
    if (!directory)
    {
        return std::unexpected(directory.error());
    }
    const FileHandle handle{CreateFileW(
        directory->c_str(),
        FILE_ADD_FILE |
            FILE_ADD_SUBDIRECTORY |
            FILE_DELETE_CHILD,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (handle.valid())
    {
        return true;
    }
    const auto code = GetLastError();
    if (code == ERROR_ACCESS_DENIED ||
        code == ERROR_PRIVILEGE_NOT_HELD)
    {
        return false;
    }
    return windows_error(
        "The writable-directory access probe failed",
        code);
}

auto pinned_runtime_sha256(const PayloadManifest& manifest)
    -> std::expected<Sha256Digest, LauncherObservationError>
{
    auto runtime = static_cast<const ManifestFile*>(nullptr);
    for (const auto& file : manifest.files)
    {
        if (file.path != "UE4SS.dll")
        {
            continue;
        }
        if (runtime != nullptr || file.role != FileRole::Runtime)
        {
            return error(
                "The payload has an invalid canonical UE4SS.dll "
                "entry.");
        }
        runtime = std::addressof(file);
    }
    if (runtime == nullptr)
    {
        return error(
            "The payload has no canonical UE4SS.dll entry.");
    }
    return runtime->sha256;
}

auto directive_state(ArtifactState state) -> DirectiveState
{
    switch (state)
    {
    case ArtifactState::Missing:
        return DirectiveState::Absent;
    case ArtifactState::ExactOwned:
    case ArtifactState::OwnedPrevious:
        return DirectiveState::Owned;
    case ArtifactState::ExactUnowned:
        return DirectiveState::Unowned;
    case ArtifactState::Conflict:
        return DirectiveState::Invalid;
    }
    return DirectiveState::Invalid;
}

auto shared_runtime_root(
    const LoaderResolution& resolution,
    const LoaderFilesystemObservation& observation,
    const fs::path& game_directory)
    -> std::expected<fs::path, LauncherObservationError>
{
    auto result = fs::path{};
    switch (resolution.source)
    {
    case LoaderSource::CommandLine:
        if (!observation.command_line_target)
        {
            return error(
                "The shared command-line runtime target was lost.");
        }
        result = observation.command_line_target->parent_path();
        break;
    case LoaderSource::Override:
        if (!observation.override_target)
        {
            return error(
                "The shared override runtime target was lost.");
        }
        result = observation.override_target->parent_path();
        break;
    case LoaderSource::ConventionalSubdirectory:
        result = game_directory / "ue4ss";
        break;
    case LoaderSource::ConventionalRoot:
        result = game_directory;
        break;
    case LoaderSource::None:
        return error(
            "The shared runtime has no resolved loader source.");
    }
    result = result.lexically_normal();
    if (!valid_directory_path(result))
    {
        return error(
            "The resolved shared runtime root is invalid.");
    }
    return result;
}

auto managed_settings_state(
    LoaderResolutionState loader,
    RuntimeCacheIdentity cache) -> SettingsState
{
    if (loader == LoaderResolutionState::Missing)
    {
        return SettingsState::Missing;
    }
    if (loader != LoaderResolutionState::ManagedCompatible)
    {
        return SettingsState::Incompatible;
    }
    return cache == RuntimeCacheIdentity::Exact ||
                   cache == RuntimeCacheIdentity::OwnedPrevious
               ? SettingsState::Compatible
               : SettingsState::Incompatible;
}

auto observation_error(
    std::string_view operation,
    std::string_view detail)
    -> std::unexpected<LauncherObservationError>
{
    return error(
        std::string{operation} + ": " + std::string{detail});
}
} // namespace

auto Win32OriginalUserObservationPlatform::game_running()
    -> std::expected<bool, LauncherObservationError>
{
    const auto result = is_target_game_running();
    if (!result)
    {
        return observation_error(
            "Game-process observation",
            result.error().detail);
    }
    return *result;
}

auto Win32OriginalUserObservationPlatform::steam_launch_options()
    -> std::expected<
        std::optional<std::string>,
        LauncherObservationError>
{
    const auto result =
        read_windows_active_steam_launch_options();
    if (!result)
    {
        return observation_error(
            "Steam launch-option observation",
            result.error().detail);
    }
    return *result;
}

auto Win32OriginalUserObservationPlatform::directory_writable(
    const fs::path& path)
    -> std::expected<bool, LauncherObservationError>
{
    return observe_directory_writable(path);
}

Win32LauncherObservationSource::Win32LauncherObservationSource(
    const PayloadManifest& manifest,
    Sha256Digest manifest_sha256,
    RuntimeStorage& runtime_storage,
    Win32LauncherObservationPaths paths,
    LauncherMaterialProvider& material_provider,
    OriginalUserObservationPlatform& platform)
    : manifest_(manifest),
      manifest_sha256_(manifest_sha256),
      runtime_storage_(runtime_storage),
      paths_(std::move(paths)),
      material_provider_(material_provider),
      platform_(platform)
{
}

auto Win32LauncherObservationSource::observe_evidence()
    -> std::expected<
        LauncherObservationEvidence,
        LauncherObservationError>
{
    shared_runtime_directory_.reset();
    const auto valid_paths = validate_paths(paths_);
    if (!valid_paths)
    {
        return std::unexpected(valid_paths.error());
    }
    const auto running = platform_.game_running();
    if (!running)
    {
        return std::unexpected(running.error());
    }
    const auto cache = observe_recovered_runtime_cache(
        runtime_storage_,
        manifest_sha256_);
    if (!cache)
    {
        return observation_error(
            "Runtime-cache observation",
            cache.error().detail);
    }
    const auto expectations =
        build_managed_loader_expectations(
            manifest_,
            manifest_sha256_,
            paths_.active_runtime_directory);
    if (!expectations)
    {
        return observation_error(
            "Managed-loader expectation",
            expectations.error().detail);
    }
    const auto managed = observe_managed_loader(
        paths_.game_directory,
        paths_.ownership_directory,
        *expectations);
    if (!managed)
    {
        return observation_error(
            "Managed-loader observation",
            managed.error().detail);
    }
    const auto launch_options =
        platform_.steam_launch_options();
    if (!launch_options)
    {
        return std::unexpected(launch_options.error());
    }
    auto command_target =
        std::optional<fs::path>{};
    if (*launch_options)
    {
        const auto analyzed = analyze_windows_launch_options(
            **launch_options,
            paths_.game_directory);
        if (!analyzed)
        {
            return observation_error(
                "Steam launch-option analysis",
                analyzed.error().detail);
        }
        command_target = *analyzed;
    }
    const auto runtime_sha256 =
        pinned_runtime_sha256(manifest_);
    if (!runtime_sha256)
    {
        return std::unexpected(runtime_sha256.error());
    }
    const auto loader = observe_loader_filesystem_details(
        LoaderFilesystemRequest{
            paths_.game_directory,
            command_target
                ? DirectiveState::Unowned
                : DirectiveState::Absent,
            command_target,
            directive_state(managed->override_file),
            *runtime_sha256,
        });
    if (!loader)
    {
        return observation_error(
            "Loader-chain observation",
            loader.error().detail);
    }
    const auto resolution = resolve_loader_chain(loader->chain);

    auto settings = managed_settings_state(
        resolution.state,
        *cache);
    auto shared_mod = ArtifactState::Missing;
    if (resolution.state ==
        LoaderResolutionState::SharedCompatible)
    {
        const auto root = shared_runtime_root(
            resolution,
            *loader,
            paths_.game_directory);
        if (!root)
        {
            return std::unexpected(root.error());
        }
        shared_runtime_directory_ = *root;
        const auto material = material_provider_.shared_mod();
        if (!material || *material == nullptr)
        {
            return observation_error(
                "Shared-mod material",
                material
                    ? "the material provider returned null"
                    : material.error().detail);
        }
        const auto runtime_settings =
            observe_shared_runtime_settings(
                *root,
                **material);
        if (!runtime_settings)
        {
            return observation_error(
                "Shared runtime/config observation",
                runtime_settings.error().detail);
        }
        settings = *runtime_settings;
        if (settings == SettingsState::Compatible)
        {
            const auto mod = observe_shared_mod(
                *root,
                paths_.ownership_directory,
                **material);
            if (!mod)
            {
                return observation_error(
                    "Shared-mod observation",
                    mod.error().detail);
            }
            shared_mod = *mod;
        }
        else
        {
            shared_mod = ArtifactState::Conflict;
        }
    }

    const auto user_cache_writable =
        platform_.directory_writable(paths_.runtime_root);
    if (!user_cache_writable)
    {
        return std::unexpected(user_cache_writable.error());
    }
    const auto game_directory_writable =
        platform_.directory_writable(paths_.game_directory);
    if (!game_directory_writable)
    {
        return std::unexpected(game_directory_writable.error());
    }
    auto shared_runtime_writable = false;
    if (shared_runtime_directory_)
    {
        const auto writable = platform_.directory_writable(
            *shared_runtime_directory_ / "Mods");
        if (!writable)
        {
            return std::unexpected(writable.error());
        }
        shared_runtime_writable = *writable;
    }

    return LauncherObservationEvidence{
        *running,
        true,
        *user_cache_writable,
        *game_directory_writable,
        shared_runtime_writable,
        loader->chain,
        *managed,
        *cache,
        settings,
        shared_mod,
    };
}

auto Win32LauncherObservationSource::observe_preparation()
    -> std::expected<
        PreparationEnvironment,
        LauncherObservationError>
{
    const auto evidence = observe_evidence();
    if (!evidence)
    {
        return std::unexpected(evidence.error());
    }
    return assemble_preparation_environment(*evidence);
}

auto Win32LauncherObservationSource::observe_removal()
    -> std::expected<
        RemovalObservation,
        LauncherObservationError>
{
    const auto evidence = observe_evidence();
    if (!evidence)
    {
        return std::unexpected(evidence.error());
    }
    return assemble_removal_observation(*evidence);
}

auto Win32LauncherObservationSource::shared_runtime_directory() const
    -> const std::optional<fs::path>&
{
    return shared_runtime_directory_;
}

auto Win32LauncherObservationSource::
    selected_shared_runtime_directory() const
    -> std::expected<fs::path, LauncherEffectError>
{
    if (!shared_runtime_directory_)
    {
        return std::unexpected(LauncherEffectError{
            "No compatible shared runtime was selected by the "
            "original-user observation.",
        });
    }
    return *shared_runtime_directory_;
}
} // namespace meccha::launcher
