#pragma once

#include <meccha/application/image_decoder.hpp>

#include <expected>
#include <span>
#include <stop_token>
#include <string_view>

namespace meccha::application
{
[[nodiscard]] auto decode_wic_image(
    std::string_view asset_id,
    core::ImageMime mime,
    std::span<const std::byte> encoded,
    std::stop_token cancellation)
    -> std::expected<core::DecodedImageSource, ImageDecodeError>;
} // namespace meccha::application
