#include <meccha/core/paint_appearance_fit.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <stop_token>
#include <utility>
#include <vector>

namespace meccha::core
{
namespace
{
struct GridPoint
{
    int x{};
    int y{};
};

struct Center
{
    double base_r{};
    double base_g{};
    double base_b{};
    double target_r{};
    double target_g{};
    double target_b{};
    double hdr_luminance{};
    double intrinsic_luminance{};
    double emission_group{};
    double normal_x{};
    double normal_y{};
    double normal_z{};
    double depth_confidence{};
    double facing{};
};

auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto pixel_count(
    std::uint32_t width,
    std::uint32_t height) -> std::optional<std::size_t>
{
    if (width == 0U || height == 0U ||
        width >
            std::numeric_limits<std::size_t>::max() /
                height)
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(width) *
           static_cast<std::size_t>(height);
}

auto rgb8_linear(Rgb8 value) -> AppearanceRgb
{
    return AppearanceRgb{
        appearance_srgb_to_linear(
            static_cast<double>(value.red) / 255.0),
        appearance_srgb_to_linear(
            static_cast<double>(value.green) / 255.0),
        appearance_srgb_to_linear(
            static_cast<double>(value.blue) / 255.0),
    };
}

auto linear_rgb8(const AppearanceRgb& value) -> Rgb8
{
    const auto encode = [](double channel)
    {
        return static_cast<std::uint8_t>(std::lround(
            appearance_linear_to_srgb(channel) * 255.0));
    };
    return Rgb8{
        encode(value.r),
        encode(value.g),
        encode(value.b),
    };
}

auto filter_emission_regions(
    const std::vector<GridPoint>& points,
    int minimum_region_samples)
    -> std::vector<bool>
{
    auto keep = std::vector<bool>(points.size(), false);
    const auto minimum =
        std::max(1, minimum_region_samples);
    auto visited = std::vector<bool>(points.size(), false);
    auto stack = std::vector<std::size_t>{};
    auto component = std::vector<std::size_t>{};
    for (auto first = std::size_t{};
         first < points.size();
         ++first)
    {
        if (visited[first])
        {
            continue;
        }
        visited[first] = true;
        stack.clear();
        component.clear();
        stack.push_back(first);
        while (!stack.empty())
        {
            const auto current = stack.back();
            stack.pop_back();
            component.push_back(current);
            for (auto candidate = std::size_t{};
                 candidate < points.size();
                 ++candidate)
            {
                if (visited[candidate])
                {
                    continue;
                }
                const auto dx = std::abs(
                    static_cast<std::int64_t>(
                        points[current].x) -
                    static_cast<std::int64_t>(
                        points[candidate].x));
                const auto dy = std::abs(
                    static_cast<std::int64_t>(
                        points[current].y) -
                    static_cast<std::int64_t>(
                        points[candidate].y));
                if (dx <= 1 && dy <= 1)
                {
                    visited[candidate] = true;
                    stack.push_back(candidate);
                }
            }
        }
        if (static_cast<int>(component.size()) >= minimum)
        {
            for (const auto index : component)
            {
                keep[index] = true;
            }
        }
    }
    return keep;
}

auto feature(const PaintAppearanceModelSample& sample)
    -> Center
{
    const auto target =
        sample.emission_roi
            ? sample.emission_albedo
            : sample.display_linear;
    return Center{
        sample.base_linear.r,
        sample.base_linear.g,
        sample.base_linear.b,
        target.r,
        target.g,
        target.b,
        std::log1p(
            appearance_luminance(sample.source_final_hdr)),
        std::log1p(
            appearance_luminance(
                sample.intrinsic_emission)),
        sample.emission_roi ? 1.0 : 0.0,
        sample.normal_available ? sample.normal.r : 0.0,
        sample.normal_available ? sample.normal.g : 0.0,
        sample.normal_available ? sample.normal.b : 0.0,
        sample.depth_available ? 1.0 : 0.0,
        sample.facing,
    };
}

auto distance_squared(
    const Center& left,
    const Center& right) -> double
{
    const auto dbr = left.base_r - right.base_r;
    const auto dbg = left.base_g - right.base_g;
    const auto dbb = left.base_b - right.base_b;
    const auto dr = left.target_r - right.target_r;
    const auto dg = left.target_g - right.target_g;
    const auto db = left.target_b - right.target_b;
    const auto dh =
        left.hdr_luminance - right.hdr_luminance;
    const auto di =
        left.intrinsic_luminance -
        right.intrinsic_luminance;
    const auto de =
        left.emission_group - right.emission_group;
    const auto dnx = left.normal_x - right.normal_x;
    const auto dny = left.normal_y - right.normal_y;
    const auto dnz = left.normal_z - right.normal_z;
    const auto dd =
        left.depth_confidence - right.depth_confidence;
    const auto df = left.facing - right.facing;
    return 0.50 * (dbr * dbr + dbg * dbg + dbb * dbb) +
           dr * dr + dg * dg + db * db +
           0.35 * dh * dh +
           0.50 * di * di +
           16.0 * de * de +
           0.15 * (dnx * dnx + dny * dny + dnz * dnz) +
           0.05 * dd * dd +
           0.10 * df * df;
}

auto center_sum(Center& destination, const Center& value)
    -> void
{
    destination.base_r += value.base_r;
    destination.base_g += value.base_g;
    destination.base_b += value.base_b;
    destination.target_r += value.target_r;
    destination.target_g += value.target_g;
    destination.target_b += value.target_b;
    destination.hdr_luminance += value.hdr_luminance;
    destination.intrinsic_luminance +=
        value.intrinsic_luminance;
    destination.emission_group += value.emission_group;
    destination.normal_x += value.normal_x;
    destination.normal_y += value.normal_y;
    destination.normal_z += value.normal_z;
    destination.depth_confidence += value.depth_confidence;
    destination.facing += value.facing;
}

auto center_average(const Center& value, int count) -> Center
{
    const auto divisor = static_cast<double>(count);
    return Center{
        value.base_r / divisor,
        value.base_g / divisor,
        value.base_b / divisor,
        value.target_r / divisor,
        value.target_g / divisor,
        value.target_b / divisor,
        value.hdr_luminance / divisor,
        value.intrinsic_luminance / divisor,
        value.emission_group / divisor,
        value.normal_x / divisor,
        value.normal_y / divisor,
        value.normal_z / divisor,
        value.depth_confidence / divisor,
        value.facing / divisor,
    };
}

auto median(std::vector<double>& values) -> double
{
    const auto middle =
        values.begin() +
        static_cast<std::ptrdiff_t>(values.size() / 2U);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}
} // namespace

auto prepare_paint_appearance_model(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const PaintAppearanceObservation> observations,
    bool include_scene_lighting,
    std::optional<AppearanceEmissionNoiseModel>
        target_e0_noise,
    std::stop_token cancellation)
    -> std::expected<
        PaintAppearanceModel,
        PaintAppearanceFitError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintAppearanceFitError::Cancelled);
    }
    const auto pixels = pixel_count(width, height);
    if (!pixels || observations.empty())
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidInput);
    }
    if (observations.size() >
        MaximumPaintAppearanceSamples)
    {
        return std::unexpected(
            PaintAppearanceFitError::ResourceLimit);
    }

    auto residual_luminance = std::vector<double>{};
    residual_luminance.reserve(observations.size());
    for (auto observation_index = std::size_t{};
         observation_index < observations.size();
         ++observation_index)
    {
        if ((observation_index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceFitError::Cancelled);
        }
        const auto& observation =
            observations[observation_index];
        if (!observation.safe ||
            observation.raster_pixel >= *pixels ||
            !unit(observation.u) || !unit(observation.v) ||
            !observation.intrinsic_emission_available)
        {
            continue;
        }
        const auto isolated =
            appearance_sanitize_hdr(
                observation.intrinsic_emission_hdr);
        if (!isolated.finite || isolated.clipped)
        {
            continue;
        }
        residual_luminance.push_back(
            appearance_luminance(
                appearance_intrinsic_emission_residual(
                    isolated.value,
                    AppearanceRgb{
                        static_cast<double>(
                            observation.base_color.red) /
                            255.0,
                        static_cast<double>(
                            observation.base_color.green) /
                            255.0,
                        static_cast<double>(
                            observation.base_color.blue) /
                            255.0,
                    })));
    }

    auto output = PaintAppearanceModel{};
    output.width = width;
    output.height = height;
    output.include_scene_lighting =
        include_scene_lighting;
    output.source_emission_noise =
        appearance_source_emission_noise_model(
            residual_luminance);
    output.effective_emission_noise =
        target_e0_noise
            ? appearance_combine_emission_noise_models(
                  output.source_emission_noise,
                  *target_e0_noise)
            : output.source_emission_noise;
    output.samples.reserve(observations.size());
    for (auto index = std::size_t{};
         index < observations.size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceFitError::Cancelled);
        }
        const auto& observation = observations[index];
        const auto source =
            appearance_sanitize_hdr(observation.final_hdr);
        if (!observation.safe ||
            observation.raster_pixel >= *pixels ||
            !unit(observation.u) || !unit(observation.v) ||
            !source.finite || source.clipped)
        {
            continue;
        }
        const auto base = rgb8_linear(observation.base_color);
        auto display =
            appearance_reinhard_display(source.value);
        if (observation.tone_curve_available &&
            appearance_rgb_finite(
                observation.tone_curve_hdr))
        {
            display = appearance_clamp_albedo(
                observation.tone_curve_hdr);
        }
        auto intrinsic = AppearanceRgb{};
        auto emission_roi = false;
        if (observation.intrinsic_emission_available)
        {
            const auto isolated =
                appearance_sanitize_hdr(
                    observation.intrinsic_emission_hdr);
            if (isolated.finite && !isolated.clipped)
            {
                intrinsic =
                    appearance_intrinsic_emission_residual(
                        isolated.value,
                        AppearanceRgb{
                            static_cast<double>(
                                observation.base_color.red) /
                                255.0,
                            static_cast<double>(
                                observation.base_color.green) /
                                255.0,
                            static_cast<double>(
                                observation.base_color.blue) /
                                255.0,
                        });
                emission_roi =
                    appearance_emission_sample_detected(
                        appearance_luminance(intrinsic),
                        output.effective_emission_noise);
            }
        }
        output.samples.push_back(
            PaintAppearanceModelSample{
                index,
                observation.raster_pixel,
                observation.u,
                observation.v,
                base,
                display,
                source.value,
                intrinsic,
                appearance_emission_chromaticity_albedo(
                    intrinsic,
                    base),
                observation.normal,
                observation.scene_depth,
                observation.facing,
                observation.normal_available &&
                    appearance_rgb_finite(
                        observation.normal),
                observation.depth_available &&
                    std::isfinite(
                        observation.scene_depth) &&
                    observation.scene_depth > 0.0,
                emission_roi,
                observation.source_surface_key,
                0U,
            });
    }
    if (output.samples.empty())
    {
        return std::unexpected(
            PaintAppearanceFitError::NoSupportedSamples);
    }

    auto emission_points = std::vector<GridPoint>{};
    auto emission_samples = std::vector<std::size_t>{};
    for (auto index = std::size_t{};
         index < output.samples.size();
         ++index)
    {
        const auto& sample = output.samples[index];
        if (!sample.emission_roi)
        {
            continue;
        }
        const auto x = sample.raster_pixel % width;
        const auto y = sample.raster_pixel / width;
        emission_points.push_back(
            GridPoint{
                static_cast<int>(x / 4U),
                static_cast<int>(y / 4U),
            });
        emission_samples.push_back(index);
    }
    const auto kept = filter_emission_regions(
        emission_points,
        3);
    for (auto index = std::size_t{};
         index < emission_samples.size();
         ++index)
    {
        if (!kept[index])
        {
            output.samples[emission_samples[index]]
                .emission_roi = false;
        }
    }
    auto surface_points =
        std::vector<AppearanceEmissionSurfacePoint>{};
    auto surface_samples = std::vector<std::size_t>{};
    surface_points.reserve(emission_samples.size());
    surface_samples.reserve(emission_samples.size());
    for (auto index = std::size_t{};
         index < emission_samples.size();
         ++index)
    {
        if (!kept[index])
        {
            continue;
        }
        const auto sample_index = emission_samples[index];
        const auto& sample = output.samples[sample_index];
        surface_points.push_back(
            AppearanceEmissionSurfacePoint{
                AppearanceEmissionGridPoint{
                    emission_points[index].x,
                    emission_points[index].y,
                },
                appearance_luminance(
                    sample.intrinsic_emission),
                sample.source_surface_key,
            });
        surface_samples.push_back(sample_index);
    }
    const auto surface_filter =
        appearance_filter_emission_surface_halo(
            surface_points);
    for (auto index = std::size_t{};
         index < surface_samples.size();
         ++index)
    {
        if (!surface_filter.keep[index])
        {
            output.samples[surface_samples[index]]
                .emission_roi = false;
        }
    }
    output.emission_roi_samples =
        static_cast<std::size_t>(std::ranges::count_if(
            output.samples,
            [](const PaintAppearanceModelSample& sample)
            {
                return sample.emission_roi;
            }));
    output.supported_samples = output.samples.size();

    const auto split_emission =
        output.emission_roi_samples > 0U &&
        output.emission_roi_samples < output.samples.size();
    const auto requested_clusters = std::min(
        AppearanceMaximumClusters,
        std::max(
            split_emission ? 2 : 1,
            static_cast<int>(output.samples.size() / 64U)));
    const auto cluster_count = std::min(
        requested_clusters,
        static_cast<int>(output.samples.size()));
    auto centers = std::vector<Center>{};
    centers.reserve(
        static_cast<std::size_t>(cluster_count));
    auto seed = std::uint64_t{0x2180a55e5ULL};
    const auto next_unit = [&seed]()
    {
        seed = appearance_spsa_hash(seed);
        return static_cast<double>(seed >> 11U) /
               static_cast<double>(
                   std::numeric_limits<std::uint64_t>::max()
                   >> 11U);
    };
    if (split_emission && cluster_count >= 2)
    {
        const auto emission = std::ranges::find_if(
            output.samples,
            [](const PaintAppearanceModelSample& sample)
            {
                return sample.emission_roi;
            });
        const auto non_emission = std::ranges::find_if(
            output.samples,
            [](const PaintAppearanceModelSample& sample)
            {
                return !sample.emission_roi;
            });
        centers.push_back(feature(*emission));
        centers.push_back(feature(*non_emission));
    }
    else
    {
        const auto selected =
            static_cast<std::size_t>(
                next_unit() *
                static_cast<double>(output.samples.size())) %
            output.samples.size();
        centers.push_back(feature(output.samples[selected]));
    }
    while (static_cast<int>(centers.size()) < cluster_count)
    {
        auto weights = std::vector<double>{};
        weights.reserve(output.samples.size());
        auto total = 0.0;
        for (const auto& sample : output.samples)
        {
            const auto candidate = feature(sample);
            auto nearest =
                std::numeric_limits<double>::infinity();
            for (const auto& center : centers)
            {
                nearest = std::min(
                    nearest,
                    distance_squared(candidate, center));
            }
            const auto weight = std::max(0.0, nearest);
            weights.push_back(weight);
            total += weight;
        }
        auto selected = std::size_t{};
        if (total > 0.0 && std::isfinite(total))
        {
            const auto threshold = next_unit() * total;
            auto cumulative = 0.0;
            for (auto index = std::size_t{};
                 index < weights.size();
                 ++index)
            {
                cumulative += weights[index];
                if (cumulative >= threshold)
                {
                    selected = index;
                    break;
                }
            }
        }
        centers.push_back(feature(output.samples[selected]));
    }

    auto assignments =
        std::vector<int>(output.samples.size(), 0);
    for (auto iteration = 0; iteration < 8; ++iteration)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceFitError::Cancelled);
        }
        auto sums = std::vector<Center>(
            static_cast<std::size_t>(cluster_count));
        auto counts = std::vector<int>(
            static_cast<std::size_t>(cluster_count),
            0);
        for (auto index = std::size_t{};
             index < output.samples.size();
             ++index)
        {
            const auto candidate = feature(
                output.samples[index]);
            auto nearest_cluster = 0;
            auto nearest_distance = distance_squared(
                candidate,
                centers.front());
            for (auto cluster = 1;
                 cluster < cluster_count;
                 ++cluster)
            {
                const auto distance = distance_squared(
                    candidate,
                    centers[static_cast<std::size_t>(
                        cluster)]);
                if (distance < nearest_distance)
                {
                    nearest_distance = distance;
                    nearest_cluster = cluster;
                }
            }
            assignments[index] = nearest_cluster;
            center_sum(
                sums[static_cast<std::size_t>(
                    nearest_cluster)],
                candidate);
            ++counts[static_cast<std::size_t>(
                nearest_cluster)];
        }
        for (auto cluster = 0;
             cluster < cluster_count;
             ++cluster)
        {
            const auto cluster_index =
                static_cast<std::size_t>(cluster);
            if (counts[cluster_index] > 0)
            {
                centers[cluster_index] = center_average(
                    sums[cluster_index],
                    counts[cluster_index]);
            }
        }
    }

    output.clusters.resize(
        static_cast<std::size_t>(cluster_count));
    for (auto index = std::size_t{};
         index < output.samples.size();
         ++index)
    {
        const auto cluster =
            static_cast<std::size_t>(assignments[index]);
        output.samples[index].cluster = cluster;
        output.clusters[cluster].sample_indices.push_back(
            index);
    }
    for (auto& cluster : output.clusters)
    {
        if (cluster.sample_indices.empty())
        {
            continue;
        }
        auto blend = 0.0;
        for (const auto index : cluster.sample_indices)
        {
            const auto& sample = output.samples[index];
            const auto target =
                sample.emission_roi
                    ? sample.emission_albedo
                    : sample.display_linear;
            blend += sample.emission_roi
                         ? 1.0
                         : appearance_initial_albedo_blend(
                               sample.base_linear,
                               target);
        }
        const auto count = static_cast<double>(
            cluster.sample_indices.size());
        cluster.material = AppearanceMaterial{
            blend / count,
            0.0,
            AppearanceFallbackRoughness,
            0.0,
        };
    }
    return output;
}

