#include <meccha/launcher/execution_win32.hpp>
#include <meccha/launcher/hash.hpp>
#include <meccha/launcher/observation_win32.hpp>
#include <meccha/launcher/shared_mod.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL launcher_observation_win32: "
                  << message << '\n';
    }
    return condition;
}

auto bytes(std::string_view text) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{text});
    return {view.begin(), view.end()};
}

auto write_bytes(
    const fs::path& path,
    std::span<const std::byte> value) -> void
{
    fs::create_directories(path.parent_path());
    std::ofstream output{
        path,
        std::ios::binary | std::ios::trunc};
    output.write(
        reinterpret_cast<const char*>(value.data()),
        static_cast<std::streamsize>(value.size()));
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static std::uint64_t sequence{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-observation-win32-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "game");
        fs::create_directories(root / "runtime");
        fs::create_directories(root / "shared" / "Mods");
    }

    TemporaryTree(const TemporaryTree&) = delete;
    auto operator=(const TemporaryTree&) -> TemporaryTree& = delete;

    ~TemporaryTree()
    {
        std::error_code ignored{};
        fs::remove_all(root, ignored);
    }

    fs::path root{};
};

class MemoryPayloadSource final : public RuntimePayloadSource
{
public:
    auto read_file(std::string_view relative_path)
        -> std::expected<
            std::vector<std::byte>,
            RuntimePayloadError> override
    {
        const auto found = files.find(std::string{relative_path});
        if (found == files.end())
        {
            return std::unexpected(RuntimePayloadError{
                "missing payload fixture",
            });
        }
        return found->second;
    }

    std::map<
        std::string,
        std::vector<std::byte>,
        std::less<>> files{};
};

struct Package
{
    PayloadManifest manifest{};
    Sha256Digest manifest_sha256{};
    MemoryPayloadSource payload{};
};

auto add_file(
    Package& package,
    std::string path,
    FileRole role,
    std::string_view contents) -> void
{
    auto value = bytes(contents);
    package.manifest.files.push_back(ManifestFile{
        path,
        role,
        value.size(),
        sha256_bytes(value).value(),
    });
    package.payload.files.emplace(
        std::move(path),
        std::move(value));
}

auto make_package() -> Package
{
    auto package = Package{};
    package.manifest.schema_version = 1;
    package.manifest.product_version = "2.0.0";
    package.manifest.ue4ss_commit =
        "6c26f038751b3d96059d4a9148f5d093012d55ad";
    add_file(
        package,
        "dwmapi.dll",
        FileRole::Proxy,
        "proxy");
    add_file(
        package,
        "UE4SS.dll",
        FileRole::Runtime,
        "runtime");
    add_file(
        package,
        "Mods/MecchaCamouflage/dlls/main.dll",
        FileRole::Mod,
        "main");
    add_file(
        package,
        "Mods/MecchaCamouflage/enabled.txt",
        FileRole::Mod,
        "");
    package.manifest.total_size = 16U;
    package.manifest_sha256 = sha256_bytes(
        std::as_bytes(std::span{
            package.manifest.product_version}))
                                  .value();
    return package;
}

class MissingRuntimeStorage final : public RuntimeStorage
{
public:
    auto identify_generation(std::string_view)
        -> std::expected<
            GenerationIdentity,
            RuntimeStorageError> override
    {
        return GenerationIdentity{
            GenerationState::Missing,
            {},
        };
    }

    auto list_staging_generations()
        -> std::expected<
            std::vector<std::string>,
            RuntimeStorageError> override
    {
        return std::vector<std::string>{};
    }

    auto read_journal()
        -> std::expected<
            std::optional<RuntimeTransactionJournal>,
            RuntimeStorageError> override
    {
        return std::nullopt;
    }

    auto stage_generation(
        std::string_view,
        const Sha256Digest&)
        -> std::expected<void, RuntimeStorageError> override
    {
        return mutation_error();
    }

    auto write_journal(const RuntimeTransactionJournal&)
        -> std::expected<void, RuntimeStorageError> override
    {
        return mutation_error();
    }

    auto rename_generation(std::string_view, std::string_view)
        -> std::expected<void, RuntimeStorageError> override
    {
        return mutation_error();
    }

    auto remove_generation(
        std::string_view,
        const Sha256Digest&)
        -> std::expected<void, RuntimeStorageError> override
    {
        return mutation_error();
    }

    auto remove_journal()
        -> std::expected<void, RuntimeStorageError> override
    {
        return mutation_error();
    }

private:
    static auto mutation_error()
        -> std::unexpected<RuntimeStorageError>
    {
        return std::unexpected(RuntimeStorageError{
            RuntimeStorageErrorCode::Conflict,
            "read-only observation attempted a mutation",
        });
    }
};

