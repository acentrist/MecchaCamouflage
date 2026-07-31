#include <meccha/core/image_paint_plan.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto SamplingDenominatorEpsilon = 1.0e-12;
constexpr auto SamplingBarycentricEpsilon = -1.0e-5;

auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto region_valid(Region region) -> bool
{
    return region == Region::Front ||
           region == Region::Side ||
           region == Region::Back;
}

auto mode_for(
    ImageAtlasFace face,
    const ImageProjectSettings& settings) -> FaceBaseMode
{
    switch (face)
    {
    case ImageAtlasFace::Front:
        return settings.front;
    case ImageAtlasFace::Right:
        return settings.right;
    case ImageAtlasFace::Back:
        return settings.back;
    case ImageAtlasFace::Left:
        return settings.left;
    }
    return FaceBaseMode::Skip;
}

auto material_key(const Material& material) -> std::uint64_t
{
    auto hash = std::uint64_t{1469598103934665603ULL};
    const auto mix =
        [&hash](double value)
        {
            const auto quantized = static_cast<std::uint8_t>(
                std::lround(
                    std::clamp(value, 0.0, 1.0) * 255.0));
            hash ^= quantized;
            hash *= 1099511628211ULL;
        };
    mix(material.metallic);
    mix(material.roughness);
    mix(material.emissive);
    return hash;
}

auto replay_error(ReplayPlanError error) -> ImagePaintPlanError
{
    switch (error)
    {
    case ReplayPlanError::InvalidArgument:
        return ImagePaintPlanError::InvalidSettings;
    case ReplayPlanError::InvalidCandidate:
        return ImagePaintPlanError::InvalidSample;
    case ReplayPlanError::ResourceLimit:
        return ImagePaintPlanError::ResourceLimit;
    case ReplayPlanError::Cancelled:
        return ImagePaintPlanError::Cancelled;
    }
    return ImagePaintPlanError::InvalidSample;
}

auto adaptive_error(AdaptivePaintPlanError error)
    -> ImagePaintPlanError
{
    switch (error)
    {
    case AdaptivePaintPlanError::InvalidArgument:
        return ImagePaintPlanError::InvalidSettings;
    case AdaptivePaintPlanError::InvalidSample:
    case AdaptivePaintPlanError::InvalidReplayEntry:
        return ImagePaintPlanError::InvalidSample;
    case AdaptivePaintPlanError::ResourceLimit:
        return ImagePaintPlanError::ResourceLimit;
    case AdaptivePaintPlanError::Cancelled:
        return ImagePaintPlanError::Cancelled;
    }
    return ImagePaintPlanError::InvalidSample;
}

auto atlas_color(
    std::span<const std::byte> atlas,
    double u,
    double v) -> std::array<std::uint8_t, 4U>
{
    const auto x = static_cast<std::uint32_t>(
        std::lround(
            std::clamp(u, 0.0, 1.0) *
            static_cast<double>(CanonicalAtlasWidth - 1U)));
    const auto y = static_cast<std::uint32_t>(
        std::lround(
            (1.0 - std::clamp(v, 0.0, 1.0)) *
            static_cast<double>(CanonicalAtlasHeight - 1U)));
    const auto offset =
        (static_cast<std::size_t>(y) *
             CanonicalAtlasWidth +
         x) *
        4U;
    return {
        std::to_integer<std::uint8_t>(atlas[offset]),
        std::to_integer<std::uint8_t>(atlas[offset + 1U]),
        std::to_integer<std::uint8_t>(atlas[offset + 2U]),
        std::to_integer<std::uint8_t>(atlas[offset + 3U]),
    };
}

auto candidate(
    std::size_t index,
    const CapturedImagePaintSample& sample,
    RegionMode mode) -> ReplayCandidate
{
    return ReplayCandidate{
        index,
        sample.paint_region,
        mode,
        sample.uv_island,
        sample.paint_u,
        sample.paint_v,
        sample.has_current_view_position,
        sample.current_view_vertical,
        sample.fallback_view_vertical,
        sample.horizontal,
        index,
    };
}

auto region_for(ImageAtlasFace face) -> Region
{
    switch (face)
    {
    case ImageAtlasFace::Front:
        return Region::Front;
    case ImageAtlasFace::Back:
        return Region::Back;
    case ImageAtlasFace::Right:
    case ImageAtlasFace::Left:
        return Region::Side;
    }
    return Region::Side;
}