auto paint_appearance_parameters(
    const PaintAppearanceModel& model)
    -> std::vector<double>
{
    auto output = std::vector<double>{};
    output.reserve(model.clusters.size() * 4U);
    for (const auto& cluster : model.clusters)
    {
        output.push_back(cluster.material.albedo_blend);
        output.push_back(cluster.material.metallic);
        output.push_back(cluster.material.roughness);
        output.push_back(cluster.material.emissive);
    }
    return output;
}

auto paint_appearance_fallback_parameters(
    const PaintAppearanceModel& model)
    -> std::vector<double>
{
    auto output = std::vector<double>{};
    output.reserve(model.clusters.size() * 4U);
    for (auto cluster = std::size_t{};
         cluster < model.clusters.size();
         ++cluster)
    {
        output.push_back(1.0);
        output.push_back(0.0);
        output.push_back(AppearanceFallbackRoughness);
        output.push_back(0.0);
    }
    return output;
}

namespace
{
auto fit_evaluation_valid(
    const PaintAppearanceEvaluation& evaluation,
    std::size_t cluster_count) -> bool
{
    return evaluation.paired_samples >=
               AppearanceFitMinimumSamples &&
           evaluation.camera_stable &&
           evaluation.readback_calibrated &&
           std::isfinite(evaluation.loss) &&
           evaluation.loss >= 0.0 &&
           std::isfinite(evaluation.median_delta_e) &&
           evaluation.median_delta_e >= 0.0 &&
           evaluation.clusters.size() == cluster_count;
}

auto fit_parameters_valid(
    std::span<const double> parameters,
    std::size_t cluster_count) -> bool
{
    return !parameters.empty() &&
           parameters.size() == cluster_count * 4U &&
           std::ranges::all_of(
               parameters,
               [](double value)
               {
                   return unit(value);
               });
}

auto consider_fit_candidate(
    PaintAppearanceFitSession& session,
    std::span<const double> parameters,
    const PaintAppearanceEvaluation& evaluation) -> void
{
    if (evaluation.loss < session.best_evaluation.loss)
    {
        session.best_parameters.assign(
            parameters.begin(),
            parameters.end());
        session.best_evaluation = evaluation;
    }
}
} // namespace

