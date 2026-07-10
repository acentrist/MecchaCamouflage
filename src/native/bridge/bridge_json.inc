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
                 "server_batch_limit",
                 "server_batch_limit_effective",
                 "server_batch_delay_ms",
                 "server_batch_rpc",
                 "server_send_custom_static_pacing",
                 "server_packed_paint_batch_used",
                 "server_packed_paint_batch_route",
                 "server_packed_paint_batch_ignored",
                 "replication_pacing_enabled",
                 "replication_pacing_requested_batch_limit",
                 "replication_pacing_resolved_batch_limit",
                 "replication_pacing_requested_delay_ms",
                 "replication_pacing_resolved_pacing_ms",
                 "replication_pacing_batch_limit",
                 "replication_pacing_delay_ms",
                 "replication_pacing_max_outgoing_strokes_per_batch",
                 "replication_pacing_max_outgoing_network_batches_per_second",
                 "replication_pacing_pressure_level",
                 "replication_pacing_backoff_count",
                 "replication_pacing_queue_wait_count",
                 "replication_pacing_queue_gate_limit",
                 "replication_pacing_batch_queue_gate_limit",
                 "replication_pacing_queue_drain_strokes_per_sec",
                 "replication_pacing_send_strokes_per_sec",
                 "replication_pacing_model_eta_ms",
                 "replication_queued_batch_count",
                 "replication_queued_stroke_count",
                 "replication_max_strokes_per_tick",
                 "replication_estimated_ticks_to_drain",
                 "replication_component_max_replicated_strokes_per_tick",
                 "replication_manager_max_replicated_strokes_per_tick",
                 "replication_manager_max_outgoing_strokes_per_batch",
                 "replication_manager_max_outgoing_network_batches_per_second",
                 "total_strokes",
                 "server_batch_calls",
                 "server_batch_success",
                 "server_batch_failures",
                 "server_strokes_sent",
                 "server_batch_elapsed_ms",
                 "server_elapsed_ms",
                 "local_visual_sync_used",
                 "local_visual_sync_elapsed_ms",
                 "local_elapsed_ms",
                 "local_batch_calls",
                 "local_stroke_success",
                 "local_stroke_failures",
                 "local_strokes_synced",
                 "local_strokes_total",
                 "total_replay_elapsed_ms",
                 "paint_elapsed_ms",
                 "paint_eta_ms",
                 "first_failure",
                 "pid",
                 "port",
                 "cancel_reason",
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
                 "paint_eta_ms",
                 "cancel_requested",
                 "cancel_reason",
                 "failure_stage",
                 "first_failure",
                 "remaining_strokes",
                 "cancel_phase",
                 "replication_pacing_enabled",
                 "replication_pacing_requested_batch_limit",
                 "replication_pacing_resolved_batch_limit",
                 "replication_pacing_requested_delay_ms",
                 "replication_pacing_resolved_pacing_ms",
                 "replication_pacing_batch_limit",
                 "replication_pacing_delay_ms",
                 "replication_pacing_max_outgoing_strokes_per_batch",
                 "replication_pacing_max_outgoing_network_batches_per_second",
                 "replication_pacing_pressure_level",
                 "replication_pacing_backoff_count",
                 "replication_pacing_queue_wait_count",
                 "replication_pacing_queue_gate_limit",
                 "replication_pacing_batch_queue_gate_limit",
                 "replication_pacing_queue_drain_strokes_per_sec",
                 "replication_pacing_send_strokes_per_sec",
                 "replication_pacing_model_eta_ms",
                 "replication_queued_batch_count",
                 "replication_queued_stroke_count",
                 "replication_max_strokes_per_tick",
                 "replication_estimated_ticks_to_drain",
                 "server_batch_rpc",
                 "server_packed_paint_batch_enabled",
                 "server_packed_paint_batch_use_relay",
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
        out += "\",\"timing_ms\":{},\"metadata\":{\"bridge\":\"runtime-bridge\"";
        if (!metadata.empty())
        {
            out += ",";
            out += compact_mesh_response_metadata(metadata);
        }
        out += "}}\n";
        return out;
    }
