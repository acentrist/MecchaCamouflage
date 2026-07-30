#include <meccha/launcher/transaction.hpp>

#include <meccha/build_identity.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
constexpr std::uint32_t JournalSchemaVersion =
    build::RuntimeTransactionSchemaVersion;
constexpr std::string_view ActiveName{"active"};
constexpr std::string_view RollbackName{"rollback"};
constexpr std::string_view StagingPrefix{"staging-"};

auto transaction_error(RuntimeTransactionErrorCode code, std::string detail)
    -> std::unexpected<RuntimeTransactionError>
{
    return std::unexpected(RuntimeTransactionError{code, std::move(detail)});
}

auto storage_error(const RuntimeStorageError& error)
    -> std::unexpected<RuntimeTransactionError>
{
    return transaction_error(
        error.code == RuntimeStorageErrorCode::Io
            ? RuntimeTransactionErrorCode::Storage
            : RuntimeTransactionErrorCode::Conflict,
        error.detail);
}

auto valid_nonce(std::string_view nonce) -> bool
{
    return nonce.size() == 32 &&
           std::ranges::all_of(nonce, [](char value) {
               return (value >= '0' && value <= '9') ||
                      (value >= 'a' && value <= 'f');
           });
}

auto valid_staging_name(std::string_view name) -> bool
{
    return name.starts_with(StagingPrefix) &&
           valid_nonce(name.substr(StagingPrefix.size()));
}

auto matches(
    const GenerationIdentity& identity,
    const Sha256Digest& manifest_sha256) -> bool
{
    return identity.state == GenerationState::OwnedExact &&
           identity.manifest_sha256 == manifest_sha256;
}

auto identify(RuntimeStorage& storage, std::string_view name)
    -> std::expected<GenerationIdentity, RuntimeTransactionError>
{
    auto identity = storage.identify_generation(name);
    if (!identity)
    {
        return storage_error(identity.error());
    }
    return *identity;
}

auto staging_names(RuntimeStorage& storage)
    -> std::expected<std::vector<std::string>, RuntimeTransactionError>
{
    auto names = storage.list_staging_generations();
    if (!names)
    {
        return storage_error(names.error());
    }
    return *names;
}

auto remove_generation(
    RuntimeStorage& storage,
    std::string_view name,
    const Sha256Digest& manifest_sha256)
    -> std::expected<void, RuntimeTransactionError>
{
    auto removed = storage.remove_generation(name, manifest_sha256);
    if (!removed)
    {
        return storage_error(removed.error());
    }
    return {};
}

auto remove_journal(RuntimeStorage& storage)
    -> std::expected<void, RuntimeTransactionError>
{
    auto removed = storage.remove_journal();
    if (!removed)
    {
        return storage_error(removed.error());
    }
    return {};
}

auto write_journal(
    RuntimeStorage& storage,
    const RuntimeTransactionJournal& journal)
    -> std::expected<void, RuntimeTransactionError>
{
    auto written = storage.write_journal(journal);
    if (!written)
    {
        return storage_error(written.error());
    }
    return {};
}

auto rename_generation(
    RuntimeStorage& storage,
    std::string_view from,
    std::string_view to) -> std::expected<void, RuntimeTransactionError>
{
    auto renamed = storage.rename_generation(from, to);
    if (!renamed)
    {
        return storage_error(renamed.error());
    }
    return {};
}

auto validate_journal(const RuntimeTransactionJournal& journal)
    -> std::expected<void, RuntimeTransactionError>
{
    if (journal.schema_version != JournalSchemaVersion ||
        !valid_staging_name(journal.staging_name) ||
        (journal.previous_manifest_sha256 &&
         *journal.previous_manifest_sha256 == journal.next_manifest_sha256))
    {
        return transaction_error(
            RuntimeTransactionErrorCode::InvalidJournal,
            "Runtime transaction journal is invalid.");
    }
    return {};
}

auto reject_conflicting_generation(
    const GenerationIdentity& identity,
    std::string_view name) -> std::expected<void, RuntimeTransactionError>
{
    if (identity.state == GenerationState::Conflict ||
        identity.state == GenerationState::OwnedPartial)
    {
        return transaction_error(
            RuntimeTransactionErrorCode::Conflict,
            "Runtime generation is not safely owned: " + std::string{name});
    }
    return {};
}
} // namespace

