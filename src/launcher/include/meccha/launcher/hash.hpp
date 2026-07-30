#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

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
};

struct HashError
{
    HashErrorCode code{};
    std::int32_t native_status{};
    std::string detail{};

    auto operator==(const HashError&) const -> bool = default;
};

[[nodiscard]] auto sha256_bytes(std::span<const std::byte> bytes)
    -> std::expected<Sha256Digest, HashError>;

[[nodiscard]] auto sha256_hex(const Sha256Digest& digest) -> std::string;
} // namespace meccha::launcher
