#include <meccha/core/paint_projective_pipeline.hpp>

#include <meccha/core/paint_capture_request.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stop_token>
#include <unordered_map>
#include <utility>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto ResponseFloor = 1.0 / 255.0;

auto pixel_count(const PaintProjectiveModel& model)
    -> std::optional<std::size_t>
{
    if (model.width == 0U || model.height == 0U ||
        model.width > MaximumPaintCaptureDimension ||
        model.height > MaximumPaintCaptureDimension ||
        model.width > std::numeric_limits<std::size_t>::max() /
                          model.height)
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(model.width) * model.height;
}

auto finite_unit(double value) -> bool
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

auto sample_valid(
    const PaintProjectiveSample& sample,
    std::size_t pixels) -> bool
{
    const auto barycentric_sum =
        sample.barycentric_a + sample.barycentric_b +
        sample.barycentric_c;
    return sample.raster_pixel < pixels &&
           finite_unit(sample.u) && finite_unit(sample.v) &&
           std::isfinite(sample.barycentric_a) &&
           std::isfinite(sample.barycentric_b) &&
           std::isfinite(sample.barycentric_c) &&
           std::abs(barycentric_sum - 1.0) <= 0.0001 &&
           appearance_rgb_finite(sample.base_albedo) &&
           appearance_rgb_finite(sample.source_final_hdr) &&
           appearance_rgb_finite(sample.source_residual_first) &&
           appearance_rgb_finite(sample.source_residual_second);
}

auto to_rgb8(const AppearanceRgb& linear) -> Rgb8
{
    const auto channel = [](double value) {
        return static_cast<std::uint8_t>(std::lround(
            std::clamp(appearance_linear_to_srgb(value), 0.0, 1.0) *
            255.0));
    };
    return {channel(linear.r), channel(linear.g), channel(linear.b)};
}

auto manual_appearance(
    const AppearanceRgb& albedo,
    const PaintSettings& settings,
    double emissive) -> ResolvedPaintAppearance
{
    return ResolvedPaintAppearance{
        to_rgb8(appearance_clamp_albedo(albedo)),
        Material{
            settings.paint_material.metallic,
            settings.paint_material.roughness,
            std::clamp(emissive, 0.0, 1.0),
        },
    };
}

auto empty_raster(
    std::size_t pixels,
    const PaintSettings& settings) -> PaintProjectiveRaster
{
    return PaintProjectiveRaster{
        std::vector<ResolvedPaintAppearance>(
            pixels,
            manual_appearance({}, settings,
                              settings.paint_material.emissive)),
        std::vector<bool>(pixels, false),
    };
}

auto transformed(
    AppearanceRgb value,
    AppearanceReadbackTransform transform) -> AppearanceRgb
{
    if (transform == AppearanceReadbackTransform::SwapRedBlue)
    {
        std::swap(value.r, value.b);
    }
    return value;
}

auto log_loss(AppearanceRgb expected, AppearanceRgb actual)
    -> double
{
    if (!appearance_rgb_finite(expected) ||
        !appearance_rgb_finite(actual))
    {
        return std::numeric_limits<double>::infinity();
    }
    const auto channel = [](double value) {
        return std::log1p(std::max(0.0, value));
    };
    return (appearance_huber_loss(
                channel(expected.r) - channel(actual.r)) +
            appearance_huber_loss(
                channel(expected.g) - channel(actual.g)) +
            appearance_huber_loss(
                channel(expected.b) - channel(actual.b))) /
           3.0;
}

struct CorrectionTopology
{
    int vertex_count{};
    std::vector<AppearanceCorrectionFieldEdge> edges{};
    std::vector<bool> side_vertices{};
};

