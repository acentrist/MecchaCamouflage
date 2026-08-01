#pragma once

#include <meccha/core/paint_capture_geometry.hpp>
#include <meccha/core/paint_plan.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <stop_token>
#include <vector>

namespace meccha::core
{
inline constexpr std::uint32_t MaximumPaintCaptureDimension =
    2048U;

struct PaintCaptureRaster
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::shared_ptr<
        const std::vector<ResolvedPaintAppearance>>
        projected_appearances{};
    std::shared_ptr<const std::vector<bool>>
        projected_available{};
};

struct PaintCaptureInput
{
    PaintSamplingProfile sampling_profile{};
    CanonicalImageProfile image_profile{};
    std::vector<PaintReferenceBoneTransform>
        current_world_transforms{};
    PaintSettings settings{};
    EspView view{};
    EspViewport viewport{};
    PaintCaptureRaster raster{};
};

enum class PaintCaptureRequestError : std::uint8_t
{
    InvalidInput,
    InvalidRaster,
    MissingProjectedAppearance,
    InvalidGeometry,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_paint_capture_request(
    const PaintCaptureInput& input,
    std::stop_token cancellation = {})
    -> std::expected<
        PaintPlanRequest,
        PaintCaptureRequestError>;
} // namespace meccha::core
