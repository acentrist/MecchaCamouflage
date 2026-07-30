#include <meccha/launcher/elevated_transport.hpp>
#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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
        std::cerr << "FAIL elevated_transport_win32: "
                  << message << '\n';
    }
    return condition;
}

auto digest(std::span<const std::byte> bytes) -> Sha256Digest
{
    return sha256_bytes(bytes).value();
}

auto bytes(std::string_view value) -> std::vector<std::byte>
{
    const auto source = std::as_bytes(std::span{value});
    return {source.begin(), source.end()};
}

auto measurement(std::span<const std::byte> value)
    -> FileMeasurement
{
    return FileMeasurement{value.size(), digest(value)};
}

auto sync_read(HANDLE pipe, std::span<std::byte> output) -> bool
{
    std::size_t offset{};
    while (offset < output.size())
    {
        DWORD transferred{};
        if (!ReadFile(
                pipe,
                output.data() + offset,
                static_cast<DWORD>(output.size() - offset),
                &transferred,
                nullptr) ||
            transferred == 0U)
        {
            return false;
        }
        offset += transferred;
    }
    return true;
}

auto sync_write(
    HANDLE pipe,
    std::span<const std::byte> input) -> bool
{
    std::size_t offset{};
    while (offset < input.size())
    {
        DWORD transferred{};
        if (!WriteFile(
                pipe,
                input.data() + offset,
                static_cast<DWORD>(input.size() - offset),
                &transferred,
                nullptr) ||
            transferred == 0U)
        {
            return false;
        }
        offset += transferred;
    }
    return true;
}

auto run_wrong_nonce_child(
    const ElevatedBrokerChildInvocation& invocation)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>
{
    auto pipe_name =
        std::wstring{
            LR"(\\.\pipe\MecchaCamouflage-v2-elevated-)"};
    for (const auto character : invocation.request_nonce)
    {
        pipe_name.push_back(
            static_cast<wchar_t>(character));
    }
    const auto pipe = CreateFileW(
        pipe_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0U,
        nullptr,
        OPEN_EXISTING,
        0U,
        nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(ElevatedLoaderMutationError{
            ElevatedLoaderMutationErrorCode::Io,
            "test pipe connect failed",
        });
    }
    auto prefix = std::array<std::byte, 4>{};
    auto ok = sync_read(pipe, prefix);
    const auto request_size =
        std::to_integer<std::uint32_t>(prefix[0]) |
        (std::to_integer<std::uint32_t>(prefix[1]) << 8U) |
        (std::to_integer<std::uint32_t>(prefix[2]) << 16U) |
        (std::to_integer<std::uint32_t>(prefix[3]) << 24U);
    auto request = std::vector<std::byte>(request_size);
    ok &= request_size != 0U &&
          request_size <= 64U * 1024U &&
          sync_read(pipe, request);
    const auto response = encode_elevated_loader_response(
        ElevatedLoaderMutationResponse{
            "ffffffffffffffffffffffffffffffff",
            ElevatedLoaderMutationResult{},
        });
    if (response)
    {
        const auto response_size =
            static_cast<std::uint32_t>(response->size());
        const std::array response_prefix{
            static_cast<std::byte>(response_size),
            static_cast<std::byte>(response_size >> 8U),
            static_cast<std::byte>(response_size >> 16U),
            static_cast<std::byte>(response_size >> 24U),
        };
        ok &= sync_write(pipe, response_prefix) &&
              sync_write(pipe, *response);
    }
    else
    {
        ok = false;
    }
    static_cast<void>(CloseHandle(pipe));
    if (!ok)
    {
        return std::unexpected(ElevatedLoaderMutationError{
            ElevatedLoaderMutationErrorCode::Io,
            "test pipe exchange failed",
        });
    }
    return ElevatedBrokerParentIdentity{
        invocation.parent_process_id,
        7U,
        L"S-1-5-21-test",
    };
}

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

