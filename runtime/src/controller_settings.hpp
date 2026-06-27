#pragma once

#include <filesystem>
#include <string>

namespace meccha
{
    inline constexpr wchar_t DefaultGameProcessName[] = L"PenguinHotel-Win64-Shipping.exe";

    struct PaintTuning
    {
        double brush_radius{0.01};
        double brush_spacing{0.18};
        double server_brush_spacing{0.08};
        int server_batch_limit{50};
        int server_batch_delay_ms{300};
    };

    struct AppSettings
    {
        int layout_version{7};
        float panel_x{-1.0f};
        float panel_y{-1.0f};
        float panel_width{1280.0f};
        float panel_height{860.0f};
        int log_retention_days{14};
        std::wstring game_process_name{DefaultGameProcessName};
        bool always_on_top{true};
        float opacity{1.0f};
        std::string paint_hotkey{"F10"};
        PaintTuning tuning{};
        bool show_info{true};
        bool show_warning{true};
        bool show_error{true};
    };

    auto default_app_dir() -> std::filesystem::path;
    auto config_path() -> std::filesystem::path;
    auto default_tuning() -> PaintTuning;
    void clamp_settings(AppSettings& settings);
    auto load_settings() -> AppSettings;
    auto save_settings(const AppSettings& settings) -> bool;
}
