#pragma once

#include <meccha/runtime/reflection_contract.hpp>

namespace meccha::runtime
{
[[nodiscard]] auto vector2d_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto paint_channel_data_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto runtime_brush_settings_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto paint_at_uv_with_brush_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto recorded_stroke_count_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto runtime_paint_replication_pressure_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto queued_stroke_count_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto queued_stroke_count_for_component_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto replication_pressure_contract()
    -> ReflectionRecordDescriptor;
} // namespace meccha::runtime
