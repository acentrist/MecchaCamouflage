#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#if (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))) || defined(__AVX2__)
#include <immintrin.h>
#define MECCHA_RUNTIME_CONTRACT_CAN_COMPILE_AVX2 1
#endif
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif
#if defined(_OPENMP)
#include <omp.h>
#endif
#include <future>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

namespace runtime_contract
{
    // Supported Shipping build: FProperty ArrayDim@0x30,
    // ElementSize@0x34, PropertyFlags@0x38.
    constexpr std::size_t FPropertyElementSizeOffset = 0x34;
    // Keep direct dispatch bounded so one scheduler tick cannot monopolize the
    // game thread. This is a CPU safety limit, not a network pacing setting.
    // Production favors a conservative burst ceiling while multiplayer limits
    // are being validated against live servers.
    constexpr int NativeRecordedPaintMaxCallsPerTick = 4;
    // Permit only a short game-owned recorded-paint lead. Waiting for zero
    // serializes every stroke; an unbounded lead recreates the visible dotted
    // frontier on joining clients. Two pending strokes halve the previous
    // production lead without changing the game's own queue implementation.
    constexpr int NativeRecordedPaintQueueTargetStrokes = 2;
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

    enum class ImageAtlasRegion
    {
        Front,
        Side,
        Back,
    };

    struct ImageAtlasMappingInput
    {
        bool cube{false};
        ImageAtlasRegion region{ImageAtlasRegion::Front};
        bool depth_is_y{false};
        double local_x{0.0};
        double local_y{0.0};
        double local_z{0.0};
        double normal_x{0.0};
        double normal_y{0.0};
        double normal_z{0.0};
        double min_x{0.0};
        double max_x{1.0};
        double min_y{0.0};
        double max_y{1.0};
        double min_z{0.0};
        double max_z{1.0};
    };

    struct ImageAtlasMappingResult
    {
        double u{0.125};
        double v{0.5};
        bool cube_side{false};
        bool cube_edge{false};
    };

    inline ImageAtlasMappingResult map_image_atlas_coordinate(const ImageAtlasMappingInput& input)
    {
        const auto normalized = [](double value, double minimum, double maximum) {
            const double range = maximum - minimum;
            return std::abs(range) <= 0.000001 ? 0.5 : std::clamp((value - minimum) / range, 0.0, 1.0);
        };
        const double horizontal = input.depth_is_y
                                      ? normalized(input.local_x, input.min_x, input.max_x)
                                      : normalized(input.local_y, input.min_y, input.max_y);
        const double depth = input.depth_is_y
                                 ? normalized(input.local_y, input.min_y, input.max_y)
                                 : normalized(input.local_x, input.min_x, input.max_x);
        ImageAtlasMappingResult result{};
        result.v = normalized(input.local_z, input.min_z, input.max_z);
        const double normal_x = std::abs(input.normal_x);
        const double normal_y = std::abs(input.normal_y);
        const double normal_z = std::abs(input.normal_z);
        if (input.cube && normal_z > normal_x && normal_z > normal_y)
        {
            const double centered_x = input.local_x - (input.min_x + input.max_x) * 0.5;
            const double centered_y = input.local_y - (input.min_y + input.max_y) * 0.5;
            result.u = std::clamp(std::atan2(centered_y, centered_x) / (2.0 * 3.14159265358979323846) + 0.5, 0.0, 1.0);
            result.v = input.normal_z >= 0.0 ? 0.0 : 1.0;
            result.cube_edge = true;
            return result;
        }
        if (input.region == ImageAtlasRegion::Side)
        {
            const double side_coordinate = input.depth_is_y ? input.local_x : input.local_y;
            const double horizontal_midpoint = input.depth_is_y
                                                   ? (input.min_x + input.max_x) * 0.5
                                                   : (input.min_y + input.max_y) * 0.5;
            result.u = side_coordinate > horizontal_midpoint
                           ? 0.25 + depth * 0.25
                           : 0.75 + (1.0 - depth) * 0.25;
            result.cube_side = input.cube;
        }
        else if (input.region == ImageAtlasRegion::Back)
        {
            result.u = 0.5 + (1.0 - horizontal) * 0.25;
        }
        else
        {
            result.u = horizontal * 0.25;
        }
        return result;
    }

