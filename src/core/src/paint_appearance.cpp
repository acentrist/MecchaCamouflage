#include <meccha/core/paint_appearance.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <set>
#include <vector>

namespace meccha::core
{
namespace
{
struct AppearanceOkLab
{
    double l{};
    double a{};
    double b{};
};

auto linear_srgb_to_oklab(const AppearanceRgb& source)
    -> AppearanceOkLab
{
    const auto l = 0.4122214708 * source.r +
                   0.5363325363 * source.g +
                   0.0514459929 * source.b;
    const auto m = 0.2119034982 * source.r +
                   0.6806995451 * source.g +
                   0.1073969566 * source.b;
    const auto s = 0.0883024619 * source.r +
                   0.2817188376 * source.g +
                   0.6299787005 * source.b;
    const auto l_root = std::cbrt(std::max(0.0, l));
    const auto m_root = std::cbrt(std::max(0.0, m));
    const auto s_root = std::cbrt(std::max(0.0, s));
    return AppearanceOkLab{
        0.2104542553 * l_root +
            0.7936177850 * m_root -
            0.0040720468 * s_root,
        1.9779984951 * l_root -
            2.4285922050 * m_root +
            0.4505937099 * s_root,
        0.0259040371 * l_root +
            0.7827717662 * m_root -
            0.8086757660 * s_root,
    };
}

auto median(std::vector<double> values) -> double
{
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2U;
    return values.size() % 2U == 0U
               ? (values[middle - 1U] + values[middle]) *
                     0.5
               : values[middle];
}
} // namespace

auto appearance_rgb_finite(const AppearanceRgb& value) -> bool
{
    return std::isfinite(value.r) &&
           std::isfinite(value.g) &&
           std::isfinite(value.b);
}

auto appearance_sanitize_hdr(const AppearanceRgb& source)
    -> AppearanceHdrSample
{
    auto output = AppearanceHdrSample{};
    output.finite = appearance_rgb_finite(source);
    if (!output.finite)
    {
        return output;
    }
    output.value = AppearanceRgb{
        std::max(0.0, source.r),
        std::max(0.0, source.g),
        std::max(0.0, source.b),
    };
    output.clipped =
        output.value.r > AppearanceHdrMaximum ||
        output.value.g > AppearanceHdrMaximum ||
        output.value.b > AppearanceHdrMaximum;
    return output;
}

auto appearance_clamp_albedo(const AppearanceRgb& source)
    -> AppearanceRgb
{
    return AppearanceRgb{
        std::clamp(source.r, 0.0, 1.0),
        std::clamp(source.g, 0.0, 1.0),
        std::clamp(source.b, 0.0, 1.0),
    };
}

auto appearance_srgb_to_linear(double encoded) -> double
{
    encoded = std::clamp(encoded, 0.0, 1.0);
    return encoded <= 0.04045
               ? encoded / 12.92
               : std::pow((encoded + 0.055) / 1.055, 2.4);
}

auto appearance_linear_to_srgb(double linear) -> double
{
    linear = std::clamp(linear, 0.0, 1.0);
    return linear <= 0.0031308
               ? linear * 12.92
               : 1.055 * std::pow(linear, 1.0 / 2.4) -
                     0.055;
}

auto appearance_reinhard_display(const AppearanceRgb& source)
    -> AppearanceRgb
{
    const auto map = [](double value)
    {
        const auto positive = std::max(0.0, value);
        return positive / (1.0 + positive);
    };
    return AppearanceRgb{
        map(source.r),
        map(source.g),
        map(source.b),
    };
}

auto appearance_oklab_delta_e(
    const AppearanceRgb& left,
    const AppearanceRgb& right) -> double
{
    const auto left_lab = linear_srgb_to_oklab(left);
    const auto right_lab = linear_srgb_to_oklab(right);
    const auto dl = left_lab.l - right_lab.l;
    const auto da = left_lab.a - right_lab.a;
    const auto db = left_lab.b - right_lab.b;
    return std::sqrt(dl * dl + da * da + db * db);
}

auto appearance_rgb_chromaticity_delta(
    const AppearanceRgb& left,
    const AppearanceRgb& right) -> double
{
    const auto left_sum =
        std::max(0.0, left.r) +
        std::max(0.0, left.g) +
        std::max(0.0, left.b);
    const auto right_sum =
        std::max(0.0, right.r) +
        std::max(0.0, right.g) +
        std::max(0.0, right.b);
    if (!std::isfinite(left_sum) ||
        !std::isfinite(right_sum) ||
        left_sum <= 0.000001 ||
        right_sum <= 0.000001)
    {
        return std::numeric_limits<double>::infinity();
    }
    const auto dr =
        std::max(0.0, left.r) / left_sum -
        std::max(0.0, right.r) / right_sum;
    const auto dg =
        std::max(0.0, left.g) / left_sum -
        std::max(0.0, right.g) / right_sum;
    const auto db =
        std::max(0.0, left.b) / left_sum -
        std::max(0.0, right.b) / right_sum;
    return std::sqrt(dr * dr + dg * dg + db * db);
}

auto appearance_huber_loss(double value, double delta) -> double
{
    const auto absolute = std::abs(value);
    const auto safe_delta = std::max(0.000001, delta);
    return absolute <= safe_delta
               ? 0.5 * absolute * absolute
               : safe_delta *
                     (absolute - 0.5 * safe_delta);
}

auto appearance_luminance(const AppearanceRgb& source) -> double
{
    return 0.2126 * source.r +
           0.7152 * source.g +
           0.0722 * source.b;
}

auto appearance_intrinsic_emission_residual(
    const AppearanceRgb& isolated_hdr,
    const AppearanceRgb& base_srgb) -> AppearanceRgb
{
    if (!appearance_rgb_finite(isolated_hdr) ||
        !appearance_rgb_finite(base_srgb))
    {
        return {};
    }
    return AppearanceRgb{
        std::max(
            0.0,
            isolated_hdr.r -
                appearance_srgb_to_linear(base_srgb.r)),
        std::max(
            0.0,
            isolated_hdr.g -
                appearance_srgb_to_linear(base_srgb.g)),
        std::max(
            0.0,
            isolated_hdr.b -
                appearance_srgb_to_linear(base_srgb.b)),
    };
}

auto appearance_emission_chromaticity_albedo(
    const AppearanceRgb& intrinsic_emission_hdr,
    const AppearanceRgb& fallback_albedo) -> AppearanceRgb
{
    if (!appearance_rgb_finite(intrinsic_emission_hdr))
    {
        return appearance_clamp_albedo(fallback_albedo);
    }
    const auto positive = AppearanceRgb{
        std::max(0.0, intrinsic_emission_hdr.r),
        std::max(0.0, intrinsic_emission_hdr.g),
        std::max(0.0, intrinsic_emission_hdr.b),
    };
    const auto peak =
        std::max({positive.r, positive.g, positive.b});
    if (!std::isfinite(peak) || peak <= 0.000001)
    {
        return appearance_clamp_albedo(fallback_albedo);
    }
    return appearance_clamp_albedo(
        AppearanceRgb{
            positive.r / peak,
            positive.g / peak,
            positive.b / peak,
        });
}

auto appearance_emission_noise_model(
    const std::vector<double>& luminance_samples)
    -> AppearanceEmissionNoiseModel
{
    auto output = AppearanceEmissionNoiseModel{};
    auto finite_samples = std::vector<double>{};
    finite_samples.reserve(luminance_samples.size());
    for (const auto sample : luminance_samples)
    {
        if (std::isfinite(sample))
        {
            finite_samples.push_back(std::max(0.0, sample));
        }
    }
    if (finite_samples.size() < 3U)
    {
        return output;
    }
    output.median = median(finite_samples);
    auto deviations = std::vector<double>{};
    deviations.reserve(finite_samples.size());
    for (const auto sample : finite_samples)
    {
        deviations.push_back(
            std::abs(sample - output.median));
    }
    output.mad = median(std::move(deviations));
    output.threshold =
        output.median + std::max(0.01, 8.0 * output.mad);
    output.ok = std::isfinite(output.threshold);
    output.baseline_samples =
        static_cast<int>(finite_samples.size());
    return output;
}

auto appearance_source_emission_noise_model(
    const std::vector<double>& luminance_samples)
    -> AppearanceEmissionNoiseModel
{
    const auto full =
        appearance_emission_noise_model(luminance_samples);
    auto ordered = std::vector<double>{};
    ordered.reserve(luminance_samples.size());
    for (const auto sample : luminance_samples)
    {
        if (std::isfinite(sample))
        {
            ordered.push_back(std::max(0.0, sample));
        }
    }
    if (!full.ok || ordered.size() < 20U)
    {
        return full;
    }
    std::sort(ordered.begin(), ordered.end());
    const auto minimum_baseline =
        std::max<std::size_t>(8U, ordered.size() / 20U);
    const auto minimum_signal =
        std::max<std::size_t>(8U, ordered.size() / 10U);
    const auto maximum_split = std::min(
        ordered.size() / 2U,
        ordered.size() - minimum_signal);
    if (minimum_baseline > maximum_split)
    {
        return full;
    }

    auto best_split = std::size_t{};
    auto best_gap = 0.0;
    for (auto split = minimum_baseline;
         split <= maximum_split;
         ++split)
    {
        const auto gap = ordered[split] - ordered[split - 1U];
        if (gap > best_gap)
        {
            best_gap = gap;
            best_split = split;
        }
    }
    if (best_split == 0U || best_gap < 0.02)
    {
        return full;
    }

    const auto separated = appearance_emission_noise_model(
        std::vector<double>{
            ordered.begin(),
            ordered.begin() +
                static_cast<std::ptrdiff_t>(best_split)});
    if (!separated.ok ||
        best_gap < std::max(0.02, 8.0 * separated.mad) ||
        ordered[best_split] <= separated.threshold + 0.02)
    {
        return full;
    }
    auto output = separated;
    output.separated_signal = true;
    output.baseline_samples =
        static_cast<int>(best_split);
    return output;
}

auto appearance_combine_emission_noise_models(
    const AppearanceEmissionNoiseModel& source,
    const AppearanceEmissionNoiseModel& target_e0)
    -> AppearanceEmissionNoiseModel
{
    auto output = source;
    if (!source.ok || !target_e0.ok ||
        !std::isfinite(source.threshold) ||
        !std::isfinite(target_e0.threshold))
    {
        output.ok = false;
        return output;
    }
    output.threshold =
        std::max(source.threshold, target_e0.threshold);
    output.ok = std::isfinite(output.threshold);
    return output;
}

auto appearance_emission_sample_detected(
    double residual_luminance,
    const AppearanceEmissionNoiseModel& e0_noise) -> bool
{
    return e0_noise.ok &&
           std::isfinite(residual_luminance) &&
           residual_luminance > e0_noise.threshold;
}

auto appearance_filter_emission_surface_halo(
    const std::vector<AppearanceEmissionSurfacePoint>& points)
    -> AppearanceEmissionSurfaceFilter
{
    auto output = AppearanceEmissionSurfaceFilter{};
    output.keep.assign(points.size(), true);
    output.kept_samples = static_cast<int>(points.size());
    if (points.size() < 6U)
    {
        return output;
    }

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
                        points[current].screen.x) -
                    static_cast<std::int64_t>(
                        points[candidate].screen.x));
                const auto dy = std::abs(
                    static_cast<std::int64_t>(
                        points[current].screen.y) -
                    static_cast<std::int64_t>(
                        points[candidate].screen.y));
                if (dx <= 1 && dy <= 1)
                {
                    visited[candidate] = true;
                    stack.push_back(candidate);
                }
            }
        }
        if (component.size() < 6U)
        {
            continue;
        }

        auto luminance = std::vector<double>{};
        luminance.reserve(component.size());
        for (const auto index : component)
        {
            if (std::isfinite(
                    points[index].residual_luminance))
            {
                luminance.push_back(std::max(
                    0.0,
                    points[index].residual_luminance));
            }
        }
        if (luminance.size() != component.size())
        {
            continue;
        }
        const auto core_model =
            appearance_emission_noise_model(luminance);
        if (!core_model.ok)
        {
            continue;
        }

        auto core = std::vector<std::size_t>{};
        auto core_surfaces = std::set<std::uint64_t>{};
        auto all_surfaces = std::set<std::uint64_t>{};
        for (const auto index : component)
        {
            const auto key = points[index].surface_key;
            if (key != 0U)
            {
                all_surfaces.insert(key);
            }
            if (key != 0U &&
                points[index].residual_luminance >
                    core_model.threshold)
            {
                core.push_back(index);
                core_surfaces.insert(key);
            }
        }
        if (core.size() < 3U ||
            core.size() * 4U > component.size() ||
            core_surfaces.empty() ||
            core_surfaces.size() >= all_surfaces.size())
        {
            continue;
        }

        auto rejected = std::vector<std::size_t>{};
        rejected.reserve(component.size());
        auto minimum_core =
            std::numeric_limits<double>::infinity();
        auto maximum_rejected = 0.0;
        for (const auto index : core)
        {
            minimum_core = std::min(
                minimum_core,
                points[index].residual_luminance);
        }
        for (const auto index : component)
        {
            const auto key = points[index].surface_key;
            if (key == 0U ||
                core_surfaces.contains(key))
            {
                continue;
            }
            rejected.push_back(index);
            maximum_rejected = std::max(
                maximum_rejected,
                points[index].residual_luminance);
        }
        const auto minimum_separation =
            std::max(0.02, 2.0 * core_model.mad);
        if (rejected.size() * 2U < component.size() ||
            !std::isfinite(minimum_core) ||
            minimum_core - maximum_rejected <
                minimum_separation)
        {
            continue;
        }

        ++output.applied_regions;
        output.core_samples +=
            static_cast<int>(core.size());
        output.core_surface_count +=
            static_cast<int>(core_surfaces.size());
        output.maximum_core_threshold = std::max(
            output.maximum_core_threshold,
            core_model.threshold);
        for (const auto index : rejected)
        {
            output.keep[index] = false;
        }
        output.halo_rejected_samples +=
            static_cast<int>(rejected.size());
        output.kept_samples -=
            static_cast<int>(rejected.size());
    }
    return output;
}

