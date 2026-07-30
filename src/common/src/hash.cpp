#include <meccha/common/hash.hpp>

#include <array>
#include <optional>

namespace meccha::common
{
auto sha256_hex(const Sha256Digest& digest) -> std::string
{
    constexpr std::array<char, 16> HexDigits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };

    std::string result(digest.bytes.size() * 2, '\0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index)
    {
        const auto value = std::to_integer<unsigned int>(digest.bytes[index]);
        result[index * 2] = HexDigits[value >> 4U];
        result[index * 2 + 1] = HexDigits[value & 0x0FU];
    }
    return result;
}

auto parse_sha256_hex(std::string_view value)
    -> std::optional<Sha256Digest>
{
    if (value.size() != 64)
    {
        return std::nullopt;
    }

    const auto nibble = [](char character) -> std::optional<std::byte> {
        if (character >= '0' && character <= '9')
        {
            return static_cast<std::byte>(character - '0');
        }
        if (character >= 'a' && character <= 'f')
        {
            return static_cast<std::byte>(character - 'a' + 10);
        }
        return std::nullopt;
    };

    Sha256Digest digest{};
    for (std::size_t index = 0; index < digest.bytes.size(); ++index)
    {
        const auto high = nibble(value[index * 2]);
        const auto low = nibble(value[index * 2 + 1]);
        if (!high || !low)
        {
            return std::nullopt;
        }
        digest.bytes[index] = (*high << 4) | *low;
    }
    return digest;
}

#ifndef _WIN32
auto sha256_bytes(std::span<const std::byte>)
    -> std::expected<Sha256Digest, HashError>
{
    return std::unexpected(HashError{
        HashErrorCode::PlatformUnsupported,
        0,
        "Payload hashing is available only in the native Windows launcher.",
    });
}

auto sha256_file(const std::filesystem::path&)
    -> std::expected<FileHash, HashError>
{
    return std::unexpected(HashError{
        HashErrorCode::PlatformUnsupported,
        0,
        "Payload hashing is available only in the native Windows launcher.",
    });
}
#endif
} // namespace meccha::common
