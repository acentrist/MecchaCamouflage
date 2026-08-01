#include <meccha/core/paint_appearance.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_appearance: "
                  << message << '\n';
    }
    return condition;
}

auto close(double left, double right) -> bool
{
    return std::abs(left - right) <= 1.0e-9;
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    const auto hdr = appearance_sanitize_hdr(
        AppearanceRgb{4.0, 2.0, 1.0});
    const auto clipped = appearance_sanitize_hdr(
        AppearanceRgb{
            AppearanceHdrMaximum + 1.0,
            0.0,
            0.0,
        });
    passed &= expect(
        hdr.finite && !hdr.clipped &&
            hdr.value == AppearanceRgb{4.0, 2.0, 1.0} &&
            clipped.finite && clipped.clipped,
        "HDR capture values were clamped to paint-channel bounds");

    const auto encoded = 0.73;
    passed &= expect(
        close(
            appearance_linear_to_srgb(
                appearance_srgb_to_linear(encoded)),
            encoded),
        "the exact sRGB transfer functions did not round-trip");

    const auto residual =
        appearance_intrinsic_emission_residual(
            AppearanceRgb{1.0, 0.5, 0.25},
            AppearanceRgb{0.5, 0.5, 0.5});
    passed &= expect(
        residual.r > residual.g &&
            residual.g > residual.b &&
            residual.b > 0.0,
        "intrinsic emission did not subtract linearized BaseColor");

    const auto source_noise =
        appearance_source_emission_noise_model(
            std::vector<double>{
                0.001, 0.002, 0.002, 0.003, 0.001,
                0.002, 0.003, 0.001, 0.002, 0.003,
                0.001, 0.002, 0.002, 0.003,
                0.20, 0.21, 0.22, 0.23, 0.24,
                0.25, 0.26, 0.27, 0.28, 0.29,
                0.30, 0.31, 0.32, 0.33,
            });
    const auto target_noise =
        appearance_emission_noise_model(
            std::vector<double>{0.01, 0.01, 0.011, 0.009});
    const auto combined =
        appearance_combine_emission_noise_models(
            source_noise,
            target_noise);
    passed &= expect(
        source_noise.ok && source_noise.separated_signal &&
            target_noise.ok && combined.ok &&
            combined.threshold >= source_noise.threshold &&
            combined.threshold >= target_noise.threshold &&
            !appearance_emission_sample_detected(
                combined.threshold,
                combined) &&
            appearance_emission_sample_detected(
                combined.threshold + 0.001,
                combined),
        "source and target E0 noise floors were not combined conservatively");

    const auto halo = appearance_filter_emission_surface_halo(
        {
            {{0, 0}, 1.20, 100}, {{1, 0}, 1.18, 100},
            {{2, 0}, 1.22, 100}, {{3, 0}, 1.19, 100},
            {{4, 0}, 1.24, 100}, {{5, 0}, 1.21, 100},
            {{0, 1}, 0.82, 100}, {{1, 1}, 0.80, 100},
            {{2, 1}, 0.74, 200}, {{3, 1}, 0.76, 200},
            {{4, 1}, 0.78, 200}, {{5, 1}, 0.80, 200},
            {{0, 2}, 0.72, 200}, {{1, 2}, 0.75, 200},
            {{2, 2}, 0.77, 200}, {{3, 2}, 0.79, 200},
            {{4, 2}, 0.81, 200}, {{5, 2}, 0.73, 200},
            {{0, 3}, 0.74, 201}, {{1, 3}, 0.76, 201},
            {{2, 3}, 0.78, 201}, {{3, 3}, 0.80, 201},
            {{4, 3}, 0.72, 201}, {{5, 3}, 0.75, 201},
            {{2, 4}, 0.77, 201}, {{3, 4}, 0.79, 201},
        });
    passed &= expect(
        halo.applied_regions == 1 &&
            halo.core_samples == 6 &&
            halo.core_surface_count == 1 &&
            halo.kept_samples == 8 &&
            halo.halo_rejected_samples == 18 &&
            halo.keep.size() == 26U &&
            std::all_of(
                halo.keep.begin(),
                halo.keep.begin() + 8,
                [](bool keep) { return keep; }) &&
            std::none_of(
                halo.keep.begin() + 8,
                halo.keep.end(),
                [](bool keep) { return keep; }),
        "surface-aware emission filtering did not reject the weak "
        "cross-surface halo");

    const auto uniform =
        appearance_filter_emission_surface_halo(
            {
                {{20, 20}, 0.74, 300},
                {{21, 20}, 0.76, 300},
                {{22, 20}, 0.78, 300},
                {{20, 21}, 0.75, 300},
                {{21, 21}, 0.77, 300},
                {{22, 21}, 0.79, 300},
            });
    passed &= expect(
        uniform.applied_regions == 0 &&
            uniform.kept_samples == 6 &&
            std::all_of(
                uniform.keep.begin(),
                uniform.keep.end(),
                [](bool keep) { return keep; }),
        "surface filtering removed a uniform emissive surface");

    const auto calibration = appearance_calibrate_emissive(
        AppearanceRgb{3.0, 1.5, 0.75},
        AppearanceRgb{1.0, 0.5, 0.25},
        AppearanceRgb{5.0, 2.5, 1.25});
    passed &= expect(
        calibration.supported &&
            calibration.emissive > 0.0 &&
            calibration.emissive < 1.0 &&
            calibration.response_energy > 0.0,
        "bounded emissive response projection was rejected");

    const auto channel_response = [](const AppearanceRgb& albedo)
    {
        return AppearanceRgb{
            0.75 * albedo.r,
            0.60 * albedo.g,
            1.10 * albedo.b,
        };
    };
    const auto calibrated_rgb =
        appearance_calibrate_albedo_chromaticity(
            AppearanceRgb{0.20, 0.20, 0.20},
            AppearanceRgb{0.30, 0.30, 0.30},
            channel_response(
                AppearanceRgb{0.20, 0.20, 0.20}));
    const auto corrected_response =
        channel_response(calibrated_rgb.albedo);
    const auto corrected_sum =
        corrected_response.r + corrected_response.g +
        corrected_response.b;
    passed &= expect(
        calibrated_rgb.supported &&
            calibrated_rgb.responsive_channels == 3 &&
            close(
                appearance_luminance(calibrated_rgb.albedo),
                appearance_luminance(
                    AppearanceRgb{0.20, 0.20, 0.20})) &&
            close(corrected_response.r / corrected_sum, 1.0 / 3.0) &&
            close(corrected_response.g / corrected_sum, 1.0 / 3.0) &&
            close(corrected_response.b / corrected_sum, 1.0 / 3.0),
        "per-channel albedo response did not recover source chromaticity");

    auto cluster_log_gains =
        std::array<std::vector<double>, 3>{};
    for (auto index = 0;
         index < AppearanceClusterEmissiveMinimumSamples;
         ++index)
    {
        cluster_log_gains[0].push_back(std::log(0.90));
        cluster_log_gains[1].push_back(std::log(1.10));
        cluster_log_gains[2].push_back(std::log(1.00));
    }
    cluster_log_gains[0].push_back(std::log(0.01));
    cluster_log_gains[1].push_back(std::log(100.0));
    const auto robust_gain =
        appearance_robust_albedo_chromaticity_gain(
            cluster_log_gains);
    const auto robust_albedo =
        appearance_apply_albedo_chromaticity_gain(
            AppearanceRgb{0.20, 0.20, 0.20},
            robust_gain.gain);
    passed &= expect(
        robust_gain.supported &&
            robust_gain.responsive_channels == 3 &&
            close(robust_gain.gain.r, 0.90) &&
            close(robust_gain.gain.g, 1.10) &&
            close(robust_gain.gain.b, 1.00) &&
            robust_albedo.r < robust_albedo.b &&
            robust_albedo.b < robust_albedo.g &&
            close(
                appearance_luminance(robust_albedo),
                appearance_luminance(
                    AppearanceRgb{0.20, 0.20, 0.20})),
        "cluster chromaticity gains were not robust or luminance preserving");
    passed &= expect(
        close(
            appearance_emission_projected_value(0.0, true),
            1.0 / 255.0) &&
            close(
                appearance_emission_projected_value(0.7, false),
                0.0),
        "emission ROI quantization floor or isolation authority changed");

    const auto fallback = appearance_make_safe_final_fallback(
        AppearanceRgb{0.1, 0.2, 0.3},
        AppearanceRgb{0.8, 0.7, 0.6},
        true,
        true);
    passed &= expect(
        fallback.albedo == AppearanceRgb{0.8, 0.7, 0.6} &&
            close(fallback.material.emissive, 0.0),
        "rejected intrinsic emission did not retain the evaluated display "
        "baseline with Emissive disabled");

    passed &= expect(
        appearance_parameterized_albedo(
            AppearanceRgb{0.1, 0.2, 0.3},
            AppearanceRgb{0.7, 0.6, 0.5},
            0.0,
            false) == AppearanceRgb{0.1, 0.2, 0.3} &&
            appearance_parameterized_albedo(
                AppearanceRgb{0.1, 0.2, 0.3},
                AppearanceRgb{0.7, 0.6, 0.5},
                1.0,
                false) == AppearanceRgb{0.7, 0.6, 0.5} &&
            appearance_parameterized_albedo(
                AppearanceRgb{0.9, 0.9, 0.9},
                AppearanceRgb{1.0, 0.5, 0.25},
                0.5,
                true) == AppearanceRgb{0.5, 0.25, 0.125},
        "bounded Albedo response changed its non-emission blend or emission "
        "chromaticity scale");

    auto expected_readback = std::vector<AppearanceRgb>{};
    auto swapped_readback = std::vector<AppearanceRgb>{};
    for (auto index = 0; index < 16; ++index)
    {
        const auto expected_sample = AppearanceRgb{
            0.05 * static_cast<double>(index),
            0.01 * static_cast<double>(index),
            0.8 - 0.03 * static_cast<double>(index),
        };
        expected_readback.push_back(expected_sample);
        swapped_readback.push_back(AppearanceRgb{
            expected_sample.b,
            expected_sample.g,
            expected_sample.r,
        });
    }
    const auto readback_calibration =
        appearance_calibrate_linear_readback(
            expected_readback,
            swapped_readback);
    passed &= expect(
        readback_calibration.ok &&
            readback_calibration.transform ==
                AppearanceReadbackTransform::SwapRedBlue &&
            close(readback_calibration.median_error, 0.0),
        "linear readback calibration did not detect a separated R/B swap");

    const auto parameters =
        std::vector<double>{0.4, 0.2, 0.7, 0.3};
    const auto pair_a =
        appearance_spsa_pair(parameters, 1, 0x1234U);
    const auto pair_b =
        appearance_spsa_pair(parameters, 1, 0x1234U);
    const auto updated = appearance_spsa_update(
        parameters,
        pair_a,
        0.5,
        0.8,
        1);
    auto bounded = updated.size() == parameters.size();
    for (const auto value : updated)
    {
        bounded = bounded && std::isfinite(value) &&
                  value >= 0.0 && value <= 1.0;
    }
    passed &= expect(
        pair_a.plus == pair_b.plus &&
            pair_a.minus == pair_b.minus &&
            pair_a.direction == pair_b.direction &&
            bounded && updated != parameters,
        "SPSA was not deterministic, bounded, or responsive to loss");

    passed &= expect(
        appearance_fit_accepted(AppearanceFitAcceptance{
            256,
            true,
            true,
            1.0,
            0.84,
            0.05,
        }) &&
            !appearance_fit_accepted(AppearanceFitAcceptance{
                255,
                true,
                true,
                1.0,
                0.1,
                0.01,
            }) &&
            !appearance_fit_accepted(AppearanceFitAcceptance{
                256,
                true,
                true,
                1.0,
                0.86,
                0.01,
            }),
        "the exact sample, improvement, or DeltaE acceptance gate changed");

    passed &= expect(
        appearance_non_emission_candidate_accepted(
            AppearanceNonEmissionCandidateAcceptance{
                256,
                0,
                true,
                true,
                true,
                1.0,
                0.8,
                0.04,
                4,
                0.5,
                0.505,
                0.2,
                0.2,
            }) &&
            !appearance_non_emission_candidate_accepted(
                AppearanceNonEmissionCandidateAcceptance{
                    256,
                    1,
                    true,
                    true,
                    true,
                    1.0,
                    0.8,
                    0.04,
                    0,
                    std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity(),
                    0.2,
                    0.2,
                }),
        "non-emission acceptance admitted packed-B contamination");

    passed &= expect(
        appearance_emission_roi_accepted(
            AppearanceEmissionRoiAcceptance{
                100,
                90,
                100,
                1,
                true,
                true,
                true,
                1.0,
                0.80,
                0.50,
                0.505,
            }) &&
            !appearance_emission_roi_accepted(
                AppearanceEmissionRoiAcceptance{
                    100,
                    89,
                    100,
                    1,
                    true,
                    true,
                    true,
                    1.0,
                    0.80,
                    0.50,
                    0.505,
                }),
        "the retained emission recall, false-positive, response, or packed-B "
        "gate changed");
    passed &= expect(
        appearance_calibrated_cluster_emissive_supported(
            AppearanceCalibratedClusterEmissiveEvidence{
                64,
                64,
                0.20,
                1.0,
                1.005,
                48,
            }) &&
            !appearance_calibrated_cluster_emissive_supported(
                AppearanceCalibratedClusterEmissiveEvidence{
                    64,
                    63,
                    0.20,
                    1.0,
                    0.80,
                    48,
                }),
        "the endpoint-calibrated cluster response gate changed");

    const auto projected =
        environment_projected_capture_coordinate({0.25, 0.75});
    passed &= expect(
        projected.ok && close(projected.capture_u, 0.25) &&
            close(projected.capture_v, 0.75) &&
            environment_projected_capture_coordinate({0.0, 0.0}).ok &&
            environment_projected_capture_coordinate({1.0, 1.0}).ok &&
            !environment_projected_capture_coordinate({-0.01, 0.5}).ok,
        "environment projection did not preserve normalized screen coordinates");

    const AppearanceRgb dark_emitter_base{0.02, 0.02, 0.02};
    const AppearanceRgb warm_emission{12.0, 4.0, 0.5};
    const AppearanceRgb warm_isolated_hdr{
        appearance_srgb_to_linear(dark_emitter_base.r) + warm_emission.r,
        appearance_srgb_to_linear(dark_emitter_base.g) + warm_emission.g,
        appearance_srgb_to_linear(dark_emitter_base.b) + warm_emission.b,
    };
    const auto rescued_emitter = appearance_rescue_emission_color(
        dark_emitter_base,
        warm_isolated_hdr,
        0.01);
    const auto unchanged_non_emitter = appearance_rescue_emission_color(
        dark_emitter_base,
        {appearance_srgb_to_linear(dark_emitter_base.r) + 0.001,
         appearance_srgb_to_linear(dark_emitter_base.g) + 0.001,
         appearance_srgb_to_linear(dark_emitter_base.b) + 0.001},
        0.01);
    passed &= expect(
        rescued_emitter.applied &&
            close(std::max({rescued_emitter.albedo_srgb.r,
                            rescued_emitter.albedo_srgb.g,
                            rescued_emitter.albedo_srgb.b}),
                  1.0) &&
            rescued_emitter.albedo_srgb.r >
                rescued_emitter.albedo_srgb.g &&
            rescued_emitter.albedo_srgb.g >
                rescued_emitter.albedo_srgb.b &&
            !unchanged_non_emitter.applied &&
            unchanged_non_emitter.albedo_srgb == dark_emitter_base,
        "emission color rescue changed the v1.7.2 bounded chromaticity contract");

    const auto diffuse_feedback = appearance_closed_loop_correction(
        {{0.25, 0.20, 0.10},
         0.0,
         {0.60, 0.40, 0.10},
         {0.30, 0.20, 0.10},
         false});
    const auto albedo_only_feedback =
        appearance_albedo_closed_loop_correction(
            {{0.20, 0.15, 0.10},
             0.25,
             {0.80, 0.45, 0.20},
             {0.30, 0.20, 0.10},
             true});
    passed &= expect(
        diffuse_feedback.supported &&
            diffuse_feedback.albedo_linear.r > 0.25 &&
            diffuse_feedback.albedo_linear.g > 0.20 &&
            close(diffuse_feedback.albedo_linear.b, 0.10) &&
            diffuse_feedback.emissive == 0.0 &&
            albedo_only_feedback.supported &&
            albedo_only_feedback.albedo_linear.r > 0.20 &&
            albedo_only_feedback.albedo_linear.g > 0.15 &&
            close(albedo_only_feedback.emissive, 0.25),
        "closed-loop Albedo correction modified the independent Emissive state");

    auto physical = AppearancePhysicalEmissionEvidenceInput{};
    physical.source_residual_first = {0.80, 0.32, 0.08};
    physical.source_residual_second = {0.79, 0.33, 0.08};
    physical.source_noise_floor_first = 0.050;
    physical.source_noise_floor_second = 0.080;
    physical.source_hdr = {1.20, 0.52, 0.18};
    physical.baseline_hdr = {0.28, 0.18, 0.09};
    physical.endpoint_hdr = {2.20, 0.92, 0.27};
    physical.manual_emissive_floor = 0.10;
    physical.camera_stable = true;
    physical.readback_calibrated = true;
    physical.packed_b_verified = true;
    const auto accepted_physical =
        appearance_physical_emission_evidence(physical);
    const auto warm_material =
        appearance_compose_physical_emission_material(
            {{0.18, 0.16, 0.03},
             physical.source_residual_first,
             physical.source_residual_second,
             physical.manual_emissive_floor,
             accepted_physical.inferred_emissive,
             accepted_physical.accepted});
    auto unstable = physical;
    unstable.source_residual_first = {0.020, 0.018, 0.019};
    unstable.source_residual_second = {0.001, 0.024, 0.002};
    unstable.source_noise_floor_first = 0.010;
    unstable.source_noise_floor_second = 0.010;
    const auto rejected_physical =
        appearance_physical_emission_evidence(unstable);
    passed &= expect(
        accepted_physical.accepted && accepted_physical.source_supported &&
            accepted_physical.target_response_supported &&
            accepted_physical.composed_emissive > 0.10 &&
            accepted_physical.candidate_loss <
                accepted_physical.baseline_loss &&
            warm_material.chromaticity_carrier_applied &&
            warm_material.albedo.r > warm_material.albedo.g * 2.0 &&
            warm_material.albedo.g > warm_material.albedo.b * 2.0 &&
            !rejected_physical.accepted &&
            !rejected_physical.source_supported &&
            rejected_physical.target_response_supported &&
            close(rejected_physical.composed_emissive, 0.10) &&
            close(appearance_compose_physical_emissive(0.65, 0.25),
                  0.65),
        "physical Emissive did not require both repeatable source and target evidence");

    const auto accepted_component =
        appearance_validate_physical_emission_component(
            {1,
             0.10,
             0.08,
             100,
             0.050,
             0.055,
             true,
             true,
             true,
             true,
             1});
    const auto weak_component =
        appearance_validate_physical_emission_component(
            {1,
             0.10,
             0.09,
             100,
             0.050,
             0.055,
             true,
             true,
             true,
             true,
             1});
    passed &= expect(
        accepted_component.accepted &&
            accepted_component.rejection ==
                AppearancePhysicalEmissionComponentRejection::None &&
            !weak_component.accepted &&
            weak_component.rejection ==
                AppearancePhysicalEmissionComponentRejection::
                    RoiImprovementBelowThreshold,
        "physical Emissive component validation lost the one-sample gate");

    auto correction = AppearanceCorrectionFieldInput{};
    correction.vertex_count = 3;
    correction.edges = {{0, 1}, {1, 2}};
    correction.side_vertices = {false, true, false};
    correction.anchors = {
        {0, {0.0, 0.0, 0.0}, 1.0,
         AppearanceCorrectionBoundary::Front},
        {2,
         {std::log(4.0), std::log(4.0), std::log(4.0)},
         1.0,
         AppearanceCorrectionBoundary::Back},
    };
    const auto interpolated = appearance_solve_correction_field(correction);
    auto reversed = correction;
    std::reverse(reversed.anchors.begin(), reversed.anchors.end());
    const auto reversed_result = appearance_solve_correction_field(reversed);
    auto one_boundary = correction;
    one_boundary.anchors.resize(1U);
    const auto extended = appearance_solve_correction_field(one_boundary);
    one_boundary.anchors.clear();
    const auto unanchored = appearance_solve_correction_field(one_boundary);
    passed &= expect(
        interpolated.ok && interpolated.values.size() == 3U &&
            std::abs(interpolated.values[1].r - std::log(2.0)) <
                0.0001 &&
            interpolated.front_anchor_vertices == 1 &&
            interpolated.back_anchor_vertices == 1 &&
            interpolated.side_components == 1 &&
            interpolated.one_boundary_side_components == 0 &&
            interpolated.hash == reversed_result.hash && extended.ok &&
            extended.one_boundary_side_components == 1 &&
            std::abs(extended.values[1].r) < 0.000001 &&
            !unanchored.ok && unanchored.unanchored_side_components == 1 &&
            unanchored.failure ==
                AppearanceCorrectionFieldFailure::SideUnanchored,
        "Front/Back correction anchors did not propagate deterministically across Side");

    if (passed)
    {
        std::cout << "PASS paint_appearance\n";
    }
    return passed ? 0 : 1;
}
