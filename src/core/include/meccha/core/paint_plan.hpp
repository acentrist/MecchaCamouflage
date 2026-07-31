#pragma once

#include <meccha/core/mesh_profile.hpp>
#include <meccha/core/paint.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <stop_token>
#include <vector>

namespace meccha::core
{
inline constexpr double PaintFillRadiusTexels = 100.0;

struct ResolvedPaintAppearance
{
    Rgb8 color{};
    Material material{};

    auto operator==(const ResolvedPaintAppearance&) const
        -> bool = default;
};

struct CapturedPaintSample
{
    Region region{Region::Front};
    int uv_island{};
    double u{};
    double v{};
    bool has_current_view_position{};
    double current_view_vertical{};
    double fallback_view_vertical{};
    double horizontal{};
    Rgb8 intrinsic_color{};
    Rgb8 scene_color{};
    ResolvedPaintAppearance automatic_appearance{};
    bool automatic_appearance_available{};
    bool safe{true};

    auto operator==(const CapturedPaintSample&) const
        -> bool = default;
};

struct PaintPlanRequest
{
    MeshProfileIdentity profile{};
    PaintSettings settings{};
    std::vector<CapturedPaintSample> samples{};
};

struct PaintStroke
{
    std::size_t source_sample{};
    ReplayPass pass{ReplayPass::Paint};
    Region region{Region::Front};
    double u{};
    double v{};
    double radius_texels{};
    Rgb8 color{};
    Material material{};
    bool include_scene_lighting{};

    auto operator==(const PaintStroke&) const -> bool = default;
};

struct PaintPlan
{
    std::vector<PaintStroke> strokes{};
    std::size_t fill_end{};
    std::size_t fill_count{};
    std::size_t paint_count{};
    std::size_t source_paint_count{};
    std::size_t compressed_paint_count{};
    std::size_t expanded_paint_count{};
    bool projection_fallback_used{};
    std::size_t projection_fallback_count{};
    std::uint32_t texture_dimension{};
};

enum class PaintPlanError : std::uint8_t
{
    InvalidProfile,
    InvalidSettings,
    InvalidSample,
    MissingAutomaticAppearance,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_paint_plan(
    const PaintPlanRequest& request,
    std::stop_token cancellation = {})
    -> std::expected<PaintPlan, PaintPlanError>;
} // namespace meccha::core
