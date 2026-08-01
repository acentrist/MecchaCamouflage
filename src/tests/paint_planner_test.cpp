#include <meccha/core/paint_plan.hpp>

#include <iostream>
#include <limits>
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
        std::cerr << "FAIL paint_planner: " << message << '\n';
    }
    return condition;
}

auto sample(
    Region region,
    double u,
    double v) -> CapturedPaintSample
{
    return CapturedPaintSample{
        region,
        0,
        u,
        v,
        true,
        1.0 - v,
        1.0 - v,
        u,
        ResolvedPaintAppearance{
            Rgb8{91U, 92U, 93U},
            Material{0.8, 0.3, 0.2},
        },
        true,
        true,
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    auto settings = PaintSettings{};
    settings.front_mode = RegionMode::Fill;
    settings.side_mode = RegionMode::Paint;
    settings.back_mode = RegionMode::Skip;
    settings.color_compression_tolerance_percent = 0.0;
    settings.paint_material = Material{0.1, 0.7, 0.2};
    settings.fill_color = Rgb8{240U, 241U, 242U};
    settings.fill_material = Material{0.9, 0.1, 0.0};

    const auto request = PaintPlanRequest{
        expected_mesh_profile(
            BodyProfile::Round,
            MeshProfileRole::Raw),
        settings,
        {
            sample(
                Region::Front,
                0.1,
                0.1),
            sample(
                Region::Side,
                0.2,
                0.2),
            sample(
                Region::Back,
                0.3,
                0.3),
        },
    };
    const auto planned = build_paint_plan(request);
    passed &= expect(
        planned && planned->strokes.size() == 4U &&
            planned->texture_dimension ==
                request.profile.texture_size &&
            planned->fill_end == 3U &&
            planned->fill_count == 3U &&
            planned->paint_count == 1U,
        "Fill-first plan counts drifted");
    if (!planned)
    {
        return 1;
    }
    for (auto index = std::size_t{};
         index < planned->fill_end;
         ++index)
    {
        passed &= expect(
            planned->strokes[index].pass == ReplayPass::Fill &&
                planned->strokes[index].color ==
                    settings.fill_color &&
                planned->strokes[index].material ==
                    settings.fill_material &&
                planned->strokes[index].radius_texels ==
                    PaintFillRadiusTexels,
            "Fill did not use its independent color/material/radius");
    }
    passed &= expect(
        planned->strokes.back().pass == ReplayPass::Paint &&
            planned->strokes.back().source_sample == 1U &&
            planned->strokes.back().color ==
                Rgb8{91U, 92U, 93U} &&
            planned->strokes.back().material ==
                Material{0.8, 0.3, 0.2} &&
            planned->strokes.back().radius_texels ==
                settings.brush_size_texels,
        "normal Paint did not use the projected appearance");

    auto missing_appearance = request;
    missing_appearance.samples[1].projected_appearance_available =
        false;
    passed &= expect(
        build_paint_plan(missing_appearance) ==
            std::unexpected(
                PaintPlanError::MissingProjectedAppearance),
        "normal Paint accepted a missing projected appearance");

    auto unsafe = request;
    unsafe.samples[2].safe = false;
    const auto safe_plan = build_paint_plan(unsafe);
    passed &= expect(
        safe_plan && safe_plan->fill_count == 2U &&
            safe_plan->paint_count == 1U,
        "an unsafe capture sample reached Fill dispatch");

    auto invalid_profile = request;
    invalid_profile.profile.profile_hash.assign(64U, '0');
    passed &= expect(
        build_paint_plan(invalid_profile) ==
            std::unexpected(PaintPlanError::InvalidProfile),
        "planning accepted an unvalidated mesh profile");

    auto invalid_sample = request;
    invalid_sample.samples[0].u =
        std::numeric_limits<double>::quiet_NaN();
    passed &= expect(
        build_paint_plan(invalid_sample) ==
            std::unexpected(PaintPlanError::InvalidSample),
        "planning accepted a non-finite sample");

    auto stop = std::stop_source{};
    stop.request_stop();
    passed &= expect(
        build_paint_plan(request, stop.get_token()) ==
            std::unexpected(PaintPlanError::Cancelled),
        "cancelled Paint planning published a plan");

    if (passed)
    {
        std::cout << "PASS paint_planner\n";
    }
    return passed ? 0 : 1;
}
