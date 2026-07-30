#pragma once

#include <meccha/launcher/hash.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::launcher
{
enum class FileRole : std::uint8_t
{
    Proxy,
    Override,
    Runtime,
    Mod,
    Config,
    Profile,
    Localization,
    Font,
    License,
};

enum class ManifestErrorCode : std::uint8_t
{
    Json,
    Schema,
    ProductVersion,
    Ue4ssCommit,
    FileCount,
    Path,
    Role,
    Hash,
    TotalSize,
};

struct ManifestError
{
    ManifestErrorCode code{};
    std::string detail{};

    auto operator==(const ManifestError&) const -> bool = default;
};

struct ManifestFile
{
    std::string path{};
    FileRole role{};
    std::uint64_t size{};
    Sha256Digest sha256{};

    auto operator==(const ManifestFile&) const -> bool = default;
};

struct PayloadManifest
{
    std::uint32_t schema_version{};
    std::string product_version{};
    std::string ue4ss_commit{};
    std::vector<ManifestFile> files{};
    std::uint64_t total_size{};

    auto operator==(const PayloadManifest&) const -> bool = default;
};

[[nodiscard]] auto parse_payload_manifest(std::string_view json)
    -> std::expected<PayloadManifest, ManifestError>;
} // namespace meccha::launcher
