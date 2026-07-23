#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <set>
#include <tuple>
#include <vector>

namespace runtime_contract
{
    // Supported Shipping build: FProperty ArrayDim@0x30,
    // ElementSize@0x34, PropertyFlags@0x38.
    constexpr std::size_t FPropertyElementSizeOffset = 0x34;
    // Keep direct dispatch bounded so one scheduler tick cannot monopolize the
    // game thread. This is a CPU safety limit, not a network pacing setting.
    // UNVALIDATED: raised 6 -> 12 for throughput. Safe only when paint stays
    // local (replication happens through a single end-of-job server flush, not
    // a per-stroke RPC); the LocalDispatchCpuBudgetUs slice still caps a tick.
    constexpr int NativeRecordedPaintMaxCallsPerTick = 12;
    // Permit at most a small, game-owned recorded-paint lead. Waiting for zero
    // serializes every stroke; an unbounded lead recreates the visible dotted
    // frontier on joining clients.
    // UNVALIDATED: raised 4 -> 8 to let bursts run before draining.
    constexpr int NativeRecordedPaintQueueTargetStrokes = 8;
    constexpr int FastLocalCadenceMs = 1;
    constexpr std::uint64_t LocalDispatchCpuBudgetUs = 4'000;

    // UE 5.6 packs Metallic, Roughness, and Emissive into one material-properties
    // render target (R/G/B).  Channel 7 updates that target atomically.  Splitting
    // a sample into channels 5 and 6 doubles the material-properties work and allowed the
    // separate local replication route to render a second visible pass.
    constexpr std::array<std::uint8_t, 1> ProductionMaterialPaintChannels{7};

    constexpr std::size_t production_material_stroke_count(std::size_t sample_count)
    {
        return sample_count * ProductionMaterialPaintChannels.size();
    }

    constexpr std::size_t production_material_sample_index(std::size_t stroke_index)
    {
        return stroke_index / ProductionMaterialPaintChannels.size();
    }

    // Direct paint delegates mesh-radius and subdivision interpretation to the
    // game. These sentinel values preserve that native behavior for anchored
    // strokes without carrying a second transport-specific contract.
    constexpr float GamePaintMeshAnchorWorldRadiusAuto = 0.0f;
    constexpr int GamePaintMeshAnchorSubdivisionLevelAuto = 0;
    constexpr float GamePaintMeshAnchorSubdivisionPixelSizeAuto = 0.0f;
    constexpr int GamePaintMeshAnchorTemplateResolutionAuto = 0;

    // UObject flags are checked against Shipping disassembly for the supported
    // build.  0x20000000 is intentionally not rejected: it is not an object
    // destruction flag and treating it as one spuriously blocks live objects.
    constexpr std::uint32_t RFClassDefaultObject = 0x00000010u;
    constexpr std::uint32_t RFBeginDestroyed = 0x00008000u;
    constexpr std::uint32_t RFFinishDestroyed = 0x00010000u;
    constexpr std::uint32_t RFMirroredGarbage = 0x40000000u;
    constexpr std::uint32_t ObjectRejectMask =
        RFClassDefaultObject | RFBeginDestroyed | RFFinishDestroyed | RFMirroredGarbage;
    constexpr std::uint32_t ClassRejectMask = RFBeginDestroyed | RFFinishDestroyed | RFMirroredGarbage;

    constexpr bool uobject_flags_usable(std::uint32_t object_flags, std::uint32_t class_flags)
    {
        return (object_flags & ObjectRejectMask) == 0 && (class_flags & ClassRejectMask) == 0;
    }

    constexpr int min_value(int left, int right)
    {
        return left < right ? left : right;
    }

    constexpr int max_value(int left, int right)
    {
        return left > right ? left : right;
    }

    constexpr int clamp_value(int value, int minimum, int maximum)
    {
        return min_value(max_value(value, minimum), maximum);
    }

    // EPaintChannel: 0..3 address one render target, All addresses four, and
    // AlbedoMetallicRoughness addresses three. UE 5.6's AMRE (7) uses the
    // one material-properties target. The game limit is expressed in
    // render-target writes, not paint-stroke calls.
    constexpr int paint_channel_write_cost(int target_channel)
    {
        return target_channel == 4 ? 4 : (target_channel == 5 ? 3 : 1);
    }

    constexpr bool local_dispatch_can_append(int processed_calls,
                                             int scheduled_writes,
                                             int next_write_cost,
                                             int max_calls,
                                             int max_render_target_writes)
    {
        if (processed_calls <= 0)
        {
            return true;
        }
        return processed_calls < max_value(1, max_calls) &&
               scheduled_writes + max_value(1, next_write_cost) <=
                   max_value(1, max_render_target_writes);
    }

