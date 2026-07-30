#include <meccha/launcher/owned_file_storage.hpp>

#include <meccha/launcher/hash.hpp>

#include "owned_file_win32_io.hpp"

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;
using Receipt = detail::OwnedFileReceipt;
using ReceiptPhase = detail::OwnedFileReceiptPhase;

auto error(OwnedFileStoreErrorCode code, std::string detail)
    -> std::unexpected<OwnedFileStoreError>
{
    return std::unexpected(
        OwnedFileStoreError{code, std::move(detail)});
}

auto matches(
    const std::optional<FileMeasurement>& measurement,
    const OwnershipRecord& record) -> bool
{
    return measurement &&
           measurement->size == record.file.size &&
           measurement->sha256 == record.file.sha256;
}

auto complete_receipt(const OwnershipRecord& record) -> Receipt
{
    return Receipt{
        ReceiptPhase::Complete,
        record,
        std::nullopt,
    };
}

auto validate_store_identity(
    const fs::path& target,
    const fs::path& ownership_record,
    std::string_view manifest_path)
    -> std::expected<void, OwnedFileStoreError>
{
    auto target_valid = detail::validate_absolute_file_path(
        target,
        "Owned-file target");
    if (!target_valid)
    {
        return std::unexpected(target_valid.error());
    }
    auto record_valid = detail::validate_absolute_file_path(
        ownership_record,
        "Owned-file receipt");
    if (!record_valid)
    {
        return std::unexpected(record_valid.error());
    }
    if (target == ownership_record || manifest_path.empty())
    {
        return error(
            OwnedFileStoreErrorCode::InvalidRequest,
            "Owned-file target and receipt identities are invalid.");
    }
    return {};
}
} // namespace

Win32OwnedFileStore::Win32OwnedFileStore(
    fs::path target,
    fs::path ownership_record,
    std::string manifest_path,
    FileRole role)
    : target_(std::move(target)),
      ownership_record_(std::move(ownership_record)),
      manifest_path_(std::move(manifest_path)),
      role_(role)
{
}

auto Win32OwnedFileStore::recover()
    -> std::expected<void, OwnedFileStoreError>
{
    auto identity = validate_store_identity(
        target_,
        ownership_record_,
        manifest_path_);
    if (!identity)
    {
        return std::unexpected(identity.error());
    }
    auto target_parent = detail::require_plain_directory_tree(
        target_.parent_path());
    if (!target_parent)
    {
        return std::unexpected(target_parent.error());
    }
    auto receipt_parent = detail::inspect_plain_directory_tree(
        ownership_record_.parent_path());
    if (!receipt_parent)
    {
        return std::unexpected(receipt_parent.error());
    }

    const auto temporary_target =
        detail::owned_file_staging_path(target_);
    const auto staged =
        detail::measure_plain_file(temporary_target);
    const auto current = detail::measure_plain_file(target_);
    if (!staged)
    {
        return std::unexpected(staged.error());
    }
    if (!current)
    {
        return std::unexpected(current.error());
    }
    if (!*receipt_parent)
    {
        if (*staged)
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "An unowned target staging file already exists.");
        }
        return {};
    }

    auto receipt = detail::read_owned_file_receipt(
        ownership_record_,
        manifest_path_,
        role_,
        true);
    if (!receipt)
    {
        return std::unexpected(receipt.error());
    }
    if (!*receipt)
    {
        if (*staged)
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "An unowned target staging file already exists.");
        }
        return {};
    }

    const auto& transaction = **receipt;
    if (transaction.phase == ReceiptPhase::Complete)
    {
        if (*staged)
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "A complete owned file has an unexpected staging "
                "file.");
        }
        if (!*current)
        {
            return detail::delete_plain_file(ownership_record_);
        }
        if (!matches(*current, transaction.next))
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "Owned-file content changed after installation.");
        }
        return {};
    }

    if (transaction.phase == ReceiptPhase::Installing)
    {
        if (matches(*current, transaction.next))
        {
            if (*staged)
            {
                if (!matches(*staged, transaction.next))
                {
                    return error(
                        OwnedFileStoreErrorCode::Conflict,
                        "Interrupted owned-file staging content "
                        "changed.");
                }
                auto removed =
                    detail::delete_plain_file(temporary_target);
                if (!removed)
                {
                    return std::unexpected(removed.error());
                }
            }
            return detail::write_owned_file_receipt(
                ownership_record_,
                complete_receipt(transaction.next));
        }

        const auto original_is_intact =
            transaction.previous
                ? matches(*current, *transaction.previous)
                : !*current;
        if (!original_is_intact)
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "Interrupted owned-file installation cannot be "
                "recovered safely.");
        }
        if (*staged)
        {
            if (!matches(*staged, transaction.next))
            {
                return error(
                    OwnedFileStoreErrorCode::Conflict,
                    "Interrupted owned-file staging content changed.");
            }
            auto removed =
                detail::delete_plain_file(temporary_target);
            if (!removed)
            {
                return std::unexpected(removed.error());
            }
        }
        if (transaction.previous)
        {
            return detail::write_owned_file_receipt(
                ownership_record_,
                complete_receipt(*transaction.previous));
        }
        return detail::delete_plain_file(ownership_record_);
    }

    if (*staged)
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file removal has an unexpected staging file.");
    }
    if (!*current)
    {
        return detail::delete_plain_file(ownership_record_);
    }
    if (!matches(*current, transaction.next))
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Interrupted owned-file removal found changed content.");
    }
    return detail::write_owned_file_receipt(
        ownership_record_,
        complete_receipt(transaction.next));
}

