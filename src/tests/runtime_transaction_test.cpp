#include <meccha/launcher/transaction.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::launcher;

auto digest(std::byte value) -> Sha256Digest
{
    Sha256Digest result{};
    result.bytes.fill(value);
    return result;
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL runtime_transaction: " << message << '\n';
    }
    return condition;
}

class FakeRuntimeStorage final : public RuntimeStorage
{
public:
    auto identify_generation(std::string_view name)
        -> std::expected<GenerationIdentity, RuntimeStorageError> override
    {
        if (const auto failure = fail("identify"); failure)
        {
            return std::unexpected(*failure);
        }
        const auto found = generations.find(std::string{name});
        if (found == generations.end())
        {
            return GenerationIdentity{GenerationState::Missing, {}};
        }
        return found->second;
    }

    auto list_staging_generations()
        -> std::expected<std::vector<std::string>, RuntimeStorageError> override
    {
        if (const auto failure = fail("list"); failure)
        {
            return std::unexpected(*failure);
        }
        std::vector<std::string> names{};
        for (const auto& [name, identity] : generations)
        {
            static_cast<void>(identity);
            if (name.starts_with("staging-"))
            {
                names.push_back(name);
            }
        }
        return names;
    }

    auto read_journal()
        -> std::expected<std::optional<RuntimeTransactionJournal>, RuntimeStorageError> override
    {
        if (const auto failure = fail("read-journal"); failure)
        {
            return std::unexpected(*failure);
        }
        return journal;
    }

    auto stage_generation(std::string_view name, const Sha256Digest& manifest)
        -> std::expected<void, RuntimeStorageError> override
    {
        if (const auto failure = fail("stage"); failure)
        {
            return std::unexpected(*failure);
        }
        generations[std::string{name}] = {
            GenerationState::OwnedExact,
            manifest,
        };
        mutations.emplace_back("stage " + std::string{name});
        return {};
    }

    auto write_journal(const RuntimeTransactionJournal& value)
        -> std::expected<void, RuntimeStorageError> override
    {
        if (const auto failure = fail("write-journal"); failure)
        {
            return std::unexpected(*failure);
        }
        journal = value;
        mutations.emplace_back(
            value.phase == TransactionPhase::Prepared
                ? "journal prepared"
                : "journal committed");
        return {};
    }

    auto rename_generation(std::string_view from, std::string_view to)
        -> std::expected<void, RuntimeStorageError> override
    {
        if (const auto failure = fail("rename"); failure)
        {
            return std::unexpected(*failure);
        }
        const auto source = generations.find(std::string{from});
        if (source == generations.end() ||
            generations.contains(std::string{to}))
        {
            return std::unexpected(RuntimeStorageError{
                RuntimeStorageErrorCode::Io,
                "invalid fake rename"});
        }
        generations[std::string{to}] = source->second;
        generations.erase(source);
        mutations.emplace_back(
            "rename " + std::string{from} + " " + std::string{to});
        return {};
    }

    auto remove_generation(
        std::string_view name,
        const Sha256Digest& expected_manifest)
        -> std::expected<void, RuntimeStorageError> override
    {
        if (const auto failure = fail("remove"); failure)
        {
            return std::unexpected(*failure);
        }
        const auto found = generations.find(std::string{name});
        if (found == generations.end() ||
            found->second.manifest_sha256 != expected_manifest ||
            (found->second.state != GenerationState::OwnedExact &&
             found->second.state != GenerationState::OwnedPartial))
        {
            return std::unexpected(RuntimeStorageError{
                RuntimeStorageErrorCode::Conflict,
                "unsafe fake removal"});
        }
        generations.erase(found);
        mutations.emplace_back("remove " + std::string{name});
        return {};
    }

    auto remove_journal() -> std::expected<void, RuntimeStorageError> override
    {
        if (const auto failure = fail("remove-journal"); failure)
        {
            return std::unexpected(*failure);
        }
        journal.reset();
        mutations.emplace_back("remove journal");
        return {};
    }

    void fail_operation(std::string name, std::size_t occurrence)
    {
        failure_name = std::move(name);
        failure_occurrence = occurrence;
        seen_failures = 0;
    }

    void clear_failure()
    {
        failure_name.clear();
        failure_occurrence = 0;
        seen_failures = 0;
    }