auto recover_runtime(RuntimeStorage& storage)
    -> std::expected<RuntimeRecoveryResult, RuntimeTransactionError>
{
    auto journal_result = storage.read_journal();
    if (!journal_result)
    {
        return storage_error(journal_result.error());
    }
    auto names = staging_names(storage);
    if (!names)
    {
        return std::unexpected(names.error());
    }

    if (!*journal_result)
    {
        const auto rollback = identify(storage, RollbackName);
        if (!rollback)
        {
            return std::unexpected(rollback.error());
        }
        if (rollback->state != GenerationState::Missing)
        {
            return transaction_error(
                RuntimeTransactionErrorCode::Conflict,
                "Rollback exists without a transaction journal.");
        }

        auto result = RuntimeRecoveryResult::Clean;
        for (const auto& name : *names)
        {
            if (!valid_staging_name(name))
            {
                return transaction_error(
                    RuntimeTransactionErrorCode::Conflict,
                    "Unrecognized staging generation: " + name);
            }
            const auto identity = identify(storage, name);
            if (!identity)
            {
                return std::unexpected(identity.error());
            }
            if ((identity->state != GenerationState::OwnedExact &&
                 identity->state != GenerationState::OwnedPartial))
            {
                return transaction_error(
                    RuntimeTransactionErrorCode::Conflict,
                    "Staging generation is not safely owned: " + name);
            }
            auto removed = remove_generation(
                storage,
                name,
                identity->manifest_sha256);
            if (!removed)
            {
                return std::unexpected(removed.error());
            }
            result = RuntimeRecoveryResult::RemovedOrphanStaging;
        }
        return result;
    }

    auto journal = **journal_result;
    const auto valid = validate_journal(journal);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    for (const auto& name : *names)
    {
        if (name != journal.staging_name)
        {
            return transaction_error(
                RuntimeTransactionErrorCode::Conflict,
                "Unexpected staging generation during recovery: " + name);
        }
    }

    const auto active = identify(storage, ActiveName);
    const auto rollback = identify(storage, RollbackName);
    auto staging = identify(storage, journal.staging_name);
    if (!active || !rollback || !staging)
    {
        if (!active)
        {
            return std::unexpected(active.error());
        }
        if (!rollback)
        {
            return std::unexpected(rollback.error());
        }
        return std::unexpected(staging.error());
    }
    if (staging->state == GenerationState::OwnedPartial &&
        staging->manifest_sha256 == journal.next_manifest_sha256 &&
        matches(*active, journal.next_manifest_sha256))
    {
        auto removed = remove_generation(
            storage,
            journal.staging_name,
            journal.next_manifest_sha256);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
        staging = GenerationIdentity{GenerationState::Missing, {}};
    }

    for (const auto& [identity, name] : {
             std::pair{*active, ActiveName},
             std::pair{*rollback, RollbackName},
             std::pair{*staging, std::string_view{journal.staging_name}},
         })
    {
        const auto accepted = reject_conflicting_generation(identity, name);
        if (!accepted)
        {
            return std::unexpected(accepted.error());
        }
    }

    const auto rollback_matches_previous =
        journal.previous_manifest_sha256 &&
        matches(*rollback, *journal.previous_manifest_sha256);
    const auto rollback_is_expected =
        journal.previous_manifest_sha256
            ? (rollback_matches_previous ||
               rollback->state == GenerationState::Missing)
            : rollback->state == GenerationState::Missing;

    if (journal.phase == TransactionPhase::Committed)
    {
        if (!matches(*active, journal.next_manifest_sha256) ||
            staging->state != GenerationState::Missing ||
            !rollback_is_expected)
        {
            return transaction_error(
                RuntimeTransactionErrorCode::Conflict,
                "Committed runtime transaction state is inconsistent.");
        }
        if (rollback_matches_previous)
        {
            auto removed = remove_generation(
                storage,
                RollbackName,
                *journal.previous_manifest_sha256);
            if (!removed)
            {
                return std::unexpected(removed.error());
            }
        }
        auto removed = remove_journal(storage);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
        return RuntimeRecoveryResult::FinalizedNext;
    }

    if (matches(*active, journal.next_manifest_sha256) &&
        staging->state == GenerationState::Missing &&
        ((journal.previous_manifest_sha256 && rollback_matches_previous) ||
         (!journal.previous_manifest_sha256 &&
          rollback->state == GenerationState::Missing)))
    {
        journal.phase = TransactionPhase::Committed;
        auto committed = write_journal(storage, journal);
        if (!committed)
        {
            return std::unexpected(committed.error());
        }
        if (rollback_matches_previous)
        {
            auto removed = remove_generation(
                storage,
                RollbackName,
                *journal.previous_manifest_sha256);
            if (!removed)
            {
                return std::unexpected(removed.error());
            }
        }
        auto removed = remove_journal(storage);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
        return RuntimeRecoveryResult::FinalizedNext;
    }

    if (journal.previous_manifest_sha256)
    {
        const auto active_is_previous =
            matches(*active, *journal.previous_manifest_sha256);
        const auto staging_is_next =
            matches(*staging, journal.next_manifest_sha256);
        if (active_is_previous &&
            rollback->state == GenerationState::Missing &&
            staging_is_next)
        {
            auto removed = remove_generation(
                storage,
                journal.staging_name,
                journal.next_manifest_sha256);
            if (!removed)
            {
                return std::unexpected(removed.error());
            }
            auto journal_removed = remove_journal(storage);
            if (!journal_removed)
            {
                return std::unexpected(journal_removed.error());
            }
            return RuntimeRecoveryResult::AbortedStaging;
        }
        if (active->state == GenerationState::Missing &&
            rollback_matches_previous &&
            staging_is_next)
        {
            auto restored = rename_generation(
                storage,
                RollbackName,
                ActiveName);
            if (!restored)
            {
                return std::unexpected(restored.error());
            }
            auto removed = remove_generation(
                storage,
                journal.staging_name,
                journal.next_manifest_sha256);
            if (!removed)
            {
                return std::unexpected(removed.error());
            }
            auto journal_removed = remove_journal(storage);
            if (!journal_removed)
            {
                return std::unexpected(journal_removed.error());
            }
            return RuntimeRecoveryResult::RestoredPrevious;
        }
    }
    else if (
        active->state == GenerationState::Missing &&
        rollback->state == GenerationState::Missing &&
        matches(*staging, journal.next_manifest_sha256))
    {
        auto removed = remove_generation(
            storage,
            journal.staging_name,
            journal.next_manifest_sha256);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
        auto journal_removed = remove_journal(storage);
        if (!journal_removed)
        {
            return std::unexpected(journal_removed.error());
        }
        return RuntimeRecoveryResult::AbortedStaging;
    }

    return transaction_error(
        RuntimeTransactionErrorCode::Conflict,
        "Prepared runtime transaction state is inconsistent.");
}

