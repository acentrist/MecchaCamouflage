#include <meccha/core/paint_preview.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stop_token>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_preview_composer: "
                  << message << '\n';
    }
    return condition;
}

auto buffers(std::uint32_t dimension)
    -> std::pair<std::vector<std::byte>, std::vector<std::byte>>
{
    const auto size = static_cast<std::size_t>(
        dimension * dimension * 4U);
    return {
        std::vector<std::byte>(size, std::byte{0}),
        std::vector<std::byte>(size, std::byte{0}),
    };
}

auto stroke(
    ReplayPass pass,
    double u,
    double v,
    double radius,
    Rgb8 color,
    Material material) -> PaintStroke
{
    return PaintStroke{
        0U,
        pass,
        Region::Side,
        u,
        v,
        radius,
        color,
        material,
    };
}

auto byte(const std::vector<std::byte>& values, std::size_t index)
    -> std::uint8_t
{
    return std::to_integer<std::uint8_t>(values[index]);
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    auto [albedo, pbr] = buffers(8U);
    auto plan = PaintPlan{};
    plan.strokes = {
        stroke(
            ReplayPass::Fill,
            0.5,
            0.5,
            PaintFillRadiusTexels,
            Rgb8{200U, 201U, 202U},
            Material{1.0, 0.0, 0.0}),
        stroke(
            ReplayPass::Paint,
            0.5,
            0.5,
            128.0,
            Rgb8{10U, 20U, 30U},
            Material{0.25, 0.5, 0.75}),
    };
    plan.fill_end = 1U;
    plan.fill_count = 1U;
    plan.paint_count = 1U;
    plan.source_paint_count = 1U;

    const auto composed = compose_paint_preview(
        8U,
        albedo,
        pbr,
        plan);
    const auto center = (4U * 8U + 4U) * 4U;
    passed &= expect(
        composed &&
            composed->dimension == 8U &&
            composed->strokes_composed == 2U &&
            composed->pixels_touched > 0U &&
            composed->pixels_changed > 0U &&
            byte(composed->albedo_rgba, center) == 10U &&
            byte(composed->albedo_rgba, center + 1U) == 20U &&
            byte(composed->albedo_rgba, center + 2U) == 30U &&
            byte(composed->albedo_rgba, center + 3U) == 255U &&
            byte(composed->packed_pbr_rgba, center) == 64U &&
            byte(composed->packed_pbr_rgba, center + 1U) == 128U &&
            byte(composed->packed_pbr_rgba, center + 2U) == 191U &&
            byte(composed->packed_pbr_rgba, center + 3U) == 255U,
        "Paint did not overwrite Fill in albedo and packed PBR order");
    passed &= expect(
        albedo == buffers(8U).first &&
            pbr == buffers(8U).second,
        "preview composition mutated the captured original");

    const auto corner = compose_paint_preview(
        8U,
        albedo,
        pbr,
        PaintPlan{
            {stroke(
                ReplayPass::Paint,
                0.0,
                0.0,
                128.0,
                Rgb8{1U, 2U, 3U},
                Material{})},
            0U,
            0U,
            1U,
            1U,
            0U,
            0U,
            false,
            0U,
        });
    passed &= expect(
        corner &&
            byte(corner->albedo_rgba, 0U) == 1U &&
            corner->pixels_touched < composed->pixels_touched,
        "edge clipping wrapped or lost a corner stroke");

    auto invalid_plan = plan;
    invalid_plan.fill_end = 2U;
    passed &= expect(
        compose_paint_preview(8U, albedo, pbr, invalid_plan)
                .error() ==
            PaintPreviewComposeError::InvalidPlan,
        "an inconsistent Fill/Paint boundary was accepted");
    passed &= expect(
        compose_paint_preview(
            8U,
            std::span<const std::byte>{albedo}.first(3U),
            pbr,
            plan)
                .error() ==
            PaintPreviewComposeError::InvalidBuffer,
        "a truncated channel buffer was accepted");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        compose_paint_preview(
            8U,
            albedo,
            pbr,
            plan,
            cancelled.get_token())
                .error() ==
            PaintPreviewComposeError::Cancelled,
        "pre-cancelled preview composition published a frame");

    auto excessive = plan;
    excessive.strokes.assign(
        MaximumPaintPreviewStrokes + 1U,
        plan.strokes.back());
    excessive.fill_end = 0U;
    excessive.fill_count = 0U;
    excessive.paint_count = excessive.strokes.size();
    excessive.source_paint_count = excessive.paint_count;
    passed &= expect(
        compose_paint_preview(8U, albedo, pbr, excessive)
                .error() ==
            PaintPreviewComposeError::ResourceLimit,
        "the preview stroke resource bound was not enforced");

    if (passed)
    {
        std::cout << "PASS paint_preview_composer\n";
    }
    return passed ? 0 : 1;
}
