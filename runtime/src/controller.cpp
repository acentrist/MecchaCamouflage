#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dwmapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <filesystem>

#include "controller_events.hpp"
#include "controller_hotkeys.hpp"
#include "controller_settings.hpp"
#include "controller_ui.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "D3d11.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif

#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace
{
    constexpr int BridgeResourceId = 101;
    constexpr int AppIconResourceId = 201;
    constexpr LONG MinAppWindowWidth = 1180;
    constexpr LONG MinAppWindowHeight = 780;
    constexpr LONG DefaultAppWindowInset = 80;
    constexpr LONG CustomTitleBarHeight = 36;
    constexpr LONG CustomCloseButtonWidth = 38;
    constexpr char DefaultBridgeHost[] = "127.0.0.1";
    constexpr int DefaultBridgePort = 0;
    using meccha::AppSettings;
    using meccha::HotkeyBinding;
    using meccha::OverlayHotkeyState;
    using meccha::OverlayHotkeys;
    using meccha::PaintTuning;
    using meccha::RuntimeEvent;
    using meccha::RuntimeEventBuffer;
    using meccha::TraceLogBuffer;

    struct Config
    {
        std::wstring mode{L"service"};
        std::wstring game_process_name{meccha::DefaultGameProcessName};
        std::wstring log_dir{};
        std::string bridge_host{DefaultBridgeHost};
        int bridge_port{DefaultBridgePort};
        double bridge_timeout_seconds{360.0};
        double status_interval_seconds{2.0};
        double frame_delay_ms{16.0};
        std::string native_apply_mode{"mesh_first_paint"};
        DWORD parent_pid{0};
        PaintTuning tuning{};
    };

    struct ProcessInfo
    {
        DWORD pid{0};
        std::wstring name{};
    };

    struct BridgeResponse
    {
        bool ok{false};
        bool success{false};
        std::string stage{};
        std::string message{};
        std::string raw{};
        std::string transport_error{};
        DWORD win32_error{0};
    };

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

    auto lower_ascii(std::wstring value) -> std::wstring
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return value;
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
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
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

    auto now_utc_iso() -> std::string
    {
        SYSTEMTIME st{};
        GetSystemTime(&st);
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03u+00:00",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return buffer;
    }

    auto seconds_now() -> double
    {
        using clock = std::chrono::steady_clock;
        static const auto start = clock::now();
        return std::chrono::duration<double>(clock::now() - start).count();
    }

    auto unix_time_seconds() -> std::uint64_t
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    auto default_log_dir() -> std::filesystem::path
    {
        wchar_t buffer[32768]{};
        const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
        if (size > 0 && size < std::size(buffer))
            return std::filesystem::path(buffer) / L"MecchaCamouflage" / L"runtime";
        return std::filesystem::temp_directory_path() / L"MecchaCamouflage" / L"runtime";
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

    auto append_text_file(const std::filesystem::path& path, const std::string& text) -> void
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (file)
            file << text;
    }

    auto date_from_timestamp(const std::string& timestamp) -> std::string
    {
        return timestamp.size() >= 10 ? timestamp.substr(0, 10) : "unknown-date";
    }

    auto clock_from_timestamp(const std::string& timestamp) -> std::string
    {
        return timestamp.size() >= 19 ? timestamp.substr(11, 8) : "--:--:--";
    }

    auto pretty_text(std::string text) -> std::string
    {
        std::replace(text.begin(), text.end(), '_', ' ');
        if (!text.empty())
            text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return text;
    }

    auto fnv1a64(const void* data, std::size_t size) -> std::uint64_t
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    auto hex64(std::uint64_t value) -> std::string
    {
        std::ostringstream out;
        out << std::hex << std::setw(16) << std::setfill('0') << value;
        return out.str();
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
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
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

    auto extract_json_number(const std::string& text, const std::string& key, double fallback = 0.0) -> double
    {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return fallback;
        const char* begin = text.c_str() + start + needle.size();
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        return end == begin ? fallback : value;
    }

    auto format_summary_number(double value, int precision) -> std::string
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(precision) << value;
        return out.str();
    }

    auto json_bool_text(bool value) -> const char*
    {
        return value ? "true" : "false";
    }

    auto extract_json_bool(const std::string& text, const std::string& key, bool fallback = false) -> bool
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
        return text.compare(pos, 4, "true") == 0;
    }

    auto extract_bridge_events(const std::string& text) -> std::vector<std::string>
    {
        std::vector<std::string> out;
        const std::string needle = "\"bridge_events\":[";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return out;
        std::size_t i = start + needle.size();
        while (i < text.size() && text[i] != ']')
        {
            if (text[i] != '"')
            {
                ++i;
                continue;
            }
            ++i;
            std::string item;
            bool escape = false;
            for (; i < text.size(); ++i)
            {
                const char c = text[i];
                if (escape)
                {
                    item.push_back(c);
                    escape = false;
                    continue;
                }
                if (c == '\\')
                {
                    escape = true;
                    continue;
                }
                if (c == '"')
                {
                    ++i;
                    break;
                }
                item.push_back(c);
            }
            if (!item.empty())
                out.push_back(item);
        }
        return out;
    }

    class Diagnostics
    {
    public:
        explicit Diagnostics(std::filesystem::path log_dir, RuntimeEventBuffer* events = nullptr, int retention_days = 14)
            : log_dir_(std::move(log_dir)),
              status_path_(log_dir_ / L"last_status.json"),
              latest_error_path_(log_dir_ / L"latest_error.json"),
              events_(events),
              retention_days_(std::max(1, retention_days))
        {
            std::error_code ec;
            std::filesystem::create_directories(log_dir_, ec);
            prune_old_logs();
        }

        auto log_dir() const -> const std::filesystem::path& { return log_dir_; }
        auto status_path() const -> const std::filesystem::path& { return status_path_; }

        void set_process(std::string json) { std::lock_guard<std::mutex> lock(mutex_); process_json_ = std::move(json); write_status_locked(); }
        void set_bridge(std::string json) { std::lock_guard<std::mutex> lock(mutex_); bridge_json_ = std::move(json); write_status_locked(); }
        void set_hotkey(std::string json) { std::lock_guard<std::mutex> lock(mutex_); hotkey_json_ = std::move(json); write_status_locked(); }
        void set_last_run(std::string json) { std::lock_guard<std::mutex> lock(mutex_); last_run_json_ = std::move(json); write_status_locked(); }
        void clear_error()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_error_json_ = "null";
            std::error_code ec;
            std::filesystem::remove(latest_error_path_, ec);
            write_status_locked();
        }

        void event(const std::string& name,
                   const std::string& level,
                   const std::string& stage,
                   const std::string& message,
                   const std::string& details_json = "{}",
                   const std::string& run_id = "")
        {
            const std::string details = details_json.empty() ? "{}" : details_json;
            RuntimeEvent event_record = meccha::make_runtime_event(name, level, stage, message, details, run_id);

            const std::string entry = std::string("{\"timestamp\":") + json_string(event_record.timestamp) +
                                      ",\"level\":" + json_string(level) +
                                      ",\"domain\":" + json_string(event_record.domain) +
                                      ",\"event\":" + json_string(name) +
                                      ",\"title\":" + json_string(event_record.title) +
                                      ",\"run_id\":" + json_string(run_id) +
                                      ",\"stage\":" + json_string(stage) +
                                      ",\"message\":" + json_string(message) +
                                      ",\"progress\":" + (event_record.progress >= 0.0 ? std::to_string(event_record.progress) : "null") +
                                      ",\"details\":" + details + "}";
            const std::string line = event_record.clock + " " + event_record.level + " " + event_record.domain + " " +
                                     event_record.title + " " + message + "\n";
            {
                std::lock_guard<std::mutex> lock(mutex_);
                append_text_file(events_path_for(event_record.timestamp), entry + "\n");
                append_text_file(runtime_log_path_for(event_record.timestamp), line);
            }
            if (events_)
                events_->push(std::move(event_record));
        }

        void record_error(const std::string& stage, const std::string& message, const std::string& details_json = "{}", const std::string& run_id = "")
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                last_error_json_ = std::string("{\"timestamp\":") + json_string(now_utc_iso()) +
                                   ",\"stage\":" + json_string(stage) +
                                   ",\"message\":" + json_string(message) +
                                   ",\"details\":" + (details_json.empty() ? "{}" : details_json) +
                                   ",\"run_id\":" + json_string(run_id) + "}";
                write_text_file(latest_error_path_, last_error_json_ + "\n");
                write_status_locked();
            }
            event("runtime_error", "error", stage, message, details_json, run_id);
        }

        void write_status()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            write_status_locked();
        }

    private:
        void write_status_locked()
        {
            const std::string status = std::string("{\n") +
                "  \"process\": " + (process_json_.empty() ? "{}" : process_json_) + ",\n" +
                "  \"bridge\": " + (bridge_json_.empty() ? "{}" : bridge_json_) + ",\n" +
                "  \"hotkey\": " + (hotkey_json_.empty() ? "{}" : hotkey_json_) + ",\n" +
                "  \"last_run\": " + (last_run_json_.empty() ? "{}" : last_run_json_) + ",\n" +
                "  \"last_error\": " + last_error_json_ + ",\n" +
                "  \"log_path\": " + json_string(wide_to_utf8(log_dir_.wstring())) + "\n" +
                "}\n";
            const auto tmp = status_path_.wstring() + L".tmp";
            if (write_text_file(tmp, status))
            {
                std::error_code ec;
                std::filesystem::rename(tmp, status_path_, ec);
                if (ec)
                {
                    std::filesystem::remove(status_path_, ec);
                    std::filesystem::rename(tmp, status_path_, ec);
                }
            }
        }
        auto events_path_for(const std::string& timestamp) const -> std::filesystem::path
        {
            return log_dir_ / utf8_to_wide("events-" + date_from_timestamp(timestamp) + ".jsonl");
        }

        auto runtime_log_path_for(const std::string& timestamp) const -> std::filesystem::path
        {
            return log_dir_ / utf8_to_wide("runtime-" + date_from_timestamp(timestamp) + ".log");
        }

        void prune_old_logs()
        {
            std::error_code ec;
            const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * retention_days_);
            for (const auto& entry : std::filesystem::directory_iterator(log_dir_, ec))
            {
                if (ec || !entry.is_regular_file(ec))
                    continue;
                const auto name = wide_to_utf8(entry.path().filename().wstring());
                const bool rotated = name.rfind("events-", 0) == 0 || name.rfind("runtime-", 0) == 0;
                if (!rotated)
                    continue;
                if (entry.last_write_time(ec) < cutoff)
                    std::filesystem::remove(entry.path(), ec);
            }
        }

        static auto upper(std::string text) -> std::string
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return text;
        }

        static auto pretty(std::string text) -> std::string
        {
            std::replace(text.begin(), text.end(), '_', ' ');
            return text;
        }

        mutable std::mutex mutex_;
        std::filesystem::path log_dir_;
        std::filesystem::path status_path_;
        std::filesystem::path latest_error_path_;
        RuntimeEventBuffer* events_{nullptr};
        int retention_days_{14};
        std::string process_json_{};
        std::string bridge_json_{};
        std::string hotkey_json_{};
        std::string last_run_json_{};
        std::string last_error_json_{"null"};
    };

    auto process_json(const ProcessInfo& process, const std::wstring& target_name) -> std::string
    {
        if (process.pid == 0)
        {
            return std::string("{\"attached\":false,\"target_name\":") + json_string(wide_to_utf8(target_name)) + "}";
        }
        return std::string("{\"attached\":true,\"pid\":") + std::to_string(process.pid) +
               ",\"name\":" + json_string(wide_to_utf8(process.name)) +
               ",\"target_name\":" + json_string(wide_to_utf8(target_name)) +
               ",\"source\":\"toolhelp32\"}";
    }

    auto bridge_json(const Config& config, const std::filesystem::path& bridge_path, const std::string& state, const BridgeResponse& response = {}) -> std::string
    {
        return std::string("{\"state\":") + json_string(state) +
               ",\"adapter\":\"bridge\"" +
               ",\"host\":" + json_string(config.bridge_host) +
               ",\"port\":" + std::to_string(config.bridge_port) +
               ",\"bridge_path\":" + json_string(wide_to_utf8(bridge_path.wstring())) +
               ",\"stage\":" + json_string(response.stage) +
               ",\"message\":" + json_string(response.message.empty() ? response.transport_error : response.message) +
               ",\"win32_error\":" + std::to_string(response.win32_error) + "}";
    }

    auto find_process_by_name(const std::wstring& expected_name) -> ProcessInfo
    {
        const std::wstring expected = lower_ascii(expected_name);
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return {};
        ProcessInfo result{};
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (lower_ascii(entry.szExeFile) == expected)
                {
                    result.pid = entry.th32ProcessID;
                    result.name = entry.szExeFile;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
    }

    auto choose_bridge_port(const std::string& host) -> int
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
            return 47654;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        addr.sin_port = 0;
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            closesocket(s);
            return 47654;
        }
        sockaddr_in bound{};
        int len = sizeof(bound);
        getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
        const int port = ntohs(bound.sin_port);
        closesocket(s);
        return port > 0 ? port : 47654;
    }

    auto parse_response(std::string raw) -> BridgeResponse
    {
        BridgeResponse response{};
        response.ok = !raw.empty();
        response.raw = std::move(raw);
        response.success = extract_json_bool(response.raw, "success");
        response.stage = extract_json_string(response.raw, "stage");
        response.message = extract_json_string(response.raw, "message");
        if (response.stage.empty())
            response.stage = response.success ? "ok" : "bridge_response";
        return response;
    }

    class BridgeClient
    {
    public:
        BridgeClient(std::string host, int port, double timeout_seconds)
            : host_(std::move(host)), port_(port), timeout_ms_(static_cast<int>(std::max(0.1, timeout_seconds) * 1000.0)) {}

        auto request(const std::string& command, const std::string& payload_json = "{}") const -> BridgeResponse
        {
            SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == INVALID_SOCKET)
                return fail("socket_failed", WSAGetLastError());
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms_), sizeof(timeout_ms_));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms_), sizeof(timeout_ms_));
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<u_short>(port_));
            inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
            if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
            {
                const DWORD err = WSAGetLastError();
                closesocket(s);
                return fail("connect_failed", err);
            }
            const std::string line = make_request_line(command, payload_json);
            const int sent = send(s, line.c_str(), static_cast<int>(line.size()), 0);
            if (sent <= 0)
            {
                const DWORD err = WSAGetLastError();
                closesocket(s);
                return fail("send_failed", err);
            }
            std::string raw;
            raw.reserve(65536);
            char buffer[16384]{};
            while (raw.find('\n') == std::string::npos && raw.size() < 8 * 1024 * 1024)
            {
                const int received = recv(s, buffer, static_cast<int>(sizeof(buffer)), 0);
                if (received <= 0)
                    break;
                raw.append(buffer, static_cast<std::size_t>(received));
            }
            closesocket(s);
            if (raw.empty())
                return fail("empty_bridge_response", 0);
            if (const auto newline = raw.find('\n'); newline != std::string::npos)
                raw.resize(newline);
            return parse_response(raw);
        }

    private:
        static auto fail(const std::string& message, DWORD error) -> BridgeResponse
        {
            BridgeResponse response{};
            response.ok = false;
            response.success = false;
            response.stage = "bridge_connect";
            response.transport_error = message;
            response.win32_error = error;
            response.message = message;
            return response;
        }

        static auto make_request_line(const std::string& command, const std::string& payload_json) -> std::string
        {
            static std::atomic<unsigned long long> counter{1};
            const auto id = counter.fetch_add(1);
            const std::string request_id = hex64(fnv1a64(&id, sizeof(id))) + hex64(GetTickCount64());
            return std::string("{\"type\":") + json_string(command) +
                   ",\"request_id\":" + json_string(request_id) +
                   ",\"timestamp_utc\":" + std::to_string(unix_time_seconds()) +
                   ",\"payload\":" + (payload_json.empty() ? "{}" : payload_json) + "}\n";
        }

        std::string host_;
        int port_;
        int timeout_ms_;
    };

    auto inject_dll(DWORD pid, const std::filesystem::path& dll_path) -> std::pair<bool, std::string>
    {
        HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                     FALSE,
                                     pid);
        if (!process)
            return {false, std::string("OpenProcess failed win32=") + std::to_string(GetLastError())};

        const std::wstring dll = std::filesystem::absolute(dll_path).wstring();
        const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
        LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!remote)
        {
            const DWORD err = GetLastError();
            CloseHandle(process);
            return {false, std::string("VirtualAllocEx failed win32=") + std::to_string(err)};
        }
        if (!WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr))
        {
            const DWORD err = GetLastError();
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            CloseHandle(process);
            return {false, std::string("WriteProcessMemory failed win32=") + std::to_string(err)};
        }
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
        HANDLE thread = CreateRemoteThread(process, nullptr, 0, load_library, remote, 0, nullptr);
        if (!thread)
        {
            const DWORD err = GetLastError();
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            CloseHandle(process);
            return {false, std::string("CreateRemoteThread failed win32=") + std::to_string(err)};
        }
        WaitForSingleObject(thread, 10000);
        DWORD remote_exit = 0;
        GetExitCodeThread(thread, &remote_exit);
        CloseHandle(thread);
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        if (remote_exit == 0)
            return {false, "LoadLibraryW failed in target process"};
        return {true, std::string("injected pid=") + std::to_string(pid) + " dll=" + wide_to_utf8(dll)};
    }

    auto find_mesh_profile_directory(const std::filesystem::path& exe_dir, const std::filesystem::path& log_dir) -> std::filesystem::path
    {
        std::error_code ec;
        const std::filesystem::path candidates[]{
            exe_dir / L"mesh-profiles",
            exe_dir.parent_path() / L"mesh-profiles",
            log_dir / L"native" / L"mesh-profiles",
        };
        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec))
                return candidate;
        }
        (void)log_dir;
        return {};
    }

    auto sync_mesh_profile_directory(const std::filesystem::path& exe_dir,
                                     const std::filesystem::path& log_dir,
                                     const std::filesystem::path& bridge_path) -> bool
    {
        const auto source = find_mesh_profile_directory(exe_dir, log_dir);
        if (source.empty())
            return false;
        std::error_code ec;
        const auto target = bridge_path.parent_path() / L"mesh-profiles";
        std::filesystem::create_directories(target, ec);
        if (ec)
            return false;
        bool copied = false;
        for (const auto& entry : std::filesystem::directory_iterator(source, ec))
        {
            if (ec)
                break;
            const auto path = entry.path();
            if (path.extension() != L".json")
                continue;
            std::filesystem::copy_file(path, target / path.filename(), std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
                return false;
            copied = true;
        }
        return copied;
    }

    auto extract_embedded_bridge(const std::filesystem::path& log_dir, int port) -> std::filesystem::path
    {
        HMODULE module = GetModuleHandleW(nullptr);
        HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(BridgeResourceId), MAKEINTRESOURCEW(10));
        if (!resource)
            throw std::runtime_error("embedded bridge resource not found");
        HGLOBAL loaded = LoadResource(module, resource);
        const DWORD size = SizeofResource(module, resource);
        const void* data = LockResource(loaded);
        if (!data || size == 0)
            throw std::runtime_error("embedded bridge resource is empty");
        const std::string hash = hex64(fnv1a64(data, size));
        const auto native_dir = log_dir / L"native";
        std::error_code ec;
        std::filesystem::create_directories(native_dir, ec);
        const auto bridge_path = native_dir / utf8_to_wide("runtime-bridge-" + hash + "-" + std::to_string(port) + ".dll");
        bool write = true;
        if (std::filesystem::exists(bridge_path, ec))
        {
            write = std::filesystem::file_size(bridge_path, ec) != size;
        }
        if (write)
        {
            std::ofstream file(bridge_path, std::ios::binary | std::ios::trunc);
            if (!file)
                throw std::runtime_error("failed to write embedded bridge dll");
            file.write(static_cast<const char*>(data), size);
        }
        wchar_t exe_path[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0)
        {
            const auto exe_dir = std::filesystem::path(exe_path).parent_path();
            sync_mesh_profile_directory(exe_dir, log_dir, bridge_path);
        }
        return bridge_path;
    }

    auto write_bridge_port_file(const std::filesystem::path& bridge_path, int port) -> bool
    {
        return write_text_file(std::filesystem::path(bridge_path.wstring() + L".port"), std::to_string(port) + "\n");
    }

    auto bridge_progress_file(const std::filesystem::path& bridge_path) -> std::filesystem::path
    {
        return std::filesystem::path(bridge_path.wstring() + L".progress.json");
    }

    enum class ControllerServiceState
    {
        Running,
        Stopped,
        Starting,
        Stopping,
        Error,
    };

    auto service_state_text(ControllerServiceState state) -> const char*
    {
        switch (state)
        {
        case ControllerServiceState::Running:
            return "Running";
        case ControllerServiceState::Stopped:
            return "Stopped";
        case ControllerServiceState::Starting:
            return "Starting";
        case ControllerServiceState::Stopping:
            return "Stopping";
        case ControllerServiceState::Error:
            return "Error";
        }
        return "Error";
    }

    struct BridgeCheckResult
    {
        bool ready{false};
        DWORD pid{0};
        DWORD injected_pid{0};
    };

    struct OverlayServiceState
    {
        ProcessInfo process{};
        ControllerServiceState state{ControllerServiceState::Running};
        bool bridge_ready{false};
        bool waiting_for_hotkey_logged{false};
        bool paint_running{false};
        std::string bridge_state{"not_ready"};
        std::string paint_state{"Idle"};
        std::string last_result{"Waiting"};
        std::future<bool> paint_future{};
        bool paint_future_active{false};
        std::future<BridgeResponse> stop_future{};
        bool stop_future_active{false};
        std::future<BridgeCheckResult> bridge_check_future{};
        bool bridge_check_future_active{false};
        DWORD bridge_check_pid{0};
        double paint_started_at{0.0};
        DWORD injected_pid{0};
        bool waiting_for_process_logged{false};
        bool not_ready_hotkey_logged{false};
        bool duplicate_paint_hotkey_logged{false};
        bool paint_after_ready_requested{false};
        double last_bridge_check{0.0};
        double paint_settle_until{0.0};
    };

    struct OverlayD3DState
    {
        ID3D11Device* device{nullptr};
        ID3D11DeviceContext* context{nullptr};
        IDXGISwapChain* swap_chain{nullptr};
        ID3D11RenderTargetView* render_target{nullptr};
    };

    OverlayD3DState g_overlay_d3d{};

    auto create_render_target() -> void
    {
        ID3D11Texture2D* back_buffer = nullptr;
        if (g_overlay_d3d.swap_chain &&
            SUCCEEDED(g_overlay_d3d.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))))
        {
            g_overlay_d3d.device->CreateRenderTargetView(back_buffer, nullptr, &g_overlay_d3d.render_target);
            back_buffer->Release();
        }
    }

    auto cleanup_render_target() -> void
    {
        if (g_overlay_d3d.render_target)
        {
            g_overlay_d3d.render_target->Release();
            g_overlay_d3d.render_target = nullptr;
        }
    }

    auto create_device_d3d(HWND hwnd) -> bool
    {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        UINT create_device_flags = 0;
        D3D_FEATURE_LEVEL feature_level{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        const HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr,
                                                         D3D_DRIVER_TYPE_HARDWARE,
                                                         nullptr,
                                                         create_device_flags,
                                                         levels,
                                                         2,
                                                         D3D11_SDK_VERSION,
                                                         &sd,
                                                         &g_overlay_d3d.swap_chain,
                                                         &g_overlay_d3d.device,
                                                         &feature_level,
                                                         &g_overlay_d3d.context);
        if (FAILED(hr))
            return false;
        create_render_target();
        return true;
    }

    auto cleanup_device_d3d() -> void
    {
        cleanup_render_target();
        if (g_overlay_d3d.swap_chain) { g_overlay_d3d.swap_chain->Release(); g_overlay_d3d.swap_chain = nullptr; }
        if (g_overlay_d3d.context) { g_overlay_d3d.context->Release(); g_overlay_d3d.context = nullptr; }
        if (g_overlay_d3d.device) { g_overlay_d3d.device->Release(); g_overlay_d3d.device = nullptr; }
    }

    auto custom_close_hit(HWND hwnd, LPARAM lparam) -> bool
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        const LONG x = static_cast<SHORT>(LOWORD(lparam));
        const LONG y = static_cast<SHORT>(HIWORD(lparam));
        return y >= 0 && y <= CustomTitleBarHeight && x >= client.right - CustomCloseButtonWidth;
    }

    LRESULT CALLBACK overlay_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
    {
        if ((msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) && custom_close_hit(hwnd, lparam))
        {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
            return true;
        switch (msg)
        {
        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = MinAppWindowWidth;
            info->ptMinTrackSize.y = MinAppWindowHeight;
            return 0;
        }
        case WM_SIZE:
            if (g_overlay_d3d.device && wparam != SIZE_MINIMIZED)
            {
                cleanup_render_target();
                g_overlay_d3d.swap_chain->ResizeBuffers(0, LOWORD(lparam), HIWORD(lparam), DXGI_FORMAT_UNKNOWN, 0);
                create_render_target();
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto primary_overlay_rect() -> RECT
    {
        RECT rect{};
        rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return rect;
    }

    auto initial_app_rect(const AppSettings& settings) -> RECT
    {
        const RECT desktop = primary_overlay_rect();
        const LONG width = static_cast<LONG>(std::max(static_cast<float>(MinAppWindowWidth), settings.panel_width));
        const LONG height = static_cast<LONG>(std::max(static_cast<float>(MinAppWindowHeight), settings.panel_height));
        RECT rect{};
        if (settings.panel_x >= 0.0f && settings.panel_y >= 0.0f)
        {
            rect.left = static_cast<LONG>(settings.panel_x);
            rect.top = static_cast<LONG>(settings.panel_y);
        }
        else
        {
            rect.left = desktop.left + DefaultAppWindowInset;
            rect.top = desktop.top + DefaultAppWindowInset;
        }
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
        return rect;
    }

    auto is_process_alive(DWORD pid) -> bool
    {
        if (!pid)
            return true;
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!process)
            return false;
        const DWORD wait = WaitForSingleObject(process, 0);
        CloseHandle(process);
        return wait == WAIT_TIMEOUT;
    }

    auto foreground_process_id() -> DWORD
    {
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return 0;
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        return pid;
    }

    auto mode_to_route(const std::string& native_apply_mode) -> std::string
    {
        if (native_apply_mode == "mesh_first_paint") return "f10_mesh_first_paint";
        return "unsupported_route";
    }

    auto is_supported_native_apply_mode(const std::string& native_apply_mode) -> bool
    {
        return native_apply_mode == "mesh_first_paint";
    }

    struct MeshUiSummary
    {
        std::string mesh{};
        std::string planner{};
        std::string replay{};
        std::string server_eta{};
        std::string server_elapsed{};
        std::string apply_eta{};
        std::string apply_elapsed{};
        std::string unsafe{};
    };

    auto format_duration_label(double milliseconds) -> std::string;

    auto file_write_age_ms(const std::filesystem::path& path) -> double
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.wstring().c_str(), GetFileExInfoStandard, &data))
            return 0.0;
        FILETIME now_filetime{};
        GetSystemTimeAsFileTime(&now_filetime);
        ULARGE_INTEGER now{};
        now.LowPart = now_filetime.dwLowDateTime;
        now.HighPart = now_filetime.dwHighDateTime;
        ULARGE_INTEGER written{};
        written.LowPart = data.ftLastWriteTime.dwLowDateTime;
        written.HighPart = data.ftLastWriteTime.dwHighDateTime;
        if (now.QuadPart <= written.QuadPart)
            return 0.0;
        return static_cast<double>(now.QuadPart - written.QuadPart) / 10000.0;
    }

    auto metric_blank(const std::string& value) -> bool
    {
        return value.empty() || value == "-";
    }

    void apply_live_progress_metrics(meccha::UiRuntimeState& ui_runtime,
                                     const std::filesystem::path& progress_path)
    {
        const std::string progress = read_text_file(progress_path);
        if (progress.empty())
            return;

        if (extract_json_number(progress, "progress_schema_version", 0.0) < 2.0)
            return;

        const std::string phase = extract_json_string(progress, "phase");
        const bool terminal = extract_json_bool(progress, "terminal", false);
        const double age_ms = terminal ? 0.0 : file_write_age_ms(progress_path);
        const double server_elapsed_ms = extract_json_number(progress, "server_elapsed_ms",
                                                             extract_json_number(progress, "server_batch_elapsed_ms", -1.0));
        const double server_eta_ms = extract_json_number(progress, "server_eta_ms", -1.0);
        const double paint_elapsed_ms = extract_json_number(progress, "paint_elapsed_ms",
                                                            extract_json_number(progress, "elapsed_ms", -1.0));
        const double paint_eta_ms = extract_json_number(progress, "paint_eta_ms", -1.0);

        if (server_elapsed_ms >= 0.0)
        {
            const bool server_running = phase == "server_batch" && !terminal;
            ui_runtime.metric_server_elapsed = format_duration_label(server_elapsed_ms + (server_running ? age_ms : 0.0));
        }
        if (server_eta_ms >= 0.0)
            ui_runtime.metric_server_eta = format_duration_label(std::max(0.0, server_eta_ms - age_ms));
        else if (phase == "local_sync" || terminal)
            ui_runtime.metric_server_eta = "0s";
        else
            ui_runtime.metric_server_eta = "Calculating";

        if (paint_elapsed_ms >= 0.0)
            ui_runtime.metric_apply_elapsed = format_duration_label(paint_elapsed_ms + (!terminal ? age_ms : 0.0));
        if (paint_eta_ms >= 0.0)
            ui_runtime.metric_apply_eta = format_duration_label(std::max(0.0, paint_eta_ms - age_ms));
        else
            ui_runtime.metric_apply_eta = terminal ? "0s" : "Calculating";
    }

    auto latest_mesh_ui_summary(const std::vector<meccha::RuntimeEvent>& events) -> MeshUiSummary
    {
        MeshUiSummary summary{};
        bool saw_relevant = false;
        for (auto it = events.rbegin(); it != events.rend(); ++it)
        {
            if (it->event == "paint_triggered")
                break;
            if (it->event != "paint_done" &&
                it->event != "paint_failed" &&
                it->event != "paint_run_summary" &&
                it->event != "paint_progress" &&
                it->event != "paint_waiting_commit")
                continue;
            saw_relevant = true;
            const auto& details = it->details_json;
            const std::string stage = it->stage.empty() ? extract_json_string(details, "stage") : it->stage;
            const bool profile_ok = extract_json_bool(details, "mesh_profile_ok", false);
            const auto profile_id = extract_json_string(details, "profile_id");
            const auto texture_size = extract_json_number(details, "texture_size", -1.0);
            const auto mesh_lod = extract_json_number(details, "mesh_profile_lod", -1.0);
            summary.mesh = profile_ok ? "Profile V2" : "Profile unavailable";
            if (mesh_lod >= 0.0)
                summary.mesh += " LOD" + std::to_string(static_cast<long long>(mesh_lod));
            if (texture_size > 0.0)
                summary.mesh += " " + std::to_string(static_cast<long long>(texture_size)) + "px";
            if (!profile_id.empty())
                summary.mesh += " (" + profile_id.substr(0, std::min<std::size_t>(24, profile_id.size())) + ")";

            const auto enabled = extract_json_number(details, "planner_samples_enabled", -1.0);
            const auto unsafe = extract_json_number(details, "unsafe_enabled", -1.0);
            const auto brush_texels = extract_json_number(details, "stroke_radius_texels", -1.0);
            const auto coverage_texels = extract_json_number(details, "planner_coverage_step_texels", -1.0);
            const auto elapsed = extract_json_number(details, "elapsed_ms", -1.0);
            const auto server_elapsed_done = extract_json_number(details, "server_elapsed_ms",
                                                                  extract_json_number(details, "server_batch_elapsed_ms", -1.0));
            const auto paint_elapsed_done = extract_json_number(details, "paint_elapsed_ms",
                                                                 extract_json_number(details, "total_replay_elapsed_ms", elapsed));
            const auto server_eta = extract_json_number(details, "server_eta_ms", -1.0);
            const auto paint_eta = extract_json_number(details, "paint_eta_ms", -1.0);
            const bool final_replay_metrics = server_elapsed_done >= 0.0 || paint_elapsed_done >= 0.0 ||
                                             it->event == "paint_done" || stage == "mesh_paint_done" ||
                                             stage == "mesh_first_paint_done" ||
                                             stage == "mesh_paint_cancelled" ||
                                             stage == "mesh_paint_failed";
            if (server_elapsed_done >= 0.0 && summary.server_elapsed.empty())
                summary.server_elapsed = format_duration_label(server_elapsed_done);
            if (paint_elapsed_done >= 0.0 && summary.apply_elapsed.empty())
                summary.apply_elapsed = format_duration_label(paint_elapsed_done);
            if (server_eta >= 0.0 && summary.server_eta.empty())
                summary.server_eta = format_duration_label(server_eta);
            if (paint_eta >= 0.0 && summary.apply_eta.empty())
                summary.apply_eta = format_duration_label(paint_eta);
            if (final_replay_metrics && summary.server_eta.empty())
                summary.server_eta = "0s";
            if (final_replay_metrics && summary.apply_eta.empty())
                summary.apply_eta = "0s";
            if (unsafe >= 0.0)
                summary.unsafe = std::to_string(static_cast<long long>(unsafe));
            if (enabled >= 0.0)
            {
                summary.planner = "strokes " + std::to_string(static_cast<long long>(enabled));
                if (brush_texels > 0.0)
                    summary.planner += ", brush " + format_summary_number(brush_texels, 1) + "tx";
                if (coverage_texels > 0.0)
                    summary.planner += ", step " + format_summary_number(coverage_texels, 1) + "tx";
                if (unsafe >= 0.0)
                    summary.planner += ", unsafe " + std::to_string(static_cast<long long>(unsafe));
            }
            else
            {
                summary.planner = it->stage.empty() ? it->event : it->stage;
            }

            const auto sent = extract_json_number(details, "server_strokes_sent", -1.0);
            const auto batches = extract_json_number(details, "server_batch_calls", -1.0);
            if (sent >= 0.0)
            {
                summary.replay = "sent " + std::to_string(static_cast<long long>(sent));
                if (batches >= 0.0)
                    summary.replay += ", batches " + std::to_string(static_cast<long long>(batches));
            }
            else
            {
                summary.replay = it->level == "error" ? "blocked" : "-";
            }
            if (!summary.server_eta.empty() &&
                !summary.server_elapsed.empty() &&
                !summary.apply_eta.empty() &&
                !summary.apply_elapsed.empty())
                break;
        }
        if (saw_relevant && summary.server_eta.empty())
            summary.server_eta = "-";
        if (saw_relevant && summary.server_elapsed.empty())
            summary.server_elapsed = "-";
        if (saw_relevant && summary.apply_eta.empty())
            summary.apply_eta = "-";
        if (saw_relevant && summary.apply_elapsed.empty())
            summary.apply_elapsed = "-";
        return summary;
    }

    auto latest_event_message(const std::vector<meccha::RuntimeEvent>& events, const std::string& event_name) -> std::string
    {
        for (auto it = events.rbegin(); it != events.rend(); ++it)
        {
            if (it->event == event_name)
                return it->message;
        }
        return {};
    }

    auto format_duration_label(double milliseconds) -> std::string
    {
        if (milliseconds < 0.0)
            return "-";
        if (milliseconds > 0.0 && milliseconds < 1000.0)
            return "<1s";
        const long long total_seconds = static_cast<long long>((milliseconds + 500.0) / 1000.0);
        const long long minutes = total_seconds / 60;
        const long long seconds = total_seconds % 60;
        if (minutes > 0)
            return std::to_string(minutes) + "m " + std::to_string(seconds) + "s";
        return std::to_string(seconds) + "s";
    }

    auto metric_count(const std::string& details, const std::string& key) -> long long
    {
        const auto value = extract_json_number(details, key, -1.0);
        return value >= 0.0 ? static_cast<long long>(value) : -1;
    }

    void append_metric(std::ostringstream& out, const char* label, long long value)
    {
        if (value >= 0)
            out << ", " << label << "=" << value;
    }

    void append_metric_double(std::ostringstream& out, const char* label, double value, int precision = 1)
    {
        if (value >= 0.0)
            out << ", " << label << "=" << format_summary_number(value, precision);
    }

    auto paint_run_summary_message(const BridgeResponse& response, double elapsed_ms) -> std::string
    {
        const std::string& raw = response.raw;
        std::ostringstream out;
        out << (response.success ? "Paint summary: " : "Paint blocked: ");
        out << (response.stage.empty() ? (response.success ? "ok" : "failed") : response.stage);
        append_metric_double(out, "elapsed_ms", elapsed_ms, 0);
        append_metric(out, "triangles", metric_count(raw, "runtime_triangle_cache_triangles"));
        append_metric(out, "raw_inside", metric_count(raw, "runtime_triangle_raw_projection_inside_view"));
        append_metric(out, "inside", metric_count(raw, "runtime_triangle_projection_inside_view"));
        append_metric_double(out, "rebuild_avg", extract_json_number(raw, "runtime_triangle_world_rebuild_avg_delta", -1.0), 1);
        if (extract_json_bool(raw, "runtime_triangle_cache_warmup_attempted", false))
        {
            out << ", warmup=1";
            append_metric_double(out, "warmup_pre", extract_json_number(raw, "runtime_triangle_cache_warmup_pre_avg_error", -1.0), 1);
            append_metric_double(out, "warmup_post", extract_json_number(raw, "runtime_triangle_cache_warmup_post_avg_error", -1.0), 1);
        }
        append_metric(out, "source", metric_count(raw, "planner_samples_source"));
        append_metric(out, "front", metric_count(raw, "planner_samples_front"));
        append_metric(out, "side", metric_count(raw, "planner_samples_side"));
        append_metric(out, "back", metric_count(raw, "planner_samples_back"));
        append_metric(out, "enabled", metric_count(raw, "planner_samples_enabled"));
        append_metric(out, "unsafe", metric_count(raw, "unsafe_enabled"));
        append_metric(out, "sent", metric_count(raw, "server_strokes_sent"));
        append_metric(out, "batches", metric_count(raw, "server_batch_calls"));
        append_metric(out, "estimated_batches", metric_count(raw, "server_batch_estimated_calls"));
        append_metric(out, "estimated_replay_ms", metric_count(raw, "estimated_replay_ms"));
        append_metric(out, "batch_limit", metric_count(raw, "server_batch_limit"));
        append_metric(out, "batch_delay_ms", metric_count(raw, "server_batch_delay_ms"));
        append_metric_double(out, "brush_tx", extract_json_number(raw, "stroke_size_texels", -1.0), 1);
        append_metric_double(out, "step_tx", extract_json_number(raw, "coverage_step_texels", -1.0), 1);
        append_metric_double(out, "metallic", extract_json_number(raw, "metallic", -1.0), 6);
        append_metric_double(out, "roughness", extract_json_number(raw, "roughness", -1.0), 6);
        return out.str();
    }

    auto paint_run_summary_details(const BridgeResponse& response, double elapsed_ms) -> std::string
    {
        const std::string& raw = response.raw;
        return std::string("{\"success\":") + json_bool_text(response.success) +
               ",\"stage\":" + json_string(response.stage) +
               ",\"message\":" + json_string(response.message.empty() ? response.transport_error : response.message) +
               ",\"elapsed_ms\":" + std::to_string(elapsed_ms) +
               ",\"stroke_size_texels\":" + std::to_string(extract_json_number(raw, "stroke_size_texels", -1.0)) +
               ",\"coverage_step_texels\":" + std::to_string(extract_json_number(raw, "coverage_step_texels", -1.0)) +
               ",\"metallic\":" + std::to_string(extract_json_number(raw, "metallic", -1.0)) +
               ",\"roughness\":" + std::to_string(extract_json_number(raw, "roughness", -1.0)) +
               ",\"server_batch_limit\":" + std::to_string(metric_count(raw, "server_batch_limit")) +
               ",\"server_batch_delay_ms\":" + std::to_string(metric_count(raw, "server_batch_delay_ms")) +
               ",\"enable_front_paint\":" + json_bool_text(extract_json_bool(raw, "enable_front_paint", false)) +
               ",\"enable_side_paint\":" + json_bool_text(extract_json_bool(raw, "enable_side_paint", false)) +
               ",\"enable_back_paint\":" + json_bool_text(extract_json_bool(raw, "enable_back_paint", false)) +
               ",\"runtime_triangle_cache_used\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_used", false)) +
               ",\"runtime_triangle_cache_mode\":" + json_string(extract_json_string(raw, "runtime_triangle_cache_mode")) +
               ",\"runtime_triangle_cache_triangles\":" + std::to_string(metric_count(raw, "runtime_triangle_cache_triangles")) +
               ",\"runtime_triangle_cache_failure\":" + json_string(extract_json_string(raw, "runtime_triangle_cache_failure")) +
               ",\"warmup_attempted\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_attempted", false)) +
               ",\"warmup_pre_avg_error\":" + std::to_string(extract_json_number(raw, "runtime_triangle_cache_warmup_pre_avg_error", -1.0)) +
               ",\"warmup_post_avg_error\":" + std::to_string(extract_json_number(raw, "runtime_triangle_cache_warmup_post_avg_error", -1.0)) +
               ",\"warmup_initialize_available\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_initialize_available", false)) +
               ",\"warmup_initialize_called\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_initialize_called", false)) +
               ",\"warmup_initialize_ok\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_initialize_ok", false)) +
               ",\"warmup_initialize_skip_reason\":" + json_string(extract_json_string(raw, "runtime_triangle_cache_warmup_initialize_skip_reason")) +
               ",\"warmup_hit_uncached_ok\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_hit_test_uncached_ok", false)) +
               ",\"warmup_hit_uncached_hit\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_hit_test_uncached_hit", false)) +
               ",\"warmup_hit_cached_ok\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_hit_test_cached_ok", false)) +
               ",\"warmup_hit_cached_hit\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_cache_warmup_hit_test_cached_hit", false)) +
               ",\"warmup_failure\":" + json_string(extract_json_string(raw, "runtime_triangle_cache_warmup_failure")) +
               ",\"raw_projection_mode\":" + json_string(extract_json_string(raw, "runtime_triangle_raw_projection_mode")) +
               ",\"raw_projection_inside\":" + std::to_string(metric_count(raw, "runtime_triangle_raw_projection_inside_view")) +
               ",\"raw_projection_summary\":" + json_string(extract_json_string(raw, "runtime_triangle_raw_projection_summary")) +
               ",\"world_rebuild_mode\":" + json_string(extract_json_string(raw, "runtime_triangle_world_rebuild_mode")) +
               ",\"world_rebuild_applied\":" + json_bool_text(extract_json_bool(raw, "runtime_triangle_world_rebuild_applied", false)) +
               ",\"world_rebuild_avg_delta\":" + std::to_string(extract_json_number(raw, "runtime_triangle_world_rebuild_avg_delta", -1.0)) +
               ",\"world_rebuild_max_delta\":" + std::to_string(extract_json_number(raw, "runtime_triangle_world_rebuild_max_delta", -1.0)) +
               ",\"projection_mode\":" + json_string(extract_json_string(raw, "runtime_triangle_projection_mode")) +
               ",\"projection_inside\":" + std::to_string(metric_count(raw, "runtime_triangle_projection_inside_view")) +
               ",\"projection_summary\":" + json_string(extract_json_string(raw, "runtime_triangle_projection_summary")) +
               ",\"source_samples\":" + std::to_string(metric_count(raw, "planner_samples_source")) +
               ",\"enabled_samples\":" + std::to_string(metric_count(raw, "planner_samples_enabled")) +
               ",\"unsafe_enabled\":" + std::to_string(metric_count(raw, "unsafe_enabled")) +
               ",\"unsafe_front\":" + std::to_string(metric_count(raw, "unsafe_front")) +
               ",\"unsafe_side\":" + std::to_string(metric_count(raw, "unsafe_side")) +
               ",\"unsafe_back\":" + std::to_string(metric_count(raw, "unsafe_back")) +
               ",\"unsafe_body_region\":" + std::to_string(metric_count(raw, "unsafe_body_region")) +
               ",\"unsafe_limb_group\":" + std::to_string(metric_count(raw, "unsafe_limb_group")) +
               ",\"unsafe_source_distance\":" + std::to_string(metric_count(raw, "unsafe_source_distance")) +
               ",\"server_strokes_sent\":" + std::to_string(metric_count(raw, "server_strokes_sent")) +
               ",\"server_batch_calls\":" + std::to_string(metric_count(raw, "server_batch_calls")) +
               ",\"server_batch_estimated_calls\":" + std::to_string(metric_count(raw, "server_batch_estimated_calls")) +
               ",\"estimated_replay_ms\":" + std::to_string(metric_count(raw, "estimated_replay_ms")) +
               ",\"server_batch_elapsed_ms\":" + std::to_string(extract_json_number(raw, "server_batch_elapsed_ms", -1.0)) +
               ",\"server_elapsed_ms\":" + std::to_string(extract_json_number(raw, "server_elapsed_ms", -1.0)) +
               ",\"server_eta_ms\":" + std::to_string(extract_json_number(raw, "server_eta_ms", -1.0)) +
               ",\"local_visual_sync_elapsed_ms\":" + std::to_string(extract_json_number(raw, "local_visual_sync_elapsed_ms", -1.0)) +
               ",\"local_elapsed_ms\":" + std::to_string(extract_json_number(raw, "local_elapsed_ms", -1.0)) +
               ",\"local_eta_ms\":" + std::to_string(extract_json_number(raw, "local_eta_ms", -1.0)) +
               ",\"paint_elapsed_ms\":" + std::to_string(extract_json_number(raw, "paint_elapsed_ms", -1.0)) +
               ",\"paint_eta_ms\":" + std::to_string(extract_json_number(raw, "paint_eta_ms", -1.0)) + "}";
    }

    auto paint_failure_hint_message(const BridgeResponse& response) -> std::string
    {
        const std::string& raw = response.raw;
        const std::string stage = response.stage;
        if (stage == "runtime_triangle_cache_unavailable" ||
            response.message.find("RuntimePaintable cached current triangles") != std::string::npos)
        {
            const auto failure = extract_json_string(raw, "runtime_triangle_cache_failure");
            std::string hint = "The selected mesh has no usable live RuntimePaintable triangle cache, so replay was blocked before sending strokes.";
            if (!failure.empty())
                hint += " Cache scan detail: " + failure + ".";
            hint += " Test the same default settings on another mesh, or report this mesh as unsupported.";
            return hint;
        }
        if (stage == "planner_source_unavailable")
        {
            return "No camera-facing source samples were found. Aim the camera at a visible painted surface that fills more of the viewport and retry with default settings.";
        }
        if (stage == "runtime_triangle_coordinate_projection_unavailable")
        {
            return "Runtime triangle local-component coordinates did not project into the current camera view. Compare raw_inside, inside, and rebuild_avg in the paint summary.";
        }
        if (stage == "runtime_triangle_coordinate_cache_unstable")
        {
            std::ostringstream out;
            out << "Runtime triangle local/world coordinates diverged from the live component transform, so replay was blocked before painting wrong body parts.";
            append_metric_double(out, "avg_cm", extract_json_number(raw, "runtime_triangle_world_rebuild_avg_delta", -1.0), 1);
            append_metric_double(out, "limit_cm", extract_json_number(raw, "runtime_triangle_coordinate_max_avg_error", -1.0), 1);
            append_metric(out, "raw_inside", metric_count(raw, "runtime_triangle_raw_projection_inside_view"));
            append_metric(out, "inside", metric_count(raw, "runtime_triangle_projection_inside_view"));
            return out.str();
        }
        if (stage == "mesh_source_capture_failed")
        {
            const auto failure = extract_json_string(raw, "front_capture_failure");
            return "SceneCapture could not read BaseColor for the camera-visible source samples." +
                   std::string(failure.empty() ? "" : " Capture detail: " + failure + ".");
        }
        if (stage == "planner_blocked")
        {
            std::ostringstream out;
            out << "Unsafe color-transfer candidates affect enabled regions, so paint was blocked instead of silently skipping samples.";
            append_metric(out, "unsafe_front", metric_count(raw, "unsafe_front"));
            append_metric(out, "unsafe_side", metric_count(raw, "unsafe_side"));
            append_metric(out, "unsafe_back", metric_count(raw, "unsafe_back"));
            append_metric(out, "unsafe_enabled", metric_count(raw, "unsafe_enabled"));
            return out.str();
        }
        if (stage == "mesh_replay_empty")
            return "Planner produced no strokes for the enabled regions. Check that at least one mesh region is enabled and the mesh has valid UV coverage.";
        if (stage == "component_world_transform_unavailable")
            return "The live mesh component transform could not be resolved. Paint is blocked because current pose/world placement is not trustworthy.";
        if (stage == "mesh_profile_missing")
            return "Optional mesh profile data is missing. The runtime mesh cache will be used when available.";
        if (stage == "mesh_profile_identity_mismatch")
            return "Optional mesh profile data did not match the live mesh. The runtime mesh cache will be used when available.";
        if (stage == "mesh_profile_runtime_identity_mismatch")
            return "The live mesh did not match the optional profile data. Paint was blocked instead of replaying with unsafe region mapping.";
        return {};
    }

    auto paint_payload(const Config& config, const ProcessInfo& process) -> std::string
    {
        const bool research_artifacts = std::getenv("MECCHA_RESEARCH_ARTIFACTS") != nullptr;
        auto payload = std::string("{\"native_apply_mode\":") + json_string(config.native_apply_mode) +
                       ",\"route\":" + json_string(mode_to_route(config.native_apply_mode)) +
                       ",\"research_artifacts\":" + (research_artifacts ? "true" : "false") +
                       ",\"process\":{\"pid\":" + std::to_string(process.pid) +
                       ",\"name\":" + json_string(wide_to_utf8(process.name)) + "}" +
                       ",\"tuning\":{\"stroke_size_texels\":" + std::to_string(config.tuning.stroke_size_texels) +
                       ",\"coverage_step_texels\":" + std::to_string(config.tuning.coverage_step_texels) +
                       ",\"side_source_max_uv\":" + std::to_string(config.tuning.side_source_max_uv) +
                       ",\"front_back_source_max_uv\":" + std::to_string(config.tuning.front_back_source_max_uv) +
                       ",\"metallic\":" + std::to_string(config.tuning.metallic) +
                       ",\"roughness\":" + std::to_string(config.tuning.roughness) +
                       ",\"server_batch_limit\":" + std::to_string(config.tuning.server_batch_limit) +
                       ",\"server_batch_delay_ms\":" + std::to_string(config.tuning.server_batch_delay_ms) +
                       ",\"enable_front_paint\":" + (config.tuning.enable_front_paint ? "true" : "false") +
                       ",\"enable_side_paint\":" + (config.tuning.enable_side_paint ? "true" : "false") +
                       ",\"enable_back_paint\":" + (config.tuning.enable_back_paint ? "true" : "false") + "}";
        payload += "}";
        return payload;
    }

    auto wait_for_bridge_ready(const Config& config, const std::filesystem::path& bridge_path, Diagnostics& diagnostics, double seconds = 5.0) -> bool
    {
        BridgeClient client(config.bridge_host, config.bridge_port, config.bridge_timeout_seconds);
        const auto start = seconds_now();
        BridgeResponse last{};
        while (seconds_now() - start < seconds)
        {
            last = client.request("ping");
            if (last.ok && last.success)
            {
                diagnostics.set_bridge(bridge_json(config, bridge_path, "ready", last));
                return true;
            }
            Sleep(250);
        }
        diagnostics.set_bridge(bridge_json(config, bridge_path, "not_ready", last));
        return false;
    }

    auto ensure_bridge(const Config& config,
                       const std::filesystem::path& bridge_path,
                       const ProcessInfo& process,
                       Diagnostics& diagnostics,
                       DWORD& injected_pid) -> bool
    {
        BridgeClient client(config.bridge_host, config.bridge_port, config.bridge_timeout_seconds);
        auto ping = client.request("ping");
        if (ping.ok && ping.success)
        {
            diagnostics.set_bridge(bridge_json(config, bridge_path, "ready", ping));
            return true;
        }
        diagnostics.set_bridge(bridge_json(config, bridge_path, "not_ready", ping));
        if (injected_pid == process.pid)
        {
            diagnostics.event("bridge_unavailable_same_pid",
                              "warning",
                              "bridge",
                              "Bridge unavailable for the injected process; retrying readiness check.",
                              std::string("{\"process\":") + process_json(process, config.game_process_name) +
                                  ",\"bridge_port\":" + std::to_string(config.bridge_port) + "}");
            return false;
        }
        diagnostics.event("inject_started", "info", "inject", "attempting native bridge injection",
                          std::string("{\"process\":") + process_json(process, config.game_process_name) +
                          ",\"bridge_dll\":" + json_string(wide_to_utf8(bridge_path.wstring())) + "}");
        write_bridge_port_file(bridge_path, config.bridge_port);
        const auto [ok, message] = inject_dll(process.pid, bridge_path);
        diagnostics.event(ok ? "inject_done" : "inject_failed", ok ? "info" : "error", "inject",
                          ok ? "native bridge injection completed" : "native bridge injection failed",
                          std::string("{\"message\":") + json_string(message) +
                          ",\"bridge_port\":" + std::to_string(config.bridge_port) + "}");
        if (!ok)
            return false;
        injected_pid = process.pid;
        return wait_for_bridge_ready(config, bridge_path, diagnostics, 5.0);
    }

    auto truncate_for_trace(const std::string& text, std::size_t limit = 1200) -> std::string
    {
        if (text.size() <= limit)
            return text;
        return text.substr(0, limit) + "...";
    }

    auto run_bridge_command(const Config& config,
                            const std::filesystem::path& bridge_path,
                            const std::string& command,
                            const std::string& payload = "{}",
                            TraceLogBuffer* trace = nullptr) -> BridgeResponse
    {
        (void)bridge_path;
        if (trace)
            trace->push("bridge", "request", std::string("{\"command\":") + json_string(command) +
                                             ",\"payload\":" + truncate_for_trace(payload) + "}");
        BridgeClient client(config.bridge_host, config.bridge_port, config.bridge_timeout_seconds);
        BridgeResponse response = client.request(command, payload);
        if (trace)
        {
            trace->push("bridge",
                        "response",
                        std::string("{\"command\":") + json_string(command) +
                        ",\"success\":" + (response.success ? "true" : "false") +
                        ",\"stage\":" + json_string(response.stage) +
                        ",\"message\":" + json_string(response.message.empty() ? response.transport_error : response.message) +
                        ",\"raw\":" + truncate_for_trace(response.raw.empty() ? "{}" : response.raw) + "}");
        }
        return response;
    }

    auto run_paint(const Config& config,
                   const std::filesystem::path& bridge_path,
                   const ProcessInfo& process,
                   Diagnostics& diagnostics,
                   TraceLogBuffer* trace = nullptr) -> bool
    {
        const std::string run_id = hex64(GetTickCount64()) + hex64(process.pid);
        const double start = seconds_now();
        if (!is_supported_native_apply_mode(config.native_apply_mode))
        {
            const std::string details = std::string("{\"native_apply_mode\":") + json_string(config.native_apply_mode) +
                                        ",\"supported_native_apply_modes\":[\"mesh_first_paint\"]}";
            diagnostics.record_error("unsupported_route", "unsupported native apply mode", details, run_id);
            diagnostics.event("paint_failed", "error", "unsupported_route", "unsupported native apply mode", details, run_id);
            return false;
        }
        diagnostics.event("plan_generated", "info", "plan", "native paint payload generated",
                          std::string("{\"native_apply_mode\":") + json_string(config.native_apply_mode) + "}", run_id);
        diagnostics.set_last_run(std::string("{\"run_id\":") + json_string(run_id) +
                                 ",\"stage\":\"paint_started\",\"success\":false,\"process\":" + process_json(process, config.game_process_name) + "}");
        diagnostics.event("paint_started", "info", "paint", "paint_full_route started",
                          std::string("{\"adapter\":\"bridge\",\"native_apply_mode\":") + json_string(config.native_apply_mode) + "}", run_id);
        const std::string payload = paint_payload(config, process);
        const auto progress_path = bridge_progress_file(bridge_path);
        {
            std::error_code ec;
            std::filesystem::remove(progress_path, ec);
        }
        auto future = std::async(std::launch::async, [&]() {
            return run_bridge_command(config, bridge_path, "paint_full_route", payload, trace);
        });
        std::string last_signature;
        auto record_progress_snapshot = [&](const std::string& progress, bool force_terminal) {
            if (progress.empty())
                return;
            const std::string stage = extract_json_string(progress, "stage");
            const double progress_value = extract_json_number(progress, "progress", 0.0);
            const int percent = static_cast<int>(progress_value * 100.0 + 0.5);
            std::string signature{};
            std::string summary{};
            if (stage == "mesh_server_batch_begin")
            {
                signature = "server:0";
                summary = "server batch";
            }
            else if (stage == "mesh_server_batch")
            {
                const auto sent = extract_json_number(progress, "server_strokes_sent", -1.0);
                const auto total = extract_json_number(progress, "server_strokes_total", -1.0);
                const int bucket = total > 0.0
                                       ? static_cast<int>(std::min(100.0, std::floor((sent / total) * 10.0) * 10.0))
                                       : percent;
                signature = "server:" + std::to_string(bucket);
                summary = "server batch";
            }
            else if (stage == "mesh_local_visual_sync")
            {
                const auto synced = extract_json_number(progress, "local_strokes_synced", 0.0);
                const auto total = extract_json_number(progress, "local_strokes_total",
                                                       extract_json_number(progress, "local_sync_strokes_total", -1.0));
                const int bucket = total > 0.0
                                       ? static_cast<int>(std::min(100.0, std::floor((synced / total) * 10.0) * 10.0))
                                       : percent;
                signature = "apply:" + std::to_string(bucket);
                summary = "paint";
            }
            else if (stage == "mesh_paint_done" || stage == "mesh_paint_cancelled" || stage == "mesh_paint_failed")
            {
                signature = stage + ":terminal";
                summary = "paint";
            }
            if (force_terminal && extract_json_bool(progress, "terminal", false))
                signature = stage + ":terminal";
            auto emit_progress = [&](const std::string& emit_stage,
                                     const std::string& emit_summary,
                                     const std::string& emit_details,
                                     const std::string& emit_signature,
                                     std::string& last_seen) {
                if (!emit_stage.empty() && !emit_signature.empty() && emit_signature != last_seen)
                {
                    last_seen = emit_signature;
                    diagnostics.event("paint_progress", "info", emit_stage, emit_summary, emit_details, run_id);
                    diagnostics.set_last_run(std::string("{\"run_id\":") + json_string(run_id) +
                                             ",\"stage\":" + json_string(emit_stage) +
                                             ",\"success\":false,\"progress\":" + emit_details + "}");
                }
            };
            emit_progress(stage, summary, progress, signature, last_signature);
        };
        while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
        {
            record_progress_snapshot(read_text_file(progress_path), false);
        }
        BridgeResponse response = future.get();
        record_progress_snapshot(read_text_file(progress_path), true);
        const double elapsed_ms = (seconds_now() - start) * 1000.0;
        for (const auto& event_name : extract_bridge_events(response.raw))
        {
            if (event_name == "paint_done" || event_name == "paint_failed")
                continue;
            diagnostics.event(event_name, "info", event_name, event_name,
                              std::string("{\"elapsed_ms\":") + std::to_string(elapsed_ms) +
                              ",\"bridge_response\":" + (response.raw.empty() ? "{}" : response.raw) + "}", run_id);
        }
        const std::string summary_details = paint_run_summary_details(response, elapsed_ms);
        diagnostics.event("paint_run_summary",
                          response.success ? "info" : "warning",
                          response.success ? "paint_summary" : "paint_blocked",
                          paint_run_summary_message(response, elapsed_ms),
                          summary_details,
                          run_id);
        if (trace)
            trace->push("paint", "summary", summary_details);
        diagnostics.set_last_run(std::string("{\"run_id\":") + json_string(run_id) +
                                 ",\"stage\":" + json_string(response.stage) +
                                 ",\"success\":" + (response.success ? "true" : "false") +
                                 ",\"elapsed_ms\":" + std::to_string(elapsed_ms) +
                                 ",\"bridge_response\":" + (response.raw.empty() ? "{}" : response.raw) + "}");
        if (response.success)
        {
            diagnostics.clear_error();
            diagnostics.event("paint_done", "info", "paint_done", response.message.empty() ? response.stage : response.message,
                              std::string("{\"elapsed_ms\":") + std::to_string(elapsed_ms) +
                              ",\"bridge_response\":" + (response.raw.empty() ? "{}" : response.raw) + "}", run_id);
            return true;
        }
        const std::string stage = response.stage.empty() ? "bridge_request_failed" : response.stage;
        const std::string message = response.message.empty() ? response.transport_error : response.message;
        const std::string details = std::string("{\"elapsed_ms\":") + std::to_string(elapsed_ms) +
                                    ",\"bridge_response\":" + (response.raw.empty() ? "{}" : response.raw) +
                                    ",\"win32_error\":" + std::to_string(response.win32_error) + "}";
        if (const std::string hint = paint_failure_hint_message(response); !hint.empty())
            diagnostics.event("paint_failure_hint", "warning", stage, hint, summary_details, run_id);
        diagnostics.record_error(stage, message, details, run_id);
        diagnostics.event("paint_failed", "error", stage, message, details, run_id);
        return false;
    }

    void apply_window_opacity(HWND hwnd, float opacity)
    {
        const float clamped = std::min(1.0f, std::max(0.35f, std::isfinite(opacity) ? opacity : 1.0f));
        LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if ((ex_style & WS_EX_LAYERED) == 0)
        {
            ex_style |= WS_EX_LAYERED;
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style);
        }
        const auto alpha = static_cast<BYTE>(std::max(1, std::min(255, static_cast<int>(clamped * 255.0f + 0.5f))));
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    }

    void apply_dark_title_bar(HWND hwnd)
    {
        BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark))))
        {
            constexpr DWORD LegacyDarkModeAttribute = 19;
            DwmSetWindowAttribute(hwnd, LegacyDarkModeAttribute, &dark, sizeof(dark));
        }
        const COLORREF black = RGB(0, 0, 0);
        const COLORREF border = RGB(37, 37, 37);
        const COLORREF text = RGB(240, 242, 244);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &black, sizeof(black));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
    }

    auto run_overlay_service(Config config,
                             const std::filesystem::path& bridge_path,
                             Diagnostics& diagnostics,
                             RuntimeEventBuffer& event_buffer,
                             AppSettings& settings) -> int
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = overlay_wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MecchaCamouflageOverlay";
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(AppIconResourceId));
        wc.hIconSm = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(AppIconResourceId));
        RegisterClassExW(&wc);

        RECT rect = initial_app_rect(settings);
        HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW,
                                    wc.lpszClassName,
                                    L"Meccha Camouflage",
                                    WS_POPUP,
                                    rect.left,
                                    rect.top,
                                    rect.right - rect.left,
                                    rect.bottom - rect.top,
                                    nullptr,
                                    nullptr,
                                    wc.hInstance,
                                    nullptr);
        if (!hwnd)
        {
            diagnostics.record_error("overlay_create_failed", "failed to create overlay window");
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return 2;
        }
        if (wc.hIcon)
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(wc.hIcon));
        if (wc.hIconSm)
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(wc.hIconSm));
        if (!create_device_d3d(hwnd))
        {
            diagnostics.record_error("overlay_d3d_failed", "failed to create D3D11 overlay device");
            DestroyWindow(hwnd);
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return 2;
        }
        ShowWindow(hwnd, SW_SHOWDEFAULT);
        UpdateWindow(hwnd);
        apply_dark_title_bar(hwnd);
        SetWindowPos(hwnd,
                     settings.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        apply_window_opacity(hwnd, settings.opacity);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
        meccha::apply_meccha_theme();
        meccha::load_meccha_fonts();
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(g_overlay_d3d.device, g_overlay_d3d.context);
        meccha::initialize_meccha_ui_resources(g_overlay_d3d.device);

        AppSettings persisted_settings = settings;
        TraceLogBuffer trace_buffer{};
        bool app_editing = false;
        bool paint_editing = false;
        bool recording_hotkey = false;
        std::string hotkey_error{};
        float applied_opacity = settings.opacity;
        bool applied_topmost = settings.always_on_top;
        OverlayServiceState service{};
        OverlayHotkeyState hotkey_state{};
        OverlayHotkeys hotkeys{meccha::parse_hotkey_binding(settings.paint_hotkey)};
        diagnostics.set_hotkey(std::string("{\"paint\":") + json_string(meccha::hotkey_to_string(hotkeys.paint_binding())) +
                               ",\"backend\":" + hotkeys.backend_json() + "}");
        trace_buffer.push("hotkey", "register", std::string("{\"paint\":") + json_string(meccha::hotkey_to_string(hotkeys.paint_binding())) +
                                               ",\"backend\":" + hotkeys.backend_json() + "}");
        diagnostics.event("runtime_start", "info", "startup", "desktop app started",
                          std::string("{\"pid\":") + std::to_string(GetCurrentProcessId()) +
                          ",\"log_dir\":" + json_string(wide_to_utf8(diagnostics.log_dir().wstring())) +
                          ",\"game_process_name\":" + json_string(wide_to_utf8(config.game_process_name)) +
                          ",\"bridge_host\":" + json_string(config.bridge_host) +
                          ",\"bridge_port\":" + std::to_string(config.bridge_port) +
                          ",\"parent_pid\":" + std::to_string(config.parent_pid) + "}");
        trace_buffer.push("app", "start",
                          std::string("{\"pid\":") + std::to_string(GetCurrentProcessId()) +
                          ",\"parent_pid\":" + std::to_string(config.parent_pid) + "}");

        bool done = false;

        auto start_paint = [&](const char* trigger) {
            if (service.paint_future_active || !service.process.pid || !service.bridge_ready)
                return;
            if (paint_editing)
            {
                diagnostics.event("paint_hotkey_ignored", "warning", "settings", "Paint settings are being edited; hotkey ignored.");
                return;
            }
            const std::string trigger_text = trigger ? trigger : "";
            const DWORD foreground_pid = foreground_process_id();
            if (trigger_text != "UI Hotkey" && foreground_pid != 0 && foreground_pid != service.process.pid)
            {
                diagnostics.event("paint_hotkey_ignored",
                                  "warning",
                                  "focus",
                                  "Focus the game window before starting paint.",
                                  std::string("{\"foreground_pid\":") + std::to_string(foreground_pid) +
                                      ",\"game_pid\":" + std::to_string(service.process.pid) +
                                      ",\"trigger\":" + json_string(trigger ? trigger : "") + "}");
                return;
            }
            meccha::clamp_settings(persisted_settings);
            config.tuning = persisted_settings.tuning;
            diagnostics.event("paint_triggered", "info", "paint", std::string("paint trigger detected: ") + trigger);
            trace_buffer.push("paint.start",
                              "",
                              std::string("{\"trigger\":") + json_string(trigger) +
                              ",\"hotkey\":" + json_string(meccha::hotkey_to_string(hotkeys.paint_binding())) +
                              ",\"stroke_size_texels\":" + std::to_string(persisted_settings.tuning.stroke_size_texels) +
                              ",\"coverage_step_texels\":" + std::to_string(persisted_settings.tuning.coverage_step_texels) +
                              ",\"side_source_max_uv\":" + std::to_string(persisted_settings.tuning.side_source_max_uv) +
                              ",\"front_back_source_max_uv\":" + std::to_string(persisted_settings.tuning.front_back_source_max_uv) +
                              ",\"metallic\":" + std::to_string(persisted_settings.tuning.metallic) +
                              ",\"roughness\":" + std::to_string(persisted_settings.tuning.roughness) +
                              ",\"server_batch_limit\":" + std::to_string(persisted_settings.tuning.server_batch_limit) +
                              ",\"server_batch_delay_ms\":" + std::to_string(persisted_settings.tuning.server_batch_delay_ms) +
                              ",\"enable_front_paint\":" + (persisted_settings.tuning.enable_front_paint ? "true" : "false") +
                              ",\"enable_side_paint\":" + (persisted_settings.tuning.enable_side_paint ? "true" : "false") +
                              ",\"enable_back_paint\":" + (persisted_settings.tuning.enable_back_paint ? "true" : "false") + "}");
            service.paint_running = true;
            service.paint_state = "Running";
            service.last_result = "Painting";
            service.paint_settle_until = 0.0;
            service.paint_started_at = seconds_now();
            {
                std::error_code ec;
                std::filesystem::remove(bridge_progress_file(bridge_path), ec);
            }
            const Config paint_config = config;
            const ProcessInfo paint_process = service.process;
            service.paint_future = std::async(std::launch::async, [paint_config, bridge_path, paint_process, &diagnostics, &trace_buffer]() {
                return run_paint(paint_config, bridge_path, paint_process, diagnostics, &trace_buffer);
            });
            service.paint_future_active = true;
        };

        auto copy_app_fields = [](AppSettings& dst, const AppSettings& src) {
            dst.layout_version = src.layout_version;
            dst.panel_x = src.panel_x;
            dst.panel_y = src.panel_y;
            dst.panel_width = src.panel_width;
            dst.panel_height = src.panel_height;
            dst.log_retention_days = src.log_retention_days;
            dst.game_process_name = src.game_process_name;
            dst.always_on_top = src.always_on_top;
            dst.opacity = src.opacity;
            dst.paint_hotkey = src.paint_hotkey;
            dst.show_info = src.show_info;
            dst.show_warning = src.show_warning;
            dst.show_error = src.show_error;
        };

        auto copy_editable_app_fields = [](AppSettings& dst, const AppSettings& src) {
            dst.always_on_top = src.always_on_top;
            dst.opacity = src.opacity;
            dst.paint_hotkey = src.paint_hotkey;
        };

        auto capture_window_layout = [&]() {
            RECT window_rect{};
            if (GetWindowRect(hwnd, &window_rect))
            {
                settings.panel_x = static_cast<float>(window_rect.left);
                settings.panel_y = static_cast<float>(window_rect.top);
                settings.panel_width = static_cast<float>(std::max(1L, window_rect.right - window_rect.left));
                settings.panel_height = static_cast<float>(std::max(1L, window_rect.bottom - window_rect.top));
            }
        };

        auto request_service_start = [&](const char* source, bool paint_after_ready = false) {
            if (service.state == ControllerServiceState::Running || service.state == ControllerServiceState::Starting)
                return;
            service.state = ControllerServiceState::Starting;
            service.bridge_ready = false;
            service.bridge_state = service.injected_pid == service.process.pid && service.process.pid != 0 ? "Checking" : "Recovering";
            service.paint_after_ready_requested = paint_after_ready;
            service.waiting_for_hotkey_logged = false;
            service.waiting_for_process_logged = false;
            service.not_ready_hotkey_logged = false;
            service.last_bridge_check = 0.0;
            diagnostics.event(source && std::string(source) == "hotkey" ? "service_started_by_hotkey" : "service_started",
                              "info",
                              "service",
                              paint_after_ready
                                  ? "Service started; paint will run when bridge is ready."
                                  : (source && std::string(source) == "hotkey"
                                         ? "Service started by hotkey. Press it again when ready."
                                         : "Service started."));
        };

        auto request_service_stop = [&]() {
            if (service.state == ControllerServiceState::Stopped || service.state == ControllerServiceState::Stopping)
                return;
            service.state = ControllerServiceState::Stopping;
            service.bridge_state = "Stopping";
            service.bridge_ready = false;
            service.waiting_for_hotkey_logged = false;
            service.not_ready_hotkey_logged = false;
            service.paint_after_ready_requested = false;
            diagnostics.event("service_stop_requested", "info", "service", "Stopping service.");
            if (!service.stop_future_active)
            {
                Config stop_config = config;
                stop_config.bridge_timeout_seconds = 5.0;
                service.stop_future = std::async(std::launch::async, [stop_config, bridge_path, &trace_buffer]() {
                    return run_bridge_command(stop_config, bridge_path, "cancel_paint", "{}", &trace_buffer);
                });
                service.stop_future_active = true;
            }
        };

        auto start_bridge_check = [&](const ProcessInfo& process, double now_seconds) {
            if (service.bridge_check_future_active)
                return;
            const bool keep_ready = service.bridge_ready &&
                                    service.injected_pid == process.pid &&
                                    process.pid != 0;
            service.bridge_state = keep_ready ? "Ready" : (service.injected_pid == process.pid ? "Checking" : "Injecting");
            if (!keep_ready)
                service.bridge_ready = false;
            service.bridge_check_pid = process.pid;
            service.last_bridge_check = now_seconds;
            Config check_config = config;
            std::filesystem::path check_bridge_path = bridge_path;
            DWORD injected_pid = service.injected_pid;
            service.bridge_check_future = std::async(std::launch::async,
                                                     [check_config, check_bridge_path, process, &diagnostics, injected_pid]() mutable {
                                                         const bool ready = ensure_bridge(check_config,
                                                                                          check_bridge_path,
                                                                                          process,
                                                                                          diagnostics,
                                                                                          injected_pid);
                                                         BridgeCheckResult result{};
                                                         result.ready = ready;
                                                         result.pid = process.pid;
                                                         result.injected_pid = injected_pid;
                                                         return result;
                                                     });
            service.bridge_check_future_active = true;
        };

        while (!done)
        {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    done = true;
                    continue;
                }
                if (recording_hotkey)
                {
                    HotkeyBinding captured{};
                    std::string capture_error{};
                    bool cancel = false;
                    const bool captured_ok = meccha::try_capture_hotkey_from_message(msg, captured, capture_error, cancel);
                    if (cancel)
                    {
                        recording_hotkey = false;
                        hotkey_error.clear();
                        diagnostics.event("hotkey_record_canceled", "info", "settings", "Hotkey recording canceled.");
                        continue;
                    }
                    if (!capture_error.empty())
                    {
                        diagnostics.event("hotkey_record_rejected", "warning", "settings", capture_error);
                        continue;
                    }
                    if (captured_ok)
                    {
                        settings.paint_hotkey = meccha::hotkey_to_string(captured);
                        hotkey_error.clear();
                        recording_hotkey = false;
                        diagnostics.event("hotkey_recorded", "info", "settings", "Hotkey draft recorded: " + settings.paint_hotkey);
                        continue;
                    }
                }
                hotkeys.handle_message(msg, hotkey_state);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            hotkeys.poll_fallback(hotkey_state);

            meccha::clamp_settings(settings);
            meccha::clamp_settings(persisted_settings);
            config.game_process_name = persisted_settings.game_process_name.empty() ? std::wstring(meccha::DefaultGameProcessName) : persisted_settings.game_process_name;
            config.tuning = persisted_settings.tuning;
            const double now = seconds_now();
            if (config.parent_pid && !is_process_alive(config.parent_pid))
            {
                diagnostics.event("parent_process_gone", "warning", "runtime", "parent process exited; shutting down controller");
                done = true;
            }

            if (service.paint_future_active &&
                service.paint_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                const bool ok = service.paint_future.get();
                service.paint_future_active = false;
                service.paint_running = false;
                if (service.state == ControllerServiceState::Stopping)
                {
                    service.paint_settle_until = 0.0;
                    service.paint_state = "Idle";
                    service.last_result = "Stopped";
                    if (!service.stop_future_active)
                    {
                        service.state = ControllerServiceState::Stopped;
                        service.bridge_state = "Paused";
                        diagnostics.event("service_stopped", "info", "service", "Service stopped.");
                    }
                }
                else if (ok)
                {
                    service.paint_settle_until = 0.0;
                    service.paint_state = "Done";
                    service.last_result = "Paint complete";
                }
                else
                {
                    service.paint_settle_until = 0.0;
                    service.paint_state = "Failed";
                    service.last_result = "Paint failed";
                }
                service.duplicate_paint_hotkey_logged = false;
            }

            if (service.stop_future_active &&
                service.stop_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                const auto response = service.stop_future.get();
                service.stop_future_active = false;
                if (response.success)
                {
                    if (!service.paint_future_active)
                    {
                        service.state = ControllerServiceState::Stopped;
                        service.bridge_state = "Paused";
                        diagnostics.event("service_stopped", "info", "service", "Service stopped.");
                    }
                    else
                    {
                        service.bridge_state = "Cancelling";
                        diagnostics.event("paint_cancel_requested",
                                          "info",
                                          "service",
                                          "Paint cancel requested; waiting for current paint request to stop.");
                    }
                    service.bridge_ready = false;
                    service.paint_settle_until = 0.0;
                    service.last_bridge_check = 0.0;
                }
                else
                {
                    service.state = ControllerServiceState::Error;
                    service.bridge_ready = false;
                    service.bridge_state = "Stop failed";
                    service.last_bridge_check = 0.0;
                    diagnostics.event("service_stop_failed",
                                      "warning",
                                      response.stage.empty() ? "service" : response.stage,
                                      response.message.empty() ? "Service stop failed." : response.message);
                }
            }

            if (service.bridge_check_future_active &&
                service.bridge_check_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                const BridgeCheckResult result = service.bridge_check_future.get();
                service.bridge_check_future_active = false;
                if ((service.state == ControllerServiceState::Running || service.state == ControllerServiceState::Starting) &&
                    service.process.pid == result.pid)
                {
                    service.state = ControllerServiceState::Running;
                    service.bridge_ready = result.ready;
                    service.injected_pid = result.injected_pid;
                    service.bridge_state = result.ready ? "Ready" : "Recovering";
                    if (result.ready)
                    {
                        service.not_ready_hotkey_logged = false;
                        service.duplicate_paint_hotkey_logged = false;
                        service.paint_settle_until = 0.0;
                        if (!service.paint_future_active && !service.paint_running)
                        {
                            service.paint_state = "Idle";
                            if (service.last_result == "Stopped" || service.last_result == "Paint failed")
                                service.last_result = "Waiting";
                        }
                        if (!service.waiting_for_hotkey_logged)
                        {
                            diagnostics.event("waiting_for_hotkey", "info", "ready", "waiting for hotkey");
                            service.waiting_for_hotkey_logged = true;
                        }
                        if (service.paint_after_ready_requested)
                        {
                            service.paint_after_ready_requested = false;
                            if (paint_editing)
                            {
                                diagnostics.event("paint_start_rejected",
                                                  "error",
                                                  "settings",
                                                  "Paint settings are being edited; hotkey rejected.");
                            }
                            else if (!service.paint_future_active && !service.paint_running && service.process.pid && service.bridge_ready)
                            {
                                start_paint("UI Hotkey");
                            }
                        }
                    }
                }
            }

            if (service.state == ControllerServiceState::Starting)
                service.state = ControllerServiceState::Running;

            if (service.state == ControllerServiceState::Running)
            {
                ProcessInfo process = find_process_by_name(config.game_process_name);
                if (process.pid == 0)
                {
                    service.process = {};
                    service.bridge_ready = false;
                    service.bridge_state = "Waiting";
                    service.injected_pid = 0;
                    service.waiting_for_hotkey_logged = false;
                    service.last_bridge_check = 0.0;
                    diagnostics.set_process(process_json(process, config.game_process_name));
                    if (!service.waiting_for_process_logged)
                    {
                        diagnostics.event("waiting_for_process", "info", "process_wait", "waiting for " + wide_to_utf8(config.game_process_name),
                                          std::string("{\"game_process_name\":") + json_string(wide_to_utf8(config.game_process_name)) + "}");
                        service.waiting_for_process_logged = true;
                    }
                }
                else
                {
                    if (service.process.pid != process.pid)
                    {
                        service.process = process;
                        service.bridge_ready = false;
                        service.bridge_state = "Attaching";
                        service.injected_pid = 0;
                        service.bridge_check_pid = 0;
                        service.waiting_for_hotkey_logged = false;
                        service.waiting_for_process_logged = false;
                        service.not_ready_hotkey_logged = false;
                        service.last_bridge_check = 0.0;
                        diagnostics.set_process(process_json(process, config.game_process_name));
                        diagnostics.event("process_attached", "info", "process", "attached to " + wide_to_utf8(process.name), process_json(process, config.game_process_name));
                    }
                    if (!service.paint_running &&
                        !service.bridge_check_future_active &&
                        now - service.last_bridge_check >= config.status_interval_seconds)
                    {
                        start_bridge_check(process, now);
                    }
                }
            }

            if (hotkey_state.paint_requested)
            {
                if (paint_editing)
                    diagnostics.event("paint_hotkey_ignored", "warning", "settings", "Paint settings are being edited; hotkey ignored.");
                else if (service.state == ControllerServiceState::Stopped || service.state == ControllerServiceState::Error)
                    request_service_start("hotkey", true);
                else if (service.state == ControllerServiceState::Stopping)
                    diagnostics.event("paint_hotkey_ignored", "warning", "service", "Service is stopping; hotkey ignored.");
                else if (service.paint_running || service.paint_future_active)
                {
                    if (!service.duplicate_paint_hotkey_logged)
                    {
                        diagnostics.event("paint_hotkey_ignored", "warning", "paint", "Paint is already running; hotkey ignored.");
                        service.duplicate_paint_hotkey_logged = true;
                    }
                }
                else if (service.process.pid && service.bridge_ready)
                    start_paint("Hotkey");
                else if (service.process.pid)
                {
                    service.bridge_ready = false;
                    service.bridge_state = "Recovering";
                    service.last_bridge_check = 0.0;
                    service.waiting_for_hotkey_logged = false;
                    service.paint_after_ready_requested = true;
                    if (!service.bridge_check_future_active)
                        start_bridge_check(service.process, now);
                    if (!service.not_ready_hotkey_logged)
                    {
                        diagnostics.event("paint_hotkey_rejected",
                                          "warning",
                                          "not_ready",
                                          "Bridge not ready; paint will start when ready.");
                        service.not_ready_hotkey_logged = true;
                    }
                }
                else
                {
                    if (!service.not_ready_hotkey_logged)
                    {
                        diagnostics.event("paint_hotkey_rejected", "error", "not_ready", "Not ready. Wait for game and bridge.");
                        service.not_ready_hotkey_logged = true;
                    }
                }
                hotkey_state.paint_requested = false;
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            auto events = event_buffer.snapshot();
            const std::string human_log_text = meccha::build_human_log_text(events,
                                                                             settings.show_info,
                                                                             settings.show_warning,
                                                                             settings.show_error,
                                                                             true);
            const std::string full_human_log_text = meccha::build_human_log_text(events, true, true, true, false);
            meccha::UiRuntimeState ui_runtime{};
            ui_runtime.target_process = wide_to_utf8(config.game_process_name);
            ui_runtime.process_name = service.process.pid ? wide_to_utf8(service.process.name) : "";
            ui_runtime.pid = static_cast<unsigned long>(service.process.pid);
            ui_runtime.service_state = service_state_text(service.state);
            ui_runtime.bridge_state = service.bridge_state;
            ui_runtime.bridge_ready = service.bridge_ready;
            ui_runtime.game_attached = service.process.pid != 0;
            const bool paint_busy = service.paint_running || service.paint_future_active;
            ui_runtime.paint_running = paint_busy;
            const bool paint_settling = service.paint_settle_until > seconds_now();
            ui_runtime.paint_ready = service.state == ControllerServiceState::Running &&
                                     service.process.pid != 0 &&
                                     service.bridge_ready &&
                                     !paint_busy &&
                                     !paint_editing &&
                                     !paint_settling;
            ui_runtime.paint_state = paint_busy ? "Running" : (paint_settling ? "Settling" : (ui_runtime.paint_ready ? "Ready" : service.paint_state));
            const auto mesh_summary = latest_mesh_ui_summary(events);
            ui_runtime.mesh_status = mesh_summary.mesh;
            ui_runtime.planner_status = mesh_summary.planner;
            ui_runtime.replay_status = mesh_summary.replay;
            ui_runtime.metric_server_eta = mesh_summary.server_eta.empty() ? "-" : mesh_summary.server_eta;
            ui_runtime.metric_server_elapsed = mesh_summary.server_elapsed.empty() ? "-" : mesh_summary.server_elapsed;
            ui_runtime.metric_apply_eta = mesh_summary.apply_eta.empty() ? "-" : mesh_summary.apply_eta;
            ui_runtime.metric_apply_elapsed = mesh_summary.apply_elapsed.empty() ? "-" : mesh_summary.apply_elapsed;
            if (paint_busy)
            {
                const double running_elapsed = std::max(0.0, (seconds_now() - service.paint_started_at) * 1000.0);
                if (metric_blank(ui_runtime.metric_server_eta))
                    ui_runtime.metric_server_eta = "Calculating";
                if (metric_blank(ui_runtime.metric_server_elapsed))
                    ui_runtime.metric_server_elapsed = format_duration_label(running_elapsed);
                apply_live_progress_metrics(ui_runtime, bridge_progress_file(bridge_path));
            }
            if (service.state == ControllerServiceState::Stopped)
            {
                ui_runtime.status_title = "Service stopped.";
                ui_runtime.status_detail = "Waiting for (Re)Start / Hotkey.";
            }
            else if (service.state == ControllerServiceState::Stopping)
            {
                ui_runtime.status_title = "Stopping service.";
                ui_runtime.status_detail = "Cancelling the current paint request.";
            }
            else if (!service.process.pid)
            {
                ui_runtime.status_title = "Waiting for game.";
                ui_runtime.status_detail = "Target: " + wide_to_utf8(config.game_process_name);
            }
            else if (!service.bridge_ready)
            {
                ui_runtime.status_title = "Recovering bridge.";
                ui_runtime.status_detail = service.paint_after_ready_requested
                                               ? "Paint will start when bridge is ready."
                                               : "Press " + meccha::hotkey_to_string(hotkeys.paint_binding()) + " again when ready.";
            }
            else if (paint_busy)
            {
                ui_runtime.status_title = "Painting.";
                ui_runtime.status_detail = "Paint request is running through ServerPaintBatch.";
            }
            else if (paint_settling)
            {
                const auto wait_seconds = static_cast<int>(std::ceil(service.paint_settle_until - seconds_now()));
                ui_runtime.status_title = "Paint settling.";
                ui_runtime.status_detail = "Wait " + std::to_string(std::max(1, wait_seconds)) + "s before the next run.";
            }
            else if (service.paint_state == "Failed")
            {
                ui_runtime.status_title = "Last paint failed.";
                const std::string hint = latest_event_message(events, "paint_failure_hint");
                ui_runtime.status_detail = hint.empty() ? "Check Log for the latest error." : hint;
            }
            else
            {
                ui_runtime.status_title = "Paint ready.";
                ui_runtime.status_detail = "Hotkey: " + meccha::hotkey_to_string(hotkeys.paint_binding());
            }
            ui_runtime.app_editing = app_editing;
            ui_runtime.paint_editing = paint_editing;
            ui_runtime.recording_hotkey = recording_hotkey;
            ui_runtime.hotkey_error = hotkey_error;
            ui_runtime.log_dir = wide_to_utf8(diagnostics.log_dir().wstring());

            meccha::UiActions actions{};
            meccha::draw_app_ui(settings,
                                persisted_settings,
                                ui_runtime,
                                human_log_text,
                                actions);

            if (actions.stop_service_clicked)
            {
                if (service.paint_running || service.paint_future_active)
                {
                    request_service_stop();
                }
            }
            if (actions.start_service_clicked)
            {
                if (service.state == ControllerServiceState::Stopped || service.state == ControllerServiceState::Error)
                {
                    request_service_start("ui", true);
                }
                else if (service.state == ControllerServiceState::Stopping)
                {
                    diagnostics.event("paint_start_rejected", "error", "service", "Service is stopping; hotkey rejected.");
                }
                else if (paint_editing)
                {
                    diagnostics.event("paint_start_rejected", "error", "settings", "Paint settings are being edited; hotkey rejected.");
                }
                else if (service.paint_running || service.paint_future_active)
                {
                    diagnostics.event("paint_start_rejected", "error", "paint", "Paint is already running; hotkey rejected.");
                }
                else if (service.process.pid && service.bridge_ready)
                {
                    start_paint("UI Hotkey");
                }
                else if (service.process.pid)
                {
                    service.bridge_ready = false;
                    service.bridge_state = "Recovering";
                    service.last_bridge_check = 0.0;
                    service.paint_after_ready_requested = true;
                    if (!service.bridge_check_future_active)
                        start_bridge_check(service.process, now);
                    diagnostics.event("paint_start_waiting", "warning", "not_ready", "Bridge not ready; paint will start when ready.");
                }
                else
                {
                    diagnostics.event("paint_start_rejected", "error", "not_ready", "Not ready. Wait for game and bridge.");
                }
            }
            if (actions.minimize_clicked)
            {
                ShowWindow(hwnd, SW_MINIMIZE);
            }
            if (actions.close_clicked)
            {
                done = true;
            }
            if (actions.open_logs_clicked)
            {
                const auto logs = diagnostics.log_dir().wstring();
                ShellExecuteW(nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                diagnostics.event("logs_opened", "info", "ui", "Opened log directory.");
                trace_buffer.push("shell", "open", std::string("{\"path\":") + json_string(wide_to_utf8(logs)) + "}");
            }
            if (actions.open_repository_clicked)
            {
                constexpr wchar_t RepositoryUrl[] = L"https://github.com/acentrist/MecchaCamouflage";
                ShellExecuteW(nullptr, L"open", RepositoryUrl, nullptr, nullptr, SW_SHOWNORMAL);
                diagnostics.event("repository_opened", "info", "ui", "Opened GitHub repository.");
                trace_buffer.push("shell", "open", "{\"url\":\"https://github.com/acentrist/MecchaCamouflage\"}");
            }
            if (actions.open_license_clicked)
            {
                constexpr wchar_t LicenseUrl[] = L"https://github.com/acentrist/MecchaCamouflage/blob/main/LICENSE.txt";
                ShellExecuteW(nullptr, L"open", LicenseUrl, nullptr, nullptr, SW_SHOWNORMAL);
                diagnostics.event("license_opened", "info", "ui", "Opened license.");
                trace_buffer.push("shell", "open", "{\"url\":\"https://github.com/acentrist/MecchaCamouflage/blob/main/LICENSE.txt\"}");
            }
            if (actions.copy_log_clicked)
            {
                ImGui::SetClipboardText(full_human_log_text.c_str());
                diagnostics.event("log_copied", "info", "ui", "Log copied.");
            }
            if (actions.edit_app_clicked)
            {
                copy_editable_app_fields(settings, persisted_settings);
                app_editing = true;
                diagnostics.event("app_settings_edit_started", "info", "settings", "Editing App Settings.");
            }
            if (actions.cancel_app_clicked)
            {
                copy_editable_app_fields(settings, persisted_settings);
                app_editing = false;
                recording_hotkey = false;
                diagnostics.event("app_settings_edit_canceled", "info", "settings", "App Settings changes canceled.");
            }
            if (actions.edit_paint_clicked)
            {
                settings.tuning = persisted_settings.tuning;
                paint_editing = true;
                diagnostics.event("paint_settings_edit_started", "info", "settings", "Editing Paint Settings.");
            }
            if (actions.cancel_paint_clicked)
            {
                settings.tuning = persisted_settings.tuning;
                paint_editing = false;
                diagnostics.event("paint_settings_edit_canceled", "info", "settings", "Paint Settings changes canceled.");
            }
            if (actions.reset_app_clicked && app_editing)
            {
                AppSettings defaults{};
                settings.always_on_top = defaults.always_on_top;
                settings.opacity = defaults.opacity;
                settings.paint_hotkey = defaults.paint_hotkey;
                diagnostics.event("app_settings_reset", "info", "settings", "App Settings reset to defaults.");
            }
            if (actions.reset_paint_clicked && paint_editing)
            {
                settings.tuning = meccha::default_tuning();
                meccha::clamp_settings(settings);
                diagnostics.event("paint_settings_reset", "info", "settings", "Paint Settings reset to defaults.");
            }
            if (actions.start_hotkey_recording && app_editing)
            {
                recording_hotkey = true;
                hotkey_error.clear();
                diagnostics.event("hotkey_record_started", "info", "settings", "Recording paint hotkey.");
            }
            if (actions.settings_changed)
            {
                meccha::clamp_settings(settings);
            }
            if (actions.save_app_clicked && app_editing)
            {
                capture_window_layout();
                meccha::clamp_settings(settings);
                AppSettings next = persisted_settings;
                copy_app_fields(next, settings);
                next.tuning = persisted_settings.tuning;

                const HotkeyBinding previous_hotkey = hotkeys.paint_binding();
                std::string register_error;
                const bool hotkey_registered = hotkeys.set_paint_hotkey(meccha::parse_hotkey_binding(next.paint_hotkey), &register_error);
                if (!hotkey_registered)
                {
                    diagnostics.event("hotkey_register_failed", "error", "settings", register_error.empty() ? "RegisterHotKey failed." : register_error);
                    trace_buffer.push("hotkey", "register", std::string("{\"success\":false,\"error\":") +
                                                       json_string(register_error.empty() ? "RegisterHotKey failed." : register_error) + "}");
                    settings.paint_hotkey = persisted_settings.paint_hotkey;
                }
                else if (meccha::save_settings(next))
                {
                    persisted_settings = next;
                    app_editing = false;
                    recording_hotkey = false;
                    const bool opacity_changed = std::abs(persisted_settings.opacity - applied_opacity) > 0.001f;
                    const bool topmost_changed = persisted_settings.always_on_top != applied_topmost;
                    applied_opacity = persisted_settings.opacity;
                    apply_window_opacity(hwnd, applied_opacity);
                    applied_topmost = persisted_settings.always_on_top;
                    SetWindowPos(hwnd,
                                 persisted_settings.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                                 0,
                                 0,
                                 0,
                                 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    if (opacity_changed)
                        trace_buffer.push("window", "opacity", std::string("{\"opacity\":") + std::to_string(persisted_settings.opacity) + "}");
                    if (topmost_changed)
                        trace_buffer.push("window", "topmost", std::string("{\"always_on_top\":") + (persisted_settings.always_on_top ? "true" : "false") + "}");
                    diagnostics.set_hotkey(std::string("{\"paint\":") + json_string(persisted_settings.paint_hotkey) +
                                           ",\"backend\":" + hotkeys.backend_json() + "}");
                    trace_buffer.push("hotkey", "register", std::string("{\"paint\":") + json_string(persisted_settings.paint_hotkey) +
                                                       ",\"backend\":" + hotkeys.backend_json() + "}");
                    trace_buffer.push("config", "save", std::string("{\"path\":") + json_string(wide_to_utf8(meccha::config_path().wstring())) +
                                                     ",\"scope\":\"app\"}");
                    diagnostics.event("app_settings_saved", "info", "settings", "App Settings saved.");
                }
                else
                {
                    std::string revert_error;
                    hotkeys.set_paint_hotkey(previous_hotkey, &revert_error);
                    settings.paint_hotkey = persisted_settings.paint_hotkey;
                    trace_buffer.push("config", "save", std::string("{\"success\":false,\"path\":") + json_string(wide_to_utf8(meccha::config_path().wstring())) +
                                                     ",\"scope\":\"app\"}");
                    diagnostics.event("app_settings_save_failed", "error", "settings", "Failed to save App Settings.");
                }
            }
            if (actions.save_paint_clicked && paint_editing)
            {
                meccha::clamp_settings(settings);
                AppSettings next = persisted_settings;
                next.tuning = settings.tuning;
                if (meccha::save_settings(next))
                {
                    persisted_settings = next;
                    paint_editing = false;
                    trace_buffer.push("config", "save", std::string("{\"path\":") + json_string(wide_to_utf8(meccha::config_path().wstring())) +
                                                     ",\"scope\":\"paint\"}");
                    diagnostics.event("paint_settings_saved", "info", "settings", "Paint Settings saved.");
                }
                else
                {
                    trace_buffer.push("config", "save", std::string("{\"success\":false,\"path\":") + json_string(wide_to_utf8(meccha::config_path().wstring())) +
                                                     ",\"scope\":\"paint\"}");
                    diagnostics.event("paint_settings_save_failed", "error", "settings", "Failed to save Paint Settings.");
                }
            }
            if (recording_hotkey && !ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow))
            {
                recording_hotkey = false;
                diagnostics.event("hotkey_record_canceled", "info", "settings", "Hotkey recording canceled.");
            }
            RECT window_rect{};
            if (GetWindowRect(hwnd, &window_rect))
            {
                const float x = static_cast<float>(window_rect.left);
                const float y = static_cast<float>(window_rect.top);
                const float width = static_cast<float>(std::max(1L, window_rect.right - window_rect.left));
                const float height = static_cast<float>(std::max(1L, window_rect.bottom - window_rect.top));
                if (std::abs(settings.panel_x - x) > 0.5f ||
                    std::abs(settings.panel_y - y) > 0.5f ||
                    std::abs(settings.panel_width - width) > 0.5f ||
                    std::abs(settings.panel_height - height) > 0.5f)
                {
                    settings.panel_x = x;
                    settings.panel_y = y;
                    settings.panel_width = width;
                    settings.panel_height = height;
                }
            }

            ImGui::Render();
            const float clear_color[4]{0.08f, 0.09f, 0.10f, 1.0f};
            g_overlay_d3d.context->OMSetRenderTargets(1, &g_overlay_d3d.render_target, nullptr);
            g_overlay_d3d.context->ClearRenderTargetView(g_overlay_d3d.render_target, clear_color);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_overlay_d3d.swap_chain->Present(1, 0);
            Sleep(static_cast<DWORD>(std::max(1.0, config.frame_delay_ms)));
        }

        if (service.paint_future_active)
        {
            Config cancel_config = config;
            cancel_config.bridge_timeout_seconds = 5.0;
            const auto cancel_response = run_bridge_command(cancel_config, bridge_path, "cancel_paint", "{}", &trace_buffer);
            diagnostics.event(cancel_response.success ? "paint_cancel_on_exit" : "paint_cancel_on_exit_failed",
                              cancel_response.success ? "info" : "warning",
                              cancel_response.stage.empty() ? "cancel_paint" : cancel_response.stage,
                              cancel_response.message.empty() ? "Cancel paint before exit." : cancel_response.message);
            if (service.paint_future.wait_for(std::chrono::seconds(10)) == std::future_status::ready)
            {
                (void)service.paint_future.get();
                service.paint_future_active = false;
            }
        }
        if (service.paint_future_active)
            service.paint_future.wait();
        if (service.bridge_check_future_active)
            service.bridge_check_future.wait();
        if (service.stop_future_active)
            service.stop_future.wait();
        {
            Config shutdown_config = config;
            shutdown_config.bridge_timeout_seconds = 5.0;
            auto response = run_bridge_command(shutdown_config, bridge_path, "shutdown", "{}", &trace_buffer);
            diagnostics.event(response.success ? "bridge_shutdown" : "bridge_shutdown_failed",
                              response.success ? "info" : "warning",
                              response.stage,
                              response.message.empty() ? response.transport_error : response.message,
                              std::string("{\"response\":") + (response.raw.empty() ? "{}" : response.raw) + "}");
        }
        meccha::shutdown_meccha_ui_resources();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        cleanup_device_d3d();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 0;
    }

    auto parse_config(int argc, wchar_t** argv) -> Config
    {
        Config config{};
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring key = argv[i];
            auto has_value = [&]() { return i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) != 0; };
            auto next_w = [&]() -> std::wstring { return has_value() ? std::wstring(argv[++i]) : std::wstring(); };
            auto next_s = [&]() -> std::string { return wide_to_utf8(next_w()); };
            if (key == L"--mode") config.mode = next_w();
            else if (key == L"--game-process-name") config.game_process_name = next_w();
            else if (key == L"--log-dir") config.log_dir = next_w();
            else if (key == L"--bridge-host") config.bridge_host = next_s();
            else if (key == L"--bridge-port") config.bridge_port = std::max(0, _wtoi(next_w().c_str()));
            else if (key == L"--bridge-timeout-seconds") config.bridge_timeout_seconds = std::max(0.1, _wtof(next_w().c_str()));
            else if (key == L"--status-interval-seconds") config.status_interval_seconds = std::max(0.5, _wtof(next_w().c_str()));
            else if (key == L"--frame-delay-ms") config.frame_delay_ms = std::max(0.0, _wtof(next_w().c_str()));
            else if (key == L"--native-apply-mode") config.native_apply_mode = next_s();
            else if (key == L"--parent-pid") config.parent_pid = static_cast<DWORD>(std::max(0, _wtoi(next_w().c_str())));
            else if (key.rfind(L"--", 0) == 0 && has_value()) { ++i; }
        }
        if (config.bridge_port <= 0 && config.mode != L"shutdown")
            config.bridge_port = choose_bridge_port(config.bridge_host);
        return config;
    }

    auto run_apply_once(const Config& config, const std::filesystem::path& bridge_path, Diagnostics& diagnostics) -> int
    {
        const ProcessInfo process = find_process_by_name(config.game_process_name);
        diagnostics.set_process(process_json(process, config.game_process_name));
        if (process.pid == 0)
        {
            diagnostics.event("waiting_for_process", "info", "process_wait", "waiting for " + wide_to_utf8(config.game_process_name),
                              std::string("{\"game_process_name\":") + json_string(wide_to_utf8(config.game_process_name)) + "}");
            return 1;
        }
        diagnostics.event("process_attached", "info", "process", "attached to " + wide_to_utf8(process.name), process_json(process, config.game_process_name));
        DWORD injected_pid = 0;
        if (!ensure_bridge(config, bridge_path, process, diagnostics, injected_pid))
            return 2;
        return run_paint(config, bridge_path, process, diagnostics) ? 0 : 4;
    }

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 2;

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        LocalFree(argv);
        return 2;
    }

    const auto run = [&]() -> int {
        AppSettings settings = meccha::load_settings();
        Config config = parse_config(argc, argv);
        if (config.game_process_name == meccha::DefaultGameProcessName && !settings.game_process_name.empty())
            config.game_process_name = settings.game_process_name;
        else
            settings.game_process_name = config.game_process_name;
        meccha::clamp_settings(settings);
        config.tuning = settings.tuning;
        const std::filesystem::path log_dir = config.log_dir.empty() ? default_log_dir() : std::filesystem::path(config.log_dir);
        RuntimeEventBuffer event_buffer{};
        Diagnostics diagnostics(log_dir, &event_buffer, settings.log_retention_days);
        if (config.mode == L"shutdown")
        {
            if (config.bridge_port <= 0)
            {
                const std::string status = read_text_file(diagnostics.status_path());
                config.bridge_port = static_cast<int>(extract_json_number(status, "port", 0.0));
            }
            if (config.bridge_port <= 0)
            {
                diagnostics.record_error("shutdown_unavailable", "bridge port is unknown");
                return 1;
            }
            BridgeClient client(config.bridge_host, config.bridge_port, config.bridge_timeout_seconds);
            auto response = client.request("shutdown");
            diagnostics.event(response.success ? "bridge_shutdown" : "bridge_shutdown_failed",
                              response.success ? "info" : "warning",
                              response.stage,
                              response.message.empty() ? response.transport_error : response.message,
                              std::string("{\"response\":") + (response.raw.empty() ? "{}" : response.raw) + "}");
            return response.success ? 0 : 1;
        }
        if (config.bridge_port <= 0)
            config.bridge_port = choose_bridge_port(config.bridge_host);
        const auto bridge_path = extract_embedded_bridge(log_dir, config.bridge_port);
        write_bridge_port_file(bridge_path, config.bridge_port);
        if (config.mode == L"apply")
        {
            const int code = run_apply_once(config, bridge_path, diagnostics);
            return code;
        }
        return run_overlay_service(config, bridge_path, diagnostics, event_buffer, settings);
    };

    int code = 1;
    try
    {
        code = run();
    }
    catch (const std::exception& exc)
    {
        RuntimeEventBuffer event_buffer{};
        Diagnostics diagnostics(default_log_dir(), &event_buffer, 14);
        diagnostics.record_error("fatal", exc.what());
        code = 1;
    }
    WSACleanup();
    LocalFree(argv);
    return code;
}
