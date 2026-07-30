#pragma once

#include <meccha/launcher/ownership.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace meccha::launcher
{
enum class OwnedFileInstallResult : std::uint8_t
{
    Reused,
    Created,
    Replaced,
};

enum class OwnedFileStoreErrorCode : std::uint8_t
{
    Conflict,
    InvalidRequest,
    InvalidData,
    Io,
};

struct OwnedFileStoreError
{
    OwnedFileStoreErrorCode code{};
    std::string detail{};

    auto operator==(const OwnedFileStoreError&) const -> bool = default;
};

#ifdef _WIN32
struct OwnedFileExternalInstallIntent
{
    OwnedFileInstallResult result{};
    bool mutation_required{};
    std::optional<FileMeasurement> expected_current{};
    FileMeasurement desired{};

    auto operator==(const OwnedFileExternalInstallIntent&) const
        -> bool = default;
};

struct OwnedFileExternalRemoveIntent
{
    FileMeasurement expected_current{};

    auto operator==(const OwnedFileExternalRemoveIntent&) const
        -> bool = default;
};

class Win32OwnedFileStore
{
public:
    Win32OwnedFileStore(
        std::filesystem::path target,
        std::filesystem::path ownership_record,
        std::string manifest_path,
        FileRole role);

    auto recover() -> std::expected<void, OwnedFileStoreError>;

    [[nodiscard]] auto observe(const OwnedFileExpectation& expected)
        -> std::expected<ArtifactState, OwnedFileStoreError>;

    auto install(
        const OwnedFileExpectation& expected,
        std::span<const std::byte> payload)
        -> std::expected<OwnedFileInstallResult, OwnedFileStoreError>;

    auto prepare_external_install(
        const OwnedFileExpectation& expected)
        -> std::expected<
            OwnedFileExternalInstallIntent,
            OwnedFileStoreError>;

    auto finalize_external_install(
        const OwnedFileExpectation& expected)
        -> std::expected<void, OwnedFileStoreError>;

    [[nodiscard]] auto removable()
        -> std::expected<bool, OwnedFileStoreError>;

    auto remove_owned()
        -> std::expected<bool, OwnedFileStoreError>;

    auto prepare_external_remove()
        -> std::expected<
            std::optional<OwnedFileExternalRemoveIntent>,
            OwnedFileStoreError>;

    auto finalize_external_remove()
        -> std::expected<void, OwnedFileStoreError>;

private:
    std::filesystem::path target_{};
    std::filesystem::path ownership_record_{};
    std::string manifest_path_{};
    FileRole role_{};
};
#endif
} // namespace meccha::launcher
