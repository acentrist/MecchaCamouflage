#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace meccha
{
    struct RuntimeEvent
    {
        std::string timestamp{};
        std::string clock{};
        std::string level{};
        std::string domain{};
        std::string event{};
        std::string stage{};
        std::string title{};
        std::string message{};
        std::string details_json{"{}"};
        std::string run_id{};
        double progress{-1.0};
    };

    class RuntimeEventBuffer
    {
    public:
        void push(RuntimeEvent event);
        auto snapshot() const -> std::vector<RuntimeEvent>;

    private:
        mutable std::mutex mutex_;
        std::deque<RuntimeEvent> events_{};
        std::size_t max_events_{1000};
    };

    class TraceLogBuffer
    {
    public:
        void push(std::string domain, std::string message, std::string details = {});
        auto snapshot() const -> std::vector<std::string>;
        auto text() const -> std::string;

    private:
        mutable std::mutex mutex_;
        std::deque<std::string> lines_{};
        std::size_t max_lines_{1000};
    };

    auto now_utc_iso() -> std::string;
    auto clock_from_timestamp(const std::string& timestamp) -> std::string;
    auto make_runtime_event(const std::string& name,
                            const std::string& level,
                            const std::string& stage,
                            const std::string& message,
                            const std::string& details_json = "{}",
                            const std::string& run_id = "") -> RuntimeEvent;
    auto build_human_log_text(const std::vector<RuntimeEvent>& events,
                              bool show_info,
                              bool show_warning,
                              bool show_error,
                              bool apply_filter) -> std::string;
}
