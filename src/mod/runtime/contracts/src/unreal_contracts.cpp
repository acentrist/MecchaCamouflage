#include <meccha/runtime/unreal_contracts.hpp>

namespace meccha::runtime
{
namespace
{
auto vector_return_contract(
    std::string name,
    std::string owner_name) -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        std::move(name),
        std::move(owner_name),
        0x18U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Struct,
                "Vector",
                0x00U,
                0x18U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto rotator_return_contract(
    std::string name,
    std::string owner_name) -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        std::move(name),
        std::move(owner_name),
        0x18U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Struct,
                "Rotator",
                0x00U,
                0x18U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto float_return_contract(
    std::string name,
    std::string owner_name) -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        std::move(name),
        std::move(owner_name),
        0x04U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Float32,
                {},
                0x00U,
                0x04U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}
} // namespace

auto k2_draw_line_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "K2_DrawLine",
        "/Script/Engine.Canvas",
        0x38U,
        {
            ReflectionPropertyDescriptor{
                "ScreenPositionA",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x00U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ScreenPositionB",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x10U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "Thickness",
                ReflectionPropertyKind::Float32,
                {},
                0x20U,
                0x04U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "RenderColor",
                ReflectionPropertyKind::Struct,
                "LinearColor",
                0x24U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
        },
    };
}

auto k2_draw_texture_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "K2_DrawTexture",
        "/Script/Engine.Canvas",
        0x70U,
        {
            ReflectionPropertyDescriptor{
                "RenderTexture",
                ReflectionPropertyKind::Object,
                "Texture",
                0x00U,
                0x08U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ScreenPosition",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x08U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ScreenSize",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x18U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "CoordinatePosition",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x28U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "CoordinateSize",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x38U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "RenderColor",
                ReflectionPropertyKind::Struct,
                "LinearColor",
                0x48U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "BlendMode",
                ReflectionPropertyKind::Enum,
                "EBlendMode",
                0x58U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "Rotation",
                ReflectionPropertyKind::Float32,
                {},
                0x5CU,
                0x04U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "PivotPoint",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x60U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
        },
    };
}

auto k2_draw_text_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "K2_DrawText",
        "/Script/Engine.Canvas",
        0x88U,
        {
            ReflectionPropertyDescriptor{
                "RenderFont",
                ReflectionPropertyKind::Object,
                "Font",
                0x00U,
                0x08U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "RenderText",
                ReflectionPropertyKind::String,
                {},
                0x08U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ScreenPosition",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x18U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "Scale",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x28U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "RenderColor",
                ReflectionPropertyKind::Struct,
                "LinearColor",
                0x38U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "Kerning",
                ReflectionPropertyKind::Float32,
                {},
                0x48U,
                0x04U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ShadowColor",
                ReflectionPropertyKind::Struct,
                "LinearColor",
                0x4CU,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ShadowOffset",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x60U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "bCentreX",
                ReflectionPropertyKind::Bool,
                {},
                0x70U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "bCentreY",
                ReflectionPropertyKind::Bool,
                {},
                0x71U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "bOutlined",
                ReflectionPropertyKind::Bool,
                {},
                0x72U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "OutlineColor",
                ReflectionPropertyKind::Struct,
                "LinearColor",
                0x74U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
        },
    };
}

auto import_buffer_as_texture2d_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "ImportBufferAsTexture2D",
        "/Script/Engine.KismetRenderingLibrary",
        0x20U,
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
                "Buffer",
                ReflectionPropertyKind::Array,
                "Byte",
                0x08U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Object,
                "Texture2D",
                0x18U,
                0x08U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto create_render_target_2d_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
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
    };
}

auto read_render_target_raw_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
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
    };
}

auto capture_scene_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "CaptureScene",
        "/Script/Engine.SceneCaptureComponent2D",
        0x00U,
        {},
    };
}

auto hide_component_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
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
    };
}

auto k2_destroy_actor_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "K2_DestroyActor",
        "/Script/Engine.Actor",
        0x00U,
        {},
    };
}

