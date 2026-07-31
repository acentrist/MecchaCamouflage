#pragma once

#include <meccha/core/image_profile_mapping.hpp>
#include <meccha/core/paint_sampling_profile.hpp>

#include <cstdint>
#include <expected>
#include <stop_token>
#include <vector>

namespace meccha::core
{
struct ProfilePaintSample
{
    int uv_island{};
    double paint_u{};
    double paint_v{};
    ImageTriangleAnchor image_anchor{};
    CanonicalImageCoordinate image{};

    auto operator==(const ProfilePaintSample&) const -> bool = default;
};

enum class ProfilePaintSamplingError : std::uint8_t
{
    InvalidProfile,
    InvalidBrushSize,
    EmptySamples,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto sample_paint_profile(
    const PaintSamplingProfile& sampling_profile,
    const CanonicalImageProfile& image_profile,
    double brush_size_texels,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<ProfilePaintSample>,
        ProfilePaintSamplingError>;
} // namespace meccha::core
