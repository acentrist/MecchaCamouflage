#pragma once

#include <meccha/runtime/reflection_contract.hpp>

namespace meccha::runtime
{
[[nodiscard]] auto k2_draw_line_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto k2_draw_texture_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto k2_draw_text_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto import_buffer_as_texture2d_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto create_render_target_2d_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto read_render_target_raw_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto capture_scene_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto set_show_flag_settings_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto hide_component_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto k2_destroy_actor_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto vector_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto rotator_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_camera_location_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_camera_rotation_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_fov_angle_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto k2_get_component_location_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto k2_get_component_rotation_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_scaled_capsule_radius_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_scaled_capsule_half_height_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto project_world_location_to_screen_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_socket_location_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_socket_transform_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto is_look_input_ignored_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto is_move_input_ignored_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto set_ignore_look_input_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto set_ignore_move_input_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto vector2d_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto linear_color_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto paint_channel_data_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto runtime_brush_settings_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto initialize_paint_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto screen_space_paint_result_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto hit_test_at_screen_position_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto is_paint_initialized_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto get_initialized_paint_mesh_contract()
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

[[nodiscard]] auto export_channel_to_bytes_contract()
    -> ReflectionRecordDescriptor;

[[nodiscard]] auto import_channel_from_bytes_contract()
    -> ReflectionRecordDescriptor;
} // namespace meccha::runtime
