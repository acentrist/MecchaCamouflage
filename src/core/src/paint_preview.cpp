#include <meccha/core/paint_preview.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr std::uint32_t MaximumTextureDimension = 4096U;
constexpr std::uint64_t BytesPerPixel = 4U;

auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto valid_plan(const PaintPlan& plan) -> bool
{
    if (plan.fill_end != plan.fill_count ||
        plan.fill_end > plan.strokes.size() ||
        plan.paint_count !=
            plan.strokes.size() - plan.fill_end ||
        plan.source_paint_count < plan.paint_count ||
        plan.compressed_paint_count !=
            plan.source_paint_count - plan.paint_count)
    {
        return false;
    }
    for (auto index = std::size_t{};
         index < plan.strokes.size();
         ++index)
    {
        const auto& stroke = plan.strokes[index];
        const auto pass =
            index < plan.fill_end
                ? ReplayPass::Fill
                : ReplayPass::Paint;
        if (stroke.pass != pass ||
            !unit(stroke.u) || !unit(stroke.v) ||
            !std::isfinite(stroke.radius_texels) ||
            stroke.radius_texels < 1.0 ||
            stroke.radius_texels > 1024.0 ||
            !unit(stroke.material.metallic) ||
            !unit(stroke.material.roughness) ||
            !unit(stroke.material.emissive) ||
            (pass == ReplayPass::Fill &&
             (stroke.radius_texels != PaintFillRadiusTexels ||
              stroke.include_scene_lighting)))
        {
            return false;
        }
    }
    return true;
}

auto quantize(double value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}
} // namespace

