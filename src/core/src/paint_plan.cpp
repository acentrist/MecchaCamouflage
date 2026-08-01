#include <meccha/core/paint_plan.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto material_valid(const Material& material) -> bool
{
    return unit(material.metallic) &&
           unit(material.roughness) &&
           unit(material.emissive);
}

auto region_valid(Region region) -> bool
{
    return region == Region::Front ||
           region == Region::Side ||
           region == Region::Back;
}

auto mode_for(Region region, const PaintSettings& settings)
    -> RegionMode
{
    switch (region)
    {
    case Region::Front:
        return settings.front_mode;
    case Region::Side:
        return settings.side_mode;
    case Region::Back:
        return settings.back_mode;
    }
    return RegionMode::Skip;
}

auto material_key(const Material& material) -> std::uint64_t
{
    auto hash = std::uint64_t{1469598103934665603ULL};
    const auto mix =
        [&hash](double value)
        {
            const auto quantized = static_cast<std::uint8_t>(
                std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
            hash ^= quantized;
            hash *= 1099511628211ULL;
        };
    mix(material.metallic);
    mix(material.roughness);
    mix(material.emissive);
    return hash;
}

auto color_from_unit(double red, double green, double blue) -> Rgb8
{
    const auto channel =
        [](double value)
        {
            return static_cast<std::uint8_t>(std::lround(
                std::clamp(value, 0.0, 1.0) * 255.0));
        };
    return {channel(red), channel(green), channel(blue)};
}

auto map_replay_error(ReplayPlanError error) -> PaintPlanError
{
    switch (error)
    {
    case ReplayPlanError::ResourceLimit:
        return PaintPlanError::ResourceLimit;
    case ReplayPlanError::Cancelled:
        return PaintPlanError::Cancelled;
    case ReplayPlanError::InvalidArgument:
        return PaintPlanError::InvalidSettings;
    case ReplayPlanError::InvalidCandidate:
        return PaintPlanError::InvalidSample;
    }
    return PaintPlanError::InvalidSample;
}

auto map_adaptive_error(AdaptivePaintPlanError error)
    -> PaintPlanError
{
    switch (error)
    {
    case AdaptivePaintPlanError::ResourceLimit:
        return PaintPlanError::ResourceLimit;
    case AdaptivePaintPlanError::Cancelled:
        return PaintPlanError::Cancelled;
    case AdaptivePaintPlanError::InvalidArgument:
        return PaintPlanError::InvalidSettings;
    case AdaptivePaintPlanError::InvalidSample:
    case AdaptivePaintPlanError::InvalidReplayEntry:
        return PaintPlanError::InvalidSample;
    }
    return PaintPlanError::InvalidSample;
}
} // namespace

