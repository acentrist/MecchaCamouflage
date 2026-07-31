#include <meccha/core/png_encoder.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto PngSignature = std::array{
    std::byte{0x89},
    std::byte{'P'},
    std::byte{'N'},
    std::byte{'G'},
    std::byte{0x0D},
    std::byte{0x0A},
    std::byte{0x1A},
    std::byte{0x0A},
};
constexpr auto Ihdr = std::array{
    std::byte{'I'},
    std::byte{'H'},
    std::byte{'D'},
    std::byte{'R'},
};
constexpr auto Idat = std::array{
    std::byte{'I'},
    std::byte{'D'},
    std::byte{'A'},
    std::byte{'T'},
};
constexpr auto Iend = std::array{
    std::byte{'I'},
    std::byte{'E'},
    std::byte{'N'},
    std::byte{'D'},
};
constexpr auto MaximumDeflateStoreBlock = std::size_t{65'535U};
constexpr auto AdlerModulus = std::uint32_t{65'521U};

auto byte(std::byte value) -> std::uint8_t
{
    return std::to_integer<std::uint8_t>(value);
}

auto checked_rgba_bytes(
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<std::uint64_t, PngEncodeError>
{
    if (width == 0U || height == 0U ||
        width > MaximumCanonicalPngDimension ||
        height > MaximumCanonicalPngDimension)
    {
        return std::unexpected(
            PngEncodeError::InvalidDimensions);
    }
    const auto pixels =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height);
    if (pixels >
        std::numeric_limits<std::uint64_t>::max() / 4U)
    {
        return std::unexpected(PngEncodeError::ResourceLimit);
    }
    const auto bytes = pixels * 4U;
    if (bytes > MaximumCanonicalPngRgbaBytes)
    {
        return std::unexpected(PngEncodeError::ResourceLimit);
    }
    return bytes;
}

auto append_be32(
    std::vector<std::byte>& output,
    std::uint32_t value) -> void
{
    output.push_back(
        static_cast<std::byte>((value >> 24U) & 0xFFU));
    output.push_back(
        static_cast<std::byte>((value >> 16U) & 0xFFU));
    output.push_back(
        static_cast<std::byte>((value >> 8U) & 0xFFU));
    output.push_back(static_cast<std::byte>(value & 0xFFU));
}

auto read_be32(std::span<const std::byte> bytes)
    -> std::uint32_t
{
    return (static_cast<std::uint32_t>(byte(bytes[0U])) << 24U) |
           (static_cast<std::uint32_t>(byte(bytes[1U])) << 16U) |
           (static_cast<std::uint32_t>(byte(bytes[2U])) << 8U) |
           static_cast<std::uint32_t>(byte(bytes[3U]));
}

auto crc32(std::span<const std::byte> bytes) -> std::uint32_t
{
    auto crc = std::uint32_t{0xFFFFFFFFU};
    for (const auto value : bytes)
    {
        crc ^= byte(value);
        for (auto bit = 0U; bit < 8U; ++bit)
        {
            const auto mask =
                static_cast<std::uint32_t>(
                    -static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

auto append_chunk(
    std::vector<std::byte>& output,
    const std::array<std::byte, 4U>& type,
    std::span<const std::byte> data) -> bool
{
    if (data.size() >
        std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    append_be32(
        output,
        static_cast<std::uint32_t>(data.size()));
    const auto crc_begin = output.size();
    output.insert(output.end(), type.begin(), type.end());
    output.insert(output.end(), data.begin(), data.end());
    append_be32(
        output,
        crc32(std::span<const std::byte>{output}.subspan(
            crc_begin,
            type.size() + data.size())));
    return true;
}

struct Chunk
{
    std::array<std::byte, 4U> type{};
    std::span<const std::byte> data{};
    std::size_t next{};
};

auto read_chunk(
    std::span<const std::byte> encoded,
    std::size_t offset)
    -> std::expected<Chunk, PngEncodeError>
{
    if (offset > encoded.size() ||
        encoded.size() - offset < 12U)
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }
    const auto length = static_cast<std::size_t>(
        read_be32(encoded.subspan(offset, 4U)));
    if (length > encoded.size() - offset - 12U)
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }
    auto result = Chunk{};
    std::copy_n(
        encoded.begin() +
            static_cast<std::ptrdiff_t>(offset + 4U),
        4U,
        result.type.begin());
    for (auto index = std::size_t{};
         index < result.type.size();
         ++index)
    {
        const auto character = byte(result.type[index]);
        const auto letter =
            (character >= static_cast<std::uint8_t>('A') &&
             character <= static_cast<std::uint8_t>('Z')) ||
            (character >= static_cast<std::uint8_t>('a') &&
             character <= static_cast<std::uint8_t>('z'));
        if (!letter ||
            (index == 2U && (character & 0x20U) != 0U))
        {
            return std::unexpected(
                PngEncodeError::InvalidEncoding);
        }
    }
    result.data = encoded.subspan(offset + 8U, length);
    result.next = offset + 12U + length;
    const auto stored_crc =
        read_be32(encoded.subspan(offset + 8U + length, 4U));
    const auto actual_crc = crc32(
        encoded.subspan(offset + 4U, 4U + length));
    if (stored_crc != actual_crc)
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }
    return result;
}

auto validate_deflate_store(
    std::span<const std::byte> idat,
    std::uint64_t expected_raw_bytes,
    std::uint64_t scanline_bytes) -> bool
{
    if (idat.size() < 2U + 1U + 2U + 2U + 4U ||
        idat[0U] != std::byte{0x78} ||
        idat[1U] != std::byte{0x01})
    {
        return false;
    }

    auto offset = std::size_t{2U};
    auto raw_offset = std::uint64_t{};
    auto adler_a = std::uint32_t{1U};
    auto adler_b = std::uint32_t{};
    auto final = false;
    while (!final)
    {
        if (offset > idat.size() ||
            idat.size() - offset < 5U + 4U)
        {
            return false;
        }
        const auto header = byte(idat[offset++]);
        if (header != 0U && header != 1U)
        {
            return false;
        }
        final = header == 1U;
        const auto length =
            static_cast<std::uint16_t>(
                byte(idat[offset])) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(
                    byte(idat[offset + 1U]))
                << 8U);
        const auto inverse =
            static_cast<std::uint16_t>(
                byte(idat[offset + 2U])) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(
                    byte(idat[offset + 3U]))
                << 8U);
        offset += 4U;
        if (static_cast<std::uint16_t>(~length) != inverse ||
            length == 0U ||
            static_cast<std::size_t>(length) >
                idat.size() - offset - 4U)
        {
            return false;
        }
        for (auto index = std::size_t{};
             index < static_cast<std::size_t>(length);
             ++index)
        {
            const auto value = byte(idat[offset + index]);
            if (scanline_bytes == 0U ||
                (raw_offset % scanline_bytes == 0U &&
                 value != 0U))
            {
                return false;
            }
            adler_a =
                (adler_a + value) % AdlerModulus;
            adler_b =
                (adler_b + adler_a) % AdlerModulus;
            ++raw_offset;
        }
        offset += length;
    }
    if (raw_offset != expected_raw_bytes ||
        offset + 4U != idat.size())
    {
        return false;
    }
    const auto expected_adler =
        (adler_b << 16U) | adler_a;
    return read_be32(idat.subspan(offset, 4U)) ==
           expected_adler;
}
} // namespace

auto encode_png_rgba8(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::byte> rgba,
    std::stop_token cancellation)
    -> std::expected<std::vector<std::byte>, PngEncodeError>
{
    const auto expected = checked_rgba_bytes(width, height);
    if (!expected)
    {
        return std::unexpected(expected.error());
    }
    if (rgba.size() != *expected)
    {
        return std::unexpected(PngEncodeError::InvalidPixels);
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(PngEncodeError::Cancelled);
    }

    const auto scanline_bytes =
        static_cast<std::uint64_t>(width) * 4U + 1U;
    const auto raw_bytes =
        scanline_bytes * static_cast<std::uint64_t>(height);
    const auto block_count =
        (raw_bytes + MaximumDeflateStoreBlock - 1U) /
        MaximumDeflateStoreBlock;
    const auto idat_bytes =
        2U + raw_bytes + block_count * 5U + 4U;
    const auto total_bytes =
        PngSignature.size() + 25U + 12U + idat_bytes + 12U;
    if (total_bytes > MaximumCanonicalPngEncodedBytes ||
        total_bytes > std::numeric_limits<std::size_t>::max())
    {
        return std::unexpected(PngEncodeError::ResourceLimit);
    }

    auto ihdr = std::vector<std::byte>{};
    ihdr.reserve(13U);
    append_be32(ihdr, width);
    append_be32(ihdr, height);
    ihdr.push_back(std::byte{8U});
    ihdr.push_back(std::byte{6U});
    ihdr.push_back(std::byte{0U});
    ihdr.push_back(std::byte{0U});
    ihdr.push_back(std::byte{0U});

    auto idat = std::vector<std::byte>{};
    idat.reserve(static_cast<std::size_t>(idat_bytes));
    idat.push_back(std::byte{0x78});
    idat.push_back(std::byte{0x01});
    auto block = std::vector<std::byte>{};
    block.reserve(MaximumDeflateStoreBlock);
    auto adler_a = std::uint32_t{1U};
    auto adler_b = std::uint32_t{};
    const auto flush =
        [&](bool final)
        {
            idat.push_back(
                final ? std::byte{1U} : std::byte{0U});
            const auto length =
                static_cast<std::uint16_t>(block.size());
            const auto inverse =
                static_cast<std::uint16_t>(~length);
            idat.push_back(
                static_cast<std::byte>(length & 0xFFU));
            idat.push_back(
                static_cast<std::byte>((length >> 8U) & 0xFFU));
            idat.push_back(
                static_cast<std::byte>(inverse & 0xFFU));
            idat.push_back(
                static_cast<std::byte>((inverse >> 8U) & 0xFFU));
            idat.insert(idat.end(), block.begin(), block.end());
            block.clear();
        };
    const auto append_raw =
        [&](std::byte value)
        {
            if (block.size() == MaximumDeflateStoreBlock)
            {
                flush(false);
            }
            block.push_back(value);
            adler_a =
                (adler_a + byte(value)) % AdlerModulus;
            adler_b =
                (adler_b + adler_a) % AdlerModulus;
        };

    const auto row_bytes = static_cast<std::size_t>(width) * 4U;
    for (auto row = std::uint32_t{}; row < height; ++row)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(PngEncodeError::Cancelled);
        }
        append_raw(std::byte{0U});
        const auto begin = static_cast<std::size_t>(row) * row_bytes;
        for (const auto value :
             rgba.subspan(begin, row_bytes))
        {
            append_raw(value);
        }
    }
    flush(true);
    append_be32(idat, (adler_b << 16U) | adler_a);

    auto output = std::vector<std::byte>{};
    output.reserve(static_cast<std::size_t>(total_bytes));
    output.insert(
        output.end(),
        PngSignature.begin(),
        PngSignature.end());
    if (!append_chunk(output, Ihdr, ihdr) ||
        !append_chunk(output, Idat, idat) ||
        !append_chunk(output, Iend, {}))
    {
        return std::unexpected(PngEncodeError::ResourceLimit);
    }
    return output;
}

