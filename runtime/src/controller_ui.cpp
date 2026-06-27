#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "controller_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <vector>

namespace meccha
{
    namespace
    {
        ImFont* g_heading_font = nullptr;
        ImFont* g_log_font = nullptr;
        constexpr int AppFontRegularResourceId = 202;
        constexpr int AppFontBoldResourceId = 203;
        constexpr int AppFontCondensedResourceId = 204;

        enum class UiIcon
        {
            Copy,
            Record,
        };

        auto icon_button(const char* id, UiIcon icon, const char* tooltip, ImVec2 size = ImVec2(28.0f, 28.0f)) -> bool;

        auto tone_color(const std::string& tone) -> ImVec4
        {
            if (tone == "error")
                return ImVec4(0.96f, 0.28f, 0.25f, 1.0f);
            if (tone == "warning")
                return ImVec4(0.95f, 0.72f, 0.31f, 1.0f);
            return ImVec4(0.62f, 0.78f, 0.95f, 1.0f);
        }

        auto status_color(const std::string& state) -> ImVec4
        {
            std::string lower = state;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("ready") != std::string::npos || lower.find("attached") != std::string::npos)
                return ImVec4(0.48f, 0.82f, 0.54f, 1.0f);
            if (lower.find("failed") != std::string::npos || lower.find("error") != std::string::npos)
                return ImVec4(0.96f, 0.28f, 0.25f, 1.0f);
            return ImVec4(0.95f, 0.72f, 0.31f, 1.0f);
        }

        void text_row(const char* label, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", value.c_str());
        }

