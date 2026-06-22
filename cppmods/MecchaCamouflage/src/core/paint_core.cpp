#include "MecchaCamouflage/core/paint_core.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>

namespace MecchaCamouflage::Core
{
    namespace
    {
        struct ProjectionTexel
        {
            double r{0.0};
            double g{0.0};
            double b{0.0};
            double roughness{0.0};
            double metallic{0.0};
            double weight{0.0};
            int priority{0};
            bool floor_like{false};
        };

        auto clamp(double value, double min_value, double max_value) -> double
        {
            return std::max(min_value, std::min(max_value, value));
        }

        auto worker_count_for_items(std::size_t item_count) -> unsigned
        {
            const auto hardware = std::max(1U, std::thread::hardware_concurrency());
            const auto useful = item_count < 65536
                                    ? 1U
                                    : std::min<unsigned>(hardware, static_cast<unsigned>((item_count + 65535) / 65536));
            return std::max(1U, useful);
        }

        template <typename Fn>
        auto parallel_ranges(std::size_t item_count, Fn&& fn) -> void
        {
            const auto workers = worker_count_for_items(item_count);
            if (workers <= 1 || item_count == 0)
            {
                fn(0, item_count, 0);
                return;
            }

            std::vector<std::thread> threads{};
            threads.reserve(workers);
            for (unsigned worker = 0; worker < workers; ++worker)
            {
                const auto begin = (item_count * static_cast<std::size_t>(worker)) / static_cast<std::size_t>(workers);
                const auto end = (item_count * static_cast<std::size_t>(worker + 1)) / static_cast<std::size_t>(workers);
                threads.emplace_back([begin, end, worker, &fn]() {
                    fn(begin, end, worker);
                });
            }
            for (auto& thread : threads)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
        }

        auto projection_texel_painted(const ProjectionTexel& texel) -> bool
        {
            return texel.weight > 0.000001;
        }

        auto lower_ascii(std::string text) -> std::string
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        auto contains_token(const std::string& text, const char* token) -> bool
        {
            return token && text.find(token) != std::string::npos;
        }

        auto clamp_int(int value, int min_value, int max_value) -> int
        {
            return std::max(min_value, std::min(max_value, value));
        }

        auto positive_or(int value, int fallback) -> int
        {
            return value > 0 ? value : fallback;
        }

        auto safe_pixel_count(int width, int height) -> double
        {
            return static_cast<double>(std::max(1, width)) *
                   static_cast<double>(std::max(1, height));
        }