auto begin_paint_appearance_fit(
    const PaintAppearanceModel& model,
    std::vector<double> fallback_parameters,
    PaintAppearanceEvaluation fallback_evaluation,
    std::uint64_t seed)
    -> std::expected<
        PaintAppearanceFitSession,
        PaintAppearanceFitError>
{
    if (model.samples.empty() || model.clusters.empty() ||
        model.supported_samples != model.samples.size())
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidModel);
    }
    if (!fit_parameters_valid(
            fallback_parameters,
            model.clusters.size()))
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidParameters);
    }
    if (!fit_evaluation_valid(
            fallback_evaluation,
            model.clusters.size()))
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidResponse);
    }
    auto parameters = fallback_parameters;
    auto best_parameters = fallback_parameters;
    auto best_evaluation = fallback_evaluation;
    return PaintAppearanceFitSession{
        model.clusters.size(),
        seed,
        0,
        PaintAppearanceFitSessionStage::NeedPlus,
        std::move(fallback_parameters),
        std::move(fallback_evaluation),
        std::move(parameters),
        {},
        std::nullopt,
        std::move(best_parameters),
        std::move(best_evaluation),
    };
}

auto next_paint_appearance_trial(
    PaintAppearanceFitSession& session)
    -> std::expected<
        std::optional<PaintAppearanceTrial>,
        PaintAppearanceFitError>
{
    if (session.stage ==
        PaintAppearanceFitSessionStage::Complete)
    {
        return std::optional<PaintAppearanceTrial>{};
    }
    if (!fit_parameters_valid(
            session.parameters,
            session.cluster_count) ||
        session.iteration < 0 ||
        session.iteration >= AppearanceSpsaIterations)
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidParameters);
    }
    if (session.stage ==
        PaintAppearanceFitSessionStage::NeedPlus)
    {
        session.pair = appearance_spsa_pair(
            session.parameters,
            session.iteration,
            session.seed);
        if (!fit_parameters_valid(
                session.pair.plus,
                session.cluster_count) ||
            !fit_parameters_valid(
                session.pair.minus,
                session.cluster_count) ||
            session.pair.direction.size() !=
                session.parameters.size())
        {
            return std::unexpected(
                PaintAppearanceFitError::InvalidParameters);
        }
        session.plus_evaluation.reset();
        session.stage =
            PaintAppearanceFitSessionStage::AwaitPlus;
        return std::optional<PaintAppearanceTrial>{
            PaintAppearanceTrial{
                session.iteration,
                PaintAppearanceTrialPhase::Plus,
                session.pair.plus,
            }};
    }
    if (session.stage ==
        PaintAppearanceFitSessionStage::NeedMinus)
    {
        session.stage =
            PaintAppearanceFitSessionStage::AwaitMinus;
        return std::optional<PaintAppearanceTrial>{
            PaintAppearanceTrial{
                session.iteration,
                PaintAppearanceTrialPhase::Minus,
                session.pair.minus,
            }};
    }
    return std::unexpected(
        PaintAppearanceFitError::InvalidInput);
}

