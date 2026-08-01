#include <meccha/core/paint_appearance.hpp>

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_appearance: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;
    auto passed = true;

    const auto projected = environment_projected_capture_coordinate(
        EnvironmentProjectedCaptureInput{0.25, 0.75});
    passed &= expect(
        projected.ok && projected.capture_u == 0.25 &&
            projected.capture_v == 0.75,
        "current-view environment projection changed coordinates");
    passed &= expect(
        !environment_projected_capture_coordinate({-0.1, 0.5}).ok,
        "out-of-view projection was accepted");

    const auto encoded = appearance_linear_to_srgb(0.25);
    passed &= expect(
        std::abs(appearance_srgb_to_linear(encoded) - 0.25) < 1e-9,
        "linear/sRGB conversion did not round-trip");
    passed &= expect(
        appearance_sanitize_hdr({65.0, 0.0, 0.0}).clipped &&
            !appearance_sanitize_hdr({1.0, 0.5, 0.25}).clipped,
        "HDR clipping contract changed");

    auto noise_samples = std::vector<double>(64U, 0.001);
    noise_samples.insert(noise_samples.end(), 64U, 0.08);
    const auto source_noise =
        appearance_source_emission_noise_model(noise_samples);
    passed &= expect(
        source_noise.ok && source_noise.separated_signal &&
            source_noise.threshold > 0.001 &&
            source_noise.threshold < 0.08,
        "repeatable source emission distribution was not separated");

    auto physical = AppearancePhysicalEmissionEvidenceInput{};
    physical.source_residual_first = {0.20, 0.05, 0.02};
    physical.source_residual_second = {0.19, 0.05, 0.02};
    physical.source_noise_floor_first = 0.01;
    physical.source_noise_floor_second = 0.01;
    physical.source_distribution_separated = true;
    physical.source_hdr = {0.8, 0.3, 0.15};
    physical.baseline_hdr = {0.2, 0.1, 0.05};
    physical.endpoint_hdr = {1.0, 0.4, 0.2};
    physical.manual_emissive_floor = 0.10;
    physical.camera_stable = true;
    physical.readback_calibrated = true;
    physical.packed_b_verified = true;
    const auto accepted =
        appearance_physical_emission_evidence(physical);
    passed &= expect(
        accepted.source_supported && accepted.target_response_supported &&
            accepted.accepted && accepted.composed_emissive >= 0.10,
        "dual-evidence physical emission was not accepted");
    physical.source_distribution_separated = false;
    passed &= expect(
        !appearance_physical_emission_evidence(physical).source_supported,
        "unseparated source residuals enabled automatic emission");

    const auto material = appearance_compose_physical_emission_material(
        AppearancePhysicalEmissionMaterialInput{
            {0.2, 0.2, 0.2},
            {0.2, 0.05, 0.02},
            {0.19, 0.05, 0.02},
            0.1,
            0.6,
            true,
        });
    passed &= expect(
        material.emissive >= 0.6 &&
            material.chromaticity_carrier_applied &&
            material.albedo.r > material.albedo.g,
        "physical emission did not keep scalar E and albedo chromaticity");

    const auto component =
        appearance_validate_physical_emission_component(
            AppearancePhysicalEmissionComponentValidationInput{
                64,
                1.0,
                0.5,
                32,
                0.20,
                0.20,
                true,
                true,
                true,
                true,
                64,
            });
    passed &= expect(
        component.accepted &&
            component.rejection ==
                AppearancePhysicalEmissionComponentRejection::None,
        "validated emission component was rejected");

    auto field_input = AppearanceCorrectionFieldInput{};
    field_input.vertex_count = 4;
    field_input.edges = {{0, 1}, {1, 2}, {2, 3}};
    field_input.side_vertices = {false, true, true, false};
    field_input.anchors = {
        {0, {0.0, 0.0, 0.0}, 1.0,
         AppearanceCorrectionBoundary::Front},
        {3, {0.6, 0.3, 0.0}, 1.0,
         AppearanceCorrectionBoundary::Back},
    };
    const auto field = appearance_solve_correction_field(field_input);
    passed &= expect(
        field.ok && field.front_anchor_vertices == 1 &&
            field.back_anchor_vertices == 1 &&
            field.side_components == 1 && field.resolved[1] &&
            field.resolved[2] && field.values[1].r > 0.0 &&
            field.values[1].r < field.values[2].r,
        "front/back anchors did not harmonically resolve Side");
    field_input.anchors.clear();
    passed &= expect(
        appearance_solve_correction_field(field_input).failure ==
            AppearanceCorrectionFieldFailure::SideUnanchored,
        "unanchored Side correction did not fail closed");

    const auto calibrated = appearance_calibrate_bounded_response(
        {0.5, 0.5, 0.5},
        {0.0, 0.0, 0.0},
        {1.0, 1.0, 1.0},
        0.0,
        1.0,
        0.0,
        1.0);
    passed &= expect(
        calibrated.supported && calibrated.parameter > 0.0 &&
            calibrated.parameter < 1.0,
        "bounded target response calibration failed");

    const auto readback = appearance_calibrate_linear_readback(
        std::vector<AppearanceRgb>(32U, {0.8, 0.2, 0.1}),
        std::vector<AppearanceRgb>(32U, {0.1, 0.2, 0.8}));
    passed &= expect(
        readback.ok && readback.transform ==
                           AppearanceReadbackTransform::SwapRedBlue,
        "readback channel calibration did not identify BGR order");

    if (passed)
    {
        std::cout << "PASS paint_appearance\n";
    }
    return passed ? 0 : 1;
}