auto inspect_canonical_png_rgba8(
    std::span<const std::byte> encoded)
    -> std::expected<PngImageInfo, PngEncodeError>
{
    if (encoded.size() > MaximumCanonicalPngEncodedBytes ||
        encoded.size() < PngSignature.size() + 49U ||
        !std::equal(
            PngSignature.begin(),
            PngSignature.end(),
            encoded.begin()))
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }
    auto offset = PngSignature.size();
    const auto ihdr = read_chunk(encoded, offset);
    if (!ihdr || ihdr->type != Ihdr ||
        ihdr->data.size() != 13U)
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }
    const auto width = read_be32(ihdr->data.first(4U));
    const auto height =
        read_be32(ihdr->data.subspan(4U, 4U));
    const auto rgba_bytes = checked_rgba_bytes(width, height);
    if (!rgba_bytes ||
        ihdr->data[8U] != std::byte{8U} ||
        ihdr->data[9U] != std::byte{6U} ||
        ihdr->data[10U] != std::byte{0U} ||
        ihdr->data[11U] != std::byte{0U} ||
        ihdr->data[12U] != std::byte{0U})
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }

    offset = ihdr->next;
    const auto idat = read_chunk(encoded, offset);
    if (!idat || idat->type != Idat || idat->data.empty())
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }
    const auto scanline_bytes =
        static_cast<std::uint64_t>(width) * 4U + 1U;
    const auto raw_bytes =
        scanline_bytes * static_cast<std::uint64_t>(height);
    if (!validate_deflate_store(
            idat->data,
            raw_bytes,
            scanline_bytes))
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }

    offset = idat->next;
    const auto iend = read_chunk(encoded, offset);
    if (!iend || iend->type != Iend || !iend->data.empty() ||
        iend->next != encoded.size())
    {
        return std::unexpected(
            PngEncodeError::InvalidEncoding);
    }
    return PngImageInfo{width, height, *rgba_bytes};
}

