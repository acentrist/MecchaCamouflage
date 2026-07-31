#include <meccha/core/profile_paint_sampling.hpp>

#include <meccha/core/paint.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto SamplingDenominatorEpsilon = 1.0e-12;
constexpr auto SamplingBarycentricEpsilon = -1.0e-5;

auto barycentric(
    const PaintSamplingVertex& first,
    const PaintSamplingVertex& second,
    const PaintSamplingVertex& third,
    double u,
    double v) -> std::optional<ImageTriangleAnchor>
{
    const auto denominator =
        (second.v - third.v) * (first.u - third.u) +
        (third.u - second.u) * (first.v - third.v);
    if (!std::isfinite(denominator) ||
        std::abs(denominator) <= SamplingDenominatorEpsilon)
    {
        return std::nullopt;
    }
    const auto first_weight =
        ((second.v - third.v) * (u - third.u) +
         (third.u - second.u) * (v - third.v)) /
        denominator;
    const auto second_weight =
        ((third.v - first.v) * (u - third.u) +
         (first.u - third.u) * (v - third.v)) /
        denominator;
    const auto third_weight =
        1.0 - first_weight - second_weight;
    if (!std::isfinite(first_weight) ||
        !std::isfinite(second_weight) ||
        !std::isfinite(third_weight) ||
        first_weight < SamplingBarycentricEpsilon ||
        second_weight < SamplingBarycentricEpsilon ||
        third_weight < SamplingBarycentricEpsilon)
    {
        return std::nullopt;
    }
    return ImageTriangleAnchor{
        0U,
        first_weight,
        second_weight,
        third_weight,
    };
}
} // namespace

auto sample_paint_profile(
    const PaintSamplingProfile& sampling_profile,
    const CanonicalImageProfile& image_profile,
    double brush_size_texels,
    std::stop_token cancellation)
    -> std::expected<
        std::vector<ProfilePaintSample>,
        ProfilePaintSamplingError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            ProfilePaintSamplingError::Cancelled);
    }
    if (!validate_pair(
            sampling_profile,
            image_profile)
             .empty())
    {
        return std::unexpected(
            ProfilePaintSamplingError::InvalidProfile);
    }

    const auto texture_size = static_cast<double>(
        sampling_profile.identity.texture_size);
    const auto step = brush_size_texels / texture_size;
    if (!std::isfinite(brush_size_texels) ||
        !std::isfinite(step) || step <= 0.0)
    {
        return std::unexpected(
            ProfilePaintSamplingError::InvalidBrushSize);
    }

    auto samples = std::vector<ProfilePaintSample>{};
    samples.reserve(std::min<std::size_t>(
        sampling_profile.triangles->size() * 8U,
        100'000U));
    const auto append =
        [&](std::size_t triangle_index,
            std::uint32_t uv_island,
            double u,
            double v,
            ImageTriangleAnchor anchor)
            -> std::expected<void, ProfilePaintSamplingError>
        {
            if (samples.size() >= MaximumAdaptivePaintSamples)
            {
                return std::unexpected(
                    ProfilePaintSamplingError::ResourceLimit);
            }
            anchor.triangle_index = triangle_index;
            const auto mapped =
                map_image_triangle(image_profile, anchor);
            if (!mapped)
            {
                return std::unexpected(
                    ProfilePaintSamplingError::InvalidProfile);
            }
            samples.push_back(ProfilePaintSample{
                static_cast<int>(uv_island),
                std::clamp(u, 0.0, 1.0),
                std::clamp(v, 0.0, 1.0),
                anchor,
                *mapped,
            });
            return {};
        };

    for (auto triangle_index = std::size_t{};
         triangle_index < sampling_profile.triangles->size();
         ++triangle_index)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                ProfilePaintSamplingError::Cancelled);
        }
        const auto& triangle =
            sampling_profile.triangles->at(triangle_index);
        const auto& first =
            sampling_profile.vertices->at(triangle.first);
        const auto& second =
            sampling_profile.vertices->at(triangle.second);
        const auto& third =
            sampling_profile.vertices->at(triangle.third);
        const auto determinant =
            (second.v - third.v) * (first.u - third.u) +
            (third.u - second.u) * (first.v - third.v);
        if (!std::isfinite(determinant) ||
            std::abs(determinant) <= SamplingDenominatorEpsilon)
        {
            return std::unexpected(
                ProfilePaintSamplingError::InvalidProfile);
        }

        const auto minimum_u = std::clamp(
            std::min({first.u, second.u, third.u}),
            0.0,
            1.0);
        const auto maximum_u = std::clamp(
            std::max({first.u, second.u, third.u}),
            0.0,
            1.0);
        const auto minimum_v = std::clamp(
            std::min({first.v, second.v, third.v}),
            0.0,
            1.0);
        const auto maximum_v = std::clamp(
            std::max({first.v, second.v, third.v}),
            0.0,
            1.0);
        const auto start_u =
            std::floor(minimum_u / step) * step +
            step * 0.5;
        const auto start_v =
            std::floor(minimum_v / step) * step +
            step * 0.5;
        auto emitted = std::size_t{};
        auto row = std::size_t{};
        for (auto v = start_v;
             v <= maximum_v + step * 0.25;
             v += step, ++row)
        {
            if ((row % 32U) == 0U &&
                cancellation.stop_requested())
            {
                return std::unexpected(
                    ProfilePaintSamplingError::Cancelled);
            }
            for (auto u = start_u;
                 u <= maximum_u + step * 0.25;
                 u += step)
            {
                auto anchor =
                    barycentric(first, second, third, u, v);
                if (!anchor)
                {
                    continue;
                }
                const auto appended = append(
                    triangle_index,
                    triangle.uv_island,
                    u,
                    v,
                    *anchor);
                if (!appended)
                {
                    return std::unexpected(appended.error());
                }
                ++emitted;
            }
        }
        if (emitted == 0U)
        {
            const auto appended = append(
                triangle_index,
                triangle.uv_island,
                (first.u + second.u + third.u) / 3.0,
                (first.v + second.v + third.v) / 3.0,
                ImageTriangleAnchor{
                    triangle_index,
                    1.0 / 3.0,
                    1.0 / 3.0,
                    1.0 / 3.0,
                });
            if (!appended)
            {
                return std::unexpected(appended.error());
            }
        }
    }
    if (samples.empty())
    {
        return std::unexpected(
            ProfilePaintSamplingError::EmptySamples);
    }
    return samples;
}
} // namespace meccha::core
