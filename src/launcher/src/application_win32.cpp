#include <meccha/launcher/application_win32.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <array>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

template <class Error>
auto application_error(Error value)
    -> std::unexpected<Win32LauncherApplicationError>
{
    return std::unexpected(Win32LauncherApplicationError{
        std::in_place_type<Error>,
        std::move(value),
    });
}

auto bootstrap_error(
    LauncherBootstrapErrorCode code,
    std::string detail)
    -> std::unexpected<LauncherBootstrapError>
{
    return std::unexpected(LauncherBootstrapError{
        code,
        std::move(detail),
    });
}

auto discovery_error(std::string detail)
    -> std::unexpected<GameDiscoveryError>
{
    return std::unexpected(GameDiscoveryError{
        GameDiscoveryErrorCode::InvalidInstallDirectory,
        std::move(detail),
    });
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

auto utf8_path(std::string_view value)
    -> std::expected<fs::path, GameDiscoveryError>
{
    if (value.empty() ||
        value.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()) ||
        value.find('\0') != std::string_view::npos)
    {
        return discovery_error(
            "The explicit game directory is not valid UTF-8.");
    }
    const auto count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (count <= 0)
    {
        return discovery_error(
            "The explicit game directory is not valid UTF-8.");
    }
    auto converted = std::wstring(
        static_cast<std::size_t>(count),
        L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            converted.data(),
            count) != count)
    {
        return discovery_error(
            "The explicit game directory could not be converted.");
    }
    return fs::path{std::move(converted)};
}

auto valid_base_directory(const fs::path& path) -> bool
{
    return path.is_absolute() &&
           path.lexically_normal() == path &&
           !path.filename().empty();
}
} // namespace

auto Win32LauncherBootstrapPlatform::
    validate_explicit_game_directory(std::string_view value)
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    const auto selected = utf8_path(value);
    if (!selected)
    {
        return std::unexpected(selected.error());
    }
    return validate_game_directory(*selected);
}

auto Win32LauncherBootstrapPlatform::discover_game_installation()
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    const auto roots = discover_windows_steam_roots();
    if (!roots)
    {
        return std::unexpected(roots.error());
    }
    return meccha::launcher::discover_game_installation(*roots);
}

auto Win32LauncherBootstrapPlatform::pick_game_installation()
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    return pick_windows_game_installation();
}

auto Win32LauncherBootstrapPlatform::local_app_data_directory()
    -> std::expected<fs::path, LauncherBootstrapError>
{
    PWSTR raw_path{};
    const auto result = SHGetKnownFolderPath(
        FOLDERID_LocalAppData,
        KF_FLAG_DEFAULT,
        nullptr,
        &raw_path);
    if (FAILED(result) || raw_path == nullptr)
    {
        if (raw_path != nullptr)
        {
            CoTaskMemFree(raw_path);
        }
        return bootstrap_error(
            LauncherBootstrapErrorCode::KnownFolder,
            "The invoking user's LocalAppData directory could not "
            "be resolved (HRESULT " +
                std::to_string(
                    static_cast<unsigned long>(result)) +
                ").");
    }
    auto path = fs::path{raw_path}.lexically_normal();
    CoTaskMemFree(raw_path);

    std::error_code directory_error{};
    if (!valid_base_directory(path) ||
        !fs::is_directory(path, directory_error) ||
        directory_error)
    {
        return bootstrap_error(
            LauncherBootstrapErrorCode::KnownFolder,
            "The invoking user's LocalAppData directory is not an "
            "available absolute directory.");
    }
    return path;
}

auto Win32LauncherBootstrapPlatform::runtime_nonce()
    -> std::expected<std::string, LauncherBootstrapError>
{
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid)))
    {
        return bootstrap_error(
            LauncherBootstrapErrorCode::Nonce,
            "A private runtime staging nonce could not be generated.");
    }
    static_assert(sizeof(GUID) == 16U);
    constexpr std::array<char, 16> Hex{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    const auto raw = std::as_bytes(std::span{&guid, 1U});
    auto result = std::string{};
    result.reserve(raw.size() * 2U);
    for (const auto value : raw)
    {
        const auto byte = std::to_integer<unsigned int>(value);
        result.push_back(Hex[byte >> 4U]);
        result.push_back(Hex[byte & 0x0FU]);
    }
    return result;
}

