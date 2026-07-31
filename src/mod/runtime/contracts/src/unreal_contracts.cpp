#include <meccha/runtime/unreal_contracts.hpp>

namespace meccha::runtime
{
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
} // namespace meccha::runtime