    constexpr bool local_dispatch_cpu_budget_reached(int processed_calls,
                                                     std::uint64_t elapsed_us)
    {
        return processed_calls > 0 && elapsed_us >= LocalDispatchCpuBudgetUs;
    }

    constexpr int recurring_scheduler_delay_ms(int requested_delay_ms)
    {
        return max_value(1, requested_delay_ms);
    }

    struct SpatialScanlineKey
    {
        int row;
        double horizontal;
        std::size_t original_ordinal;
    };

    inline int spatial_scanline_row(double top_z, double point_z, double row_height)
    {
        if (!std::isfinite(top_z) || !std::isfinite(point_z) ||
            !std::isfinite(row_height) || row_height <= 0.000001)
        {
            return 0;
        }
        return static_cast<int>(std::floor(std::max(0.0, top_z - point_z) / row_height));
    }

    inline bool spatial_scanline_less(const SpatialScanlineKey& left,
                                      const SpatialScanlineKey& right)
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

    enum class ReplayRegion
    {
        Front,
        Side,
        Back,
    };

    enum class ReplayRegionMode
    {
        Paint,
        Fill,
        Skip,
    };

    enum class ReplayPass
    {
        Fill,
        Paint,
        Complete,
    };

    struct ReplayPassWindow
    {
        ReplayPass pass;
        std::size_t begin;
        std::size_t end;
    };

    // Resolve the pass containing an offset in the effective replay stream.  The
    // planner stores exclusive boundaries, so an offset exactly at a boundary is
    // reported as the next pass.  Clamp malformed boundaries here so diagnostics
    // remain safe even when a runtime limit truncates the planned stream.
    constexpr ReplayPassWindow replay_pass_window(std::size_t offset,
                                                  std::size_t total,
                                                  std::size_t fill_end)
    {
        const std::size_t safe_fill_end = std::min(fill_end, total);
        const std::size_t safe_offset = std::min(offset, total);
        if (safe_offset >= total)
        {
            return {ReplayPass::Complete, total, total};
        }
        if (safe_offset < safe_fill_end)
        {
            return {ReplayPass::Fill, 0, safe_fill_end};
        }
        return {ReplayPass::Paint, safe_fill_end, total};
    }

    struct ReplayCandidate
    {
        std::size_t sample_index;
        ReplayRegion region;
        ReplayRegionMode mode;
        int uv_island;
        double u;
        double v;
        bool has_current_view_position;
        double current_view_vertical;
        double fallback_view_vertical;
        double horizontal;
        std::size_t original_ordinal;
    };

    struct ReplayEntry
    {
        std::size_t sample_index;
        ReplayPass pass;
        ReplayRegion region;
        SpatialScanlineKey spatial_key;
    };

    struct ReplayPlan
    {
        std::vector<ReplayEntry> entries{};
        std::size_t fill_end{0};
        std::size_t fill_count{0};
        std::size_t paint_count{0};
        std::size_t fill_candidates{0};
        std::size_t fill_deduplicated{0};
        std::size_t paint_candidates{0};
        std::size_t paint_deduplicated{0};
        bool current_view_projection_fallback_used{false};
        std::size_t current_view_projection_fallback_candidates{0};
    };

