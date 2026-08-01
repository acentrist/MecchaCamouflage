#pragma once

#include <meccha/core/esp.hpp>
#include <meccha/core/paint_sampling_profile.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
struct PaintCaptureGeometrySample
{
    Region region{Region::Front};
    int uv_island{};
    double u{};
    double v{};
    Vector3d world_position{};
    Vector3d world_normal{};
    bool projected{};
    EspScreenPoint screen{};
    double fallback_view_vertical{};
    double fallback_view_horizontal{};
    std::uint32_t triangle_index{};
    double view_depth{};
    std::uint32_t first_vertex{};
    std::uint32_t second_vertex{};
    std::uint32_t third_vertex{};
    double barycentric_a{};
    double barycentric_b{};
    double barycentric_c{};
    bool replay_relevant{true};
    bool calibration_sample{};

    auto operator==(const PaintCaptureGeometrySample&) const
        -> bool = default;
};

enum class PaintCaptureGeometryError : std::uint8_t
{
    InvalidProfile,
    InvalidSkeleton,
    InvalidView,
    InvalidSample,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_paint_capture_geometry(
    const PaintSamplingProfile& sampling_profile,
    const CanonicalImageProfile& image_profile,
    std::span<const PaintReferenceBoneTransform>
        current_world_transforms,
    double brush_size_texels,
    const EspView& view,
    EspViewport viewport,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<PaintCaptureGeometrySample>,
        PaintCaptureGeometryError>;
} // namespace meccha::core
