#include "../include/sdk.hpp"
#include "../include/runtime_contract.hpp"
#include "../include/direct_bridge_abi.hpp"

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
    BridgeStartBlockV2 bootstrap{};
    bootstrap.magic = BridgeStartMagicV2;
    bootstrap.size = sizeof(bootstrap);
    bootstrap.abi = BridgeStartAbiV2;
    bootstrap.protocol = BridgeBootstrapProtocolV2;
    bootstrap.runtime_bundle_sha256[0] = 1;
    if (sizeof(bootstrap) != 160 ||
        offsetof(BridgeStartBlockV2, runtime_bundle_sha256) != 96 ||
        bootstrap.magic != 0x3253434D ||
        bootstrap.abi != 2 ||
        bootstrap.protocol != 2)
    {
        return 118;
    }
    if (std::string(
            runtime_contract::
                paint_dispatch_exception_kind(
                    0xC0000005U)) !=
            "access_violation" ||
        std::string(
            runtime_contract::
                paint_dispatch_exception_kind(
                    0xC00000FDU)) !=
            "stack_overflow" ||
        std::string(
            runtime_contract::
                paint_dispatch_exception_kind(
                    0xDEADBEEFU)) !=
            "unknown" ||
        std::string(
            runtime_contract::
                paint_dispatch_access_operation(
                    true, 0U)) !=
            "read" ||
        std::string(
            runtime_contract::
                paint_dispatch_access_operation(
                    true, 1U)) !=
            "write" ||
        std::string(
            runtime_contract::
                paint_dispatch_access_operation(
                    true, 8U)) !=
            "execute" ||
        std::string(
            runtime_contract::
                paint_dispatch_access_operation(
                    false, 0U)) !=
            "not_applicable")
    {
        return 123;
    }
    const auto compact_exception_metadata =
        compact_mesh_response_metadata(
            "\"paint_dispatch_exception_stage\":\"resolve_runtime_triangles\","
            "\"paint_dispatch_exception_kind\":\"access_violation\","
            "\"paint_dispatch_exception_access_operation\":\"read\","
            "\"paint_dispatch_exception_fault_address\":\"0x10\","
            "\"paint_dispatch_exception_instruction_module\":\"bridge\","
            "\"paint_dispatch_exception_module_offset\":\"0x1234\","
            "\"paint_dispatch_exception_restore_verified\":false");
    if (compact_exception_metadata.find(
            "\"paint_dispatch_exception_stage\":\"resolve_runtime_triangles\"") ==
            std::string::npos ||
        compact_exception_metadata.find(
            "\"paint_dispatch_exception_kind\":\"access_violation\"") ==
            std::string::npos ||
        compact_exception_metadata.find(
            "\"paint_dispatch_exception_access_operation\":\"read\"") ==
            std::string::npos ||
        compact_exception_metadata.find(
            "\"paint_dispatch_exception_fault_address\":\"0x10\"") ==
            std::string::npos ||
        compact_exception_metadata.find(
            "\"paint_dispatch_exception_restore_verified\":false") ==
            std::string::npos)
    {
        return 124;
    }
    const auto compact_correction_metadata =
        compact_mesh_response_metadata(
            "\"appearance_calibration_step_texels\":4.0,"
            "\"appearance_correction_field_ok\":true,"
            "\"appearance_correction_field_hash\":\"123\","
            "\"appearance_physical_source_candidate_samples\":7,"
            "\"appearance_physical_accepted_samples\":1,"
            "\"appearance_physical_color_carrier_candidate_samples\":1,"
            "\"appearance_physical_color_carrier_final_samples\":1,"
            "\"appearance_physical_accepted_components\":1,"
            "\"appearance_albedo_only_improvement\":0.25,"
            "\"appearance_final_painted_nonzero_emissive_pixels\":9,"
            "\"appearance_physical_rejection_reason\":\"accepted\"");
    if (compact_correction_metadata.find(
            "\"appearance_calibration_step_texels\":4.0") ==
            std::string::npos ||
        compact_correction_metadata.find(
            "\"appearance_correction_field_ok\":true") ==
            std::string::npos ||
        compact_correction_metadata.find(
            "\"appearance_physical_accepted_samples\":1") ==
            std::string::npos ||
        compact_correction_metadata.find(
            "\"appearance_physical_color_carrier_candidate_samples\":1") ==
            std::string::npos ||
        compact_correction_metadata.find(
            "\"appearance_physical_color_carrier_final_samples\":1") ==
            std::string::npos ||
        compact_correction_metadata.find(
            "\"appearance_physical_accepted_components\":1") ==
            std::string::npos ||
        compact_correction_metadata.find(
            "\"appearance_albedo_only_improvement\":0.25") ==
            std::string::npos ||
        compact_correction_metadata.find(
            "\"appearance_final_painted_nonzero_emissive_pixels\":9") ==
            std::string::npos)
    {
        return 129;
    }

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
    const auto non_finite_response = response_json(
        true,
        "json_finite_regression",
        0,
        0,
        "ok",
        std::string{"\"finite\":1.250000,\"positive\":"} +
            std::to_string(std::numeric_limits<double>::infinity()) +
            ",\"negative\":" +
            std::to_string(-std::numeric_limits<double>::infinity()) +
            ",\"not_a_number\":" +
            std::to_string(std::numeric_limits<double>::quiet_NaN()) +
            ",\"diagnostic\":\"literal inf remains text\"");
    if (non_finite_response.find("\"positive\":-1.000000") ==
            std::string::npos ||
        non_finite_response.find("\"negative\":-1.000000") ==
            std::string::npos ||
        non_finite_response.find("\"not_a_number\":-1.000000") ==
            std::string::npos ||
        non_finite_response.find(
            "\"diagnostic\":\"literal inf remains text\"") ==
            std::string::npos)
    {
        return 78;
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

    // HUD instances are replaced during lobby -> match travel. The callback
    // identity remains the durable contract; the UObject address must not be.
    if (!runtime_contract::esp_hud_callback_matches(0x2000, 0x2000) ||
        runtime_contract::esp_hud_callback_matches(0, 0x2000) ||
        runtime_contract::esp_hud_callback_matches(0x2000, 0x3000))
    {
        return 32;
    }
    if (runtime_contract::esp_hud_rebind_due(true, 1, 10'000, 10'999, 0) ||
        !runtime_contract::esp_hud_rebind_due(true, 1, 10'000, 11'000, 0) ||
        runtime_contract::esp_hud_rebind_due(true, 1, 10'000, 11'500, 11'000) ||
        !runtime_contract::esp_hud_rebind_due(true, 1, 10'000, 13'000, 11'000) ||
        runtime_contract::esp_hud_rebind_due(false, 1, 10'000, 13'000, 0) ||
        runtime_contract::esp_hud_rebind_due(true, 0, 10'000, 13'000, 0))
    {
        return 33;
    }
    if (!runtime_contract::esp_native_renderer_configuration_is_reusable(
            true, true, true, false) ||
        runtime_contract::esp_native_renderer_configuration_is_reusable(
            true, false, true, false) ||
        runtime_contract::esp_native_renderer_configuration_is_reusable(
            true, true, false, false) ||
        runtime_contract::esp_native_renderer_configuration_is_reusable(
            true, true, true, true) ||
        runtime_contract::esp_native_renderer_configuration_is_reusable(
            false, true, false, false))
    {
        return 34;
    }

    const runtime_contract::EspScreenBounds capsule_bounds{
        100.0, 200.0, 200.0, 400.0};
    const auto expanded_bounds =
        runtime_contract::esp_expand_screen_bounds(capsule_bounds, 0.10, 0.05);
    if (expanded_bounds.left != 90.0 ||
        expanded_bounds.top != 190.0 ||
        expanded_bounds.right != 210.0 ||
        expanded_bounds.bottom != 410.0)
    {
        return 35;
    }
    if (!runtime_contract::esp_pose_array_header_usable(0x1000, 96, 128, 28) ||
        runtime_contract::esp_pose_array_header_usable(0, 96, 128, 28) ||
        runtime_contract::esp_pose_array_header_usable(0x1000, 27, 128, 28) ||
        runtime_contract::esp_pose_array_header_usable(0x1000, 96, 95, 28) ||
        runtime_contract::esp_pose_array_header_usable(0x1000, 96, 513, 28))
    {
        return 36;
    }
    if (sdk::FieldOffsets::
            SkeletalMeshComponent_CachedComponentSpaceTransforms !=
        0x05F0)
    {
        return 44;
    }
    constexpr std::array<std::uintptr_t, 4> one_queue{
        0, 0x1000, 0x1000, 0};
    constexpr std::array<std::uintptr_t, 4> ambiguous_queues{
        0x1000, 0x2000, 0x1000, 0};
    constexpr std::array<std::uintptr_t, 4> no_queues{};
    if (runtime_contract::esp_unique_queue_identity(one_queue) !=
            0x1000 ||
        runtime_contract::esp_unique_queue_identity(
            ambiguous_queues) != 0 ||
        runtime_contract::esp_unique_queue_identity(no_queues) != 0)
    {
        return 45;
    }
    const auto glyph_a = runtime_contract::esp_ascii_glyph_rows(L'A');
    const auto glyph_lower_a = runtime_contract::esp_ascii_glyph_rows(L'a');
    const auto glyph_unknown = runtime_contract::esp_ascii_glyph_rows(L'\u3042');
    const auto glyph_question = runtime_contract::esp_ascii_glyph_rows(L'?');
    if (glyph_a[0] != 0x0E || glyph_a[3] != 0x1F ||
        glyph_lower_a != glyph_a ||
        glyph_unknown != glyph_question)
    {
        return 38;
    }
    const auto projection_scale =
        runtime_contract::esp_projection_scale_from_sample(
            1720.0, 1848.0, 1892.0);
    if (std::abs(projection_scale - 1.34375) > 0.000001 ||
        runtime_contract::esp_projection_scale_from_sample(
            1720.0, 1720.0, 1892.0) >= 0.0 ||
        runtime_contract::esp_projection_scale_from_sample(
            1720.0, 1848.0, 2500.0) >= 0.0)
    {
        return 39;
    }
    if (runtime_contract::select_active_world(
            0x2000, true, 0x1000, true) != 0x2000 ||
        runtime_contract::select_active_world(
            0, false, 0x1000, true) != 0x1000 ||
        runtime_contract::select_active_world(
            0, false, 0, false) != 0)
    {
        return 40;
    }
    using PreviewSnapshotDisposition =
        runtime_contract::PreviewSnapshotDisposition;
    if (runtime_contract::preview_snapshot_disposition(
            false, 0, 0x2000) != PreviewSnapshotDisposition::Create ||
        runtime_contract::preview_snapshot_disposition(
            true, 0x2000, 0x2000) != PreviewSnapshotDisposition::Reuse ||
        runtime_contract::preview_snapshot_disposition(
            true, 0x1000, 0x2000) != PreviewSnapshotDisposition::Replace)
    {
        return 42;
    }
    using EspRole = runtime_contract::EspRole;
    using EspScope = runtime_contract::EspScope;
    using EspTargetPawnSource =
        runtime_contract::EspTargetPawnSource;
    if (runtime_contract::esp_select_target_pawn_source(
            EspRole::Hider,
            EspRole::Spectator,
            EspRole::Hider,
            true) != EspTargetPawnSource::RoleRoster ||
        runtime_contract::esp_select_target_pawn_source(
            EspRole::Hunter,
            EspRole::Unknown,
            EspRole::Hunter,
            true) != EspTargetPawnSource::RoleRoster ||
        runtime_contract::esp_select_target_pawn_source(
            EspRole::Hider,
            EspRole::Hider,
            EspRole::Hider,
            true) != EspTargetPawnSource::PlayerArray ||
        runtime_contract::esp_select_target_pawn_source(
            EspRole::Hider,
            EspRole::Spectator,
            EspRole::Hunter,
            true) != EspTargetPawnSource::PlayerArray ||
        runtime_contract::esp_select_target_pawn_source(
            EspRole::Hider,
            EspRole::Spectator,
            EspRole::Hider,
            false) != EspTargetPawnSource::PlayerArray)
    {
        return 39;
    }
    if (!runtime_contract::esp_should_refresh_avatar_directory(
            true, true, 1000, 995, 250) ||
        !runtime_contract::esp_should_refresh_avatar_directory(
            true, false, 1000, 0, 250) ||
        !runtime_contract::esp_should_refresh_avatar_directory(
            true, false, 1250, 1000, 250) ||
        runtime_contract::esp_should_refresh_avatar_directory(
            true, false, 1249, 1000, 250) ||
        runtime_contract::esp_should_refresh_avatar_directory(
            false, true, 1000, 0, 250) ||
        !runtime_contract::esp_should_refresh_avatar_directory(
            true, false, 5, 1000, 250))
    {
        return 39;
    }
    if (!runtime_contract::esp_cached_avatar_binding_is_usable(
            true,
            true,
            true,
            true,
            EspRole::Hider,
            EspRole::Hider) ||
        runtime_contract::esp_cached_avatar_binding_is_usable(
            false,
            true,
            true,
            true,
            EspRole::Hider,
            EspRole::Hider) ||
        runtime_contract::esp_cached_avatar_binding_is_usable(
            true,
            false,
            true,
            true,
            EspRole::Hider,
            EspRole::Hider) ||
        runtime_contract::esp_cached_avatar_binding_is_usable(
            true,
            true,
            false,
            true,
            EspRole::Hider,
            EspRole::Hider) ||
        runtime_contract::esp_cached_avatar_binding_is_usable(
            true,
            true,
            true,
            true,
            EspRole::Hider,
            EspRole::Hunter))
    {
        return 39;
    }
    if (runtime_contract::esp_resolve_target_role(
            EspRole::Hider, EspRole::Hider) !=
            EspRole::Hider ||
        runtime_contract::esp_resolve_target_role(
            EspRole::Hider, EspRole::Unknown) !=
            EspRole::Hider ||
        runtime_contract::esp_resolve_target_role(
            EspRole::Hider, EspRole::Spectator) !=
            EspRole::Spectator ||
        runtime_contract::esp_resolve_target_role(
            EspRole::Unknown, EspRole::Unknown) !=
            EspRole::Unknown)
    {
        return 40;
    }
    if (runtime_contract::esp_current_pawn_role(
            EspRole::Hider, EspRole::Hunter) !=
            EspRole::Hunter ||
        runtime_contract::esp_current_pawn_role(
            EspRole::Hunter, EspRole::Hider) !=
            EspRole::Hider ||
        runtime_contract::esp_current_pawn_role(
            EspRole::Hunter, EspRole::Spectator) !=
            EspRole::Spectator ||
        runtime_contract::esp_current_pawn_role(
            EspRole::Hider, EspRole::Unknown) !=
            EspRole::Hider ||
        runtime_contract::esp_role_scope_matches(
            EspScope::All, EspRole::Spectator) ||
        runtime_contract::esp_role_scope_matches(
            EspScope::Hider, EspRole::Spectator) ||
        !runtime_contract::esp_role_scope_matches(EspScope::All, EspRole::Unknown) ||
        !runtime_contract::esp_role_scope_matches(EspScope::Hider, EspRole::Hider) ||
        runtime_contract::esp_role_scope_matches(EspScope::Hider, EspRole::Hunter) ||
        !runtime_contract::esp_role_scope_matches(EspScope::Hunter, EspRole::Hunter) ||
        runtime_contract::esp_role_scope_matches(EspScope::Hunter, EspRole::Unknown) ||
        runtime_contract::esp_role_color(EspRole::Hider, 0x010203u, 0x040506u) != 0x010203u ||
        runtime_contract::esp_role_color(EspRole::Hunter, 0x010203u, 0x040506u) != 0x040506u ||
        runtime_contract::esp_role_color(EspRole::Unknown, 0x010203u, 0x040506u) != 0xFFFFFFu)
    {
        return 41;
    }
    const auto spectator_capabilities =
        runtime_contract::esp_pawn_geometry_capabilities(
            true, true, true);
    const auto complete_capabilities =
        runtime_contract::esp_pawn_geometry_capabilities(
            false, true, true);
    const auto mesh_only_capabilities =
        runtime_contract::esp_pawn_geometry_capabilities(
            false, false, true);
    const auto capsule_only_capabilities =
        runtime_contract::esp_pawn_geometry_capabilities(
            false, true, false);
    if (!spectator_capabilities.spectator ||
        spectator_capabilities.root_capsule ||
        spectator_capabilities.skeletal_mesh ||
        complete_capabilities.spectator ||
        !complete_capabilities.root_capsule ||
        !complete_capabilities.skeletal_mesh ||
        mesh_only_capabilities.spectator ||
        mesh_only_capabilities.root_capsule ||
        !mesh_only_capabilities.skeletal_mesh ||
        capsule_only_capabilities.spectator ||
        !capsule_only_capabilities.root_capsule ||
        capsule_only_capabilities.skeletal_mesh)
    {
        return 43;
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

    if (runtime_contract::local_dispatch_adaptive_delay_ms(21, 0) != 21 ||
        runtime_contract::local_dispatch_adaptive_delay_ms(21, 4'000) != 21 ||
        runtime_contract::local_dispatch_adaptive_delay_ms(21, 16'423) != 53 ||
        runtime_contract::local_dispatch_adaptive_delay_ms(1, 100'000) != 250 ||
        runtime_contract::local_dispatch_adaptive_delay_ms(
            1,
            std::numeric_limits<std::uint64_t>::max()) != 250 ||
        runtime_contract::local_dispatch_adaptive_delay_ms(300, 100'000) != 300)
    {
        return 142;
    }

    const auto conservative_replication =
        runtime_contract::paint_replication_pacing_plan(
            20,
            20,
            6,
            runtime_contract::NativeRecordedPaintMaxCallsPerTick,
            2,
            false,
            12);
    const auto fallback_replication =
        runtime_contract::paint_replication_pacing_plan(
            0,
            -1,
            -1,
            runtime_contract::NativeRecordedPaintMaxCallsPerTick,
            2,
            false,
            0);
    const auto small_batch_replication =
        runtime_contract::paint_replication_pacing_plan(
            10,
            3,
            6,
            runtime_contract::NativeRecordedPaintMaxCallsPerTick,
            2,
            true,
            12);
    if (conservative_replication.max_outgoing_batches_per_second != 20 ||
        conservative_replication.max_outgoing_strokes_per_batch != 20 ||
        conservative_replication.conservative_network_strokes_per_second != 320 ||
        conservative_replication.conservative_receiver_strokes_per_second != 144 ||
        conservative_replication.effective_strokes_per_second != 144 ||
        conservative_replication.conservative_strokes_per_window != 8 ||
        conservative_replication.calls_per_tick != 3 ||
        conservative_replication.network_window_ms != 50 ||
        conservative_replication.cadence_ms != 21 ||
        conservative_replication.final_confirmation_ms != 467 ||
        fallback_replication.max_outgoing_batches_per_second != 20 ||
        fallback_replication.max_outgoing_strokes_per_batch != 20 ||
        fallback_replication.conservative_receiver_strokes_per_second != 320 ||
        fallback_replication.effective_strokes_per_second != 320 ||
        fallback_replication.calls_per_tick != 3 ||
        fallback_replication.cadence_ms != 10 ||
        small_batch_replication.conservative_strokes_per_window != 2 ||
        small_batch_replication.calls_per_tick != 2 ||
        small_batch_replication.cadence_ms != 100 ||
        small_batch_replication.conservative_receiver_strokes_per_second != 24 ||
        small_batch_replication.effective_strokes_per_second != 20 ||
        small_batch_replication.final_confirmation_ms != 1200)
    {
        return 118;
    }

    if (runtime_contract::paint_queue_observer_authoritative(false, false) ||
        runtime_contract::paint_queue_observer_authoritative(true, false) ||
        !runtime_contract::paint_queue_observer_authoritative(true, true) ||
        runtime_contract::paint_final_queue_ready(
            true, true, 1, true, 0) ||
        runtime_contract::paint_final_queue_ready(
            true, false, 0, true, 1) ||
        !runtime_contract::paint_final_queue_ready(
            true, true, 0, true, 0) ||
        runtime_contract::paint_visual_drain_complete(
            true, false, 0, true, 0, 466, 467) ||
        !runtime_contract::paint_visual_drain_complete(
            true, false, 0, true, 0, 467, 467) ||
        runtime_contract::paint_visual_drain_complete(
            true, true, 1, true, 0, 500, 467) ||
        runtime_contract::paint_visual_drain_complete(
            true, true, 0, true, 1, 500, 467) ||
        runtime_contract::paint_final_confirmation_remaining_ms(
            false, 0, 467, 50) != 517 ||
        runtime_contract::paint_final_confirmation_remaining_ms(
            true, 200, 467, 50) != 267 ||
        runtime_contract::paint_final_confirmation_remaining_ms(
            true, 467, 467, 50) != 0)
    {
        return 119;
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
    const std::array<std::size_t, 3> expected_cross_region_view_order{{2, 1, 0}};
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
        no_compression.entries[0].has_color_override ||
        no_compression.entries[1].replay.sample_index != 1)
    {
        return 24;
    }
    const auto compressed = runtime_contract::build_adaptive_paint_plan(
        adaptive_entries, adaptive_samples, 0.01, 1.0);
    if (compressed.entries.size() != 3 ||
        compressed.compressed_paint_entries != 1 ||
        compressed.expanded_paint_entries != 0 ||
        compressed.entries[0].replay.sample_index != 0 ||
        compressed.entries[0].radius_multiplier != 1.0 ||
        compressed.entries[1].replay.sample_index != 2 ||
        compressed.entries[2].replay.sample_index != 3)
    {
        return 25;
    }
    const std::vector<runtime_contract::AdaptivePaintSample>
        order_invariant_compression_samples{
            {0.100, 0.100, runtime_contract::ReplayRegion::Front, 0,
             0.50, 0.50, 0.50, true, true, 1},
            {0.139, 0.100, runtime_contract::ReplayRegion::Front, 0,
             0.50, 0.50, 0.50, true, true, 1},
            {0.151, 0.100, runtime_contract::ReplayRegion::Front, 0,
             1.00, 0.00, 0.00, false, true, 1},
        };
    const runtime_contract::ReplayEntry order_invariant_first{
        0, runtime_contract::ReplayPass::Paint,
        runtime_contract::ReplayRegion::Front, {0, 0.0, 0}};
    const runtime_contract::ReplayEntry order_invariant_second{
        1, runtime_contract::ReplayPass::Paint,
        runtime_contract::ReplayRegion::Front, {0, 1.0, 1}};
    const auto order_invariant_forward =
        runtime_contract::build_adaptive_paint_plan(
            {order_invariant_first, order_invariant_second},
            order_invariant_compression_samples,
            0.01,
            5.0);
    const auto order_invariant_reverse =
        runtime_contract::build_adaptive_paint_plan(
            {order_invariant_second, order_invariant_first},
            order_invariant_compression_samples,
            0.01,
            5.0);
    if (order_invariant_forward.entries.size() != 2 ||
        order_invariant_reverse.entries.size() != 2 ||
        order_invariant_forward.entries[0].replay.sample_index != 0 ||
        order_invariant_reverse.entries[0].replay.sample_index != 0 ||
        order_invariant_forward.entries[1].replay.sample_index != 1 ||
        order_invariant_reverse.entries[1].replay.sample_index != 1 ||
        order_invariant_forward.entries[0].radius_multiplier != 1.0 ||
        order_invariant_reverse.entries[0].radius_multiplier != 1.0)
    {
        return 132;
    }
    const std::vector<runtime_contract::AdaptivePaintSample>
        isolated_compression_samples{
            {0.500, 0.500, runtime_contract::ReplayRegion::Front, 0,
             0.50, 0.50, 0.50, true, true, 1},
        };
    const auto isolated_compression =
        runtime_contract::build_adaptive_paint_plan(
            {{0,
              runtime_contract::ReplayPass::Paint,
              runtime_contract::ReplayRegion::Front,
              {0, 0.0, 0}}},
            isolated_compression_samples,
            0.01,
            5.0);
    if (isolated_compression.entries.size() != 1 ||
        isolated_compression.entries[0].radius_multiplier != 1.0 ||
        isolated_compression.expanded_paint_entries != 0)
    {
        return 133;
    }
    std::vector<runtime_contract::AdaptivePaintSample>
        compression_hole_samples{};
    for (int y = -8; y <= 8; ++y)
    {
        for (int x = -8; x <= 8; ++x)
        {
            if (x == 1 && y == 0)
            {
                continue;
            }
            compression_hole_samples.push_back(
                {0.505 + static_cast<double>(x) * 0.01,
                 0.505 + static_cast<double>(y) * 0.01,
                 runtime_contract::ReplayRegion::Front,
                 0,
                 0.50,
                 0.50,
                 0.50,
                 true,
                 true,
                 1});
        }
    }
    const std::size_t compression_hole_center =
        static_cast<std::size_t>(8 * 17 + 8);
    const auto compression_hole =
        runtime_contract::build_adaptive_paint_plan(
            {{compression_hole_center,
              runtime_contract::ReplayPass::Paint,
              runtime_contract::ReplayRegion::Front,
              {0, 0.0, compression_hole_center}}},
            compression_hole_samples,
            0.01,
            5.0);
    if (compression_hole.entries.size() != 1 ||
        compression_hole.entries[0].radius_multiplier != 1.0 ||
        compression_hole.expanded_paint_entries != 0)
    {
        return 134;
    }
    std::vector<runtime_contract::AdaptivePaintSample>
        flat_compression_samples{};
    std::vector<runtime_contract::ReplayEntry>
        flat_compression_entries{};
    for (int y = 0; y < 33; ++y)
    {
        for (int x = 0; x < 33; ++x)
        {
            const auto sample_index = flat_compression_samples.size();
            flat_compression_samples.push_back(
                {(20.5 + static_cast<double>(x)) * 0.01,
                 (20.5 + static_cast<double>(y)) * 0.01,
                 runtime_contract::ReplayRegion::Front,
                 0,
                 0.50,
                 0.50,
                 0.50,
                 true,
                 true,
                 1});
            flat_compression_entries.push_back(
                {sample_index,
                 runtime_contract::ReplayPass::Paint,
                 runtime_contract::ReplayRegion::Front,
                 {y, static_cast<double>(x), sample_index}});
        }
    }
    const auto flat_compression =
        runtime_contract::build_adaptive_paint_plan(
            flat_compression_entries,
            flat_compression_samples,
            0.01,
            5.0);
    if (flat_compression.entries.size() >=
            flat_compression_entries.size() / 4 ||
        flat_compression.compressed_paint_entries <=
            flat_compression_entries.size() * 3 / 4 ||
        flat_compression.expanded_paint_entries == 0)
    {
        return 135;
    }
    std::vector<runtime_contract::AdaptivePaintSample>
        large_flat_compression_samples{};
    std::vector<runtime_contract::ReplayEntry>
        large_flat_compression_entries{};
    constexpr int large_flat_side = 64;
    constexpr double large_flat_step =
        1.0 / static_cast<double>(large_flat_side);
    for (int y = 0; y < large_flat_side; ++y)
    {
        for (int x = 0; x < large_flat_side; ++x)
        {
            const auto sample_index =
                large_flat_compression_samples.size();
            large_flat_compression_samples.push_back(
                {(static_cast<double>(x) + 0.5) * large_flat_step,
                 (static_cast<double>(y) + 0.5) * large_flat_step,
                 runtime_contract::ReplayRegion::Front,
                 0,
                 0.50,
                 0.50,
                 0.50,
                 true,
                 true,
                 1});
            large_flat_compression_entries.push_back(
                {sample_index,
                 runtime_contract::ReplayPass::Paint,
                 runtime_contract::ReplayRegion::Front,
                 {y, static_cast<double>(x), sample_index}});
        }
    }
    const auto large_flat_compression =
        runtime_contract::build_adaptive_paint_plan(
            large_flat_compression_entries,
            large_flat_compression_samples,
            large_flat_step,
            5.0);
    if (large_flat_compression.entries.size() > 50 ||
        large_flat_compression.compressed_paint_entries < 4046)
    {
        return 140;
    }
    auto reversed_large_flat_entries =
        large_flat_compression_entries;
    std::reverse(
        reversed_large_flat_entries.begin(),
        reversed_large_flat_entries.end());
    const auto reversed_large_flat_compression =
        runtime_contract::build_adaptive_paint_plan(
            reversed_large_flat_entries,
            large_flat_compression_samples,
            large_flat_step,
            5.0);
    bool large_flat_order_changed_plan =
        reversed_large_flat_compression.entries.size() !=
        large_flat_compression.entries.size();
    for (std::size_t index = 0;
         !large_flat_order_changed_plan &&
         index < large_flat_compression.entries.size();
         ++index)
    {
        const auto& forward =
            large_flat_compression.entries[index];
        const auto& reverse =
            reversed_large_flat_compression.entries[index];
        large_flat_order_changed_plan =
            forward.replay.sample_index !=
                reverse.replay.sample_index ||
            forward.radius_multiplier !=
                reverse.radius_multiplier ||
            forward.has_color_override !=
                reverse.has_color_override ||
            std::abs(forward.r - reverse.r) > 0.000001 ||
            std::abs(forward.g - reverse.g) > 0.000001 ||
            std::abs(forward.b - reverse.b) > 0.000001;
    }
    if (large_flat_order_changed_plan)
    {
        return 141;
    }
    auto flat_compression_with_irrelevant_samples =
        flat_compression_samples;
    flat_compression_with_irrelevant_samples.push_back(
        {0.365,
         0.365,
         runtime_contract::ReplayRegion::Side,
         7,
         1.00,
         0.00,
         0.00,
         false,
         true,
         999,
         false});
    const auto flat_compression_with_irrelevant =
        runtime_contract::build_adaptive_paint_plan(
            flat_compression_entries,
            flat_compression_with_irrelevant_samples,
            0.01,
            5.0);
    bool irrelevant_sample_changed_plan =
        flat_compression_with_irrelevant.entries.size() !=
        flat_compression.entries.size();
    for (std::size_t index = 0;
         !irrelevant_sample_changed_plan &&
         index < flat_compression.entries.size();
         ++index)
    {
        irrelevant_sample_changed_plan =
            flat_compression_with_irrelevant.entries[index]
                    .replay.sample_index !=
                flat_compression.entries[index]
                    .replay.sample_index ||
            flat_compression_with_irrelevant.entries[index]
                    .radius_multiplier !=
                flat_compression.entries[index]
                    .radius_multiplier;
    }
    if (irrelevant_sample_changed_plan)
    {
        return 136;
    }
    std::vector<runtime_contract::AdaptivePaintSample>
        representative_color_samples{};
    for (int y = 0; y < 17; ++y)
    {
        for (int x = 0; x < 17; ++x)
        {
            const double color = x <= 8 ? 0.451 : 0.549;
            representative_color_samples.push_back(
                {(40.5 + static_cast<double>(x)) * 0.01,
                 (40.5 + static_cast<double>(y)) * 0.01,
                 runtime_contract::ReplayRegion::Front,
                 0,
                 color,
                 color,
                 color,
                 true,
                 true,
                 1});
        }
    }
    const std::size_t representative_color_center =
        static_cast<std::size_t>(8 * 17 + 8);
    const auto representative_color =
        runtime_contract::build_adaptive_paint_plan(
            {{representative_color_center,
              runtime_contract::ReplayPass::Paint,
              runtime_contract::ReplayRegion::Front,
              {0, 0.0, representative_color_center}}},
            representative_color_samples,
            0.01,
            10.0);
    if (representative_color.entries.size() != 1 ||
        representative_color.entries[0].radius_multiplier != 8.0 ||
        !representative_color.entries[0].has_color_override ||
        std::abs(representative_color.entries[0].r - 0.5) >
            0.000001 ||
        std::abs(representative_color.entries[0].g - 0.5) >
            0.000001 ||
        std::abs(representative_color.entries[0].b - 0.5) >
            0.000001 ||
        representative_color.representative_paint_entries != 1 ||
        std::abs(
            representative_color.representative_error_max -
            0.049) > 0.000001)
    {
        return 137;
    }
    auto compression_payload_boundary_samples =
        compression_hole_samples;
    compression_payload_boundary_samples.push_back(
        {0.515,
         0.505,
         runtime_contract::ReplayRegion::Front,
         0,
         0.50,
         0.50,
         0.50,
         true,
         true,
         2});
    const auto compression_payload_boundary =
        runtime_contract::build_adaptive_paint_plan(
            {{compression_hole_center,
              runtime_contract::ReplayPass::Paint,
              runtime_contract::ReplayRegion::Front,
              {0, 0.0, compression_hole_center}}},
            compression_payload_boundary_samples,
            0.01,
            5.0);
    if (compression_payload_boundary.entries.size() != 1 ||
        compression_payload_boundary.entries[0]
                .radius_multiplier != 1.0 ||
        compression_payload_boundary.expanded_paint_entries != 0)
    {
        return 138;
    }
    std::vector<runtime_contract::AdaptivePaintSample>
        compression_outer_ring_samples{};
    for (int y = -8; y <= 8; ++y)
    {
        for (int x = -8; x <= 8; ++x)
        {
            compression_outer_ring_samples.push_back(
                {0.505 + static_cast<double>(x) * 0.01,
                 0.505 + static_cast<double>(y) * 0.01,
                 runtime_contract::ReplayRegion::Front,
                 0,
                 0.50,
                 0.50,
                 0.50,
                 true,
                 true,
                 x == 2 && y == 0 ? 2U : 1U});
        }
    }
    const std::size_t compression_outer_ring_center =
        static_cast<std::size_t>(8 * 17 + 8);
    const auto compression_outer_ring =
        runtime_contract::build_adaptive_paint_plan(
            {{compression_outer_ring_center,
              runtime_contract::ReplayPass::Paint,
              runtime_contract::ReplayRegion::Front,
              {0, 0.0, compression_outer_ring_center}}},
            compression_outer_ring_samples,
            0.01,
            5.0,
            0.002);
    if (compression_outer_ring.entries.size() != 1 ||
        compression_outer_ring.entries[0]
                .radius_multiplier != 1.5)
    {
        return 139;
    }
    const std::vector<runtime_contract::AdaptivePaintSample>
        cross_region_compression_samples{
            {0.10, 0.10, runtime_contract::ReplayRegion::Back, 0,
             0.50, 0.50, 0.50, true, true, 1},
            {0.50, 0.50, runtime_contract::ReplayRegion::Side, 0,
             0.50, 0.50, 0.50, true, true, 1},
            {0.90, 0.90, runtime_contract::ReplayRegion::Front, 0,
             0.50, 0.50, 0.50, true, true, 1},
            {0.105, 0.10, runtime_contract::ReplayRegion::Back, 0,
             0.50, 0.50, 0.50, true, true, 2},
            {0.905, 0.90, runtime_contract::ReplayRegion::Front, 0,
             0.50, 0.50, 0.50, true, true, 2},
        };
    const std::vector<runtime_contract::ReplayEntry>
        cross_region_compression_entries{
            {0, runtime_contract::ReplayPass::Paint,
             runtime_contract::ReplayRegion::Back, {0, 0.0, 0}},
            {1, runtime_contract::ReplayPass::Paint,
             runtime_contract::ReplayRegion::Side, {0, 0.0, 1}},
            {2, runtime_contract::ReplayPass::Paint,
             runtime_contract::ReplayRegion::Front, {0, 0.0, 2}},
        };
    const auto cross_region_compression =
        runtime_contract::build_adaptive_paint_plan(
            cross_region_compression_entries,
            cross_region_compression_samples,
            0.01,
            5.0);
    if (cross_region_compression.entries.size() != 3 ||
        cross_region_compression.entries[0].replay.region !=
            runtime_contract::ReplayRegion::Back ||
        cross_region_compression.entries[1].replay.region !=
            runtime_contract::ReplayRegion::Side ||
        cross_region_compression.entries[2].replay.region !=
            runtime_contract::ReplayRegion::Front)
    {
        return 120;
    }
    const std::vector<runtime_contract::AdaptivePaintSample>
        five_percent_blue_boundary_samples{
            {0.10, 0.10, runtime_contract::ReplayRegion::Front, 0,
             1.0, 1.0, 0.729412, true, true, 1},
            {0.102, 0.10, runtime_contract::ReplayRegion::Front, 0,
             1.0, 1.0, 0.584314, true, true, 1},
        };
    const std::vector<runtime_contract::ReplayEntry>
        five_percent_blue_boundary_entries{
            {0, runtime_contract::ReplayPass::Paint,
             runtime_contract::ReplayRegion::Front, {0, 0.0, 0}},
            {1, runtime_contract::ReplayPass::Paint,
             runtime_contract::ReplayRegion::Front, {0, 1.0, 1}},
        };
    const auto five_percent_blue_boundary =
        runtime_contract::build_adaptive_paint_plan(
            five_percent_blue_boundary_entries,
            five_percent_blue_boundary_samples,
            0.01,
            5.0);
    if (five_percent_blue_boundary.entries.size() != 2 ||
        five_percent_blue_boundary.compressed_paint_entries != 0)
    {
        return 111;
    }
    const std::vector<runtime_contract::AdaptivePaintSample>
        five_percent_dark_boundary_samples{
            {0.10, 0.10, runtime_contract::ReplayRegion::Front, 0,
             0.17, 0.10, 0.10, true, true, 1},
            {0.102, 0.10, runtime_contract::ReplayRegion::Front, 0,
             0.10, 0.10, 0.10, true, true, 1},
        };
    const auto five_percent_dark_boundary =
        runtime_contract::build_adaptive_paint_plan(
            five_percent_blue_boundary_entries,
            five_percent_dark_boundary_samples,
            0.01,
            5.0);
    if (five_percent_dark_boundary.entries.size() != 2 ||
        five_percent_dark_boundary.compressed_paint_entries != 0)
    {
        return 112;
    }

    // Appearance Match must not discard a valid game-owned runtime triangle
    // UV merely because the current camera cannot hit that (for example back)
    // surface. A screen hit may replace it only when the hit resolves to the
    // same world surface within one centimetre.
    if (runtime_contract::appearance_paint_uv_route(
            true, true, false, -1.0) !=
            runtime_contract::AppearancePaintUvRoute::RuntimeTriangle ||
        runtime_contract::appearance_paint_uv_route(
            true, true, true, 8.0) !=
            runtime_contract::AppearancePaintUvRoute::RuntimeTriangle ||
        runtime_contract::appearance_paint_uv_route(
            true, true, true, 0.5) !=
            runtime_contract::AppearancePaintUvRoute::ScreenHit ||
        runtime_contract::appearance_paint_uv_route(
            true, false, false, -1.0) !=
            runtime_contract::AppearancePaintUvRoute::Invalid)
    {
        return 81;
    }
    if (!runtime_contract::
            appearance_normalized_screen_position_valid(
                0.5,
                0.5) ||
        runtime_contract::
            appearance_normalized_screen_position_valid(
                -1.0,
                0.5) ||
        runtime_contract::
            appearance_normalized_screen_position_valid(
                std::numeric_limits<double>::quiet_NaN(),
                0.5))
    {
        return 94;
    }

    // Appearance passes sample every safe destination texel in an enabled
    // paint region. Camera-facing source status must not remove back texels;
    // those texels receive the same fixed-view screen projection as BaseColor.
    if (!runtime_contract::appearance_capture_sample_included(
            {true, false}) ||
        runtime_contract::appearance_capture_sample_included(
            {false, false}) ||
        runtime_contract::appearance_capture_sample_included(
            {true, true}))
    {
        return 82;
    }
    if (runtime_contract::
            appearance_manual_preview_feedback_required(
                true, false, 0) ||
        !runtime_contract::
            appearance_manual_preview_feedback_required(
                true, true, 0) ||
        !runtime_contract::
            appearance_manual_preview_feedback_required(
                true, false, 1) ||
        runtime_contract::
            appearance_manual_preview_feedback_required(
                false, true, 1))
    {
        return 122;
    }
    // Manual and Auto share the bounded Albedo-response parameters. Manual
    // contributes only a fixed M/R/E policy, including legal bound values.
    const auto fixed_material_parameters =
        runtime_contract::
            appearance_fixed_material_parameters(
                {0.10, 0.90, 0.10, 0.00,
                 0.75, 0.10, 0.90, 1.00},
                0.25,
                1.00,
                0.40);
    if (fixed_material_parameters.size() != 8 ||
        std::abs(fixed_material_parameters[0] - 0.10) >
            0.000001 ||
        std::abs(fixed_material_parameters[4] - 0.75) >
            0.000001 ||
        std::abs(fixed_material_parameters[1] - 0.25) >
            0.000001 ||
        std::abs(fixed_material_parameters[2] - 1.00) >
            0.000001 ||
        std::abs(fixed_material_parameters[3] - 0.40) >
            0.000001 ||
        std::abs(fixed_material_parameters[5] - 0.25) >
            0.000001 ||
        std::abs(fixed_material_parameters[6] - 1.00) >
            0.000001 ||
        std::abs(fixed_material_parameters[7] - 0.40) >
            0.000001)
    {
        return 106;
    }
    const auto manual_shared_feedback =
        runtime_contract::
            appearance_calibrate_albedo_chromaticity(
                {0.20, 0.10, 0.05},
                {0.40, 0.30, 0.10},
                {0.20, 0.10, 0.05});
    const auto manual_shared_feedback_applied =
        runtime_contract::
            appearance_apply_albedo_chromaticity_gain(
                {0.20, 0.10, 0.05},
                {2.0, 3.0, 2.0});
    if (!manual_shared_feedback.supported ||
        manual_shared_feedback.responsive_channels != 3 ||
        std::abs(
            runtime_contract::appearance_luminance(
                manual_shared_feedback.albedo) -
            runtime_contract::appearance_luminance(
                {0.20, 0.10, 0.05})) >
            0.000001 ||
        std::abs(
            runtime_contract::appearance_luminance(
                manual_shared_feedback_applied) -
            runtime_contract::appearance_luminance(
                {0.20, 0.10, 0.05})) >
            0.000001)
    {
        return 107;
    }
    if (!runtime_contract::
            appearance_albedo_candidate_accepted(
                {19574,
                 true,
                 true,
                 0.077,
                 0.070,
                 0.075,
                 0.065}) ||
        !runtime_contract::
            appearance_albedo_candidate_accepted(
                {19574,
                 true,
                 true,
                 0.077,
                 0.0761,
                 0.075,
                 0.070}) ||
        !runtime_contract::
            appearance_albedo_candidate_accepted(
                {32028,
                 true,
                 true,
                 0.005764,
                 0.005708,
                 0.046401,
                 0.043562}) ||
        runtime_contract::
            appearance_albedo_candidate_accepted(
                {19574,
                 true,
                 true,
                 0.077,
                 0.078,
                 0.075,
                 0.065}) ||
        runtime_contract::
            appearance_albedo_candidate_accepted(
                {19574,
                 true,
                 true,
                 0.077,
                 0.070,
                 0.075,
                 0.076}))
    {
        return 109;
    }
    if (!runtime_contract::
            appearance_feedback_sample_supported(
                {true, true, true, false, true, false}) ||
        runtime_contract::
            appearance_feedback_sample_supported(
                {true, true, true, false, false, false}) ||
        !runtime_contract::
            appearance_feedback_sample_supported(
                {true, true, true, false, false, true}) ||
        runtime_contract::
            appearance_feedback_sample_supported(
                {true, true, true, true, true, true}) ||
        !runtime_contract::appearance_use_direct_face_capture(
            true, true, false, false) ||
        runtime_contract::appearance_use_direct_face_capture(
            true, false, false, false) ||
        !runtime_contract::appearance_projected_material_sample_available(
            true, false, true) ||
        runtime_contract::appearance_projected_material_sample_available(
            true, false, false))
    {
        return 85;
    }
    const runtime_contract::AppearanceRgb occluded_source_base{
        0.05, 0.05, 0.05};
    const runtime_contract::AppearanceRgb occluded_source_display{
        0.08, 0.12, 0.20};
    const auto retained_occluded_source_display =
        runtime_contract::appearance_source_display_or_base(
            occluded_source_base,
            occluded_source_display,
            true);
    const auto missing_source_display =
        runtime_contract::appearance_source_display_or_base(
            occluded_source_base,
            occluded_source_display,
            false);
    if (runtime_contract::appearance_max_channel_delta(
            retained_occluded_source_display,
            occluded_source_display) > 0.000001 ||
        runtime_contract::appearance_max_channel_delta(
            missing_source_display,
            occluded_source_base) > 0.000001)
    {
        return 121;
    }
    if (!runtime_contract::
            appearance_candidate_may_zero_emissive(0) ||
        runtime_contract::
            appearance_candidate_may_zero_emissive(1))
    {
        return 86;
    }
    if (runtime_contract::appearance_source_query_probe_budget(5229) !=
            5229 ||
        runtime_contract::appearance_source_query_probe_budget(9000) !=
            8192 ||
        runtime_contract::appearance_source_query_probe_budget(-1) !=
            0)
    {
        return 83;
    }

    // Appearance Match must retain physical HDR capture values independently
    // of paint-channel limits, while its final fallback remains a legal AMRE
    // payload.  This guards against the FinalColorLDR-style clamp regression.
    const auto hdr = runtime_contract::appearance_sanitize_hdr({2.5, 1.25, 0.5});
    if (!hdr.finite || hdr.clipped || std::abs(hdr.value.r - 2.5) > 0.000001 ||
        std::abs(hdr.value.g - 1.25) > 0.000001 ||
        std::abs(hdr.value.b - 0.5) > 0.000001)
    {
        return 43;
    }
    const auto encoded_half = 0.5;
    const auto linear_half = runtime_contract::appearance_srgb_to_linear(encoded_half);
    if (std::abs(linear_half - 0.2140411405) > 0.000001 ||
        std::abs(runtime_contract::appearance_linear_to_srgb(linear_half) - encoded_half) > 0.000001)
    {
        return 52;
    }
    const auto clipped_hdr = runtime_contract::appearance_sanitize_hdr({128.0, 0.0, 0.0});
    if (!clipped_hdr.finite || !clipped_hdr.clipped ||
        std::abs(clipped_hdr.value.r - runtime_contract::AppearanceHdrMaximum) > 0.000001)
    {
        return 44;
    }
    const auto fallback = runtime_contract::appearance_make_fallback({1.25, 0.50, -1.0});
    if (std::abs(fallback.albedo.r - 1.0) > 0.000001 ||
        std::abs(fallback.albedo.g - 0.50) > 0.000001 ||
        std::abs(fallback.albedo.b - 0.0) > 0.000001 ||
        fallback.material.metallic != 0.0 ||
        std::abs(fallback.material.roughness - runtime_contract::AppearanceFallbackRoughness) > 0.000001 ||
        fallback.material.emissive != 0.0)
    {
        return 45;
    }
    const auto safe_base_fallback =
        runtime_contract::appearance_make_safe_fallback(
            {0.25, 0.10, 0.02},
            {0.60, 0.25, 0.08},
            true);
    const auto safe_display_fallback =
        runtime_contract::appearance_make_safe_fallback(
            {0.25, 0.10, 0.02},
            {0.60, 0.25, 0.08},
            false);
    if (std::abs(safe_base_fallback.albedo.r - 0.25) >
            0.000001 ||
        std::abs(safe_base_fallback.albedo.g - 0.10) >
            0.000001 ||
        std::abs(safe_base_fallback.albedo.b - 0.02) >
            0.000001 ||
        std::abs(safe_display_fallback.albedo.r - 0.60) >
            0.000001 ||
        std::abs(safe_display_fallback.albedo.g - 0.25) >
            0.000001 ||
        std::abs(safe_display_fallback.albedo.b - 0.08) >
            0.000001 ||
        safe_base_fallback.material.metallic != 0.0 ||
        std::abs(
            safe_base_fallback.material.roughness -
            runtime_contract::AppearanceFallbackRoughness) >
            0.000001 ||
        safe_base_fallback.material.emissive != 0.0)
    {
        return 89;
    }
    const auto rejected_emission_fallback =
        runtime_contract::appearance_make_safe_final_fallback(
            {0.02, 0.01, 0.01},
            {0.90, 0.20, 0.05},
            true,
            true);
    const auto rejected_non_emission_fallback =
        runtime_contract::appearance_make_safe_final_fallback(
            {0.02, 0.01, 0.01},
            {0.90, 0.20, 0.05},
            true,
            false);
    if (std::abs(
            rejected_emission_fallback.albedo.r -
            0.90) >
            0.000001 ||
        std::abs(
            rejected_emission_fallback.albedo.g -
            0.20) >
            0.000001 ||
        std::abs(
            rejected_emission_fallback.albedo.b -
            0.05) >
            0.000001 ||
        rejected_emission_fallback.material.emissive !=
            0.0 ||
        std::abs(
            rejected_non_emission_fallback.albedo.r -
            0.02) >
            0.000001 ||
        std::abs(
            rejected_non_emission_fallback.albedo.g -
            0.01) >
            0.000001 ||
        std::abs(
            rejected_non_emission_fallback.albedo.b -
            0.01) >
            0.000001 ||
        rejected_non_emission_fallback.material.emissive !=
            0.0)
    {
        return 113;
    }
    const runtime_contract::AppearanceMaterial matte{0.5, 0.0, 0.65, 0.0};
    const runtime_contract::AppearanceMaterial emissive{0.5, 0.0, 0.65, 0.5};
    if (runtime_contract::appearance_material_key(matte, false) ==
        runtime_contract::appearance_material_key(emissive, false))
    {
        return 46;
    }
    if (runtime_contract::appearance_oklab_delta_e({0.25, 0.50, 0.75}, {0.25, 0.50, 0.75}) != 0.0 ||
        runtime_contract::appearance_oklab_delta_e({0.25, 0.50, 0.75}, {0.75, 0.50, 0.25}) <= 0.01 ||
        runtime_contract::appearance_huber_loss(0.0) != 0.0)
    {
        return 50;
    }
    std::vector<runtime_contract::AppearanceRgb> calibration_expected(16, {0.10, 0.25, 0.80});
    std::vector<runtime_contract::AppearanceRgb> calibration_raw(16, {0.80, 0.25, 0.10});
    const auto calibration = runtime_contract::appearance_calibrate_linear_readback(
        calibration_expected, calibration_raw);
    if (!calibration.ok ||
        calibration.transform != runtime_contract::AppearanceReadbackTransform::SwapRedBlue ||
        calibration.median_error > 0.000001)
    {
        return 47;
    }
    std::vector<double> fit_parameters{0.90, 0.90, 0.10, 0.10};
    const std::vector<double> fit_target{0.25, 0.15, 0.75, 0.80};
    const auto fit_loss = [&fit_target](const std::vector<double>& values) {
        double total = 0.0;
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            const auto delta = values[index] - fit_target[index];
            total += delta * delta;
        }
        return total;
    };
    const auto initial_fit_loss = fit_loss(fit_parameters);
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        const auto pair = runtime_contract::appearance_spsa_pair(fit_parameters, iteration, 0x218u);
        fit_parameters = runtime_contract::appearance_spsa_update(
            fit_parameters, pair, fit_loss(pair.plus), fit_loss(pair.minus), iteration);
    }
    if (fit_loss(fit_parameters) >= initial_fit_loss * 0.70)
    {
        return 48;
    }
    const auto bounded_albedo =
        runtime_contract::appearance_blend_albedo(
            {1.0, 0.8, 0.6},
            {0.5, 0.4, 0.3},
            2.0);
    const runtime_contract::AppearanceRgb neutral_emission_base{
        0.20, 0.19, 0.18};
    const runtime_contract::AppearanceRgb yellow_emission_target{
        1.0, 0.30, 0.01};
    const runtime_contract::AppearanceRgb shadowed_display{
        0.04, 0.03, 0.02};
    const auto shadows_excluded_target =
        runtime_contract::appearance_source_albedo_target(
            neutral_emission_base,
            shadowed_display,
            yellow_emission_target,
            false,
            false);
    const auto shadows_included_target =
        runtime_contract::appearance_source_albedo_target(
            neutral_emission_base,
            shadowed_display,
            yellow_emission_target,
            false,
            true);
    const auto emission_target_with_shadows_enabled =
        runtime_contract::appearance_source_albedo_target(
            neutral_emission_base,
            shadowed_display,
            yellow_emission_target,
            true,
            true);
    if (runtime_contract::appearance_max_channel_delta(
            shadows_excluded_target,
            neutral_emission_base) > 0.000001 ||
        runtime_contract::appearance_max_channel_delta(
            shadows_included_target,
            shadowed_display) > 0.000001 ||
        runtime_contract::appearance_max_channel_delta(
            emission_target_with_shadows_enabled,
            yellow_emission_target) > 0.000001)
    {
        return 101;
    }
    const auto scaled_emission_albedo =
        runtime_contract::appearance_parameterized_albedo(
            neutral_emission_base,
            yellow_emission_target,
            0.50,
            true);
    const auto blended_non_emission_albedo =
        runtime_contract::appearance_parameterized_albedo(
            neutral_emission_base,
            yellow_emission_target,
            0.50,
            false);
    if (std::abs(bounded_albedo.r - 0.5) > 0.000001 ||
        std::abs(bounded_albedo.g - 0.4) > 0.000001 ||
        std::abs(bounded_albedo.b - 0.3) > 0.000001 ||
        std::abs(scaled_emission_albedo.r - 0.50) > 0.000001 ||
        std::abs(scaled_emission_albedo.g - 0.15) > 0.000001 ||
        std::abs(scaled_emission_albedo.b - 0.005) > 0.000001 ||
        runtime_contract::appearance_rgb_chromaticity_delta(
            scaled_emission_albedo,
            yellow_emission_target) > 0.000001 ||
        std::abs(blended_non_emission_albedo.r - 0.60) > 0.000001 ||
        std::abs(blended_non_emission_albedo.g - 0.245) > 0.000001 ||
        std::abs(blended_non_emission_albedo.b - 0.095) > 0.000001 ||
        runtime_contract::appearance_parameter_bound(0) != 1.0 ||
        runtime_contract::appearance_parameter_bound(1) != 1.0)
    {
        return 71;
    }
    const runtime_contract::AppearanceRgb manual_emission_display{
        0.9400, 0.9382, 0.8655};
    const runtime_contract::AppearanceRgb manual_emission_chromaticity{
        1.0000, 0.9350, 0.2652};
    const auto manual_e0_emission_albedo =
        runtime_contract::appearance_manual_fixed_material_albedo(
            manual_emission_display,
            manual_emission_chromaticity,
            true,
            0.0);
    const auto manual_e1_emission_albedo =
        runtime_contract::appearance_manual_fixed_material_albedo(
            manual_emission_display,
            manual_emission_chromaticity,
            true,
            1.0);
    const auto manual_e0_surface_albedo =
        runtime_contract::appearance_manual_fixed_material_albedo(
            manual_emission_display,
            neutral_emission_base,
            false,
            0.0);
    if (runtime_contract::appearance_max_channel_delta(
            manual_e0_emission_albedo,
            manual_emission_display) > 0.000001 ||
        runtime_contract::appearance_max_channel_delta(
            manual_e1_emission_albedo,
            manual_emission_chromaticity) > 0.000001 ||
        runtime_contract::appearance_max_channel_delta(
            manual_e0_surface_albedo,
            neutral_emission_base) > 0.000001)
    {
        return 115;
    }
    const runtime_contract::AppearanceRgb response_baseline{0.80, 0.60, 0.40};
    const runtime_contract::AppearanceRgb response_endpoint{0.20, 0.15, 0.10};
    const runtime_contract::AppearanceRgb response_midpoint{
        std::sqrt((1.0 + response_baseline.r) *
                  (1.0 + response_endpoint.r)) -
            1.0,
        std::sqrt((1.0 + response_baseline.g) *
                  (1.0 + response_endpoint.g)) -
            1.0,
        std::sqrt((1.0 + response_baseline.b) *
                  (1.0 + response_endpoint.b)) -
            1.0};
    const auto bounded_response =
        runtime_contract::appearance_calibrate_bounded_response(
            response_midpoint,
            response_baseline,
            response_endpoint,
            1.0,
            2.0,
            0.0,
            2.0);
    if (!bounded_response.supported ||
        std::abs(bounded_response.parameter - 1.5) > 0.000001)
    {
        return 72;
    }
    const runtime_contract::AppearanceRgb display_endpoint{0.20, 0.15, 0.10};
    const runtime_contract::AppearanceRgb base_endpoint{0.80, 0.60, 0.40};
    const runtime_contract::AppearanceRgb albedo_midpoint{
        std::sqrt((1.0 + display_endpoint.r) *
                  (1.0 + base_endpoint.r)) -
            1.0,
        std::sqrt((1.0 + display_endpoint.g) *
                  (1.0 + base_endpoint.g)) -
            1.0,
        std::sqrt((1.0 + display_endpoint.b) *
                  (1.0 + base_endpoint.b)) -
            1.0};
    const auto calibrated_albedo =
        runtime_contract::appearance_calibrate_albedo_blend(
            albedo_midpoint,
            display_endpoint,
            base_endpoint);
    if (!calibrated_albedo.supported ||
        std::abs(calibrated_albedo.parameter - 0.5) > 0.000001)
    {
        return 73;
    }
    const runtime_contract::AppearanceRgb channel_base_albedo{
        0.20, 0.20, 0.20};
    const auto channel_response = [](
                                      const runtime_contract::AppearanceRgb& albedo) {
        return runtime_contract::AppearanceRgb{
            0.75 * albedo.r,
            0.60 * albedo.g,
            1.10 * albedo.b};
    };
    const auto calibrated_rgb =
        runtime_contract::appearance_calibrate_albedo_chromaticity(
            channel_base_albedo,
            {0.30, 0.30, 0.30},
            channel_response(channel_base_albedo));
    const auto corrected_response =
        channel_response(calibrated_rgb.albedo);
    const auto corrected_sum =
        corrected_response.r +
        corrected_response.g +
        corrected_response.b;
    if (!calibrated_rgb.supported ||
        calibrated_rgb.responsive_channels != 3 ||
        std::abs(
            runtime_contract::appearance_luminance(
                calibrated_rgb.albedo) -
            runtime_contract::appearance_luminance(
                channel_base_albedo)) > 0.000001 ||
        std::abs(
            corrected_response.r / corrected_sum -
            1.0 / 3.0) > 0.000001 ||
        std::abs(
            corrected_response.g / corrected_sum -
            1.0 / 3.0) > 0.000001 ||
        std::abs(
            corrected_response.b / corrected_sum -
            1.0 / 3.0) > 0.000001)
    {
        return 81;
    }
    std::array<std::vector<double>, 3>
        cluster_log_gain_estimates{};
    for (int index = 0; index < 64; ++index)
    {
        cluster_log_gain_estimates[0].push_back(
            std::log(0.90));
        cluster_log_gain_estimates[1].push_back(
            std::log(1.10));
        cluster_log_gain_estimates[2].push_back(
            std::log(1.00));
    }
    cluster_log_gain_estimates[0].push_back(
        std::log(0.01));
    cluster_log_gain_estimates[1].push_back(
        std::log(100.0));
    const auto robust_cluster_gain =
        runtime_contract::
            appearance_robust_albedo_chromaticity_gain(
                cluster_log_gain_estimates);
    const auto robust_cluster_albedo =
        runtime_contract::
            appearance_apply_albedo_chromaticity_gain(
                {0.20, 0.20, 0.20},
                robust_cluster_gain.gain);
    if (!robust_cluster_gain.supported ||
        robust_cluster_gain.responsive_channels != 3 ||
        std::abs(robust_cluster_gain.gain.r - 0.90) >
            0.000001 ||
        std::abs(robust_cluster_gain.gain.g - 1.10) >
            0.000001 ||
        std::abs(robust_cluster_gain.gain.b - 1.00) >
            0.000001 ||
        !(robust_cluster_albedo.r <
              robust_cluster_albedo.b &&
          robust_cluster_albedo.b <
              robust_cluster_albedo.g) ||
        std::abs(
            runtime_contract::appearance_luminance(
                robust_cluster_albedo) -
            runtime_contract::appearance_luminance(
                {0.20, 0.20, 0.20})) > 0.000001)
    {
        return 84;
    }
    const auto dark_neutral_calibration =
        runtime_contract::appearance_calibrate_albedo_chromaticity(
            {0.05, 0.05, 0.05},
            {0.05, 0.05, 0.05},
            {0.0450, 0.0450, 0.0550});
    if (!dark_neutral_calibration.supported ||
        dark_neutral_calibration.albedo.r <=
            dark_neutral_calibration.albedo.b ||
        dark_neutral_calibration.albedo.g <=
            dark_neutral_calibration.albedo.b ||
        std::abs(
            runtime_contract::appearance_luminance(
                dark_neutral_calibration.albedo) -
            runtime_contract::appearance_luminance(
                {0.05, 0.05, 0.05})) > 0.000001)
    {
        return 82;
    }
    if (runtime_contract::appearance_rgb_chromaticity_delta(
            {0.20, 0.10, 0.05},
            {0.40, 0.20, 0.10}) > 0.000001 ||
        runtime_contract::appearance_rgb_chromaticity_delta(
            {0.20, 0.20, 0.20},
            {0.16, 0.20, 0.24}) < 0.05)
    {
        return 83;
    }
    const std::vector<double> clustered_parameters{
        0.50, 0.20, 0.60, 0.30,
        0.50, 0.20, 0.60, 0.30};
    const auto clustered_pair = runtime_contract::appearance_spsa_pair(
        clustered_parameters, 0, 0x218u);
    const auto clustered_update =
        runtime_contract::appearance_spsa_update_by_cluster(
            clustered_parameters,
            clustered_pair,
            {0.10, 0.90},
            {0.90, 0.10},
            0);
    if (clustered_update.size() != clustered_parameters.size() ||
        clustered_update == clustered_parameters)
    {
        return 70;
    }
    for (std::size_t index = 0; index < clustered_update.size(); ++index)
    {
        if (!std::isfinite(clustered_update[index]) ||
            clustered_update[index] < 0.0 ||
            clustered_update[index] >
                runtime_contract::appearance_parameter_bound(index))
        {
            return 70;
        }
    }
    runtime_contract::AppearanceBestCandidate deadline_best{};
    const std::vector<double> deadline_plus{0.20, 0.30, 0.40, 0.50};
    const std::vector<double> deadline_later{0.90, 0.90, 0.90, 0.90};
    if (!runtime_contract::appearance_keep_best_candidate(
            deadline_best, deadline_plus, 0.12, true, true) ||
        runtime_contract::appearance_keep_best_candidate(
            deadline_best, deadline_later, 0.20, false, false) ||
        !deadline_best.available || deadline_best.parameters != deadline_plus ||
        std::abs(deadline_best.loss - 0.12) > 0.000001 ||
        !deadline_best.use_feedback_albedo ||
        !deadline_best.use_base_fallback)
    {
        return 51;
    }
    if (!runtime_contract::appearance_fit_accepted({256, true, true, 1.0, 0.80, 0.04}) ||
        runtime_contract::appearance_fit_accepted({255, true, true, 1.0, 0.80, 0.04}) ||
        runtime_contract::appearance_fit_accepted({256, false, true, 1.0, 0.80, 0.04}) ||
        runtime_contract::appearance_fit_accepted({256, true, true, 1.0, 0.90, 0.06}))
    {
        return 49;
    }
    const runtime_contract::AppearanceNonEmissionCandidateAcceptance
        non_emission_candidate{
            256,
            0,
            true,
            true,
            true,
            1.0,
            0.80,
            0.04,
            32,
            0.10,
            0.105,
            0.34,
            0.30};
    if (!runtime_contract::appearance_non_emission_candidate_accepted(
            non_emission_candidate) ||
        runtime_contract::appearance_non_emission_candidate_accepted(
            {256, 1, true, true, true,
             1.0, 0.80, 0.04, 32, 0.10, 0.105}) ||
        runtime_contract::appearance_non_emission_candidate_accepted(
            {256, 0, true, true, true,
             1.0, 0.80, 0.04, 32, 0.10, 0.111,
             0.34, 0.30}) ||
        runtime_contract::appearance_non_emission_candidate_accepted(
            {256, 0, true, true, true,
             1.0, 0.80, 0.04, 32, 0.10, 0.105,
             0.34, 0.64}))
    {
        return 74;
    }
    if (runtime_contract::
            appearance_preview_refinement_worthwhile(
                {1364, 5, 5, true, 0.0124, 0.0215}) ||
        !runtime_contract::
            appearance_preview_refinement_worthwhile(
                {1364, 5, 5, true, 0.0124, 0.0100}) ||
        !runtime_contract::
            appearance_preview_refinement_worthwhile(
                {1364, 4, 5, true, 0.0124, 0.0215}) ||
        !runtime_contract::
            appearance_preview_refinement_worthwhile(
                {0, 5, 5, true, 0.0, 0.0}) ||
        !runtime_contract::
            appearance_preview_refinement_worthwhile(
                {1364, 5, 5, false, 0.0124, 0.0215}))
    {
        return 91;
    }
    const auto response_channel = [](double fallback_channel,
                                     double endpoint_channel,
                                     double emissive) {
        return std::expm1(
            std::log1p(fallback_channel) +
            (std::log1p(endpoint_channel) - std::log1p(fallback_channel)) * emissive);
    };
    const runtime_contract::AppearanceRgb calibrated_source{
        response_channel(0.20, 1.20, 0.65),
        response_channel(0.10, 0.70, 0.65),
        response_channel(0.05, 0.35, 0.65)};
    const auto calibrated_emissive = runtime_contract::appearance_calibrate_emissive(
        calibrated_source,
        {0.20, 0.10, 0.05},
        {1.20, 0.70, 0.35});
    if (!calibrated_emissive.supported ||
        std::abs(calibrated_emissive.emissive - 0.65) > 0.000001 ||
        calibrated_emissive.response_energy <= 0.0 ||
        runtime_contract::appearance_emission_projected_value(
            0.0,
            false) != 0.0 ||
        runtime_contract::appearance_quantize_unit(
            runtime_contract::appearance_emission_projected_value(
                0.0,
                true)) == 0 ||
        std::abs(
            runtime_contract::appearance_emission_projected_value(
                0.65,
                true) -
            0.65) >
            0.000001 ||
        runtime_contract::appearance_calibrate_emissive(
            {0.40, 0.30, 0.20},
            {0.20, 0.10, 0.05},
            {0.20, 0.10, 0.05}).supported ||
        runtime_contract::appearance_calibrate_emissive(
            {0.40, 0.30, 0.20},
            {0.20, 0.10, 0.05},
            {0.10, 0.05, 0.02}).supported)
    {
        return 55;
    }
    if (!runtime_contract::appearance_cluster_emissive_supported(
            {976, 697, 0.538698, 0.178313, 0.176231}) ||
        runtime_contract::appearance_cluster_emissive_supported(
            {2135, 452, 0.000001, 0.020603, 0.020559}) ||
        !runtime_contract::appearance_cluster_emissive_supported(
            {1890, 634, 0.785203, 0.206280, 0.206299}) ||
        runtime_contract::appearance_cluster_emissive_supported(
            {1000, 40, 0.538698, 0.178313, 0.176231}) ||
        !runtime_contract::appearance_cluster_emissive_supported(
            {1000, 600, 0.538698, 0.178313, 0.178100}))
    {
        return 56;
    }
    if (runtime_contract::appearance_select_cluster_candidate(
            {206, 200, 0.263041, 0.121084, 0.120619, 0.120523}) !=
            runtime_contract::AppearanceClusterCandidate::SourceSeed ||
        runtime_contract::appearance_select_cluster_candidate(
            {954, 954, 0.789117, 0.224147, 0.224237, 0.224119}) !=
            runtime_contract::AppearanceClusterCandidate::SourceSeed ||
        runtime_contract::appearance_select_cluster_candidate(
            {1105, 1105, 0.746368, 0.259324, 0.258232, 0.258398}) !=
            runtime_contract::AppearanceClusterCandidate::Endpoint ||
        runtime_contract::appearance_select_cluster_candidate(
            {577, 37, 0.015234, 0.022281, 0.022935, 0.022173}) !=
            runtime_contract::AppearanceClusterCandidate::Fallback ||
        runtime_contract::appearance_select_cluster_candidate(
            {954, 954, 0.789117, 0.224147, 0.224237, 0.224140}) !=
            runtime_contract::AppearanceClusterCandidate::Fallback)
    {
        return 58;
    }
    if (runtime_contract::appearance_calibrated_cluster_emissive_supported(
            {851, 811, 0.610148, 0.121491, 0.125644}) ||
        !runtime_contract::appearance_calibrated_cluster_emissive_supported(
            {2945, 2935, 1.0, 0.471371, 0.123137}) ||
        !runtime_contract::appearance_calibrated_cluster_emissive_supported(
            {800, 743, 0.541389, 0.099532, 0.099931, 629}) ||
        runtime_contract::appearance_calibrated_cluster_emissive_supported(
            {800, 719, 0.541389, 0.099532, 0.099931, 629}) ||
        runtime_contract::appearance_calibrated_cluster_emissive_supported(
            {800, 743, 0.541389, 0.099532, 0.099931, 599}) ||
        runtime_contract::appearance_calibrated_cluster_emissive_supported(
            {800, 743, 0.541389, 0.099532, 0.101000, 629}))
    {
        return 116;
    }
    if (runtime_contract::appearance_emission_material_active(
            true,
            0.0) ||
        runtime_contract::appearance_emission_material_active(
            false,
            0.610148) ||
        !runtime_contract::appearance_emission_material_active(
            true,
            0.610148))
    {
        return 96;
    }
    const runtime_contract::AppearanceCalibratedEmissiveAcceptance calibrated_acceptance{
        19404,
        697,
        1,
        true,
        true,
        0.050935,
        0.040000,
        0.114237,
        0.040000,
        1.0};
    if (!runtime_contract::appearance_calibrated_emissive_accepted(
            calibrated_acceptance) ||
        runtime_contract::appearance_calibrated_emissive_accepted(
            {19404, 697, 0, true, true, 0.050935, 0.040000, 0.114237, 0.040000, 1.0}) ||
        runtime_contract::appearance_calibrated_emissive_accepted(
            {19404, 40, 1, true, true, 0.050935, 0.040000, 0.114237, 0.040000, 1.0}) ||
        runtime_contract::appearance_calibrated_emissive_accepted(
            {19404, 697, 1, true, true, 0.050935, 0.050950, 0.114237, 0.114100, 1.0}) ||
        runtime_contract::appearance_calibrated_emissive_accepted(
            {19404, 697, 1, true, true, 0.050935, 0.040000, 0.114237, 0.060000, 1.0}) ||
        // Regression: the former v3/v4 path accepted this 0.07% loss change
        // with a visibly bad median. It must never be reported as matched.
        runtime_contract::appearance_calibrated_emissive_accepted(
            {19404, 697, 1, true, true, 0.050935, 0.050900, 0.114237, 0.114100, 1.0}))
    {
        return 57;
    }
    const auto r10 = runtime_contract::appearance_decode_r10g10b10a2(
        1023u | (512u << 10u) | (1u << 20u));
    const auto rgba8 = runtime_contract::appearance_decode_rgba8(
        0xff008040u, false);
    const auto bgra8 = runtime_contract::appearance_decode_rgba8(
        0xff008040u, true);
    if (std::abs(r10.r - 1.0) > 0.000001 ||
        std::abs(r10.g - 512.0 / 1023.0) > 0.000001 ||
        std::abs(r10.b - 1.0 / 1023.0) > 0.000001 ||
        std::abs(rgba8.r - 64.0 / 255.0) > 0.000001 ||
        std::abs(rgba8.g - 128.0 / 255.0) > 0.000001 ||
        rgba8.b != 0.0 ||
        bgra8.r != 0.0 ||
        std::abs(bgra8.g - 128.0 / 255.0) > 0.000001 ||
        std::abs(bgra8.b - 64.0 / 255.0) > 0.000001)
    {
        return 59;
    }

    const auto& intrinsic_show_flags =
        runtime_contract::appearance_intrinsic_emission_show_flags();
    const auto intrinsic_flag_disabled =
        [&](const char* expected_name) {
            return std::any_of(
                intrinsic_show_flags.begin(),
                intrinsic_show_flags.end(),
                [&](const auto& setting) {
                    return std::string(setting.name) == expected_name &&
                           !setting.enabled;
                });
        };
    if (intrinsic_show_flags.size() < 20 ||
        !intrinsic_flag_disabled("Lighting") ||
        !intrinsic_flag_disabled("DeferredLighting") ||
        !intrinsic_flag_disabled("GlobalIllumination") ||
        !intrinsic_flag_disabled("ReflectionEnvironment") ||
        !intrinsic_flag_disabled("Bloom") ||
        !intrinsic_flag_disabled("Tonemapper") ||
        !intrinsic_flag_disabled("EyeAdaptation") ||
        !intrinsic_flag_disabled("UnlitViewmode"))
    {
        return 74;
    }
    const runtime_contract::AppearanceCapturePoolKey
        reusable_capture{
            1920,
            1080,
            3,
            false,
            0x218ULL};
    const runtime_contract::AppearanceCapturePoolKey
        same_resource_different_pass{
            1920,
            1080,
            3,
            false,
            0x218ULL};
    const runtime_contract::AppearanceCapturePoolKey
        isolated_resource{
            1920,
            1080,
            3,
            true,
            0x218ULL};
    const runtime_contract::AppearanceCapturePoolKey
        different_visibility{
            1920,
            1080,
            3,
            false,
            0x219ULL};
    if (!runtime_contract::
            appearance_capture_pool_key_matches(
                reusable_capture,
                same_resource_different_pass) ||
        runtime_contract::
            appearance_capture_pool_key_matches(
                reusable_capture,
                isolated_resource) ||
        runtime_contract::
            appearance_capture_pool_key_matches(
                reusable_capture,
                different_visibility))
    {
        return 90;
    }
    const auto sparse_capture_indices =
        runtime_contract::
            appearance_capture_sparse_pixel_indices(
                4,
                3,
                {{0, 0}, {1, 1}});
    const std::vector<std::size_t>
        expected_sparse_capture_indices{
            0U, 3U, 5U, 6U, 8U, 11U};
    if (sparse_capture_indices !=
            expected_sparse_capture_indices ||
        !runtime_contract::
            appearance_capture_sparse_pixel_indices(
                0,
                3,
                {{0, 0}})
             .empty() ||
        !runtime_contract::
            appearance_capture_sparse_pixel_indices(
                4,
                3,
                {{-1, 0}, {4, 2}, {0, 3}})
             .empty())
    {
        return 92;
    }
    std::size_t projection_diagnostic_samples = 0;
    for (std::size_t index = 0; index < 1000U; ++index)
    {
        projection_diagnostic_samples +=
            runtime_contract::
                    appearance_projection_diagnostic_sample(
                        index,
                        1000U,
                        128U)
                ? 1U
                : 0U;
    }
    if (projection_diagnostic_samples == 0U ||
        projection_diagnostic_samples > 128U ||
        !runtime_contract::
            appearance_projection_diagnostic_sample(
                63U,
                64U,
                128U) ||
        runtime_contract::
            appearance_projection_diagnostic_sample(
                1000U,
                1000U,
                128U) ||
        runtime_contract::
            appearance_projection_diagnostic_sample(
                0U,
                1000U,
                0U))
    {
        return 93;
    }

    const auto emission_noise = runtime_contract::appearance_emission_noise_model(
        {0.001, 0.002, 0.003, 0.004, 0.005});
    if (!emission_noise.ok ||
        std::abs(emission_noise.median - 0.003) > 0.000001 ||
        std::abs(emission_noise.mad - 0.001) > 0.000001 ||
        std::abs(emission_noise.threshold - 0.013) > 0.000001 ||
        runtime_contract::appearance_emission_sample_detected(
            0.013,
            emission_noise) ||
        !runtime_contract::appearance_emission_sample_detected(
            0.014,
            emission_noise))
    {
        return 73;
    }
    std::vector<double> broad_emission_samples{};
    for (int index = 0; index < 28; ++index)
    {
        broad_emission_samples.push_back(
            0.017 + static_cast<double>(index % 3) * 0.001);
    }
    for (int index = 0; index < 72; ++index)
    {
        broad_emission_samples.push_back(
            0.49 + static_cast<double>(index % 5) * 0.005);
    }
    std::vector<double> uniform_source_bias(100, 0.50);
    const auto broad_emission_noise =
        runtime_contract::appearance_source_emission_noise_model(
            broad_emission_samples);
    const auto uniform_source_noise =
        runtime_contract::appearance_source_emission_noise_model(
            uniform_source_bias);
    const auto broad_emission_with_target_e0 =
        runtime_contract::appearance_combine_emission_noise_models(
            broad_emission_noise,
            {true, 0.020, 0.001, 0.030});
    if (!broad_emission_noise.ok ||
        !broad_emission_noise.separated_signal ||
        broad_emission_noise.baseline_samples != 28 ||
        broad_emission_noise.threshold >= 0.05 ||
        !runtime_contract::appearance_emission_sample_detected(
            0.49,
            broad_emission_noise) ||
        !uniform_source_noise.ok ||
        uniform_source_noise.separated_signal ||
        runtime_contract::appearance_emission_sample_detected(
            0.50,
            uniform_source_noise) ||
        !broad_emission_with_target_e0.ok ||
        std::abs(
            broad_emission_with_target_e0.threshold -
            0.030) >
            0.000001)
    {
        return 114;
    }
    const auto combined_emission_noise =
        runtime_contract::appearance_combine_emission_noise_models(
            {true, 0.066392, 0.055373, 0.509374},
            {true, 0.017971, 0.000681, 0.027971});
    const auto target_dominant_emission_noise =
        runtime_contract::appearance_combine_emission_noise_models(
            {true, 0.001, 0.001, 0.011},
            {true, 0.020, 0.010, 0.100});
    if (!combined_emission_noise.ok ||
        std::abs(
            combined_emission_noise.threshold -
            0.509374) >
            0.000001 ||
        runtime_contract::appearance_emission_sample_detected(
            0.10,
            combined_emission_noise) ||
        !runtime_contract::appearance_emission_sample_detected(
            0.60,
            combined_emission_noise) ||
        !target_dominant_emission_noise.ok ||
        std::abs(
            target_dominant_emission_noise.threshold -
            0.100) >
            0.000001 ||
        runtime_contract::appearance_combine_emission_noise_models(
            {},
            {true, 0.020, 0.010, 0.100})
            .ok)
    {
        return 79;
    }

    const auto spatial_emission =
        runtime_contract::appearance_filter_emission_regions(
            {{0, 0}, {1, 0}, {1, 1}, {10, 10}},
            3);
    if (spatial_emission.region_count != 1 ||
        spatial_emission.kept_samples != 3 ||
        spatial_emission.rejected_samples != 1 ||
        spatial_emission.keep.size() != 4 ||
        !spatial_emission.keep[0] ||
        !spatial_emission.keep[1] ||
        !spatial_emission.keep[2] ||
        spatial_emission.keep[3])
    {
        return 77;
    }

    const std::vector<runtime_contract::AppearanceEmissionSurfacePoint>
        core_halo_emission{
            {{0, 0}, 1.20, 100}, {{1, 0}, 1.18, 100},
            {{2, 0}, 1.22, 100}, {{3, 0}, 1.19, 100},
            {{4, 0}, 1.24, 100}, {{5, 0}, 1.21, 100},
            {{0, 1}, 0.82, 100}, {{1, 1}, 0.80, 100},
            {{2, 1}, 0.74, 200}, {{3, 1}, 0.76, 200},
            {{4, 1}, 0.78, 200}, {{5, 1}, 0.80, 200},
            {{0, 2}, 0.72, 200}, {{1, 2}, 0.75, 200},
            {{2, 2}, 0.77, 200}, {{3, 2}, 0.79, 200},
            {{4, 2}, 0.81, 200}, {{5, 2}, 0.73, 200},
            {{0, 3}, 0.74, 201}, {{1, 3}, 0.76, 201},
            {{2, 3}, 0.78, 201}, {{3, 3}, 0.80, 201},
            {{4, 3}, 0.72, 201}, {{5, 3}, 0.75, 201},
            {{2, 4}, 0.77, 201}, {{3, 4}, 0.79, 201},
        };
    const auto core_halo_filter =
        runtime_contract::appearance_filter_emission_surface_halo(
            core_halo_emission);
    const std::vector<runtime_contract::AppearanceEmissionSurfacePoint>
        uniform_weak_emission{
            {{20, 20}, 0.74, 300}, {{21, 20}, 0.76, 300},
            {{22, 20}, 0.78, 300}, {{20, 21}, 0.75, 300},
            {{21, 21}, 0.77, 300}, {{22, 21}, 0.79, 300},
        };
    const auto uniform_weak_filter =
        runtime_contract::appearance_filter_emission_surface_halo(
            uniform_weak_emission);
    const std::vector<runtime_contract::AppearanceEmissionSurfacePoint>
        separate_emission_regions{
            {{30, 30}, 0.74, 400}, {{31, 30}, 0.76, 400},
            {{32, 30}, 0.78, 400}, {{30, 31}, 0.75, 400},
            {{31, 31}, 0.77, 400}, {{32, 31}, 0.79, 400},
            {{50, 50}, 1.14, 500}, {{51, 50}, 1.16, 500},
            {{52, 50}, 1.18, 500}, {{50, 51}, 1.15, 500},
            {{51, 51}, 1.17, 500}, {{52, 51}, 1.19, 500},
        };
    const auto separate_regions_filter =
        runtime_contract::appearance_filter_emission_surface_halo(
            separate_emission_regions);
    if (core_halo_filter.applied_regions != 1 ||
        core_halo_filter.core_samples != 6 ||
        core_halo_filter.kept_samples != 8 ||
        core_halo_filter.halo_rejected_samples != 18 ||
        core_halo_filter.keep.size() != core_halo_emission.size() ||
        !std::all_of(
            core_halo_filter.keep.begin(),
            core_halo_filter.keep.begin() + 8,
            [](bool keep) { return keep; }) ||
        std::any_of(
            core_halo_filter.keep.begin() + 8,
            core_halo_filter.keep.end(),
            [](bool keep) { return keep; }) ||
        uniform_weak_filter.applied_regions != 0 ||
        uniform_weak_filter.kept_samples !=
            static_cast<int>(uniform_weak_emission.size()) ||
        !std::all_of(
            uniform_weak_filter.keep.begin(),
            uniform_weak_filter.keep.end(),
            [](bool keep) { return keep; }) ||
        separate_regions_filter.applied_regions != 0 ||
        separate_regions_filter.kept_samples !=
            static_cast<int>(separate_emission_regions.size()) ||
        !std::all_of(
            separate_regions_filter.keep.begin(),
            separate_regions_filter.keep.end(),
            [](bool keep) { return keep; }))
    {
        return 80;
    }

    const runtime_contract::AppearanceEmissionRoiAcceptance
        emission_roi_acceptance{
            100,
            95,
            9900,
            50,
            true,
            true,
            true,
            0.20,
            0.15,
            0.10,
            0.105};
    if (!runtime_contract::appearance_emission_roi_accepted(
            emission_roi_acceptance) ||
        runtime_contract::appearance_emission_roi_accepted(
            {100, 89, 9900, 50, true, true, true,
             0.20, 0.15, 0.10, 0.105}) ||
        runtime_contract::appearance_emission_roi_accepted(
            {100, 95, 9900, 100, true, true, true,
             0.20, 0.15, 0.10, 0.105}) ||
        runtime_contract::appearance_emission_roi_accepted(
            {100, 95, 9900, 50, true, true, true,
             0.20, 0.19, 0.10, 0.105}) ||
        runtime_contract::appearance_emission_roi_accepted(
            {100, 95, 9900, 50, true, true, true,
             0.20, 0.15, 0.10, 0.111}) ||
        runtime_contract::appearance_emission_roi_accepted(
            {100, 95, 9900, 50, true, false, true,
             0.20, 0.15, 0.10, 0.105}))
    {
        return 76;
    }

    const runtime_contract::AppearanceRgb residual_base_srgb{
        0.5, 0.25, 0.75};
    const runtime_contract::AppearanceRgb residual_expected{
        0.5, 0.0, 0.25};
    const runtime_contract::AppearanceRgb residual_isolated_hdr{
        runtime_contract::appearance_srgb_to_linear(
            residual_base_srgb.r) +
            residual_expected.r,
        runtime_contract::appearance_srgb_to_linear(
            residual_base_srgb.g) +
            residual_expected.g,
        runtime_contract::appearance_srgb_to_linear(
            residual_base_srgb.b) +
            residual_expected.b};
    const auto intrinsic_residual =
        runtime_contract::appearance_intrinsic_emission_residual(
            residual_isolated_hdr,
            residual_base_srgb);
    const auto emission_chromaticity =
        runtime_contract::appearance_emission_chromaticity_albedo(
            {2.0, 1.0, 0.5},
            {0.1, 0.2, 0.3});
    const runtime_contract::AppearanceRgb dark_emitter_base{
        0.02, 0.02, 0.02};
    const runtime_contract::AppearanceRgb warm_emission{
        12.0, 4.0, 0.5};
    const runtime_contract::AppearanceRgb warm_isolated_hdr{
        runtime_contract::appearance_srgb_to_linear(
            dark_emitter_base.r) + warm_emission.r,
        runtime_contract::appearance_srgb_to_linear(
            dark_emitter_base.g) + warm_emission.g,
        runtime_contract::appearance_srgb_to_linear(
            dark_emitter_base.b) + warm_emission.b};
    const auto rescued_emitter =
        runtime_contract::appearance_rescue_emission_color(
            dark_emitter_base,
            warm_isolated_hdr,
            0.01);
    const auto unchanged_non_emitter =
        runtime_contract::appearance_rescue_emission_color(
            dark_emitter_base,
            {runtime_contract::appearance_srgb_to_linear(
                 dark_emitter_base.r) + 0.001,
             runtime_contract::appearance_srgb_to_linear(
                 dark_emitter_base.g) + 0.001,
             runtime_contract::appearance_srgb_to_linear(
                 dark_emitter_base.b) + 0.001},
            0.01);
    if (std::abs(
            intrinsic_residual.r -
            residual_expected.r) >
            0.000001 ||
        std::abs(
            intrinsic_residual.g -
            residual_expected.g) >
            0.000001 ||
        std::abs(
            intrinsic_residual.b -
            residual_expected.b) >
            0.000001 ||
        std::abs(emission_chromaticity.r - 1.0) >
            0.000001 ||
        std::abs(emission_chromaticity.g - 0.5) >
            0.000001 ||
        std::abs(emission_chromaticity.b - 0.25) >
            0.000001 ||
        !rescued_emitter.applied ||
        std::abs(
            std::max({rescued_emitter.albedo_srgb.r,
                      rescued_emitter.albedo_srgb.g,
                      rescued_emitter.albedo_srgb.b}) -
            runtime_contract::AppearanceEmissionRescuePeakSrgb) >
            0.000001 ||
        !(rescued_emitter.albedo_srgb.r >
              rescued_emitter.albedo_srgb.g &&
          rescued_emitter.albedo_srgb.g >
              rescued_emitter.albedo_srgb.b) ||
        unchanged_non_emitter.applied ||
        runtime_contract::appearance_max_channel_delta(
            unchanged_non_emitter.albedo_srgb,
            dark_emitter_base) > 0.000001)
    {
        return 75;
    }

    const auto diffuse_feedback =
        runtime_contract::appearance_closed_loop_correction(
            {{0.25, 0.20, 0.10},
             0.0,
             {0.60, 0.40, 0.10},
             {0.30, 0.20, 0.10},
             false});
    const auto stable_feedback =
        runtime_contract::appearance_closed_loop_correction(
            {{0.25, 0.20, 0.10},
             0.0,
             {0.30, 0.20, 0.10},
             {0.30, 0.20, 0.10},
             false});
    const auto emitter_feedback =
        runtime_contract::appearance_closed_loop_correction(
            {{1.0, 0.60, 0.10},
             0.0,
             {4.0, 2.0, 0.20},
             {0.80, 0.40, 0.10},
             true});
    const auto non_emitter_feedback =
        runtime_contract::appearance_closed_loop_correction(
            {{1.0, 0.60, 0.10},
             0.0,
             {4.0, 2.0, 0.20},
             {0.80, 0.40, 0.10},
             false});
    if (!diffuse_feedback.supported ||
        !(diffuse_feedback.albedo_linear.r > 0.25) ||
        !(diffuse_feedback.albedo_linear.g > 0.20) ||
        std::abs(diffuse_feedback.albedo_linear.b - 0.10) >
            0.000001 ||
        diffuse_feedback.emissive != 0.0 ||
        !stable_feedback.supported ||
        runtime_contract::appearance_max_channel_delta(
            stable_feedback.albedo_linear,
            {0.25, 0.20, 0.10}) > 0.000001 ||
        stable_feedback.emissive != 0.0 ||
        !emitter_feedback.supported ||
        !(emitter_feedback.emissive > 0.0) ||
        non_emitter_feedback.emissive != 0.0)
    {
        return 76;
    }

    const auto albedo_only_feedback =
        runtime_contract::appearance_albedo_closed_loop_correction(
            {{0.20, 0.15, 0.10},
             0.25,
             {0.80, 0.45, 0.20},
             {0.30, 0.20, 0.10},
             true});
    if (!albedo_only_feedback.supported ||
        !(albedo_only_feedback.albedo_linear.r > 0.20) ||
        !(albedo_only_feedback.albedo_linear.g > 0.15) ||
        std::abs(albedo_only_feedback.emissive - 0.25) >
            0.000001)
    {
        return 126;
    }

    runtime_contract::AppearancePhysicalEmissionEvidenceInput
        unstable_speckle{};
    unstable_speckle.source_residual_first =
        {0.020, 0.018, 0.019};
    unstable_speckle.source_residual_second =
        {0.001, 0.024, 0.002};
    unstable_speckle.source_noise_floor_first = 0.010;
    unstable_speckle.source_noise_floor_second = 0.010;
    unstable_speckle.source_hdr = {0.90, 0.70, 0.55};
    unstable_speckle.baseline_hdr = {0.30, 0.25, 0.20};
    unstable_speckle.endpoint_hdr = {1.40, 1.10, 0.85};
    unstable_speckle.manual_emissive_floor = 0.20;
    unstable_speckle.source_distribution_separated = false;
    unstable_speckle.camera_stable = true;
    unstable_speckle.readback_calibrated = true;
    unstable_speckle.packed_b_verified = true;
    const auto rejected_speckle =
        runtime_contract::appearance_physical_emission_evidence(
            unstable_speckle);

    auto stable_but_below_noise = unstable_speckle;
    stable_but_below_noise.source_residual_first =
        {0.020, 0.018, 0.019};
    stable_but_below_noise.source_residual_second =
        {0.019, 0.018, 0.020};
    stable_but_below_noise.source_noise_floor_first = 0.050;
    stable_but_below_noise.source_noise_floor_second = 0.080;
    const auto rejected_below_noise =
        runtime_contract::appearance_physical_emission_evidence(
            stable_but_below_noise);

    runtime_contract::AppearancePhysicalEmissionEvidenceInput
        small_physical_emitter{};
    small_physical_emitter.source_residual_first =
        {0.80, 0.32, 0.08};
    small_physical_emitter.source_residual_second =
        {0.79, 0.33, 0.08};
    small_physical_emitter.source_noise_floor_first = 0.050;
    small_physical_emitter.source_noise_floor_second = 0.080;
    small_physical_emitter.source_hdr = {1.20, 0.52, 0.18};
    small_physical_emitter.baseline_hdr = {0.28, 0.18, 0.09};
    small_physical_emitter.endpoint_hdr = {2.20, 0.92, 0.27};
    small_physical_emitter.manual_emissive_floor = 0.10;
    small_physical_emitter.source_distribution_separated = false;
    small_physical_emitter.camera_stable = true;
    small_physical_emitter.readback_calibrated = true;
    small_physical_emitter.packed_b_verified = true;
    const auto accepted_small_emitter =
        runtime_contract::appearance_physical_emission_evidence(
            small_physical_emitter);
    const auto warm_emission_material =
        runtime_contract::appearance_compose_physical_emission_material(
            {{0.18, 0.16, 0.03},
             small_physical_emitter.source_residual_first,
             small_physical_emitter.source_residual_second,
             small_physical_emitter.manual_emissive_floor,
             accepted_small_emitter.inferred_emissive,
             accepted_small_emitter.accepted});
    const auto rejected_emission_material =
        runtime_contract::appearance_compose_physical_emission_material(
            {{0.18, 0.16, 0.03},
             unstable_speckle.source_residual_first,
             unstable_speckle.source_residual_second,
             unstable_speckle.manual_emissive_floor,
             rejected_speckle.inferred_emissive,
             rejected_speckle.accepted});
    if (rejected_speckle.accepted ||
        rejected_speckle.source_supported ||
        !rejected_speckle.target_response_supported ||
        rejected_below_noise.accepted ||
        rejected_below_noise.source_supported ||
        !rejected_below_noise.target_response_supported ||
        std::abs(rejected_speckle.composed_emissive - 0.20) >
            0.000001 ||
        !accepted_small_emitter.accepted ||
        !accepted_small_emitter.source_supported ||
        !accepted_small_emitter.target_response_supported ||
        !(accepted_small_emitter.composed_emissive > 0.10) ||
        !(accepted_small_emitter.candidate_loss <
          accepted_small_emitter.baseline_loss) ||
        !warm_emission_material.chromaticity_carrier_applied ||
        !(warm_emission_material.albedo.r >
          warm_emission_material.albedo.g * 2.0) ||
        !(warm_emission_material.albedo.g >
          warm_emission_material.albedo.b * 2.0) ||
        std::abs(
            warm_emission_material.emissive -
            accepted_small_emitter.composed_emissive) >
            0.000001 ||
        rejected_emission_material.chromaticity_carrier_applied ||
        std::abs(rejected_emission_material.albedo.r - 0.18) >
            0.000001 ||
        std::abs(rejected_emission_material.albedo.g - 0.16) >
            0.000001 ||
        std::abs(rejected_emission_material.albedo.b - 0.03) >
            0.000001 ||
        std::abs(
            runtime_contract::appearance_compose_physical_emissive(
                0.65,
                0.25) -
            0.65) >
            0.000001)
    {
        return 127;
    }

    runtime_contract::AppearancePhysicalEmissionComponentValidationInput
        one_sample_component{};
    one_sample_component.paired_samples = 1;
    one_sample_component.baseline_loss = 0.10;
    one_sample_component.candidate_loss = 0.08;
    one_sample_component.non_emission_paired_samples = 100;
    one_sample_component.baseline_non_emission_loss = 0.050;
    one_sample_component.candidate_non_emission_loss = 0.055;
    one_sample_component.dual_evidence_prevalidated = true;
    one_sample_component.camera_stable = true;
    one_sample_component.readback_calibrated = true;
    one_sample_component.packed_b_verified = true;
    one_sample_component.painted_emissive_nonzero_pixels = 1;
    const auto accepted_one_sample_component =
        runtime_contract::
            appearance_validate_physical_emission_component(
                one_sample_component);
    auto weak_component = one_sample_component;
    weak_component.candidate_loss = 0.09;
    const auto rejected_weak_component =
        runtime_contract::
            appearance_validate_physical_emission_component(
                weak_component);
    const auto misleading_aggregate_improvement =
        (1.10 - 0.59) / 1.10;
    if (!accepted_one_sample_component.accepted ||
        accepted_one_sample_component.rejection !=
            runtime_contract::
                AppearancePhysicalEmissionComponentRejection::None ||
        rejected_weak_component.accepted ||
        misleading_aggregate_improvement <
            runtime_contract::AppearanceFitMinimumImprovement ||
        rejected_weak_component.rejection !=
            runtime_contract::
                AppearancePhysicalEmissionComponentRejection::
                    RoiImprovementBelowThreshold)
    {
        return 130;
    }

    runtime_contract::AppearanceCorrectionFieldInput
        two_boundary_field{};
    two_boundary_field.vertex_count = 3;
    two_boundary_field.edges = {{0, 1}, {1, 2}};
    two_boundary_field.side_vertices = {false, true, false};
    two_boundary_field.anchors = {
        {0,
         {0.0, 0.0, 0.0},
         1.0,
         runtime_contract::AppearanceCorrectionBoundary::Front},
        {2,
         {std::log(4.0), std::log(4.0), std::log(4.0)},
         1.0,
         runtime_contract::AppearanceCorrectionBoundary::Back}};
    const auto interpolated_field =
        runtime_contract::appearance_solve_correction_field(
            two_boundary_field);
    auto reversed_boundary_field = two_boundary_field;
    std::reverse(
        reversed_boundary_field.anchors.begin(),
        reversed_boundary_field.anchors.end());
    const auto reversed_field =
        runtime_contract::appearance_solve_correction_field(
            reversed_boundary_field);

    auto one_boundary_field = two_boundary_field;
    one_boundary_field.anchors.resize(1);
    const auto extended_field =
        runtime_contract::appearance_solve_correction_field(
            one_boundary_field);
    auto unanchored_field = one_boundary_field;
    unanchored_field.anchors.clear();
    const auto rejected_field =
        runtime_contract::appearance_solve_correction_field(
            unanchored_field);
    if (!interpolated_field.ok ||
        interpolated_field.values.size() != 3 ||
        std::abs(
            interpolated_field.values[1].r -
            std::log(2.0)) >
            0.0001 ||
        interpolated_field.front_anchor_vertices != 1 ||
        interpolated_field.back_anchor_vertices != 1 ||
        interpolated_field.side_components != 1 ||
        interpolated_field.one_boundary_side_components != 0 ||
        interpolated_field.hash != reversed_field.hash ||
        !extended_field.ok ||
        extended_field.one_boundary_side_components != 1 ||
        std::abs(extended_field.values[1].r) > 0.000001 ||
        rejected_field.ok ||
        rejected_field.unanchored_side_components != 1 ||
        rejected_field.failure !=
            runtime_contract::AppearanceCorrectionFieldFailure::
                SideUnanchored)
    {
        return 128;
    }

    struct CorrectionReplayVariation
    {
        double brush_size{1.0};
        bool front{false};
        bool side{false};
        bool back{false};
    };
    const std::array<CorrectionReplayVariation, 8>
        correction_replay_variations{{
            {1.0, true, false, false},
            {1.0, false, true, false},
            {1.0, false, false, true},
            {1.0, true, true, true},
            {4.0, true, false, false},
            {4.0, false, true, false},
            {4.0, false, false, true},
            {4.0, true, true, true},
        }};
    for (const auto& variation : correction_replay_variations)
    {
        const auto field =
            runtime_contract::appearance_solve_correction_field(
                two_boundary_field);
        const std::vector<runtime_contract::ReplayCandidate>
            replay_candidates{
                {0,
                 runtime_contract::ReplayRegion::Front,
                 variation.front
                     ? runtime_contract::ReplayRegionMode::Paint
                     : runtime_contract::ReplayRegionMode::Skip,
                 0,
                 0.1,
                 0.1,
                 true,
                 1.0,
                 1.0,
                 0.0,
                 0},
                {1,
                 runtime_contract::ReplayRegion::Side,
                 variation.side
                     ? runtime_contract::ReplayRegionMode::Paint
                     : runtime_contract::ReplayRegionMode::Skip,
                 0,
                 0.5,
                 0.5,
                 true,
                 0.5,
                 0.5,
                 0.0,
                 1},
                {2,
                 runtime_contract::ReplayRegion::Back,
                 variation.back
                     ? runtime_contract::ReplayRegionMode::Paint
                     : runtime_contract::ReplayRegionMode::Skip,
                 0,
                 0.9,
                 0.9,
                 true,
                 0.0,
                 0.0,
                 0.0,
                 2}};
        const auto replay =
            runtime_contract::build_single_brush_replay_plan(
                replay_candidates,
                1024,
                variation.brush_size,
                8.0);
        const auto expected_paint_count =
            static_cast<std::size_t>(
                (variation.front ? 1 : 0) +
                (variation.side ? 1 : 0) +
                (variation.back ? 1 : 0));
        if (!field.ok ||
            field.hash != interpolated_field.hash ||
            field.values.size() != interpolated_field.values.size() ||
            std::abs(
                field.values[1].r -
                interpolated_field.values[1].r) >
                0.000001 ||
            replay.fill_count != 0 ||
            replay.paint_count != expected_paint_count)
        {
            return 131;
        }
    }

    if (runtime_contract::production_material_stroke_count(3) != 3 ||
        runtime_contract::production_material_sample_index(0) != 0 ||
        runtime_contract::production_material_sample_index(1) != 1 ||
        runtime_contract::production_material_sample_index(2) != 2)
    {
        return 23;
    }

    const auto projected_environment =
        runtime_contract::environment_projected_capture_coordinate(
            {0.25, 0.75});
    const auto same_projected_back =
        runtime_contract::environment_projected_capture_coordinate(
            {0.25, 0.75});
    const auto projected_top_left =
        runtime_contract::environment_projected_capture_coordinate(
            {0.0, 0.0});
    const auto projected_bottom_right =
        runtime_contract::environment_projected_capture_coordinate(
            {1.0, 1.0});
    const auto projected_outside =
        runtime_contract::environment_projected_capture_coordinate(
            {-0.01, 0.50});
    if (!projected_environment.ok ||
        !same_projected_back.ok ||
        !projected_top_left.ok ||
        !projected_bottom_right.ok ||
        projected_outside.ok ||
        std::abs(projected_environment.capture_u - 0.25) > 0.000001 ||
        std::abs(projected_environment.capture_v - 0.75) > 0.000001 ||
        std::abs(projected_environment.capture_u -
                 same_projected_back.capture_u) > 0.000001 ||
        std::abs(projected_environment.capture_v -
                 same_projected_back.capture_v) > 0.000001)
    {
        return 125;
    }

    if (runtime_contract::esp_capture_status(
            false, false, 0, false) !=
            runtime_contract::EspCaptureStatus::Disabled ||
        runtime_contract::esp_capture_status(
            true, false, 500, false) !=
            runtime_contract::EspCaptureStatus::Waiting ||
        runtime_contract::esp_capture_status(
            true, true, 16, false) !=
            runtime_contract::EspCaptureStatus::Active ||
        runtime_contract::esp_capture_status(
            true, true, 1500, true) !=
            runtime_contract::EspCaptureStatus::Busy ||
        runtime_contract::esp_capture_status(
            true, true, 1500, false) !=
            runtime_contract::EspCaptureStatus::Stalled)
    {
        return 117;
    }
    if (runtime_contract::ProductionMaterialPaintChannels !=
        std::array<std::uint8_t, 1>{
            static_cast<std::uint8_t>(sdk::EPaintChannel::AlbedoMetallicRoughnessEmissive)})
    {
        return 26;
    }
    return 0;
}
