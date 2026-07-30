#pragma once

#include <meccha/launcher/game_discovery.hpp>
#include <meccha/launcher/managed_loader.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace meccha::launcher
{
#ifdef _WIN32
inline constexpr std::uint32_t ElevatedLoaderMutationSchemaVersion{1U};

enum class ElevatedLoaderOperation : std::uint8_t
{
    Apply,
    Remove,
};

enum class ElevatedLoaderFileAction : std::uint8_t
{
    Ignore,
    Verify,
    Install,
    Remove,
};

struct ElevatedLoaderFileMutation
{
    ElevatedLoaderFileAction action{};
    std::optional<FileMeasurement> expected_current{};
    FileMeasurement desired{};

    auto operator==(const ElevatedLoaderFileMutation&) const
        -> bool = default;
};

struct ElevatedLoaderMutationRequest
{
    std::uint32_t schema_version{};
    ElevatedLoaderOperation operation{};
    Sha256Digest manifest_sha256{};
    std::string request_nonce{};
    std::filesystem::path game_directory{};
    ElevatedLoaderFileMutation proxy{};
    ElevatedLoaderFileMutation override_file{};

    auto operator==(const ElevatedLoaderMutationRequest&) const
        -> bool = default;
};

enum class ElevatedLoaderMutationErrorCode : std::uint8_t
{
    InvalidRequest,
    GameRunning,
    GameDirectory,
    Payload,
    Precondition,
    Io,
};

struct ElevatedLoaderMutationError
{
    ElevatedLoaderMutationErrorCode code{};
    std::string detail{};

    auto operator==(const ElevatedLoaderMutationError&) const
        -> bool = default;
};

struct ElevatedLoaderMutationResult
{
    bool proxy_mutated{};
    bool override_mutated{};

    auto operator==(const ElevatedLoaderMutationResult&) const
        -> bool = default;
};

class ElevatedLoaderMutationPlatform
{
public:
    ElevatedLoaderMutationPlatform() = default;
    ElevatedLoaderMutationPlatform(
        const ElevatedLoaderMutationPlatform&) = delete;
    auto operator=(const ElevatedLoaderMutationPlatform&)
        -> ElevatedLoaderMutationPlatform& = delete;
    ElevatedLoaderMutationPlatform(
        ElevatedLoaderMutationPlatform&&) = delete;
    auto operator=(ElevatedLoaderMutationPlatform&&)
        -> ElevatedLoaderMutationPlatform& = delete;
    virtual ~ElevatedLoaderMutationPlatform() = default;

    virtual auto game_running()
        -> std::expected<
            bool,
            ElevatedLoaderMutationError> = 0;

    virtual auto validate_game_directory(
        const std::filesystem::path& path)
        -> std::expected<
            GameInstallation,
            ElevatedLoaderMutationError> = 0;
};

class Win32ElevatedLoaderMutationPlatform final
    : public ElevatedLoaderMutationPlatform
{
public:
    auto game_running()
        -> std::expected<
            bool,
            ElevatedLoaderMutationError> override;

    auto validate_game_directory(
        const std::filesystem::path& path)
        -> std::expected<
            GameInstallation,
            ElevatedLoaderMutationError> override;
};

[[nodiscard]] auto execute_elevated_loader_mutation(
    const ElevatedLoaderMutationRequest& request,
    const Sha256Digest& accepted_manifest_sha256,
    const ManagedLoaderMaterial* material,
    ElevatedLoaderMutationPlatform& platform)
    -> std::expected<
        ElevatedLoaderMutationResult,
        ElevatedLoaderMutationError>;
#endif
} // namespace meccha::launcher