auto validate_runtime_reuse(
    RuntimeStorage& storage,
    const Sha256Digest& expected_manifest_sha256)
    -> std::expected<void, RuntimeTransactionError>
{
    auto journal = storage.read_journal();
    if (!journal)
    {
        return storage_error(journal.error());
    }
    if (*journal)
    {
        return transaction_error(
            RuntimeTransactionErrorCode::Conflict,
            "Runtime reuse found a pending transaction journal.");
    }
    auto names = staging_names(storage);
    if (!names)
    {
        return std::unexpected(names.error());
    }
    if (!names->empty())
    {
        return transaction_error(
            RuntimeTransactionErrorCode::Conflict,
            "Runtime reuse found a staging generation.");
    }
    const auto rollback = identify(storage, RollbackName);
    if (!rollback)
    {
        return std::unexpected(rollback.error());
    }
    if (rollback->state != GenerationState::Missing)
    {
        return transaction_error(
            RuntimeTransactionErrorCode::Conflict,
            "Runtime reuse found a rollback generation.");
    }
    const auto active = identify(storage, ActiveName);
    if (!active)
    {
        return std::unexpected(active.error());
    }
    if (!matches(*active, expected_manifest_sha256))
    {
        return transaction_error(
            RuntimeTransactionErrorCode::Conflict,
            "The active runtime changed after reuse planning.");
    }
    return {};
}

