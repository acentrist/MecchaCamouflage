#include <meccha/application/image_decoder.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;

constexpr auto AssetId = std::string_view{
    "0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef"};

constexpr auto RedWebP = std::array<std::uint8_t, 36U>{
    0x52, 0x49, 0x46, 0x46, 0x1c, 0x00, 0x00, 0x00, 0x57,
    0x45, 0x42, 0x50, 0x56, 0x50, 0x38, 0x4c, 0x0f, 0x00,
    0x00, 0x00, 0x2f, 0x01, 0x00, 0x00, 0x00, 0x07, 0x10,
    0xfd, 0x8f, 0xfe, 0x07, 0x22, 0xa2, 0xff, 0x01, 0x00,
};

#ifdef _WIN32
constexpr auto RedPng = std::array<std::uint8_t, 161U>{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00,
    0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x01, 0x03, 0x00,
    0x00, 0x00, 0xce, 0xec, 0xed, 0xc9, 0x00, 0x00, 0x00,
    0x20, 0x63, 0x48, 0x52, 0x4d, 0x00, 0x00, 0x7a, 0x26,
    0x00, 0x00, 0x80, 0x84, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0x00, 0x80, 0xe8, 0x00, 0x00, 0x75, 0x30, 0x00, 0x00,
    0xea, 0x60, 0x00, 0x00, 0x3a, 0x98, 0x00, 0x00, 0x17,
    0x70, 0x9c, 0xba, 0x51, 0x3c, 0x00, 0x00, 0x00, 0x06,
    0x50, 0x4c, 0x54, 0x45, 0xff, 0x00, 0x00, 0xff, 0xff,
    0xff, 0x41, 0x1d, 0x34, 0x11, 0x00, 0x00, 0x00, 0x01,
    0x62, 0x4b, 0x47, 0x44, 0x01, 0xff, 0x02, 0x2d, 0xde,
    0x00, 0x00, 0x00, 0x07, 0x74, 0x49, 0x4d, 0x45, 0x07,
    0xea, 0x07, 0x1e, 0x0a, 0x2c, 0x38, 0x65, 0x49, 0x38,
    0x1e, 0x00, 0x00, 0x00, 0x0a, 0x49, 0x44, 0x41, 0x54,
    0x08, 0xd7, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x01, 0xe2, 0x21, 0xbc, 0x33, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

constexpr auto RedJpegBase64 = std::string_view{
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAMCAgMCAgMDAwMEAwMEBQgFBQ"
    "QEBQoHBwYIDAoMDAsKCwsNDhIQDQ4RDgsLEBYQERMUFRUVDA8XGBYUGBIU"
    "FRT/2wBDAQMEBAUEBQkFBQkUDQsNFBQUFBQUFBQUFBQUFBQUFBQUFBQUFB"
    "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBT/wAARCAABAAIDASIAAhEB"
    "AxEB"
    "/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAw"
    "IEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAk"
    "M2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZW"
    "ZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2"
    "t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6"
    "/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBA"
    "QDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAV"
    "YnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZG"
    "VmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0"
    "tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6"
    "/9oADAMBAAIRAxEAPwD50ooor8MP9Uz/2Q=="};

auto decode_base64(std::string_view text) -> std::vector<std::uint8_t>
{
    constexpr auto Alphabet = std::string_view{
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        "0123456789+/"};
    auto bytes = std::vector<std::uint8_t>{};
    bytes.reserve(text.size() * 3U / 4U);
    auto accumulator = std::uint32_t{};
    auto bits = 0;
    for (const auto character : text)
    {
        if (character == '=')
        {
            break;
        }
        const auto index = Alphabet.find(character);
        if (index == std::string_view::npos)
        {
            return {};
        }
        accumulator =
            (accumulator << 6U) | static_cast<std::uint32_t>(index);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            bytes.push_back(static_cast<std::uint8_t>(
                accumulator >> static_cast<unsigned int>(bits)));
        }
    }
    return bytes;
}
#endif

auto as_bytes(std::span<const std::uint8_t> bytes)
    -> std::span<const std::byte>
{
    return std::as_bytes(bytes);
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_decoder: " << message << '\n';
    }
    return condition;
}

