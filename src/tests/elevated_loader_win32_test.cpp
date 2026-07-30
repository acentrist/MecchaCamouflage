#include <meccha/launcher/elevated_loader.hpp>
#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::launcher;
namespace fs = std::filesystem;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL elevated_loader_win32: "
                  << message << '\n';
    }
    return condition;
}

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{value});
    return {view.begin(), view.end()};
}

auto measurement(std::string_view value) -> FileMeasurement
{
    const auto data = bytes(value);
    return FileMeasurement{
        data.size(),
        sha256_bytes(data).value(),
    };
}

auto write_text(const fs::path& path, std::string_view value) -> void
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

auto read_text(const fs::path& path) -> std::string
{
    std::ifstream input{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static std::uint64_t sequence{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-elevated-loader-" +
                std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(GetTickCount64()) + "-" +
                std::to_string(sequence));
        fs::create_directories(root / "game");
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

class FakePlatform final : public ElevatedLoaderMutationPlatform
{
public:
    auto game_running()
        -> std::expected<
            bool,
            ElevatedLoaderMutationError> override
    {
        ++running_checks;
        return running;
    }

    auto validate_game_directory(const fs::path& path)
        -> std::expected<
            GameInstallation,
            ElevatedLoaderMutationError> override
    {
        ++game_validations;
        if (!game_valid)
        {
            return std::unexpected(ElevatedLoaderMutationError{
                ElevatedLoaderMutationErrorCode::GameDirectory,
                "invalid game fixture",
            });
        }
        return GameInstallation{
            {},
            path,
            path,
            path / "PenguinHotel-Win64-Shipping.exe",
        };
    }

    bool running{};
    bool game_valid{true};
    std::size_t running_checks{};
    std::size_t game_validations{};
};

struct MaterialFixture
{
    Sha256Digest manifest_sha256{};
    ManagedLoaderMaterial material{};
};

auto make_material(
    std::string_view proxy,
    std::string_view override_text) -> MaterialFixture
{
    const auto manifest_sha256 =
        sha256_bytes(bytes("manifest")).value();
    const auto proxy_bytes = bytes(proxy);
    const auto override_bytes = bytes(override_text);
    return MaterialFixture{
        manifest_sha256,
        ManagedLoaderMaterial{
            OwnedFileExpectation{
                "2.0.0",
                manifest_sha256,
                ManifestFile{
                    "dwmapi.dll",
                    FileRole::Proxy,
                    proxy_bytes.size(),
                    sha256_bytes(proxy_bytes).value(),
                },
            },
            proxy_bytes,
            OwnedFileExpectation{
                "2.0.0",
                manifest_sha256,
                ManifestFile{
                    "override.txt",
                    FileRole::Override,
                    override_bytes.size(),
                    sha256_bytes(override_bytes).value(),
                },
            },
            override_bytes,
        },
    };
}

auto install(
    std::optional<FileMeasurement> current,
    FileMeasurement desired) -> ElevatedLoaderFileMutation
{
    return ElevatedLoaderFileMutation{
        ElevatedLoaderFileAction::Install,
        current,
        desired,
    };
}

auto verify(FileMeasurement desired)
    -> ElevatedLoaderFileMutation
{
    return ElevatedLoaderFileMutation{
        ElevatedLoaderFileAction::Verify,
        desired,
        desired,
    };
}

auto remove(FileMeasurement current)
    -> ElevatedLoaderFileMutation
{
    return ElevatedLoaderFileMutation{
        ElevatedLoaderFileAction::Remove,
        current,
        {},
    };
}

auto ignore() -> ElevatedLoaderFileMutation
{
    return ElevatedLoaderFileMutation{};
}

auto request(
    ElevatedLoaderOperation operation,
    const MaterialFixture& material,
    const TemporaryTree& tree,
    ElevatedLoaderFileMutation proxy,
    ElevatedLoaderFileMutation override_file)
    -> ElevatedLoaderMutationRequest
{
    return ElevatedLoaderMutationRequest{
        ElevatedLoaderMutationSchemaVersion,
        operation,
        material.manifest_sha256,
        "0123456789abcdef0123456789abcdef",
        tree.root / "game",
        std::move(proxy),
        std::move(override_file),
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    const auto material = make_material(
        "pinned-proxy",
        "C:\\Users\\original\\runtime\\active");

    TemporaryTree fresh{};
    FakePlatform platform{};
    const auto fresh_result = execute_elevated_loader_mutation(
        request(
            ElevatedLoaderOperation::Apply,
            material,
            fresh,
            install(std::nullopt, measurement("pinned-proxy")),
            install(
                std::nullopt,
                measurement(
                    "C:\\Users\\original\\runtime\\active"))),
        material.manifest_sha256,
        &material.material,
        platform);
    passed &= expect(
        fresh_result &&
            fresh_result->proxy_mutated &&
            fresh_result->override_mutated &&
            read_text(fresh.root / "game" / "dwmapi.dll") ==
                "pinned-proxy" &&
            read_text(fresh.root / "game" / "override.txt") ==
                "C:\\Users\\original\\runtime\\active" &&
            !fs::exists(fresh.root / "ownership") &&
            platform.running_checks == 1U &&
            platform.game_validations == 1U,
        "fresh elevated mutation did not stay inside two game files");

    TemporaryTree shared_proxy{};
    write_text(
        shared_proxy.root / "game" / "dwmapi.dll",
        "pinned-proxy");
    const auto shared_result = execute_elevated_loader_mutation(
        request(
            ElevatedLoaderOperation::Apply,
            material,
            shared_proxy,
            verify(measurement("pinned-proxy")),
            install(
                std::nullopt,
                measurement(
                    "C:\\Users\\original\\runtime\\active"))),
        material.manifest_sha256,
        &material.material,
        platform);
    passed &= expect(
        shared_result &&
            !shared_result->proxy_mutated &&
            shared_result->override_mutated &&
            read_text(
                shared_proxy.root / "game" / "dwmapi.dll") ==
                "pinned-proxy",
        "verified proxy reuse was rewritten or refused");

    TemporaryTree stale{};
    write_text(
        stale.root / "game" / "override.txt",
        "unknown");
    const auto stale_result = execute_elevated_loader_mutation(
        request(
            ElevatedLoaderOperation::Apply,
            material,
            stale,
            install(std::nullopt, measurement("pinned-proxy")),
            install(
                std::nullopt,
                measurement(
                    "C:\\Users\\original\\runtime\\active"))),
        material.manifest_sha256,
        &material.material,
        platform);
    passed &= expect(
        !stale_result &&
            stale_result.error().code ==
                ElevatedLoaderMutationErrorCode::Precondition &&
            !fs::exists(stale.root / "game" / "dwmapi.dll") &&
            read_text(stale.root / "game" / "override.txt") ==
                "unknown",
        "two-file preflight allowed a partial mutation");

    TemporaryTree replacement{};
    write_text(
        replacement.root / "game" / "dwmapi.dll",
        "old-proxy");
    const auto replace_result = execute_elevated_loader_mutation(
        request(
            ElevatedLoaderOperation::Apply,
            material,
            replacement,
            install(
                measurement("old-proxy"),
                measurement("pinned-proxy")),
            install(
                std::nullopt,
                measurement(
                    "C:\\Users\\original\\runtime\\active"))),
        material.manifest_sha256,
        &material.material,
        platform);
    passed &= expect(
        replace_result &&
            replace_result->proxy_mutated &&
            read_text(
                replacement.root / "game" / "dwmapi.dll") ==
                "pinned-proxy",
        "owned previous proxy was not replaced");

    TemporaryTree removal{};
    write_text(
        removal.root / "game" / "dwmapi.dll",
        "old-owned-proxy");
    write_text(
        removal.root / "game" / "override.txt",
        "old-owned-override");
    const auto remove_result = execute_elevated_loader_mutation(
        request(
            ElevatedLoaderOperation::Remove,
            material,
            removal,
            remove(measurement("old-owned-proxy")),
            remove(measurement("old-owned-override"))),
        material.manifest_sha256,
        nullptr,
        platform);
    passed &= expect(
        remove_result &&
            remove_result->proxy_mutated &&
            remove_result->override_mutated &&
            !fs::exists(removal.root / "game" / "dwmapi.dll") &&
            !fs::exists(removal.root / "game" / "override.txt"),
        "exact requested loader removal failed");

    TemporaryTree stale_removal{};
    write_text(
        stale_removal.root / "game" / "dwmapi.dll",
        "owned-proxy");
    write_text(
        stale_removal.root / "game" / "override.txt",
        "changed");
    const auto stale_remove_result =
        execute_elevated_loader_mutation(
            request(
                ElevatedLoaderOperation::Remove,
                material,
                stale_removal,
                remove(measurement("owned-proxy")),
                remove(measurement("owned-override"))),
            material.manifest_sha256,
            nullptr,
            platform);
    passed &= expect(
        !stale_remove_result &&
            fs::exists(
                stale_removal.root / "game" / "dwmapi.dll") &&
            read_text(
                stale_removal.root / "game" /
                "override.txt") == "changed",
        "removal preflight deleted one file before detecting change");

    TemporaryTree running{};
    FakePlatform running_platform{};
    running_platform.running = true;
    const auto running_result = execute_elevated_loader_mutation(
        request(
            ElevatedLoaderOperation::Apply,
            material,
            running,
            install(std::nullopt, measurement("pinned-proxy")),
            install(
                std::nullopt,
                measurement(
                    "C:\\Users\\original\\runtime\\active"))),
        material.manifest_sha256,
        &material.material,
        running_platform);
    passed &= expect(
        !running_result &&
            running_result.error().code ==
                ElevatedLoaderMutationErrorCode::GameRunning &&
            running_platform.game_validations == 0U &&
            fs::is_empty(running.root / "game"),
        "running-game broker preflight reached validation or mutation");

    TemporaryTree malformed{};
    auto malformed_request = request(
        ElevatedLoaderOperation::Apply,
        material,
        malformed,
        install(std::nullopt, measurement("pinned-proxy")),
        ignore());
    malformed_request.request_nonce = "invalid";
    const auto malformed_result = execute_elevated_loader_mutation(
        malformed_request,
        material.manifest_sha256,
        &material.material,
        platform);
    passed &= expect(
        !malformed_result &&
            malformed_result.error().code ==
                ElevatedLoaderMutationErrorCode::InvalidRequest &&
            fs::is_empty(malformed.root / "game"),
        "malformed privileged request reached mutation");

    if (passed)
    {
        std::cout << "PASS elevated_loader_win32\n";
        return 0;
    }
    return 1;
}