auto prepare_runtime(
    RuntimeStorage& storage,
    const Sha256Digest& next_manifest_sha256,
    std::string_view nonce)
    -> std::expected<RuntimePrepareResult, RuntimeTransactionError>
{
    if (!valid_nonce(nonce))
    {
        return transaction_error(
            RuntimeTransactionErrorCode::InvalidNonce,
            "Runtime staging nonce must be 32 lowercase hexadecimal characters.");
    }

    auto recovered = recover_runtime(storage);
    if (!recovered)
    {
        return std::unexpected(recovered.error());
    }

    const auto active = identify(storage, ActiveName);
    if (!active)
    {
        return std::unexpected(active.error());
    }
    const auto active_accepted =
        reject_conflicting_generation(*active, ActiveName);
    if (!active_accepted)
    {
        return std::unexpected(active_accepted.error());
    }
    if (matches(*active, next_manifest_sha256))
    {
        return RuntimePrepareResult::Reused;
    }

    std::optional<Sha256Digest> previous_manifest{};
    if (active->state == GenerationState::OwnedExact)
    {
        previous_manifest = active->manifest_sha256;
    }

    const std::string staging_name{StagingPrefix};
    auto full_staging_name = staging_name + std::string{nonce};
    auto staged =
        storage.stage_generation(full_staging_name, next_manifest_sha256);
    if (!staged)
    {
        return storage_error(staged.error());
    }
    const auto staged_identity = identify(storage, full_staging_name);
    if (!staged_identity)
    {
        return std::unexpected(staged_identity.error());
    }
    if (!matches(*staged_identity, next_manifest_sha256))
    {
        return transaction_error(
            RuntimeTransactionErrorCode::Conflict,
            "Staged runtime did not verify against the next manifest.");
    }

    RuntimeTransactionJournal journal{
        JournalSchemaVersion,
        TransactionPhase::Prepared,
        previous_manifest,
        next_manifest_sha256,
        full_staging_name,
    };
    auto journal_written = write_journal(storage, journal);
    if (!journal_written)
    {
        return std::unexpected(journal_written.error());
    }

    if (previous_manifest)
    {
        auto moved_previous =
            rename_generation(storage, ActiveName, RollbackName);
        if (!moved_previous)
        {
            return std::unexpected(moved_previous.error());
        }
    }
    auto promoted =
        rename_generation(storage, full_staging_name, ActiveName);
    if (!promoted)
    {
        return std::unexpected(promoted.error());
    }

    const auto promoted_identity = identify(storage, ActiveName);
    if (!promoted_identity)
    {
        return std::unexpected(promoted_identity.error());
    }
    if (!matches(*promoted_identity, next_manifest_sha256))
    {
        return transaction_error(
            RuntimeTransactionErrorCode::Conflict,
            "Promoted runtime did not verify against the next manifest.");
    }

    journal.phase = TransactionPhase::Committed;
    auto committed = write_journal(storage, journal);
    if (!committed)
    {
        return std::unexpected(committed.error());
    }
    if (previous_manifest)
    {
        auto removed =
            remove_generation(storage, RollbackName, *previous_manifest);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
    }
    auto journal_removed = remove_journal(storage);
    if (!journal_removed)
    {
        return std::unexpected(journal_removed.error());
    }
    return RuntimePrepareResult::Published;
}

auto remove_runtime(RuntimeStorage& storage)
    -> std::expected<RuntimeRemoveResult, RuntimeTransactionError>
{
    auto recovered = recover_runtime(storage);
    if (!recovered)
    {
        return std::unexpected(recovered.error());
    }
    const auto active = identify(storage, ActiveName);
    if (!active)
    {
        return std::unexpected(active.error());
    }
    const auto accepted =
        reject_conflicting_generation(*active, ActiveName);
    if (!accepted)
    {
        return std::unexpected(accepted.error());
    }
    if (active->state == GenerationState::Missing)
    {
        return RuntimeRemoveResult::Missing;
    }
    auto removed = remove_generation(
        storage,
        ActiveName,
        active->manifest_sha256);
    if (!removed)
    {
        return std::unexpected(removed.error());
    }
    return RuntimeRemoveResult::Removed;
}
} // namespace meccha::launcher