auto inspect_png_rgba8(
    std::span<const std::byte> encoded)
    -> std::expected<PngImageInfo, PngEncodeError>
{
    if (encoded.size() > MaximumCanonicalPngEncodedBytes ||
        encoded.size() < PngSignature.size() + 49U ||
        !std::equal(
            PngSignature.begin(),
            PngSignature.end(),
            encoded.begin()))
    {
        return std::unexpected(PngEncodeError::InvalidEncoding);
    }

    auto offset = PngSignature.size();
    const auto ihdr = read_chunk(encoded, offset);
    if (!ihdr || ihdr->type != Ihdr ||
        ihdr->data.size() != 13U)
    {
        return std::unexpected(PngEncodeError::InvalidEncoding);
    }
    const auto width = read_be32(ihdr->data.first(4U));
    const auto height = read_be32(ihdr->data.subspan(4U, 4U));
    const auto rgba_bytes = checked_rgba_bytes(width, height);
    if (!rgba_bytes || ihdr->data[8U] != std::byte{8U} ||
        ihdr->data[9U] != std::byte{6U} ||
        ihdr->data[10U] != std::byte{0U} ||
        ihdr->data[11U] != std::byte{0U} ||
        ihdr->data[12U] != std::byte{0U})
    {
        return std::unexpected(PngEncodeError::InvalidEncoding);
    }

    offset = ihdr->next;
    auto saw_idat = false;
    auto closed_idat = false;
    auto idat_bytes = std::size_t{};
    while (offset < encoded.size())
    {
        const auto chunk = read_chunk(encoded, offset);
        if (!chunk || chunk->type == Ihdr)
        {
            return std::unexpected(PngEncodeError::InvalidEncoding);
        }
        if (chunk->type == Idat)
        {
            if (closed_idat ||
                chunk->data.size() >
                    encoded.size() - idat_bytes)
            {
                return std::unexpected(
                    PngEncodeError::InvalidEncoding);
            }
            saw_idat = true;
            idat_bytes += chunk->data.size();
            offset = chunk->next;
            continue;
        }
        if (saw_idat)
        {
            closed_idat = true;
        }
        if (chunk->type == Iend)
        {
            if (!saw_idat || idat_bytes == 0U ||
                !chunk->data.empty() ||
                chunk->next != encoded.size())
            {
                return std::unexpected(
                    PngEncodeError::InvalidEncoding);
            }
            return PngImageInfo{width, height, *rgba_bytes};
        }
        if ((byte(chunk->type[0U]) & 0x20U) == 0U)
        {
            return std::unexpected(PngEncodeError::InvalidEncoding);
        }
        offset = chunk->next;
    }
    return std::unexpected(PngEncodeError::InvalidEncoding);
}
} // namespace meccha::core