auto make_launcher_data_paths(
    const fs::path& local_app_data_directory)
    -> std::expected<
        Win32LauncherDataPaths,
        LauncherBootstrapError>
{
    const auto base = local_app_data_directory.lexically_normal();
    if (!valid_base_directory(base))
    {
        return bootstrap_error(
            LauncherBootstrapErrorCode::InvalidPath,
            "The LocalAppData base must be an absolute normalized "
            "directory path.");
    }
    const auto data_root =
        base / "MecchaCamouflage" / "v2";
    return Win32LauncherDataPaths{
        data_root,
        data_root / "runtime",
        data_root / "ownership",
    };
}

auto resolve_launcher_game_installation(
    const LauncherArguments& arguments,
    LauncherBootstrapPlatform& platform)
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    if (arguments.game_directory_utf8)
    {
        return platform.validate_explicit_game_directory(
            *arguments.game_directory_utf8);
    }
    const auto automatic = platform.discover_game_installation();
    if (automatic)
    {
        return automatic;
    }
    return platform.pick_game_installation();
}

auto run_win32_launcher_application(
    const Win32LauncherApplicationInputs& inputs)
    -> std::expected<
        Win32LauncherApplicationResult,
        Win32LauncherApplicationError>
{
    auto instance = acquire_launcher_instance();
    if (!instance)
    {
        return application_error(instance.error());
    }

    const auto arguments =
        parse_launcher_arguments(inputs.arguments);
    if (!arguments)
    {
        return application_error(arguments.error());
    }
    const auto installation = resolve_launcher_game_installation(
        *arguments,
        inputs.bootstrap_platform);
    if (!installation)
    {
        return application_error(installation.error());
    }
    const auto local_app_data =
        inputs.bootstrap_platform.local_app_data_directory();
    if (!local_app_data)
    {
        return application_error(local_app_data.error());
    }
    const auto paths = make_launcher_data_paths(*local_app_data);
    if (!paths)
    {
        return application_error(paths.error());
    }

    const auto running =
        inputs.observation_platform.game_running();
    if (!running)
    {
        return application_error(running.error());
    }
    if (*running)
    {
        return application_error(running_error(arguments->mode));
    }

    const auto runtime_nonce =
        inputs.bootstrap_platform.runtime_nonce();
    if (!runtime_nonce)
    {
        return application_error(runtime_nonce.error());
    }
    auto package = inputs.package_source.load(
        paths->runtime_root,
        arguments->mode);
    if (!package)
    {
        return application_error(package.error());
    }
    if (!package->payload_source)
    {
        return application_error(RuntimePayloadError{
            "The launcher package source returned no payload source.",
        });
    }
    const auto elevated_loader_broker =
        inputs.elevated_loader_broker_provider.bind(
            package->manifest_sha256);
    if (!elevated_loader_broker ||
        *elevated_loader_broker == nullptr)
    {
        return application_error(RuntimePayloadError{
            elevated_loader_broker
                ? "The elevated loader broker provider returned "
                  "no broker."
                : elevated_loader_broker.error().detail,
        });
    }

    auto composition = run_win32_launcher_composition(
        Win32LauncherCompositionInputs{
            arguments->mode,
            package->manifest,
            package->manifest_json,
            package->manifest_sha256,
            *package->payload_source,
            installation->binaries_directory,
            paths->runtime_root,
            paths->ownership_directory,
            *runtime_nonce,
            inputs.observation_platform,
            **elevated_loader_broker,
            inputs.steam_launcher,
        });
    if (!composition)
    {
        return std::visit(
            [](auto error)
                -> std::expected<
                    Win32LauncherApplicationResult,
                    Win32LauncherApplicationError>
            {
                return application_error(std::move(error));
            },
            std::move(composition.error()));
    }

    return Win32LauncherApplicationResult{
        *arguments,
        *installation,
        *paths,
        std::move(*composition),
    };
}
} // namespace meccha::launcher