class TestPeerValidator final : public ElevatedBrokerPeerValidator
{
public:
    auto validate_original_parent(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
        -> std::expected<
            ElevatedBrokerParentIdentity,
            ElevatedLoaderMutationError> override
    {
        if (expected_process_id != actual_process_id)
        {
            return std::unexpected(ElevatedLoaderMutationError{
                ElevatedLoaderMutationErrorCode::InvalidRequest,
                "parent PID mismatch",
            });
        }
        return ElevatedBrokerParentIdentity{
            actual_process_id,
            7U,
            L"S-1-5-21-test",
        };
    }

    auto validate_elevated_child(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
        -> std::expected<
            void,
            ElevatedLoaderMutationError> override
    {
        if (expected_process_id != actual_process_id)
        {
            return std::unexpected(ElevatedLoaderMutationError{
                ElevatedLoaderMutationErrorCode::InvalidRequest,
                "child PID mismatch",
            });
        }
        return {};
    }
};

class ThreadChildProcess final : public ElevatedBrokerChildProcess
{
public:
    ThreadChildProcess(
        HANDLE completed,
        std::jthread thread,
        std::uint32_t process_id)
        : completed_(completed),
          thread_(std::move(thread)),
          process_id_(process_id)
    {
    }

    ~ThreadChildProcess() override
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
        if (completed_ != nullptr)
        {
            static_cast<void>(CloseHandle(completed_));
        }
    }

    auto process_id() const -> std::uint32_t override
    {
        return process_id_;
    }

    auto native_wait_handle() const -> std::uintptr_t override
    {
        return reinterpret_cast<std::uintptr_t>(completed_);
    }

private:
    HANDLE completed_{};
    std::jthread thread_{};
    std::uint32_t process_id_{};
};

class InProcessChildLauncher final
    : public ElevatedBrokerChildLauncher
{
public:
    InProcessChildLauncher(
        Sha256Digest accepted_manifest,
        const ManagedLoaderMaterial* material,
        ElevatedLoaderMutationPlatform& mutation_platform,
        ElevatedBrokerPeerValidator& peer_validator,
        std::uint32_t reported_process_id =
            GetCurrentProcessId(),
        bool wrong_nonce_response = false)
        : accepted_manifest_(accepted_manifest),
          material_(material),
          mutation_platform_(mutation_platform),
          peer_validator_(peer_validator),
          reported_process_id_(reported_process_id),
          wrong_nonce_response_(wrong_nonce_response)
    {
    }

    auto launch(const ElevatedBrokerChildLaunchRequest& request)
        -> std::expected<
            std::unique_ptr<ElevatedBrokerChildProcess>,
            ElevatedLoaderMutationError> override
    {
        auto* completed = CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            nullptr);
        if (completed == nullptr)
        {
            return std::unexpected(ElevatedLoaderMutationError{
                ElevatedLoaderMutationErrorCode::Io,
                "CreateEventW failed",
            });
        }
        auto thread = std::jthread{
            [this, invocation = request.invocation, completed]
            {
                child_result = wrong_nonce_response_
                                   ? run_wrong_nonce_child(
                                         invocation)
                                   : run_elevated_broker_child(
                                         invocation,
                                         accepted_manifest_,
                                         material_,
                                         mutation_platform_,
                                         peer_validator_);
                static_cast<void>(SetEvent(completed));
            }};
        return std::unique_ptr<ElevatedBrokerChildProcess>{
            std::make_unique<ThreadChildProcess>(
                completed,
                std::move(thread),
                reported_process_id_)};
    }

    std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError> child_result =
        std::unexpected(ElevatedLoaderMutationError{
            ElevatedLoaderMutationErrorCode::Io,
            "child did not run",
        });

private:
    Sha256Digest accepted_manifest_{};
    const ManagedLoaderMaterial* material_{};
    ElevatedLoaderMutationPlatform& mutation_platform_;
    ElevatedBrokerPeerValidator& peer_validator_;
    std::uint32_t reported_process_id_{};
    bool wrong_nonce_response_{};
};