auto appearance_emission_projected_value(
    double projected_emissive,
    bool intrinsic_emission_roi) -> double
{
    if (!intrinsic_emission_roi ||
        !std::isfinite(projected_emissive))
    {
        return 0.0;
    }
    return std::clamp(
        std::max(projected_emissive, 1.0 / 255.0),
        0.0,
        1.0);
}

auto appearance_calibrate_emissive(
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& fallback_hdr,
    const AppearanceRgb& endpoint_hdr)
    -> AppearanceEmissiveCalibration
{
    auto output = AppearanceEmissiveCalibration{};
    if (!appearance_rgb_finite(source_hdr) ||
        !appearance_rgb_finite(fallback_hdr) ||
        !appearance_rgb_finite(endpoint_hdr))
    {
        return output;
    }
    const auto log_channel = [](double channel)
    {
        return std::log1p(std::max(0.0, channel));
    };
    const auto source = AppearanceRgb{
        log_channel(source_hdr.r),
        log_channel(source_hdr.g),
        log_channel(source_hdr.b),
    };
    const auto fallback = AppearanceRgb{
        log_channel(fallback_hdr.r),
        log_channel(fallback_hdr.g),
        log_channel(fallback_hdr.b),
    };
    const auto endpoint = AppearanceRgb{
        log_channel(endpoint_hdr.r),
        log_channel(endpoint_hdr.g),
        log_channel(endpoint_hdr.b),
    };
    const auto response = AppearanceRgb{
        endpoint.r - fallback.r,
        endpoint.g - fallback.g,
        endpoint.b - fallback.b,
    };
    const auto residual = AppearanceRgb{
        source.r - fallback.r,
        source.g - fallback.g,
        source.b - fallback.b,
    };
    output.response_energy =
        response.r * response.r +
        response.g * response.g +
        response.b * response.b;
    const auto endpoint_luminance =
        appearance_luminance(endpoint_hdr);
    const auto fallback_luminance =
        appearance_luminance(fallback_hdr);
    if (!std::isfinite(output.response_energy) ||
        output.response_energy <= 0.00000001 ||
        !std::isfinite(endpoint_luminance) ||
        !std::isfinite(fallback_luminance) ||
        endpoint_luminance <= fallback_luminance + 0.0001)
    {
        return output;
    }
    const auto projected =
        (residual.r * response.r +
         residual.g * response.g +
         residual.b * response.b) /
        output.response_energy;
    if (!std::isfinite(projected))
    {
        return output;
    }
    output.supported = true;
    output.emissive = std::clamp(projected, 0.0, 1.0);
    return output;
}