auto vector_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "Vector",
        "/Script/CoreUObject",
        0x18U,
        {
            ReflectionPropertyDescriptor{
                "X",
                ReflectionPropertyKind::Float64,
                {},
                0x00U,
                0x08U,
            },
            ReflectionPropertyDescriptor{
                "Y",
                ReflectionPropertyKind::Float64,
                {},
                0x08U,
                0x08U,
            },
            ReflectionPropertyDescriptor{
                "Z",
                ReflectionPropertyKind::Float64,
                {},
                0x10U,
                0x08U,
            },
        },
    };
}

auto rotator_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "Rotator",
        "/Script/CoreUObject",
        0x18U,
        {
            ReflectionPropertyDescriptor{
                "Pitch",
                ReflectionPropertyKind::Float64,
                {},
                0x00U,
                0x08U,
            },
            ReflectionPropertyDescriptor{
                "Yaw",
                ReflectionPropertyKind::Float64,
                {},
                0x08U,
                0x08U,
            },
            ReflectionPropertyDescriptor{
                "Roll",
                ReflectionPropertyKind::Float64,
                {},
                0x10U,
                0x08U,
            },
        },
    };
}

auto get_camera_location_contract()
    -> ReflectionRecordDescriptor
{
    return vector_return_contract(
        "GetCameraLocation",
        "/Script/Engine.PlayerCameraManager");
}

auto get_camera_rotation_contract()
    -> ReflectionRecordDescriptor
{
    return rotator_return_contract(
        "GetCameraRotation",
        "/Script/Engine.PlayerCameraManager");
}

auto get_fov_angle_contract() -> ReflectionRecordDescriptor
{
    return float_return_contract(
        "GetFOVAngle",
        "/Script/Engine.PlayerCameraManager");
}

auto k2_get_component_location_contract()
    -> ReflectionRecordDescriptor
{
    return vector_return_contract(
        "K2_GetComponentLocation",
        "/Script/Engine.SceneComponent");
}

auto k2_get_component_rotation_contract()
    -> ReflectionRecordDescriptor
{
    return rotator_return_contract(
        "K2_GetComponentRotation",
        "/Script/Engine.SceneComponent");
}

auto get_scaled_capsule_radius_contract()
    -> ReflectionRecordDescriptor
{
    return float_return_contract(
        "GetScaledCapsuleRadius",
        "/Script/Engine.CapsuleComponent");
}

auto get_scaled_capsule_half_height_contract()
    -> ReflectionRecordDescriptor
{
    return float_return_contract(
        "GetScaledCapsuleHalfHeight",
        "/Script/Engine.CapsuleComponent");
}

auto project_world_location_to_screen_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "ProjectWorldLocationToScreen",
        "/Script/Engine.PlayerController",
        0x30U,
        {
            ReflectionPropertyDescriptor{
                "WorldLocation",
                ReflectionPropertyKind::Struct,
                "Vector",
                0x00U,
                0x18U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ScreenLocation",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x18U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Output,
            },
            ReflectionPropertyDescriptor{
                "bPlayerViewportRelative",
                ReflectionPropertyKind::Bool,
                {},
                0x28U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Bool,
                {},
                0x29U,
                0x01U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto get_socket_location_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "GetSocketLocation",
        "/Script/Engine.SceneComponent",
        0x28U,
        {
            ReflectionPropertyDescriptor{
                "InSocketName",
                ReflectionPropertyKind::Name,
                {},
                0x00U,
                0x0CU,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Struct,
                "Vector",
                0x10U,
                0x18U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto get_socket_transform_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "GetSocketTransform",
        "/Script/Engine.SceneComponent",
        0x70U,
        {
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
        },
    };
}

auto is_look_input_ignored_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "IsLookInputIgnored",
        "/Script/Engine.Controller",
        0x01U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Bool,
                {},
                0x00U,
                0x01U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto is_move_input_ignored_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "IsMoveInputIgnored",
        "/Script/Engine.Controller",
        0x01U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Bool,
                {},
                0x00U,
                0x01U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto set_ignore_look_input_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "SetIgnoreLookInput",
        "/Script/Engine.Controller",
        0x01U,
        {
            ReflectionPropertyDescriptor{
                "bNewLookInput",
                ReflectionPropertyKind::Bool,
                {},
                0x00U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
        },
    };
}

auto set_ignore_move_input_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "SetIgnoreMoveInput",
        "/Script/Engine.Controller",
        0x01U,
        {
            ReflectionPropertyDescriptor{
                "bNewMoveInput",
                ReflectionPropertyKind::Bool,
                {},
                0x00U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
        },
    };
}