auto make_material(
    const Sha256Digest& manifest,
    const std::vector<std::byte>& proxy,
    const std::vector<std::byte>& override_file)
    -> ManagedLoaderMaterial
{
    return ManagedLoaderMaterial{
        OwnedFileExpectation{
            "2.0.0",
            manifest,
            ManifestFile{
                "dwmapi.dll",
                FileRole::Proxy,
                proxy.size(),
                digest(proxy),
            },
        },
        proxy,
        OwnedFileExpectation{
            "2.0.0",
            manifest,
            ManifestFile{
                "override.txt",
                FileRole::Override,
                override_file.size(),
                digest(override_file),
            },
        },
        override_file,
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::launcher;

    auto passed = true;
    const auto root =
        fs::temp_directory_path() /
        ("meccha-elevated-transport-" +
         std::to_string(GetCurrentProcessId()));
    std::error_code cleanup_error{};
    fs::remove_all(root, cleanup_error);
    fs::create_directories(root);

    const auto manifest = digest(
        std::as_bytes(std::span{"manifest", 8U}));
    const auto proxy = bytes("trusted proxy");
    const auto override_file = bytes("trusted override");
    const auto material = make_material(
        manifest,
        proxy,
        override_file);
    auto mutation_platform = FakeMutationPlatform{root};
    auto peer_validator = TestPeerValidator{};
    auto child_launcher = InProcessChildLauncher{
        manifest,
        &material,
        mutation_platform,
        peer_validator,
    };
    auto client = Win32NamedPipeElevatedLoaderMutationClient{
        child_launcher,
        peer_validator,
    };

    const auto result = client.execute(
        ElevatedLoaderMutationRequest{
            ElevatedLoaderMutationSchemaVersion,
            ElevatedLoaderOperation::Apply,
            manifest,
            "0123456789abcdef0123456789abcdef",
            root,
            ElevatedLoaderFileMutation{
                ElevatedLoaderFileAction::Install,
                std::nullopt,
                measurement(proxy),
            },
            ElevatedLoaderFileMutation{
                ElevatedLoaderFileAction::Install,
                std::nullopt,
                measurement(override_file),
            },
        });
    passed &= expect(
        result &&
            *result ==
                ElevatedLoaderMutationResult{true, true},
        "authenticated pipe request did not execute");
    passed &= expect(
        child_launcher.child_result &&
            child_launcher.child_result->process_id ==
                GetCurrentProcessId() &&
            child_launcher.child_result->session_id == 7U &&
            child_launcher.child_result->user_sid ==
                L"S-1-5-21-test",
        "child did not retain the validated original-user identity");
    passed &= expect(
        fs::exists(root / "dwmapi.dll") &&
            fs::exists(root / "override.txt"),
        "restricted child mutation was not published");

    auto mismatched_launcher = InProcessChildLauncher{
        manifest,
        &material,
        mutation_platform,
        peer_validator,
        GetCurrentProcessId() + 1U,
    };
    auto mismatched_client =
        Win32NamedPipeElevatedLoaderMutationClient{
            mismatched_launcher,
            peer_validator,
        };
    auto mismatched_request = ElevatedLoaderMutationRequest{
        ElevatedLoaderMutationSchemaVersion,
        ElevatedLoaderOperation::Apply,
        manifest,
        "1123456789abcdef0123456789abcdef",
        root,
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Verify,
            measurement(proxy),
            measurement(proxy),
        },
        ElevatedLoaderFileMutation{
            ElevatedLoaderFileAction::Verify,
            measurement(override_file),
            measurement(override_file),
        },
    };
    passed &= expect(
        !mismatched_client.execute(mismatched_request),
        "a named-pipe client outside the launched PID was accepted");

    auto wrong_nonce_launcher = InProcessChildLauncher{
        manifest,
        &material,
        mutation_platform,
        peer_validator,
        GetCurrentProcessId(),
        true,
    };
    auto wrong_nonce_client =
        Win32NamedPipeElevatedLoaderMutationClient{
            wrong_nonce_launcher,
            peer_validator,
        };
    mismatched_request.request_nonce =
        "2123456789abcdef0123456789abcdef";
    passed &= expect(
        !wrong_nonce_client.execute(mismatched_request),
        "a response with a different nonce was accepted");

    const std::array internal_arguments{
        std::wstring_view{
            L"--meccha-internal-elevated-broker-v1"},
        std::wstring_view{
            L"0123456789abcdef0123456789abcdef"},
        std::wstring_view{L"4242"},
    };
    const auto internal =
        parse_elevated_broker_child_invocation(
            internal_arguments);
    const std::array public_arguments{
        std::wstring_view{L"--prepare-only"},
    };
    passed &= expect(
        internal && *internal ==
                        ElevatedBrokerChildInvocation{
                            "0123456789abcdef0123456789abcdef",
                            4242U,
                        },
        "strict internal child invocation was not parsed");
    passed &= expect(
        !is_elevated_broker_child_invocation(public_arguments) &&
            !parse_elevated_broker_child_invocation(
                std::array{
                    internal_arguments[0],
                    internal_arguments[1],
                    std::wstring_view{L"0"},
                }) &&
            !parse_elevated_broker_child_invocation(
                std::array{
                    internal_arguments[0],
                    std::wstring_view{
                        L"0123456789ABCDEF0123456789ABCDEF"},
                    internal_arguments[2],
                }) &&
            !parse_elevated_broker_child_invocation(
                std::array{
                    internal_arguments[0],
                    internal_arguments[1],
                    internal_arguments[2],
                    std::wstring_view{L"extra"},
                }),
        "public or malformed arguments entered internal mode");
    const auto internal_parameters =
        format_elevated_broker_child_parameters(*internal);
    passed &= expect(
        internal_parameters &&
            *internal_parameters ==
                L"--meccha-internal-elevated-broker-v1 "
                L"0123456789abcdef0123456789abcdef 4242",
        "runas child parameters were not canonical");

    auto native_validator =
        Win32ElevatedBrokerPeerValidator{};
    const auto current_process_id =
        GetCurrentProcessId();
    const auto native_parent =
        native_validator.validate_original_parent(
            current_process_id,
            current_process_id);
    passed &= expect(
        native_parent &&
            native_parent->process_id == current_process_id &&
            !native_parent->user_sid.empty() &&
            !native_validator.validate_original_parent(
                current_process_id,
                current_process_id + 1U) &&
            !native_validator.validate_elevated_child(
                current_process_id,
                current_process_id + 1U),
        "native peer identity validation did not bind exact PIDs");

    auto nonce_source = Win32ElevatedBrokerNonceSource{};
    const auto first_nonce = nonce_source.next_nonce();
    const auto second_nonce = nonce_source.next_nonce();
    passed &= expect(
        first_nonce && second_nonce &&
            first_nonce->size() == 32U &&
            second_nonce->size() == 32U &&
            *first_nonce != *second_nonce,
        "production elevated broker nonces were not private and unique");

    fs::remove_all(root, cleanup_error);
    if (passed)
    {
        std::cout << "PASS elevated_transport_win32\n";
        return 0;
    }
    return 1;
}
