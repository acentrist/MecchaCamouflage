#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "controller_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace meccha
{
    namespace
    {
        auto wide_to_utf8(const std::wstring& value) -> std::string
        {
            if (value.empty())
                return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
            if (size > 0)
                WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), size, nullptr, nullptr);
            return out;
        }

        auto utf8_to_wide(const std::string& value) -> std::wstring
        {
            if (value.empty())
                return {};
            const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
            std::wstring out(static_cast<std::size_t>(std::max(0, size)), L'\0');
            if (size > 0)
                MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), size);
            return out;
        }

        auto json_escape(const std::string& value) -> std::string
        {
            std::ostringstream out;
            for (unsigned char c : value)
            {
                switch (c)
                {
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    if (c < 0x20)
                        out << "\\u" << std::hex << static_cast<int>(c) << std::dec;
                    else
                        out << static_cast<char>(c);
                }
            }
            return out.str();
        }

        auto json_string(const std::string& value) -> std::string
        {
            return std::string("\"") + json_escape(value) + "\"";
        }

        auto read_text_file(const std::filesystem::path& path) -> std::string
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return {};
            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        auto write_text_file(const std::filesystem::path& path, const std::string& text) -> bool
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file)
                return false;
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(file);
        }

        auto extract_json_string(const std::string& text, const std::string& key) -> std::string
        {
            const std::string needle = std::string("\"") + key + "\":\"";
            const auto start = text.find(needle);
            if (start == std::string::npos)
                return {};
            std::string out;
            bool escape = false;
            for (std::size_t i = start + needle.size(); i < text.size(); ++i)
            {
                const char c = text[i];
                if (escape)
                {
                    switch (c)
                    {
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(c); break;
                    }
                    escape = false;
                    continue;
                }
                if (c == '\\')
                {
                    escape = true;
                    continue;
                }
                if (c == '"')
                    break;
                out.push_back(c);
            }
            return out;
        }

        auto extract_json_number(const std::string& text, const std::string& key, double fallback) -> double
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

        auto extract_json_bool(const std::string& text, const std::string& key, bool fallback) -> bool
        {
            const std::string needle = std::string("\"") + key + "\":";
            const auto start = text.find(needle);
            if (start == std::string::npos)
                return fallback;
            auto pos = start + needle.size();
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
                ++pos;
            if (text.compare(pos, 5, "false") == 0)
                return false;
            if (text.compare(pos, 4, "true") == 0)
                return true;
            return fallback;
        }

        auto clamp_double(double value, double min_value, double max_value) -> double
        {
            if (!std::isfinite(value))
                return min_value;
            return std::min(max_value, std::max(min_value, value));
        }

    }

    auto default_app_dir() -> std::filesystem::path
    {
        wchar_t buffer[32768]{};
        const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
        if (size > 0 && size < std::size(buffer))
            return std::filesystem::path(buffer) / L"MecchaCamouflage";
        return std::filesystem::temp_directory_path() / L"MecchaCamouflage";
    }

    auto config_path() -> std::filesystem::path
    {
        return default_app_dir() / L"config.json";
    }

    auto default_tuning() -> PaintTuning
    {
        return PaintTuning{};
    }

    void clamp_settings(AppSettings& settings)
    {
        settings.panel_width = std::max(1040.0f, settings.panel_width);
        settings.panel_height = std::max(600.0f, settings.panel_height);
        settings.opacity = static_cast<float>(clamp_double(settings.opacity, 0.35, 1.0));
        settings.tuning.stroke_size_texels = clamp_double(settings.tuning.stroke_size_texels, 1.0, 12.0);
        settings.tuning.coverage_step_texels = clamp_double(settings.tuning.coverage_step_texels, 1.0, 12.0);
        settings.tuning.side_source_max_uv = clamp_double(settings.tuning.side_source_max_uv, 0.001, 0.50);
        settings.tuning.front_back_source_max_uv = clamp_double(settings.tuning.front_back_source_max_uv, 0.001, 2.00);
        settings.tuning.metallic = clamp_double(settings.tuning.metallic, 0.0, 1.0);
        settings.tuning.roughness = clamp_double(settings.tuning.roughness, 0.0, 1.0);
        settings.tuning.server_batch_limit = std::max(1, std::min(1000000, settings.tuning.server_batch_limit));
        settings.tuning.server_batch_delay_ms = std::max(0, std::min(1000, settings.tuning.server_batch_delay_ms));
        if (settings.start_hotkey.empty())
            settings.start_hotkey = "F10";
        if (settings.stop_hotkey.empty())
            settings.stop_hotkey = "F9";
    }

    auto load_settings() -> AppSettings
    {
        AppSettings settings{};
        const std::string text = read_text_file(config_path());
        if (text.empty())
            return settings;

        const int layout_version = static_cast<int>(extract_json_number(text, "layout_version", 0.0));
        if (const auto process = extract_json_string(text, "game_process_name"); !process.empty())
            settings.game_process_name = utf8_to_wide(process);
        settings.panel_x = static_cast<float>(extract_json_number(text, "panel_x", settings.panel_x));
        settings.panel_y = static_cast<float>(extract_json_number(text, "panel_y", settings.panel_y));
        settings.panel_width = static_cast<float>(extract_json_number(text, "panel_width", settings.panel_width));
        settings.panel_height = static_cast<float>(extract_json_number(text, "panel_height", settings.panel_height));
        if (layout_version < settings.layout_version && settings.panel_width <= 1280.5f)
            settings.panel_width = 1040.0f;
        if (layout_version < settings.layout_version && settings.panel_height <= 660.5f)
            settings.panel_height = 600.0f;
        settings.always_on_top = extract_json_bool(text, "always_on_top", settings.always_on_top);
        settings.opacity = static_cast<float>(extract_json_number(text, "opacity", settings.opacity));
        if (const auto hotkey = extract_json_string(text, "start_hotkey"); !hotkey.empty())
            settings.start_hotkey = hotkey;
        else if (const auto legacy_hotkey = extract_json_string(text, "paint_hotkey"); !legacy_hotkey.empty())
            settings.start_hotkey = legacy_hotkey;
        if (const auto hotkey = extract_json_string(text, "stop_hotkey"); !hotkey.empty())
            settings.stop_hotkey = hotkey;
        settings.tuning.stroke_size_texels = extract_json_number(text, "stroke_size_texels", settings.tuning.stroke_size_texels);
        settings.tuning.coverage_step_texels = extract_json_number(text, "coverage_step_texels", settings.tuning.coverage_step_texels);
        settings.tuning.side_source_max_uv = extract_json_number(text, "side_source_max_uv", settings.tuning.side_source_max_uv);
        settings.tuning.front_back_source_max_uv = extract_json_number(text, "front_back_source_max_uv", settings.tuning.front_back_source_max_uv);
        settings.tuning.metallic = extract_json_number(text, "metallic", settings.tuning.metallic);
        settings.tuning.roughness = extract_json_number(text, "roughness", settings.tuning.roughness);
        if (layout_version < 20 && settings.tuning.roughness <= 0.000001)
            settings.tuning.roughness = default_tuning().roughness;
        settings.tuning.server_batch_limit = static_cast<int>(extract_json_number(text, "server_batch_limit", settings.tuning.server_batch_limit));
        settings.tuning.server_batch_delay_ms = static_cast<int>(extract_json_number(text, "server_batch_delay_ms", settings.tuning.server_batch_delay_ms));
        clamp_settings(settings);
        return settings;
    }

    auto save_settings(const AppSettings& input) -> bool
    {
        AppSettings settings = input;
        clamp_settings(settings);
        const std::string text = std::string("{\n") +
            "  \"layout_version\": " + std::to_string(settings.layout_version) + ",\n" +
            "  \"panel_x\": " + std::to_string(settings.panel_x) + ",\n" +
            "  \"panel_y\": " + std::to_string(settings.panel_y) + ",\n" +
            "  \"panel_width\": " + std::to_string(settings.panel_width) + ",\n" +
            "  \"panel_height\": " + std::to_string(settings.panel_height) + ",\n" +
            "  \"game_process_name\": " + json_string(wide_to_utf8(settings.game_process_name)) + ",\n" +
            "  \"always_on_top\": " + std::string(settings.always_on_top ? "true" : "false") + ",\n" +
            "  \"opacity\": " + std::to_string(settings.opacity) + ",\n" +
            "  \"start_hotkey\": " + json_string(settings.start_hotkey) + ",\n" +
            "  \"stop_hotkey\": " + json_string(settings.stop_hotkey) + ",\n" +
            "  \"stroke_size_texels\": " + std::to_string(settings.tuning.stroke_size_texels) + ",\n" +
            "  \"coverage_step_texels\": " + std::to_string(settings.tuning.coverage_step_texels) + ",\n" +
            "  \"side_source_max_uv\": " + std::to_string(settings.tuning.side_source_max_uv) + ",\n" +
            "  \"front_back_source_max_uv\": " + std::to_string(settings.tuning.front_back_source_max_uv) + ",\n" +
            "  \"server_batch_limit\": " + std::to_string(settings.tuning.server_batch_limit) + ",\n" +
            "  \"server_batch_delay_ms\": " + std::to_string(settings.tuning.server_batch_delay_ms) + ",\n" +
            "  \"metallic\": " + std::to_string(settings.tuning.metallic) + ",\n" +
            "  \"roughness\": " + std::to_string(settings.tuning.roughness) + "\n" +
            "}\n";
        const auto path = config_path();
        const auto tmp = std::filesystem::path(path.wstring() + L".tmp");
        if (!write_text_file(tmp, text))
            return false;
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec)
        {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(tmp, path, ec);
        }
        return !ec;
    }

}
