#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
inline constexpr std::uint32_t MaximumCanonicalPngDimension =
    8192U;
inline constexpr std::uint64_t MaximumCanonicalPngRgbaBytes =
    64U * 1024U * 1024U;
inline constexpr std::uint64_t MaximumCanonicalPngEncodedBytes =
    65U * 1024U * 1024U;

enum class PngEncodeError : std::uint8_t
{
    InvalidDimensions,
    InvalidPixels,
    InvalidEncoding,
    ResourceLimit,
    Cancelled,
};

struct PngImageInfo
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t rgba_bytes{};

    auto operator==(const PngImageInfo&) const -> bool = default;
};

[[nodiscard]] auto encode_png_rgba8(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::byte> rgba,
    std::stop_token cancellation = {})
    -> std::expected<std::vector<std::byte>, PngEncodeError>;

[[nodiscard]] auto inspect_canonical_png_rgba8(
    std::span<const std::byte> encoded)
    -> std::expected<PngImageInfo, PngEncodeError>;
} // namespace meccha::core
