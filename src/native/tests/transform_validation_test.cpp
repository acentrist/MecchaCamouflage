#include "../include/sdk.hpp"
#include "../include/runtime_contract.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace
{
#include "../bridge/bridge_json.inc"
}

int main()
{
    struct DuplicateUvTriangle
    {
        int geometry_id{0};
        double u{0.0};
        double v{0.0};
    };
    struct DuplicateUvMatch
    {
        bool ok{false};
        double error{1000000.0};
    };
    const std::vector<DuplicateUvTriangle> duplicated_uv_runtime{{
        {101, 0.25, 0.75},
        {202, 0.25, 0.75},
    }};
    std::vector<DuplicateUvTriangle> duplicated_uv_ordered{};
    double duplicated_uv_average_error = 0.0;
    const auto duplicated_uv_direct_index_ok =
        runtime_contract::order_runtime_triangles_by_direct_profile_index(
            duplicated_uv_runtime,
            2,
            [](int profile_triangle, const DuplicateUvTriangle& runtime) {
                static constexpr std::array<double, 2> expected_u{{0.25, 0.25}};
                static constexpr std::array<double, 2> expected_v{{0.75, 0.75}};
                return DuplicateUvMatch{
                    std::abs(runtime.u - expected_u[static_cast<std::size_t>(profile_triangle)]) <= 0.000001 &&
                        std::abs(runtime.v - expected_v[static_cast<std::size_t>(profile_triangle)]) <= 0.000001,
                    0.0};
            },
            [](const DuplicateUvTriangle& runtime, const DuplicateUvMatch&) { return runtime; },
            duplicated_uv_ordered,
            duplicated_uv_average_error);
    if (!duplicated_uv_direct_index_ok || duplicated_uv_ordered.size() != 2 ||
        duplicated_uv_ordered[0].geometry_id != 101 ||
        duplicated_uv_ordered[1].geometry_id != 202 ||
        duplicated_uv_average_error != 0.0)
    {
        return 31;
    }

    if (json_string_field(R"({"image_paint_rgba_base64":"AA\u002BAA=="})", "image_paint_rgba_base64") != "AA+AA==" ||
        json_string_field(R"({"label":"\u3042\uD83D\uDE00"})", "label") !=
            std::string("\xE3\x81\x82\xF0\x9F\x98\x80"))
    {
        return 28;
    }

    sdk::FTransform valid{};
    valid.Rotation = {0.0, 0.0, 0.705717, 0.708494};
    valid.Translation = {-295.483835, 6223.716973, 8.323874};
    valid.Scale3D = {1.1, 1.1, 1.1};

    sdk::FTransform malformed = valid;
    malformed.Rotation = {0.0, 0.0, -2889820.0, 11160673.0};

    if (!sdk::transform_is_plausible(valid))
    {
        return 1;
    }
    if (sdk::transform_is_plausible(malformed))
    {
        return 2;
    }

    std::array<std::uint8_t, 0x48> fake_property{};
    const std::int32_t array_dim = 1;
    const std::int32_t element_size = 0x20;
    const std::uint64_t property_flags = 0x0018001000000000ULL;
    std::memcpy(fake_property.data() + 0x30, &array_dim, sizeof(array_dim));
    std::memcpy(fake_property.data() + runtime_contract::FPropertyElementSizeOffset,
                &element_size,
                sizeof(element_size));
    std::memcpy(fake_property.data() + 0x38, &property_flags, sizeof(property_flags));
    std::int32_t decoded_element_size = 0;
    std::memcpy(&decoded_element_size,
                fake_property.data() + runtime_contract::FPropertyElementSizeOffset,
                sizeof(decoded_element_size));
    if (decoded_element_size != element_size || runtime_contract::FPropertyElementSizeOffset != 0x34)
    {
        return 3;
    }

    if (!runtime_contract::uobject_flags_usable(0, 0) ||
        runtime_contract::uobject_flags_usable(runtime_contract::RFClassDefaultObject, 0) ||
        runtime_contract::uobject_flags_usable(runtime_contract::RFBeginDestroyed, 0) ||
        runtime_contract::uobject_flags_usable(runtime_contract::RFFinishDestroyed, 0) ||
        runtime_contract::uobject_flags_usable(runtime_contract::RFMirroredGarbage, 0) ||
        runtime_contract::uobject_flags_usable(0, runtime_contract::RFBeginDestroyed) ||
        runtime_contract::uobject_flags_usable(0, runtime_contract::RFFinishDestroyed) ||
        runtime_contract::uobject_flags_usable(0, runtime_contract::RFMirroredGarbage) ||
        !runtime_contract::uobject_flags_usable(0x20000000u, 0))
    {
        return 5;
    }

    if (!runtime_contract::event_watch_generation_active(true, 7, 7) ||
        runtime_contract::event_watch_generation_active(false, 7, 7) ||
        runtime_contract::event_watch_generation_active(true, 8, 7))
    {
        return 9;
    }

    const runtime_contract::ImageAtlasMappingInput round_front{
        false, runtime_contract::ImageAtlasRegion::Front, false,
        0.0, 0.80, 0.50, 0.0, 0.0, 1.0,
        0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
    const auto round_front_coordinate = runtime_contract::map_image_atlas_coordinate(round_front);
    const runtime_contract::ImageAtlasMappingInput cube_side{
        true, runtime_contract::ImageAtlasRegion::Side, false,
        0.75, 0.90, 0.25, 0.0, 1.0, 0.0,
        0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
    const auto cube_side_coordinate = runtime_contract::map_image_atlas_coordinate(cube_side);
    const runtime_contract::ImageAtlasMappingInput cube_top{
        true, runtime_contract::ImageAtlasRegion::Front, false,
        1.0, 0.50, 0.25, 0.0, 0.0, 1.0,
        0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
    const runtime_contract::ImageAtlasMappingInput cube_bottom{
        true, runtime_contract::ImageAtlasRegion::Front, false,
        0.50, 0.0, 0.25, 0.0, 0.0, -1.0,
        0.0, 1.0, 0.0, 1.0, 0.0, 1.0};
    const auto cube_top_coordinate = runtime_contract::map_image_atlas_coordinate(cube_top);
    const auto cube_bottom_coordinate = runtime_contract::map_image_atlas_coordinate(cube_bottom);
    if (std::abs(round_front_coordinate.u - 0.20) > 0.000001 ||
        std::abs(round_front_coordinate.v - 0.50) > 0.000001 ||
        round_front_coordinate.cube_edge || round_front_coordinate.cube_side ||
        std::abs(cube_side_coordinate.u - 0.4375) > 0.000001 ||
        std::abs(cube_side_coordinate.v - 0.25) > 0.000001 ||
        !cube_side_coordinate.cube_side || cube_side_coordinate.cube_edge ||
        std::abs(cube_top_coordinate.u - 0.50) > 0.000001 ||
        cube_top_coordinate.v != 0.0 || !cube_top_coordinate.cube_edge ||
        std::abs(cube_bottom_coordinate.u - 0.25) > 0.000001 ||
        cube_bottom_coordinate.v != 1.0 || !cube_bottom_coordinate.cube_edge)
    {
        return 27;
    }

    const runtime_contract::CubeCanonicalImageProjectionInput cube_canonical_front{
        10.0, 3.0, 20.0, 0.0, -1.0, 0.0, 0.0, 0.0, 2.0};
    const runtime_contract::CubeCanonicalImageProjectionInput cube_canonical_right{
        3.0, 10.0, 20.0, 1.0, 0.0, 0.0, 0.0, 0.0, 2.0};
    const runtime_contract::CubeCanonicalImageProjectionInput cube_canonical_back{
        10.0, 3.0, 20.0, 0.0, 1.0, 0.0, 0.0, 0.0, 2.0};
    const runtime_contract::CubeCanonicalImageProjectionInput cube_canonical_left{
        3.0, 10.0, 20.0, -1.0, 0.0, 0.0, 0.0, 0.0, 2.0};
    const auto cube_canonical_front_coordinate = runtime_contract::map_cube_canonical_image_coordinate(cube_canonical_front);
    const auto cube_canonical_right_coordinate = runtime_contract::map_cube_canonical_image_coordinate(cube_canonical_right);
    const auto cube_canonical_back_coordinate = runtime_contract::map_cube_canonical_image_coordinate(cube_canonical_back);
    const auto cube_canonical_left_coordinate = runtime_contract::map_cube_canonical_image_coordinate(cube_canonical_left);
    if (cube_canonical_front_coordinate.face != runtime_contract::CubeCanonicalImageFace::Front ||
        cube_canonical_right_coordinate.face != runtime_contract::CubeCanonicalImageFace::Right ||
        cube_canonical_back_coordinate.face != runtime_contract::CubeCanonicalImageFace::Back ||
        cube_canonical_left_coordinate.face != runtime_contract::CubeCanonicalImageFace::Left ||
        std::abs(cube_canonical_front_coordinate.u - (148.0 / 1024.0)) > 0.000001 ||
        std::abs(cube_canonical_right_coordinate.u - (404.0 / 1024.0)) > 0.000001 ||
        std::abs(cube_canonical_back_coordinate.u - (620.0 / 1024.0)) > 0.000001 ||
        std::abs(cube_canonical_left_coordinate.u - (876.0 / 1024.0)) > 0.000001 ||
        std::abs(cube_canonical_front_coordinate.v - (296.0 / 512.0)) > 0.000001 ||
        std::abs(cube_canonical_right_coordinate.v - cube_canonical_front_coordinate.v) > 0.000001)
    {
        return 29;
    }

    const runtime_contract::RoundCanonicalImageProjectionInput round_canonical_front{
        runtime_contract::ImageAtlasRegion::Front, true,
        10.0, -3.0, 20.0, 0.0, 0.0, 0.0, 2.0};
    const runtime_contract::RoundCanonicalImageProjectionInput round_canonical_right{
        runtime_contract::ImageAtlasRegion::Side, true,
        10.0, 3.0, 20.0, 0.0, 0.0, 0.0, 2.0};
    const runtime_contract::RoundCanonicalImageProjectionInput round_canonical_back{
        runtime_contract::ImageAtlasRegion::Back, true,
        10.0, -3.0, 20.0, 0.0, 0.0, 0.0, 2.0};
    const runtime_contract::RoundCanonicalImageProjectionInput round_canonical_left{
        runtime_contract::ImageAtlasRegion::Side, true,
        -10.0, 3.0, 20.0, 0.0, 0.0, 0.0, 2.0};
    const auto round_canonical_front_coordinate = runtime_contract::map_round_canonical_image_coordinate(round_canonical_front);
    const auto round_canonical_right_coordinate = runtime_contract::map_round_canonical_image_coordinate(round_canonical_right);
    const auto round_canonical_back_coordinate = runtime_contract::map_round_canonical_image_coordinate(round_canonical_back);
    const auto round_canonical_left_coordinate = runtime_contract::map_round_canonical_image_coordinate(round_canonical_left);
    if (round_canonical_front_coordinate.tile != 0 ||
        round_canonical_right_coordinate.tile != 1 ||
        round_canonical_back_coordinate.tile != 2 ||
        round_canonical_left_coordinate.tile != 3 ||
        std::abs(round_canonical_front_coordinate.u - (148.0 / 1024.0)) > 0.000001 ||
        std::abs(round_canonical_right_coordinate.u - (390.0 / 1024.0)) > 0.000001 ||
        std::abs(round_canonical_back_coordinate.u - (620.0 / 1024.0)) > 0.000001 ||
        std::abs(round_canonical_left_coordinate.u - (890.0 / 1024.0)) > 0.000001 ||
        std::abs(round_canonical_front_coordinate.v - (296.0 / 512.0)) > 0.000001 ||
        std::abs(round_canonical_right_coordinate.v - round_canonical_front_coordinate.v) > 0.000001)
    {
        return 30;
    }

    if (runtime_contract::paint_channel_write_cost(4) != 4 ||
        runtime_contract::paint_channel_write_cost(5) != 3 ||
        runtime_contract::paint_channel_write_cost(7) != 1 ||
        runtime_contract::paint_channel_write_cost(0) != 1 ||
        !runtime_contract::local_dispatch_can_append(0, 0, 4, 6, 6) ||
        runtime_contract::local_dispatch_can_append(1, 4, 4, 6, 6) ||
        !runtime_contract::local_dispatch_cpu_budget_reached(1, 4'000) ||
        runtime_contract::local_dispatch_cpu_budget_reached(0, 10'000) ||
        runtime_contract::recurring_scheduler_delay_ms(0) != 1)
    {
        return 10;
    }

    std::array<runtime_contract::SpatialScanlineKey, 4> scanline{{
        {runtime_contract::spatial_scanline_row(100.0, 100.0, 10.0), 10.0, 0},
        {runtime_contract::spatial_scanline_row(100.0, 90.0, 10.0), -20.0, 1},
        {runtime_contract::spatial_scanline_row(100.0, 100.0, 10.0), -10.0, 2},
        {runtime_contract::spatial_scanline_row(100.0, 100.0, 10.0), -10.0, 3},
    }};
    std::stable_sort(scanline.begin(), scanline.end(), runtime_contract::spatial_scanline_less);
    if (scanline[0].original_ordinal != 2 || scanline[1].original_ordinal != 3 ||
        scanline[2].original_ordinal != 0 || scanline[3].original_ordinal != 1)
    {
        return 11;
    }

    const std::vector<runtime_contract::ReplayCandidate> routed_candidates{
        {0, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Fill,
         0, 0.10, 0.10, true, 100.0, 10.0, -5.0, 0},
        {1, runtime_contract::ReplayRegion::Side, runtime_contract::ReplayRegionMode::Paint,
         1, 0.20, 0.20, true, 90.0, 9.0, 0.0, 1},
        {2, runtime_contract::ReplayRegion::Front, runtime_contract::ReplayRegionMode::Skip,
         2, 0.30, 0.30, true, 80.0, 8.0, 5.0, 2},
    };
    const auto routed_plan = runtime_contract::build_single_brush_replay_plan(
        routed_candidates, 1024, 5.0, 80.0);
    if (routed_plan.entries.size() != 4 ||
        routed_plan.fill_end != 3 ||
        routed_plan.fill_count != 3 || routed_plan.paint_count != 1 ||
        routed_plan.fill_candidates != 3 || routed_plan.fill_deduplicated != 0 ||
        routed_plan.entries[0].pass != runtime_contract::ReplayPass::Fill ||
        routed_plan.entries[0].sample_index != 0 ||
        routed_plan.entries[1].pass != runtime_contract::ReplayPass::Fill ||
        routed_plan.entries[1].sample_index != 1 ||
        routed_plan.entries[2].pass != runtime_contract::ReplayPass::Fill ||
        routed_plan.entries[2].sample_index != 2 ||
        routed_plan.entries[3].pass != runtime_contract::ReplayPass::Paint ||
        routed_plan.entries[3].sample_index != 1)
    {
        return 12;
    }

    const std::vector<runtime_contract::ReplayCandidate> dedupe_candidates{
        {0, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Fill,
         0, 0.100, 0.100, true, 100.0, 10.0, -5.0, 0},
        {1, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Fill,
         0, 0.105, 0.105, true, 99.0, 9.0, -4.0, 1},
        {2, runtime_contract::ReplayRegion::Side, runtime_contract::ReplayRegionMode::Paint,
         1, 0.200, 0.200, true, 90.0, 8.0, -3.0, 2},
        {3, runtime_contract::ReplayRegion::Side, runtime_contract::ReplayRegionMode::Paint,
         1, 0.205, 0.205, true, 89.0, 7.0, -2.0, 3},
        {4, runtime_contract::ReplayRegion::Side, runtime_contract::ReplayRegionMode::Paint,
         1, 0.250, 0.250, true, 80.0, 6.0, -1.0, 4},
    };
    const auto dedupe_plan = runtime_contract::build_single_brush_replay_plan(
        dedupe_candidates, 1024, 5.0, 80.0);
    if (dedupe_plan.entries.size() != 6 ||
        dedupe_plan.fill_end != 3 ||
        dedupe_plan.fill_count != 3 || dedupe_plan.paint_count != 3 ||
        dedupe_plan.fill_candidates != 5 || dedupe_plan.fill_deduplicated != 2 ||
        dedupe_plan.paint_candidates != 3 || dedupe_plan.paint_deduplicated != 0 ||
        dedupe_plan.entries[0].sample_index != 0 ||
        dedupe_plan.entries[1].sample_index != 2 ||
        dedupe_plan.entries[2].sample_index != 4)
    {
        return 13;
    }

    const std::vector<runtime_contract::ReplayCandidate> current_view_order_candidates{
        {0, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Paint,
         0, 0.10, 0.10, true, 90.0, 1000.0, 10.0, 0},
        {1, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Paint,
         0, 0.20, 0.20, true, 100.0, 0.0, 10.0, 1},
        {2, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Paint,
         0, 0.30, 0.30, true, 100.0, 0.0, -10.0, 2},
        {3, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Paint,
         0, 0.40, 0.40, false, 999.0, 80.0, 0.0, 3},
    };
    const auto current_view_order_plan = runtime_contract::build_single_brush_replay_plan(
        current_view_order_candidates, 1024, 5.0, 80.0);
    const std::array<std::size_t, 4> expected_current_view_order{{2, 1, 0, 3}};
    for (std::size_t index = 0; index < expected_current_view_order.size(); ++index)
    {
        if (current_view_order_plan.entries[index].sample_index != expected_current_view_order[index])
        {
            return 14;
        }
    }
    if (!current_view_order_plan.current_view_projection_fallback_used ||
        current_view_order_plan.current_view_projection_fallback_candidates != 1)
    {
        return 14;
    }

    const std::vector<runtime_contract::ReplayCandidate> cross_region_view_order_candidates{
        {0, runtime_contract::ReplayRegion::Front, runtime_contract::ReplayRegionMode::Paint,
         0, 0.10, 0.10, true, 300.0, 0.0, 0.0, 0},
        {1, runtime_contract::ReplayRegion::Side, runtime_contract::ReplayRegionMode::Paint,
         0, 0.10, 0.10, true, 200.0, 0.0, 0.0, 1},
        {2, runtime_contract::ReplayRegion::Back, runtime_contract::ReplayRegionMode::Paint,
         0, 0.10, 0.10, true, 100.0, 0.0, 0.0, 2},
    };
    const auto cross_region_view_order_plan = runtime_contract::build_single_brush_replay_plan(
        cross_region_view_order_candidates, 1024, 5.0, 80.0);
    const std::array<std::size_t, 3> expected_cross_region_view_order{{0, 1, 2}};
    for (std::size_t index = 0; index < expected_cross_region_view_order.size(); ++index)
    {
        if (cross_region_view_order_plan.entries[index].sample_index != expected_cross_region_view_order[index])
        {
            return 15;
        }
    }

    const auto fill_window = runtime_contract::replay_pass_window(0, 100, 20);
    const auto paint_window = runtime_contract::replay_pass_window(20, 100, 20);
    const auto complete_window = runtime_contract::replay_pass_window(100, 100, 20);
    const auto clamped_window = runtime_contract::replay_pass_window(999, 10, 50);
    if (fill_window.pass != runtime_contract::ReplayPass::Fill ||
        fill_window.begin != 0 || fill_window.end != 20 ||
        paint_window.pass != runtime_contract::ReplayPass::Paint ||
        paint_window.begin != 20 || paint_window.end != 100 ||
        complete_window.pass != runtime_contract::ReplayPass::Complete ||
        complete_window.begin != 100 || complete_window.end != 100 ||
        clamped_window.pass != runtime_contract::ReplayPass::Complete ||
        clamped_window.begin != 10 || clamped_window.end != 10)
    {
        return 22;
    }

    const std::vector<runtime_contract::AdaptivePaintSample> adaptive_samples{
        {0.10, 0.10, runtime_contract::ReplayRegion::Front, 0, 0.50, 0.50, 0.50, true, true, 1},
        {0.102, 0.10, runtime_contract::ReplayRegion::Front, 0, 0.50, 0.50, 0.50, true, true, 1},
        {0.30, 0.10, runtime_contract::ReplayRegion::Front, 0, 0.20, 0.20, 0.20, true, true, 1},
        {0.60, 0.10, runtime_contract::ReplayRegion::Front, 1, 0.50, 0.50, 0.50, true, true, 1},
    };
    const std::vector<runtime_contract::ReplayEntry> adaptive_entries{
        {0, runtime_contract::ReplayPass::Paint, runtime_contract::ReplayRegion::Front, {0, 0.0, 0}},
        {1, runtime_contract::ReplayPass::Paint, runtime_contract::ReplayRegion::Front, {0, 1.0, 1}},
        {2, runtime_contract::ReplayPass::Paint, runtime_contract::ReplayRegion::Front, {0, 2.0, 2}},
        {3, runtime_contract::ReplayPass::Paint, runtime_contract::ReplayRegion::Front, {0, 3.0, 3}},
    };
    const auto no_compression = runtime_contract::build_adaptive_paint_plan(
        adaptive_entries, adaptive_samples, 0.01, 0.0);
    if (no_compression.entries.size() != adaptive_entries.size() ||
        no_compression.compressed_paint_entries != 0 ||
        no_compression.entries[0].radius_multiplier != 1.0 ||
        no_compression.entries[1].replay.sample_index != 1)
    {
        return 24;
    }
    const auto compressed = runtime_contract::build_adaptive_paint_plan(
        adaptive_entries, adaptive_samples, 0.01, 1.0);
    if (compressed.entries.size() != 3 ||
        compressed.compressed_paint_entries != 1 ||
        compressed.entries[0].replay.sample_index != 0 ||
        compressed.entries[0].radius_multiplier != 8.0 ||
        compressed.entries[1].replay.sample_index != 2 ||
        compressed.entries[2].replay.sample_index != 3)
    {
        return 25;
    }

    if (runtime_contract::production_material_stroke_count(3) != 3 ||
        runtime_contract::production_material_sample_index(0) != 0 ||
        runtime_contract::production_material_sample_index(1) != 1 ||
        runtime_contract::production_material_sample_index(2) != 2)
    {
        return 23;
    }
    if (runtime_contract::ProductionMaterialPaintChannels !=
        std::array<std::uint8_t, 1>{
            static_cast<std::uint8_t>(sdk::EPaintChannel::AlbedoMetallicRoughnessEmissive)})
    {
        return 26;
    }
    return 0;
}
