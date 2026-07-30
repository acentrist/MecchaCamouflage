#include <meccha/application/image_decoder.hpp>

#ifdef _WIN32
#include "image_decoder_wic.hpp"
#endif

#include <meccha/core/image_project.hpp>

#include <webp/decode.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application
{
namespace
{
constexpr auto BytesPerPixel = std::size_t{4U};

auto lowercase_hex(std::string_view value, std::size_t length) -> bool
{
    return value.size() == length &&
           std::ranges::all_of(
               value,
               [](unsigned char character)
               {
                   return (character >= '0' && character <= '9') ||
                          (character >= 'a' && character <= 'f');
               });
}

auto has_prefix(
    std::span<const std::byte> encoded,
    std::span<const std::uint8_t> prefix) -> bool
{
    if (encoded.size() < prefix.size())
    {
        return false;
    }
    for (auto index = std::size_t{}; index < prefix.size(); ++index)
    {
        if (std::to_integer<std::uint8_t>(encoded[index]) !=
            prefix[index])
        {
            return false;
        }
    }
    return true;
}

auto header_matches(
    core::ImageMime mime,
    std::span<const std::byte> encoded) -> bool
{
    constexpr auto Png = std::array<std::uint8_t, 8U>{
        0x89U,
        0x50U,
        0x4eU,
        0x47U,
        0x0dU,
        0x0aU,
        0x1aU,
        0x0aU,
    };
    constexpr auto Jpeg = std::array<std::uint8_t, 3U>{
        0xffU,
        0xd8U,
        0xffU,
    };
    constexpr auto Riff = std::array<std::uint8_t, 4U>{
        0x52U,
        0x49U,
        0x46U,
        0x46U,
    };
    constexpr auto WebP = std::array<std::uint8_t, 4U>{
        0x57U,
        0x45U,
        0x42U,
        0x50U,
    };
    switch (mime)
    {
    case core::ImageMime::Png:
        return has_prefix(encoded, Png);
    case core::ImageMime::Jpeg:
        return has_prefix(encoded, Jpeg);
    case core::ImageMime::WebP:
        return encoded.size() >= 12U &&
               has_prefix(encoded, Riff) &&
               has_prefix(encoded.subspan(8U), WebP);
    }
    return false;
}

auto decode_webp(
    std::string_view asset_id,
    std::span<const std::byte> encoded,
    std::stop_token cancellation)
    -> std::expected<core::DecodedImageSource, ImageDecodeError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageDecodeError::Cancelled);
    }
    auto configuration = WebPDecoderConfig{};
    if (WebPInitDecoderConfig(&configuration) == 0)
    {
        return std::unexpected(ImageDecodeError::PlatformFailure);
    }
    const auto features = WebPGetFeatures(
        reinterpret_cast<const std::uint8_t*>(encoded.data()),
        encoded.size(),
        &configuration.input);
    if (features != VP8_STATUS_OK ||
        configuration.input.width <= 0 ||
        configuration.input.height <= 0)
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    if (configuration.input.has_animation != 0)
    {
        return std::unexpected(ImageDecodeError::AnimatedImage);
    }
    const auto width =
        static_cast<std::uint32_t>(configuration.input.width);
    const auto height =
        static_cast<std::uint32_t>(configuration.input.height);
    const auto bytes = checked_decoded_rgba_bytes(width, height);
    if (!bytes)
    {
        return std::unexpected(ImageDecodeError::DimensionLimit);
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageDecodeError::Cancelled);
    }

    auto rgba = std::make_shared<std::vector<std::byte>>(*bytes);
    const auto stride =
        static_cast<int>(static_cast<std::size_t>(width) *
                         BytesPerPixel);
    const auto decoded = WebPDecodeRGBAInto(
        reinterpret_cast<const std::uint8_t*>(encoded.data()),
        encoded.size(),
        reinterpret_cast<std::uint8_t*>(rgba->data()),
        rgba->size(),
        stride);
    if (decoded == nullptr)
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageDecodeError::Cancelled);
    }
    return core::DecodedImageSource{
        std::string{asset_id},
        width,
        height,
        std::move(rgba),
    };
}
} // namespace

auto checked_decoded_rgba_bytes(
    std::uint32_t width,
    std::uint32_t height) -> std::optional<std::size_t>
{
    if (width == 0U || height == 0U ||
        width > core::MaximumDecodedImageDimension ||
        height > core::MaximumDecodedImageDimension)
    {
        return std::nullopt;
    }
    const auto wide_width = static_cast<std::uint64_t>(width);
    const auto wide_height = static_cast<std::uint64_t>(height);
    if (wide_width >
        std::numeric_limits<std::uint64_t>::max() / wide_height)
    {
        return std::nullopt;
    }
    const auto pixels = wide_width * wide_height;
    if (pixels >
        std::numeric_limits<std::uint64_t>::max() /
            BytesPerPixel)
    {
        return std::nullopt;
    }
    const auto bytes =
        pixels * static_cast<std::uint64_t>(BytesPerPixel);
    if (bytes > core::MaximumDecodedImageBytes ||
        bytes > std::numeric_limits<std::size_t>::max())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(bytes);
}

auto NativeImageSourceDecoder::decode(
    std::string_view asset_id,
    core::ImageMime mime,
    std::span<const std::byte> encoded,
    std::stop_token cancellation)
    -> std::expected<core::DecodedImageSource, ImageDecodeError>
{
    if (!lowercase_hex(asset_id, 64U))
    {
        return std::unexpected(ImageDecodeError::InvalidAssetId);
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageDecodeError::Cancelled);
    }
    if (mime != core::ImageMime::Png &&
        mime != core::ImageMime::Jpeg &&
        mime != core::ImageMime::WebP)
    {
        return std::unexpected(ImageDecodeError::UnsupportedMime);
    }
    if (encoded.empty() ||
        encoded.size() > core::MaximumImageSourceBytes)
    {
        return std::unexpected(ImageDecodeError::EncodedSize);
    }
    if (!header_matches(mime, encoded))
    {
        return std::unexpected(ImageDecodeError::HeaderMismatch);
    }

    try
    {
        if (mime == core::ImageMime::WebP)
        {
            return decode_webp(asset_id, encoded, cancellation);
        }
#ifdef _WIN32
        return decode_wic_image(
            asset_id,
            mime,
            encoded,
            cancellation);
#else
        return std::unexpected(
            ImageDecodeError::PlatformUnavailable);
#endif
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            ImageDecodeError::AllocationFailure);
    }
    catch (...)
    {
        return std::unexpected(ImageDecodeError::PlatformFailure);
    }
}
} // namespace meccha::application