auto valid_sampling_profile(
    const PaintSamplingProfile& sampling,
    const CanonicalImageProfile& image) -> bool
{
    const auto& identity = sampling.identity;
    const auto& image_identity = image.geometry.identity;
    if (!validate(identity).empty() ||
        identity.role != MeshProfileRole::Raw ||
        !validate(image_identity).empty() ||
        image_identity.role != MeshProfileRole::ImageReference ||
        identity.body != image_identity.body ||
        image_identity.base_profile_id != identity.profile_id ||
        image_identity.base_profile_hash != identity.profile_hash ||
        !sampling.vertices || !sampling.triangles ||
        sampling.vertices->size() != identity.vertex_count ||
        sampling.triangles->size() != identity.triangle_count ||
        !image.geometry.indices ||
        image.geometry.indices->size() != identity.index_count ||
        identity.index_count != identity.triangle_count * 3U)
    {
        return false;
    }

    for (const auto& vertex : *sampling.vertices)
    {
        if (!unit(vertex.u) || !unit(vertex.v))
        {
            return false;
        }
    }
    for (auto index = std::size_t{};
         index < sampling.triangles->size();
         ++index)
    {
        const auto& triangle = sampling.triangles->at(index);
        if (triangle.first >= sampling.vertices->size() ||
            triangle.second >= sampling.vertices->size() ||
            triangle.third >= sampling.vertices->size() ||
            triangle.uv_island >= identity.uv_island_count)
        {
            return false;
        }
        const auto base = index * 3U;
        if (image.geometry.indices->at(base) != triangle.first ||
            image.geometry.indices->at(base + 1U) !=
                triangle.second ||
            image.geometry.indices->at(base + 2U) !=
                triangle.third)
        {
            return false;
        }
    }
    return true;
}

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

auto generate_profile_samples(
    const ImagePaintProfilePlanRequest& request,
    std::stop_token cancellation)
    -> std::expected<
        std::vector<CapturedImagePaintSample>,
        ImagePaintPlanError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImagePaintPlanError::Cancelled);
    }
    if (!valid_sampling_profile(
            request.sampling_profile,
            request.image_profile))
    {
        return std::unexpected(ImagePaintPlanError::InvalidProfile);
    }
    if (!validate(request.settings).empty() ||
        request.settings.body !=
            request.sampling_profile.identity.body)
    {
        return std::unexpected(ImagePaintPlanError::InvalidSettings);
    }

    const auto texture_size = static_cast<double>(
        request.sampling_profile.identity.texture_size);
    const auto step =
        request.settings.brush_size_texels / texture_size;
    if (!std::isfinite(step) || step <= 0.0)
    {
        return std::unexpected(ImagePaintPlanError::InvalidSettings);
    }

    auto samples = std::vector<CapturedImagePaintSample>{};
    samples.reserve(std::min<std::size_t>(
        request.sampling_profile.triangles->size() * 8U,
        100'000U));
    const auto append =
        [&](std::size_t triangle_index,
            std::uint32_t uv_island,
            double u,
            double v,
            ImageTriangleAnchor anchor)
            -> std::expected<void, ImagePaintPlanError>
        {
            if (samples.size() >= MaximumAdaptivePaintSamples)
            {
                return std::unexpected(
                    ImagePaintPlanError::ResourceLimit);
            }
            anchor.triangle_index = triangle_index;
            const auto mapped =
                map_image_triangle(request.image_profile, anchor);
            if (!mapped)
            {
                return std::unexpected(
                    ImagePaintPlanError::InvalidProfile);
            }
            samples.push_back(CapturedImagePaintSample{
                region_for(mapped->face),
                static_cast<int>(uv_island),
                std::clamp(u, 0.0, 1.0),
                std::clamp(v, 0.0, 1.0),
                false,
                0.0,
                mapped->v,
                mapped->u,
                anchor,
                true,
            });
            return {};
        };

    for (auto triangle_index = std::size_t{};
         triangle_index <
         request.sampling_profile.triangles->size();
         ++triangle_index)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ImagePaintPlanError::Cancelled);
        }
        const auto& triangle =
            request.sampling_profile.triangles->at(
                triangle_index);
        const auto& first =
            request.sampling_profile.vertices->at(
                triangle.first);
        const auto& second =
            request.sampling_profile.vertices->at(
                triangle.second);
        const auto& third =
            request.sampling_profile.vertices->at(
                triangle.third);
        const auto determinant =
            (second.v - third.v) * (first.u - third.u) +
            (third.u - second.u) * (first.v - third.v);
        if (!std::isfinite(determinant) ||
            std::abs(determinant) <=
                SamplingDenominatorEpsilon)
        {
            return std::unexpected(
                ImagePaintPlanError::InvalidProfile);
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
                    ImagePaintPlanError::Cancelled);
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
        return std::unexpected(ImagePaintPlanError::EmptySamples);
    }
    return samples;
}
} // namespace

