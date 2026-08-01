#include <meccha/core/config.hpp>
#include <meccha/core/esp.hpp>
#include <meccha/core/image_mapping.hpp>
#include <meccha/core/image_project.hpp>
#include <meccha/core/mesh_profile.hpp>
#include <meccha/core/paint.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
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

    const auto round_profile = expected_mesh_profile(
        BodyProfile::Round,
        MeshProfileRole::Raw);
    passed &= expect(
        round_profile.vertex_count == 1668U &&
            round_profile.index_count == 8352U &&
            round_profile.profile_hash ==
                "cd469e35ad0cbd1e483bd82b2406849429d24037807bd7a294534fb79633f55b",
        "Round profile identity drifted from the game 3.3.0 cache");

    const PaintSettings defaults{};
    passed &= expect(
        validate(defaults).empty() &&
            defaults.brush_size_texels == 5.0 &&
            defaults.front_mode == RegionMode::Skip &&
            defaults.side_mode == RegionMode::Paint &&
            defaults.back_mode == RegionMode::Paint &&
            defaults.paint_material == Material{0.0, 1.0, 0.0} &&
            defaults.fill_color == Rgb8{255U, 255U, 255U} &&
            defaults.fill_material == Material{1.0, 0.0, 0.0},
        "Paint defaults drifted from the v1.7.2 contract");

    auto invalid = defaults;
    invalid.brush_size_texels =
        std::numeric_limits<double>::quiet_NaN();
    invalid.paint_material.emissive = 1.01;
    invalid.color_compression_tolerance_percent = -0.01;
    invalid.front_mode = static_cast<RegionMode>(255U);
    invalid.side_mode = static_cast<RegionMode>(254U);
    invalid.back_mode = static_cast<RegionMode>(253U);
    const auto invalid_fields = validate(invalid);
    passed &= expect(
        invalid_fields ==
            std::vector{
                PaintSettingField::BrushSize,
                PaintSettingField::FrontMode,
                PaintSettingField::SideMode,
                PaintSettingField::BackMode,
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
    const auto replay_result =
        build_replay_plan(replay_candidates, 1024, 5.0, 4.0);
    passed &= expect(
        replay_result.has_value(),
        "valid replay candidates did not produce a plan");
    if (!replay_result)
    {
        return 1;
    }
    const auto& replay = *replay_result;
    passed &= expect(
        replay.entries.size() == 4U && replay.fill_end == 3U &&
            replay.fill_count == 3U && replay.paint_count == 1U &&
            replay.entries[0].sample_index == 2U &&
            replay.entries[0].spatial_key.row == 256 &&
            replay.entries[1].sample_index == 1U &&
            replay.entries[1].spatial_key.row == 128 &&
            replay.entries[2].sample_index == 0U &&
            replay.entries[2].spatial_key.row == 0 &&
            replay.entries[3].sample_index == 1U &&
            replay.entries[3].pass == ReplayPass::Paint &&
            replay.entries[3].spatial_key.row == 102 &&
            replay.current_view_projection_fallback_used &&
            replay.current_view_projection_fallback_candidates == 1U,
        "Fill-first replay routing drifted from the golden fixture");

    const std::vector region_order_candidates{
        ReplayCandidate{
            0U,
            Region::Front,
            RegionMode::Paint,
            0,
            0.1,
            0.1,
            true,
            2.0,
            2.0,
            0.0,
            0U,
        },
        ReplayCandidate{
            1U,
            Region::Side,
            RegionMode::Paint,
            0,
            0.2,
            0.2,
            true,
            1.0,
            1.0,
            0.0,
            1U,
        },
        ReplayCandidate{
            2U,
            Region::Back,
            RegionMode::Paint,
            0,
            0.3,
            0.3,
            true,
            0.0,
            0.0,
            0.0,
            2U,
        },
    };
    const auto region_order_replay = build_replay_plan(
        region_order_candidates,
        1024,
        5.0,
        4.0);
    passed &= expect(
        region_order_replay &&
            region_order_replay->entries.size() == 3U &&
            region_order_replay->entries[0].region == Region::Back &&
            region_order_replay->entries[1].region == Region::Side &&
            region_order_replay->entries[2].region == Region::Front,
        "Paint replay did not preserve Back-Side-Front region order");

    passed &= expect(
        replay_pass_window(3U, 4U, 3U) ==
            ReplayPassWindow{ReplayPass::Paint, 3U, 4U} &&
            replay_pass_window(9U, 4U, 7U) ==
                ReplayPassWindow{ReplayPass::Complete, 4U, 4U},
        "replay pass boundary clamping is incorrect");

    const std::vector adaptive_samples{
        AdaptivePaintSample{
            0.5,
            0.5,
            Region::Front,
            1,
            0.4,
            0.5,
            0.6,
            true,
            true,
            42U,
        },
        AdaptivePaintSample{
            0.502,
            0.5,
            Region::Front,
            1,
            0.4,
            0.5,
            0.6,
            true,
            true,
            42U,
        },
    };
    const std::vector adaptive_replay{
        ReplayEntry{0U, ReplayPass::Paint, Region::Front, {}},
        ReplayEntry{1U, ReplayPass::Paint, Region::Front, {}},
    };
    const auto compression_disabled = build_adaptive_paint_plan(
        adaptive_replay,
        adaptive_samples,
        0.01,
        0.0);
    passed &= expect(
        compression_disabled &&
            compression_disabled->entries.size() == 2U &&
            compression_disabled->compressed_paint_entries == 0U &&
            compression_disabled->entries[0].radius_multiplier == 1.0 &&
            compression_disabled->entries[1].radius_multiplier == 1.0,
        "disabled adaptive compression changed replay entries");

    const auto compressed = build_adaptive_paint_plan(
        adaptive_replay,
        adaptive_samples,
        0.01,
        5.0);
    passed &= expect(
        compressed && compressed->entries.size() == 1U &&
            compressed->compressed_paint_entries == 1U &&
            compressed->expanded_paint_entries == 0U &&
            compressed->entries[0].radius_multiplier == 1.0 &&
            compressed->entries[0].has_color_override &&
            compressed->representative_paint_entries == 1U,
        "brush-aligned Paint samples did not coalesce deterministically");

    auto different_material = adaptive_samples;
    different_material[1].material_key = 43U;
    const auto separated = build_adaptive_paint_plan(
        adaptive_replay,
        different_material,
        0.01,
        5.0);
    passed &= expect(
        separated && separated->entries.size() == 2U &&
            separated->compressed_paint_entries == 0U,
        "adaptive compression crossed a material boundary");

    const std::vector region_compression_samples{
        AdaptivePaintSample{
            0.10, 0.10, Region::Back, 0,
            0.4, 0.5, 0.6, true, true, 10U},
        AdaptivePaintSample{
            0.115, 0.10, Region::Back, 0,
            0.4, 0.5, 0.6, true, true, 11U},
        AdaptivePaintSample{
            0.40, 0.40, Region::Side, 0,
            0.4, 0.5, 0.6, true, true, 20U},
        AdaptivePaintSample{
            0.435, 0.40, Region::Side, 0,
            0.4, 0.5, 0.6, true, true, 21U},
        AdaptivePaintSample{
            0.80, 0.80, Region::Front, 0,
            0.4, 0.5, 0.6, true, true, 30U},
    };
    const std::vector region_compression_replay{
        ReplayEntry{0U, ReplayPass::Paint, Region::Back, {}},
        ReplayEntry{2U, ReplayPass::Paint, Region::Side, {}},
        ReplayEntry{4U, ReplayPass::Paint, Region::Front, {}},
    };
    const auto region_compression = build_adaptive_paint_plan(
        region_compression_replay,
        region_compression_samples,
        0.01,
        5.0);
    passed &= expect(
        region_compression &&
            region_compression->entries.size() == 3U &&
            region_compression->entries[0].replay.region ==
                Region::Back &&
            region_compression->entries[1].replay.region ==
                Region::Side &&
            region_compression->entries[2].replay.region ==
                Region::Front,
        "adaptive compression reordered Back-Side-Front regions");

    auto compression_hole_samples =
        std::vector<AdaptivePaintSample>{};
    for (auto y = -8; y <= 8; ++y)
    {
        for (auto x = -8; x <= 8; ++x)
        {
            if (x == 1 && y == 0)
            {
                continue;
            }
            compression_hole_samples.push_back(
                {0.505 + static_cast<double>(x) * 0.01,
                 0.505 + static_cast<double>(y) * 0.01,
                 Region::Front,
                 0,
                 0.50,
                 0.50,
                 0.50,
                 true,
                 true,
                 1U});
        }
    }
    const auto compression_hole_center =
        static_cast<std::size_t>(8 * 17 + 8);
    const auto compression_hole_entry = ReplayEntry{
        compression_hole_center,
        ReplayPass::Paint,
        Region::Front,
        {0, 0.0, compression_hole_center}};
    const auto compression_hole = build_adaptive_paint_plan(
        {&compression_hole_entry, 1U},
        compression_hole_samples,
        0.01,
        5.0);
    passed &= expect(
        compression_hole && compression_hole->entries.size() == 1U &&
            compression_hole->entries[0].radius_multiplier == 1.0 &&
            compression_hole->expanded_paint_entries == 0U &&
            compression_hole->coverage_grid_size == 100,
        "a coverage hole did not block widened replay");

    auto representative_color_samples =
        std::vector<AdaptivePaintSample>{};
    for (auto y = 0; y < 17; ++y)
    {
        for (auto x = 0; x < 17; ++x)
        {
            const auto color = x <= 8 ? 0.451 : 0.549;
            representative_color_samples.push_back(
                {(40.5 + static_cast<double>(x)) * 0.01,
                 (40.5 + static_cast<double>(y)) * 0.01,
                 Region::Front,
                 0,
                 color,
                 color,
                 color,
                 true,
                 true,
                 1U});
        }
    }
    const auto representative_color_center =
        static_cast<std::size_t>(8 * 17 + 8);
    const auto representative_entry = ReplayEntry{
        representative_color_center,
        ReplayPass::Paint,
        Region::Front,
        {0, 0.0, representative_color_center}};
    const auto representative_color = build_adaptive_paint_plan(
        {&representative_entry, 1U},
        representative_color_samples,
        0.01,
        10.0);
    passed &= expect(
        representative_color &&
            representative_color->entries.size() == 1U &&
            representative_color->entries[0].radius_multiplier == 8.0 &&
            representative_color->entries[0].has_color_override &&
            std::abs(
                representative_color->entries[0].red - 0.5) <
                0.000001 &&
            std::abs(
                representative_color->entries[0].green - 0.5) <
                0.000001 &&
            std::abs(
                representative_color->entries[0].blue - 0.5) <
                0.000001 &&
            representative_color->representative_paint_entries == 1U &&
            std::abs(
                representative_color->representative_error_max -
                0.049) < 0.000001,
        "representative color did not minimize per-channel maximum error");

    auto invalid_adaptive = adaptive_samples;
    invalid_adaptive[0].u =
        std::numeric_limits<double>::quiet_NaN();
    const auto invalid_adaptive_result = build_adaptive_paint_plan(
        adaptive_replay,
        invalid_adaptive,
        0.01,
        5.0);
    passed &= expect(
        !invalid_adaptive_result &&
            invalid_adaptive_result.error() ==
                AdaptivePaintPlanError::InvalidSample,
        "non-finite adaptive input was accepted");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        build_replay_plan(
            replay_candidates,
            1024,
            5.0,
            4.0,
            cancelled.get_token()) ==
            std::unexpected(ReplayPlanError::Cancelled),
        "cancelled replay planning still published a plan");
    passed &= expect(
        build_adaptive_paint_plan(
            replay.entries,
            adaptive_samples,
            0.01,
            5.0,
            0.0,
            cancelled.get_token()) ==
            std::unexpected(AdaptivePaintPlanError::Cancelled),
        "cancelled adaptive planning still published a plan");

    auto excessive_candidates =
        std::vector<ReplayCandidate>(
            MaximumAdaptivePaintSamples + 1U);
    passed &= expect(
        build_replay_plan(
            excessive_candidates,
            1024,
            5.0,
            4.0) ==
            std::unexpected(ReplayPlanError::ResourceLimit),
        "an unbounded replay candidate set was accepted");

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
        local_dispatch_adaptive_delay_ms(21, 0U) == 21 &&
            local_dispatch_adaptive_delay_ms(21, 4'000U) == 21 &&
            local_dispatch_adaptive_delay_ms(21, 16'423U) == 53 &&
            local_dispatch_adaptive_delay_ms(1, 100'000U) == 250 &&
            local_dispatch_adaptive_delay_ms(-1, 0U) == 1 &&
            local_dispatch_adaptive_delay_ms(300, 100'000U) == 300,
        "measured local Paint slice did not produce the bounded adaptive delay");
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
            image_defaults.alpha == AlphaMode::Skip &&
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

    auto invalid_layer_text = layer;
    invalid_layer_text.file_name =
        std::string{"bad"} + static_cast<char>(0xC0);
    passed &= expect(
        validate(invalid_layer_text) ==
            std::vector{ImageLayerField::FileName},
        "an invalid UTF-8 Image Paint file name was accepted");

    auto invalid_layer_mime = layer;
    invalid_layer_mime.mime = static_cast<ImageMime>(0xFFU);
    passed &= expect(
        validate(invalid_layer_mime) ==
            std::vector{ImageLayerField::Mime},
        "an invalid Image Paint MIME enum was accepted");

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

    const auto source_bytes =
        std::make_shared<const std::vector<std::byte>>(
            3U,
            std::byte{0x2A});
    const auto atlas =
        std::make_shared<const std::vector<std::byte>>(
            CanonicalAtlasByteLength,
            std::byte{0x7F});
    const auto asset_id = std::string(64U, 'a');
    auto project_layer = layer;
    project_layer.asset_id = asset_id;
    project_layer.source_bytes = source_bytes->size();
    const auto project = ImageProject{
        ImageProjectSchemaVersion,
        "0123456789abcdef0123456789abcdef",
        "Project 日本語",
        1U,
        image_defaults,
        {project_layer},
        {ImageSourceAsset{
            asset_id,
            ImageMime::Png,
            source_bytes,
        }},
        atlas,
    };
    passed &= expect(
        validate(project).empty(),
        "a valid immutable Image Paint project was rejected");

    auto shared_source_project = project;
    shared_source_project.sources.front().bytes =
        std::make_shared<const std::vector<std::byte>>(
            MaximumImageSourceBytes,
            std::byte{0x2A});
    shared_source_project.layers.assign(6U, project_layer);
    for (auto& shared_layer : shared_source_project.layers)
    {
        shared_layer.source_bytes = MaximumImageSourceBytes;
    }
    passed &= expect(
        validate(shared_source_project).empty(),
        "reused content-addressed source bytes were counted per layer");

    auto invalid_project_settings = project;
    invalid_project_settings.settings.alpha =
        static_cast<AlphaMode>(0xFFU);
    passed &= expect(
        validate(invalid_project_settings) ==
            std::vector{ImageProjectField::Settings},
        "invalid project settings were not isolated from layer errors");

    auto invalid_project_text = project;
    invalid_project_text.display_name =
        std::string{"bad"} + static_cast<char>(0xC0);
    passed &= expect(
        validate(invalid_project_text) ==
            std::vector{ImageProjectField::DisplayName},
        "an invalid UTF-8 project display name was accepted");

    auto invalid_source_codec = project;
    invalid_source_codec.sources.front().mime =
        static_cast<ImageMime>(0xFFU);
    passed &= expect(
        validate(invalid_source_codec) ==
            std::vector{
                ImageProjectField::SourceCodec,
                ImageProjectField::SourceReference,
            },
        "an invalid source codec enum was accepted");

    auto invalid_project = project;
    invalid_project.project_id = "../escape";
    invalid_project.sources.front().asset_id.assign(64U, 'A');
    invalid_project.canonical_atlas =
        std::make_shared<const std::vector<std::byte>>(1U);
    passed &= expect(
        validate(invalid_project) ==
            std::vector{
                ImageProjectField::ProjectId,
                ImageProjectField::SourceIdentity,
                ImageProjectField::SourceReference,
                ImageProjectField::CanonicalAtlas,
            },
        "unsafe project identity, source, or atlas was accepted");

    for (const auto body : {
             BodyProfile::Round,
             BodyProfile::Cube,
             BodyProfile::Fukuyoka,
         })
    {
        const auto raw =
            expected_mesh_profile(body, MeshProfileRole::Raw);
        const auto image = expected_mesh_profile(
            body,
            MeshProfileRole::ImageReference);
        passed &= expect(
            validate(raw).empty() && validate(image).empty() &&
                image.base_profile_id == raw.profile_id &&
                image.base_profile_hash == raw.profile_hash &&
                image.reference_pose_bone_count == raw.bone_count,
            "a frozen body profile contract is internally inconsistent");
    }

    auto misspelled_alias = expected_mesh_profile(
        BodyProfile::Fukuyoka,
        MeshProfileRole::Raw);
    misspelled_alias.source_path.replace(
        misspelled_alias.source_path.find("hukuyoka"),
        std::string_view{"hukuyoka"}.size(),
        "fukuyoka");
    passed &= expect(
        validate(misspelled_alias) ==
            std::vector{MeshProfileField::SourceIdentity},
        "the fukuyoka UI name leaked into the game asset alias");

    auto invalid_topology = expected_mesh_profile(
        BodyProfile::Cube,
        MeshProfileRole::Raw);
    invalid_topology.maximum_vertex_index =
        invalid_topology.vertex_count;
    invalid_topology.serialized_index_count -= 1U;
    passed &= expect(
        validate(invalid_topology) ==
            std::vector{
                MeshProfileField::SerializedCounts,
                MeshProfileField::IndexBounds,
            },
        "invalid profile topology or index bounds were accepted");

    auto wrong_base = expected_mesh_profile(
        BodyProfile::Round,
        MeshProfileRole::ImageReference);
    wrong_base.base_profile_hash.assign(64U, '0');
    passed &= expect(
        validate(wrong_base) ==
            std::vector{MeshProfileField::BaseProfile},
        "a derived image profile accepted the wrong raw profile hash");

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

    auto invalid_esp = EspSettings{};
    invalid_esp.scope = static_cast<EspScope>(255U);
    passed &= expect(
        validate(invalid_esp) ==
            std::vector{EspSettingField::Scope},
        "an invalid ESP scope enum was accepted");

    const ApplicationConfig config_defaults{};
    passed &= expect(
        validate(config_defaults).empty() &&
            SupportedLocales.size() == 16U &&
            config_defaults.ui.language == "en" &&
            config_defaults.ui.scale == 1.0 &&
            config_defaults.ui.hotkeys.toggle_ui ==
                FunctionKey::F9 &&
            config_defaults.esp == EspSettings{},
        "configuration defaults or locale inventory drifted");

    auto duplicate_hotkey = config_defaults;
    duplicate_hotkey.ui.hotkeys.image_cancel =
        FunctionKey::F1;
    passed &= expect(
        validate(duplicate_hotkey) ==
            std::vector{ConfigurationField::DuplicateHotkey},
        "duplicate action hotkeys were accepted");

    auto invalid_config = config_defaults;
    invalid_config.schema_version = 2U;
    invalid_config.ui.language = "en-US";
    invalid_config.ui.scale = 2.01;
    invalid_config.ui.hotkeys.toggle_ui =
        static_cast<FunctionKey>(0U);
    invalid_config.esp.scope = static_cast<EspScope>(255U);
    passed &= expect(
        validate(invalid_config) ==
            std::vector{
                ConfigurationField::SchemaVersion,
                ConfigurationField::Language,
                ConfigurationField::UiScale,
                ConfigurationField::HotkeyRange,
                ConfigurationField::Esp,
            },
        "configuration validation errors are incomplete or unstable");

    if (passed)
    {
        std::cout << "PASS core_contract\n";
        return 0;
    }
    return 1;
}