auto correction_topology(const PaintProjectiveModel& model)
    -> std::optional<CorrectionTopology>
{
    auto maximum_vertex = std::uint32_t{};
    for (const auto& sample : model.samples)
    {
        maximum_vertex = std::max(
            {maximum_vertex,
             sample.first_vertex,
             sample.second_vertex,
             sample.third_vertex});
    }
    if (model.samples.empty() ||
        maximum_vertex >= MaximumPaintProjectiveSamples * 3U)
    {
        return std::nullopt;
    }
    auto output = CorrectionTopology{};
    output.vertex_count = static_cast<int>(maximum_vertex + 1U);
    output.side_vertices.assign(
        static_cast<std::size_t>(output.vertex_count), false);
    output.edges.reserve(model.samples.size() * 3U);
    for (const auto& sample : model.samples)
    {
        const std::array<std::uint32_t, 3> vertices{
            sample.first_vertex,
            sample.second_vertex,
            sample.third_vertex,
        };
        for (const auto vertex : vertices)
        {
            if (vertex >= static_cast<std::uint32_t>(output.vertex_count))
            {
                return std::nullopt;
            }
            if (sample.region == Region::Side)
            {
                output.side_vertices[vertex] = true;
            }
        }
        output.edges.push_back(
            {static_cast<int>(vertices[0]),
             static_cast<int>(vertices[1])});
        output.edges.push_back(
            {static_cast<int>(vertices[1]),
             static_cast<int>(vertices[2])});
        output.edges.push_back(
            {static_cast<int>(vertices[2]),
             static_cast<int>(vertices[0])});
    }
    return output;
}

auto interpolate_field(
    const PaintProjectiveSample& sample,
    const AppearanceCorrectionFieldResult& field)
    -> std::optional<AppearanceRgb>
{
    const std::array<std::uint32_t, 3> vertices{
        sample.first_vertex,
        sample.second_vertex,
        sample.third_vertex,
    };
    const std::array<double, 3> weights{
        sample.barycentric_a,
        sample.barycentric_b,
        sample.barycentric_c,
    };
    auto value = AppearanceRgb{};
    for (auto corner = std::size_t{}; corner < vertices.size(); ++corner)
    {
        const auto vertex = static_cast<std::size_t>(vertices[corner]);
        if (vertex >= field.values.size() ||
            vertex >= field.resolved.size() || !field.resolved[vertex])
        {
            return std::nullopt;
        }
        value.r += field.values[vertex].r * weights[corner];
        value.g += field.values[vertex].g * weights[corner];
        value.b += field.values[vertex].b * weights[corner];
    }
    return appearance_rgb_finite(value)
               ? std::optional<AppearanceRgb>{value}
               : std::nullopt;
}

auto corrected_albedo(
    AppearanceRgb albedo,
    AppearanceRgb log_gain) -> AppearanceRgb
{
    return appearance_clamp_albedo({
        (albedo.r + ResponseFloor) * std::exp(log_gain.r) -
            ResponseFloor,
        (albedo.g + ResponseFloor) * std::exp(log_gain.g) -
            ResponseFloor,
        (albedo.b + ResponseFloor) * std::exp(log_gain.b) -
            ResponseFloor,
    });
}

auto source_emission_supported(
    const PaintProjectiveSample& sample,
    const PaintProjectiveModel& model) -> bool
{
    return appearance_physical_emission_evidence(
               AppearancePhysicalEmissionEvidenceInput{
                   sample.source_residual_first,
                   sample.source_residual_second,
                   model.source_noise_first.ok
                       ? model.source_noise_first.threshold
                       : std::numeric_limits<double>::infinity(),
                   model.source_noise_second.ok
                       ? model.source_noise_second.threshold
                       : std::numeric_limits<double>::infinity(),
                   sample.source_final_hdr,
                   {},
                   {},
                   0.0,
                   model.source_noise_first.separated_signal &&
                       model.source_noise_second.separated_signal,
                   false,
                   false,
                   false,
               })
        .source_supported;
}

class DisjointSet
{
public:
    explicit DisjointSet(std::size_t size) : parent_(size)
    {
        std::iota(parent_.begin(), parent_.end(), std::size_t{});
    }

    auto find(std::size_t value) -> std::size_t
    {
        while (parent_[value] != value)
        {
            parent_[value] = parent_[parent_[value]];
            value = parent_[value];
        }
        return value;
    }

    auto unite(std::size_t first, std::size_t second) -> void
    {
        first = find(first);
        second = find(second);
        if (first != second)
        {
            parent_[std::max(first, second)] = std::min(first, second);
        }
    }

private:
    std::vector<std::size_t> parent_{};
};
} // namespace