auto Win32OwnedFileStore::observe(
    const OwnedFileExpectation& expected)
    -> std::expected<ArtifactState, OwnedFileStoreError>
{
    auto identity = validate_store_identity(
        target_,
        ownership_record_,
        manifest_path_);
    if (!identity)
    {
        return std::unexpected(identity.error());
    }
    if (expected.file.path != manifest_path_ ||
        expected.file.role != role_)
    {
        return error(
            OwnedFileStoreErrorCode::InvalidRequest,
            "Owned-file expectation does not match the store "
            "identity.");
    }
    auto target_parent = detail::require_plain_directory_tree(
        target_.parent_path());
    if (!target_parent)
    {
        return std::unexpected(target_parent.error());
    }
    auto receipt_parent = detail::inspect_plain_directory_tree(
        ownership_record_.parent_path());
    if (!receipt_parent)
    {
        return std::unexpected(receipt_parent.error());
    }
    const auto temporary_target =
        detail::owned_file_staging_path(target_);
    auto staged =
        detail::measure_plain_file(temporary_target);
    if (!staged)
    {
        return std::unexpected(staged.error());
    }
    if (*staged)
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file observation found a staging file.");
    }
    auto current = detail::measure_plain_file(target_);
    if (!current)
    {
        return std::unexpected(current.error());
    }
    std::optional<OwnershipRecord> record{};
    if (*receipt_parent)
    {
        auto receipt = detail::read_owned_file_receipt(
            ownership_record_,
            manifest_path_,
            role_,
            false);
        if (!receipt)
        {
            return std::unexpected(receipt.error());
        }
        if (*receipt &&
            (**receipt).phase != ReceiptPhase::Complete)
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "Owned-file recovery is required before "
                "observation.");
        }
        if (*receipt)
        {
            record = (**receipt).next;
        }
    }
    return classify_owned_file(expected, *current, record);
}

auto Win32OwnedFileStore::install(
    const OwnedFileExpectation& expected,
    std::span<const std::byte> payload)
    -> std::expected<OwnedFileInstallResult, OwnedFileStoreError>
{
    if (expected.file.path != manifest_path_ ||
        expected.file.role != role_)
    {
        return error(
            OwnedFileStoreErrorCode::InvalidRequest,
            "Owned-file expectation does not match the store "
            "identity.");
    }
    const auto digest = sha256_bytes(payload);
    if (!digest || payload.size() != expected.file.size ||
        *digest != expected.file.sha256)
    {
        return error(
            OwnedFileStoreErrorCode::InvalidData,
            "Owned-file payload does not match its manifest.");
    }
    auto recovered = recover();
    if (!recovered)
    {
        return std::unexpected(recovered.error());
    }
    auto state = observe(expected);
    if (!state)
    {
        return std::unexpected(state.error());
    }
    if (*state == ArtifactState::ExactOwned)
    {
        return OwnedFileInstallResult::Reused;
    }
    if (*state != ArtifactState::Missing &&
        *state != ArtifactState::OwnedPrevious)
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file target is not safe to create or replace.");
    }

    auto receipt_parent = detail::ensure_plain_directory_tree(
        ownership_record_.parent_path());
    if (!receipt_parent)
    {
        return std::unexpected(receipt_parent.error());
    }
    auto current_receipt = detail::read_owned_file_receipt(
        ownership_record_,
        manifest_path_,
        role_,
        false);
    if (!current_receipt)
    {
        return std::unexpected(current_receipt.error());
    }
    std::optional<OwnershipRecord> previous{};
    if (*current_receipt)
    {
        previous = (**current_receipt).next;
    }
    const auto next = OwnershipRecord{
        expected.product_version,
        expected.manifest_sha256,
        expected.file,
    };
    auto installing = detail::write_owned_file_receipt(
        ownership_record_,
        Receipt{
            ReceiptPhase::Installing,
            next,
            previous,
        });
    if (!installing)
    {
        return std::unexpected(installing.error());
    }

    const auto temporary =
        detail::owned_file_staging_path(target_);
    auto staged = detail::write_new_durable(temporary, payload);
    if (!staged)
    {
        return std::unexpected(staged.error());
    }

    auto current = detail::measure_plain_file(target_);
    if (!current)
    {
        return std::unexpected(current.error());
    }
    const auto still_safe =
        *state == ArtifactState::Missing
            ? !*current
            : previous && matches(*current, *previous);
    if (!still_safe)
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file target changed during installation.");
    }
    auto published = detail::publish_staged_file(
        temporary,
        target_,
        *state == ArtifactState::OwnedPrevious);
    if (!published)
    {
        return std::unexpected(published.error());
    }
    const auto verified = detail::measure_plain_file(target_);
    if (!verified)
    {
        return std::unexpected(verified.error());
    }
    if (!matches(*verified, next))
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Published owned-file target failed verification.");
    }
    auto completed = detail::write_owned_file_receipt(
        ownership_record_,
        complete_receipt(next));
    if (!completed)
    {
        return std::unexpected(completed.error());
    }
    return *state == ArtifactState::Missing
               ? OwnedFileInstallResult::Created
               : OwnedFileInstallResult::Replaced;
}

