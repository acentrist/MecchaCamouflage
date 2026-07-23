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
                 "mesh_first_pipeline",
                 "preview_only",
                 "unpreview_only",
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
                 "brush_radius_texels",
                 "brush_radius_uv",
                 "fill_stroke_radius_texels",
                 "fill_stroke_radius_source",
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
                 "native_queue_max_strokes_per_tick",
                 "native_queue_target_strokes",
                 "native_queue_component_peak_strokes",
                 "native_queue_samples",
                 "native_queue_waits",
                 "native_queue_final_idle_polls",
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
                 "replay_spatial_order",
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
                 "paint_elapsed_ms",
                 "preprocessing_ms",
                 "pose_ms",
                 "capture_ms",
                 "sample_ms",
                 "adaptive_plan_ms",
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
                 "native_queue_max_strokes_per_tick",
                 "native_queue_target_strokes",
                 "native_queue_component_peak_strokes",
                 "native_queue_samples",
                 "native_queue_waits",
                 "native_queue_final_idle_polls",
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
        out += "\",\"timing_ms\":{},\"metadata\":{\"bridge\":\"meccha-direct-bridge-v1\"";
        if (!metadata.empty())
        {
            out += ",";
            out += compact_mesh_response_metadata(metadata);
        }
        out += "}}\n";
        return out;
    }
