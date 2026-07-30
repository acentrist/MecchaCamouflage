#pragma once

#include <meccha/launcher/elevated_loader.hpp>
#include <meccha/launcher/execution_win32.hpp>

#include <expected>
#include <string>

namespace meccha::launcher
{
#ifdef _WIN32
class ElevatedLoaderMutationClient
{
public:
    ElevatedLoaderMutationClient() = default;
    ElevatedLoaderMutationClient(
        const ElevatedLoaderMutationClient&) = delete;
    auto operator=(const ElevatedLoaderMutationClient&)
        -> ElevatedLoaderMutationClient& = delete;
    ElevatedLoaderMutationClient(
        ElevatedLoaderMutationClient&&) = delete;
    auto operator=(ElevatedLoaderMutationClient&&)
        -> ElevatedLoaderMutationClient& = delete;
    virtual ~ElevatedLoaderMutationClient() = default;

    virtual auto execute(
        const ElevatedLoaderMutationRequest& request)
        -> std::expected<
            ElevatedLoaderMutationResult,
            ElevatedLoaderMutationError> = 0;
};

class ElevatedBrokerNonceSource
{
public:
    ElevatedBrokerNonceSource() = default;
    ElevatedBrokerNonceSource(
        const ElevatedBrokerNonceSource&) = delete;
    auto operator=(const ElevatedBrokerNonceSource&)
        -> ElevatedBrokerNonceSource& = delete;
    ElevatedBrokerNonceSource(
        ElevatedBrokerNonceSource&&) = delete;
    auto operator=(ElevatedBrokerNonceSource&&)
        -> ElevatedBrokerNonceSource& = delete;
    virtual ~ElevatedBrokerNonceSource() = default;

    virtual auto next_nonce()
        -> std::expected<
            std::string,
            LauncherEffectError> = 0;
};

class Win32ElevatedBrokerNonceSource final
    : public ElevatedBrokerNonceSource
{
public:
    auto next_nonce()
        -> std::expected<
            std::string,
            LauncherEffectError> override;
};

class Win32OriginalUserElevatedLoaderBroker final
    : public ElevatedLoaderBroker
{
public:
    Win32OriginalUserElevatedLoaderBroker(
        Sha256Digest accepted_manifest_sha256,
        ElevatedBrokerNonceSource& nonce_source,
        ElevatedLoaderMutationClient& client);

    auto apply(
        const ManagedLoaderPlan& plan,
        const std::filesystem::path& game_directory,
        const std::filesystem::path& ownership_directory,
        const ManagedLoaderMaterial& material)
        -> std::expected<void, LauncherEffectError> override;

    auto remove(
        const RemovalPlan& plan,
        const std::filesystem::path& game_directory,
        const std::filesystem::path& ownership_directory)
        -> std::expected<void, LauncherEffectError> override;

private:
    Sha256Digest accepted_manifest_sha256_{};
    ElevatedBrokerNonceSource& nonce_source_;
    ElevatedLoaderMutationClient& client_;
};
#endif
} // namespace meccha::launcher
