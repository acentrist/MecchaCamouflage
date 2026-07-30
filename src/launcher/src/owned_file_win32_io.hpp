#pragma once

#include "owned_file_receipt.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace meccha::launcher::detail
{
[[nodiscard]] auto owned_file_staging_path(
    const std::filesystem::path& target) -> std::filesystem::path;

[[nodiscard]] auto validate_absolute_file_path(
    const std::filesystem::path& path,
    std::string_view label)
    -> std::expected<void, OwnedFileStoreError>;

[[nodiscard]] auto require_plain_directory_tree(
    const std::filesystem::path& path)
    -> std::expected<void, OwnedFileStoreError>;

[[nodiscard]] auto inspect_plain_directory_tree(
    const std::filesystem::path& path)
    -> std::expected<bool, OwnedFileStoreError>;

[[nodiscard]] auto ensure_plain_directory_tree(
    const std::filesystem::path& path)
    -> std::expected<void, OwnedFileStoreError>;

[[nodiscard]] auto measure_plain_file(
    const std::filesystem::path& path)
    -> std::expected<
        std::optional<FileMeasurement>,
        OwnedFileStoreError>;

[[nodiscard]] auto read_plain_file_bytes(
    const std::filesystem::path& path,
    std::size_t maximum_size)
    -> std::expected<
        std::optional<std::vector<std::byte>>,
        OwnedFileStoreError>;

auto delete_plain_file(const std::filesystem::path& path)
    -> std::expected<void, OwnedFileStoreError>;

auto write_new_durable(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes)
    -> std::expected<void, OwnedFileStoreError>;

auto publish_staged_file(
    const std::filesystem::path& staging,
    const std::filesystem::path& target,
    bool replace_existing)
    -> std::expected<void, OwnedFileStoreError>;

auto write_owned_file_receipt(
    const std::filesystem::path& path,
    const OwnedFileReceipt& receipt)
    -> std::expected<void, OwnedFileStoreError>;

[[nodiscard]] auto read_owned_file_receipt(
    const std::filesystem::path& path,
    std::string_view manifest_path,
    FileRole role,
    bool discard_atomic_temporary)
    -> std::expected<
        std::optional<OwnedFileReceipt>,
        OwnedFileStoreError>;
} // namespace meccha::launcher::detail
