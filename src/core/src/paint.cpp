#include <meccha/core/paint.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <span>
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

auto valid_range(double value, double minimum, double maximum) -> bool
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
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
    double fill_radius_texels) -> ReplayPlan
{
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
            auto pending = std::vector<ReplayEntry>{};
            const auto row_height = std::max(
                0.000001,
                vertical_span * std::max(0.001, row_size_texels) /
                    texture_dimension);
            for (const auto& candidate : candidates)
            {
                if (!include_all_regions &&
                    candidate.mode != required_mode)
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
        };

    append_pass(
        ReplayPass::Fill,
        RegionMode::Fill,
        fill_cell_uv,
        fill_radius_texels,
        fill_all_regions);
    plan.fill_end = plan.entries.size();
    plan.fill_count = plan.fill_end;
    append_pass(
        ReplayPass::Paint,
        RegionMode::Paint,
        0.0,
        brush_size_texels,
        false);
    plan.paint_count = plan.entries.size() - plan.fill_end;
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
