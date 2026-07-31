#include <meccha/core/paint_capture_request.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <stop_token>

namespace meccha::core
{
namespace
{
auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto appearance_valid(
    const ResolvedPaintAppearance& appearance) -> bool
{
    return unit(appearance.material.metallic) &&
           unit(appearance.material.roughness) &&
           unit(appearance.material.emissive);
}

auto map_geometry_error(PaintCaptureGeometryError error)
    -> PaintCaptureRequestError
{
    switch (error)
    {
    case PaintCaptureGeometryError::ResourceLimit:
        return PaintCaptureRequestError::ResourceLimit;
    case PaintCaptureGeometryError::Cancelled:
        return PaintCaptureRequestError::Cancelled;
    case PaintCaptureGeometryError::InvalidProfile:
    case PaintCaptureGeometryError::InvalidSkeleton:
    case PaintCaptureGeometryError::InvalidView:
    case PaintCaptureGeometryError::InvalidSample:
        return PaintCaptureRequestError::InvalidGeometry;
    }
    return PaintCaptureRequestError::InvalidGeometry;
}
} // namespace

auto build_paint_capture_request(
    const PaintCaptureInput& input,
    std::stop_token cancellation)
    -> std::expected<
        PaintPlanRequest,
        PaintCaptureRequestError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintCaptureRequestError::Cancelled);
    }
    if (!validate(input.settings).empty())
    {
        return std::unexpected(
            PaintCaptureRequestError::InvalidInput);
    }
    const auto& raster = input.raster;
    if (raster.width == 0U || raster.height == 0U ||
        raster.width > MaximumPaintCaptureDimension ||
        raster.height > MaximumPaintCaptureDimension ||
        !std::isfinite(input.viewport.width) ||
        !std::isfinite(input.viewport.height) ||
        input.viewport.width !=
            static_cast<double>(raster.width) ||
        input.viewport.height !=
            static_cast<double>(raster.height) ||
        raster.width >
            std::numeric_limits<std::size_t>::max() /
                raster.height)
    {
        return std::unexpected(
            PaintCaptureRequestError::InvalidRaster);
    }
    const auto pixel_count =
        static_cast<std::size_t>(raster.width) *
        static_cast<std::size_t>(raster.height);
    if (!raster.intrinsic_colors ||
        !raster.scene_colors ||
        raster.intrinsic_colors->size() != pixel_count ||
        raster.scene_colors->size() != pixel_count ||
        (raster.automatic_appearances &&
         raster.automatic_appearances->size() != pixel_count))
    {
        return std::unexpected(
            PaintCaptureRequestError::InvalidRaster);
    }
    if (input.settings.auto_material &&
        !raster.automatic_appearances)
    {
        return std::unexpected(
            PaintCaptureRequestError::
                MissingAutomaticAppearance);
    }
    if (raster.automatic_appearances &&
        !std::ranges::all_of(
            *raster.automatic_appearances,
            appearance_valid))
    {
        return std::unexpected(
            PaintCaptureRequestError::InvalidRaster);
    }

    const auto geometry = build_paint_capture_geometry(
        input.sampling_profile,
        input.image_profile,
        input.current_world_transforms,
        input.settings.brush_size_texels,
        input.view,
        input.viewport,
        cancellation);
    if (!geometry)
    {
        return std::unexpected(
            map_geometry_error(geometry.error()));
    }

    auto request = PaintPlanRequest{
        input.sampling_profile.identity,
        input.settings,
        {},
    };
    request.samples.reserve(geometry->size());
    auto safe_count = std::size_t{};
    for (auto index = std::size_t{};
         index < geometry->size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintCaptureRequestError::Cancelled);
        }
        const auto& sample = (*geometry)[index];
        auto pixel = std::size_t{};
        if (sample.projected)
        {
            const auto x = static_cast<std::uint32_t>(
                std::clamp(
                    std::floor(sample.screen.x),
                    0.0,
                    static_cast<double>(raster.width - 1U)));
            const auto y = static_cast<std::uint32_t>(
                std::clamp(
                    std::floor(sample.screen.y),
                    0.0,
                    static_cast<double>(raster.height - 1U)));
            pixel =
                static_cast<std::size_t>(y) * raster.width + x;
            ++safe_count;
        }
        const auto automatic_available =
            sample.projected &&
            raster.automatic_appearances != nullptr;
        request.samples.push_back(CapturedPaintSample{
            sample.region,
            sample.uv_island,
            sample.u,
            sample.v,
            sample.projected,
            sample.projected ? -sample.screen.y : 0.0,
            sample.fallback_view_vertical,
            sample.projected
                ? sample.screen.x
                : sample.fallback_view_horizontal,
            sample.projected
                ? (*raster.intrinsic_colors)[pixel]
                : Rgb8{},
            sample.projected
                ? (*raster.scene_colors)[pixel]
                : Rgb8{},
            automatic_available
                ? (*raster.automatic_appearances)[pixel]
                : ResolvedPaintAppearance{},
            automatic_available,
            sample.projected,
        });
    }
    if (safe_count == 0U)
    {
        return std::unexpected(
            PaintCaptureRequestError::InvalidGeometry);
    }
    return request;
}
} // namespace meccha::core