    // Cube image designs are not UV-like unwraps. They are four orthographic
    // views of one canonical, natural-standing reference pose. Keep this tiny
    // projection contract shared by the bridge and its native validation test;
    // the UI mirrors the same fixed 1024 x 512 canvas geometry.
    enum class CubeCanonicalImageFace
    {
        Front,
        Right,
        Back,
        Left,
    };

    struct CubeCanonicalImageProjectionInput
    {
        double local_x{0.0};
        double local_y{0.0};
        double local_z{0.0};
        double normal_x{0.0};
        double normal_y{-1.0};
        double center_x{0.0};
        double center_y{0.0};
        double center_z{0.0};
        double pixels_per_unit{1.0};
    };

    struct CubeCanonicalImageProjectionResult
    {
        CubeCanonicalImageFace face{CubeCanonicalImageFace::Front};
        double u{0.125};
        double v{0.5};
    };

    inline CubeCanonicalImageFace classify_cube_canonical_image_face(double normal_x, double normal_y)
    {
        if (std::abs(normal_x) >= std::abs(normal_y))
        {
            return normal_x >= 0.0 ? CubeCanonicalImageFace::Right : CubeCanonicalImageFace::Left;
        }
        return normal_y >= 0.0 ? CubeCanonicalImageFace::Back : CubeCanonicalImageFace::Front;
    }

    inline CubeCanonicalImageProjectionResult map_cube_canonical_image_coordinate(const CubeCanonicalImageProjectionInput& input)
    {
        CubeCanonicalImageProjectionResult result{};
        result.face = classify_cube_canonical_image_face(input.normal_x, input.normal_y);
        const double pixels_per_unit = std::isfinite(input.pixels_per_unit) && input.pixels_per_unit > 0.000001
                                           ? input.pixels_per_unit
                                           : 1.0;
        double horizontal = input.local_x - input.center_x;
        int tile = 0;
        switch (result.face)
        {
        case CubeCanonicalImageFace::Front:
            tile = 0;
            break;
        case CubeCanonicalImageFace::Right:
            tile = 1;
            horizontal = input.local_y - input.center_y;
            break;
        case CubeCanonicalImageFace::Back:
            tile = 2;
            horizontal = input.center_x - input.local_x;
            break;
        case CubeCanonicalImageFace::Left:
            tile = 3;
            horizontal = input.center_y - input.local_y;
            break;
        }
        result.u = (static_cast<double>(tile) * 256.0 + 128.0 + horizontal * pixels_per_unit) / 1024.0;
        result.v = 0.5 + (input.local_z - input.center_z) * pixels_per_unit / 512.0;
        return result;
    }

    // Round uses the same four orthographic canvas tiles as Cube. Unlike the
    // legacy normalized atlas, every tile shares one pixels-per-unit value:
    // the narrow side silhouette must not be stretched to the front width.
    struct RoundCanonicalImageProjectionInput
    {
        ImageAtlasRegion region{ImageAtlasRegion::Front};
        bool depth_is_y{true};
        double local_x{0.0};
        double local_y{0.0};
        double local_z{0.0};
        double center_x{0.0};
        double center_y{0.0};
        double center_z{0.0};
        double pixels_per_unit{1.0};
    };

    struct RoundCanonicalImageProjectionResult
    {
        int tile{0};
        double u{0.125};
        double v{0.5};
    };

