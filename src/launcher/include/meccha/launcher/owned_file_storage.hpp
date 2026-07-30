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

    [[nodiscard]] auto removable()
        -> std::expected<bool, OwnedFileStoreError>;

    auto remove_owned()
        -> std::expected<bool, OwnedFileStoreError>;

private:
    std::filesystem::path target_{};
    std::filesystem::path ownership_record_{};
    std::string manifest_path_{};
    FileRole role_{};
};
#endif
} // namespace meccha::launcher
