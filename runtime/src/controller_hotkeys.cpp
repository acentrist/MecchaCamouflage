#include "controller_hotkeys.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace meccha
{
    namespace
    {
        auto upper_copy(std::string value) -> std::string
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return value;
        }

        auto is_modifier_vk(UINT vk) -> bool
        {
            return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                   vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                   vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
                   vk == VK_LWIN || vk == VK_RWIN;
        }

        auto vk_label(UINT vk) -> std::string
        {
            if (vk >= 'A' && vk <= 'Z')
                return std::string(1, static_cast<char>(vk));
            if (vk >= '0' && vk <= '9')
                return std::string(1, static_cast<char>(vk));
            if (vk >= VK_F1 && vk <= VK_F24)
                return "F" + std::to_string(vk - VK_F1 + 1);
            switch (vk)
            {
            case VK_SPACE: return "Space";
            case VK_TAB: return "Tab";
            case VK_RETURN: return "Enter";
            case VK_BACK: return "Backspace";
            case VK_DELETE: return "Delete";
            case VK_INSERT: return "Insert";
            case VK_HOME: return "Home";
            case VK_END: return "End";
            case VK_PRIOR: return "PageUp";
            case VK_NEXT: return "PageDown";
            case VK_LEFT: return "Left";
            case VK_RIGHT: return "Right";
            case VK_UP: return "Up";
            case VK_DOWN: return "Down";
            case VK_OEM_PLUS: return "Plus";
            case VK_OEM_MINUS: return "Minus";
            case VK_OEM_COMMA: return "Comma";
            case VK_OEM_PERIOD: return "Period";
            case VK_OEM_1: return "Semicolon";
            case VK_OEM_2: return "Slash";
            case VK_OEM_3: return "Backquote";
            case VK_OEM_4: return "LeftBracket";
            case VK_OEM_5: return "Backslash";
            case VK_OEM_6: return "RightBracket";
            case VK_OEM_7: return "Quote";
            default: return "VK:" + std::to_string(vk);
            }
        }

        auto vk_from_label(std::string label, UINT fallback) -> UINT
        {
            label = upper_copy(label);
            if (label.size() == 1 && label[0] >= 'A' && label[0] <= 'Z')
                return static_cast<UINT>(label[0]);
            if (label.size() == 1 && label[0] >= '0' && label[0] <= '9')
                return static_cast<UINT>(label[0]);
            if (label.size() >= 2 && label[0] == 'F')
            {
                const int index = std::atoi(label.c_str() + 1);
                if (index >= 1 && index <= 24)
                    return static_cast<UINT>(VK_F1 + index - 1);
            }
            if (label.rfind("VK:", 0) == 0)
            {
                const int vk = std::atoi(label.c_str() + 3);
                if (vk > 0 && vk < 256)
                    return static_cast<UINT>(vk);
            }
            if (label == "SPACE") return VK_SPACE;
            if (label == "TAB") return VK_TAB;
            if (label == "ENTER") return VK_RETURN;
            if (label == "BACKSPACE") return VK_BACK;
            if (label == "DELETE") return VK_DELETE;
            if (label == "INSERT") return VK_INSERT;
            if (label == "HOME") return VK_HOME;
            if (label == "END") return VK_END;
            if (label == "PAGEUP") return VK_PRIOR;
            if (label == "PAGEDOWN") return VK_NEXT;
            if (label == "LEFT") return VK_LEFT;
            if (label == "RIGHT") return VK_RIGHT;
            if (label == "UP") return VK_UP;
            if (label == "DOWN") return VK_DOWN;
            if (label == "PLUS") return VK_OEM_PLUS;
            if (label == "MINUS") return VK_OEM_MINUS;
            if (label == "COMMA") return VK_OEM_COMMA;
            if (label == "PERIOD") return VK_OEM_PERIOD;
            if (label == "SEMICOLON") return VK_OEM_1;
            if (label == "SLASH") return VK_OEM_2;
            if (label == "BACKQUOTE") return VK_OEM_3;
            if (label == "LEFTBRACKET") return VK_OEM_4;
            if (label == "BACKSLASH") return VK_OEM_5;
            if (label == "RIGHTBRACKET") return VK_OEM_6;
            if (label == "QUOTE") return VK_OEM_7;
            return fallback;
        }

        auto split_tokens(const std::string& text) -> std::vector<std::string>
        {
            std::vector<std::string> out;
            std::string token;
            for (char c : text)
            {
                if (c == '+')
                {
                    if (!token.empty())
                        out.push_back(token);
                    token.clear();
                    continue;
                }
                if (!std::isspace(static_cast<unsigned char>(c)))
                    token.push_back(c);
            }
            if (!token.empty())
                out.push_back(token);
            return out;
        }

        auto current_modifiers() -> UINT
        {
            UINT mods = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
            if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
            if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
            if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= MOD_WIN;
            return mods;
        }
    }

    auto parse_hotkey_binding(const std::string& text) -> HotkeyBinding
    {
        HotkeyBinding binding{};
        binding.vk = VK_F10;
        binding.modifiers = 0;
        for (const auto& raw : split_tokens(text.empty() ? std::string("F10") : text))
        {
            const auto token = upper_copy(raw);
            if (token == "CTRL" || token == "CONTROL") binding.modifiers |= MOD_CONTROL;
            else if (token == "ALT") binding.modifiers |= MOD_ALT;
            else if (token == "SHIFT") binding.modifiers |= MOD_SHIFT;
            else if (token == "WIN" || token == "WINDOWS") binding.modifiers |= MOD_WIN;
            else binding.vk = vk_from_label(token, binding.vk);
        }
        if (is_modifier_vk(binding.vk))
            binding.vk = VK_F10;
        return binding;
    }

    auto hotkey_to_string(const HotkeyBinding& binding) -> std::string
    {
        std::string out;
        if (binding.modifiers & MOD_CONTROL) out += "Ctrl+";
        if (binding.modifiers & MOD_ALT) out += "Alt+";
        if (binding.modifiers & MOD_SHIFT) out += "Shift+";
        if (binding.modifiers & MOD_WIN) out += "Win+";
        out += vk_label(binding.vk);
        return out;
    }

    auto hotkey_backend_json(const HotkeyBinding& binding, bool registered) -> std::string
    {
        return std::string("{\"paint\":\"") + (registered ? "register_hotkey" : "async_state") +
               "\",\"paint_key\":\"" + hotkey_to_string(binding) + "\"}";
    }

    auto try_capture_hotkey_from_message(const MSG& msg, HotkeyBinding& out, std::string& error, bool& cancel) -> bool
    {
        cancel = false;
        if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN)
            return false;
        const UINT vk = static_cast<UINT>(msg.wParam);
        if (vk == VK_ESCAPE)
        {
            cancel = true;
            return false;
        }
        if (is_modifier_vk(vk))
        {
            error = "Modifier-only hotkeys are not valid.";
            return false;
        }
        out.vk = vk;
        out.modifiers = current_modifiers();
        error.clear();
        return true;
    }

    OverlayHotkeys::OverlayHotkeys(HotkeyBinding paint)
        : paint_(paint)
    {
        set_paint_hotkey(paint_);
    }

    OverlayHotkeys::~OverlayHotkeys()
    {
        unregister_paint();
    }

    void OverlayHotkeys::unregister_paint()
    {
        if (paint_registered_)
            UnregisterHotKey(nullptr, 1);
        paint_registered_ = false;
    }

    auto OverlayHotkeys::set_paint_hotkey(HotkeyBinding paint, std::string* error) -> bool
    {
        if (paint_registered_ && paint.vk == paint_.vk && paint.modifiers == paint_.modifiers)
            return true;

        const HotkeyBinding previous = paint_;
        const bool previous_registered = paint_registered_;
        unregister_paint();
        paint_ = paint;
        paint_down_ = false;
        paint_registered_ = RegisterHotKey(nullptr, 1, paint_.modifiers | MOD_NOREPEAT, paint_.vk) != FALSE;
        if (!paint_registered_)
        {
            const DWORD code = GetLastError();
            if (error)
                *error = "RegisterHotKey failed win32=" + std::to_string(code);
            paint_ = previous;
            if (previous_registered)
                paint_registered_ = RegisterHotKey(nullptr, 1, paint_.modifiers | MOD_NOREPEAT, paint_.vk) != FALSE;
            return false;
        }
        if (error)
            error->clear();
        return true;
    }

    auto OverlayHotkeys::backend_json() const -> std::string
    {
        return hotkey_backend_json(paint_, paint_registered_);
    }

    void OverlayHotkeys::handle_message(const MSG& msg, OverlayHotkeyState& state) const
    {
        if (msg.message == WM_HOTKEY && msg.wParam == 1)
            state.paint_requested = true;
    }

    void OverlayHotkeys::poll_fallback(OverlayHotkeyState& state)
    {
        if (!paint_registered_)
        {
            const bool down = (GetAsyncKeyState(static_cast<int>(paint_.vk)) & 0x8000) != 0;
            state.paint_requested = state.paint_requested || (down && !paint_down_);
            paint_down_ = down;
        }
    }
}
