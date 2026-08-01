#include <meccha/core/paint_projective_pipeline.hpp>

#include <array>
#include <iostream>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_projective_pipeline: " << message
                  << '\n';
    }
    return condition;
}

auto sample(
    std::size_t index,
    meccha::core::Region region,
    std::array<std::uint32_t, 3> vertices)
    -> meccha::core::PaintProjectiveSample
{
    using namespace meccha::core;
    return PaintProjectiveSample{
        index,
        index,
        region,
        0,
        0.2 + static_cast<double>(index) * 0.1,
        0.2,
        static_cast<std::uint32_t>(index),
        vertices[0],
        vertices[1],
        vertices[2],
        1.0 / 3.0,
        1.0 / 3.0,
        1.0 / 3.0,
        true,
        true,
        true,
        {0.1, 0.1, 0.1},
        {0.3, 0.3, 0.3},
        {},
        {},
        static_cast<std::uint64_t>(index + 1U),
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;
    auto passed = true;
    auto model = PaintProjectiveModel{};
    model.width = 2U;
    model.height = 2U;
    model.replay_samples = 3U;
    model.calibration_samples = 3U;
    model.source_noise_first = {true, 0.0, 0.0, 0.005, true, 64};
    model.source_noise_second = model.source_noise_first;
    model.samples = {
        sample(0U, Region::Front, {0U, 1U, 2U}),
        sample(1U, Region::Side, {1U, 2U, 3U}),
        sample(2U, Region::Back, {2U, 3U, 4U}),
    };

    auto settings = PaintSettings{};
    settings.front_mode = RegionMode::Paint;
    settings.side_mode = RegionMode::Paint;
    settings.back_mode = RegionMode::Paint;
    const auto baseline =
        build_paint_projective_baseline(model, settings);
    passed &= expect(
        baseline && baseline->appearances.size() == 4U &&
            baseline->available[0] && baseline->available[1] &&
            baseline->available[2],
        "direct projected baseline did not publish a bounded raster");
    if (!baseline)
    {
        return 1;
    }
    const auto trial = build_paint_projective_trial_plan(
        model,
        baseline->appearances,
        16U);
    passed &= expect(
        trial && trial->paint_count == 3U &&
            trial->strokes.front().radius_texels ==
                AppearanceCalibrationStepTexels,
        "trial preview did not use the fixed four-texel lattice");

    auto baseline_feedback = PaintProjectiveFeedback{
        true,
        true,
        AppearanceReadbackTransform::Identity,
        std::vector<AppearanceHdrSample>(
            3U,
            AppearanceHdrSample{{0.1, 0.1, 0.1}, true, false}),
    };
    const auto target_noise =
        AppearanceEmissionNoiseModel{true, 0.0, 0.0, 0.005, true, 64};
    const auto calibration = calibrate_paint_projective_baseline(
        model,
        baseline_feedback,
        target_noise,
        settings,
        true);
    passed &= expect(
        calibration && calibration->corrected_samples == 2 &&
            calibration->front_anchor_vertices > 0 &&
            calibration->back_anchor_vertices > 0 &&
            calibration->side_components == 1 &&
            calibration->endpoint.appearances.size() == 4U,
        "front/back correction did not resolve the Side field");
    if (!calibration)
    {
        return 1;
    }

    auto endpoint_feedback = baseline_feedback;
    for (auto& value : endpoint_feedback.target_hdr_by_sample)
    {
        value.value = {0.3, 0.3, 0.3};
    }
    const auto resolved = finalize_paint_projective_raster(
        model,
        *calibration,
        baseline_feedback,
        endpoint_feedback,
        target_noise,
        settings,
        true);
    passed &= expect(
        resolved && resolved->local_albedo_acceptances == 3 &&
            resolved->physical_emission_samples == 0 &&
            resolved->raster.available[0] &&
            resolved->raster.available[1] &&
            resolved->raster.available[2],
        "best local albedo-only feedback was not retained");

    auto unanchored = model;
    unanchored.samples = {
        sample(0U, Region::Side, {0U, 1U, 2U}),
    };
    unanchored.replay_samples = 1U;
    unanchored.calibration_samples = 1U;
    auto one_feedback = PaintProjectiveFeedback{
        true,
        true,
        AppearanceReadbackTransform::Identity,
        std::vector<AppearanceHdrSample>(
            1U,
            AppearanceHdrSample{{0.1, 0.1, 0.1}, true, false}),
    };
    passed &= expect(
        calibrate_paint_projective_baseline(
            unanchored,
            one_feedback,
            target_noise,
            settings,
            true)
                .error() == PaintProjectiveError::NoSupportedSamples,
        "a Side-only correction field did not fail closed");

    if (passed)
    {
        std::cout << "PASS paint_projective_pipeline\n";
    }
    return passed ? 0 : 1;
}
