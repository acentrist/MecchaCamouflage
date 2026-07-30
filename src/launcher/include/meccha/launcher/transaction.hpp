#pragma once

#include <meccha/launcher/hash.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::launcher
{
enum class GenerationState : std::uint8_t
{
    Missing,
    OwnedExact,
    OwnedPartial,
    Conflict,
};

struct GenerationIdentity
{
    GenerationState state{};
    Sha256Digest manifest_sha256{};

    auto operator==(const GenerationIdentity&) const -> bool = default;
};

enum class TransactionPhase : std::uint8_t
{
    Prepared,
    Committed,
};

struct RuntimeTransactionJournal
{
    std::uint32_t schema_version{};
    TransactionPhase phase{};
    std::optional<Sha256Digest> previous_manifest_sha256{};
    Sha256Digest next_manifest_sha256{};
    std::string staging_name{};

    auto operator==(const RuntimeTransactionJournal&) const -> bool = default;
};

enum class RuntimeStorageErrorCode : std::uint8_t
{
    Io,
    InvalidData,
    Conflict,
};

struct RuntimeStorageError
{
    RuntimeStorageErrorCode code{};
    std::string detail{};

    auto operator==(const RuntimeStorageError&) const -> bool = default;
};

class RuntimeStorage
{
public:
    RuntimeStorage() = default;
    RuntimeStorage(const RuntimeStorage&) = delete;
    auto operator=(const RuntimeStorage&) -> RuntimeStorage& = delete;
    RuntimeStorage(RuntimeStorage&&) = delete;
    auto operator=(RuntimeStorage&&) -> RuntimeStorage& = delete;
    virtual ~RuntimeStorage() = default;

    virtual auto identify_generation(std::string_view name)
        -> std::expected<GenerationIdentity, RuntimeStorageError> = 0;

    virtual auto list_staging_generations()
        -> std::expected<std::vector<std::string>, RuntimeStorageError> = 0;

    virtual auto read_journal()
        -> std::expected<
            std::optional<RuntimeTransactionJournal>,
            RuntimeStorageError> = 0;

    virtual auto stage_generation(
        std::string_view name,
        const Sha256Digest& manifest_sha256)
        -> std::expected<void, RuntimeStorageError> = 0;

    virtual auto write_journal(const RuntimeTransactionJournal& journal)
        -> std::expected<void, RuntimeStorageError> = 0;

    virtual auto rename_generation(
        std::string_view from,
        std::string_view to) -> std::expected<void, RuntimeStorageError> = 0;

    virtual auto remove_generation(
        std::string_view name,
        const Sha256Digest& expected_manifest_sha256)
        -> std::expected<void, RuntimeStorageError> = 0;

    virtual auto remove_journal()
        -> std::expected<void, RuntimeStorageError> = 0;
};

enum class RuntimeTransactionErrorCode : std::uint8_t
{
    InvalidNonce,
    InvalidJournal,
    Conflict,
    Storage,
};

struct RuntimeTransactionError
{
    RuntimeTransactionErrorCode code{};
    std::string detail{};

    auto operator==(const RuntimeTransactionError&) const -> bool = default;
};

enum class RuntimeRecoveryResult : std::uint8_t
{
    Clean,
    RemovedOrphanStaging,
    AbortedStaging,
    RestoredPrevious,
    FinalizedNext,
};

enum class RuntimePrepareResult : std::uint8_t
{
    Reused,
    Published,
};

enum class RuntimeRemoveResult : std::uint8_t
{
    Missing,
    Removed,
};

enum class RuntimeCacheIdentity : std::uint8_t
{
    Missing,
    Exact,
    OwnedPrevious,
    Conflict,
};

[[nodiscard]] auto recover_runtime(RuntimeStorage& storage)
    -> std::expected<RuntimeRecoveryResult, RuntimeTransactionError>;

[[nodiscard]] auto observe_recovered_runtime_cache(
    RuntimeStorage& storage,
    const Sha256Digest& expected_manifest_sha256)
    -> std::expected<RuntimeCacheIdentity, RuntimeTransactionError>;

[[nodiscard]] auto validate_runtime_reuse(
    RuntimeStorage& storage,
    const Sha256Digest& expected_manifest_sha256)
    -> std::expected<void, RuntimeTransactionError>;

[[nodiscard]] auto prepare_runtime(
    RuntimeStorage& storage,
    const Sha256Digest& next_manifest_sha256,
    std::string_view nonce)
    -> std::expected<RuntimePrepareResult, RuntimeTransactionError>;

[[nodiscard]] auto remove_runtime(RuntimeStorage& storage)
    -> std::expected<RuntimeRemoveResult, RuntimeTransactionError>;
} // namespace meccha::launcher
