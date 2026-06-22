#include "MecchaCamouflage/core/paint_core.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Core = MecchaCamouflage::Core;

auto make_channel(int width, int height, std::uint8_t value) -> Core::ChannelBuffer
{
    return Core::ChannelBuffer{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), value)};
}

int main()
{
    {
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            false,
            3000,
            3000,
            2048,
            false,
            false,
            0.34,
            0.02,
            0.03});
        assert(!decision.ok);
        assert(decision.failure == "bulk_calibration_failed_no_paint");
    }

    {
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            true,
            3000,
            3000,
            2048,
            false,
            false,
            0.05,
            0.40,
            0.42});
        assert(decision.ok);
        assert(decision.failure == "ok");
    }

    {
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            true,
            3000,
            3000,
            2048,
            false,
            false,
            0.05,
            0.57,
            0.77});
        assert(!decision.ok);
        assert(decision.failure == "capture_trace_chroma_failed_no_paint");
        const auto report = Core::evaluate_capture_quality(Core::CaptureQualityInput{
            true,
            true,
            3000,
            3000,
            2048,
            false,
            false,
            0.05,
            0.57,
            0.77});
        assert(report.chroma_validation_failed);
        assert(!report.low_luma_suspect);
    }

    {
        auto input = Core::CaptureQualityInput{
            true,
            true,
            3000,
            3000,
            2048,
            false,
            false,
            0.05,
            0.02,
            0.03};
        input.capture_rgb_max = 0.015;
        input.capture_rgb_range = 0.014;
        input.capture_luma_range = 0.003;
        const auto decision = Core::validate_capture_quality(input);
        assert(!decision.ok);
        assert(decision.failure == "capture_low_dynamic_range_no_paint");
        const auto report = Core::evaluate_capture_quality(input);
        assert(report.low_luma_suspect);
        assert(!report.chroma_validation_failed);
    }

    {
        const auto distance = Core::chroma_distance_rgb(
            Core::Color{0.399658, 0.0, 0.009842, 0.9, 0.0},
            Core::Color{0.30047, 0.30227, 0.266078, 0.9, 0.0});
        assert(distance < Core::MaxCaptureTraceChromaP95);
    }

    {
        assert(!Core::is_floor_like_label("StaticMeshActor /Game/stage_level/cLeon_game.cLeon_game:PersistentLevel.StageWall_01"));
        assert(Core::is_floor_like_label("StaticMeshActor /Game/map.PersistentLevel.FloorTile_04"));
    }

    {
        const auto resolved = Core::resolve_material_channels(Core::MaterialResolveInput{
            0.43,
            0.21,
            false,
            0.90,
            false,
            0.0,
            true});
        assert(resolved.roughness == 0.43);
        assert(resolved.metallic == 0.21);
        assert(resolved.confidence == Core::MaterialConfidence::PreservedOriginal);
    }

    {
        const auto resolved = Core::resolve_material_channels(Core::MaterialResolveInput{
            0.43,
            0.21,
            true,
            0.91,
            true,
            0.34,
            false});
        assert(resolved.roughness == 0.91);
        assert(resolved.metallic == 0.34);
        assert(resolved.confidence == Core::MaterialConfidence::ScalarParameter);
    }

    {
        Core::FrameBudget budget{4.0};
        assert(!budget.consume(1.5));
        assert(!budget.consume(2.0));
        assert(budget.consume(0.6));
        assert(budget.overrun);
    }

    {
        Core::FrameBudget budget{4.0, 8.0};
        assert(!budget.consume(3.0));
        assert(budget.consume(1.2));
        assert(!budget.hard_overrun);
        assert(budget.consume(4.1));
        assert(budget.hard_overrun);
    }

    {
        const auto first = Core::choose_capture_dimensions(Core::CaptureSizingInput{
            3440,
            1440,
            1024,
            1024,
            0});
        assert(first.width == 2048);
        assert(first.height == 857);
        assert(!first.uses_viewport_size);
        assert(first.reason == "texture_limited_viewport");

        const auto retry = Core::choose_capture_dimensions(Core::CaptureSizingInput{
            3440,
            1440,
            1024,
            1024,
            1});
        assert(retry.width == 1024);
        assert(retry.height == 429);
        assert(!retry.uses_viewport_size);
        assert(retry.reason == "texture_limited_retry_ladder");

        const auto normal_viewport = Core::choose_capture_dimensions(Core::CaptureSizingInput{
            1920,
            1080,
            1024,
            1024,
            0});
        assert(normal_viewport.width == 1920);
        assert(normal_viewport.height == 1080);
        assert(normal_viewport.uses_viewport_size);
        assert(normal_viewport.reason == "viewport_size");

        const auto required_viewport = Core::choose_capture_dimensions(Core::CaptureSizingInput{
            3440,
            1440,
            1024,
            1024,
            0,
            true});
        assert(required_viewport.width == 3440);
        assert(required_viewport.height == 1440);
        assert(required_viewport.uses_viewport_size);
        assert(required_viewport.reason == "viewport_size_required");
    }

    {
        const auto small = Core::choose_adaptive_sampling_policy(Core::AdaptiveSamplingInput{
            1280,
            720,
            512,
            512,
            360.0,
            520.0,
            1200,
            0,
            0});
        const auto large = Core::choose_adaptive_sampling_policy(Core::AdaptiveSamplingInput{
            3440,
            1440,
            2048,
            2048,
            1100.0,
            1280.0,
            9000,
            0,
            0});
        assert(large.target_front_hits > small.target_front_hits);
        assert(large.hard_max_attempts > small.hard_max_attempts);
        assert(large.target_side_seeds > 512);
        assert(large.side_view_count >= small.side_view_count);
        assert(large.refine_grid_x >= 24);
        assert(large.refine_grid_y >= 24);
    }

    {
        const auto order = Core::stratified_grid_order(5, 4, 4);
        assert(order.size() == 4);
        std::vector<int> rows{};
        for (const auto linear : order)
        {
            rows.push_back(linear / 5);
        }
        assert(rows[0] == 0);
        assert(rows[1] == 1);
        assert(rows[2] == 2);
        assert(rows[3] == 3);
        assert(Core::stratified_grid_linear_index(5, 4, 4) == 1);
    }

    {
        const auto latest_log_like = Core::evaluate_front_coverage(Core::FrontCoverageInput{
            0.4537,
            0.35,
            0.5463,
            0.6167,
            0.4419,
            0.3326,
            0.5581,
            0.4071,
            140,
            117,
            5853,
            2048,
            5853,
            16361,
            16380,
            32,
            117,
            true});
        assert(!latest_log_like.ok);
        assert(latest_log_like.failed);
        assert(!latest_log_like.reaches_coarse_bottom);
        assert(latest_log_like.failure == "front_coverage_bottom_not_reached");

        const auto latest_right_edge_gap = Core::evaluate_front_coverage(Core::FrontCoverageInput{
            0.4537,
            0.35,
            0.5463,
            0.6167,
            0.4419,
            0.3326,
            0.5458,
            0.6282,
            256,
            256,
            21688,
            2048,
            5853,
            58461,
            65536,
            226,
            256,
            true});
        assert(!latest_right_edge_gap.ok);
        assert(latest_right_edge_gap.failed);
        assert(!latest_right_edge_gap.refined_grid_complete);
        assert(latest_right_edge_gap.failure == "front_coverage_grid_incomplete");

        const auto covered_front = Core::evaluate_front_coverage(Core::FrontCoverageInput{
            0.45,
            0.35,
            0.55,
            0.62,
            0.449,
            0.348,
            0.551,
            0.619,
            96,
            96,
            7000,
            2048,
            5853,
            9216,
            9216,
            90,
            96,
            false});
        assert(covered_front.ok);
        assert(!covered_front.failed);
        assert(covered_front.reaches_coarse_bottom);
        assert(covered_front.refined_grid_complete);
        assert(covered_front.failure == "ok");
    }

    {
        const auto dev_partial = Core::plan_replicated_stroke_apply(Core::ReplicatedStrokePlanInput{
            5853,
            5854,
            24,
            true,
            true});
        assert(dev_partial.ok);
        assert(dev_partial.partial);
        assert(!dev_partial.quality_success);
        assert(dev_partial.failure == "dev_replicated_partial_apply");
    }

    {
        const auto views = Core::generate_golden_angle_views(12, 35.0);
        assert(views.size() == 12);
        for (const auto& view : views)
        {
            assert(view.yaw_degrees >= -180.0);
            assert(view.yaw_degrees <= 180.0);
            assert(std::abs(view.pitch_degrees) <= 35.0);
        }
        assert(views[0].yaw_degrees != views[1].yaw_degrees);
    }

    {
        const auto latest_log_like = Core::evaluate_uv_coverage(Core::UvCoverageInput{
            1024,
            1024,
            290919,
            180000,
            512,
            true,
            640,
            1024});
        assert(!latest_log_like.ok);
        assert(latest_log_like.failure == "coverage_failed_side_budget_exhausted_no_import");
        assert(latest_log_like.coverage_ratio > 0.27);
        assert(latest_log_like.coverage_ratio < 0.28);

        const auto current_map_run = Core::evaluate_uv_coverage(Core::UvCoverageInput{
            1024,
            1024,
            427784,
            117106,
            2323,
            true,
            1,
            5396});
        assert(current_map_run.ok);
        assert(current_map_run.failure == "ok");
        assert(current_map_run.coverage_ratio > 0.40);

        const auto valid = Core::evaluate_uv_coverage(Core::UvCoverageInput{
            1024,
            1024,
            735000,
            420000,
            2600,
            false,
            900,
            12000});
        assert(valid.ok);
        assert(valid.failure == "ok");
        assert(valid.coverage_ratio > 0.70);
    }

    {
        const auto sparse = Core::estimate_seed_radius_for_density(1024, 1024, 512);
        const auto dense = Core::estimate_seed_radius_for_density(1024, 1024, 8192);
        assert(sparse > dense);
        assert(dense >= 1);

        const auto precision = Core::choose_precision_brush_radius(Core::PrecisionBrushInput{
            1024,
            1024,
            3,
            0.02,
            0.20,
            true});
        assert(std::abs(precision.requested_radius - (3.0 / 1024.0)) < 0.000001);
        assert(std::abs(precision.radius - (3.0 / 1024.0)) < 0.000001);
        assert(!precision.clamped_by_game_min);
        assert(precision.source == "density_precision");

        const auto game_clamped = Core::choose_precision_brush_radius(Core::PrecisionBrushInput{
            1024,
            1024,
            3,
            0.02,
            0.20,
            false});
        assert(game_clamped.radius == 0.02);
        assert(game_clamped.clamped_by_game_min);
        assert(game_clamped.source == "game_min_clamped");
    }

    {
        std::vector<Core::PaintSeed> seeds{};
        seeds.push_back(Core::PaintSeed{0.1000, 0.1000, Core::Color{1.0, 0.0, 0.0, 0.5, 0.0}});
        seeds.push_back(Core::PaintSeed{0.1010, 0.1005, Core::Color{0.0, 1.0, 0.0, 0.7, 0.2}});
        seeds.push_back(Core::PaintSeed{0.2500, 0.1000, Core::Color{0.0, 0.0, 1.0, 0.9, 0.4}});
        const auto merged = Core::merge_nearby_paint_seeds(seeds, 0.003);
        assert(merged.size() == 2);
        assert(std::abs(merged[0].u - 0.1005) < 0.000001);
        assert(std::abs(merged[0].v - 0.10025) < 0.000001);
        assert(std::abs(merged[0].color.r - 0.5) < 0.000001);
        assert(std::abs(merged[0].color.g - 0.5) < 0.000001);
        assert(std::abs(merged[0].color.roughness - 0.6) < 0.000001);
        assert(std::abs(merged[0].color.metallic - 0.1) < 0.000001);
        assert(std::abs(merged[1].u - 0.25) < 0.000001);
    }

    {
        Core::PipelineJob job{};
        job.id = 42;
        job.stage = Core::PipelineStage::RefinedHit;
        job.no_import = true;
        job.failure = "budget_stop";
        job.timing.refined_hit_ms = 18.0;
        assert(job.id == 42);
        assert(job.stage == Core::PipelineStage::RefinedHit);
        assert(job.no_import);
        assert(!job.fallback_used);
        assert(job.timing.refined_hit_ms == 18.0);
    }

    {
        const auto before = make_channel(8, 8, 17);
        const auto decision = Core::validate_capture_quality(Core::CaptureQualityInput{
            true,
            false,
            3000,
            3000,
            2048,
            false,
            false,
            0.40,
            0.02,
            0.03});
        auto albedo_after = before;
        if (!decision.ok)
        {
            albedo_after = before;
        }
        assert(Core::hash_bytes(before.bytes) == Core::hash_bytes(albedo_after.bytes));
    }

    {
        const auto albedo = make_channel(16, 16, 0);
        const auto metallic = make_channel(16, 16, 0);
        const auto roughness = make_channel(16, 16, 0);
        const std::vector<Core::PaintSeed> seeds{
            Core::PaintSeed{0.5, 0.5, Core::Color{0.2, 0.3, 0.4, 0.8, 0.1}, false}};
        const auto result = Core::assemble_direct_texture(albedo, metallic, roughness, seeds);
        assert(result.stats.uv_coverage > 0);
        assert(result.stats.filled_by_extension > 0);
        assert(result.stats.direct_texels > 0);
        assert(result.stats.filled_by_direct == result.stats.direct_texels);
        bool has_white = false;
        for (std::size_t i = 0; i + 2 < result.albedo.bytes.size(); i += 4)
        {
            if (result.albedo.bytes[i] == 255 && result.albedo.bytes[i + 1] == 255 && result.albedo.bytes[i + 2] == 255)
            {
                has_white = true;
                break;
            }
        }
        assert(!has_white);
    }

    {
        const auto albedo = make_channel(64, 64, 0);
        const auto metallic = make_channel(64, 64, 0);
        const auto roughness = make_channel(64, 64, 0);
        const Core::Color color{0.2, 0.3, 0.4, 0.8, 0.1};
        const std::vector<Core::PaintSeed> small_radius{
            Core::PaintSeed{0.5, 0.5, color, false, 11, 2, 72.0}};
        const std::vector<Core::PaintSeed> larger_radius{
            Core::PaintSeed{0.5, 0.5, color, false, 11, 4, 72.0}};
        const auto small = Core::assemble_direct_texture(albedo, metallic, roughness, small_radius);
        const auto large = Core::assemble_direct_texture(albedo, metallic, roughness, larger_radius);
        assert(large.stats.direct_texels > small.stats.direct_texels);
        assert(large.stats.uv_coverage > small.stats.uv_coverage);
    }

    {
        auto unavailable = Core::evaluate_runtime_atlas_probe(Core::RuntimeAtlasProbeReport{
            false,
            "runtime_probe",
            "not_run",
            1024,
            1024,
            0,
            0,
            0,
            0});
        assert(!unavailable.ok);
        assert(unavailable.failure == "atlas_source_unavailable_no_import");

        auto ok = Core::evaluate_runtime_atlas_probe(Core::RuntimeAtlasProbeReport{
            false,
            "runtime_probe",
            "not_run",
            1024,
            1024,
            900000,
            24,
            100,
            10});
        assert(ok.ok);
        assert(ok.failure == "ok");
        assert(ok.overlap_ratio < 0.01);
    }

    {
        const auto low = Core::evaluate_atlas_coverage(Core::AtlasCoverageInput{
            1024,
            1024,
            900000,
            60000,
            700000,
            12,
            0.91,
            0.91,
            0.91,
            0.91});
        assert(!low.ok);
        assert(low.failure == "coverage_failed_direct_evidence_no_import");

        const auto lower_body_missing = Core::evaluate_atlas_coverage(Core::AtlasCoverageInput{
            1024,
            1024,
            900000,
            160000,
            650000,
            12,
            0.89,
            0.40,
            0.90,
            0.90});
        assert(!lower_body_missing.ok);
        assert(lower_body_missing.failure == "coverage_failed_lower_body_no_import");
    }

    {
        const int width = 8;
        const int height = 4;
        std::vector<std::uint8_t> valid(static_cast<std::size_t>(width * height), 1);
        std::vector<int> charts(static_cast<std::size_t>(width * height), 0);
        std::vector<std::uint8_t> direct(static_cast<std::size_t>(width * height), 0);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto index = static_cast<std::size_t>(y * width + x);
                charts[index] = x < 4 ? 0 : 1;
            }
        }
        direct[static_cast<std::size_t>(1 * width + 1)] = 1;
        const auto atlas = Core::build_runtime_atlas_from_masks(Core::AtlasBuildInput{
            width,
            height,
            valid,
            charts,
            direct});
        assert(atlas.ok);
        assert(atlas.charts.size() == 2);
        assert(atlas.charts[0].direct_texels == 1);
        assert(atlas.charts[1].direct_texels == 0);

        const auto albedo = make_channel(width, height, 7);
        const auto metallic = make_channel(width, height, 11);
        const auto roughness = make_channel(width, height, 13);
        const std::vector<Core::PaintSeed> seeds{
            Core::PaintSeed{1.0 / 7.0, 1.0 / 3.0, Core::Color{0.8, 0.1, 0.1, 0.5, 0.0}, false, 11, 1, 72.0}};
        const auto result = Core::assemble_chart_aware_texture(albedo, metallic, roughness, atlas, seeds);
        bool right_chart_changed = false;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 4; x < width; ++x)
            {
                const auto offset = static_cast<std::size_t>(y * width + x) * 4;
                if (result.albedo.bytes[offset] != 7 || result.albedo.bytes[offset + 1] != 7 || result.albedo.bytes[offset + 2] != 7)
                {
                    right_chart_changed = true;
                }
            }
        }
        assert(!right_chart_changed);
        assert(result.stats.filled_by_extension > 0);
    }

    {
        Core::RuntimeCapabilities release_caps{};
        release_caps.import_channel_from_bytes = true;
        const auto release_backend = Core::choose_apply_backend(release_caps, false);
        assert(!release_backend.ok);
        assert(release_backend.blocking_only);
        assert(release_backend.failure == "apply_backend_unavailable_no_import");

        release_caps.chunked_paint_api = true;
        const auto local_only_backend = Core::choose_apply_backend(release_caps, false);
        assert(!local_only_backend.ok);
        assert(local_only_backend.failure == "replicated_apply_unavailable_no_import");

        release_caps.server_paint_api = true;
        const auto chunked_backend = Core::choose_apply_backend(release_caps, false);
        assert(chunked_backend.ok);
        assert(chunked_backend.backend == Core::ApplyBackend::ChunkedPaintApi);
    }

    {
        const auto no_backend = Core::plan_replicated_stroke_apply(Core::ReplicatedStrokePlanInput{
            100,
            50,
            0,
            true,
            false});
        assert(!no_backend.ok);
        assert(no_backend.failure == "replicated_apply_unavailable_no_apply");

        const auto release_low_coverage = Core::plan_replicated_stroke_apply(Core::ReplicatedStrokePlanInput{
            40,
            80,
            24,
            false,
            true});
        assert(!release_low_coverage.ok);
        assert(release_low_coverage.failure == "coverage_failed_no_apply");

        const auto dev_partial = Core::plan_replicated_stroke_apply(Core::ReplicatedStrokePlanInput{
            40,
            80,
            0,
            true,
            true});
        assert(dev_partial.ok);
        assert(dev_partial.partial);
        assert(!dev_partial.quality_success);
        assert(dev_partial.strokes_per_tick == 24);

        const auto complete = Core::plan_replicated_stroke_apply(Core::ReplicatedStrokePlanInput{
            160,
            80,
            256,
            false,
            true});
        assert(complete.ok);
        assert(!complete.partial);
        assert(complete.quality_success);
        assert(complete.strokes_per_tick == 128);
    }

    {
        const auto first = Core::plan_sampled_readback_tick(Core::SampledReadbackTickInput{
            10,
            0,
            10,
            1.5,
            4.0,
            8.0});
        assert(first.next_cursor == 3);
        assert(first.samples_this_tick == 3);
        assert(!first.complete);
        assert(first.frame_budget_overrun);
        assert(!first.hard_budget_overrun);

        const auto second = Core::plan_sampled_readback_tick(Core::SampledReadbackTickInput{
            10,
            first.next_cursor,
            2,
            0.1,
            4.0,
            8.0});
        assert(second.next_cursor == 5);
        assert(second.samples_this_tick == 2);
        assert(!second.frame_budget_overrun);

        const auto final = Core::plan_sampled_readback_tick(Core::SampledReadbackTickInput{
            10,
            9,
            64,
            0.05,
            4.0,
            8.0});
        assert(final.next_cursor == 10);
        assert(final.complete);
    }

    {
        std::vector<Core::SurfaceStretchSeed> seeds{};
        seeds.push_back(Core::SurfaceStretchSeed{
            0.10,
            0.10,
            100.0,
            100.0,
            0.0,
            0.0,
            1.0,
            Core::Color{1.0, 0.0, 0.0, 0.5, 0.0},
            true,
            1});
        seeds.push_back(Core::SurfaceStretchSeed{
            0.12,
            0.10,
            120.0,
            102.0,
            0.0,
            0.0,
            1.0,
            Core::Color{0.0, 0.0, 1.0, 0.7, 0.2},
            true,
            1});
        const auto report = Core::infer_surface_stretch_seeds(seeds, Core::SurfaceStretchPolicy{
            0.03,
            32.0,
            0.90,
            8});
        assert(report.direct_preserved == 2);
        assert(report.inferred.size() == 1);
        assert(std::abs(report.inferred[0].u - 0.11) < 0.000001);
        assert(std::abs(report.inferred[0].color.r - 0.5) < 0.000001);
        assert(std::abs(report.inferred[0].color.b - 0.5) < 0.000001);
        assert(report.inferred[0].priority < 0);

        seeds.push_back(Core::SurfaceStretchSeed{
            0.90,
            0.90,
            400.0,
            400.0,
            1.0,
            0.0,
            0.0,
            Core::Color{0.2, 0.2, 0.2, 0.5, 0.0},
            true,
            1});
        const auto limited = Core::infer_surface_stretch_seeds(seeds, Core::SurfaceStretchPolicy{
            0.03,
            32.0,
            0.90,
            16});
        assert(limited.rejected_seam > 0);
        assert(limited.normal_limit == 0);

        seeds[1].normal_x = 1.0;
        seeds[1].normal_z = 0.0;
        const auto normal_limited = Core::infer_surface_stretch_seeds(seeds, Core::SurfaceStretchPolicy{
            0.03,
            32.0,
            0.90,
            16});
        assert(normal_limited.normal_limit > 0);
    }

    {
        std::vector<Core::MicroFillSource> sources{};
        sources.push_back(Core::MicroFillSource{
            0.100,
            0.100,
            100.0,
            100.0,
            0.0,
            0.0,
            1.0,
            Core::Color{1.0, 0.0, 0.0, 0.5, 0.0},
            true,
            true,
            1});
        sources.push_back(Core::MicroFillSource{
            0.106,
            0.100,
            124.0,
            102.0,
            0.0,
            0.0,
            1.0,
            Core::Color{0.0, 0.0, 1.0, 0.7, 0.2},
            true,
            true,
            1});
        const auto fill = Core::plan_front_micro_fill(sources, Core::MicroFillPolicy{
            0.002,
            0.012,
            48.0,
            0.90,
            4,
            8});
        assert(fill.direct_preserved == 2);
        assert(!fill.candidates.empty());
        assert(fill.candidates.size() <= 2);
        assert(fill.candidates[0].priority < 0);
        assert(fill.candidates[0].u > 0.100);
        assert(fill.candidates[0].u < 0.106);
        assert(std::abs(fill.candidates[0].color.r + fill.candidates[0].color.b - 1.0) < 0.000001);

        sources[1].verified = false;
        const auto unverified = Core::plan_front_micro_fill(sources, Core::MicroFillPolicy{
            0.002,
            0.012,
            48.0,
            0.90,
            4,
            8});
        assert(unverified.candidates.empty());
        assert(unverified.rejected_unverified_source > 0);

        sources[1].verified = true;
        sources[1].normal_x = 1.0;
        sources[1].normal_z = 0.0;
        const auto normal_rejected = Core::plan_front_micro_fill(sources, Core::MicroFillPolicy{
            0.002,
            0.012,
            48.0,
            0.90,
            4,
            8});
        assert(normal_rejected.candidates.empty());
        assert(normal_rejected.rejected_normal > 0);
    }

    {
        const auto low_side = Core::evaluate_side_coverage(Core::SideCoverageInput{
            true,
            48,
            40,
            128,
            true});
        assert(low_side.front_quality_success);
        assert(!low_side.side_quality_success);
        assert(low_side.side_quality_failed);
        assert(low_side.failure == "side_coverage_failed_budget_exhausted");

        const auto enough_side = Core::evaluate_side_coverage(Core::SideCoverageInput{
            true,
            512,
            80,
            128,
            false});
        assert(enough_side.front_quality_success);
        assert(enough_side.side_quality_success);
        assert(!enough_side.side_quality_failed);
        assert(enough_side.inferred_ratio > 0.15);
    }

    {
        const auto preserved = Core::resolve_verified_material_evidence(Core::MaterialEvidence{},
                                                                        0.74,
                                                                        0.12);
        assert(preserved.roughness == 0.74);
        assert(preserved.metallic == 0.12);
        assert(preserved.confidence == Core::MaterialConfidence::PreservedOriginal);

        Core::MaterialEvidence evidence{};
        evidence.has_base_color_texture = true;
        evidence.has_readable_texture = true;
        evidence.has_roughness_scalar = true;
        evidence.scalar_roughness = 0.35;
        evidence.has_metallic_scalar = true;
        evidence.scalar_metallic = 0.02;
        const auto resolved = Core::resolve_verified_material_evidence(evidence, 0.74, 0.12);
        assert(resolved.roughness == 0.35);
        assert(resolved.metallic == 0.02);
        assert(resolved.confidence == Core::MaterialConfidence::TextureParameter);

        assert(!Core::should_send_material_channels(Core::MaterialConfidence::Unknown));
        assert(!Core::should_send_material_channels(Core::MaterialConfidence::PreservedOriginal));
        assert(Core::should_send_material_channels(Core::MaterialConfidence::ScalarParameter));
        assert(Core::should_send_material_channels(Core::MaterialConfidence::TextureParameter));
    }

    return 0;
}