auto observe_paint_appearance_trial(
    PaintAppearanceFitSession& session,
    PaintAppearanceEvaluation evaluation)
    -> std::expected<void, PaintAppearanceFitError>
{
    if (!fit_evaluation_valid(
            evaluation,
            session.cluster_count))
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidResponse);
    }
    if (session.stage ==
        PaintAppearanceFitSessionStage::AwaitPlus)
    {
        consider_fit_candidate(
            session,
            session.pair.plus,
            evaluation);
        session.plus_evaluation = std::move(evaluation);
        session.stage =
            PaintAppearanceFitSessionStage::NeedMinus;
        return {};
    }
    if (session.stage !=
            PaintAppearanceFitSessionStage::AwaitMinus ||
        !session.plus_evaluation)
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidInput);
    }
    consider_fit_candidate(
        session,
        session.pair.minus,
        evaluation);
    auto plus_losses = std::vector<double>{};
    auto minus_losses = std::vector<double>{};
    plus_losses.reserve(session.cluster_count);
    minus_losses.reserve(session.cluster_count);
    for (auto cluster = std::size_t{};
         cluster < session.cluster_count;
         ++cluster)
    {
        plus_losses.push_back(
            session.plus_evaluation->clusters[cluster].loss);
        minus_losses.push_back(
            evaluation.clusters[cluster].loss);
    }
    session.parameters = appearance_spsa_update_by_cluster(
        session.parameters,
        session.pair,
        plus_losses,
        minus_losses,
        session.iteration);
    if (!fit_parameters_valid(
            session.parameters,
            session.cluster_count))
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidParameters);
    }
    ++session.iteration;
    session.plus_evaluation.reset();
    session.stage =
        session.iteration == AppearanceSpsaIterations
            ? PaintAppearanceFitSessionStage::Complete
            : PaintAppearanceFitSessionStage::NeedPlus;
    return {};
}