        auto splat_projection_texel(std::vector<ProjectionTexel>& texels,
                                    int width,
                                    int height,
                                    double u,
                                    double v,
                                    const Color& color,
                                    double weight,
                                    int priority,
                                    bool floor_like,
                                    int radius) -> void
        {
            if (width <= 0 || height <= 0 || texels.empty())
            {
                return;
            }

            const auto center_x = static_cast<int>(std::round(clamp(u, 0.0, 1.0) * static_cast<double>(width - 1)));
            const auto center_y = static_cast<int>(std::round(clamp(v, 0.0, 1.0) * static_cast<double>(height - 1)));
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    const auto x = center_x + dx;
                    const auto y = center_y + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height)
                    {
                        continue;
                    }
                    const auto dist_sq = static_cast<double>(dx * dx + dy * dy);
                    const auto radius_sq = static_cast<double>(std::max(1, radius) * std::max(1, radius));
                    const auto local_weight = radius <= 0 ? weight : weight * clamp(1.0 - dist_sq / (radius_sq + 1.0), 0.15, 1.0);
                    const auto index = static_cast<std::size_t>(y * width + x);
                    auto& texel = texels[index];
                    if (priority < texel.priority)
                    {
                        continue;
                    }
                    if (priority > texel.priority)
                    {
                        texel = ProjectionTexel{};
                        texel.priority = priority;
                    }
                    texel.r += color.r * local_weight;
                    texel.g += color.g * local_weight;
                    texel.b += color.b * local_weight;
                    texel.roughness += color.roughness * local_weight;
                    texel.metallic += color.metallic * local_weight;
                    texel.weight += local_weight;
                    texel.floor_like = texel.floor_like || floor_like;
                }
            }
        }
    }

    auto clamp_unit(double value) -> double
    {
        return clamp(value, 0.0, 1.0);
    }

    auto byte_from_unit(double value) -> std::uint8_t
    {
        const auto clamped = clamp_unit(value);
        return static_cast<std::uint8_t>(std::round(clamped * 255.0));
    }

    auto chroma_distance_rgb(const Color& a, const Color& b) -> double
    {
        const auto ar = a.r - (a.r + a.g + a.b) / 3.0;
        const auto ag = a.g - (a.r + a.g + a.b) / 3.0;
        const auto ab = a.b - (a.r + a.g + a.b) / 3.0;
        const auto br = b.r - (b.r + b.g + b.b) / 3.0;
        const auto bg = b.g - (b.r + b.g + b.b) / 3.0;
        const auto bb = b.b - (b.r + b.g + b.b) / 3.0;
        return std::sqrt((ar - br) * (ar - br) + (ag - bg) * (ag - bg) + (ab - bb) * (ab - bb));
    }

    auto hash_bytes(const std::vector<std::uint8_t>& bytes) -> std::uint64_t
    {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto byte : bytes)
        {
            hash ^= static_cast<std::uint64_t>(byte);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    auto changed_byte_count(const std::vector<std::uint8_t>& before,
                            const std::vector<std::uint8_t>& after) -> int
    {
        const auto count = std::min(before.size(), after.size());
        int changed = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (before[i] != after[i])
            {
                ++changed;
            }
        }
        changed += static_cast<int>(std::max(before.size(), after.size()) - count);
        return changed;
    }

    auto FrameBudget::consume(double elapsed_ms) -> bool
    {
        consumed_ms += std::max(0.0, elapsed_ms);
        overrun = overrun || (soft_budget_ms > 0.0 && consumed_ms > soft_budget_ms);
        hard_overrun = hard_overrun || (hard_budget_ms > 0.0 && consumed_ms > hard_budget_ms);
        return overrun;
    }

    auto evaluate_capture_quality(const CaptureQualityInput& input) -> CaptureQualityReport
    {
        CaptureQualityReport report{};
        report.low_luma_suspect =
            input.capture_rgb_max < MinCaptureRgbMax ||
            (input.capture_rgb_range < MinCaptureRgbRange && input.capture_luma_range < MinCaptureLumaRange);
        report.chroma_validation_failed =
            input.capture_trace_chroma_avg > MaxCaptureTraceChromaAvg ||
            input.capture_trace_chroma_p95 > MaxCaptureTraceChromaP95;

        if (!input.image_ok)
        {
            report.failure = "capture_image_unavailable";
            return report;
        }
        if (!input.bulk_calibration_ok || input.bulk_best_median > MaxBulkMedianRgbError)
        {
            report.failure = "bulk_calibration_failed_no_paint";
            return report;
        }
        if (input.trace_hits < input.min_hits || input.background_pixels < input.min_hits)
        {
            report.failure = "insufficient_validated_samples_no_paint";
            return report;
        }
        if (input.uniform || input.clear_suspect)
        {
            report.failure = "capture_color_quality_failed_no_paint";
            return report;
        }
        if (report.chroma_validation_failed)
        {
            report.failure = "capture_trace_chroma_failed_no_paint";
            return report;
        }
        if (report.low_luma_suspect)
        {
            report.failure = "capture_low_dynamic_range_no_paint";
            return report;
        }
        report.ok = true;
        report.failure = "ok";
        return report;
    }

    auto validate_capture_quality(const CaptureQualityInput& input) -> CaptureQualityDecision
    {
        const auto report = evaluate_capture_quality(input);
        return {report.ok, report.failure};
    }

    auto choose_capture_dimensions(const CaptureSizingInput& input) -> CaptureSizingDecision
    {
        CaptureSizingDecision decision{};
        if (input.viewport_width <= 0 || input.viewport_height <= 0)
        {
            decision.reason = "invalid_viewport";
            return decision;
        }
        if (input.require_viewport_size)
        {
            decision.width = input.viewport_width;
            decision.height = input.viewport_height;
            decision.scale = 1.0;
            decision.uses_viewport_size = true;
            decision.reason = "viewport_size_required";
            return decision;
        }

        const auto failed_attempts = std::max(0, input.failed_attempts);
        const auto viewport_edge = std::max(input.viewport_width, input.viewport_height);
        const auto texture_edge = std::max(input.texture_width, input.texture_height);
        const auto texture_scale =
            texture_edge > 0 && viewport_edge > 0
                ? clamp((static_cast<double>(texture_edge) * 2.0) / static_cast<double>(viewport_edge), 0.25, 1.0)
                : 1.0;
        auto scale = texture_scale;
        for (int i = 0; i < failed_attempts; ++i)
        {
            scale *= 0.5;
        }
        scale = clamp(scale, 1.0 / 16.0, 1.0);

        decision.width = std::max(1, static_cast<int>(std::round(static_cast<double>(input.viewport_width) * scale)));
        decision.height = std::max(1, static_cast<int>(std::round(static_cast<double>(input.viewport_height) * scale)));
        decision.scale = scale;
        decision.uses_viewport_size =
            failed_attempts == 0 &&
            decision.width == input.viewport_width &&
            decision.height == input.viewport_height;
        if (decision.uses_viewport_size)
        {
            decision.reason = "viewport_size";
        }
        else if (texture_scale < 0.999)
        {
            decision.reason = failed_attempts > 0 ? "texture_limited_retry_ladder" : "texture_limited_viewport";
        }
        else
        {
            decision.reason = "viewport_retry_ladder";
        }
        return decision;
    }

    auto choose_adaptive_sampling_policy(const AdaptiveSamplingInput& input) -> AdaptiveSamplingPolicy
    {
        constexpr int MinFrontHits = 2048;
        constexpr int MaxFrontHits = 32768;
        constexpr int MinPreferredFrontHits = 4096;
        constexpr int MinRefineGrid = 24;
        constexpr int MaxRefineGrid = 256;
        constexpr int MinSideSeeds = 512;
        constexpr int MaxSideSeeds = 8192;
        constexpr int MinSideViews = 6;
        constexpr int MaxSideViews = 36;
        constexpr int MinSideGrid = 10;
        constexpr int MaxSideGrid = 64;
        constexpr double DuplicateStopRate = 0.58;

        const auto viewport_pixels = safe_pixel_count(input.viewport_width, input.viewport_height);
        const auto texture_width = positive_or(input.texture_width, input.viewport_width);
        const auto texture_height = positive_or(input.texture_height, input.viewport_height);
        const auto texture_pixels = safe_pixel_count(texture_width, texture_height);
        const auto bbox_pixels = std::max(1.0, input.bbox_width_px * input.bbox_height_px);
        const auto bbox_fraction = clamp(bbox_pixels / viewport_pixels, 0.02, 1.0);
        const auto bbox_aspect = clamp(input.bbox_width_px / std::max(1.0, input.bbox_height_px), 0.25, 4.0);
        const auto texture_edge = std::sqrt(texture_pixels);
        const auto bbox_weight = std::sqrt(bbox_fraction);

        AdaptiveSamplingPolicy policy{};
        policy.target_front_hits = clamp_int(
            static_cast<int>(std::round(texture_edge * bbox_weight * 14.0)),
            MinFrontHits,
            MaxFrontHits);
        policy.preferred_front_hits = std::max(MinPreferredFrontHits, static_cast<int>(std::round(policy.target_front_hits * 0.72)));
        policy.preferred_front_hits = std::min(policy.preferred_front_hits, policy.target_front_hits);
        policy.min_front_hits = std::max(MinFrontHits, static_cast<int>(std::round(policy.target_front_hits * 0.28)));
        policy.min_front_hits = std::min(policy.min_front_hits, policy.preferred_front_hits);
        policy.hard_max_attempts = std::max(policy.target_front_hits * 4,
                                            static_cast<int>(std::round(std::sqrt(bbox_pixels) * std::sqrt(texture_edge) * 4.0)));
        policy.hard_max_attempts = clamp_int(policy.hard_max_attempts, policy.target_front_hits, MaxFrontHits * 4);

        policy.refine_grid_x = clamp_int(
            static_cast<int>(std::round(std::sqrt(static_cast<double>(policy.hard_max_attempts) * bbox_aspect))),
            MinRefineGrid,
            MaxRefineGrid);
        policy.refine_grid_y = clamp_int(
            static_cast<int>(std::ceil(static_cast<double>(policy.hard_max_attempts) / static_cast<double>(std::max(1, policy.refine_grid_x)))),
            MinRefineGrid,
            MaxRefineGrid);
        while (policy.refine_grid_x * policy.refine_grid_y > policy.hard_max_attempts && policy.refine_grid_y > MinRefineGrid)
        {
            --policy.refine_grid_y;
        }
        while (policy.refine_grid_x * policy.refine_grid_y > policy.hard_max_attempts && policy.refine_grid_x > MinRefineGrid)
        {
            --policy.refine_grid_x;
        }

        const auto seed_basis = std::max(input.existing_seed_count, policy.target_front_hits);
        policy.target_side_seeds = clamp_int(
            std::max(MinSideSeeds, static_cast<int>(std::round(static_cast<double>(seed_basis) * 0.38))),
            MinSideSeeds,
            MaxSideSeeds);
        policy.min_side_seeds = std::max(128, policy.target_side_seeds / 5);
        policy.hard_side_attempts = clamp_int(policy.target_side_seeds * 5, policy.target_side_seeds, MaxSideSeeds * 8);
        policy.side_view_count = clamp_int(
            static_cast<int>(std::round(std::sqrt(static_cast<double>(policy.target_side_seeds)) * 0.55)),
            MinSideViews,
            MaxSideViews);
        const auto rays_per_view = std::max(1, policy.hard_side_attempts / std::max(1, policy.side_view_count));
        policy.side_grid_x = clamp_int(
            static_cast<int>(std::round(std::sqrt(static_cast<double>(rays_per_view) * bbox_aspect))),
            MinSideGrid,
            MaxSideGrid);
        policy.side_grid_y = clamp_int(
            static_cast<int>(std::ceil(static_cast<double>(rays_per_view) / static_cast<double>(std::max(1, policy.side_grid_x)))),
            MinSideGrid,
            MaxSideGrid);
        policy.duplicate_limited =
            input.attempts > 0 &&
            (static_cast<double>(std::max(0, input.duplicate_texels)) / static_cast<double>(input.attempts)) > DuplicateStopRate;
        return policy;
    }

    auto stratified_grid_linear_index(int grid_width, int grid_height, int ordinal) -> int
    {
        if (grid_width <= 0 || grid_height <= 0 || ordinal < 0)
        {
            return 0;
        }
        const auto total = grid_width * grid_height;
        if (ordinal >= total)
        {
            return total;
        }
        const auto row = ordinal % grid_height;
        const auto column = ordinal / grid_height;
        return row * grid_width + column;
    }

    auto stratified_grid_order(int grid_width, int grid_height, int limit) -> std::vector<int>
    {
        const auto total = std::max(0, grid_width) * std::max(0, grid_height);
        const auto count = limit > 0 ? std::min(limit, total) : total;
        std::vector<int> order{};
        order.reserve(static_cast<std::size_t>(count));
        for (int ordinal = 0; ordinal < count; ++ordinal)
        {
            order.push_back(stratified_grid_linear_index(grid_width, grid_height, ordinal));
        }
        return order;
    }

    auto evaluate_front_coverage(const FrontCoverageInput& input) -> FrontCoverageReport
    {
        FrontCoverageReport report{};
        report.vertical_band_hits = std::max(0, input.vertical_band_hits);
        report.vertical_band_count = std::max(0, input.vertical_band_count);
        report.refined_cell_width =
            input.refine_grid_x > 0
                ? std::max(0.0, input.coarse_max_nx - input.coarse_min_nx) / static_cast<double>(input.refine_grid_x)
                : 0.0;
        report.refined_cell_height =
            input.refine_grid_y > 0
                ? std::max(0.0, input.coarse_max_ny - input.coarse_min_ny) / static_cast<double>(input.refine_grid_y)
                : 0.0;

        const auto tolerance_x = std::max(report.refined_cell_width * 2.0, 0.000001);
        const auto tolerance_y = std::max(report.refined_cell_height * 2.0, 0.000001);
        report.reaches_coarse_top = input.refined_min_ny <= input.coarse_min_ny + tolerance_y;
        report.reaches_coarse_bottom = input.refined_max_ny >= input.coarse_max_ny - tolerance_y;
        report.reaches_coarse_left = input.refined_min_nx <= input.coarse_min_nx + tolerance_x;
        report.reaches_coarse_right = input.refined_max_nx >= input.coarse_max_nx - tolerance_x;
        report.refined_grid_complete =
            input.refined_total_cells <= 0 ||
            input.refined_grid_cursor >= input.refined_total_cells;

        if (input.sample_count < input.min_samples)
        {
            report.failure = "front_coverage_insufficient_samples";
            return report;
        }
        if (!report.reaches_coarse_bottom)
        {
            report.failure = "front_coverage_bottom_not_reached";
            return report;
        }
        if (!report.reaches_coarse_top)
        {
            report.failure = "front_coverage_top_not_reached";
            return report;
        }
        if (!report.reaches_coarse_left || !report.reaches_coarse_right)
        {
            report.failure = "front_coverage_horizontal_span_incomplete";
            return report;
        }
        if (!report.refined_grid_complete)
        {
            report.failure = "front_coverage_grid_incomplete";
            return report;
        }

        report.ok = true;
        report.failed = false;
        report.failure = "ok";
        return report;
    }

    auto generate_golden_angle_views(int count, double pitch_limit_degrees) -> std::vector<VirtualView>
    {
        constexpr double GoldenAngleDegrees = 137.50776405003785;
        std::vector<VirtualView> views{};
        if (count <= 0)
        {
            return views;
        }
        const auto pitch_limit = clamp(std::abs(pitch_limit_degrees), 0.0, 89.0);
        views.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index)
        {
            auto yaw = std::fmod(static_cast<double>(index) * GoldenAngleDegrees, 360.0);
            if (yaw > 180.0)
            {
                yaw -= 360.0;
            }
            const auto t = (static_cast<double>(index) + 0.5) / static_cast<double>(count);
            const auto pitch = (2.0 * t - 1.0) * pitch_limit;
            views.push_back(VirtualView{yaw, pitch});
        }
        return views;
    }

    auto evaluate_uv_coverage(const UvCoverageInput& input) -> UvCoverageReport
    {
        constexpr double MinVerifiedCoverage = 0.40;
        constexpr double MinCoverageWhenSideExhausted = 0.40;
        constexpr double DuplicateLimitedRate = 0.62;

        UvCoverageReport report{};
        if (input.texture_width <= 0 || input.texture_height <= 0)
        {
            report.failure = "invalid_texture_for_coverage_no_import";
            return report;
        }

        const auto total_texels = safe_pixel_count(input.texture_width, input.texture_height);
        report.coverage_ratio = clamp(static_cast<double>(std::max(0, input.covered_texels)) / total_texels, 0.0, 1.0);
        report.direct_ratio = clamp(static_cast<double>(std::max(0, input.direct_texels)) / total_texels, 0.0, 1.0);
        report.duplicate_rate = input.attempts > 0
                                    ? clamp(static_cast<double>(std::max(0, input.duplicate_texels)) /
                                                static_cast<double>(input.attempts),
                                            0.0,
                                            1.0)
                                    : 0.0;
        report.duplicate_limited = report.duplicate_rate > DuplicateLimitedRate;
        report.side_exhausted_low_coverage =
            input.side_budget_exhausted && report.coverage_ratio < MinCoverageWhenSideExhausted;

        if (report.side_exhausted_low_coverage)
        {
            report.failure = "coverage_failed_side_budget_exhausted_no_import";
            return report;
        }
        if (report.coverage_ratio < MinVerifiedCoverage)
        {
            report.failure = "coverage_failed_no_import";
            return report;
        }
        if (report.duplicate_limited && report.coverage_ratio < MinCoverageWhenSideExhausted)
        {
            report.failure = "coverage_failed_duplicate_limited_no_import";
            return report;
        }

        report.ok = true;
        report.failure = "ok";
        (void)input.side_seeds;
        return report;
    }

    auto evaluate_runtime_atlas_probe(const RuntimeAtlasProbeReport& input) -> RuntimeAtlasProbeReport
    {
        auto report = input;
        report.ok = false;
        if (report.texture_width <= 0 || report.texture_height <= 0)
        {
            report.failure = "atlas_probe_invalid_texture_no_import";
            return report;
        }
        if (report.valid_texels <= 0)
        {
            report.failure = "atlas_source_unavailable_no_import";
            return report;
        }
        if (report.chart_count <= 0)
        {
            report.failure = "atlas_chart_unavailable_no_import";
            return report;
        }

        const auto valid = std::max(1, report.valid_texels);
        report.overlap_ratio = clamp(static_cast<double>(std::max(0, report.overlap_texels)) /
                                         static_cast<double>(valid),
                                     0.0,
                                     1.0);
        if (report.overlap_ratio > 0.08)
        {
            report.failure = "atlas_overlap_ambiguous_no_import";
            return report;
        }
        if (report.degenerate_texels > valid / 20)
        {
            report.failure = "atlas_degenerate_uv_no_import";
            return report;
        }

        report.ok = true;
        report.failure = "ok";
        return report;
    }

    auto evaluate_atlas_coverage(const AtlasCoverageInput& input) -> AtlasCoverageReport
    {
        constexpr double MinGlobalCoverage = 0.82;
        constexpr double MinDirectCoverage = 0.12;
        constexpr double MinChartCoverage = 0.72;
        constexpr double MinMajorRegionCoverage = 0.70;

        AtlasCoverageReport report{};
        report.valid_texels = std::max(0, input.valid_texels);
        report.direct_texels = std::max(0, input.direct_texels);
        report.inferred_texels = std::max(0, input.inferred_texels);
        report.covered_texels = std::min(report.valid_texels, report.direct_texels + report.inferred_texels);
        report.chart_count = std::max(0, input.chart_count);
        report.min_chart_coverage = clamp(input.min_chart_coverage, 0.0, 1.0);

        if (input.texture_width <= 0 || input.texture_height <= 0 || report.valid_texels <= 0)
        {
            report.failure = "atlas_coverage_invalid_no_import";
            return report;
        }
        const auto valid = static_cast<double>(std::max(1, report.valid_texels));
        report.valid_coverage_ratio = clamp(static_cast<double>(report.covered_texels) / valid, 0.0, 1.0);
        report.direct_coverage_ratio = clamp(static_cast<double>(report.direct_texels) / valid, 0.0, 1.0);
        report.inferred_coverage_ratio = clamp(static_cast<double>(report.inferred_texels) / valid, 0.0, 1.0);
        report.lower_body_undercovered = input.lower_body_coverage < MinMajorRegionCoverage;
        report.side_undercovered = input.side_coverage < MinMajorRegionCoverage;
        report.back_undercovered = input.back_coverage < MinMajorRegionCoverage;

        if (report.direct_coverage_ratio < MinDirectCoverage)
        {
            report.failure = "coverage_failed_direct_evidence_no_import";
            return report;
        }
        if (report.valid_coverage_ratio < MinGlobalCoverage)
        {
            report.failure = "coverage_failed_no_import";
            return report;
        }
        if (report.chart_count <= 0 || report.min_chart_coverage < MinChartCoverage)
        {
            report.failure = "coverage_failed_chart_no_import";
            return report;
        }
        if (report.lower_body_undercovered)
        {
            report.failure = "coverage_failed_lower_body_no_import";
            return report;
        }
        if (report.side_undercovered)
        {
            report.failure = "coverage_failed_side_no_import";
            return report;
        }
        if (report.back_undercovered)
        {
            report.failure = "coverage_failed_back_no_import";
            return report;
        }

        report.ok = true;
        report.failure = "ok";
        return report;
    }

    auto build_runtime_atlas_from_masks(const AtlasBuildInput& input) -> AtlasBuildResult
    {
        AtlasBuildResult result{};
        if (input.texture_width <= 0 || input.texture_height <= 0)
        {
            result.failure = "atlas_invalid_texture_no_import";
            return result;
        }
        const auto texel_count = static_cast<std::size_t>(input.texture_width) *
                                 static_cast<std::size_t>(input.texture_height);
        if (input.valid_mask.size() != texel_count ||
            input.chart_ids.size() != texel_count ||
            (!input.direct_mask.empty() && input.direct_mask.size() != texel_count))
        {
            result.failure = "atlas_mask_size_mismatch_no_import";
            return result;
        }

        result.texels.resize(texel_count);
        int max_chart_id = -1;
        for (std::size_t index = 0; index < texel_count; ++index)
        {
            if (input.valid_mask[index] == 0)
            {
                continue;
            }
            max_chart_id = std::max(max_chart_id, input.chart_ids[index]);
        }
        if (max_chart_id < 0)
        {
            result.failure = "atlas_source_unavailable_no_import";
            return result;
        }

        result.charts.resize(static_cast<std::size_t>(max_chart_id + 1));
        for (int id = 0; id <= max_chart_id; ++id)
        {
            result.charts[static_cast<std::size_t>(id)].id = id;
        }

        for (std::size_t index = 0; index < texel_count; ++index)
        {
            auto& texel = result.texels[index];
            if (input.valid_mask[index] == 0)
            {
                texel.flags = 0;
                continue;
            }
            const auto chart_id = input.chart_ids[index];
            if (chart_id < 0 || chart_id > max_chart_id)
            {
                texel.flags = AtlasTexelDegenerate;
                continue;
            }

            const auto y = static_cast<int>(index / static_cast<std::size_t>(input.texture_width));
            const auto x = static_cast<int>(index - static_cast<std::size_t>(y * input.texture_width));
            texel.triangle_id = 0;
            texel.chart_id = chart_id;
            texel.bary0 = static_cast<double>(x) / static_cast<double>(std::max(1, input.texture_width - 1));
            texel.bary1 = static_cast<double>(y) / static_cast<double>(std::max(1, input.texture_height - 1));
            texel.flags = AtlasTexelValid;
            auto& chart = result.charts[static_cast<std::size_t>(chart_id)];
            ++chart.valid_texels;
            if (!input.direct_mask.empty() && input.direct_mask[index] != 0)
            {
                texel.flags |= AtlasTexelDirect;
                ++chart.direct_texels;
            }
        }

        int valid_texels = 0;
        int direct_texels = 0;
        double min_chart_coverage = 1.0;
        int chart_count = 0;
        for (auto& chart : result.charts)
        {
            if (chart.valid_texels <= 0)
            {
                continue;
            }
            ++chart_count;
            valid_texels += chart.valid_texels;
            direct_texels += chart.direct_texels;
            chart.coverage_ratio = static_cast<double>(chart.direct_texels + chart.inferred_texels) /
                                   static_cast<double>(std::max(1, chart.valid_texels));
            min_chart_coverage = std::min(min_chart_coverage, chart.coverage_ratio);
        }

        result.coverage = evaluate_atlas_coverage(AtlasCoverageInput{
            input.texture_width,
            input.texture_height,
            valid_texels,
            direct_texels,
            0,
            chart_count,
            chart_count > 0 ? min_chart_coverage : 0.0,
            1.0,
            1.0,
            1.0});
        result.ok = valid_texels > 0;
        result.failure = result.ok ? "ok" : "atlas_source_unavailable_no_import";
        return result;
    }

    auto choose_apply_backend(const RuntimeCapabilities& capabilities, bool dev_diagnostic_allowed) -> ApplyBackendProbe
    {
        ApplyBackendProbe probe{};
        probe.dev_diagnostic_allowed = dev_diagnostic_allowed;
        if (capabilities.non_blocking_texture_update && capabilities.server_paint_api)
        {
            probe.backend = ApplyBackend::NonBlockingTextureUpdate;
            probe.ok = true;
            probe.failure = "ok";
            return probe;
        }
        if (capabilities.chunked_paint_api && capabilities.server_paint_api)
        {
            probe.backend = ApplyBackend::ChunkedPaintApi;
            probe.ok = true;
            probe.failure = "ok";
            return probe;
        }
        if ((capabilities.batched_reflected_paint_api || capabilities.paint_stroke_uv ||
             capabilities.paint_at_uv_with_brush || capabilities.paint_at_uv) &&
            capabilities.server_paint_api)
        {
            probe.backend = ApplyBackend::BatchedReflectedPaintApi;
            probe.ok = true;
            probe.failure = "ok";
            return probe;
        }
        if (capabilities.paint_stroke_uv || capabilities.paint_at_uv_with_brush || capabilities.paint_at_uv ||
            capabilities.chunked_paint_api || capabilities.batched_reflected_paint_api)
        {
            probe.backend = ApplyBackend::Unavailable;
            probe.failure = "replicated_apply_unavailable_no_import";
            return probe;
        }
        if (capabilities.import_channel_from_bytes)
        {
            probe.backend = ApplyBackend::BlockingImportChannelFromBytes;
            probe.blocking_only = true;
            probe.ok = dev_diagnostic_allowed;
            probe.failure = dev_diagnostic_allowed ? "dev_blocking_import_allowed" : "apply_backend_unavailable_no_import";
            return probe;
        }
        probe.backend = ApplyBackend::Unavailable;
        probe.failure = "apply_backend_unavailable_no_import";
        return probe;
    }

    auto plan_replicated_stroke_apply(const ReplicatedStrokePlanInput& input) -> ReplicatedStrokePlan
    {
        ReplicatedStrokePlan plan{};
        plan.strokes_per_tick = input.max_replicated_strokes_per_tick > 0
                                    ? std::max(1, std::min(input.max_replicated_strokes_per_tick, 128))
                                    : 24;
        if (!input.apply_backend_ok)
        {
            plan.failure = "replicated_apply_unavailable_no_apply";
            return plan;
        }
        if (input.sample_count <= 0)
        {
            plan.failure = "replicated_stroke_samples_unavailable_no_apply";
            return plan;
        }

        plan.quality_success = input.min_quality_samples <= 0 ||
                               input.sample_count >= input.min_quality_samples;
        if (!plan.quality_success && !input.dev_diagnostic_allowed)
        {
            plan.failure = "coverage_failed_no_apply";
            return plan;
        }

        plan.ok = true;
        plan.partial = !plan.quality_success;
        plan.failure = plan.partial ? "dev_replicated_partial_apply" : "ok";
        return plan;
    }

    auto plan_sampled_readback_tick(const SampledReadbackTickInput& input) -> SampledReadbackTickPlan
    {
        SampledReadbackTickPlan plan{};
        const auto total = std::max(0, input.total_samples);
        plan.next_cursor = clamp_int(input.cursor, 0, total);
        if (total <= 0 || plan.next_cursor >= total)
        {
            plan.complete = true;
            return plan;
        }

        const auto max_samples = input.max_samples_per_tick > 0 ? input.max_samples_per_tick : 64;
        const auto sample_ms = std::max(0.0, input.estimated_sample_ms);
        FrameBudget budget{input.soft_budget_ms, input.hard_budget_ms};
        while (plan.next_cursor < total && plan.samples_this_tick < max_samples)
        {
            ++plan.next_cursor;
            ++plan.samples_this_tick;
            budget.consume(sample_ms);
            if (budget.overrun || budget.hard_overrun)
            {
                break;
            }
        }
        plan.complete = plan.next_cursor >= total;
        plan.frame_budget_overrun = budget.overrun;
        plan.hard_budget_overrun = budget.hard_overrun;
        plan.budget_ms = budget.consumed_ms;
        return plan;
    }

    auto estimate_seed_radius_for_density(int texture_width, int texture_height, int seed_count) -> int
    {
        constexpr int MinRadius = 1;
        constexpr int MaxRadius = 24;
        if (texture_width <= 0 || texture_height <= 0 || seed_count <= 0)
        {
            return MinRadius;
        }
        const auto pixels_per_seed = safe_pixel_count(texture_width, texture_height) /
                                     static_cast<double>(std::max(1, seed_count));
        const auto radius = static_cast<int>(std::round(std::sqrt(pixels_per_seed) * 0.48));
        return clamp_int(radius, MinRadius, MaxRadius);
    }

    auto choose_precision_brush_radius(const PrecisionBrushInput& input) -> PrecisionBrushDecision
    {
        PrecisionBrushDecision out{};
        const auto texture_edge = static_cast<double>(std::max(1, std::max(input.texture_width, input.texture_height)));
        out.texture_min_radius = clamp(input.texture_min_radius, 0.0, 1.0);
        out.texture_max_radius = clamp(input.texture_max_radius, 0.0, 1.0);
        if (out.texture_max_radius < out.texture_min_radius)
        {
            std::swap(out.texture_min_radius, out.texture_max_radius);
        }
        out.requested_radius = static_cast<double>(std::max(1, input.seed_radius_px)) / texture_edge;
        const auto max_radius = out.texture_max_radius > 0.0 ? out.texture_max_radius : 1.0;
        if (input.precision_mode)
        {
            out.radius = clamp(out.requested_radius, 1.0 / texture_edge, max_radius);
            out.clamped_by_game_min = false;
            out.source = "density_precision";
        }
        else
        {
            out.radius = clamp(out.requested_radius, out.texture_min_radius, max_radius);
            out.clamped_by_game_min = out.radius > out.requested_radius && out.radius == out.texture_min_radius;
            out.source = out.clamped_by_game_min ? "game_min_clamped" : "density";
        }
        out.brush_footprint_texels = out.radius * texture_edge;
        return out;
    }

    auto infer_surface_stretch_seeds(const std::vector<SurfaceStretchSeed>& seeds,
                                     const SurfaceStretchPolicy& policy) -> SurfaceStretchReport
    {
        SurfaceStretchReport report{};
        const auto max_inferred = policy.max_inferred > 0 ? policy.max_inferred : static_cast<int>(seeds.size());
        const auto max_uv = std::max(0.0, policy.max_uv_distance);
        const auto max_screen = std::max(0.0, policy.max_screen_distance);
        const auto min_normal_dot = clamp(policy.min_normal_dot, -1.0, 1.0);

        const auto normal_dot = [](const SurfaceStretchSeed& a, const SurfaceStretchSeed& b) {
            const auto al = std::sqrt(a.normal_x * a.normal_x + a.normal_y * a.normal_y + a.normal_z * a.normal_z);
            const auto bl = std::sqrt(b.normal_x * b.normal_x + b.normal_y * b.normal_y + b.normal_z * b.normal_z);
            if (al <= 0.000001 || bl <= 0.000001)
            {
                return 1.0;
            }
            return (a.normal_x * b.normal_x + a.normal_y * b.normal_y + a.normal_z * b.normal_z) / (al * bl);
        };

        for (const auto& seed : seeds)
        {
            if (seed.direct)
            {
                ++report.direct_preserved;
            }
        }

        for (std::size_t i = 0; i < seeds.size() && static_cast<int>(report.inferred.size()) < max_inferred; ++i)
        {
            if (!seeds[i].direct)
            {
                continue;
            }
            for (std::size_t j = i + 1; j < seeds.size() && static_cast<int>(report.inferred.size()) < max_inferred; ++j)
            {
                if (!seeds[j].direct)
                {
                    continue;
                }
                const auto du = seeds[i].u - seeds[j].u;
                const auto dv = seeds[i].v - seeds[j].v;
                const auto uv_distance = std::sqrt(du * du + dv * dv);
                const auto dsx = seeds[i].screen_x - seeds[j].screen_x;
                const auto dsy = seeds[i].screen_y - seeds[j].screen_y;
                const auto screen_distance = std::sqrt(dsx * dsx + dsy * dsy);
                if (uv_distance > max_uv || screen_distance > max_screen)
                {
                    ++report.rejected_seam;
                    continue;
                }
                if (normal_dot(seeds[i], seeds[j]) < min_normal_dot)
                {
                    ++report.normal_limit;
                    continue;
                }

                PaintSeed inferred{};
                inferred.u = clamp((seeds[i].u + seeds[j].u) * 0.5, 0.0, 0.999999);
                inferred.v = clamp((seeds[i].v + seeds[j].v) * 0.5, 0.0, 0.999999);
                inferred.color.r = (seeds[i].color.r + seeds[j].color.r) * 0.5;
                inferred.color.g = (seeds[i].color.g + seeds[j].color.g) * 0.5;
                inferred.color.b = (seeds[i].color.b + seeds[j].color.b) * 0.5;
                inferred.color.roughness = (seeds[i].color.roughness + seeds[j].color.roughness) * 0.5;
                inferred.color.metallic = (seeds[i].color.metallic + seeds[j].color.metallic) * 0.5;
                inferred.floor_like = false;
                inferred.priority = -1;
                inferred.radius = std::max(1, std::min(seeds[i].radius, seeds[j].radius));
                inferred.weight = 0.5;
                report.inferred.push_back(inferred);
            }
        }
        return report;
    }

    auto plan_front_micro_fill(const std::vector<MicroFillSource>& sources,
                               const MicroFillPolicy& policy) -> MicroFillReport
    {
        MicroFillReport report{};
        const auto brush_radius = std::max(0.000001, policy.brush_radius_uv);
        const auto max_uv = std::max(0.0, policy.max_uv_distance);
        const auto max_screen = std::max(0.0, policy.max_screen_distance);
        const auto min_normal_dot = clamp(policy.min_normal_dot, -1.0, 1.0);
        const auto max_gap_cells = std::max(1, policy.max_gap_cells);
        const auto max_candidates = policy.max_candidates > 0
                                        ? policy.max_candidates
                                        : static_cast<int>(sources.size());

        const auto normal_dot = [](const MicroFillSource& a, const MicroFillSource& b) {
            const auto al = std::sqrt(a.normal_x * a.normal_x + a.normal_y * a.normal_y + a.normal_z * a.normal_z);
            const auto bl = std::sqrt(b.normal_x * b.normal_x + b.normal_y * b.normal_y + b.normal_z * b.normal_z);
            if (al <= 0.000001 || bl <= 0.000001)
            {
                return 1.0;
            }
            return (a.normal_x * b.normal_x + a.normal_y * b.normal_y + a.normal_z * b.normal_z) / (al * bl);
        };
        const auto cell_index = [brush_radius](double value) {
            return static_cast<int>(std::floor(clamp(value, 0.0, 0.999999) / brush_radius));
        };
        const auto cell_key = [](int x, int y) {
            return (static_cast<std::int64_t>(x) << 32) ^ (static_cast<std::int64_t>(y) & 0xffffffffLL);
        };

        struct IndexedCell
        {
            std::size_t index{0};
            int x{0};
            int y{0};
        };
        std::vector<std::pair<int, int>> cells{};
        std::vector<IndexedCell> direct_cells{};
        cells.reserve(sources.size());
        direct_cells.reserve(sources.size());
        std::vector<std::int64_t> direct_keys{};
        direct_keys.reserve(sources.size());
        for (std::size_t index = 0; index < sources.size(); ++index)
        {
            const auto& source = sources[index];
            const auto cx = cell_index(source.u);
            const auto cy = cell_index(source.v);
            cells.push_back({cx, cy});
            if (source.direct)
            {
                ++report.direct_preserved;
                direct_keys.push_back(cell_key(cx, cy));
                direct_cells.push_back(IndexedCell{index, cx, cy});
            }
        }

        std::vector<std::int64_t> emitted_keys{};
        emitted_keys.reserve(static_cast<std::size_t>(std::max(0, max_candidates)));
        const auto has_key = [](const std::vector<std::int64_t>& keys, std::int64_t key) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        };

        auto attempt_pair = [&](std::size_t i, std::size_t j) {
            if (static_cast<int>(report.candidates.size()) >= max_candidates)
            {
                return;
            }
            if (!sources[i].verified || !sources[j].verified)
            {
                ++report.rejected_unverified_source;
                return;
            }
            if (normal_dot(sources[i], sources[j]) < min_normal_dot)
            {
                ++report.rejected_normal;
                return;
            }

            const auto du = sources[i].u - sources[j].u;
            const auto dv = sources[i].v - sources[j].v;
            const auto uv_distance = std::sqrt(du * du + dv * dv);
            const auto dsx = sources[i].screen_x - sources[j].screen_x;
            const auto dsy = sources[i].screen_y - sources[j].screen_y;
            const auto screen_distance = std::sqrt(dsx * dsx + dsy * dsy);
            const auto dx_cells = cells[i].first - cells[j].first;
            const auto dy_cells = cells[i].second - cells[j].second;
            const auto gap_cells = std::max(std::abs(dx_cells), std::abs(dy_cells));
            if (uv_distance > max_uv || screen_distance > max_screen ||
                gap_cells <= 1 || gap_cells > max_gap_cells)
            {
                ++report.rejected_unbounded;
                return;
            }

            for (int step = 1; step < gap_cells && static_cast<int>(report.candidates.size()) < max_candidates; ++step)
            {
                const auto t = static_cast<double>(step) / static_cast<double>(gap_cells);
                const auto u = clamp(sources[i].u * (1.0 - t) + sources[j].u * t, 0.0, 0.999999);
                const auto v = clamp(sources[i].v * (1.0 - t) + sources[j].v * t, 0.0, 0.999999);
                const auto key = cell_key(cell_index(u), cell_index(v));
                if (has_key(direct_keys, key) || has_key(emitted_keys, key))
                {
                    continue;
                }

                PaintSeed candidate{};
                candidate.u = u;
                candidate.v = v;
                candidate.color.r = sources[i].color.r * (1.0 - t) + sources[j].color.r * t;
                candidate.color.g = sources[i].color.g * (1.0 - t) + sources[j].color.g * t;
                candidate.color.b = sources[i].color.b * (1.0 - t) + sources[j].color.b * t;
                candidate.color.roughness =
                    sources[i].color.roughness * (1.0 - t) + sources[j].color.roughness * t;
                candidate.color.metallic =
                    sources[i].color.metallic * (1.0 - t) + sources[j].color.metallic * t;
                candidate.floor_like = sources[i].floor_like && sources[j].floor_like;
                candidate.priority = -2;
                candidate.radius = std::max(1, std::min(sources[i].radius, sources[j].radius));
                candidate.weight = 0.5;
                emitted_keys.push_back(key);
                report.candidates.push_back(candidate);
            }
        };
        auto scan_neighbors = [&](std::vector<IndexedCell> ordered, bool y_major) {
            if (y_major)
            {
                std::sort(ordered.begin(), ordered.end(), [](const IndexedCell& a, const IndexedCell& b) {
                    return a.y == b.y ? a.x < b.x : a.y < b.y;
                });
            }
            else
            {
                std::sort(ordered.begin(), ordered.end(), [](const IndexedCell& a, const IndexedCell& b) {
                    return a.x == b.x ? a.y < b.y : a.x < b.x;
                });
            }
            constexpr int lookahead = 8;
            for (std::size_t pos = 0; pos < ordered.size() && static_cast<int>(report.candidates.size()) < max_candidates; ++pos)
            {
                for (int step = 1;
                     step <= lookahead && pos + static_cast<std::size_t>(step) < ordered.size() &&
                     static_cast<int>(report.candidates.size()) < max_candidates;
                     ++step)
                {
                    const auto& a = ordered[pos];
                    const auto& b = ordered[pos + static_cast<std::size_t>(step)];
                    const auto major_gap = y_major ? std::abs(a.y - b.y) : std::abs(a.x - b.x);
                    const auto minor_gap = y_major ? std::abs(a.x - b.x) : std::abs(a.y - b.y);
                    if (major_gap > 1)
                    {
                        break;
                    }
                    if (minor_gap > max_gap_cells)
                    {
                        if (major_gap == 0)
                        {
                            break;
                        }
                        continue;
                    }
                    attempt_pair(a.index, b.index);
                }
            }
        };
        scan_neighbors(direct_cells, true);
        scan_neighbors(direct_cells, false);
        return report;
    }

    auto evaluate_side_coverage(const SideCoverageInput& input) -> SideCoverageReport
    {
        SideCoverageReport report{};
        report.front_quality_success = input.front_quality_success;
        const auto side_samples = std::max(0, input.side_samples);
        const auto inferred = std::max(0, std::min(input.inferred_side_samples, side_samples));
        report.inferred_ratio = side_samples > 0
                                    ? clamp(static_cast<double>(inferred) / static_cast<double>(side_samples), 0.0, 1.0)
                                    : 0.0;
        const auto min_side = std::max(0, input.min_side_samples);
        if (side_samples < min_side)
        {
            report.side_quality_failed = true;
            report.failure = input.budget_exhausted
                                 ? "side_coverage_failed_budget_exhausted"
                                 : "side_coverage_failed_low_samples";
            return report;
        }
        report.side_quality_success = true;
        report.failure = "ok";
        return report;
    }

    auto merge_nearby_paint_seeds(const std::vector<PaintSeed>& seeds,
                                  double brush_radius_uv) -> std::vector<PaintSeed>
    {
        if (seeds.empty() || brush_radius_uv <= 0.0)
        {
            return seeds;
        }

        struct Bucket
        {
            PaintSeed seed{};
            int count{0};
        };

        std::vector<Bucket> buckets{};
        buckets.reserve(seeds.size());
        const auto radius_sq = brush_radius_uv * brush_radius_uv;
        for (const auto& seed : seeds)
        {
            auto* matched = static_cast<Bucket*>(nullptr);
            for (auto& bucket : buckets)
            {
                const auto du = bucket.seed.u - seed.u;
                const auto dv = bucket.seed.v - seed.v;
                if (du * du + dv * dv <= radius_sq)
                {
                    matched = &bucket;
                    break;
                }
            }
            if (!matched)
            {
                buckets.push_back(Bucket{seed, 1});
                continue;
            }

            const auto next_count = matched->count + 1;
            const auto old_weight = static_cast<double>(matched->count) / static_cast<double>(next_count);
            const auto new_weight = 1.0 / static_cast<double>(next_count);
            matched->seed.u = matched->seed.u * old_weight + seed.u * new_weight;
            matched->seed.v = matched->seed.v * old_weight + seed.v * new_weight;
            matched->seed.color.r = matched->seed.color.r * old_weight + seed.color.r * new_weight;
            matched->seed.color.g = matched->seed.color.g * old_weight + seed.color.g * new_weight;
            matched->seed.color.b = matched->seed.color.b * old_weight + seed.color.b * new_weight;
            matched->seed.color.roughness =
                matched->seed.color.roughness * old_weight + seed.color.roughness * new_weight;
            matched->seed.color.metallic =
                matched->seed.color.metallic * old_weight + seed.color.metallic * new_weight;
            matched->seed.floor_like = matched->seed.floor_like || seed.floor_like;
            matched->seed.priority = std::max(matched->seed.priority, seed.priority);
            matched->seed.radius = std::max(matched->seed.radius, seed.radius);
            matched->seed.weight += seed.weight;
            matched->count = next_count;
        }

        std::vector<PaintSeed> out{};
        out.reserve(buckets.size());
        for (const auto& bucket : buckets)
        {
            out.push_back(bucket.seed);
        }
        return out;
    }

    auto is_floor_like_label(const std::string& label) -> bool
    {
        const auto text = lower_ascii(label);
        if (contains_token(text, "stage_level") || contains_token(text, "persistentlevel"))
        {
            const auto dot = text.find_last_of(".:/\\");
            const auto leaf = dot == std::string::npos ? text : text.substr(dot + 1);
            return is_floor_like_label(leaf);
        }

        return contains_token(text, "floor") ||
               contains_token(text, "ground") ||
               contains_token(text, "terrain") ||
               contains_token(text, "landscape") ||
               contains_token(text, "road") ||
               contains_token(text, "asphalt") ||
               contains_token(text, "tile") ||
               contains_token(text, "dirt") ||
               contains_token(text, "soil") ||
               contains_token(text, "sand");
    }

    auto resolve_material_channels(const MaterialResolveInput& input) -> ResolvedMaterial
    {
        ResolvedMaterial out{};
        out.roughness = clamp(input.original_roughness, 0.0, 1.0);
        out.metallic = clamp(input.original_metallic, 0.0, 1.0);
        out.confidence = MaterialConfidence::PreservedOriginal;

        if (input.has_roughness_scalar)
        {
            out.roughness = clamp(input.scalar_roughness, 0.0, 1.0);
            out.confidence = MaterialConfidence::ScalarParameter;
        }
        if (input.has_metallic_scalar)
        {
            out.metallic = clamp(input.scalar_metallic, 0.0, 1.0);
            out.confidence = MaterialConfidence::ScalarParameter;
        }

        if (input.floor_like && out.confidence == MaterialConfidence::ScalarParameter)
        {
            if (input.has_roughness_scalar)
            {
                out.roughness = clamp(std::max(out.roughness, 0.86), 0.86, 0.99);
            }
            if (input.has_metallic_scalar)
            {
                out.metallic = clamp(out.metallic, 0.0, 0.12);
            }
        }
        return out;
    }

    auto resolve_verified_material_evidence(const MaterialEvidence& evidence,
                                            double original_roughness,
                                            double original_metallic) -> ResolvedMaterial
    {
        ResolvedMaterial out{};
        out.roughness = clamp(original_roughness, 0.0, 1.0);
        out.metallic = clamp(original_metallic, 0.0, 1.0);
        out.confidence = MaterialConfidence::PreservedOriginal;

        if (evidence.has_base_color_constant)
        {
            out.confidence = MaterialConfidence::ConstantParameter;
        }
        if (evidence.has_base_color_texture && evidence.has_readable_texture)
        {
            out.confidence = MaterialConfidence::TextureParameter;
        }
        if (evidence.has_roughness_scalar)
        {
            out.roughness = clamp(evidence.scalar_roughness, 0.0, 1.0);
            if (out.confidence == MaterialConfidence::PreservedOriginal)
            {
                out.confidence = MaterialConfidence::ScalarParameter;
            }
        }
        if (evidence.has_metallic_scalar)
        {
            out.metallic = clamp(evidence.scalar_metallic, 0.0, 1.0);
            if (out.confidence == MaterialConfidence::PreservedOriginal)
            {
                out.confidence = MaterialConfidence::ScalarParameter;
            }
        }
        return out;
    }

    auto should_send_material_channels(MaterialConfidence confidence) -> bool
    {
        return confidence == MaterialConfidence::ScalarParameter ||
               confidence == MaterialConfidence::TextureParameter ||
               confidence == MaterialConfidence::ConstantParameter;
    }

    auto assemble_direct_texture(const ChannelBuffer& albedo_before,
                                 const ChannelBuffer& metallic_before,
                                 const ChannelBuffer& roughness_before,
                                 const std::vector<PaintSeed>& seeds) -> TextureAssemblyResult
    {
        TextureAssemblyResult result{};
        result.albedo = albedo_before;
        result.metallic = metallic_before;
        result.roughness = roughness_before;

        const auto texture_pixels = static_cast<std::size_t>(std::max(0, albedo_before.width)) *
                                    static_cast<std::size_t>(std::max(0, albedo_before.height));
        std::vector<ProjectionTexel> texels(texture_pixels);
        for (const auto& seed : seeds)
        {
            const auto priority = seed.priority > 0 ? seed.priority : (seed.floor_like ? 12 : 11);
            const auto weight = seed.weight > 0.0 ? seed.weight : (seed.floor_like ? 88.0 : 72.0);
            const auto radius = std::max(0, seed.radius);
            splat_projection_texel(texels,
                                   albedo_before.width,
                                   albedo_before.height,
                                   seed.u,
                                   seed.v,
                                   seed.color,
                                   weight,
                                   priority,
                                   seed.floor_like,
                                   radius);
        }

        std::vector<std::uint8_t> direct_mask(texture_pixels, 0);
        for (std::size_t index = 0; index < texels.size(); ++index)
        {
            if (projection_texel_painted(texels[index]))
            {
                direct_mask[index] = 1;
                ++result.stats.direct_texels;
            }
        }

        const auto front_gap_fill_radius = clamp_int(
            estimate_seed_radius_for_density(albedo_before.width,
                                             albedo_before.height,
                                             static_cast<int>(std::max<std::size_t>(1, seeds.size()))) *
                3,
            8,
            24);
        auto extended_texels = texels;
        parallel_ranges(texture_pixels, [&](std::size_t begin, std::size_t end, unsigned) {
            const auto width = std::max(1, albedo_before.width);
            const auto height = std::max(1, albedo_before.height);
            for (std::size_t index = begin; index < end; ++index)
            {
                if (index >= texels.size() || direct_mask[index] != 0)
                {
                    continue;
                }
                const auto y = static_cast<int>(index / static_cast<std::size_t>(width));
                const auto x = static_cast<int>(index - static_cast<std::size_t>(y * width));
                int best_distance_sq = front_gap_fill_radius * front_gap_fill_radius + 1;
                int best_priority = -1;
                std::size_t best_index = static_cast<std::size_t>(-1);
                for (int dy = -front_gap_fill_radius; dy <= front_gap_fill_radius; ++dy)
                {
                    const auto sy = y + dy;
                    if (sy < 0 || sy >= height)
                    {
                        continue;
                    }
                    for (int dx = -front_gap_fill_radius; dx <= front_gap_fill_radius; ++dx)
                    {
                        const auto sx = x + dx;
                        if (sx < 0 || sx >= width)
                        {
                            continue;
                        }
                        const auto distance_sq = dx * dx + dy * dy;
                        if (distance_sq > front_gap_fill_radius * front_gap_fill_radius)
                        {
                            continue;
                        }
                        const auto source_index = static_cast<std::size_t>(sy * width + sx);
                        if (source_index >= texels.size() || direct_mask[source_index] == 0 ||
                            !projection_texel_painted(texels[source_index]))
                        {
                            continue;
                        }
                        const auto priority = texels[source_index].priority;
                        if (distance_sq < best_distance_sq ||
                            (distance_sq == best_distance_sq && priority > best_priority) ||
                            (distance_sq == best_distance_sq && priority == best_priority && source_index < best_index))
                        {
                            best_distance_sq = distance_sq;
                            best_priority = priority;
                            best_index = source_index;
                        }
                    }
                }
                if (best_index == static_cast<std::size_t>(-1))
                {
                    continue;
                }

                const auto& source = texels[best_index];
                const auto inv = 1.0 / source.weight;
                ProjectionTexel copy{};
                copy.r = source.r * inv;
                copy.g = source.g * inv;
                copy.b = source.b * inv;
                copy.roughness = source.roughness * inv;
                copy.metallic = source.metallic * inv;
                copy.weight = 1.0;
                copy.priority = source.priority;
                copy.floor_like = source.floor_like;
                extended_texels[index] = copy;
            }
        });
        texels.swap(extended_texels);

        result.stats.worker_threads = worker_count_for_items(texture_pixels);
        std::vector<TextureWriteStats> worker_stats(result.stats.worker_threads);
        const auto write_scalar = [](std::vector<std::uint8_t>& bytes, int x, int y, int width, double value) {
            const auto offset = static_cast<std::size_t>(y * width + x) * 4;
            if (offset < bytes.size())
            {
                bytes[offset] = byte_from_unit(value);
            }
        };

        parallel_ranges(texture_pixels, [&](std::size_t begin, std::size_t end, unsigned worker) {
            auto& local = worker_stats[static_cast<std::size_t>(worker)];
            for (std::size_t index = begin; index < end; ++index)
            {
                if (index >= texels.size())
                {
                    continue;
                }
                const auto y = static_cast<int>(index / static_cast<std::size_t>(std::max(1, albedo_before.width)));
                const auto x = static_cast<int>(index - static_cast<std::size_t>(y * albedo_before.width));
                const auto offset = index * 4;
                const auto& source = texels[index];
                if (!projection_texel_painted(source) || offset + 2 >= result.albedo.bytes.size())
                {
                    ++local.preserved_original;
                    continue;
                }

                const auto inv = 1.0 / source.weight;
                result.albedo.bytes[offset + 0] = byte_from_unit(clamp(source.r * inv, 0.02, 0.98));
                result.albedo.bytes[offset + 1] = byte_from_unit(clamp(source.g * inv, 0.02, 0.98));
                result.albedo.bytes[offset + 2] = byte_from_unit(clamp(source.b * inv, 0.02, 0.98));
                if (x < result.metallic.width && y < result.metallic.height)
                {
                    write_scalar(result.metallic.bytes, x, y, result.metallic.width, clamp(source.metallic * inv, 0.0, 1.0));
                }
                if (x < result.roughness.width && y < result.roughness.height)
                {
                    write_scalar(result.roughness.bytes, x, y, result.roughness.width, clamp(source.roughness * inv, 0.0, 1.0));
                }

                ++local.uv_coverage;
                if (direct_mask[index] != 0)
                {
                    ++local.filled_by_direct;
                }
                else
                {
                    ++local.filled_by_extension;
                }
                if (source.floor_like)
                {
                    ++local.filled_by_floor;
                }
            }
        });

        for (const auto& local : worker_stats)
        {
            result.stats.uv_coverage += local.uv_coverage;
            result.stats.filled_by_direct += local.filled_by_direct;
            result.stats.filled_by_extension += local.filled_by_extension;
            result.stats.filled_by_floor += local.filled_by_floor;
            result.stats.preserved_original += local.preserved_original;
        }

        return result;
    }

    auto assemble_chart_aware_texture(const ChannelBuffer& albedo_before,
                                      const ChannelBuffer& metallic_before,
                                      const ChannelBuffer& roughness_before,
                                      const AtlasBuildResult& atlas,
                                      const std::vector<PaintSeed>& seeds) -> TextureAssemblyResult
    {
        TextureAssemblyResult result{};
        result.albedo = albedo_before;
        result.metallic = metallic_before;
        result.roughness = roughness_before;

        const auto texture_pixels = static_cast<std::size_t>(std::max(0, albedo_before.width)) *
                                    static_cast<std::size_t>(std::max(0, albedo_before.height));
        if (!atlas.ok || atlas.texels.size() != texture_pixels || texture_pixels == 0)
        {
            result.stats.preserved_original = static_cast<int>(texture_pixels);
            return result;
        }

        std::vector<ProjectionTexel> texels(texture_pixels);
        for (const auto& seed : seeds)
        {
            const auto priority = seed.priority > 0 ? seed.priority : 11;
            const auto weight = seed.weight > 0.0 ? seed.weight : 72.0;
            const auto radius = std::max(0, seed.radius);
            splat_projection_texel(texels,
                                   albedo_before.width,
                                   albedo_before.height,
                                   seed.u,
                                   seed.v,
                                   seed.color,
                                   weight,
                                   priority,
                                   seed.floor_like,
                                   radius);
        }

        std::vector<std::uint8_t> direct_mask(texture_pixels, 0);
        for (std::size_t index = 0; index < texture_pixels; ++index)
        {
            if ((atlas.texels[index].flags & AtlasTexelValid) == 0)
            {
                texels[index] = ProjectionTexel{};
                continue;
            }
            if (projection_texel_painted(texels[index]))
            {
                direct_mask[index] = 1;
            }
        }

        const auto fill_radius = clamp_int(
            estimate_seed_radius_for_density(albedo_before.width,
                                             albedo_before.height,
                                             static_cast<int>(std::max<std::size_t>(1, seeds.size()))) *
                4,
            8,
            32);
        auto extended_texels = texels;
        parallel_ranges(texture_pixels, [&](std::size_t begin, std::size_t end, unsigned) {
            const auto width = std::max(1, albedo_before.width);
            const auto height = std::max(1, albedo_before.height);
            for (std::size_t index = begin; index < end; ++index)
            {
                if (index >= atlas.texels.size() ||
                    (atlas.texels[index].flags & AtlasTexelValid) == 0 ||
                    direct_mask[index] != 0)
                {
                    continue;
                }
                const auto chart_id = atlas.texels[index].chart_id;
                const auto y = static_cast<int>(index / static_cast<std::size_t>(width));
                const auto x = static_cast<int>(index - static_cast<std::size_t>(y * width));
                int best_distance_sq = fill_radius * fill_radius + 1;
                std::size_t best_index = static_cast<std::size_t>(-1);
                for (int dy = -fill_radius; dy <= fill_radius; ++dy)
                {
                    const auto sy = y + dy;
                    if (sy < 0 || sy >= height)
                    {
                        continue;
                    }
                    for (int dx = -fill_radius; dx <= fill_radius; ++dx)
                    {
                        const auto sx = x + dx;
                        if (sx < 0 || sx >= width)
                        {
                            continue;
                        }
                        const auto distance_sq = dx * dx + dy * dy;
                        if (distance_sq > fill_radius * fill_radius)
                        {
                            continue;
                        }
                        const auto source_index = static_cast<std::size_t>(sy * width + sx);
                        if (source_index >= atlas.texels.size() ||
                            atlas.texels[source_index].chart_id != chart_id ||
                            direct_mask[source_index] == 0 ||
                            !projection_texel_painted(texels[source_index]))
                        {
                            continue;
                        }
                        if (distance_sq < best_distance_sq ||
                            (distance_sq == best_distance_sq && source_index < best_index))
                        {
                            best_distance_sq = distance_sq;
                            best_index = source_index;
                        }
                    }
                }
                if (best_index == static_cast<std::size_t>(-1))
                {
                    continue;
                }

                const auto& source = texels[best_index];
                const auto inv = 1.0 / source.weight;
                ProjectionTexel copy{};
                copy.r = source.r * inv;
                copy.g = source.g * inv;
                copy.b = source.b * inv;
                copy.roughness = source.roughness * inv;
                copy.metallic = source.metallic * inv;
                copy.weight = 1.0;
                copy.priority = source.priority;
                copy.floor_like = source.floor_like;
                extended_texels[index] = copy;
            }
        });
        texels.swap(extended_texels);

        result.stats.worker_threads = worker_count_for_items(texture_pixels);
        std::vector<TextureWriteStats> worker_stats(result.stats.worker_threads);
        parallel_ranges(texture_pixels, [&](std::size_t begin, std::size_t end, unsigned worker) {
            auto& local = worker_stats[static_cast<std::size_t>(worker)];
            for (std::size_t index = begin; index < end; ++index)
            {
                if (index >= atlas.texels.size() || (atlas.texels[index].flags & AtlasTexelValid) == 0)
                {
                    ++local.preserved_original;
                    continue;
                }
                const auto offset = index * 4;
                const auto& source = texels[index];
                if (!projection_texel_painted(source) || offset + 2 >= result.albedo.bytes.size())
                {
                    ++local.preserved_original;
                    continue;
                }
                const auto inv = 1.0 / source.weight;
                result.albedo.bytes[offset + 0] = byte_from_unit(clamp(source.r * inv, 0.02, 0.98));
                result.albedo.bytes[offset + 1] = byte_from_unit(clamp(source.g * inv, 0.02, 0.98));
                result.albedo.bytes[offset + 2] = byte_from_unit(clamp(source.b * inv, 0.02, 0.98));
                if (offset + 3 < result.albedo.bytes.size())
                {
                    result.albedo.bytes[offset + 3] = 255;
                }
                if (direct_mask[index] != 0)
                {
                    ++local.filled_by_direct;
                    ++local.direct_texels;
                }
                else
                {
                    ++local.filled_by_extension;
                }
            }
        });

        for (const auto& local : worker_stats)
        {
            result.stats.uv_coverage += local.filled_by_direct + local.filled_by_extension;
            result.stats.filled_by_direct += local.filled_by_direct;
            result.stats.filled_by_extension += local.filled_by_extension;
            result.stats.preserved_original += local.preserved_original;
            result.stats.direct_texels += local.direct_texels;
        }
        return result;
    }
}