auto prepare_paint_projective_model(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const PaintProjectiveObservation> observations,
    std::stop_token cancellation)
    -> std::expected<PaintProjectiveModel, PaintProjectiveError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(PaintProjectiveError::Cancelled);
    }
    if (width == 0U || height == 0U ||
        width > MaximumPaintCaptureDimension ||
        height > MaximumPaintCaptureDimension ||
        width > std::numeric_limits<std::size_t>::max() / height ||
        observations.empty())
    {
        return std::unexpected(PaintProjectiveError::InvalidInput);
    }
    if (observations.size() > MaximumPaintProjectiveSamples)
    {
        return std::unexpected(PaintProjectiveError::ResourceLimit);
    }
    const auto pixels = static_cast<std::size_t>(width) * height;
    auto output = PaintProjectiveModel{width, height};
    output.samples.reserve(observations.size());
    auto first_luminance = std::vector<double>{};
    auto second_luminance = std::vector<double>{};
    first_luminance.reserve(observations.size());
    second_luminance.reserve(observations.size());
    for (auto index = std::size_t{}; index < observations.size(); ++index)
    {
        if ((index % 256U) == 0U && cancellation.stop_requested())
        {
            return std::unexpected(PaintProjectiveError::Cancelled);
        }
        const auto& observation = observations[index];
        const auto base_srgb = AppearanceRgb{
            static_cast<double>(observation.base_color.red) / 255.0,
            static_cast<double>(observation.base_color.green) / 255.0,
            static_cast<double>(observation.base_color.blue) / 255.0,
        };
        const auto sample = PaintProjectiveSample{
            observation.geometry_index,
            observation.raster_pixel,
            observation.region,
            observation.uv_island,
            observation.u,
            observation.v,
            observation.triangle_index,
            observation.first_vertex,
            observation.second_vertex,
            observation.third_vertex,
            observation.barycentric_a,
            observation.barycentric_b,
            observation.barycentric_c,
            observation.replay_relevant,
            observation.calibration_sample,
            observation.safe,
            AppearanceRgb{
                appearance_srgb_to_linear(base_srgb.r),
                appearance_srgb_to_linear(base_srgb.g),
                appearance_srgb_to_linear(base_srgb.b),
            },
            observation.final_hdr,
            appearance_intrinsic_emission_residual(
                observation.intrinsic_emission_first_hdr,
                base_srgb),
            appearance_intrinsic_emission_residual(
                observation.intrinsic_emission_second_hdr,
                base_srgb),
            observation.source_surface_key,
        };
        if (!sample_valid(sample, pixels))
        {
            return std::unexpected(PaintProjectiveError::InvalidEvidence);
        }
        if (!sample.safe)
        {
            continue;
        }
        output.replay_samples += sample.replay_relevant ? 1U : 0U;
        output.calibration_samples += sample.calibration_sample ? 1U : 0U;
        first_luminance.push_back(
            appearance_luminance(sample.source_residual_first));
        second_luminance.push_back(
            appearance_luminance(sample.source_residual_second));
        output.samples.push_back(sample);
    }
    if (output.replay_samples == 0U || output.calibration_samples == 0U)
    {
        return std::unexpected(PaintProjectiveError::NoSupportedSamples);
    }
    output.source_noise_first =
        appearance_source_emission_noise_model(first_luminance);
    output.source_noise_second =
        appearance_source_emission_noise_model(second_luminance);
    return output;
}

auto build_paint_projective_baseline(
    const PaintProjectiveModel& model,
    const PaintSettings& settings,
    std::stop_token cancellation)
    -> std::expected<PaintProjectiveRaster, PaintProjectiveError>
{
    const auto pixels = pixel_count(model);
    if (!pixels || !validate(settings).empty() || model.samples.empty())
    {
        return std::unexpected(PaintProjectiveError::InvalidModel);
    }
    auto output = empty_raster(*pixels, settings);
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if ((index % 256U) == 0U && cancellation.stop_requested())
        {
            return std::unexpected(PaintProjectiveError::Cancelled);
        }
        const auto& sample = model.samples[index];
        if (!sample_valid(sample, *pixels) || !sample.safe)
        {
            continue;
        }
        output.appearances[sample.raster_pixel] = manual_appearance(
            sample.base_albedo,
            settings,
            settings.paint_material.emissive);
        output.available[sample.raster_pixel] = true;
    }
    if (std::ranges::none_of(output.available, [](bool value) {
            return value;
        }))
    {
        return std::unexpected(PaintProjectiveError::NoSupportedSamples);
    }
    return output;
}

