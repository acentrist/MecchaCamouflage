#include <meccha/core/paint_appearance.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

auto appearance_make_fallback(
    const AppearanceRgb& display_linear) -> AppearanceFallback
{
    return AppearanceFallback{
        appearance_clamp_albedo(display_linear),
        AppearanceMaterial{
            1.0,
            0.0,
            AppearanceFallbackRoughness,
            0.0,
        },
    };
}

auto appearance_make_safe_fallback(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    bool base_available) -> AppearanceFallback
{
    const auto use_base =
        base_available && appearance_rgb_finite(base_linear);
    auto output = appearance_make_fallback(
        use_base ? base_linear : display_linear);
    output.material.albedo_blend = use_base ? 0.0 : 1.0;
    return output;
}

auto appearance_make_safe_final_fallback(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    bool base_available,
    bool intrinsic_emission_roi) -> AppearanceFallback
{
    return intrinsic_emission_roi
               ? appearance_make_fallback(display_linear)
               : appearance_make_safe_fallback(
                     base_linear,
                     display_linear,
                     base_available);
}

auto appearance_blend_albedo(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    double blend) -> AppearanceRgb
{
    const auto safe_blend = std::clamp(blend, 0.0, 1.0);
    return appearance_clamp_albedo(
        AppearanceRgb{
            base_linear.r +
                (display_linear.r - base_linear.r) *
                    safe_blend,
            base_linear.g +
                (display_linear.g - base_linear.g) *
                    safe_blend,
            base_linear.b +
                (display_linear.b - base_linear.b) *
                    safe_blend,
        });
}

auto appearance_source_albedo_target(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    const AppearanceRgb& emission_linear,
    bool emission_roi,
    bool include_shadows) -> AppearanceRgb
{
    if (emission_roi &&
        appearance_rgb_finite(emission_linear))
    {
        return appearance_clamp_albedo(emission_linear);
    }
    if (include_shadows &&
        appearance_rgb_finite(display_linear))
    {
        return appearance_clamp_albedo(display_linear);
    }
    return appearance_clamp_albedo(base_linear);
}

auto appearance_parameterized_albedo(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& target_linear,
    double parameter,
    bool emission_roi) -> AppearanceRgb
{
    if (!emission_roi)
    {
        return appearance_blend_albedo(
            base_linear,
            target_linear,
            parameter);
    }
    if (!appearance_rgb_finite(target_linear))
    {
        return appearance_clamp_albedo(base_linear);
    }
    const auto scale =
        std::isfinite(parameter)
            ? std::clamp(parameter, 0.0, 1.0)
            : 0.0;
    return appearance_clamp_albedo(
        AppearanceRgb{
            target_linear.r * scale,
            target_linear.g * scale,
            target_linear.b * scale,
        });
}

auto appearance_initial_albedo_blend(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear) -> double
{
    const auto maximum_delta = std::max(
        {
            std::abs(base_linear.r - display_linear.r),
            std::abs(base_linear.g - display_linear.g),
            std::abs(base_linear.b - display_linear.b),
        });
    return std::clamp(maximum_delta / 0.20, 0.0, 1.0);
}

auto appearance_initial_emissive(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& final_hdr) -> double
{
    const auto base =
        std::max(0.0, appearance_luminance(base_linear));
    const auto final =
        std::max(0.0, appearance_luminance(final_hdr));
    const auto stops =
        std::log2(1.0 + final) -
        std::log2(1.0 + base);
    return std::clamp((stops - 0.15) / 2.0, 0.0, 1.0);
}

auto appearance_quantize_unit(double value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
}