auto appearance_calibrate_bounded_response(
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& baseline_hdr,
    const AppearanceRgb& endpoint_hdr,
    double baseline_parameter,
    double endpoint_parameter,
    double minimum_parameter,
    double maximum_parameter)
    -> AppearanceBoundedResponseCalibration
{
    auto output = AppearanceBoundedResponseCalibration{};
    if (!appearance_rgb_finite(source_hdr) ||
        !appearance_rgb_finite(baseline_hdr) ||
        !appearance_rgb_finite(endpoint_hdr) ||
        !std::isfinite(baseline_parameter) ||
        !std::isfinite(endpoint_parameter) ||
        !std::isfinite(minimum_parameter) ||
        !std::isfinite(maximum_parameter) ||
        endpoint_parameter == baseline_parameter ||
        maximum_parameter < minimum_parameter)
    {
        return output;
    }
    const auto log_channel = [](double channel)
    {
        return std::log1p(std::max(0.0, channel));
    };
    const auto source = AppearanceRgb{
        log_channel(source_hdr.r),
        log_channel(source_hdr.g),
        log_channel(source_hdr.b),
    };
    const auto baseline = AppearanceRgb{
        log_channel(baseline_hdr.r),
        log_channel(baseline_hdr.g),
        log_channel(baseline_hdr.b),
    };
    const auto endpoint = AppearanceRgb{
        log_channel(endpoint_hdr.r),
        log_channel(endpoint_hdr.g),
        log_channel(endpoint_hdr.b),
    };
    const auto response = AppearanceRgb{
        endpoint.r - baseline.r,
        endpoint.g - baseline.g,
        endpoint.b - baseline.b,
    };
    const auto residual = AppearanceRgb{
        source.r - baseline.r,
        source.g - baseline.g,
        source.b - baseline.b,
    };
    output.response_energy =
        response.r * response.r +
        response.g * response.g +
        response.b * response.b;
    if (!std::isfinite(output.response_energy) ||
        output.response_energy <= 0.00000001)
    {
        return output;
    }
    const auto projected =
        (residual.r * response.r +
         residual.g * response.g +
         residual.b * response.b) /
        output.response_energy;
    if (!std::isfinite(projected))
    {
        return output;
    }
    output.supported = true;
    output.parameter = std::clamp(
        baseline_parameter +
            (endpoint_parameter - baseline_parameter) *
                projected,
        minimum_parameter,
        maximum_parameter);
    return output;
}

