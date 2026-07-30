#pragma once

#include <meccha/core/image_compositor.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

namespace meccha::application
{
enum class ImageDecodeError : std::uint8_t
{
    InvalidAssetId,
    UnsupportedMime,
    EncodedSize,
    HeaderMismatch,
    MalformedImage,
    AnimatedImage,
    DimensionLimit,
    AllocationFailure,
    PlatformUnavailable,
    PlatformFailure,
    Cancelled,
};

[[nodiscard]] auto checked_decoded_rgba_bytes(
    std::uint32_t width,
    std::uint32_t height) -> std::optional<std::size_t>;

class ImageSourceDecoder
{
public:
    ImageSourceDecoder() = default;
    ImageSourceDecoder(const ImageSourceDecoder&) = delete;
    auto operator=(const ImageSourceDecoder&)
        -> ImageSourceDecoder& = delete;
    virtual ~ImageSourceDecoder() = default;

    [[nodiscard]] virtual auto decode(
        std::string_view asset_id,
        core::ImageMime mime,
        std::span<const std::byte> encoded,
        std::stop_token cancellation = {})
        -> std::expected<
            core::DecodedImageSource,
            ImageDecodeError> = 0;
};

class NativeImageSourceDecoder final : public ImageSourceDecoder
{
public:
    [[nodiscard]] auto decode(
        std::string_view asset_id,
        core::ImageMime mime,
        std::span<const std::byte> encoded,
        std::stop_token cancellation = {})
        -> std::expected<
            core::DecodedImageSource,
            ImageDecodeError> override;
};
} // namespace meccha::application
