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
#include <limits>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace runtime_contract
{
    constexpr const char* paint_dispatch_exception_kind(
        std::uint32_t code)
    {
        switch (code)
        {
        case 0x80000003U:
            return "breakpoint";
        case 0xC0000005U:
            return "access_violation";
        case 0xC0000006U:
            return "in_page_error";
        case 0xC000001DU:
            return "illegal_instruction";
        case 0xC000008CU:
            return "array_bounds_exceeded";
        case 0xC0000094U:
            return "integer_divide_by_zero";
        case 0xC0000095U:
            return "integer_overflow";
        case 0xC0000096U:
            return "privileged_instruction";
        case 0xC00000FDU:
            return "stack_overflow";
        case 0xE06D7363U:
            return "msvc_cpp_exception";
        default:
            return "unknown";
        }
    }

    constexpr const char* paint_dispatch_access_operation(
        bool available,
        std::uintptr_t operation)
    {
        if (!available)
        {
            return "not_applicable";
        }
        switch (operation)
        {
        case 0U:
            return "read";
        case 1U:
            return "write";
        case 8U:
            return "execute";
        default:
            return "unknown";
        }
    }

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
    constexpr int FallbackMaxOutgoingNetworkBatchesPerSecond = 20;
    constexpr int FallbackMaxOutgoingStrokesPerBatch = 20;
    constexpr int ConservativeReplicationCapacityNumerator = 4;
    constexpr int ConservativeReplicationCapacityDenominator = 5;
    constexpr int ConservativeReplicationBurstCalls = 3;
    constexpr int AssumedRemotePaintFps = 60;
    constexpr int RemoteQueueObservationWindows = 8;
    constexpr int ConservativeVisualConfirmationFps = 30;

    struct PaintReplicationPacingPlan
    {
        int max_outgoing_batches_per_second;
        int max_outgoing_strokes_per_batch;
        int conservative_network_strokes_per_second;
        int conservative_receiver_strokes_per_second;
        int effective_strokes_per_second;
        int conservative_strokes_per_window;
        int calls_per_tick;
        int network_window_ms;
        int cadence_ms;
        int final_confirmation_ms;
    };

    constexpr int ceil_div_positive(std::int64_t numerator, int denominator)
    {
        return denominator > 0 && numerator > 0
                   ? static_cast<int>((numerator + denominator - 1) / denominator)
                   : 0;
    }

    constexpr PaintReplicationPacingPlan paint_replication_pacing_plan(
        int reported_batches_per_second,
        int reported_strokes_per_batch,
        int reported_render_target_writes_per_frame,
        int max_calls_per_tick,
        int min_remote_frames_after_local_paint,
        bool adaptive_remote_interval,
        int max_adaptive_remote_frame_interval)
    {
        const int batches_per_second =
            reported_batches_per_second > 0 && reported_batches_per_second <= 240
                ? reported_batches_per_second
                : FallbackMaxOutgoingNetworkBatchesPerSecond;
        const int strokes_per_batch =
            reported_strokes_per_batch > 0 && reported_strokes_per_batch <= 4096
                ? reported_strokes_per_batch
                : FallbackMaxOutgoingStrokesPerBatch;
        const int reported_conservative_strokes =
            strokes_per_batch * ConservativeReplicationCapacityNumerator /
            ConservativeReplicationCapacityDenominator;
        const int conservative_network_strokes_per_window =
            reported_conservative_strokes > 1 ? reported_conservative_strokes : 1;
        const int conservative_network_strokes_per_second =
            conservative_network_strokes_per_window * batches_per_second > 1
                ? conservative_network_strokes_per_window * batches_per_second
                : 1;
        const int minimum_remote_frames =
            min_remote_frames_after_local_paint > 0
                ? min_remote_frames_after_local_paint
                : 1;
        const int adaptive_remote_frames =
            max_adaptive_remote_frame_interval > 0
                ? max_adaptive_remote_frame_interval
                : 0;
        const int confirmation_frames =
            adaptive_remote_interval
                ? (minimum_remote_frames > adaptive_remote_frames
                       ? minimum_remote_frames
                       : adaptive_remote_frames)
                : minimum_remote_frames;
        const int render_target_writes_per_frame =
            reported_render_target_writes_per_frame > 0 &&
                    reported_render_target_writes_per_frame <= 4096
                ? reported_render_target_writes_per_frame
                : 0;
        const int reported_receiver_strokes_per_second =
            render_target_writes_per_frame > 0
                ? static_cast<int>(
                      static_cast<std::int64_t>(render_target_writes_per_frame) *
                      AssumedRemotePaintFps /
                      (confirmation_frames > 0 ? confirmation_frames : 1))
                : conservative_network_strokes_per_second;
        const int conservative_receiver_strokes_per_second =
            render_target_writes_per_frame > 0
                ? std::max(
                      1,
                      reported_receiver_strokes_per_second *
                          ConservativeReplicationCapacityNumerator /
                          ConservativeReplicationCapacityDenominator)
                : conservative_network_strokes_per_second;
        const int effective_strokes_per_second =
            conservative_network_strokes_per_second <
                    conservative_receiver_strokes_per_second
                ? conservative_network_strokes_per_second
                : conservative_receiver_strokes_per_second;
        const int safe_max_calls = max_calls_per_tick > 1 ? max_calls_per_tick : 1;
        const int burst_calls =
            safe_max_calls < ConservativeReplicationBurstCalls
                ? safe_max_calls
                : ConservativeReplicationBurstCalls;
        const int calls_per_tick =
            burst_calls < conservative_network_strokes_per_window
                ? burst_calls
                : conservative_network_strokes_per_window;
        const int reported_network_window_ms =
            ceil_div_positive(1000, batches_per_second);
        const int network_window_ms =
            reported_network_window_ms > 1 ? reported_network_window_ms : 1;
        const int receiver_strokes_per_window =
            ceil_div_positive(effective_strokes_per_second, batches_per_second);
        const int conservative_strokes_per_window =
            std::min(
                conservative_network_strokes_per_window,
                std::max(calls_per_tick, receiver_strokes_per_window));
        const int reported_cadence_ms =
            ceil_div_positive(
                static_cast<std::int64_t>(calls_per_tick) * 1000,
                effective_strokes_per_second);
        const int cadence_ms = reported_cadence_ms > 1 ? reported_cadence_ms : 1;
        const int final_confirmation_ms =
            network_window_ms * RemoteQueueObservationWindows +
            ceil_div_positive(
                static_cast<std::int64_t>(confirmation_frames) * 1000,
                ConservativeVisualConfirmationFps);
        return {
            batches_per_second,
            strokes_per_batch,
            conservative_network_strokes_per_second,
            conservative_receiver_strokes_per_second,
            effective_strokes_per_second,
            conservative_strokes_per_window,
            calls_per_tick,
            network_window_ms,
            cadence_ms,
            final_confirmation_ms,
        };
    }

    constexpr bool paint_queue_observer_authoritative(bool available,
                                                       bool observed_activity)
    {
        return available && observed_activity;
    }

    constexpr bool paint_final_queue_ready(bool observer_available,
                                           bool observer_observed_activity,
                                           int visual_pending_strokes,
                                           bool outgoing_available,
                                           int outgoing_pending_strokes)
    {
        if (outgoing_available && outgoing_pending_strokes > 0)
        {
            return false;
        }
        return !paint_queue_observer_authoritative(observer_available,
                                                   observer_observed_activity) ||
               visual_pending_strokes <= 0;
    }

    constexpr bool paint_visual_drain_complete(bool observer_available,
                                               bool observer_observed_activity,
                                               int visual_pending_strokes,
                                               bool outgoing_available,
                                               int outgoing_pending_strokes,
                                               int final_elapsed_ms,
                                               int final_confirmation_ms)
    {
        return paint_final_queue_ready(observer_available,
                                       observer_observed_activity,
                                       visual_pending_strokes,
                                       outgoing_available,
                                       outgoing_pending_strokes) &&
               final_elapsed_ms >=
               (final_confirmation_ms > 0 ? final_confirmation_ms : 0);
    }

    constexpr int paint_final_confirmation_remaining_ms(bool final_queue_ready,
                                                        int ready_elapsed_ms,
                                                        int final_confirmation_ms,
                                                        int network_window_ms)
    {
        const int required_ms = final_confirmation_ms > 0 ? final_confirmation_ms : 0;
        if (!final_queue_ready)
        {
            return required_ms + (network_window_ms > 0 ? network_window_ms : 1);
        }
        const int elapsed_ms = ready_elapsed_ms > 0 ? ready_elapsed_ms : 0;
        return elapsed_ms < required_ms ? required_ms - elapsed_ms : 0;
    }

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
        constexpr ReplayRegion region_order[]{ReplayRegion::Back,
                                               ReplayRegion::Side,
                                               ReplayRegion::Front};
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
            const double row_height = std::max(
                0.000001,
                vertical_span * std::max(0.001, row_size_texels) / texture_size_double);
            for (const auto region : region_order)
            {
                std::vector<ReplayEntry> pending{};
                for (const auto& candidate : candidates)
                {
                    if (candidate.region != region ||
                        (!include_all_regions && candidate.mode != required_mode))
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
            }
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

    // Appearance Match intentionally keeps source HDR information separate
    // from paint-channel values.  UE's FPaintChannelData accepts only
    // [0, 1] material controls, but a scene capture can legitimately contain
    // values above one.  Never use a paint-channel clamp on capture data.
    enum class AppearancePaintUvRoute
    {
        Invalid,
        RuntimeTriangle,
        ScreenHit,
    };

    inline auto appearance_paint_uv_route(bool screen_hit_enabled,
                                          bool runtime_triangle_uv_valid,
                                          bool screen_hit_valid,
                                          double screen_hit_world_delta_cm)
        -> AppearancePaintUvRoute
    {
        if (screen_hit_enabled &&
            screen_hit_valid &&
            std::isfinite(screen_hit_world_delta_cm) &&
            screen_hit_world_delta_cm >= 0.0 &&
            screen_hit_world_delta_cm <= 1.0)
        {
            return AppearancePaintUvRoute::ScreenHit;
        }
        return runtime_triangle_uv_valid
                   ? AppearancePaintUvRoute::RuntimeTriangle
                   : AppearancePaintUvRoute::Invalid;
    }

    constexpr bool appearance_should_resolve_screen_hit_uv(
        bool auto_material,
        bool any_paint_region,
        bool image_paint,
        bool research_screen_hit_requested)
    {
        // The validated runtime-triangle cache is the production paint-UV
        // source. HitTestAtScreenPosition is retained as an explicit research
        // cross-check, not a per-sample production prerequisite.
        return auto_material &&
               any_paint_region &&
               !image_paint &&
               research_screen_hit_requested;
    }

    inline bool appearance_normalized_screen_position_valid(
        double x,
        double y)
    {
        return std::isfinite(x) &&
               std::isfinite(y) &&
               x >= 0.0 &&
               x <= 1.0 &&
               y >= 0.0 &&
               y <= 1.0;
    }

    struct AppearanceCaptureSampleEligibility
    {
        bool region_enabled{false};
        bool unsafe{false};
    };

    constexpr bool appearance_capture_sample_included(
        const AppearanceCaptureSampleEligibility& sample)
    {
        return sample.region_enabled && !sample.unsafe;
    }

    constexpr bool appearance_manual_preview_feedback_required(
        bool manual_feedback_requested,
        bool include_scene_lighting,
        int emission_roi_samples)
    {
        return manual_feedback_requested &&
               (include_scene_lighting ||
                emission_roi_samples > 0);
    }

    constexpr bool appearance_projected_material_sample_available(
        bool capture_sample_available,
        bool projection_visible,
        bool shared_face)
    {
        // Front and Back are two destinations for the same captured face
        // material. Visibility chooses which one can be measured on screen,
        // not whether either destination receives that material.
        return capture_sample_available &&
               (shared_face || projection_visible);
    }

    constexpr bool appearance_use_direct_face_capture(
        bool capture_sample_available,
        bool shared_face,
        bool source_candidate,
        bool projection_visible)
    {
        // Side retains its topology-aware transfer path. Front and Back share
        // the direct projected sample so camera orientation cannot select a
        // different material path for either destination.
        return capture_sample_available &&
               (shared_face ||
                source_candidate ||
                projection_visible);
    }

    struct AppearanceFeedbackSampleEligibility
    {
        bool hdr_available{false};
        bool camera_stable{false};
        bool hdr_finite{false};
        bool unsafe{false};
        bool projection_visible{false};
        bool shared_face{false};
    };

    constexpr bool appearance_feedback_sample_supported(
        const AppearanceFeedbackSampleEligibility& sample)
    {
        // Front and Back intentionally share the measured face response.
        // Side remains visibility-gated because it uses a separate
        // topology-aware source transfer.
        return sample.hdr_available &&
               sample.camera_stable &&
               sample.hdr_finite &&
               !sample.unsafe &&
               (sample.shared_face ||
                sample.projection_visible);
    }

    constexpr bool
    appearance_candidate_may_zero_emissive(
        int emission_roi_samples)
    {
        return emission_roi_samples <= 0;
    }

    constexpr int appearance_source_query_probe_budget(
        int candidate_samples)
    {
        // Keep the game-thread query bounded, while covering the full
        // candidate set observed on the supported 1080p shipping profile.
        return std::min(8192, std::max(0, candidate_samples));
    }

    constexpr double AppearanceHdrMaximum = 64.0;
    constexpr double AppearanceFallbackRoughness = 0.65;
    constexpr int AppearanceMaxClusters = 8;
    constexpr int AppearanceSpsaIterations = 3;
    constexpr double AppearanceFitMedianDeltaEMaximum = 0.05;
    constexpr double AppearanceFitMinimumImprovement = 0.15;
    constexpr int AppearanceFitMinimumSamples = 256;
    constexpr double AppearanceCalibratedEmissiveMinimumMaximum = 0.15;
    constexpr int AppearanceClusterEmissiveMinimumSamples = 64;
    constexpr double AppearanceClusterEmissiveMinimumCoverage = 0.10;
    constexpr double AppearanceClusterEmissiveMinimumSourceSeed = 0.05;
    constexpr double AppearanceClusterEmissiveMinimumImprovement = 0.0025;
    constexpr double AppearanceClusterSourceCandidateMinimumCoverage = 0.80;
    constexpr double AppearanceClusterCandidateMinimumImprovement = 0.0001;
    constexpr double AppearanceClusterIntrinsicCoreMinimumCoverage = 0.75;
    constexpr double AppearanceClusterCalibratedMinimumCoverage = 0.90;
    constexpr double AppearanceClusterNearNeutralMaximumLossIncrease = 0.01;

    struct AppearanceRgb
    {
        double r{0.0};
        double g{0.0};
        double b{0.0};
    };

    struct AppearanceHdrSample
    {
        AppearanceRgb value{};
        bool finite{false};
        bool clipped{false};
    };

    inline bool appearance_rgb_finite(const AppearanceRgb& value)
    {
        return std::isfinite(value.r) && std::isfinite(value.g) && std::isfinite(value.b);
    }

    inline AppearanceHdrSample appearance_sanitize_hdr(const AppearanceRgb& source)
    {
        AppearanceHdrSample out{};
        out.finite = appearance_rgb_finite(source);
        if (!out.finite)
        {
            return out;
        }
        const auto sanitize = [&out](double value) {
            if (value < 0.0)
            {
                return 0.0;
            }
            if (value > AppearanceHdrMaximum)
            {
                out.clipped = true;
                return AppearanceHdrMaximum;
            }
            return value;
        };
        out.value = {sanitize(source.r), sanitize(source.g), sanitize(source.b)};
        return out;
    }

    inline AppearanceRgb appearance_clamp_albedo(const AppearanceRgb& source)
    {
        return {std::clamp(source.r, 0.0, 1.0),
                std::clamp(source.g, 0.0, 1.0),
                std::clamp(source.b, 0.0, 1.0)};
    }

    inline double appearance_srgb_to_linear(double encoded)
    {
        const auto value = std::clamp(encoded, 0.0, 1.0);
        return value <= 0.04045
                   ? value / 12.92
                   : std::pow((value + 0.055) / 1.055, 2.4);
    }

    inline double appearance_linear_to_srgb(double linear)
    {
        const auto value = std::clamp(linear, 0.0, 1.0);
        return value <= 0.0031308
                   ? value * 12.92
                   : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    }

    inline AppearanceRgb appearance_reinhard_display(const AppearanceRgb& source)
    {
        const auto tone_map = [](double value) {
            const auto safe = std::max(0.0, value);
            return safe / (1.0 + safe);
        };
        return {tone_map(source.r), tone_map(source.g), tone_map(source.b)};
    }

    struct AppearanceOkLab
    {
        double l{0.0};
        double a{0.0};
        double b{0.0};
    };

    inline AppearanceOkLab appearance_linear_srgb_to_oklab(const AppearanceRgb& source)
    {
        const auto value = appearance_clamp_albedo(source);
        const double l = 0.4122214708 * value.r + 0.5363325363 * value.g + 0.0514459929 * value.b;
        const double m = 0.2119034982 * value.r + 0.6806995451 * value.g + 0.1073969566 * value.b;
        const double s = 0.0883024619 * value.r + 0.2817188376 * value.g + 0.6299787005 * value.b;
        const auto cube_root = [](double channel) {
            return std::cbrt(std::max(0.0, channel));
        };
        const double l_root = cube_root(l);
        const double m_root = cube_root(m);
        const double s_root = cube_root(s);
        return {0.2104542553 * l_root + 0.7936177850 * m_root - 0.0040720468 * s_root,
                1.9779984951 * l_root - 2.4285922050 * m_root + 0.4505937099 * s_root,
                0.0259040371 * l_root + 0.7827717662 * m_root - 0.8086757660 * s_root};
    }

    inline double appearance_oklab_delta_e(const AppearanceRgb& left,
                                            const AppearanceRgb& right)
    {
        const auto a = appearance_linear_srgb_to_oklab(left);
        const auto b = appearance_linear_srgb_to_oklab(right);
        const auto dl = a.l - b.l;
        const auto da = a.a - b.a;
        const auto db = a.b - b.b;
        return std::sqrt(dl * dl + da * da + db * db);
    }

    inline double appearance_rgb_chromaticity_delta(
        const AppearanceRgb& left,
        const AppearanceRgb& right)
    {
        if (!appearance_rgb_finite(left) ||
            !appearance_rgb_finite(right))
        {
            return std::numeric_limits<double>::infinity();
        }
        const auto left_sum =
            std::max(0.0, left.r) +
            std::max(0.0, left.g) +
            std::max(0.0, left.b);
        const auto right_sum =
            std::max(0.0, right.r) +
            std::max(0.0, right.g) +
            std::max(0.0, right.b);
        if (left_sum <= 0.001 || right_sum <= 0.001)
        {
            return 0.0;
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

    inline double appearance_huber_loss(double value, double delta = 0.05)
    {
        const auto absolute = std::abs(value);
        const auto safe_delta = std::max(0.000001, delta);
        return absolute <= safe_delta
                   ? 0.5 * absolute * absolute
                   : safe_delta * (absolute - 0.5 * safe_delta);
    }

    inline double appearance_luminance(const AppearanceRgb& source)
    {
        return 0.2126 * source.r + 0.7152 * source.g + 0.0722 * source.b;
    }

    struct AppearanceShowFlagSetting
    {
        const char* name{nullptr};
        bool enabled{false};
    };

    inline const std::array<AppearanceShowFlagSetting, 33>&
    appearance_intrinsic_emission_show_flags()
    {
        static const std::array<AppearanceShowFlagSetting, 33> settings{{
            {"Lighting", false},
            {"DeferredLighting", false},
            {"DirectLighting", false},
            {"SkyLighting", false},
            {"DynamicShadows", false},
            {"ContactShadows", false},
            {"RayTracedDistanceFieldShadows", false},
            {"GlobalIllumination", false},
            {"LumenGlobalIllumination", false},
            {"ReflectionEnvironment", false},
            {"ScreenSpaceReflections", false},
            {"LumenReflections", false},
            {"AmbientOcclusion", false},
            {"AmbientCubemap", false},
            {"Bloom", false},
            {"PostProcessing", false},
            {"Tonemapper", false},
            {"EyeAdaptation", false},
            {"LocalExposure", false},
            {"ColorGrading", false},
            {"Fog", false},
            {"VolumetricFog", false},
            {"Atmosphere", false},
            {"SkyAtmosphere", false},
            {"Cloud", false},
            {"VolumetricCloud", false},
            {"LightShafts", false},
            {"MotionBlur", false},
            {"DepthOfField", false},
            {"LensFlares", false},
            {"Vignette", false},
            {"FilmGrain", false},
            {"UnlitViewmode", false},
        }};
        return settings;
    }

    struct AppearanceCapturePoolKey
    {
        int width{0};
        int height{0};
        int render_target_format{0};
        bool isolated_show_flags{false};
        std::uint64_t visibility_signature{0};
    };

    constexpr bool appearance_capture_pool_key_matches(
        const AppearanceCapturePoolKey& left,
        const AppearanceCapturePoolKey& right)
    {
        // CaptureSource is intentionally absent: one render target and
        // SceneCapture actor can serve BaseColor/Normal/Depth passes when the
        // format, visibility set and ShowFlags profile are unchanged.
        return left.width == right.width &&
               left.height == right.height &&
               left.render_target_format ==
                   right.render_target_format &&
               left.isolated_show_flags ==
                   right.isolated_show_flags &&
               left.visibility_signature ==
                   right.visibility_signature;
    }

    constexpr bool appearance_projection_diagnostic_sample(
        std::size_t index,
        std::size_t sample_count,
        std::size_t maximum_samples)
    {
        if (maximum_samples == 0U ||
            sample_count == 0U ||
            index >= sample_count)
        {
            return false;
        }
        if (sample_count <= maximum_samples)
        {
            return true;
        }
        const auto stride =
            (sample_count + maximum_samples - 1U) /
            maximum_samples;
        return index % stride == 0U;
    }

    inline std::vector<std::size_t>
    appearance_capture_sparse_pixel_indices(
        int width,
        int height,
        const std::vector<std::pair<int, int>>&
            projected_pixels)
    {
        std::vector<std::size_t> indices{};
        if (width <= 0 || height <= 0)
        {
            return indices;
        }
        indices.reserve(
            projected_pixels.size() * 4U);
        for (const auto& pixel :
             projected_pixels)
        {
            const int x = pixel.first;
            const int y = pixel.second;
            if (x < 0 || x >= width ||
                y < 0 || y >= height)
            {
                continue;
            }
            const int xs[]{x, width - 1 - x};
            const int ys[]{y, height - 1 - y};
            for (const auto sample_y : ys)
            {
                for (const auto sample_x : xs)
                {
                    indices.push_back(
                        static_cast<std::size_t>(
                            sample_y) *
                            static_cast<std::size_t>(
                                width) +
                        static_cast<std::size_t>(
                            sample_x));
                }
            }
        }
        std::sort(indices.begin(), indices.end());
        indices.erase(
            std::unique(
                indices.begin(),
                indices.end()),
            indices.end());
        return indices;
    }

    inline AppearanceRgb appearance_intrinsic_emission_residual(
        const AppearanceRgb& isolated_hdr,
        const AppearanceRgb& base_srgb)
    {
        if (!appearance_rgb_finite(isolated_hdr) ||
            !appearance_rgb_finite(base_srgb))
        {
            return {};
        }
        return {
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

    inline AppearanceRgb appearance_emission_chromaticity_albedo(
        const AppearanceRgb& intrinsic_emission_hdr,
        const AppearanceRgb& fallback_albedo)
    {
        if (!appearance_rgb_finite(intrinsic_emission_hdr))
        {
            return appearance_clamp_albedo(fallback_albedo);
        }
        const AppearanceRgb positive{
            std::max(0.0, intrinsic_emission_hdr.r),
            std::max(0.0, intrinsic_emission_hdr.g),
            std::max(0.0, intrinsic_emission_hdr.b)};
        const auto peak =
            std::max({positive.r, positive.g, positive.b});
        if (!std::isfinite(peak) || peak <= 0.000001)
        {
            return appearance_clamp_albedo(fallback_albedo);
        }
        return appearance_clamp_albedo(
            {positive.r / peak,
             positive.g / peak,
             positive.b / peak});
    }

    struct AppearanceEmissionNoiseModel
    {
        bool ok{false};
        double median{0.0};
        double mad{0.0};
        double threshold{0.01};
        bool separated_signal{false};
        int baseline_samples{0};
    };

    inline AppearanceEmissionNoiseModel appearance_emission_noise_model(
        const std::vector<double>& luminance_samples)
    {
        AppearanceEmissionNoiseModel out{};
        std::vector<double> finite_samples{};
        finite_samples.reserve(luminance_samples.size());
        for (const auto sample : luminance_samples)
        {
            if (std::isfinite(sample))
            {
                finite_samples.push_back(std::max(0.0, sample));
            }
        }
        if (finite_samples.size() < 3)
        {
            return out;
        }
        const auto median_of = [](std::vector<double> values) {
            std::sort(values.begin(), values.end());
            const auto middle = values.size() / 2;
            if ((values.size() % 2) == 0)
            {
                return (values[middle - 1] + values[middle]) * 0.5;
            }
            return values[middle];
        };
        out.median = median_of(finite_samples);
        std::vector<double> deviations{};
        deviations.reserve(finite_samples.size());
        for (const auto sample : finite_samples)
        {
            deviations.push_back(std::abs(sample - out.median));
        }
        out.mad = median_of(std::move(deviations));
        // The model describes an E=0 target baseline.  Its median is the
        // capture/backend bias, while max(0.01, 8*MAD) is the minimum positive
        // response required above that bias.  Taking max around the complete
        // expression made a biased but stable E=0 capture too permissive.
        out.threshold = out.median + std::max(0.01, 8.0 * out.mad);
        out.ok = std::isfinite(out.threshold);
        out.baseline_samples =
            static_cast<int>(finite_samples.size());
        return out;
    }

    inline AppearanceEmissionNoiseModel
    appearance_source_emission_noise_model(
        const std::vector<double>& luminance_samples)
    {
        auto full =
            appearance_emission_noise_model(luminance_samples);
        std::vector<double> ordered{};
        ordered.reserve(luminance_samples.size());
        for (const auto sample : luminance_samples)
        {
            if (std::isfinite(sample))
            {
                ordered.push_back(std::max(0.0, sample));
            }
        }
        if (!full.ok || ordered.size() < 20)
        {
            return full;
        }
        std::sort(ordered.begin(), ordered.end());
        const auto minimum_baseline =
            std::max<std::size_t>(8, ordered.size() / 20);
        const auto minimum_signal =
            std::max<std::size_t>(8, ordered.size() / 10);
        const auto maximum_split =
            std::min(
                ordered.size() / 2,
                ordered.size() - minimum_signal);
        if (minimum_baseline > maximum_split)
        {
            return full;
        }

        std::size_t best_split = 0;
        double best_gap = 0.0;
        for (std::size_t split = minimum_baseline;
             split <= maximum_split;
             ++split)
        {
            const auto gap =
                ordered[split] - ordered[split - 1];
            if (gap > best_gap)
            {
                best_gap = gap;
                best_split = split;
            }
        }
        if (best_split == 0 || best_gap < 0.02)
        {
            return full;
        }

        std::vector<double> baseline(
            ordered.begin(),
            ordered.begin() +
                static_cast<std::ptrdiff_t>(best_split));
        auto separated =
            appearance_emission_noise_model(baseline);
        if (!separated.ok ||
            best_gap <
                std::max(0.02, 8.0 * separated.mad) ||
            ordered[best_split] <=
                separated.threshold + 0.02)
        {
            return full;
        }
        separated.separated_signal = true;
        separated.baseline_samples =
            static_cast<int>(best_split);
        return separated;
    }

    inline AppearanceEmissionNoiseModel
    appearance_combine_emission_noise_models(
        const AppearanceEmissionNoiseModel& source,
        const AppearanceEmissionNoiseModel& target_e0)
    {
        AppearanceEmissionNoiseModel out = source;
        if (!source.ok || !target_e0.ok ||
            !std::isfinite(source.threshold) ||
            !std::isfinite(target_e0.threshold))
        {
            out.ok = false;
            return out;
        }
        // The source residual decides whether a source pixel is intrinsically
        // emissive.  Target E=0 measures the paint material/backend floor.
        // Both constraints must hold; replacing the source floor with the
        // usually lower target floor turns lighting/base-pass residuals into
        // broad false-positive emission.
        out.threshold =
            std::max(source.threshold, target_e0.threshold);
        out.ok = std::isfinite(out.threshold);
        return out;
    }

    inline bool appearance_emission_sample_detected(
        double residual_luminance,
        const AppearanceEmissionNoiseModel& e0_noise)
    {
        return e0_noise.ok &&
               std::isfinite(residual_luminance) &&
               residual_luminance > e0_noise.threshold;
    }

    struct AppearanceEmissionGridPoint
    {
        int x{0};
        int y{0};
    };

    struct AppearanceEmissionSpatialFilter
    {
        std::vector<bool> keep{};
        int region_count{0};
        int kept_samples{0};
        int rejected_samples{0};
    };

    inline AppearanceEmissionSpatialFilter
    appearance_filter_emission_regions(
        const std::vector<AppearanceEmissionGridPoint>& points,
        int minimum_region_samples)
    {
        AppearanceEmissionSpatialFilter out{};
        out.keep.assign(points.size(), false);
        const auto minimum =
            std::max(1, minimum_region_samples);
        std::vector<bool> visited(points.size(), false);
        std::vector<std::size_t> stack{};
        std::vector<std::size_t> component{};
        for (std::size_t first = 0;
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
                for (std::size_t candidate = 0;
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
            if (static_cast<int>(component.size()) >=
                minimum)
            {
                ++out.region_count;
                for (const auto index : component)
                {
                    out.keep[index] = true;
                }
                out.kept_samples +=
                    static_cast<int>(component.size());
            }
            else
            {
                out.rejected_samples +=
                    static_cast<int>(component.size());
            }
        }
        return out;
    }

    struct AppearanceEmissionSurfacePoint
    {
        AppearanceEmissionGridPoint screen{};
        double residual_luminance{0.0};
        std::uint64_t surface_key{0};
    };

    struct AppearanceEmissionSurfaceFilter
    {
        std::vector<bool> keep{};
        int applied_regions{0};
        int core_samples{0};
        int core_surface_count{0};
        int kept_samples{0};
        int halo_rejected_samples{0};
        double maximum_core_threshold{0.0};
    };

    inline AppearanceEmissionSurfaceFilter
    appearance_filter_emission_surface_halo(
        const std::vector<AppearanceEmissionSurfacePoint>& points)
    {
        AppearanceEmissionSurfaceFilter out{};
        out.keep.assign(points.size(), true);
        out.kept_samples = static_cast<int>(points.size());
        if (points.size() < 6)
        {
            return out;
        }

        std::vector<bool> visited(points.size(), false);
        std::vector<std::size_t> stack{};
        std::vector<std::size_t> component{};
        for (std::size_t first = 0;
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
                for (std::size_t candidate = 0;
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
            if (component.size() < 6)
            {
                continue;
            }

            std::vector<double> luminance{};
            luminance.reserve(component.size());
            for (const auto index : component)
            {
                if (std::isfinite(
                        points[index].residual_luminance))
                {
                    luminance.push_back(
                        std::max(
                            0.0,
                            points[index]
                                .residual_luminance));
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

            std::vector<std::size_t> core{};
            std::set<std::uint64_t> core_surfaces{};
            std::set<std::uint64_t> all_surfaces{};
            for (const auto index : component)
            {
                const auto key = points[index].surface_key;
                if (key != 0)
                {
                    all_surfaces.insert(key);
                }
                if (key != 0 &&
                    points[index].residual_luminance >
                        core_model.threshold)
                {
                    core.push_back(index);
                    core_surfaces.insert(key);
                }
            }
            // This is only a conservative correction for a small, strong
            // emitter embedded in a much larger, weaker response. Uniform
            // emissive surfaces and disconnected weak emitters must survive.
            if (core.size() < 3 ||
                core.size() * 4 >
                    component.size() ||
                core_surfaces.empty() ||
                core_surfaces.size() >=
                    all_surfaces.size())
            {
                continue;
            }

            std::vector<std::size_t> rejected{};
            rejected.reserve(component.size());
            double minimum_core =
                std::numeric_limits<double>::infinity();
            double maximum_rejected = 0.0;
            for (const auto index : core)
            {
                minimum_core =
                    std::min(
                        minimum_core,
                        points[index]
                            .residual_luminance);
            }
            for (const auto index : component)
            {
                const auto key = points[index].surface_key;
                if (key == 0 ||
                    core_surfaces.find(key) !=
                        core_surfaces.end())
                {
                    continue;
                }
                rejected.push_back(index);
                maximum_rejected =
                    std::max(
                        maximum_rejected,
                        points[index]
                            .residual_luminance);
            }
            const auto minimum_separation =
                std::max(0.02, 2.0 * core_model.mad);
            if (rejected.size() * 2 <
                    component.size() ||
                !std::isfinite(minimum_core) ||
                minimum_core - maximum_rejected <
                    minimum_separation)
            {
                continue;
            }

            ++out.applied_regions;
            out.core_samples +=
                static_cast<int>(core.size());
            out.core_surface_count +=
                static_cast<int>(core_surfaces.size());
            out.maximum_core_threshold =
                std::max(
                    out.maximum_core_threshold,
                    core_model.threshold);
            for (const auto index : rejected)
            {
                out.keep[index] = false;
            }
            out.halo_rejected_samples +=
                static_cast<int>(rejected.size());
            out.kept_samples -=
                static_cast<int>(rejected.size());
        }
        return out;
    }

    inline double appearance_emission_projected_value(
        double projected_emissive,
        bool intrinsic_emission_roi)
    {
        if (!intrinsic_emission_roi ||
            !std::isfinite(projected_emissive))
        {
            return 0.0;
        }
        // Intrinsic isolation is the classification authority.  A projection
        // can legitimately land at zero when target E0 lighting is already
        // brighter than the source final colour, but dropping packed B then
        // destroys a confirmed emission ROI. Preserve the smallest 8-bit
        // representable positive value and let ROI loss acceptance decide
        // whether the candidate is safe.
        return std::clamp(
            std::max(projected_emissive, 1.0 / 255.0),
            0.0,
            1.0);
    }

    struct AppearanceEmissionRoiAcceptance
    {
        int emission_roi_samples{0};
        int emission_roi_nonzero_b_samples{0};
        int non_emission_samples{0};
        int non_emission_nonzero_b_samples{0};
        bool camera_stable{false};
        bool readback_calibrated{false};
        bool packed_b_verified{false};
        double emission_roi_loss_initial{
            std::numeric_limits<double>::infinity()};
        double emission_roi_loss_best{
            std::numeric_limits<double>::infinity()};
        double non_emission_loss_initial{
            std::numeric_limits<double>::infinity()};
        double non_emission_loss_best{
            std::numeric_limits<double>::infinity()};
    };

    inline bool appearance_emission_roi_accepted(
        const AppearanceEmissionRoiAcceptance& value)
    {
        const auto recall =
            value.emission_roi_samples > 0
                ? static_cast<double>(
                      value.emission_roi_nonzero_b_samples) /
                      static_cast<double>(value.emission_roi_samples)
                : 0.0;
        const auto false_positive_rate =
            value.non_emission_samples > 0
                ? static_cast<double>(
                      value.non_emission_nonzero_b_samples) /
                      static_cast<double>(value.non_emission_samples)
                : 0.0;
        const auto roi_improvement =
            std::isfinite(value.emission_roi_loss_initial) &&
                    std::isfinite(value.emission_roi_loss_best) &&
                    value.emission_roi_loss_initial > 0.0
                ? (value.emission_roi_loss_initial -
                   value.emission_roi_loss_best) /
                      value.emission_roi_loss_initial
                : -std::numeric_limits<double>::infinity();
        const auto non_emission_delta =
            value.non_emission_loss_best -
            value.non_emission_loss_initial;
        const bool non_emission_stable =
            value.non_emission_samples == 0 ||
            (std::isfinite(non_emission_delta) &&
             non_emission_delta <= 0.01);
        return value.emission_roi_samples > 0 &&
               value.non_emission_samples >= 0 &&
               value.camera_stable &&
               value.readback_calibrated &&
               value.packed_b_verified &&
               std::isfinite(recall) &&
               recall >= 0.90 &&
               std::isfinite(false_positive_rate) &&
               false_positive_rate <= 0.01 &&
               std::isfinite(roi_improvement) &&
               roi_improvement >= AppearanceFitMinimumImprovement &&
               non_emission_stable;
    }

    struct AppearanceEmissiveCalibration
    {
        bool supported{false};
        double emissive{0.0};
        double response_energy{0.0};
    };

    inline AppearanceEmissiveCalibration appearance_calibrate_emissive(
        const AppearanceRgb& source_hdr,
        const AppearanceRgb& fallback_hdr,
        const AppearanceRgb& endpoint_hdr)
    {
        AppearanceEmissiveCalibration out{};
        if (!appearance_rgb_finite(source_hdr) ||
            !appearance_rgb_finite(fallback_hdr) ||
            !appearance_rgb_finite(endpoint_hdr))
        {
            return out;
        }
        const auto log_channel = [](double channel) {
            return std::log1p(std::max(0.0, channel));
        };
        const AppearanceRgb source{
            log_channel(source_hdr.r),
            log_channel(source_hdr.g),
            log_channel(source_hdr.b)};
        const AppearanceRgb fallback{
            log_channel(fallback_hdr.r),
            log_channel(fallback_hdr.g),
            log_channel(fallback_hdr.b)};
        const AppearanceRgb endpoint{
            log_channel(endpoint_hdr.r),
            log_channel(endpoint_hdr.g),
            log_channel(endpoint_hdr.b)};
        const AppearanceRgb response{
            endpoint.r - fallback.r,
            endpoint.g - fallback.g,
            endpoint.b - fallback.b};
        const AppearanceRgb residual{
            source.r - fallback.r,
            source.g - fallback.g,
            source.b - fallback.b};
        out.response_energy =
            response.r * response.r +
            response.g * response.g +
            response.b * response.b;
        const auto endpoint_luminance = appearance_luminance(endpoint_hdr);
        const auto fallback_luminance = appearance_luminance(fallback_hdr);
        if (!std::isfinite(out.response_energy) ||
            out.response_energy <= 0.00000001 ||
            !std::isfinite(endpoint_luminance) ||
            !std::isfinite(fallback_luminance) ||
            endpoint_luminance <= fallback_luminance + 0.0001)
        {
            return out;
        }
        const auto projected =
            (residual.r * response.r +
             residual.g * response.g +
             residual.b * response.b) /
            out.response_energy;
        if (!std::isfinite(projected))
        {
            return out;
        }
        out.supported = true;
        out.emissive = std::clamp(projected, 0.0, 1.0);
        return out;
    }

    struct AppearanceBoundedResponseCalibration
    {
        bool supported{false};
        double parameter{0.0};
        double response_energy{0.0};
    };

    inline AppearanceBoundedResponseCalibration
    appearance_calibrate_bounded_response(
        const AppearanceRgb& source_hdr,
        const AppearanceRgb& baseline_hdr,
        const AppearanceRgb& endpoint_hdr,
        double baseline_parameter,
        double endpoint_parameter,
        double minimum_parameter,
        double maximum_parameter)
    {
        AppearanceBoundedResponseCalibration out{};
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
            return out;
        }
        const auto log_channel = [](double channel) {
            return std::log1p(std::max(0.0, channel));
        };
        const AppearanceRgb source{
            log_channel(source_hdr.r),
            log_channel(source_hdr.g),
            log_channel(source_hdr.b)};
        const AppearanceRgb baseline{
            log_channel(baseline_hdr.r),
            log_channel(baseline_hdr.g),
            log_channel(baseline_hdr.b)};
        const AppearanceRgb endpoint{
            log_channel(endpoint_hdr.r),
            log_channel(endpoint_hdr.g),
            log_channel(endpoint_hdr.b)};
        const AppearanceRgb response{
            endpoint.r - baseline.r,
            endpoint.g - baseline.g,
            endpoint.b - baseline.b};
        const AppearanceRgb residual{
            source.r - baseline.r,
            source.g - baseline.g,
            source.b - baseline.b};
        out.response_energy =
            response.r * response.r +
            response.g * response.g +
            response.b * response.b;
        if (!std::isfinite(out.response_energy) ||
            out.response_energy <= 0.00000001)
        {
            return out;
        }
        const auto projected =
            (residual.r * response.r +
             residual.g * response.g +
             residual.b * response.b) /
            out.response_energy;
        if (!std::isfinite(projected))
        {
            return out;
        }
        out.supported = true;
        out.parameter = std::clamp(
            baseline_parameter +
                (endpoint_parameter - baseline_parameter) * projected,
            minimum_parameter,
            maximum_parameter);
        return out;
    }

    inline AppearanceBoundedResponseCalibration
    appearance_calibrate_albedo_blend(
        const AppearanceRgb& source_hdr,
        const AppearanceRgb& display_albedo_capture_hdr,
        const AppearanceRgb& base_albedo_capture_hdr)
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

    struct AppearanceAlbedoRgbCalibration
    {
        bool supported{false};
        int responsive_channels{0};
        AppearanceRgb albedo{};
        std::array<bool, 3> channel_supported{{false, false, false}};
    };

    inline AppearanceAlbedoRgbCalibration
    appearance_calibrate_albedo_chromaticity(
        const AppearanceRgb& base_albedo,
        const AppearanceRgb& source_hdr,
        const AppearanceRgb& base_albedo_capture_hdr)
    {
        AppearanceAlbedoRgbCalibration out{};
        out.albedo = appearance_clamp_albedo(base_albedo);
        if (!appearance_rgb_finite(base_albedo) ||
            !appearance_rgb_finite(source_hdr) ||
            !appearance_rgb_finite(base_albedo_capture_hdr))
        {
            return out;
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
            source_sum <= 0.001 ||
            target_sum <= 0.001 ||
            base_luminance <= 0.00001)
        {
            return out;
        }

        const auto calibrate_channel = [](
                                           double base,
                                           double source_chroma,
                                           double target_chroma,
                                           double& calibrated) {
            if (!std::isfinite(source_chroma) ||
                !std::isfinite(target_chroma) ||
                target_chroma <= 0.005)
            {
                return false;
            }
            calibrated =
                base * source_chroma / target_chroma;
            return std::isfinite(calibrated);
        };

        out.channel_supported[0] = calibrate_channel(
            base_albedo.r,
            source_hdr.r / source_sum,
            base_albedo_capture_hdr.r / target_sum,
            out.albedo.r);
        out.channel_supported[1] = calibrate_channel(
            base_albedo.g,
            source_hdr.g / source_sum,
            base_albedo_capture_hdr.g / target_sum,
            out.albedo.g);
        out.channel_supported[2] = calibrate_channel(
            base_albedo.b,
            source_hdr.b / source_sum,
            base_albedo_capture_hdr.b / target_sum,
            out.albedo.b);
        out.responsive_channels =
            static_cast<int>(out.channel_supported[0]) +
            static_cast<int>(out.channel_supported[1]) +
            static_cast<int>(out.channel_supported[2]);
        out.supported = out.responsive_channels >= 2;
        const auto calibrated_luminance =
            appearance_luminance(out.albedo);
        if (!out.supported ||
            !std::isfinite(calibrated_luminance) ||
            calibrated_luminance <= 0.00001)
        {
            out.albedo = appearance_clamp_albedo(base_albedo);
            out.supported = false;
            return out;
        }
        const auto preserve_luminance =
            base_luminance / calibrated_luminance;
        out.albedo = appearance_clamp_albedo(
            {out.albedo.r * preserve_luminance,
             out.albedo.g * preserve_luminance,
             out.albedo.b * preserve_luminance});
        return out;
    }

    struct AppearanceAlbedoCandidateAcceptance
    {
        int paired_samples{0};
        bool camera_stable{false};
        bool readback_calibrated{false};
        double baseline_loss{
            std::numeric_limits<double>::infinity()};
        double candidate_loss{
            std::numeric_limits<double>::infinity()};
        double baseline_median_delta_e{
            std::numeric_limits<double>::infinity()};
        double candidate_median_delta_e{
            std::numeric_limits<double>::infinity()};
    };

    inline bool appearance_albedo_candidate_accepted(
        const AppearanceAlbedoCandidateAcceptance& value)
    {
        return value.paired_samples >=
                   AppearanceFitMinimumSamples &&
               value.camera_stable &&
               value.readback_calibrated &&
               std::isfinite(value.baseline_loss) &&
               std::isfinite(value.candidate_loss) &&
               std::isfinite(
                   value.baseline_median_delta_e) &&
               std::isfinite(
                   value.candidate_median_delta_e) &&
               value.baseline_loss > 0.0 &&
               value.candidate_loss <
                   value.baseline_loss &&
               value.candidate_median_delta_e <=
                   value.baseline_median_delta_e;
    }

    struct AppearanceAlbedoChromaticityGain
    {
        bool supported{false};
        int responsive_channels{0};
        AppearanceRgb gain{1.0, 1.0, 1.0};
    };

    inline AppearanceAlbedoChromaticityGain
    appearance_robust_albedo_chromaticity_gain(
        const std::array<std::vector<double>, 3>&
            log_gain_estimates)
    {
        AppearanceAlbedoChromaticityGain out{};
        std::array<double, 3> gains{{1.0, 1.0, 1.0}};
        for (std::size_t channel = 0;
             channel < log_gain_estimates.size();
             ++channel)
        {
            std::vector<double> finite{};
            finite.reserve(
                log_gain_estimates[channel].size());
            for (const auto estimate :
                 log_gain_estimates[channel])
            {
                if (std::isfinite(estimate))
                {
                    finite.push_back(estimate);
                }
            }
            if (finite.size() <
                static_cast<std::size_t>(
                    AppearanceClusterEmissiveMinimumSamples))
            {
                continue;
            }
            const auto middle =
                finite.begin() +
                static_cast<std::ptrdiff_t>(
                    finite.size() / 2U);
            std::nth_element(
                finite.begin(),
                middle,
                finite.end());
            const auto gain = std::exp(*middle);
            if (!std::isfinite(gain) || gain <= 0.0)
            {
                continue;
            }
            gains[channel] = gain;
            ++out.responsive_channels;
        }
        out.supported = out.responsive_channels >= 2;
        if (out.supported)
        {
            out.gain = {gains[0], gains[1], gains[2]};
        }
        return out;
    }

    inline AppearanceRgb
    appearance_apply_albedo_chromaticity_gain(
        const AppearanceRgb& base_albedo,
        const AppearanceRgb& gain)
    {
        const auto base =
            appearance_clamp_albedo(base_albedo);
        if (!appearance_rgb_finite(base) ||
            !appearance_rgb_finite(gain) ||
            gain.r <= 0.0 ||
            gain.g <= 0.0 ||
            gain.b <= 0.0)
        {
            return base;
        }
        AppearanceRgb adjusted{
            base.r * gain.r,
            base.g * gain.g,
            base.b * gain.b};
        const auto base_luminance =
            appearance_luminance(base);
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
            {adjusted.r * preserve_luminance,
             adjusted.g * preserve_luminance,
             adjusted.b * preserve_luminance});
    }

    struct AppearanceClusterEmissiveEvidence
    {
        int paired_samples{0};
        int responsive_samples{0};
        double source_seed{0.0};
        double fallback_loss{std::numeric_limits<double>::infinity()};
        double endpoint_loss{std::numeric_limits<double>::infinity()};
    };

    inline bool appearance_cluster_emissive_supported(
        const AppearanceClusterEmissiveEvidence& value)
    {
        if (value.paired_samples < AppearanceClusterEmissiveMinimumSamples ||
            value.responsive_samples < AppearanceClusterEmissiveMinimumSamples ||
            !std::isfinite(value.source_seed) ||
            !std::isfinite(value.fallback_loss) ||
            !std::isfinite(value.endpoint_loss) ||
            value.fallback_loss <= 0.0)
        {
            return false;
        }
        const auto coverage =
            static_cast<double>(value.responsive_samples) /
            static_cast<double>(value.paired_samples);
        // E=1 is a response endpoint, not an expected solution. A valid
        // emissive material can easily overshoot the requested appearance at
        // that endpoint and therefore have a worse loss than E=0. Positive
        // per-sample response is established before this helper is called;
        // final calibrated candidates still have to pass the strict global
        // loss/DeltaE acceptance gate.
        return coverage >= AppearanceClusterEmissiveMinimumCoverage &&
               value.source_seed >= AppearanceClusterEmissiveMinimumSourceSeed;
    }

    struct AppearanceCalibratedClusterEmissiveEvidence
    {
        int paired_samples{0};
        int responsive_samples{0};
        double calibrated_emissive{0.0};
        double fallback_loss{std::numeric_limits<double>::infinity()};
        double calibrated_loss{std::numeric_limits<double>::infinity()};
        int intrinsic_core_samples{0};
    };

    inline bool appearance_emission_material_active(
        bool intrinsic_emission_roi,
        double emissive)
    {
        return intrinsic_emission_roi &&
               std::isfinite(emissive) &&
               std::llround(
                   std::clamp(emissive, 0.0, 1.0) *
                   255.0) > 0;
    }

    inline bool appearance_calibrated_cluster_emissive_supported(
        const AppearanceCalibratedClusterEmissiveEvidence& value)
    {
        if (value.paired_samples <
                AppearanceClusterEmissiveMinimumSamples ||
            value.responsive_samples <
                AppearanceClusterEmissiveMinimumSamples ||
            !appearance_emission_material_active(
                true,
                value.calibrated_emissive) ||
            !std::isfinite(value.fallback_loss) ||
            !std::isfinite(value.calibrated_loss) ||
            value.fallback_loss <= 0.0)
        {
            return false;
        }
        const auto coverage =
            static_cast<double>(value.responsive_samples) /
            static_cast<double>(value.paired_samples);
        const auto improvement =
            (value.fallback_loss - value.calibrated_loss) /
            value.fallback_loss;
        if (coverage >=
                AppearanceClusterEmissiveMinimumCoverage &&
            improvement >=
                AppearanceClusterCandidateMinimumImprovement)
        {
            return true;
        }
        const auto intrinsic_core_coverage =
            static_cast<double>(
                std::max(0, value.intrinsic_core_samples)) /
            static_cast<double>(value.paired_samples);
        // A compact source emitter can already match E=0 from Albedo alone,
        // making calibrated E nearly loss-neutral. Preserve its physical
        // emissive channel only when the isolated capture and target E
        // response independently cover most of the cluster. This does not
        // relax the gate for sparse light spill or reflection samples.
        return coverage >=
                   AppearanceClusterCalibratedMinimumCoverage &&
               intrinsic_core_coverage >=
                   AppearanceClusterIntrinsicCoreMinimumCoverage &&
               improvement >=
                   -AppearanceClusterNearNeutralMaximumLossIncrease;
    }

    enum class AppearanceClusterCandidate
    {
        Fallback,
        Endpoint,
        SourceSeed,
    };

    struct AppearanceClusterCandidateEvidence
    {
        int paired_samples{0};
        int source_candidate_samples{0};
        double source_seed{0.0};
        double fallback_loss{std::numeric_limits<double>::infinity()};
        double endpoint_loss{std::numeric_limits<double>::infinity()};
        double source_seed_loss{std::numeric_limits<double>::infinity()};
    };

    inline AppearanceClusterCandidate appearance_select_cluster_candidate(
        const AppearanceClusterCandidateEvidence& value)
    {
        if (value.paired_samples < AppearanceClusterEmissiveMinimumSamples ||
            value.source_candidate_samples < AppearanceClusterEmissiveMinimumSamples ||
            !std::isfinite(value.source_seed) ||
            !std::isfinite(value.fallback_loss) ||
            !std::isfinite(value.endpoint_loss) ||
            !std::isfinite(value.source_seed_loss) ||
            value.fallback_loss <= 0.0 ||
            value.source_seed < AppearanceClusterEmissiveMinimumSourceSeed)
        {
            return AppearanceClusterCandidate::Fallback;
        }
        const auto coverage =
            static_cast<double>(value.source_candidate_samples) /
            static_cast<double>(value.paired_samples);
        if (coverage < AppearanceClusterSourceCandidateMinimumCoverage)
        {
            return AppearanceClusterCandidate::Fallback;
        }
        const auto endpoint_improvement =
            (value.fallback_loss - value.endpoint_loss) /
            value.fallback_loss;
        const auto seed_improvement =
            (value.fallback_loss - value.source_seed_loss) /
            value.fallback_loss;
        if (endpoint_improvement < AppearanceClusterCandidateMinimumImprovement &&
            seed_improvement < AppearanceClusterCandidateMinimumImprovement)
        {
            return AppearanceClusterCandidate::Fallback;
        }
        return endpoint_improvement > seed_improvement
                   ? AppearanceClusterCandidate::Endpoint
                   : AppearanceClusterCandidate::SourceSeed;
    }

    struct AppearanceCalibratedEmissiveAcceptance
    {
        int paired_samples{0};
        int responsive_samples{0};
        int active_clusters{0};
        bool camera_stable{false};
        bool readback_calibrated{false};
        double fallback_loss{std::numeric_limits<double>::infinity()};
        double calibrated_loss{std::numeric_limits<double>::infinity()};
        double fallback_median_delta_e{std::numeric_limits<double>::infinity()};
        double calibrated_median_delta_e{std::numeric_limits<double>::infinity()};
        double emissive_max{0.0};
    };

    inline bool appearance_calibrated_emissive_accepted(
        const AppearanceCalibratedEmissiveAcceptance& value)
    {
        const auto improvement =
            std::isfinite(value.fallback_loss) &&
                    std::isfinite(value.calibrated_loss) &&
                    value.fallback_loss > 0.0
                ? (value.fallback_loss - value.calibrated_loss) /
                      value.fallback_loss
                : -std::numeric_limits<double>::infinity();
        return value.paired_samples >= AppearanceFitMinimumSamples &&
               value.responsive_samples >= AppearanceClusterEmissiveMinimumSamples &&
               value.active_clusters > 0 &&
               value.camera_stable &&
               value.readback_calibrated &&
               std::isfinite(value.fallback_loss) &&
               std::isfinite(value.calibrated_loss) &&
               std::isfinite(value.fallback_median_delta_e) &&
               std::isfinite(value.calibrated_median_delta_e) &&
               value.fallback_loss > 0.0 &&
               improvement >= AppearanceFitMinimumImprovement &&
               value.calibrated_median_delta_e <=
                   AppearanceFitMedianDeltaEMaximum &&
               value.emissive_max >= AppearanceCalibratedEmissiveMinimumMaximum;
    }

    using AppearanceClusterLocalAcceptance =
        AppearanceCalibratedEmissiveAcceptance;

    inline bool appearance_cluster_local_candidate_accepted(
        const AppearanceClusterLocalAcceptance& value)
    {
        return appearance_calibrated_emissive_accepted(value);
    }

    inline double appearance_max_channel_delta(const AppearanceRgb& left,
                                               const AppearanceRgb& right)
    {
        return std::max({std::abs(left.r - right.r),
                         std::abs(left.g - right.g),
                         std::abs(left.b - right.b)});
    }

    inline AppearanceRgb appearance_decode_r10g10b10a2(
        std::uint32_t packed)
    {
        return {
            static_cast<double>(packed & 0x3ffu) / 1023.0,
            static_cast<double>((packed >> 10u) & 0x3ffu) / 1023.0,
            static_cast<double>((packed >> 20u) & 0x3ffu) / 1023.0};
    }

    inline AppearanceRgb appearance_decode_rgba8(
        std::uint32_t packed,
        bool bgra)
    {
        const auto c0 = static_cast<double>(packed & 0xffu) / 255.0;
        const auto c1 =
            static_cast<double>((packed >> 8u) & 0xffu) / 255.0;
        const auto c2 =
            static_cast<double>((packed >> 16u) & 0xffu) / 255.0;
        return bgra ? AppearanceRgb{c2, c1, c0}
                    : AppearanceRgb{c0, c1, c2};
    }

    inline AppearanceRgb appearance_blend_albedo(const AppearanceRgb& base_linear,
                                                  const AppearanceRgb& display_linear,
                                                  double blend)
    {
        // Appearance Match may interpolate between the material BaseColor and
        // the observed display colour.  It must not extrapolate past that
        // observation: doing so made non-emissive surfaces visibly change
        // hue/value merely because the target lighting response differed.
        const auto safe_blend = std::clamp(blend, 0.0, 1.0);
        return appearance_clamp_albedo({base_linear.r + (display_linear.r - base_linear.r) * safe_blend,
                                        base_linear.g + (display_linear.g - base_linear.g) * safe_blend,
                                        base_linear.b + (display_linear.b - base_linear.b) * safe_blend});
    }

    inline AppearanceRgb appearance_source_albedo_target(
        const AppearanceRgb& base_linear,
        const AppearanceRgb& display_linear,
        const AppearanceRgb& emission_linear,
        bool emission_roi,
        bool include_shadows)
    {
        if (emission_roi && appearance_rgb_finite(emission_linear))
        {
            return appearance_clamp_albedo(emission_linear);
        }
        if (include_shadows && appearance_rgb_finite(display_linear))
        {
            return appearance_clamp_albedo(display_linear);
        }
        return appearance_clamp_albedo(base_linear);
    }

    inline AppearanceRgb appearance_source_display_or_base(
        const AppearanceRgb& base_linear,
        const AppearanceRgb& captured_display_linear,
        bool captured_display_available)
    {
        if (captured_display_available &&
            appearance_rgb_finite(captured_display_linear))
        {
            return appearance_clamp_albedo(
                captured_display_linear);
        }
        return appearance_clamp_albedo(base_linear);
    }

    inline AppearanceRgb appearance_parameterized_albedo(
        const AppearanceRgb& base_linear,
        const AppearanceRgb& target_linear,
        double parameter,
        bool emission_roi)
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
        // Emissive colour is carried by Albedo in RuntimePaintable.  Mixing
        // isolated emission chromaticity back toward a neutral BaseColor can
        // turn a coloured source light white.  Keep the RGB ratio fixed and
        // expose only a bounded intensity scale to feedback fitting.
        const auto scale =
            std::isfinite(parameter)
                ? std::clamp(parameter, 0.0, 1.0)
                : 0.0;
        return appearance_clamp_albedo(
            {target_linear.r * scale,
             target_linear.g * scale,
             target_linear.b * scale});
    }

    inline double appearance_initial_albedo_blend(const AppearanceRgb& base_linear,
                                                  const AppearanceRgb& display_linear)
    {
        // A 0.20 linear-channel residual has enough visual significance to
        // prefer the observed final colour over the source material colour.
        return std::clamp(appearance_max_channel_delta(base_linear, display_linear) / 0.20, 0.0, 1.0);
    }

    inline double appearance_initial_emissive(const AppearanceRgb& base_linear,
                                              const AppearanceRgb& final_hdr)
    {
        const auto base = std::max(0.0, appearance_luminance(base_linear));
        const auto final = std::max(0.0, appearance_luminance(final_hdr));
        const auto stops = std::log2(1.0 + final) - std::log2(1.0 + base);
        return std::clamp((stops - 0.15) / 2.0, 0.0, 1.0);
    }

    struct AppearanceMaterial
    {
        double albedo_blend{0.0};
        double metallic{0.0};
        double roughness{AppearanceFallbackRoughness};
        double emissive{0.0};
    };

    struct AppearanceFallback
    {
        AppearanceRgb albedo{};
        AppearanceMaterial material{};
    };

    inline AppearanceFallback appearance_make_fallback(const AppearanceRgb& display_linear)
    {
        AppearanceFallback out{};
        out.albedo = appearance_clamp_albedo(display_linear);
        out.material = {1.0, 0.0, AppearanceFallbackRoughness, 0.0};
        return out;
    }

    inline AppearanceFallback appearance_make_safe_fallback(
        const AppearanceRgb& base_linear,
        const AppearanceRgb& display_linear,
        bool base_available)
    {
        // A display-referred colour already contains the source scene's
        // lighting. Feeding it back as Albedo applies the target lighting a
        // second time and can rotate dark neutrals toward the light colour.
        // Prefer the intrinsic BaseColor whenever that capture is usable.
        const auto use_base =
            base_available &&
            appearance_rgb_finite(base_linear);
        auto out = appearance_make_fallback(
            use_base ? base_linear : display_linear);
        out.material.albedo_blend =
            use_base ? 0.0 : 1.0;
        return out;
    }

    inline AppearanceFallback appearance_make_safe_final_fallback(
        const AppearanceRgb& base_linear,
        const AppearanceRgb& display_linear,
        bool base_available,
        bool intrinsic_emission_roi)
    {
        // A rejected intrinsic-emission fit must retain the display-colour
        // baseline that was actually evaluated. Falling back to a dark
        // emitter BaseColor here produces an unobserved black result. Keep
        // Emissive disabled, while preserving the emitter's observed colour
        // in Albedo. Non-emission samples retain the lighting-safe BaseColor
        // fallback.
        return intrinsic_emission_roi
                   ? appearance_make_fallback(display_linear)
                   : appearance_make_safe_fallback(
                         base_linear,
                         display_linear,
                         base_available);
    }

    inline std::uint8_t appearance_quantize_unit(double value)
    {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    }

    inline AppearanceRgb appearance_manual_fixed_material_albedo(
        const AppearanceRgb& display_linear,
        const AppearanceRgb& current_albedo,
        bool intrinsic_emission_roi,
        double fixed_emissive)
    {
        // With Manual Emissive disabled, isolated source emission is only an
        // input classification signal. Using its chromaticity as Albedo makes
        // bright sources yellow, while the zero-blend response endpoint turns
        // them black. Keep those texels at the observed display colour for
        // every temporary feedback candidate.
        if (intrinsic_emission_roi &&
            std::isfinite(fixed_emissive) &&
            appearance_quantize_unit(fixed_emissive) == 0 &&
            appearance_rgb_finite(display_linear))
        {
            return appearance_clamp_albedo(display_linear);
        }
        return appearance_clamp_albedo(current_albedo);
    }

    inline std::uint64_t appearance_material_key(const AppearanceMaterial& material,
                                                 bool fallback)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](std::uint8_t value) {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ull;
        };
        mix(appearance_quantize_unit(material.metallic));
        mix(appearance_quantize_unit(material.roughness));
        mix(appearance_quantize_unit(material.emissive));
        mix(fallback ? 1u : 0u);
        return hash;
    }

    enum class AppearanceReadbackTransform
    {
        Identity,
        SwapRedBlue,
    };

    struct AppearanceReadbackCalibration
    {
        bool ok{false};
        AppearanceReadbackTransform transform{AppearanceReadbackTransform::Identity};
        double median_error{std::numeric_limits<double>::infinity()};
        double runner_up_median{std::numeric_limits<double>::infinity()};
    };

    inline AppearanceReadbackCalibration appearance_calibrate_linear_readback(
        const std::vector<AppearanceRgb>& expected,
        const std::vector<AppearanceRgb>& raw,
        double maximum_median_error = 0.04)
    {
        AppearanceReadbackCalibration out{};
        const auto count = std::min(expected.size(), raw.size());
        if (count < 16)
        {
            return out;
        }
        const auto median_for = [&](AppearanceReadbackTransform transform) {
            std::vector<double> errors{};
            errors.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                auto candidate = raw[index];
                if (transform == AppearanceReadbackTransform::SwapRedBlue)
                {
                    std::swap(candidate.r, candidate.b);
                }
                errors.push_back(appearance_max_channel_delta(expected[index], candidate));
            }
            const auto middle = errors.begin() + static_cast<std::ptrdiff_t>(errors.size() / 2);
            std::nth_element(errors.begin(), middle, errors.end());
            return *middle;
        };
        const auto identity = median_for(AppearanceReadbackTransform::Identity);
        const auto swapped = median_for(AppearanceReadbackTransform::SwapRedBlue);
        out.transform = swapped < identity ? AppearanceReadbackTransform::SwapRedBlue
                                           : AppearanceReadbackTransform::Identity;
        out.median_error = std::min(identity, swapped);
        out.runner_up_median = std::max(identity, swapped);
        const auto separated = out.runner_up_median >= out.median_error * 1.10 ||
                               out.runner_up_median - out.median_error >= 0.005;
        out.ok = std::isfinite(out.median_error) && out.median_error <= maximum_median_error && separated;
        return out;
    }

    struct AppearanceSpsaPair
    {
        std::vector<double> plus{};
        std::vector<double> minus{};
        std::vector<double> direction{};
    };

    inline double appearance_parameter_bound(std::size_t parameter_index)
    {
        (void)parameter_index;
        return 1.0;
    }

    inline std::vector<double>
    appearance_fixed_material_parameters(
        const std::vector<double>& source,
        double metallic,
        double roughness,
        double emissive)
    {
        if (source.size() % 4U != 0U)
        {
            return {};
        }
        auto out = source;
        const auto fixed_metallic =
            std::clamp(metallic, 0.0, 1.0);
        const auto fixed_roughness =
            std::clamp(roughness, 0.0, 1.0);
        const auto fixed_emissive =
            std::clamp(emissive, 0.0, 1.0);
        for (std::size_t offset = 0;
             offset < out.size();
             offset += 4U)
        {
            out[offset + 0U] =
                std::clamp(
                    out[offset + 0U],
                    0.0,
                    appearance_parameter_bound(offset));
            out[offset + 1U] = fixed_metallic;
            out[offset + 2U] = fixed_roughness;
            out[offset + 3U] = fixed_emissive;
        }
        return out;
    }

    inline std::uint64_t appearance_spsa_hash(std::uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ull;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31u);
    }

    inline AppearanceSpsaPair appearance_spsa_pair(const std::vector<double>& parameters,
                                                    int iteration,
                                                    std::uint64_t seed)
    {
        AppearanceSpsaPair out{};
        out.plus.resize(parameters.size());
        out.minus.resize(parameters.size());
        out.direction.resize(parameters.size());
        const double decay = std::pow(static_cast<double>(std::max(1, iteration + 1)), 0.101);
        constexpr std::array<double, 4> perturbation{{0.12, 0.12, 0.15, 0.15}};
        for (std::size_t index = 0; index < parameters.size(); ++index)
        {
            const auto random = appearance_spsa_hash(seed ^
                                                      (static_cast<std::uint64_t>(iteration + 1) << 32u) ^
                                                      static_cast<std::uint64_t>(index + 1));
            const double direction = (random & 1ull) == 0 ? -1.0 : 1.0;
            const double delta = perturbation[index % perturbation.size()] / decay;
            out.direction[index] = direction;
            out.plus[index] = std::clamp(parameters[index] + direction * delta,
                                         0.0,
                                         appearance_parameter_bound(index));
            out.minus[index] = std::clamp(parameters[index] - direction * delta,
                                          0.0,
                                          appearance_parameter_bound(index));
        }
        return out;
    }

    inline std::vector<double> appearance_spsa_update(const std::vector<double>& parameters,
                                                       const AppearanceSpsaPair& pair,
                                                       double loss_plus,
                                                       double loss_minus,
                                                       int iteration)
    {
        if (parameters.size() != pair.plus.size() ||
            parameters.size() != pair.minus.size() ||
            parameters.size() != pair.direction.size() ||
            !std::isfinite(loss_plus) || !std::isfinite(loss_minus))
        {
            return parameters;
        }
        std::vector<double> out(parameters);
        const double gain_decay = std::pow(static_cast<double>(std::max(1, iteration + 1)), 0.602);
        constexpr std::array<double, 4> gains{{0.18, 0.15, 0.18, 0.20}};
        constexpr std::array<double, 4> perturbation{{0.12, 0.12, 0.15, 0.15}};
        for (std::size_t index = 0; index < parameters.size(); ++index)
        {
            const double delta = perturbation[index % perturbation.size()] /
                                 std::pow(static_cast<double>(std::max(1, iteration + 1)), 0.101);
            if (delta <= 0.0 || !std::isfinite(delta) || pair.direction[index] == 0.0)
            {
                continue;
            }
            const double gradient = ((loss_plus - loss_minus) / (2.0 * delta)) * pair.direction[index];
            const double gain = gains[index % gains.size()] / gain_decay;
            out[index] = std::clamp(parameters[index] - gain * gradient,
                                    0.0,
                                    appearance_parameter_bound(index));
        }
        return out;
    }

    inline std::vector<double> appearance_spsa_update_by_cluster(
        const std::vector<double>& parameters,
        const AppearanceSpsaPair& pair,
        const std::vector<double>& loss_plus_by_cluster,
        const std::vector<double>& loss_minus_by_cluster,
        int iteration)
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
        std::vector<double> out(parameters);
        const double gain_decay =
            std::pow(static_cast<double>(std::max(1, iteration + 1)), 0.602);
        constexpr std::array<double, 4> gains{{0.18, 0.15, 0.18, 0.20}};
        constexpr std::array<double, 4> perturbation{{0.12, 0.12, 0.15, 0.15}};
        for (std::size_t index = 0; index < parameters.size(); ++index)
        {
            const auto cluster = index / 4U;
            const double loss_plus = loss_plus_by_cluster[cluster];
            const double loss_minus = loss_minus_by_cluster[cluster];
            const double delta =
                perturbation[index % perturbation.size()] /
                std::pow(
                    static_cast<double>(std::max(1, iteration + 1)),
                    0.101);
            if (!std::isfinite(loss_plus) ||
                !std::isfinite(loss_minus) ||
                delta <= 0.0 ||
                !std::isfinite(delta) ||
                pair.direction[index] == 0.0)
            {
                continue;
            }
            const double gradient =
                ((loss_plus - loss_minus) / (2.0 * delta)) *
                pair.direction[index];
            const double gain = gains[index % gains.size()] / gain_decay;
            out[index] = std::clamp(
                parameters[index] - gain * gradient,
                0.0,
                appearance_parameter_bound(index));
        }
        return out;
    }

    // SPSA evaluations are cancellable at a wall-clock deadline.  Record a
    // valid plus/minus candidate immediately so an expired later evaluation
    // cannot discard the best material tuple already observed.
    struct AppearanceBestCandidate
    {
        bool available{false};
        std::vector<double> parameters{};
        double loss{std::numeric_limits<double>::infinity()};
        // A candidate is more than its four cluster parameters.  Albedo
        // feedback and unsupported-sample fallback alter the actual preview
        // texture, so they must be replayed with the winning tuple.
        bool use_feedback_albedo{false};
        bool use_base_fallback{false};
    };

    inline bool appearance_keep_best_candidate(AppearanceBestCandidate& best,
                                                const std::vector<double>& parameters,
                                                double loss,
                                                bool use_feedback_albedo = false,
                                                bool use_base_fallback = false)
    {
        if (!std::isfinite(loss) || parameters.empty() ||
            (best.available && loss >= best.loss))
        {
            return false;
        }
        best.available = true;
        best.parameters = parameters;
        best.loss = loss;
        best.use_feedback_albedo = use_feedback_albedo;
        best.use_base_fallback = use_base_fallback;
        return true;
    }

    struct AppearanceFitAcceptance
    {
        int paired_samples{0};
        bool camera_stable{false};
        bool readback_calibrated{false};
        double fallback_loss{std::numeric_limits<double>::infinity()};
        double best_loss{std::numeric_limits<double>::infinity()};
        double median_delta_e{std::numeric_limits<double>::infinity()};
    };

    inline bool appearance_fit_accepted(const AppearanceFitAcceptance& value)
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
        const double improvement = (value.fallback_loss - value.best_loss) / value.fallback_loss;
        return improvement >= AppearanceFitMinimumImprovement &&
               value.median_delta_e <= AppearanceFitMedianDeltaEMaximum;
    }

    struct AppearanceNonEmissionCandidateAcceptance
    {
        int paired_samples{0};
        int emissive_nonzero_samples{0};
        bool camera_stable{false};
        bool readback_calibrated{false};
        bool packed_b_verified{false};
        double fallback_loss{std::numeric_limits<double>::infinity()};
        double candidate_loss{std::numeric_limits<double>::infinity()};
        double candidate_median_delta_e{
            std::numeric_limits<double>::infinity()};
        int emission_roi_samples{0};
        double emission_roi_loss_initial{
            std::numeric_limits<double>::infinity()};
        double emission_roi_loss_candidate{
            std::numeric_limits<double>::infinity()};
        double reference_max_chromaticity_delta{
            std::numeric_limits<double>::infinity()};
        double candidate_max_chromaticity_delta{
            std::numeric_limits<double>::infinity()};
    };

    inline bool appearance_non_emission_candidate_accepted(
        const AppearanceNonEmissionCandidateAcceptance& value)
    {
        const bool emission_roi_stable =
            value.emission_roi_samples <= 0 ||
            (std::isfinite(value.emission_roi_loss_initial) &&
             std::isfinite(value.emission_roi_loss_candidate) &&
             value.emission_roi_loss_candidate -
                     value.emission_roi_loss_initial <=
                 0.01);
        const bool chromaticity_tail_stable =
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
                   {value.paired_samples,
                    value.camera_stable,
                    value.readback_calibrated,
                    value.fallback_loss,
                    value.candidate_loss,
                    value.candidate_median_delta_e});
    }

    struct AppearancePreviewRefinementObservation
    {
        int emission_roi_samples{0};
        int preview_count{0};
        int minimum_preview_count{0};
        bool observations_valid{false};
        double fallback_emission_roi_loss{0.0};
        double best_emission_roi_loss{0.0};
    };

    inline bool appearance_preview_refinement_worthwhile(
        const AppearancePreviewRefinementObservation& value)
    {
        if (value.preview_count <
            value.minimum_preview_count)
        {
            return true;
        }
        if (value.emission_roi_samples <= 0)
        {
            // Non-emission fits use their existing global acceptance and may
            // still benefit from AMR refinement.
            return true;
        }
        if (!value.observations_valid ||
            !std::isfinite(
                value.fallback_emission_roi_loss) ||
            !std::isfinite(
                value.best_emission_roi_loss))
        {
            // Do not turn missing diagnostics into an optimisation shortcut.
            return true;
        }
        // Once the E=1 endpoint, bounded E projection and both Albedo
        // endpoints have all failed to beat E=0 inside the isolated ROI,
        // spending the remaining preview pair on AMR perturbations cannot
        // justify accepting emission. Keep the best observed candidate.
        return value.best_emission_roi_loss <
               value.fallback_emission_roi_loss;
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
                alignas(32) double result[4]{};
                _mm256_storeu_pd(result, vdiff2);
                return std::max(
                    result[0],
                    std::max(result[1], result[2]));
            }