        void path_row(const char* label, const std::string& value, bool& clicked)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.80f, 0.94f, 1.0f));
            ImGui::TextUnformatted(value.c_str());
            const bool hovered = ImGui::IsItemHovered();
            const bool item_clicked = ImGui::IsItemClicked();
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
            if (hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s", value.c_str());
            }
            if (item_clicked)
                clicked = true;
            ImGui::PopStyleColor();
        }

        void status_row(const char* label, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, status_color(value));
            ImGui::TextWrapped("%s", value.c_str());
            ImGui::PopStyleColor();
        }

        void section_header(const char* text)
        {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::AlignTextToFramePadding();
            if (g_heading_font)
                ImGui::PushFont(g_heading_font);
            ImGui::TextUnformatted(text);
            if (g_heading_font)
                ImGui::PopFont();
        }

        auto checkbox_control_width(const char* label) -> float
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
        }

        void same_line_right(float width)
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float target_x = ImGui::GetContentRegionMax().x - width;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x, target_x));
        }

        auto icon_button(const char* id, UiIcon icon, const char* tooltip, ImVec2 size) -> bool
        {
            const bool pressed = ImGui::Button(id, size);
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
            const float scale = std::min(size.x, size.y) / 28.0f;
            if (icon == UiIcon::Copy)
            {
                const ImVec2 a(center.x - 6.0f * scale, center.y - 4.0f * scale);
                const ImVec2 b(center.x + 4.0f * scale, center.y + 6.0f * scale);
                draw->AddRect(a, b, color, 1.5f * scale, 0, 1.5f * scale);
                draw->AddRect(ImVec2(a.x + 4.0f * scale, a.y - 4.0f * scale),
                              ImVec2(b.x + 4.0f * scale, b.y - 4.0f * scale),
                              color,
                              1.5f * scale,
                              0,
                              1.5f * scale);
            }
            else
            {
                draw->AddCircle(center, 6.0f * scale, color, 24, 1.8f * scale);
                draw->AddCircleFilled(center, 2.4f * scale, color, 16);
            }
            if (ImGui::IsItemHovered() && tooltip && *tooltip)
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        }

        void align_to_bottom_right(float width)
        {
            const float button_height = ImGui::GetFrameHeight();
            const float bottom_y = ImGui::GetWindowContentRegionMax().y - button_height;
            const float right_x = ImGui::GetWindowContentRegionMax().x - width;
            ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), bottom_y));
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right_x));
        }

        void settings_action_row(bool editing, bool& edit, bool& save, bool& cancel, bool& reset)
        {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float width = 86.0f;
            const float total_width = editing ? width * 3.0f + spacing * 2.0f : width;
            align_to_bottom_right(total_width);
            if (!editing)
            {
                if (ImGui::Button("Edit", ImVec2(width, 0.0f)))
                    edit = true;
                return;
            }
            if (ImGui::Button("Save", ImVec2(width, 0.0f)))
                save = true;
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(width, 0.0f)))
                cancel = true;
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2(width, 0.0f)))
                reset = true;
        }

        auto begin_section(const char* id, const char* title, const ImVec2& size = ImVec2(0.0f, 0.0f)) -> bool
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.035f, 0.035f, 0.038f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            const bool open = ImGui::BeginChild(id, size, true);
            if (open)
                section_header(title);
            return open;
        }

        void end_section()
        {
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }

        auto begin_form_table(const char* id, float label_width = 148.0f) -> bool
        {
            if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
                return false;
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, label_width);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            return true;
        }

        auto stable_id(const char* prefix, const char* label) -> std::string
        {
            return std::string("##") + prefix + label;
        }

        void input_double_setting(const char* label, double& value, double min_value, double max_value, const char* format, bool& changed)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            const float input_width = 98.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            float slider_value = static_cast<float>(value);
            const double before = value;
            ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - input_width - spacing));
            if (ImGui::SliderFloat(stable_id("Slider", label).c_str(),
                                   &slider_value,
                                   static_cast<float>(min_value),
                                   static_cast<float>(max_value),
                                   format,
                                   ImGuiSliderFlags_AlwaysClamp))
            {
                value = static_cast<double>(slider_value);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_width);
            if (ImGui::InputDouble(stable_id("Input", label).c_str(), &value, 0.0, 0.0, format, ImGuiInputTextFlags_EnterReturnsTrue))
                changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                value = std::min(max_value, std::max(min_value, value));
                changed = changed || before != value;
            }
        }

        void input_int_setting(const char* label, int& value, int min_value, int max_value, bool& changed)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            const float input_width = 98.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const int before = value;
            int slider_value = value;
            ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - input_width - spacing));
            if (ImGui::SliderInt(stable_id("Slider", label).c_str(), &slider_value, min_value, max_value, "%d", ImGuiSliderFlags_AlwaysClamp))
            {
                value = slider_value;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_width);
            if (ImGui::InputInt(stable_id("Input", label).c_str(), &value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                value = std::min(max_value, std::max(min_value, value));
                changed = changed || before != value;
            }
        }

        void input_opacity_setting(float& opacity, bool& changed)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Opacity");
            ImGui::TableSetColumnIndex(1);
            int opacity_percent = static_cast<int>(opacity * 100.0f + 0.5f);
            bool opacity_changed = false;
            const float input_width = 98.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - input_width - spacing));
            if (ImGui::SliderInt("##OpacitySlider", &opacity_percent, 35, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp))
                opacity_changed = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_width);
            if (ImGui::InputInt("##OpacityInput", &opacity_percent, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                opacity_changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
                opacity_changed = true;
            if (opacity_changed)
            {
                opacity_percent = std::min(100, std::max(35, opacity_percent));
                opacity = static_cast<float>(opacity_percent) / 100.0f;
                changed = true;
            }
        }

        struct LogCallbackState
        {
            bool scroll_to_end{false};
        };

        auto log_line_color(const std::string& line) -> ImVec4
        {
            if (line.find(" ERROR ") != std::string::npos)
                return tone_color("error");
            if (line.find(" WARN ") != std::string::npos)
                return tone_color("warning");
            return tone_color("info");
        }

        auto log_text_callback(ImGuiInputTextCallbackData* data) -> int
        {
            auto* state = static_cast<LogCallbackState*>(data->UserData);
            if (state && state->scroll_to_end)
            {
                const bool has_selection = data->SelectionStart != data->SelectionEnd;
                if (!has_selection)
                {
                    data->CursorPos = data->BufTextLen;
                    data->SelectionStart = data->BufTextLen;
                    data->SelectionEnd = data->BufTextLen;
                }
            }
            return 0;
        }

        void draw_colored_log_box(const char* id, const std::string& text, const ImVec2& size, std::size_t& previous_size)
        {
            const bool scroll_to_end = text.size() != previous_size;
            previous_size = text.size();
            if (g_log_font)
                ImGui::PushFont(g_log_font);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            if (ImGui::BeginChild(id, size, true, ImGuiWindowFlags_AlwaysVerticalScrollbar))
            {
                std::size_t start = 0;
                bool wrote_line = false;
                while (start < text.size())
                {
                    const std::size_t end = text.find('\n', start);
                    const std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!line.empty())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, log_line_color(line));
                        ImGui::TextWrapped("%s", line.c_str());
                        ImGui::PopStyleColor();
                        wrote_line = true;
                    }
                    if (end == std::string::npos)
                        break;
                    start = end + 1;
                }
                if (!wrote_line)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, tone_color("info"));
                    ImGui::TextUnformatted("No log events.");
                    ImGui::PopStyleColor();
                }
                if (scroll_to_end)
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            if (g_log_font)
                ImGui::PopFont();
        }

        void draw_log_box(const char* id, const std::string& text, const ImVec2& size, std::size_t& previous_size)
        {
            std::vector<char> buffer(text.begin(), text.end());
            buffer.push_back('\0');
            LogCallbackState callback_state{};
            callback_state.scroll_to_end = text.size() != previous_size;
            previous_size = text.size();
            if (g_log_font)
                ImGui::PushFont(g_log_font);
            ImGui::InputTextMultiline(id,
                                      buffer.data(),
                                      buffer.size(),
                                      size,
                                      ImGuiInputTextFlags_ReadOnly |
                                          ImGuiInputTextFlags_NoUndoRedo |
                                          ImGuiInputTextFlags_NoHorizontalScroll |
                                          ImGuiInputTextFlags_WordWrap |
                                          ImGuiInputTextFlags_CallbackAlways,
                                      log_text_callback,
                                      &callback_state);
            if (g_log_font)
                ImGui::PopFont();
        }

        auto add_embedded_font(int resource_id, float size) -> ImFont*
        {
            HMODULE module = GetModuleHandleW(nullptr);
            HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), MAKEINTRESOURCEW(10));
            if (!resource)
                return nullptr;
            HGLOBAL loaded = LoadResource(module, resource);
            if (!loaded)
                return nullptr;
            void* data = LockResource(loaded);
            const DWORD data_size = SizeofResource(module, resource);
            if (!data || data_size == 0)
                return nullptr;
            ImFontConfig config{};
            config.FontDataOwnedByAtlas = false;
            return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, static_cast<int>(data_size), size, &config);
        }
    }

    void apply_meccha_theme()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.ChildRounding = 8.0f;
        style.FrameRounding = 5.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 6.0f;
        style.WindowPadding = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(9.0f, 6.0f);
        style.ItemSpacing = ImVec2(9.0f, 7.0f);
        style.ItemInnerSpacing = ImVec2(7.0f, 5.0f);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 15.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.94f, 0.95f, 0.96f, 1.0f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.70f, 0.72f, 0.74f, 1.0f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        colors[ImGuiCol_Border] = ImVec4(0.145f, 0.145f, 0.150f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.055f, 0.055f, 0.060f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.090f, 0.092f, 0.100f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.120f, 0.125f, 0.135f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.090f, 0.092f, 0.100f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.145f, 0.150f, 0.165f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.190f, 0.200f, 0.220f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.075f, 0.078f, 0.085f, 1.0f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.120f, 0.125f, 0.140f, 1.0f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.160f, 0.170f, 0.190f, 1.0f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.88f, 0.92f, 0.98f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.78f, 0.84f, 0.94f, 1.0f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.94f, 0.96f, 1.0f, 1.0f);
        colors[ImGuiCol_Separator] = ImVec4(0.145f, 0.145f, 0.150f, 1.0f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.300f, 0.310f, 0.330f, 1.0f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.520f, 0.540f, 0.570f, 1.0f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.020f, 0.020f, 0.022f, 1.0f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.170f, 0.175f, 0.185f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.250f, 0.260f, 0.275f, 1.0f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.340f, 0.350f, 0.370f, 1.0f);
    }

    void load_meccha_fonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImFont* embedded_ui = add_embedded_font(AppFontRegularResourceId, 15.0f);
        if (embedded_ui)
        {
            io.FontDefault = embedded_ui;
            g_heading_font = add_embedded_font(AppFontBoldResourceId, 18.0f);
            g_log_font = add_embedded_font(AppFontCondensedResourceId, 15.5f);
            if (!g_heading_font)
                g_heading_font = embedded_ui;
            if (!g_log_font)
                g_log_font = embedded_ui;
            return;
        }
        const char* ui_fonts[] = {
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\SegoeUI.ttf",
        };
        for (const char* path : ui_fonts)
        {
            ImFont* ui_font = io.Fonts->AddFontFromFileTTF(path, 15.0f);
            if (ui_font)
            {
                io.FontDefault = ui_font;
                g_heading_font = io.Fonts->AddFontFromFileTTF(path, 18.0f);
                break;
            }
        }
        const char* mono_fonts[] = {
            "C:\\Windows\\Fonts\\consola.ttf",
            "C:\\Windows\\Fonts\\Consola.ttf",
        };
        for (const char* path : mono_fonts)
        {
            g_log_font = io.Fonts->AddFontFromFileTTF(path, 15.5f);
            if (g_log_font)
                break;
        }
    }

    void draw_app_ui(AppSettings& draft,
                     const AppSettings& persisted,
                     const UiRuntimeState& runtime,
                     const std::string& human_log_text,
                     const std::string& trace_log_text,
                     UiActions& actions)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings;
        if (!ImGui::Begin("MecchaCamouflageDesktop", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float content_height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
        const float total_width = ImGui::GetContentRegionAvail().x;
        const float pane_gap = style.ItemSpacing.x;
        const float left_width = std::max(1.0f, (total_width - pane_gap) * 0.5f);
        const float left_available_height = std::max(1.0f, content_height - style.ItemSpacing.y * 2.0f);
        const float info_block_height = std::max(220.0f, left_available_height * 0.27f);
        const float app_block_height = std::max(275.0f, left_available_height * 0.34f);
        const float paint_block_height = std::max(1.0f, left_available_height - info_block_height - app_block_height);

        if (ImGui::BeginChild("InfoPane", ImVec2(left_width, content_height), false))
        {
            if (begin_section("InfoBlock", "Info", ImVec2(0.0f, info_block_height)))
            {
                ImGui::Spacing();
                if (ImGui::BeginTable("InfoTable", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 148.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    text_row("Target", runtime.target_process);
                    text_row("Process", runtime.process_name.empty() ? "-" : runtime.process_name);
                    text_row("PID", std::to_string(runtime.pid));
                    status_row("Bridge", runtime.bridge_ready ? "Ready" : runtime.bridge_state);
                    path_row("Log dir", runtime.log_dir, actions.open_logs_clicked);
                    ImGui::EndTable();
                }
            }
            end_section();

            if (begin_section("AppSettingsBlock", "App Settings", ImVec2(0.0f, app_block_height)))
            {
                ImGui::Spacing();
                if (begin_form_table("AppSettingsTable"))
                {
                    bool always_on_top = runtime.app_editing ? draft.always_on_top : persisted.always_on_top;
                    float opacity = runtime.app_editing ? draft.opacity : persisted.opacity;
                    bool app_value_changed = false;
                    ImGui::BeginDisabled(!runtime.app_editing);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("Always on top");
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::Checkbox("##AlwaysOnTop", &always_on_top))
                        app_value_changed = true;

                    input_opacity_setting(opacity, app_value_changed);
                    ImGui::EndDisabled();

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted("Paint hotkey");
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Dummy(ImVec2(0.0f, 1.0f));
                    if (!runtime.app_editing)
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (g_heading_font)
                        ImGui::PushFont(g_heading_font);
                    ImGui::TextUnformatted((runtime.app_editing ? draft.paint_hotkey : persisted.paint_hotkey).c_str());
                    if (g_heading_font)
                        ImGui::PopFont();
                    if (!runtime.app_editing)
                        ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(!runtime.app_editing);
                    if (runtime.recording_hotkey)
                        ImGui::Button("Press a key...", ImVec2(132.0f, 0.0f));
                    else if (icon_button("##RecordHotkey", UiIcon::Record, "Record hotkey"))
                        actions.start_hotkey_recording = true;
                    ImGui::EndDisabled();
                    if (runtime.app_editing && app_value_changed)
                    {
                        draft.always_on_top = always_on_top;
                        draft.opacity = opacity;
                        actions.settings_changed = true;
                    }
                    ImGui::EndTable();
                }
                settings_action_row(runtime.app_editing,
                                    actions.edit_app_clicked,
                                    actions.save_app_clicked,
                                    actions.cancel_app_clicked,
                                    actions.reset_app_clicked);
            }
            end_section();

            if (begin_section("PaintSettingsBlock", "Paint Settings", ImVec2(0.0f, paint_block_height)))
            {
                ImGui::Spacing();
                if (begin_form_table("PaintSettingsTable"))
                {
                    PaintTuning tuning = runtime.paint_editing ? draft.tuning : persisted.tuning;
                    bool paint_value_changed = false;
                    ImGui::BeginDisabled(!runtime.paint_editing);
                    input_double_setting("Brush radius", tuning.brush_radius, 0.001, 0.05, "%.4f", paint_value_changed);
                    input_double_setting("Brush spacing", tuning.brush_spacing, 0.01, 0.5, "%.3f", paint_value_changed);
                    input_double_setting("Server spacing", tuning.server_brush_spacing, 0.01, 0.5, "%.3f", paint_value_changed);
                    input_int_setting("Batch limit", tuning.server_batch_limit, 1, 500, paint_value_changed);
                    input_int_setting("Batch delay ms", tuning.server_batch_delay_ms, 1, 1000, paint_value_changed);
                    ImGui::EndDisabled();
                    if (runtime.paint_editing && paint_value_changed)
                    {
                        draft.tuning = tuning;
                        actions.settings_changed = true;
                    }
                    ImGui::EndTable();
                }

                settings_action_row(runtime.paint_editing,
                                    actions.edit_paint_clicked,
                                    actions.save_paint_clicked,
                                    actions.cancel_paint_clicked,
                                    actions.reset_paint_clicked);
            }
            end_section();

        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("LogPane", ImVec2(0.0f, content_height), false))
        {
            static std::size_t previous_log_size = 0;
            static std::size_t previous_trace_size = 0;

            const float log_width = ImGui::GetContentRegionAvail().x;
            const float total_log_height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
            const float log_block_height = std::max(1.0f, (total_log_height - style.ItemSpacing.y) * 0.5f);

            if (begin_section("LogBlock", "Log", ImVec2(log_width, log_block_height)))
            {
                const float log_controls_width = checkbox_control_width("Info") +
                                                 checkbox_control_width("Warn") +
                                                 checkbox_control_width("Error") +
                                                 28.0f +
                                                 style.ItemSpacing.x * 4.0f;
                same_line_right(log_controls_width);
                ImGui::PushStyleColor(ImGuiCol_Text, tone_color("info"));
                if (ImGui::Checkbox("Info", &draft.show_info))
                    actions.settings_changed = true;
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, tone_color("warning"));
                if (ImGui::Checkbox("Warn", &draft.show_warning))
                    actions.settings_changed = true;
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, tone_color("error"));
                if (ImGui::Checkbox("Error", &draft.show_error))
                    actions.settings_changed = true;
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (icon_button("##CopyLog", UiIcon::Copy, "Copy log"))
                    actions.copy_log_clicked = true;
                ImGui::Spacing();
                draw_colored_log_box("##MainLog", human_log_text, ImGui::GetContentRegionAvail(), previous_log_size);
            }
            end_section();

            if (begin_section("TraceBlock", "Trace", ImVec2(log_width, log_block_height)))
            {
                same_line_right(28.0f);
                if (icon_button("##CopyTrace", UiIcon::Copy, "Copy trace"))
                    actions.copy_trace_clicked = true;
                ImGui::Spacing();
                draw_log_box("##TraceLog", trace_log_text, ImGui::GetContentRegionAvail(), previous_trace_size);
            }
            end_section();
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
