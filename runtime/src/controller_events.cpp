#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "controller_events.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace meccha
{
    namespace
    {
        auto lower_copy(std::string value) -> std::string
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        auto pretty_text(std::string text) -> std::string
        {
            std::replace(text.begin(), text.end(), '_', ' ');
            if (!text.empty())
                text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
            return text;
        }

        auto event_domain(const std::string& name, const std::string& stage) -> std::string
        {
            const auto lower = lower_copy(name + " " + stage);
            if (lower.find("paint") != std::string::npos || lower.find("template") != std::string::npos)
                return "paint";
            if (lower.find("bridge") != std::string::npos || lower.find("inject") != std::string::npos)
                return "bridge";
            if (lower.find("process") != std::string::npos || lower.find("game") != std::string::npos || lower.find("pawn") != std::string::npos)
                return "game";
            return "app";
        }

        auto extract_json_number(const std::string& text, const std::string& key, double fallback = 0.0) -> double
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

        auto level_tag(const RuntimeEvent& event) -> std::string
        {
            if (event.level == "error")
                return "ERROR";
            if (event.level == "warning")
                return "WARN";
            return "INFO";
        }

        auto progress_bar_text(double progress, int width = 26) -> std::string
        {
            progress = std::min(1.0, std::max(0.0, progress));
            const int filled = std::max(0, std::min(width, static_cast<int>(progress * static_cast<double>(width) + 0.5)));
            std::string bar;
            bar.reserve(static_cast<std::size_t>(width + 8));
            bar.push_back('[');
            for (int i = 0; i < width; ++i)
                bar.push_back(i < filled ? '#' : '-');
            bar += "] ";
            bar += std::to_string(static_cast<int>(progress * 100.0 + 0.5));
            bar.push_back('%');
            return bar;
        }

        auto passes_filter(const RuntimeEvent& event, bool show_info, bool show_warning, bool show_error) -> bool
        {
            if (event.level == "error")
                return show_error;
            if (event.level == "warning")
                return show_warning;
            return show_info;
        }

        auto format_human_line(const RuntimeEvent& event) -> std::string
        {
            const std::string prefix = event.clock + " " + level_tag(event) + " " + event.domain + " ";
            if (event.event == "paint_progress" && event.progress >= 0.0)
            {
                const std::string stage = event.stage.empty() ? "paint" : pretty_text(event.stage);
                return prefix + progress_bar_text(std::min(0.98, event.progress)) + " " + stage + " " + event.message;
            }
            if (event.event == "paint_waiting_commit")
                return prefix + "[applying] " + (event.message.empty() ? std::string("Applying paint...") : event.message);
            if (event.event == "paint_done")
                return prefix + progress_bar_text(1.0) + " Complete " + event.message;
            if (event.event == "paint_failed")
                return prefix + "[failed] " + event.message;
            return prefix + event.title + " " + event.message;
        }
    }

    void RuntimeEventBuffer::push(RuntimeEvent event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
        while (events_.size() > max_events_)
            events_.pop_front();
    }

    auto RuntimeEventBuffer::snapshot() const -> std::vector<RuntimeEvent>
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<RuntimeEvent>(events_.begin(), events_.end());
    }

    void TraceLogBuffer::push(std::string domain, std::string message, std::string details)
    {
        std::ostringstream line;
        line << clock_from_timestamp(now_utc_iso()) << " " << domain;
        if (!message.empty())
            line << "." << message;
        if (!details.empty())
            line << " " << details;
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(line.str());
        while (lines_.size() > max_lines_)
            lines_.pop_front();
    }

    auto TraceLogBuffer::snapshot() const -> std::vector<std::string>
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<std::string>(lines_.begin(), lines_.end());
    }

    auto TraceLogBuffer::text() const -> std::string
    {
        std::ostringstream out;
        for (const auto& line : snapshot())
            out << line << "\n";
        auto text = out.str();
        if (text.empty())
            text = "No raw trace events.\n";
        return text;
    }

    auto now_utc_iso() -> std::string
    {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03u+00:00",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return buffer;
    }

    auto clock_from_timestamp(const std::string& timestamp) -> std::string
    {
        return timestamp.size() >= 19 ? timestamp.substr(11, 8) : "--:--:--";
    }

    auto make_runtime_event(const std::string& name,
                            const std::string& level,
                            const std::string& stage,
                            const std::string& message,
                            const std::string& details_json,
                            const std::string& run_id) -> RuntimeEvent
    {
        RuntimeEvent event{};
        event.timestamp = now_utc_iso();
        event.clock = clock_from_timestamp(event.timestamp);
        event.level = level;
        event.domain = event_domain(name, stage);
        event.event = name;
        event.stage = stage;
        event.title = pretty_text(name);
        event.message = message;
        event.details_json = details_json.empty() ? "{}" : details_json;
        event.run_id = run_id;
        event.progress = name == "paint_progress" ? extract_json_number(event.details_json, "progress", -1.0) : -1.0;
        return event;
    }

    auto build_human_log_text(const std::vector<RuntimeEvent>& events,
                              bool show_info,
                              bool show_warning,
                              bool show_error,
                              bool apply_filter) -> std::string
    {
        std::ostringstream out;
        for (const auto& event : events)
        {
            if (apply_filter && !passes_filter(event, show_info, show_warning, show_error))
                continue;
            out << format_human_line(event) << "\n";
        }
        auto text = out.str();
        if (text.empty())
            text = "No matching events.\n";
        return text;
    }

}