auto vector2d_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "Vector2D",
        "/Script/CoreUObject",
        0x10U,
        {
            ReflectionPropertyDescriptor{
                "X",
                ReflectionPropertyKind::Float64,
                {},
                0x00U,
                0x08U,
            },
            ReflectionPropertyDescriptor{
                "Y",
                ReflectionPropertyKind::Float64,
                {},
                0x08U,
                0x08U,
            },
        },
    };
}

auto linear_color_contract() -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "LinearColor",
        "/Script/CoreUObject",
        0x10U,
        {
            ReflectionPropertyDescriptor{
                "R",
                ReflectionPropertyKind::Float32,
                {},
                0x00U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "G",
                ReflectionPropertyKind::Float32,
                {},
                0x04U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "B",
                ReflectionPropertyKind::Float32,
                {},
                0x08U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "A",
                ReflectionPropertyKind::Float32,
                {},
                0x0CU,
                0x04U,
            },
        },
    };
}

auto paint_channel_data_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "PaintChannelData",
        "/Script/PenguinHotel",
        0x24U,
        {
            ReflectionPropertyDescriptor{
                "AlbedoColor",
                ReflectionPropertyKind::Struct,
                "LinearColor",
                0x00U,
                0x10U,
            },
            ReflectionPropertyDescriptor{
                "Metallic",
                ReflectionPropertyKind::Float32,
                {},
                0x10U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "Roughness",
                ReflectionPropertyKind::Float32,
                {},
                0x14U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "Height",
                ReflectionPropertyKind::Float32,
                {},
                0x18U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "EmissiveColor",
                ReflectionPropertyKind::Float32,
                {},
                0x1CU,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "ApplyMode",
                ReflectionPropertyKind::Enum,
                "EPaintChannelApplyMode",
                0x20U,
                0x01U,
            },
        },
    };
}

auto runtime_brush_settings_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "RuntimeBrushSettings",
        "/Script/PenguinHotel",
        0x28U,
        {
            ReflectionPropertyDescriptor{
                "Radius",
                ReflectionPropertyKind::Float32,
                {},
                0x00U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "Hardness",
                ReflectionPropertyKind::Float32,
                {},
                0x04U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "Opacity",
                ReflectionPropertyKind::Float32,
                {},
                0x08U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "Spacing",
                ReflectionPropertyKind::Float32,
                {},
                0x0CU,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "Falloff",
                ReflectionPropertyKind::Enum,
                "EBrushFalloff",
                0x10U,
                0x01U,
            },
            ReflectionPropertyDescriptor{
                "BlendMode",
                ReflectionPropertyKind::Enum,
                "EPaintBlendMode",
                0x11U,
                0x01U,
            },
            ReflectionPropertyDescriptor{
                "BrushTexture",
                ReflectionPropertyKind::Object,
                "Texture2D",
                0x18U,
                0x08U,
            },
            ReflectionPropertyDescriptor{
                "Rotation",
                ReflectionPropertyKind::Float32,
                {},
                0x20U,
                0x04U,
            },
        },
    };
}

auto paint_at_uv_with_brush_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "PaintAtUVWithBrush",
        "/Script/PenguinHotel.RuntimePaintableComponent",
        0x68U,
        {
            ReflectionPropertyDescriptor{
                "Uv",
                ReflectionPropertyKind::Struct,
                "Vector2D",
                0x00U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ChannelData",
                ReflectionPropertyKind::Struct,
                "PaintChannelData",
                0x10U,
                0x24U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "BrushSettings",
                ReflectionPropertyKind::Struct,
                "RuntimeBrushSettings",
                0x38U,
                0x28U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "Channel",
                ReflectionPropertyKind::Enum,
                "EPaintChannel",
                0x60U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
        },
    };
}

