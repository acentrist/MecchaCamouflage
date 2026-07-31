#include <meccha/core/paint.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <span>
#include <stop_token>
#include <tuple>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr int FallbackBatchesPerSecond = 20;
constexpr int FallbackStrokesPerBatch = 20;
constexpr int CapacityNumerator = 4;
constexpr int CapacityDenominator = 5;
constexpr int MaximumBurstCalls = 3;
constexpr int AssumedRemoteFramesPerSecond = 60;
constexpr int QueueObservationWindows = 8;
constexpr int ConfirmationFramesPerSecond = 30;
constexpr std::array ReplayRegionOrder{
    Region::Back,
    Region::Side,
    Region::Front,
};

auto valid_range(double value, double minimum, double maximum) -> bool
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

auto valid_region_mode(RegionMode mode) -> bool
{
    return mode == RegionMode::Paint ||
           mode == RegionMode::Fill ||
           mode == RegionMode::Skip;
}

auto ceil_div_positive(std::int64_t numerator, int denominator) -> int
{
    return denominator > 0 && numerator > 0
               ? static_cast<int>(
                     (numerator + denominator - 1) / denominator)
               : 0;
}

auto scanline_row(
    double top,
    double point,
    double row_height) -> int
{
    if (!std::isfinite(top) || !std::isfinite(point) ||
        !std::isfinite(row_height) || row_height <= 0.000001)
    {
        return 0;
    }
    return static_cast<int>(
        std::floor(std::max(0.0, top - point) / row_height));
}

auto scanline_less(
    const SpatialScanlineKey& left,
    const SpatialScanlineKey& right) -> bool
{
    if (left.row != right.row)
    {
        return left.row < right.row;
    }
    if (left.horizontal != right.horizontal)
    {
        return left.horizontal < right.horizontal;
    }
    return left.original_ordinal < right.original_ordinal;
}

auto replay_region_rank(Region region) -> int
{
    switch (region)
    {
    case Region::Back:
        return 0;
    case Region::Side:
        return 1;
    case Region::Front:
        return 2;
    }
    return 3;
}

auto queue_ready(
    bool observer_available,
    bool observer_observed_activity,
    int visual_pending_strokes,
    bool outgoing_available,
    int outgoing_pending_strokes) -> bool
{
    if (outgoing_available && outgoing_pending_strokes > 0)
    {
        return false;
    }
    const auto authoritative =
        observer_available && observer_observed_activity;
    return !authoritative || visual_pending_strokes <= 0;
}
} // namespace

auto validate(const PaintSettings& settings)
    -> std::vector<PaintSettingField>
{
    auto errors = std::vector<PaintSettingField>{};
    const auto check =
        [&errors](bool valid, PaintSettingField field)
        {
            if (!valid)
            {
                errors.push_back(field);
            }
        };
    check(
        valid_range(settings.brush_size_texels, 1.0, 10.0),
        PaintSettingField::BrushSize);
    check(
        valid_range(settings.side_source_max_uv, 0.001, 0.5),
        PaintSettingField::SideSourceMaximum);
    check(
        valid_range(settings.front_back_source_max_uv, 0.001, 2.0),
        PaintSettingField::FrontBackSourceMaximum);
    check(
        valid_region_mode(settings.front_mode),
        PaintSettingField::FrontMode);
    check(
        valid_region_mode(settings.side_mode),
        PaintSettingField::SideMode);
    check(
        valid_region_mode(settings.back_mode),
        PaintSettingField::BackMode);
    check(
        valid_range(settings.paint_material.metallic, 0.0, 1.0),
        PaintSettingField::PaintMetallic);
    check(
        valid_range(settings.paint_material.roughness, 0.0, 1.0),
        PaintSettingField::PaintRoughness);
    check(
        valid_range(settings.paint_material.emissive, 0.0, 1.0),
        PaintSettingField::PaintEmissive);
    check(
        valid_range(settings.fill_material.metallic, 0.0, 1.0),
        PaintSettingField::FillMetallic);
    check(
        valid_range(settings.fill_material.roughness, 0.0, 1.0),
        PaintSettingField::FillRoughness);
    check(
        valid_range(settings.fill_material.emissive, 0.0, 1.0),
        PaintSettingField::FillEmissive);
    check(
        valid_range(
            settings.color_compression_tolerance_percent,
            0.0,
            10.0),
        PaintSettingField::CompressionTolerance);
    return errors;
}