auto finish_paint_appearance_fit(
    const PaintAppearanceFitSession& session)
    -> std::expected<
        PaintAppearanceFitResult,
        PaintAppearanceFitError>
{
    if (session.stage !=
            PaintAppearanceFitSessionStage::Complete ||
        session.iteration != AppearanceSpsaIterations ||
        !fit_parameters_valid(
            session.fallback_parameters,
            session.cluster_count) ||
        !fit_parameters_valid(
            session.best_parameters,
            session.cluster_count) ||
        !fit_evaluation_valid(
            session.fallback_evaluation,
            session.cluster_count) ||
        !fit_evaluation_valid(
            session.best_evaluation,
            session.cluster_count))
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidInput);
    }
    const auto accepted = appearance_fit_accepted(
        AppearanceFitAcceptance{
            session.best_evaluation.paired_samples,
            session.best_evaluation.camera_stable,
            session.best_evaluation.readback_calibrated,
            session.fallback_evaluation.loss,
            session.best_evaluation.loss,
            session.best_evaluation.median_delta_e,
        });
    return PaintAppearanceFitResult{
        accepted
            ? session.best_parameters
            : session.fallback_parameters,
        accepted
            ? session.best_evaluation
            : session.fallback_evaluation,
        accepted,
        session.iteration,
    };
}

