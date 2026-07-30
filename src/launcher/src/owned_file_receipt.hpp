#pragma once

#include <meccha/launcher/owned_file_storage.hpp>

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace meccha::launcher::detail
{
enum class OwnedFileReceiptPhase : std::uint8_t
{
    Installing,
    Complete,
    Removing,
};

struct OwnedFileReceipt
{
    OwnedFileReceiptPhase phase{};
    OwnershipRecord next{};
    std::optional<OwnershipRecord> previous{};
};

[[nodiscard]] auto serialize_owned_file_receipt(
    const OwnedFileReceipt& receipt)
    -> std::expected<std::string, OwnedFileStoreError>;

[[nodiscard]] auto parse_owned_file_receipt(
    std::string_view json,
    std::string_view manifest_path,
    FileRole role)
    -> std::expected<OwnedFileReceipt, OwnedFileStoreError>;
} // namespace meccha::launcher::detail