auto replay_pass_window(
    std::size_t offset,
    std::size_t total,
    std::size_t fill_end) -> ReplayPassWindow
{
    const auto safe_fill_end = std::min(fill_end, total);
    const auto safe_offset = std::min(offset, total);
    if (safe_offset >= total)
    {
        return {ReplayPass::Complete, total, total};
    }
    if (safe_offset < safe_fill_end)
    {
        return {ReplayPass::Fill, 0U, safe_fill_end};
    }
    return {ReplayPass::Paint, safe_fill_end, total};
}

auto build_replay_plan(
    std::span<const ReplayCandidate> candidates,
    int texture_size,
    double brush_size_texels,
    double fill_radius_texels,
    std::stop_token cancellation)
    -> std::expected<ReplayPlan, ReplayPlanError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ReplayPlanError::Cancelled);
    }
    if (candidates.size() > MaximumAdaptivePaintSamples)
    {
        return std::unexpected(ReplayPlanError::ResourceLimit);
    }
    if (texture_size <= 0 || texture_size > 4096 ||
        !valid_range(brush_size_texels, 1.0, 10.0) ||
        !valid_range(
            fill_radius_texels,
            0.001,
            static_cast<double>(texture_size)))
    {
        return std::unexpected(ReplayPlanError::InvalidArgument);
    }
    const auto unit =
        [](double value)
        {
            return std::isfinite(value) &&
                   value >= 0.0 && value <= 1.0;
        };
    for (const auto& candidate : candidates)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ReplayPlanError::Cancelled);
        }
        if ((candidate.region != Region::Front &&
             candidate.region != Region::Side &&
             candidate.region != Region::Back) ||
            (candidate.mode != RegionMode::Paint &&
             candidate.mode != RegionMode::Fill &&
             candidate.mode != RegionMode::Skip) ||
            candidate.uv_island < 0 ||
            !unit(candidate.u) || !unit(candidate.v) ||
            (candidate.has_current_view_position &&
             !std::isfinite(candidate.current_view_vertical)) ||
            !std::isfinite(candidate.fallback_view_vertical) ||
            !std::isfinite(candidate.horizontal))
        {
            return std::unexpected(ReplayPlanError::InvalidCandidate);
        }
    }

    ReplayPlan plan{};
    const auto texture_dimension =
        static_cast<double>(std::max(1, texture_size));
    const auto fill_cell_uv =
        fill_radius_texels * 0.75 / texture_dimension;
    const auto fill_all_regions = std::ranges::any_of(
        candidates,
        [](const auto& candidate)
        {
            return candidate.mode == RegionMode::Fill;
        });

    auto vertical_top = 0.0;
    auto vertical_bottom = 0.0;
    auto have_vertical_bounds = false;
    const auto selected_vertical =
        [](const ReplayCandidate& candidate)
        {
            if (candidate.has_current_view_position &&
                std::isfinite(candidate.current_view_vertical))
            {
                return candidate.current_view_vertical;
            }
            return std::isfinite(candidate.fallback_view_vertical)
                       ? candidate.fallback_view_vertical
                       : 0.0;
        };
    for (const auto& candidate : candidates)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ReplayPlanError::Cancelled);
        }
        if (!fill_all_regions && candidate.mode == RegionMode::Skip)
        {
            continue;
        }
        const auto vertical = selected_vertical(candidate);
        if (!have_vertical_bounds)
        {
            vertical_top = vertical;
            vertical_bottom = vertical;
            have_vertical_bounds = true;
        }
        else
        {
            vertical_top = std::max(vertical_top, vertical);
            vertical_bottom = std::min(vertical_bottom, vertical);
        }
        if (!candidate.has_current_view_position ||
            !std::isfinite(candidate.current_view_vertical))
        {
            ++plan.current_view_projection_fallback_candidates;
        }
    }
    plan.current_view_projection_fallback_used =
        plan.current_view_projection_fallback_candidates > 0U;
    const auto vertical_span =
        std::max(0.001, vertical_top - vertical_bottom);

    const auto append_pass =
        [&](ReplayPass pass,
            RegionMode required_mode,
            double dedupe_cell_uv,
            double row_size_texels,
            bool include_all_regions)
        {
            auto emitted_cells =
                std::set<std::tuple<int, int, int, int>>{};
            const auto row_height = std::max(
                0.000001,
                vertical_span * std::max(0.001, row_size_texels) /
                    texture_dimension);
            for (const auto region : ReplayRegionOrder)
            {
                auto pending = std::vector<ReplayEntry>{};
                for (const auto& candidate : candidates)
                {
                    if (cancellation.stop_requested())
                    {
                        return false;
                    }
                    if (candidate.region != region ||
                        (!include_all_regions &&
                         candidate.mode != required_mode))
                    {
                        continue;
                    }
                    auto& candidate_count =
                        pass == ReplayPass::Fill
                            ? plan.fill_candidates
                            : plan.paint_candidates;
                    ++candidate_count;

                    if (dedupe_cell_uv > 0.000001)
                    {
                        const auto cell_coordinate =
                            [dedupe_cell_uv](double value)
                            {
                                const auto finite =
                                    std::isfinite(value) ? value : 0.0;
                                return static_cast<int>(std::floor(
                                    std::clamp(finite, 0.0, 1.0) /
                                    dedupe_cell_uv));
                            };
                        const auto cell = std::make_tuple(
                            static_cast<int>(candidate.region),
                            candidate.uv_island,
                            cell_coordinate(candidate.u),
                            cell_coordinate(candidate.v));
                        if (!emitted_cells.insert(cell).second)
                        {
                            auto& deduplicated =
                                pass == ReplayPass::Fill
                                    ? plan.fill_deduplicated
                                    : plan.paint_deduplicated;
                            ++deduplicated;
                            continue;
                        }
                    }
                    pending.push_back(ReplayEntry{
                        candidate.sample_index,
                        pass,
                        candidate.region,
                        SpatialScanlineKey{
                            scanline_row(
                                vertical_top,
                                selected_vertical(candidate),
                                row_height),
                            candidate.horizontal,
                            candidate.original_ordinal,
                        },
                    });
                }
                std::ranges::stable_sort(
                    pending,
                    [](const auto& left, const auto& right)
                    {
                        return scanline_less(
                            left.spatial_key,
                            right.spatial_key);
                    });
                plan.entries.insert(
                    plan.entries.end(),
                    pending.begin(),
                    pending.end());
            }
            return true;
        };

    if (!append_pass(
            ReplayPass::Fill,
            RegionMode::Fill,
            fill_cell_uv,
            fill_radius_texels,
            fill_all_regions))
    {
        return std::unexpected(ReplayPlanError::Cancelled);
    }
    plan.fill_end = plan.entries.size();
    plan.fill_count = plan.fill_end;
    if (!append_pass(
            ReplayPass::Paint,
            RegionMode::Paint,
            0.0,
            brush_size_texels,
            false))
    {
        return std::unexpected(ReplayPlanError::Cancelled);
    }
    plan.paint_count = plan.entries.size() - plan.fill_end;
    return plan;
}

