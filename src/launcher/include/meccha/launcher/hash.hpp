#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace meccha::launcher
{
struct Sha256Digest
{
    std::array<std::byte, 32> bytes{};

    auto operator==(const Sha256Digest&) const -> bool = default;
};

enum class HashErrorCode : std::uint8_t
{
    PlatformUnsupported,
    Provider,
    AlgorithmProperty,
    HashObject,
    Update,
    Finish,
    DigestSize,
    FileOpen,
    FileRead,
    FileSize,
};

struct HashError
{
    HashErrorCode code{};
    std::int32_t native_status{};
    std::string detail{};

    auto operator==(const HashError&) const -> bool = default;
};

struct FileHash
{
    std::uint64_t size{};
    Sha256Digest sha256{};

    auto operator==(const FileHash&) const -> bool = default;
};

[[nodiscard]] auto sha256_bytes(std::span<const std::byte> bytes)
    -> std::expected<Sha256Digest, HashError>;

[[nodiscard]] auto sha256_file(const std::filesystem::path& path)
    -> std::expected<FileHash, HashError>;

[[nodiscard]] auto sha256_hex(const Sha256Digest& digest) -> std::string;

[[nodiscard]] auto parse_sha256_hex(std::string_view value)
    -> std::optional<Sha256Digest>;
} // namespace meccha::launcher