    inline ReplayPlan build_single_brush_replay_plan(
        const std::vector<ReplayCandidate>& candidates,
        int texture_size,
        double brush_size_texels,
        double fill_radius_texels)
    {
        ReplayPlan plan{};
        const double texture_size_double = static_cast<double>(max_value(1, texture_size));
        const double fill_cell_uv = fill_radius_texels * 0.75 / texture_size_double;
        bool fill_all_regions = false;
        for (const auto& candidate : candidates)
        {
            if (candidate.mode == ReplayRegionMode::Fill)
            {
                fill_all_regions = true;
                break;
            }
        }
        double vertical_top = 0.0;
        double vertical_bottom = 0.0;
        bool have_vertical_bounds = false;
        const auto selected_vertical = [](const ReplayCandidate& candidate) {
            return candidate.has_current_view_position && std::isfinite(candidate.current_view_vertical)
                       ? candidate.current_view_vertical
                       : (std::isfinite(candidate.fallback_view_vertical)
                              ? candidate.fallback_view_vertical
                              : 0.0);
        };
        for (const auto& candidate : candidates)
        {
            if (!fill_all_regions && candidate.mode == ReplayRegionMode::Skip)
            {
                continue;
            }
            const double vertical = selected_vertical(candidate);
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
            if (!candidate.has_current_view_position || !std::isfinite(candidate.current_view_vertical))
            {
                ++plan.current_view_projection_fallback_candidates;
            }
        }
        plan.current_view_projection_fallback_used =
            plan.current_view_projection_fallback_candidates > 0;
        const double vertical_span = std::max(0.001, vertical_top - vertical_bottom);
        auto append_pass = [&](ReplayPass pass,
                               ReplayRegionMode required_mode,
                               double dedupe_cell_uv,
                               double row_size_texels,
                               bool include_all_regions = false) {
            std::set<std::tuple<int, int, int, int>> emitted_cells{};
            std::vector<ReplayEntry> pending{};
            const double row_height = std::max(
                0.000001,
                vertical_span * std::max(0.001, row_size_texels) / texture_size_double);
            for (const auto& candidate : candidates)
            {
                if (!include_all_regions && candidate.mode != required_mode)
                {
                    continue;
                }
                if (pass == ReplayPass::Fill)
                {
                    ++plan.fill_candidates;
                }
                else if (pass == ReplayPass::Paint)
                {
                    ++plan.paint_candidates;
                }
                if (dedupe_cell_uv > 0.000001)
                {
                    const auto cell_coordinate = [&](double value) {
                        const double finite_value = std::isfinite(value) ? value : 0.0;
                        return static_cast<int>(std::floor(
                            std::max(0.0, std::min(1.0, finite_value)) / dedupe_cell_uv));
                    };
                    const auto cell = std::make_tuple(
                        static_cast<int>(candidate.region),
                        candidate.uv_island,
                        cell_coordinate(candidate.u),
                        cell_coordinate(candidate.v));
                    if (!emitted_cells.insert(cell).second)
                    {
                        if (pass == ReplayPass::Fill)
                        {
                            ++plan.fill_deduplicated;
                        }
                        else if (pass == ReplayPass::Paint)
                        {
                            ++plan.paint_deduplicated;
                        }
                        continue;
                    }
                }
                pending.push_back(
                    {candidate.sample_index,
                     pass,
                     candidate.region,
                     {spatial_scanline_row(vertical_top,
                                           selected_vertical(candidate),
                                           row_height),
                      candidate.horizontal,
                      candidate.original_ordinal}});
            }
            std::stable_sort(pending.begin(), pending.end(), [](const auto& left, const auto& right) {
                return spatial_scanline_less(left.spatial_key, right.spatial_key);
            });
            plan.entries.insert(plan.entries.end(), pending.begin(), pending.end());
        };

        append_pass(ReplayPass::Fill,
                    ReplayRegionMode::Fill,
                    fill_cell_uv,
                    fill_radius_texels,
                    fill_all_regions);
        plan.fill_end = plan.entries.size();
        plan.fill_count = plan.fill_end;
        append_pass(ReplayPass::Paint,
                    ReplayRegionMode::Paint,
                    0.0,
                    brush_size_texels);
        plan.paint_count = plan.entries.size() - plan.fill_end;
        return plan;
    }

    // Compression is intentionally plan-local: a widened direct stroke may
    // cover only samples that share its region, UV island, and final material
    // payload.  Fill entries are never compressed.
    struct AdaptivePaintSample
    {
        double u;
        double v;
        ReplayRegion region;
        int uv_island;
        double r;
        double g;
        double b;
        bool paint_eligible;
        bool safe;
        std::uint64_t material_key;
    };

    struct AdaptiveReplayEntry
    {
        ReplayEntry replay;
        double radius_multiplier{1.0};
    };

    struct AdaptivePaintPlan
    {
        std::vector<AdaptiveReplayEntry> entries{};
        std::size_t compressed_paint_entries{0};
        std::size_t expanded_paint_entries{0};
    };

    inline AdaptivePaintPlan build_adaptive_paint_plan(
        const std::vector<ReplayEntry>& replay_entries,
        const std::vector<AdaptivePaintSample>& samples,
        double base_radius_uv,
        double tolerance_percent,
        double edge_margin_uv = 0.0)
    {
        AdaptivePaintPlan plan{};
        plan.entries.reserve(replay_entries.size());
        if (replay_entries.empty())
        {
            return plan;
        }
        if (tolerance_percent <= 0.0 || base_radius_uv <= 0.000001 || samples.empty())
        {
            for (const auto& entry : replay_entries)
            {
                plan.entries.push_back({entry, 1.0});
            }
            return plan;
        }

        int grid_size = 128;
        if (samples.size() > 200000)
        {
            grid_size = 256;
        }
        if (samples.size() > 500000)
        {
            grid_size = 512;
        }
        std::vector<std::vector<std::size_t>> grid(
            static_cast<std::size_t>(grid_size * grid_size));
        const auto cell_coordinate = [&](double value) {
            const double finite_value = std::isfinite(value) ? value : 0.0;
            return std::clamp(static_cast<int>(std::floor(finite_value * grid_size)), 0, grid_size - 1);
        };
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            const auto& sample = samples[index];
            grid[static_cast<std::size_t>(cell_coordinate(sample.v) * grid_size +
                                          cell_coordinate(sample.u))]
                .push_back(index);
        }

