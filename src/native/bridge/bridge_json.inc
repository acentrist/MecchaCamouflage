    auto json_number_field(const std::string& text, const std::string& key, double fallback) -> double
    {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return fallback;
        const char* begin = text.c_str() + start + needle.size();
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        return end == begin || !std::isfinite(value) ? fallback : value;
    }

    auto json_int_field(const std::string& text, const std::string& key, int fallback, int min_value, int max_value) -> int
    {
        const double value = json_number_field(text, key, static_cast<double>(fallback));
        return std::max(min_value, std::min(max_value, static_cast<int>(value)));
    }

    auto json_bool_field(const std::string& text, const std::string& key, bool fallback) -> bool
    {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return fallback;
        auto pos = start + needle.size();
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            ++pos;
        if (text.compare(pos, 4, "true") == 0)
            return true;
        if (text.compare(pos, 5, "false") == 0)
            return false;
        return fallback;
    }

    auto json_string_field(const std::string& text, const std::string& key, const std::string& fallback = {}) -> std::string
    {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return fallback;
        auto pos = text.find('"', start + needle.size());
        if (pos == std::string::npos)
            return fallback;
        ++pos;
        std::string out{};
        const auto hex_digit = [](char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        const auto read_unicode_escape = [&text, &hex_digit](std::size_t start, std::uint32_t& codepoint) -> bool {
            if (start + 4 >= text.size())
                return false;
            codepoint = 0;
            for (std::size_t offset = 1; offset <= 4; ++offset)
            {
                const int digit = hex_digit(text[start + offset]);
                if (digit < 0)
                    return false;
                codepoint = (codepoint << 4) | static_cast<std::uint32_t>(digit);
            }
            return true;
        };
        const auto append_utf8 = [](std::string& target, std::uint32_t codepoint) {
            if (codepoint <= 0x7F)
            {
                target.push_back(static_cast<char>(codepoint));
            }
            else if (codepoint <= 0x7FF)
            {
                target.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                target.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else if (codepoint <= 0xFFFF)
            {
                target.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                target.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                target.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else
            {
                target.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                target.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                target.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                target.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        };
        bool escaped = false;
        for (; pos < text.size(); ++pos)
        {
            const char ch = text[pos];
            if (escaped)
            {
                switch (ch)
                {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                {
                    std::uint32_t codepoint = 0;
                    if (!read_unicode_escape(pos, codepoint))
                        return fallback;
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF)
                    {
                        std::uint32_t low = 0;
                        if (pos + 10 >= text.size() || text[pos + 5] != '\\' || text[pos + 6] != 'u' ||
                            !read_unicode_escape(pos + 6, low) || low < 0xDC00 || low > 0xDFFF)
                            return fallback;
                        codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        pos += 10;
                    }
                    else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
                    {
                        return fallback;
                    }
                    else
                    {
                        pos += 4;
                    }
                    append_utf8(out, codepoint);
                    break;
                }
                default: out.push_back(ch); break;
                }
                escaped = false;
                continue;
            }
            if (ch == '\\')
            {
                escaped = true;
                continue;
            }
            if (ch == '"')
            {
                return out;
            }
            out.push_back(ch);
        }
        return fallback;
    }

    auto json_escape(const std::string& text) -> std::string
    {
        std::string out{};
        out.reserve(text.size() + 8);
        for (const char c : text)
        {
            const auto value = static_cast<unsigned char>(c);
            if (c == '\\' || c == '"')
            {
                out.push_back('\\');
                out.push_back(c);
            }
            else if (c == '\n')
            {
                out += "\\n";
            }
            else if (c == '\r')
            {
                out += "\\r";
            }
            else if (c == '\t')
            {
                out += "\\t";
            }
            else if (value < 0x20)
            {
                static constexpr char digits[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(value >> 4) & 0x0f]);
                out.push_back(digits[value & 0x0f]);
            }
            else
            {
                out.push_back(c);
            }
        }
        return out;
    }

    auto metadata_contains_bool(const std::string& metadata, const char* key, bool value) -> bool
    {
        return metadata.find(std::string("\"") + key + "\":" + (value ? "true" : "false")) != std::string::npos;
    }

    auto metadata_contains_string(const std::string& metadata, const char* key, const char* value) -> bool
    {
        return metadata.find(std::string("\"") + key + "\":\"" + value + "\"") != std::string::npos;
    }

    struct JsonNonFiniteSanitization
    {
        std::string text{};
        int replacements{0};
        std::string first_key{};
    };

    auto sanitize_json_non_finite_numbers(const std::string& input)
        -> JsonNonFiniteSanitization
    {
        JsonNonFiniteSanitization out{};
        out.text.reserve(input.size());
        bool in_string = false;
        bool escaped = false;
        const auto is_value_boundary = [](char value) {
            return value == '\0' || value == ',' || value == '}' ||
                   value == ']' ||
                   std::isspace(static_cast<unsigned char>(value));
        };
        const auto first_key_before = [&input](std::size_t value_begin) {
            const auto colon =
                value_begin == 0
                    ? std::string::npos
                    : input.rfind(':', value_begin - 1);
            if (colon == std::string::npos)
            {
                return std::string{};
            }
            const auto quote_end =
                colon == 0
                    ? std::string::npos
                    : input.rfind('"', colon - 1);
            if (quote_end == std::string::npos || quote_end == 0)
            {
                return std::string{};
            }
            const auto quote_begin =
                input.rfind('"', quote_end - 1);
            if (quote_begin == std::string::npos)
            {
                return std::string{};
            }
            return input.substr(
                quote_begin + 1,
                quote_end - quote_begin - 1);
        };

        for (std::size_t index = 0; index < input.size();)
        {
            const char current = input[index];
            if (escaped)
            {
                out.text.push_back(current);
                escaped = false;
                ++index;
                continue;
            }
            if (in_string && current == '\\')
            {
                out.text.push_back(current);
                escaped = true;
                ++index;
                continue;
            }
            if (current == '"')
            {
                out.text.push_back(current);
                in_string = !in_string;
                ++index;
                continue;
            }
            if (in_string)
            {
                out.text.push_back(current);
                ++index;
                continue;
            }

            const bool token_start =
                std::isalpha(static_cast<unsigned char>(current)) ||
                ((current == '-' || current == '+') &&
                 index + 1 < input.size() &&
                 std::isalpha(
                     static_cast<unsigned char>(input[index + 1])));
            if (!token_start)
            {
                out.text.push_back(current);
                ++index;
                continue;
            }

            std::size_t token_end = index + 1;
            while (token_end < input.size() &&
                   !is_value_boundary(input[token_end]))
            {
                ++token_end;
            }
            auto token = input.substr(index, token_end - index);
            std::transform(
                token.begin(),
                token.end(),
                token.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            const bool non_finite =
                token == "inf" || token == "+inf" ||
                token == "-inf" || token == "infinity" ||
                token == "+infinity" || token == "-infinity" ||
                token == "nan" || token == "+nan" ||
                token == "-nan" ||
                (token.size() > 4 &&
                 (token.rfind("nan(", 0) == 0 ||
                  token.rfind("+nan(", 0) == 0 ||
                  token.rfind("-nan(", 0) == 0) &&
                 token.back() == ')');
            if (!non_finite)
            {
                out.text.append(input, index, token_end - index);
                index = token_end;
                continue;
            }

            out.text += "-1.000000";
            ++out.replacements;
            if (out.first_key.empty())
            {
                out.first_key = first_key_before(index);
            }
            index = token_end;
        }
        return out;
    }

    auto append_metadata_field(std::string& out, const std::string& metadata, const char* key) -> void
    {
        const std::string pattern = std::string("\"") + key + "\":";
        const auto begin = metadata.find(pattern);
        if (begin == std::string::npos)
        {
            return;
        }

        std::size_t pos = begin + pattern.size();
        bool in_string = false;
        bool escaped = false;
        int depth = 0;
        for (; pos < metadata.size(); ++pos)
        {
            const char c = metadata[pos];
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (in_string && c == '\\')
            {
                escaped = true;
                continue;
            }
            if (c == '"')
            {
                in_string = !in_string;
                continue;
            }
            if (in_string)
            {
                continue;
            }
            if (c == '{' || c == '[')
            {
                ++depth;
                continue;
            }
            if (c == '}' || c == ']')
            {
                --depth;
                continue;
            }
            if (c == ',' && depth == 0)
            {
                break;
            }
        }

        if (!out.empty())
        {
            out += ",";
        }
        out += pattern;
        out += metadata.substr(begin + pattern.size(), pos - (begin + pattern.size()));
    }

    auto compact_mesh_response_metadata(const std::string& metadata) -> std::string
    {
        if (!metadata_contains_string(metadata, "route", "mesh_first_paint") ||
            metadata_contains_bool(metadata, "research_artifacts_requested", true))
        {
            return metadata;
        }

        std::string out;
        for (const char* key : {
                 "route",
                 "paint_source_kind",
                 "mesh_first_pipeline",
                 "preview_only",
                 "unpreview_only",
                 "unpreview_snapshot_available",
                 "unpreview_snapshot_component",
                 "unpreview_snapshot_texture_size",
                 "unpreview_snapshot_hash",
                 "unpreview_import_ok",
                 "unpreview_restore_verified",
                 "unpreview_import_failure",
                 "front_region_mode",
                 "side_region_mode",
                 "back_region_mode",
                 "paint_region_count",
                 "fill_region_count",
                 "skip_region_count",
                 "image_fill_region_count",
                 "profile_id",
                 "image_paint_enabled",
                 "image_paint_body_type",
                 "image_paint_requested_body_type",
                 "image_paint_body_type_source",
                 "image_paint_body_type_auto_corrected",
                 "image_paint_profile_kind",
                 "image_paint_assignments",
                 "image_paint_transparent_skips",
                 "image_paint_cube_edge_assignments",
                 "image_paint_cube_side_assignments",
                 "image_paint_cube_canonical_mapping_failures",
                 "image_paint_round_canonical_mapping_failures",
                 "image_paint_atlas_ms",
                 "image_paint_atlas_cache_hit",
                 "image_paint_mapping_ms",
                 "image_paint_mapping_workers",
                 "image_paint_mapping_parallel",
                 "image_paint_mapping_mode",
                 "image_paint_candidate_ms",
                 "image_paint_candidate_mode",
                 "image_paint_revision",
                 "image_paint_metallic",
                 "image_paint_roughness",
                 "image_paint_emissive",
                 "image_paint_fill_color_r",
                 "image_paint_fill_color_g",
                 "image_paint_fill_color_b",
                 "image_paint_fill_metallic",
                 "image_paint_fill_roughness",
                 "image_paint_fill_emissive",
                 "runtime_triangle_cache_mode",
                 "runtime_triangle_cache_failure",
                 "runtime_triangle_profile_cache_failure",
                 "runtime_triangle_cache_used",
                 "runtime_triangle_cache_offset",
                 "runtime_triangle_cache_stride",
                 "runtime_triangle_cache_triangles",
                 "runtime_triangle_cache_expected_triangles",
                 "runtime_triangle_cache_array_headers_seen",
                 "runtime_triangle_cache_matching_count_arrays_seen",
                 "runtime_triangle_cache_capacity_rejections",
                 "runtime_triangle_cache_read_rejections",
                 "runtime_triangle_cache_uv_rejections",
                 "runtime_triangle_cache_closest_triangle_count",
                 "runtime_triangle_cache_closest_rejected_uv_error",
                 "runtime_triangle_cache_profile_uv_mapping_mode",
                 "runtime_triangle_cache_profile_uv_avg_error",
                 "runtime_triangle_cache_warmup_attempted",
                 "runtime_triangle_cache_warmup_reason",
                 "runtime_triangle_cache_warmup_pre_avg_error",
                 "runtime_triangle_cache_warmup_pre_samples",
                 "runtime_triangle_cache_warmup_post_avg_error",
                 "runtime_triangle_cache_warmup_post_samples",
                 "runtime_triangle_cache_warmup_is_initialized_available",
                 "runtime_triangle_cache_warmup_is_initialized_before_ok",
                 "runtime_triangle_cache_warmup_is_initialized_before",
                 "runtime_triangle_cache_warmup_is_initialized_after_ok",
                 "runtime_triangle_cache_warmup_is_initialized_after",
                 "runtime_triangle_cache_warmup_initialize_available",
                 "runtime_triangle_cache_warmup_initialize_called",
                 "runtime_triangle_cache_warmup_initialize_ok",
                 "runtime_triangle_cache_warmup_initialize_skip_reason",
                 "runtime_triangle_cache_warmup_initialized_mesh_before",
                 "runtime_triangle_cache_warmup_initialized_mesh_after",
                 "runtime_triangle_cache_warmup_hit_test_available",
                 "runtime_triangle_cache_warmup_hit_test_uncached_called",
                 "runtime_triangle_cache_warmup_hit_test_uncached_ok",
                 "runtime_triangle_cache_warmup_hit_test_uncached_hit",
                 "runtime_triangle_cache_warmup_hit_test_cached_called",
                 "runtime_triangle_cache_warmup_hit_test_cached_ok",
                 "runtime_triangle_cache_warmup_hit_test_cached_hit",
                 "runtime_triangle_cache_warmup_failure",
                 "fill_all_regions",
                 "fill_scope",
                 "brush_pipeline",
                 "brush_size_texels",
                 "coverage_step_texels",
                 "planner_coverage_step_texels",
                 "appearance_calibration_step_texels",
                 "appearance_replay_sample_count",
                 "appearance_calibration_sample_count",
                 "brush_radius_texels",
                 "brush_radius_uv",
                 "fill_stroke_radius_texels",
                 "fill_stroke_radius_source",
                 "environment_lighting_policy",
                 "material_properties_requested_mode",
                 "material_properties_mode",
                 "material_properties_source",
                 "material_properties_candidate_count",
                 "material_properties_candidates",
                 "material_properties_selection",
                 "material_properties_failure",
                 "resolved_paint_metallic",
                 "resolved_paint_roughness",
                 "resolved_paint_emissive",
                 "environment_capture_source",
                 "environment_emission_capture_ok",
                 "environment_emission_capture_failure",
                 "environment_emission_show_flags_ok",
                 "environment_emission_evidence_available",
                 "environment_emission_noise_ok",
                 "environment_emission_noise_threshold",
                 "environment_emission_noise_paired_samples",
                 "environment_emission_rescued_samples",
                 "environment_emission_unchanged_samples",
                 "environment_projection_schema",
                 "environment_projection_ok",
                 "environment_projection_failure",
                 "environment_projection_inputs",
                 "environment_projection_assignments",
                 "environment_projection_missing",
                 "environment_projection_sampling_failures",
                 "environment_emission_evidence_failures",
                 "appearance_feedback_lattice_samples",
                 "appearance_feedback_lattice_samples_front",
                 "appearance_feedback_lattice_samples_side",
                 "appearance_feedback_lattice_samples_back",
                 "appearance_correction_field_ok",
                 "appearance_correction_field_failure",
                 "appearance_correction_front_anchor_vertices",
                 "appearance_correction_back_anchor_vertices",
                 "appearance_correction_side_components",
                 "appearance_correction_one_boundary_side_components",
                 "appearance_correction_unanchored_side_components",
                 "appearance_correction_side_applied_samples",
                 "appearance_correction_field_hash",
                 "appearance_physical_manual_emissive_floor",
                 "appearance_physical_inferred_emissive_max",
                 "appearance_physical_source_candidate_samples",
                 "appearance_physical_source_paired_samples",
                 "appearance_physical_source_rejected_samples",
                 "appearance_physical_probe_run",
                 "appearance_physical_probe_supported_samples",
                 "appearance_physical_candidate_samples",
                 "appearance_physical_accepted_samples",
                 "appearance_physical_rejected_samples",
                 "appearance_physical_color_carrier_candidate_samples",
                 "appearance_physical_color_carrier_final_samples",
                 "appearance_physical_candidate_components",
                 "appearance_physical_accepted_components",
                 "appearance_physical_rejected_components",
                 "appearance_physical_final_verification_run",
                 "appearance_physical_final_verification_accepted",
                 "appearance_physical_rejection_reason",
                 "appearance_emission_response_field_hash",
                 "environment_closed_loop_iterations",
                 "appearance_albedo_only_iterations",
                 "appearance_albedo_only_loss_initial",
                 "appearance_albedo_only_loss_best",
                 "appearance_albedo_only_improvement",
                 "environment_closed_loop_corrected_samples",
                 "environment_closed_loop_emissive_samples",
                 "environment_closed_loop_mean_albedo_adjustment",
                 "environment_closed_loop_emissive_max",
                 "mesh_debug_environment_capture_written",
                 "mesh_debug_environment_capture_bmp",
                 "mesh_debug_preview_visible_capture_ok",
                 "mesh_debug_preview_visible_capture_failure",
                 "mesh_debug_preview_visible_capture_written",
                 "mesh_debug_preview_visible_capture_bmp",
                 "appearance_paint_emissive_inspected",
                 "appearance_paint_emissive_supported",
                 "appearance_paint_emissive_reason",
                 "appearance_paint_emissive_parameter",
                 "appearance_paint_material_properties_parameter",
                 "appearance_paint_material_properties_render_target",
                 "appearance_paint_dynamic_material",
                 "appearance_paint_emissive_render_target",
                 "appearance_paint_get_render_target_available",
                 "appearance_paint_get_render_target_process_ok",
                 "appearance_paint_get_render_target_failure",
                 "appearance_paint_selected_emissive_render_target",
                 "appearance_source_status",
                 "appearance_source_reason",
                 "appearance_match_status",
                 "appearance_match_reason",
                 "appearance_fallback_color_mode",
                 "appearance_emission_isolation_probe",
                 "appearance_emission_isolation_target_visible",
                 "appearance_emission_isolation_probe_complete",
                 "appearance_emission_isolation_probe_ok",
                 "appearance_production_connected",
                 "appearance_direct_stroke_count",
                 "appearance_capture_final_hdr_ok",
                 "appearance_capture_intrinsic_emission_ok",
                 "appearance_capture_intrinsic_emission_failure",
                 "appearance_capture_final_ldr_ok",
                 "appearance_capture_tone_curve_ok",
                 "appearance_capture_normal_ok",
                 "appearance_capture_depth_ok",
                 "appearance_manual_color_mode",
                 "appearance_manual_ldr_capture_ok",
                 "appearance_manual_ldr_capture_failure",
                 "appearance_manual_camera_stable",
                 "appearance_manual_paired_samples",
                 "appearance_manual_base_color_samples",
                 "appearance_manual_final_ldr_samples",
                 "paint_source_color_space",
                 "appearance_manual_preview_skipped",
                 "appearance_manual_preview_skip_reason",
                 "appearance_manual_color_feedback_requested",
                 "appearance_manual_color_feedback_applied",
                 "appearance_manual_feedback_kept_clusters",
                 "appearance_manual_feedback_excluded_emission_samples",
                 "appearance_manual_fixed_metallic",
                 "appearance_manual_fixed_roughness",
                 "appearance_manual_fixed_emissive",
                 "appearance_source_component_resolution",
                 "appearance_source_component_count",
                 "appearance_source_material_count",
                 "appearance_source_query_available",
                 "appearance_source_query_function_available",
                 "appearance_source_query_params_size",
                 "appearance_source_query_schema",
                 "appearance_source_query_result_schema",
                 "appearance_source_query_with_filter_available",
                 "appearance_source_query_with_filter_schema",
                 "appearance_source_query_filter_schema",
                 "appearance_source_query_with_filter_result_schema",
                 "appearance_source_query_core_threshold",
                 "appearance_source_query_probe_count",
                 "appearance_source_query_hits",
                 "appearance_source_query_target_component_hits",
                 "appearance_source_query_target_actor_hits",
                 "appearance_source_query_other_hits",
                 "appearance_source_query_core_hits",
                 "appearance_source_query_halo_hits",
                 "appearance_source_query_unique_components",
                 "appearance_source_query_component_summary",
                 "appearance_source_query_material_hits",
                 "appearance_source_query_unique_materials",
                 "appearance_source_query_material_summary",
                 "appearance_source_parameter_prior_count",
                 "appearance_source_parameter_prior",
                 "appearance_source_parameter_prior_used",
                 "appearance_screen_hit_uv_enabled",
                 "appearance_screen_hit_uv_attempted",
                 "appearance_screen_hit_uv_function_available",
                 "appearance_screen_hit_result_schema_ok",
                 "appearance_screen_hit_result_return_offset",
                 "appearance_screen_hit_result_return_size",
                 "appearance_screen_hit_result_success_offset",
                 "appearance_screen_hit_result_uv_offset",
                 "appearance_screen_hit_result_world_offset",
                 "appearance_screen_hit_result_normal_offset",
                 "appearance_screen_hit_uv_candidates",
                 "appearance_screen_hit_uv_projected",
                 "appearance_screen_hit_uv_inside_view",
                 "appearance_screen_hit_uv_calls_ok",
                 "appearance_screen_hit_uv_hits",
                 "appearance_screen_hit_uv_accepted",
                 "appearance_screen_hit_uv_rejected_world_mismatch",
                 "appearance_runtime_triangle_uv_used",
                 "appearance_screen_hit_uv_changed_from_runtime_uv",
                 "appearance_screen_hit_uv_delta_mean",
                 "appearance_screen_hit_uv_delta_max",
                 "appearance_screen_hit_world_delta_samples",
                 "appearance_screen_hit_world_delta_mean_cm",
                 "appearance_screen_hit_world_delta_max_cm",
                 "appearance_screen_hit_world_delta_over_1cm",
                 "appearance_screen_hit_world_delta_over_5cm",
                 "appearance_screen_hit_world_delta_over_10cm",
                 "appearance_screen_hit_cached_world_delta_samples",
                 "appearance_screen_hit_cached_world_delta_mean_cm",
                 "appearance_screen_hit_cached_world_delta_max_cm",
                 "appearance_rebuilt_cached_world_delta_samples",
                 "appearance_rebuilt_cached_world_delta_mean_cm",
                 "appearance_rebuilt_cached_world_delta_max_cm",
                 "appearance_screen_hit_uv_elapsed_ms",
                 "appearance_screen_hit_uv_failure",
                 "appearance_capture_destination_samples",
                 "base_capture_projection_samples",
                 "base_capture_source_candidates",
                 "appearance_capture_destination_samples_front",
                 "appearance_capture_destination_samples_side",
                 "appearance_capture_destination_samples_back",
                 "appearance_brush_visual_actor",
                 "appearance_brush_visual_component_count",
                 "appearance_brush_visual_component_summary",
                 "appearance_brush_visual_hidden_requested",
                 "appearance_brush_visual_hidden_count",
                 "appearance_brush_visual_hide_failures",
                 "appearance_capture_occluded_projection_kept",
                 "appearance_capture_final_hdr_failure",
                 "appearance_isolation_backend",
                 "appearance_show_flags_requested",
                 "appearance_show_flags_schema_ok",
                 "appearance_show_flags_process_ok",
                 "appearance_show_flags_readback_ok",
                 "appearance_show_flags_requested_count",
                 "appearance_show_flags_applied_count",
                 "appearance_show_flags_mismatch_count",
                 "appearance_show_flags_failure",
                 "appearance_show_flags_requested_names",
                 "appearance_show_flags_readback_names",
                 "appearance_isolated_sample_count",
                 "appearance_isolation_target_visible",
                 "appearance_isolation_energy_mean",
                 "appearance_isolation_energy_max",
                 "appearance_isolation_energy_p95",
                 "appearance_isolation_noise_model_ok",
                 "appearance_isolation_noise_median",
                 "appearance_isolation_noise_mad",
                 "appearance_isolation_noise_floor",
                 "appearance_isolation_effective_backend",
                 "appearance_isolation_residual_paired_samples",
                 "appearance_isolation_residual_isolated_samples",
                 "appearance_isolation_residual_mean",
                 "appearance_isolation_residual_max",
                 "appearance_isolation_residual_p95",
                 "appearance_isolation_residual_noise_model_ok",
                 "appearance_isolation_residual_noise_median",
                 "appearance_isolation_residual_noise_mad",
                 "appearance_isolation_residual_noise_floor",
                 "appearance_e0_isolation_ok",
                 "appearance_e0_camera_stable",
                 "appearance_e0_isolation_paired_samples",
                 "appearance_e0_noise_median",
                 "appearance_e0_noise_mad",
                 "appearance_e0_noise_floor",
                 "appearance_source_emission_noise_median",
                 "appearance_source_emission_noise_mad",
                 "appearance_source_emission_noise_floor",
                 "appearance_source_emission_signal_separated",
                 "appearance_source_emission_baseline_samples",
                 "appearance_effective_emission_noise_floor",
                 "appearance_emission_surface_filter_applied",
                 "appearance_emission_surface_filter_regions",
                 "appearance_emission_core_samples",
                 "appearance_emission_core_surface_count",
                 "appearance_emission_halo_rejected_samples",
                 "appearance_emission_core_threshold",
                 "appearance_emission_roi_samples",
                 "appearance_non_emission_samples",
                 "appearance_emission_roi_count",
                 "appearance_supported_samples_front",
                 "appearance_supported_samples_side",
                 "appearance_supported_samples_back",
                 "appearance_emission_roi_samples_front",
                 "appearance_emission_roi_samples_side",
                 "appearance_emission_roi_samples_back",
                 "appearance_non_emission_samples_front",
                 "appearance_non_emission_samples_side",
                 "appearance_non_emission_samples_back",
                 "appearance_paint_uv_valid_samples_front",
                 "appearance_paint_uv_valid_samples_side",
                 "appearance_paint_uv_valid_samples_back",
                 "appearance_paint_uv_invalid_samples_front",
                 "appearance_paint_uv_invalid_samples_side",
                 "appearance_paint_uv_invalid_samples_back",
                 "appearance_emission_roi_nonzero_b_samples",
                 "appearance_non_emission_nonzero_b_samples",
                 "appearance_emission_recall",
                 "appearance_emission_false_positive_rate",
                 "appearance_emission_roi_loss_initial",
                 "appearance_emission_roi_loss_best",
                 "appearance_non_emission_loss_initial",
                 "appearance_non_emission_loss_best",
                 "appearance_non_emission_loss_delta",
                 "appearance_preview_packed_b_verified",
                 "appearance_preview_packed_b_failures",
                 "appearance_preview_expected_emissive_nonzero_pixels",
                 "appearance_preview_actual_emissive_nonzero_pixels",
                 "appearance_preview_initial_emissive_nonzero_pixels",
                 "appearance_preview_painted_emissive_nonzero_pixels",
                 "preview_packed_pbr_import_verified",
                 "preview_packed_pbr_bytes_mismatch",
                 "preview_emissive_bytes_mismatch",
                 "preview_expected_emissive_nonzero_pixels",
                 "preview_actual_emissive_nonzero_pixels",
                 "preview_initial_emissive_nonzero_pixels",
                 "preview_painted_emissive_nonzero_pixels",
                 "appearance_final_nonzero_emissive_pixels",
                 "appearance_final_painted_nonzero_emissive_pixels",
                 "preview_expected_packed_pbr_hash",
                 "preview_actual_packed_pbr_hash",
                 "preview_snapshot_reused",
                 "preview_pixels_changed",
                 "appearance_isolation_artifact_written",
                 "appearance_isolation_artifact_paired_samples",
                 "appearance_isolation_artifact_above_threshold",
                 "appearance_isolation_artifact_path",
                 "appearance_isolation_artifact_schema",
                 "appearance_diagnostic_samples_requested",
                 "appearance_sample_artifact_written",
                 "appearance_sample_artifact_failure",
                 "appearance_sample_artifact_samples",
                 "appearance_sample_artifact_path",
                 "appearance_sample_artifact_schema",
                 "appearance_capture_elapsed_ms",
                 "appearance_capture_call_count",
                 "appearance_capture_sparse_call_count",
                 "appearance_capture_materialized_pixels",
                 "appearance_capture_pool_created",
                 "appearance_capture_pool_reused",
                 "appearance_capture_pool_resources",
                 "appearance_capture_total_elapsed_ms",
                 "appearance_capture_setup_elapsed_ms",
                 "appearance_capture_settle_elapsed_ms",
                 "appearance_capture_projection_elapsed_ms",
                 "appearance_capture_projection_diagnostic_samples",
                 "appearance_capture_readback_elapsed_ms",
                 "appearance_source_query_elapsed_ms",
                 "appearance_sample_artifact_elapsed_ms",
                 "appearance_preprocessing_timed_out",
                 "appearance_clusters",
                 "appearance_supported_samples",
                 "appearance_fallback_samples",
                 "appearance_projection_occluded_samples",
                 "appearance_invalid_samples",
                 "appearance_source_camera_stable",
                 "appearance_camera_stable",
                 "appearance_hdr_clipped_pixels",
                 "appearance_preview_count",
                 "appearance_iterations",
                 "appearance_refinement_skipped",
                 "appearance_refinement_reason",
                 "appearance_best_observed_emission_roi_loss",
                 "appearance_loss_initial",
                 "appearance_fallback_median_delta_e",
                 "appearance_loss_best",
                 "appearance_best_median_delta_e",
                 "appearance_emissive_probe_loss",
                 "appearance_emissive_probe_median_delta_e",
                 "appearance_emissive_probe_improvement",
                 "appearance_seed_loss",
                 "appearance_seed_median_delta_e",
                 "appearance_seed_emissive_mean",
                 "appearance_seed_emissive_max",
                 "appearance_calibrated_loss",
                 "appearance_calibrated_median_delta_e",
                 "appearance_calibrated_emissive_mean",
                 "appearance_calibrated_emissive_max",
                 "appearance_calibrated_responsive_samples",
                 "appearance_calibrated_eligible_samples",
                 "appearance_calibrated_active_clusters",
                 "appearance_response_supported_emission_clusters",
                 "appearance_response_rejected_emission_clusters",
                 "appearance_response_supported_emission_samples",
                 "appearance_response_rejected_emission_samples",
                 "appearance_calibrated_source_albedo_loss",
                 "appearance_calibrated_source_albedo_median_delta_e",
                 "appearance_calibrated_cluster_mix_loss",
                 "appearance_calibrated_cluster_mix_median_delta_e",
                 "appearance_calibrated_cluster_mix_source_clusters",
                 "appearance_albedo_endpoint_loss",
                 "appearance_albedo_endpoint_median_delta_e",
                 "appearance_albedo_endpoint_median_chromaticity_delta",
                 "appearance_albedo_endpoint_max_chromaticity_delta",
                 "appearance_non_emission_albedo_loss",
                 "appearance_non_emission_albedo_median_delta_e",
                 "appearance_non_emission_albedo_roi_loss",
                 "appearance_non_emission_albedo_median_chromaticity_delta",
                 "appearance_non_emission_albedo_max_chromaticity_delta",
                 "appearance_feedback_albedo_samples",
                 "appearance_feedback_albedo_channels",
                 "appearance_feedback_albedo_mean_adjustment",
                 "appearance_calibrated_albedo_responsive_samples",
                 "appearance_calibrated_albedo_active_clusters",
                 "appearance_calibrated_albedo_blend_mean",
                 "appearance_calibrated_albedo_blend_min",
                 "appearance_calibrated_albedo_blend_max",
                 "appearance_cluster_local_loss",
                 "appearance_cluster_local_median_delta_e",
                 "appearance_cluster_local_emissive_mean",
                 "appearance_cluster_local_emissive_max",
                 "appearance_cluster_local_candidate_samples",
                 "appearance_cluster_local_active_clusters",
                 "appearance_cluster_local_seed_clusters",
                 "appearance_cluster_local_endpoint_clusters",
                 "appearance_best_emissive_mean",
                 "appearance_best_emissive_max",
                 "appearance_cluster_diagnostics",
                 "appearance_emissive_probe_preview_hash",
                 "appearance_emissive_probe_preview_pixels_changed",
                 "appearance_restore_verified",
                 "appearance_preview_target_capture_ok",
                 "appearance_preview_target_settle_ms",
                 "appearance_preview_target_capture_failure",
                 "appearance_preview_paired_samples",
                 "appearance_preview_evaluation_reason",
                 "appearance_cpu_worker_count",
                 "appearance_cpu_parallel",
                 "appearance_kmeans_elapsed_ms",
                 "appearance_evaluation_elapsed_ms",
                 "appearance_present_capture_ok",
                 "appearance_present_capture_samples",
                 "appearance_present_capture_failure",
                 "appearance_research_target_sample_artifacts",
                 "appearance_research_present_sample_artifacts",
                 "appearance_preview_snapshot_discarded",
                 "appearance_elapsed_ms",
                 "preview_request_elapsed_ms",
                 "preview_snapshot_export_elapsed_ms",
                 "preprocessing_ms",
                 "setup_ms",
                 "pose_ms",
                 "capture_ms",
                 "sample_ms",
                 "appearance_preview_failure",
                 "appearance_match_stroke_samples",
                 "appearance_emissive_stroke_samples",
                 "appearance_match_stroke_samples_front",
                 "appearance_match_stroke_samples_side",
                 "appearance_match_stroke_samples_back",
                 "appearance_emissive_stroke_samples_front",
                 "appearance_emissive_stroke_samples_side",
                 "appearance_emissive_stroke_samples_back",
                 "appearance_stroke_emissive_mean",
                 "appearance_stroke_emissive_max",
                 "appearance_source_emissive_candidate_samples",
                 "appearance_source_emissive_painted_samples",
                 "appearance_source_emissive_missed_samples",
                 "appearance_source_emissive_recall",
                 "diagnostic_strokes_before_limit",
                 "diagnostic_strokes_after_limit",
                 "diagnostic_stroke_limit_applied",
                 "local_stroke_calls",
                 "local_paint_rpc",
                 "first_stroke_target_channel",
                 "first_stroke_metallic",
                 "first_stroke_roughness",
                 "first_stroke_emissive",
                 "total_strokes",
                 "paint_strokes_submitted",
                 "paint_strokes_rendered",
                 "paint_strokes_completed",
                 "local_visual_sync_used",
                 "local_visual_sync_elapsed_ms",
                 "local_elapsed_ms",
                 "local_batch_calls",
                 "local_stroke_success",
                 "local_stroke_failures",
                 "local_strokes_synced",
                 "local_strokes_total",
                 "local_apply_route",
                 "game_main_module_size",
                 "game_text_identity_available",
                 "game_text_raw_size",
                 "game_text_fnv1a64",
                 "native_queue_backpressure",
                 "native_queue_component_counter_available",
                 "native_queue_component_available",
                 "native_queue_recorded_last_strokes",
                 "native_queue_component_last_strokes",
                 "native_queue_global_last_strokes",
                 "native_queue_pressure_last_strokes",
                 "native_queue_outgoing_available",
                 "native_queue_outgoing_observed_activity",
                 "native_queue_outgoing_last_strokes",
                 "native_queue_max_strokes_per_tick",
                 "native_queue_target_strokes",
                 "native_queue_component_peak_strokes",
                 "native_queue_outgoing_peak_strokes",
                 "native_queue_samples",
                 "native_queue_waits",
                 "native_queue_final_idle_polls",
                 "native_queue_final_confirmation_waits",
                 "replication_max_outgoing_batches_per_second",
                 "replication_max_outgoing_strokes_per_batch",
                 "replication_conservative_network_strokes_per_second",
                 "replication_conservative_receiver_strokes_per_second",
                 "replication_effective_strokes_per_second",
                 "replication_conservative_strokes_per_window",
                 "replication_network_window_ms",
                 "replication_final_confirmation_ms",
                 "replication_reported_render_target_writes_per_frame",
                 "final_queue_ready",
                 "final_queue_ready_elapsed_ms",
                 "final_queue_ready_resets",
                 "final_visual_completion_confirmed",
                 "local_apply_calls_returned",
                 "local_apply_calls_validated",
                 "local_apply_call_exceptions",
                 "local_apply_preflight_complete",
                 "local_apply_preflight_strokes_validated",
                 "local_texture_import_started",
                 "local_texture_import_ok",
                 "local_texture_import_export_ok",
                 "local_texture_import_import_ok",
                 "local_texture_import_incremental_enabled",
                 "local_texture_import_calls",
                 "local_texture_import_pacing_ms",
                 "local_texture_import_chunk_limit",
                 "local_texture_import_texture_size",
                 "local_texture_import_source_bytes",
                 "local_texture_import_strokes_considered",
                 "local_texture_import_strokes_painted",
                 "local_texture_import_pixels_touched",
                 "local_texture_import_pixels_changed",
                 "local_texture_import_compose_elapsed_ms",
                 "local_texture_import_channel_elapsed_ms",
                 "local_texture_import_elapsed_ms",
                 "local_texture_import_failure",
                 "local_render_target_write_budget",
                 "local_render_target_writes_scheduled",
                 "local_write_budget_yields",
                 "local_cpu_budget_us",
                 "local_cpu_budget_yields",
                 "local_common_call_total_ms",
                 "local_common_call_max_ms",
                 "local_dispatch_total_ms",
                 "local_dispatch_max_ms",
                 "local_apply_preflight_failure",
                 "local_apply_postcondition",
                 "local_apply_completion_semantics",
                 "local_apply_cpu_submission_ok",
                 "paint_dispatch_exception_stage",
                 "paint_dispatch_exception_stage_id",
                 "paint_dispatch_exception_kind",
                 "paint_dispatch_exception_code",
                 "paint_dispatch_exception_address",
                 "paint_dispatch_exception_instruction_address",
                 "paint_dispatch_exception_parameter_count",
                 "paint_dispatch_exception_access_operation",
                 "paint_dispatch_exception_fault_address",
                 "paint_dispatch_exception_instruction_module",
                 "paint_dispatch_exception_module_offset",
                 "paint_dispatch_exception_in_bridge",
                 "paint_dispatch_exception_restore_attempted",
                 "paint_dispatch_exception_restore_verified",
                 "paint_dispatch_exception_restore_failure",
                 "paint_dispatch_exception_restore_code",
                 "partial_commit",
                 "automatic_retry_safe",
                 "total_replay_elapsed_ms",
                 "paint_elapsed_ms",
                 "paint_eta_ms",
                 "replay_pass_order",
                 "replay_region_order",
                 "replay_fill_end",
                 "replay_paint_begin",
                 "replay_local_offset",
                 "replay_visual_offset",
                 "replay_local_current_pass",
                 "replay_local_current_pass_start",
                 "replay_local_current_pass_end",
                 "replay_current_pass",
                 "replay_current_pass_start",
                 "replay_current_pass_end",
                 "replay_progress_source",
                 "replay_current_pass_completed",
                 "replay_current_pass_total",
                 "replay_current_pass_eta_ms",
                 "replay_effective_fill_end",
                 "replay_effective_paint_begin",
                 "replay_strokes_fill",
                 "replay_strokes_paint",
                 "replay_strokes_fill_candidates",
                 "replay_strokes_fill_deduplicated",
                 "replay_strokes_paint_candidates",
                 "replay_strokes_paint_deduplicated",
                 "replay_strokes_front_paint",
                 "replay_strokes_side_paint",
                 "replay_strokes_back_paint",
                 "replay_strokes_front_fill",
                 "replay_strokes_side_fill",
                 "replay_strokes_back_fill",
                 "color_compression_tolerance",
                 "color_compression_requested",
                 "color_compression_enabled",
                 "color_compression_disabled_reason",
                 "color_compression_expanded_strokes",
                 "color_compression_skipped_strokes",
                 "adaptive_plan_ms",
                 "adaptive_plan_worker_count",
                 "adaptive_plan_parallel",
                 "adaptive_plan_avx2_available",
                 "adaptive_plan_avx2_used",
                 "replay_candidate_elapsed_ms",
                 "replay_materialize_elapsed_ms",
                 "source_assignment_elapsed_ms",
                 "source_assignment_worker_count",
                 "source_assignment_parallel",
                 "replay_spatial_order",
                 "replay_spatial_projection_backend",
                 "replay_spatial_cached_projection_candidates",
                 "replay_spatial_live_projection_calls",
                 "replay_spatial_current_view_vertical_min",
                 "replay_spatial_current_view_vertical_max",
                 "replay_spatial_current_view_projection_fallback_used",
                 "replay_spatial_current_view_projection_fallback_candidates",
                 "replay_spatial_fallback_order",
                 "replay_spatial_camera_right_available",
                 "replay_spatial_camera_up_available",
                 "replay_spatial_sort_partitions",
                 "replay_spatial_order_violations",
                 "replay_spatial_sort_elapsed_ms",
                 "progress_updates_written",
                 "progress_updates_suppressed",
                 "replay_pass_boundary_limited_batches",
                 "progress_min_interval_ms",
                 "first_failure",
                 "pid",
                 "port",
                 "cancel_reason",
                 "cancel_after_submission",
                 "queued_rendering_drained",
                 "rendering_remaining_strokes",
                 "paint_strokes_remaining_total",
                 "research_artifacts_requested",
                 "appearance_capture_artifacts_requested",
             })
        {
            append_metadata_field(out, metadata, key);
        }
        return out.empty() ? metadata : out;
    }

    auto compact_mesh_progress_metadata(const std::string& metadata) -> std::string
    {
        std::string out;
        for (const char* key : {
                 "progress_schema_version",
                 "phase",
                 "terminal",
                 "result",
                 "appearance_match",
                 "capture_pass",
                 "appearance_match_status",
                 "appearance_match_reason",
                 "appearance_paint_emissive_supported",
                 "appearance_paint_emissive_reason",
                 "appearance_preview_count",
                 "appearance_iterations",
                 "appearance_refinement_skipped",
                 "appearance_refinement_reason",
                 "appearance_loss_initial",
                 "appearance_loss_best",
                 "appearance_camera_stable",
                 "appearance_restore_verified",
                 "appearance_elapsed_ms",
                 "paint_elapsed_ms",
                 "preview_request_elapsed_ms",
                 "preview_snapshot_export_elapsed_ms",
                 "preprocessing_ms",
                 "setup_ms",
                 "pose_ms",
                 "capture_ms",
                 "sample_ms",
                 "adaptive_plan_ms",
                 "replay_materialize_elapsed_ms",
                 "paint_eta_ms",
                 "replay_pass_order",
                 "replay_fill_end",
                 "replay_paint_begin",
                 "replay_current_pass",
                 "replay_current_pass_start",
                 "replay_current_pass_end",
                 "replay_progress_source",
                 "replay_current_pass_completed",
                 "replay_current_pass_total",
                 "replay_current_pass_eta_ms",
                 "cancel_requested",
                 "cancel_reason",
                 "failure_stage",
                 "first_failure",
                 "remaining_strokes",
                 "cancel_phase",
                 "local_strokes_synced",
                 "local_strokes_submitted",
                 "native_queue_backpressure",
                 "native_queue_component_counter_available",
                 "native_queue_component_available",
                 "native_queue_recorded_last_strokes",
                 "native_queue_component_last_strokes",
                 "native_queue_global_last_strokes",
                 "native_queue_pressure_last_strokes",
                 "native_queue_outgoing_available",
                 "native_queue_outgoing_observed_activity",
                 "native_queue_outgoing_last_strokes",
                 "native_queue_max_strokes_per_tick",
                 "native_queue_target_strokes",
                 "native_queue_component_peak_strokes",
                 "native_queue_outgoing_peak_strokes",
                 "native_queue_samples",
                 "native_queue_waits",
                 "native_queue_final_idle_polls",
                 "native_queue_final_confirmation_waits",
                 "replication_max_outgoing_batches_per_second",
                 "replication_max_outgoing_strokes_per_batch",
                 "replication_conservative_network_strokes_per_second",
                 "replication_conservative_receiver_strokes_per_second",
                 "replication_effective_strokes_per_second",
                 "replication_conservative_strokes_per_window",
                 "replication_network_window_ms",
                 "replication_final_confirmation_ms",
                 "replication_reported_render_target_writes_per_frame",
                 "final_queue_ready",
                 "final_queue_ready_elapsed_ms",
                 "final_queue_ready_resets",
                 "final_visual_completion_confirmed",
                 "replay_local_offset",
                 "replay_visual_offset",
                 "paint_eta_source",
                 "local_apply_calls_validated",
                 "local_apply_call_exceptions",
                 "local_apply_preflight_complete",
                 "local_apply_preflight_strokes_validated",
                 "local_texture_import_started",
                 "local_texture_import_ok",
                 "local_texture_import_export_ok",
                 "local_texture_import_import_ok",
                 "local_texture_import_incremental_enabled",
                 "local_texture_import_calls",
                 "local_texture_import_pacing_ms",
                 "local_texture_import_chunk_limit",
                 "local_texture_import_texture_size",
                 "local_texture_import_source_bytes",
                 "local_texture_import_strokes_considered",
                 "local_texture_import_strokes_painted",
                 "local_texture_import_pixels_touched",
                 "local_texture_import_pixels_changed",
                 "local_texture_import_compose_elapsed_ms",
                 "local_texture_import_channel_elapsed_ms",
                 "local_texture_import_elapsed_ms",
                 "local_texture_import_failure",
                 "local_batch_limit",
                 "local_batch_pacing_ms",
                 "local_render_target_write_budget",
                 "local_render_target_writes_scheduled",
                 "local_write_budget_yields",
                 "local_cpu_budget_us",
                 "local_cpu_budget_yields",
                 "local_common_call_total_ms",
                 "local_common_call_max_ms",
                 "local_dispatch_total_ms",
                 "local_dispatch_max_ms",
                 "progress_updates_written",
                 "progress_updates_suppressed",
                 "progress_min_interval_ms",
                 "local_apply_preflight_failure",
                 "local_apply_postcondition",
                 "local_apply_completion_semantics",
                 "local_apply_cpu_submission_ok",
                 "partial_commit",
                 "automatic_retry_safe",
             })
        {
            append_metadata_field(out, metadata, key);
        }
        return out.empty() ? metadata : out;
    }

    auto response_json(bool success,
                       const char* stage,
                       int applied,
                       int failures,
                       const std::string& message,
                       const std::string& metadata = "") -> std::string
    {
        std::string out = "{\"success\":";
        out += success ? "true" : "false";
        out += ",\"stage\":\"";
        out += stage;
        out += "\",\"applied\":";
        out += std::to_string(applied);
        out += ",\"failures\":";
        out += std::to_string(failures);
        out += ",\"message\":\"";
        out += json_escape(message);
        out += "\",\"timing_ms\":{},\"metadata\":{\"bridge\":\"meccha-direct-bridge-v2\"";
        if (!metadata.empty())
        {
            const auto sanitized = sanitize_json_non_finite_numbers(
                compact_mesh_response_metadata(metadata));
            out += ",";
            out += sanitized.text;
            if (sanitized.replacements > 0)
            {
                out += ",\"bridge_json_non_finite_replacements\":";
                out += std::to_string(sanitized.replacements);
                out += ",\"bridge_json_non_finite_first_key\":\"";
                out += json_escape(sanitized.first_key);
                out += "\"";
            }
        }
        out += "}}\n";
        return out;
    }
