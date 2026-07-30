#pragma once

#include <meccha/launcher/elevated_broker.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace meccha::launcher
{
#ifdef _WIN32
struct ElevatedBrokerParentIdentity
{
    std::uint32_t process_id{};
    std::uint32_t session_id{};
    std::wstring user_sid{};

    auto operator==(const ElevatedBrokerParentIdentity&) const
        -> bool = default;
};

struct ElevatedBrokerChildInvocation
{
    std::string request_nonce{};
    std::uint32_t parent_process_id{};

    auto operator==(const ElevatedBrokerChildInvocation&) const
        -> bool = default;
};

struct ElevatedBrokerChildLaunchRequest
{
    ElevatedBrokerChildInvocation invocation{};
    std::filesystem::path game_directory{};
    ElevatedLoaderOperation operation{};
    ElevatedLoaderFileAction proxy_action{};
    ElevatedLoaderFileAction override_action{};
};

class ElevatedBrokerChildProcess
{
public:
    ElevatedBrokerChildProcess() = default;
    ElevatedBrokerChildProcess(
        const ElevatedBrokerChildProcess&) = delete;
    auto operator=(const ElevatedBrokerChildProcess&)
        -> ElevatedBrokerChildProcess& = delete;
    ElevatedBrokerChildProcess(
        ElevatedBrokerChildProcess&&) = delete;
    auto operator=(ElevatedBrokerChildProcess&&)
        -> ElevatedBrokerChildProcess& = delete;
    virtual ~ElevatedBrokerChildProcess() = default;

    [[nodiscard]] virtual auto process_id() const
        -> std::uint32_t = 0;

    [[nodiscard]] virtual auto native_wait_handle() const
        -> std::uintptr_t = 0;
};

class ElevatedBrokerChildLauncher
{
public:
    ElevatedBrokerChildLauncher() = default;
    ElevatedBrokerChildLauncher(
        const ElevatedBrokerChildLauncher&) = delete;
    auto operator=(const ElevatedBrokerChildLauncher&)
        -> ElevatedBrokerChildLauncher& = delete;
    ElevatedBrokerChildLauncher(
        ElevatedBrokerChildLauncher&&) = delete;
    auto operator=(ElevatedBrokerChildLauncher&&)
        -> ElevatedBrokerChildLauncher& = delete;
    virtual ~ElevatedBrokerChildLauncher() = default;

    virtual auto launch(
        const ElevatedBrokerChildLaunchRequest& request)
        -> std::expected<
            std::unique_ptr<ElevatedBrokerChildProcess>,
            ElevatedLoaderMutationError> = 0;
};

class Win32RunAsElevatedBrokerChildLauncher final
    : public ElevatedBrokerChildLauncher
{
public:
    auto launch(
        const ElevatedBrokerChildLaunchRequest& request)
        -> std::expected<
            std::unique_ptr<ElevatedBrokerChildProcess>,
            ElevatedLoaderMutationError> override;
};

class ElevatedBrokerPeerValidator
{
public:
    ElevatedBrokerPeerValidator() = default;
    ElevatedBrokerPeerValidator(
        const ElevatedBrokerPeerValidator&) = delete;
    auto operator=(const ElevatedBrokerPeerValidator&)
        -> ElevatedBrokerPeerValidator& = delete;
    ElevatedBrokerPeerValidator(
        ElevatedBrokerPeerValidator&&) = delete;
    auto operator=(ElevatedBrokerPeerValidator&&)
        -> ElevatedBrokerPeerValidator& = delete;
    virtual ~ElevatedBrokerPeerValidator() = default;

    virtual auto validate_original_parent(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
        -> std::expected<
            ElevatedBrokerParentIdentity,
            ElevatedLoaderMutationError> = 0;

    virtual auto validate_elevated_child(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
        -> std::expected<
            void,
            ElevatedLoaderMutationError> = 0;
};

class ElevatedBrokerRequestExecutor
{
public:
    ElevatedBrokerRequestExecutor() = default;
    ElevatedBrokerRequestExecutor(
        const ElevatedBrokerRequestExecutor&) = delete;
    auto operator=(const ElevatedBrokerRequestExecutor&)
        -> ElevatedBrokerRequestExecutor& = delete;
    ElevatedBrokerRequestExecutor(
        ElevatedBrokerRequestExecutor&&) = delete;
    auto operator=(ElevatedBrokerRequestExecutor&&)
        -> ElevatedBrokerRequestExecutor& = delete;
    virtual ~ElevatedBrokerRequestExecutor() = default;

    virtual auto execute(
        const ElevatedLoaderMutationRequest& request,
        const ElevatedBrokerParentIdentity& parent_identity)
        -> std::expected<
            ElevatedLoaderMutationResult,
            ElevatedLoaderMutationError> = 0;
};

class Win32ElevatedBrokerPeerValidator final
    : public ElevatedBrokerPeerValidator
{
public:
    auto validate_original_parent(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
        -> std::expected<
            ElevatedBrokerParentIdentity,
            ElevatedLoaderMutationError> override;

    auto validate_elevated_child(
        std::uint32_t expected_process_id,
        std::uint32_t actual_process_id)
        -> std::expected<
            void,
            ElevatedLoaderMutationError> override;
};

class Win32NamedPipeElevatedLoaderMutationClient final
    : public ElevatedLoaderMutationClient
{
public:
    Win32NamedPipeElevatedLoaderMutationClient(
        ElevatedBrokerChildLauncher& child_launcher,
        ElevatedBrokerPeerValidator& peer_validator);

    auto execute(
        const ElevatedLoaderMutationRequest& request)
        -> std::expected<
            ElevatedLoaderMutationResult,
            ElevatedLoaderMutationError> override;

private:
    ElevatedBrokerChildLauncher& child_launcher_;
    ElevatedBrokerPeerValidator& peer_validator_;
};

[[nodiscard]] auto is_elevated_broker_child_invocation(
    std::span<const std::wstring_view> arguments) -> bool;

[[nodiscard]] auto parse_elevated_broker_child_invocation(
    std::span<const std::wstring_view> arguments)
    -> std::expected<
        ElevatedBrokerChildInvocation,
        ElevatedLoaderMutationError>;

[[nodiscard]] auto format_elevated_broker_child_parameters(
    const ElevatedBrokerChildInvocation& invocation)
    -> std::expected<
        std::wstring,
        ElevatedLoaderMutationError>;

[[nodiscard]] auto run_elevated_broker_child(
    const ElevatedBrokerChildInvocation& invocation,
    ElevatedBrokerRequestExecutor& request_executor,
    ElevatedBrokerPeerValidator& peer_validator)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>;

[[nodiscard]] auto run_elevated_broker_child(
    const ElevatedBrokerChildInvocation& invocation,
    const Sha256Digest& accepted_manifest_sha256,
    const ManagedLoaderMaterial* material,
    ElevatedLoaderMutationPlatform& mutation_platform,
    ElevatedBrokerPeerValidator& peer_validator)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>;
#endif
} // namespace meccha::launcher
