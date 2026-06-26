#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace util {

constexpr UINT StatusProgressMessage = WM_APP + 0x4D01;

struct StatusState {
    HWND hwnd{nullptr};
    bool verbose{false};
    bool mute{false};
};

inline StatusState& status_state() {
    static StatusState s;
    return s;
}

inline void init_status(HWND hwnd, bool verbose) {
    auto& s = status_state();
    s.hwnd = hwnd;
    s.verbose = verbose;
}

inline void send_status(const char* text, int progress = -1) {
    const auto& s = status_state();
    if (!s.hwnd || s.mute) return;
    PostMessageW(s.hwnd, StatusProgressMessage, static_cast<WPARAM>(progress), reinterpret_cast<LPARAM>(text));
}

inline auto status_printf(const char* fmt, ...) -> std::string {
    char buf[4096]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';
    if (status_state().verbose) send_status(buf);
    return buf;
}

inline auto mute() -> void { status_state().mute = true; }
inline auto unmute() -> void { status_state().mute = false; }

} // namespace util
