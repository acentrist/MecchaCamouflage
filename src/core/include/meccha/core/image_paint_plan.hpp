#pragma once

#include <meccha/core/image_compositor.hpp>
#include <meccha/core/image_profile_mapping.hpp>
#include <meccha/core/paint_plan.hpp>
#include <meccha/core/paint_sampling_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <vector>

namespace meccha::core
{
struct CapturedImagePaintSample
{
    Region paint_region{Region::Front};
    int uv_island{};
    double paint_u{};
    double paint_v{};
    bool has_current_view_position{};
    double current_view_vertical{};
    double fallback_view_vertical{};
    double horizontal{};
    ImageTriangleAnchor image_anchor{};
    bool safe{true};

    auto operator==(const CapturedImagePaintSample&) const -> bool =
        default;
};

struct ImagePaintPlanRequest
{
    MeshProfileIdentity raw_profile{};
    CanonicalImageProfile image_profile{};
    ImageProjectSettings settings{};
    std::shared_ptr<const std::vector<std::byte>> atlas{};
    std::vector<CapturedImagePaintSample> samples{};
};

struct ImagePaintProfilePlanRequest
{
    PaintSamplingProfile sampling_profile{};
    CanonicalImageProfile image_profile{};
    ImageProjectSettings settings{};
    std::shared_ptr<const std::vector<std::byte>> atlas{};
};

struct ImagePaintPlan
{
    ImageProjectSettings settings{};
    PaintPlan paint{};
    std::size_t opaque_samples{};
    std::size_t transparent_samples{};
    std::size_t background_marker_samples{};
    std::size_t unsafe_samples{};
    std::size_t fill_face_samples{};
    std::size_t generated_samples{};
};

enum class ImagePaintPlanError : std::uint8_t
{
    InvalidProfile,
    InvalidSettings,
    InvalidAtlas,
    EmptySamples,
    InvalidSample,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_image_paint_plan(
    const ImagePaintPlanRequest& request,
    std::stop_token cancellation = {})
    -> std::expected<ImagePaintPlan, ImagePaintPlanError>;

[[nodiscard]] auto build_image_paint_plan_from_profile(
    const ImagePaintProfilePlanRequest& request,
    std::stop_token cancellation = {})
    -> std::expected<ImagePaintPlan, ImagePaintPlanError>;
} // namespace meccha::core
