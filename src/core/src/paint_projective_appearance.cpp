#include <meccha/core/paint_appearance.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace meccha::core
{
auto environment_projected_capture_coordinate(
    const EnvironmentProjectedCaptureInput& input)
    -> EnvironmentProjectedCaptureResult
{
    auto output = EnvironmentProjectedCaptureResult{};
    if (!std::isfinite(input.screen_x) ||
        !std::isfinite(input.screen_y) || input.screen_x < 0.0 ||
        input.screen_x > 1.0 || input.screen_y < 0.0 ||
        input.screen_y > 1.0)
    {
        return output;
    }
    output.ok = true;
    output.capture_u = input.screen_x;
    output.capture_v = input.screen_y;
    return output;
}

auto appearance_rescue_emission_color(
    const AppearanceRgb& base_srgb,
    const AppearanceRgb& isolated_hdr,
    double residual_threshold) -> AppearanceEmissionColorRescue
{
    auto output = AppearanceEmissionColorRescue{};
    output.albedo_srgb = appearance_clamp_albedo(base_srgb);
    if (!appearance_rgb_finite(base_srgb) ||
        !appearance_rgb_finite(isolated_hdr) ||
        !std::isfinite(residual_threshold))
    {
        return output;
    }

    const auto residual = appearance_intrinsic_emission_residual(
        isolated_hdr,
        base_srgb);
    output.residual_luminance = appearance_luminance(residual);
    const auto threshold = std::max(0.0, residual_threshold);
    if (!std::isfinite(output.residual_luminance) ||
        output.residual_luminance <= threshold)
    {
        return output;
    }

    const AppearanceRgb compressed_linear{
        residual.r / (1.0 + residual.r),
        residual.g / (1.0 + residual.g),
        residual.b / (1.0 + residual.b),
    };
    const auto peak = std::max(
        {compressed_linear.r,
         compressed_linear.g,
         compressed_linear.b});
    if (!std::isfinite(peak) || peak <= 0.000001)
    {
        return output;
    }
    const auto scale = appearance_srgb_to_linear(1.0) / peak;
    const AppearanceRgb rescued_srgb{
        appearance_linear_to_srgb(
            std::clamp(compressed_linear.r * scale, 0.0, 1.0)),
        appearance_linear_to_srgb(
            std::clamp(compressed_linear.g * scale, 0.0, 1.0)),
        appearance_linear_to_srgb(
            std::clamp(compressed_linear.b * scale, 0.0, 1.0)),
    };
    auto confidence = std::clamp(
        (output.residual_luminance - threshold) /
            std::max(0.01, threshold),
        0.0,
        1.0);
    confidence = confidence * confidence * (3.0 - 2.0 * confidence);
    output.albedo_srgb = appearance_clamp_albedo(
        {output.albedo_srgb.r +
             (rescued_srgb.r - output.albedo_srgb.r) * confidence,
         output.albedo_srgb.g +
             (rescued_srgb.g - output.albedo_srgb.g) * confidence,
         output.albedo_srgb.b +
             (rescued_srgb.b - output.albedo_srgb.b) * confidence});
    output.applied = confidence > 0.0;
    return output;
}

auto appearance_albedo_closed_loop_correction(
    const AppearanceClosedLoopCorrectionInput& input)
    -> AppearanceClosedLoopCorrection
{
    auto output = AppearanceClosedLoopCorrection{};
    output.albedo_linear = appearance_clamp_albedo(input.albedo_linear);
    output.emissive = std::clamp(input.emissive, 0.0, 1.0);
    if (!appearance_rgb_finite(input.albedo_linear) ||
        !appearance_rgb_finite(input.source_hdr) ||
        !appearance_rgb_finite(input.rendered_hdr) ||
        !std::isfinite(input.emissive) || input.source_hdr.r < 0.0 ||
        input.source_hdr.g < 0.0 || input.source_hdr.b < 0.0 ||
        input.rendered_hdr.r < 0.0 || input.rendered_hdr.g < 0.0 ||
        input.rendered_hdr.b < 0.0)
    {
        return output;
    }

    const auto source_display =
        appearance_reinhard_display(input.source_hdr);
    const auto rendered_display =
        appearance_reinhard_display(input.rendered_hdr);
    const std::array<double, 3> albedo{
        output.albedo_linear.r,
        output.albedo_linear.g,
        output.albedo_linear.b,
    };
    const std::array<double, 3> source{
        source_display.r,
        source_display.g,
        source_display.b,
    };
    const std::array<double, 3> rendered{
        rendered_display.r,
        rendered_display.g,
        rendered_display.b,
    };
    auto corrected = albedo;
    constexpr auto ResponseFloor = 1.0 / 255.0;
    constexpr auto MaximumRatioPerPass = 2.0;
    constexpr auto AlbedoStep = 0.75;
    auto display_error = 0.0;
    for (auto channel = std::size_t{}; channel < corrected.size();
         ++channel)
    {
        display_error +=
            std::abs(source[channel] - rendered[channel]);
        const auto ratio = std::clamp(
            (source[channel] + ResponseFloor) /
                (rendered[channel] + ResponseFloor),
            1.0 / MaximumRatioPerPass,
            MaximumRatioPerPass);
        corrected[channel] = std::clamp(
            std::max(albedo[channel], ResponseFloor) *
                std::pow(ratio, AlbedoStep),
            0.0,
            1.0);
    }
    output.albedo_linear =
        {corrected[0], corrected[1], corrected[2]};
    output.display_error = display_error / 3.0;
    output.supported = true;
    return output;
}

auto appearance_closed_loop_correction(
    const AppearanceClosedLoopCorrectionInput& input)
    -> AppearanceClosedLoopCorrection
{
    auto output = appearance_albedo_closed_loop_correction(input);
    output.emissive = input.intrinsic_emission_roi
                          ? std::clamp(input.emissive, 0.0, 1.0)
                          : 0.0;
    if (!output.supported || !input.intrinsic_emission_roi)
    {
        return output;
    }
    const auto source_luminance =
        std::max(0.0, appearance_luminance(input.source_hdr));
    const auto rendered_luminance =
        std::max(0.0, appearance_luminance(input.rendered_hdr));
    const auto log_luminance_error =
        std::log1p(source_luminance) -
        std::log1p(rendered_luminance);
    constexpr auto EmissiveStep = 0.50;
    constexpr auto MaximumEmissiveDeltaPerPass = 0.35;
    const auto emissive_delta = std::clamp(
        log_luminance_error * EmissiveStep,
        -MaximumEmissiveDeltaPerPass,
        MaximumEmissiveDeltaPerPass);
    output.emissive =
        std::clamp(output.emissive + emissive_delta, 0.0, 1.0);
    return output;
}

auto appearance_compose_physical_emissive(
    double manual_emissive_floor,
    double inferred_emissive) -> double
{
    if (!std::isfinite(manual_emissive_floor) ||
        !std::isfinite(inferred_emissive))
    {
        return 0.0;
    }
    return std::max(
        std::clamp(manual_emissive_floor, 0.0, 1.0),
        std::clamp(inferred_emissive, 0.0, 1.0));
}

auto appearance_compose_physical_emission_material(
    const AppearancePhysicalEmissionMaterialInput& input)
    -> AppearancePhysicalEmissionMaterial
{
    auto output = AppearancePhysicalEmissionMaterial{};
    output.albedo = appearance_clamp_albedo(input.albedo);
    output.emissive = appearance_compose_physical_emissive(
        input.manual_emissive_floor,
        input.inferred_emissive);
    if (!input.dual_evidence_accepted ||
        !appearance_rgb_finite(input.source_residual_first) ||
        !appearance_rgb_finite(input.source_residual_second))
    {
        return output;
    }

    const AppearanceRgb mean_positive_residual{
        std::max(0.0,
                 (input.source_residual_first.r +
                  input.source_residual_second.r) *
                     0.5),
        std::max(0.0,
                 (input.source_residual_first.g +
                  input.source_residual_second.g) *
                     0.5),
        std::max(0.0,
                 (input.source_residual_first.b +
                  input.source_residual_second.b) *
                     0.5),
    };
    const auto peak = std::max(
        {mean_positive_residual.r,
         mean_positive_residual.g,
         mean_positive_residual.b});
    if (!std::isfinite(peak) || peak <= 0.000001)
    {
        return output;
    }
    output.albedo = appearance_emission_chromaticity_albedo(
        mean_positive_residual,
        output.albedo);
    output.chromaticity_carrier_applied = true;
    return output;
}

auto appearance_physical_emission_evidence(
    const AppearancePhysicalEmissionEvidenceInput& input)
    -> AppearancePhysicalEmissionEvidence
{
    auto output = AppearancePhysicalEmissionEvidence{};
    const auto manual_floor =
        std::clamp(input.manual_emissive_floor, 0.0, 1.0);
    output.inferred_emissive = manual_floor;
    output.composed_emissive = manual_floor;
    if (!appearance_rgb_finite(input.source_residual_first) ||
        !appearance_rgb_finite(input.source_residual_second) ||
        !appearance_rgb_finite(input.source_hdr) ||
        !appearance_rgb_finite(input.baseline_hdr) ||
        !appearance_rgb_finite(input.endpoint_hdr) ||
        !std::isfinite(input.manual_emissive_floor))
    {
        return output;
    }

    const auto positive = [](const AppearanceRgb& value) {
        return AppearanceRgb{
            std::max(0.0, value.r),
            std::max(0.0, value.g),
            std::max(0.0, value.b),
        };
    };
    const auto first = positive(input.source_residual_first);
    const auto second = positive(input.source_residual_second);
    const AppearanceRgb repeatability_delta{
        std::abs(first.r - second.r),
        std::abs(first.g - second.g),
        std::abs(first.b - second.b),
    };
    const auto first_luminance = appearance_luminance(first);
    const auto second_luminance = appearance_luminance(second);
    const auto minimum_luminance =
        std::min(first_luminance, second_luminance);
    output.source_noise_floor_calibrated =
        std::isfinite(input.source_noise_floor_first) &&
        input.source_noise_floor_first >= 0.0 &&
        std::isfinite(input.source_noise_floor_second) &&
        input.source_noise_floor_second >= 0.0;
    const auto first_noise_floor = std::max(
        AppearancePhysicalEmissionReadbackFloor,
        input.source_noise_floor_first);
    const auto second_noise_floor = std::max(
        AppearancePhysicalEmissionReadbackFloor,
        input.source_noise_floor_second);
    output.source_first_above_noise_floor =
        output.source_noise_floor_calibrated &&
        first_luminance > first_noise_floor;
    output.source_second_above_noise_floor =
        output.source_noise_floor_calibrated &&
        second_luminance > second_noise_floor;
    output.source_repeatability_error =
        appearance_luminance(repeatability_delta);
    output.source_chromaticity_delta =
        appearance_rgb_chromaticity_delta(first, second);
    output.source_supported =
        input.source_distribution_separated &&
        output.source_first_above_noise_floor &&
        output.source_second_above_noise_floor &&
        std::isfinite(minimum_luminance) &&
        minimum_luminance >
            output.source_repeatability_error +
                AppearancePhysicalEmissionReadbackFloor &&
        std::isfinite(output.source_chromaticity_delta) &&
        output.source_chromaticity_delta <=
            AppearancePhysicalEmissionMaximumChromaticityDelta;

    if (manual_floor >= 1.0 || !input.camera_stable ||
        !input.readback_calibrated || !input.packed_b_verified)
    {
        return output;
    }
    const auto calibrated = appearance_calibrate_bounded_response(
        input.source_hdr,
        input.baseline_hdr,
        input.endpoint_hdr,
        manual_floor,
        1.0,
        manual_floor,
        1.0);
    output.response_energy = calibrated.response_energy;
    if (!calibrated.supported ||
        appearance_luminance(input.endpoint_hdr) <=
            appearance_luminance(input.baseline_hdr) + 0.0001)
    {
        return output;
    }

    const auto log_channel = [](double channel) {
        return std::log1p(std::max(0.0, channel));
    };
    const auto interpolation =
        (calibrated.parameter - manual_floor) /
        std::max(0.000001, 1.0 - manual_floor);
    const std::array<double, 3> source{
        log_channel(input.source_hdr.r),
        log_channel(input.source_hdr.g),
        log_channel(input.source_hdr.b),
    };
    const std::array<double, 3> baseline{
        log_channel(input.baseline_hdr.r),
        log_channel(input.baseline_hdr.g),
        log_channel(input.baseline_hdr.b),
    };
    const std::array<double, 3> endpoint{
        log_channel(input.endpoint_hdr.r),
        log_channel(input.endpoint_hdr.g),
        log_channel(input.endpoint_hdr.b),
    };
    output.baseline_loss = 0.0;
    output.candidate_loss = 0.0;
    for (auto channel = std::size_t{}; channel < source.size(); ++channel)
    {
        const auto predicted =
            baseline[channel] +
            (endpoint[channel] - baseline[channel]) * interpolation;
        output.baseline_loss += appearance_huber_loss(
            source[channel] - baseline[channel]);
        output.candidate_loss += appearance_huber_loss(
            source[channel] - predicted);
    }
    output.baseline_loss /= 3.0;
    output.candidate_loss /= 3.0;
    const auto manual_quantized = std::llround(manual_floor * 255.0);
    const auto inferred_quantized =
        std::llround(calibrated.parameter * 255.0);
    output.target_response_supported =
        inferred_quantized > manual_quantized &&
        std::isfinite(output.baseline_loss) &&
        std::isfinite(output.candidate_loss) &&
        output.candidate_loss + 0.000001 < output.baseline_loss;
    output.inferred_emissive = calibrated.parameter;
    output.accepted =
        output.source_supported && output.target_response_supported;
    if (output.accepted)
    {
        output.composed_emissive = appearance_compose_physical_emissive(
            manual_floor,
            output.inferred_emissive);
    }
    return output;
}

auto appearance_validate_physical_emission_component(
    const AppearancePhysicalEmissionComponentValidationInput& input)
    -> AppearancePhysicalEmissionComponentValidation
{
    auto output = AppearancePhysicalEmissionComponentValidation{};
    if (!input.dual_evidence_prevalidated)
    {
        return output;
    }
    if (!input.camera_stable)
    {
        output.rejection =
            AppearancePhysicalEmissionComponentRejection::CameraUnstable;
        return output;
    }
    if (!input.readback_calibrated)
    {
        output.rejection = AppearancePhysicalEmissionComponentRejection::
            ReadbackUncalibrated;
        return output;
    }
    if (!input.packed_b_verified)
    {
        output.rejection = AppearancePhysicalEmissionComponentRejection::
            PackedBNotVerified;
        return output;
    }
    if (input.painted_emissive_nonzero_pixels <= 0)
    {
        output.rejection = AppearancePhysicalEmissionComponentRejection::
            QuantizedEmissiveZero;
        return output;
    }

    output.non_emission_loss_delta =
        input.non_emission_paired_samples > 0 &&
                std::isfinite(input.baseline_non_emission_loss) &&
                std::isfinite(input.candidate_non_emission_loss)
            ? input.candidate_non_emission_loss -
                  input.baseline_non_emission_loss
            : 0.0;
    if (input.non_emission_paired_samples > 0 &&
        (!std::isfinite(output.non_emission_loss_delta) ||
         output.non_emission_loss_delta > 0.01))
    {
        output.rejection = AppearancePhysicalEmissionComponentRejection::
            NonEmissionLossRegressed;
        return output;
    }
    if (input.paired_samples > 0)
    {
        output.roi_improvement =
            std::isfinite(input.baseline_loss) &&
                    std::isfinite(input.candidate_loss) &&
                    input.baseline_loss > 0.0
                ? (input.baseline_loss - input.candidate_loss) /
                      input.baseline_loss
                : -std::numeric_limits<double>::infinity();
        if (!std::isfinite(output.roi_improvement) ||
                output.roi_improvement <
                    AppearancePhysicalEmissionMinimumImprovement)
        {
            output.rejection = AppearancePhysicalEmissionComponentRejection::
                RoiImprovementBelowThreshold;
            return output;
        }
    }
    output.accepted = true;
    output.rejection = AppearancePhysicalEmissionComponentRejection::None;
    return output;
}

auto appearance_solve_correction_field(
    const AppearanceCorrectionFieldInput& input)
    -> AppearanceCorrectionFieldResult
{
    auto output = AppearanceCorrectionFieldResult{};
    if (input.vertex_count <= 0 ||
        input.side_vertices.size() !=
            static_cast<std::size_t>(input.vertex_count))
    {
        output.failure = AppearanceCorrectionFieldFailure::InvalidInput;
        return output;
    }

    auto adjacency = std::vector<std::vector<int>>(
        static_cast<std::size_t>(input.vertex_count));
    for (const auto& edge : input.edges)
    {
        if (edge.first < 0 || edge.second < 0 ||
            edge.first >= input.vertex_count ||
            edge.second >= input.vertex_count || edge.first == edge.second)
        {
            output.failure = AppearanceCorrectionFieldFailure::InvalidInput;
            return output;
        }
        adjacency[static_cast<std::size_t>(edge.first)].push_back(
            edge.second);
        adjacency[static_cast<std::size_t>(edge.second)].push_back(
            edge.first);
    }
    for (auto& neighbours : adjacency)
    {
        std::sort(neighbours.begin(), neighbours.end());
        neighbours.erase(
            std::unique(neighbours.begin(), neighbours.end()),
            neighbours.end());
    }

    struct WeightedValue
    {
        double value{};
        double weight{};
    };
    auto contributions =
        std::vector<std::array<std::vector<WeightedValue>, 3>>(
            static_cast<std::size_t>(input.vertex_count));
    auto front_anchor = std::vector<bool>(
        static_cast<std::size_t>(input.vertex_count), false);
    auto back_anchor = std::vector<bool>(
        static_cast<std::size_t>(input.vertex_count), false);
    for (const auto& anchor : input.anchors)
    {
        if (anchor.vertex < 0 || anchor.vertex >= input.vertex_count ||
            !appearance_rgb_finite(anchor.value) ||
            !std::isfinite(anchor.weight) || anchor.weight <= 0.0)
        {
            continue;
        }
        auto& target =
            contributions[static_cast<std::size_t>(anchor.vertex)];
        target[0].push_back({anchor.value.r, anchor.weight});
        target[1].push_back({anchor.value.g, anchor.weight});
        target[2].push_back({anchor.value.b, anchor.weight});
        if (anchor.boundary == AppearanceCorrectionBoundary::Front)
        {
            front_anchor[static_cast<std::size_t>(anchor.vertex)] = true;
        }
        else
        {
            back_anchor[static_cast<std::size_t>(anchor.vertex)] = true;
        }
    }

    output.values.assign(
        static_cast<std::size_t>(input.vertex_count), {});
    output.resolved.assign(
        static_cast<std::size_t>(input.vertex_count), false);
    auto anchored = std::vector<bool>(
        static_cast<std::size_t>(input.vertex_count), false);
    const auto weighted_median = [](std::vector<WeightedValue> values) {
        std::sort(values.begin(), values.end(), [](const auto& left,
                                                   const auto& right) {
            if (left.value != right.value)
            {
                return left.value < right.value;
            }
            return left.weight < right.weight;
        });
        auto total = 0.0;
        for (const auto& value : values)
        {
            total += value.weight;
        }
        const auto middle = total * 0.5;
        auto cumulative = 0.0;
        for (const auto& value : values)
        {
            cumulative += value.weight;
            if (cumulative >= middle)
            {
                return value.value;
            }
        }
        return values.empty() ? 0.0 : values.back().value;
    };
    for (auto vertex = 0; vertex < input.vertex_count; ++vertex)
    {
        const auto index = static_cast<std::size_t>(vertex);
        if (contributions[index][0].empty())
        {
            continue;
        }
        output.values[index] = {
            weighted_median(contributions[index][0]),
            weighted_median(contributions[index][1]),
            weighted_median(contributions[index][2]),
        };
        anchored[index] = true;
        output.resolved[index] = true;
        output.front_anchor_vertices += front_anchor[index] ? 1 : 0;
        output.back_anchor_vertices += back_anchor[index] ? 1 : 0;
    }

    auto component_by_vertex = std::vector<int>(
        static_cast<std::size_t>(input.vertex_count), -1);
    auto components = std::vector<std::vector<int>>{};
    auto stack = std::vector<int>{};
    for (auto first = 0; first < input.vertex_count; ++first)
    {
        if (component_by_vertex[static_cast<std::size_t>(first)] >= 0)
        {
            continue;
        }
        const auto component_index = static_cast<int>(components.size());
        components.push_back({});
        stack.clear();
        stack.push_back(first);
        component_by_vertex[static_cast<std::size_t>(first)] =
            component_index;
        while (!stack.empty())
        {
            const auto vertex = stack.back();
            stack.pop_back();
            components.back().push_back(vertex);
            for (const auto neighbour :
                 adjacency[static_cast<std::size_t>(vertex)])
            {
                if (component_by_vertex[
                        static_cast<std::size_t>(neighbour)] >= 0)
                {
                    continue;
                }
                component_by_vertex[static_cast<std::size_t>(neighbour)] =
                    component_index;
                stack.push_back(neighbour);
            }
        }
    }

    for (const auto& component : components)
    {
        auto has_side = false;
        auto has_front = false;
        auto has_back = false;
        auto anchor_count = 0;
        auto anchor_mean = AppearanceRgb{};
        for (const auto vertex : component)
        {
            const auto index = static_cast<std::size_t>(vertex);
            has_side = has_side || input.side_vertices[index];
            has_front = has_front || front_anchor[index];
            has_back = has_back || back_anchor[index];
            if (anchored[index])
            {
                anchor_mean.r += output.values[index].r;
                anchor_mean.g += output.values[index].g;
                anchor_mean.b += output.values[index].b;
                ++anchor_count;
            }
        }
        if (has_side)
        {
            ++output.side_components;
        }
        if (has_side && anchor_count == 0)
        {
            ++output.unanchored_side_components;
            continue;
        }
        if (has_side && (has_front != has_back))
        {
            ++output.one_boundary_side_components;
        }
        if (anchor_count == 0)
        {
            continue;
        }
        anchor_mean.r /= static_cast<double>(anchor_count);
        anchor_mean.g /= static_cast<double>(anchor_count);
        anchor_mean.b /= static_cast<double>(anchor_count);
        for (const auto vertex : component)
        {
            const auto index = static_cast<std::size_t>(vertex);
            if (!anchored[index])
            {
                output.values[index] = anchor_mean;
                output.resolved[index] = true;
            }
        }
    }

    constexpr auto MaximumIterations = 512;
    constexpr auto ConvergenceEpsilon = 0.0000001;
    auto next = output.values;
    for (auto iteration = 0; iteration < MaximumIterations; ++iteration)
    {
        auto maximum_delta = 0.0;
        for (auto vertex = 0; vertex < input.vertex_count; ++vertex)
        {
            const auto index = static_cast<std::size_t>(vertex);
            if (anchored[index] || !output.resolved[index] ||
                adjacency[index].empty())
            {
                next[index] = output.values[index];
                continue;
            }
            auto average = AppearanceRgb{};
            auto neighbours = 0;
            for (const auto neighbour : adjacency[index])
            {
                const auto neighbour_index =
                    static_cast<std::size_t>(neighbour);
                if (!output.resolved[neighbour_index])
                {
                    continue;
                }
                average.r += output.values[neighbour_index].r;
                average.g += output.values[neighbour_index].g;
                average.b += output.values[neighbour_index].b;
                ++neighbours;
            }
            if (neighbours <= 0)
            {
                continue;
            }
            average.r /= static_cast<double>(neighbours);
            average.g /= static_cast<double>(neighbours);
            average.b /= static_cast<double>(neighbours);
            maximum_delta = std::max(
                maximum_delta,
                std::max({std::abs(average.r - output.values[index].r),
                          std::abs(average.g - output.values[index].g),
                          std::abs(average.b - output.values[index].b)}));
            next[index] = average;
        }
        output.values.swap(next);
        output.iterations = iteration + 1;
        if (maximum_delta <= ConvergenceEpsilon)
        {
            break;
        }
    }

    const auto hash_mix = [&output](std::uint64_t value) {
        output.hash ^= value;
        output.hash *= 1099511628211ULL;
    };
    hash_mix(static_cast<std::uint64_t>(input.vertex_count));
    for (auto index = std::size_t{}; index < output.values.size(); ++index)
    {
        hash_mix(output.resolved[index] ? 1ULL : 0ULL);
        if (!output.resolved[index])
        {
            continue;
        }
        const std::array<double, 3> channels{
            output.values[index].r,
            output.values[index].g,
            output.values[index].b,
        };
        for (const auto channel : channels)
        {
            const auto quantized = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    std::llround(channel * 1000000.0)));
            hash_mix(quantized);
        }
    }
    output.ok = output.unanchored_side_components == 0;
    output.failure = output.ok
                         ? AppearanceCorrectionFieldFailure::None
                         : AppearanceCorrectionFieldFailure::SideUnanchored;
    return output;
}
} // namespace meccha::core
