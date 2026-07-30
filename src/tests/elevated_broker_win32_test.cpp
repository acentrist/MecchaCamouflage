#include <meccha/launcher/elevated_broker.hpp>
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
        std::cerr << "FAIL elevated_broker_win32: "
                  << message << '\n';
    }
    return condition;
}

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto view = std::as_bytes(std::span{value});
    return {view.begin(), view.end()};
}

auto write_text(const fs::path& path, std::string_view value) -> void
{
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

class TemporaryTree
{
public:
    TemporaryTree()
    {
        static std::uint64_t sequence{};
        ++sequence;
        root = fs::temp_directory_path() /
               ("meccha-v2-elevated-broker-" +
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

struct MaterialFixture
{
    Sha256Digest manifest_sha256{};
    ManagedLoaderMaterial material{};
};

auto make_material() -> MaterialFixture
{
    const auto manifest_sha256 =
        sha256_bytes(bytes("manifest")).value();
    const auto proxy = bytes("pinned-proxy");
    const auto override_file =
        bytes("C:\\Users\\original\\runtime\\active");
    return MaterialFixture{
        manifest_sha256,
        ManagedLoaderMaterial{
            OwnedFileExpectation{
                "2.0.0",
                manifest_sha256,
                ManifestFile{
                    "dwmapi.dll",
                    FileRole::Proxy,
                    proxy.size(),
                    sha256_bytes(proxy).value(),
                },
            },
            proxy,
            OwnedFileExpectation{
                "2.0.0",
                manifest_sha256,
                ManifestFile{
                    "override.txt",
                    FileRole::Override,
                    override_file.size(),
                    sha256_bytes(override_file).value(),
                },
            },
            override_file,
        },
    };
}

class FixedNonceSource final : public ElevatedBrokerNonceSource
{
public:
    auto next_nonce()
        -> std::expected<
            std::string,
            LauncherEffectError> override
    {
        ++calls;
        return "0123456789abcdef0123456789abcdef";
    }

    std::size_t calls{};
};

class FakeMutationPlatform final
    : public ElevatedLoaderMutationPlatform
{
public:
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
        return GameInstallation{
            {},
            path,
            path,
            path / "PenguinHotel-Win64-Shipping.exe",
        };
    }
};

class InProcessClient final : public ElevatedLoaderMutationClient
{
public:
    InProcessClient(
        const MaterialFixture& material,
        ElevatedLoaderMutationPlatform& platform)
        : material_{material}, platform_{platform}
    {
    }

    auto execute(const ElevatedLoaderMutationRequest& request)
        -> std::expected<
            ElevatedLoaderMutationResult,
            ElevatedLoaderMutationError> override
    {
        ++calls;
        last_request = request;
        return execute_elevated_loader_mutation(
            request,
            material_.manifest_sha256,
            request.operation == ElevatedLoaderOperation::Apply
                ? &material_.material
                : nullptr,
            platform_);
    }

    const MaterialFixture& material_;
    ElevatedLoaderMutationPlatform& platform_;
    std::size_t calls{};
    std::optional<ElevatedLoaderMutationRequest> last_request{};
};

enum class FailureMode : std::uint8_t
{
    BeforeMutation,
    AfterProxy,
    FalseSuccess,
};

class FailingClient final : public ElevatedLoaderMutationClient
{
public:
    explicit FailingClient(FailureMode mode) : mode_{mode}
    {
    }

    auto execute(const ElevatedLoaderMutationRequest& request)
        -> std::expected<
            ElevatedLoaderMutationResult,
            ElevatedLoaderMutationError> override
    {
        ++calls;
        if (mode_ == FailureMode::AfterProxy)
        {
            write_text(
                request.game_directory / "dwmapi.dll",
                "pinned-proxy");
        }
        if (mode_ == FailureMode::FalseSuccess)
        {
            return ElevatedLoaderMutationResult{true, true};
        }
        return std::unexpected(ElevatedLoaderMutationError{
            ElevatedLoaderMutationErrorCode::Io,
            "injected client failure",
        });
    }

    FailureMode mode_{};
    std::size_t calls{};
};

auto observe(
    const TemporaryTree& tree,
    const MaterialFixture& material)
    -> std::expected<
        ManagedLoaderObservation,
        ManagedLoaderError>
{
    return observe_managed_loader(
        tree.root / "game",
        tree.root / "ownership",
        ManagedLoaderExpectations{
            material.material.proxy,
            material.material.override_file,
        });
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    const auto material = make_material();
    FakeMutationPlatform mutation_platform{};
    FixedNonceSource nonce{};

    TemporaryTree lifecycle{};
    InProcessClient client{material, mutation_platform};
    Win32OriginalUserElevatedLoaderBroker broker{
        material.manifest_sha256,
        nonce,
        client,
    };
    const auto applied = broker.apply(
        ManagedLoaderPlan{
            ArtifactDisposition::CreateOwned,
            ArtifactDisposition::CreateOwned,
            true,
        },
        lifecycle.root / "game",
        lifecycle.root / "ownership",
        material.material);
    const auto applied_state = observe(lifecycle, material);
    passed &= expect(
        applied &&
            applied_state &&
            applied_state->proxy == ArtifactState::ExactOwned &&
            applied_state->override_file ==
                ArtifactState::ExactOwned &&
            client.calls == 1U &&
            nonce.calls == 1U &&
            client.last_request &&
            client.last_request->proxy.action ==
                ElevatedLoaderFileAction::Install &&
            client.last_request->override_file.action ==
                ElevatedLoaderFileAction::Install,
        "original-user apply did not coordinate intents and mutation");

    const auto removed = broker.remove(
        RemovalPlan{
            RemovalAction::None,
            RemovalAction::RemoveOwned,
            RemovalAction::RemoveOwned,
            RemovalAction::None,
            true,
        },
        lifecycle.root / "game",
        lifecycle.root / "ownership");
    const auto removed_state = observe(lifecycle, material);
    passed &= expect(
        removed &&
            removed_state &&
            removed_state->proxy == ArtifactState::Missing &&
            removed_state->override_file ==
                ArtifactState::Missing &&
            fs::is_empty(lifecycle.root / "ownership") &&
            client.calls == 2U &&
            nonce.calls == 2U,
        "original-user removal did not finalize target and receipts");

    TemporaryTree failed{};
    FailingClient failed_client{FailureMode::BeforeMutation};
    FixedNonceSource failed_nonce{};
    Win32OriginalUserElevatedLoaderBroker failed_broker{
        material.manifest_sha256,
        failed_nonce,
        failed_client,
    };
    const auto failed_apply = failed_broker.apply(
        ManagedLoaderPlan{
            ArtifactDisposition::CreateOwned,
            ArtifactDisposition::CreateOwned,
            true,
        },
        failed.root / "game",
        failed.root / "ownership",
        material.material);
    const auto failed_state = observe(failed, material);
    passed &= expect(
        !failed_apply &&
            failed_state &&
            failed_state->proxy == ArtifactState::Missing &&
            failed_state->override_file ==
                ArtifactState::Missing &&
            !fs::exists(failed.root / "ownership" /
                        "dwmapi.owner.json") &&
            !fs::exists(failed.root / "ownership" /
                        "override.owner.json"),
        "failed client left unperformed receipt intents active");

    TemporaryTree partial{};
    FailingClient partial_client{FailureMode::AfterProxy};
    FixedNonceSource partial_nonce{};
    Win32OriginalUserElevatedLoaderBroker partial_broker{
        material.manifest_sha256,
        partial_nonce,
        partial_client,
    };
    const auto partial_apply = partial_broker.apply(
        ManagedLoaderPlan{
            ArtifactDisposition::CreateOwned,
            ArtifactDisposition::CreateOwned,
            true,
        },
        partial.root / "game",
        partial.root / "ownership",
        material.material);
    const auto partial_state = observe(partial, material);
    passed &= expect(
        !partial_apply &&
            partial_state &&
            partial_state->proxy == ArtifactState::ExactOwned &&
            partial_state->override_file == ArtifactState::Missing,
        "partial privileged success was not recovered deterministically");

    TemporaryTree false_success{};
    FailingClient false_client{FailureMode::FalseSuccess};
    FixedNonceSource false_nonce{};
    Win32OriginalUserElevatedLoaderBroker false_broker{
        material.manifest_sha256,
        false_nonce,
        false_client,
    };
    const auto false_apply = false_broker.apply(
        ManagedLoaderPlan{
            ArtifactDisposition::CreateOwned,
            ArtifactDisposition::CreateOwned,
            true,
        },
        false_success.root / "game",
        false_success.root / "ownership",
        material.material);
    const auto false_state = observe(false_success, material);
    passed &= expect(
        !false_apply &&
            false_state &&
            false_state->proxy == ArtifactState::Missing &&
            false_state->override_file == ArtifactState::Missing,
        "false client success was accepted without target verification");

    TemporaryTree reuse{};
    const auto locally_installed = apply_managed_loader_plan(
        ManagedLoaderPlan{
            ArtifactDisposition::CreateOwned,
            ArtifactDisposition::CreateOwned,
            false,
        },
        reuse.root / "game",
        reuse.root / "ownership",
        material.material);
    FailingClient reuse_client{FailureMode::BeforeMutation};
    FixedNonceSource reuse_nonce{};
    Win32OriginalUserElevatedLoaderBroker reuse_broker{
        material.manifest_sha256,
        reuse_nonce,
        reuse_client,
    };
    const auto reused = reuse_broker.apply(
        ManagedLoaderPlan{
            ArtifactDisposition::ReuseOwned,
            ArtifactDisposition::ReuseOwned,
            true,
        },
        reuse.root / "game",
        reuse.root / "ownership",
        material.material);
    passed &= expect(
        locally_installed &&
            reused &&
            reuse_client.calls == 0U &&
            reuse_nonce.calls == 0U,
        "exact reuse requested an unnecessary privileged client");

    if (passed)
    {
        std::cout << "PASS elevated_broker_win32\n";
        return 0;
    }
    return 1;
}