auto build_paint_projective_trial_plan(
    const PaintProjectiveModel& model,
    std::span<const ResolvedPaintAppearance> appearances,
    std::uint32_t texture_dimension,
    std::stop_token cancellation)
    -> std::expected<PaintPlan, PaintProjectiveError>
{
    const auto pixels = pixel_count(model);
    if (!pixels || appearances.size() != *pixels ||
        texture_dimension == 0U ||
        texture_dimension > MaximumPaintCaptureDimension)
    {
        return std::unexpected(PaintProjectiveError::InvalidInput);
    }
    auto output = PaintPlan{};
    output.texture_dimension = texture_dimension;
    output.strokes.reserve(model.calibration_samples);
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if ((index % 256U) == 0U && cancellation.stop_requested())
        {
            return std::unexpected(PaintProjectiveError::Cancelled);
        }
        const auto& sample = model.samples[index];
        if (!sample.safe || !sample.calibration_sample ||
            sample.raster_pixel >= appearances.size())
        {
            continue;
        }
        const auto& appearance = appearances[sample.raster_pixel];
        output.strokes.push_back(PaintStroke{
            sample.geometry_index,
            ReplayPass::Paint,
            sample.region,
            sample.u,
            sample.v,
            AppearanceCalibrationStepTexels,
            appearance.color,
            appearance.material,
        });
    }
    output.paint_count = output.strokes.size();
    output.source_paint_count = output.paint_count;
    if (output.strokes.empty())
    {
        return std::unexpected(PaintProjectiveError::NoSupportedSamples);
    }
    return output;
}

auto evaluate_paint_projective_feedback(
    const PaintProjectiveModel& model,
    std::span<const AppearanceRgb> target_hdr,
    bool camera_stable,
    bool readback_calibrated,
    AppearanceReadbackTransform transform,
    std::stop_token cancellation)
    -> std::expected<PaintProjectiveFeedback, PaintProjectiveError>
{
    const auto pixels = pixel_count(model);
    if (!pixels || target_hdr.size() != *pixels || model.samples.empty())
    {
        return std::unexpected(PaintProjectiveError::InvalidEvidence);
    }
    auto output = PaintProjectiveFeedback{
        camera_stable,
        readback_calibrated,
        transform,
        std::vector<AppearanceHdrSample>(model.samples.size()),
    };
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if ((index % 256U) == 0U && cancellation.stop_requested())
        {
            return std::unexpected(PaintProjectiveError::Cancelled);
        }
        const auto& sample = model.samples[index];
        if (!sample.safe || sample.raster_pixel >= target_hdr.size())
        {
            continue;
        }
        output.target_hdr_by_sample[index] = appearance_sanitize_hdr(
            transformed(target_hdr[sample.raster_pixel], transform));
    }
    return output;
}