auto initialize_paint_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "InitializePaint",
        "/Script/PenguinHotel.RuntimePaintableComponent",
        0x10U,
        {
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
        },
    };
}

auto is_paint_initialized_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "IsInitialized",
        "/Script/PenguinHotel.RuntimePaintableComponent",
        0x01U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Bool,
                {},
                0x00U,
                0x01U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto get_initialized_paint_mesh_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "GetInitializedPaintMesh",
        "/Script/PenguinHotel.RuntimePaintableComponent",
        0x08U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Object,
                "MeshComponent",
                0x00U,
                0x08U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto recorded_stroke_count_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "GetRecordedStrokeCount",
        "/Script/PenguinHotel.RuntimePaintableComponent",
        0x04U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Int32,
                {},
                0x00U,
                0x04U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto runtime_paint_replication_pressure_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "RuntimePaintReplicationPressure",
        "/Script/PenguinHotel",
        0x10U,
        {
            ReflectionPropertyDescriptor{
                "QueuedBatchCount",
                ReflectionPropertyKind::Int32,
                {},
                0x00U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "QueuedStrokeCount",
                ReflectionPropertyKind::Int32,
                {},
                0x04U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "MaxStrokesPerTick",
                ReflectionPropertyKind::Int32,
                {},
                0x08U,
                0x04U,
            },
            ReflectionPropertyDescriptor{
                "EstimatedTicksToDrain",
                ReflectionPropertyKind::Float32,
                {},
                0x0CU,
                0x04U,
            },
        },
    };
}

auto queued_stroke_count_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "GetQueuedStrokeCount",
        "/Script/PenguinHotel.RuntimePaintReplicationManager",
        0x04U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Int32,
                {},
                0x00U,
                0x04U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto queued_stroke_count_for_component_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "GetQueuedStrokeCountForComponent",
        "/Script/PenguinHotel.RuntimePaintReplicationManager",
        0x10U,
        {
            ReflectionPropertyDescriptor{
                "PaintComponent",
                ReflectionPropertyKind::Object,
                "RuntimePaintableComponent",
                0x00U,
                0x08U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Int32,
                {},
                0x08U,
                0x04U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto replication_pressure_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "GetReplicationPressure",
        "/Script/PenguinHotel.RuntimePaintReplicationManager",
        0x10U,
        {
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Struct,
                "RuntimePaintReplicationPressure",
                0x00U,
                0x10U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto export_channel_to_bytes_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "ExportChannelToBytes",
        "/Script/PenguinHotel.RuntimePaintableComponent",
        0x20U,
        {
            ReflectionPropertyDescriptor{
                "Channel",
                ReflectionPropertyKind::Enum,
                "EPaintChannel",
                0x00U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "OutData",
                ReflectionPropertyKind::Array,
                "Byte",
                0x08U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Output,
            },
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Bool,
                {},
                0x18U,
                0x01U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}

auto import_channel_from_bytes_contract()
    -> ReflectionRecordDescriptor
{
    return ReflectionRecordDescriptor{
        "ImportChannelFromBytes",
        "/Script/PenguinHotel.RuntimePaintableComponent",
        0x20U,
        {
            ReflectionPropertyDescriptor{
                "Channel",
                ReflectionPropertyKind::Enum,
                "EPaintChannel",
                0x00U,
                0x01U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "Data",
                ReflectionPropertyKind::Array,
                "Byte",
                0x08U,
                0x10U,
                1U,
                ReflectionPropertyDirection::Input,
            },
            ReflectionPropertyDescriptor{
                "ReturnValue",
                ReflectionPropertyKind::Bool,
                {},
                0x18U,
                0x01U,
                1U,
                ReflectionPropertyDirection::ReturnValue,
            },
        },
    };
}
} // namespace meccha::runtime
