#pragma once

#include <meccha/core/image_profile_mapping.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <vector>

namespace meccha::core
{
inline constexpr std::uint32_t ImageGuideSchemaVersion = 1U;
inline constexpr std::uint64_t MaximumImageGuidePixelOperations =
    200'000'000U;

struct ImageGuideBitmap
{
    std::uint32_t schema_version{ImageGuideSchemaVersion};
    MeshProfileIdentity profile{};
    std::uint32_t width{CanonicalAtlasWidth};
    std::uint32_t height{CanonicalAtlasHeight};
    std::shared_ptr<const std::vector<std::byte>> rgba{};
    std::size_t projected_triangles{};
    std::size_t bone_segments{};

    auto operator==(const ImageGuideBitmap&) const -> bool = default;
};

enum class ImageGuideError : std::uint8_t
{
    InvalidProfile,
    InvalidGeometry,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_image_guide_bitmap(
    const CanonicalImageProfile& profile,
    std::stop_token cancellation = {})
    -> std::expected<ImageGuideBitmap, ImageGuideError>;
} // namespace meccha::core
