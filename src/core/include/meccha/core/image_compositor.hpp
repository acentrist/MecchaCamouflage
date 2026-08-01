#pragma once

#include <meccha/core/image_mapping.hpp>
#include <meccha/core/image_project.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace meccha::core
{
inline constexpr std::uint32_t MaximumDecodedImageDimension = 8192U;
inline constexpr std::uint64_t MaximumDecodedImageBytes =
    64U * 1024U * 1024U;
inline constexpr std::uint64_t MaximumDecodedProjectBytes =
    256U * 1024U * 1024U;
inline constexpr std::uint64_t MaximumImageCompositionPixelOperations =
    200'000'000U;
inline constexpr std::uint8_t ImageOpaqueAlphaThreshold = 128U;
inline constexpr std::uint8_t ImageBackgroundAlphaMarker = 254U;

struct DecodedImageSource
{
    std::string asset_id{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::shared_ptr<const std::vector<std::byte>> rgba{};
};

struct ImageAtlasComposition
{
    std::vector<std::byte> rgba{};
    std::size_t layers_composed{};
    std::size_t rectangles_composed{};
    std::uint64_t pixel_operations{};
    std::uint64_t pixels_blended{};

    auto operator==(const ImageAtlasComposition&) const -> bool = default;
};

enum class ImageComposeError : std::uint8_t
{
    InvalidSettings,
    EmptyLayers,
    InvalidLayer,
    InvalidSource,
    SourceMismatch,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto compose_image_atlas(
    const ImageProjectSettings& settings,
    std::span<const ImageLayer> layers,
    std::span<const DecodedImageSource> sources,
    std::stop_token cancellation = {})
    -> std::expected<ImageAtlasComposition, ImageComposeError>;
} // namespace meccha::core
