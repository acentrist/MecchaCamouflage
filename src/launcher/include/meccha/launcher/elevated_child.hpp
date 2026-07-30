#pragma once

#include <meccha/launcher/embedded_package.hpp>
#include <meccha/launcher/elevated_transport.hpp>

#include <expected>
#include <filesystem>

namespace meccha::launcher
{
#ifdef _WIN32
class ElevatedBrokerChildEnvironment
{
public:
    ElevatedBrokerChildEnvironment() = default;
    ElevatedBrokerChildEnvironment(
        const ElevatedBrokerChildEnvironment&) = delete;
    auto operator=(const ElevatedBrokerChildEnvironment&)
        -> ElevatedBrokerChildEnvironment& = delete;
    ElevatedBrokerChildEnvironment(
        ElevatedBrokerChildEnvironment&&) = delete;
    auto operator=(ElevatedBrokerChildEnvironment&&)
        -> ElevatedBrokerChildEnvironment& = delete;
    virtual ~ElevatedBrokerChildEnvironment() = default;

    virtual auto temporary_directory()
        -> std::expected<
            std::filesystem::path,
            ElevatedLoaderMutationError> = 0;

    virtual auto original_user_local_app_data(
        const ElevatedBrokerParentIdentity& parent)
        -> std::expected<
            std::filesystem::path,
            ElevatedLoaderMutationError> = 0;
};

class Win32ElevatedBrokerChildEnvironment final
    : public ElevatedBrokerChildEnvironment
{
public:
    auto temporary_directory()
        -> std::expected<
            std::filesystem::path,
            ElevatedLoaderMutationError> override;

    auto original_user_local_app_data(
        const ElevatedBrokerParentIdentity& parent)
        -> std::expected<
            std::filesystem::path,
            ElevatedLoaderMutationError> override;
};

class Win32EmbeddedElevatedLoaderRequestExecutor final
    : public ElevatedBrokerRequestExecutor
{
public:
    Win32EmbeddedElevatedLoaderRequestExecutor(
        LauncherPackageSource& package_source,
        ElevatedBrokerChildEnvironment& environment,
        ElevatedLoaderMutationPlatform& mutation_platform);

    auto execute(
        const ElevatedLoaderMutationRequest& request,
        const ElevatedBrokerParentIdentity& parent_identity)
        -> std::expected<
            ElevatedLoaderMutationResult,
            ElevatedLoaderMutationError> override;

private:
    LauncherPackageSource& package_source_;
    ElevatedBrokerChildEnvironment& environment_;
    ElevatedLoaderMutationPlatform& mutation_platform_;
};

[[nodiscard]] auto run_embedded_elevated_broker_child(
    const ElevatedBrokerChildInvocation& invocation,
    LauncherPackageSource& package_source,
    ElevatedBrokerChildEnvironment& environment,
    ElevatedLoaderMutationPlatform& mutation_platform,
    ElevatedBrokerPeerValidator& peer_validator)
    -> std::expected<
        ElevatedBrokerParentIdentity,
        ElevatedLoaderMutationError>;
#endif
} // namespace meccha::launcher
