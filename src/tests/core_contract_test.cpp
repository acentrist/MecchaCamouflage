#include <meccha/core/esp.hpp>
#include <meccha/core/image_mapping.hpp>
#include <meccha/core/image_project.hpp>
#include <meccha/core/paint.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL core_contract: " << message << '\n';
    }
    return condition;
}

auto near(double left, double right) -> bool
{
    return std::abs(left - right) <= 0.0000001;
}
} // namespace

auto main() -> int
{
    bool passed = true;

    const PaintSettings defaults{};
    passed &= expect(
        validate(defaults).empty() &&
            defaults.brush_size_texels == 5.0 &&
            defaults.side_source_max_uv == 0.08 &&
            defaults.front_back_source_max_uv == 0.45 &&
            defaults.front_mode == RegionMode::Skip &&
            defaults.side_mode == RegionMode::Paint &&
            defaults.back_mode == RegionMode::Paint &&
            defaults.paint_material == Material{0.0, 1.0, 0.0} &&
            defaults.fill_color == Rgb8{255U, 255U, 255U} &&
            defaults.fill_material == Material{1.0, 0.0, 0.0},
        "Paint defaults drifted from the frozen v1 contract");

    auto invalid = defaults;
    invalid.brush_size_texels =
        std::numeric_limits<double>::quiet_NaN();
    invalid.paint_material.emissive = 1.01;
    invalid.color_compression_tolerance_percent = -0.01;
    const auto invalid_fields = validate(invalid);
    passed &= expect(
        invalid_fields ==
            std::vector{
                PaintSettingField::BrushSize,
                PaintSettingField::PaintEmissive,
                PaintSettingField::CompressionTolerance,
            },
        "Paint validation did not report stable field errors");

    const std::vector replay_candidates{
        ReplayCandidate{
            0U,
            Region::Front,
            RegionMode::Fill,
            0,
            0.1,
            0.1,
            true,
            1.0,
            1.0,
            0.0,
            0U,
        },
        ReplayCandidate{
            1U,
            Region::Side,
            RegionMode::Paint,
            1,
            0.2,
            0.2,
            true,
            0.0,
            0.0,
            -1.0,
            1U,
        },
        ReplayCandidate{
            2U,
            Region::Back,
            RegionMode::Skip,
            2,
            0.3,
            0.3,
            false,
            0.0,
            -1.0,
            1.0,
            2U,
        },
    };
    const auto replay =
        build_replay_plan(replay_candidates, 1024, 5.0, 4.0);
    passed &= expect(
        replay.entries.size() == 4U && replay.fill_end == 3U &&
            replay.fill_count == 3U && replay.paint_count == 1U &&
            replay.entries[0].sample_index == 0U &&
            replay.entries[0].spatial_key.row == 0 &&
            replay.entries[1].sample_index == 1U &&
            replay.entries[1].spatial_key.row == 128 &&
            replay.entries[2].sample_index == 2U &&
            replay.entries[2].spatial_key.row == 256 &&
            replay.entries[3].sample_index == 1U &&
            replay.entries[3].pass == ReplayPass::Paint &&
            replay.entries[3].spatial_key.row == 102 &&
            replay.current_view_projection_fallback_used &&
            replay.current_view_projection_fallback_candidates == 1U,
        "Fill-first replay routing drifted from the golden fixture");

    passed &= expect(
        replay_pass_window(3U, 4U, 3U) ==
            ReplayPassWindow{ReplayPass::Paint, 3U, 4U} &&
            replay_pass_window(9U, 4U, 7U) ==
                ReplayPassWindow{ReplayPass::Complete, 4U, 4U},
        "replay pass boundary clamping is incorrect");

    const auto fallback_pacing = replication_pacing_plan({});
    passed &= expect(
        fallback_pacing == ReplicationPacingPlan{
            20,
            20,
            320,
            320,
            320,
            16,
            1,
            50,
            4,
            434,
        },
        "fallback replication pacing drifted");
    const auto receiver_pacing = replication_pacing_plan(
        ReplicationPacingInput{60, 100, 2, 2, 2, true, 4});
    passed &= expect(
        receiver_pacing == ReplicationPacingPlan{
            60,
            100,
            4800,
            24,
            24,
            2,
            2,
            17,
            84,
            270,
        },
        "receiver-bounded replication pacing drifted");
    passed &= expect(
        !visual_drain_complete(true, true, 0, true, 1, 1000, 434) &&
            !visual_drain_complete(true, true, 1, true, 0, 1000, 434) &&
            !visual_drain_complete(true, true, 0, true, 0, 433, 434) &&
            visual_drain_complete(true, true, 0, true, 0, 434, 434) &&
            visual_drain_complete(
                false,
                false,
                999,
                false,
                999,
                434,
                434),
        "terminal queue-drain rules drifted");

    const auto front = map_atlas_coordinate(AtlasMappingInput{
        false,
        Region::Front,
        true,
        0.5,
        0.5,
        0.5,
        0.0,
        -1.0,
        0.0,
        0.0,
        1.0,
        0.0,
        1.0,
        0.0,
        1.0,
    });
    const auto right = map_atlas_coordinate(AtlasMappingInput{
        true,
        Region::Side,
        true,
        0.75,
        0.25,
        0.5,
        1.0,
        0.0,
        0.0,
        0.0,
        1.0,
        0.0,
        1.0,
        0.0,
        1.0,
    });
    passed &= expect(
        near(front.u, 0.125) && near(front.v, 0.5) &&
            !front.cube_side && !front.cube_edge &&
            near(right.u, 0.3125) && right.cube_side &&
            !right.cube_edge,
        "normalized atlas mapping drifted");

    const auto cube = map_cube_coordinate(CubeProjectionInput{
        10.0,
        0.0,
        8.0,
        0.0,
        -1.0,
        0.0,
        0.0,
        0.0,
        2.0,
    });
    const auto round = map_round_coordinate(RoundProjectionInput{
        Region::Side,
        true,
        -10.0,
        -4.0,
        0.0,
        0.0,
        0.0,
        0.0,
        2.0,
    });
    passed &= expect(
        cube.face == CubeFace::Front &&
            near(cube.u, 0.14453125) &&
            near(cube.v, 0.53125) && round.tile == 3 &&
            near(round.u, 0.8828125) && near(round.v, 0.5),
        "canonical body projection drifted");

    const ImageProjectSettings image_defaults{};
    const ImageLayer layer{
        "sha256:asset",
        "source.png",
        ImageMime::Png,
        1024U,
    };
    passed &= expect(
        validate(layer).empty() &&
            validate(image_defaults, std::vector{layer}).empty() &&
            image_defaults.body == BodyProfile::Round &&
            image_defaults.placement == PlacementMode::Fit &&
            image_defaults.front == FaceBaseMode::Skip &&
            image_defaults.right == FaceBaseMode::Skip &&
            image_defaults.back == FaceBaseMode::Skip &&
            image_defaults.left == FaceBaseMode::Skip &&
            image_defaults.brush_size_texels == 5.0 &&
            image_defaults.color_compression_tolerance_percent == 0.0,
        "Image Paint defaults or valid-layer contract drifted");

    auto invalid_crop = layer;
    invalid_crop.crop =
        NormalizedCrop{0.8, 0.0, 0.3, 1.0};
    passed &= expect(
        validate(invalid_crop) ==
            std::vector{ImageLayerField::Crop},
        "an out-of-source normalized crop was accepted");

    auto oversized_layers = std::vector<ImageLayer>{};
    for (auto index = 0; index < 6; ++index)
    {
        auto item = layer;
        item.asset_id += std::to_string(index);
        item.source_bytes = MaximumImageSourceBytes;
        oversized_layers.push_back(std::move(item));
    }
    passed &= expect(
        validate(image_defaults, oversized_layers) ==
            std::vector{ImageProjectError::SourceSizeLimit},
        "the total Image Paint source-byte limit was not enforced");

    passed &= expect(
        esp_scope_matches(EspScope::All, EspRole::Hider) &&
            !esp_scope_matches(EspScope::All, EspRole::Spectator) &&
            esp_scope_matches(EspScope::Hunter, EspRole::Hunter) &&
            !esp_scope_matches(EspScope::Hunter, EspRole::Hider),
        "ESP role filtering drifted");
    passed &= expect(
        select_esp_pawn_source(
            EspRole::Hider,
            EspRole::Spectator,
            EspRole::Hider,
            true) == EspPawnSource::RoleRoster &&
            select_esp_pawn_source(
                EspRole::Hunter,
                EspRole::Hunter,
                EspRole::Hunter,
                true) == EspPawnSource::PlayerArray,
        "ESP avatar replacement selection drifted");
    passed &= expect(
        esp_geometry_capabilities(true, true, true) ==
                EspGeometryCapabilities{true, false, false} &&
            expand_screen_bounds(
                ScreenBounds{100.0, 50.0, 300.0, 250.0},
                0.1,
                0.2) ==
                ScreenBounds{80.0, 10.0, 320.0, 290.0} &&
            projection_scale_from_sample(960.0, 1440.0, 1680.0) ==
                1.5 &&
            projection_scale_from_sample(960.0, 960.5, 961.0) ==
                -1.0,
        "ESP geometry/projection contracts drifted");

    if (passed)
    {
        std::cout << "PASS core_contract\n";
        return 0;
    }
    return 1;
}
