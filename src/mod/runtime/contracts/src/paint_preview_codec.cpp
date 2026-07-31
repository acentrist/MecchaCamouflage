#include <meccha/runtime/paint_preview_codec.hpp>

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>

namespace meccha::runtime
{
namespace
{
constexpr std::size_t RgbaBytesPerPixel = 4U;
}

auto infer_paint_texture_dimension(
    std::span<const std::byte> albedo,
    std::span<const std::byte> packed_pbr)
    -> std::expected<
        std::uint32_t,
        PaintPreviewCodecError>
{
    if (albedo.empty() || packed_pbr.empty() ||
        (albedo.size() % RgbaBytesPerPixel) != 0U ||
        (packed_pbr.size() % RgbaBytesPerPixel) != 0U)
    {
        return std::unexpected(
            PaintPreviewCodecError::InvalidBytes);
    }
    if (albedo.size() != packed_pbr.size())
    {
        return std::unexpected(
            PaintPreviewCodecError::MismatchedChannels);
    }
    if (albedo.size() > MaximumPaintChannelBytes)
    {
        return std::unexpected(
            PaintPreviewCodecError::ResourceLimit);
    }

    const auto pixels = albedo.size() / RgbaBytesPerPixel;
    const auto root = std::sqrt(static_cast<long double>(pixels));
    if (!std::isfinite(root) ||
        root < 1.0L ||
        root >
            static_cast<long double>(
                MaximumPaintTextureDimension))
    {
        return std::unexpected(
            PaintPreviewCodecError::InvalidDimension);
    }
    const auto dimension = static_cast<std::uint32_t>(root);
    if (static_cast<std::uint64_t>(dimension) * dimension !=
        pixels)
    {
        return std::unexpected(
            PaintPreviewCodecError::InvalidDimension);
    }
    return dimension;
}

auto encode_channel_import(
    RuntimePaintChannel channel,
    std::span<const std::byte> bytes)
    -> std::expected<
        ImportChannelFromBytesParametersAbi,
        PaintPreviewCodecError>
{
    if (bytes.empty())
    {
        return std::unexpected(
            PaintPreviewCodecError::InvalidBytes);
    }
    if (bytes.size() > MaximumPaintChannelBytes ||
        bytes.size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::int32_t>::max()))
    {
        return std::unexpected(
            PaintPreviewCodecError::ResourceLimit);
    }

    auto parameters = ImportChannelFromBytesParametersAbi{};
    parameters.channel = channel;
    parameters.data = RuntimeByteArrayAbi{
        bytes.data(),
        static_cast<std::int32_t>(bytes.size()),
        static_cast<std::int32_t>(bytes.size()),
    };
    return parameters;
}
} // namespace meccha::runtime