auto appearance_calibrate_albedo_blend(
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& display_albedo_capture_hdr,
    const AppearanceRgb& base_albedo_capture_hdr)
    -> AppearanceBoundedResponseCalibration
{
    return appearance_calibrate_bounded_response(
        source_hdr,
        display_albedo_capture_hdr,
        base_albedo_capture_hdr,
        1.0,
        0.0,
        0.0,
        1.0);
}

auto appearance_calibrate_albedo_chromaticity(
    const AppearanceRgb& base_albedo,
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& base_albedo_capture_hdr)
    -> AppearanceAlbedoRgbCalibration
{
    auto output = AppearanceAlbedoRgbCalibration{};
    output.albedo = appearance_clamp_albedo(base_albedo);
    if (!appearance_rgb_finite(base_albedo) ||
        !appearance_rgb_finite(source_hdr) ||
        !appearance_rgb_finite(base_albedo_capture_hdr))
    {
        return output;
    }
    const auto source_sum =
        source_hdr.r + source_hdr.g + source_hdr.b;
    const auto target_sum =
        base_albedo_capture_hdr.r +
        base_albedo_capture_hdr.g +
        base_albedo_capture_hdr.b;
    const auto base_luminance =
        appearance_luminance(base_albedo);
    if (!std::isfinite(source_sum) ||
        !std::isfinite(target_sum) ||
        source_sum <= 0.001 || target_sum <= 0.001 ||
        base_luminance <= 0.00001)
    {
        return output;
    }

    const auto calibrate_channel = [](
        double base,
        double source_chroma,
        double target_chroma,
        double& calibrated)
    {
        if (!std::isfinite(source_chroma) ||
            !std::isfinite(target_chroma) ||
            target_chroma <= 0.005)
        {
            return false;
        }
        calibrated = base * source_chroma / target_chroma;
        return std::isfinite(calibrated);
    };
    output.channel_supported[0] = calibrate_channel(
        base_albedo.r,
        source_hdr.r / source_sum,
        base_albedo_capture_hdr.r / target_sum,
        output.albedo.r);
    output.channel_supported[1] = calibrate_channel(
        base_albedo.g,
        source_hdr.g / source_sum,
        base_albedo_capture_hdr.g / target_sum,
        output.albedo.g);
    output.channel_supported[2] = calibrate_channel(
        base_albedo.b,
        source_hdr.b / source_sum,
        base_albedo_capture_hdr.b / target_sum,
        output.albedo.b);
    output.responsive_channels =
        static_cast<int>(output.channel_supported[0]) +
        static_cast<int>(output.channel_supported[1]) +
        static_cast<int>(output.channel_supported[2]);
    output.supported = output.responsive_channels >= 2;
    const auto calibrated_luminance =
        appearance_luminance(output.albedo);
    if (!output.supported ||
        !std::isfinite(calibrated_luminance) ||
        calibrated_luminance <= 0.00001)
    {
        output.albedo = appearance_clamp_albedo(base_albedo);
        output.supported = false;
        return output;
    }
    const auto preserve_luminance =
        base_luminance / calibrated_luminance;
    output.albedo = appearance_clamp_albedo(
        AppearanceRgb{
            output.albedo.r * preserve_luminance,
            output.albedo.g * preserve_luminance,
            output.albedo.b * preserve_luminance,
        });
    return output;
}

