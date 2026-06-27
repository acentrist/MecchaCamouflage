#pragma once

#include "controller_settings.hpp"

#include <string>

namespace meccha
{
    struct UiRuntimeState
    {
        std::string target_process{};
        std::string process_name{};
        unsigned long pid{0};
        std::string bridge_state{};
        bool bridge_ready{false};
        bool app_editing{false};
        bool paint_editing{false};
        bool recording_hotkey{false};
        std::string hotkey_error{};
        std::string log_dir{};
    };

    struct UiActions
    {
        bool edit_app_clicked{false};
        bool cancel_app_clicked{false};
        bool save_app_clicked{false};
        bool reset_app_clicked{false};
        bool edit_paint_clicked{false};
        bool cancel_paint_clicked{false};
        bool save_paint_clicked{false};
        bool reset_paint_clicked{false};
        bool open_logs_clicked{false};
        bool copy_log_clicked{false};
        bool copy_trace_clicked{false};
        bool start_hotkey_recording{false};
        bool settings_changed{false};
    };

    void apply_meccha_theme();
    void load_meccha_fonts();
    void draw_app_ui(AppSettings& draft,
                     const AppSettings& persisted,
                     const UiRuntimeState& runtime,
                     const std::string& human_log_text,
                     const std::string& trace_log_text,
                     UiActions& actions);
}