    std::map<std::string, GenerationIdentity, std::less<>> generations{};
    std::optional<RuntimeTransactionJournal> journal{};
    std::vector<std::string> mutations{};

private:
    auto fail(std::string_view operation) -> std::optional<RuntimeStorageError>
    {
        if (operation == failure_name)
        {
            ++seen_failures;
            if (seen_failures == failure_occurrence)
            {
                return RuntimeStorageError{
                    RuntimeStorageErrorCode::Io,
                    "injected failure at " + std::string{operation}};
            }
        }
        return std::nullopt;
    }

    std::string failure_name{};
    std::size_t failure_occurrence{};
    std::size_t seen_failures{};
};

auto exact(const Sha256Digest& manifest) -> GenerationIdentity
{
    return GenerationIdentity{GenerationState::OwnedExact, manifest};
}
} // namespace

auto main() -> int
{
    const auto old_manifest = digest(std::byte{0x11});
    const auto new_manifest = digest(std::byte{0x22});
    constexpr std::string_view Nonce{"0123456789abcdef0123456789abcdef"};
    constexpr std::string_view Staging{
        "staging-0123456789abcdef0123456789abcdef"};

    bool passed = true;

    FakeRuntimeStorage fresh{};
    const auto fresh_result = prepare_runtime(fresh, new_manifest, Nonce);
    passed &= expect(
        fresh_result &&
            *fresh_result == RuntimePrepareResult::Published &&
            fresh.generations.size() == 1 &&
            fresh.generations.at("active") == exact(new_manifest) &&
            !fresh.journal,
        "fresh publication did not leave one active generation");

    const auto mutations_before_reuse = fresh.mutations.size();
    const auto reused = prepare_runtime(fresh, new_manifest, Nonce);
    passed &= expect(
        reused && *reused == RuntimePrepareResult::Reused &&
            fresh.mutations.size() == mutations_before_reuse,
        "exact active generation was not a no-op");

    const auto mutations_before_validation =
        fresh.mutations.size();
    const auto validated_reuse =
        validate_runtime_reuse(fresh, new_manifest);
    passed &= expect(
        validated_reuse &&
            fresh.mutations.size() ==
                mutations_before_validation,
        "runtime reuse validation was not read-only");

    FakeRuntimeStorage stale_reuse{};
    stale_reuse.generations["active"] = exact(old_manifest);
    const auto rejected_stale_reuse =
        validate_runtime_reuse(stale_reuse, new_manifest);
    passed &= expect(
        !rejected_stale_reuse &&
            rejected_stale_reuse.error().code ==
                RuntimeTransactionErrorCode::Conflict &&
            stale_reuse.mutations.empty(),
        "stale runtime reuse was repaired without a publish plan");

    FakeRuntimeStorage pending_reuse{};
    pending_reuse.journal = RuntimeTransactionJournal{
        1,
        TransactionPhase::Prepared,
        std::nullopt,
        new_manifest,
        std::string{Staging},
    };
    const auto rejected_pending_reuse =
        validate_runtime_reuse(pending_reuse, new_manifest);
    passed &= expect(
        !rejected_pending_reuse &&
            rejected_pending_reuse.error().code ==
                RuntimeTransactionErrorCode::Conflict &&
            pending_reuse.mutations.empty(),
        "runtime reuse mutated a pending transaction");

    FakeRuntimeStorage update{};
    update.generations["active"] = exact(old_manifest);
    const auto updated = prepare_runtime(update, new_manifest, Nonce);
    passed &= expect(
        updated && *updated == RuntimePrepareResult::Published &&
            update.generations.size() == 1 &&
            update.generations.at("active") == exact(new_manifest) &&
            !update.journal,
        "managed update did not clean rollback and journal");

    FakeRuntimeStorage interrupted{};
    interrupted.generations["active"] = exact(old_manifest);
    interrupted.fail_operation("rename", 2);
    const auto failed_update =
        prepare_runtime(interrupted, new_manifest, Nonce);
    passed &= expect(
        !failed_update &&
            interrupted.generations.at("rollback") == exact(old_manifest) &&
            interrupted.generations.at(std::string{Staging}) ==
                exact(new_manifest) &&
            interrupted.journal &&
            interrupted.journal->phase == TransactionPhase::Prepared,
        "injected promotion failure lost recovery evidence");

    interrupted.clear_failure();
    const auto recovered_update =
        prepare_runtime(interrupted, new_manifest, Nonce);
    passed &= expect(
        recovered_update &&
            *recovered_update == RuntimePrepareResult::Published &&
            interrupted.generations.size() == 1 &&
            interrupted.generations.at("active") == exact(new_manifest) &&
            !interrupted.journal,
        "retry did not recover the old generation before updating");

    FakeRuntimeStorage promoted_crash{};
    promoted_crash.generations["active"] = exact(new_manifest);
    promoted_crash.generations["rollback"] = exact(old_manifest);
    promoted_crash.generations[std::string{Staging}] = {
        GenerationState::OwnedPartial,
        new_manifest,
    };
    promoted_crash.journal = RuntimeTransactionJournal{
        1,
        TransactionPhase::Prepared,
        old_manifest,
        new_manifest,
        std::string{Staging},
    };
    const auto finalized = recover_runtime(promoted_crash);
    passed &= expect(
        finalized &&
            promoted_crash.generations.size() == 1 &&
            promoted_crash.generations.at("active") == exact(new_manifest) &&
            !promoted_crash.journal,
        "recovery did not finalize an already promoted generation");

    FakeRuntimeStorage orphan{};
    orphan.generations[std::string{Staging}] = {
        GenerationState::OwnedPartial,
        new_manifest,
    };
    const auto orphan_recovery = recover_runtime(orphan);
    passed &= expect(
        orphan_recovery && orphan.generations.empty(),
        "owned partial staging was not cleaned");

    FakeRuntimeStorage hostile{};
    hostile.generations["active"] = {
        GenerationState::Conflict,
        {},
    };
    const auto hostile_result = prepare_runtime(hostile, new_manifest, Nonce);
    passed &= expect(
        !hostile_result &&
            hostile_result.error().code == RuntimeTransactionErrorCode::Conflict &&
            hostile.mutations.empty(),
        "unknown active content was mutated");

    FakeRuntimeStorage bad_nonce{};
    const auto invalid_nonce =
        prepare_runtime(bad_nonce, new_manifest, "../escape");
    passed &= expect(
        !invalid_nonce &&
            invalid_nonce.error().code ==
                RuntimeTransactionErrorCode::InvalidNonce &&
            bad_nonce.mutations.empty(),
        "an unsafe staging nonce reached storage");

    FakeRuntimeStorage removable{};
    removable.generations["active"] = exact(old_manifest);
    const auto removed_runtime = remove_runtime(removable);
    passed &= expect(
        removed_runtime &&
            *removed_runtime == RuntimeRemoveResult::Removed &&
            removable.generations.empty() &&
            removable.mutations ==
                std::vector<std::string>{
                    "remove active",
                },
        "owned active runtime was not removed through its identity");

    FakeRuntimeStorage missing_runtime{};
    const auto missing_remove = remove_runtime(missing_runtime);
    passed &= expect(
        missing_remove &&
            *missing_remove == RuntimeRemoveResult::Missing &&
            missing_runtime.mutations.empty(),
        "missing runtime removal was not an idempotent no-op");

    FakeRuntimeStorage conflicted_remove{};
    conflicted_remove.generations["active"] = {
        GenerationState::Conflict,
        {},
    };
    const auto rejected_remove = remove_runtime(conflicted_remove);
    passed &= expect(
        !rejected_remove &&
            rejected_remove.error().code ==
                RuntimeTransactionErrorCode::Conflict &&
            conflicted_remove.mutations.empty(),
        "conflicting runtime content was removed");

    FakeRuntimeStorage interrupted_remove{};
    interrupted_remove.generations["active"] = exact(old_manifest);
    interrupted_remove.generations[std::string{Staging}] =
        exact(new_manifest);
    interrupted_remove.journal = RuntimeTransactionJournal{
        1,
        TransactionPhase::Prepared,
        old_manifest,
        new_manifest,
        std::string{Staging},
    };
    const auto recovered_remove =
        remove_runtime(interrupted_remove);
    passed &= expect(
        recovered_remove &&
            *recovered_remove == RuntimeRemoveResult::Removed &&
            interrupted_remove.generations.empty() &&
            !interrupted_remove.journal &&
            interrupted_remove.mutations ==
                std::vector<std::string>{
                    "remove " + std::string{Staging},
                    "remove journal",
                    "remove active",
                },
        "runtime removal did not recover the transaction first");

    if (passed)
    {
        std::cout << "PASS runtime_transaction\n";
        return 0;
    }
    return 1;
}