auto resolve_paint_appearance_raster(
    const PaintAppearanceModel& model,
    std::span<const Rgb8> base_colors,
    std::span<const Rgb8> scene_colors,
    std::span<const double> parameters,
    std::stop_token cancellation)
    -> std::expected<
        std::vector<ResolvedPaintAppearance>,
        PaintAppearanceFitError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintAppearanceFitError::Cancelled);
    }
    const auto pixels = pixel_count(model.width, model.height);
    if (!pixels || model.samples.empty() ||
        model.clusters.empty() ||
        model.supported_samples != model.samples.size())
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidModel);
    }
    if (base_colors.size() != *pixels ||
        scene_colors.size() != *pixels)
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidInput);
    }
    if (parameters.size() != model.clusters.size() * 4U ||
        !std::ranges::all_of(
            parameters,
            [](double value)
            {
                return unit(value);
            }))
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidParameters);
    }

    auto output =
        std::vector<ResolvedPaintAppearance>(*pixels);
    for (auto pixel = std::size_t{};
         pixel < *pixels;
         ++pixel)
    {
        if ((pixel % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceFitError::Cancelled);
        }
        const auto base = rgb8_linear(base_colors[pixel]);
        const auto display =
            rgb8_linear(scene_colors[pixel]);
        const auto fallback =
            model.include_scene_lighting
                ? appearance_make_fallback(display)
                : appearance_make_safe_fallback(
                      base,
                      display,
                      true);
        output[pixel] = ResolvedPaintAppearance{
            linear_rgb8(fallback.albedo),
            Material{
                fallback.material.metallic,
                fallback.material.roughness,
                fallback.material.emissive,
            },
        };
    }
    for (auto index = std::size_t{};
         index < model.samples.size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceFitError::Cancelled);
        }
        const auto& sample = model.samples[index];
        if (sample.cluster >= model.clusters.size() ||
            sample.raster_pixel >= output.size())
        {
            return std::unexpected(
                PaintAppearanceFitError::InvalidModel);
        }
        const auto offset = sample.cluster * 4U;
        const auto target =
            appearance_source_albedo_target(
                sample.base_linear,
                sample.display_linear,
                sample.emission_albedo,
                sample.emission_roi,
                model.include_scene_lighting);
        const auto albedo =
            appearance_parameterized_albedo(
                sample.base_linear,
                target,
                parameters[offset],
                sample.emission_roi);
        const auto emissive =
            sample.emission_roi
                ? parameters[offset + 3U]
                : 0.0;
        output[sample.raster_pixel] =
            ResolvedPaintAppearance{
                linear_rgb8(albedo),
                Material{
                    parameters[offset + 1U],
                    parameters[offset + 2U],
                    emissive,
                },
            };
    }
    return output;
}