#endif
            const double dr = left.r - right.r;
            const double dg = left.g - right.g;
            const double db = left.b - right.b;
            return std::max(
                dr * dr,
                std::max(dg * dg, db * db));
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
        const auto region_order = [](ReplayRegion region) {
            switch (region)
            {
            case ReplayRegion::Back:
                return 0;
            case ReplayRegion::Side:
                return 1;
            case ReplayRegion::Front:
                return 2;
            }
            return 3;
        };
        std::stable_sort(paint_entries.begin(), paint_entries.end(), [&](const auto& left, const auto& right) {
            const int left_region = region_order(left.replay.region);
            const int right_region = region_order(right.replay.region);
            if (left_region != right_region)
            {
                return left_region < right_region;
            }
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

    constexpr bool runtime_triangle_dynamic_fallback_allowed(
        bool profile_cache_ok,
        bool dynamic_cache_ok,
        bool image_paint_enabled,
        int expected_triangle_count,
        int dynamic_triangle_count)
    {
        return !profile_cache_ok &&
               dynamic_cache_ok &&
               !image_paint_enabled &&
               expected_triangle_count > 0 &&
               dynamic_triangle_count == expected_triangle_count;
    }

    constexpr bool event_watch_generation_active(bool enabled,
                                                 std::uint64_t current_generation,
                                                 std::uint64_t captured_generation)
    {
        return enabled && current_generation == captured_generation;
    }

    constexpr std::uint64_t EspHudCaptureStallMs = 1'000;
    constexpr std::uint64_t EspHudRebindMinIntervalMs = 2'000;

    enum class EspCaptureStatus
    {
        Disabled,
        Waiting,
        Active,
        Busy,
        Stalled,
    };

    constexpr EspCaptureStatus esp_capture_status(bool enabled,
                                                  bool has_capture,
                                                  std::uint64_t capture_age_ms,
                                                  bool paint_busy)
    {
        if (!enabled)
        {
            return EspCaptureStatus::Disabled;
        }
        if (!has_capture)
        {
            return EspCaptureStatus::Waiting;
        }
        if (capture_age_ms < EspHudCaptureStallMs)
        {
            return EspCaptureStatus::Active;
        }
        return paint_busy
                   ? EspCaptureStatus::Busy
                   : EspCaptureStatus::Stalled;
    }

    // A reflected UFunction is shared by every instance which dispatches that
    // callback. Lobby -> match travel replaces the HUD UObject, so its address
    // is intentionally not part of the durable callback identity.
    constexpr bool esp_hud_callback_matches(std::uintptr_t expected_function,
                                            std::uintptr_t actual_function)
    {
        return expected_function != 0 && expected_function == actual_function;
    }

    constexpr bool esp_hud_rebind_due(bool enabled,
                                      std::uint64_t present_callbacks,
                                      std::uint64_t last_capture_ms,
                                      std::uint64_t now_ms,
                                      std::uint64_t last_request_ms)
    {
        return enabled &&
               present_callbacks > 0 &&
               now_ms >= last_capture_ms &&
               now_ms - last_capture_ms >= EspHudCaptureStallMs &&
               now_ms >= last_request_ms &&
               now_ms - last_request_ms >= EspHudRebindMinIntervalMs;
    }

    constexpr auto select_active_world(std::uintptr_t viewport_world,
                                       bool viewport_world_valid,
                                       std::uintptr_t fallback_world,
                                       bool fallback_world_valid) -> std::uintptr_t
    {
        return viewport_world_valid
                   ? viewport_world
                   : fallback_world_valid ? fallback_world : 0;
    }

    constexpr auto esp_select_snapshot_world(std::uintptr_t viewport_world,
                                             bool viewport_world_valid,
                                             std::uintptr_t cached_world,
                                             bool cached_world_valid) -> std::uintptr_t
    {
        return select_active_world(viewport_world,
                                   viewport_world_valid,
                                   cached_world,
                                   cached_world_valid);
    }

    enum class PreviewSnapshotDisposition : int
    {
        Create = 0,
        Reuse = 1,
        Replace = 2,
    };

    constexpr auto preview_snapshot_disposition(bool snapshot_available,
                                                std::uintptr_t snapshot_component,
                                                std::uintptr_t current_component)
        -> PreviewSnapshotDisposition
    {
        if (!snapshot_available)
        {
            return PreviewSnapshotDisposition::Create;
        }
        return snapshot_component == current_component
                   ? PreviewSnapshotDisposition::Reuse
                   : PreviewSnapshotDisposition::Replace;
    }

    enum class EspRole : int
    {
        Unknown = 0,
        Hider = 1,
        Hunter = 2,
        Spectator = 3,
    };

    enum class EspScope : int
    {
        All = 0,
        Hider = 1,
        Hunter = 2,
    };

    enum class EspTargetPawnSource : int
    {
        PlayerArray = 0,
        RoleRoster = 1,
    };

    constexpr auto esp_active_roster_role(
        bool authoritative,
        std::uint8_t membership) -> EspRole
    {
        if (membership == 1u)
        {
            return EspRole::Hider;
        }
        if (membership == 2u)
        {
            return EspRole::Hunter;
        }
        return authoritative && membership == 0u
                   ? EspRole::Spectator
                   : EspRole::Unknown;
    }

    constexpr auto esp_select_target_pawn_source(
        EspRole roster_role,
        EspRole player_array_pawn_role,
        EspRole role_roster_pawn_role,
        bool same_player_state) -> EspTargetPawnSource
    {
        const bool active_roster_role =
            roster_role == EspRole::Hider ||
            roster_role == EspRole::Hunter;
        const bool current_is_not_avatar =
            player_array_pawn_role == EspRole::Unknown ||
            player_array_pawn_role == EspRole::Spectator;
        return same_player_state &&
                       active_roster_role &&
                       current_is_not_avatar &&
                       role_roster_pawn_role == roster_role
                   ? EspTargetPawnSource::RoleRoster
                   : EspTargetPawnSource::PlayerArray;
    }

    constexpr bool esp_should_refresh_avatar_directory(
        bool unresolved_active_avatar,
        bool world_changed,
        std::uint64_t now_ms,
        std::uint64_t last_refresh_ms,
        std::uint64_t refresh_interval_ms)
    {
        if (!unresolved_active_avatar)
        {
            return false;
        }
        return world_changed ||
               last_refresh_ms == 0 ||
               now_ms < last_refresh_ms ||
               now_ms - last_refresh_ms >= refresh_interval_ms;
    }

    constexpr bool esp_cached_avatar_binding_is_usable(
        bool verified_when_cached,
        bool same_world,
        bool player_still_present,
        bool candidate_live,
        EspRole expected_role,
        EspRole candidate_role)
    {
        return verified_when_cached &&
               same_world &&
               player_still_present &&
               candidate_live &&
               (expected_role == EspRole::Hider ||
                expected_role == EspRole::Hunter) &&
               candidate_role == expected_role;
    }

    constexpr auto esp_current_pawn_role(
        EspRole roster_role,
        EspRole current_pawn_role) -> EspRole
    {
        if (roster_role == EspRole::Spectator)
        {
            return EspRole::Spectator;
        }
        return current_pawn_role == EspRole::Unknown
                   ? roster_role
                   : current_pawn_role;
    }

    constexpr auto esp_resolve_target_role(
        EspRole roster_role,
        EspRole current_pawn_role) -> EspRole
    {
        return esp_current_pawn_role(
            roster_role,
            current_pawn_role);
    }

    constexpr bool esp_role_scope_matches(EspScope scope, EspRole role)
    {
        return role != EspRole::Spectator &&
               (scope == EspScope::All ||
               (scope == EspScope::Hider && role == EspRole::Hider) ||
               (scope == EspScope::Hunter && role == EspRole::Hunter));
    }

    struct EspPawnGeometryCapabilities
    {
        bool spectator{false};
        bool root_capsule{false};
        bool skeletal_mesh{false};
    };

    constexpr auto esp_pawn_geometry_capabilities(
        bool spectator,
        bool root_is_capsule,
        bool skeletal_mesh)
        -> EspPawnGeometryCapabilities
    {
        return {
            spectator,
            !spectator && root_is_capsule,
            !spectator && skeletal_mesh};
    }

    constexpr auto esp_role_color(EspRole role,
                                  std::uint32_t hider_color,
                                  std::uint32_t hunter_color) -> std::uint32_t
    {
        return role == EspRole::Hider
                   ? hider_color
                   : role == EspRole::Hunter ? hunter_color : 0xFFFFFFu;
    }

    constexpr bool esp_native_renderer_configuration_is_reusable(
        bool requested_enabled,
        bool same_configuration,
        bool renderer_enabled,
        bool renderer_unavailable)
    {
        return requested_enabled &&
               same_configuration &&
               renderer_enabled &&
               !renderer_unavailable;
    }

    struct EspScreenBounds
    {
        double left{0.0};
        double top{0.0};
        double right{0.0};
        double bottom{0.0};
    };

    constexpr auto esp_expand_screen_bounds(EspScreenBounds bounds,
                                            double horizontal_ratio,
                                            double vertical_ratio) -> EspScreenBounds
    {
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
        {
            return bounds;
        }
        const auto horizontal =
            horizontal_ratio > 0.0 ? (bounds.right - bounds.left) * horizontal_ratio : 0.0;
        const auto vertical =
            vertical_ratio > 0.0 ? (bounds.bottom - bounds.top) * vertical_ratio : 0.0;
        return {
            bounds.left - horizontal,
            bounds.top - vertical,
            bounds.right + horizontal,
            bounds.bottom + vertical};
    }

    constexpr bool esp_pose_array_header_usable(std::uintptr_t data,
                                                int count,
                                                int capacity,
                                                int required_bones)
    {
        return data != 0 &&
               required_bones > 0 &&
               count >= required_bones &&
               capacity >= count &&
               capacity <= 512;
    }

    template <std::size_t Size>
    constexpr auto esp_unique_queue_identity(
        const std::array<std::uintptr_t, Size>& identities)
        -> std::uintptr_t
    {
        std::uintptr_t selected{};
        for (const auto identity : identities)
        {
            if (identity == 0 || identity == selected)
            {
                continue;
            }
            if (selected != 0)
            {
                return 0;
            }
            selected = identity;
        }
        return selected;
    }

    constexpr auto esp_ascii_glyph_rows(wchar_t character)
        -> std::array<std::uint8_t, 7>
    {
        const wchar_t value =
            character >= L'a' && character <= L'z'
                ? static_cast<wchar_t>(character - L'a' + L'A')
                : character;
        switch (value)
        {
        case L'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case L'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
        case L'C': return {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F};
        case L'D': return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
        case L'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
        case L'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
        case L'G': return {0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F};
        case L'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case L'I': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
        case L'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
        case L'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case L'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
        case L'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
        case L'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
        case L'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case L'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
        case L'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
        case L'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
        case L'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
        case L'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case L'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case L'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
        case L'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
        case L'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
        case L'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
        case L'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
        case L'0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        case L'1': return {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F};
        case L'2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
        case L'3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
        case L'4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        case L'5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
        case L'6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        case L'7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case L'8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        case L'9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
        case L' ': return {};
        case L'-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        case L'_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
        case L'.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
        case L':': return {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
        case L'/': return {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10};
        case L'[': return {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E};
        case L']': return {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E};
        case L'?': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
        default: return esp_ascii_glyph_rows(L'?');
        }
    }

    constexpr double esp_projection_scale_from_sample(double center,
                                                      double raw_screen,
                                                      double engine_screen)
    {
        const auto raw_delta = raw_screen - center;
        if (raw_delta > -1.0 && raw_delta < 1.0)
        {
            return -1.0;
        }
        const auto scale = (engine_screen - center) / raw_delta;
        return scale >= 0.5 && scale <= 2.5 ? scale : -1.0;
    }

}
