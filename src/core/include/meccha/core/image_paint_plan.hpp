#pragma once

#include <meccha/core/image_compositor.hpp>
#include <meccha/core/paint_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <vector>

namespace meccha::core
{
enum class ImageAtlasFace : std::uint8_t
{
    Front,
    Right,
    Back,
    Left,
};

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
    ImageAtlasFace face{ImageAtlasFace::Front};
    double atlas_u{};
    double atlas_v{};
    bool safe{true};

    auto operator==(const CapturedImagePaintSample&) const -> bool =
        default;
};

struct ImagePaintPlanRequest
{
    MeshProfileIdentity raw_profile{};
    MeshProfileIdentity image_profile{};
    ImageProjectSettings settings{};
    std::shared_ptr<const std::vector<std::byte>> atlas{};
    std::vector<CapturedImagePaintSample> samples{};
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
} // namespace meccha::core
