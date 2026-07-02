#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>

#include "controller_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace meccha
{
    namespace
    {
        ImFont* g_heading_font = nullptr;
        ImFont* g_log_font = nullptr;
        constexpr int AppFontRegularResourceId = 202;
        constexpr int AppFontSemiBoldResourceId = 203;
        constexpr int AppIconPngResourceId = 205;

        constexpr const char* RepositoryUrl = "https://github.com/acentrist/MecchaCamouflage";
        constexpr const char* LicenseLabel = "GPL-3.0-or-later";

        struct UiTexture
        {
            ID3D11ShaderResourceView* srv{nullptr};
            int width{0};
            int height{0};
        };

        UiTexture g_app_icon{};

        void release_texture(UiTexture& texture)
        {
            if (texture.srv)
                texture.srv->Release();
            texture = {};
        }

        template <typename T>
        void safe_release(T*& ptr)
        {
            if (ptr)
                ptr->Release();
            ptr = nullptr;
        }

        auto load_png_resource_texture(ID3D11Device* device, int resource_id, UiTexture& out) -> bool
        {
            if (!device || out.srv)
                return out.srv != nullptr;

            HMODULE module = GetModuleHandleW(nullptr);
            HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resource_id), MAKEINTRESOURCEW(10));
            if (!resource)
                return false;
            HGLOBAL loaded = LoadResource(module, resource);
            const DWORD resource_size = SizeofResource(module, resource);
            void* resource_data = loaded ? LockResource(loaded) : nullptr;
            if (!resource_data || resource_size == 0)
                return false;

            const HRESULT coinit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(coinit) && coinit != RPC_E_CHANGED_MODE)
                return false;

            IWICImagingFactory* factory = nullptr;
            IWICStream* stream = nullptr;
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICFormatConverter* converter = nullptr;
            ID3D11Texture2D* texture = nullptr;

            bool ok = false;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,
                                           nullptr,
                                           CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&factory))) &&
                SUCCEEDED(factory->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(resource_data), resource_size)) &&
                SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
                SUCCEEDED(decoder->GetFrame(0, &frame)) &&
                SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                SUCCEEDED(converter->Initialize(frame,
                                                GUID_WICPixelFormat32bppRGBA,
                                                WICBitmapDitherTypeNone,
                                                nullptr,
                                                0.0,
                                                WICBitmapPaletteTypeCustom)))
            {
                UINT width = 0;
                UINT height = 0;
                if (SUCCEEDED(converter->GetSize(&width, &height)) && width > 0 && height > 0)
                {
                    const UINT stride = width * 4;
                    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
                    if (SUCCEEDED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data())))
                    {
                        D3D11_TEXTURE2D_DESC desc{};
                        desc.Width = width;
                        desc.Height = height;
                        desc.MipLevels = 1;
                        desc.ArraySize = 1;
                        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        desc.SampleDesc.Count = 1;
                        desc.Usage = D3D11_USAGE_DEFAULT;
                        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                        D3D11_SUBRESOURCE_DATA init{};
                        init.pSysMem = pixels.data();
                        init.SysMemPitch = stride;
                        if (SUCCEEDED(device->CreateTexture2D(&desc, &init, &texture)) &&
                            SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &out.srv)))
                        {
                            out.width = static_cast<int>(width);
                            out.height = static_cast<int>(height);
                            ok = true;
                        }
                    }
                }
            }

            safe_release(texture);
            safe_release(converter);
            safe_release(frame);
            safe_release(decoder);
            safe_release(stream);
            safe_release(factory);
            return ok;
        }

        auto texture_ref(const UiTexture& texture) -> ImTextureRef
        {
            return ImTextureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(texture.srv)));
        }

        auto status_color(const std::string& state) -> ImVec4
        {
            std::string lower = state;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("ready") != std::string::npos ||
                lower.find("attached") != std::string::npos ||
                lower.find("running") != std::string::npos)
                return ImVec4(0.463f, 0.725f, 0.0f, 1.0f);
            if (lower.find("failed") != std::string::npos ||
                lower.find("error") != std::string::npos)
                return ImVec4(0.96f, 0.28f, 0.25f, 1.0f);
            return ImVec4(0.95f, 0.72f, 0.25f, 1.0f);
        }

        auto log_line_color(const std::string& line) -> ImU32
        {
            if (line.find("[ERROR]") != std::string::npos)
                return ImGui::GetColorU32(ImVec4(0.96f, 0.28f, 0.25f, 1.0f));
            if (line.find("[WARN]") != std::string::npos)
                return ImGui::GetColorU32(ImVec4(0.95f, 0.72f, 0.31f, 1.0f));
            return ImGui::GetColorU32(ImVec4(0.18f, 0.56f, 1.0f, 1.0f));
        }

        auto stable_id(const char* prefix, const char* label) -> std::string
        {
            return std::string("##") + prefix + label;
        }

        auto log_lines_from_text(const std::string& text) -> std::vector<std::string>
        {
            std::vector<std::string> lines;
            if (text.empty())
            {
                lines.emplace_back("No log events.");
                return lines;
            }
            std::size_t start = 0;
            while (start <= text.size())
            {
                const std::size_t end = text.find('\n', start);
                lines.emplace_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            if (!lines.empty() && lines.back().empty())
                lines.pop_back();
            return lines;
        }

        struct LogTextPos
        {
            int line{-1};
            int column{0};
        };

        auto compare_log_pos(const LogTextPos& lhs, const LogTextPos& rhs) -> int
        {
            if (lhs.line != rhs.line)
                return lhs.line < rhs.line ? -1 : 1;
            if (lhs.column != rhs.column)
                return lhs.column < rhs.column ? -1 : 1;
            return 0;
        }

        auto clamp_log_pos(LogTextPos pos, const std::vector<std::string>& lines) -> LogTextPos
        {
            if (lines.empty())
                return {};
            pos.line = std::max(0, std::min(pos.line, static_cast<int>(lines.size()) - 1));
            const auto& line = lines[static_cast<std::size_t>(pos.line)];
            pos.column = std::max(0, std::min(pos.column, static_cast<int>(line.size())));
            return pos;
        }

        auto log_text_range_width(const std::string& line, int begin, int end) -> float
        {
            begin = std::max(0, std::min(begin, static_cast<int>(line.size())));
            end = std::max(begin, std::min(end, static_cast<int>(line.size())));
            if (end <= begin)
                return 0.0f;
            return ImGui::CalcTextSize(line.c_str() + begin, line.c_str() + end).x;
        }

        auto log_column_from_x(const std::string& line, int begin, int end, float x) -> int
        {
            if (x <= 0.0f || line.empty())
                return begin;
            begin = std::max(0, std::min(begin, static_cast<int>(line.size())));
            end = std::max(begin, std::min(end, static_cast<int>(line.size())));
            float previous_width = 0.0f;
            for (int column = begin + 1; column <= end; ++column)
            {
                const float width = log_text_range_width(line, begin, column);
                if (x < previous_width + (width - previous_width) * 0.5f)
                    return column - 1;
                previous_width = width;
            }
            return end;
        }

        struct LogVisualRow
        {
            int line{0};
            int begin{0};
            int end{0};
        };

        auto wrap_log_lines(const std::vector<std::string>& lines, float max_width) -> std::vector<LogVisualRow>
        {
            std::vector<LogVisualRow> rows;
            const float wrap_width = std::max(16.0f, max_width);
            for (int line_index = 0; line_index < static_cast<int>(lines.size()); ++line_index)
            {
                const auto& line = lines[static_cast<std::size_t>(line_index)];
                const int line_size = static_cast<int>(line.size());
                if (line_size == 0)
                {
                    rows.push_back({line_index, 0, 0});
                    continue;
                }

                int begin = 0;
                while (begin < line_size)
                {
                    int end = begin + 1;
                    while (end < line_size && log_text_range_width(line, begin, end + 1) <= wrap_width)
                        ++end;
                    rows.push_back({line_index, begin, end});
                    begin = end;
                }
            }
            return rows;
        }

        auto selected_log_text(const std::vector<std::string>& lines, LogTextPos begin, LogTextPos end) -> std::string
        {
            if (lines.empty())
                return {};
            begin = clamp_log_pos(begin, lines);
            end = clamp_log_pos(end, lines);
            if (compare_log_pos(begin, end) > 0)
                std::swap(begin, end);
            if (compare_log_pos(begin, end) == 0)
                return {};

            std::string out;
            for (int line_index = begin.line; line_index <= end.line; ++line_index)
            {
                const auto& line = lines[static_cast<std::size_t>(line_index)];
                const int start = line_index == begin.line ? begin.column : 0;
                const int stop = line_index == end.line ? end.column : static_cast<int>(line.size());
                if (line_index > begin.line)
                    out.push_back('\n');
                if (stop > start)
                    out += line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(stop - start));
            }
            return out;
        }

        void draw_colored_selectable_log_box(const char* id, const std::string& text, const ImVec2& size, std::size_t& previous_size)
        {
            const bool text_changed = text.size() != previous_size;
            previous_size = text.size();

            static LogTextPos selection_anchor{};
            static LogTextPos selection_cursor{};
            static bool dragging_selection = false;
            const std::vector<std::string> lines = log_lines_from_text(text);
            const int line_count = static_cast<int>(lines.size());
            selection_anchor = clamp_log_pos(selection_anchor, lines);
            selection_cursor = clamp_log_pos(selection_cursor, lines);

            if (g_log_font)
                ImGui::PushFont(g_log_font);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            if (ImGui::BeginChild(id, size, true, ImGuiWindowFlags_AlwaysVerticalScrollbar))
            {
                const ImGuiIO& io = ImGui::GetIO();
                const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                const float row_height = ImGui::GetTextLineHeightWithSpacing();
                const float text_offset_y = std::max(0.0f, (row_height - ImGui::GetTextLineHeight()) * 0.5f);
                const float row_width = ImGui::GetContentRegionAvail().x;
                const std::vector<LogVisualRow> visual_rows = wrap_log_lines(lines, row_width - 8.0f);
                if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && line_count > 0)
                {
                    selection_anchor = {0, 0};
                    selection_cursor = {line_count - 1, static_cast<int>(lines.back().size())};
                }
                const bool has_selection = compare_log_pos(selection_anchor, selection_cursor) != 0;
                if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && has_selection)
                {
                    const std::string copied = selected_log_text(lines, selection_anchor, selection_cursor);
                    ImGui::SetClipboardText(copied.c_str());
                }

                const LogTextPos selection_begin = compare_log_pos(selection_anchor, selection_cursor) <= 0 ? selection_anchor : selection_cursor;
                const LogTextPos selection_end = compare_log_pos(selection_anchor, selection_cursor) <= 0 ? selection_cursor : selection_anchor;
                for (int row_index = 0; row_index < static_cast<int>(visual_rows.size()); ++row_index)
                {
                    const LogVisualRow& row = visual_rows[static_cast<std::size_t>(row_index)];
                    const ImVec2 row_pos = ImGui::GetCursorScreenPos();
                    ImGui::PushID(row_index);
                    ImGui::InvisibleButton("##logrow", ImVec2(row_width, row_height));
                    const std::string& line = lines[static_cast<std::size_t>(row.line)];
                    const int hovered_column = log_column_from_x(line, row.begin, row.end, io.MousePos.x - row_pos.x - 4.0f);
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        if (io.KeyShift && selection_anchor.line >= 0)
                        {
                            selection_cursor = {row.line, hovered_column};
                        }
                        else
                        {
                            selection_anchor = {row.line, hovered_column};
                            selection_cursor = selection_anchor;
                        }
                        dragging_selection = true;
                    }
                    if (dragging_selection && ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    {
                        selection_cursor = {row.line, hovered_column};
                    }
                    ImGui::PopID();

                    if (has_selection && row.line >= selection_begin.line && row.line <= selection_end.line)
                    {
                        const int start_column = std::max(row.begin, row.line == selection_begin.line ? selection_begin.column : row.begin);
                        const int end_column = std::min(row.end, row.line == selection_end.line ? selection_end.column : row.end);
                        if (end_column > start_column)
                        {
                            const float start_x = row_pos.x + 4.0f + log_text_range_width(line, row.begin, start_column);
                            const float end_x = row_pos.x + 4.0f + log_text_range_width(line, row.begin, end_column);
                            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start_x, row_pos.y),
                                                                      ImVec2(std::max(start_x + 2.0f, end_x), row_pos.y + row_height),
                                                                      ImGui::GetColorU32(ImVec4(0.18f, 0.30f, 0.18f, 0.80f)));
                        }
                    }
                    ImGui::GetWindowDrawList()->AddText(ImVec2(row_pos.x + 4.0f, row_pos.y + text_offset_y),
                                                        log_line_color(line),
                                                        line.c_str() + row.begin,
                                                        line.c_str() + row.end);
                }
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    dragging_selection = false;
                if (text_changed && !has_selection && !dragging_selection)
                    ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
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
        style.ChildRounding = 2.0f;
        style.FrameRounding = 2.0f;
        style.PopupRounding = 2.0f;
        style.ScrollbarRounding = 6.0f;
        style.GrabRounding = 2.0f;
        style.WindowPadding = ImVec2(12.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 5.0f);
        style.ItemSpacing = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing = ImVec2(7.0f, 5.0f);
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 15.0f;
        style.FrameBorderSize = 1.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.94f, 0.95f, 0.96f, 1.0f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.62f, 0.64f, 0.62f, 1.0f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.075f, 0.075f, 1.0f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.075f, 0.075f, 1.0f);
        colors[ImGuiCol_Border] = ImVec4(0.37f, 0.37f, 0.37f, 1.0f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.035f, 0.035f, 0.035f, 1.0f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.090f, 0.100f, 0.080f, 1.0f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.115f, 0.140f, 0.090f, 1.0f);
        colors[ImGuiCol_Button] = ImVec4(0.075f, 0.075f, 0.075f, 1.0f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.150f, 0.190f, 0.095f, 1.0f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.463f, 0.725f, 0.0f, 1.0f);
        colors[ImGuiCol_Header] = ImVec4(0.075f, 0.075f, 0.075f, 1.0f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.150f, 0.190f, 0.095f, 1.0f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.463f, 0.725f, 0.0f, 1.0f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.463f, 0.725f, 0.0f, 1.0f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.463f, 0.725f, 0.0f, 1.0f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.58f, 0.86f, 0.12f, 1.0f);
        colors[ImGuiCol_Separator] = ImVec4(0.34f, 0.34f, 0.34f, 1.0f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.463f, 0.725f, 0.0f, 1.0f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.58f, 0.86f, 0.12f, 1.0f);
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
            g_heading_font = add_embedded_font(AppFontSemiBoldResourceId, 18.0f);
            g_log_font = add_embedded_font(AppFontRegularResourceId, 15.5f);
            if (!g_heading_font)
                g_heading_font = embedded_ui;
            if (!g_log_font)
                g_log_font = embedded_ui;
            return;
        }
        const char* ui_fonts[] = {
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\Arial.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
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
        const char* log_fonts[] = {
            "C:\\Windows\\Fonts\\arial.ttf",
            "C:\\Windows\\Fonts\\Arial.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
        };
        for (const char* path : log_fonts)
        {
            g_log_font = io.Fonts->AddFontFromFileTTF(path, 15.5f);
            if (g_log_font)
                break;
        }
    }

    void initialize_meccha_ui_resources(ID3D11Device* device)
    {
        load_png_resource_texture(device, AppIconPngResourceId, g_app_icon);
    }

    void shutdown_meccha_ui_resources()
    {
        release_texture(g_app_icon);
    }

    void draw_app_ui(AppSettings& draft,
                     const AppSettings& persisted,
                     const UiRuntimeState& runtime,
                     const std::string& human_log_text,
                     UiActions& actions)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize |
                                       ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (!ImGui::Begin("MecchaCamouflageDesktop", nullptr, flags))
        {
            ImGui::PopStyleVar();
            ImGui::End();
            return;
        }
        ImGui::PopStyleVar();

        const ImGuiStyle& style = ImGui::GetStyle();
        constexpr ImVec4 Primary = ImVec4(0.463f, 0.725f, 0.0f, 1.0f);
        constexpr ImVec4 PrimaryText = ImVec4(0.02f, 0.02f, 0.02f, 1.0f);
        constexpr ImVec4 Surface = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        constexpr ImVec4 SurfaceLow = ImVec4(0.105f, 0.105f, 0.105f, 1.0f);
        constexpr ImVec4 Hairline = ImVec4(0.37f, 0.37f, 0.37f, 1.0f);
        constexpr ImVec4 Muted = ImVec4(0.64f, 0.64f, 0.64f, 1.0f);

        auto draw_corner_accent = [&](bool bottom_right = false) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            const float len = 16.0f;
            const ImU32 color = ImGui::GetColorU32(Primary);
            draw->AddLine(pos, ImVec2(pos.x + len, pos.y), color, 3.0f);
            draw->AddLine(pos, ImVec2(pos.x, pos.y + len), color, 3.0f);
            if (bottom_right)
            {
                const ImVec2 br(pos.x + size.x - 1.0f, pos.y + size.y - 1.0f);
                draw->AddLine(br, ImVec2(br.x - len, br.y), color, 3.0f);
                draw->AddLine(br, ImVec2(br.x, br.y - len), color, 3.0f);
            }
        };

        auto begin_panel = [&](const char* id,
                               const ImVec2& size,
                               bool accent = true,
                               bool bottom_right = false,
                               ImVec2 padding = ImVec2(8.0f, 8.0f),
                               ImGuiChildFlags child_flags = ImGuiChildFlags_Borders,
                               ImGuiWindowFlags window_flags = 0) -> bool {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Surface);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            const bool open = ImGui::BeginChild(id, size, child_flags, window_flags);
            if (open && accent)
                draw_corner_accent(bottom_right);
            return open;
        };

        auto end_panel = [&]() {
            ImGui::EndChild();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor();
        };

        auto section_header = [&](const char* title, bool accent = false) {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = 31.0f;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(SurfaceLow), 0.0f);
            draw->AddLine(ImVec2(pos.x, pos.y + height), ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(Hairline), 1.0f);
            if (accent)
            {
                const ImU32 color = ImGui::GetColorU32(Primary);
                draw->AddLine(pos, ImVec2(pos.x + 16.0f, pos.y), color, 3.0f);
                draw->AddLine(pos, ImVec2(pos.x, pos.y + 16.0f), color, 3.0f);
            }
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 8.0f, pos.y + 8.0f));
            if (g_heading_font)
                ImGui::PushFont(g_heading_font);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.94f, 0.92f, 1.0f));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            if (g_heading_font)
                ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 12.0f, pos.y + height + 9.0f));
        };

        auto action_button = [&](const char* label, bool enabled, bool primary, const ImVec2& size) -> bool {
            if (!enabled)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.055f, 0.060f, 0.050f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.055f, 0.060f, 0.050f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.055f, 0.060f, 0.050f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.38f, 0.32f, 1.0f));
            }
            else if (primary)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, Primary);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.86f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.64f, 0.92f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, PrimaryText);
            }
            ImGui::BeginDisabled(!enabled);
            const bool pressed = ImGui::Button(label, size);
            ImGui::EndDisabled();
            if (!enabled || primary)
                ImGui::PopStyleColor(4);
            return pressed;
        };

        auto custom_checkbox = [&](const char* label, bool& value, bool enabled = true) -> bool {
            ImGui::PushID(label);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float box = 15.0f;
            const float row_height = 24.0f;
            const ImVec2 label_size = ImGui::CalcTextSize(label);
            const ImVec2 total_size(box + 7.0f + label_size.x, row_height);
            const bool pressed = ImGui::InvisibleButton("##checkbox", total_size);
            if (pressed && enabled)
                value = !value;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImU32 border = ImGui::GetColorU32((enabled || value) ? Primary : Muted);
            const ImVec2 box_min(start.x, start.y + (row_height - box) * 0.5f);
            draw->AddRectFilled(box_min, ImVec2(box_min.x + box, box_min.y + box), IM_COL32(10, 10, 10, 255), 1.0f);
            draw->AddRect(box_min, ImVec2(box_min.x + box, box_min.y + box), border, 1.0f, 0, 1.6f);
            if (value)
            {
                draw->AddRectFilled(ImVec2(box_min.x + 4.0f, box_min.y + 4.0f),
                                    ImVec2(box_min.x + box - 4.0f, box_min.y + box - 4.0f),
                                    ImGui::GetColorU32(Primary),
                                    1.0f);
            }
            const ImVec2 text_pos(start.x + box + 7.0f, start.y + (row_height - label_size.y) * 0.5f - 1.0f);
            draw->AddText(text_pos, ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
            ImGui::PopID();
            return pressed && enabled;
        };

        auto checkbox_box = [&](const char* id, bool& value, bool enabled = true) -> bool {
            ImGui::PushID(id);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float box = 15.0f;
            const bool pressed = ImGui::InvisibleButton("##checkbox_box", ImVec2(box, box));
            if (pressed && enabled)
                value = !value;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImU32 border = ImGui::GetColorU32((enabled || value) ? Primary : Muted);
            draw->AddRectFilled(start, ImVec2(start.x + box, start.y + box), IM_COL32(10, 10, 10, 255), 1.0f);
            draw->AddRect(start, ImVec2(start.x + box, start.y + box), border, 1.0f, 0, 1.6f);
            if (value)
            {
                draw->AddRectFilled(ImVec2(start.x + 4.0f, start.y + 4.0f),
                                    ImVec2(start.x + box - 4.0f, start.y + box - 4.0f),
                                    ImGui::GetColorU32(Primary),
                                    1.0f);
            }
            ImGui::PopID();
            return pressed && enabled;
        };

        constexpr float FormPaddingX = 14.0f;
        constexpr float FormRightPaddingX = 14.0f;
        constexpr float FormLabelWidth = 132.0f;
        constexpr float AppLabelWidth = 106.0f;
        constexpr float FormControlMaxWidth = 310.0f;
        constexpr float AppControlMaxWidth = 300.0f;
        auto set_form_x = [&]() {
            ImGui::SetCursorPosX(FormPaddingX);
        };
        auto form_control_x = [&]() -> float {
            return FormPaddingX + FormLabelWidth;
        };
        auto form_control_width = [&]() -> float {
            return std::min(FormControlMaxWidth, std::max(1.0f, ImGui::GetWindowContentRegionMax().x - form_control_x() - FormRightPaddingX));
        };
        auto app_control_x = [&]() -> float {
            return FormPaddingX + AppLabelWidth;
        };
        auto app_control_width = [&]() -> float {
            return std::min(AppControlMaxWidth, std::max(1.0f, ImGui::GetWindowContentRegionMax().x - app_control_x() - FormRightPaddingX));
        };
        auto app_label = [&](const char* label) {
            set_form_x();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
        };
        auto app_row = [&](const char* label) -> float {
            const float row_top = ImGui::GetCursorScreenPos().y;
            set_form_x();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(app_control_x());
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, row_top));
            return row_top;
        };

        auto readonly_input_box = [&](const char* id, const char* text, float width) {
            ImGui::PushID(id);
            std::array<char, 96> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", text ? text : "");
            ImGui::BeginDisabled(true);
            ImGui::SetNextItemWidth(width);
            ImGui::InputText("##readonly", buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            ImGui::PopID();
        };

        auto app_frame_button = [&](const char* id, const char* label, bool enabled, const ImVec2& size) -> bool {
            ImGui::PushID(id);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
            ImGui::BeginDisabled(!enabled);
            const bool pressed = ImGui::Button(label, size);
            ImGui::EndDisabled();
            ImGui::PopStyleColor(3);
            ImGui::PopID();
            return pressed;
        };

        auto draw_app_mark = [&](const ImVec2& min, const ImVec2& max) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            if (g_app_icon.srv)
            {
                draw->AddImage(texture_ref(g_app_icon), min, max);
                return;
            }
            draw->AddRectFilled(min, max, ImGui::GetColorU32(Primary), 3.0f);
            draw->AddRectFilled(ImVec2(min.x + 6.0f, min.y + 5.0f),
                                ImVec2(max.x - 6.0f, max.y - 5.0f),
                                IM_COL32(6, 8, 4, 255),
                                1.0f);
        };

        auto draw_folder_icon = [&](const ImVec2& min, ImU32 color) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float x = min.x;
            const float y = min.y;
            draw->AddLine(ImVec2(x, y + 4.0f), ImVec2(x + 5.0f, y + 4.0f), color, 1.4f);
            draw->AddLine(ImVec2(x + 5.0f, y + 4.0f), ImVec2(x + 7.0f, y + 7.0f), color, 1.4f);
            draw->AddLine(ImVec2(x + 7.0f, y + 7.0f), ImVec2(x + 17.0f, y + 7.0f), color, 1.4f);
            draw->AddRect(ImVec2(x, y + 6.0f), ImVec2(x + 18.0f, y + 15.0f), color, 1.0f, 0, 1.4f);
        };

        auto draw_copy_icon = [&](const ImVec2& min, ImU32 color) {
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRect(ImVec2(min.x + 5.0f, min.y + 4.0f), ImVec2(min.x + 14.0f, min.y + 15.0f), color, 1.0f, 0, 1.2f);
            draw->AddRect(ImVec2(min.x + 2.0f, min.y + 1.0f), ImVec2(min.x + 11.0f, min.y + 12.0f), color, 1.0f, 0, 1.2f);
        };

        auto field_double = [&](const char* label, double& value, double min_value, double max_value, const char* format, bool enabled, bool& changed) {
            ImGui::PushID(label);
            const float row_top = ImGui::GetCursorScreenPos().y;
            set_form_x();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(form_control_x());
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, row_top));
            ImGui::BeginDisabled(!enabled);
            const double before = value;
            float slider_value = static_cast<float>(value);
            const float input_width = 82.0f;
            const float control_width = form_control_width();
            const float slider_width = std::max(1.0f, control_width - input_width - ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(slider_width);
            if (ImGui::SliderFloat("##slider", &slider_value, static_cast<float>(min_value), static_cast<float>(max_value), format, ImGuiSliderFlags_AlwaysClamp))
            {
                value = static_cast<double>(slider_value);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_width);
            if (ImGui::InputDouble("##input", &value, 0.0, 0.0, format, ImGuiInputTextFlags_EnterReturnsTrue))
                changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                value = std::min(max_value, std::max(min_value, value));
                changed = changed || before != value;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        };

        auto field_int = [&](const char* label, int& value, int min_value, int max_value, bool enabled, bool& changed) {
            ImGui::PushID(label);
            const float row_top = ImGui::GetCursorScreenPos().y;
            set_form_x();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(form_control_x());
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, row_top));
            ImGui::BeginDisabled(!enabled);
            const int before = value;
            const float input_width = 82.0f;
            const float control_width = form_control_width();
            const float slider_width = std::max(1.0f, control_width - input_width - ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(slider_width);
            if (ImGui::SliderInt("##slider", &value, min_value, max_value, "%d", ImGuiSliderFlags_AlwaysClamp))
                changed = true;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_width);
            if (ImGui::InputInt("##input", &value, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                changed = true;
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                value = std::min(max_value, std::max(min_value, value));
                changed = changed || before != value;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        };

        auto metric_card = [&](const char* label, const std::string& value, ImVec4 value_color = ImVec4(0.90f, 0.90f, 0.90f, 1.0f)) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Surface);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            if (ImGui::BeginChild(label, ImVec2(0.0f, 48.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                ImGui::SetCursorPos(ImVec2(8.0f, 5.0f));
                ImGui::TextDisabled("%s", label);
                ImGui::SetCursorPos(ImVec2(8.0f, 20.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                ImGui::TextWrapped("%s", value.empty() ? "-" : value.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        };

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.045f, 0.045f, 0.045f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        constexpr float HeaderHeight = 36.0f;
        if (ImGui::BeginChild("TopAppBar", ImVec2(0.0f, HeaderHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const ImVec2 bar_pos = ImGui::GetWindowPos();
            const ImVec2 bar_size = ImGui::GetWindowSize();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float icon_size = 20.0f;
            const ImVec2 icon_pos(bar_pos.x + 10.0f, bar_pos.y + (HeaderHeight - icon_size) * 0.5f);
            ImGui::SetCursorScreenPos(icon_pos);
            draw_app_mark(icon_pos, ImVec2(icon_pos.x + icon_size, icon_pos.y + icon_size));
            const float title_x = icon_pos.x + 28.0f;
            const float heading_height = g_heading_font ? 18.0f : ImGui::GetTextLineHeight();
            const float title_y = bar_pos.y + (HeaderHeight - heading_height) * 0.5f - 1.0f;
            ImGui::SetCursorScreenPos(ImVec2(title_x, title_y));
            if (g_heading_font)
                ImGui::PushFont(g_heading_font);
            ImGui::TextUnformatted("Meccha Camouflage");
            const float title_width = ImGui::GetItemRectSize().x;
            if (g_heading_font)
                ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(title_x + title_width + 8.0f,
                                             bar_pos.y + (HeaderHeight - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::TextDisabled("v1.4.0");

            const float close_width = 28.0f;
            const float minimize_width = 28.0f;
            const ImVec2 minimize_min(bar_pos.x + bar_size.x - close_width - minimize_width - 14.0f, bar_pos.y + (HeaderHeight - 28.0f) * 0.5f);
            ImGui::SetCursorScreenPos(minimize_min);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.13f, 0.12f, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.20f, 0.15f, 0.80f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            if (ImGui::Button("##MinimizeApp", ImVec2(minimize_width, 28.0f)))
                actions.minimize_clicked = true;
            const ImVec2 min_min = ImGui::GetItemRectMin();
            const ImVec2 min_max = ImGui::GetItemRectMax();
            const ImU32 minimize_color = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            draw->AddLine(ImVec2(min_min.x + 9.0f, min_max.y - 9.0f), ImVec2(min_max.x - 9.0f, min_max.y - 9.0f), minimize_color, 1.5f);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);

            const ImVec2 close_min(bar_pos.x + bar_size.x - close_width - 10.0f, bar_pos.y + (HeaderHeight - 28.0f) * 0.5f);
            ImGui::SetCursorScreenPos(close_min);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.13f, 0.12f, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.20f, 0.15f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_Text, Muted);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            if (ImGui::Button("##CloseApp", ImVec2(close_width, 28.0f)))
                actions.close_clicked = true;
            const ImVec2 x_min = ImGui::GetItemRectMin();
            const ImVec2 x_max = ImGui::GetItemRectMax();
            const ImU32 x_color = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            draw->AddLine(ImVec2(x_min.x + 9.0f, x_min.y + 9.0f), ImVec2(x_max.x - 9.0f, x_max.y - 9.0f), x_color, 1.5f);
            draw->AddLine(ImVec2(x_max.x - 9.0f, x_min.y + 9.0f), ImVec2(x_min.x + 9.0f, x_max.y - 9.0f), x_color, 1.5f);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);

            draw->AddLine(ImVec2(bar_pos.x, bar_pos.y + HeaderHeight - 1.0f),
                          ImVec2(bar_pos.x + bar_size.x, bar_pos.y + HeaderHeight - 1.0f),
                          ImGui::GetColorU32(Hairline),
                          1.0f);

            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            {
                ReleaseCapture();
                SendMessageW(GetActiveWindow(), WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        const float footer_height = 24.0f;
        constexpr float ContentPadX = 8.0f;
        constexpr float HeaderGap = 6.0f;
        constexpr float FooterGap = 6.0f;
        ImGui::SetCursorPos(ImVec2(ContentPadX, HeaderHeight + HeaderGap));
        const float content_height = std::max(1.0f, io.DisplaySize.y - HeaderHeight - footer_height - HeaderGap - FooterGap);
        const float total_width = std::max(1.0f, ImGui::GetContentRegionAvail().x - ContentPadX);
        const float gutter = 14.0f;
        const float available_left_width = std::max(1.0f, total_width - gutter);
        const float left_width = std::min(available_left_width,
                                          std::max(430.0f, std::min(460.0f, available_left_width * 0.40f)));
        if (ImGui::BeginChild("MainContent", ImVec2(total_width, content_height), false))
        {
            if (ImGui::BeginChild("ControlsColumn", ImVec2(left_width, 0.0f), false))
            {
                const bool can_stop = runtime.paint_running;
                const bool can_start = runtime.service_state != "Starting" &&
                                       runtime.service_state != "Stopping" &&
                                       !runtime.paint_running;
                const float service_gap = style.ItemSpacing.x;
                const float button_width = std::max(1.0f, (ImGui::GetContentRegionAvail().x - service_gap) * 0.5f);
                if (action_button("Start / Hotkey", can_start, can_start, ImVec2(button_width, 30.0f)))
                    actions.start_service_clicked = true;
                ImGui::SameLine();
                if (action_button("Stop / Hotkey", can_stop, false, ImVec2(button_width, 30.0f)))
                    actions.stop_service_clicked = true;
                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                const float settings_height = std::max(1.0f, ImGui::GetContentRegionAvail().y);
                constexpr ImGuiChildFlags SettingsPanelFlags = ImGuiChildFlags_Borders;
                constexpr ImGuiWindowFlags SettingsPanelWindowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
                if (begin_panel("SettingsPanel",
                                ImVec2(0.0f, settings_height),
                                true,
                                false,
                                ImVec2(0.0f, 0.0f),
                                SettingsPanelFlags,
                                SettingsPanelWindowFlags))
                {
                    section_header("PAINT SETTINGS", true);
                    PaintTuning tuning = runtime.paint_editing ? draft.tuning : persisted.tuning;
                    bool paint_value_changed = false;
                    field_double("Brush size (texels)", tuning.stroke_size_texels, 1.0, 12.0, "%.1f", runtime.paint_editing, paint_value_changed);
                    field_double("Coverage step (texels)", tuning.coverage_step_texels, 1.0, 12.0, "%.1f", runtime.paint_editing, paint_value_changed);
                    field_int("Batch limit", tuning.server_batch_limit, 1, 1000000, runtime.paint_editing, paint_value_changed);
                    field_int("Batch delay (ms)", tuning.server_batch_delay_ms, 0, 1000, runtime.paint_editing, paint_value_changed);
                    field_double("Metallic", tuning.metallic, 0.0, 1.0, "%.6f", runtime.paint_editing, paint_value_changed);
                    field_double("Roughness", tuning.roughness, 0.0, 1.0, "%.6f", runtime.paint_editing, paint_value_changed);
                    if (runtime.paint_editing && paint_value_changed)
                    {
                        draft.tuning = tuning;
                        actions.settings_changed = true;
                    }

                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
                    const float app_block_start_y = ImGui::GetCursorPosY();
                    const ImVec2 app_block_pos = ImGui::GetCursorScreenPos();
                    const float app_block_width = ImGui::GetContentRegionAvail().x;
                    ImDrawList* settings_draw = ImGui::GetWindowDrawList();
                    ImDrawListSplitter app_block_splitter;
                    app_block_splitter.Split(settings_draw, 2);
                    app_block_splitter.SetCurrentChannel(settings_draw, 1);
                    section_header("APP SETTINGS");
                    bool always_on_top = runtime.app_editing ? draft.always_on_top : persisted.always_on_top;
                    float opacity = runtime.app_editing ? draft.opacity : persisted.opacity;
                    bool app_value_changed = false;
                    app_row("Start Hotkey");
                    const float hotkey_input_width = 82.0f;
                    const float record_width = std::max(1.0f, app_control_width() - hotkey_input_width - style.ItemSpacing.x);
                    if (runtime.recording_start_hotkey)
                        app_frame_button("StartHotkeyRecordButton", "Press key...", runtime.app_editing, ImVec2(record_width, 0.0f));
                    else if (app_frame_button("StartHotkeyRecordButton", "Record", runtime.app_editing, ImVec2(record_width, 0.0f)))
                        actions.start_hotkey_recording = true;
                    ImGui::SameLine();
                    readonly_input_box("StartHotkeyReadonly",
                                       (runtime.app_editing ? draft.start_hotkey : persisted.start_hotkey).c_str(),
                                       hotkey_input_width);

                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
                    app_row("Stop Hotkey");
                    if (runtime.recording_stop_hotkey)
                        app_frame_button("StopHotkeyRecordButton", "Press key...", runtime.app_editing, ImVec2(record_width, 0.0f));
                    else if (app_frame_button("StopHotkeyRecordButton", "Record", runtime.app_editing, ImVec2(record_width, 0.0f)))
                        actions.stop_hotkey_recording = true;
                    ImGui::SameLine();
                    readonly_input_box("StopHotkeyReadonly",
                                       (runtime.app_editing ? draft.stop_hotkey : persisted.stop_hotkey).c_str(),
                                       hotkey_input_width);

                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
                    app_row("Always on top");
                    if (checkbox_box("AlwaysOnTop", always_on_top, runtime.app_editing))
                        app_value_changed = true;
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
                    app_row("Opacity");
                    int opacity_percent = static_cast<int>(opacity * 100.0f + 0.5f);
                    ImGui::BeginDisabled(!runtime.app_editing);
                    const float opacity_input_width = 82.0f;
                    ImGui::SetNextItemWidth(std::max(1.0f, app_control_width() - opacity_input_width - style.ItemSpacing.x));
                    if (ImGui::SliderInt("##OpacitySliderNvidia", &opacity_percent, 35, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp))
                    {
                        opacity = static_cast<float>(std::min(100, std::max(35, opacity_percent))) / 100.0f;
                        app_value_changed = true;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(opacity_input_width);
                    if (ImGui::InputInt("##OpacityInputNvidia", &opacity_percent, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        opacity_percent = std::min(100, std::max(35, opacity_percent));
                        opacity = static_cast<float>(opacity_percent) / 100.0f;
                        app_value_changed = true;
                    }
                    ImGui::EndDisabled();
                    app_row("Logs");
                    ImGui::PushID("OpenLogsIcon");
                    const ImVec2 log_button_pos = ImGui::GetCursorScreenPos();
                    const float log_button_size = ImGui::GetFrameHeight();
                    if (ImGui::InvisibleButton("##icon", ImVec2(log_button_size, log_button_size)))
                        actions.open_logs_clicked = true;
                    draw_folder_icon(ImVec2(log_button_pos.x + 4.0f, log_button_pos.y + (log_button_size - 16.0f) * 0.5f),
                                     ImGui::GetColorU32(Primary));
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::SetTooltip("%s", runtime.log_dir.c_str());
                    }
                    ImGui::PopID();
                    if (runtime.app_editing && app_value_changed)
                    {
                        draft.always_on_top = always_on_top;
                        draft.opacity = opacity;
                        actions.settings_changed = true;
                    }
                    constexpr float FooterPadY = 5.0f;
                    constexpr float FooterButtonHeight = 28.0f;
                    constexpr float FooterHeight = FooterPadY + FooterButtonHeight + FooterPadY;
                    const float footer_line_y = std::max(ImGui::GetCursorPosY() + style.ItemSpacing.y,
                                                         ImGui::GetWindowContentRegionMax().y - FooterHeight);
                    ImGui::SetCursorPosY(footer_line_y);
                    const ImVec2 footer_line_pos = ImGui::GetCursorScreenPos();
                    settings_draw->AddLine(footer_line_pos,
                                           ImVec2(footer_line_pos.x + ImGui::GetContentRegionAvail().x, footer_line_pos.y),
                                           ImGui::GetColorU32(Hairline));
                    ImGui::SetCursorPosY(footer_line_y + FooterPadY);
                    const float button_width = 64.0f;
                    const float button_gap = style.ItemSpacing.x;
                    const float button_total = button_width * 4.0f + button_gap * 3.0f;
                    set_form_x();
                    const float actions_x = std::max(FormPaddingX, ImGui::GetWindowContentRegionMax().x - button_total - FormRightPaddingX);
                    ImGui::SetCursorPosX(actions_x);
                    ImGui::PushID("PaintFooterActions");
                    const bool editing_any = runtime.paint_editing || runtime.app_editing;
                    if (action_button("Reset", editing_any, false, ImVec2(button_width, 28.0f)))
                    {
                        actions.reset_app_clicked = true;
                        actions.reset_paint_clicked = true;
                    }
                    ImGui::SameLine();
                    if (action_button("Cancel", editing_any, false, ImVec2(button_width, 28.0f)))
                    {
                        actions.cancel_app_clicked = true;
                        actions.cancel_paint_clicked = true;
                    }
                    ImGui::SameLine();
                    if (action_button("Edit", !editing_any, false, ImVec2(button_width, 28.0f)))
                    {
                        actions.edit_app_clicked = true;
                        actions.edit_paint_clicked = true;
                    }
                    ImGui::SameLine();
                    if (action_button("Save", editing_any, true, ImVec2(button_width, 28.0f)))
                    {
                        actions.save_app_clicked = true;
                        actions.save_paint_clicked = true;
                    }
                    const float footer_buttons_bottom_y = ImGui::GetItemRectMax().y - ImGui::GetWindowPos().y;
                    ImGui::PopID();
                    const float app_block_end_y = std::max(footer_buttons_bottom_y + FooterPadY, ImGui::GetWindowContentRegionMax().y);
                    ImGui::SetCursorPosY(app_block_end_y);
                    ImGui::Dummy(ImVec2(0.0f, 0.0f));
                    app_block_splitter.SetCurrentChannel(settings_draw, 0);
                    settings_draw->AddRectFilled(app_block_pos,
                                                 ImVec2(app_block_pos.x + app_block_width,
                                                        app_block_pos.y + std::max(0.0f, app_block_end_y - app_block_start_y)),
                                                 ImGui::GetColorU32(SurfaceLow));
                    app_block_splitter.Merge(settings_draw);
                }
                end_panel();
            }
            ImGui::EndChild();

            ImGui::SameLine(0.0f, gutter);

            if (ImGui::BeginChild("RuntimeColumn", ImVec2(0.0f, 0.0f), false))
            {
                static std::size_t previous_log_size = 0;
                if (ImGui::BeginChild("StatusStrip", ImVec2(0.0f, 56.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
                {
                    if (ImGui::BeginTable("RuntimeStatusGrid", 4, ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        metric_card("PROCESS", runtime.game_attached ? "ATTACHED" : "WAITING", status_color(runtime.game_attached ? "attached" : "waiting"));
                        ImGui::TableSetColumnIndex(1);
                        metric_card("HOOK", runtime.bridge_ready ? "READY" : "WAITING", status_color(runtime.bridge_ready ? "ready" : "waiting"));
                        ImGui::TableSetColumnIndex(2);
                        metric_card("CONTROL", runtime.service_state.empty() ? "-" : runtime.service_state, status_color(runtime.service_state));
                        ImGui::TableSetColumnIndex(3);
                        metric_card("PAINT", runtime.paint_ready ? "READY" : (runtime.paint_running ? "RUNNING" : "WAITING"), status_color(runtime.paint_ready ? "ready" : (runtime.paint_running ? "running" : "waiting")));
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();

                if (ImGui::BeginChild("MetricStrip", ImVec2(0.0f, 56.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
                {
                    if (ImGui::BeginTable("MetricGrid", 4, ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        metric_card("SERVER ETA", runtime.metric_server_eta);
                        ImGui::TableSetColumnIndex(1);
                        metric_card("SERVER ELAPSED", runtime.metric_server_elapsed);
                        ImGui::TableSetColumnIndex(2);
                        metric_card("PAINT ETA", runtime.metric_apply_eta);
                        ImGui::TableSetColumnIndex(3);
                        metric_card("PAINT ELAPSED", runtime.metric_apply_elapsed);
                        ImGui::EndTable();
                    }
                }
                ImGui::EndChild();

                if (begin_panel("LogPanel", ImVec2(0.0f, 0.0f), true, false, ImVec2(16.0f, 14.0f)))
                {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
                    ImGui::SetCursorPosX(16.0f);
                    const bool all_active = draft.show_info && draft.show_warning && draft.show_error;
                    bool all_filter = all_active;
                    if (custom_checkbox("All", all_filter, true))
                    {
                        draft.show_info = all_filter;
                        draft.show_warning = all_filter;
                        draft.show_error = all_filter;
                        actions.settings_changed = true;
                    }
                    ImGui::SameLine();
                    if (custom_checkbox("Info", draft.show_info, true))
                    {
                        actions.settings_changed = true;
                    }
                    ImGui::SameLine();
                    if (custom_checkbox("Warn", draft.show_warning, true))
                    {
                        actions.settings_changed = true;
                    }
                    ImGui::SameLine();
                    if (custom_checkbox("Error", draft.show_error, true))
                    {
                        actions.settings_changed = true;
                    }
                    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetContentRegionMax().x - 104.0f));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.13f, 0.12f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.20f, 0.15f, 0.95f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                    if (ImGui::Button("    Copy All", ImVec2(96.0f, 24.0f)))
                        actions.copy_log_clicked = true;
                    draw_copy_icon(ImVec2(ImGui::GetItemRectMin().x + 7.0f, ImGui::GetItemRectMin().y + 4.0f), ImGui::GetColorU32(Muted));
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(3);
                    ImGui::Spacing();
                    draw_colored_selectable_log_box("##MainLog", human_log_text, ImGui::GetContentRegionAvail(), previous_log_size);
                }
                end_panel();
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::SetCursorPosY(std::max(HeaderHeight, io.DisplaySize.y - footer_height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginChild("FooterBar", ImVec2(0.0f, footer_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            const ImVec2 footer_pos = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddLine(footer_pos,
                                                ImVec2(footer_pos.x + ImGui::GetWindowSize().x, footer_pos.y),
                                                ImGui::GetColorU32(Hairline),
                                                1.0f);
            const float text_y = std::max(0.0f, (footer_height - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::SetCursorPos(ImVec2(8.0f, text_y));
            ImGui::TextDisabled("(c) %u acentrist. All rights reserved. |", static_cast<unsigned>(now.wYear));
            auto footer_link = [&](const char* id, const char* label, const char* tooltip, bool& clicked) {
                ImGui::SameLine(0.0f, 5.0f);
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const ImVec2 size = ImGui::CalcTextSize(label);
                ImGui::InvisibleButton(id, size);
                const bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                    clicked = true;
                ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(hovered ? Primary : Muted), label);
                if (hovered)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", tooltip);
                }
            };
            footer_link("FooterLicenseLink", LicenseLabel, "Open license", actions.open_license_clicked);
            ImGui::SameLine(0.0f, 5.0f);
            ImGui::TextDisabled("|");
            footer_link("FooterGitHubLink", "GitHub", RepositoryUrl, actions.open_repository_clicked);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();
    }
}
