#pragma once

#include <meccha/launcher/elevated_loader.hpp>
#include <meccha/launcher/execution_win32.hpp>

#include <expected>
#include <memory>
#include <optional>
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

class ElevatedLoaderBrokerProvider
{
public:
    ElevatedLoaderBrokerProvider() = default;
    ElevatedLoaderBrokerProvider(
        const ElevatedLoaderBrokerProvider&) = delete;
    auto operator=(const ElevatedLoaderBrokerProvider&)
        -> ElevatedLoaderBrokerProvider& = delete;
    ElevatedLoaderBrokerProvider(
        ElevatedLoaderBrokerProvider&&) = delete;
    auto operator=(ElevatedLoaderBrokerProvider&&)
        -> ElevatedLoaderBrokerProvider& = delete;
    virtual ~ElevatedLoaderBrokerProvider() = default;

    virtual auto bind(
        const Sha256Digest& accepted_manifest_sha256)
        -> std::expected<
            ElevatedLoaderBroker*,
            LauncherEffectError> = 0;
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

class Win32ElevatedLoaderBrokerProvider final
    : public ElevatedLoaderBrokerProvider
{
public:
    Win32ElevatedLoaderBrokerProvider(
        ElevatedBrokerNonceSource& nonce_source,
        ElevatedLoaderMutationClient& client);

    auto bind(
        const Sha256Digest& accepted_manifest_sha256)
        -> std::expected<
            ElevatedLoaderBroker*,
            LauncherEffectError> override;

private:
    ElevatedBrokerNonceSource& nonce_source_;
    ElevatedLoaderMutationClient& client_;
    std::unique_ptr<
        Win32OriginalUserElevatedLoaderBroker> broker_{};
    std::optional<Sha256Digest> bound_manifest_sha256_{};
};
#endif
} // namespace meccha::launcher
