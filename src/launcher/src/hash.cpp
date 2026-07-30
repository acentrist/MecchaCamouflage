#include <meccha/launcher/hash.hpp>

#include <array>

namespace meccha::launcher
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
#endif
} // namespace meccha::launcher