auto appearance_material_key(
    const AppearanceMaterial& material,
    bool fallback) -> std::uint64_t
{
    auto hash = std::uint64_t{1469598103934665603ULL};
    const auto mix = [&hash](std::uint8_t value)
    {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    };
    mix(appearance_quantize_unit(material.metallic));
    mix(appearance_quantize_unit(material.roughness));
    mix(appearance_quantize_unit(material.emissive));
    mix(fallback ? 1U : 0U);
    return hash;
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

auto appearance_parameter_bound(std::size_t parameter_index)
    -> double
{
    static_cast<void>(parameter_index);
    return 1.0;
}

auto appearance_spsa_hash(std::uint64_t value) -> std::uint64_t
{
    value += 0x9e3779b97f4a7c15ULL;
    value =
        (value ^ (value >> 30U)) *
        0xbf58476d1ce4e5b9ULL;
    value =
        (value ^ (value >> 27U)) *
        0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

auto appearance_spsa_pair(
    const std::vector<double>& parameters,
    int iteration,
    std::uint64_t seed) -> AppearanceSpsaPair
{
    auto output = AppearanceSpsaPair{};
    output.plus.resize(parameters.size());
    output.minus.resize(parameters.size());
    output.direction.resize(parameters.size());
    const auto decay = std::pow(
        static_cast<double>(std::max(1, iteration + 1)),
        0.101);
    constexpr auto perturbation =
        std::array<double, 4U>{0.12, 0.12, 0.15, 0.15};
    for (auto index = std::size_t{};
         index < parameters.size();
         ++index)
    {
        const auto random = appearance_spsa_hash(
            seed ^
            (static_cast<std::uint64_t>(iteration + 1)
             << 32U) ^
            static_cast<std::uint64_t>(index + 1U));
        const auto direction =
            (random & 1ULL) == 0ULL ? -1.0 : 1.0;
        const auto delta =
            perturbation[index % perturbation.size()] /
            decay;
        output.direction[index] = direction;
        output.plus[index] = std::clamp(
            parameters[index] + direction * delta,
            0.0,
            appearance_parameter_bound(index));
        output.minus[index] = std::clamp(
            parameters[index] - direction * delta,
            0.0,
            appearance_parameter_bound(index));
    }
    return output;
}

auto appearance_spsa_update(
    const std::vector<double>& parameters,
    const AppearanceSpsaPair& pair,
    double loss_plus,
    double loss_minus,
    int iteration) -> std::vector<double>
{
    if (parameters.size() != pair.plus.size() ||
        parameters.size() != pair.minus.size() ||
        parameters.size() != pair.direction.size() ||
        !std::isfinite(loss_plus) ||
        !std::isfinite(loss_minus))
    {
        return parameters;
    }
    auto output = parameters;
    const auto gain_decay = std::pow(
        static_cast<double>(std::max(1, iteration + 1)),
        0.602);
    constexpr auto gains =
        std::array<double, 4U>{0.18, 0.15, 0.18, 0.20};
    constexpr auto perturbation =
        std::array<double, 4U>{0.12, 0.12, 0.15, 0.15};
    for (auto index = std::size_t{};
         index < parameters.size();
         ++index)
    {
        const auto delta =
            perturbation[index % perturbation.size()] /
            std::pow(
                static_cast<double>(
                    std::max(1, iteration + 1)),
                0.101);
        if (delta <= 0.0 || !std::isfinite(delta) ||
            pair.direction[index] == 0.0)
        {
            continue;
        }
        const auto gradient =
            ((loss_plus - loss_minus) / (2.0 * delta)) *
            pair.direction[index];
        const auto gain =
            gains[index % gains.size()] / gain_decay;
        output[index] = std::clamp(
            parameters[index] - gain * gradient,
            0.0,
            appearance_parameter_bound(index));
    }
    return output;
}

auto appearance_spsa_update_by_cluster(
    const std::vector<double>& parameters,
    const AppearanceSpsaPair& pair,
    const std::vector<double>& loss_plus_by_cluster,
    const std::vector<double>& loss_minus_by_cluster,
    int iteration) -> std::vector<double>
{
    if (parameters.size() != pair.plus.size() ||
        parameters.size() != pair.minus.size() ||
        parameters.size() != pair.direction.size() ||
        parameters.size() % 4U != 0U ||
        loss_plus_by_cluster.size() < parameters.size() / 4U ||
        loss_minus_by_cluster.size() < parameters.size() / 4U)
    {
        return parameters;
    }
    auto output = parameters;
    const auto gain_decay = std::pow(
        static_cast<double>(std::max(1, iteration + 1)),
        0.602);
    constexpr auto gains =
        std::array<double, 4U>{0.18, 0.15, 0.18, 0.20};
    constexpr auto perturbation =
        std::array<double, 4U>{0.12, 0.12, 0.15, 0.15};
    for (auto index = std::size_t{};
         index < parameters.size();
         ++index)
    {
        const auto cluster = index / 4U;
        const auto loss_plus =
            loss_plus_by_cluster[cluster];
        const auto loss_minus =
            loss_minus_by_cluster[cluster];
        const auto delta =
            perturbation[index % perturbation.size()] /
            std::pow(
                static_cast<double>(
                    std::max(1, iteration + 1)),
                0.101);
        if (!std::isfinite(loss_plus) ||
            !std::isfinite(loss_minus) || delta <= 0.0 ||
            !std::isfinite(delta) ||
            pair.direction[index] == 0.0)
        {
            continue;
        }
        const auto gradient =
            ((loss_plus - loss_minus) / (2.0 * delta)) *
            pair.direction[index];
        const auto gain =
            gains[index % gains.size()] / gain_decay;
        output[index] = std::clamp(
            parameters[index] - gain * gradient,
            0.0,
            appearance_parameter_bound(index));
    }
    return output;
}

auto appearance_fit_accepted(
    const AppearanceFitAcceptance& value) -> bool
{
    if (value.paired_samples < AppearanceFitMinimumSamples ||
        !value.camera_stable ||
        !value.readback_calibrated ||
        !std::isfinite(value.fallback_loss) ||
        !std::isfinite(value.best_loss) ||
        !std::isfinite(value.median_delta_e) ||
        value.fallback_loss <= 0.0)
    {
        return false;
    }
    const auto improvement =
        (value.fallback_loss - value.best_loss) /
        value.fallback_loss;
    return improvement >= AppearanceFitMinimumImprovement &&
           value.median_delta_e <=
               AppearanceFitMedianDeltaEMaximum;
}

auto appearance_non_emission_candidate_accepted(
    const AppearanceNonEmissionCandidateAcceptance& value)
    -> bool
{
    const auto emission_roi_stable =
        value.emission_roi_samples <= 0 ||
        (std::isfinite(value.emission_roi_loss_initial) &&
         std::isfinite(value.emission_roi_loss_candidate) &&
         value.emission_roi_loss_candidate -
                 value.emission_roi_loss_initial <=
             0.01);
    const auto chromaticity_tail_stable =
        std::isfinite(
            value.reference_max_chromaticity_delta) &&
        std::isfinite(
            value.candidate_max_chromaticity_delta) &&
        value.candidate_max_chromaticity_delta <=
            value.reference_max_chromaticity_delta +
                1.0 / 255.0;
    return value.emissive_nonzero_samples == 0 &&
           value.packed_b_verified &&
           emission_roi_stable &&
           chromaticity_tail_stable &&
           appearance_fit_accepted(
               AppearanceFitAcceptance{
                   value.paired_samples,
                   value.camera_stable,
                   value.readback_calibrated,
                   value.fallback_loss,
                   value.candidate_loss,
                   value.candidate_median_delta_e,
               });
}
} // namespace meccha::core
