#include <meccha/launcher/elevated_child.hpp>
#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
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
        std::cerr << "FAIL elevated_child_win32: "
                  << message << '\n';
    }
    return condition;
}

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto input = std::as_bytes(std::span{value});
    return {input.begin(), input.end()};
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static std::uint64_t sequence{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-elevated-child-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "game");
        fs::create_directories(
            root / "local/MecchaCamouflage/v2/runtime/active");
        fs::create_directories(root / "scratch");
    }

    TemporaryTree(const TemporaryTree&) = delete;
    auto operator=(const TemporaryTree&)
        -> TemporaryTree& = delete;

    ~TemporaryTree()
    {
        std::error_code ignored{};
        fs::remove_all(root, ignored);
    }

    fs::path root{};
};

struct PackageFixture
{
    PayloadManifest manifest{};
    Sha256Digest manifest_sha256{};
    std::map<
        std::string,
        std::vector<std::byte>,
        std::less<>> files{};
};

auto make_package() -> PackageFixture
{
    auto result = PackageFixture{};
    result.files["dwmapi.dll"] = bytes("trusted proxy");
    const auto& proxy = result.files.at("dwmapi.dll");
    result.manifest = PayloadManifest{
        1U,
        "2.0.0",
        "6c26f038751b3d96059d4a9148f5d093012d55ad",
        {
            ManifestFile{
                "dwmapi.dll",
                FileRole::Proxy,
                proxy.size(),
                sha256_bytes(proxy).value(),
            },
        },
        {},
        proxy.size(),
    };
    result.manifest_sha256 =
        sha256_bytes(
            std::as_bytes(std::span{"manifest", 8U}))
            .value();
    return result;
}

class MemoryPayloadSource final : public RuntimePayloadSource
{
public:
    explicit MemoryPayloadSource(
        const PackageFixture& package)
        : package_(package)
    {
    }

    auto read_file(std::string_view relative_path)
        -> std::expected<
            std::vector<std::byte>,
            RuntimePayloadError> override
    {
        const auto found =
            package_.files.find(std::string{relative_path});
        if (found == package_.files.end())
        {
            return std::unexpected(RuntimePayloadError{
                "missing test payload",
            });
        }
        return found->second;
    }

private:
    const PackageFixture& package_;
};

class FakePackageSource final : public LauncherPackageSource
{
public:
    explicit FakePackageSource(
        const PackageFixture& package)
        : package_(package)
    {
    }

    auto load(
        const fs::path& scratch_parent,
        LauncherInvocationMode mode)
        -> std::expected<
            LoadedLauncherPackage,
            RuntimePayloadError> override
    {
        ++calls;
        observed_scratch = scratch_parent;
        observed_mode = mode;
        return LoadedLauncherPackage{
            "{}",
            package_.manifest,
            package_.manifest_sha256,
            std::make_unique<MemoryPayloadSource>(
                package_),
        };
    }

    const PackageFixture& package_;
    std::size_t calls{};
    fs::path observed_scratch{};
    LauncherInvocationMode observed_mode{};
};

class FakeEnvironment final
    : public ElevatedBrokerChildEnvironment
{
public:
    explicit FakeEnvironment(const TemporaryTree& tree)
        : tree_(tree)
    {
    }

    auto temporary_directory()
        -> std::expected<
            fs::path,
            ElevatedLoaderMutationError> override
    {
        ++temporary_calls;
        return tree_.root / "scratch";
    }

    auto original_user_local_app_data(
        const ElevatedBrokerParentIdentity& parent)
        -> std::expected<
            fs::path,
            ElevatedLoaderMutationError> override
    {
        ++local_data_calls;
        observed_parent = parent;
        return tree_.root / "local";
    }

    const TemporaryTree& tree_;
    std::size_t temporary_calls{};
    std::size_t local_data_calls{};
    ElevatedBrokerParentIdentity observed_parent{};
};