auto build_image_paint_plan(
    const ImagePaintPlanRequest& request,
    std::stop_token cancellation)
    -> std::expected<ImagePaintPlan, ImagePaintPlanError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImagePaintPlanError::Cancelled);
    }
    if (!validate(request.raw_profile).empty() ||
        request.raw_profile.role != MeshProfileRole::Raw ||
        !validate(request.image_profile.geometry.identity).empty() ||
        request.image_profile.geometry.identity.role !=
            MeshProfileRole::ImageReference ||
        request.raw_profile.body != request.settings.body ||
        request.image_profile.geometry.identity.body !=
            request.settings.body ||
        request.image_profile.geometry.identity.base_profile_id !=
            request.raw_profile.profile_id ||
        request.image_profile.geometry.identity.base_profile_hash !=
            request.raw_profile.profile_hash)
    {
        return std::unexpected(ImagePaintPlanError::InvalidProfile);
    }
    if (!validate(request.settings).empty())
    {
        return std::unexpected(ImagePaintPlanError::InvalidSettings);
    }
    if (!request.atlas ||
        request.atlas->size() != CanonicalAtlasByteLength)
    {
        return std::unexpected(ImagePaintPlanError::InvalidAtlas);
    }
    if (request.samples.empty())
    {
        return std::unexpected(ImagePaintPlanError::EmptySamples);
    }
    if (request.samples.size() > MaximumAdaptivePaintSamples)
    {
        return std::unexpected(ImagePaintPlanError::ResourceLimit);
    }

    auto output = ImagePaintPlan{};
    output.settings = request.settings;
    auto fill_candidates = std::vector<ReplayCandidate>{};
    auto paint_candidates = std::vector<ReplayCandidate>{};
    auto adaptive_samples = std::vector<AdaptivePaintSample>{};
    auto colors = std::vector<Rgb8>{};
    fill_candidates.reserve(request.samples.size());
    paint_candidates.reserve(request.samples.size());
    adaptive_samples.reserve(request.samples.size());
    colors.reserve(request.samples.size());
    const auto atlas = std::span<const std::byte>{*request.atlas};
    const auto paint_material_key =
        material_key(request.settings.image_material);

    for (auto index = std::size_t{};
         index < request.samples.size();
         ++index)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ImagePaintPlanError::Cancelled);
        }
        const auto& sample = request.samples[index];
        if (!region_valid(sample.paint_region) ||
            sample.uv_island < 0 ||
            !unit(sample.paint_u) || !unit(sample.paint_v) ||
            (sample.has_current_view_position &&
             !std::isfinite(sample.current_view_vertical)) ||
            !std::isfinite(sample.fallback_view_vertical) ||
            !std::isfinite(sample.horizontal))
        {
            return std::unexpected(
                ImagePaintPlanError::InvalidSample);
        }
        const auto mapped = map_image_triangle(
            request.image_profile,
            sample.image_anchor);
        if (!mapped)
        {
            return std::unexpected(
                ImagePaintPlanError::InvalidSample);
        }

        auto color = Rgb8{};
        auto paint_eligible = false;
        if (!sample.safe)
        {
            ++output.unsafe_samples;
        }
        else
        {
            if (mode_for(mapped->face, request.settings) ==
                FaceBaseMode::Fill)
            {
                fill_candidates.push_back(
                    candidate(index, sample, RegionMode::Fill));
                ++output.fill_face_samples;
            }

            const auto pixel =
                atlas_color(atlas, mapped->u, mapped->v);
            color = Rgb8{pixel[0U], pixel[1U], pixel[2U]};
            if (pixel[3U] == ImageBackgroundAlphaMarker)
            {
                ++output.background_marker_samples;
            }
            else if (pixel[3U] < ImageOpaqueAlphaThreshold)
            {
                ++output.transparent_samples;
            }
            else
            {
                paint_eligible = true;
                ++output.opaque_samples;
                paint_candidates.push_back(
                    candidate(index, sample, RegionMode::Paint));
            }
        }
        colors.push_back(color);
        adaptive_samples.push_back(AdaptivePaintSample{
            sample.paint_u,
            sample.paint_v,
            sample.paint_region,
            sample.uv_island,
            static_cast<double>(color.red) / 255.0,
            static_cast<double>(color.green) / 255.0,
            static_cast<double>(color.blue) / 255.0,
            paint_eligible,
            sample.safe,
            paint_material_key,
        });
    }

    const auto texture_size =
        static_cast<int>(request.raw_profile.texture_size);
    const auto fill_replay = build_replay_plan(
        fill_candidates,
        texture_size,
        request.settings.brush_size_texels,
        PaintFillRadiusTexels,
        cancellation);
    if (!fill_replay)
    {
        return std::unexpected(replay_error(fill_replay.error()));
    }
    const auto paint_replay = build_replay_plan(
        paint_candidates,
        texture_size,
        request.settings.brush_size_texels,
        PaintFillRadiusTexels,
        cancellation);
    if (!paint_replay)
    {
        return std::unexpected(replay_error(paint_replay.error()));
    }

    auto replay_entries = fill_replay->entries;
    replay_entries.insert(
        replay_entries.end(),
        paint_replay->entries.begin(),
        paint_replay->entries.end());
    if (replay_entries.size() > MaximumAdaptiveReplayEntries)
    {
        return std::unexpected(ImagePaintPlanError::ResourceLimit);
    }
    const auto texture_dimension =
        static_cast<double>(request.raw_profile.texture_size);
    const auto adaptive = build_adaptive_paint_plan(
        replay_entries,
        adaptive_samples,
        request.settings.brush_size_texels / texture_dimension,
        request.settings.color_compression_tolerance_percent,
        0.5 / texture_dimension,
        cancellation);
    if (!adaptive)
    {
        return std::unexpected(
            adaptive_error(adaptive.error()));
    }

    auto& paint = output.paint;
    paint.texture_dimension = request.raw_profile.texture_size;
    paint.strokes.reserve(adaptive->entries.size());
    for (const auto& entry : adaptive->entries)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ImagePaintPlanError::Cancelled);
        }
        const auto sample_index = entry.replay.sample_index;
        if (sample_index >= request.samples.size() ||
            sample_index >= colors.size())
        {
            return std::unexpected(
                ImagePaintPlanError::InvalidSample);
        }
        const auto& sample = request.samples[sample_index];
        const auto fill = entry.replay.pass == ReplayPass::Fill;
        paint.strokes.push_back(PaintStroke{
            sample_index,
            entry.replay.pass,
            entry.replay.region,
            sample.paint_u,
            sample.paint_v,
            fill
                ? PaintFillRadiusTexels
                : request.settings.brush_size_texels *
                      entry.radius_multiplier,
            fill ? request.settings.fill_color : colors[sample_index],
            fill
                ? request.settings.fill_material
                : request.settings.image_material,
            false,
        });
    }

    paint.fill_end = fill_replay->entries.size();
    paint.fill_count = paint.fill_end;
    paint.paint_count = paint.strokes.size() - paint.fill_end;
    paint.source_paint_count = paint_replay->paint_count;
    paint.compressed_paint_count =
        adaptive->compressed_paint_entries;
    paint.expanded_paint_count =
        adaptive->expanded_paint_entries;
    paint.projection_fallback_count =
        fill_replay->current_view_projection_fallback_candidates +
        paint_replay->current_view_projection_fallback_candidates;
    paint.projection_fallback_used =
        paint.projection_fallback_count > 0U;
    return output;
}

auto build_image_paint_plan_from_profile(
    const ImagePaintProfilePlanRequest& request,
    std::stop_token cancellation)
    -> std::expected<ImagePaintPlan, ImagePaintPlanError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImagePaintPlanError::Cancelled);
    }
    if (!request.atlas ||
        request.atlas->size() != CanonicalAtlasByteLength)
    {
        return std::unexpected(ImagePaintPlanError::InvalidAtlas);
    }
    auto samples = generate_profile_samples(request, cancellation);
    if (!samples)
    {
        return std::unexpected(samples.error());
    }
    auto planned = build_image_paint_plan(
        ImagePaintPlanRequest{
            request.sampling_profile.identity,
            request.image_profile,
            request.settings,
            request.atlas,
            std::move(*samples),
        },
        cancellation);
    if (!planned)
    {
        return std::unexpected(planned.error());
    }
    planned->generated_samples =
        planned->opaque_samples +
        planned->transparent_samples +
        planned->background_marker_samples +
        planned->unsafe_samples;
    return planned;
}
} // namespace meccha::core