auto build_paint_plan(
    const PaintPlanRequest& request,
    std::stop_token cancellation)
    -> std::expected<PaintPlan, PaintPlanError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(PaintPlanError::Cancelled);
    }
    if (!validate(request.profile).empty() ||
        request.profile.role != MeshProfileRole::Raw)
    {
        return std::unexpected(PaintPlanError::InvalidProfile);
    }
    if (!validate(request.settings).empty())
    {
        return std::unexpected(PaintPlanError::InvalidSettings);
    }
    if (request.samples.size() > MaximumAdaptivePaintSamples)
    {
        return std::unexpected(PaintPlanError::ResourceLimit);
    }

    auto candidates = std::vector<ReplayCandidate>{};
    auto adaptive_samples = std::vector<AdaptivePaintSample>{};
    auto appearances = std::vector<ResolvedPaintAppearance>{};
    candidates.reserve(request.samples.size());
    adaptive_samples.reserve(request.samples.size());
    appearances.reserve(request.samples.size());

    for (auto index = std::size_t{};
         index < request.samples.size();
         ++index)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(PaintPlanError::Cancelled);
        }
        const auto& sample = request.samples[index];
        if (!region_valid(sample.region) ||
            sample.uv_island < 0 ||
            !unit(sample.u) || !unit(sample.v) ||
            (sample.has_current_view_position &&
             !std::isfinite(sample.current_view_vertical)) ||
            !std::isfinite(sample.fallback_view_vertical) ||
            !std::isfinite(sample.horizontal) ||
            (sample.projected_appearance_available &&
             !material_valid(
                 sample.projected_appearance.material)))
        {
            return std::unexpected(PaintPlanError::InvalidSample);
        }

        const auto mode =
            mode_for(sample.region, request.settings);
        if (sample.safe)
        {
            candidates.push_back(ReplayCandidate{
                index,
                sample.region,
                mode,
                sample.uv_island,
                sample.u,
                sample.v,
                sample.has_current_view_position,
                sample.current_view_vertical,
                sample.fallback_view_vertical,
                sample.horizontal,
                index,
            });
        }

        if (sample.safe && mode == RegionMode::Paint &&
            !sample.projected_appearance_available)
        {
            return std::unexpected(
                PaintPlanError::MissingProjectedAppearance);
        }
        const auto appearance = sample.projected_appearance_available
                                    ? sample.projected_appearance
                                    : ResolvedPaintAppearance{};
        appearances.push_back(appearance);
        adaptive_samples.push_back(AdaptivePaintSample{
            sample.u,
            sample.v,
            sample.region,
            sample.uv_island,
            static_cast<double>(appearance.color.red) / 255.0,
            static_cast<double>(appearance.color.green) / 255.0,
            static_cast<double>(appearance.color.blue) / 255.0,
            mode == RegionMode::Paint,
            sample.safe,
            material_key(appearance.material),
        });
    }

    const auto replay = build_replay_plan(
        candidates,
        static_cast<int>(request.profile.texture_size),
        request.settings.brush_size_texels,
        PaintFillRadiusTexels,
        cancellation);
    if (!replay)
    {
        return std::unexpected(map_replay_error(replay.error()));
    }
    const auto texture_size =
        static_cast<double>(request.profile.texture_size);
    const auto adaptive = build_adaptive_paint_plan(
        replay->entries,
        adaptive_samples,
        request.settings.brush_size_texels / texture_size,
        request.settings.color_compression_tolerance_percent,
        0.5 / texture_size,
        cancellation);
    if (!adaptive)
    {
        return std::unexpected(
            map_adaptive_error(adaptive.error()));
    }

    auto plan = PaintPlan{};
    plan.texture_dimension = request.profile.texture_size;
    plan.strokes.reserve(adaptive->entries.size());
    for (const auto& entry : adaptive->entries)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(PaintPlanError::Cancelled);
        }
        const auto sample_index = entry.replay.sample_index;
        if (sample_index >= request.samples.size() ||
            sample_index >= appearances.size())
        {
            return std::unexpected(PaintPlanError::InvalidSample);
        }
        const auto& sample = request.samples[sample_index];
        const auto fill = entry.replay.pass == ReplayPass::Fill;
        plan.strokes.push_back(PaintStroke{
            sample_index,
            entry.replay.pass,
            entry.replay.region,
            sample.u,
            sample.v,
            fill
                ? PaintFillRadiusTexels
                : request.settings.brush_size_texels *
                      entry.radius_multiplier,
            fill
                ? request.settings.fill_color
                : entry.has_color_override
                      ? color_from_unit(
                            entry.red,
                            entry.green,
                            entry.blue)
                      : appearances[sample_index].color,
            fill
                ? request.settings.fill_material
                : appearances[sample_index].material,
        });
    }

    plan.fill_end = std::ranges::count_if(
        plan.strokes,
        [](const PaintStroke& stroke)
        {
            return stroke.pass == ReplayPass::Fill;
        });
    plan.fill_count = plan.fill_end;
    plan.paint_count = plan.strokes.size() - plan.fill_end;
    plan.source_paint_count = replay->paint_count;
    plan.compressed_paint_count =
        adaptive->compressed_paint_entries;
    plan.expanded_paint_count =
        adaptive->expanded_paint_entries;
    plan.projection_fallback_used =
        replay->current_view_projection_fallback_used;
    plan.projection_fallback_count =
        replay->current_view_projection_fallback_candidates;
    return plan;
}
} // namespace meccha::core