    inline RoundCanonicalImageProjectionResult map_round_canonical_image_coordinate(
        const RoundCanonicalImageProjectionInput& input)
    {
        RoundCanonicalImageProjectionResult result{};
        const double pixels_per_unit =
            std::isfinite(input.pixels_per_unit) && input.pixels_per_unit > 0.000001
                ? input.pixels_per_unit
                : 1.0;
        const double front_horizontal = input.depth_is_y
                                            ? input.local_x - input.center_x
                                            : input.local_y - input.center_y;
        double horizontal = front_horizontal;
        switch (input.region)
        {
        case ImageAtlasRegion::Front:
            result.tile = 0;
            break;
        case ImageAtlasRegion::Back:
            result.tile = 2;
            horizontal = -front_horizontal;
            break;
        case ImageAtlasRegion::Side:
        {
            const double side_coordinate = input.depth_is_y
                                               ? input.local_x - input.center_x
                                               : input.local_y - input.center_y;
            const double side_horizontal = input.depth_is_y
                                               ? input.local_y - input.center_y
                                               : input.local_x - input.center_x;
            result.tile = side_coordinate > 0.0 ? 1 : 3;
            horizontal = result.tile == 1 ? side_horizontal : -side_horizontal;
            break;
        }
        }
        result.u = (static_cast<double>(result.tile) * 256.0 + 128.0 +
                    horizontal * pixels_per_unit) / 1024.0;
        result.v = 0.5 + (input.local_z - input.center_z) * pixels_per_unit / 512.0;
        return result;
    }

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
        int adaptive_plan_worker_count{0};
        bool adaptive_plan_parallel{false};
        bool adaptive_plan_avx2_available{false};
        bool adaptive_plan_avx2_used{false};
    };

    inline bool adaptive_plan_avx2_available()
    {
#if defined(MECCHA_RUNTIME_CONTRACT_CAN_COMPILE_AVX2)
        static const bool available = []() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
            int registers[4]{};
            __cpuid(registers, 0);
            if (registers[0] < 7)
            {
                return false;
            }
            __cpuidex(registers, 1, 0);
            constexpr int osxsave_bit = 1 << 27;
            constexpr int avx_bit = 1 << 28;
            if ((registers[2] & (osxsave_bit | avx_bit)) != (osxsave_bit | avx_bit))
            {
                return false;
            }
            if ((_xgetbv(0) & 0x6) != 0x6)
            {
                return false;
            }
            __cpuidex(registers, 7, 0);
            constexpr int avx2_bit = 1 << 5;
            return (registers[1] & avx2_bit) != 0;
#elif defined(__GNUC__) || defined(__clang__)
            return __builtin_cpu_supports("avx2");
#else
            return false;
#endif
        }();
        return available;
#else
        return false;
