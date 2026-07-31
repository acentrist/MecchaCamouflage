#include <meccha/core/png_encoder.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL png_encoder: " << message << '\n';
    }
    return condition;
}

auto has_png_signature(std::span<const std::byte> bytes) -> bool
{
    constexpr auto signature = std::array{
        std::byte{0x89},
        std::byte{'P'},
        std::byte{'N'},
        std::byte{'G'},
        std::byte{0x0D},
        std::byte{0x0A},
        std::byte{0x1A},
        std::byte{0x0A},
    };
    return bytes.size() >= signature.size() &&
           std::equal(signature.begin(), signature.end(), bytes.begin());
}

auto byte(std::byte value) -> std::uint8_t
{
    return std::to_integer<std::uint8_t>(value);
}

auto append_be32(
    std::vector<std::byte>& output,
    std::uint32_t value) -> void
{
    output.push_back(static_cast<std::byte>(value >> 24U));
    output.push_back(static_cast<std::byte>(value >> 16U));
    output.push_back(static_cast<std::byte>(value >> 8U));
    output.push_back(static_cast<std::byte>(value));
}

auto crc32(std::span<const std::byte> bytes) -> std::uint32_t
{
    auto crc = std::uint32_t{0xFFFFFFFFU};
    for (const auto value : bytes)
    {
        crc ^= byte(value);
        for (auto bit = 0U; bit < 8U; ++bit)
        {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

auto append_chunk(
    std::vector<std::byte>& output,
    std::array<std::byte, 4U> type,
    std::span<const std::byte> data) -> void
{
    append_be32(output, static_cast<std::uint32_t>(data.size()));
    const auto begin = output.size();
    output.insert(output.end(), type.begin(), type.end());
    output.insert(output.end(), data.begin(), data.end());
    append_be32(
        output,
        crc32(std::span<const std::byte>{output}.subspan(
            begin,
            type.size() + data.size())));
}

auto read_be32(std::span<const std::byte> bytes) -> std::uint32_t
{
    return (static_cast<std::uint32_t>(byte(bytes[0U])) << 24U) |
           (static_cast<std::uint32_t>(byte(bytes[1U])) << 16U) |
           (static_cast<std::uint32_t>(byte(bytes[2U])) << 8U) |
           static_cast<std::uint32_t>(byte(bytes[3U]));
}

auto with_split_idat(
    std::span<const std::byte> canonical,
    bool interleave_ancillary) -> std::vector<std::byte>
{
    constexpr auto SignatureBytes = std::size_t{8U};
    constexpr auto IhdrChunkBytes = std::size_t{25U};
    const auto idat_offset = SignatureBytes + IhdrChunkBytes;
    const auto idat_size = static_cast<std::size_t>(
        read_be32(canonical.subspan(idat_offset, 4U)));
    const auto idat_data = canonical.subspan(
        idat_offset + 8U,
        idat_size);
    const auto midpoint = idat_data.size() / 2U;

    auto result = std::vector<std::byte>{};
    result.insert(
        result.end(),
        canonical.begin(),
        canonical.begin() +
            static_cast<std::ptrdiff_t>(idat_offset));
    constexpr auto Idat = std::array{
        std::byte{'I'}, std::byte{'D'}, std::byte{'A'}, std::byte{'T'}};
    append_chunk(result, Idat, idat_data.first(midpoint));
    if (interleave_ancillary)
    {
        constexpr auto Text = std::array{
            std::byte{'t'}, std::byte{'E'}, std::byte{'X'}, std::byte{'t'}};
        append_chunk(result, Text, {});
    }
    append_chunk(result, Idat, idat_data.subspan(midpoint));
    result.insert(
        result.end(),
        canonical.begin() + static_cast<std::ptrdiff_t>(
            idat_offset + 12U + idat_size),
        canonical.end());
    return result;
}

auto with_chunk_before_idat(
    std::span<const std::byte> canonical,
    std::array<std::byte, 4U> type)
    -> std::vector<std::byte>
{
    constexpr auto IdatOffset = std::size_t{33U};
    auto result = std::vector<std::byte>{};
    result.insert(
        result.end(),
        canonical.begin(),
        canonical.begin() +
            static_cast<std::ptrdiff_t>(IdatOffset));
    append_chunk(result, type, {});
    result.insert(
        result.end(),
        canonical.begin() +
            static_cast<std::ptrdiff_t>(IdatOffset),
        canonical.end());
    return result;
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    const auto rgba = std::array{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
        std::byte{0x55},
        std::byte{0x66},
        std::byte{0x77},
        std::byte{0x88},
    };

    const auto encoded = encode_png_rgba8(2U, 1U, rgba);
    passed &= expect(
        encoded && has_png_signature(*encoded),
        "a valid RGBA image did not produce a PNG");
    const auto inspected =
        encoded ? inspect_canonical_png_rgba8(*encoded)
                : std::expected<PngImageInfo, PngEncodeError>{
                      std::unexpected(
                          PngEncodeError::InvalidEncoding)};
    passed &= expect(
        inspected && inspected->width == 2U &&
            inspected->height == 1U &&
            inspected->rgba_bytes == rgba.size(),
        "the canonical PNG metadata did not round-trip");
    const auto general =
        encoded ? inspect_png_rgba8(*encoded)
                : std::expected<PngImageInfo, PngEncodeError>{
                      std::unexpected(
                          PngEncodeError::InvalidEncoding)};
    passed &= expect(
        general == inspected,
        "the general RGBA8 PNG inspector rejected canonical output");

    const auto split = encoded
                           ? with_split_idat(*encoded, false)
                           : std::vector<std::byte>{};
    passed &= expect(
        inspect_png_rgba8(split) == inspected &&
            inspect_canonical_png_rgba8(split) ==
                std::unexpected(PngEncodeError::InvalidEncoding),
        "contiguous multi-IDAT PNG handling drifted");
    const auto interleaved = encoded
                                 ? with_split_idat(*encoded, true)
                                 : std::vector<std::byte>{};
    passed &= expect(
        inspect_png_rgba8(interleaved) ==
            std::unexpected(PngEncodeError::InvalidEncoding),
        "a non-contiguous IDAT sequence was accepted");
    const auto ancillary = encoded
                               ? with_chunk_before_idat(
                                     *encoded,
                                     {std::byte{'t'},
                                      std::byte{'E'},
                                      std::byte{'X'},
                                      std::byte{'t'}})
                               : std::vector<std::byte>{};
    passed &= expect(
        inspect_png_rgba8(ancillary) == inspected,
        "a valid ancillary PNG chunk was rejected");
    const auto unknown_critical = encoded
                                      ? with_chunk_before_idat(
                                            *encoded,
                                            {std::byte{'A'},
                                             std::byte{'B'},
                                             std::byte{'C'},
                                             std::byte{'D'}})
                                      : std::vector<std::byte>{};
    passed &= expect(
        inspect_png_rgba8(unknown_critical) ==
            std::unexpected(PngEncodeError::InvalidEncoding),
        "an unknown critical PNG chunk was accepted");

    const auto repeated = encode_png_rgba8(2U, 1U, rgba);
    passed &= expect(
        repeated && encoded && *repeated == *encoded,
        "encoding the same pixels was not deterministic");

    auto corrupt = encoded.value_or(std::vector<std::byte>{});
    if (corrupt.size() > 32U)
    {
        corrupt[32U] ^= std::byte{0x01};
    }
    passed &= expect(
        inspect_canonical_png_rgba8(corrupt) ==
            std::unexpected(PngEncodeError::InvalidEncoding),
        "a corrupt chunk CRC was accepted");

    passed &= expect(
        encode_png_rgba8(0U, 1U, {}) ==
            std::unexpected(PngEncodeError::InvalidDimensions),
        "zero-width PNG input was accepted");
    passed &= expect(
        encode_png_rgba8(
            2U,
            1U,
            std::span<const std::byte>{rgba}.first(7U)) ==
            std::unexpected(PngEncodeError::InvalidPixels),
        "a mismatched RGBA byte count was accepted");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        encode_png_rgba8(
            2U,
            1U,
            rgba,
            cancelled.get_token()) ==
            std::unexpected(PngEncodeError::Cancelled),
        "pre-cancelled PNG work was executed");

    if (passed)
    {
        std::cout << "PASS png_encoder\n";
        return 0;
    }
    return 1;
}