auto Win32OwnedFileStore::remove_owned()
    -> std::expected<bool, OwnedFileStoreError>
{
    auto recovered = recover();
    if (!recovered)
    {
        return std::unexpected(recovered.error());
    }
    auto can_remove = removable();
    if (!can_remove)
    {
        return std::unexpected(can_remove.error());
    }
    if (!*can_remove)
    {
        return false;
    }
    auto receipt = detail::read_owned_file_receipt(
        ownership_record_,
        manifest_path_,
        role_,
        false);
    if (!receipt)
    {
        return std::unexpected(receipt.error());
    }
    auto current = detail::measure_plain_file(target_);
    if (!current)
    {
        return std::unexpected(current.error());
    }
    if (!*receipt || !matches(*current, (**receipt).next))
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file content changed during removal.");
    }
    auto removing = detail::write_owned_file_receipt(
        ownership_record_,
        Receipt{
            ReceiptPhase::Removing,
            (**receipt).next,
            std::nullopt,
        });
    if (!removing)
    {
        return std::unexpected(removing.error());
    }
    auto removed = detail::delete_plain_file(target_);
    if (!removed)
    {
        return std::unexpected(removed.error());
    }
    auto receipt_removed =
        detail::delete_plain_file(ownership_record_);
    if (!receipt_removed)
    {
        return std::unexpected(receipt_removed.error());
    }
    return true;
}

auto Win32OwnedFileStore::removable()
    -> std::expected<bool, OwnedFileStoreError>
{
    auto identity = validate_store_identity(
        target_,
        ownership_record_,
        manifest_path_);
    if (!identity)
    {
        return std::unexpected(identity.error());
    }
    auto target_parent = detail::require_plain_directory_tree(
        target_.parent_path());
    if (!target_parent)
    {
        return std::unexpected(target_parent.error());
    }
    auto receipt_parent = detail::inspect_plain_directory_tree(
        ownership_record_.parent_path());
    if (!receipt_parent)
    {
        return std::unexpected(receipt_parent.error());
    }
    if (!*receipt_parent)
    {
        return false;
    }
    const auto staging =
        detail::measure_plain_file(
            detail::owned_file_staging_path(target_));
    if (!staging)
    {
        return std::unexpected(staging.error());
    }
    if (*staging)
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file removal found a staging file.");
    }
    auto receipt = detail::read_owned_file_receipt(
        ownership_record_,
        manifest_path_,
        role_,
        false);
    if (!receipt)
    {
        return std::unexpected(receipt.error());
    }
    if (!*receipt)
    {
        return false;
    }
    if ((**receipt).phase != ReceiptPhase::Complete)
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file removal requires a complete receipt.");
    }
    auto current = detail::measure_plain_file(target_);
    if (!current)
    {
        return std::unexpected(current.error());
    }
    if (!matches(*current, (**receipt).next))
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Refusing to remove changed owned-file content.");
    }
    return true;
}
} // namespace meccha::launcher