auto appearance_robust_albedo_chromaticity_gain(
    const std::array<std::vector<double>, 3>&
        log_gain_estimates)
    -> AppearanceAlbedoChromaticityGain
{
    auto output = AppearanceAlbedoChromaticityGain{};
    auto gains = std::array<double, 3>{1.0, 1.0, 1.0};
    for (auto channel = std::size_t{};
         channel < log_gain_estimates.size(); ++channel)
    {
        auto finite = std::vector<double>{};
        finite.reserve(log_gain_estimates[channel].size());
        std::ranges::copy_if(
            log_gain_estimates[channel],
            std::back_inserter(finite),
            [](double estimate)
            {
                return std::isfinite(estimate);
            });
        if (finite.size() < static_cast<std::size_t>(
                AppearanceMinimumLocalResponseSamples))
        {
            continue;
        }
        const auto middle = finite.begin() +
            static_cast<std::ptrdiff_t>(finite.size() / 2U);
        std::nth_element(finite.begin(), middle, finite.end());
        const auto gain = std::exp(*middle);
        if (!std::isfinite(gain) || gain <= 0.0)
        {
            continue;
        }
        gains[channel] = gain;
        ++output.responsive_channels;
    }
    output.supported = output.responsive_channels >= 2;
    if (output.supported)
    {
        output.gain = AppearanceRgb{
            gains[0], gains[1], gains[2]};
    }
    return output;
}