auto calibrate_paint_projective_baseline(
    const PaintProjectiveModel& model,
    const PaintProjectiveFeedback& baseline,
    const AppearanceEmissionNoiseModel& target_e0_noise,
    const PaintSettings& settings,
    bool packed_b_verified,
    std::stop_token cancellation)
    -> std::expected<PaintProjectiveCalibration, PaintProjectiveError>
{
    const auto pixels = pixel_count(model);
    const auto topology = correction_topology(model);
    if (!pixels || !topology || !validate(settings).empty() ||
        baseline.target_hdr_by_sample.size() != model.samples.size() ||
        !baseline.camera_stable || !baseline.readback_calibrated ||
        !packed_b_verified || !target_e0_noise.ok)
    {
        return std::unexpected(PaintProjectiveError::InvalidEvidence);
    }
    auto field_input = AppearanceCorrectionFieldInput{
        topology->vertex_count,
        topology->edges,
        topology->side_vertices,
        {},
    };
    auto output = PaintProjectiveCalibration{};
    output.corrected_albedo_by_sample.resize(model.samples.size());
    output.corrected_albedo_available.assign(model.samples.size(), false);
    output.physical_emission_candidate.assign(model.samples.size(), false);
    output.endpoint = empty_raster(*pixels, settings);
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if ((index % 256U) == 0U && cancellation.stop_requested())
        {
            return std::unexpected(PaintProjectiveError::Cancelled);
        }
        const auto& sample = model.samples[index];
        output.physical_emission_candidate[index] =
            source_emission_supported(sample, model);
        output.emission_candidates +=
            output.physical_emission_candidate[index] ? 1 : 0;
        if (!sample.safe || !sample.calibration_sample ||
            sample.region == Region::Side)
        {
            continue;
        }
        const auto& rendered = baseline.target_hdr_by_sample[index];
        if (!rendered.finite || rendered.clipped)
        {
            continue;
        }
        const auto corrected = appearance_albedo_closed_loop_correction(
            AppearanceClosedLoopCorrectionInput{
                sample.base_albedo,
                settings.paint_material.emissive,
                sample.source_final_hdr,
                rendered.value,
                output.physical_emission_candidate[index],
            });
        if (!corrected.supported)
        {
            continue;
        }
        const auto log_gain = AppearanceRgb{
            std::log((corrected.albedo_linear.r + ResponseFloor) /
                     (sample.base_albedo.r + ResponseFloor)),
            std::log((corrected.albedo_linear.g + ResponseFloor) /
                     (sample.base_albedo.g + ResponseFloor)),
            std::log((corrected.albedo_linear.b + ResponseFloor) /
                     (sample.base_albedo.b + ResponseFloor)),
        };
        const auto boundary = sample.region == Region::Back
                                  ? AppearanceCorrectionBoundary::Back
                                  : AppearanceCorrectionBoundary::Front;
        const std::array<std::uint32_t, 3> vertices{
            sample.first_vertex,
            sample.second_vertex,
            sample.third_vertex,
        };
        const std::array<double, 3> weights{
            std::max(0.0, sample.barycentric_a),
            std::max(0.0, sample.barycentric_b),
            std::max(0.0, sample.barycentric_c),
        };
        for (auto corner = std::size_t{}; corner < vertices.size(); ++corner)
        {
            if (weights[corner] > 0.00000001)
            {
                field_input.anchors.push_back({
                    static_cast<int>(vertices[corner]),
                    log_gain,
                    weights[corner],
                    boundary,
                });
            }
        }
        ++output.corrected_samples;
    }
    if (field_input.anchors.empty())
    {
        return std::unexpected(PaintProjectiveError::NoSupportedSamples);
    }
    const auto field = appearance_solve_correction_field(field_input);
    if (!field.ok)
    {
        return std::unexpected(
            field.failure == AppearanceCorrectionFieldFailure::SideUnanchored
                ? PaintProjectiveError::SideUnanchored
                : PaintProjectiveError::InvalidModel);
    }
    output.front_anchor_vertices = field.front_anchor_vertices;
    output.back_anchor_vertices = field.back_anchor_vertices;
    output.side_components = field.side_components;
    output.one_boundary_side_components =
        field.one_boundary_side_components;
    output.correction_field_hash = field.hash;
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        const auto& sample = model.samples[index];
        if (!sample.safe)
        {
            continue;
        }
        const auto gain = interpolate_field(sample, field);
        if (!gain)
        {
            return std::unexpected(PaintProjectiveError::SideUnanchored);
        }
        const auto albedo = corrected_albedo(sample.base_albedo, *gain);
        output.corrected_albedo_by_sample[index] = albedo;
        output.corrected_albedo_available[index] = true;
        const auto emissive = output.physical_emission_candidate[index]
                                  ? 1.0
                                  : settings.paint_material.emissive;
        output.endpoint.appearances[sample.raster_pixel] =
            manual_appearance(albedo, settings, emissive);
        output.endpoint.available[sample.raster_pixel] = true;
    }
    return output;
}