class FakeOriginalUserPlatform final
    : public OriginalUserObservationPlatform
{
public:
    auto game_running()
        -> std::expected<
            bool,
            LauncherObservationError> override
    {
        ++game_checks;
        return running;
    }

    auto steam_launch_options()
        -> std::expected<
            std::optional<std::string>,
            LauncherObservationError> override
    {
        ++launch_option_reads;
        return launch_options;
    }

    auto directory_writable(const fs::path& path)
        -> std::expected<
            bool,
            LauncherObservationError> override
    {
        writable_paths.push_back(path);
        return writable;
    }

    bool running{};
    bool writable{true};
    std::optional<std::string> launch_options{};
    std::size_t game_checks{};
    std::size_t launch_option_reads{};
    std::vector<fs::path> writable_paths{};
};

class StaticMaterialProvider final
    : public LauncherMaterialProvider
{
public:
    StaticMaterialProvider(
        const ManagedLoaderMaterial& managed,
        const SharedModMaterial& shared)
        : managed_(managed),
          shared_(shared)
    {
    }

    auto managed_loader()
        -> std::expected<
            const ManagedLoaderMaterial*,
            LauncherEffectError> override
    {
        return &managed_;
    }

    auto shared_mod()
        -> std::expected<
            const SharedModMaterial*,
            LauncherEffectError> override
    {
        return &shared_;
    }

private:
    const ManagedLoaderMaterial& managed_;
    const SharedModMaterial& shared_;
};
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    TemporaryTree tree{};
    auto package = make_package();
    const auto shared_material = build_shared_mod_material(
        package.manifest,
        package.manifest_sha256,
        package.payload);
    if (!shared_material)
    {
        std::cerr << "FAIL launcher_observation_win32: "
                  << shared_material.error().detail << '\n';
        return 1;
    }
    const auto managed_material = ManagedLoaderMaterial{};
    StaticMaterialProvider material_provider{
        managed_material,
        *shared_material,
    };
    MissingRuntimeStorage storage{};
    FakeOriginalUserPlatform platform{};
    const auto paths = Win32LauncherObservationPaths{
        tree.root / "game",
        tree.root / "runtime",
        tree.root / "runtime" / "active",
        tree.root / "ownership",
    };
    Win32LauncherObservationSource clean_source{
        package.manifest,
        package.manifest_sha256,
        storage,
        paths,
        material_provider,
        platform,
    };

    const auto clean = clean_source.observe_preparation();
    auto passed = expect(
        clean &&
            clean->runtime_cache ==
                RuntimeCacheState::PublishRequired &&
            clean->deployment ==
                DeploymentObservation{
                    LaunchOptionState::Absent,
                    ArtifactState::Missing,
                    ArtifactState::Missing,
                    RuntimeState::Missing,
                    SettingsState::Missing,
                    ArtifactState::Missing,
                } &&
            !clean_source.shared_runtime_directory() &&
            !fs::exists(paths.ownership_directory),
        "clean managed observation was not read-only or complete");

    const auto proxy = package.payload.files.at("dwmapi.dll");
    const auto runtime = package.payload.files.at("UE4SS.dll");
    write_bytes(paths.game_directory / "dwmapi.dll", proxy);
    write_bytes(tree.root / "shared" / "UE4SS.dll", runtime);
    platform.launch_options =
        "--ue4ss-path \"" +
        (tree.root / "shared" / "UE4SS.dll").string() +
        "\"";
    platform.writable_paths.clear();

    Win32LauncherObservationSource shared_source{
        package.manifest,
        package.manifest_sha256,
        storage,
        paths,
        material_provider,
        platform,
    };
    const auto shared = shared_source.observe_preparation();
    const auto shared_removal =
        shared_source.observe_removal();
    passed &= expect(
        shared &&
            shared->shared_runtime_writable &&
            shared->deployment ==
                DeploymentObservation{
                    LaunchOptionState::PinnedRuntime,
                    ArtifactState::ExactUnowned,
                    ArtifactState::Missing,
                    RuntimeState::SharedCompatible,
                    SettingsState::Compatible,
                    ArtifactState::Missing,
                } &&
            shared_source.shared_runtime_directory() ==
                std::optional<fs::path>{
                    tree.root / "shared"} &&
            shared_removal &&
            shared_removal->mode == RemovalMode::Shared &&
            shared_removal->mod == ArtifactState::Missing &&
            !fs::exists(paths.ownership_directory),
        "exact shared runtime observation was not bound safely");
    passed &= expect(
        platform.game_checks == 3U &&
            platform.launch_option_reads == 3U,
        "the platform boundary was not called exactly once per observation");

    Win32OriginalUserObservationPlatform native_platform{};
    const auto absent_probe =
        tree.root / "not-created" / "nested";
    const auto writable =
        native_platform.directory_writable(absent_probe);
    passed &= expect(
        writable && *writable &&
            !fs::exists(tree.root / "not-created") &&
            !native_platform.directory_writable(
                fs::path{"relative"}),
        "the native access probe mutated or accepted an invalid path");

    if (passed)
    {
        std::cout << "PASS launcher_observation_win32\n";
        return 0;
    }
    return 1;
}