class FakeMutationPlatform final
    : public ElevatedLoaderMutationPlatform
{
public:
    explicit FakeMutationPlatform(fs::path game_directory)
        : game_directory_(std::move(game_directory))
    {
    }

    auto game_running()
        -> std::expected<
            bool,
            ElevatedLoaderMutationError> override
    {
        return false;
    }

    auto validate_game_directory(const fs::path& path)
        -> std::expected<
            GameInstallation,
            ElevatedLoaderMutationError> override
    {
        if (path != game_directory_)
        {
            return std::unexpected(ElevatedLoaderMutationError{
                ElevatedLoaderMutationErrorCode::GameDirectory,
                "unexpected game directory",
            });
        }
        return GameInstallation{
            {},
            {},
            game_directory_,
            game_directory_ /
                fs::path{TargetGameExecutable},
        };
    }

private:
    fs::path game_directory_{};
};
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    TemporaryTree tree{};
    const auto package = make_package();
    auto material_source = MemoryPayloadSource{package};
    const auto active =
        tree.root /
        "local/MecchaCamouflage/v2/runtime/active";
    const auto material = build_managed_loader_material(
        package.manifest,
        package.manifest_sha256,
        active,
        material_source);
    if (!material)
    {
        std::cerr << material.error().detail << '\n';
        return 1;
    }

    auto package_source = FakePackageSource{package};
    auto environment = FakeEnvironment{tree};
    auto mutation_platform =
        FakeMutationPlatform{tree.root / "game"};
    auto executor =
        Win32EmbeddedElevatedLoaderRequestExecutor{
            package_source,
            environment,
            mutation_platform,
        };
    const auto parent = ElevatedBrokerParentIdentity{
        GetCurrentProcessId(),
        17U,
        L"S-1-5-21-original",
    };
    const auto request = ElevatedLoaderMutationRequest{
        ElevatedLoaderMutationSchemaVersion,
        ElevatedLoaderOperation::Apply,
        package.manifest_sha256,
        "0123456789abcdef0123456789abcdef",
        tree.root / "game",
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Install,
            std::nullopt,
            FileMeasurement{
                material->proxy.file.size,
                material->proxy.file.sha256,
            },
        },
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Install,
            std::nullopt,
            FileMeasurement{
                material->override_file.file.size,
                material->override_file.file.sha256,
            },
        },
    };
    const auto executed = executor.execute(request, parent);
    passed &= expect(
        executed &&
            *executed ==
                ElevatedLoaderMutationResult{true, true} &&
            package_source.calls == 1U &&
            package_source.observed_scratch ==
                tree.root / "scratch" &&
            package_source.observed_mode ==
                LauncherInvocationMode::PrepareOnly &&
            environment.temporary_calls == 1U &&
            environment.local_data_calls == 1U &&
            environment.observed_parent == parent &&
            fs::exists(tree.root / "game/dwmapi.dll") &&
            fs::exists(tree.root / "game/override.txt"),
        "authenticated child did not rebuild and execute its package");

    auto remove_request = request;
    remove_request.operation =
        ElevatedLoaderOperation::Remove;
    remove_request.request_nonce =
        "1123456789abcdef0123456789abcdef";
    remove_request.proxy =
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Remove,
            request.proxy.desired,
            {},
        };
    remove_request.override_file =
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Remove,
            request.override_file.desired,
            {},
        };
    const auto removed =
        executor.execute(remove_request, parent);
    passed &= expect(
        removed &&
            *removed ==
                ElevatedLoaderMutationResult{true, true} &&
            package_source.calls == 2U &&
            package_source.observed_mode ==
                LauncherInvocationMode::Remove &&
            environment.temporary_calls == 2U &&
            environment.local_data_calls == 1U &&
            !fs::exists(tree.root / "game/dwmapi.dll") &&
            !fs::exists(tree.root / "game/override.txt"),
        "remove rebuilt original-user material or left loader files");

    auto wrong_manifest_request = request;
    wrong_manifest_request.manifest_sha256.bytes.front() ^=
        std::byte{0xff};
    const auto rejected =
        executor.execute(wrong_manifest_request, parent);
    passed &= expect(
        !rejected &&
            environment.temporary_calls == 3U &&
            environment.local_data_calls == 1U,
        "manifest mismatch reached original-user data resolution");

    auto peer_validator =
        Win32ElevatedBrokerPeerValidator{};
    const auto current_process_id = GetCurrentProcessId();
    const auto current_parent =
        peer_validator.validate_original_parent(
            current_process_id,
            current_process_id);
    auto native_environment =
        Win32ElevatedBrokerChildEnvironment{};
    const auto native_temporary =
        native_environment.temporary_directory();
    const auto native_local =
        current_parent
            ? native_environment
                  .original_user_local_app_data(
                      *current_parent)
            : std::expected<
                  fs::path,
                  ElevatedLoaderMutationError>{
                  std::unexpected(
                      current_parent.error())};
    passed &= expect(
        current_parent &&
            native_temporary &&
            native_temporary->is_absolute() &&
            fs::is_directory(*native_temporary) &&
            native_local &&
            native_local->is_absolute() &&
            fs::is_directory(*native_local),
        "native child environment did not resolve validated user paths");

    if (passed)
    {
        std::cout << "PASS elevated_child_win32\n";
        return 0;
    }
    return 1;
}
