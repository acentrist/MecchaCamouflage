#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace meccha
{
    struct HotkeyBinding
    {
        UINT vk{VK_F10};
        UINT modifiers{0};
    };

    struct OverlayHotkeyState
    {
        bool paint_requested{false};
    };

    auto parse_hotkey_binding(const std::string& text) -> HotkeyBinding;
    auto hotkey_to_string(const HotkeyBinding& binding) -> std::string;
    auto hotkey_backend_json(const HotkeyBinding& binding, bool registered) -> std::string;
    auto try_capture_hotkey_from_message(const MSG& msg, HotkeyBinding& out, std::string& error, bool& cancel) -> bool;

    class OverlayHotkeys
    {
    public:
        explicit OverlayHotkeys(HotkeyBinding paint);
        ~OverlayHotkeys();

        auto set_paint_hotkey(HotkeyBinding paint, std::string* error = nullptr) -> bool;
        auto backend_json() const -> std::string;
        auto paint_binding() const -> HotkeyBinding { return paint_; }
        auto paint_registered() const -> bool { return paint_registered_; }
        void handle_message(const MSG& msg, OverlayHotkeyState& state) const;
        void poll_fallback(OverlayHotkeyState& state);

    private:
        void unregister_paint();

        HotkeyBinding paint_{};
        bool paint_registered_{false};
        bool paint_down_{false};
    };
}