auto compose_paint_preview(
    std::uint32_t dimension,
    std::span<const std::byte> original_albedo_rgba,
    std::span<const std::byte> original_packed_pbr_rgba,
    const PaintPlan& plan,
    std::stop_token cancellation)
    -> std::expected<
        PaintPreviewComposition,
        PaintPreviewComposeError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintPreviewComposeError::Cancelled);
    }
    if (dimension == 0U ||
        dimension > MaximumTextureDimension)
    {
        return std::unexpected(
            PaintPreviewComposeError::InvalidDimension);
    }

    const auto side = static_cast<std::uint64_t>(dimension);
    if (side >
        std::numeric_limits<std::uint64_t>::max() / side)
    {
        return std::unexpected(
            PaintPreviewComposeError::InvalidDimension);
    }
    const auto pixels = side * side;
    if (pixels >
        std::numeric_limits<std::uint64_t>::max() /
            BytesPerPixel)
    {
        return std::unexpected(
            PaintPreviewComposeError::InvalidDimension);
    }
    const auto expected_bytes = pixels * BytesPerPixel;
    if (expected_bytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()) ||
        original_albedo_rgba.size() != expected_bytes ||
        original_packed_pbr_rgba.size() != expected_bytes)
    {
        return std::unexpected(
            PaintPreviewComposeError::InvalidBuffer);
    }
    if (!valid_plan(plan))
    {
        return std::unexpected(
            PaintPreviewComposeError::InvalidPlan);
    }
    if (plan.strokes.size() > MaximumPaintPreviewStrokes)
    {
        return std::unexpected(
            PaintPreviewComposeError::ResourceLimit);
    }

    auto output = PaintPreviewComposition{
        dimension,
        std::vector<std::byte>{
            original_albedo_rgba.begin(),
            original_albedo_rgba.end()},
        std::vector<std::byte>{
            original_packed_pbr_rgba.begin(),
            original_packed_pbr_rgba.end()},
        0U,
        0U,
        0U,
    };
    const auto size = static_cast<int>(dimension);
    auto candidate_operations = std::uint64_t{};
    for (const auto& stroke : plan.strokes)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                PaintPreviewComposeError::Cancelled);
        }

        const auto center_x = std::clamp(
            static_cast<int>(std::lround(
                stroke.u * static_cast<double>(size - 1))),
            0,
            size - 1);
        const auto center_y = std::clamp(
            static_cast<int>(std::lround(
                stroke.v * static_cast<double>(size - 1))),
            0,
            size - 1);
        const auto radius = std::max(
            1,
            static_cast<int>(std::lround(
                stroke.radius_texels *
                static_cast<double>(dimension) /
                static_cast<double>(MeshProfileTextureSize))));
        const auto minimum_x = std::max(0, center_x - radius);
        const auto maximum_x =
            std::min(size - 1, center_x + radius);
        const auto minimum_y = std::max(0, center_y - radius);
        const auto maximum_y =
            std::min(size - 1, center_y + radius);
        const auto width = static_cast<std::uint64_t>(
            maximum_x - minimum_x + 1);
        const auto height = static_cast<std::uint64_t>(
            maximum_y - minimum_y + 1);
        const auto candidates = width * height;
        if (candidate_operations >
            MaximumPaintPreviewPixelOperations - candidates)
        {
            return std::unexpected(
                PaintPreviewComposeError::ResourceLimit);
        }
        candidate_operations += candidates;

        const auto metallic =
            quantize(stroke.material.metallic);
        const auto roughness =
            quantize(stroke.material.roughness);
        const auto emissive =
            quantize(stroke.material.emissive);
        const auto radius_squared =
            static_cast<std::int64_t>(radius) *
            static_cast<std::int64_t>(radius);
        auto composed = false;
        for (auto y = minimum_y; y <= maximum_y; ++y)
        {
            if (cancellation.stop_requested())
            {
                return std::unexpected(
                    PaintPreviewComposeError::Cancelled);
            }
            const auto delta_y =
                static_cast<std::int64_t>(y - center_y);
            for (auto x = minimum_x; x <= maximum_x; ++x)
            {
                const auto delta_x =
                    static_cast<std::int64_t>(x - center_x);
                if (delta_x * delta_x + delta_y * delta_y >
                    radius_squared)
                {
                    continue;
                }

                const auto offset =
                    (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(dimension) +
                     static_cast<std::size_t>(x)) *
                    4U;
                const auto albedo_changed =
                    output.albedo_rgba[offset] !=
                        static_cast<std::byte>(
                            stroke.color.red) ||
                    output.albedo_rgba[offset + 1U] !=
                        static_cast<std::byte>(
                            stroke.color.green) ||
                    output.albedo_rgba[offset + 2U] !=
                        static_cast<std::byte>(
                            stroke.color.blue) ||
                    output.albedo_rgba[offset + 3U] !=
                        std::byte{0xFF};
                const auto pbr_changed =
                    output.packed_pbr_rgba[offset] !=
                        static_cast<std::byte>(metallic) ||
                    output.packed_pbr_rgba[offset + 1U] !=
                        static_cast<std::byte>(roughness) ||
                    output.packed_pbr_rgba[offset + 2U] !=
                        static_cast<std::byte>(emissive) ||
                    output.packed_pbr_rgba[offset + 3U] !=
                        std::byte{0xFF};
                output.albedo_rgba[offset] =
                    static_cast<std::byte>(stroke.color.red);
                output.albedo_rgba[offset + 1U] =
                    static_cast<std::byte>(stroke.color.green);
                output.albedo_rgba[offset + 2U] =
                    static_cast<std::byte>(stroke.color.blue);
                output.albedo_rgba[offset + 3U] =
                    std::byte{0xFF};
                output.packed_pbr_rgba[offset] =
                    static_cast<std::byte>(metallic);
                output.packed_pbr_rgba[offset + 1U] =
                    static_cast<std::byte>(roughness);
                output.packed_pbr_rgba[offset + 2U] =
                    static_cast<std::byte>(emissive);
                output.packed_pbr_rgba[offset + 3U] =
                    std::byte{0xFF};
                ++output.pixels_touched;
                if (albedo_changed || pbr_changed)
                {
                    ++output.pixels_changed;
                }
                composed = true;
            }
        }
        if (composed)
        {
            ++output.strokes_composed;
        }
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintPreviewComposeError::Cancelled);
    }
    return output;
}
} // namespace meccha::core
