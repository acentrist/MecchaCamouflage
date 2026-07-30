#pragma once

#include <meccha/launcher/transaction.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::launcher
{
struct RuntimePayloadError
{
    std::string detail{};

    auto operator==(const RuntimePayloadError&) const -> bool = default;
};

class RuntimePayloadSource
{
public:
    RuntimePayloadSource() = default;
    RuntimePayloadSource(const RuntimePayloadSource&) = delete;
    auto operator=(const RuntimePayloadSource&) -> RuntimePayloadSource& = delete;
    RuntimePayloadSource(RuntimePayloadSource&&) = default;
    auto operator=(RuntimePayloadSource&&) -> RuntimePayloadSource& = default;
    virtual ~RuntimePayloadSource() = default;

    virtual auto read_file(std::string_view relative_path)
        -> std::expected<std::vector<std::byte>, RuntimePayloadError> = 0;
};

class Win32RuntimeStorage final : public RuntimeStorage
{
public:
    Win32RuntimeStorage(
        std::filesystem::path root,
        std::string payload_manifest_json,
        Sha256Digest payload_manifest_sha256,
        RuntimePayloadSource& payload_source);

    auto identify_generation(std::string_view name)
        -> std::expected<GenerationIdentity, RuntimeStorageError> override;

    auto list_staging_generations()
        -> std::expected<std::vector<std::string>, RuntimeStorageError> override;

    auto read_journal()
        -> std::expected<
            std::optional<RuntimeTransactionJournal>,
            RuntimeStorageError> override;

    auto stage_generation(
        std::string_view name,
        const Sha256Digest& manifest_sha256)
        -> std::expected<void, RuntimeStorageError> override;

    auto write_journal(const RuntimeTransactionJournal& journal)
        -> std::expected<void, RuntimeStorageError> override;

    auto rename_generation(
        std::string_view from,
        std::string_view to) -> std::expected<void, RuntimeStorageError> override;

    auto remove_generation(
        std::string_view name,
        const Sha256Digest& expected_manifest_sha256)
        -> std::expected<void, RuntimeStorageError> override;

    auto remove_journal()
        -> std::expected<void, RuntimeStorageError> override;

private:
    std::filesystem::path root_{};
    std::string payload_manifest_json_{};
    Sha256Digest payload_manifest_sha256_{};
    RuntimePayloadSource& payload_source_;
};
} // namespace meccha::launcher