#endif
    }

    inline AdaptivePaintPlan build_adaptive_paint_plan(
        const std::vector<ReplayEntry>& replay_entries,
        const std::vector<AdaptivePaintSample>& samples,
        double base_radius_uv,
        double tolerance_percent,
        double edge_margin_uv = 0.0)
    {
        AdaptivePaintPlan plan{};
        plan.entries.reserve(replay_entries.size());
        plan.adaptive_plan_avx2_available = adaptive_plan_avx2_available();
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

        const double threshold = std::clamp(tolerance_percent, 0.0, 10.0) / 100.0;
        const double threshold_squared = threshold * threshold;
        const auto same_payload = [](const AdaptivePaintSample& center,
                                     const AdaptivePaintSample& other) {
            return center.paint_eligible && center.safe && other.paint_eligible && other.safe &&
                   center.region == other.region && center.uv_island == other.uv_island &&
                   center.material_key == other.material_key;
        };
        const bool use_avx2 = plan.adaptive_plan_avx2_available;
        plan.adaptive_plan_avx2_used = use_avx2;
        const auto color_distance_squared = [use_avx2](const AdaptivePaintSample& left,
                                                       const AdaptivePaintSample& right) {
#if defined(MECCHA_RUNTIME_CONTRACT_CAN_COMPILE_AVX2)
            if (use_avx2)
            {
                const __m256d vleft = _mm256_setr_pd(left.r, left.g, left.b, 0.0);
                const __m256d vright = _mm256_setr_pd(right.r, right.g, right.b, 0.0);
                const __m256d vdiff = _mm256_sub_pd(vleft, vright);
                const __m256d vdiff2 = _mm256_mul_pd(vdiff, vdiff);
                const __m256d vweights = _mm256_setr_pd(0.299, 0.587, 0.114, 0.0);
                const __m256d vprod = _mm256_mul_pd(vdiff2, vweights);
                alignas(32) double result[4]{};
                _mm256_storeu_pd(result, vprod);
                return result[0] + result[1] + result[2];
            }
#endif
            const double dr = left.r - right.r;
            const double dg = left.g - right.g;
            const double db = left.b - right.b;
            return 0.299 * (dr * dr) + 0.587 * (dg * dg) + 0.114 * (db * db);
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
        constexpr std::array<double, 6> multipliers{8.0, 6.0, 4.0, 3.0, 2.0, 1.5};
        const std::size_t num_replay_entries = replay_entries.size();
        std::vector<double> candidate_multipliers(num_replay_entries, 1.0);

        const unsigned int hw_threads = std::max(1u, std::thread::hardware_concurrency());
        if (num_replay_entries > 128 && hw_threads > 1)
        {
            const std::size_t num_threads = std::min<std::size_t>(hw_threads, 16);
            plan.adaptive_plan_worker_count = static_cast<int>(num_threads);
            plan.adaptive_plan_parallel = num_threads > 1;
            const std::size_t chunk_size = (num_replay_entries + num_threads - 1) / num_threads;
            std::vector<std::future<void>> futures;
            futures.reserve(num_threads);

            for (std::size_t thread_idx = 0; thread_idx < num_threads; ++thread_idx)
            {
                const std::size_t start_idx = thread_idx * chunk_size;
                const std::size_t end_idx = std::min(start_idx + chunk_size, num_replay_entries);
                if (start_idx >= end_idx) continue;

                futures.push_back(std::async(std::launch::async, [&, start_idx, end_idx]() {
                    for (std::size_t i = start_idx; i < end_idx; ++i)
                    {
                        const auto& entry = replay_entries[i];
                        if (entry.pass != ReplayPass::Paint || entry.sample_index >= samples.size())
                        {
                            continue;
                        }

                        const auto& center = samples[entry.sample_index];
                        const double center_luma = 0.299 * center.r + 0.587 * center.g + 0.114 * center.b;
                        const double effective_threshold_squared = (center_luma < 0.20)
                            ? threshold_squared * 2.25
                            : threshold_squared;
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
                                        color_distance_squared(center, other) > effective_threshold_squared)
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
                        candidate_multipliers[i] = multiplier;
                    }
                }));
            }
            for (auto& f : futures)
            {
                f.get();
            }
        }
        else
        {
            plan.adaptive_plan_worker_count = 1;
            for (std::size_t i = 0; i < num_replay_entries; ++i)
            {
                const auto& entry = replay_entries[i];
                if (entry.pass != ReplayPass::Paint || entry.sample_index >= samples.size())
                {
                    continue;
                }

                const auto& center = samples[entry.sample_index];
                const double center_luma = 0.299 * center.r + 0.587 * center.g + 0.114 * center.b;
                const double effective_threshold_squared = (center_luma < 0.20)
                    ? threshold_squared * 2.25
                    : threshold_squared;
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
                                color_distance_squared(center, other) > effective_threshold_squared)
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
                candidate_multipliers[i] = multiplier;
            }
        }

        for (std::size_t i = 0; i < num_replay_entries; ++i)
        {
            const auto& entry = replay_entries[i];
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

            const double multiplier = candidate_multipliers[i];
            const auto& center = samples[entry.sample_index];
            const double center_luma = 0.299 * center.r + 0.587 * center.g + 0.114 * center.b;
            const double effective_threshold_squared = (center_luma < 0.20)
                ? threshold_squared * 2.25
                : threshold_squared;

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
                    color_distance_squared(center, other) <= effective_threshold_squared)
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

    // A mesh can reuse UV islands.  When the game preserves triangle order,
    // every runtime triangle can still be verified against its profile index
    // without guessing which duplicate island owns the geometry.
    template <typename Triangle, typename MatchRuntimeTriangle, typename ReorderRuntimeTriangle>
    inline bool order_runtime_triangles_by_direct_profile_index(
        const std::vector<Triangle>& runtime,
        int expected_triangle_count,
        MatchRuntimeTriangle match_runtime_triangle,
        ReorderRuntimeTriangle reorder_runtime_triangle,
        std::vector<Triangle>& ordered,
        double& average_error)
    {
        ordered.clear();
        average_error = 0.0;
        if (expected_triangle_count <= 0 ||
            static_cast<int>(runtime.size()) != expected_triangle_count)
        {
            return false;
        }

        ordered.reserve(static_cast<std::size_t>(expected_triangle_count));
        double error_sum = 0.0;
        for (int profile_triangle = 0;
             profile_triangle < expected_triangle_count;
             ++profile_triangle)
        {
            const auto& runtime_triangle = runtime[static_cast<std::size_t>(profile_triangle)];
            const auto match = match_runtime_triangle(profile_triangle, runtime_triangle);
            if (!match.ok || !std::isfinite(match.error))
            {
                ordered.clear();
                return false;
            }
            ordered.push_back(reorder_runtime_triangle(runtime_triangle, match));
            error_sum += match.error;
        }
        average_error = error_sum / static_cast<double>(expected_triangle_count);
        return true;
    }

    constexpr bool event_watch_generation_active(bool enabled,
                                                 std::uint64_t current_generation,
                                                 std::uint64_t captured_generation)
    {
        return enabled && current_generation == captured_generation;
    }

}