auto build_paint_appearance_trial_plan(
    const PaintAppearanceModel& model,
    std::span<const ResolvedPaintAppearance> appearances,
    double brush_size_texels,
    std::uint32_t texture_dimension,
    std::stop_token cancellation)
    -> std::expected<
        PaintPlan,
        PaintAppearanceFitError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintAppearanceFitError::Cancelled);
    }
    const auto pixels = pixel_count(model.width, model.height);
    if (!pixels || model.samples.empty() ||
        model.clusters.empty() ||
        model.supported_samples != model.samples.size())
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidModel);
    }
    if (appearances.size() != *pixels ||
        !std::isfinite(brush_size_texels) ||
        brush_size_texels < 1.0 ||
        brush_size_texels > 10.0 ||
        texture_dimension == 0U ||
        texture_dimension > 4096U)
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidInput);
    }
    auto plan = PaintPlan{};
    plan.strokes.reserve(model.samples.size());
    for (auto index = std::size_t{};
         index < model.samples.size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceFitError::Cancelled);
        }
        const auto& sample = model.samples[index];
        if (sample.raster_pixel >= appearances.size())
        {
            return std::unexpected(
                PaintAppearanceFitError::InvalidModel);
        }
        const auto& appearance =
            appearances[sample.raster_pixel];
        if (!unit(appearance.material.metallic) ||
            !unit(appearance.material.roughness) ||
            !unit(appearance.material.emissive))
        {
            return std::unexpected(
                PaintAppearanceFitError::InvalidInput);
        }
        plan.strokes.push_back(
            PaintStroke{
                sample.source_index,
                ReplayPass::Paint,
                Region::Side,
                sample.u,
                sample.v,
                brush_size_texels,
                appearance.color,
                appearance.material,
                model.include_scene_lighting,
            });
    }
    plan.fill_end = 0U;
    plan.fill_count = 0U;
    plan.paint_count = plan.strokes.size();
    plan.source_paint_count = plan.strokes.size();
    plan.compressed_paint_count = 0U;
    plan.expanded_paint_count = 0U;
    plan.texture_dimension = texture_dimension;
    return plan;
}