auto finalize_paint_projective_raster(
    const PaintProjectiveModel& model,
    const PaintProjectiveCalibration& calibration,
    const PaintProjectiveFeedback& baseline,
    const PaintProjectiveFeedback& endpoint,
    const AppearanceEmissionNoiseModel& target_e0_noise,
    const PaintSettings& settings,
    bool packed_b_verified,
    std::stop_token cancellation)
    -> std::expected<PaintProjectiveResolution, PaintProjectiveError>
{
    const auto pixels = pixel_count(model);
    if (!pixels || !validate(settings).empty() ||
        calibration.corrected_albedo_by_sample.size() !=
            model.samples.size() ||
        calibration.corrected_albedo_available.size() !=
            model.samples.size() ||
        calibration.physical_emission_candidate.size() !=
            model.samples.size() ||
        baseline.target_hdr_by_sample.size() != model.samples.size() ||
        endpoint.target_hdr_by_sample.size() != model.samples.size() ||
        !baseline.camera_stable || !baseline.readback_calibrated ||
        !endpoint.camera_stable || !endpoint.readback_calibrated ||
        !target_e0_noise.ok || !packed_b_verified)
    {
        return std::unexpected(PaintProjectiveError::InvalidEvidence);
    }

    auto output = PaintProjectiveResolution{};
    output.raster = empty_raster(*pixels, settings);
    auto physical = std::vector<AppearancePhysicalEmissionEvidence>(
        model.samples.size());
    auto provisional = std::vector<bool>(model.samples.size(), false);
    auto local_corrected = std::vector<bool>(model.samples.size(), false);
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if ((index % 256U) == 0U && cancellation.stop_requested())
        {
            return std::unexpected(PaintProjectiveError::Cancelled);
        }
        const auto& sample = model.samples[index];
        const auto& base_rendered = baseline.target_hdr_by_sample[index];
        const auto& endpoint_rendered = endpoint.target_hdr_by_sample[index];
        if (!sample.safe || !base_rendered.finite || base_rendered.clipped ||
            !endpoint_rendered.finite || endpoint_rendered.clipped ||
            !calibration.corrected_albedo_available[index])
        {
            continue;
        }
        const auto baseline_loss =
            log_loss(sample.source_final_hdr, base_rendered.value);
        const auto corrected_loss =
            log_loss(sample.source_final_hdr, endpoint_rendered.value);
        if (!calibration.physical_emission_candidate[index] &&
            corrected_loss + 0.000001 < baseline_loss)
        {
            local_corrected[index] = true;
            ++output.local_albedo_acceptances;
        }
        if (!calibration.physical_emission_candidate[index])
        {
            continue;
        }
        physical[index] = appearance_physical_emission_evidence(
            AppearancePhysicalEmissionEvidenceInput{
                sample.source_residual_first,
                sample.source_residual_second,
                model.source_noise_first.ok
                    ? model.source_noise_first.threshold
                    : std::numeric_limits<double>::infinity(),
                model.source_noise_second.ok
                    ? model.source_noise_second.threshold
                    : std::numeric_limits<double>::infinity(),
                sample.source_final_hdr,
                base_rendered.value,
                endpoint_rendered.value,
                settings.paint_material.emissive,
                model.source_noise_first.separated_signal &&
                    model.source_noise_second.separated_signal,
                true,
                true,
                packed_b_verified,
            });
        provisional[index] = physical[index].accepted;
    }

    auto vertex_count = std::size_t{};
    for (const auto& sample : model.samples)
    {
        vertex_count = std::max(
            vertex_count,
            static_cast<std::size_t>(std::max(
                {sample.first_vertex,
                 sample.second_vertex,
                 sample.third_vertex})) +
                1U);
    }
    auto sets = DisjointSet(vertex_count);
    auto active_vertex = std::vector<bool>(vertex_count, false);
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if (!provisional[index])
        {
            continue;
        }
        const auto& sample = model.samples[index];
        sets.unite(sample.first_vertex, sample.second_vertex);
        sets.unite(sample.second_vertex, sample.third_vertex);
        active_vertex[sample.first_vertex] = true;
        active_vertex[sample.second_vertex] = true;
        active_vertex[sample.third_vertex] = true;
    }
    struct ComponentEvidence
    {
        int paired{};
        double baseline{};
        double candidate{};
        int non_emission_paired{};
        double non_emission_baseline{};
        double non_emission_candidate{};
        int painted{};
    };
    auto component = std::unordered_map<std::size_t, ComponentEvidence>{};
    auto component_by_sample = std::vector<std::size_t>(
        model.samples.size(), std::numeric_limits<std::size_t>::max());
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if (!provisional[index])
        {
            continue;
        }
        const auto root = sets.find(model.samples[index].first_vertex);
        component_by_sample[index] = root;
        auto& evidence = component[root];
        ++evidence.paired;
        evidence.baseline += physical[index].baseline_loss;
        evidence.candidate += physical[index].candidate_loss;
        evidence.painted +=
            appearance_quantize_unit(physical[index].composed_emissive) > 0U
                ? 1
                : 0;
    }
    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        if (provisional[index] || !model.samples[index].safe)
        {
            continue;
        }
        const auto& sample = model.samples[index];
        const std::array<std::uint32_t, 3> vertices{
            sample.first_vertex,
            sample.second_vertex,
            sample.third_vertex,
        };
        auto root = std::numeric_limits<std::size_t>::max();
        for (const auto vertex : vertices)
        {
            if (active_vertex[vertex])
            {
                root = sets.find(vertex);
                break;
            }
        }
        if (root == std::numeric_limits<std::size_t>::max() ||
            !component.contains(root))
        {
            continue;
        }
        const auto& first = baseline.target_hdr_by_sample[index];
        const auto& second = endpoint.target_hdr_by_sample[index];
        if (!first.finite || first.clipped || !second.finite ||
            second.clipped)
        {
            continue;
        }
        auto& evidence = component[root];
        ++evidence.non_emission_paired;
        evidence.non_emission_baseline +=
            log_loss(sample.source_final_hdr, first.value);
        evidence.non_emission_candidate +=
            log_loss(sample.source_final_hdr, second.value);
    }
    auto accepted_component =
        std::unordered_map<std::size_t, bool>{};
    for (auto& [root, evidence] : component)
    {
        const auto divide = [](double value, int count) {
            return count > 0 ? value / static_cast<double>(count) : 0.0;
        };
        const auto validated =
            appearance_validate_physical_emission_component(
                AppearancePhysicalEmissionComponentValidationInput{
                    evidence.paired,
                    divide(evidence.baseline, evidence.paired),
                    divide(evidence.candidate, evidence.paired),
                    evidence.non_emission_paired,
                    divide(evidence.non_emission_baseline,
                           evidence.non_emission_paired),
                    divide(evidence.non_emission_candidate,
                           evidence.non_emission_paired),
                    true,
                    true,
                    true,
                    packed_b_verified,
                    evidence.painted,
                });
        accepted_component[root] = validated.accepted;
        output.physical_emission_components += validated.accepted ? 1 : 0;
    }

    for (auto index = std::size_t{}; index < model.samples.size(); ++index)
    {
        const auto& sample = model.samples[index];
        if (!sample.safe || !sample.replay_relevant)
        {
            continue;
        }
        auto albedo = local_corrected[index]
                          ? calibration.corrected_albedo_by_sample[index]
                          : sample.base_albedo;
        auto emissive = settings.paint_material.emissive;
        const auto root = component_by_sample[index];
        const auto physical_accepted =
            provisional[index] &&
            root != std::numeric_limits<std::size_t>::max() &&
            accepted_component.contains(root) &&
            accepted_component[root];
        if (physical_accepted)
        {
            const auto material =
                appearance_compose_physical_emission_material(
                    AppearancePhysicalEmissionMaterialInput{
                        calibration.corrected_albedo_by_sample[index],
                        sample.source_residual_first,
                        sample.source_residual_second,
                        settings.paint_material.emissive,
                        physical[index].inferred_emissive,
                        true,
                    });
            albedo = material.albedo;
            emissive = material.emissive;
            ++output.physical_emission_samples;
        }
        output.raster.appearances[sample.raster_pixel] =
            manual_appearance(albedo, settings, emissive);
        output.raster.available[sample.raster_pixel] = true;
    }
    if (std::ranges::none_of(output.raster.available, [](bool value) {
            return value;
        }))
    {
        return std::unexpected(PaintProjectiveError::NoSupportedSamples);
    }
    return output;
}
} // namespace meccha::core