auto red_pixels(const core::DecodedImageSource& image) -> bool
{
    if (image.width != 2U || image.height != 1U || !image.rgba ||
        image.rgba->size() != 8U)
    {
        return false;
    }
    for (auto offset = std::size_t{}; offset < 8U; offset += 4U)
    {
        const auto red =
            std::to_integer<std::uint8_t>((*image.rgba)[offset]);
        const auto green =
            std::to_integer<std::uint8_t>((*image.rgba)[offset + 1U]);
        const auto blue =
            std::to_integer<std::uint8_t>((*image.rgba)[offset + 2U]);
        const auto alpha =
            std::to_integer<std::uint8_t>((*image.rgba)[offset + 3U]);
        if (red < 240U || green > 16U || blue > 16U ||
            alpha != 255U)
        {
            return false;
        }
    }
    return true;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    passed &= expect(
        checked_decoded_rgba_bytes(1U, 1U) == 4U &&
            checked_decoded_rgba_bytes(8192U, 2048U) ==
                core::MaximumDecodedImageBytes &&
            !checked_decoded_rgba_bytes(0U, 1U) &&
            !checked_decoded_rgba_bytes(8193U, 1U) &&
            !checked_decoded_rgba_bytes(8192U, 2049U),
        "decoded pixel bounds are not checked before allocation");

    auto decoder = NativeImageSourceDecoder{};
    const auto webp = decoder.decode(
        AssetId,
        core::ImageMime::WebP,
        as_bytes(RedWebP));
    passed &= expect(
        webp && webp->asset_id == AssetId && red_pixels(*webp),
        "the pinned WebP decoder did not produce bounded RGBA");

    auto damaged_webp = RedWebP;
    damaged_webp[0U] = 0U;
    const auto damaged = decoder.decode(
        AssetId,
        core::ImageMime::WebP,
        as_bytes(damaged_webp));
    passed &= expect(
        !damaged &&
            damaged.error() == ImageDecodeError::HeaderMismatch,
        "a malformed WebP signature was not rejected before decode");

    const auto disguised = decoder.decode(
        AssetId,
        core::ImageMime::Png,
        as_bytes(RedWebP));
    passed &= expect(
        !disguised &&
            disguised.error() == ImageDecodeError::HeaderMismatch,
        "declared MIME bypassed encoded-container validation");

    const auto truncated = decoder.decode(
        AssetId,
        core::ImageMime::WebP,
        as_bytes(std::span{RedWebP}.first(12U)));
    passed &= expect(
        !truncated &&
            truncated.error() == ImageDecodeError::MalformedImage,
        "a truncated WebP container reached allocation");

    const auto empty = decoder.decode(
        AssetId,
        core::ImageMime::WebP,
        {});
    passed &= expect(
        !empty && empty.error() == ImageDecodeError::EncodedSize,
        "empty encoded input was accepted");

    auto oversized = std::vector<std::byte>(
        core::MaximumImageSourceBytes + 1U,
        std::byte{});
    const auto too_large = decoder.decode(
        AssetId,
        core::ImageMime::WebP,
        oversized);
    passed &= expect(
        !too_large &&
            too_large.error() == ImageDecodeError::EncodedSize,
        "an oversized encoded source reached header parsing");

    const auto invalid_asset = decoder.decode(
        "not-content-addressed",
        core::ImageMime::WebP,
        as_bytes(RedWebP));
    passed &= expect(
        !invalid_asset &&
            invalid_asset.error() ==
                ImageDecodeError::InvalidAssetId,
        "a decoded source accepted an invalid content identity");

#ifdef _WIN32
    const auto png = decoder.decode(
        AssetId,
        core::ImageMime::Png,
        as_bytes(RedPng));
    passed &= expect(
        png && red_pixels(*png),
        "WIC PNG decode did not produce RGBA");

    const auto red_jpeg = decode_base64(RedJpegBase64);
    const auto jpeg = decoder.decode(
        AssetId,
        core::ImageMime::Jpeg,
        as_bytes(red_jpeg));
    passed &= expect(
        jpeg && red_pixels(*jpeg),
        "WIC JPEG decode did not produce opaque RGBA");
#endif

    if (passed)
    {
        std::cout << "PASS image_decoder\n";
    }
    return passed ? 0 : 1;
}