auto appearance_apply_albedo_chromaticity_gain(
    const AppearanceRgb& base_albedo,
    const AppearanceRgb& gain) -> AppearanceRgb
{
    const auto base = appearance_clamp_albedo(base_albedo);
    if (!appearance_rgb_finite(base) ||
        !appearance_rgb_finite(gain) ||
        gain.r <= 0.0 || gain.g <= 0.0 || gain.b <= 0.0)
    {
        return base;
    }
    const auto adjusted = AppearanceRgb{
        base.r * gain.r,
        base.g * gain.g,
        base.b * gain.b,
    };
    const auto base_luminance = appearance_luminance(base);
    const auto adjusted_luminance =
        appearance_luminance(adjusted);
    if (!std::isfinite(base_luminance) ||
        !std::isfinite(adjusted_luminance) ||
        adjusted_luminance <= 0.00001)
    {
        return base;
    }
    const auto preserve_luminance =
        base_luminance / adjusted_luminance;
    return appearance_clamp_albedo(
        AppearanceRgb{
            adjusted.r * preserve_luminance,
            adjusted.g * preserve_luminance,
            adjusted.b * preserve_luminance,
        });
}

auto appearance_quantize_unit(double value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

auto appearance_calibrate_linear_readback(
    const std::vector<AppearanceRgb>& expected,
    const std::vector<AppearanceRgb>& raw,
    double maximum_median_error)
    -> AppearanceReadbackCalibration
{
    auto output = AppearanceReadbackCalibration{};
    const auto count = std::min(expected.size(), raw.size());
    if (count < 16U)
    {
        return output;
    }
    const auto median_for =
        [&](AppearanceReadbackTransform transform)
    {
        auto errors = std::vector<double>{};
        errors.reserve(count);
        for (auto index = std::size_t{};
             index < count;
             ++index)
        {
            auto candidate = raw[index];
            if (transform ==
                AppearanceReadbackTransform::SwapRedBlue)
            {
                std::swap(candidate.r, candidate.b);
            }
            errors.push_back(std::max(
                {
                    std::abs(
                        expected[index].r - candidate.r),
                    std::abs(
                        expected[index].g - candidate.g),
                    std::abs(
                        expected[index].b - candidate.b),
                }));
        }
        const auto middle =
            errors.begin() +
            static_cast<std::ptrdiff_t>(
                errors.size() / 2U);
        std::nth_element(
            errors.begin(),
            middle,
            errors.end());
        return *middle;
    };
    const auto identity = median_for(
        AppearanceReadbackTransform::Identity);
    const auto swapped = median_for(
        AppearanceReadbackTransform::SwapRedBlue);
    output.transform =
        swapped < identity
            ? AppearanceReadbackTransform::SwapRedBlue
            : AppearanceReadbackTransform::Identity;
    output.median_error = std::min(identity, swapped);
    output.runner_up_median = std::max(identity, swapped);
    const auto separated =
        output.runner_up_median >=
            output.median_error * 1.10 ||
        output.runner_up_median - output.median_error >=
            0.005;
    output.ok =
        std::isfinite(output.median_error) &&
        output.median_error <= maximum_median_error &&
        separated;
    return output;
}

} // namespace meccha::core