auto build_adaptive_paint_plan(
    std::span<const ReplayEntry> replay_entries,
    std::span<const AdaptivePaintSample> samples,
    double base_radius_uv,
    double tolerance_percent,
    double edge_margin_uv,
    std::stop_token cancellation)
    -> std::expected<AdaptivePaintPlan, AdaptivePaintPlanError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            AdaptivePaintPlanError::Cancelled);
    }
    if (!std::isfinite(base_radius_uv) ||
        !std::isfinite(tolerance_percent) ||
        !std::isfinite(edge_margin_uv) ||
        base_radius_uv < 0.0 || tolerance_percent < 0.0 ||
        tolerance_percent > 10.0 || edge_margin_uv < 0.0)
    {
        return std::unexpected(
            AdaptivePaintPlanError::InvalidArgument);
    }
    if (samples.size() > MaximumAdaptivePaintSamples ||
        replay_entries.size() > MaximumAdaptiveReplayEntries)
    {
        return std::unexpected(
            AdaptivePaintPlanError::ResourceLimit);
    }

    const auto unit =
        [](double value)
        {
            return std::isfinite(value) &&
                   value >= 0.0 && value <= 1.0;
        };
    for (const auto& sample : samples)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                AdaptivePaintPlanError::Cancelled);
        }
        if (!unit(sample.u) || !unit(sample.v) ||
            !unit(sample.red) || !unit(sample.green) ||
            !unit(sample.blue))
        {
            return std::unexpected(
                AdaptivePaintPlanError::InvalidSample);
        }
    }
    for (const auto& entry : replay_entries)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                AdaptivePaintPlanError::Cancelled);
        }
        if (entry.sample_index >= samples.size())
        {
            return std::unexpected(
                AdaptivePaintPlanError::InvalidReplayEntry);
        }
    }

    auto plan = AdaptivePaintPlan{};
    plan.entries.reserve(replay_entries.size());
    if (replay_entries.empty())
    {
        return plan;
    }
    if (tolerance_percent == 0.0 ||
        base_radius_uv <= 0.000001 || samples.empty())
    {
        for (const auto& entry : replay_entries)
        {
            if (cancellation.stop_requested())
            {
                return std::unexpected(
                    AdaptivePaintPlanError::Cancelled);
            }
            plan.entries.push_back({entry, 1.0});
        }
        return plan;
    }

    auto grid_size = 128;
    if (samples.size() > 200'000U)
    {
        grid_size = 256;
    }
    if (samples.size() > 500'000U)
    {
        grid_size = 512;
    }
    auto grid = std::vector<std::vector<std::size_t>>(
        static_cast<std::size_t>(grid_size) *
        static_cast<std::size_t>(grid_size));
    const auto cell_coordinate =
        [grid_size](double value)
        {
            return std::clamp(
                static_cast<int>(
                    std::floor(value * grid_size)),
                0,
                grid_size - 1);
        };
    for (auto index = std::size_t{}; index < samples.size(); ++index)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                AdaptivePaintPlanError::Cancelled);
        }
        const auto& sample = samples[index];
        const auto cell = static_cast<std::size_t>(
            cell_coordinate(sample.v) * grid_size +
            cell_coordinate(sample.u));
        grid[cell].push_back(index);
    }

    const auto threshold = tolerance_percent / 100.0;
    const auto threshold_squared = threshold * threshold;
    const auto same_payload =
        [](const AdaptivePaintSample& center,
           const AdaptivePaintSample& other)
        {
            return center.paint_eligible && center.safe &&
                   other.paint_eligible && other.safe &&
                   center.region == other.region &&
                   center.uv_island == other.uv_island &&
                   center.material_key == other.material_key;
        };
    const auto color_distance_squared =
        [](const AdaptivePaintSample& left,
           const AdaptivePaintSample& right)
        {
            const auto red = left.red - right.red;
            const auto green = left.green - right.green;
            const auto blue = left.blue - right.blue;
            return std::max({
                red * red,
                green * green,
                blue * blue,
            });
        };
    const auto visit_nearby =
        [&](const AdaptivePaintSample& center,
            double radius_uv,
            const auto& visit)
        {
            const auto radius = std::max(0.0, radius_uv);
            const auto radius_squared = radius * radius;
            const auto minimum_u =
                cell_coordinate(center.u - radius);
            const auto maximum_u =
                cell_coordinate(center.u + radius);
            const auto minimum_v =
                cell_coordinate(center.v - radius);
            const auto maximum_v =
                cell_coordinate(center.v + radius);
            for (auto cell_v = minimum_v;
                 cell_v <= maximum_v;
                 ++cell_v)
            {
                for (auto cell_u = minimum_u;
                     cell_u <= maximum_u;
                     ++cell_u)
                {
                    const auto cell = static_cast<std::size_t>(
                        cell_v * grid_size + cell_u);
                    for (const auto other_index : grid[cell])
                    {
                        const auto& other = samples[other_index];
                        const auto delta_u = other.u - center.u;
                        const auto delta_v = other.v - center.v;
                        if (delta_u * delta_u +
                                delta_v * delta_v <=
                            radius_squared)
                        {
                            visit(other_index, other);
                        }
                    }
                }
            }
        };

    constexpr std::array RadiusMultipliers{
        8.0,
        6.0,
        4.0,
        3.0,
        2.0,
        1.5,
    };
    auto candidate_multipliers =
        std::vector<double>(replay_entries.size(), 1.0);
    for (auto index = std::size_t{};
         index < replay_entries.size();
         ++index)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                AdaptivePaintPlanError::Cancelled);
        }
        const auto& entry = replay_entries[index];
        if (entry.pass != ReplayPass::Paint)
        {
            continue;
        }
        const auto& center = samples[entry.sample_index];
        if (!center.paint_eligible || !center.safe)
        {
            continue;
        }
        for (const auto multiplier : RadiusMultipliers)
        {
            const auto check_radius = std::max(
                0.0,
                multiplier * base_radius_uv - edge_margin_uv);
            auto valid = true;
            visit_nearby(
                center,
                check_radius,
                [&](std::size_t,
                    const AdaptivePaintSample& other)
                {
                    if (cancellation.stop_requested())
                    {
                        valid = false;
                        return;
                    }
                    if (!same_payload(center, other) ||
                        color_distance_squared(center, other) >
                            threshold_squared)
                    {
                        valid = false;
                    }
                });
            if (valid)
            {
                candidate_multipliers[index] = multiplier;
                break;
            }
            if (cancellation.stop_requested())
            {
                return std::unexpected(
                    AdaptivePaintPlanError::Cancelled);
            }
        }
    }

    auto covered = std::vector<bool>(samples.size(), false);
    auto paint_entries = std::vector<AdaptiveReplayEntry>{};
    paint_entries.reserve(replay_entries.size());
    for (auto index = std::size_t{};
         index < replay_entries.size();
         ++index)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                AdaptivePaintPlanError::Cancelled);
        }
        const auto& entry = replay_entries[index];
        if (entry.pass != ReplayPass::Paint)
        {
            plan.entries.push_back({entry, 1.0});
            continue;
        }
        if (covered[entry.sample_index])
        {
            ++plan.compressed_paint_entries;
            continue;
        }

        const auto multiplier = candidate_multipliers[index];
        const auto& center = samples[entry.sample_index];
        paint_entries.push_back({entry, multiplier});
        if (multiplier > 1.0)
        {
            ++plan.expanded_paint_entries;
        }
        covered[entry.sample_index] = true;
        const auto coverage_radius = std::max(
            0.0,
            multiplier * base_radius_uv - edge_margin_uv);
        visit_nearby(
            center,
            coverage_radius,
            [&](std::size_t other_index,
                const AdaptivePaintSample& other)
            {
                if (same_payload(center, other) &&
                    color_distance_squared(center, other) <=
                        threshold_squared)
                {
                    covered[other_index] = true;
                }
            });
        if (cancellation.stop_requested())
        {
            return std::unexpected(
                AdaptivePaintPlanError::Cancelled);
        }
    }
    std::ranges::stable_sort(
        paint_entries,
        [](const auto& left, const auto& right)
        {
            const auto left_region =
                replay_region_rank(left.replay.region);
            const auto right_region =
                replay_region_rank(right.replay.region);
            if (left_region != right_region)
            {
                return left_region < right_region;
            }
            return left.radius_multiplier >
                   right.radius_multiplier;
        });
    plan.entries.insert(
        plan.entries.end(),
        paint_entries.begin(),
        paint_entries.end());
    return plan;
}

