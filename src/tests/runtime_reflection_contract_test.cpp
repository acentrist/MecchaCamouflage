#include <meccha/runtime/canvas_call_codec.hpp>
#include <meccha/runtime/input_control_codec.hpp>
#include <meccha/runtime/reflection_contract.hpp>
#include <meccha/runtime/paint_call_codec.hpp>
#include <meccha/runtime/paint_capture_codec.hpp>
#include <meccha/runtime/paint_preview_codec.hpp>
#include <meccha/runtime/paint_queue_codec.hpp>
#include <meccha/runtime/texture_import_codec.hpp>
#include <meccha/runtime/unreal_contracts.hpp>

#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::runtime;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL runtime_reflection_contract: "
                  << message << '\n';
    }
    return condition;
}

auto paint_contract() -> ReflectionRecordDescriptor
{
    return paint_at_uv_with_brush_contract();
}

auto changed_property(
    ReflectionRecordDescriptor record,
    std::size_t index,
    ReflectionPropertyDescriptor property)
    -> ReflectionRecordDescriptor
{
    record.properties[index] = std::move(property);
    return record;
}

auto has_error(
    const ReflectionRecordDescriptor& actual,
    const ReflectionRecordDescriptor& expected,
    ReflectionContractErrorCode code,
    std::string_view property = {}) -> bool
{
    const auto result =
        validate_reflection_contract(actual, expected);
    return !result && result.error().code == code &&
           result.error().property == property;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::runtime;

    auto passed = true;
    const auto expected = paint_contract();

    passed &= expect(
        validate_reflection_contract(expected, expected).has_value(),
        "the exact Paint contract was rejected");
    const auto initialize = initialize_paint_contract();
    passed &= expect(
        initialize.owner_name ==
                "/Script/PenguinHotel.RuntimePaintableComponent" &&
            initialize.size == 0x10U &&
            initialize.properties ==
                std::vector<ReflectionPropertyDescriptor>{
                    ReflectionPropertyDescriptor{
                        "MeshComponent",
                        ReflectionPropertyKind::Object,
                        "MeshComponent",
                        0x00U,
                        0x08U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "ReturnValue",
                        ReflectionPropertyKind::Bool,
                        {},
                        0x08U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::ReturnValue,
                    },
                } &&
            validate_reflection_contract(initialize, initialize)
                .has_value(),
        "the exact InitializePaint contract drifted");
    auto* const mesh_component =
        reinterpret_cast<void*>(std::uintptr_t{0x1234U});
    const auto initialize_parameters =
        encode_initialize_paint(mesh_component);
    passed &= expect(
        initialize_parameters.has_value() &&
            initialize_parameters->mesh_component ==
                mesh_component &&
            !initialize_parameters->return_value &&
            sizeof(InitializePaintParameters) == 0x10U &&
            offsetof(
                InitializePaintParameters,
                return_value) == 0x08U &&
            !encode_initialize_paint(nullptr).has_value(),
        "the typed InitializePaint parameter encoder drifted");
    const auto is_initialized = is_paint_initialized_contract();
    const auto initialized_mesh =
        get_initialized_paint_mesh_contract();
    passed &= expect(
        is_initialized.size == 0x01U &&
            is_initialized.properties ==
                std::vector<ReflectionPropertyDescriptor>{
                    ReflectionPropertyDescriptor{
                        "ReturnValue",
                        ReflectionPropertyKind::Bool,
                        {},
                        0x00U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::ReturnValue,
                    },
                } &&
            initialized_mesh.size == 0x08U &&
            initialized_mesh.properties ==
                std::vector<ReflectionPropertyDescriptor>{
                    ReflectionPropertyDescriptor{
                        "ReturnValue",
                        ReflectionPropertyKind::Object,
                        "MeshComponent",
                        0x00U,
                        0x08U,
                        1U,
                        ReflectionPropertyDirection::ReturnValue,
                    },
                } &&
            sizeof(IsPaintInitializedParameters) == 0x01U &&
            sizeof(GetInitializedPaintMeshParameters) == 0x08U,
        "the exact paint initialization query contracts drifted");
    const auto socket_transform = get_socket_transform_contract();
    passed &= expect(
        socket_transform.owner_name ==
                "/Script/Engine.SceneComponent" &&
            socket_transform.size == 0x70U &&
            socket_transform.properties ==
                std::vector<ReflectionPropertyDescriptor>{
                    ReflectionPropertyDescriptor{
                        "InSocketName",
                        ReflectionPropertyKind::Name,
                        {},
                        0x00U,
                        0x08U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "TransformSpace",
                        ReflectionPropertyKind::Enum,
                        "ERelativeTransformSpace",
                        0x08U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "ReturnValue",
                        ReflectionPropertyKind::Struct,
                        "Transform",
                        0x10U,
                        0x60U,
                        1U,
                        ReflectionPropertyDirection::ReturnValue,
                    },
                } &&
            sizeof(RuntimeTransform) == 0x60U &&
            sizeof(GetSocketTransformParameters) == 0x70U &&
            offsetof(
                GetSocketTransformParameters,
                return_value) == 0x10U,
        "the exact world-space socket transform contract drifted");
    auto runtime_transform = RuntimeTransform{};
    runtime_transform.rotation =
        RuntimeQuaternion{0.0, 0.0, 0.0, 2.0};
    runtime_transform.translation =
        RuntimeVector3d{1.0, 2.0, 3.0, 0.0};
    runtime_transform.scale =
        RuntimeVector3d{1.0, 2.0, 3.0, 0.0};
    const auto decoded_transform =
        decode_runtime_transform(runtime_transform);
    runtime_transform.scale.x = 0.0;
    passed &= expect(
        decoded_transform &&
            decoded_transform->translation ==
                core::Vector3d{1.0, 2.0, 3.0} &&
            decoded_transform->rotation ==
                core::PaintQuaternion{0.0, 0.0, 0.0, 1.0} &&
            decoded_transform->scale ==
                core::Vector3d{1.0, 2.0, 3.0} &&
            !decode_runtime_transform(runtime_transform)
                 .has_value(),
        "world-space socket transform decoding did not fail closed");
    passed &= expect(
        expected.owner_name ==
                "/Script/PenguinHotel.RuntimePaintableComponent" &&
            expected.size == 0x68U &&
            vector2d_contract().size == 0x10U &&
            linear_color_contract().size == 0x10U &&
            paint_channel_data_contract().size == 0x24U &&
            runtime_brush_settings_contract().size == 0x28U,
        "the reviewed game-owned Paint contract sizes drifted");
    passed &= expect(
        recorded_stroke_count_contract().owner_name ==
                "/Script/PenguinHotel.RuntimePaintableComponent" &&
            recorded_stroke_count_contract().size == 0x04U &&
            queued_stroke_count_contract().owner_name ==
                "/Script/PenguinHotel.RuntimePaintReplicationManager" &&
            queued_stroke_count_contract().size == 0x04U &&
            queued_stroke_count_for_component_contract().size ==
                0x10U &&
            runtime_paint_replication_pressure_contract().size ==
                0x10U &&
            replication_pressure_contract().size == 0x10U,
        "the reviewed queue-observation contract sizes drifted");
    passed &= expect(
        validate_reflection_contract(
            recorded_stroke_count_contract(),
            recorded_stroke_count_contract()).has_value() &&
            validate_reflection_contract(
                queued_stroke_count_contract(),
                queued_stroke_count_contract()).has_value() &&
            validate_reflection_contract(
                queued_stroke_count_for_component_contract(),
                queued_stroke_count_for_component_contract())
                .has_value() &&
            validate_reflection_contract(
                runtime_paint_replication_pressure_contract(),
                runtime_paint_replication_pressure_contract())
                .has_value() &&
            validate_reflection_contract(
                replication_pressure_contract(),
            replication_pressure_contract()).has_value(),
        "an exact queue-observation contract was rejected");
    passed &= expect(
        export_channel_to_bytes_contract().size == 0x20U &&
            import_channel_from_bytes_contract().size == 0x20U &&
            validate_reflection_contract(
                export_channel_to_bytes_contract(),
                export_channel_to_bytes_contract()).has_value() &&
            validate_reflection_contract(
                import_channel_from_bytes_contract(),
                import_channel_from_bytes_contract()).has_value(),
        "the reviewed preview channel contracts drifted");
    passed &= expect(
        k2_draw_line_contract().owner_name ==
                "/Script/Engine.Canvas" &&
            k2_draw_line_contract().size == 0x38U &&
            validate_reflection_contract(
                k2_draw_line_contract(),
                k2_draw_line_contract()).has_value(),
        "the reviewed UCanvas line contract drifted");
    const auto canvas_line = CanvasLineInput{
        {12.5, 24.0},
        {96.0, 128.5},
        {128U, 64U, 255U, 127U},
        2.5,
    };
    const auto encoded_line = encode_canvas_line(canvas_line);
    passed &= expect(
        encoded_line &&
            encoded_line->screen_position_a.x == 12.5 &&
            encoded_line->screen_position_a.y == 24.0 &&
            encoded_line->screen_position_b.x == 96.0 &&
            encoded_line->screen_position_b.y == 128.5 &&
            encoded_line->thickness == 2.5F &&
            encoded_line->render_color.alpha ==
                (127.0F / 255.0F),
        "a validated Canvas line did not encode to the UE5.6 ABI");
    passed &= expect(
        k2_draw_texture_contract().owner_name ==
                "/Script/Engine.Canvas" &&
            k2_draw_texture_contract().size == 0x70U &&
            validate_reflection_contract(
                k2_draw_texture_contract(),
                k2_draw_texture_contract()).has_value(),
        "the reviewed UCanvas texture contract drifted");
    const auto encoded_box = encode_canvas_filled_box(
        CanvasBoxInput{
            {40.0, 60.0, 320.0, 180.0},
            {16U, 32U, 64U, 192U},
        });
    passed &= expect(
        encoded_box &&
            encoded_box->render_texture == nullptr &&
            encoded_box->screen_position.x == 40.0 &&
            encoded_box->screen_position.y == 60.0 &&
            encoded_box->screen_size.x == 320.0 &&
            encoded_box->screen_size.y == 180.0 &&
            encoded_box->coordinate_position.x == 0.0 &&
            encoded_box->coordinate_position.y == 0.0 &&
            encoded_box->coordinate_size.x == 1.0 &&
            encoded_box->coordinate_size.y == 1.0 &&
            encoded_box->blend_mode ==
                CanvasBlendMode::Translucent &&
            encoded_box->rotation == 0.0F &&
            encoded_box->pivot_point.x == 0.5 &&
            encoded_box->pivot_point.y == 0.5 &&
            encoded_box->render_color.alpha ==
                (192.0F / 255.0F),
        "a filled Canvas box did not encode as a white-texture tile");
    const auto texture_identity =
        reinterpret_cast<void*>(0x1234U);
    const auto encoded_texture = encode_canvas_texture(
        CanvasTextureInput{
            texture_identity,
            {100.0, 200.0, 400.0, 300.0},
            {0.25, 0.125, 0.75, 0.875},
            {255U, 255U, 255U, 224U},
        });
    passed &= expect(
        encoded_texture &&
            encoded_texture->render_texture ==
                texture_identity &&
            encoded_texture->screen_position.x == 100.0 &&
            encoded_texture->screen_position.y == 200.0 &&
            encoded_texture->screen_size.x == 400.0 &&
            encoded_texture->screen_size.y == 300.0 &&
            encoded_texture->coordinate_position.x == 0.25 &&
            encoded_texture->coordinate_position.y == 0.125 &&
            encoded_texture->coordinate_size.x == 0.5 &&
            encoded_texture->coordinate_size.y == 0.75 &&
            encoded_texture->render_color.alpha ==
                (224.0F / 255.0F),
        "a Canvas texture did not preserve its clipped UV range");
    passed &= expect(
        k2_draw_text_contract().owner_name ==
                "/Script/Engine.Canvas" &&
            k2_draw_text_contract().size == 0x88U &&
            validate_reflection_contract(
                k2_draw_text_contract(),
                k2_draw_text_contract()).has_value(),
        "the reviewed UCanvas text contract drifted");
    passed &= expect(
        import_buffer_as_texture2d_contract().owner_name ==
                "/Script/Engine.KismetRenderingLibrary" &&
            import_buffer_as_texture2d_contract().size == 0x20U &&
            validate_reflection_contract(
                import_buffer_as_texture2d_contract(),
                import_buffer_as_texture2d_contract())
                .has_value(),
        "the reviewed texture-import contract drifted");
    const auto create_capture_target =
        create_render_target_2d_contract();
    passed &= expect(
        create_capture_target ==
            ReflectionRecordDescriptor{
                "CreateRenderTarget2D",
                "/Script/Engine.KismetRenderingLibrary",
                0x38U,
                {
                    ReflectionPropertyDescriptor{
                        "WorldContextObject",
                        ReflectionPropertyKind::Object,
                        "Object",
                        0x00U,
                        0x08U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "Width",
                        ReflectionPropertyKind::Int32,
                        {},
                        0x08U,
                        0x04U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "Height",
                        ReflectionPropertyKind::Int32,
                        {},
                        0x0CU,
                        0x04U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "Slices",
                        ReflectionPropertyKind::Int32,
                        {},
                        0x10U,
                        0x04U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "Format",
                        ReflectionPropertyKind::Enum,
                        "ETextureRenderTargetFormat",
                        0x14U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "ClearColor",
                        ReflectionPropertyKind::Struct,
                        "LinearColor",
                        0x18U,
                        0x10U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "bAutoGenerateMipMaps",
                        ReflectionPropertyKind::Bool,
                        {},
                        0x28U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "bSupportUAVs",
                        ReflectionPropertyKind::Bool,
                        {},
                        0x29U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "ReturnValue",
                        ReflectionPropertyKind::Object,
                        "TextureRenderTarget2D",
                        0x30U,
                        0x08U,
                        1U,
                        ReflectionPropertyDirection::ReturnValue,
                    },
                },
            } &&
            validate_reflection_contract(
                create_capture_target,
                create_capture_target)
                .has_value(),
        "the current CreateRenderTarget2D contract drifted");
    const auto read_capture_target =
        read_render_target_raw_contract();
    passed &= expect(
        read_capture_target ==
            ReflectionRecordDescriptor{
                "ReadRenderTargetRaw",
                "/Script/Engine.KismetRenderingLibrary",
                0x28U,
                {
                    ReflectionPropertyDescriptor{
                        "WorldContextObject",
                        ReflectionPropertyKind::Object,
                        "Object",
                        0x00U,
                        0x08U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "TextureRenderTarget",
                        ReflectionPropertyKind::Object,
                        "TextureRenderTarget2D",
                        0x08U,
                        0x08U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "OutLinearSamples",
                        ReflectionPropertyKind::Array,
                        "LinearColor",
                        0x10U,
                        0x10U,
                        1U,
                        ReflectionPropertyDirection::Output,
                    },
                    ReflectionPropertyDescriptor{
                        "bNormalize",
                        ReflectionPropertyKind::Bool,
                        {},
                        0x20U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::Input,
                    },
                    ReflectionPropertyDescriptor{
                        "ReturnValue",
                        ReflectionPropertyKind::Bool,
                        {},
                        0x21U,
                        0x01U,
                        1U,
                        ReflectionPropertyDirection::ReturnValue,
                    },
                },
            } &&
            validate_reflection_contract(
                read_capture_target,
                read_capture_target)
                .has_value(),
        "the current ReadRenderTargetRaw contract drifted");
    auto* const capture_world =
        reinterpret_cast<void*>(std::uintptr_t{0x1234U});
    auto* const render_target =
        reinterpret_cast<void*>(std::uintptr_t{0x5678U});
    const auto encoded_capture_target =
        encode_create_paint_capture_render_target(
            PaintCaptureRenderTargetInput{
                capture_world,
                1920U,
                1080U,
                PaintCaptureRenderTargetFormat::Rgba16Float,
            });
    const auto encoded_capture_read =
        encode_read_paint_capture_render_target(
            capture_world,
            render_target,
            false);
    passed &= expect(
        encoded_capture_target &&
            encoded_capture_target->world_context_object ==
                capture_world &&
            encoded_capture_target->width == 1920 &&
            encoded_capture_target->height == 1080 &&
            encoded_capture_target->slices == 1 &&
            encoded_capture_target->format ==
                PaintCaptureRenderTargetFormat::Rgba16Float &&
            encoded_capture_target->clear_color.alpha == 1.0F &&
            !encoded_capture_target->auto_generate_mip_maps &&
            !encoded_capture_target->support_uavs &&
            encoded_capture_target->return_value == nullptr &&
            encoded_capture_read &&
            encoded_capture_read->world_context_object ==
                capture_world &&
            encoded_capture_read->texture_render_target ==
                render_target &&
            !encoded_capture_read->normalize &&
            !encoded_capture_read->return_value &&
            sizeof(CreatePaintCaptureRenderTargetParameters) ==
                0x38U &&
            offsetof(
                CreatePaintCaptureRenderTargetParameters,
                return_value) == 0x30U &&
            sizeof(ReadPaintCaptureRenderTargetParameters) ==
                0x28U,
        "the typed SceneCapture render-target codecs drifted");
    passed &= expect(
        !encode_create_paint_capture_render_target(
             PaintCaptureRenderTargetInput{
                 nullptr,
                 1920U,
                 1080U,
                 PaintCaptureRenderTargetFormat::Rgba8Srgb,
             }) &&
            !encode_create_paint_capture_render_target(
                PaintCaptureRenderTargetInput{
                    capture_world,
                    core::MaximumPaintCaptureDimension + 1U,
                    1080U,
                    PaintCaptureRenderTargetFormat::Rgba8Srgb,
                }) &&
            !encode_read_paint_capture_render_target(
                nullptr,
                render_target,
                false) &&
            !encode_read_paint_capture_render_target(
                capture_world,
                nullptr,
                false),
        "invalid SceneCapture render-target input was accepted");
    auto capture_pixels = std::array{
        PaintCaptureLinearColor{0.25F, 0.5F, 0.75F, 1.0F},
        PaintCaptureLinearColor{2.0F, 1.5F, 1.0F, 1.0F},
    };
    auto captured_readback = *encoded_capture_read;
    captured_readback.out_linear_samples =
        PaintCaptureLinearColorArray{
            capture_pixels.data(),
            2,
            2,
        };
    captured_readback.return_value = true;
    const auto decoded_capture =
        decode_paint_capture_linear_colors(
            captured_readback,
            2U,
            1U);
    capture_pixels[1].red =
        std::numeric_limits<float>::infinity();
    passed &= expect(
        decoded_capture &&
            decoded_capture->size() == 2U &&
            (*decoded_capture)[0].green == 0.5F &&
            (*decoded_capture)[1].red == 2.0F &&
            !decode_paint_capture_linear_colors(
                 captured_readback,
                 2U,
                 1U) &&
            !decode_paint_capture_linear_colors(
                 captured_readback,
                 1U,
                 1U),
        "SceneCapture linear readback validation drifted");
    const auto capture_scene = capture_scene_contract();
    const auto hide_component = hide_component_contract();
    const auto destroy_capture_actor = k2_destroy_actor_contract();
    passed &= expect(
        capture_scene ==
                ReflectionRecordDescriptor{
                    "CaptureScene",
                    "/Script/Engine.SceneCaptureComponent2D",
                    0x00U,
                    {},
                } &&
            hide_component ==
                ReflectionRecordDescriptor{
                    "HideComponent",
                    "/Script/Engine.SceneCaptureComponent",
                    0x08U,
                    {
                        ReflectionPropertyDescriptor{
                            "InComponent",
                            ReflectionPropertyKind::Object,
                            "PrimitiveComponent",
                            0x00U,
                            0x08U,
                            1U,
                            ReflectionPropertyDirection::Input,
                        },
                    },
                } &&
            destroy_capture_actor ==
                ReflectionRecordDescriptor{
                    "K2_DestroyActor",
                    "/Script/Engine.Actor",
                    0x00U,
                    {},
                } &&
            validate_reflection_contract(
                capture_scene,
                capture_scene).has_value() &&
            validate_reflection_contract(
                hide_component,
                hide_component).has_value() &&
            validate_reflection_contract(
                destroy_capture_actor,
                destroy_capture_actor).has_value(),
        "the exact SceneCapture operation contracts drifted");

    const auto brush_plane =
        paint_brush_plane_visual_contract();
    passed &= expect(
        brush_plane.actor_class_path ==
                "/Game/BluePrints/cLeon/BP_BrushPlane."
                "BP_BrushPlane_C" &&
            brush_plane.components ==
                std::array{
                    PaintBrushPlaneVisualComponent{
                        "Plane",
                        PaintBrushPlaneVisualKind::StaticMesh,
                    },
                    PaintBrushPlaneVisualComponent{
                        "Plane1",
                        PaintBrushPlaneVisualKind::StaticMesh,
                    },
                    PaintBrushPlaneVisualComponent{
                        "Niagara",
                        PaintBrushPlaneVisualKind::Niagara,
                    },
                },
        "the exact cooked brush-plane visual contract drifted");

    const auto unlit_capture_plan =
        build_paint_scene_capture_plan(core::PaintSettings{});
    auto lit_settings = core::PaintSettings{};
    lit_settings.include_scene_lighting = true;
    const auto lit_capture_plan =
        build_paint_scene_capture_plan(lit_settings);
    auto automatic_settings = lit_settings;
    automatic_settings.auto_material = true;
    const auto automatic_capture_plan =
        build_paint_scene_capture_plan(automatic_settings);
    auto invalid_capture_settings = core::PaintSettings{};
    invalid_capture_settings.brush_size_texels =
        std::numeric_limits<double>::quiet_NaN();
    passed &= expect(
        static_cast<std::uint8_t>(
            PaintSceneCaptureSource::BaseColor) == 7U &&
            static_cast<std::uint8_t>(
                PaintSceneCaptureSource::FinalColorHdr) == 8U &&
            static_cast<std::uint8_t>(
                PaintSceneCaptureSource::FinalToneCurveHdr) ==
                9U &&
            static_cast<std::uint8_t>(
                PaintSceneCaptureProjection::Perspective) ==
                0U &&
        unlit_capture_plan &&
            unlit_capture_plan->passes ==
                std::vector<PaintSceneCapturePass>{
                    PaintSceneCapturePass{
                        PaintSceneCapturePassKind::BaseColor,
                        PaintSceneCaptureSource::BaseColor,
                        PaintCaptureRenderTargetFormat::Rgba8Srgb,
                        PaintSceneCaptureProfile::Standard,
                        false,
                        false,
                    },
                } &&
            !unlit_capture_plan->requires_preview_feedback &&
            lit_capture_plan &&
            lit_capture_plan->passes.size() == 2U &&
            lit_capture_plan->passes[1] ==
                PaintSceneCapturePass{
                    PaintSceneCapturePassKind::FinalColorHdr,
                    PaintSceneCaptureSource::FinalColorHdr,
                    PaintCaptureRenderTargetFormat::Rgba16Float,
                    PaintSceneCaptureProfile::Standard,
                    false,
                    true,
                } &&
            automatic_capture_plan &&
            automatic_capture_plan->passes.size() == 7U &&
            automatic_capture_plan->requires_preview_feedback &&
            automatic_capture_plan->passes[2].kind ==
                PaintSceneCapturePassKind::IntrinsicEmissionHdr &&
            automatic_capture_plan->passes[2].profile ==
                PaintSceneCaptureProfile::IntrinsicEmission &&
            automatic_capture_plan->passes[4].source ==
                PaintSceneCaptureSource::Normal &&
            automatic_capture_plan->passes[5].source ==
                PaintSceneCaptureSource::SceneDepth &&
            automatic_capture_plan->passes[6].source ==
                PaintSceneCaptureSource::FinalColorLdr &&
            build_paint_scene_capture_plan(
                invalid_capture_settings) ==
                std::unexpected(
                    PaintCaptureEncodingError::
                        InvalidSettings),
        "the bounded Paint SceneCapture pass plan drifted");

    const auto capture_camera = encode_paint_scene_capture_camera(
        core::EspView{
            {100.0, -25.0, 50.0},
            10.0,
            20.0,
            0.0,
            90.0,
            16.0 / 9.0,
            core::EspAspectConstraint::MaintainXFov,
            1.0,
            1.0,
        },
        1920U,
        1080U);
    passed &= expect(
        capture_camera &&
            capture_camera->location ==
                EspVector3dAbi{100.0, -25.0, 50.0} &&
            capture_camera->rotation ==
                EspRotatorAbi{10.0, 20.0, 0.0} &&
            capture_camera->field_of_view_degrees == 90.0F &&
            capture_camera->width == 1920U &&
            capture_camera->height == 1080U &&
            !encode_paint_scene_capture_camera(
                core::EspView{},
                1024U,
                1024U),
        "the Paint SceneCapture camera codec drifted");

    const auto srgb_pixels =
        convert_paint_capture_linear_colors_to_srgb8(
            std::array{
                PaintCaptureLinearColor{
                    0.0F,
                    0.0031308F,
                    1.0F,
                    1.0F,
                },
                PaintCaptureLinearColor{
                    0.21404114F,
                    2.0F,
                    -1.0F,
                    1.0F,
                },
            });
    passed &= expect(
        srgb_pixels &&
            *srgb_pixels ==
                std::vector<core::Rgb8>{
                    core::Rgb8{0U, 10U, 255U},
                    core::Rgb8{128U, 255U, 0U},
                } &&
            !convert_paint_capture_linear_colors_to_srgb8(
                std::array{
                    PaintCaptureLinearColor{
                        std::numeric_limits<float>::quiet_NaN(),
                        0.0F,
                        0.0F,
                        1.0F,
                    },
                }),
        "the Paint SceneCapture linear-to-sRGB conversion drifted");
    passed &= expect(
        is_look_input_ignored_contract().owner_name ==
                "/Script/Engine.Controller" &&
            is_look_input_ignored_contract().size == 0x01U &&
            is_move_input_ignored_contract().size == 0x01U &&
            set_ignore_look_input_contract().size == 0x01U &&
            set_ignore_move_input_contract().size == 0x01U &&
            validate_reflection_contract(
                is_look_input_ignored_contract(),
                is_look_input_ignored_contract()).has_value() &&
            validate_reflection_contract(
                is_move_input_ignored_contract(),
                is_move_input_ignored_contract()).has_value() &&
            validate_reflection_contract(
                set_ignore_look_input_contract(),
                set_ignore_look_input_contract()).has_value() &&
            validate_reflection_contract(
                set_ignore_move_input_contract(),
                set_ignore_move_input_contract()).has_value(),
        "the reviewed input-control contracts drifted");
    const auto ignored_input = encode_ignore_input(true);
    passed &= expect(
        ignored_input.new_input &&
            !IgnoreInputQueryParametersAbi{}.return_value,
        "the input-control bool ABI did not preserve its value");
    const auto import_world =
        reinterpret_cast<void*>(0x1000U);
    const auto import_bytes = std::array{
        std::byte{0x89},
        std::byte{'P'},
        std::byte{'N'},
        std::byte{'G'},
    };
    const auto encoded_texture_import = encode_texture_import(
        import_world,
        import_bytes);
    passed &= expect(
        encoded_texture_import &&
            encoded_texture_import->world_context_object ==
                import_world &&
            encoded_texture_import->buffer.data ==
                import_bytes.data() &&
            encoded_texture_import->buffer.count == 4 &&
            encoded_texture_import->buffer.capacity == 4 &&
            encoded_texture_import->return_value == nullptr,
        "a texture import did not encode to the UE5.6 ABI");
    passed &= expect(
        encode_texture_import(nullptr, import_bytes) ==
            std::unexpected(
                TextureImportCodecError::InvalidWorld) &&
            encode_texture_import(import_world, {}) ==
                std::unexpected(
                    TextureImportCodecError::InvalidBuffer),
        "an invalid texture import ABI was accepted");
    const auto localized_units = encode_canvas_utf16(
        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
        "\xF0\x9F\xA6\x8E");
    passed &= expect(
        localized_units ==
            std::vector<char16_t>{
                u'\u65E5',
                u'\u672C',
                u'\u8A9E',
                static_cast<char16_t>(0xD83EU),
                static_cast<char16_t>(0xDD8EU),
                u'\0',
            },
        "localized UTF-8 text did not become an owned terminated "
        "UTF-16 FString buffer");
    passed &= expect(
        encode_canvas_utf16(
            std::string_view{"A\0B", 3U}) ==
                std::unexpected(
                    CanvasCallCodecError::InvalidText) &&
            encode_canvas_utf16(
                "\xF0\x28\x8C\x28") ==
                std::unexpected(
                    CanvasCallCodecError::InvalidText) &&
            encode_canvas_utf16(
                std::string(4'097U, 'a')) ==
                std::unexpected(
                    CanvasCallCodecError::InvalidText),
        "an invalid or unbounded Canvas string reached the "
        "FString ABI");
    const auto text_units =
        std::array{u'日', u'本', u'語', u'\0'};
    const auto font_identity =
        reinterpret_cast<void*>(0x5678U);
    const auto encoded_text = encode_canvas_text(
        CanvasTextInput{
            font_identity,
            {
                text_units.data(),
                static_cast<std::int32_t>(text_units.size()),
                static_cast<std::int32_t>(text_units.size()),
            },
            {24.0, 48.0},
            {240U, 224U, 192U, 255U},
            1.5,
        });
    passed &= expect(
        encoded_text &&
            encoded_text->render_font == font_identity &&
            encoded_text->render_text.data ==
                text_units.data() &&
            encoded_text->render_text.count == 4 &&
            encoded_text->screen_position.x == 24.0 &&
            encoded_text->screen_position.y == 48.0 &&
            encoded_text->scale.x == 1.5 &&
            encoded_text->scale.y == 1.5 &&
            encoded_text->shadow_color.alpha == 0.0F &&
            !encoded_text->centre_x &&
            !encoded_text->centre_y &&
            !encoded_text->outlined &&
            encoded_text->outline_color.alpha == 0.0F,
        "localized Canvas text did not encode to the UE5.6 ABI");
    passed &= expect(
        encode_canvas_line(
            CanvasLineInput{
                {1.0, 1.0},
                {1.0, 1.0},
                {},
                1.0,
            }) ==
            std::unexpected(
                CanvasCallCodecError::InvalidGeometry) &&
            encode_canvas_filled_box(
                CanvasBoxInput{
                    {0.0, 0.0, 0.0, 10.0},
                    {},
                }) ==
                std::unexpected(
                    CanvasCallCodecError::InvalidGeometry) &&
            encode_canvas_texture(
                CanvasTextureInput{
                    nullptr,
                    {0.0, 0.0, 10.0, 10.0},
                    {},
                    {},
                }) ==
                std::unexpected(
                    CanvasCallCodecError::InvalidGeometry),
        "an invalid Canvas geometry/resource reached an ABI call");
    auto unterminated_text = CanvasTextInput{
        font_identity,
        {
            text_units.data(),
            3,
            4,
        },
        {1.0, 1.0},
        {},
        1.0,
    };
    passed &= expect(
        encode_canvas_text(unterminated_text) ==
            std::unexpected(
                CanvasCallCodecError::InvalidText),
        "unterminated Canvas text reached the FString ABI");
    auto wrong_text_kind = k2_draw_text_contract();
    wrong_text_kind.properties[1].kind =
        ReflectionPropertyKind::Array;
    wrong_text_kind.properties[1].type_name = "Byte";
    passed &= expect(
        has_error(
            wrong_text_kind,
            k2_draw_text_contract(),
            ReflectionContractErrorCode::PropertyKind,
            "RenderText"),
        "a byte array was accepted as a Canvas FString");
    passed &= expect(
        export_channel_to_bytes_contract().properties[1] ==
                ReflectionPropertyDescriptor{
                    "OutData",
                    ReflectionPropertyKind::Array,
                    "Byte",
                    0x08U,
                    0x10U,
                    1U,
                    ReflectionPropertyDirection::Output,
                } &&
            import_channel_from_bytes_contract().properties[1] ==
                ReflectionPropertyDescriptor{
                    "Data",
                    ReflectionPropertyKind::Array,
                    "Byte",
                    0x08U,
                    0x10U,
                    1U,
                    ReflectionPropertyDirection::Input,
                },
        "the exact preview byte-array property contract drifted");
    auto wrong_preview_array =
        export_channel_to_bytes_contract();
    wrong_preview_array.properties[1].type_name =
        "EPaintChannel";
    passed &= expect(
        has_error(
            wrong_preview_array,
            export_channel_to_bytes_contract(),
            ReflectionContractErrorCode::PropertyType,
            "OutData"),
        "an enum-backed preview byte array was accepted");
    wrong_preview_array =
        export_channel_to_bytes_contract();
    wrong_preview_array.properties[1].direction =
        ReflectionPropertyDirection::Input;
    passed &= expect(
        has_error(
            wrong_preview_array,
            export_channel_to_bytes_contract(),
            ReflectionContractErrorCode::PropertyDirection,
            "OutData"),
        "an input preview array was accepted as output storage");

    const auto albedo =
        std::array<std::byte, 4U * 4U * 4U>{};
    const auto packed_pbr =
        std::array<std::byte, 4U * 4U * 4U>{};
    passed &= expect(
        infer_paint_texture_dimension(albedo, packed_pbr) ==
            4U,
        "a square exact preview texture was rejected");
    passed &= expect(
        infer_paint_texture_dimension(
            std::span<const std::byte>{albedo}.first(60U),
            packed_pbr) ==
            std::unexpected(
                PaintPreviewCodecError::MismatchedChannels),
        "mismatched preview channels were accepted");
    passed &= expect(
        infer_paint_texture_dimension(
            std::span<const std::byte>{albedo}.first(48U),
            std::span<const std::byte>{packed_pbr}.first(48U)) ==
            std::unexpected(
                PaintPreviewCodecError::InvalidDimension),
        "a non-square preview texture was accepted");
    const auto encoded_import = encode_channel_import(
        RuntimePaintChannel::Albedo,
        albedo);
    passed &= expect(
        encoded_import &&
            encoded_import->channel ==
                RuntimePaintChannel::Albedo &&
            encoded_import->data.data == albedo.data() &&
            encoded_import->data.count ==
                static_cast<std::int32_t>(albedo.size()) &&
            encoded_import->data.capacity ==
                static_cast<std::int32_t>(albedo.size()) &&
            !encoded_import->return_value,
        "preview bytes did not encode to the reviewed import ABI");
    passed &= expect(
        encode_channel_import(
            RuntimePaintChannel::Albedo,
            std::span<const std::byte>{}) ==
            std::unexpected(
                PaintPreviewCodecError::InvalidBytes),
        "an empty preview import was accepted");

    auto queue_tracker = PaintQueueObservationTracker{};
    const auto component = application::RuntimeObjectHandle{8U, 3U};
    const auto first_queue_sample = queue_tracker.observe(
        component,
        application::JobGeneration{5U},
        PaintQueueCounters{
            0,
            0,
            0,
            RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
        });
    passed &= expect(
        first_queue_sample &&
            *first_queue_sample ==
                application::PaintQueueObservation{
                    true,
                    false,
                    0U,
                    true,
                    0U,
                },
        "an idle exact queue sample was not preserved");
    const auto active_queue_sample = queue_tracker.observe(
        component,
        application::JobGeneration{5U},
        PaintQueueCounters{
            2,
            3,
            3,
            RuntimePaintReplicationPressureAbi{1, 3, 48, 1.0F},
        });
    passed &= expect(
        active_queue_sample &&
            *active_queue_sample ==
                application::PaintQueueObservation{
                    true,
                    true,
                    2U,
                    true,
                    3U,
                },
        "owned visual and outgoing queue counters were not mapped");
    const auto drained_queue_sample = queue_tracker.observe(
        component,
        application::JobGeneration{5U},
        PaintQueueCounters{
            0,
            0,
            0,
            RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
        });
    passed &= expect(
        drained_queue_sample &&
            drained_queue_sample->visual_observed_activity &&
            drained_queue_sample->visual_pending == 0U &&
            drained_queue_sample->outgoing_pending == 0U,
        "visual activity was not sticky through queue drain");
    const auto next_generation_sample = queue_tracker.observe(
        component,
        application::JobGeneration{6U},
        PaintQueueCounters{
            0,
            0,
            0,
            RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
        });
    passed &= expect(
        next_generation_sample &&
            !next_generation_sample->visual_observed_activity,
        "queue activity leaked into a new job generation");
    passed &= expect(
        queue_tracker.observe(
            component,
            application::JobGeneration{6U},
            PaintQueueCounters{
                -1,
                0,
                0,
                RuntimePaintReplicationPressureAbi{0, 0, 48, 0.0F},
            }) ==
            std::unexpected(PaintQueueCodecError::InvalidCounter),
        "a negative game-owned queue counter was accepted");
    passed &= expect(
        queue_tracker.observe(
            component,
            application::JobGeneration{6U},
            PaintQueueCounters{
                0,
                0,
                0,
                RuntimePaintReplicationPressureAbi{
                    0,
                    0,
                    48,
                    std::numeric_limits<float>::infinity(),
                },
            }) ==
            std::unexpected(PaintQueueCodecError::InvalidPressure),
        "a non-finite replication-pressure result was accepted");
    passed &= expect(
        queue_tracker.observe(
            component,
            application::JobGeneration{6U},
            PaintQueueCounters{
                0,
                std::nullopt,
                0,
                RuntimePaintReplicationPressureAbi{
                    0,
                    0,
                    48,
                    0.0F,
                },
            }) ==
            std::unexpected(
                PaintQueueCodecError::MissingOwnedObserver),
        "a queue sample without the component-owned observer was accepted");

    const auto request = application::PaintAtUvWithBrush{
        1U,
        2U,
        application::RuntimeObjectHandle{3U, 4U},
        0.25,
        0.75,
        5.0,
        1024U,
        core::Rgb8{128U, 64U, 255U},
        core::Material{0.2, 0.8, 0.4},
        true,
    };
    const auto encoded = encode_paint_call(request);
    passed &= expect(
        encoded &&
            encoded->uv.x == 0.25 &&
            encoded->uv.y == 0.75 &&
            std::abs(
                encoded->brush_settings.radius -
                (5.0F / 1024.0F)) < 0.000001F &&
            encoded->brush_settings.hardness == 1.0F &&
            encoded->brush_settings.opacity == 1.0F &&
            encoded->brush_settings.spacing == 1.0F &&
            encoded->brush_settings.falloff ==
                RuntimeBrushFalloff::Spherical &&
            encoded->brush_settings.blend_mode ==
                RuntimePaintBlendMode::Normal &&
            encoded->brush_settings.brush_texture == nullptr &&
            encoded->channel_data.apply_mode ==
                PaintChannelApplyMode::Override &&
            encoded->channel ==
                RuntimePaintChannel::
                    AlbedoMetallicRoughnessEmissive &&
            std::abs(encoded->channel_data.albedo.red -
                     0.2158605F) < 0.00001F &&
            std::abs(encoded->channel_data.albedo.green -
                     0.0512695F) < 0.00001F &&
            encoded->channel_data.albedo.blue == 1.0F &&
            encoded->channel_data.albedo.alpha == 1.0F &&
            std::abs(encoded->channel_data.metallic - 0.2F) <
                0.000001F &&
            std::abs(encoded->channel_data.roughness - 0.8F) <
                0.000001F &&
            std::abs(encoded->channel_data.emissive - 0.4F) <
                0.000001F,
        "the typed Paint request did not encode to the reviewed game ABI");

    auto invalid_call = request;
    invalid_call.texture_dimension = 0U;
    passed &= expect(
        encode_paint_call(invalid_call) ==
            std::unexpected(PaintCallEncodingError::InvalidDimension),
        "the Paint codec accepted a missing texture dimension");

    auto wrong_name = expected;
    wrong_name.name = "PaintAtUvWithBrush";
    passed &= expect(
        has_error(
            wrong_name,
            expected,
            ReflectionContractErrorCode::RecordName),
        "a function-name near-match was accepted");

    auto wrong_owner = expected;
    wrong_owner.owner_name = "OtherPaintableComponent";
    passed &= expect(
        has_error(
            wrong_owner,
            expected,
            ReflectionContractErrorCode::OwnerName),
        "a wrong function owner was accepted");

    auto wrong_record_size = expected;
    wrong_record_size.size = 0x61U;
    passed &= expect(
        has_error(
            wrong_record_size,
            expected,
            ReflectionContractErrorCode::RecordSize),
        "a wrong parameter buffer size was accepted");

    auto missing = expected;
    missing.properties.erase(missing.properties.begin() + 1);
    passed &= expect(
        has_error(
            missing,
            expected,
            ReflectionContractErrorCode::MissingProperty,
            "ChannelData"),
        "a missing parameter was accepted");

    auto unexpected = expected;
    unexpected.properties.push_back(
        ReflectionPropertyDescriptor{
            "ResearchOnly",
            ReflectionPropertyKind::Bool,
            {},
            0x61U,
            0x01U,
            1U,
            ReflectionPropertyDirection::Input,
        });
    passed &= expect(
        has_error(
            unexpected,
            expected,
            ReflectionContractErrorCode::UnexpectedProperty,
            "ResearchOnly"),
        "an extra parameter was accepted");

    auto duplicate = expected;
    duplicate.properties[1].name = "Uv";
    passed &= expect(
        has_error(
            duplicate,
            expected,
            ReflectionContractErrorCode::DuplicateProperty,
            "Uv"),
        "duplicate reflected parameter names were accepted");

    auto property = expected.properties[0];
    property.kind = ReflectionPropertyKind::Object;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyKind,
            "Uv"),
        "a wrong parameter property kind was accepted");

    property = expected.properties[0];
    property.type_name = "Vector2f";
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyType,
            "Uv"),
        "a wrong struct type was accepted");

    property = expected.properties[0];
    property.offset = 0x08U;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyOffset,
            "Uv"),
        "a wrong parameter offset was accepted");

    property = expected.properties[0];
    property.size = 0x08U;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertySize,
            "Uv"),
        "a wrong parameter size was accepted");

    property = expected.properties[0];
    property.array_dimension = 2U;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyArrayDimension,
            "Uv"),
        "a fixed-array parameter was accepted");

    property = expected.properties[0];
    property.direction = ReflectionPropertyDirection::Output;
    passed &= expect(
        has_error(
            changed_property(expected, 0U, property),
            expected,
            ReflectionContractErrorCode::PropertyDirection,
            "Uv"),
        "an output parameter was accepted as an input");

    if (passed)
    {
        std::cout << "PASS runtime_reflection_contract\n";
        return 0;
    }
    return 1;
}