        const double threshold = std::clamp(tolerance_percent, 0.0, 10.0) / 100.0 * std::sqrt(3.0);
        const double threshold_squared = threshold * threshold;
        const auto same_payload = [](const AdaptivePaintSample& center,
                                     const AdaptivePaintSample& other) {
            return center.paint_eligible && center.safe && other.paint_eligible && other.safe &&
                   center.region == other.region && center.uv_island == other.uv_island &&
                   center.material_key == other.material_key;
        };
        const auto color_distance_squared = [](const AdaptivePaintSample& left,
                                               const AdaptivePaintSample& right) {
            const double dr = left.r - right.r;
            const double dg = left.g - right.g;
            const double db = left.b - right.b;
            return dr * dr + dg * dg + db * db;
        };
        const auto visit_nearby = [&](const AdaptivePaintSample& center,
                                      double radius_uv,
                                      const auto& visit) {
            const double safe_radius = std::max(0.0, radius_uv);
            const double radius_squared = safe_radius * safe_radius;
            const int min_u = cell_coordinate(center.u - safe_radius);
            const int max_u = cell_coordinate(center.u + safe_radius);
            const int min_v = cell_coordinate(center.v - safe_radius);
            const int max_v = cell_coordinate(center.v + safe_radius);
            for (int cell_v = min_v; cell_v <= max_v; ++cell_v)
            {
                for (int cell_u = min_u; cell_u <= max_u; ++cell_u)
                {
                    for (const auto other_index : grid[static_cast<std::size_t>(cell_v * grid_size + cell_u)])
                    {
                        const auto& other = samples[other_index];
                        const double du = other.u - center.u;
                        const double dv = other.v - center.v;
                        if (du * du + dv * dv <= radius_squared)
                        {
                            visit(other_index, other);
                        }
                    }
                }
            }
        };

        std::vector<bool> covered(samples.size(), false);
        std::vector<AdaptiveReplayEntry> paint_entries{};
        paint_entries.reserve(replay_entries.size());
        constexpr std::array<double, 4> multipliers{4.0, 3.0, 2.0, 1.5};
        for (const auto& entry : replay_entries)
        {
            if (entry.pass != ReplayPass::Paint || entry.sample_index >= samples.size())
            {
                plan.entries.push_back({entry, 1.0});
                continue;
            }
            if (covered[entry.sample_index])
            {
                ++plan.compressed_paint_entries;
                continue;
            }

            const auto& center = samples[entry.sample_index];
            double multiplier = 1.0;
            if (center.paint_eligible && center.safe)
            {
                for (const auto candidate_multiplier : multipliers)
                {
                    const double check_radius = std::max(
                        0.0, candidate_multiplier * base_radius_uv - std::max(0.0, edge_margin_uv));
                    bool valid = true;
                    visit_nearby(center, check_radius, [&](std::size_t, const AdaptivePaintSample& other) {
                        if (!same_payload(center, other) ||
                            color_distance_squared(center, other) > threshold_squared)
                        {
                            valid = false;
                        }
                    });
                    if (valid)
                    {
                        multiplier = candidate_multiplier;
                        break;
                    }
                }
            }

            paint_entries.push_back({entry, multiplier});
            if (multiplier > 1.0)
            {
                ++plan.expanded_paint_entries;
            }
            covered[entry.sample_index] = true;
            const double coverage_radius = std::max(
                0.0, multiplier * base_radius_uv - std::max(0.0, edge_margin_uv));
            visit_nearby(center, coverage_radius, [&](std::size_t other_index,
                                                       const AdaptivePaintSample& other) {
                if (same_payload(center, other) &&
                    color_distance_squared(center, other) <= threshold_squared)
                {
                    covered[other_index] = true;
                }
            });
        }
        std::stable_sort(paint_entries.begin(), paint_entries.end(), [](const auto& left, const auto& right) {
            return left.radius_multiplier > right.radius_multiplier;
        });
        plan.entries.insert(plan.entries.end(), paint_entries.begin(), paint_entries.end());
        return plan;
    }

    constexpr bool event_watch_generation_active(bool enabled,
                                                 std::uint64_t current_generation,
                                                 std::uint64_t captured_generation)
    {
        return enabled && current_generation == captured_generation;
    }

}