auto replication_pacing_plan(const ReplicationPacingInput& input)
    -> ReplicationPacingPlan
{
    const auto batches =
        input.batches_per_second > 0 &&
                input.batches_per_second <= 240
            ? input.batches_per_second
            : FallbackBatchesPerSecond;
    const auto strokes_per_batch =
        input.strokes_per_batch > 0 &&
                input.strokes_per_batch <= 4096
            ? input.strokes_per_batch
            : FallbackStrokesPerBatch;
    const auto network_strokes_per_window = std::max(
        1,
        strokes_per_batch * CapacityNumerator /
            CapacityDenominator);
    const auto network_strokes_per_second = std::max(
        1,
        network_strokes_per_window * batches);
    const auto minimum_remote_frames =
        std::max(1, input.min_remote_frames_after_local_paint);
    const auto adaptive_frames = std::max(
        0,
        input.max_adaptive_remote_frame_interval);
    const auto confirmation_frames =
        input.adaptive_remote_interval
            ? std::max(minimum_remote_frames, adaptive_frames)
            : minimum_remote_frames;
    const auto writes_per_frame =
        input.render_target_writes_per_frame > 0 &&
                input.render_target_writes_per_frame <= 4096
            ? input.render_target_writes_per_frame
            : 0;
    const auto reported_receiver =
        writes_per_frame > 0
            ? static_cast<int>(
                  static_cast<std::int64_t>(writes_per_frame) *
                  AssumedRemoteFramesPerSecond /
                  confirmation_frames)
            : network_strokes_per_second;
    const auto receiver_strokes_per_second =
        writes_per_frame > 0
            ? std::max(
                  1,
                  reported_receiver * CapacityNumerator /
                      CapacityDenominator)
            : network_strokes_per_second;
    const auto effective = std::min(
        network_strokes_per_second,
        receiver_strokes_per_second);
    const auto safe_max_calls =
        std::max(1, input.max_calls_per_tick);
    const auto burst_calls =
        std::min(safe_max_calls, MaximumBurstCalls);
    const auto calls_per_tick =
        std::min(burst_calls, network_strokes_per_window);
    const auto network_window_ms =
        std::max(1, ceil_div_positive(1000, batches));
    const auto receiver_strokes_per_window =
        ceil_div_positive(effective, batches);
    const auto strokes_per_window = std::min(
        network_strokes_per_window,
        std::max(calls_per_tick, receiver_strokes_per_window));
    const auto cadence_ms = std::max(
        1,
        ceil_div_positive(
            static_cast<std::int64_t>(calls_per_tick) * 1000,
            effective));
    const auto final_confirmation_ms =
        network_window_ms * QueueObservationWindows +
        ceil_div_positive(
            static_cast<std::int64_t>(confirmation_frames) * 1000,
            ConfirmationFramesPerSecond);
    return {
        batches,
        strokes_per_batch,
        network_strokes_per_second,
        receiver_strokes_per_second,
        effective,
        strokes_per_window,
        calls_per_tick,
        network_window_ms,
        cadence_ms,
        final_confirmation_ms,
    };
}

auto visual_drain_complete(
    bool observer_available,
    bool observer_observed_activity,
    int visual_pending_strokes,
    bool outgoing_available,
    int outgoing_pending_strokes,
    int final_elapsed_ms,
    int final_confirmation_ms) -> bool
{
    return queue_ready(
               observer_available,
               observer_observed_activity,
               visual_pending_strokes,
               outgoing_available,
               outgoing_pending_strokes) &&
           final_elapsed_ms >= std::max(0, final_confirmation_ms);
}
} // namespace meccha::core