auto evaluate_paint_appearance_response(
    const PaintAppearanceModel& model,
    std::span<const AppearanceRgb> target_hdr,
    bool camera_stable,
    bool readback_calibrated,
    AppearanceReadbackTransform transform,
    std::stop_token cancellation)
    -> std::expected<
        PaintAppearanceEvaluation,
        PaintAppearanceFitError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintAppearanceFitError::Cancelled);
    }
    const auto pixels = pixel_count(model.width, model.height);
    if (!pixels || target_hdr.size() != *pixels)
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidResponse);
    }
    if (model.samples.empty() || model.clusters.empty())
    {
        return std::unexpected(
            PaintAppearanceFitError::InvalidModel);
    }

    auto output = PaintAppearanceEvaluation{};
    output.camera_stable = camera_stable;
    output.readback_calibrated = readback_calibrated;
    output.clusters.resize(model.clusters.size());
    auto delta_es = std::vector<double>{};
    auto chromaticity_deltas = std::vector<double>{};
    auto cluster_losses =
        std::vector<double>(model.clusters.size(), 0.0);
    auto cluster_delta_es =
        std::vector<std::vector<double>>(
            model.clusters.size());
    auto total_loss = 0.0;
    for (auto index = std::size_t{};
         index < model.samples.size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceFitError::Cancelled);
        }
        const auto& sample = model.samples[index];
        if (sample.raster_pixel >= target_hdr.size() ||
            sample.cluster >= model.clusters.size())
        {
            return std::unexpected(
                PaintAppearanceFitError::InvalidModel);
        }
        auto target = target_hdr[sample.raster_pixel];
        if (transform ==
            AppearanceReadbackTransform::SwapRedBlue)
        {
            std::swap(target.r, target.b);
        }
        const auto sanitized =
            appearance_sanitize_hdr(target);
        if (!sanitized.finite || sanitized.clipped)
        {
            continue;
        }
        const auto source_display =
            appearance_reinhard_display(
                sample.source_final_hdr);
        const auto target_display =
            appearance_reinhard_display(sanitized.value);
        const auto delta_e = appearance_oklab_delta_e(
            source_display,
            target_display);
        const auto chromaticity =
            appearance_rgb_chromaticity_delta(
                sample.source_final_hdr,
                sanitized.value);
        const auto source_luminance = std::log1p(
            std::max(
                0.0,
                appearance_luminance(
                    sample.source_final_hdr)));
        const auto target_luminance = std::log1p(
            std::max(
                0.0,
                appearance_luminance(sanitized.value)));
        const auto loss =
            0.8 * appearance_huber_loss(delta_e) +
            0.2 * std::abs(
                      source_luminance -
                      target_luminance);
        total_loss += loss;
        delta_es.push_back(delta_e);
        chromaticity_deltas.push_back(chromaticity);
        cluster_losses[sample.cluster] += loss;
        cluster_delta_es[sample.cluster].push_back(delta_e);
    }
    if (delta_es.empty())
    {
        return std::unexpected(
            PaintAppearanceFitError::NoPairedSamples);
    }
    output.paired_samples =
        static_cast<int>(delta_es.size());
    output.loss =
        total_loss / static_cast<double>(delta_es.size());
    output.median_delta_e = median(delta_es);
    output.median_chromaticity_delta =
        median(chromaticity_deltas);
    output.maximum_chromaticity_delta =
        *std::max_element(
            chromaticity_deltas.begin(),
            chromaticity_deltas.end());
    for (auto index = std::size_t{};
         index < output.clusters.size();
         ++index)
    {
        auto& cluster = output.clusters[index];
        auto& cluster_deltas = cluster_delta_es[index];
        if (cluster_deltas.empty())
        {
            continue;
        }
        cluster.paired_samples =
            static_cast<int>(cluster_deltas.size());
        cluster.loss =
            cluster_losses[index] /
            static_cast<double>(cluster_deltas.size());
        cluster.median_delta_e = median(cluster_deltas);
    }
    return output;
}
} // namespace meccha::core
