#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../include/sdk.hpp"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Gdi32.lib")

namespace
{
    constexpr int DefaultBridgePort = 47654;
    constexpr int IdleShutdownSeconds = 15;
    constexpr std::size_t MaxRequestBytes = 8 * 1024 * 1024;
    constexpr int ProcessEventVtableIndex = 0x4C;
    constexpr UINT PaintDispatchMessage = WM_APP + 0x4D43;
    constexpr int ServerPaintBatchStrokeLimit = 50;
    constexpr int ServerPaintBatchDelayMs = 300;

    constexpr std::uintptr_t OffClass = 0x10;
    constexpr std::uintptr_t OffName = 0x18;
    constexpr std::uintptr_t OffOuter = 0x20;
    constexpr std::uintptr_t OffObjectFlags = 0x08;
    constexpr std::uint32_t RFClassDefaultObject = 0x10;
    constexpr std::uintptr_t OffSuperStruct = 0x40;
    constexpr std::uintptr_t OffChildren = 0x48;
    constexpr std::uintptr_t OffChildProperties = 0x50;
    constexpr std::uintptr_t OffPropertiesSize = 0x58;
    constexpr std::uintptr_t OffUFieldNext = 0x28;
    constexpr std::uintptr_t OffFFieldNext = 0x18;
    constexpr std::uintptr_t OffFFieldName = 0x20;
    constexpr std::uintptr_t OffFPropertyElementSize = 0x3C;
    constexpr std::uintptr_t OffFPropertyOffset = 0x44;
    constexpr std::uintptr_t OffFStructPropertyStruct = 0x78;

    HMODULE g_module = nullptr;
    std::atomic<bool> g_running{true};
    std::atomic<bool> g_process_event_hook_installed{false};
    std::atomic<std::uintptr_t> g_original_process_event{0};
    std::atomic<HHOOK> g_message_hook{nullptr};
    std::atomic<DWORD> g_game_thread_id{0};
    std::mutex g_hook_mutex;
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> g_process_event_hook_slots;
    thread_local bool g_inside_process_event_hook = false;

    using ProcessEventFn = void(__fastcall*)(void*, void*, void*);

    struct QueuedPaintJob
    {
        std::string request{};
        std::string response{};
        bool done{false};
    };

    std::mutex g_paint_jobs_mutex;
    std::condition_variable g_paint_jobs_cv;
    std::vector<std::shared_ptr<QueuedPaintJob>> g_paint_jobs;
    std::atomic<bool> g_mesh_snapshot_ready{false};
    std::atomic<bool> g_dump_cancel_requested{false};

    auto paint_full_route_native_direct(const std::string& request) -> std::string;
    auto is_mesh_first_paint_request(const std::string& request) -> bool;
    auto paint_mesh_first_on_game_thread(const std::string& request,
                                         const std::shared_ptr<QueuedPaintJob>& queued_job = {}) -> std::string;
    auto start_mesh_first_paint_async_job(const std::string& request,
                                          const std::shared_ptr<QueuedPaintJob>& queued_job) -> bool;
    auto tick_mesh_first_batch_async_job() -> void;
    auto drain_paint_jobs_on_game_thread() -> void;
    void __fastcall hooked_process_event(void* object, void* function, void* params);
    LRESULT CALLBACK message_hook_proc(int code, WPARAM wparam, LPARAM lparam);

    template <typename T>
    auto safe_read(std::uintptr_t address, T fallback = T{}) -> T
    {
        __try
        {
            return *reinterpret_cast<T*>(address);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return fallback;
        }
    }

    auto safe_copy(void* dest, const void* src, std::size_t size) -> bool
    {
        __try
        {
            std::memcpy(dest, src, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    auto lower_copy(std::string text) -> std::string
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }

    auto contains_text(const std::string& text, const char* needle) -> bool
    {
        return text.find(needle) != std::string::npos;
    }

    auto clamp_range(double value, double min_value, double max_value) -> double
    {
        if (!std::isfinite(value))
            return min_value;
        return std::min(max_value, std::max(min_value, value));
    }

    auto json_number_field(const std::string& text, const std::string& key, double fallback) -> double
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

    auto json_int_field(const std::string& text, const std::string& key, int fallback, int min_value, int max_value) -> int
    {
        const double value = json_number_field(text, key, static_cast<double>(fallback));
        return std::max(min_value, std::min(max_value, static_cast<int>(value)));
    }

    auto json_bool_field(const std::string& text, const std::string& key, bool fallback) -> bool
    {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return fallback;
        auto pos = start + needle.size();
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            ++pos;
        if (text.compare(pos, 4, "true") == 0)
            return true;
        if (text.compare(pos, 5, "false") == 0)
            return false;
        return fallback;
    }

    auto json_string_field(const std::string& text, const std::string& key, const std::string& fallback = {}) -> std::string
    {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return fallback;
        auto pos = text.find('"', start + needle.size());
        if (pos == std::string::npos)
            return fallback;
        ++pos;
        std::string out{};
        bool escaped = false;
        for (; pos < text.size(); ++pos)
        {
            const char ch = text[pos];
            if (escaped)
            {
                switch (ch)
                {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(ch); break;
                }
                escaped = false;
                continue;
            }
            if (ch == '\\')
            {
                escaped = true;
                continue;
            }
            if (ch == '"')
            {
                return out;
            }
            out.push_back(ch);
        }
        return fallback;
    }

    auto json_escape(const std::string& text) -> std::string
    {
        std::string out{};
        out.reserve(text.size() + 8);
        for (const char c : text)
        {
            const auto value = static_cast<unsigned char>(c);
            if (c == '\\' || c == '"')
            {
                out.push_back('\\');
                out.push_back(c);
            }
            else if (c == '\n')
            {
                out += "\\n";
            }
            else if (c == '\r')
            {
                out += "\\r";
            }
            else if (c == '\t')
            {
                out += "\\t";
            }
            else if (value < 0x20)
            {
                static constexpr char digits[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(digits[(value >> 4) & 0x0f]);
                out.push_back(digits[value & 0x0f]);
            }
            else
            {
                out.push_back(c);
            }
        }
        return out;
    }

    auto response_json(bool success,
                       const char* stage,
                       int applied,
                       int failures,
                       const std::string& message,
                       const std::string& metadata = "") -> std::string
    {
        std::string out = "{\"success\":";
        out += success ? "true" : "false";
        out += ",\"stage\":\"";
        out += stage;
        out += "\",\"applied\":";
        out += std::to_string(applied);
        out += ",\"failures\":";
        out += std::to_string(failures);
        out += ",\"message\":\"";
        out += json_escape(message);
        out += "\",\"timing_ms\":{},\"metadata\":{\"bridge\":\"runtime-bridge\"";
        if (!metadata.empty())
        {
            out += ",";
            out += metadata;
        }
        out += "}}\n";
        return out;
    }

    void complete_queued_paint_job(const std::shared_ptr<QueuedPaintJob>& job, const std::string& response)
    {
        if (!job)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            job->response = response;
            job->done = true;
        }
        g_paint_jobs_cv.notify_all();
    }

    auto resolve_bridge_port() -> int
    {
        wchar_t dll_path[MAX_PATH]{};
        if (g_module != nullptr && GetModuleFileNameW(g_module, dll_path, MAX_PATH) > 0)
        {
            std::wstring port_path = dll_path;
            port_path += L".port";
            HANDLE file = CreateFileW(port_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file != INVALID_HANDLE_VALUE)
            {
                char buffer[32]{};
                DWORD bytes_read = 0;
                const BOOL ok = ReadFile(file, buffer, static_cast<DWORD>(sizeof(buffer) - 1), &bytes_read, nullptr);
                CloseHandle(file);
                if (ok && bytes_read > 0)
                {
                    const int port = std::atoi(buffer);
                    if (port >= 1024 && port <= 65535)
                    {
                        return port;
                    }
                }
            }
        }
        return DefaultBridgePort;
    }

    auto bridge_sidecar_path(const wchar_t* suffix) -> std::wstring
    {
        wchar_t dll_path[MAX_PATH]{};
        if (g_module != nullptr && GetModuleFileNameW(g_module, dll_path, MAX_PATH) > 0)
        {
            std::wstring path = dll_path;
            path += suffix;
            return path;
        }
        return {};
    }

    auto write_bridge_progress(const std::string& stage,
                               const std::string& message,
                               int step,
                               int total_steps,
                               double elapsed_ms,
                               const std::string& extra = "") -> void
    {
        const auto path = bridge_sidecar_path(L".progress.json");
        if (path.empty())
        {
            return;
        }
        const double progress = total_steps > 0 ? std::max(0.0, std::min(1.0, static_cast<double>(step) / static_cast<double>(total_steps))) : 0.0;
        const bool stream_progress = extra.find("server_batch_index") != std::string::npos ||
                                     extra.find("server_batches") != std::string::npos ||
                                     extra.find("server_sent") != std::string::npos;
        const double eta_ms = stream_progress && progress > 0.02 ? std::max(0.0, (elapsed_ms / progress) - elapsed_ms) : 0.0;
        std::string json = "{\"stage\":\"" + json_escape(stage) +
                           "\",\"message\":\"" + json_escape(message) +
                           "\",\"step\":" + std::to_string(step) +
                           ",\"total_steps\":" + std::to_string(total_steps) +
                           ",\"progress\":" + std::to_string(progress) +
                           ",\"elapsed_ms\":" + std::to_string(elapsed_ms) +
                           ",\"eta_ms\":" + std::to_string(eta_ms);
        if (!extra.empty())
        {
            json += ",";
            json += extra;
        }
        json += "}\n";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        DWORD written = 0;
        WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written, nullptr);
        CloseHandle(file);
    }

    auto clear_bridge_progress() -> void
    {
        const auto path = bridge_sidecar_path(L".progress.json");
        if (!path.empty())
        {
            DeleteFileW(path.c_str());
        }
    }

    auto write_bridge_sidecar_text(const wchar_t* suffix, const std::string& text) -> bool
    {
        const auto path = bridge_sidecar_path(suffix);
        if (path.empty())
        {
            return false;
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD written = 0;
        const auto ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        CloseHandle(file);
        return ok && written == text.size();
    }

    auto write_binary_file_w(const std::wstring& path, const std::vector<std::uint8_t>& bytes) -> bool
    {
        if (path.empty() || bytes.empty())
        {
            return false;
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD written = 0;
        const auto ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        CloseHandle(file);
        return ok && written == bytes.size();
    }

    auto read_bridge_sidecar_text(const wchar_t* suffix, std::string& text) -> bool
    {
        text.clear();
        const auto path = bridge_sidecar_path(suffix);
        if (path.empty())
        {
            return false;
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 16LL * 1024LL * 1024LL)
        {
            CloseHandle(file);
            return false;
        }
        text.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        const auto ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
        CloseHandle(file);
        if (!ok || read != text.size())
        {
            text.clear();
            return false;
        }
        return true;
    }

    auto read_text_file_w(const std::wstring& path, std::string& text) -> bool
    {
        text.clear();
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 32LL * 1024LL * 1024LL)
        {
            CloseHandle(file);
            return false;
        }
        text.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        const auto ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
        CloseHandle(file);
        if (!ok || read != text.size())
        {
            text.clear();
            return false;
        }
        return true;
    }

    auto bridge_directory_path() -> std::wstring
    {
        wchar_t dll_path[MAX_PATH]{};
        if (g_module == nullptr || GetModuleFileNameW(g_module, dll_path, MAX_PATH) <= 0)
        {
            return {};
        }
        std::wstring path = dll_path;
        const auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
        {
            return {};
        }
        return path.substr(0, slash);
    }

    auto ensure_directory(const std::wstring& path) -> bool
    {
        if (path.empty())
        {
            return false;
        }
        if (CreateDirectoryW(path.c_str(), nullptr))
        {
            return true;
        }
        return GetLastError() == ERROR_ALREADY_EXISTS;
    }

    auto runtime_log_dir_path() -> std::wstring
    {
        wchar_t local_appdata[MAX_PATH]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return {};
        }
        std::wstring base = local_appdata;
        const std::wstring root = base + L"\\MecchaCamouflage";
        const std::wstring runtime = root + L"\\runtime";
        if (!ensure_directory(root) || !ensure_directory(runtime))
        {
            return {};
        }
        return runtime;
    }

    struct ModuleRange
    {
        std::uintptr_t base{0};
        std::size_t size{0};
    };

    auto main_module_range() -> ModuleRange
    {
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
        {
            return {};
        }
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            return {};
        }
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return {};
        }
        return {reinterpret_cast<std::uintptr_t>(base), nt->OptionalHeader.SizeOfImage};
    }

    auto address_in_main_module(std::uintptr_t address) -> bool
    {
        const auto module = main_module_range();
        return module.base && address >= module.base && address < module.base + module.size;
    }

    auto live_uobject(std::uintptr_t object) -> bool
    {
        if (!object || address_in_main_module(object))
        {
            return false;
        }
        const auto flags = safe_read<std::uint32_t>(object + OffObjectFlags, 0);
        return (flags & RFClassDefaultObject) == 0;
    }

    auto match_pattern(const std::uint8_t* data, const std::uint8_t* pattern, const std::uint8_t* mask, std::size_t length) -> bool
    {
        for (std::size_t i = 0; i < length; ++i)
        {
            if (mask[i] && data[i] != pattern[i])
            {
                return false;
            }
        }
        return true;
    }

    auto scan_pattern(const std::vector<std::uint8_t>& pattern, const std::vector<std::uint8_t>& mask) -> std::uintptr_t
    {
        const auto module = main_module_range();
        if (!module.base || !module.size || pattern.empty() || pattern.size() != mask.size())
        {
            return 0;
        }
        const auto* base = reinterpret_cast<const std::uint8_t*>(module.base);
        const std::size_t length = pattern.size();
        for (std::size_t offset = 0; offset + length < module.size; ++offset)
        {
            __try
            {
                if (match_pattern(base + offset, pattern.data(), mask.data(), length))
                {
                    return module.base + offset;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        return 0;
    }

    auto early_hex_address(std::uintptr_t value) -> std::string;

    struct FNameResolver
    {
        std::uintptr_t pool{0};
        int table_offset{0x10};
        int style{1};
        const int offsets[14]{0x8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70};

        auto entry(std::uint32_t id, int table, int entry_style) const -> std::string
        {
            const auto block_index = id >> 16;
            const auto within = (id & 0xFFFF) << 1;
            const auto block = safe_read<std::uintptr_t>(pool + table + static_cast<std::uintptr_t>(block_index) * 8);
            if (!block)
            {
                return {};
            }
            const auto header = safe_read<std::uint16_t>(block + within);
            bool wide = false;
            int length = 0;
            if (entry_style == 0)
            {
                wide = (header & 1) != 0;
                length = header >> 1;
            }
            else if (entry_style == 2)
            {
                wide = (header & 1) != 0;
                length = (header >> 6) & 0x3FF;
            }
            else
            {
                length = header & 0x3FF;
                wide = ((header >> 10) & 1) != 0;
            }
            if (length <= 0 || length > 512)
            {
                return {};
            }
            if (wide)
            {
                std::wstring text(length, L'\0');
                if (!safe_copy(text.data(), reinterpret_cast<void*>(block + within + 2), static_cast<std::size_t>(length) * sizeof(wchar_t)))
                {
                    return {};
                }
                std::string out{};
                out.reserve(text.size());
                for (wchar_t c : text)
                {
                    out.push_back(c >= 0 && c < 128 ? static_cast<char>(c) : '?');
                }
                return out;
            }
            std::string text(length, '\0');
            if (!safe_copy(text.data(), reinterpret_cast<void*>(block + within + 2), static_cast<std::size_t>(length)))
            {
                return {};
            }
            return text;
        }

        auto detect() -> void
        {
            for (const int off : offsets)
            {
                for (const int st : {2, 1, 0})
                {
                    if (entry(0, off, st) == "None")
                    {
                        table_offset = off;
                        style = st;
                        return;
                    }
                }
            }
        }

        auto resolve(std::uint32_t id) -> std::string
        {
            auto name = entry(id, table_offset, style);
            if (!name.empty())
            {
                return name;
            }
            for (const int off : offsets)
            {
                for (const int st : {2, 1, 0})
                {
                    name = entry(id, off, st);
                    if (!name.empty())
                    {
                        table_offset = off;
                        style = st;
                        return name;
                    }
                }
            }
            return {};
        }
    };

    struct Reflection
    {
        std::uintptr_t guobject_array{0};
        std::uintptr_t fname_pool{0};
        FNameResolver names{};
        std::uintptr_t meta_class{0};

        auto init(std::string& failure) -> bool
        {
            static const std::vector<std::uint8_t> gu_sig{0x48, 0x8D, 0x05, 0, 0, 0, 0, 0x48, 0x89, 0x01, 0x45, 0x8B, 0xD1};
            static const std::vector<std::uint8_t> gu_mask{1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1};
            const auto gu_ref = scan_pattern(gu_sig, gu_mask);
            if (!gu_ref)
            {
                failure = "guobject_pattern_not_found";
                return false;
            }
            const auto rel = safe_read<std::int32_t>(gu_ref + 3);
            guobject_array = gu_ref + 7 + rel;
            const auto delta_candidate = guobject_array - 0xE3B40;
            names.pool = delta_candidate;
            names.detect();
            if (names.resolve(0) == "None")
            {
                fname_pool = delta_candidate;
                return true;
            }

            const std::vector<std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>> patterns{
                {{0x48, 0x8D, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x4C, 0x8B, 0xC0}, {1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1}},
                {{0x48, 0x8D, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x48, 0x8B}, {1, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1}},
                {{0x48, 0x8D, 0x35, 0, 0, 0, 0}, {1, 1, 1, 0, 0, 0, 0}},
                {{0x48, 0x8D, 0x3D, 0, 0, 0, 0}, {1, 1, 1, 0, 0, 0, 0}},
            };
            for (const auto& [sig, mask] : patterns)
            {
                const auto ref = scan_pattern(sig, mask);
                if (!ref)
                {
                    continue;
                }
                const auto fname_rel = safe_read<std::int32_t>(ref + 3);
                names.pool = ref + 7 + fname_rel;
                names.detect();
                if (names.resolve(0) == "None")
                {
                    fname_pool = names.pool;
                    return true;
                }
            }
            failure = "fname_pool_not_found";
            return false;
        }

        auto object_name(std::uintptr_t object) -> std::string
        {
            if (!object)
            {
                return {};
            }
            auto out = object_name_raw(object);
            const auto slash = out.find_last_of("/.");
            if (slash != std::string::npos)
            {
                out = out.substr(slash + 1);
            }
            if (out.rfind("Default__", 0) == 0)
            {
                out = out.substr(9);
            }
            return out;
        }

        auto object_name_raw(std::uintptr_t object) -> std::string
        {
            if (!object)
            {
                return {};
            }
            return names.resolve(safe_read<std::uint32_t>(object + OffName));
        }

        auto object_path(std::uintptr_t object) -> std::string
        {
            std::vector<std::string> parts{};
            for (int depth = 0; live_uobject(object) && depth < 32; ++depth)
            {
                auto name = object_name_raw(object);
                if (name.empty())
                {
                    name = early_hex_address(object);
                }
                parts.push_back(name);
                object = safe_read<std::uintptr_t>(object + OffOuter);
            }
            if (parts.empty())
            {
                return {};
            }
            std::reverse(parts.begin(), parts.end());
            std::string out{};
            for (const auto& part : parts)
            {
                if (part.empty())
                {
                    continue;
                }
                if (!out.empty())
                {
                    out += '.';
                }
                out += part;
            }
            return out;
        }

        auto class_ptr(std::uintptr_t object) -> std::uintptr_t
        {
            return object ? safe_read<std::uintptr_t>(object + OffClass) : 0;
        }

        auto class_name(std::uintptr_t object) -> std::string
        {
            return object_name(class_ptr(object));
        }

        template <typename Fn>
        auto for_each_object(Fn fn) -> void
        {
            const auto chunks = safe_read<std::uintptr_t>(guobject_array + 0x10);
            if (!chunks)
            {
                return;
            }
            for (int ci = 0; ci < 64; ++ci)
            {
                const auto chunk = safe_read<std::uintptr_t>(chunks + static_cast<std::uintptr_t>(ci) * 8);
                if (!chunk)
                {
                    break;
                }
                for (int wi = 0; wi < 65536; ++wi)
                {
                    const auto obj = safe_read<std::uintptr_t>(chunk + static_cast<std::uintptr_t>(wi) * 0x18);
                    if (obj && fn(obj))
                    {
                        return;
                    }
                }
            }
        }

        auto find_meta_class() -> std::uintptr_t
        {
            if (meta_class)
            {
                return meta_class;
            }
            for_each_object([&](std::uintptr_t obj) {
                if (object_name(obj) == "Class")
                {
                    meta_class = obj;
                    return true;
                }
                return false;
            });
            return meta_class;
        }

        auto find_class(const char* name) -> std::uintptr_t
        {
            const auto meta = find_meta_class();
            if (!meta)
            {
                return 0;
            }
            std::uintptr_t found = 0;
            for_each_object([&](std::uintptr_t obj) {
                if (class_ptr(obj) == meta && object_name(obj) == name)
                {
                    found = obj;
                    return true;
                }
                return false;
            });
            return found;
        }

        auto find_first_instance(const char* class_name_text) -> std::uintptr_t
        {
            const auto cls = find_class(class_name_text);
            if (!cls)
            {
                return 0;
            }
            std::uintptr_t found = 0;
            for_each_object([&](std::uintptr_t obj) {
                if (class_ptr(obj) == cls && object_name(obj).rfind("Default__", 0) != 0)
                {
                    found = obj;
                    return true;
                }
                return false;
            });
            return found;
        }

        auto find_property(std::uintptr_t structure, const char* property_name) -> std::uintptr_t
        {
            for (auto prop = safe_read<std::uintptr_t>(structure + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
            {
                if (names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)) == property_name)
                {
                    return prop;
                }
            }
            return 0;
        }

        auto resolve_property_offset(const char* class_name_text, const char* property_name) -> int
        {
            auto cls = find_class(class_name_text);
            for (int depth = 0; cls && depth < 32; ++depth)
            {
                const auto prop = find_property(cls, property_name);
                if (prop)
                {
                    return safe_read<int>(prop + OffFPropertyOffset, -1);
                }
                cls = safe_read<std::uintptr_t>(cls + OffSuperStruct);
            }
            return -1;
        }

        auto find_function(std::uintptr_t object, const char* function_name) -> std::uintptr_t
        {
            auto cls = class_ptr(object);
            for (int depth = 0; cls && depth < 64; ++depth)
            {
                for (auto child = safe_read<std::uintptr_t>(cls + OffChildren); child; child = safe_read<std::uintptr_t>(child + OffUFieldNext))
                {
                    if (object_name(child) == function_name)
                    {
                        return child;
                    }
                }
                cls = safe_read<std::uintptr_t>(cls + OffSuperStruct);
            }
            return 0;
        }
    };

    struct Color
    {
        double r{1.0};
        double g{1.0};
        double b{1.0};
        double roughness{0.0};
        double metallic{1.0};
        int apply_mode{0};
    };

    struct FrontSample
    {
        double u{0.5};
        double v{0.5};
        double r{1.0};
        double g{1.0};
        double b{1.0};
        double roughness{0.65};
        double metallic{0.0};
        double screen_nx{0.5};
        double screen_ny{0.5};
        int uv_island{-1};
        int dominant_bone{-1};
        int mesh_region{0};
        int plan_index{-1};
        std::string body_region{"unknown"};
        bool has_world_position{false};
        sdk::FVector world_position{};
        bool has_component_position{false};
        sdk::FVector component_position{};
    };

    auto clamp01(double value) -> double;
    auto prop_offset(std::uintptr_t prop) -> int;
    auto prop_element_size(std::uintptr_t prop) -> int;
    auto write_number(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, double value) -> bool;
    auto process_event(std::uintptr_t object, std::uintptr_t function, std::uint8_t* params, std::string& failure) -> bool;
    auto read_return_bool(Reflection& ref, std::uintptr_t function, std::uint8_t* params) -> bool;

    auto clamp01(double value) -> double
    {
        return std::max(0.0, std::min(1.0, value));
    }

    auto sdk_worker_count_for_items(std::size_t item_count) -> unsigned
    {
        const auto hardware = std::max(1U, std::thread::hardware_concurrency());
        const auto useful = item_count < 65536
                                ? 1U
                                : std::min<unsigned>(hardware, static_cast<unsigned>((item_count + 65535) / 65536));
        return std::max(1U, useful);
    }

    template <typename Fn>
    auto sdk_parallel_ranges(std::size_t item_count, Fn&& fn) -> void
    {
        const auto workers = sdk_worker_count_for_items(item_count);
        if (workers <= 1 || item_count == 0)
        {
            fn(0, item_count, 0);
            return;
        }
        std::vector<std::thread> threads{};
        threads.reserve(workers);
        for (unsigned worker = 0; worker < workers; ++worker)
        {
            const auto begin = (item_count * static_cast<std::size_t>(worker)) / static_cast<std::size_t>(workers);
            const auto end = (item_count * static_cast<std::size_t>(worker + 1)) / static_cast<std::size_t>(workers);
            threads.emplace_back([begin, end, worker, &fn]() {
                fn(begin, end, worker);
            });
        }
        for (auto& thread : threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    auto prop_offset(std::uintptr_t prop) -> int
    {
        return safe_read<int>(prop + OffFPropertyOffset, -1);
    }

    auto prop_element_size(std::uintptr_t prop) -> int
    {
        return safe_read<int>(prop + OffFPropertyElementSize, 0);
    }

    auto find_property_any(Reflection& ref, std::uintptr_t structure, std::initializer_list<const char*> field_names) -> std::uintptr_t
    {
        if (!structure)
        {
            return 0;
        }
        for (const auto* field_name : field_names)
        {
            if (field_name)
            {
                if (const auto prop = ref.find_property(structure, field_name))
                {
                    return prop;
                }
            }
        }
        for (auto prop = safe_read<std::uintptr_t>(structure + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto prop_name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            for (const auto* field_name : field_names)
            {
                if (field_name && prop_name == lower_copy(field_name))
                {
                    return prop;
                }
            }
        }
        return 0;
    }

    auto struct_has_any_field(Reflection& ref, std::uintptr_t structure, std::initializer_list<const char*> field_names) -> bool
    {
        return find_property_any(ref, structure, field_names) != 0;
    }

    auto struct_type(Reflection& ref, std::uintptr_t prop, std::initializer_list<const char*> field_names) -> std::uintptr_t
    {
        const std::uintptr_t candidate_offsets[]{
            OffFStructPropertyStruct,
            0x68,
            0x70,
            0x80,
            0x88,
            0x90,
            0x98,
            0xA0,
        };
        for (const auto offset : candidate_offsets)
        {
            const auto structure = safe_read<std::uintptr_t>(prop + offset);
            if (struct_has_any_field(ref, structure, field_names))
            {
                return structure;
            }
        }
        return safe_read<std::uintptr_t>(prop + OffFStructPropertyStruct);
    }

    auto strict_vector_struct_type(Reflection& ref, std::uintptr_t prop, std::initializer_list<const char*> field_names, int min_size) -> std::uintptr_t
    {
        if (prop_element_size(prop) < min_size)
        {
            return 0;
        }
        const auto structure = struct_type(ref, prop, field_names);
        if (!structure)
        {
            return 0;
        }
        for (const auto* field_name : field_names)
        {
            if (!field_name || !find_property_any(ref, structure, {field_name}))
            {
                return 0;
            }
        }
        return structure;
    }

    auto array_inner_property_for_struct_fields(Reflection& ref,
                                                std::uintptr_t array_prop,
                                                std::initializer_list<const char*> field_names) -> std::uintptr_t
    {
        const std::uintptr_t candidate_offsets[]{
            0x68,
            0x70,
            0x78,
            0x80,
            0x88,
            0x90,
            0x98,
            0xA0,
            0xA8,
        };
        for (const auto offset : candidate_offsets)
        {
            const auto inner = safe_read<std::uintptr_t>(array_prop + offset);
            if (!inner)
            {
                continue;
            }
            const auto structure = struct_type(ref, inner, field_names);
            if (!structure)
            {
                continue;
            }
            bool has_all_fields = true;
            for (const auto* field_name : field_names)
            {
                if (!field_name || !find_property_any(ref, structure, {field_name}))
                {
                    has_all_fields = false;
                    break;
                }
            }
            if (has_all_fields)
            {
                return inner;
            }
        }
        return 0;
    }

    auto reflected_field_offset(Reflection& ref, std::uintptr_t structure, const char* field_name) -> int
    {
        const auto prop = find_property_any(ref, structure, {field_name});
        return prop ? prop_offset(prop) : -1;
    }

    auto reflected_field_size(Reflection& ref, std::uintptr_t structure, const char* field_name) -> int
    {
        const auto prop = find_property_any(ref, structure, {field_name});
        return prop ? prop_element_size(prop) : -1;
    }

    auto paint_stroke_reflection_metadata(Reflection& ref, std::uintptr_t server_batch_function) -> std::string
    {
        if (!server_batch_function)
        {
            return ",\"paint_stroke_reflection_ok\":false,\"paint_stroke_reflection_failure\":\"server_batch_unavailable\"";
        }
        const auto batch_prop = ref.find_property(server_batch_function, "Batch");
        if (!batch_prop)
        {
            return ",\"paint_stroke_reflection_ok\":false,\"paint_stroke_reflection_failure\":\"batch_param_unavailable\"";
        }
        const auto batch_struct = struct_type(ref, batch_prop, {"Strokes"});
        const auto strokes_prop = find_property_any(ref, batch_struct, {"Strokes"});
        if (!batch_struct || !strokes_prop)
        {
            return ",\"paint_stroke_reflection_ok\":false,\"paint_stroke_reflection_failure\":\"strokes_array_unavailable\"";
        }
        const auto stroke_inner = array_inner_property_for_struct_fields(ref, strokes_prop, {"Uv", "WorldPosition", "BrushSettings", "ChannelData"});
        const auto stroke_struct = struct_type(ref, stroke_inner, {"Uv", "WorldPosition", "BrushSettings", "ChannelData"});
        if (!stroke_inner || !stroke_struct)
        {
            return ",\"paint_stroke_reflection_ok\":false,\"paint_stroke_reflection_failure\":\"stroke_struct_unavailable\"" +
                   std::string(",\"paint_stroke_reflected_batch_offset\":") + std::to_string(prop_offset(batch_prop)) +
                   ",\"paint_stroke_reflected_strokes_offset\":" + std::to_string(prop_offset(strokes_prop));
        }
        const int reflected_size = prop_element_size(stroke_inner);
        const int uv_offset = reflected_field_offset(ref, stroke_struct, "Uv");
        const int world_offset = reflected_field_offset(ref, stroke_struct, "WorldPosition");
        const int has_world_offset = reflected_field_offset(ref, stroke_struct, "bHasWorldPosition");
        const int local_offset = reflected_field_offset(ref, stroke_struct, "LocalPosition");
        const int has_local_offset = reflected_field_offset(ref, stroke_struct, "bHasLocalPosition");
        const int has_triangle_offset = reflected_field_offset(ref, stroke_struct, "bHasSkeletalTriangleAnchor");
        const int triangle_index_offset = reflected_field_offset(ref, stroke_struct, "SkeletalTriangleIndex");
        const int bary_offset = reflected_field_offset(ref, stroke_struct, "SkeletalTriangleBarycentric");
        const int brush_offset = reflected_field_offset(ref, stroke_struct, "BrushSettings");
        const int channel_offset = reflected_field_offset(ref, stroke_struct, "ChannelData");
        const int target_offset = reflected_field_offset(ref, stroke_struct, "TargetChannel");
        const bool static_layout_matches =
            reflected_size == static_cast<int>(sizeof(sdk::FPaintStroke)) &&
            uv_offset == static_cast<int>(offsetof(sdk::FPaintStroke, Uv)) &&
            world_offset == static_cast<int>(offsetof(sdk::FPaintStroke, WorldPosition)) &&
            has_world_offset == static_cast<int>(offsetof(sdk::FPaintStroke, bHasWorldPosition)) &&
            local_offset == static_cast<int>(offsetof(sdk::FPaintStroke, LocalPosition)) &&
            has_local_offset == static_cast<int>(offsetof(sdk::FPaintStroke, bHasLocalPosition)) &&
            has_triangle_offset == static_cast<int>(offsetof(sdk::FPaintStroke, bHasSkeletalTriangleAnchor)) &&
            triangle_index_offset == static_cast<int>(offsetof(sdk::FPaintStroke, SkeletalTriangleIndex)) &&
            bary_offset == static_cast<int>(offsetof(sdk::FPaintStroke, SkeletalTriangleBarycentric)) &&
            brush_offset == static_cast<int>(offsetof(sdk::FPaintStroke, BrushSettings)) &&
            channel_offset == static_cast<int>(offsetof(sdk::FPaintStroke, ChannelData)) &&
            target_offset == static_cast<int>(offsetof(sdk::FPaintStroke, TargetChannel));
        return ",\"paint_stroke_reflection_ok\":true"
               ",\"paint_stroke_static_layout_matches\":" + std::string(static_layout_matches ? "true" : "false") +
               ",\"paint_stroke_reflected_size\":" + std::to_string(reflected_size) +
               ",\"paint_stroke_static_size\":" + std::to_string(sizeof(sdk::FPaintStroke)) +
               ",\"paint_stroke_reflected_batch_offset\":" + std::to_string(prop_offset(batch_prop)) +
               ",\"paint_stroke_reflected_strokes_offset\":" + std::to_string(prop_offset(strokes_prop)) +
               ",\"paint_stroke_reflected_strokes_size\":" + std::to_string(prop_element_size(strokes_prop)) +
               ",\"paint_stroke_reflected_uv_offset\":" + std::to_string(uv_offset) +
               ",\"paint_stroke_reflected_world_offset\":" + std::to_string(world_offset) +
               ",\"paint_stroke_reflected_has_world_offset\":" + std::to_string(has_world_offset) +
               ",\"paint_stroke_reflected_local_offset\":" + std::to_string(local_offset) +
               ",\"paint_stroke_reflected_has_local_offset\":" + std::to_string(has_local_offset) +
               ",\"paint_stroke_reflected_has_triangle_offset\":" + std::to_string(has_triangle_offset) +
               ",\"paint_stroke_reflected_triangle_index_offset\":" + std::to_string(triangle_index_offset) +
               ",\"paint_stroke_reflected_bary_offset\":" + std::to_string(bary_offset) +
               ",\"paint_stroke_reflected_brush_offset\":" + std::to_string(brush_offset) +
               ",\"paint_stroke_reflected_channel_offset\":" + std::to_string(channel_offset) +
               ",\"paint_stroke_reflected_target_offset\":" + std::to_string(target_offset) +
               ",\"paint_stroke_reflected_bary_size\":" + std::to_string(reflected_field_size(ref, stroke_struct, "SkeletalTriangleBarycentric"));
    }

    auto early_hex_address(std::uintptr_t value) -> std::string
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
        return buffer;
    }

    auto hex_address(std::uintptr_t value) -> std::string
    {
        return early_hex_address(value);
    }

    auto property_name_at_or_before_offset(Reflection& ref, std::uintptr_t object, int offset) -> std::string
    {
        std::string best_name{};
        int best_offset = -1;
        for (auto cls = ref.class_ptr(object); cls; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
        {
            for (auto prop = safe_read<std::uintptr_t>(cls + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
            {
                const auto current_offset = prop_offset(prop);
                if (current_offset <= offset && current_offset > best_offset)
                {
                    best_offset = current_offset;
                    best_name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
                }
            }
        }
        if (best_name.empty())
        {
            return "";
        }
        return best_name + "+" + std::to_string(offset - best_offset);
    }

    auto find_object_property(Reflection& ref, std::uintptr_t object, const char* property_name) -> std::uintptr_t
    {
        auto cls = ref.class_ptr(object);
        for (int depth = 0; cls && depth < 32; ++depth)
        {
            const auto prop = ref.find_property(cls, property_name);
            if (prop)
            {
                return prop;
            }
            cls = safe_read<std::uintptr_t>(cls + OffSuperStruct);
        }
        return 0;
    }

    auto write_number(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, double value) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* dest = container + offset;
        const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
        const auto size = prop_element_size(prop);
        const bool integral = contains_text(name, "channel") || contains_text(name, "mode") || contains_text(name, "level") ||
                              contains_text(name, "resolution") || contains_text(name, "triangles") || contains_text(name, "pixels");
        if (integral)
        {
            if (size <= 1)
            {
                *dest = static_cast<std::uint8_t>(value);
            }
            else
            {
                *reinterpret_cast<std::int32_t*>(dest) = static_cast<std::int32_t>(value);
            }
            return true;
        }
        if (size == 8)
        {
            *reinterpret_cast<double*>(dest) = value;
            return true;
        }
        if (size >= 4)
        {
            *reinterpret_cast<float*>(dest) = static_cast<float>(value);
            return true;
        }
        if (size == 1)
        {
            *dest = static_cast<std::uint8_t>(value);
            return true;
        }
        return false;
    }

    auto write_bool(std::uintptr_t prop, std::uint8_t* container, bool value) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        *(container + offset) = value ? 1 : 0;
        return true;
    }

    auto process_event(std::uintptr_t object, std::uintptr_t function, std::uint8_t* params, std::string& failure) -> bool
    {
        auto target = g_original_process_event.load();
        if (!target)
        {
            const auto vtable = safe_read<std::uintptr_t>(object);
            if (!vtable)
            {
                failure = "vtable_unavailable";
                return false;
            }
            target = safe_read<std::uintptr_t>(vtable + static_cast<std::uintptr_t>(ProcessEventVtableIndex) * sizeof(std::uintptr_t));
            if (!target)
            {
                failure = "process_event_unavailable";
                return false;
            }
        }
        if (!address_in_main_module(target))
        {
            failure = "process_event_target_outside_main_module";
            return false;
        }
        __try
        {
            reinterpret_cast<ProcessEventFn>(target)(reinterpret_cast<void*>(object), reinterpret_cast<void*>(function), params);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            failure = "process_event_exception";
            return false;
        }
    }

    auto read_return_bool(Reflection& ref, std::uintptr_t function, std::uint8_t* params) -> bool
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (name == "ReturnValue")
            {
                const auto offset = prop_offset(prop);
                return offset < 0 ? true : (*(params + offset) != 0);
            }
        }
        return true;
    }

    auto read_return_object(Reflection& ref, std::uintptr_t function, std::uint8_t* params) -> std::uintptr_t
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (name == "ReturnValue")
            {
                const auto offset = prop_offset(prop);
                return offset < 0 ? 0 : safe_read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(params + offset));
            }
        }
        return 0;
    }

    auto call_no_params_return_object(Reflection& ref, std::uintptr_t object, const char* function_name) -> std::uintptr_t
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return 0;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 1024)
        {
            return 0;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(1, params_size)), 0);
        std::string failure{};
        if (!process_event(object, function, params.data(), failure))
        {
            return 0;
        }
        return read_return_object(ref, function, params.data());
    }

    auto call_no_params_return_bool(Reflection& ref, std::uintptr_t object, const char* function_name) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 1024)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(1, params_size)), 0);
        std::string failure{};
        if (!process_event(object, function, params.data(), failure))
        {
            return false;
        }
        return read_return_bool(ref, function, params.data());
    }

    auto read_object_property_by_names(Reflection& ref, std::uintptr_t object, std::initializer_list<const char*> names) -> std::uintptr_t
    {
        if (!live_uobject(object))
        {
            return 0;
        }
        for (const auto* name : names)
        {
            if (!name)
            {
                continue;
            }
            const auto prop = find_object_property(ref, object, name);
            const auto offset = prop ? prop_offset(prop) : -1;
            if (offset < 0)
            {
                continue;
            }
            const auto value = safe_read<std::uintptr_t>(object + offset);
            if (live_uobject(value))
            {
                return value;
            }
        }
        return 0;
    }

    struct ComponentSelection
    {
        std::uintptr_t component{0};
        std::uintptr_t owner{0};
        std::uintptr_t target{0};
        std::string target_source{};
        std::uintptr_t target_mesh{0};
        std::string mesh_source{};
        std::string source{};
        std::uintptr_t pawn{0};
    };

    auto find_component(Reflection& ref, std::string& failure) -> ComponentSelection
    {
        ComponentSelection selected{};
        const auto root_component_offset = ref.resolve_property_offset("Actor", "RootComponent");
        const auto attach_children_offset = ref.resolve_property_offset("SceneComponent", "AttachChildren");
        const auto owned_components_offset = ref.resolve_property_offset("Actor", "OwnedComponents");
        int owner_offset = ref.resolve_property_offset("ActorComponent", "OwnerPrivate");
        if (owner_offset < 0)
        {
            owner_offset = ref.resolve_property_offset("ActorComponent", "Owner");
        }

        const auto engine = ref.find_first_instance("GameEngine");
        const auto viewport_offset = ref.resolve_property_offset("Engine", "GameViewport");
        const auto world_offset = ref.resolve_property_offset("GameViewportClient", "World");
        const auto game_instance_offset = ref.resolve_property_offset("World", "OwningGameInstance");
        const auto local_players_offset = ref.resolve_property_offset("GameInstance", "LocalPlayers");
        int controller_offset = ref.resolve_property_offset("Player", "PlayerController");
        if (controller_offset < 0)
        {
            controller_offset = ref.resolve_property_offset("LocalPlayer", "PlayerController");
        }
        int pawn_offset = ref.resolve_property_offset("PlayerController", "AcknowledgedPawn");
        if (pawn_offset < 0)
        {
            pawn_offset = ref.resolve_property_offset("Controller", "Pawn");
        }
        const auto viewport = viewport_offset >= 0 ? safe_read<std::uintptr_t>(engine + viewport_offset) : 0;
        const auto world = world_offset >= 0 ? safe_read<std::uintptr_t>(viewport + world_offset) : 0;
        const auto game_instance = game_instance_offset >= 0 ? safe_read<std::uintptr_t>(world + game_instance_offset) : 0;
        const auto local_players_data = local_players_offset >= 0 ? safe_read<std::uintptr_t>(game_instance + local_players_offset) : 0;
        const auto local_players_count = local_players_offset >= 0 ? safe_read<int>(game_instance + local_players_offset + 8) : 0;
        const auto local_player = local_players_data && local_players_count > 0 ? safe_read<std::uintptr_t>(local_players_data) : 0;
        auto controller = controller_offset >= 0 ? safe_read<std::uintptr_t>(local_player + controller_offset) : 0;
        auto pawn = pawn_offset >= 0 ? safe_read<std::uintptr_t>(controller + pawn_offset) : 0;
        auto read_controller_pawn = [&](std::uintptr_t candidate_controller) -> std::uintptr_t {
            if (!live_uobject(candidate_controller))
            {
                return 0;
            }
            if (pawn_offset >= 0)
            {
                if (const auto candidate_pawn = safe_read<std::uintptr_t>(candidate_controller + pawn_offset); live_uobject(candidate_pawn))
                {
                    return candidate_pawn;
                }
            }
            const auto candidate_pawn = call_no_params_return_object(ref, candidate_controller, "GetPawn");
            return live_uobject(candidate_pawn) ? candidate_pawn : 0;
        };
        if (!live_uobject(pawn))
        {
            pawn = read_controller_pawn(controller);
        }
        if (!live_uobject(pawn))
        {
            ref.for_each_object([&](std::uintptr_t obj) {
                if (!live_uobject(obj))
                {
                    return false;
                }
                const auto cls = lower_copy(ref.class_name(obj));
                if (!contains_text(cls, "playercontroller"))
                {
                    return false;
                }
                if (const auto candidate_pawn = read_controller_pawn(obj))
                {
                    controller = obj;
                    pawn = candidate_pawn;
                    return true;
                }
                return false;
            });
        }
        selected.pawn = pawn;
        const auto controller_view_target = live_uobject(controller) ? call_no_params_return_object(ref, controller, "GetViewTarget") : 0;
        const auto camera = live_uobject(controller) ? call_no_params_return_object(ref, controller, "GetPlayerCameraManager") : 0;
        const auto camera_view_target = live_uobject(camera) ? call_no_params_return_object(ref, camera, "GetViewTarget") : 0;
        std::vector<std::pair<std::uintptr_t, const char*>> targets{};
        auto add_target = [&](std::uintptr_t object, const char* source) {
            if (!live_uobject(object))
            {
                return;
            }
            for (const auto& existing : targets)
            {
                if (existing.first == object)
                {
                    return;
                }
            }
            targets.push_back({object, source});
        };
        add_target(controller_view_target, "controller_view_target");
        add_target(camera_view_target, "camera_view_target");
        add_target(pawn, "controller_pawn");

        auto read_owner = [&](std::uintptr_t obj) -> std::uintptr_t {
            if (owner_offset >= 0)
            {
                if (const auto owner = safe_read<std::uintptr_t>(obj + owner_offset))
                {
                    return owner;
                }
            }
            return call_no_params_return_object(ref, obj, "GetOwner");
        };

        auto live_object = [&](std::uintptr_t obj) -> bool {
            return live_uobject(obj);
        };

        auto owner_matches_target = [&](std::uintptr_t owner) -> bool {
            for (const auto& target : targets)
            {
                if (owner && owner == target.first)
                {
                    return true;
                }
            }
            return false;
        };

        auto target_source_for_owner = [&](std::uintptr_t owner) -> const char* {
            for (const auto& target : targets)
            {
                if (owner && owner == target.first)
                {
                    return target.second;
                }
            }
            return "";
        };

        auto outer_matches_target = [&](std::uintptr_t object, std::uintptr_t& matched_target, const char*& matched_source) -> bool {
            for (int depth = 0; live_uobject(object) && depth < 8; ++depth)
            {
                const auto outer = safe_read<std::uintptr_t>(object + OffOuter);
                if (!live_uobject(outer))
                {
                    return false;
                }
                if (owner_matches_target(outer))
                {
                    matched_target = outer;
                    matched_source = target_source_for_owner(outer);
                    return true;
                }
                object = outer;
            }
            return false;
        };

        std::vector<std::pair<std::uintptr_t, const char*>> target_meshes{};
        auto add_target_mesh = [&](std::uintptr_t mesh, const char* source) {
            if (!live_uobject(mesh))
            {
                return;
            }
            const auto cls = lower_copy(ref.class_name(mesh));
            if (!contains_text(cls, "mesh"))
            {
                return;
            }
            for (const auto& existing : target_meshes)
            {
                if (existing.first == mesh)
                {
                    return;
                }
            }
            target_meshes.push_back({mesh, source});
        };

        auto collect_meshes_from_actor = [&](std::uintptr_t actor, const char* source) {
            add_target_mesh(read_object_property_by_names(ref,
                                                          actor,
                                                          {"Mesh", "MeshComponent", "SkeletalMeshComponent", "TargetMeshComponent", "TargetMesh"}),
                            source);
            if (root_component_offset >= 0 && attach_children_offset >= 0)
            {
                const auto root = safe_read<std::uintptr_t>(actor + root_component_offset);
                const auto data = safe_read<std::uintptr_t>(root + attach_children_offset);
                const auto count = safe_read<int>(root + attach_children_offset + 8);
                if (data && count > 0 && count <= 512)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        add_target_mesh(safe_read<std::uintptr_t>(data + static_cast<std::uintptr_t>(i) * 8), source);
                    }
                }
            }
            if (owned_components_offset >= 0)
            {
                const auto data = safe_read<std::uintptr_t>(actor + owned_components_offset);
                const auto count = safe_read<int>(actor + owned_components_offset + 8);
                if (data && count > 0 && count <= 512)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        add_target_mesh(safe_read<std::uintptr_t>(data + static_cast<std::uintptr_t>(i) * 8), source);
                    }
                }
            }
        };

        for (const auto& target : targets)
        {
            collect_meshes_from_actor(target.first, target.second);
        }

        ref.for_each_object([&](std::uintptr_t obj) {
            if (!live_uobject(obj))
            {
                return false;
            }
            const auto cls = lower_copy(ref.class_name(obj));
            if (!contains_text(cls, "meshcomponent"))
            {
                return false;
            }
            const auto owner = read_owner(obj);
            if (owner_matches_target(owner))
            {
                add_target_mesh(obj, target_source_for_owner(owner));
            }
            return false;
        });

        auto mesh_match_source = [&](std::uintptr_t mesh) -> const char* {
            for (const auto& target_mesh : target_meshes)
            {
                if (mesh && mesh == target_mesh.first)
                {
                    return target_mesh.second;
                }
            }
            return "";
        };

        auto property_reference_matches = [&](std::uintptr_t object,
                                              std::uintptr_t& matched_target,
                                              const char*& matched_target_source,
                                              std::uintptr_t& matched_mesh,
                                              const char*& matched_mesh_source) -> bool {
            auto cls = ref.class_ptr(object);
            for (int depth = 0; cls && depth < 32; ++depth)
            {
                for (auto prop = safe_read<std::uintptr_t>(cls + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
                {
                    const auto offset = prop_offset(prop);
                    if (offset < 0 || offset > 0x10000)
                    {
                        continue;
                    }
                    const auto value = safe_read<std::uintptr_t>(object + offset);
                    if (!live_uobject(value))
                    {
                        continue;
                    }
                    if (owner_matches_target(value))
                    {
                        matched_target = value;
                        matched_target_source = target_source_for_owner(value);
                        return true;
                    }
                    const auto* source = mesh_match_source(value);
                    if (source && source[0] != '\0')
                    {
                        matched_mesh = value;
                        matched_mesh_source = source;
                        return true;
                    }
                }
                cls = safe_read<std::uintptr_t>(cls + OffSuperStruct);
            }
            return false;
        };

        auto component_matches_target_mesh = [&](std::uintptr_t obj, std::uintptr_t& matched_mesh, const char*& matched_source) -> bool {
            const auto mesh = read_object_property_by_names(ref,
                                                            obj,
                                                            {"TargetMeshComponent",
                                                             "TargetMesh",
                                                             "MeshComponent",
                                                             "SkeletalMeshComponent",
                                                             "Mesh",
                                                             "OwnerMesh"});
            const auto* source = mesh_match_source(mesh);
            if (mesh && source && source[0] != '\0')
            {
                matched_mesh = mesh;
                matched_source = source;
                return true;
            }
            return false;
        };

        int candidate_count = 0;
        int owner_match_count = 0;
        int outer_match_count = 0;
        int ref_match_count = 0;
        int mesh_match_count = 0;
        int any_owner_candidate_count = 0;
        auto check_component = [&](std::uintptr_t obj, const char* source, bool require_owner) -> bool {
            if (!live_object(obj))
            {
                return false;
            }
            const auto cls = lower_copy(ref.class_name(obj));
            const bool paint_component = contains_text(cls, "runtimepaint") || contains_text(cls, "paint");
            const bool server_ready = ref.find_function(obj, "ServerPaintBatch") != 0;
            if (paint_component && server_ready)
            {
                ++candidate_count;
                const auto owner = read_owner(obj);
                const bool owner_match = owner_matches_target(owner);
                std::uintptr_t matched_outer_target = 0;
                const char* matched_outer_source = "";
                const bool outer_match = outer_matches_target(obj, matched_outer_target, matched_outer_source);
                std::uintptr_t matched_ref_target = 0;
                const char* matched_ref_target_source = "";
                std::uintptr_t matched_mesh = 0;
                const char* matched_mesh_source = "";
                bool mesh_match = component_matches_target_mesh(obj, matched_mesh, matched_mesh_source);
                const bool ref_match = property_reference_matches(obj, matched_ref_target, matched_ref_target_source, matched_mesh, matched_mesh_source);
                mesh_match = mesh_match || (matched_mesh != 0);
                if (owner_match)
                {
                    ++owner_match_count;
                }
                if (outer_match)
                {
                    ++outer_match_count;
                }
                if (ref_match)
                {
                    ++ref_match_count;
                }
                if (mesh_match)
                {
                    ++mesh_match_count;
                }
                if (require_owner && !owner_match && !outer_match && !ref_match && !mesh_match)
                {
                    return false;
                }
                selected.component = obj;
                selected.owner = owner;
                selected.target = owner_match ? owner : (outer_match ? matched_outer_target : matched_ref_target);
                selected.target_source = owner_match ? target_source_for_owner(owner) : (outer_match ? matched_outer_source : matched_ref_target_source);
                selected.target_mesh = matched_mesh;
                selected.mesh_source = matched_mesh ? matched_mesh_source : "";
                selected.source = source ? source : "unknown";
                return true;
            }
            return false;
        };

        for (const auto& target : targets)
        {
            if (root_component_offset >= 0 && attach_children_offset >= 0)
            {
                const auto root = safe_read<std::uintptr_t>(target.first + root_component_offset);
                const auto data = safe_read<std::uintptr_t>(root + attach_children_offset);
                const auto count = safe_read<int>(root + attach_children_offset + 8);
                if (data && count > 0 && count <= 512)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        if (check_component(safe_read<std::uintptr_t>(data + static_cast<std::uintptr_t>(i) * 8), "root_attach_children", false))
                        {
                            selected.target = target.first;
                            selected.target_source = target.second;
                            return selected;
                        }
                    }
                }
            }
            if (owned_components_offset >= 0)
            {
                const auto data = safe_read<std::uintptr_t>(target.first + owned_components_offset);
                const auto count = safe_read<int>(target.first + owned_components_offset + 8);
                if (data && count > 0 && count <= 512)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        if (check_component(safe_read<std::uintptr_t>(data + static_cast<std::uintptr_t>(i) * 8), "owned_components", false))
                        {
                            selected.target = target.first;
                            selected.target_source = target.second;
                            return selected;
                        }
                    }
                }
            }
        }

        struct OwnedComponentCandidate
        {
            std::uintptr_t component{0};
            std::uintptr_t owner{0};
            std::uintptr_t mesh{0};
            std::string mesh_source{};
            int score{-1000000};
        };
        OwnedComponentCandidate best_owned{};
        ref.for_each_object([&](std::uintptr_t obj) {
            if (!live_object(obj))
            {
                return false;
            }
            const auto cls = lower_copy(ref.class_name(obj));
            if (!(contains_text(cls, "runtimepaint") || contains_text(cls, "paint")) ||
                !ref.find_function(obj, "ServerPaintBatch"))
            {
                return false;
            }
            const auto owner = read_owner(obj);
            if (!live_uobject(owner))
            {
                return false;
            }
            ++any_owner_candidate_count;
            const auto owner_cls = lower_copy(ref.class_name(owner));
            int score = 10;
            if (owner_matches_target(owner))
            {
                score += 1000;
            }
            if (call_no_params_return_bool(ref, owner, "IsPlayerControlled"))
            {
                score += 250;
            }
            if (contains_text(owner_cls, "character"))
            {
                score += 80;
            }
            if (contains_text(owner_cls, "pawn"))
            {
                score += 60;
            }
            if (call_no_params_return_object(ref, owner, "GetController"))
            {
                score += 25;
            }
            std::uintptr_t matched_mesh = 0;
            const char* matched_mesh_source = "";
            if (component_matches_target_mesh(obj, matched_mesh, matched_mesh_source))
            {
                score += 40;
            }
            if (score > best_owned.score)
            {
                best_owned.component = obj;
                best_owned.owner = owner;
                best_owned.mesh = matched_mesh;
                best_owned.mesh_source = matched_mesh_source ? matched_mesh_source : "";
                best_owned.score = score;
            }
            return false;
        });
        if (best_owned.component && best_owned.score >= 10)
        {
            selected.component = best_owned.component;
            selected.owner = best_owned.owner;
            selected.target = best_owned.owner;
            selected.target_source = owner_matches_target(best_owned.owner) ? target_source_for_owner(best_owned.owner) : "owned_runtimepaint_owner_scan";
            selected.target_mesh = best_owned.mesh;
            selected.mesh_source = best_owned.mesh_source;
            selected.source = "owned_runtimepaint_owner_scan";
            selected.pawn = best_owned.owner;
            return selected;
        }

        ref.for_each_object([&](std::uintptr_t obj) {
            return check_component(obj, "owned_runtimepaint_scan", true);
        });
        if (selected.component)
        {
            return selected;
        }
        if (!selected.component)
        {
            failure = "runtime_paint_component_unavailable pawn=" + hex_address(pawn) +
                      " view_target=" + hex_address(controller_view_target) +
                      " camera_view_target=" + hex_address(camera_view_target) +
                      " meshes=" + std::to_string(target_meshes.size()) +
                      " candidates=" + std::to_string(candidate_count) +
                      " any_owner_candidates=" + std::to_string(any_owner_candidate_count) +
                      " owner_matches=" + std::to_string(owner_match_count) +
                      " outer_matches=" + std::to_string(outer_match_count) +
                      " ref_matches=" + std::to_string(ref_match_count) +
                      " mesh_matches=" + std::to_string(mesh_match_count);
        }
        return selected;
    }

    auto install_process_event_hook(std::string& failure) -> bool
    {
        if (g_process_event_hook_installed.load())
        {
            return true;
        }
        DWORD thread_id = 0;
        const DWORD process_id = GetCurrentProcessId();
        EnumWindows(
            [](HWND hwnd, LPARAM lparam) -> BOOL {
                DWORD owner_pid = 0;
                const DWORD tid = GetWindowThreadProcessId(hwnd, &owner_pid);
                if (owner_pid == GetCurrentProcessId() && tid != 0 && IsWindowVisible(hwnd))
                {
                    *reinterpret_cast<DWORD*>(lparam) = tid;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&thread_id));
        if (thread_id == 0)
        {
            failure = "game_window_thread_unavailable pid=" + std::to_string(process_id);
            return false;
        }
        const auto hook = SetWindowsHookExW(WH_GETMESSAGE, message_hook_proc, nullptr, thread_id);
        if (!hook)
        {
            failure = "message_hook_install_failed win32=" + std::to_string(GetLastError()) + " thread=" + std::to_string(thread_id);
            return false;
        }
        g_message_hook.store(hook);
        g_game_thread_id.store(thread_id);
        g_process_event_hook_installed.store(true);
        PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
        return true;
    }

    auto uninstall_process_event_hook() -> void
    {
        const auto message_hook = g_message_hook.exchange(nullptr);
        if (message_hook)
        {
            UnhookWindowsHookEx(message_hook);
        }
        g_game_thread_id.store(0);
        const auto hook = reinterpret_cast<std::uintptr_t>(&hooked_process_event);
        std::lock_guard<std::mutex> hook_lock(g_hook_mutex);
        for (const auto& entry : g_process_event_hook_slots)
        {
            const auto slot_address = entry.first;
            const auto original = entry.second;
            auto* slot = reinterpret_cast<std::uintptr_t*>(slot_address);
            DWORD old_protect = 0;
            if (VirtualProtect(slot, sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &old_protect))
            {
                if (safe_read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(slot)) == hook)
                {
                    *slot = original;
                    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(std::uintptr_t));
                }
                DWORD ignored = 0;
                VirtualProtect(slot, sizeof(std::uintptr_t), old_protect, &ignored);
            }
        }
        g_process_event_hook_slots.clear();
        g_original_process_event.store(0);
        g_process_event_hook_installed.store(false);
    }

    auto json_bool(bool value) -> const char*
    {
        return value ? "true" : "false";
    }

    struct SdkResolutionException : std::runtime_error
    {
        std::string stage;

        SdkResolutionException(std::string stage_text, std::string message_text)
            : std::runtime_error(message_text), stage(std::move(stage_text))
        {
        }
    };

    [[noreturn]] auto throw_sdk_update_required(const std::string& message) -> void
    {
        throw SdkResolutionException("sdk_update_required", message);
    }

    struct SdkContext
    {
        bool ok{false};
        std::string stage{"sdk_unavailable"};
        std::string message{"sdk unavailable"};
        std::uintptr_t module_base{0};
        std::uintptr_t actual_guobject_array{0};
        std::string world_source{"runtime_object_scan"};
        std::string process_event_source{"uobject_vtable"};
        std::uintptr_t process_event{0};
        std::uintptr_t world{0};
        std::uintptr_t game_instance{0};
        int local_players_count{0};
        std::uintptr_t local_player{0};
        std::uintptr_t controller{0};
        std::uintptr_t k2_get_pawn_function{0};
        std::uintptr_t pawn{0};
        std::uintptr_t k2_get_actor_location_function{0};
        sdk::FVector body_world_position{};
        std::uintptr_t component{0};
        std::uintptr_t server_paint_batch_function{0};
    };

    struct SdkViewportInfo
    {
        int width{0};
        int height{0};
    };

    struct SdkDeprojectRay
    {
        bool ok{false};
        std::string failure{};
        sdk::FVector location{};
        sdk::FVector direction{};
    };

    auto sdk_vec_add(const sdk::FVector& a, const sdk::FVector& b) -> sdk::FVector
    {
        return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
    }

    auto sdk_vec_sub(const sdk::FVector& a, const sdk::FVector& b) -> sdk::FVector
    {
        return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
    }

    auto sdk_vec_mul(const sdk::FVector& a, double scale) -> sdk::FVector
    {
        return {a.X * scale, a.Y * scale, a.Z * scale};
    }

    auto sdk_vec_dot(const sdk::FVector& a, const sdk::FVector& b) -> double
    {
        return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    }

    auto sdk_vec_cross(const sdk::FVector& a, const sdk::FVector& b) -> sdk::FVector
    {
        return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
    }

    auto sdk_vec_len(const sdk::FVector& a) -> double
    {
        return std::sqrt(a.X * a.X + a.Y * a.Y + a.Z * a.Z);
    }

    auto sdk_vec_normalize(const sdk::FVector& a) -> sdk::FVector
    {
        const auto len = sdk_vec_len(a);
        if (len <= 0.000001)
        {
            return {};
        }
        return {a.X / len, a.Y / len, a.Z / len};
    }

    auto sdk_read_number(Reflection& ref, std::uintptr_t prop, std::uint8_t* container) -> double
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return 0.0;
        }
        auto* src = container + offset;
        const auto size = prop_element_size(prop);
        const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
        if (size == 8 && !contains_text(name, "int") && !contains_text(name, "channel") && !contains_text(name, "mode"))
        {
            return *reinterpret_cast<double*>(src);
        }
        if (size >= 4)
        {
            if (contains_text(name, "size") || contains_text(name, "width") || contains_text(name, "height") ||
                contains_text(name, "count") || contains_text(name, "index") || contains_text(name, "channel") ||
                contains_text(name, "mode"))
            {
                return static_cast<double>(*reinterpret_cast<std::int32_t*>(src));
            }
            return static_cast<double>(*reinterpret_cast<float*>(src));
        }
        if (size == 2)
        {
            return static_cast<double>(*reinterpret_cast<std::int16_t*>(src));
        }
        if (size == 1)
        {
            return static_cast<double>(*src);
        }
        return 0.0;
    }

    auto sdk_read_bool(std::uintptr_t prop, std::uint8_t* container) -> bool
    {
        const auto offset = prop_offset(prop);
        return offset >= 0 && *(container + offset) != 0;
    }

    auto sdk_write_object(std::uintptr_t prop, std::uint8_t* container, std::uintptr_t value) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        *reinterpret_cast<std::uintptr_t*>(container + offset) = value;
        return true;
    }

    auto sdk_read_object(std::uintptr_t prop, std::uint8_t* container) -> std::uintptr_t
    {
        const auto offset = prop_offset(prop);
        return offset < 0 ? 0 : safe_read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(container + offset));
    }

    auto sdk_write_vector3(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const sdk::FVector& value) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref, prop, {"X", "Y", "Z"});
        bool wrote = false;
        if (st)
        {
            const auto x = find_property_any(ref, st, {"X"});
            const auto y = find_property_any(ref, st, {"Y"});
            const auto z = find_property_any(ref, st, {"Z"});
            const auto xo = x ? prop_offset(x) : -1;
            const auto yo = y ? prop_offset(y) : -1;
            const auto zo = z ? prop_offset(z) : -1;
            if (xo >= 0 && yo > xo && zo > yo)
            {
                if (yo - xo >= 8 && zo - yo >= 8)
                {
                    *reinterpret_cast<double*>(base + xo) = value.X;
                    *reinterpret_cast<double*>(base + yo) = value.Y;
                    *reinterpret_cast<double*>(base + zo) = value.Z;
                }
                else
                {
                    *reinterpret_cast<float*>(base + xo) = static_cast<float>(value.X);
                    *reinterpret_cast<float*>(base + yo) = static_cast<float>(value.Y);
                    *reinterpret_cast<float*>(base + zo) = static_cast<float>(value.Z);
                }
                return true;
            }
        }
        const auto size = prop_element_size(prop);
        if (size >= 24)
        {
            const double values[3]{value.X, value.Y, value.Z};
            std::memcpy(base, values, sizeof(values));
            return true;
        }
        if (size >= 12)
        {
            const float values[3]{static_cast<float>(value.X), static_cast<float>(value.Y), static_cast<float>(value.Z)};
            std::memcpy(base, values, sizeof(values));
            return true;
        }
        return false;
    }

    auto sdk_read_vector3(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, sdk::FVector& out) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref, prop, {"X", "Y", "Z"});
        if (st)
        {
            const auto x = find_property_any(ref, st, {"X"});
            const auto y = find_property_any(ref, st, {"Y"});
            const auto z = find_property_any(ref, st, {"Z"});
            const auto xo = x ? prop_offset(x) : -1;
            const auto yo = y ? prop_offset(y) : -1;
            const auto zo = z ? prop_offset(z) : -1;
            if (xo >= 0 && yo > xo && zo > yo)
            {
                if (yo - xo >= 8 && zo - yo >= 8)
                {
                    out.X = *reinterpret_cast<double*>(base + xo);
                    out.Y = *reinterpret_cast<double*>(base + yo);
                    out.Z = *reinterpret_cast<double*>(base + zo);
                }
                else
                {
                    out.X = *reinterpret_cast<float*>(base + xo);
                    out.Y = *reinterpret_cast<float*>(base + yo);
                    out.Z = *reinterpret_cast<float*>(base + zo);
                }
                return std::isfinite(out.X) && std::isfinite(out.Y) && std::isfinite(out.Z);
            }
        }
        const auto size = prop_element_size(prop);
        if (size >= 24)
        {
            const auto* values = reinterpret_cast<double*>(base);
            out = {values[0], values[1], values[2]};
            return true;
        }
        if (size >= 12)
        {
            const auto* values = reinterpret_cast<float*>(base);
            out = {values[0], values[1], values[2]};
            return true;
        }
        return false;
    }

    auto sdk_read_vector2(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, double& x, double& y) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref, prop, {"X", "Y"});
        if (st)
        {
            const auto xp = find_property_any(ref, st, {"X"});
            const auto yp = find_property_any(ref, st, {"Y"});
            const auto xo = xp ? prop_offset(xp) : -1;
            const auto yo = yp ? prop_offset(yp) : -1;
            if (xo >= 0 && yo > xo)
            {
                if (yo - xo >= 8)
                {
                    x = *reinterpret_cast<double*>(base + xo);
                    y = *reinterpret_cast<double*>(base + yo);
                }
                else
                {
                    x = *reinterpret_cast<float*>(base + xo);
                    y = *reinterpret_cast<float*>(base + yo);
                }
                return std::isfinite(x) && std::isfinite(y);
            }
        }
        const auto size = prop_element_size(prop);
        if (size >= 16)
        {
            const auto* values = reinterpret_cast<double*>(base);
            x = values[0];
            y = values[1];
            return true;
        }
        if (size >= 8)
        {
            const auto* values = reinterpret_cast<float*>(base);
            x = values[0];
            y = values[1];
            return true;
        }
        return false;
    }

    auto sdk_call_no_params(Reflection& ref, std::uintptr_t object, const char* function_name) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 1024)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(1, params_size)), 0);
        std::string failure{};
        return process_event(object, function, params.data(), failure);
    }

    auto sdk_call_single_number(Reflection& ref, std::uintptr_t object, const char* function_name, double value) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 1024)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (name != "ReturnValue")
            {
                wrote = write_number(ref, prop, params.data(), value) || wrote;
            }
        }
        std::string failure{};
        return wrote && process_event(object, function, params.data(), failure);
    }

    auto sdk_call_single_bool(Reflection& ref, std::uintptr_t object, const char* function_name, bool value) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 1024)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (name != "ReturnValue")
            {
                wrote = write_bool(prop, params.data(), value) || wrote;
            }
        }
        std::string failure{};
        return wrote && process_event(object, function, params.data(), failure);
    }

    auto sdk_call_two_bools(Reflection& ref, std::uintptr_t object, const char* function_name, bool first, bool second) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 1024)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote = false;
        int bool_index = 0;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (name == "ReturnValue")
            {
                continue;
            }
            const bool value = (name.find("Propagate") != std::string::npos || bool_index > 0) ? second : first;
            wrote = write_bool(prop, params.data(), value) || wrote;
            ++bool_index;
        }
        std::string failure{};
        return wrote && process_event(object, function, params.data(), failure);
    }

    auto sdk_call_object_param(Reflection& ref, std::uintptr_t object, const char* function_name, std::uintptr_t value) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function || !value)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 1024)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (name != "ReturnValue")
            {
                wrote = sdk_write_object(prop, params.data(), value) || wrote;
            }
        }
        std::string failure{};
        return wrote && process_event(object, function, params.data(), failure);
    }

    auto sdk_context_metadata(Reflection& ref, const SdkContext& ctx) -> std::string
    {
        return "\"sdk_version\":\"runtime_dynamic_reflection_min\"" +
               std::string(",\"sdk_route\":\"sdk_server_paint_batch_strokes\"") +
               ",\"module_base\":\"" + hex_address(ctx.module_base) + "\"" +
               ",\"guobject_array\":\"" + hex_address(ctx.actual_guobject_array) + "\"" +
               ",\"global_offset_source\":\"runtime_pattern_scan\"" +
               ",\"world_source\":\"" + json_escape(ctx.world_source) + "\"" +
               ",\"process_event_source\":\"" + json_escape(ctx.process_event_source) + "\"" +
               ",\"process_event_vtable_index\":" + std::to_string(ProcessEventVtableIndex) +
               ",\"world\":\"" + hex_address(ctx.world) + "\"" +
               ",\"game_instance\":\"" + hex_address(ctx.game_instance) + "\"" +
               ",\"local_players_count\":" + std::to_string(ctx.local_players_count) +
               ",\"local_player\":\"" + hex_address(ctx.local_player) + "\"" +
               ",\"controller\":\"" + hex_address(ctx.controller) + "\"" +
               ",\"k2_get_pawn_function\":\"" + hex_address(ctx.k2_get_pawn_function) + "\"" +
               ",\"pawn\":\"" + hex_address(ctx.pawn) + "\"" +
               ",\"pawn_class\":\"" + json_escape(ref.class_name(ctx.pawn)) + "\"" +
               ",\"k2_get_actor_location_function\":\"" + hex_address(ctx.k2_get_actor_location_function) + "\"" +
               ",\"body_world_x\":" + std::to_string(ctx.body_world_position.X) +
               ",\"body_world_y\":" + std::to_string(ctx.body_world_position.Y) +
               ",\"body_world_z\":" + std::to_string(ctx.body_world_position.Z) +
               ",\"runtime_paintable_offset\":\"0xb68\"" +
               ",\"component\":\"" + hex_address(ctx.component) + "\"" +
               ",\"component_class\":\"" + json_escape(ref.class_name(ctx.component)) + "\"" +
               ",\"function_server_paint_batch_available\":" + std::string(json_bool(ctx.server_paint_batch_function != 0)) +
               ",\"function_server_paint_batch\":\"" + hex_address(ctx.server_paint_batch_function) + "\"" +
               ",\"param_schema\":\"FPaintStroke{Uv@0,WorldPosition@16,bHasWorldPosition@40,BrushSettings@104,ChannelData@144,TargetChannel@176};ServerPaintBatch{Batch@0}\"" +
               std::string(",\"sdk_replication_api\":\"component_server_paint_batch\"") +
               ",\"multiplayer_replicated\":true";
    }

    auto sdk_resolve_context(Reflection& ref) -> SdkContext
    {
        SdkContext ctx{};
        const auto module = main_module_range();
        ctx.module_base = module.base;
        ctx.actual_guobject_array = ref.guobject_array;
        if (!module.base)
        {
            ctx.stage = "sdk_unavailable";
            ctx.message = "main module unavailable";
            return ctx;
        }
        if (!ctx.actual_guobject_array)
        {
            throw_sdk_update_required("runtime GUObjectArray pattern scan failed");
        }
        auto world_has_local_context = [&](std::uintptr_t world) -> bool {
            if (!live_uobject(world))
            {
                return false;
            }
            const auto game_instance = safe_read<std::uintptr_t>(world + sdk::FieldOffsets::UWorld_OwningGameInstance);
            if (!live_uobject(game_instance))
            {
                return false;
            }
            const auto local_players = safe_read<sdk::TArray<std::uintptr_t>>(game_instance + sdk::FieldOffsets::UGameInstance_LocalPlayers);
            return local_players.Data && local_players.Num > 0 && local_players.Num <= 8;
        };

        const auto world_class = ref.find_class("World");
        if (!world_class)
        {
            throw_sdk_update_required("UWorld class unavailable from runtime object scan");
        }
        ref.for_each_object([&](std::uintptr_t object) {
            if (ref.class_ptr(object) == world_class && world_has_local_context(object))
            {
                ctx.world = object;
                return true;
            }
            return false;
        });
        if (!live_uobject(ctx.world))
        {
            throw_sdk_update_required("runtime object scan found no active UWorld with LocalPlayers");
        }
        ctx.game_instance = safe_read<std::uintptr_t>(ctx.world + sdk::FieldOffsets::UWorld_OwningGameInstance);
        if (!live_uobject(ctx.game_instance))
        {
            throw_sdk_update_required("UWorld::OwningGameInstance unavailable");
        }

        const auto local_players = safe_read<sdk::TArray<std::uintptr_t>>(ctx.game_instance + sdk::FieldOffsets::UGameInstance_LocalPlayers);
        ctx.local_players_count = local_players.Num;
        if (!local_players.Data || local_players.Num <= 0 || local_players.Num > 8)
        {
            throw_sdk_update_required("GameInstance.LocalPlayers is empty or invalid");
        }
        ctx.local_player = safe_read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(local_players.Data));
        if (!live_uobject(ctx.local_player))
        {
            ctx.stage = "local_pawn_unavailable";
            ctx.message = "LocalPlayers[0] unavailable";
            return ctx;
        }
        ctx.controller = safe_read<std::uintptr_t>(ctx.local_player + sdk::FieldOffsets::UPlayer_PlayerController);
        if (!live_uobject(ctx.controller))
        {
            ctx.stage = "local_pawn_unavailable";
            ctx.message = "LocalPlayers[0].PlayerController unavailable";
            return ctx;
        }
        ctx.k2_get_pawn_function = ref.find_function(ctx.controller, "K2_GetPawn");
        if (!ctx.k2_get_pawn_function)
        {
            ctx.stage = "local_pawn_unavailable";
            ctx.message = "PlayerController.K2_GetPawn unavailable";
            return ctx;
        }
        sdk::Controller_K2_GetPawn pawn_params{};
        std::string process_failure{};
        if (!process_event(ctx.controller, ctx.k2_get_pawn_function, reinterpret_cast<std::uint8_t*>(&pawn_params), process_failure))
        {
            ctx.stage = "local_pawn_unavailable";
            ctx.message = "K2_GetPawn ProcessEvent failed: " + process_failure;
            return ctx;
        }
        ctx.pawn = reinterpret_cast<std::uintptr_t>(pawn_params.ReturnValue);
        if (!live_uobject(ctx.pawn))
        {
            ctx.stage = "local_pawn_unavailable";
            ctx.message = "K2_GetPawn returned null or invalid pawn";
            return ctx;
        }
        ctx.k2_get_actor_location_function = ref.find_function(ctx.pawn, "K2_GetActorLocation");
        if (ctx.k2_get_actor_location_function)
        {
            sdk::Actor_K2_GetActorLocation location_params{};
            std::string location_failure{};
            if (process_event(ctx.pawn, ctx.k2_get_actor_location_function, reinterpret_cast<std::uint8_t*>(&location_params), location_failure))
            {
                ctx.body_world_position = location_params.ReturnValue;
            }
        }
        ctx.component = safe_read<std::uintptr_t>(ctx.pawn + sdk::FieldOffsets::BP_FirstPersonCharacter_RuntimePaintable);
        auto component_class = lower_copy(ref.class_name(ctx.component));
        if (!live_uobject(ctx.component) || !contains_text(component_class, "runtimepaint"))
        {
            std::string component_failure{};
            const auto selected = find_component(ref, component_failure);
            if (live_uobject(selected.component))
            {
                ctx.component = selected.component;
                if (live_uobject(selected.pawn))
                {
                    ctx.pawn = selected.pawn;
                }
                component_class = lower_copy(ref.class_name(ctx.component));
            }
        }
        if (!live_uobject(ctx.component) || !contains_text(component_class, "runtimepaint"))
        {
            ctx.stage = "paint_component_unavailable";
            ctx.message = "BP_FirstPersonCharacter.RuntimePaintable unavailable";
            return ctx;
        }
        ctx.server_paint_batch_function = ref.find_function(ctx.component, "ServerPaintBatch");
        ctx.ok = true;
        ctx.stage = "sdk_ready";
        ctx.message = "SDK context ready";
        return ctx;
    }

    struct SdkNativeFrontSampleResult
    {
        std::vector<FrontSample> samples{};
        std::string failure{};
        std::uintptr_t mesh{0};
        std::uintptr_t hit_test_function{0};
        std::string sampling_backend{"unset"};
        bool keep_occluded_projected_samples{false};
        int viewport_width{0};
        int viewport_height{0};
        int min_front_hits{0};
        int target_front_hits{0};
        int hard_attempt_budget{0};
    };

    struct SdkFrontCaptureResult
    {
        bool ok{false};
        std::string failure{"front_capture_unavailable"};
        std::vector<FrontSample> samples{};
        std::vector<Color> capture_pixels{};
        bool capture_pixels_available{false};
        bool capture_flip_x{false};
        bool capture_flip_y{false};
        std::string texture_source{"bulk_calibrated_direct_texture_unavailable"};
        int width{0};
        int height{0};
        std::uintptr_t render_target{0};
        std::uintptr_t capture_actor{0};
        std::uintptr_t capture_component{0};
        std::uintptr_t read_function{0};
        bool render_target_created{false};
        bool capture_actor_spawned{false};
        bool capture_component_found{false};
        bool texture_target_written{false};
        bool hide_component_called{false};
        bool capture_scene_called{false};
        double capture_fov{90.0};
        int viewport_width{0};
        int viewport_height{0};
        int requested_texture_width{0};
        int requested_texture_height{0};
        double viewport_aspect{1.0};
        double capture_aspect{1.0};
        double capture_scale_x{1.0};
        double capture_scale_y{1.0};
        std::string capture_resolution_source{"viewport"};
        std::uintptr_t camera_manager{0};
        bool camera_location_used{false};
        bool camera_rotation_used{false};
        bool camera_fov_used{false};
        std::string camera_manager_source{"function:GetPlayerCameraManager"};
        std::string camera_location_source{"deproject_center"};
        std::string camera_rotation_source{"deproject_center_ray"};
        std::string camera_fov_source{"deproject_horizontal"};
        sdk::FVector capture_location{};
        sdk::FVector capture_direction{};
        int project_attempts{0};
        int project_success{0};
        int project_failed{0};
        int project_out_of_view{0};
        double project_delta_sum_px{0.0};
        double project_delta_max_px{0.0};
        std::string projection_backend{"scene_capture_matrix_unset"};
        int visibility_input{0};
        int visibility_kept{0};
        int visibility_rejected{0};
        int visibility_cell_px{0};
        double visibility_depth_min{0.0};
        double visibility_depth_max{0.0};
        int read_attempts{0};
        int read_success{0};
        int missing_color{0};
        double raw_rgb_min{0.0};
        double raw_rgb_max{0.0};
        double raw_rgb_avg{0.0};
        double raw_luma_range{0.0};
        int raw_whiteish_samples{0};
        double resolved_rgb_delta_avg{0.0};
        double resolved_rgb_delta_max{0.0};
        int resolved_rgb_delta_samples{0};
        double rgb_min{0.0};
        double rgb_max{0.0};
        double rgb_avg{0.0};
        double luma_range{0.0};
        int whiteish_samples{0};
        bool uniform{false};
        bool all_whiteish{false};
        bool bulk_readback_used{false};
        bool image_bulk_calibration_ok{false};
        int bulk_candidates{0};
        int bulk_available{0};
        int bulk_decoded_pixels{0};
        int bulk_function_attempts{0};
        int bulk_process_event_ok{0};
        int bulk_array_param_count{0};
        int bulk_array_offset{-1};
        int bulk_array_num{0};
        int bulk_array_max{0};
        int bulk_array_element_size{0};
        std::string bulk_decode_candidate_type{"none"};
        int bulk_calibration_samples{0};
        int bulk_calibration_pairs{0};
        double bulk_calibration_best_median{0.0};
        double bulk_calibration_runner_up_median{0.0};
        std::string bulk_backend{"not_run"};
        std::string bulk_inner_type{"none"};
        std::string bulk_bool_variant{"none"};
        std::string bulk_color_transform{"identity"};
        std::string bulk_calibration_backend{"not_run"};
        std::string capture_transform_backend{"project_world_to_screen_scaled"};
    };

    auto sdk_find_object_named(Reflection& ref, const char* object_name) -> std::uintptr_t
    {
        std::uintptr_t found = 0;
        ref.for_each_object([&](std::uintptr_t object) {
            if (ref.object_name(object) == object_name)
            {
                found = object;
                return true;
            }
            return false;
        });
        return found;
    }

    struct ScriptStringParam
    {
        wchar_t* data{nullptr};
        int num{0};
        int max{0};
    };

    auto widen_ascii(const std::string& text) -> std::wstring
    {
        std::wstring out{};
        out.reserve(text.size());
        for (const char ch : text)
        {
            out.push_back(static_cast<unsigned char>(ch) < 128 ? static_cast<wchar_t>(ch) : L'?');
        }
        return out;
    }

    auto sdk_write_fstring_param(std::uintptr_t prop,
                                 std::uint8_t* container,
                                 const std::string& text,
                                 std::vector<std::wstring>& backing) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        backing.push_back(widen_ascii(text));
        auto& wide = backing.back();
        auto* dest = reinterpret_cast<ScriptStringParam*>(container + offset);
        dest->data = wide.empty() ? nullptr : wide.data();
        dest->num = static_cast<int>(wide.size()) + 1;
        dest->max = dest->num;
        return true;
    }

    auto sdk_write_linear_color_param(Reflection& ref,
                                      std::uintptr_t prop,
                                      std::uint8_t* container,
                                      bool failure) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* dest = container + offset;
        const auto st = struct_type(ref, prop, {"R", "G", "B", "A"});
        if (!st)
        {
            return false;
        }
        bool wrote = false;
        if (const auto p = find_property_any(ref, st, {"R"})) wrote = write_number(ref, p, dest, 1.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"G"})) wrote = write_number(ref, p, dest, failure ? 0.08 : 0.72) || wrote;
        if (const auto p = find_property_any(ref, st, {"B"})) wrote = write_number(ref, p, dest, failure ? 0.06 : 0.12) || wrote;
        if (const auto p = find_property_any(ref, st, {"A"})) wrote = write_number(ref, p, dest, 1.0) || wrote;
        return wrote;
    }

    auto sdk_screen_message(Reflection& ref,
                            const SdkContext& ctx,
                            const std::string& stage,
                            const std::string& message,
                            bool failure = false,
                            double duration = 2.0) -> bool
    {
        static std::string last_stage{};
        static auto last_emit = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (!failure && stage == last_stage &&
            std::chrono::duration<double>(now - last_emit).count() < 1.0)
        {
            return true;
        }
        last_stage = stage;
        last_emit = now;

        const std::string text = failure ? ("FAILED " + stage + ": " + message) : message;
        const auto library = sdk_find_object_named(ref, "Default__KismetSystemLibrary");
        const auto print_function = library ? ref.find_function(library, "PrintString") : 0;
        if (library && print_function)
        {
            const auto params_size = safe_read<int>(print_function + OffPropertiesSize, 0);
            if (params_size > 0 && params_size <= 4096)
            {
                std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
                std::vector<std::wstring> backing{};
                backing.reserve(4);
                bool wrote_string = false;
                for (auto prop = safe_read<std::uintptr_t>(print_function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
                {
                    const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
                    if (contains_text(name, "world") || contains_text(name, "context"))
                    {
                        sdk_write_object(prop, params.data(), ctx.world ? ctx.world : ctx.controller);
                    }
                    else if (contains_text(name, "string") || contains_text(name, "message"))
                    {
                        wrote_string = sdk_write_fstring_param(prop, params.data(), text, backing) || wrote_string;
                    }
                    else if (contains_text(name, "screen"))
                    {
                        write_bool(prop, params.data(), true);
                    }
                    else if (contains_text(name, "log"))
                    {
                        write_bool(prop, params.data(), false);
                    }
                    else if (contains_text(name, "duration"))
                    {
                        write_number(ref, prop, params.data(), failure ? 10.0 : duration);
                    }
                    else if (contains_text(name, "color"))
                    {
                        sdk_write_linear_color_param(ref, prop, params.data(), failure);
                    }
                }
                std::string pe_failure{};
                if (wrote_string && process_event(library, print_function, params.data(), pe_failure))
                {
                    return true;
                }
            }
        }

        const auto client_message = ctx.controller ? ref.find_function(ctx.controller, "ClientMessage") : 0;
        if (ctx.controller && client_message)
        {
            const auto params_size = safe_read<int>(client_message + OffPropertiesSize, 0);
            if (params_size > 0 && params_size <= 4096)
            {
                std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
                std::vector<std::wstring> backing{};
                backing.reserve(4);
                bool wrote_string = false;
                for (auto prop = safe_read<std::uintptr_t>(client_message + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
                {
                    const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
                    if (contains_text(name, "string") || contains_text(name, "message") || name == "s")
                    {
                        wrote_string = sdk_write_fstring_param(prop, params.data(), text, backing) || wrote_string;
                    }
                    else if (contains_text(name, "life") || contains_text(name, "duration") || contains_text(name, "time"))
                    {
                        write_number(ref, prop, params.data(), failure ? 10.0 : duration);
                    }
                }
                std::string pe_failure{};
                if (wrote_string && process_event(ctx.controller, client_message, params.data(), pe_failure))
                {
                    return true;
                }
            }
        }
        return false;
    }

    auto sdk_format_progress_text(const std::string& stage,
                                  const std::string& message,
                                  int step,
                                  int total_steps,
                                  double elapsed_ms) -> std::string
    {
        const double progress = total_steps > 0 ? std::max(0.0, std::min(1.0, static_cast<double>(step) / static_cast<double>(total_steps))) : 0.0;
        const double eta_ms = progress > 0.02 ? std::max(0.0, (elapsed_ms / progress) - elapsed_ms) : 0.0;
        std::string out = "Meccha p " + std::to_string(step) + "/" + std::to_string(total_steps) +
                          " " + stage +
                          " " + std::to_string(static_cast<int>(progress * 100.0)) + "%" +
                          " elapsed=" + std::to_string(static_cast<int>(elapsed_ms / 1000.0)) + "s";
        if (eta_ms > 0.0)
        {
            out += " eta=" + std::to_string(static_cast<int>(eta_ms / 1000.0)) + "s";
        }
        out += "\n" + message;
        return out;
    }

    auto sdk_object_is_or_belongs_to(Reflection& ref, std::uintptr_t object, std::uintptr_t target) -> bool
    {
        if (!live_uobject(object) || !live_uobject(target))
        {
            return false;
        }
        if (object == target)
        {
            return true;
        }
        for (auto current = object; live_uobject(current); current = safe_read<std::uintptr_t>(current + OffOuter))
        {
            if (current == target)
            {
                return true;
            }
        }
        const auto owner = read_object_property_by_names(ref, object, {"OwnerPrivate", "Owner"});
        if (owner == target)
        {
            return true;
        }
        for (auto current = owner; live_uobject(current); current = safe_read<std::uintptr_t>(current + OffOuter))
        {
            if (current == target)
            {
                return true;
            }
        }
        return false;
    }

    struct SdkFrontMeshCandidate
    {
        std::uintptr_t mesh{0};
        std::uintptr_t asset{0};
        std::string source{};
    };

    auto sdk_collect_front_mesh_candidates(Reflection& ref, const SdkContext& ctx) -> std::vector<SdkFrontMeshCandidate>
    {
        std::vector<SdkFrontMeshCandidate> out{};
        auto add_candidate = [&](std::uintptr_t mesh, const char* source) {
            if (!live_uobject(mesh))
            {
                return;
            }
            const auto cls = lower_copy(ref.class_name(mesh));
            if (!contains_text(cls, "mesh"))
            {
                return;
            }
            for (const auto& existing : out)
            {
                if (existing.mesh == mesh)
                {
                    return;
                }
            }
            const auto asset = read_object_property_by_names(ref,
                                                             mesh,
                                                             {"SkinnedAsset",
                                                              "SkeletalMesh",
                                                              "StaticMesh",
                                                              "Mesh",
                                                              "MeshAsset",
                                                              "SkeletalMeshAsset"});
            out.push_back({mesh, asset, source ? source : "unknown"});
        };

        add_candidate(call_no_params_return_object(ref, ctx.component, "GetInitializedPaintMesh"),
                      "runtime_paint_get_initialized_paint_mesh");
        add_candidate(read_object_property_by_names(ref,
                                                    ctx.component,
                                                    {"MeshComponent", "Mesh", "OwnerMesh"}),
                      "runtime_paint_component_property");
        add_candidate(read_object_property_by_names(ref,
                                                    ctx.pawn,
                                                    {"Mesh",
                                                     "FirstPersonMesh",
                                                     "BodyMesh",
                                                     "CharacterMesh",
                                                     "SkeletalMeshComponent",
                                                     "TargetMeshComponent"}),
                      "pawn_mesh_property");

        ref.for_each_object([&](std::uintptr_t object) {
            if (!live_uobject(object))
            {
                return false;
            }
            const auto cls = lower_copy(ref.class_name(object));
            if (!contains_text(cls, "meshcomponent"))
            {
                return false;
            }
            if (sdk_object_is_or_belongs_to(ref, object, ctx.pawn))
            {
                add_candidate(object, "pawn_owned_mesh_component_scan");
            }
            return false;
        });
        return out;
    }

    auto sdk_front_mesh_candidates_json(Reflection& ref, const std::vector<SdkFrontMeshCandidate>& candidates) -> std::string
    {
        std::string out = "[";
        bool first = true;
        for (const auto& candidate : candidates)
        {
            if (!first)
            {
                out += ",";
            }
            first = false;
            out += "{\"mesh\":\"" + hex_address(candidate.mesh) + "\"";
            out += ",\"source\":\"" + json_escape(candidate.source) + "\"";
            out += ",\"name\":\"" + json_escape(ref.object_name(candidate.mesh)) + "\"";
            out += ",\"class\":\"" + json_escape(ref.class_name(candidate.mesh)) + "\"";
            out += ",\"path\":\"" + json_escape(ref.object_path(candidate.mesh)) + "\"";
            out += ",\"asset\":\"" + hex_address(candidate.asset) + "\"";
            out += ",\"asset_name\":\"" + json_escape(ref.object_name(candidate.asset)) + "\"";
            out += ",\"asset_class\":\"" + json_escape(ref.class_name(candidate.asset)) + "\"";
            out += ",\"asset_path\":\"" + json_escape(ref.object_path(candidate.asset)) + "\"}";
        }
        out += "]";
        return out;
    }

    auto sdk_candidate_is_usable_paint_mesh(Reflection& ref, const SdkFrontMeshCandidate& candidate) -> bool
    {
        const auto text = lower_copy(ref.object_name(candidate.mesh) + " " +
                                     ref.class_name(candidate.mesh) + " " +
                                     ref.object_path(candidate.mesh) + " " +
                                     ref.object_name(candidate.asset) + " " +
                                     ref.class_name(candidate.asset) + " " +
                                     ref.object_path(candidate.asset));
        return contains_text(text, "mesh") &&
               (live_uobject(candidate.asset) || candidate.source == "runtime_paint_get_initialized_paint_mesh");
    }

    auto sdk_select_profile_mesh_candidate(Reflection& ref,
                                           const std::vector<SdkFrontMeshCandidate>& candidates,
                                           SdkFrontMeshCandidate& out) -> bool
    {
        for (const auto& candidate : candidates)
        {
            if (candidate.source == "runtime_paint_get_initialized_paint_mesh" &&
                sdk_candidate_is_usable_paint_mesh(ref, candidate))
            {
                out = candidate;
                return true;
            }
        }
        for (const auto& candidate : candidates)
        {
            if (sdk_candidate_is_usable_paint_mesh(ref, candidate))
            {
                out = candidate;
                return true;
            }
        }
        return false;
    }

    auto sdk_transform_finite(const sdk::FTransform& transform) -> bool
    {
        const double values[]{
            transform.Rotation.X,
            transform.Rotation.Y,
            transform.Rotation.Z,
            transform.Rotation.W,
            transform.Translation.X,
            transform.Translation.Y,
            transform.Translation.Z,
            transform.Scale3D.X,
            transform.Scale3D.Y,
            transform.Scale3D.Z,
        };
        for (const auto value : values)
        {
            if (!std::isfinite(value) || std::abs(value) > 1.0e8)
            {
                return false;
            }
        }
        return true;
    }

    auto sdk_transform_score(const sdk::FTransform& transform) -> int
    {
        if (!sdk_transform_finite(transform))
        {
            return -1000000;
        }
        int score = 0;
        const double quat_len = std::sqrt(transform.Rotation.X * transform.Rotation.X +
                                          transform.Rotation.Y * transform.Rotation.Y +
                                          transform.Rotation.Z * transform.Rotation.Z +
                                          transform.Rotation.W * transform.Rotation.W);
        if (quat_len > 0.25 && quat_len < 2.0)
        {
            score += 20;
        }
        const double scale_len = std::abs(transform.Scale3D.X) + std::abs(transform.Scale3D.Y) + std::abs(transform.Scale3D.Z);
        if (scale_len > 0.001 && scale_len < 1000.0)
        {
            score += 20;
        }
        const double translation_len = sdk_vec_len(transform.Translation);
        if (translation_len < 1000000.0)
        {
            score += 10;
        }
        if (translation_len > 0.0001)
        {
            score += 4;
        }
        return score;
    }

    auto sdk_read_transform_array(std::uintptr_t data,
                                  int count,
                                  std::vector<sdk::FTransform>& transforms,
                                  int& valid_count,
                                  double& min_translation_len,
                                  double& max_translation_len) -> bool
    {
        transforms.clear();
        valid_count = 0;
        min_translation_len = std::numeric_limits<double>::infinity();
        max_translation_len = 0.0;
        if (!data || count <= 0 || count > 512)
        {
            return false;
        }
        transforms.resize(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            auto& transform = transforms[static_cast<std::size_t>(i)];
            if (!safe_copy(&transform, reinterpret_cast<const void*>(data + static_cast<std::uintptr_t>(i) * sizeof(sdk::FTransform)), sizeof(transform)))
            {
                transforms.clear();
                return false;
            }
            if (sdk_transform_score(transform) > 0)
            {
                ++valid_count;
                const auto len = sdk_vec_len(transform.Translation);
                min_translation_len = std::min(min_translation_len, len);
                max_translation_len = std::max(max_translation_len, len);
            }
        }
        if (!std::isfinite(min_translation_len))
        {
            min_translation_len = 0.0;
        }
        return valid_count == count;
    }

    struct SdkPoseResolveResult
    {
        bool ok{false};
        std::string stage{"pose_resolver_unavailable"};
        std::string message{"pose resolver unavailable"};
        std::string source{};
        int expected_bones{0};
        int reported_bones{-1};
        int transform_count{0};
        int valid_transform_count{0};
        int array_offset{-1};
        std::uintptr_t array_data{0};
        double min_translation_len{0.0};
        double max_translation_len{0.0};
        bool trusted{false};
        int validation_score{0};
        int validation_scale_violations{0};
        int validation_hierarchy_violations{0};
        double validation_reference_delta_avg{0.0};
        double validation_reference_delta_max{0.0};
        std::string validation_failure{};
        std::vector<sdk::FTransform> component_space_transforms{};
    };

    auto sdk_pose_result_metadata(const SdkPoseResolveResult& pose) -> std::string
    {
        return "\"pose_stage\":\"" + json_escape(pose.stage) + "\"" +
               ",\"pose_ok\":" + json_bool(pose.ok) +
               ",\"pose_source\":\"" + json_escape(pose.source) + "\"" +
               ",\"pose_expected_bones\":" + std::to_string(pose.expected_bones) +
               ",\"pose_reported_bones\":" + std::to_string(pose.reported_bones) +
               ",\"pose_transform_count\":" + std::to_string(pose.transform_count) +
               ",\"pose_valid_transform_count\":" + std::to_string(pose.valid_transform_count) +
               ",\"pose_array_offset\":" + std::to_string(pose.array_offset) +
               ",\"pose_array_data\":\"" + hex_address(pose.array_data) + "\"" +
               ",\"pose_min_translation_len\":" + std::to_string(pose.min_translation_len) +
               ",\"pose_max_translation_len\":" + std::to_string(pose.max_translation_len) +
               ",\"pose_trusted\":" + json_bool(pose.trusted) +
               ",\"pose_validation_score\":" + std::to_string(pose.validation_score) +
               ",\"pose_validation_scale_violations\":" + std::to_string(pose.validation_scale_violations) +
               ",\"pose_validation_hierarchy_violations\":" + std::to_string(pose.validation_hierarchy_violations) +
               ",\"pose_validation_reference_delta_avg\":" + std::to_string(pose.validation_reference_delta_avg) +
               ",\"pose_validation_reference_delta_max\":" + std::to_string(pose.validation_reference_delta_max) +
               ",\"pose_validation_failure\":\"" + json_escape(pose.validation_failure) + "\"";
    }

    auto sdk_read_pose_array_at(Reflection& ref,
                                std::uintptr_t mesh,
                                int offset,
                                const std::string& source,
                                int expected_bones,
                                SdkPoseResolveResult& pose) -> bool
    {
        if (offset < 0 || offset > 0x20000)
        {
            return false;
        }
        const auto header = safe_read<sdk::TArray<sdk::FTransform>>(mesh + static_cast<std::uintptr_t>(offset));
        if (!header.Data || header.Num != expected_bones || header.Max < header.Num || header.Max > 512)
        {
            return false;
        }
        std::vector<sdk::FTransform> transforms{};
        int valid_count = 0;
        double min_translation_len = 0.0;
        double max_translation_len = 0.0;
        if (!sdk_read_transform_array(reinterpret_cast<std::uintptr_t>(header.Data),
                                      header.Num,
                                      transforms,
                                      valid_count,
                                      min_translation_len,
                                      max_translation_len))
        {
            return false;
        }
        pose.ok = true;
        pose.stage = "pose_resolved";
        pose.message = "current skinned pose resolved";
        pose.source = source.empty() ? property_name_at_or_before_offset(ref, mesh, offset) : source;
        pose.transform_count = header.Num;
        pose.valid_transform_count = valid_count;
        pose.array_offset = offset;
        pose.array_data = reinterpret_cast<std::uintptr_t>(header.Data);
        pose.min_translation_len = min_translation_len;
        pose.max_translation_len = max_translation_len;
        pose.component_space_transforms = std::move(transforms);
        return true;
    }

    auto sdk_try_pose_property_names(Reflection& ref,
                                     std::uintptr_t mesh,
                                     int expected_bones,
                                     SdkPoseResolveResult& pose) -> bool
    {
        const char* names[]{
            "ComponentSpaceTransforms",
            "ComponentSpaceTransformsArray",
            "CachedComponentSpaceTransforms",
            "SpaceBases",
            "BoneSpaceTransforms",
            "CachedBoneSpaceTransforms",
        };
        for (const auto* name : names)
        {
            if (!name)
            {
                continue;
            }
            const auto prop = find_object_property(ref, mesh, name);
            const auto offset = prop ? prop_offset(prop) : -1;
            if (offset >= 0 && sdk_read_pose_array_at(ref, mesh, offset, name, expected_bones, pose))
            {
                return true;
            }
        }
        return false;
    }

    auto sdk_scan_pose_transform_arrays(Reflection& ref,
                                        std::uintptr_t mesh,
                                        int expected_bones,
                                        SdkPoseResolveResult& pose) -> bool
    {
        struct Candidate
        {
            int score{0};
            int offset{-1};
            std::string source{};
            SdkPoseResolveResult pose{};
        };
        std::vector<Candidate> candidates{};
        for (int offset = 0; mesh && offset + static_cast<int>(sizeof(sdk::TArray<sdk::FTransform>)) <= 0x6000; offset += 8)
        {
            SdkPoseResolveResult candidate_pose{};
            candidate_pose.expected_bones = expected_bones;
            candidate_pose.reported_bones = pose.reported_bones;
            const auto source = property_name_at_or_before_offset(ref, mesh, offset);
            if (!sdk_read_pose_array_at(ref, mesh, offset, source, expected_bones, candidate_pose))
            {
                continue;
            }
            int score = candidate_pose.valid_transform_count * 10;
            const auto lower_source = lower_copy(source);
            if (contains_text(lower_source, "component") || contains_text(lower_source, "spacebase") || contains_text(lower_source, "space"))
            {
                score += 1000;
            }
            if (contains_text(lower_source, "bone"))
            {
                score += 100;
            }
            if (contains_text(lower_source, "cached"))
            {
                score += 25;
            }
            if (candidate_pose.max_translation_len > candidate_pose.min_translation_len + 1.0)
            {
                score += 20;
            }
            candidates.push_back({score, offset, source, std::move(candidate_pose)});
        }
        if (candidates.empty())
        {
            return false;
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score)
            {
                return a.score > b.score;
            }
            return a.offset < b.offset;
        });
        pose = std::move(candidates.front().pose);
        if (pose.source.empty())
        {
            pose.source = "guarded_component_array_scan";
        }
        else
        {
            pose.source = "guarded_component_array_scan:" + pose.source;
        }
        return true;
    }

    auto sdk_call_no_params_return_number(Reflection& ref, std::uintptr_t object, const char* function_name, double& value) -> bool;
    auto sdk_call_no_params_return_transform(Reflection& ref, std::uintptr_t object, const char* function_name, sdk::FTransform& value) -> bool;
    auto sdk_project_world_to_screen(Reflection& ref, const SdkContext& ctx, const sdk::FVector& world, double& x, double& y) -> bool;
    auto sdk_deproject_screen_position(Reflection& ref, const SdkContext& ctx, double screen_x, double screen_y) -> SdkDeprojectRay;
    auto sdk_get_viewport_info(Reflection& ref, const SdkContext& ctx) -> SdkViewportInfo;
    auto sdk_capture_front_colors(Reflection& ref,
                                  const SdkContext& ctx,
                                  const SdkNativeFrontSampleResult& native_front,
                                  int target_width,
                                  int target_height) -> SdkFrontCaptureResult;
    auto sdk_capture_metadata(const SdkFrontCaptureResult& capture) -> std::string;
    auto sdk_srgb_to_linear_unit(double value) -> double;
    auto sdk_make_channel(double r,
                          double g,
                          double b,
                          double metallic,
                          double roughness,
                          sdk::EPaintChannelApplyMode apply_mode) -> sdk::FPaintChannelData;
    auto sdk_make_uv_stroke(double u,
                            double v,
                            const sdk::FPaintChannelData& channel,
                            const sdk::FRuntimeBrushSettings& brush,
                            sdk::EPaintChannel target_channel) -> sdk::FPaintStroke;
    auto sdk_make_mesh_anchor_stroke(double u,
                                     double v,
                                     const sdk::FPaintChannelData& channel,
                                     const sdk::FRuntimeBrushSettings& brush,
                                     sdk::EPaintChannel target_channel,
                                     const sdk::FVector& world_position,
                                     const sdk::FVector& local_position,
                                     int triangle_index,
                                     double barycentric_a,
                                     double barycentric_b,
                                     double barycentric_c) -> sdk::FPaintStroke;
    auto sdk_call_paint_batch_function(std::uintptr_t component,
                                       std::uintptr_t function,
                                       const std::vector<sdk::FPaintStroke>& strokes,
                                       std::size_t offset,
                                       std::size_t count,
                                       std::string& failure) -> bool;
    auto sdk_call_server_paint_batch(const SdkContext& ctx,
                                     const std::vector<sdk::FPaintStroke>& strokes,
                                     std::size_t offset,
                                     std::size_t count,
                                     std::string& failure) -> bool;
    auto sdk_call_paint_at_uv_with_brush(std::uintptr_t component,
                                         std::uintptr_t function,
                                         const sdk::FPaintStroke& stroke,
                                         std::string& failure) -> bool;

    auto sdk_resolve_skinned_pose(Reflection& ref,
                                  std::uintptr_t mesh,
                                  int expected_bones) -> SdkPoseResolveResult
    {
        SdkPoseResolveResult pose{};
        pose.expected_bones = expected_bones;
        double reported_bones = -1.0;
        if (sdk_call_no_params_return_number(ref, mesh, "GetNumBones", reported_bones) && reported_bones >= 0.0)
        {
            pose.reported_bones = static_cast<int>(reported_bones + 0.5);
            if (pose.reported_bones > 0 && pose.reported_bones != expected_bones)
            {
                pose.stage = "pose_bone_count_mismatch";
                pose.message = "live mesh bone count does not match mesh profile";
                return pose;
            }
        }

        if (sdk_try_pose_property_names(ref, mesh, expected_bones, pose))
        {
            return pose;
        }
        if (sdk_scan_pose_transform_arrays(ref, mesh, expected_bones, pose))
        {
            return pose;
        }

        pose.stage = "pose_transform_array_unavailable";
        pose.message = "could not find a current FTransform array with the expected bone count";
        return pose;
    }

    auto count_occurrences(const std::string& text, const std::string& needle) -> int
    {
        if (needle.empty())
        {
            return 0;
        }
        int count = 0;
        std::size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos)
        {
            ++count;
            pos += needle.size();
        }
        return count;
    }

    auto json_key_window_contains(const std::string& text,
                                  const std::string& key,
                                  const std::string& expected,
                                  std::size_t window = 512) -> bool
    {
        const auto lower = lower_copy(text);
        const auto lower_key = lower_copy(key);
        const auto lower_expected = lower_copy(expected);
        const auto pos = lower.find(std::string("\"") + lower_key + "\"");
        if (pos == std::string::npos)
        {
            return false;
        }
        const auto end = std::min(lower.size(), pos + window);
        return lower.substr(pos, end - pos).find(lower_expected) != std::string::npos;
    }

    struct MeshFirstProfile
    {
        struct Influence
        {
            int bone{-1};
            double weight{0.0};
        };

        struct Vertex
        {
            sdk::FVector position{};
            double u{0.5};
            double v{0.5};
            std::vector<Influence> influences{};
        };

        struct Bone
        {
            int index{-1};
            int parent_index{-1};
            std::string name{};
            sdk::FTransform local_bind{};
        };

        struct TriangleMeta
        {
            int uv_island{-1};
            int dominant_bone{-1};
            std::string body_region{"unknown"};
            sdk::FVector local_normal{};
            double uv_area{0.0};
        };

        bool ok{false};
        std::string stage{"mesh_profile_missing"};
        std::string message{"mesh profile is missing"};
        int schema_version{0};
        std::string profile_id{};
        std::string profile_hash{};
        std::string source_path{};
        std::string export_name{};
        int texture_size{1024};
        int vertex_count{0};
        int index_count{0};
        int bone_count{0};
        bool expected_export{false};
        bool has_vertices{false};
        bool has_indices{false};
        std::vector<int> indices{};
        std::vector<Vertex> vertices{};
        std::vector<Bone> bones{};
        std::vector<TriangleMeta> triangles{};
    };

    auto json_matching_bracket(const std::string& text, std::size_t open, char open_ch, char close_ch) -> std::size_t
    {
        bool in_string = false;
        bool escaped = false;
        int depth = 0;
        for (std::size_t i = open; i < text.size(); ++i)
        {
            const char ch = text[i];
            if (in_string)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (ch == '\\')
                {
                    escaped = true;
                }
                else if (ch == '"')
                {
                    in_string = false;
                }
                continue;
            }
            if (ch == '"')
            {
                in_string = true;
                continue;
            }
            if (ch == open_ch)
            {
                ++depth;
            }
            else if (ch == close_ch)
            {
                --depth;
                if (depth == 0)
                {
                    return i;
                }
            }
        }
        return std::string::npos;
    }

    auto json_find_array_span(const std::string& text,
                              const std::string& key,
                              std::size_t& begin,
                              std::size_t& end) -> bool
    {
        const std::string needle = std::string("\"") + key + "\"";
        auto key_pos = text.find(needle);
        if (key_pos == std::string::npos)
        {
            return false;
        }
        auto open = text.find('[', key_pos + needle.size());
        if (open == std::string::npos)
        {
            return false;
        }
        auto close = json_matching_bracket(text, open, '[', ']');
        if (close == std::string::npos || close <= open)
        {
            return false;
        }
        begin = open + 1;
        end = close;
        return true;
    }

    template <typename Fn>
    auto json_for_each_object_in_array(const std::string& text,
                                       std::size_t begin,
                                       std::size_t end,
                                       Fn&& fn) -> void
    {
        for (std::size_t pos = begin; pos < end;)
        {
            const auto open = text.find('{', pos);
            if (open == std::string::npos || open >= end)
            {
                break;
            }
            const auto close = json_matching_bracket(text, open, '{', '}');
            if (close == std::string::npos || close >= end)
            {
                break;
            }
            fn(text.substr(open, close - open + 1));
            pos = close + 1;
        }
    }

    auto mesh_first_parse_indices(const std::string& text, std::vector<int>& indices) -> bool
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        if (!json_find_array_span(text, "Indices", begin, end))
        {
            return false;
        }
        indices.clear();
        indices.reserve(9000);
        const char* cursor = text.c_str() + begin;
        const char* limit = text.c_str() + end;
        while (cursor < limit)
        {
            while (cursor < limit && !std::isdigit(static_cast<unsigned char>(*cursor)) && *cursor != '-')
            {
                ++cursor;
            }
            if (cursor >= limit)
            {
                break;
            }
            char* parsed_end = nullptr;
            const long value = std::strtol(cursor, &parsed_end, 10);
            if (parsed_end == cursor)
            {
                break;
            }
            indices.push_back(static_cast<int>(value));
            cursor = parsed_end;
        }
        return !indices.empty();
    }

    auto mesh_first_parse_influences(const std::string& object_text,
                                     std::vector<MeshFirstProfile::Influence>& influences) -> void
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        if (!json_find_array_span(object_text, "Influences", begin, end))
        {
            return;
        }
        json_for_each_object_in_array(object_text, begin, end, [&](const std::string& influence_text) {
            MeshFirstProfile::Influence influence{};
            influence.bone = json_int_field(influence_text, "Bone", -1, -1, 4096);
            influence.weight = clamp_range(json_number_field(influence_text, "Weight", 0.0), 0.0, 1.0);
            if (influence.bone >= 0 && influence.weight > 0.0)
            {
                influences.push_back(influence);
            }
        });
    }

    auto mesh_first_parse_vertices(const std::string& text, std::vector<MeshFirstProfile::Vertex>& vertices) -> bool
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        if (!json_find_array_span(text, "Vertices", begin, end))
        {
            return false;
        }
        vertices.clear();
        vertices.reserve(2000);
        json_for_each_object_in_array(text, begin, end, [&](const std::string& vertex_text) {
            MeshFirstProfile::Vertex vertex{};
            vertex.position.X = json_number_field(vertex_text, "X", 0.0);
            vertex.position.Y = json_number_field(vertex_text, "Y", 0.0);
            vertex.position.Z = json_number_field(vertex_text, "Z", 0.0);
            vertex.u = clamp01(json_number_field(vertex_text, "U", 0.5));
            vertex.v = clamp01(json_number_field(vertex_text, "V", 0.5));
            mesh_first_parse_influences(vertex_text, vertex.influences);
            vertices.push_back(std::move(vertex));
        });
        return !vertices.empty();
    }

    auto mesh_first_parse_bones(const std::string& text,
                                int expected_bones,
                                std::vector<MeshFirstProfile::Bone>& bones) -> bool
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        if (!json_find_array_span(text, "Bones", begin, end) || expected_bones <= 0 || expected_bones > 512)
        {
            return false;
        }
        bones.assign(static_cast<std::size_t>(expected_bones), {});
        std::vector<bool> seen(static_cast<std::size_t>(expected_bones), false);
        json_for_each_object_in_array(text, begin, end, [&](const std::string& bone_text) {
            const int index = json_int_field(bone_text, "Index", -1, -1, 4096);
            if (index < 0 || index >= expected_bones)
            {
                return;
            }
            MeshFirstProfile::Bone bone{};
            bone.index = index;
            bone.parent_index = json_int_field(bone_text, "ParentIndex", -1, -1, 4096);
            bone.name = json_string_field(bone_text, "Name", "");
            bone.local_bind.Translation.X = json_number_field(bone_text, "X", 0.0);
            bone.local_bind.Translation.Y = json_number_field(bone_text, "Y", 0.0);
            bone.local_bind.Translation.Z = json_number_field(bone_text, "Z", 0.0);
            bone.local_bind.Rotation.X = json_number_field(bone_text, "RotationX", 0.0);
            bone.local_bind.Rotation.Y = json_number_field(bone_text, "RotationY", 0.0);
            bone.local_bind.Rotation.Z = json_number_field(bone_text, "RotationZ", 0.0);
            bone.local_bind.Rotation.W = json_number_field(bone_text, "RotationW", 1.0);
            bone.local_bind.Scale3D = {1.0, 1.0, 1.0};
            bones[static_cast<std::size_t>(index)] = bone;
            seen[static_cast<std::size_t>(index)] = true;
        });
        return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
    }

    auto mesh_first_parse_triangle_metadata(const std::string& text,
                                            int expected_triangles,
                                            std::vector<MeshFirstProfile::TriangleMeta>& triangles) -> bool
    {
        std::size_t begin = 0;
        std::size_t end = 0;
        if (!json_find_array_span(text, "Triangles", begin, end) || expected_triangles <= 0 || expected_triangles > 10'000'000)
        {
            return false;
        }
        triangles.assign(static_cast<std::size_t>(expected_triangles), {});
        std::vector<bool> seen(static_cast<std::size_t>(expected_triangles), false);
        json_for_each_object_in_array(text, begin, end, [&](const std::string& tri_text) {
            const int index = json_int_field(tri_text, "Index", -1, -1, 10'000'000);
            if (index < 0 || index >= expected_triangles)
            {
                return;
            }
            MeshFirstProfile::TriangleMeta meta{};
            meta.uv_island = json_int_field(tri_text, "UvIsland", -1, -1, 10'000'000);
            meta.dominant_bone = json_int_field(tri_text, "DominantBone", -1, -1, 4096);
            meta.body_region = json_string_field(tri_text, "BodyRegion", "unknown");
            meta.local_normal.X = json_number_field(tri_text, "LocalNormalX", 0.0);
            meta.local_normal.Y = json_number_field(tri_text, "LocalNormalY", 0.0);
            meta.local_normal.Z = json_number_field(tri_text, "LocalNormalZ", 0.0);
            meta.uv_area = json_number_field(tri_text, "UvArea", 0.0);
            triangles[static_cast<std::size_t>(index)] = meta;
            seen[static_cast<std::size_t>(index)] = true;
        });
        return std::all_of(seen.begin(), seen.end(), [](bool value) { return value; });
    }

    auto parse_mesh_first_profile_text(const std::string& text, const std::string& source_label) -> MeshFirstProfile
    {
        MeshFirstProfile profile{};
        profile.schema_version = json_int_field(text, "ProfileSchemaVersion", json_int_field(text, "SchemaVersion", 0, 0, 100), 0, 100);
        profile.profile_id = json_string_field(text, "ProfileId", source_label);
        profile.profile_hash = json_string_field(text, "ProfileHash", "");
        profile.source_path = json_string_field(text, "SourcePath", "");
        profile.export_name = json_string_field(text, "Export", "");
        profile.texture_size = json_int_field(text, "TextureSize", 1024, 1, 65536);
        profile.vertex_count = json_int_field(text, "VertexCount", 0, 0, 10'000'000);
        profile.index_count = json_int_field(text, "IndexCount", 0, 0, 30'000'000);
        profile.bone_count = count_occurrences(text, "\"ParentIndex\"");
        profile.expected_export = json_key_window_contains(text, "Export", "paintman") ||
                                  contains_text(lower_copy(profile.profile_id + " " + profile.source_path + " " + profile.export_name), "paintman");
        profile.has_vertices = text.find("\"Vertices\"") != std::string::npos;
        profile.has_indices = text.find("\"Indices\"") != std::string::npos;

        if (profile.schema_version != 2)
        {
            profile.stage = "mesh_profile_schema_mismatch";
            profile.message = "mesh profile must use ProfileSchemaVersion 2";
            return profile;
        }
        if (!profile.expected_export)
        {
            profile.stage = "mesh_profile_identity_mismatch";
            profile.message = "mesh profile export is not paintman";
            return profile;
        }
        if (profile.vertex_count != 1660 || profile.index_count != 8352 || profile.bone_count != 28)
        {
            profile.stage = "mesh_profile_shape_mismatch";
            profile.message = "mesh profile shape does not match expected paintman LOD0";
            return profile;
        }
        if (!profile.has_vertices || !profile.has_indices)
        {
            profile.stage = "mesh_profile_invalid";
            profile.message = "mesh profile is missing vertices or indices";
            return profile;
        }
        if (!mesh_first_parse_indices(text, profile.indices) ||
            !mesh_first_parse_vertices(text, profile.vertices) ||
            !mesh_first_parse_bones(text, profile.bone_count, profile.bones) ||
            !mesh_first_parse_triangle_metadata(text, profile.index_count / 3, profile.triangles))
        {
            profile.stage = "mesh_profile_parse_failed";
            profile.message = "mesh profile parser could not read indices, vertices, bones, or V2 triangle metadata";
            return profile;
        }
        if (static_cast<int>(profile.vertices.size()) != profile.vertex_count ||
            static_cast<int>(profile.indices.size()) != profile.index_count ||
            static_cast<int>(profile.bones.size()) != profile.bone_count ||
            static_cast<int>(profile.triangles.size()) != profile.index_count / 3 ||
            profile.index_count % 3 != 0)
        {
            profile.stage = "mesh_profile_data_mismatch";
            profile.message = "mesh profile parsed data does not match exported counts";
            return profile;
        }

        profile.ok = true;
        profile.stage = "mesh_profile_loaded";
        profile.message = "mesh profile loaded";
        return profile;
    }

    auto load_mesh_first_profile_catalog() -> std::vector<MeshFirstProfile>
    {
        std::vector<MeshFirstProfile> profiles{};
        const auto base_dir = bridge_directory_path();
        if (!base_dir.empty())
        {
            const auto pattern = base_dir + L"\\mesh-profiles\\*.json";
            WIN32_FIND_DATAW find_data{};
            HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
            if (find != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    {
                        continue;
                    }
                    const auto path = base_dir + L"\\mesh-profiles\\" + find_data.cFileName;
                    std::string text{};
                    if (read_text_file_w(path, text))
                    {
                        profiles.push_back(parse_mesh_first_profile_text(text, "mesh-profiles"));
                    }
                } while (FindNextFileW(find, &find_data));
                FindClose(find);
            }
        }

        std::string sidecar_text{};
        if (read_bridge_sidecar_text(L".mesh-profile.json", sidecar_text))
        {
            profiles.push_back(parse_mesh_first_profile_text(sidecar_text, "legacy-sidecar"));
        }
        return profiles;
    }

    auto mesh_first_normalize_asset_identity(std::string text) -> std::string
    {
        text = lower_copy(std::move(text));
        std::replace(text.begin(), text.end(), '\\', '/');
        const std::string content = "content/";
        if (const auto pos = text.find(content); pos != std::string::npos)
        {
            text = "/game/" + text.substr(pos + content.size());
        }
        const std::string suffix = ".uasset";
        if (text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
            text.resize(text.size() - suffix.size());
        }
        return text;
    }

    auto mesh_first_strip_object_suffix(std::string text) -> std::string
    {
        const auto slash = text.find_last_of('/');
        const auto dot = text.find_last_of('.');
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash))
        {
            text.resize(dot);
        }
        return text;
    }

    auto mesh_first_profile_candidate_score(Reflection& ref,
                                            const MeshFirstProfile& profile,
                                            const SdkFrontMeshCandidate& candidate) -> int
    {
        if (!profile.ok)
        {
            return 0;
        }
        const auto profile_path = mesh_first_normalize_asset_identity(profile.source_path);
        const auto export_name = lower_copy(profile.export_name);
        const auto candidate_asset_name = lower_copy(ref.object_name(candidate.asset));
        const auto candidate_asset_path = mesh_first_normalize_asset_identity(ref.object_path(candidate.asset));
        const auto candidate_asset_package = mesh_first_strip_object_suffix(candidate_asset_path);
        int score = 0;
        if (!profile_path.empty() &&
            (candidate_asset_package == profile_path || candidate_asset_path == profile_path))
        {
            score += 1000;
        }
        if (!export_name.empty() && candidate_asset_name == export_name)
        {
            score += 100;
        }
        return score;
    }

    auto select_mesh_first_profile_for_candidate(Reflection& ref,
                                                 const std::vector<MeshFirstProfile>& profiles,
                                                 const SdkFrontMeshCandidate& candidate,
                                                 MeshFirstProfile& out,
                                                 std::string& failure) -> bool
    {
        int invalid_count = 0;
        int best_score = 0;
        for (const auto& profile : profiles)
        {
            if (!profile.ok)
            {
                ++invalid_count;
                continue;
            }
            const int score = mesh_first_profile_candidate_score(ref, profile, candidate);
            if (score > best_score)
            {
                out = profile;
                best_score = score;
            }
        }
        if (best_score > 0)
        {
            failure.clear();
            return true;
        }
        if (profiles.empty())
        {
            failure = "mesh profile catalog is empty; run scripts\\research\\prepare-mesh-profile.ps1";
        }
        else
        {
            failure = "no Mesh Profile V2 matched the live mesh identity";
            if (invalid_count > 0)
            {
                failure += " (" + std::to_string(invalid_count) + " invalid profile(s) ignored)";
            }
        }
        return false;
    }

    auto mesh_first_profile_metadata(const MeshFirstProfile& profile) -> std::string
    {
        return "\"mesh_profile_stage\":\"" + json_escape(profile.stage) + "\"" +
               ",\"mesh_profile_ok\":" + json_bool(profile.ok) +
               ",\"profile_id\":\"" + json_escape(profile.profile_id) + "\"" +
               ",\"profile_hash\":\"" + json_escape(profile.profile_hash) + "\"" +
               ",\"mesh_profile_schema_version\":" + std::to_string(profile.schema_version) +
               ",\"mesh_profile_source_path\":\"" + json_escape(profile.source_path) + "\"" +
               ",\"mesh_profile_export\":\"" + json_escape(profile.export_name) + "\"" +
               ",\"mesh_profile_lod\":0" +
               ",\"texture_size\":" + std::to_string(profile.texture_size) +
               ",\"mesh_profile_vertex_count\":" + std::to_string(profile.vertex_count) +
               ",\"mesh_profile_index_count\":" + std::to_string(profile.index_count) +
               ",\"mesh_profile_bone_count\":" + std::to_string(profile.bone_count) +
               ",\"mesh_profile_expected_export\":" + json_bool(profile.expected_export) +
               ",\"mesh_profile_has_vertices\":" + json_bool(profile.has_vertices) +
               ",\"mesh_profile_has_indices\":" + json_bool(profile.has_indices) +
               ",\"mesh_profile_has_triangle_metadata\":" + json_bool(!profile.triangles.empty());
    }

    auto mesh_first_identity_transform() -> sdk::FTransform
    {
        sdk::FTransform transform{};
        transform.Rotation = {0.0, 0.0, 0.0, 1.0};
        transform.Translation = {};
        transform.Scale3D = {1.0, 1.0, 1.0};
        return transform;
    }

    auto mesh_first_quat_normalize(const sdk::FQuat& quat) -> sdk::FQuat
    {
        const double len = std::sqrt(quat.X * quat.X + quat.Y * quat.Y + quat.Z * quat.Z + quat.W * quat.W);
        if (!std::isfinite(len) || len <= 0.000001)
        {
            return {0.0, 0.0, 0.0, 1.0};
        }
        return {quat.X / len, quat.Y / len, quat.Z / len, quat.W / len};
    }

    auto mesh_first_quat_conjugate(const sdk::FQuat& quat) -> sdk::FQuat
    {
        return {-quat.X, -quat.Y, -quat.Z, quat.W};
    }

    auto mesh_first_quat_mul(const sdk::FQuat& a, const sdk::FQuat& b) -> sdk::FQuat
    {
        return {
            a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
            a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
            a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
            a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z,
        };
    }

    auto mesh_first_quat_rotate(const sdk::FQuat& quat, const sdk::FVector& value) -> sdk::FVector
    {
        const auto q = mesh_first_quat_normalize(quat);
        const sdk::FVector u{q.X, q.Y, q.Z};
        const double s = q.W;
        const double uu = sdk_vec_dot(u, u);
        const double uv = sdk_vec_dot(u, value);
        const auto term0 = sdk_vec_mul(u, 2.0 * uv);
        const auto term1 = sdk_vec_mul(value, s * s - uu);
        const auto term2 = sdk_vec_mul(sdk_vec_cross(u, value), 2.0 * s);
        return sdk_vec_add(sdk_vec_add(term0, term1), term2);
    }

    auto mesh_first_component_scale(const sdk::FVector& a, const sdk::FVector& b) -> sdk::FVector
    {
        return {a.X * b.X, a.Y * b.Y, a.Z * b.Z};
    }

    auto mesh_first_transform_apply_point(const sdk::FTransform& transform, const sdk::FVector& point) -> sdk::FVector
    {
        const auto scaled = mesh_first_component_scale(point, transform.Scale3D);
        return sdk_vec_add(mesh_first_quat_rotate(transform.Rotation, scaled), transform.Translation);
    }

    auto mesh_first_transform_inverse_apply_point(const sdk::FTransform& transform, const sdk::FVector& point) -> sdk::FVector
    {
        const auto translated = sdk_vec_sub(point, transform.Translation);
        const auto unrotated = mesh_first_quat_rotate(mesh_first_quat_conjugate(mesh_first_quat_normalize(transform.Rotation)), translated);
        sdk::FVector out{};
        out.X = std::abs(transform.Scale3D.X) <= 0.000001 ? unrotated.X : unrotated.X / transform.Scale3D.X;
        out.Y = std::abs(transform.Scale3D.Y) <= 0.000001 ? unrotated.Y : unrotated.Y / transform.Scale3D.Y;
        out.Z = std::abs(transform.Scale3D.Z) <= 0.000001 ? unrotated.Z : unrotated.Z / transform.Scale3D.Z;
        return out;
    }

    auto mesh_first_transform_compose(const sdk::FTransform& parent, const sdk::FTransform& child) -> sdk::FTransform
    {
        sdk::FTransform out = mesh_first_identity_transform();
        out.Rotation = mesh_first_quat_normalize(mesh_first_quat_mul(mesh_first_quat_normalize(parent.Rotation),
                                                                     mesh_first_quat_normalize(child.Rotation)));
        out.Translation = mesh_first_transform_apply_point(parent, child.Translation);
        out.Scale3D = mesh_first_component_scale(parent.Scale3D, child.Scale3D);
        return out;
    }

    auto mesh_first_build_reference_component_transforms(const MeshFirstProfile& profile,
                                                         std::vector<sdk::FTransform>& out,
                                                         std::string& failure) -> bool
    {
        if (profile.bone_count <= 0 || static_cast<int>(profile.bones.size()) != profile.bone_count)
        {
            failure = "profile_bones_unavailable";
            return false;
        }
        out.assign(static_cast<std::size_t>(profile.bone_count), mesh_first_identity_transform());
        std::vector<bool> ready(static_cast<std::size_t>(profile.bone_count), false);
        for (int pass = 0; pass < profile.bone_count; ++pass)
        {
            bool progressed = false;
            for (const auto& bone : profile.bones)
            {
                if (bone.index < 0 || bone.index >= profile.bone_count || ready[static_cast<std::size_t>(bone.index)])
                {
                    continue;
                }
                if (bone.parent_index < 0)
                {
                    out[static_cast<std::size_t>(bone.index)] = bone.local_bind;
                    ready[static_cast<std::size_t>(bone.index)] = true;
                    progressed = true;
                    continue;
                }
                if (bone.parent_index >= profile.bone_count)
                {
                    failure = "profile_bone_parent_invalid";
                    return false;
                }
                if (ready[static_cast<std::size_t>(bone.parent_index)])
                {
                    out[static_cast<std::size_t>(bone.index)] =
                        mesh_first_transform_compose(out[static_cast<std::size_t>(bone.parent_index)], bone.local_bind);
                    ready[static_cast<std::size_t>(bone.index)] = true;
                    progressed = true;
                }
            }
            if (!progressed)
            {
                break;
            }
        }
        if (!std::all_of(ready.begin(), ready.end(), [](bool value) { return value; }))
        {
            failure = "profile_bone_hierarchy_unresolved";
            return false;
        }
        return true;
    }

    auto mesh_first_validate_pose_for_profile(const MeshFirstProfile& profile,
                                              SdkPoseResolveResult& pose) -> bool
    {
        pose.trusted = false;
        pose.validation_score = 0;
        pose.validation_scale_violations = 0;
        pose.validation_hierarchy_violations = 0;
        pose.validation_reference_delta_avg = 0.0;
        pose.validation_reference_delta_max = 0.0;
        pose.validation_failure.clear();
        if (!pose.ok)
        {
            pose.validation_failure = "pose_not_resolved";
            return false;
        }
        if (profile.bone_count <= 0 ||
            static_cast<int>(profile.bones.size()) != profile.bone_count ||
            static_cast<int>(pose.component_space_transforms.size()) < profile.bone_count)
        {
            pose.validation_failure = "pose_profile_count_mismatch";
            return false;
        }

        std::vector<sdk::FTransform> reference_component_transforms{};
        std::string reference_failure{};
        if (!mesh_first_build_reference_component_transforms(profile, reference_component_transforms, reference_failure))
        {
            pose.validation_failure = reference_failure.empty() ? "reference_pose_unavailable" : reference_failure;
            return false;
        }

        double delta_sum = 0.0;
        int delta_count = 0;
        for (int bone_index = 0; bone_index < profile.bone_count; ++bone_index)
        {
            const auto& current = pose.component_space_transforms[static_cast<std::size_t>(bone_index)];
            if (sdk_transform_score(current) <= 0)
            {
                ++pose.validation_scale_violations;
                continue;
            }
            const double scale_sum = std::abs(current.Scale3D.X) + std::abs(current.Scale3D.Y) + std::abs(current.Scale3D.Z);
            if (!std::isfinite(scale_sum) || scale_sum < 0.01 || scale_sum > 30.0)
            {
                ++pose.validation_scale_violations;
            }
            const double delta = sdk_vec_len(sdk_vec_sub(current.Translation,
                                                         reference_component_transforms[static_cast<std::size_t>(bone_index)].Translation));
            if (std::isfinite(delta))
            {
                delta_sum += delta;
                pose.validation_reference_delta_max = std::max(pose.validation_reference_delta_max, delta);
                ++delta_count;
            }
        }
        if (delta_count > 0)
        {
            pose.validation_reference_delta_avg = delta_sum / static_cast<double>(delta_count);
        }

        for (const auto& bone : profile.bones)
        {
            if (bone.index < 0 || bone.index >= profile.bone_count || bone.parent_index < 0)
            {
                continue;
            }
            if (bone.parent_index >= profile.bone_count)
            {
                ++pose.validation_hierarchy_violations;
                continue;
            }
            const auto& ref_child = reference_component_transforms[static_cast<std::size_t>(bone.index)];
            const auto& ref_parent = reference_component_transforms[static_cast<std::size_t>(bone.parent_index)];
            const auto& cur_child = pose.component_space_transforms[static_cast<std::size_t>(bone.index)];
            const auto& cur_parent = pose.component_space_transforms[static_cast<std::size_t>(bone.parent_index)];
            const double bind_len = sdk_vec_len(sdk_vec_sub(ref_child.Translation, ref_parent.Translation));
            const double pose_len = sdk_vec_len(sdk_vec_sub(cur_child.Translation, cur_parent.Translation));
            const double max_len = std::max(12.0, bind_len * 3.5 + 18.0);
            if (!std::isfinite(bind_len) || !std::isfinite(pose_len) || pose_len > max_len)
            {
                ++pose.validation_hierarchy_violations;
            }
        }

        const auto lower_source = lower_copy(pose.source);
        const bool named_pose_source =
            contains_text(lower_source, "component") ||
            contains_text(lower_source, "spacebase") ||
            contains_text(lower_source, "space") ||
            contains_text(lower_source, "bone") ||
            contains_text(lower_source, "cached");
        const bool generic_scan_source = lower_source.rfind("guarded_component_array_scan", 0) == 0;
        const int allowed_hierarchy_violations = std::max(1, profile.bone_count / 10);
        pose.validation_score =
            pose.valid_transform_count * 10 -
            pose.validation_scale_violations * 100 -
            pose.validation_hierarchy_violations * 50 +
            (named_pose_source ? 200 : 0) -
            (generic_scan_source ? 50 : 0);

        if (pose.valid_transform_count != profile.bone_count)
        {
            pose.validation_failure = "pose_valid_transform_count_mismatch";
            return false;
        }
        if (pose.validation_scale_violations > 0)
        {
            pose.validation_failure = "pose_scale_or_transform_invalid";
            return false;
        }
        if (pose.validation_hierarchy_violations > allowed_hierarchy_violations)
        {
            pose.validation_failure = "pose_hierarchy_distance_invalid";
            return false;
        }
        if (!std::isfinite(pose.validation_reference_delta_avg) ||
            !std::isfinite(pose.validation_reference_delta_max) ||
            pose.validation_reference_delta_avg > 250.0 ||
            pose.validation_reference_delta_max > 600.0)
        {
            pose.validation_failure = "pose_reference_delta_invalid";
            return false;
        }

        pose.trusted = true;
        pose.validation_failure = "ok";
        return true;
    }

    auto mesh_first_resolve_component_to_world(Reflection& ref,
                                               std::uintptr_t mesh,
                                               const sdk::FVector& expected_location,
                                               sdk::FTransform& out,
                                               std::string& source) -> bool
    {
        std::string failure_details{};
        const char* candidates[]{"K2_GetComponentToWorld", "GetComponentTransform"};
        for (const auto* function_name : candidates)
        {
            sdk::FTransform transform{};
            if (sdk_call_no_params_return_transform(ref, mesh, function_name, transform) &&
                sdk_transform_score(transform) > 0)
            {
                out = transform;
                source = std::string("function:") + function_name;
                return true;
            }
            if (!failure_details.empty())
            {
                failure_details += ";";
            }
            failure_details += std::string(function_name) + "=failed";
        }
        const int component_to_world_offset = ref.resolve_property_offset("SceneComponent", "ComponentToWorld");
        if (component_to_world_offset >= 0)
        {
            sdk::FTransform transform{};
            if (safe_copy(&transform,
                          reinterpret_cast<const void*>(mesh + static_cast<std::uintptr_t>(component_to_world_offset)),
                          sizeof(transform)) &&
                sdk_transform_score(transform) > 0)
            {
                out = transform;
                source = "property:SceneComponent.ComponentToWorld@" + hex_address(static_cast<std::uintptr_t>(component_to_world_offset));
                return true;
            }
            failure_details += ";property_offset=" + hex_address(static_cast<std::uintptr_t>(component_to_world_offset)) + ":invalid";
        }
        else
        {
            failure_details += ";property_offset=unavailable";
        }

        sdk::FTransform best_transform{};
        int best_score = -1000000;
        int best_offset = -1;
        for (int offset = 0; mesh && offset + static_cast<int>(sizeof(sdk::FTransform)) <= 0x1200; offset += 8)
        {
            sdk::FTransform transform{};
            if (!safe_copy(&transform,
                           reinterpret_cast<const void*>(mesh + static_cast<std::uintptr_t>(offset)),
                           sizeof(transform)))
            {
                continue;
            }
            int score = sdk_transform_score(transform);
            if (score <= 0)
            {
                continue;
            }
            const auto delta = sdk_vec_sub(transform.Translation, expected_location);
            const double xy_distance = std::sqrt(delta.X * delta.X + delta.Y * delta.Y);
            const double z_distance = std::abs(delta.Z);
            const double scale_sum = std::abs(transform.Scale3D.X) + std::abs(transform.Scale3D.Y) + std::abs(transform.Scale3D.Z);
            if (!std::isfinite(xy_distance) || !std::isfinite(z_distance) || scale_sum <= 0.001 || scale_sum > 1000.0)
            {
                continue;
            }
            if (xy_distance <= 50.0)
            {
                score += 400;
            }
            else if (xy_distance <= 250.0)
            {
                score += 250;
            }
            else if (xy_distance <= 1000.0)
            {
                score += 50;
            }
            else
            {
                score -= 500;
            }
            if (z_distance <= 100.0)
            {
                score += 150;
            }
            else if (z_distance <= 500.0)
            {
                score += 25;
            }
            else
            {
                score -= 250;
            }
            if (score > best_score)
            {
                best_score = score;
                best_offset = offset;
                best_transform = transform;
            }
        }
        if (best_offset >= 0 && best_score >= 150)
        {
            out = best_transform;
            source = "scan:component_ftransform@" + hex_address(static_cast<std::uintptr_t>(best_offset));
            return true;
        }
        source = "unavailable:" + failure_details + ";scan_best_score=" + std::to_string(best_score) +
                 ";scan_best_offset=" + (best_offset >= 0 ? hex_address(static_cast<std::uintptr_t>(best_offset)) : std::string("none"));
        return false;
    }

    auto mesh_first_skin_vertices(const MeshFirstProfile& profile,
                                  const SdkPoseResolveResult& pose,
                                  const sdk::FTransform& component_to_world,
                                  std::vector<sdk::FVector>& component_positions,
                                  std::vector<sdk::FVector>& world_positions,
                                  std::string& failure) -> bool
    {
        if (static_cast<int>(pose.component_space_transforms.size()) < profile.bone_count)
        {
            failure = "pose_transform_count_mismatch";
            return false;
        }
        std::vector<sdk::FTransform> reference_component_transforms{};
        if (!mesh_first_build_reference_component_transforms(profile, reference_component_transforms, failure))
        {
            return false;
        }
        component_positions.assign(profile.vertices.size(), {});
        world_positions.assign(profile.vertices.size(), {});
        for (std::size_t i = 0; i < profile.vertices.size(); ++i)
        {
            const auto& vertex = profile.vertices[i];
            sdk::FVector skinned{};
            double weight_sum = 0.0;
            for (const auto& influence : vertex.influences)
            {
                if (influence.bone < 0 || influence.bone >= profile.bone_count)
                {
                    failure = "vertex_skin_weight_bone_invalid";
                    return false;
                }
                const auto& ref_bone = reference_component_transforms[static_cast<std::size_t>(influence.bone)];
                const auto& current_bone = pose.component_space_transforms[static_cast<std::size_t>(influence.bone)];
                const auto bone_local = mesh_first_transform_inverse_apply_point(ref_bone, vertex.position);
                const auto current_component = mesh_first_transform_apply_point(current_bone, bone_local);
                skinned = sdk_vec_add(skinned, sdk_vec_mul(current_component, influence.weight));
                weight_sum += influence.weight;
            }
            if (weight_sum > 0.000001)
            {
                skinned = sdk_vec_mul(skinned, 1.0 / weight_sum);
            }
            else
            {
                skinned = vertex.position;
            }
            if (!std::isfinite(skinned.X) || !std::isfinite(skinned.Y) || !std::isfinite(skinned.Z))
            {
                failure = "skinned_vertex_invalid";
                return false;
            }
            component_positions[i] = skinned;
            world_positions[i] = mesh_first_transform_apply_point(component_to_world, skinned);
        }
        return true;
    }

    auto mesh_first_pose_displacement_metadata(const MeshFirstProfile& profile,
                                               const std::vector<sdk::FVector>& component_positions) -> std::string
    {
        if (component_positions.size() != profile.vertices.size() || profile.vertices.empty())
        {
            return "\"pose_skinned_delta_available\":false";
        }
        double sum = 0.0;
        double max_delta = 0.0;
        int over_one = 0;
        int over_ten = 0;
        for (std::size_t i = 0; i < profile.vertices.size(); ++i)
        {
            const auto delta = sdk_vec_len(sdk_vec_sub(component_positions[i], profile.vertices[i].position));
            if (!std::isfinite(delta))
            {
                continue;
            }
            sum += delta;
            max_delta = std::max(max_delta, delta);
            if (delta > 1.0)
            {
                ++over_one;
            }
            if (delta > 10.0)
            {
                ++over_ten;
            }
        }
        const double avg = sum / static_cast<double>(std::max<std::size_t>(1, profile.vertices.size()));
        return "\"pose_skinned_delta_available\":true"
               ",\"pose_skinned_delta_avg\":" + std::to_string(avg) +
               ",\"pose_skinned_delta_max\":" + std::to_string(max_delta) +
               ",\"pose_skinned_delta_over_1cm\":" + std::to_string(over_one) +
               ",\"pose_skinned_delta_over_10cm\":" + std::to_string(over_ten);
    }

    struct MeshFirstChannelChecksum
    {
        bool ok{false};
        int bytes{0};
        std::uint64_t hash{1469598103934665603ULL};
        std::string failure{"not_run"};
    };

    auto mesh_first_export_channel_checksum(Reflection& ref,
                                            std::uintptr_t component,
                                            sdk::EPaintChannel channel) -> MeshFirstChannelChecksum
    {
        MeshFirstChannelChecksum out{};
        if (!live_uobject(component))
        {
            out.failure = "paint_component_unavailable";
            return out;
        }
        const auto function = ref.find_function(component, "ExportChannelToBytes");
        if (!function)
        {
            out.failure = "ExportChannelToBytes_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 256)
        {
            out.failure = "ExportChannelToBytes_params_size_invalid";
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            const auto offset = prop_offset(prop);
            if (offset < 0 || offset >= params_size)
            {
                continue;
            }
            if (name == "channel")
            {
                params[static_cast<std::size_t>(offset)] = static_cast<std::uint8_t>(channel);
            }
        }
        std::string failure{};
        if (!process_event(component, function, params.data(), failure))
        {
            out.failure = "ExportChannelToBytes_failed:" + failure;
            return out;
        }

        bool return_value = true;
        sdk::TArray<std::uint8_t> bytes{};
        bool found_array = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            const auto offset = prop_offset(prop);
            if (offset < 0 || offset >= params_size)
            {
                continue;
            }
            if (name == "returnvalue")
            {
                return_value = params[static_cast<std::size_t>(offset)] != 0;
            }
            else if (name == "outdata" && offset + static_cast<int>(sizeof(sdk::TArray<std::uint8_t>)) <= params_size)
            {
                bytes = *reinterpret_cast<sdk::TArray<std::uint8_t>*>(params.data() + offset);
                found_array = true;
            }
        }
        if (!return_value)
        {
            out.failure = "ExportChannelToBytes_return_false";
            return out;
        }
        if (!found_array || !bytes.Data || bytes.Num <= 0 || bytes.Max < bytes.Num || bytes.Num > 64 * 1024 * 1024)
        {
            out.failure = "ExportChannelToBytes_no_bytes";
            return out;
        }
        std::uint64_t hash = 1469598103934665603ULL;
        for (int i = 0; i < bytes.Num; ++i)
        {
            const auto value = safe_read<std::uint8_t>(reinterpret_cast<std::uintptr_t>(bytes.Data) + static_cast<std::uintptr_t>(i), 0);
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ULL;
        }
        out.ok = true;
        out.failure.clear();
        out.bytes = bytes.Num;
        out.hash = hash;
        return out;
    }

    enum class MeshFirstRegion
    {
        Front,
        Side,
        Back,
    };

    auto mesh_first_region_code(MeshFirstRegion region) -> int
    {
        if (region == MeshFirstRegion::Side)
            return 1;
        if (region == MeshFirstRegion::Back)
            return 2;
        return 0;
    }

    auto mesh_first_axis_component(const sdk::FVector& value, char axis) -> double
    {
        if (axis == 'y' || axis == 'Y')
            return value.Y;
        if (axis == 'z' || axis == 'Z')
            return value.Z;
        return value.X;
    }

    auto mesh_first_region_axis(const MeshFirstProfile& profile) -> char
    {
        if (profile.vertices.empty())
            return 'x';
        double min_x = profile.vertices.front().position.X;
        double max_x = min_x;
        double min_y = profile.vertices.front().position.Y;
        double max_y = min_y;
        for (const auto& vertex : profile.vertices)
        {
            min_x = std::min(min_x, vertex.position.X);
            max_x = std::max(max_x, vertex.position.X);
            min_y = std::min(min_y, vertex.position.Y);
            max_y = std::max(max_y, vertex.position.Y);
        }
        const double range_x = max_x - min_x;
        const double range_y = max_y - min_y;
        if (std::isfinite(range_x) && std::isfinite(range_y) && range_y > 0.001 && range_y < range_x)
            return 'y';
        return 'x';
    }

    auto mesh_first_region_axis_label(char axis) -> const char*
    {
        if (axis == 'y' || axis == 'Y')
            return "local_y";
        if (axis == 'z' || axis == 'Z')
            return "local_z";
        return "local_x";
    }

    struct MeshFirstPlanSample
    {
        int triangle_index{0};
        MeshFirstRegion region{MeshFirstRegion::Front};
        double u{0.5};
        double v{0.5};
        double barycentric_a{1.0 / 3.0};
        double barycentric_b{1.0 / 3.0};
        double barycentric_c{1.0 / 3.0};
        sdk::FVector local_position{};
        sdk::FVector world_position{};
        double facing_dot{0.0};
        double uv_area{0.0};
        int uv_island{-1};
        int dominant_bone{-1};
        std::string body_region{"unknown"};
        double r{1.0};
        double g{1.0};
        double b{1.0};
        double roughness{0.65};
        double metallic{0.0};
        double source_distance_uv{0.0};
        double source_distance_component{0.0};
        bool source_candidate{false};
        bool unsafe{false};
    };

    struct MeshFirstRuntimeTriangle
    {
        sdk::FVector world[3]{};
        sdk::FVector local[3]{};
        sdk::FVector2D uv[3]{};
    };

    struct MeshFirstRuntimeTriangleCache
    {
        bool ok{false};
        std::uintptr_t data{0};
        int owner_offset{-1};
        int stride{0};
        int triangle_count{0};
        double profile_uv_avg_error{0.0};
        std::string failure{"not_run"};
        std::vector<MeshFirstRuntimeTriangle> triangles{};
    };

    struct MeshFirstRuntimeTriangleCoordinateSelection
    {
        bool swapped{false};
        double direct_avg_error{0.0};
        double swapped_avg_error{0.0};
        double selected_avg_error{0.0};
        int samples{0};
        std::string mode{"world_local"};
    };

    struct MeshFirstRuntimeTriangleProjectionSelection
    {
        std::string mode{"field0_world_field1_local"};
        int samples{0};
        int source_candidates{0};
        int project_ok{0};
        int inside_view{0};
        int best_score{0};
        std::string summary{};
    };

    struct MeshFirstPlanStats
    {
        int total_triangles{0};
        int invalid_triangles{0};
        int degenerate_triangles{0};
        int front_triangles{0};
        int side_triangles{0};
        int back_triangles{0};
        int total_samples{0};
        int source_samples{0};
        int front_samples{0};
        int side_samples{0};
        int back_samples{0};
        int enabled_samples{0};
        int unsafe_candidates{0};
        int unsafe_front{0};
        int unsafe_side{0};
        int unsafe_back{0};
        int unsafe_enabled{0};
        int unsafe_projection_color{0};
        int source_depth_rejected{0};
        int source_facing_rejected{0};
        int source_direct_assignments{0};
        int source_projection_assignments{0};
        double source_distance_avg_uv{0.0};
        double source_distance_p95_uv{0.0};
        double source_distance_max_uv{0.0};
        double source_distance_avg_component{0.0};
        double source_distance_p95_component{0.0};
        double source_distance_max_component{0.0};
    };

    auto mesh_first_sample_region_count(MeshFirstPlanStats& stats, MeshFirstRegion region) -> int&
    {
        if (region == MeshFirstRegion::Front)
        {
            return stats.front_samples;
        }
        if (region == MeshFirstRegion::Side)
        {
            return stats.side_samples;
        }
        return stats.back_samples;
    }

    auto mesh_first_triangle_region_count(MeshFirstPlanStats& stats, MeshFirstRegion region) -> int&
    {
        if (region == MeshFirstRegion::Front)
        {
            return stats.front_triangles;
        }
        if (region == MeshFirstRegion::Side)
        {
            return stats.side_triangles;
        }
        return stats.back_triangles;
    }

    auto mesh_first_uv_triangle_area(double u0, double v0, double u1, double v1, double u2, double v2) -> double
    {
        return std::abs((u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0)) * 0.5;
    }

    auto mesh_first_barycentric_uv(double u0,
                                   double v0,
                                   double u1,
                                   double v1,
                                   double u2,
                                   double v2,
                                   double u,
                                   double v,
                                   double& a,
                                   double& b,
                                   double& c) -> bool
    {
        const double denom = ((v1 - v2) * (u0 - u2)) + ((u2 - u1) * (v0 - v2));
        if (!std::isfinite(denom) || std::abs(denom) <= 1.0e-12)
        {
            return false;
        }
        a = (((v1 - v2) * (u - u2)) + ((u2 - u1) * (v - v2))) / denom;
        b = (((v2 - v0) * (u - u2)) + ((u0 - u2) * (v - v2))) / denom;
        c = 1.0 - a - b;
        constexpr double kEpsilon = -1.0e-5;
        return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) &&
               a >= kEpsilon && b >= kEpsilon && c >= kEpsilon;
    }

    auto mesh_first_finite_vector(const sdk::FVector& value) -> bool
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    auto mesh_first_finite_uv(const sdk::FVector2D& value) -> bool
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) &&
               value.X >= -0.05 && value.X <= 1.05 &&
               value.Y >= -0.05 && value.Y <= 1.05;
    }

    auto mesh_first_region_axis_from_runtime_triangles(const std::vector<MeshFirstRuntimeTriangle>& triangles) -> char
    {
        bool initialized = false;
        double min_x = 0.0;
        double max_x = 0.0;
        double min_y = 0.0;
        double max_y = 0.0;
        for (const auto& triangle : triangles)
        {
            for (const auto& local : triangle.local)
            {
                if (!mesh_first_finite_vector(local))
                {
                    continue;
                }
                if (!initialized)
                {
                    min_x = max_x = local.X;
                    min_y = max_y = local.Y;
                    initialized = true;
                    continue;
                }
                min_x = std::min(min_x, local.X);
                max_x = std::max(max_x, local.X);
                min_y = std::min(min_y, local.Y);
                max_y = std::max(max_y, local.Y);
            }
        }
        if (!initialized)
        {
            return 'x';
        }
        const double range_x = max_x - min_x;
        const double range_y = max_y - min_y;
        if (std::isfinite(range_x) && std::isfinite(range_y) && range_y > 0.001 && range_y < range_x)
        {
            return 'y';
        }
        return 'x';
    }

    auto mesh_first_read_runtime_triangle(std::uintptr_t base, MeshFirstRuntimeTriangle& out) -> bool
    {
        for (int i = 0; i < 3; ++i)
        {
            const auto world_base = base + static_cast<std::uintptr_t>(i * 24);
            out.world[i].X = safe_read<double>(world_base + 0, std::numeric_limits<double>::quiet_NaN());
            out.world[i].Y = safe_read<double>(world_base + 8, std::numeric_limits<double>::quiet_NaN());
            out.world[i].Z = safe_read<double>(world_base + 16, std::numeric_limits<double>::quiet_NaN());

            const auto local_base = base + 72 + static_cast<std::uintptr_t>(i * 24);
            out.local[i].X = safe_read<double>(local_base + 0, std::numeric_limits<double>::quiet_NaN());
            out.local[i].Y = safe_read<double>(local_base + 8, std::numeric_limits<double>::quiet_NaN());
            out.local[i].Z = safe_read<double>(local_base + 16, std::numeric_limits<double>::quiet_NaN());

            const auto uv_base = base + 144 + static_cast<std::uintptr_t>(i * 16);
            out.uv[i].X = safe_read<double>(uv_base + 0, std::numeric_limits<double>::quiet_NaN());
            out.uv[i].Y = safe_read<double>(uv_base + 8, std::numeric_limits<double>::quiet_NaN());
            if (!mesh_first_finite_vector(out.world[i]) ||
                !mesh_first_finite_vector(out.local[i]) ||
                !mesh_first_finite_uv(out.uv[i]))
            {
                return false;
            }
        }
        const auto edge0 = sdk_vec_sub(out.world[1], out.world[0]);
        const auto edge1 = sdk_vec_sub(out.world[2], out.world[0]);
        return sdk_vec_len(sdk_vec_cross(edge0, edge1)) > 0.000001;
    }

    auto mesh_first_runtime_triangle_profile_uv_error(const MeshFirstProfile& profile,
                                                      int triangle_index,
                                                      const MeshFirstRuntimeTriangle& triangle) -> double
    {
        const std::size_t tri = static_cast<std::size_t>(triangle_index) * 3;
        if (tri + 2 >= profile.indices.size())
        {
            return 1000000.0;
        }
        double sum = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            const int vertex_index = profile.indices[tri + static_cast<std::size_t>(i)];
            if (vertex_index < 0 || vertex_index >= profile.vertex_count)
            {
                return 1000000.0;
            }
            const auto& vertex = profile.vertices[static_cast<std::size_t>(vertex_index)];
            sum += std::abs(vertex.u - triangle.uv[i].X) + std::abs(vertex.v - triangle.uv[i].Y);
        }
        return sum / 6.0;
    }

    auto mesh_first_resolve_runtime_triangle_cache(std::uintptr_t component,
                                                   const MeshFirstProfile& profile) -> MeshFirstRuntimeTriangleCache
    {
        MeshFirstRuntimeTriangleCache out{};
        const int expected_triangles = static_cast<int>(profile.indices.size() / 3);
        if (!component || expected_triangles <= 0)
        {
            out.failure = "runtime_triangle_cache_invalid_profile";
            return out;
        }

        constexpr int kStride = 208;
        double best_error = 1000000.0;
        int best_offset = -1;
        std::uintptr_t best_data = 0;
        std::vector<MeshFirstRuntimeTriangle> best_triangles{};

        for (int offset = 0; offset + 16 <= 0x3000; offset += 8)
        {
            const auto data = safe_read<std::uintptr_t>(component + static_cast<std::uintptr_t>(offset), 0);
            const auto num = safe_read<int>(component + static_cast<std::uintptr_t>(offset + 8), 0);
            const auto max = safe_read<int>(component + static_cast<std::uintptr_t>(offset + 12), 0);
            if (!data || num != expected_triangles || max < num || max > num + std::max(32, num / 2))
            {
                continue;
            }

            std::vector<MeshFirstRuntimeTriangle> triangles{};
            triangles.resize(static_cast<std::size_t>(num));
            double uv_error_sum = 0.0;
            int checked = 0;
            bool valid = true;
            const int check_count = std::min(num, 96);
            for (int i = 0; i < check_count; ++i)
            {
                MeshFirstRuntimeTriangle triangle{};
                if (!mesh_first_read_runtime_triangle(data + static_cast<std::uintptr_t>(i) * kStride, triangle))
                {
                    valid = false;
                    break;
                }
                const double uv_error = mesh_first_runtime_triangle_profile_uv_error(profile, i, triangle);
                if (!std::isfinite(uv_error) || uv_error > 0.02)
                {
                    valid = false;
                    break;
                }
                uv_error_sum += uv_error;
                triangles[static_cast<std::size_t>(i)] = triangle;
                ++checked;
            }
            if (!valid || checked <= 0)
            {
                continue;
            }

            for (int i = checked; i < num; ++i)
            {
                MeshFirstRuntimeTriangle triangle{};
                if (!mesh_first_read_runtime_triangle(data + static_cast<std::uintptr_t>(i) * kStride, triangle))
                {
                    valid = false;
                    break;
                }
                triangles[static_cast<std::size_t>(i)] = triangle;
            }
            if (!valid)
            {
                continue;
            }

            const double avg_error = uv_error_sum / static_cast<double>(checked);
            if (avg_error < best_error)
            {
                best_error = avg_error;
                best_offset = offset;
                best_data = data;
                best_triangles = std::move(triangles);
            }
        }

        if (best_offset < 0 || best_triangles.empty())
        {
            out.failure = "runtime_triangle_cache_unavailable";
            return out;
        }
        out.ok = true;
        out.failure.clear();
        out.owner_offset = best_offset;
        out.data = best_data;
        out.stride = kStride;
        out.triangle_count = static_cast<int>(best_triangles.size());
        out.profile_uv_avg_error = best_error;
        out.triangles = std::move(best_triangles);
        return out;
    }

    auto mesh_first_resolve_runtime_triangle_cache_dynamic(std::uintptr_t component) -> MeshFirstRuntimeTriangleCache
    {
        MeshFirstRuntimeTriangleCache out{};
        if (!component)
        {
            out.failure = "runtime_triangle_cache_invalid_component";
            return out;
        }

        constexpr int kStride = 208;
        int best_score = 0;
        int best_offset = -1;
        std::uintptr_t best_data = 0;
        int best_count = 0;
        std::vector<MeshFirstRuntimeTriangle> best_triangles{};

        for (int offset = 0; offset + 16 <= 0x3000; offset += 8)
        {
            const auto data = safe_read<std::uintptr_t>(component + static_cast<std::uintptr_t>(offset), 0);
            const auto num = safe_read<int>(component + static_cast<std::uintptr_t>(offset + 8), 0);
            const auto max = safe_read<int>(component + static_cast<std::uintptr_t>(offset + 12), 0);
            if (!data || num <= 0 || num > 200000 || max < num || max > num + std::max(1024, num / 2))
            {
                continue;
            }

            const int check_count = std::min(num, 96);
            bool valid = true;
            double uv_area_sum = 0.0;
            double world_area_sum = 0.0;
            for (int i = 0; i < check_count; ++i)
            {
                MeshFirstRuntimeTriangle triangle{};
                if (!mesh_first_read_runtime_triangle(data + static_cast<std::uintptr_t>(i) * kStride, triangle))
                {
                    valid = false;
                    break;
                }
                const double uv_area = mesh_first_uv_triangle_area(triangle.uv[0].X,
                                                                   triangle.uv[0].Y,
                                                                   triangle.uv[1].X,
                                                                   triangle.uv[1].Y,
                                                                   triangle.uv[2].X,
                                                                   triangle.uv[2].Y);
                const auto edge0 = sdk_vec_sub(triangle.world[1], triangle.world[0]);
                const auto edge1 = sdk_vec_sub(triangle.world[2], triangle.world[0]);
                const double world_area = sdk_vec_len(sdk_vec_cross(edge0, edge1)) * 0.5;
                if (!std::isfinite(uv_area) || uv_area <= 0.0 ||
                    !std::isfinite(world_area) || world_area <= 0.000001)
                {
                    valid = false;
                    break;
                }
                uv_area_sum += uv_area;
                world_area_sum += world_area;
            }
            if (!valid)
            {
                continue;
            }

            const int score = check_count * 100000 + std::min(num, 99999);
            if (score <= best_score)
            {
                continue;
            }

            std::vector<MeshFirstRuntimeTriangle> triangles{};
            triangles.resize(static_cast<std::size_t>(num));
            for (int i = 0; i < num; ++i)
            {
                MeshFirstRuntimeTriangle triangle{};
                if (!mesh_first_read_runtime_triangle(data + static_cast<std::uintptr_t>(i) * kStride, triangle))
                {
                    valid = false;
                    break;
                }
                triangles[static_cast<std::size_t>(i)] = triangle;
            }
            if (!valid)
            {
                continue;
            }

            best_score = score + static_cast<int>(std::min(9999.0, uv_area_sum * 1000000.0 + world_area_sum));
            best_offset = offset;
            best_data = data;
            best_count = num;
            best_triangles = std::move(triangles);
        }

        if (best_offset < 0 || best_triangles.empty())
        {
            out.failure = "runtime_triangle_cache_unavailable";
            return out;
        }

        out.ok = true;
        out.failure.clear();
        out.owner_offset = best_offset;
        out.data = best_data;
        out.stride = kStride;
        out.triangle_count = best_count;
        out.profile_uv_avg_error = 0.0;
        out.triangles = std::move(best_triangles);
        return out;
    }

    auto mesh_first_select_runtime_triangle_coordinates(std::vector<MeshFirstRuntimeTriangle>& triangles,
                                                        const sdk::FTransform& component_to_world) -> MeshFirstRuntimeTriangleCoordinateSelection
    {
        MeshFirstRuntimeTriangleCoordinateSelection out{};
        if (triangles.empty())
        {
            return out;
        }
        double direct_sum = 0.0;
        double swapped_sum = 0.0;
        int samples = 0;
        const int step = std::max(1, static_cast<int>(triangles.size() / 256));
        for (std::size_t tri = 0; tri < triangles.size(); tri += static_cast<std::size_t>(step))
        {
            const auto& triangle = triangles[tri];
            for (int vertex = 0; vertex < 3; ++vertex)
            {
                const auto direct_world_from_local = mesh_first_transform_apply_point(component_to_world, triangle.local[vertex]);
                const auto swapped_world_from_local = mesh_first_transform_apply_point(component_to_world, triangle.world[vertex]);
                const double direct_error = sdk_vec_len(sdk_vec_sub(triangle.world[vertex], direct_world_from_local));
                const double swapped_error = sdk_vec_len(sdk_vec_sub(triangle.local[vertex], swapped_world_from_local));
                if (!std::isfinite(direct_error) || !std::isfinite(swapped_error))
                {
                    continue;
                }
                direct_sum += direct_error;
                swapped_sum += swapped_error;
                ++samples;
            }
        }
        out.samples = samples;
        if (samples <= 0)
        {
            return out;
        }
        out.direct_avg_error = direct_sum / static_cast<double>(samples);
        out.swapped_avg_error = swapped_sum / static_cast<double>(samples);
        out.swapped = out.swapped_avg_error + 0.001 < out.direct_avg_error;
        out.selected_avg_error = out.swapped ? out.swapped_avg_error : out.direct_avg_error;
        out.mode = out.swapped ? "local_world_swapped" : "world_local";
        if (out.swapped)
        {
            for (auto& triangle : triangles)
            {
                for (int vertex = 0; vertex < 3; ++vertex)
                {
                    std::swap(triangle.world[vertex], triangle.local[vertex]);
                }
            }
        }
        return out;
    }

    auto mesh_first_runtime_triangle_with_coordinate_mode(const MeshFirstRuntimeTriangle& raw,
                                                          const sdk::FTransform& component_to_world,
                                                          const std::string& mode) -> MeshFirstRuntimeTriangle
    {
        MeshFirstRuntimeTriangle out = raw;
        if (mode == "field1_world_field0_local")
        {
            for (int vertex = 0; vertex < 3; ++vertex)
            {
                out.world[vertex] = raw.local[vertex];
                out.local[vertex] = raw.world[vertex];
            }
        }
        else if (mode == "field0_local_component_world")
        {
            for (int vertex = 0; vertex < 3; ++vertex)
            {
                out.local[vertex] = raw.world[vertex];
                out.world[vertex] = mesh_first_transform_apply_point(component_to_world, raw.world[vertex]);
            }
        }
        else if (mode == "field1_local_component_world")
        {
            for (int vertex = 0; vertex < 3; ++vertex)
            {
                out.local[vertex] = raw.local[vertex];
                out.world[vertex] = mesh_first_transform_apply_point(component_to_world, raw.local[vertex]);
            }
        }
        return out;
    }

    auto mesh_first_select_runtime_triangle_projection_coordinates(Reflection& ref,
                                                                  const SdkContext& ctx,
                                                                  std::vector<MeshFirstRuntimeTriangle>& triangles,
                                                                  const sdk::FTransform& component_to_world,
                                                                  const sdk::FVector& camera_location,
                                                                  const sdk::FVector& camera_direction,
                                                                  const SdkViewportInfo& viewport) -> MeshFirstRuntimeTriangleProjectionSelection
    {
        MeshFirstRuntimeTriangleProjectionSelection out{};
        if (triangles.empty() || viewport.width <= 0 || viewport.height <= 0)
        {
            return out;
        }
        struct Candidate
        {
            std::string mode{};
            int samples{0};
            int source_candidates{0};
            int project_ok{0};
            int inside_view{0};
            int score{0};
        };
        std::vector<Candidate> candidates{
            {"field0_world_field1_local"},
            {"field1_world_field0_local"},
            {"field0_local_component_world"},
            {"field1_local_component_world"},
        };
        const int step = std::max(1, static_cast<int>(triangles.size() / 256));
        const auto view_direction = sdk_vec_normalize(camera_direction);
        for (auto& candidate : candidates)
        {
            for (std::size_t tri = 0; tri < triangles.size(); tri += static_cast<std::size_t>(step))
            {
                const auto triangle = mesh_first_runtime_triangle_with_coordinate_mode(triangles[tri], component_to_world, candidate.mode);
                const auto world_normal = sdk_vec_normalize(sdk_vec_cross(sdk_vec_sub(triangle.world[1], triangle.world[0]),
                                                                          sdk_vec_sub(triangle.world[2], triangle.world[0])));
                if (sdk_vec_len(world_normal) <= 0.000001)
                {
                    continue;
                }
                ++candidate.samples;
                const auto center = sdk_vec_mul(sdk_vec_add(sdk_vec_add(triangle.world[0], triangle.world[1]), triangle.world[2]), 1.0 / 3.0);
                const double depth = sdk_vec_dot(sdk_vec_sub(center, camera_location), view_direction);
                const double facing = sdk_vec_dot(world_normal, view_direction);
                if (!std::isfinite(depth) || depth <= 0.0 || !std::isfinite(facing) || facing >= -0.001)
                {
                    continue;
                }
                ++candidate.source_candidates;
                double x = 0.0;
                double y = 0.0;
                if (!sdk_project_world_to_screen(ref, ctx, center, x, y))
                {
                    continue;
                }
                ++candidate.project_ok;
                if (x >= 0.0 && y >= 0.0 &&
                    x < static_cast<double>(viewport.width) &&
                    y < static_cast<double>(viewport.height))
                {
                    ++candidate.inside_view;
                }
            }
            candidate.score = candidate.inside_view * 100000 + candidate.project_ok * 100 + candidate.source_candidates;
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score)
            {
                return a.score > b.score;
            }
            return a.mode < b.mode;
        });
        const auto& best = candidates.front();
        out.mode = best.mode;
        out.samples = best.samples;
        out.source_candidates = best.source_candidates;
        out.project_ok = best.project_ok;
        out.inside_view = best.inside_view;
        out.best_score = best.score;
        for (const auto& candidate : candidates)
        {
            if (!out.summary.empty())
            {
                out.summary += ";";
            }
            out.summary += candidate.mode + ":samples=" + std::to_string(candidate.samples) +
                           ",source=" + std::to_string(candidate.source_candidates) +
                           ",project=" + std::to_string(candidate.project_ok) +
                           ",inside=" + std::to_string(candidate.inside_view) +
                           ",score=" + std::to_string(candidate.score);
        }
        if (best.mode != "field0_world_field1_local")
        {
            for (auto& triangle : triangles)
            {
                triangle = mesh_first_runtime_triangle_with_coordinate_mode(triangle, component_to_world, best.mode);
            }
        }
        return out;
    }

    auto mesh_first_emit_runtime_triangle_sample(std::vector<MeshFirstPlanSample>& samples,
                                                 const MeshFirstRuntimeTriangle& triangle,
                                                 const MeshFirstProfile::TriangleMeta& meta,
                                                 int triangle_index,
                                                 MeshFirstRegion region,
                                                 bool source_candidate,
                                                 double facing,
                                                 double uv_area,
                                                 double a,
                                                 double b,
                                                 double c) -> void
    {
        MeshFirstPlanSample sample{};
        sample.triangle_index = triangle_index;
        sample.region = region;
        sample.source_candidate = source_candidate;
        sample.facing_dot = facing;
        sample.uv_area = uv_area;
        sample.u = clamp01(triangle.uv[0].X * a + triangle.uv[1].X * b + triangle.uv[2].X * c);
        sample.v = clamp01(triangle.uv[0].Y * a + triangle.uv[1].Y * b + triangle.uv[2].Y * c);
        sample.barycentric_a = a;
        sample.barycentric_b = b;
        sample.barycentric_c = c;
        sample.local_position = sdk_vec_add(sdk_vec_add(sdk_vec_mul(triangle.local[0], a), sdk_vec_mul(triangle.local[1], b)), sdk_vec_mul(triangle.local[2], c));
        sample.world_position = sdk_vec_add(sdk_vec_add(sdk_vec_mul(triangle.world[0], a), sdk_vec_mul(triangle.world[1], b)), sdk_vec_mul(triangle.world[2], c));
        sample.uv_island = meta.uv_island;
        sample.dominant_bone = meta.dominant_bone;
        sample.body_region = meta.body_region;
        samples.push_back(sample);
    }

    auto mesh_first_generate_plan_samples_from_runtime_cache(const MeshFirstProfile* profile,
                                                             const std::vector<MeshFirstRuntimeTriangle>& triangles,
                                                             int texture_size,
                                                             const sdk::FVector& camera_location,
                                                             const sdk::FVector& camera_direction,
                                                             char region_axis,
                                                             double coverage_step_texels,
                                                             int max_strokes,
                                                             std::vector<MeshFirstPlanSample>& samples,
                                                             MeshFirstPlanStats& stats,
                                                             std::string& failure) -> bool
    {
        constexpr double kMeshFrontThreshold = 0.35;
        constexpr double kMeshBackThreshold = 0.35;
        constexpr double kMeshSourceFacingThreshold = -0.001;

        samples.clear();
        stats = {};
        const int expected_triangles = profile ? profile->index_count / 3 : static_cast<int>(triangles.size());
        if (expected_triangles <= 0 ||
            static_cast<int>(triangles.size()) != expected_triangles)
        {
            failure = "planner_profile_triangle_mismatch";
            return false;
        }
        if (profile &&
            (static_cast<int>(profile->indices.size()) < profile->index_count ||
             static_cast<int>(profile->triangles.size()) != expected_triangles))
        {
            failure = "planner_profile_triangle_mismatch";
            return false;
        }
        const double texture_size_double = static_cast<double>(std::max(1, texture_size));
        const double step_uv = clamp_range(coverage_step_texels, 1.0, 64.0) / texture_size_double;
        samples.reserve(std::min<std::size_t>(static_cast<std::size_t>(std::max(1, max_strokes)) + 1, 100000));

        for (std::size_t tri = 0; tri < static_cast<std::size_t>(expected_triangles); ++tri)
        {
            const auto& triangle = triangles[tri];
            const std::size_t index_base = tri * 3;
            if (profile && index_base + 2 >= profile->indices.size())
            {
                ++stats.invalid_triangles;
                continue;
            }
            bool valid_indices = true;
            for (int vertex_slot = 0; vertex_slot < 3; ++vertex_slot)
            {
                if (profile)
                {
                    const int vertex_index = profile->indices[index_base + static_cast<std::size_t>(vertex_slot)];
                    if (vertex_index < 0 || vertex_index >= profile->vertex_count)
                    {
                        valid_indices = false;
                        continue;
                    }
                }
                if (!mesh_first_finite_vector(triangle.world[vertex_slot]) ||
                    !mesh_first_finite_vector(triangle.local[vertex_slot]) ||
                    !mesh_first_finite_uv(triangle.uv[vertex_slot]))
                {
                    valid_indices = false;
                    continue;
                }
            }
            if (!valid_indices)
            {
                ++stats.invalid_triangles;
                continue;
            }
            const auto world_normal = sdk_vec_normalize(sdk_vec_cross(sdk_vec_sub(triangle.world[1], triangle.world[0]),
                                                                      sdk_vec_sub(triangle.world[2], triangle.world[0])));
            const auto runtime_local_normal = sdk_vec_normalize(sdk_vec_cross(sdk_vec_sub(triangle.local[1], triangle.local[0]),
                                                                              sdk_vec_sub(triangle.local[2], triangle.local[0])));
            auto region_normal = profile ? sdk_vec_normalize(profile->triangles[tri].local_normal) : runtime_local_normal;
            if (sdk_vec_len(region_normal) <= 0.000001)
            {
                region_normal = runtime_local_normal;
            }
            if (sdk_vec_len(world_normal) <= 0.000001 || sdk_vec_len(region_normal) <= 0.000001)
            {
                ++stats.degenerate_triangles;
                continue;
            }
            const double camera_facing = sdk_vec_dot(world_normal, camera_direction);
            const auto world_center = sdk_vec_mul(sdk_vec_add(sdk_vec_add(triangle.world[0], triangle.world[1]), triangle.world[2]), 1.0 / 3.0);
            const double camera_depth = sdk_vec_dot(sdk_vec_sub(world_center, camera_location), camera_direction);
            const double mesh_facing = mesh_first_axis_component(region_normal, region_axis);
            const bool source_candidate = camera_depth > 0.0 && camera_facing < kMeshSourceFacingThreshold;
            if (camera_depth <= 0.0)
            {
                ++stats.source_depth_rejected;
            }
            else if (!source_candidate)
            {
                ++stats.source_facing_rejected;
            }
            const auto region = mesh_facing <= -kMeshFrontThreshold
                                    ? MeshFirstRegion::Front
                                    : (mesh_facing >= kMeshBackThreshold ? MeshFirstRegion::Back : MeshFirstRegion::Side);
            ++stats.total_triangles;
            ++mesh_first_triangle_region_count(stats, region);

            const double uv_area = mesh_first_uv_triangle_area(triangle.uv[0].X,
                                                              triangle.uv[0].Y,
                                                              triangle.uv[1].X,
                                                              triangle.uv[1].Y,
                                                              triangle.uv[2].X,
                                                              triangle.uv[2].Y);
            MeshFirstProfile::TriangleMeta runtime_meta{};
            runtime_meta.uv_island = -1;
            runtime_meta.dominant_bone = -1;
            runtime_meta.body_region = "runtime";
            runtime_meta.local_normal = region_normal;
            runtime_meta.uv_area = uv_area;
            const auto& meta = profile ? profile->triangles[tri] : runtime_meta;
            int emitted = 0;
            const double min_u = clamp01(std::min({triangle.uv[0].X, triangle.uv[1].X, triangle.uv[2].X}));
            const double max_u = clamp01(std::max({triangle.uv[0].X, triangle.uv[1].X, triangle.uv[2].X}));
            const double min_v = clamp01(std::min({triangle.uv[0].Y, triangle.uv[1].Y, triangle.uv[2].Y}));
            const double max_v = clamp01(std::max({triangle.uv[0].Y, triangle.uv[1].Y, triangle.uv[2].Y}));
            const double start_u = std::floor(min_u / step_uv) * step_uv + step_uv * 0.5;
            const double start_v = std::floor(min_v / step_uv) * step_uv + step_uv * 0.5;
            for (double v = start_v; v <= max_v + step_uv * 0.25; v += step_uv)
            {
                for (double u = start_u; u <= max_u + step_uv * 0.25; u += step_uv)
                {
                    double a = 0.0;
                    double b = 0.0;
                    double c = 0.0;
                    if (!mesh_first_barycentric_uv(triangle.uv[0].X,
                                                   triangle.uv[0].Y,
                                                   triangle.uv[1].X,
                                                   triangle.uv[1].Y,
                                                   triangle.uv[2].X,
                                                   triangle.uv[2].Y,
                                                   u,
                                                   v,
                                                   a,
                                                   b,
                                                   c))
                    {
                        continue;
                    }
                    mesh_first_emit_runtime_triangle_sample(samples, triangle, meta, static_cast<int>(tri), region, source_candidate, camera_facing, uv_area, a, b, c);
                    ++emitted;
                }
            }
            if (emitted == 0 && uv_area > 0.0)
            {
                mesh_first_emit_runtime_triangle_sample(samples, triangle, meta, static_cast<int>(tri), region, source_candidate, camera_facing, uv_area, 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0);
            }
        }

        stats.total_samples = static_cast<int>(samples.size());
        stats.source_samples = 0;
        stats.front_samples = 0;
        stats.side_samples = 0;
        stats.back_samples = 0;
        for (const auto& sample : samples)
        {
            if (sample.source_candidate)
            {
                ++stats.source_samples;
            }
            ++mesh_first_sample_region_count(stats, sample.region);
        }
        if (stats.invalid_triangles > 0 || stats.degenerate_triangles > 0)
        {
            failure = "planner_invalid_runtime_triangles";
            return false;
        }
        if (samples.empty())
        {
            failure = "planner_no_runtime_samples";
            return false;
        }
        if (stats.total_samples > max_strokes)
        {
            failure = "planner_max_strokes_exceeded";
            return false;
        }
        return true;
    }

    auto mesh_first_capture_project_color(const SdkFrontCaptureResult& capture,
                                          const sdk::FVector& world_position,
                                          Color& color) -> bool
    {
        const auto expected_pixels = static_cast<std::size_t>(std::max(0, capture.width)) *
                                     static_cast<std::size_t>(std::max(0, capture.height));
        if (!capture.capture_pixels_available ||
            capture.width <= 0 ||
            capture.height <= 0 ||
            capture.capture_pixels.size() < expected_pixels)
        {
            return false;
        }

        const auto capture_forward = sdk_vec_normalize(capture.capture_direction);
        sdk::FVector world_up{0.0, 0.0, 1.0};
        auto capture_right = sdk_vec_normalize(sdk_vec_cross(world_up, capture_forward));
        if (sdk_vec_len(capture_right) <= 0.000001)
        {
            world_up = {0.0, 1.0, 0.0};
            capture_right = sdk_vec_normalize(sdk_vec_cross(world_up, capture_forward));
        }
        const auto capture_up = sdk_vec_normalize(sdk_vec_cross(capture_forward, capture_right));

        const double half_fov_radians = capture.capture_fov * 3.14159265358979323846 / 360.0;
        const double tan_half_horizontal = std::tan(half_fov_radians);
        const double tan_half_vertical = tan_half_horizontal / std::max(0.001, capture.capture_aspect);
        if (sdk_vec_len(capture_forward) <= 0.000001 ||
            sdk_vec_len(capture_right) <= 0.000001 ||
            sdk_vec_len(capture_up) <= 0.000001 ||
            !std::isfinite(tan_half_horizontal) ||
            !std::isfinite(tan_half_vertical) ||
            tan_half_horizontal <= 0.000001 ||
            tan_half_vertical <= 0.000001)
        {
            return false;
        }

        const auto rel = sdk_vec_sub(world_position, capture.capture_location);
        const double depth = sdk_vec_dot(rel, capture_forward);
        if (!std::isfinite(depth) || depth <= 0.000001)
        {
            return false;
        }

        const double right = sdk_vec_dot(rel, capture_right);
        const double up = sdk_vec_dot(rel, capture_up);
        const double ndc_x = right / (depth * tan_half_horizontal);
        const double ndc_y = up / (depth * tan_half_vertical);
        if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y))
        {
            return false;
        }

        const double sx = (ndc_x * 0.5 + 0.5) * static_cast<double>(capture.width);
        const double sy = (0.5 - ndc_y * 0.5) * static_cast<double>(capture.height);
        if (sx < 0.0 ||
            sy < 0.0 ||
            sx >= static_cast<double>(capture.width) ||
            sy >= static_cast<double>(capture.height))
        {
            return false;
        }

        const int px = std::max(0, std::min(capture.width - 1, static_cast<int>(std::round(sx))));
        const int py = std::max(0, std::min(capture.height - 1, static_cast<int>(std::round(sy))));
        const int bx = capture.capture_flip_x ? (capture.width - 1 - px) : px;
        const int by = capture.capture_flip_y ? (capture.height - 1 - py) : py;
        const auto pixel_index = static_cast<std::size_t>(by) * static_cast<std::size_t>(capture.width) +
                                 static_cast<std::size_t>(bx);
        if (pixel_index >= capture.capture_pixels.size())
        {
            return false;
        }

        color = capture.capture_pixels[pixel_index];
        color.r = clamp01(color.r);
        color.g = clamp01(color.g);
        color.b = clamp01(color.b);
        color.roughness = 0.65;
        color.metallic = 0.0;
        return true;
    }

    auto mesh_first_assign_colors(std::vector<MeshFirstPlanSample>& samples,
                                  const SdkFrontCaptureResult& capture,
                                  bool enable_front,
                                  bool enable_side,
                                  bool enable_back,
                                  MeshFirstPlanStats& stats) -> void
    {
        const auto& source_samples = capture.samples;
        std::vector<double> uv_distances{};
        std::vector<double> component_distances{};
        uv_distances.reserve(samples.size());
        component_distances.reserve(samples.size());
        std::vector<const FrontSample*> direct_source_by_plan_index(samples.size(), nullptr);
        for (const auto& source : source_samples)
        {
            if (source.plan_index >= 0 && source.plan_index < static_cast<int>(direct_source_by_plan_index.size()))
            {
                direct_source_by_plan_index[static_cast<std::size_t>(source.plan_index)] = &source;
            }
        }
        for (std::size_t sample_index = 0; sample_index < samples.size(); ++sample_index)
        {
            auto& sample = samples[sample_index];
            const bool enabled = (sample.region == MeshFirstRegion::Front && enable_front) ||
                                 (sample.region == MeshFirstRegion::Side && enable_side) ||
                                 (sample.region == MeshFirstRegion::Back && enable_back);
            if (!enabled)
            {
                continue;
            }
            if (source_samples.empty())
            {
                sample.unsafe = true;
                sample.source_distance_uv = std::numeric_limits<double>::infinity();
                sample.source_distance_component = std::numeric_limits<double>::infinity();
            }
            else
            {
                const auto* direct_source = direct_source_by_plan_index[sample_index];
                if (sample.source_candidate && direct_source)
                {
                    sample.source_distance_component = 0.0;
                    sample.source_distance_uv = 0.0;
                    component_distances.push_back(0.0);
                    uv_distances.push_back(0.0);
                    sample.r = clamp01(direct_source->r);
                    sample.g = clamp01(direct_source->g);
                    sample.b = clamp01(direct_source->b);
                    sample.roughness = clamp01(std::max(0.35, direct_source->roughness));
                    sample.metallic = clamp01(direct_source->metallic);
                    sample.unsafe = false;
                    ++stats.source_direct_assignments;
                    ++stats.enabled_samples;
                    continue;
                }
                Color projected_color{};
                if (mesh_first_capture_project_color(capture, sample.world_position, projected_color))
                {
                    sample.source_distance_component = 0.0;
                    sample.source_distance_uv = 0.0;
                    component_distances.push_back(0.0);
                    uv_distances.push_back(0.0);
                    sample.r = clamp01(projected_color.r);
                    sample.g = clamp01(projected_color.g);
                    sample.b = clamp01(projected_color.b);
                    sample.roughness = clamp01(projected_color.roughness);
                    sample.metallic = clamp01(projected_color.metallic);
                    sample.unsafe = false;
                    ++stats.source_projection_assignments;
                    ++stats.enabled_samples;
                    continue;
                }
                sample.source_distance_uv = std::numeric_limits<double>::infinity();
                sample.source_distance_component = std::numeric_limits<double>::infinity();
                sample.unsafe = true;
                ++stats.unsafe_projection_color;
            }
            if (sample.unsafe)
            {
                ++stats.unsafe_candidates;
                if (sample.region == MeshFirstRegion::Front)
                {
                    ++stats.unsafe_front;
                }
                else if (sample.region == MeshFirstRegion::Side)
                {
                    ++stats.unsafe_side;
                }
                else
                {
                    ++stats.unsafe_back;
                }
            }
            ++stats.enabled_samples;
            if (sample.unsafe)
            {
                ++stats.unsafe_enabled;
            }
        }
        if (!uv_distances.empty())
        {
            double sum = 0.0;
            for (const auto distance : uv_distances)
            {
                sum += distance;
                stats.source_distance_max_uv = std::max(stats.source_distance_max_uv, distance);
            }
            stats.source_distance_avg_uv = sum / static_cast<double>(uv_distances.size());
            std::sort(uv_distances.begin(), uv_distances.end());
            const auto index = std::min(uv_distances.size() - 1,
                                        static_cast<std::size_t>(std::floor(static_cast<double>(uv_distances.size() - 1) * 0.95)));
            stats.source_distance_p95_uv = uv_distances[index];
        }
        if (!component_distances.empty())
        {
            double sum = 0.0;
            for (const auto distance : component_distances)
            {
                sum += distance;
                stats.source_distance_max_component = std::max(stats.source_distance_max_component, distance);
            }
            stats.source_distance_avg_component = sum / static_cast<double>(component_distances.size());
            std::sort(component_distances.begin(), component_distances.end());
            const auto index = std::min(component_distances.size() - 1,
                                        static_cast<std::size_t>(std::floor(static_cast<double>(component_distances.size() - 1) * 0.95)));
            stats.source_distance_p95_component = component_distances[index];
        }
    }

    auto mesh_first_write_bmp_rgb(const std::wstring& path,
                                  int width,
                                  int height,
                                  const std::vector<std::uint8_t>& rgb) -> bool
    {
        if (width <= 0 || height <= 0 ||
            rgb.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3)
        {
            return false;
        }
        const int row_stride = ((width * 3 + 3) / 4) * 4;
        const std::uint32_t pixel_bytes = static_cast<std::uint32_t>(row_stride * height);
        const std::uint32_t file_size = 54U + pixel_bytes;
        std::vector<std::uint8_t> bytes{};
        bytes.resize(file_size, 0);
        bytes[0] = 'B';
        bytes[1] = 'M';
        auto put_u16 = [&](std::size_t offset, std::uint16_t value) {
            bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xff);
            bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
        };
        auto put_u32 = [&](std::size_t offset, std::uint32_t value) {
            bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xff);
            bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
            bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
            bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
        };
        put_u32(2, file_size);
        put_u32(10, 54);
        put_u32(14, 40);
        put_u32(18, static_cast<std::uint32_t>(width));
        put_u32(22, static_cast<std::uint32_t>(height));
        put_u16(26, 1);
        put_u16(28, 24);
        put_u32(34, pixel_bytes);
        for (int y = 0; y < height; ++y)
        {
            const int src_y = height - 1 - y;
            auto* dst = bytes.data() + 54 + static_cast<std::size_t>(y) * static_cast<std::size_t>(row_stride);
            const auto* src = rgb.data() + (static_cast<std::size_t>(src_y) * static_cast<std::size_t>(width)) * 3;
            for (int x = 0; x < width; ++x)
            {
                dst[x * 3 + 0] = src[x * 3 + 2];
                dst[x * 3 + 1] = src[x * 3 + 1];
                dst[x * 3 + 2] = src[x * 3 + 0];
            }
        }
        return write_binary_file_w(path, bytes);
    }

    auto mesh_first_write_uv_debug_artifacts(const std::vector<MeshFirstPlanSample>& samples,
                                             int texture_size,
                                             bool enable_front,
                                             bool enable_side,
                                             bool enable_back,
                                             std::string& metadata) -> void
    {
        const auto dir = runtime_log_dir_path();
        if (dir.empty())
        {
            metadata += ",\"mesh_debug_artifacts_written\":false";
            metadata += ",\"mesh_debug_artifacts_failure\":\"runtime_log_dir_unavailable\"";
            return;
        }
        const int size = std::max(64, std::min(2048, texture_size));
        const auto stamp = std::to_wstring(GetTickCount64());
        const auto color_path = dir + L"\\mesh-first-uv-color-" + stamp + L".bmp";
        const auto region_path = dir + L"\\mesh-first-uv-region-" + stamp + L".bmp";
        std::vector<std::uint8_t> color_rgb(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3, 8);
        std::vector<std::uint8_t> region_rgb(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 3, 8);
        auto draw = [&](std::vector<std::uint8_t>& image, double u, double v, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const int cx = std::max(0, std::min(size - 1, static_cast<int>(std::round(clamp01(u) * static_cast<double>(size - 1)))));
            const int cy = std::max(0, std::min(size - 1, static_cast<int>(std::round((1.0 - clamp01(v)) * static_cast<double>(size - 1)))));
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int x = cx + dx;
                    const int y = cy + dy;
                    if (x < 0 || y < 0 || x >= size || y >= size)
                    {
                        continue;
                    }
                    const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 3;
                    image[index + 0] = r;
                    image[index + 1] = g;
                    image[index + 2] = b;
                }
            }
        };
        int written_samples = 0;
        for (const auto& sample : samples)
        {
            const bool enabled = (sample.region == MeshFirstRegion::Front && enable_front) ||
                                 (sample.region == MeshFirstRegion::Side && enable_side) ||
                                 (sample.region == MeshFirstRegion::Back && enable_back);
            if (!enabled || sample.unsafe)
            {
                continue;
            }
            draw(color_rgb,
                 sample.u,
                 sample.v,
                 static_cast<std::uint8_t>(std::round(clamp01(sample.r) * 255.0)),
                 static_cast<std::uint8_t>(std::round(clamp01(sample.g) * 255.0)),
                 static_cast<std::uint8_t>(std::round(clamp01(sample.b) * 255.0)));
            if (sample.region == MeshFirstRegion::Front)
            {
                draw(region_rgb, sample.u, sample.v, 255, 80, 80);
            }
            else if (sample.region == MeshFirstRegion::Side)
            {
                draw(region_rgb, sample.u, sample.v, 80, 220, 120);
            }
            else
            {
                draw(region_rgb, sample.u, sample.v, 100, 150, 255);
            }
            ++written_samples;
        }
        const bool color_ok = mesh_first_write_bmp_rgb(color_path, size, size, color_rgb);
        const bool region_ok = mesh_first_write_bmp_rgb(region_path, size, size, region_rgb);
        auto narrow = [](const std::wstring& value) {
            std::string out{};
            out.reserve(value.size());
            for (const auto ch : value)
            {
                out.push_back(ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?');
            }
            return out;
        };
        metadata += ",\"mesh_debug_artifacts_written\":" + std::string(json_bool(color_ok && region_ok));
        metadata += ",\"mesh_debug_artifact_samples\":" + std::to_string(written_samples);
        metadata += ",\"mesh_debug_uv_color_bmp\":\"" + json_escape(narrow(color_path)) + "\"";
        metadata += ",\"mesh_debug_uv_region_bmp\":\"" + json_escape(narrow(region_path)) + "\"";
    }

    auto mesh_first_plan_stats_metadata(const MeshFirstPlanStats& stats) -> std::string
    {
        return "\"planner_triangles_total\":" + std::to_string(stats.total_triangles) +
               ",\"planner_triangles_front\":" + std::to_string(stats.front_triangles) +
               ",\"planner_triangles_side\":" + std::to_string(stats.side_triangles) +
               ",\"planner_triangles_back\":" + std::to_string(stats.back_triangles) +
               ",\"planner_invalid_triangles\":" + std::to_string(stats.invalid_triangles) +
               ",\"planner_degenerate_triangles\":" + std::to_string(stats.degenerate_triangles) +
               ",\"planner_samples_total\":" + std::to_string(stats.total_samples) +
               ",\"planner_samples_source\":" + std::to_string(stats.source_samples) +
               ",\"planner_samples_front\":" + std::to_string(stats.front_samples) +
               ",\"planner_samples_side\":" + std::to_string(stats.side_samples) +
               ",\"planner_samples_back\":" + std::to_string(stats.back_samples) +
               ",\"planner_samples_enabled\":" + std::to_string(stats.enabled_samples) +
               ",\"unsafe_candidates\":" + std::to_string(stats.unsafe_candidates) +
               ",\"unsafe_front\":" + std::to_string(stats.unsafe_front) +
               ",\"unsafe_side\":" + std::to_string(stats.unsafe_side) +
               ",\"unsafe_back\":" + std::to_string(stats.unsafe_back) +
               ",\"unsafe_enabled\":" + std::to_string(stats.unsafe_enabled) +
               ",\"unsafe_projection_color\":" + std::to_string(stats.unsafe_projection_color) +
               ",\"source_depth_rejected\":" + std::to_string(stats.source_depth_rejected) +
               ",\"source_facing_rejected\":" + std::to_string(stats.source_facing_rejected) +
               ",\"source_direct_assignments\":" + std::to_string(stats.source_direct_assignments) +
               ",\"source_projection_assignments\":" + std::to_string(stats.source_projection_assignments) +
               ",\"source_distance_avg_uv\":" + std::to_string(stats.source_distance_avg_uv) +
               ",\"source_distance_p95_uv\":" + std::to_string(stats.source_distance_p95_uv) +
               ",\"source_distance_max_uv\":" + std::to_string(stats.source_distance_max_uv) +
               ",\"source_distance_avg_component\":" + std::to_string(stats.source_distance_avg_component) +
               ",\"source_distance_p95_component\":" + std::to_string(stats.source_distance_p95_component) +
               ",\"source_distance_max_component\":" + std::to_string(stats.source_distance_max_component);
    }

    struct MeshFirstReplicationSnapshot
    {
        bool recorded_count_available{false};
        int recorded_count{-1};
        bool manager_available{false};
        std::uintptr_t manager{0};
        bool manager_queued_count_available{false};
        int manager_queued_count{-1};
        bool manager_component_queued_count_available{false};
        int manager_component_queued_count{-1};
        std::string failure{};
    };

    struct MeshFirstRecordedStrokeCountParams
    {
        int ReturnValue{0};
    };

    struct MeshFirstQueuedStrokeCountParams
    {
        int ReturnValue{0};
    };

    struct MeshFirstQueuedStrokeCountForComponentParams
    {
        void* PaintComponent{nullptr};
        int ReturnValue{0};
        std::uint8_t Pad_C[0x4]{};
    };

    auto mesh_first_capture_replication_snapshot(Reflection& ref, std::uintptr_t component) -> MeshFirstReplicationSnapshot
    {
        MeshFirstReplicationSnapshot snapshot{};
        if (live_uobject(component))
        {
            if (const auto function = ref.find_function(component, "GetRecordedStrokeCount"))
            {
                MeshFirstRecordedStrokeCountParams params{};
                std::string failure{};
                if (process_event(component, function, reinterpret_cast<std::uint8_t*>(&params), failure))
                {
                    snapshot.recorded_count_available = true;
                    snapshot.recorded_count = params.ReturnValue;
                }
                else if (snapshot.failure.empty())
                {
                    snapshot.failure = failure;
                }
            }
            else if (snapshot.failure.empty())
            {
                snapshot.failure = "GetRecordedStrokeCount_unavailable";
            }
        }
        else if (snapshot.failure.empty())
        {
            snapshot.failure = "paint_component_unavailable";
        }

        const auto manager = ref.find_first_instance("RuntimePaintReplicationManager");
        snapshot.manager = manager;
        snapshot.manager_available = live_uobject(manager);
        if (!snapshot.manager_available)
        {
            if (snapshot.failure.empty())
            {
                snapshot.failure = "RuntimePaintReplicationManager_unavailable";
            }
            return snapshot;
        }

        if (const auto function = ref.find_function(manager, "GetQueuedStrokeCount"))
        {
            MeshFirstQueuedStrokeCountParams params{};
            std::string failure{};
            if (process_event(manager, function, reinterpret_cast<std::uint8_t*>(&params), failure))
            {
                snapshot.manager_queued_count_available = true;
                snapshot.manager_queued_count = params.ReturnValue;
            }
            else if (snapshot.failure.empty())
            {
                snapshot.failure = failure;
            }
        }
        else if (snapshot.failure.empty())
        {
            snapshot.failure = "GetQueuedStrokeCount_unavailable";
        }

        if (const auto function = ref.find_function(manager, "GetQueuedStrokeCountForComponent"))
        {
            MeshFirstQueuedStrokeCountForComponentParams params{};
            params.PaintComponent = reinterpret_cast<void*>(component);
            std::string failure{};
            if (process_event(manager, function, reinterpret_cast<std::uint8_t*>(&params), failure))
            {
                snapshot.manager_component_queued_count_available = true;
                snapshot.manager_component_queued_count = params.ReturnValue;
            }
            else if (snapshot.failure.empty())
            {
                snapshot.failure = failure;
            }
        }
        else if (snapshot.failure.empty())
        {
            snapshot.failure = "GetQueuedStrokeCountForComponent_unavailable";
        }

        return snapshot;
    }

    auto mesh_first_replication_snapshot_metadata(const char* prefix, const MeshFirstReplicationSnapshot& snapshot) -> std::string
    {
        std::string key(prefix ? prefix : "mesh_replication");
        return ",\"" + key + "_recorded_count_available\":" + json_bool(snapshot.recorded_count_available) +
               ",\"" + key + "_recorded_count\":" + std::to_string(snapshot.recorded_count) +
               ",\"" + key + "_manager_available\":" + json_bool(snapshot.manager_available) +
               ",\"" + key + "_manager\":\"" + hex_address(snapshot.manager) + "\"" +
               ",\"" + key + "_manager_queued_count_available\":" + json_bool(snapshot.manager_queued_count_available) +
               ",\"" + key + "_manager_queued_count\":" + std::to_string(snapshot.manager_queued_count) +
               ",\"" + key + "_manager_component_queued_count_available\":" + json_bool(snapshot.manager_component_queued_count_available) +
               ",\"" + key + "_manager_component_queued_count\":" + std::to_string(snapshot.manager_component_queued_count) +
               ",\"" + key + "_failure\":\"" + json_escape(snapshot.failure) + "\"";
    }

    struct MeshFirstServerBatchAsyncJob
    {
        std::shared_ptr<QueuedPaintJob> queued{};
        std::uintptr_t component{0};
        std::uintptr_t server_paint_batch_function{0};
        std::uintptr_t local_paint_at_uv_function{0};
        std::vector<sdk::FPaintStroke> strokes{};
        std::string metadata{};
        MeshFirstChannelChecksum albedo_before{};
        MeshFirstReplicationSnapshot replication_before{};
        int server_batch_limit{ServerPaintBatchStrokeLimit};
        int server_batch_delay_ms{ServerPaintBatchDelayMs};
        int local_visual_sync_batch_limit{512};
        int replay_front{0};
        int replay_side{0};
        int replay_back{0};
        int server_batch_calls{0};
        int server_batch_success{0};
        int server_batch_failures{0};
        int server_strokes_sent{0};
        std::size_t offset{0};
        std::string first_failure{};
        bool local_sync_started{false};
        std::size_t local_offset{0};
        int local_stroke_calls{0};
        int local_stroke_success{0};
        int local_stroke_failures{0};
        std::string local_visual_sync_failure{};
        std::chrono::steady_clock::time_point started{};
        std::chrono::steady_clock::time_point local_sync_started_at{};
        std::chrono::steady_clock::time_point server_next_batch_time{};
        UINT_PTR server_batch_timer_id{0};
    };

    std::mutex g_mesh_first_batch_mutex;
    std::shared_ptr<MeshFirstServerBatchAsyncJob> g_mesh_first_batch_job{};

    auto is_mesh_first_paint_request(const std::string& request) -> bool
    {
        return request.find("\"native_apply_mode\":\"mesh_first_paint\"") != std::string::npos ||
               request.find("\"route\":\"f10_mesh_first_paint\"") != std::string::npos;
    }

    auto paint_mesh_first_on_game_thread(const std::string& request,
                                         const std::shared_ptr<QueuedPaintJob>& queued_job) -> std::string
    {
        const bool enable_front = json_bool_field(request, "enable_front_paint", true);
        const bool enable_side = json_bool_field(request, "enable_side_paint", true);
        const bool enable_back = json_bool_field(request, "enable_back_paint", true);
        const bool research_artifacts = json_bool_field(request, "research_artifacts", false);
        const std::string quality_preset = json_string_field(request, "quality_preset", "High");
        const double tuning_stroke_size_texels = clamp_range(json_number_field(request, "stroke_size_texels", 4.0), 1.0, 12.0);
        const double tuning_coverage_step_texels = clamp_range(json_number_field(request, "coverage_step_texels", 6.0), 1.0, 12.0);
        const double tuning_side_source_max_uv = clamp_range(json_number_field(request, "side_source_max_uv", 0.08), 0.001, 0.50);
        const double tuning_front_back_source_max_uv = clamp_range(json_number_field(request, "front_back_source_max_uv", 0.45), 0.001, 2.00);
        const int tuning_max_strokes = json_int_field(request, "max_strokes", 25000, 1000, 100000);
        const int tuning_server_batch_limit = json_int_field(request, "server_batch_limit", ServerPaintBatchStrokeLimit, 1, ServerPaintBatchStrokeLimit);
        const int tuning_server_batch_delay_ms = json_int_field(request, "server_batch_delay_ms", ServerPaintBatchDelayMs, 1, 1000);

        std::string metadata = "\"route\":\"mesh_first_paint\"";
        metadata += ",\"mesh_first_pipeline\":\"profile_v2_pose_uv_atlas_server_batch\"";
        metadata += ",\"mesh_region_model\":\"mesh_local_normal\"";
        metadata += ",\"mesh_source_model\":\"projected_visible_zbuffer\"";
        metadata += ",\"mesh_back_color_source\":\"shared_camera_facing_source\"";
        metadata += ",\"old_dense_hittest_fallback_used\":false";
        metadata += ",\"runtime_hit_test_used\":false";
        metadata += ",\"server_paint_batch_required\":true";
        metadata += ",\"research_artifacts_requested\":" + std::string(json_bool(research_artifacts));
        metadata += ",\"enable_front_paint\":" + std::string(json_bool(enable_front));
        metadata += ",\"enable_side_paint\":" + std::string(json_bool(enable_side));
        metadata += ",\"enable_back_paint\":" + std::string(json_bool(enable_back));
        metadata += ",\"quality_preset\":\"" + json_escape(quality_preset) + "\"";
        metadata += ",\"stroke_size_texels\":" + std::to_string(tuning_stroke_size_texels);
        metadata += ",\"coverage_step_texels\":" + std::to_string(tuning_coverage_step_texels);
        metadata += ",\"side_source_max_uv\":" + std::to_string(tuning_side_source_max_uv);
        metadata += ",\"front_back_source_max_uv\":" + std::to_string(tuning_front_back_source_max_uv);
        metadata += ",\"max_strokes\":" + std::to_string(tuning_max_strokes);
        metadata += ",\"server_batch_limit\":" + std::to_string(tuning_server_batch_limit);
        metadata += ",\"server_batch_delay_ms\":" + std::to_string(tuning_server_batch_delay_ms);
        metadata += ",\"bridge_events\":[\"mesh_profile_load\",\"pose_resolve\",\"planner_build\",\"bridge.paint_batch.request\",\"bridge.paint_batch.response\"]";

        if (queued_job)
        {
            std::lock_guard<std::mutex> lock(g_mesh_first_batch_mutex);
            if (g_mesh_first_batch_job)
            {
                return response_json(false,
                                     "mesh_first_busy",
                                     0,
                                     1,
                                     "mesh-first paint is already running",
                                     metadata + ",\"replay_blocked\":true");
            }
        }
        else
        {
            return response_json(false,
                                 "mesh_first_async_required",
                                 0,
                                 1,
                                 "mesh-first paint requires the async queued dispatcher",
                                 metadata + ",\"replay_blocked\":true");
        }

        if (!enable_front && !enable_side && !enable_back)
        {
            return response_json(false,
                                 "mesh_regions_disabled",
                                 0,
                                 1,
                                 "all mesh-first paint regions are disabled",
                                 metadata);
        }

        Reflection ref{};
        std::string failure{};
        if (!ref.init(failure))
        {
            return response_json(false,
                                 "sdk_update_required",
                                 0,
                                 1,
                                 failure.empty() ? "SDK reflection init failed" : failure,
                                 metadata + ",\"sdk_resolution_exception\":true");
        }

        SdkContext ctx{};
        try
        {
            ctx = sdk_resolve_context(ref);
        }
        catch (const SdkResolutionException& ex)
        {
            return response_json(false,
                                 ex.stage.c_str(),
                                 0,
                                 1,
                                 ex.what(),
                                 metadata + ",\"sdk_resolution_exception\":true");
        }

        metadata += ",";
        metadata += sdk_context_metadata(ref, ctx);
        metadata += paint_stroke_reflection_metadata(ref, ctx.server_paint_batch_function);
        if (!ctx.ok)
        {
            return response_json(false, ctx.stage.c_str(), 0, 1, ctx.message, metadata);
        }
        if (!ctx.server_paint_batch_function)
        {
            return response_json(false,
                                 "mesh_server_batch_unavailable",
                                 0,
                                 1,
                                 "ServerPaintBatch is unavailable; mesh-first paint cannot replay",
                                 metadata);
        }

        const auto mesh_candidates = sdk_collect_front_mesh_candidates(ref, ctx);
        metadata += ",\"front_mesh_candidate_count\":" + std::to_string(mesh_candidates.size());
        metadata += ",\"front_mesh_candidates\":" + sdk_front_mesh_candidates_json(ref, mesh_candidates);
        if (mesh_candidates.empty())
        {
            return response_json(false,
                                 "mesh_component_unavailable",
                                 0,
                                 1,
                                 "no live skeletal mesh component candidate was found",
                                 metadata);
        }
        SdkFrontMeshCandidate selected_mesh{};
        if (!sdk_select_profile_mesh_candidate(ref, mesh_candidates, selected_mesh))
        {
            return response_json(false,
                                 "mesh_profile_runtime_identity_mismatch",
                                 0,
                                 1,
                                 "no live mesh candidate matched the paintman mesh profile",
                                 metadata);
        }
        metadata += ",\"selected_mesh\":\"" + hex_address(selected_mesh.mesh) + "\"";
        metadata += ",\"selected_mesh_source\":\"" + json_escape(selected_mesh.source) + "\"";
        metadata += ",\"selected_mesh_name\":\"" + json_escape(ref.object_name(selected_mesh.mesh)) + "\"";
        metadata += ",\"selected_mesh_class\":\"" + json_escape(ref.class_name(selected_mesh.mesh)) + "\"";
        metadata += ",\"selected_mesh_path\":\"" + json_escape(ref.object_path(selected_mesh.mesh)) + "\"";
        metadata += ",\"selected_mesh_asset\":\"" + hex_address(selected_mesh.asset) + "\"";
        metadata += ",\"selected_mesh_asset_name\":\"" + json_escape(ref.object_name(selected_mesh.asset)) + "\"";
        metadata += ",\"selected_mesh_asset_path\":\"" + json_escape(ref.object_path(selected_mesh.asset)) + "\"";

        write_bridge_progress("mesh_profile_load",
                              "Loading Mesh Profile V2",
                              1,
                              4,
                              0.0,
                              "\"pipeline\":\"mesh_first_paint\"");
        const auto profile_catalog = load_mesh_first_profile_catalog();
        metadata += ",\"mesh_profile_catalog_count\":" + std::to_string(profile_catalog.size());
        MeshFirstProfile profile{};
        std::string profile_failure{};
        const bool profile_available = select_mesh_first_profile_for_candidate(ref, profile_catalog, selected_mesh, profile, profile_failure);
        if (profile_available)
        {
            metadata += ",";
            metadata += mesh_first_profile_metadata(profile);
            metadata += ",\"mesh_identity_match\":true";
        }
        else
        {
            metadata += ",\"mesh_profile_stage\":\"" + std::string(profile_catalog.empty() ? "mesh_profile_missing" : "mesh_profile_identity_mismatch") + "\"";
            metadata += ",\"mesh_profile_ok\":false";
            metadata += ",\"mesh_profile_failure\":\"" + json_escape(profile_failure.empty() ? "Mesh Profile V2 is unavailable or does not match the live mesh" : profile_failure) + "\"";
            metadata += ",\"mesh_identity_match\":false";
            metadata += ",\"runtime_dynamic_profile_required\":true";
        }

        write_bridge_progress("pose_resolve",
                              "Resolving current skinned pose",
                              2,
                              4,
                              0.0,
                              "\"pipeline\":\"mesh_first_paint\",\"mesh_profile_bone_count\":" + std::to_string(profile_available ? profile.bone_count : 0));
        SdkPoseResolveResult pose{};
        if (profile_available)
        {
            pose = sdk_resolve_skinned_pose(ref, selected_mesh.mesh, profile.bone_count);
            if (pose.ok && !mesh_first_validate_pose_for_profile(profile, pose))
            {
                pose.stage = "pose_untrusted";
                pose.message = "current skinned pose candidate failed validation: " + pose.validation_failure;
            }
        }
        else
        {
            pose.stage = "pose_skipped_profile_unavailable";
            pose.message = "profile-free runtime triangle planning does not require skeletal pose resolution";
            pose.source = "runtime_triangle_cache";
            pose.validation_failure = "profile_unavailable";
        }
        metadata += ",";
        metadata += sdk_pose_result_metadata(pose);

        write_bridge_progress("planner_build",
                              "Building mesh-first plan",
                              3,
                              4,
                              0.0,
                              "\"pipeline\":\"mesh_first_paint\",\"pose_transform_count\":" + std::to_string(pose.transform_count));
        sdk::FTransform component_to_world{};
        std::string component_transform_source{};
        if (!mesh_first_resolve_component_to_world(ref, selected_mesh.mesh, ctx.body_world_position, component_to_world, component_transform_source))
        {
            return response_json(false,
                                 "component_world_transform_unavailable",
                                 0,
                                 1,
                                 "live mesh component transform is unavailable",
                                 metadata + ",\"component_world_transform_source\":\"" + json_escape(component_transform_source) +
                                     "\",\"pose_required\":true,\"bind_pose_fallback_used\":false,\"replay_blocked\":true");
        }
        metadata += ",\"component_world_transform_source\":\"" + json_escape(component_transform_source) + "\"";
        metadata += ",\"component_world_x\":" + std::to_string(component_to_world.Translation.X);
        metadata += ",\"component_world_y\":" + std::to_string(component_to_world.Translation.Y);
        metadata += ",\"component_world_z\":" + std::to_string(component_to_world.Translation.Z);

        std::vector<sdk::FVector> skinned_component_positions{};
        std::vector<sdk::FVector> skinned_world_positions{};
        std::string skin_failure{};
        bool skeletal_skin_available = false;
        if (profile_available && pose.ok && pose.trusted)
        {
            skeletal_skin_available = mesh_first_skin_vertices(profile,
                                                               pose,
                                                               component_to_world,
                                                               skinned_component_positions,
                                                               skinned_world_positions,
                                                               skin_failure);
        }
        metadata += ",\"skeletal_pose_available\":" + std::string(json_bool(pose.ok));
        metadata += ",\"skeletal_pose_trusted\":" + std::string(json_bool(pose.trusted));
        metadata += ",\"skeletal_skin_available\":" + std::string(json_bool(skeletal_skin_available));
        metadata += ",\"skeletal_skin_failure\":\"" + json_escape(skin_failure) + "\"";
        metadata += ",\"skinned_vertex_count\":" + std::to_string(skinned_world_positions.size());
        if (skeletal_skin_available)
        {
            metadata += ",";
            metadata += mesh_first_pose_displacement_metadata(profile, skinned_component_positions);
        }
        else
        {
            metadata += ",\"pose_skinned_delta_available\":false";
            metadata += ",\"pose_skinned_delta_avg\":0";
            metadata += ",\"pose_skinned_delta_max\":0";
            metadata += ",\"pose_skinned_delta_over_1cm\":0";
            metadata += ",\"pose_skinned_delta_over_10cm\":0";
        }

        MeshFirstRuntimeTriangleCache runtime_triangle_cache{};
        std::string runtime_triangle_cache_mode{};
        if (profile_available)
        {
            runtime_triangle_cache = mesh_first_resolve_runtime_triangle_cache(ctx.component, profile);
            runtime_triangle_cache_mode = "profile_verified";
        }
        if (!runtime_triangle_cache.ok)
        {
            const auto profile_cache_failure = runtime_triangle_cache.failure;
            runtime_triangle_cache = mesh_first_resolve_runtime_triangle_cache_dynamic(ctx.component);
            runtime_triangle_cache_mode = "dynamic_runtime_scan";
            if (!profile_cache_failure.empty())
            {
                metadata += ",\"runtime_triangle_profile_cache_failure\":\"" + json_escape(profile_cache_failure) + "\"";
            }
        }
        metadata += ",\"runtime_triangle_cache_used\":" + std::string(json_bool(runtime_triangle_cache.ok));
        metadata += ",\"runtime_triangle_cache_mode\":\"" + json_escape(runtime_triangle_cache_mode) + "\"";
        metadata += ",\"runtime_triangle_cache_offset\":\"" + (runtime_triangle_cache.owner_offset >= 0 ? hex_address(static_cast<std::uintptr_t>(runtime_triangle_cache.owner_offset)) : std::string("none")) + "\"";
        metadata += ",\"runtime_triangle_cache_stride\":" + std::to_string(runtime_triangle_cache.stride);
        metadata += ",\"runtime_triangle_cache_triangles\":" + std::to_string(runtime_triangle_cache.triangle_count);
        metadata += ",\"runtime_triangle_cache_profile_uv_avg_error\":" + std::to_string(runtime_triangle_cache.profile_uv_avg_error);
        metadata += ",\"runtime_triangle_cache_failure\":\"" + json_escape(runtime_triangle_cache.failure) + "\"";
        metadata += ",\"planner_position_source\":\"runtime_paintable_cached_current_triangles\"";
        metadata += ",\"pose_used_for_projection\":true";
        metadata += ",\"pose_used_for_replay_anchor\":true";
        metadata += ",\"skeletal_pose_used_for_projection\":false";
        metadata += ",\"runtime_triangle_cache_pose_used\":true";
        if (!runtime_triangle_cache.ok)
        {
            return response_json(false,
                                 runtime_triangle_cache.failure.empty() ? "runtime_triangle_cache_unavailable" : runtime_triangle_cache.failure.c_str(),
                                 0,
                                 1,
                                 "RuntimePaintable cached current triangles are unavailable; mesh-first paint cannot plan safely",
                                 metadata + ",\"replay_blocked\":true");
        }
        const int active_texture_size = profile_available ? profile.texture_size : 1024;
        const char region_axis = profile_available ? mesh_first_region_axis(profile)
                                                   : mesh_first_region_axis_from_runtime_triangles(runtime_triangle_cache.triangles);
        metadata += ",\"mesh_region_axis\":\"" + std::string(mesh_first_region_axis_label(region_axis)) + "\"";
        metadata += ",\"mesh_region_axis_selection\":\"" + std::string(profile_available ? "profile_min_horizontal_extent" : "runtime_triangle_min_horizontal_extent") + "\"";
        metadata += ",\"mesh_region_normal_source\":\"" + std::string(profile_available ? "profile_v2_triangle_local_normal" : "runtime_triangle_local_normal") + "\"";
        metadata += ",\"texture_size\":" + std::to_string(active_texture_size);
        auto runtime_coordinate_probe_triangles = runtime_triangle_cache.triangles;
        const auto runtime_coordinate_selection =
            mesh_first_select_runtime_triangle_coordinates(runtime_coordinate_probe_triangles, component_to_world);
        metadata += ",\"runtime_triangle_coordinate_mode\":\"" + json_escape(runtime_coordinate_selection.mode) + "\"";
        metadata += ",\"runtime_triangle_coordinates_swapped\":" + std::string(runtime_coordinate_selection.swapped ? "true" : "false");
        metadata += ",\"runtime_triangle_coordinate_samples\":" + std::to_string(runtime_coordinate_selection.samples);
        metadata += ",\"runtime_triangle_coordinate_direct_avg_error\":" + std::to_string(runtime_coordinate_selection.direct_avg_error);
        metadata += ",\"runtime_triangle_coordinate_swapped_avg_error\":" + std::to_string(runtime_coordinate_selection.swapped_avg_error);
        metadata += ",\"runtime_triangle_coordinate_selected_avg_error\":" + std::to_string(runtime_coordinate_selection.selected_avg_error);

        const auto viewport = sdk_get_viewport_info(ref, ctx);
        if (viewport.width <= 0 || viewport.height <= 0)
        {
            return response_json(false,
                                 "viewport_unavailable",
                                 0,
                                 1,
                                 "viewport size is unavailable",
                                 metadata + ",\"replay_blocked\":true");
        }
        const auto center_ray = sdk_deproject_screen_position(ref,
                                                              ctx,
                                                              static_cast<double>(viewport.width) * 0.5,
                                                              static_cast<double>(viewport.height) * 0.5);
        if (!center_ray.ok || sdk_vec_len(center_ray.direction) <= 0.000001)
        {
            return response_json(false,
                                 "camera_transform_unavailable",
                                 0,
                                 1,
                                 "current camera direction is unavailable",
                                 metadata + ",\"camera_failure\":\"" + json_escape(center_ray.failure) + "\",\"replay_blocked\":true");
        }
        const auto camera_direction = sdk_vec_normalize(center_ray.direction);
        metadata += ",\"viewport_width\":" + std::to_string(viewport.width);
        metadata += ",\"viewport_height\":" + std::to_string(viewport.height);
        metadata += ",\"camera_direction_x\":" + std::to_string(camera_direction.X);
        metadata += ",\"camera_direction_y\":" + std::to_string(camera_direction.Y);
        metadata += ",\"camera_direction_z\":" + std::to_string(camera_direction.Z);
        const auto runtime_projection_selection =
            mesh_first_select_runtime_triangle_projection_coordinates(ref,
                                                                     ctx,
                                                                     runtime_triangle_cache.triangles,
                                                                     component_to_world,
                                                                     center_ray.location,
                                                                     camera_direction,
                                                                     viewport);
        metadata += ",\"runtime_triangle_projection_mode\":\"" + json_escape(runtime_projection_selection.mode) + "\"";
        metadata += ",\"runtime_triangle_projection_samples\":" + std::to_string(runtime_projection_selection.samples);
        metadata += ",\"runtime_triangle_projection_source_candidates\":" + std::to_string(runtime_projection_selection.source_candidates);
        metadata += ",\"runtime_triangle_projection_project_ok\":" + std::to_string(runtime_projection_selection.project_ok);
        metadata += ",\"runtime_triangle_projection_inside_view\":" + std::to_string(runtime_projection_selection.inside_view);
        metadata += ",\"runtime_triangle_projection_best_score\":" + std::to_string(runtime_projection_selection.best_score);
        metadata += ",\"runtime_triangle_projection_summary\":\"" + json_escape(runtime_projection_selection.summary) + "\"";
        if (runtime_projection_selection.inside_view <= 0)
        {
            return response_json(false,
                                 "runtime_triangle_coordinate_projection_unavailable",
                                 0,
                                 1,
                                 "no runtime triangle coordinate mode projects camera-facing samples into the viewport",
                                 metadata + ",\"replay_blocked\":true");
        }

        MeshFirstPlanStats plan_stats{};
        std::vector<MeshFirstPlanSample> plan_samples{};
        std::string planner_failure{};
        if (!mesh_first_generate_plan_samples_from_runtime_cache(profile_available ? &profile : nullptr,
                                                                 runtime_triangle_cache.triangles,
                                                                 active_texture_size,
                                                                 center_ray.location,
                                                                 camera_direction,
                                                                 region_axis,
                                                                 tuning_coverage_step_texels,
                                                                 tuning_max_strokes,
                                                                 plan_samples,
                                                                 plan_stats,
                                                                 planner_failure))
        {
            metadata += ",";
            metadata += mesh_first_plan_stats_metadata(plan_stats);
            return response_json(false,
                                 planner_failure.empty() ? "planner_build_failed" : planner_failure.c_str(),
                                 0,
                                 1,
                                 "mesh-first planner could not build a safe plan",
                                 metadata + ",\"replay_blocked\":true");
        }
        SdkNativeFrontSampleResult native_front{};
        native_front.mesh = selected_mesh.mesh;
        native_front.viewport_width = viewport.width;
        native_front.viewport_height = viewport.height;
        native_front.sampling_backend = "mesh_first_camera_facing_mesh_source_projection";
        native_front.min_front_hits = std::max(16, std::min(2048, plan_stats.source_samples));
        native_front.target_front_hits = plan_stats.source_samples;
        native_front.hard_attempt_budget = plan_stats.total_samples;
        native_front.samples.reserve(static_cast<std::size_t>(plan_stats.source_samples));
        for (std::size_t sample_index = 0; sample_index < plan_samples.size(); ++sample_index)
        {
            const auto& sample = plan_samples[sample_index];
            if (!sample.source_candidate)
            {
                continue;
            }
            FrontSample front{};
            front.plan_index = static_cast<int>(sample_index);
            front.u = sample.u;
            front.v = sample.v;
            front.roughness = 0.65;
            front.metallic = 0.0;
            front.uv_island = sample.uv_island;
            front.dominant_bone = sample.dominant_bone;
            front.mesh_region = mesh_first_region_code(sample.region);
            front.body_region = sample.body_region;
            front.has_world_position = true;
            front.world_position = sample.world_position;
            front.has_component_position = true;
            front.component_position = sample.local_position;
            native_front.samples.push_back(front);
        }
        if (native_front.samples.empty())
        {
            metadata += ",";
            metadata += mesh_first_plan_stats_metadata(plan_stats);
            return response_json(false,
                                 "planner_source_unavailable",
                                 0,
                                 1,
                                 "mesh-first planner produced no camera-facing source samples",
                                 metadata + ",\"replay_blocked\":true");
        }

        int capture_request_width = std::max(1, viewport.width);
        int capture_request_height = std::max(1, viewport.height);
        constexpr int kMeshFirstCaptureMaxDimension = 4096;
        const int request_max_dimension = std::max(capture_request_width, capture_request_height);
        if (request_max_dimension > kMeshFirstCaptureMaxDimension)
        {
            const double scale = static_cast<double>(kMeshFirstCaptureMaxDimension) / static_cast<double>(request_max_dimension);
            capture_request_width = std::max(1, static_cast<int>(std::round(static_cast<double>(capture_request_width) * scale)));
            capture_request_height = std::max(1, static_cast<int>(std::round(static_cast<double>(capture_request_height) * scale)));
        }

        write_bridge_progress("mesh_basecolor_capture",
                              "Capturing mesh-first source BaseColor",
                              3,
                              4,
                              0.0,
                              "\"source_samples\":" + std::to_string(native_front.samples.size()));
        const auto capture_started = std::chrono::steady_clock::now();
        const auto capture = sdk_capture_front_colors(ref, ctx, native_front, capture_request_width, capture_request_height);
        const auto capture_elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - capture_started).count();
        metadata += sdk_capture_metadata(capture);
        metadata += ",\"mesh_capture_elapsed_ms\":" + std::to_string(capture_elapsed_ms);
        metadata += ",\"mesh_capture_request_width\":" + std::to_string(capture_request_width);
        metadata += ",\"mesh_capture_request_height\":" + std::to_string(capture_request_height);
        if (!capture.bulk_readback_used || !capture.image_bulk_calibration_ok || capture.samples.empty())
        {
            metadata += ",";
            metadata += mesh_first_plan_stats_metadata(plan_stats);
            return response_json(false,
                                 "mesh_source_capture_failed",
                                 0,
                                 1,
                                 "SceneCapture BaseColor bulk capture failed: " + capture.failure,
                                 metadata + ",\"replay_blocked\":true");
        }

        mesh_first_assign_colors(plan_samples,
                                 capture,
                                 enable_front,
                                 enable_side,
                                 enable_back,
                                 plan_stats);
        metadata += ",";
        metadata += mesh_first_plan_stats_metadata(plan_stats);
        metadata += ",\"planner_coverage_step_texels\":" + std::to_string(tuning_coverage_step_texels);
        metadata += ",\"source_distance_policy\":\"camera_projection_pixel\"";
        metadata += ",\"source_projection_color_available\":" + std::string(json_bool(capture.capture_pixels_available));
        metadata += ",\"source_samples\":" + std::to_string(capture.samples.size());
        if (plan_stats.unsafe_enabled > 0)
        {
            return response_json(false,
                                 "planner_blocked",
                                 0,
                                 1,
                                 "mesh-first planner found unsafe color-transfer candidates in enabled regions",
                                 metadata + ",\"replay_blocked\":true");
        }
        if (research_artifacts)
        {
            mesh_first_write_uv_debug_artifacts(plan_samples,
                                                active_texture_size,
                                                enable_front,
                                                enable_side,
                                                enable_back,
                                                metadata);
        }

        sdk::FRuntimeBrushSettings brush{};
        safe_copy(&brush,
                  reinterpret_cast<const void*>(ctx.component + sdk::FieldOffsets::RuntimePaintable_CurrentBrushSettings),
                  sizeof(brush));
        brush.Hardness = 1.0f;
        brush.Opacity = 1.0f;
        const double stroke_radius_texels = tuning_stroke_size_texels;
        const double stroke_radius_uv = stroke_radius_texels / static_cast<double>(std::max(1, active_texture_size));
        brush.Radius = static_cast<float>(stroke_radius_uv);
        brush.Spacing = 1.0f;
        brush.Falloff = sdk::EBrushFalloff::Spherical;
        brush.BlendMode = sdk::EPaintBlendMode::Normal;
        metadata += ",\"stroke_size_texels\":" + std::to_string(tuning_stroke_size_texels);
        metadata += ",\"stroke_radius_texels\":" + std::to_string(stroke_radius_texels);
        metadata += ",\"stroke_radius_uv\":" + std::to_string(stroke_radius_uv);

        std::vector<sdk::FPaintStroke> strokes{};
        strokes.reserve(static_cast<std::size_t>(plan_stats.enabled_samples));
        const bool use_mesh_anchors = false;
        metadata += ",\"replay_anchor_policy\":\"uv_only\"";
        int replay_front = 0;
        int replay_side = 0;
        int replay_back = 0;
        int replay_world_anchors = 0;
        int replay_local_anchors = 0;
        int replay_triangle_anchors = 0;
        for (const auto& sample : plan_samples)
        {
            const bool enabled = (sample.region == MeshFirstRegion::Front && enable_front) ||
                                 (sample.region == MeshFirstRegion::Side && enable_side) ||
                                 (sample.region == MeshFirstRegion::Back && enable_back);
            if (!enabled)
            {
                continue;
            }
            const auto channel = sdk_make_channel(sdk_srgb_to_linear_unit(sample.r),
                                                  sdk_srgb_to_linear_unit(sample.g),
                                                  sdk_srgb_to_linear_unit(sample.b),
                                                  sample.metallic,
                                                  sample.roughness,
                                                  sdk::EPaintChannelApplyMode::Override);
            auto stroke = use_mesh_anchors
                              ? sdk_make_mesh_anchor_stroke(sample.u,
                                                            sample.v,
                                                            channel,
                                                            brush,
                                                            sdk::EPaintChannel::Albedo,
                                                            sample.world_position,
                                                            sample.local_position,
                                                            sample.triangle_index,
                                                            sample.barycentric_a,
                                                            sample.barycentric_b,
                                                            sample.barycentric_c)
                              : sdk_make_uv_stroke(sample.u,
                                                   sample.v,
                                                   channel,
                                                   brush,
                                                   sdk::EPaintChannel::Albedo);
            if (stroke.bHasWorldPosition)
            {
                ++replay_world_anchors;
            }
            if (stroke.bHasLocalPosition)
            {
                ++replay_local_anchors;
            }
            if (stroke.bHasSkeletalTriangleAnchor)
            {
                ++replay_triangle_anchors;
            }
            strokes.push_back(stroke);
            if (sample.region == MeshFirstRegion::Front)
            {
                ++replay_front;
            }
            else if (sample.region == MeshFirstRegion::Side)
            {
                ++replay_side;
            }
            else
            {
                ++replay_back;
            }
        }
        if (strokes.empty())
        {
            return response_json(false,
                                 "mesh_replay_empty",
                                 0,
                                 1,
                                 "mesh-first planner produced no strokes for enabled regions",
                                 metadata + ",\"replay_blocked\":true");
        }
        if (static_cast<int>(strokes.size()) > tuning_max_strokes)
        {
            return response_json(false,
                                 "planner_max_strokes_exceeded",
                                 0,
                                 1,
                                 "mesh-first planner exceeded Max strokes; increase Max strokes or reduce coverage quality",
                                 metadata + ",\"replay_blocked\":true,\"planner_strokes_total\":" + std::to_string(strokes.size()));
        }

        const auto& first_stroke = strokes.front();
        metadata += ",\"first_stroke_u\":" + std::to_string(first_stroke.Uv.X);
        metadata += ",\"first_stroke_v\":" + std::to_string(first_stroke.Uv.Y);
        metadata += ",\"first_stroke_has_world_position\":" + std::string(json_bool(first_stroke.bHasWorldPosition));
        metadata += ",\"first_stroke_has_local_position\":" + std::string(json_bool(first_stroke.bHasLocalPosition));
        metadata += ",\"first_stroke_has_skeletal_triangle_anchor\":" + std::string(json_bool(first_stroke.bHasSkeletalTriangleAnchor));
        metadata += ",\"first_stroke_local_x\":" + std::to_string(first_stroke.LocalPosition.X);
        metadata += ",\"first_stroke_local_y\":" + std::to_string(first_stroke.LocalPosition.Y);
        metadata += ",\"first_stroke_local_z\":" + std::to_string(first_stroke.LocalPosition.Z);
        metadata += ",\"first_stroke_triangle_index\":" + std::to_string(first_stroke.SkeletalTriangleIndex);
        metadata += ",\"first_stroke_barycentric_x\":" + std::to_string(first_stroke.SkeletalTriangleBarycentric.X);
        metadata += ",\"first_stroke_barycentric_y\":" + std::to_string(first_stroke.SkeletalTriangleBarycentric.Y);
        metadata += ",\"first_stroke_barycentric_z\":" + std::to_string(first_stroke.SkeletalTriangleBarycentric.Z);
        metadata += ",\"first_stroke_brush_radius\":" + std::to_string(first_stroke.BrushSettings.Radius);
        metadata += ",\"first_stroke_brush_spacing\":" + std::to_string(first_stroke.BrushSettings.Spacing);
        metadata += ",\"first_stroke_albedo_r\":" + std::to_string(first_stroke.ChannelData.AlbedoColor.R);
        metadata += ",\"first_stroke_albedo_g\":" + std::to_string(first_stroke.ChannelData.AlbedoColor.G);
        metadata += ",\"first_stroke_albedo_b\":" + std::to_string(first_stroke.ChannelData.AlbedoColor.B);
        metadata += ",\"replay_strokes_front\":" + std::to_string(replay_front);
        metadata += ",\"replay_strokes_side\":" + std::to_string(replay_side);
        metadata += ",\"replay_strokes_back\":" + std::to_string(replay_back);
        metadata += ",\"replay_strokes_total\":" + std::to_string(strokes.size());
        metadata += ",\"planner_strokes_front\":" + std::to_string(replay_front);
        metadata += ",\"planner_strokes_side\":" + std::to_string(replay_side);
        metadata += ",\"planner_strokes_back\":" + std::to_string(replay_back);
        metadata += ",\"planner_strokes_total\":" + std::to_string(strokes.size());
        const int estimated_batches = (static_cast<int>(strokes.size()) + std::max(1, tuning_server_batch_limit) - 1) / std::max(1, tuning_server_batch_limit);
        const int estimated_replay_ms = std::max(0, estimated_batches - 1) * std::max(1, tuning_server_batch_delay_ms);
        metadata += ",\"server_batch_estimated_calls\":" + std::to_string(estimated_batches);
        metadata += ",\"estimated_replay_ms\":" + std::to_string(estimated_replay_ms);
        metadata += ",\"skeletal_triangle_anchor_used\":true";
        metadata += ",\"replay_anchor_mode\":\"skeletal_triangle\"";
        metadata += ",\"replay_world_anchors\":" + std::to_string(replay_world_anchors);
        metadata += ",\"replay_local_anchors\":" + std::to_string(replay_local_anchors);
        metadata += ",\"replay_triangle_anchors\":" + std::to_string(replay_triangle_anchors);
        metadata += ",\"server_batch_rpc\":\"ServerPaintBatch\"";
        metadata += ",\"server_paint_batch_used\":true";
        const auto local_paint_at_uv_function = ref.find_function(ctx.component, "PaintAtUVWithBrush");
        metadata += ",\"local_paint_rpc\":\"PaintAtUVWithBrush\"";
        metadata += ",\"local_paint_available\":" + std::string(json_bool(local_paint_at_uv_function != 0));
        metadata += ",\"local_visual_sync_required\":true";
        metadata += ",\"local_visual_sync_after_server_success\":true";
        metadata += ",\"authoritative_replay\":\"server_paint_batch_then_local_visual_sync\"";

        const auto albedo_before = mesh_first_export_channel_checksum(ref, ctx.component, sdk::EPaintChannel::Albedo);
        metadata += ",\"albedo_export_before_ok\":" + std::string(json_bool(albedo_before.ok));
        metadata += ",\"albedo_export_before_bytes\":" + std::to_string(albedo_before.bytes);
        metadata += ",\"albedo_export_before_hash\":\"" + std::to_string(albedo_before.hash) + "\"";
        if (!albedo_before.ok)
        {
            metadata += ",\"albedo_export_before_failure\":\"" + json_escape(albedo_before.failure) + "\"";
        }

        write_bridge_progress("mesh_server_batch_begin",
                              "mesh-first ServerPaintBatch stream prepared",
                              3,
                              4,
                              0.0,
                              "\"server_strokes_total\":" + std::to_string(strokes.size()) +
                                      ",\"server_batch_limit\":" + std::to_string(tuning_server_batch_limit) +
                                      ",\"server_batch_delay_ms\":" + std::to_string(tuning_server_batch_delay_ms));
        const auto replication_before = mesh_first_capture_replication_snapshot(ref, ctx.component);

        if (queued_job)
        {
            auto async_job = std::make_shared<MeshFirstServerBatchAsyncJob>();
            async_job->queued = queued_job;
            async_job->component = ctx.component;
            async_job->server_paint_batch_function = ctx.server_paint_batch_function;
            async_job->local_paint_at_uv_function = local_paint_at_uv_function;
            async_job->strokes = std::move(strokes);
            async_job->metadata = metadata + ",\"server_batch_schedule\":\"timer_drained\"";
            async_job->albedo_before = albedo_before;
            async_job->replication_before = replication_before;
            async_job->server_batch_limit = std::max(1, tuning_server_batch_limit);
            async_job->server_batch_delay_ms = std::max(1, tuning_server_batch_delay_ms);
            async_job->replay_front = replay_front;
            async_job->replay_side = replay_side;
            async_job->replay_back = replay_back;
            async_job->started = std::chrono::steady_clock::now();
            {
                std::lock_guard<std::mutex> lock(g_mesh_first_batch_mutex);
                g_mesh_first_batch_job = async_job;
            }
            if (const auto thread_id = g_game_thread_id.load())
            {
                PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
        }
        return {};
    }

        return response_json(false,
                             "mesh_first_async_required",
                             0,
                             1,
                             "mesh-first paint requires the async queued dispatcher",
                             metadata + ",\"replay_blocked\":true");
    }

    void complete_mesh_first_batch_job(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job, const std::string& response)
    {
        if (!job)
        {
            return;
        }
        if (job->server_batch_timer_id)
        {
            KillTimer(nullptr, job->server_batch_timer_id);
            job->server_batch_timer_id = 0;
        }
        complete_queued_paint_job(job->queued, response);
        {
            std::lock_guard<std::mutex> lock(g_mesh_first_batch_mutex);
            if (g_mesh_first_batch_job == job)
            {
                g_mesh_first_batch_job.reset();
            }
        }
    }

    auto start_mesh_first_paint_async_job(const std::string& request,
                                          const std::shared_ptr<QueuedPaintJob>& queued_job) -> bool
    {
        const auto response = paint_mesh_first_on_game_thread(request, queued_job);
        if (!response.empty())
        {
            complete_queued_paint_job(queued_job, response);
        }
        return true;
    }

    auto tick_mesh_first_batch_async_job() -> void
    {
        std::shared_ptr<MeshFirstServerBatchAsyncJob> job{};
        {
            std::lock_guard<std::mutex> lock(g_mesh_first_batch_mutex);
            job = g_mesh_first_batch_job;
        }
        if (!job)
        {
            return;
        }

        auto clear_server_timer = [&]() {
            if (job->server_batch_timer_id)
            {
                KillTimer(nullptr, job->server_batch_timer_id);
                job->server_batch_timer_id = 0;
            }
        };
        clear_server_timer();

        auto post_next_after = [&](int delay_ms) {
            if (const auto thread_id = g_game_thread_id.load())
            {
                if (delay_ms <= 0)
                {
                    PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
                    return;
                }
                const auto timer_id = SetTimer(nullptr, 0, static_cast<UINT>(std::max(1, delay_ms)), nullptr);
                if (timer_id)
                {
                    job->server_batch_timer_id = timer_id;
                    return;
                }
                PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
            }
        };

        auto elapsed_ms = [&]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->started).count();
        };

        if (job->strokes.empty())
        {
            complete_mesh_first_batch_job(job,
                                          response_json(false,
                                                        "mesh_replay_empty",
                                                        0,
                                                        1,
                                                        "mesh-first async replay has no strokes",
                                                        job->metadata + ",\"replay_blocked\":true"));
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (job->server_next_batch_time.time_since_epoch().count() != 0 &&
            now < job->server_next_batch_time)
        {
            const int remaining_ms = std::max(
                1,
                static_cast<int>(std::ceil(std::chrono::duration<double, std::milli>(
                    job->server_next_batch_time - now).count())));
            post_next_after(remaining_ms);
            return;
        }
        job->server_next_batch_time = {};

        if (job->offset >= job->strokes.size())
        {
            const double server_elapsed_ms = elapsed_ms();
            if (!job->local_sync_started)
            {
                job->local_sync_started = true;
                job->local_sync_started_at = std::chrono::steady_clock::now();
                if (!job->local_paint_at_uv_function)
                {
                    job->local_visual_sync_failure = "PaintAtUVWithBrush_unavailable";
                }
                write_bridge_progress("mesh_local_visual_sync",
                                      "Syncing local visual paint",
                                      99,
                                      100,
                                      server_elapsed_ms,
                                      "\"local_sync_strokes_total\":" + std::to_string(job->strokes.size()) +
                                          ",\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent));
            }

            if (job->local_visual_sync_failure.empty() && job->local_offset < job->strokes.size())
            {
                const std::size_t local_count =
                    std::min<std::size_t>(static_cast<std::size_t>(std::max(1, job->local_visual_sync_batch_limit)),
                                          job->strokes.size() - job->local_offset);
                for (std::size_t index = 0; index < local_count; ++index)
                {
                    std::string local_failure{};
                    ++job->local_stroke_calls;
                    if (!sdk_call_paint_at_uv_with_brush(job->component,
                                                         job->local_paint_at_uv_function,
                                                         job->strokes[job->local_offset],
                                                         local_failure))
                    {
                        ++job->local_stroke_failures;
                        job->local_visual_sync_failure = local_failure.empty() ? "PaintAtUVWithBrush_failed" : local_failure;
                        break;
                    }
                    ++job->local_stroke_success;
                    ++job->local_offset;
                }

                write_bridge_progress("mesh_local_visual_sync",
                                      "Syncing local visual paint",
                                      99,
                                      100,
                                      elapsed_ms(),
                                      "\"local_strokes_synced\":" + std::to_string(job->local_stroke_success) +
                                          ",\"local_strokes_total\":" + std::to_string(job->strokes.size()) +
                                          ",\"local_sync_batch_limit\":" + std::to_string(job->local_visual_sync_batch_limit));

                if (job->local_visual_sync_failure.empty() && job->local_offset < job->strokes.size())
                {
                    post_next_after(0);
                    return;
                }
            }

            const double total_elapsed_ms = elapsed_ms();
            const double local_sync_elapsed_ms =
                job->local_sync_started
                    ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->local_sync_started_at).count()
                    : 0.0;
            const bool local_visual_sync_ok =
                job->local_visual_sync_failure.empty() &&
                job->local_stroke_success == static_cast<int>(job->strokes.size());
            const std::string final_failure = !job->first_failure.empty() ? job->first_failure : job->local_visual_sync_failure;
            std::string metadata = job->metadata;
            metadata += ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls);
            metadata += ",\"server_batch_success\":" + std::to_string(job->server_batch_success);
            metadata += ",\"server_batch_failures\":" + std::to_string(job->server_batch_failures);
            metadata += ",\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent);
            metadata += ",\"server_batch_elapsed_ms\":" + std::to_string(server_elapsed_ms);
            metadata += ",\"local_stroke_calls\":" + std::to_string(job->local_stroke_calls);
            metadata += ",\"local_stroke_success\":" + std::to_string(job->local_stroke_success);
            metadata += ",\"local_stroke_failures\":" + std::to_string(job->local_stroke_failures);
            metadata += ",\"local_visual_sync_used\":" + std::string(json_bool(job->local_paint_at_uv_function != 0));
            metadata += ",\"local_visual_sync_ok\":" + std::string(json_bool(local_visual_sync_ok));
            metadata += ",\"local_visual_sync_failure\":\"" + json_escape(job->local_visual_sync_failure) + "\"";
            metadata += ",\"local_visual_sync_elapsed_ms\":" + std::to_string(local_sync_elapsed_ms);
            metadata += ",\"total_replay_elapsed_ms\":" + std::to_string(total_elapsed_ms);
            metadata += ",\"first_failure\":\"" + json_escape(final_failure) + "\"";

            Reflection ref{};
            std::string init_failure{};
            if (ref.init(init_failure))
            {
                const auto albedo_after = mesh_first_export_channel_checksum(ref, job->component, sdk::EPaintChannel::Albedo);
                metadata += ",\"albedo_export_after_ok\":" + std::string(json_bool(albedo_after.ok));
                metadata += ",\"albedo_export_after_bytes\":" + std::to_string(albedo_after.bytes);
                metadata += ",\"albedo_export_after_hash\":\"" + std::to_string(albedo_after.hash) + "\"";
                metadata += ",\"albedo_export_changed\":" +
                            std::string(json_bool(job->albedo_before.ok && albedo_after.ok && job->albedo_before.hash != albedo_after.hash));
                if (!albedo_after.ok)
                {
                    metadata += ",\"albedo_export_after_failure\":\"" + json_escape(albedo_after.failure) + "\"";
                }
                const auto replication_after = mesh_first_capture_replication_snapshot(ref, job->component);
                metadata += mesh_first_replication_snapshot_metadata("mesh_rep_after", replication_after);
            }
            else
            {
                metadata += ",\"albedo_export_after_ok\":false";
                metadata += ",\"albedo_export_after_failure\":\"reflection_unavailable:" + json_escape(init_failure) + "\"";
                metadata += ",\"albedo_export_changed\":false";
            }
            metadata += mesh_first_replication_snapshot_metadata("mesh_rep_before", job->replication_before);

            write_bridge_progress("mesh_paint_done",
                                  local_visual_sync_ok ? "mesh-first paint completed" : "mesh-first local visual sync failed",
                                  100,
                                  100,
                                  total_elapsed_ms,
                                  "\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent) +
                                      ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls) +
                                      ",\"local_strokes_synced\":" + std::to_string(job->local_stroke_success) +
                                      ",\"front_strokes\":" + std::to_string(job->replay_front) +
                                      ",\"side_strokes\":" + std::to_string(job->replay_side) +
                                      ",\"back_strokes\":" + std::to_string(job->replay_back));
            complete_mesh_first_batch_job(job,
                                          response_json(local_visual_sync_ok,
                                                        local_visual_sync_ok ? "mesh_first_paint_done" : "mesh_local_visual_sync_failed",
                                                        job->server_strokes_sent,
                                                        local_visual_sync_ok ? 0 : 1,
                                                        local_visual_sync_ok
                                                            ? "mesh-first paint dispatched through ServerPaintBatch and local visual sync"
                                                            : "ServerPaintBatch succeeded but local visual sync failed: " + final_failure,
                                                        metadata));
            return;
        }

        const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(std::max(1, job->server_batch_limit)),
                                                        job->strokes.size() - job->offset);
        SdkContext ctx{};
        ctx.component = job->component;
        ctx.server_paint_batch_function = job->server_paint_batch_function;
        std::string batch_failure{};
        ++job->server_batch_calls;
        if (!sdk_call_server_paint_batch(ctx, job->strokes, job->offset, count, batch_failure))
        {
            ++job->server_batch_failures;
            job->first_failure = batch_failure.empty() ? "ServerPaintBatch_failed" : batch_failure;
            const double batch_elapsed_ms = elapsed_ms();
            std::string metadata = job->metadata;
            metadata += ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls);
            metadata += ",\"server_batch_success\":" + std::to_string(job->server_batch_success);
            metadata += ",\"server_batch_failures\":" + std::to_string(job->server_batch_failures);
            metadata += ",\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent);
            metadata += ",\"server_batch_elapsed_ms\":" + std::to_string(batch_elapsed_ms);
            metadata += ",\"first_failure\":\"" + json_escape(job->first_failure) + "\"";
            metadata += mesh_first_replication_snapshot_metadata("mesh_rep_before", job->replication_before);
            Reflection ref{};
            std::string init_failure{};
            if (ref.init(init_failure))
            {
                metadata += mesh_first_replication_snapshot_metadata("mesh_rep_after_failure",
                                                                     mesh_first_capture_replication_snapshot(ref, job->component));
            }
            else
            {
                metadata += ",\"mesh_rep_after_failure_failure\":\"reflection_unavailable:" + json_escape(init_failure) + "\"";
            }
            complete_mesh_first_batch_job(job,
                                          response_json(false,
                                                        "mesh_server_batch_failed",
                                                        job->server_strokes_sent,
                                                        1,
                                                        "ServerPaintBatch failed: " + job->first_failure,
                                                        metadata));
            return;
        }

        ++job->server_batch_success;
        job->server_strokes_sent += static_cast<int>(count);
        job->offset += count;

        const int percent = 80 + static_cast<int>((static_cast<long long>(job->server_strokes_sent) * 19LL) /
                                                  std::max<int>(1, static_cast<int>(job->strokes.size())));
        write_bridge_progress("mesh_server_batch",
                              "mesh-first ServerPaintBatch stream",
                              percent,
                              100,
                              elapsed_ms(),
                              "\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent) +
                                  ",\"server_strokes_total\":" + std::to_string(job->strokes.size()) +
                                  ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls) +
                                  ",\"server_batch_limit\":" + std::to_string(job->server_batch_limit) +
                                  ",\"server_batch_delay_ms\":" + std::to_string(job->server_batch_delay_ms));
        if (job->offset < job->strokes.size())
        {
            job->server_next_batch_time = std::chrono::steady_clock::now() +
                                          std::chrono::milliseconds(std::max(1, job->server_batch_delay_ms));
            post_next_after(job->server_batch_delay_ms);
            return;
        }
        post_next_after(0);
    }

    auto sdk_find_front_mesh(Reflection& ref, const SdkContext& ctx) -> std::uintptr_t
    {
        const auto candidates = sdk_collect_front_mesh_candidates(ref, ctx);
        if (!candidates.empty())
        {
            return candidates.front().mesh;
        }
        return 0;
    }

    auto sdk_find_screen_space_brush_query(Reflection& ref, const SdkContext& ctx) -> std::uintptr_t
    {
        std::uintptr_t fallback = 0;
        std::uintptr_t owned = 0;
        ref.for_each_object([&](std::uintptr_t object) {
            if (!live_uobject(object))
            {
                return false;
            }
            const auto cls = lower_copy(ref.class_name(object));
            const auto name = lower_copy(ref.object_name(object));
            if (!contains_text(cls, "screenspacebrushquery") && !contains_text(name, "screenspacebrushquery"))
            {
                return false;
            }
            if (!ref.find_function(object, "QueryFromWorldRay"))
            {
                return false;
            }
            if (!fallback)
            {
                fallback = object;
            }
            if (sdk_object_is_or_belongs_to(ref, object, ctx.pawn) || sdk_object_is_or_belongs_to(ref, object, ctx.controller))
            {
                owned = object;
                return true;
            }
            return false;
        });
        return owned ? owned : fallback;
    }

    auto sdk_configure_screen_space_brush_query(Reflection& ref, std::uintptr_t query, std::uintptr_t pawn, std::uintptr_t mesh) -> bool
    {
        if (!live_uobject(query) || !live_uobject(pawn))
        {
            return false;
        }
        sdk_call_no_params(ref, query, "ResetFilter");
        sdk_call_no_params(ref, query, "ClearTargetComponents");
        sdk_call_no_params(ref, query, "ClearTargetActors");
        sdk_call_no_params(ref, query, "ClearNoCollisionMeshTargets");
        sdk_call_no_params(ref, query, "ClearIgnoreActors");
        sdk_call_single_number(ref, query, "SetUVChannel", 0.0);
        sdk_call_single_number(ref, query, "SetMaxTraceDistance", 12000.0);
        sdk_call_single_bool(ref, query, "SetTraceComplex", true);
        sdk_call_single_bool(ref, query, "SetAllowNoCollisionMesh", true);
        const bool actor_ok = sdk_call_object_param(ref, query, "AddTargetActor", pawn);
        const bool component_ok = mesh && sdk_call_object_param(ref, query, "AddTargetComponent", mesh);
        const bool no_collision_ok = mesh && sdk_call_object_param(ref, query, "AddNoCollisionMeshTarget", mesh);
        return ref.find_function(query, "QueryFromWorldRay") && (actor_ok || component_ok || no_collision_ok);
    }

    auto sdk_get_viewport_info(Reflection& ref, const SdkContext& ctx) -> SdkViewportInfo
    {
        SdkViewportInfo out{};
        const auto function = ref.find_function(ctx.controller, "GetViewportSize");
        if (!function)
        {
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 1024)
        {
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        std::string failure{};
        if (!process_event(ctx.controller, function, params.data(), failure))
        {
            return out;
        }
        std::vector<int> numeric_values{};
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (name == "returnvalue")
            {
                continue;
            }
            const int value = static_cast<int>(sdk_read_number(ref, prop, params.data()));
            if (value <= 0)
            {
                continue;
            }
            if (contains_text(name, "sizex") || contains_text(name, "width") || name == "x")
            {
                out.width = value;
            }
            else if (contains_text(name, "sizey") || contains_text(name, "height") || name == "y")
            {
                out.height = value;
            }
            numeric_values.push_back(value);
        }
        if ((out.width <= 0 || out.height <= 0) && numeric_values.size() >= 2)
        {
            out.width = numeric_values[0];
            out.height = numeric_values[1];
        }
        return out;
    }

    auto sdk_deproject_screen_position(Reflection& ref, const SdkContext& ctx, double screen_x, double screen_y) -> SdkDeprojectRay
    {
        SdkDeprojectRay out{};
        const auto function = ref.find_function(ctx.controller, "DeprojectScreenPositionToWorld");
        if (!function)
        {
            out.failure = "deproject_function_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 2048)
        {
            out.failure = "deproject_params_size_invalid";
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        int numeric_index = 0;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (name == "returnvalue")
            {
                continue;
            }
            if (strict_vector_struct_type(ref, prop, {"X", "Y", "Z"}, 12))
            {
                continue;
            }
            if (contains_text(name, "screenx") || contains_text(name, "screen_x") || name == "x" || numeric_index == 0)
            {
                write_number(ref, prop, params.data(), screen_x);
                ++numeric_index;
            }
            else if (contains_text(name, "screeny") || contains_text(name, "screen_y") || name == "y" || numeric_index == 1)
            {
                write_number(ref, prop, params.data(), screen_y);
                ++numeric_index;
            }
        }
        std::string failure{};
        if (!process_event(ctx.controller, function, params.data(), failure))
        {
            out.failure = "deproject_process_event_failed:" + failure;
            return out;
        }
        out.ok = read_return_bool(ref, function, params.data());
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (name == "returnvalue")
            {
                continue;
            }
            if (!strict_vector_struct_type(ref, prop, {"X", "Y", "Z"}, 12))
            {
                continue;
            }
            if (contains_text(name, "worldlocation") || contains_text(name, "world_location") || contains_text(name, "location"))
            {
                sdk_read_vector3(ref, prop, params.data(), out.location);
            }
            else if (contains_text(name, "worlddirection") || contains_text(name, "world_direction") || contains_text(name, "direction"))
            {
                sdk::FVector direction{};
                if (sdk_read_vector3(ref, prop, params.data(), direction))
                {
                    out.direction = sdk_vec_normalize(direction);
                }
            }
        }
        if (!out.ok)
        {
            out.failure = "deproject_return_false";
        }
        else if (sdk_vec_len(out.direction) < 0.01)
        {
            out.ok = false;
            out.failure = "deproject_direction_invalid";
        }
        return out;
    }

    struct SdkRecordedStrokeCountParams
    {
        int ReturnValue{0};
    };
    static_assert(sizeof(SdkRecordedStrokeCountParams) == 0x04, "GetRecordedStrokeCount params layout mismatch");

    struct SdkRuntimePaintReplicationPressure
    {
        int QueuedBatchCount{0};
        int QueuedStrokeCount{0};
        int MaxStrokesPerTick{0};
        float EstimatedTicksToDrain{0.0f};
    };
    static_assert(sizeof(SdkRuntimePaintReplicationPressure) == 0x10, "RuntimePaintReplicationPressure layout mismatch");

    struct SdkReplicationManagerGetQueuedStrokeCountParams
    {
        int ReturnValue{0};
    };
    static_assert(sizeof(SdkReplicationManagerGetQueuedStrokeCountParams) == 0x04, "GetQueuedStrokeCount params layout mismatch");

    struct SdkReplicationManagerGetQueuedStrokeCountForComponentParams
    {
        void* PaintComponent{nullptr};
        int ReturnValue{0};
        std::uint8_t Pad_C[0x4]{};
    };
    static_assert(sizeof(SdkReplicationManagerGetQueuedStrokeCountForComponentParams) == 0x10,
                  "GetQueuedStrokeCountForComponent params layout mismatch");

    struct SdkReplicationManagerGetReplicationPressureParams
    {
        SdkRuntimePaintReplicationPressure ReturnValue{};
    };
    static_assert(sizeof(SdkReplicationManagerGetReplicationPressureParams) == 0x10,
                  "GetReplicationPressure params layout mismatch");

    struct SdkReplicationSnapshot
    {
        bool recorded_count_available{false};
        int recorded_count{-1};
        bool manager_available{false};
        std::uintptr_t manager{0};
        bool manager_queued_count_available{false};
        int manager_queued_count{-1};
        bool manager_component_queued_count_available{false};
        int manager_component_queued_count{-1};
        bool manager_pressure_available{false};
        SdkRuntimePaintReplicationPressure pressure{};
        std::string failure{};
    };

    auto sdk_has_replicated_api(const SdkContext& ctx) -> bool
    {
        return ctx.server_paint_batch_function != 0;
    }

    auto sdk_find_replication_manager(Reflection& ref) -> std::uintptr_t
    {
        return ref.find_first_instance("RuntimePaintReplicationManager");
    }

    auto sdk_read_recorded_stroke_count(Reflection& ref, std::uintptr_t component, int& out, std::string& failure) -> bool
    {
        if (!live_uobject(component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        const auto function = ref.find_function(component, "GetRecordedStrokeCount");
        if (!function)
        {
            failure = "GetRecordedStrokeCount_unavailable";
            return false;
        }
        SdkRecordedStrokeCountParams params{};
        if (!process_event(component, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        out = params.ReturnValue;
        return true;
    }

    auto sdk_capture_replication_snapshot(Reflection& ref, std::uintptr_t component) -> SdkReplicationSnapshot
    {
        SdkReplicationSnapshot snapshot{};
        std::string failure{};
        int recorded_count = -1;
        if (sdk_read_recorded_stroke_count(ref, component, recorded_count, failure))
        {
            snapshot.recorded_count_available = true;
            snapshot.recorded_count = recorded_count;
        }
        else if (snapshot.failure.empty())
        {
            snapshot.failure = failure;
        }

        const auto manager = sdk_find_replication_manager(ref);
        snapshot.manager = manager;
        snapshot.manager_available = live_uobject(manager);
        if (!snapshot.manager_available)
        {
            if (snapshot.failure.empty())
            {
                snapshot.failure = "RuntimePaintReplicationManager_unavailable";
            }
            return snapshot;
        }

        if (const auto function = ref.find_function(manager, "GetQueuedStrokeCount"))
        {
            SdkReplicationManagerGetQueuedStrokeCountParams params{};
            failure.clear();
            if (process_event(manager, function, reinterpret_cast<std::uint8_t*>(&params), failure))
            {
                snapshot.manager_queued_count_available = true;
                snapshot.manager_queued_count = params.ReturnValue;
            }
            else if (snapshot.failure.empty())
            {
                snapshot.failure = failure;
            }
        }
        else if (snapshot.failure.empty())
        {
            snapshot.failure = "GetQueuedStrokeCount_unavailable";
        }

        if (const auto function = ref.find_function(manager, "GetQueuedStrokeCountForComponent"))
        {
            SdkReplicationManagerGetQueuedStrokeCountForComponentParams params{};
            params.PaintComponent = reinterpret_cast<void*>(component);
            failure.clear();
            if (process_event(manager, function, reinterpret_cast<std::uint8_t*>(&params), failure))
            {
                snapshot.manager_component_queued_count_available = true;
                snapshot.manager_component_queued_count = params.ReturnValue;
            }
            else if (snapshot.failure.empty())
            {
                snapshot.failure = failure;
            }
        }
        else if (snapshot.failure.empty())
        {
            snapshot.failure = "GetQueuedStrokeCountForComponent_unavailable";
        }

        if (const auto function = ref.find_function(manager, "GetReplicationPressure"))
        {
            SdkReplicationManagerGetReplicationPressureParams params{};
            failure.clear();
            if (process_event(manager, function, reinterpret_cast<std::uint8_t*>(&params), failure))
            {
                snapshot.manager_pressure_available = true;
                snapshot.pressure = params.ReturnValue;
            }
            else if (snapshot.failure.empty())
            {
                snapshot.failure = failure;
            }
        }
        else if (snapshot.failure.empty())
        {
            snapshot.failure = "GetReplicationPressure_unavailable";
        }

        return snapshot;
    }

    auto sdk_replication_snapshot_metadata(const char* prefix, const SdkReplicationSnapshot& snapshot) -> std::string
    {
        std::string key(prefix ? prefix : "replication");
        return ",\"" + key + "_recorded_count_available\":" + json_bool(snapshot.recorded_count_available) +
               ",\"" + key + "_recorded_count\":" + std::to_string(snapshot.recorded_count) +
               ",\"" + key + "_manager_available\":" + json_bool(snapshot.manager_available) +
               ",\"" + key + "_manager\":\"" + hex_address(snapshot.manager) + "\"" +
               ",\"" + key + "_manager_queued_count_available\":" + json_bool(snapshot.manager_queued_count_available) +
               ",\"" + key + "_manager_queued_count\":" + std::to_string(snapshot.manager_queued_count) +
               ",\"" + key + "_manager_component_queued_count_available\":" + json_bool(snapshot.manager_component_queued_count_available) +
               ",\"" + key + "_manager_component_queued_count\":" + std::to_string(snapshot.manager_component_queued_count) +
               ",\"" + key + "_manager_pressure_available\":" + json_bool(snapshot.manager_pressure_available) +
               ",\"" + key + "_queued_batch_count\":" + std::to_string(snapshot.pressure.QueuedBatchCount) +
               ",\"" + key + "_queued_stroke_count\":" + std::to_string(snapshot.pressure.QueuedStrokeCount) +
               ",\"" + key + "_max_strokes_per_tick\":" + std::to_string(snapshot.pressure.MaxStrokesPerTick) +
               ",\"" + key + "_estimated_ticks_to_drain\":" + std::to_string(snapshot.pressure.EstimatedTicksToDrain) +
               ",\"" + key + "_failure\":\"" + json_escape(snapshot.failure) + "\"";
    }

    auto sdk_srgb_to_linear_unit(double value) -> double
    {
        const auto srgb = clamp01(value);
        if (srgb <= 0.04045)
        {
            return srgb / 12.92;
        }
        return std::pow((srgb + 0.055) / 1.055, 2.4);
    }

    auto sdk_make_channel(double r,
                          double g,
                          double b,
                          double metallic,
                          double roughness,
                          sdk::EPaintChannelApplyMode apply_mode) -> sdk::FPaintChannelData
    {
        sdk::FPaintChannelData data{};
        data.AlbedoColor.R = static_cast<float>(clamp01(r));
        data.AlbedoColor.G = static_cast<float>(clamp01(g));
        data.AlbedoColor.B = static_cast<float>(clamp01(b));
        data.AlbedoColor.A = 1.0f;
        data.Metallic = static_cast<float>(clamp01(metallic));
        data.Roughness = static_cast<float>(clamp01(roughness));
        data.Height = 0.0f;
        data.ApplyMode = apply_mode;
        return data;
    }

    auto sdk_make_stroke(double u,
                         double v,
                         const sdk::FPaintChannelData& channel,
                         const sdk::FRuntimeBrushSettings& brush,
                         sdk::EPaintChannel target_channel,
                         const sdk::FVector& world_position) -> sdk::FPaintStroke
    {
        sdk::FPaintStroke stroke{};
        stroke.Uv.X = clamp01(u);
        stroke.Uv.Y = clamp01(v);
        stroke.WorldPosition = world_position;
        stroke.bHasWorldPosition = true;
        stroke.bHasLocalPosition = false;
        stroke.bHasSkeletalTriangleAnchor = false;
        stroke.BrushSettings = brush;
        stroke.ChannelData = channel;
        stroke.TargetChannel = target_channel;
        stroke.EffectiveBrushWorldRadius = brush.Radius;
        stroke.EffectiveSubdivisionLevel = 0;
        stroke.EffectiveSubdivisionPixelSize = 1.0f;
        stroke.EffectiveTemplateResolution = 0;
        stroke.EffectiveMaxGeneratedBrushTriangles = 0;
        stroke.EffectiveGutterExpandPixels = 0;
        return stroke;
    }

    auto sdk_make_uv_stroke(double u,
                            double v,
                            const sdk::FPaintChannelData& channel,
                            const sdk::FRuntimeBrushSettings& brush,
                            sdk::EPaintChannel target_channel) -> sdk::FPaintStroke
    {
        auto stroke = sdk_make_stroke(u, v, channel, brush, target_channel, {});
        stroke.bHasWorldPosition = false;
        stroke.WorldPosition = {};
        return stroke;
    }

    auto sdk_make_mesh_anchor_stroke(double u,
                                     double v,
                                     const sdk::FPaintChannelData& channel,
                                     const sdk::FRuntimeBrushSettings& brush,
                                     sdk::EPaintChannel target_channel,
                                     const sdk::FVector& world_position,
                                     const sdk::FVector& local_position,
                                     int triangle_index,
                                     double barycentric_a,
                                     double barycentric_b,
                                     double barycentric_c) -> sdk::FPaintStroke
    {
        auto stroke = sdk_make_stroke(u, v, channel, brush, target_channel, world_position);
        stroke.bHasWorldPosition = true;
        stroke.WorldPosition = world_position;
        stroke.bHasLocalPosition = true;
        stroke.LocalPosition = local_position;
        stroke.bHasSkeletalTriangleAnchor = true;
        stroke.SkeletalTriangleIndex = std::max(0, triangle_index);
        stroke.SkeletalTriangleBarycentric.X = barycentric_a;
        stroke.SkeletalTriangleBarycentric.Y = barycentric_b;
        stroke.SkeletalTriangleBarycentric.Z = barycentric_c;
        return stroke;
    }

    auto sdk_call_paint_batch_function(std::uintptr_t component,
                                       std::uintptr_t function,
                                       const std::vector<sdk::FPaintStroke>& strokes,
                                       std::size_t offset,
                                       std::size_t count,
                                       std::string& failure) -> bool
    {
        if (!function || count == 0)
        {
            failure = "server_paint_batch_unavailable";
            return false;
        }
        if (!live_uobject(component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        sdk::RuntimePaintableComponent_ServerPaintBatch params{};
        params.Batch.Strokes.Data = const_cast<sdk::FPaintStroke*>(strokes.data() + offset);
        params.Batch.Strokes.Num = static_cast<std::int32_t>(count);
        params.Batch.Strokes.Max = static_cast<std::int32_t>(count);
        if (!process_event(component, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        return true;
    }

    auto sdk_call_server_paint_batch(const SdkContext& ctx,
                                     const std::vector<sdk::FPaintStroke>& strokes,
                                     std::size_t offset,
                                     std::size_t count,
                                     std::string& failure) -> bool
    {
        return sdk_call_paint_batch_function(ctx.component,
                                             ctx.server_paint_batch_function,
                                             strokes,
                                             offset,
                                             count,
                                             failure);
    }

    auto sdk_call_paint_at_uv_with_brush(std::uintptr_t component,
                                         std::uintptr_t function,
                                         const sdk::FPaintStroke& stroke,
                                         std::string& failure) -> bool
    {
        if (!function)
        {
            failure = "PaintAtUVWithBrush_unavailable";
            return false;
        }
        if (!live_uobject(component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        sdk::RuntimePaintableComponent_PaintAtUVWithBrush params{};
        params.Uv = stroke.Uv;
        params.ChannelData = stroke.ChannelData;
        params.BrushSettings = stroke.BrushSettings;
        params.Channel = stroke.TargetChannel;
        return process_event(component, function, reinterpret_cast<std::uint8_t*>(&params), failure);
    }

    struct SdkFrontColorStats
    {
        int count{0};
        double min_rgb{0.0};
        double max_rgb{0.0};
        double avg_rgb{0.0};
        int whiteish_samples{0};
        bool all_whiteish{false};
    };

    auto sdk_front_color_stats(const std::vector<sdk::FPaintStroke>& strokes) -> SdkFrontColorStats
    {
        SdkFrontColorStats stats{};
        stats.count = static_cast<int>(strokes.size());
        bool initialized = false;
        double sum = 0.0;
        int channels = 0;
        for (const auto& stroke : strokes)
        {
            const double values[]{
                stroke.ChannelData.AlbedoColor.R,
                stroke.ChannelData.AlbedoColor.G,
                stroke.ChannelData.AlbedoColor.B,
            };
            bool sample_whiteish = true;
            for (const auto value : values)
            {
                if (!initialized)
                {
                    stats.min_rgb = value;
                    stats.max_rgb = value;
                    initialized = true;
                }
                stats.min_rgb = std::min(stats.min_rgb, value);
                stats.max_rgb = std::max(stats.max_rgb, value);
                sum += value;
                ++channels;
                if (value < 0.97)
                {
                    sample_whiteish = false;
                }
            }
            if (sample_whiteish)
            {
                ++stats.whiteish_samples;
            }
        }
        stats.avg_rgb = channels > 0 ? sum / static_cast<double>(channels) : 0.0;
        stats.all_whiteish = stats.count > 0 && stats.whiteish_samples == stats.count;
        return stats;
    }

    auto sdk_front_color_metadata(const SdkFrontColorStats& stats) -> std::string
    {
        return ",\"front_rgb_count\":" + std::to_string(stats.count) +
               ",\"front_rgb_min\":" + std::to_string(stats.min_rgb) +
               ",\"front_rgb_max\":" + std::to_string(stats.max_rgb) +
               ",\"front_rgb_avg\":" + std::to_string(stats.avg_rgb) +
               ",\"front_rgb_whiteish_samples\":" + std::to_string(stats.whiteish_samples) +
               ",\"front_rgb_all_whiteish\":" + json_bool(stats.all_whiteish);
    }

    auto sdk_function_caller(Reflection& ref, std::uintptr_t function) -> std::uintptr_t
    {
        const auto owner_class = safe_read<std::uintptr_t>(function + OffOuter);
        if (!owner_class)
        {
            return 0;
        }
        std::uintptr_t fallback = 0;
        std::uintptr_t cdo = 0;
        ref.for_each_object([&](std::uintptr_t object) {
            if (!object || address_in_main_module(object))
            {
                return false;
            }
            if (safe_read<std::uintptr_t>(object + OffClass) != owner_class)
            {
                return false;
            }
            if (!fallback)
            {
                fallback = object;
            }
            if ((safe_read<std::uint32_t>(object + OffObjectFlags, 0) & RFClassDefaultObject) != 0)
            {
                cdo = object;
                return true;
            }
            return false;
        });
        return cdo ? cdo : (fallback ? fallback : owner_class);
    }

    auto sdk_read_return_object_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params) -> std::uintptr_t
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            if (lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName))) == "returnvalue")
            {
                return sdk_read_object(prop, params);
            }
        }
        return 0;
    }

    auto sdk_read_return_number_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params, double& value) -> bool
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            if (lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName))) == "returnvalue")
            {
                value = sdk_read_number(ref, prop, params);
                return std::isfinite(value);
            }
        }
        return false;
    }

    auto sdk_read_rotator(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, sdk::FRotator& out) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref, prop, {"Pitch", "Yaw", "Roll"});
        if (st)
        {
            bool read = false;
            if (const auto p = find_property_any(ref, st, {"Pitch"})) { out.Pitch = sdk_read_number(ref, p, base); read = true; }
            if (const auto p = find_property_any(ref, st, {"Yaw"})) { out.Yaw = sdk_read_number(ref, p, base); read = true; }
            if (const auto p = find_property_any(ref, st, {"Roll"})) { out.Roll = sdk_read_number(ref, p, base); read = true; }
            return read && std::isfinite(out.Pitch) && std::isfinite(out.Yaw) && std::isfinite(out.Roll);
        }
        const auto size = prop_element_size(prop);
        if (size >= 24)
        {
            const auto* values = reinterpret_cast<double*>(base);
            out = {values[0], values[1], values[2]};
            return true;
        }
        if (size >= 12)
        {
            const auto* values = reinterpret_cast<float*>(base);
            out = {values[0], values[1], values[2]};
            return true;
        }
        return false;
    }

    auto sdk_read_quat(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, sdk::FQuat& out) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref, prop, {"X", "Y", "Z", "W"});
        if (st)
        {
            bool read = false;
            if (const auto p = find_property_any(ref, st, {"X"})) { out.X = sdk_read_number(ref, p, base); read = true; }
            if (const auto p = find_property_any(ref, st, {"Y"})) { out.Y = sdk_read_number(ref, p, base); read = true; }
            if (const auto p = find_property_any(ref, st, {"Z"})) { out.Z = sdk_read_number(ref, p, base); read = true; }
            if (const auto p = find_property_any(ref, st, {"W"})) { out.W = sdk_read_number(ref, p, base); read = true; }
            return read && std::isfinite(out.X) && std::isfinite(out.Y) && std::isfinite(out.Z) && std::isfinite(out.W);
        }
        const auto size = prop_element_size(prop);
        if (size >= 32)
        {
            const auto* values = reinterpret_cast<double*>(base);
            out = {values[0], values[1], values[2], values[3]};
            return true;
        }
        if (size >= 16)
        {
            const auto* values = reinterpret_cast<float*>(base);
            out = {values[0], values[1], values[2], values[3]};
            return true;
        }
        return false;
    }

    auto sdk_read_transform(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, sdk::FTransform& out) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref, prop, {"Rotation", "Translation", "Scale3D"});
        if (st)
        {
            out = mesh_first_identity_transform();
            bool read = false;
            if (const auto p = find_property_any(ref, st, {"Rotation"}))
            {
                read = sdk_read_quat(ref, p, base, out.Rotation) || read;
            }
            if (const auto p = find_property_any(ref, st, {"Translation", "Location"}))
            {
                read = sdk_read_vector3(ref, p, base, out.Translation) || read;
            }
            if (const auto p = find_property_any(ref, st, {"Scale3D", "Scale"}))
            {
                read = sdk_read_vector3(ref, p, base, out.Scale3D) || read;
            }
            return read && sdk_transform_finite(out);
        }
        const auto size = prop_element_size(prop);
        if (size >= static_cast<int>(sizeof(sdk::FTransform)))
        {
            return safe_copy(&out, base, sizeof(out)) && sdk_transform_finite(out);
        }
        return false;
    }

    auto sdk_read_return_vector3_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params, sdk::FVector& value) -> bool
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            if (lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName))) == "returnvalue")
            {
                return sdk_read_vector3(ref, prop, params, value);
            }
        }
        return false;
    }

    auto sdk_read_return_transform_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params, sdk::FTransform& value) -> bool
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            if (lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName))) == "returnvalue")
            {
                return sdk_read_transform(ref, prop, params, value);
            }
        }
        return false;
    }

    auto sdk_read_return_rotator_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params, sdk::FRotator& value) -> bool
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            if (lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName))) == "returnvalue")
            {
                return sdk_read_rotator(ref, prop, params, value);
            }
        }
        return false;
    }

    auto sdk_call_no_params_return_object(Reflection& ref, std::uintptr_t object, const char* function_name, std::string& failure) -> std::uintptr_t
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            failure = std::string(function_name) + "_unavailable";
            return 0;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 4096)
        {
            failure = std::string(function_name) + "_params_size_invalid";
            return 0;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(0, params_size)), 0);
        if (!process_event(object, function, params.data(), failure))
        {
            failure = std::string(function_name) + "_process_event_failed:" + failure;
            return 0;
        }
        return sdk_read_return_object_param(ref, function, params.data());
    }

    auto sdk_call_no_params_return_number(Reflection& ref, std::uintptr_t object, const char* function_name, double& value) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 4096)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(0, params_size)), 0);
        std::string failure{};
        return process_event(object, function, params.data(), failure) &&
               sdk_read_return_number_param(ref, function, params.data(), value);
    }

    auto sdk_call_no_params_return_vector3(Reflection& ref, std::uintptr_t object, const char* function_name, sdk::FVector& value) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 4096)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(0, params_size)), 0);
        std::string failure{};
        return process_event(object, function, params.data(), failure) &&
               sdk_read_return_vector3_param(ref, function, params.data(), value);
    }

    auto sdk_call_no_params_return_rotator(Reflection& ref, std::uintptr_t object, const char* function_name, sdk::FRotator& value) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 4096)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(0, params_size)), 0);
        std::string failure{};
        return process_event(object, function, params.data(), failure) &&
               sdk_read_return_rotator_param(ref, function, params.data(), value);
    }

    auto sdk_call_no_params_return_transform(Reflection& ref, std::uintptr_t object, const char* function_name, sdk::FTransform& value) -> bool
    {
        const auto function = ref.find_function(object, function_name);
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 4096)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(0, params_size)), 0);
        std::string failure{};
        return process_event(object, function, params.data(), failure) &&
               sdk_read_return_transform_param(ref, function, params.data(), value);
    }

    auto sdk_write_object_property_by_name(Reflection& ref, std::uintptr_t object, const char* name, std::uintptr_t value) -> bool
    {
        for (auto cls = ref.class_ptr(object); cls; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
        {
            const auto prop = ref.find_property(cls, name);
            const auto offset = prop ? prop_offset(prop) : -1;
            if (offset < 0)
            {
                continue;
            }
            __try
            {
                *reinterpret_cast<std::uintptr_t*>(object + static_cast<std::uintptr_t>(offset)) = value;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
        return false;
    }

    auto sdk_write_number_property_by_name(Reflection& ref, std::uintptr_t object, const char* name, double value) -> bool
    {
        for (auto cls = ref.class_ptr(object); cls; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
        {
            const auto prop = ref.find_property(cls, name);
            if (prop && prop_offset(prop) >= 0)
            {
                return write_number(ref, prop, reinterpret_cast<std::uint8_t*>(object), value);
            }
        }
        return false;
    }

    auto sdk_write_enum_byte(Reflection&, std::uintptr_t prop, std::uint8_t* container, std::uint8_t value) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        __try
        {
            *(container + offset) = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    auto sdk_write_enum_property_by_name(Reflection& ref, std::uintptr_t object, const char* name, std::uint8_t value) -> bool
    {
        for (auto cls = ref.class_ptr(object); cls; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
        {
            const auto prop = ref.find_property(cls, name);
            if (prop && prop_offset(prop) >= 0)
            {
                return sdk_write_enum_byte(ref, prop, reinterpret_cast<std::uint8_t*>(object), value);
            }
        }
        return false;
    }

    auto sdk_write_bool_property_by_name(Reflection&, std::uintptr_t object, const char* name, bool value) -> bool
    {
        Reflection ref{};
        std::string ignored{};
        if (!ref.init(ignored))
        {
            return false;
        }
        for (auto cls = ref.class_ptr(object); cls; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
        {
            const auto prop = ref.find_property(cls, name);
            if (prop && prop_offset(prop) >= 0)
            {
                return write_bool(prop, reinterpret_cast<std::uint8_t*>(object), value);
            }
        }
        return false;
    }

    auto sdk_write_quat_identity(Reflection& ref, std::uintptr_t prop, std::uint8_t* container) -> bool
    {
        const auto offset = prop_offset(prop);
        const auto structure = struct_type(ref, prop, {"X", "Y", "Z", "W"});
        if (offset < 0 || !structure)
        {
            return false;
        }
        auto* base = container + offset;
        bool wrote = false;
        if (const auto p = find_property_any(ref, structure, {"X"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, structure, {"Y"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, structure, {"Z"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, structure, {"W"})) wrote = write_number(ref, p, base, 1.0) || wrote;
        return wrote;
    }

    auto sdk_write_transform(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const sdk::FVector& location) -> bool
    {
        const auto offset = prop_offset(prop);
        const auto structure = struct_type(ref, prop, {"Rotation", "Translation", "Scale3D"});
        if (offset < 0 || !structure)
        {
            return false;
        }
        auto* base = container + offset;
        bool wrote = false;
        if (const auto p = find_property_any(ref, structure, {"Rotation"}))
        {
            wrote = sdk_write_quat_identity(ref, p, base) || wrote;
        }
        if (const auto p = find_property_any(ref, structure, {"Translation", "Location"}))
        {
            wrote = sdk_write_vector3(ref, p, base, location) || wrote;
        }
        if (const auto p = find_property_any(ref, structure, {"Scale3D", "Scale"}))
        {
            sdk::FVector scale{};
            scale.X = 1.0;
            scale.Y = 1.0;
            scale.Z = 1.0;
            wrote = sdk_write_vector3(ref, p, base, scale) || wrote;
        }
        return wrote;
    }

    auto sdk_write_rotator(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const sdk::FVector& direction) -> bool
    {
        const auto offset = prop_offset(prop);
        const auto structure = struct_type(ref, prop, {"Pitch", "Yaw", "Roll"});
        if (offset < 0 || !structure)
        {
            return false;
        }
        auto* base = container + offset;
        const auto horizontal = std::sqrt(direction.X * direction.X + direction.Y * direction.Y);
        const auto pitch = std::atan2(direction.Z, std::max(0.000001, horizontal)) * 180.0 / 3.14159265358979323846;
        const auto yaw = std::atan2(direction.Y, direction.X) * 180.0 / 3.14159265358979323846;
        bool wrote = false;
        if (const auto p = find_property_any(ref, structure, {"Pitch"})) wrote = write_number(ref, p, base, pitch) || wrote;
        if (const auto p = find_property_any(ref, structure, {"Yaw"})) wrote = write_number(ref, p, base, yaw) || wrote;
        if (const auto p = find_property_any(ref, structure, {"Roll"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        return wrote;
    }

    auto sdk_make_rotator(const sdk::FVector& direction) -> sdk::FRotator
    {
        const auto horizontal = std::sqrt(direction.X * direction.X + direction.Y * direction.Y);
        sdk::FRotator rot{};
        rot.Pitch = std::atan2(direction.Z, std::max(0.000001, horizontal)) * 180.0 / 3.14159265358979323846;
        rot.Yaw = std::atan2(direction.Y, direction.X) * 180.0 / 3.14159265358979323846;
        rot.Roll = 0.0;
        return rot;
    }

    auto sdk_rotator_forward(const sdk::FRotator& rotator) -> sdk::FVector
    {
        const auto pitch = rotator.Pitch * 3.14159265358979323846 / 180.0;
        const auto yaw = rotator.Yaw * 3.14159265358979323846 / 180.0;
        const auto cp = std::cos(pitch);
        return sdk_vec_normalize({cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch)});
    }

    auto sdk_make_transform(const sdk::FVector& location) -> sdk::FTransform
    {
        sdk::FTransform transform{};
        transform.Rotation.W = 1.0;
        transform.Translation = location;
        transform.Scale3D.X = 1.0;
        transform.Scale3D.Y = 1.0;
        transform.Scale3D.Z = 1.0;
        return transform;
    }

    auto sdk_create_render_target(Reflection& ref, const SdkContext& ctx, int width, int height, std::string& failure) -> std::uintptr_t
    {
        const auto function = sdk_find_object_named(ref, "CreateRenderTarget2D");
        const auto caller = sdk_function_caller(ref, function);
        if (!function || !caller)
        {
            failure = "create_render_target_function_unavailable";
            return 0;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 4096)
        {
            failure = "create_render_target_params_size_invalid";
            return 0;
        }
        if (params_size != static_cast<int>(sizeof(sdk::KismetRenderingLibrary_CreateRenderTarget2D)))
        {
            failure = "create_render_target_typed_params_size_mismatch";
            return 0;
        }
        sdk::KismetRenderingLibrary_CreateRenderTarget2D params{};
        params.WorldContextObject = reinterpret_cast<void*>(ctx.pawn);
        params.Width = width;
        params.Height = height;
        params.Format = sdk::ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
        params.ClearColor.R = 0.0f;
        params.ClearColor.G = 0.0f;
        params.ClearColor.B = 0.0f;
        params.ClearColor.A = 1.0f;
        params.bAutoGenerateMipMaps = false;
        params.bSupportUAVs = false;
        if (!process_event(caller, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            failure = "create_render_target_process_event_failed:" + failure;
            return 0;
        }
        const auto rt = reinterpret_cast<std::uintptr_t>(params.ReturnValue);
        if (!rt)
        {
            failure = "create_render_target_return_null";
        }
        return rt;
    }

    auto sdk_spawn_actor_from_class(Reflection& ref,
                                    const SdkContext& ctx,
                                    std::uintptr_t actor_class,
                                    const sdk::FVector& location,
                                    std::string& failure) -> std::uintptr_t
    {
        const auto function = sdk_find_object_named(ref, "BeginDeferredActorSpawnFromClass");
        const auto caller = sdk_function_caller(ref, function);
        if (!function || !caller || !actor_class)
        {
            failure = "begin_deferred_spawn_function_unavailable";
            return 0;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size != static_cast<int>(sizeof(sdk::GameplayStatics_BeginDeferredActorSpawnFromClass)))
        {
            failure = "begin_deferred_spawn_typed_params_size_mismatch";
            return 0;
        }
        const auto transform = sdk_make_transform(location);
        sdk::GameplayStatics_BeginDeferredActorSpawnFromClass params{};
        params.WorldContextObject = reinterpret_cast<void*>(ctx.pawn);
        params.ActorClass = reinterpret_cast<void*>(actor_class);
        params.SpawnTransform = transform;
        params.CollisionHandlingOverride = sdk::ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        params.Owner = reinterpret_cast<void*>(ctx.pawn);
        params.TransformScaleMethod = sdk::ESpawnActorScaleMethod::SelectDefaultAtRuntime;
        std::string begin_failure{};
        if (!process_event(caller, function, reinterpret_cast<std::uint8_t*>(&params), begin_failure) || !params.ReturnValue)
        {
            failure = "begin_deferred_spawn_process_event_failed:" + begin_failure;
            return 0;
        }

        auto actor = reinterpret_cast<std::uintptr_t>(params.ReturnValue);
        const auto finish = sdk_find_object_named(ref, "FinishSpawningActor");
        const auto finish_caller = sdk_function_caller(ref, finish);
        const auto finish_size = safe_read<int>(finish + OffPropertiesSize, 0);
        if (finish && finish_caller && finish_size == static_cast<int>(sizeof(sdk::GameplayStatics_FinishSpawningActor)))
        {
            sdk::GameplayStatics_FinishSpawningActor finish_params{};
            finish_params.Actor = reinterpret_cast<void*>(actor);
            finish_params.SpawnTransform = transform;
            finish_params.TransformScaleMethod = sdk::ESpawnActorScaleMethod::SelectDefaultAtRuntime;
            std::string finish_failure{};
            if (process_event(finish_caller, finish, reinterpret_cast<std::uint8_t*>(&finish_params), finish_failure) && finish_params.ReturnValue)
            {
                actor = reinterpret_cast<std::uintptr_t>(finish_params.ReturnValue);
            }
        }
        failure.clear();
        return actor;
    }

    auto sdk_set_actor_capture_transform(Reflection& ref,
                                         std::uintptr_t actor,
                                         const sdk::FVector& location,
                                         const sdk::FVector& direction) -> bool
    {
        bool ok = false;
        if (const auto function = ref.find_function(actor, "K2_SetActorLocation"))
        {
            const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
            if (params_size == static_cast<int>(sizeof(sdk::Actor_K2_SetActorLocation)))
            {
                sdk::Actor_K2_SetActorLocation params{};
                params.NewLocation = location;
                params.bSweep = false;
                params.bTeleport = true;
                std::string failure{};
                ok = process_event(actor, function, reinterpret_cast<std::uint8_t*>(&params), failure) || ok;
            }
        }
        if (const auto function = ref.find_function(actor, "K2_SetActorRotation"))
        {
            const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
            if (params_size == static_cast<int>(sizeof(sdk::Actor_K2_SetActorRotation)))
            {
                sdk::Actor_K2_SetActorRotation params{};
                params.NewRotation = sdk_make_rotator(direction);
                params.bTeleportPhysics = true;
                std::string failure{};
                ok = process_event(actor, function, reinterpret_cast<std::uint8_t*>(&params), failure) || ok;
            }
        }
        return ok;
    }

    auto sdk_project_world_to_screen(Reflection& ref,
                                     const SdkContext& ctx,
                                     const sdk::FVector& world,
                                     double& x,
                                     double& y) -> bool
    {
        const auto function = ref.find_function(ctx.controller, "ProjectWorldLocationToScreen");
        if (!function)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 4096)
        {
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (name == "returnvalue")
            {
                continue;
            }
            if ((contains_text(name, "world") || contains_text(name, "location")) && !contains_text(name, "screen"))
            {
                sdk_write_vector3(ref, prop, params.data(), world);
            }
            else if (contains_text(name, "viewport"))
            {
                write_bool(prop, params.data(), false);
            }
        }
        std::string failure{};
        if (!process_event(ctx.controller, function, params.data(), failure) || !read_return_bool(ref, function, params.data()))
        {
            return false;
        }
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (name == "returnvalue")
            {
                continue;
            }
            if (contains_text(name, "screen"))
            {
                return sdk_read_vector2(ref, prop, params.data(), x, y);
            }
        }
        return false;
    }

    auto sdk_read_return_linear_color(Reflection& ref, std::uintptr_t function, std::uint8_t* params, Color& color) -> bool
    {
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            if (lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName))) != "returnvalue")
            {
                continue;
            }
            const auto offset = prop_offset(prop);
            const auto structure = struct_type(ref, prop, {"R", "G", "B", "A"});
            if (offset < 0 || !structure)
            {
                return false;
            }
            auto* base = params + offset;
            bool read = false;
            if (const auto p = find_property_any(ref, structure, {"R"})) { color.r = sdk_read_number(ref, p, base); read = true; }
            if (const auto p = find_property_any(ref, structure, {"G"})) { color.g = sdk_read_number(ref, p, base); read = true; }
            if (const auto p = find_property_any(ref, structure, {"B"})) { color.b = sdk_read_number(ref, p, base); read = true; }
            color.r = clamp01(color.r);
            color.g = clamp01(color.g);
            color.b = clamp01(color.b);
            color.roughness = 0.65;
            color.metallic = 0.0;
            return read;
        }
        return false;
    }

    auto sdk_read_render_target_raw_pixel(Reflection& ref,
                                          const SdkContext& ctx,
                                          std::uintptr_t render_target,
                                          int x,
                                          int y,
                                          Color& color,
                                          std::uintptr_t& function_used) -> bool
    {
        const auto function = sdk_find_object_named(ref, "ReadRenderTargetPixel");
        const auto caller = sdk_function_caller(ref, function);
        function_used = function;
        if (!function || !caller || !render_target)
        {
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size != static_cast<int>(sizeof(sdk::KismetRenderingLibrary_ReadRenderTargetPixel)))
        {
            return false;
        }
        sdk::KismetRenderingLibrary_ReadRenderTargetPixel params{};
        params.WorldContextObject = reinterpret_cast<void*>(ctx.pawn);
        params.TextureRenderTarget = reinterpret_cast<void*>(render_target);
        params.X = x;
        params.Y = y;
        std::string failure{};
        if (!process_event(caller, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        color.r = static_cast<double>(params.ReturnValue.R) / 255.0;
        color.g = static_cast<double>(params.ReturnValue.G) / 255.0;
        color.b = static_cast<double>(params.ReturnValue.B) / 255.0;
        color.roughness = 0.65;
        color.metallic = 0.0;
        return true;
    }

    struct SdkBulkRenderTargetImage
    {
        bool ok{false};
        std::string failure{"bulk_read_not_run"};
        std::string backend{"not_run"};
        std::string function_name{};
        std::string inner_type{};
        std::string bool_variant{"none"};
        int width{0};
        int height{0};
        int decoded_pixels{0};
        std::vector<Color> pixels{};
    };

    struct SdkBulkReadbackDiagnostics
    {
        int function_attempts{0};
        int process_event_ok{0};
        int array_param_count{0};
        int first_array_offset{-1};
        int first_array_num{0};
        int first_array_max{0};
        int first_array_element_size{0};
        std::string first_candidate_type{"none"};
        int decoded_pixels{0};
    };

    auto sdk_color_distance_rgb(const Color& a, const Color& b) -> double
    {
        return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
    }

    auto sdk_median(std::vector<double> values) -> double
    {
        if (values.empty())
        {
            return 1000000.0;
        }
        const auto mid = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
        return values[mid];
    }

    enum class SdkBulkColorTransform
    {
        Identity,
        SwapRedBlue,
        SrgbToLinear,
        LinearToSrgb,
        SwapRedBlueSrgbToLinear,
        SwapRedBlueLinearToSrgb,
    };

    auto sdk_srgb_to_linear_component(double value) -> double
    {
        value = clamp01(value);
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    }

    auto sdk_linear_to_srgb_component(double value) -> double
    {
        value = clamp01(value);
        return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    }

    auto sdk_bulk_color_transform_label(SdkBulkColorTransform transform) -> const char*
    {
        switch (transform)
        {
        case SdkBulkColorTransform::Identity: return "identity";
        case SdkBulkColorTransform::SwapRedBlue: return "swap_rb";
        case SdkBulkColorTransform::SrgbToLinear: return "srgb_to_linear";
        case SdkBulkColorTransform::LinearToSrgb: return "linear_to_srgb";
        case SdkBulkColorTransform::SwapRedBlueSrgbToLinear: return "swap_rb_srgb_to_linear";
        case SdkBulkColorTransform::SwapRedBlueLinearToSrgb: return "swap_rb_linear_to_srgb";
        }
        return "unknown";
    }

    auto sdk_apply_bulk_color_transform(Color color, SdkBulkColorTransform transform) -> Color
    {
        const auto swap_rb = [&]() {
            std::swap(color.r, color.b);
        };
        const auto srgb_to_linear = [&]() {
            color.r = sdk_srgb_to_linear_component(color.r);
            color.g = sdk_srgb_to_linear_component(color.g);
            color.b = sdk_srgb_to_linear_component(color.b);
        };
        const auto linear_to_srgb = [&]() {
            color.r = sdk_linear_to_srgb_component(color.r);
            color.g = sdk_linear_to_srgb_component(color.g);
            color.b = sdk_linear_to_srgb_component(color.b);
        };
        switch (transform)
        {
        case SdkBulkColorTransform::Identity: break;
        case SdkBulkColorTransform::SwapRedBlue: swap_rb(); break;
        case SdkBulkColorTransform::SrgbToLinear: srgb_to_linear(); break;
        case SdkBulkColorTransform::LinearToSrgb: linear_to_srgb(); break;
        case SdkBulkColorTransform::SwapRedBlueSrgbToLinear: swap_rb(); srgb_to_linear(); break;
        case SdkBulkColorTransform::SwapRedBlueLinearToSrgb: swap_rb(); linear_to_srgb(); break;
        }
        color.r = clamp01(color.r);
        color.g = clamp01(color.g);
        color.b = clamp01(color.b);
        return color;
    }

    auto sdk_allowed_bulk_color_transforms(const std::string& inner_type) -> std::vector<SdkBulkColorTransform>
    {
        if (inner_type == "FLinearColor")
        {
            return {SdkBulkColorTransform::LinearToSrgb,
                    SdkBulkColorTransform::SwapRedBlueLinearToSrgb};
        }
        return {SdkBulkColorTransform::Identity,
                SdkBulkColorTransform::SwapRedBlue};
    }

    auto sdk_decode_bulk_array_candidates(const std::string& backend,
                                          const std::string& function_name,
                                          const std::string& bool_variant,
                                          std::uintptr_t data,
                                          int num,
                                          int max,
                                          int width,
                                          int height) -> std::vector<SdkBulkRenderTargetImage>
    {
        std::vector<SdkBulkRenderTargetImage> out{};
        const auto expected = width > 0 && height > 0 ? width * height : 0;
        if (!data || expected <= 0 || num <= 0 || max < num)
        {
            return out;
        }
        auto make_base = [&]() {
            SdkBulkRenderTargetImage image{};
            image.ok = true;
            image.backend = backend;
            image.function_name = function_name;
            image.bool_variant = bool_variant;
            image.width = width;
            image.height = height;
            image.decoded_pixels = expected;
            image.failure.clear();
            return image;
        };
        const auto is_raw_function = contains_text(lower_copy(function_name), "raw");
        if (num == expected)
        {
            if (!is_raw_function)
            {
                std::vector<sdk::FColor> raw(static_cast<std::size_t>(expected));
                if (safe_copy(raw.data(), reinterpret_cast<void*>(data), raw.size() * sizeof(sdk::FColor)))
                {
                    auto image = make_base();
                    image.inner_type = "FColor";
                    image.pixels.reserve(raw.size());
                    for (const auto& px : raw)
                    {
                        Color c{};
                        c.r = static_cast<double>(px.R) / 255.0;
                        c.g = static_cast<double>(px.G) / 255.0;
                        c.b = static_cast<double>(px.B) / 255.0;
                        c.roughness = 0.65;
                        c.metallic = 0.0;
                        image.pixels.push_back(c);
                    }
                    out.push_back(std::move(image));
                }
            }
            if (is_raw_function)
            {
                std::vector<sdk::FLinearColor> raw(static_cast<std::size_t>(expected));
                if (safe_copy(raw.data(), reinterpret_cast<void*>(data), raw.size() * sizeof(sdk::FLinearColor)))
                {
                    auto image = make_base();
                    image.inner_type = "FLinearColor";
                    image.pixels.reserve(raw.size());
                    for (const auto& px : raw)
                    {
                        Color c{};
                        c.r = clamp01(px.R);
                        c.g = clamp01(px.G);
                        c.b = clamp01(px.B);
                        c.roughness = 0.65;
                        c.metallic = 0.0;
                        image.pixels.push_back(c);
                    }
                    out.push_back(std::move(image));
                }
            }
        }
        if (num == expected * 4)
        {
            std::vector<std::uint8_t> raw(static_cast<std::size_t>(num));
            if (safe_copy(raw.data(), reinterpret_cast<void*>(data), raw.size()))
            {
                auto image = make_base();
                image.inner_type = "uint8_bgra";
                image.pixels.reserve(static_cast<std::size_t>(expected));
                for (int i = 0; i < expected; ++i)
                {
                    const auto offset = static_cast<std::size_t>(i) * 4;
                    Color c{};
                    c.b = static_cast<double>(raw[offset + 0]) / 255.0;
                    c.g = static_cast<double>(raw[offset + 1]) / 255.0;
                    c.r = static_cast<double>(raw[offset + 2]) / 255.0;
                    c.roughness = 0.65;
                    c.metallic = 0.0;
                    image.pixels.push_back(c);
                }
                out.push_back(std::move(image));
            }
        }
        return out;
    }

    auto sdk_read_render_target_bulk_candidates(Reflection& ref,
                                                const SdkContext& ctx,
                                                std::uintptr_t render_target,
                                                int width,
                                                int height,
                                                SdkBulkReadbackDiagnostics* diagnostics = nullptr) -> std::vector<SdkBulkRenderTargetImage>
    {
        std::vector<SdkBulkRenderTargetImage> out{};
        const auto expected_pixels = width > 0 && height > 0 ? width * height : 0;
        const char* function_names[]{"ReadRenderTarget", "ReadRenderTargetRaw"};
        for (const auto* function_name : function_names)
        {
            const auto function = sdk_find_object_named(ref, function_name);
            const auto caller = sdk_function_caller(ref, function);
            if (!function || !caller || !render_target)
            {
                continue;
            }
            const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
            if (params_size <= 0 || params_size > 4096)
            {
                continue;
            }
            for (int variant = 0; variant < 3; ++variant)
            {
                std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
                bool wrote_bool = false;
                bool wants_bool = variant != 0;
                bool bool_value = variant == 2;
                for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
                {
                    const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
                    if (name == "returnvalue")
                    {
                        continue;
                    }
                    if (contains_text(name, "worldcontext"))
                    {
                        sdk_write_object(prop, params.data(), ctx.pawn ? ctx.pawn : ctx.controller);
                    }
                    else if (contains_text(name, "rendertarget") || contains_text(name, "texture"))
                    {
                        sdk_write_object(prop, params.data(), render_target);
                    }
                    else if (contains_text(name, "normaliz") || contains_text(name, "srgb"))
                    {
                        if (wants_bool)
                        {
                            write_bool(prop, params.data(), bool_value);
                            wrote_bool = true;
                        }
                    }
                }
                if (wants_bool && !wrote_bool)
                {
                    continue;
                }
                if (diagnostics)
                {
                    ++diagnostics->function_attempts;
                }
                std::string failure{};
                if (!process_event(caller, function, params.data(), failure))
                {
                    continue;
                }
                if (diagnostics)
                {
                    ++diagnostics->process_event_ok;
                }
                for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
                {
                    const auto offset = prop_offset(prop);
                    const auto element_size = prop_element_size(prop);
                    if (offset < 0 || offset + static_cast<int>(sizeof(sdk::TArray<std::uint8_t>)) > params_size)
                    {
                        continue;
                    }
                    const auto array = *reinterpret_cast<sdk::TArray<std::uint8_t>*>(params.data() + offset);
                    const bool plausible_array =
                        array.Data != nullptr &&
                        array.Num > 0 &&
                        array.Max >= array.Num &&
                        expected_pixels > 0 &&
                        (array.Num == expected_pixels || array.Num == expected_pixels * 4);
                    if (!plausible_array)
                    {
                        continue;
                    }
                    if (diagnostics)
                    {
                        ++diagnostics->array_param_count;
                        if (diagnostics->first_array_offset < 0)
                        {
                            diagnostics->first_array_offset = offset;
                            diagnostics->first_array_num = array.Num;
                            diagnostics->first_array_max = array.Max;
                            diagnostics->first_array_element_size = element_size;
                        }
                    }
                    auto images = sdk_decode_bulk_array_candidates("bulk_array",
                                                                   function_name,
                                                                   wants_bool ? (bool_value ? "bool_true" : "bool_false") : "no_bool",
                                                                   reinterpret_cast<std::uintptr_t>(array.Data),
                                                                   array.Num,
                                                                   array.Max,
                                                                   width,
                                                                   height);
                    if (diagnostics && !images.empty() && diagnostics->first_candidate_type == "none")
                    {
                        diagnostics->first_candidate_type = images.front().function_name + ":" + images.front().inner_type;
                        diagnostics->decoded_pixels = images.front().decoded_pixels;
                    }
                    if (!images.empty())
                    {
                        return images;
                    }
                }
            }
        }
        return out;
    }

    auto sdk_configure_scene_capture_component_typed(std::uintptr_t capture_component,
                                                     std::uintptr_t render_target,
                                                     double fov_degrees = 90.0) -> bool
    {
        if (!capture_component || !render_target)
        {
            return false;
        }
        __try
        {
            *reinterpret_cast<std::uintptr_t*>(capture_component + sdk::FieldOffsets::SceneCaptureComponent2D_TextureTarget) = render_target;
            *reinterpret_cast<std::uint8_t*>(capture_component + sdk::FieldOffsets::SceneCaptureComponent_CaptureSource) =
                static_cast<std::uint8_t>(sdk::ESceneCaptureSource::BaseColor);
            auto* capture_flags = reinterpret_cast<std::uint8_t*>(capture_component + sdk::FieldOffsets::SceneCaptureComponent_CaptureFlags);
            *capture_flags = static_cast<std::uint8_t>(*capture_flags & ~0x03);
            *reinterpret_cast<bool*>(capture_component + sdk::FieldOffsets::SceneCaptureComponent_bAlwaysPersistRenderingState) = true;
            *reinterpret_cast<std::uint8_t*>(capture_component + sdk::FieldOffsets::SceneCaptureComponent2D_ProjectionType) =
                static_cast<std::uint8_t>(sdk::ECameraProjectionMode::Perspective);
            const auto fov = std::isfinite(fov_degrees) ? std::max(10.0, std::min(150.0, fov_degrees)) : 90.0;
            *reinterpret_cast<float*>(capture_component + sdk::FieldOffsets::SceneCaptureComponent2D_FOVAngle) = static_cast<float>(fov);
            return *reinterpret_cast<std::uintptr_t*>(capture_component + sdk::FieldOffsets::SceneCaptureComponent2D_TextureTarget) == render_target;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    constexpr bool kEnableNativeSceneCaptureForF10 = true;

    auto sdk_capture_front_colors(Reflection& ref,
                                  const SdkContext& ctx,
                                  const SdkNativeFrontSampleResult& native_front,
                                  int target_width,
                                  int target_height) -> SdkFrontCaptureResult
    {
        SdkFrontCaptureResult out{};
        if (native_front.samples.empty())
        {
            out.failure = "front_capture_no_surface_samples";
            return out;
        }
        const auto viewport = sdk_get_viewport_info(ref, ctx);
        out.width = viewport.width;
        out.height = viewport.height;
        if (out.width <= 0 || out.height <= 0)
        {
            out.failure = "front_capture_viewport_unavailable";
            return out;
        }
        if (!kEnableNativeSceneCaptureForF10)
        {
            out.failure = "front_capture_backend_disabled_after_d3d12_crash";
            return out;
        }
        const int viewport_width = out.width;
        const int viewport_height = out.height;
        out.viewport_width = viewport_width;
        out.viewport_height = viewport_height;
        out.viewport_aspect = static_cast<double>(std::max(1, viewport_width)) / static_cast<double>(std::max(1, viewport_height));
        if (target_width > 0 && target_height > 0)
        {
            out.requested_texture_width = target_width;
            out.requested_texture_height = target_height;
            int capture_width = std::max(1, target_width);
            int capture_height = std::max(1, target_height);
            constexpr int max_capture_dimension = 4096;
            const auto max_dimension = std::max(capture_width, capture_height);
            if (max_dimension > max_capture_dimension)
            {
                const auto scale = static_cast<double>(max_capture_dimension) / static_cast<double>(max_dimension);
                capture_width = std::max(1, static_cast<int>(std::round(static_cast<double>(capture_width) * scale)));
                capture_height = std::max(1, static_cast<int>(std::round(static_cast<double>(capture_height) * scale)));
            }
            out.width = capture_width;
            out.height = capture_height;
            out.capture_resolution_source = "viewport_full_38923_parity";
        }
        out.capture_aspect = static_cast<double>(std::max(1, out.width)) / static_cast<double>(std::max(1, out.height));
        const double capture_scale_x = static_cast<double>(out.width) / static_cast<double>(viewport_width);
        const double capture_scale_y = static_cast<double>(out.height) / static_cast<double>(viewport_height);
        out.capture_scale_x = capture_scale_x;
        out.capture_scale_y = capture_scale_y;
        const auto center_ray = sdk_deproject_screen_position(ref, ctx, static_cast<double>(viewport_width) * 0.5, static_cast<double>(viewport_height) * 0.5);
        if (!center_ray.ok)
        {
            out.failure = "front_capture_camera_deproject_failed:" + center_ray.failure;
            return out;
        }
        auto capture_location = center_ray.location;
        auto capture_direction = center_ray.direction;
        bool deproject_fov_valid = false;
        const auto left_ray = sdk_deproject_screen_position(ref, ctx, 0.0, static_cast<double>(viewport_height) * 0.5);
        const auto right_ray = sdk_deproject_screen_position(ref, ctx, static_cast<double>(std::max(1, viewport_width - 1)), static_cast<double>(viewport_height) * 0.5);
        if (left_ray.ok && right_ray.ok)
        {
            const auto left_dir = sdk_vec_normalize(left_ray.direction);
            const auto right_dir = sdk_vec_normalize(right_ray.direction);
            const auto dot = std::max(-1.0, std::min(1.0, sdk_vec_dot(left_dir, right_dir)));
            const auto fov = std::acos(dot) * 180.0 / 3.14159265358979323846;
            if (std::isfinite(fov) && fov >= 10.0 && fov <= 150.0)
            {
                out.capture_fov = fov;
                deproject_fov_valid = true;
            }
        }
        std::string camera_failure{};
        out.camera_manager = sdk_call_no_params_return_object(ref, ctx.controller, "GetPlayerCameraManager", camera_failure);
        std::string camera_manager_source = "function:GetPlayerCameraManager";
        if (!live_uobject(out.camera_manager) && ctx.controller)
        {
            const auto field_camera_manager = safe_read<std::uintptr_t>(
                ctx.controller + sdk::FieldOffsets::PlayerController_PlayerCameraManager,
                0);
            if (live_uobject(field_camera_manager))
            {
                out.camera_manager = field_camera_manager;
                camera_manager_source = "field:APlayerController.PlayerCameraManager@0x360";
            }
        }
        if (live_uobject(out.camera_manager))
        {
            sdk::FVector camera_location{};
            if (sdk_call_no_params_return_vector3(ref, out.camera_manager, "GetCameraLocation", camera_location))
            {
                capture_location = camera_location;
                out.camera_location_used = true;
                out.camera_location_source = "player_camera_manager";
            }
            sdk::FRotator camera_rotation{};
            if (sdk_call_no_params_return_rotator(ref, out.camera_manager, "GetCameraRotation", camera_rotation))
            {
                const auto camera_forward = sdk_rotator_forward(camera_rotation);
                const auto center_forward = sdk_vec_normalize(center_ray.direction);
                const auto dot = sdk_vec_dot(sdk_vec_normalize(camera_forward), center_forward);
                if (std::isfinite(dot) && dot > 0.80)
                {
                    capture_direction = camera_forward;
                    out.camera_rotation_used = true;
                    out.camera_rotation_source = "player_camera_manager";
                }
                else
                {
                    out.camera_rotation_used = false;
                    out.camera_rotation_source = "deproject_center_ray_rejected_player_camera_rotation";
                }
            }
            double camera_fov = 0.0;
            if (sdk_call_no_params_return_number(ref, out.camera_manager, "GetFOVAngle", camera_fov) &&
                std::isfinite(camera_fov) && camera_fov >= 10.0 && camera_fov <= 150.0)
            {
                if (!deproject_fov_valid)
                {
                    out.capture_fov = camera_fov;
                    out.camera_fov_used = true;
                    out.camera_fov_source = "player_camera_manager";
                }
                else
                {
                    out.camera_fov_used = false;
                    out.camera_fov_source = "deproject_horizontal_preferred_over_player_camera_manager";
                }
            }
        }
        if (!out.camera_rotation_used && ctx.controller)
        {
            const auto control_rotation = safe_read<sdk::FRotator>(
                ctx.controller + sdk::FieldOffsets::Controller_ControlRotation,
                sdk::FRotator{});
            const auto control_forward = sdk_rotator_forward(control_rotation);
            const auto center_forward = sdk_vec_normalize(center_ray.direction);
            const auto dot = sdk_vec_dot(sdk_vec_normalize(control_forward), center_forward);
            if (std::isfinite(control_forward.X) && std::isfinite(control_forward.Y) && std::isfinite(control_forward.Z) &&
                (std::abs(control_forward.X) + std::abs(control_forward.Y) + std::abs(control_forward.Z)) > 0.001 &&
                std::isfinite(dot) && dot > 0.80)
            {
                capture_direction = control_forward;
                out.camera_rotation_used = true;
                out.camera_rotation_source = "field:AController.ControlRotation@0x320";
            }
        }
        out.camera_manager_source = camera_manager_source;
        out.capture_location = capture_location;
        out.capture_direction = sdk_vec_normalize(capture_direction);
        std::string failure{};
        out.render_target = sdk_create_render_target(ref, ctx, out.width, out.height, failure);
        out.render_target_created = out.render_target != 0;
        if (!out.render_target)
        {
            out.failure = failure.empty() ? "front_capture_render_target_unavailable" : failure;
            return out;
        }
        const auto scene_capture_class = ref.find_class("SceneCapture2D");
        out.capture_actor = sdk_spawn_actor_from_class(ref, ctx, scene_capture_class, out.capture_location, failure);
        out.capture_actor_spawned = out.capture_actor != 0;
        if (!out.capture_actor)
        {
            out.failure = failure.empty() ? "front_capture_actor_spawn_failed" : failure;
            return out;
        }
        sdk_set_actor_capture_transform(ref, out.capture_actor, out.capture_location, out.capture_direction);
        out.capture_component = safe_read<std::uintptr_t>(out.capture_actor + sdk::FieldOffsets::SceneCapture2D_CaptureComponent2D, 0);
        if (!out.capture_component)
        {
            out.capture_component = sdk_call_no_params_return_object(ref, out.capture_actor, "GetCaptureComponent2D", failure);
        }
        out.capture_component_found = out.capture_component != 0;
        if (!out.capture_component)
        {
            out.failure = failure.empty() ? "front_capture_component_unavailable" : failure;
            sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
            return out;
        }
        out.texture_target_written = sdk_configure_scene_capture_component_typed(out.capture_component, out.render_target, out.capture_fov);
        out.hide_component_called = native_front.mesh && sdk_call_object_param(ref, out.capture_component, "HideComponent", native_front.mesh);
        if (!out.texture_target_written)
        {
            out.failure = "front_capture_texture_target_write_failed";
            sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
            return out;
        }
        out.capture_scene_called = sdk_call_no_params(ref, out.capture_component, "CaptureScene");
        if (!out.capture_scene_called)
        {
            out.failure = "front_capture_scene_failed";
            sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
            return out;
        }
        Sleep(12);

        struct ProjectedFrontSample
        {
            FrontSample surface{};
            int x{0};
            int y{0};
            double depth{0.0};
            bool has_depth{false};
            Color pixel_color{};
            bool has_pixel{false};
        };
        std::vector<ProjectedFrontSample> projected{};
        projected.reserve(native_front.samples.size());
        const auto capture_forward = sdk_vec_normalize(out.capture_direction);
        sdk::FVector world_up{0.0, 0.0, 1.0};
        auto capture_right = sdk_vec_normalize(sdk_vec_cross(world_up, capture_forward));
        if (sdk_vec_len(capture_right) <= 0.000001)
        {
            world_up = {0.0, 1.0, 0.0};
            capture_right = sdk_vec_normalize(sdk_vec_cross(world_up, capture_forward));
        }
        const auto capture_up = sdk_vec_normalize(sdk_vec_cross(capture_forward, capture_right));
        const double half_fov_radians = out.capture_fov * 3.14159265358979323846 / 360.0;
        const double tan_half_horizontal = std::tan(half_fov_radians);
        const double tan_half_vertical = tan_half_horizontal / std::max(0.001, out.capture_aspect);
        out.projection_backend = "scene_capture_camera_matrix";
        auto project_to_capture = [&](const sdk::FVector& world, double& sx, double& sy, double& depth) -> bool {
            if (sdk_vec_len(capture_forward) <= 0.000001 ||
                sdk_vec_len(capture_right) <= 0.000001 ||
                sdk_vec_len(capture_up) <= 0.000001 ||
                !std::isfinite(tan_half_horizontal) ||
                !std::isfinite(tan_half_vertical) ||
                tan_half_horizontal <= 0.000001 ||
                tan_half_vertical <= 0.000001)
            {
                return false;
            }
            const auto rel = sdk_vec_sub(world, out.capture_location);
            depth = sdk_vec_dot(rel, capture_forward);
            if (!std::isfinite(depth) || depth <= 0.000001)
            {
                return false;
            }
            const double right = sdk_vec_dot(rel, capture_right);
            const double up = sdk_vec_dot(rel, capture_up);
            const double ndc_x = right / (depth * tan_half_horizontal);
            const double ndc_y = up / (depth * tan_half_vertical);
            if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y))
            {
                return false;
            }
            sx = (ndc_x * 0.5 + 0.5) * static_cast<double>(out.width);
            sy = (0.5 - ndc_y * 0.5) * static_cast<double>(out.height);
            return true;
        };
        double sum = 0.0;
        int channels = 0;
        bool initialized = false;
        double raw_sum = 0.0;
        int raw_channels = 0;
        bool raw_initialized = false;
        double resolved_delta_sum = 0.0;
        double resolved_delta_max = 0.0;
        int resolved_delta_samples = 0;
        bool has_depth_samples = false;
        bool depth_initialized = false;
        for (const auto& surface : native_front.samples)
        {
            ++out.project_attempts;
            double sx = clamp01(surface.screen_nx) * static_cast<double>(out.width);
            double sy = clamp01(surface.screen_ny) * static_cast<double>(out.height);
            double depth = 0.0;
            bool has_depth = false;
            if (surface.has_world_position)
            {
                if (!project_to_capture(surface.world_position, sx, sy, depth))
                {
                    ++out.project_failed;
                    continue;
                }
                double player_projected_x = 0.0;
                double player_projected_y = 0.0;
                if (sdk_project_world_to_screen(ref, ctx, surface.world_position, player_projected_x, player_projected_y))
                {
                    const auto dx = sx - player_projected_x * capture_scale_x;
                    const auto dy = sy - player_projected_y * capture_scale_y;
                    const auto delta = std::sqrt(dx * dx + dy * dy);
                    out.project_delta_sum_px += delta;
                    out.project_delta_max_px = std::max(out.project_delta_max_px, delta);
                }
                if (!std::isfinite(depth) || depth <= 0.0)
                {
                    ++out.project_out_of_view;
                    continue;
                }
                has_depth = true;
                has_depth_samples = true;
                if (!depth_initialized)
                {
                    out.visibility_depth_min = depth;
                    out.visibility_depth_max = depth;
                    depth_initialized = true;
                }
                out.visibility_depth_min = std::min(out.visibility_depth_min, depth);
                out.visibility_depth_max = std::max(out.visibility_depth_max, depth);
            }
            const bool outside = sx < 0.0 || sy < 0.0 ||
                                 sx >= static_cast<double>(out.width) ||
                                 sy >= static_cast<double>(out.height);
            if (outside)
            {
                ++out.project_out_of_view;
                continue;
            }
            ++out.project_success;
            const auto px = std::max(0, std::min(out.width - 1, static_cast<int>(std::round(sx))));
            const auto py = std::max(0, std::min(out.height - 1, static_cast<int>(std::round(sy))));
            auto projected_surface = surface;
            projected_surface.screen_nx = clamp01(sx / static_cast<double>(std::max(1, out.width)));
            projected_surface.screen_ny = clamp01(sy / static_cast<double>(std::max(1, out.height)));
            projected.push_back(ProjectedFrontSample{projected_surface, px, py, depth, has_depth, {}, false});
        }
        if (projected.empty())
        {
            out.failure = "front_capture_project_to_scene_capture_failed";
            sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
            return out;
        }
        out.visibility_input = static_cast<int>(projected.size());
        out.visibility_kept = out.visibility_input;
        if (has_depth_samples && !native_front.keep_occluded_projected_samples)
        {
            constexpr int kVisibilityCellPx = 4;
            constexpr double kVisibilityDepthTolerance = 6.0;
            out.visibility_cell_px = kVisibilityCellPx;
            const int depth_cols = std::max(1, (out.width + kVisibilityCellPx - 1) / kVisibilityCellPx);
            const int depth_rows = std::max(1, (out.height + kVisibilityCellPx - 1) / kVisibilityCellPx);
            std::vector<double> nearest_depth(static_cast<std::size_t>(depth_cols * depth_rows),
                                              std::numeric_limits<double>::infinity());
            auto depth_index = [&](int x, int y) -> std::size_t {
                const int cx = std::max(0, std::min(depth_cols - 1, x / kVisibilityCellPx));
                const int cy = std::max(0, std::min(depth_rows - 1, y / kVisibilityCellPx));
                return static_cast<std::size_t>(cy * depth_cols + cx);
            };
            for (const auto& sample : projected)
            {
                if (!sample.has_depth)
                    continue;
                auto& depth_slot = nearest_depth[depth_index(sample.x, sample.y)];
                depth_slot = std::min(depth_slot, sample.depth);
            }
            std::vector<ProjectedFrontSample> visible_projected{};
            visible_projected.reserve(projected.size());
            for (const auto& sample : projected)
            {
                if (!sample.has_depth)
                {
                    visible_projected.push_back(sample);
                    continue;
                }
                const auto nearest = nearest_depth[depth_index(sample.x, sample.y)];
                if (std::isfinite(nearest) && sample.depth <= nearest + kVisibilityDepthTolerance)
                {
                    visible_projected.push_back(sample);
                }
            }
            out.visibility_kept = static_cast<int>(visible_projected.size());
            out.visibility_rejected = out.visibility_input - out.visibility_kept;
            if (visible_projected.empty())
            {
                out.failure = "front_capture_visibility_filter_empty";
                sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
                return out;
            }
            projected = std::move(visible_projected);
        }

        SdkBulkReadbackDiagnostics bulk_diagnostics{};
        auto bulk_candidates = sdk_read_render_target_bulk_candidates(ref, ctx, out.render_target, out.width, out.height, &bulk_diagnostics);
        out.bulk_candidates = static_cast<int>(bulk_candidates.size());
        out.bulk_available = out.bulk_candidates;
        out.bulk_function_attempts = bulk_diagnostics.function_attempts;
        out.bulk_process_event_ok = bulk_diagnostics.process_event_ok;
        out.bulk_array_param_count = bulk_diagnostics.array_param_count;
        out.bulk_array_offset = bulk_diagnostics.first_array_offset;
        out.bulk_array_num = bulk_diagnostics.first_array_num;
        out.bulk_array_max = bulk_diagnostics.first_array_max;
        out.bulk_array_element_size = bulk_diagnostics.first_array_element_size;
        out.bulk_decode_candidate_type = bulk_diagnostics.first_candidate_type;
        out.bulk_decoded_pixels = bulk_diagnostics.decoded_pixels;
        double best_median = 1000000.0;
        double runner_up_median = 1000000.0;
        int best_pairs = 0;
        int best_candidate = -1;
        bool best_flip_x = false;
        bool best_flip_y = false;
        SdkBulkColorTransform best_transform = SdkBulkColorTransform::Identity;
        const std::pair<bool, bool> flip_candidates[]{{false, false}, {true, false}, {false, true}, {true, true}};
        const int calibration_limit = std::min<int>(128, static_cast<int>(projected.size()));
        const double stride = static_cast<double>(std::max<std::size_t>(1, projected.size())) / static_cast<double>(std::max(1, calibration_limit));
        for (int i = 0; i < calibration_limit; ++i)
        {
            const auto sample_index = std::min<std::size_t>(projected.size() - 1,
                                                            static_cast<std::size_t>(std::floor((static_cast<double>(i) + 0.5) * stride)));
            auto& sample = projected[sample_index];
            Color color{};
            ++out.read_attempts;
            const bool pixel_ok = sdk_read_render_target_raw_pixel(ref, ctx, out.render_target, sample.x, sample.y, color, out.read_function);
            if (!pixel_ok)
            {
                ++out.missing_color;
                continue;
            }
            sample.pixel_color = color;
            sample.has_pixel = true;
            ++out.read_success;
        }
        for (int candidate_index = 0; candidate_index < static_cast<int>(bulk_candidates.size()); ++candidate_index)
        {
            const auto& candidate = bulk_candidates[static_cast<std::size_t>(candidate_index)];
            if (!candidate.ok || candidate.pixels.size() < static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height))
            {
                continue;
            }
            const auto color_candidates = sdk_allowed_bulk_color_transforms(candidate.inner_type);
            for (const auto& flip : flip_candidates)
            {
                for (const auto transform : color_candidates)
                {
                    std::vector<double> distances{};
                    distances.reserve(static_cast<std::size_t>(calibration_limit));
                    for (int i = 0; i < calibration_limit; ++i)
                    {
                        const auto sample_index = std::min<std::size_t>(projected.size() - 1,
                                                                        static_cast<std::size_t>(std::floor((static_cast<double>(i) + 0.5) * stride)));
                        const auto& sample = projected[sample_index];
                        if (!sample.has_pixel)
                        {
                            continue;
                        }
                        const int bx = flip.first ? (out.width - 1 - sample.x) : sample.x;
                        const int by = flip.second ? (out.height - 1 - sample.y) : sample.y;
                        const auto pixel_index = static_cast<std::size_t>(by) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(bx);
                        if (pixel_index >= candidate.pixels.size())
                        {
                            continue;
                        }
                        distances.push_back(sdk_color_distance_rgb(sample.pixel_color,
                                                                   sdk_apply_bulk_color_transform(candidate.pixels[pixel_index], transform)));
                    }
                    const int pairs = static_cast<int>(distances.size());
                    const double median = sdk_median(std::move(distances));
                    if (median < best_median)
                    {
                        runner_up_median = best_median;
                        best_median = median;
                        best_pairs = pairs;
                        best_candidate = candidate_index;
                        best_flip_x = flip.first;
                        best_flip_y = flip.second;
                        best_transform = transform;
                    }
                    else if (median < runner_up_median)
                    {
                        runner_up_median = median;
                    }
                }
            }
        }
        out.bulk_calibration_samples = calibration_limit;
        out.bulk_calibration_pairs = best_pairs;
        out.bulk_calibration_best_median = best_median < 999999.0 ? best_median : 0.0;
        out.bulk_calibration_runner_up_median = runner_up_median < 999999.0 ? runner_up_median : 0.0;
        const bool separated_from_runner = runner_up_median >= 999999.0 ||
                                           best_median <= runner_up_median * 0.90 ||
                                           (runner_up_median - best_median) >= 0.012;
        out.image_bulk_calibration_ok = best_candidate >= 0 &&
                                        best_pairs >= std::min(16, std::max(1, calibration_limit / 2)) &&
                                        best_median <= 0.18 &&
                                        separated_from_runner;
        if (!out.image_bulk_calibration_ok)
        {
            out.ok = false;
            out.failure = "front_texture_bulk_calibration_unavailable";
            sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
            return out;
        }

        auto& bulk = bulk_candidates[static_cast<std::size_t>(best_candidate)];
        out.bulk_readback_used = true;
        out.texture_source = "bulk_calibrated_direct_texture";
        out.bulk_backend = bulk.backend;
        out.bulk_inner_type = bulk.inner_type;
        out.bulk_bool_variant = bulk.bool_variant;
        out.bulk_decoded_pixels = bulk.decoded_pixels;
        out.bulk_decode_candidate_type = bulk.function_name + ":" + bulk.inner_type;
        out.bulk_color_transform = sdk_bulk_color_transform_label(best_transform);
        out.bulk_calibration_backend = bulk.function_name + "|" + bulk.bool_variant + "|" +
                                       std::string(best_flip_x ? "flip_x" : "identity_x") + "|" +
                                       std::string(best_flip_y ? "flip_y" : "identity_y") + "|" +
                                       out.bulk_color_transform;
        out.capture_transform_backend = std::string(best_flip_x || best_flip_y ? "bulk_calibrated_flip" : "bulk_calibrated_identity");
        out.capture_flip_x = best_flip_x;
        out.capture_flip_y = best_flip_y;
        for (auto& pixel : bulk.pixels)
        {
            pixel = sdk_apply_bulk_color_transform(pixel, best_transform);
            pixel.roughness = 0.65;
            pixel.metallic = 0.0;
        }
        out.capture_pixels = std::move(bulk.pixels);
        out.capture_pixels_available = out.capture_pixels.size() >= static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height);

        out.samples.reserve(projected.size());
        for (const auto& projected_sample : projected)
        {
            const int bx = best_flip_x ? (out.width - 1 - projected_sample.x) : projected_sample.x;
            const int by = best_flip_y ? (out.height - 1 - projected_sample.y) : projected_sample.y;
            const auto pixel_index = static_cast<std::size_t>(by) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(bx);
            if (pixel_index >= out.capture_pixels.size())
            {
                ++out.missing_color;
                continue;
            }
            const auto raw_color = out.capture_pixels[pixel_index];
            const double raw_values[]{clamp01(raw_color.r), clamp01(raw_color.g), clamp01(raw_color.b)};
            bool raw_whiteish = true;
            for (const auto value : raw_values)
            {
                if (!raw_initialized)
                {
                    out.raw_rgb_min = value;
                    out.raw_rgb_max = value;
                    raw_initialized = true;
                }
                out.raw_rgb_min = std::min(out.raw_rgb_min, value);
                out.raw_rgb_max = std::max(out.raw_rgb_max, value);
                raw_sum += value;
                ++raw_channels;
                if (value < 0.97)
                {
                    raw_whiteish = false;
                }
            }
            if (raw_whiteish)
            {
                ++out.raw_whiteish_samples;
            }
            auto resolved_color = raw_color;
            resolved_color.roughness = 0.65;
            resolved_color.metallic = 0.0;
            const auto resolved_delta = sdk_color_distance_rgb(raw_color, resolved_color);
            if (std::isfinite(resolved_delta))
            {
                resolved_delta_sum += resolved_delta;
                resolved_delta_max = std::max(resolved_delta_max, resolved_delta);
                ++resolved_delta_samples;
            }
            FrontSample sample = projected_sample.surface;
            sample.r = clamp01(resolved_color.r);
            sample.g = clamp01(resolved_color.g);
            sample.b = clamp01(resolved_color.b);
            sample.metallic = clamp01(resolved_color.metallic);
            sample.roughness = clamp01(resolved_color.roughness);
            out.samples.push_back(sample);
            const double values[]{sample.r, sample.g, sample.b};
            bool whiteish = true;
            for (const auto value : values)
            {
                if (!initialized)
                {
                    out.rgb_min = value;
                    out.rgb_max = value;
                    initialized = true;
                }
                out.rgb_min = std::min(out.rgb_min, value);
                out.rgb_max = std::max(out.rgb_max, value);
                sum += value;
                ++channels;
                if (value < 0.97)
                {
                    whiteish = false;
                }
            }
            if (whiteish)
            {
                ++out.whiteish_samples;
            }
        }
        out.rgb_avg = channels > 0 ? sum / static_cast<double>(channels) : 0.0;
        out.luma_range = out.rgb_max - out.rgb_min;
        out.raw_rgb_avg = raw_channels > 0 ? raw_sum / static_cast<double>(raw_channels) : 0.0;
        out.raw_luma_range = out.raw_rgb_max - out.raw_rgb_min;
        out.resolved_rgb_delta_avg = resolved_delta_samples > 0 ? resolved_delta_sum / static_cast<double>(resolved_delta_samples) : 0.0;
        out.resolved_rgb_delta_max = resolved_delta_max;
        out.resolved_rgb_delta_samples = resolved_delta_samples;
        out.uniform = out.samples.size() > 0 && out.luma_range < 0.006;
        out.all_whiteish = out.samples.size() > 0 && out.whiteish_samples == static_cast<int>(out.samples.size());
        out.ok = static_cast<int>(out.samples.size()) >= native_front.min_front_hits && !out.uniform && !out.all_whiteish;
        out.failure = out.ok ? "ok" : (out.samples.empty() ? "front_capture_color_empty" : "front_capture_quality_failed");
        sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
        return out;
    }

    auto sdk_capture_metadata(const SdkFrontCaptureResult& capture) -> std::string
    {
        return ",\"front_capture_ok\":" + std::string(json_bool(capture.ok)) +
               ",\"front_capture_failure\":\"" + json_escape(capture.failure) + "\"" +
               ",\"capture_resolution\":\"" + std::to_string(capture.width) + "x" + std::to_string(capture.height) + "\"" +
               ",\"capture_fov\":" + std::to_string(capture.capture_fov) +
               ",\"capture_resolution_source\":\"" + json_escape(capture.capture_resolution_source) + "\"" +
               ",\"capture_requested_texture_width\":" + std::to_string(capture.requested_texture_width) +
               ",\"capture_requested_texture_height\":" + std::to_string(capture.requested_texture_height) +
               ",\"capture_viewport_width\":" + std::to_string(capture.viewport_width) +
               ",\"capture_viewport_height\":" + std::to_string(capture.viewport_height) +
               ",\"capture_viewport_aspect\":" + std::to_string(capture.viewport_aspect) +
               ",\"capture_aspect\":" + std::to_string(capture.capture_aspect) +
               ",\"capture_scale_x\":" + std::to_string(capture.capture_scale_x) +
               ",\"capture_scale_y\":" + std::to_string(capture.capture_scale_y) +
               ",\"front_capture_render_target\":\"" + hex_address(capture.render_target) + "\"" +
               ",\"front_capture_actor\":\"" + hex_address(capture.capture_actor) + "\"" +
               ",\"front_capture_component\":\"" + hex_address(capture.capture_component) + "\"" +
               ",\"front_capture_read_function\":\"" + hex_address(capture.read_function) + "\"" +
               ",\"front_capture_render_target_created\":" + std::string(json_bool(capture.render_target_created)) +
               ",\"front_capture_actor_spawned\":" + std::string(json_bool(capture.capture_actor_spawned)) +
               ",\"front_capture_component_found\":" + std::string(json_bool(capture.capture_component_found)) +
               ",\"front_capture_texture_target_written\":" + std::string(json_bool(capture.texture_target_written)) +
               ",\"front_capture_hide_component_called\":" + std::string(json_bool(capture.hide_component_called)) +
               ",\"front_capture_scene_called\":" + std::string(json_bool(capture.capture_scene_called)) +
               ",\"capture_camera_manager\":\"" + hex_address(capture.camera_manager) + "\"" +
               ",\"capture_camera_manager_source\":\"" + json_escape(capture.camera_manager_source) + "\"" +
               ",\"capture_camera_location_used\":" + std::string(json_bool(capture.camera_location_used)) +
               ",\"capture_camera_rotation_used\":" + std::string(json_bool(capture.camera_rotation_used)) +
               ",\"capture_camera_fov_used\":" + std::string(json_bool(capture.camera_fov_used)) +
               ",\"capture_camera_location_source\":\"" + json_escape(capture.camera_location_source) + "\"" +
               ",\"capture_camera_rotation_source\":\"" + json_escape(capture.camera_rotation_source) + "\"" +
               ",\"capture_camera_fov_source\":\"" + json_escape(capture.camera_fov_source) + "\"" +
               ",\"capture_location_x\":" + std::to_string(capture.capture_location.X) +
               ",\"capture_location_y\":" + std::to_string(capture.capture_location.Y) +
               ",\"capture_location_z\":" + std::to_string(capture.capture_location.Z) +
               ",\"capture_direction_x\":" + std::to_string(capture.capture_direction.X) +
               ",\"capture_direction_y\":" + std::to_string(capture.capture_direction.Y) +
               ",\"capture_direction_z\":" + std::to_string(capture.capture_direction.Z) +
               ",\"front_capture_projection_backend\":\"" + json_escape(capture.projection_backend) + "\"" +
               ",\"front_capture_project_attempts\":" + std::to_string(capture.project_attempts) +
               ",\"front_capture_project_success\":" + std::to_string(capture.project_success) +
               ",\"front_capture_project_failed\":" + std::to_string(capture.project_failed) +
               ",\"front_capture_project_out_of_view\":" + std::to_string(capture.project_out_of_view) +
               ",\"front_capture_project_delta_avg_px\":" + std::to_string(capture.project_success > 0 ? capture.project_delta_sum_px / static_cast<double>(capture.project_success) : 0.0) +
               ",\"front_capture_project_delta_max_px\":" + std::to_string(capture.project_delta_max_px) +
               ",\"front_capture_visibility_input\":" + std::to_string(capture.visibility_input) +
               ",\"front_capture_visibility_kept\":" + std::to_string(capture.visibility_kept) +
               ",\"front_capture_visibility_rejected\":" + std::to_string(capture.visibility_rejected) +
               ",\"front_capture_visibility_cell_px\":" + std::to_string(capture.visibility_cell_px) +
               ",\"front_capture_visibility_depth_min\":" + std::to_string(capture.visibility_depth_min) +
               ",\"front_capture_visibility_depth_max\":" + std::to_string(capture.visibility_depth_max) +
               ",\"front_capture_read_attempts\":" + std::to_string(capture.read_attempts) +
               ",\"front_capture_read_success\":" + std::to_string(capture.read_success) +
               ",\"front_capture_missing_color\":" + std::to_string(capture.missing_color) +
               ",\"front_raw_rgb_min\":" + std::to_string(capture.raw_rgb_min) +
               ",\"front_raw_rgb_max\":" + std::to_string(capture.raw_rgb_max) +
               ",\"front_raw_rgb_avg\":" + std::to_string(capture.raw_rgb_avg) +
               ",\"front_raw_luma_range\":" + std::to_string(capture.raw_luma_range) +
               ",\"front_raw_rgb_whiteish_samples\":" + std::to_string(capture.raw_whiteish_samples) +
               ",\"front_resolved_rgb_delta_avg\":" + std::to_string(capture.resolved_rgb_delta_avg) +
               ",\"front_resolved_rgb_delta_max\":" + std::to_string(capture.resolved_rgb_delta_max) +
               ",\"front_resolved_rgb_delta_samples\":" + std::to_string(capture.resolved_rgb_delta_samples) +
               ",\"front_rgb_min\":" + std::to_string(capture.rgb_min) +
               ",\"front_rgb_max\":" + std::to_string(capture.rgb_max) +
               ",\"front_rgb_avg\":" + std::to_string(capture.rgb_avg) +
               ",\"front_luma_range\":" + std::to_string(capture.luma_range) +
               ",\"front_rgb_whiteish_samples\":" + std::to_string(capture.whiteish_samples) +
               ",\"front_rgb_uniform\":" + std::string(json_bool(capture.uniform)) +
               ",\"front_rgb_all_whiteish\":" + std::string(json_bool(capture.all_whiteish)) +
               ",\"front_texture_source\":\"" + json_escape(capture.texture_source) + "\"" +
               ",\"bulk_readback_used\":" + std::string(json_bool(capture.bulk_readback_used)) +
               ",\"image_bulk_calibration_ok\":" + std::string(json_bool(capture.image_bulk_calibration_ok)) +
               ",\"bulk_candidates\":" + std::to_string(capture.bulk_candidates) +
               ",\"bulk_available\":" + std::to_string(capture.bulk_available) +
               ",\"bulk_decoded_pixels\":" + std::to_string(capture.bulk_decoded_pixels) +
               ",\"bulk_function_attempts\":" + std::to_string(capture.bulk_function_attempts) +
               ",\"bulk_process_event_ok\":" + std::to_string(capture.bulk_process_event_ok) +
               ",\"bulk_array_param_count\":" + std::to_string(capture.bulk_array_param_count) +
               ",\"bulk_array_offset\":" + std::to_string(capture.bulk_array_offset) +
               ",\"bulk_array_num\":" + std::to_string(capture.bulk_array_num) +
               ",\"bulk_array_max\":" + std::to_string(capture.bulk_array_max) +
               ",\"bulk_array_element_size\":" + std::to_string(capture.bulk_array_element_size) +
               ",\"bulk_decode_candidate_type\":\"" + json_escape(capture.bulk_decode_candidate_type) + "\"" +
               ",\"bulk_decode_pixels\":" + std::to_string(capture.bulk_decoded_pixels) +
               ",\"bulk_calibration_samples\":" + std::to_string(capture.bulk_calibration_samples) +
               ",\"bulk_calibration_pairs\":" + std::to_string(capture.bulk_calibration_pairs) +
               ",\"bulk_calibration_best_median\":" + std::to_string(capture.bulk_calibration_best_median) +
               ",\"bulk_calibration_runner_up_median\":" + std::to_string(capture.bulk_calibration_runner_up_median) +
               ",\"bulk_backend\":\"" + json_escape(capture.bulk_backend) + "\"" +
               ",\"bulk_inner_type\":\"" + json_escape(capture.bulk_inner_type) + "\"" +
               ",\"bulk_bool_variant\":\"" + json_escape(capture.bulk_bool_variant) + "\"" +
               ",\"bulk_color_transform\":\"" + json_escape(capture.bulk_color_transform) + "\"" +
               ",\"bulk_calibration_backend\":\"" + json_escape(capture.bulk_calibration_backend) + "\"" +
               ",\"capture_transform_backend\":\"" + json_escape(capture.capture_transform_backend) + "\"" +
               ",\"texture_source_verified\":" + std::string(json_bool(capture.bulk_readback_used &&
                                                                        capture.image_bulk_calibration_ok &&
                                                                        capture.texture_source == "bulk_calibrated_direct_texture"));
    }

    auto sdk_find_color_picker_caller(Reflection& ref) -> std::uintptr_t
    {
        if (const auto instance = ref.find_first_instance("ColorPicker"))
        {
            return instance;
        }
        const auto cls = ref.find_class("ColorPicker");
        if (!cls)
        {
            return 0;
        }
        std::uintptr_t cdo = 0;
        ref.for_each_object([&](std::uintptr_t obj) {
            if (ref.class_ptr(obj) == cls && (safe_read<std::uint32_t>(obj + OffObjectFlags, 0) & RFClassDefaultObject) != 0)
            {
                cdo = obj;
                return true;
            }
            return false;
        });
        return cdo ? cdo : cls;
    }

    struct TemplateUvBrushAsyncJob
    {
        enum class Phase
        {
            Phase0BaseGrid,
            Phase0Dense,
            CaptureSource,
            BeginPaint,
            ReplicateStrokes,
            Finish
        };

        struct TemplatePoint
        {
            double x{0.0};
            double y{0.0};
            double u{0.0};
            double v{0.0};
            double r{0.0};
            double g{0.0};
            double b{0.0};
            double metallic{0.0};
            double roughness{0.85};
            double stroke_radius{0.01};
            int paint_pass{0};
            bool has_color{false};
        };

        struct ScreenCandidate
        {
            double nx{0.0};
            double ny{0.0};
        };

        std::shared_ptr<QueuedPaintJob> queued{};
        Phase phase{Phase::Phase0BaseGrid};
        std::uintptr_t world{0};
        std::uintptr_t controller{0};
        std::uintptr_t component{0};
        std::uintptr_t mesh{0};
        std::uintptr_t hit_test_function{0};
        std::uintptr_t server_paint_batch_function{0};
        int viewport_width{0};
        int viewport_height{0};
        int base_cols{0};
        int base_rows{0};
        int dense_rows{0};
        int next_index{0};
        int capture_index{0};
        int replicate_index{0};
        int point_target{0};
        int coverage_sample_target{0};
        int coverage_candidate_target{0};
        int paint_sample_attempts{0};
        int paint_sample_success{0};
        int paint_sample_failures{0};
        int sampler_probe_attempts{0};
        int sampler_probe_misses{0};
        int candidate_count{0};
        int points_before_downsample{0};
        int downsample_removed{0};
        int sample_pool_points{0};
        int fill_sample_target{0};
        int coverage_strokes{0};
        int detail_strokes{0};
        double silhouette_area_px{0.0};
        int base_attempts{0};
        int base_hits{0};
        int dense_attempts{0};
        int dense_hits{0};
        int dedupe_skipped{0};
        int runtime_hit_test_attempts{0};
        int runtime_hit_test_hits{0};
        int runtime_hit_test_failures{0};
        int basecolor_samples{0};
        int server_batch_success{0};
        int server_batch_failures{0};
        int server_batch_calls{0};
        int server_strokes_sent{0};
        int server_batch_limit{0};
        int server_batch_delay_ms{0};
        int front_samples_artifact_count{0};
        int commit_pulses{0};
        int progress_percent{-1};
        double template_point_elapsed_ms{0.0};
        double template_capture_elapsed_ms{0.0};
        double server_batch_elapsed_ms{0.0};
        double base_probe_radius{0.010};
        double base_probe_spacing_px{0.0};
        double coverage_brush_screen_radius_px{0.0};
        double coverage_sample_spacing_px{0.0};
        double fill_sample_spacing_px{0.0};
        double detail_sample_spacing_px{0.0};
        double coverage_candidate_spacing_px{0.0};
        double coverage_estimated_acceptance{0.0};
        double sampling_brush_radius{0.010};
        double brush_spacing{0.18};
        double server_brush_spacing{0.08};
        double bbox_min_nx{1.0};
        double bbox_min_ny{1.0};
        double bbox_max_nx{0.0};
        double bbox_max_ny{0.0};
        bool source_sorted{false};
        bool research_artifacts{false};
        bool front_samples_artifact_written{false};
        sdk::FRuntimeBrushSettings old_brush{};
        sdk::FRuntimeBrushSettings brush{};
        bool explicit_stroke_batch_used{false};
        SdkReplicationSnapshot replication_after_explicit_batches{};
        double brush_radius{0.01};
        double brush_radius_raw{0.01};
        double rgb_min{1.0};
        double rgb_max{0.0};
        double rgb_sum_r{0.0};
        double rgb_sum_g{0.0};
        double rgb_sum_b{0.0};
        double metallic_sum{0.0};
        double roughness_sum{0.0};
        std::string metadata{};
        std::string first_failure{};
        std::string server_batch_rpc{"<none>"};
        std::string color_source{"scene_capture_basecolor_bulk_readback"};
        std::vector<TemplatePoint> points{};
        std::vector<TemplatePoint> sample_pool{};
        std::vector<ScreenCandidate> dense_candidates{};
        std::vector<int> silhouette_min_ix{};
        std::vector<int> silhouette_max_ix{};
        std::vector<std::uint8_t> uv_bins{};
        std::chrono::steady_clock::time_point started{};
        std::chrono::steady_clock::time_point server_batch_started{};
        std::chrono::steady_clock::time_point server_next_batch_time{};
        UINT_PTR server_batch_timer_id{0};
        std::chrono::steady_clock::time_point last_tick{};
    };

    std::mutex g_template_uv_brush_mutex;
    std::shared_ptr<TemplateUvBrushAsyncJob> g_template_uv_brush_job{};

    auto write_front_samples_artifact(const std::shared_ptr<TemplateUvBrushAsyncJob>& job,
                                      const SdkFrontCaptureResult& capture) -> bool
    {
        if (!job)
        {
            return false;
        }
        std::string json{};
        json.reserve(256 + job->sample_pool.size() * 160);
        json += "{\"schema_version\":1";
        json += ",\"kind\":\"front_samples\"";
        json += ",\"color_space\":\"linear\"";
        json += ",\"mesh\":\"" + json_escape(early_hex_address(job->mesh)) + "\"";
        json += ",\"component\":\"" + json_escape(early_hex_address(job->component)) + "\"";
        json += ",\"viewport_width\":" + std::to_string(job->viewport_width);
        json += ",\"viewport_height\":" + std::to_string(job->viewport_height);
        json += ",\"capture_width\":" + std::to_string(capture.width);
        json += ",\"capture_height\":" + std::to_string(capture.height);
        json += ",\"capture_direction_x\":" + std::to_string(capture.capture_direction.X);
        json += ",\"capture_direction_y\":" + std::to_string(capture.capture_direction.Y);
        json += ",\"capture_direction_z\":" + std::to_string(capture.capture_direction.Z);
        json += ",\"sample_count\":" + std::to_string(job->sample_pool.size());
        json += ",\"samples\":[";
        for (std::size_t i = 0; i < job->sample_pool.size(); ++i)
        {
            const auto& point = job->sample_pool[i];
            if (i != 0)
            {
                json += ",";
            }
            json += "{\"x\":" + std::to_string(point.x);
            json += ",\"y\":" + std::to_string(point.y);
            json += ",\"u\":" + std::to_string(point.u);
            json += ",\"v\":" + std::to_string(point.v);
            json += ",\"r\":" + std::to_string(point.r);
            json += ",\"g\":" + std::to_string(point.g);
            json += ",\"b\":" + std::to_string(point.b);
            json += ",\"metallic\":" + std::to_string(point.metallic);
            json += ",\"roughness\":" + std::to_string(point.roughness);
            json += ",\"stroke_radius\":" + std::to_string(point.stroke_radius);
            json += "}";
        }
        json += "]}\n";
        return write_bridge_sidecar_text(L".front_samples.json", json);
    }

    auto is_template_uv_brush_request(const std::string& request) -> bool
    {
        return request.find("\"native_apply_mode\":\"template_brush_paint\"") != std::string::npos ||
               request.find("\"route\":\"f10_template_brush_paint\"") != std::string::npos;
    }

    void complete_template_uv_brush_job(const std::shared_ptr<TemplateUvBrushAsyncJob>& job, const std::string& response)
    {
        if (!job || !job->queued)
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            job->queued->response = response;
            job->queued->done = true;
        }
        g_paint_jobs_cv.notify_all();
    }

    auto template_add_template_point(const std::shared_ptr<TemplateUvBrushAsyncJob>& job,
                                   double screen_x,
                                   double screen_y,
                                   double u,
                                   double v) -> bool
    {
        if (!job)
        {
            return false;
        }
        if (!std::isfinite(screen_x) || !std::isfinite(screen_y) || !std::isfinite(u) || !std::isfinite(v))
        {
            return false;
        }
        u = clamp01(u);
        v = clamp01(v);
        constexpr int Quant = 2048;
        if (job->uv_bins.empty())
        {
            job->uv_bins.assign(Quant * Quant, 0);
        }
        const int qu = std::max(0, std::min(Quant - 1, static_cast<int>(u * static_cast<double>(Quant - 1) + 0.5)));
        const int qv = std::max(0, std::min(Quant - 1, static_cast<int>(v * static_cast<double>(Quant - 1) + 0.5)));
        const int qi = qv * Quant + qu;
        if (job->uv_bins[static_cast<std::size_t>(qi)] != 0)
        {
            ++job->dedupe_skipped;
            return false;
        }
        job->uv_bins[static_cast<std::size_t>(qi)] = 1;
        TemplateUvBrushAsyncJob::TemplatePoint point{};
        point.x = screen_x;
        point.y = screen_y;
        point.u = u;
        point.v = v;
        job->points.push_back(point);
        return true;
    }

    auto template_hit_to_point(const std::shared_ptr<TemplateUvBrushAsyncJob>& job, double screen_x, double screen_y) -> bool
    {
        ++job->runtime_hit_test_attempts;
        sdk::RuntimePaintableComponent_HitTestAtScreenPosition hit{};
        hit.MeshComponent = reinterpret_cast<void*>(job->mesh);
        hit.ScreenPosition = sdk::FVector2D{screen_x, screen_y};
        hit.PlayerController = reinterpret_cast<void*>(job->controller);
        hit.bUseCachedTriangles = true;
        std::string failure{};
        if (!process_event(job->component, job->hit_test_function, reinterpret_cast<std::uint8_t*>(&hit), failure) || !hit.ReturnValue.bSuccess)
        {
            ++job->runtime_hit_test_failures;
            return false;
        }
        if (template_add_template_point(job, screen_x, screen_y, hit.ReturnValue.HitUV.X, hit.ReturnValue.HitUV.Y))
        {
            ++job->runtime_hit_test_hits;
            return true;
        }
        return false;
    }

    auto template_build_dense_candidates(const std::shared_ptr<TemplateUvBrushAsyncJob>& job) -> void
    {
        if (!job || job->base_rows <= 0 || job->base_cols <= 0 || job->viewport_width <= 0 || job->viewport_height <= 0)
        {
            return;
        }
        job->dense_candidates.clear();
        job->silhouette_area_px = 0.0;

        struct RowInterval
        {
            double min_nx{0.0};
            double max_nx{0.0};
            bool valid{false};
        };

        std::vector<RowInterval> rows(static_cast<std::size_t>(std::max(1, job->dense_rows)));
        const double min_ny = clamp01(job->bbox_min_ny);
        const double max_ny = clamp01(job->bbox_max_ny);
        const double span_ny = std::max(0.001, max_ny - min_ny);
        const double span_height_px = std::max(1.0, span_ny * static_cast<double>(job->viewport_height));

        auto nearest_base_row = [&](double ny) -> int {
            const double normalized = clamp01((ny - 0.02) / 0.96);
            const int preferred = std::max(0, std::min(job->base_rows - 1, static_cast<int>(std::round(normalized * static_cast<double>(job->base_rows - 1)))));
            int best = -1;
            int best_distance = 1000000;
            for (int row = 0; row < job->base_rows; ++row)
            {
                if (row >= static_cast<int>(job->silhouette_min_ix.size()) ||
                    row >= static_cast<int>(job->silhouette_max_ix.size()) ||
                    job->silhouette_min_ix[static_cast<std::size_t>(row)] > job->silhouette_max_ix[static_cast<std::size_t>(row)])
                {
                    continue;
                }
                const int distance = std::abs(row - preferred);
                if (distance < best_distance)
                {
                    best = row;
                    best_distance = distance;
                }
            }
            return best;
        };

        auto build_rows = [&](int row_count, std::vector<RowInterval>& out_rows) -> double {
            row_count = std::max(1, row_count);
            out_rows.assign(static_cast<std::size_t>(row_count), RowInterval{});
            double area_px = 0.0;
            const double row_height_px = span_height_px / static_cast<double>(row_count);
            for (int row = 0; row < row_count; ++row)
            {
                const double ty = (static_cast<double>(row) + 0.5) / static_cast<double>(row_count);
                const double ny = min_ny + span_ny * ty;
                const int base_row = nearest_base_row(ny);
                if (base_row < 0)
                {
                    continue;
                }
                const int min_ix = std::max(0, job->silhouette_min_ix[static_cast<std::size_t>(base_row)] - 1);
                const int max_ix = std::min(job->base_cols - 1, job->silhouette_max_ix[static_cast<std::size_t>(base_row)] + 1);
                if (min_ix > max_ix)
                {
                    continue;
                }
                const double min_nx = clamp01(0.06 + (static_cast<double>(min_ix) / static_cast<double>(job->base_cols)) * 0.88);
                const double max_nx = clamp01(0.06 + (static_cast<double>(max_ix + 1) / static_cast<double>(job->base_cols)) * 0.88);
                if (max_nx <= min_nx)
                {
                    continue;
                }
                out_rows[static_cast<std::size_t>(row)] = RowInterval{min_nx, max_nx, true};
                area_px += (max_nx - min_nx) * static_cast<double>(job->viewport_width) * row_height_px;
            }
            return area_px;
        };

        const int probe_rows = std::max(1, job->base_rows * 2);
        job->silhouette_area_px = build_rows(probe_rows, rows);
        if (job->silhouette_area_px <= 0.0)
        {
            return;
        }

        const double paint_radius = std::max(0.0001, job->brush_radius);
        const double sampling_radius = std::max(0.0001, job->sampling_brush_radius);
        const double viewport_short_px = static_cast<double>(std::max(1, std::min(job->viewport_width, job->viewport_height)));
        job->coverage_brush_screen_radius_px = std::max(1.0, viewport_short_px * paint_radius);
        job->fill_sample_spacing_px = std::max(1.0, job->coverage_brush_screen_radius_px * 0.25);
        job->fill_sample_target = std::max(
            1,
            static_cast<int>(std::ceil(job->silhouette_area_px /
                                       std::max(1.0, job->fill_sample_spacing_px * job->fill_sample_spacing_px))));
        const double sampling_brush_screen_radius_px = std::max(1.0, viewport_short_px * sampling_radius);
        job->detail_sample_spacing_px = std::max(1.0, sampling_brush_screen_radius_px * 0.25);
        job->coverage_sample_spacing_px = job->detail_sample_spacing_px;
        job->coverage_sample_target = std::max(
            job->fill_sample_target,
            static_cast<int>(std::ceil(job->silhouette_area_px /
                                       std::max(1.0, job->coverage_sample_spacing_px * job->coverage_sample_spacing_px))));
        const double base_acceptance = job->base_attempts > 0
                                           ? static_cast<double>(job->base_hits) / static_cast<double>(job->base_attempts)
                                           : 0.0;
        job->coverage_estimated_acceptance = std::max(0.18, std::min(0.78, base_acceptance * 16.0));
        job->coverage_candidate_target = std::max(
            job->coverage_sample_target,
            static_cast<int>(std::ceil(static_cast<double>(job->coverage_sample_target) /
                                       std::max(0.01, job->coverage_estimated_acceptance))));
        job->coverage_candidate_spacing_px = std::max(
            1.0,
            std::sqrt(job->silhouette_area_px / static_cast<double>(std::max(1, job->coverage_candidate_target))));
        job->point_target = job->coverage_sample_target;
        job->dense_rows = std::max(
            1,
            std::min(job->viewport_height,
                     static_cast<int>(std::ceil(span_height_px / job->coverage_candidate_spacing_px))));
        job->silhouette_area_px = build_rows(job->dense_rows, rows);
        if (job->silhouette_area_px <= 0.0)
        {
            return;
        }
        job->points.reserve(static_cast<std::size_t>(std::max(job->point_target, static_cast<int>(job->points.size()))));

        std::vector<TemplateUvBrushAsyncJob::ScreenCandidate> candidates{};
        candidates.reserve(static_cast<std::size_t>(std::max(1, job->coverage_candidate_target)));
        for (int row = 0; row < job->dense_rows; ++row)
        {
            const auto& interval = rows[static_cast<std::size_t>(row)];
            if (!interval.valid)
            {
                continue;
            }
            const double ty = (static_cast<double>(row) + 0.5) / static_cast<double>(std::max(1, job->dense_rows));
            const double ny = min_ny + span_ny * ty;
            const double width_px = std::max(1.0, (interval.max_nx - interval.min_nx) * static_cast<double>(job->viewport_width));
            const int cols = std::max(1, static_cast<int>(std::ceil(width_px / job->coverage_candidate_spacing_px)));
            for (int col = 0; col < cols; ++col)
            {
                const double tx = (static_cast<double>(col) + 0.5) / static_cast<double>(cols);
                candidates.push_back(TemplateUvBrushAsyncJob::ScreenCandidate{interval.min_nx + (interval.max_nx - interval.min_nx) * tx, ny});
            }
        }

        if (static_cast<int>(candidates.size()) > job->coverage_candidate_target)
        {
            job->dense_candidates.reserve(static_cast<std::size_t>(job->coverage_candidate_target));
            for (int i = 0; i < job->coverage_candidate_target; ++i)
            {
                const std::size_t index = static_cast<std::size_t>((static_cast<unsigned long long>(i) * static_cast<unsigned long long>(candidates.size())) / static_cast<unsigned long long>(job->coverage_candidate_target));
                job->dense_candidates.push_back(candidates[std::min(index, candidates.size() - 1)]);
            }
        }
        else
        {
            job->dense_candidates.swap(candidates);
        }
        job->candidate_count = static_cast<int>(job->dense_candidates.size());
    }

    auto template_point_yx_less(const TemplateUvBrushAsyncJob::TemplatePoint& a,
                                const TemplateUvBrushAsyncJob::TemplatePoint& b) -> bool
    {
        if (a.y == b.y) return a.x < b.x;
        return a.y < b.y;
    }

    auto template_select_uniform_yx(std::vector<TemplateUvBrushAsyncJob::TemplatePoint> source,
                                    int target) -> std::vector<TemplateUvBrushAsyncJob::TemplatePoint>
    {
        if (target <= 0 || static_cast<int>(source.size()) <= target)
        {
            std::sort(source.begin(), source.end(), template_point_yx_less);
            return source;
        }

        std::sort(source.begin(), source.end(), template_point_yx_less);
        std::vector<TemplateUvBrushAsyncJob::TemplatePoint> selected{};
        selected.reserve(static_cast<std::size_t>(target));
        const auto source_count = source.size();
        for (int i = 0; i < target; ++i)
        {
            const auto index = static_cast<std::size_t>(
                std::min<double>(
                    static_cast<double>(source_count - 1),
                    std::floor(((static_cast<double>(i) + 0.5) * static_cast<double>(source_count)) /
                               static_cast<double>(target))));
            selected.push_back(source[index]);
        }
        return selected;
    }

    auto template_downsample_points_to_target(const std::shared_ptr<TemplateUvBrushAsyncJob>& job) -> void
    {
        if (!job)
        {
            return;
        }
        job->points_before_downsample = static_cast<int>(job->points.size());
        job->downsample_removed = 0;
        if (job->point_target <= 0 || static_cast<int>(job->points.size()) <= job->point_target)
        {
            std::sort(job->points.begin(), job->points.end(), template_point_yx_less);
            return;
        }

        auto selected = template_select_uniform_yx(job->points, job->point_target);
        job->downsample_removed = static_cast<int>(job->points.size() - selected.size());
        job->points.swap(selected);
    }

    auto template_configure_server_batch_stream(const std::shared_ptr<TemplateUvBrushAsyncJob>& job, int total_points) -> void
    {
        if (!job)
        {
            return;
        }
        (void)total_points;
        if (job->server_batch_limit <= 0)
            job->server_batch_limit = ServerPaintBatchStrokeLimit;
        if (job->server_batch_delay_ms <= 0)
            job->server_batch_delay_ms = ServerPaintBatchDelayMs;
    }

    auto start_template_uv_brush_async_job(const std::string& request, const std::shared_ptr<QueuedPaintJob>& queued_job) -> bool
    {
        {
            std::lock_guard<std::mutex> lock(g_template_uv_brush_mutex);
            if (g_template_uv_brush_job)
            {
                complete_template_uv_brush_job(std::make_shared<TemplateUvBrushAsyncJob>(TemplateUvBrushAsyncJob{queued_job}),
                                               response_json(false,
                                                             "template_busy",
                                                             0,
                                                             1,
                                                             "template brush job is already running",
                                                             "\"route\":\"template_brush_paint\""));
                return true;
            }
        }

        Reflection ref{};
        std::string failure{};
        if (!ref.init(failure))
        {
            complete_template_uv_brush_job(std::make_shared<TemplateUvBrushAsyncJob>(TemplateUvBrushAsyncJob{queued_job}),
                                           response_json(false,
                                                         "sdk_update_required",
                                                         0,
                                                         1,
                                                         failure.empty() ? "SDK reflection init failed" : failure,
                                                         "\"route\":\"template_brush_paint\",\"sdk_resolution_exception\":true"));
            return true;
        }
        SdkContext ctx{};
        try
        {
            ctx = sdk_resolve_context(ref);
        }
        catch (const SdkResolutionException& ex)
        {
            complete_template_uv_brush_job(std::make_shared<TemplateUvBrushAsyncJob>(TemplateUvBrushAsyncJob{queued_job}),
                                           response_json(false,
                                                         ex.stage.c_str(),
                                                         0,
                                                         1,
                                                         ex.what(),
                                                         "\"route\":\"template_brush_paint\",\"sdk_resolution_exception\":true"));
            return true;
        }
        std::string metadata = sdk_context_metadata(ref, ctx);
        metadata += ",\"route\":\"template_brush_paint\"";
        metadata += ",\"template_apply_backend\":\"server_paint_batch\"";
        metadata += ",\"replication\":\"server_paint_batch\"";
        metadata += ",\"template_pipeline\":\"phase0_template_points_server_batch\"";
        metadata += ",\"texture_import_used\":false";
        metadata += ",\"local_paint_used\":false";
        metadata += ",\"paint_at_uv_with_brush_used\":false";
        metadata += ",\"paint_at_uv_with_brush_production_forbidden\":true";
        metadata += ",\"server_paint_batch_used\":true";
        metadata += ",\"explicit_stroke_batch_used\":true";
        metadata += ",\"paint_at_screen_position_used\":false";
        metadata += ",\"live_set_hidden_in_game_used\":false";
        metadata += ",\"scene_capture_basecolor_required\":true";
        metadata += ",\"template_min_direct_points\":0";
        metadata += ",\"template_sample_count_fixed\":false";
        metadata += ",\"template_sample_target_mode\":\"sampling_radius_dynamic\"";
        metadata += ",\"two_pass_enabled\":false";
        metadata += ",\"single_pass_enabled\":true";
        metadata += ",\"single_pass_strategy\":\"fixed_radius_server_batch\"";
        metadata += ",\"template_paint_target_channel\":\"Albedo\"";
        metadata += ",\"template_material_channel_overwrite\":false";
        metadata += ",\"template_material_source\":\"preserve_existing_material_channels\"";
        metadata += ",\"template_paint_albedo_transfer\":\"basecolor_srgb_to_linear_flinearcolor\"";
        metadata += ",\"template_color_source\":\"scene_capture_basecolor_bulk_readback\"";
        metadata += ",\"template_profile\":\"high_density_basecolor_scene_capture_template\"";
        metadata += ",\"inferred_fields\":[\"brush_radius_ui_tuning\",\"scene_capture_basecolor_srgb_to_linear\"]";
        metadata += ",\"phase0_lower_rescan_used\":false";
        metadata += ",\"template_fill_enabled\":false";
        metadata += ",\"template_clone_enabled\":false";
        if (!ctx.ok)
        {
            complete_template_uv_brush_job(std::make_shared<TemplateUvBrushAsyncJob>(TemplateUvBrushAsyncJob{queued_job}),
                                           response_json(false, ctx.stage.c_str(), 0, 1, ctx.message, metadata));
            return true;
        }

        const auto viewport = sdk_get_viewport_info(ref, ctx);
        const auto mesh_candidates = sdk_collect_front_mesh_candidates(ref, ctx);
        const auto mesh = mesh_candidates.empty() ? 0 : mesh_candidates.front().mesh;
        const auto hit_test_function = ref.find_function(ctx.component, "HitTestAtScreenPosition");
        const auto screen_query = sdk_find_screen_space_brush_query(ref, ctx);
        const auto query_at_screen_function = screen_query ? ref.find_function(screen_query, "QueryAtScreenPosition") : 0;
        constexpr bool screen_query_configured = false;
        const auto server_paint_batch_function = ctx.server_paint_batch_function ?
            ctx.server_paint_batch_function :
            ref.find_function(ctx.component, "ServerPaintBatch");

        metadata += std::string(",\"viewport_available\":") + json_bool(viewport.width > 0 && viewport.height > 0);
        metadata += ",\"viewport_width\":" + std::to_string(viewport.width);
        metadata += ",\"viewport_height\":" + std::to_string(viewport.height);
        metadata += ",\"front_mesh_candidate_count\":" + std::to_string(mesh_candidates.size());
        metadata += ",\"front_mesh_candidates\":" + sdk_front_mesh_candidates_json(ref, mesh_candidates);
        metadata += std::string(",\"front_mesh_source\":\"") + (mesh_candidates.empty() ? "" : json_escape(mesh_candidates.front().source)) + "\"";
        metadata += std::string(",\"front_mesh_available\":") + json_bool(mesh != 0);
        metadata += std::string(",\"function_hit_test_at_screen_position_available\":") + json_bool(hit_test_function != 0);
        metadata += std::string(",\"screen_space_brush_query_available\":") + json_bool(screen_query != 0);
        metadata += std::string(",\"screen_space_brush_query\":\"") + hex_address(screen_query) + "\"";
        metadata += std::string(",\"function_query_at_screen_position_available\":") + json_bool(query_at_screen_function != 0);
        metadata += std::string(",\"screen_space_brush_query_configured\":") + json_bool(screen_query_configured);
        metadata += std::string(",\"screen_space_brush_query_used\":") + json_bool(screen_query_configured);
        metadata += ",\"screen_space_brush_query_production_disabled\":true";
        metadata += ",\"screen_space_brush_query_production_disabled_reason\":\"query_at_screen_position_caused_working_set_spike_and_slower_dense_sampling\"";
        metadata += ",\"runtime_hit_test_used\":true";
        metadata += std::string(",\"template_dense_sampling_backend\":\"") +
            (screen_query_configured ? "screen_space_brush_query_at_screen_position" : "runtime_paintable_hit_test_cached_triangles") + "\"";
        metadata += std::string(",\"function_server_paint_batch_available_for_production\":") + json_bool(server_paint_batch_function != 0);

        if (viewport.width <= 0 || viewport.height <= 0)
        {
            complete_template_uv_brush_job(std::make_shared<TemplateUvBrushAsyncJob>(TemplateUvBrushAsyncJob{queued_job}),
                                           response_json(false, "viewport_unavailable", 0, 1, "viewport size is unavailable", metadata));
            return true;
        }
        if (!mesh || !hit_test_function)
        {
            complete_template_uv_brush_job(std::make_shared<TemplateUvBrushAsyncJob>(TemplateUvBrushAsyncJob{queued_job}),
                                           response_json(false, "template_phase0_surface_unavailable", 0, 1, "front mesh HitTestAtScreenPosition is unavailable", metadata));
            return true;
        }
        if (!server_paint_batch_function)
        {
            complete_template_uv_brush_job(std::make_shared<TemplateUvBrushAsyncJob>(TemplateUvBrushAsyncJob{queued_job}),
                                           response_json(false,
                                                         "template_server_batch_unavailable",
                                                         0,
                                                         1,
                                                         "ServerPaintBatch is unavailable; local paint path is disabled",
                                                         metadata));
            return true;
        }

        constexpr std::uintptr_t OffCurrentBrushSettings = sdk::FieldOffsets::RuntimePaintable_CurrentBrushSettings;
        const double tuning_brush_radius = clamp_range(json_number_field(request, "brush_radius", 0.01), 0.001, 0.05);
        const double tuning_brush_spacing = clamp_range(json_number_field(request, "brush_spacing", 0.18), 0.01, 0.5);
        const double tuning_server_brush_spacing = clamp_range(json_number_field(request, "server_brush_spacing", 0.08), 0.01, 0.5);
        const int tuning_server_batch_limit = json_int_field(request, "server_batch_limit", ServerPaintBatchStrokeLimit, 1, ServerPaintBatchStrokeLimit);
        const int tuning_server_batch_delay_ms = json_int_field(request, "server_batch_delay_ms", ServerPaintBatchDelayMs, 1, 1000);
        const bool research_artifacts = request.find("\"research_artifacts\":true") != std::string::npos;

        auto job = std::make_shared<TemplateUvBrushAsyncJob>();
        job->queued = queued_job;
        job->world = ctx.world;
        job->controller = ctx.controller;
        job->component = ctx.component;
        job->mesh = mesh;
        job->hit_test_function = hit_test_function;
        job->server_paint_batch_function = server_paint_batch_function;
        job->viewport_width = viewport.width;
        job->viewport_height = viewport.height;
        safe_copy(&job->old_brush, reinterpret_cast<const void*>(ctx.component + OffCurrentBrushSettings), sizeof(job->old_brush));
        job->brush = job->old_brush;
        job->brush.Hardness = 1.0f;
        job->brush.Opacity = 1.0f;
        job->brush_radius_raw = tuning_brush_radius;
        job->brush_radius = tuning_brush_radius;
        job->sampling_brush_radius = job->brush_radius;
        job->base_probe_radius = job->brush_radius;
        job->brush_spacing = tuning_brush_spacing;
        job->server_brush_spacing = tuning_server_brush_spacing;
        job->server_batch_limit = tuning_server_batch_limit;
        job->server_batch_delay_ms = tuning_server_batch_delay_ms;
        job->research_artifacts = research_artifacts;
        job->brush.Radius = static_cast<float>(job->brush_radius);
        const double visible_probe_width_px = std::max(1.0, static_cast<double>(job->viewport_width) * 0.88);
        const double visible_probe_height_px = std::max(1.0, static_cast<double>(job->viewport_height) * 0.96);
        job->base_probe_spacing_px = std::max(
            1.0,
            std::sqrt(visible_probe_width_px * visible_probe_height_px) * job->base_probe_radius * 0.75);
        job->base_cols = std::max(1, static_cast<int>(std::ceil(visible_probe_width_px / job->base_probe_spacing_px)));
        job->base_rows = std::max(1, static_cast<int>(std::ceil(visible_probe_height_px / job->base_probe_spacing_px)));
        job->brush.Spacing = static_cast<float>(job->brush_spacing);
        job->brush.Falloff = sdk::EBrushFalloff::Spherical;
        job->brush.BlendMode = sdk::EPaintBlendMode::Normal;
        job->metadata = metadata;
        job->metadata += ",\"template_base_cols\":" + std::to_string(job->base_cols);
        job->metadata += ",\"template_base_rows\":" + std::to_string(job->base_rows);
        job->metadata += ",\"template_base_probe_spacing_px\":" + std::to_string(job->base_probe_spacing_px);
        job->metadata += ",\"template_base_probe_radius\":" + std::to_string(job->base_probe_radius);
        job->metadata += ",\"template_base_probe_policy\":\"fixed_brush_radius\"";
        job->metadata += ",\"template_base_probe_formula\":\"ceil(visible_probe_extent/(sqrt(visible_probe_area)*base_probe_radius*0.75))\"";
        job->metadata += ",\"template_explicit_stroke_batch_enabled\":true";
        job->metadata += ",\"template_explicit_stroke_batch_mode\":\"ui_tuned_server_batch_timer_drained\"";
        job->metadata += ",\"template_dense_order\":\"front_silhouette_interval_top_down\"";
        job->metadata += ",\"template_hittest_tick_chunk\":256";
        job->metadata += ",\"single_pass_radius\":" + std::to_string(job->brush_radius);
        job->metadata += ",\"tuning_brush_spacing\":" + std::to_string(job->brush_spacing);
        job->metadata += ",\"tuning_server_brush_spacing\":" + std::to_string(job->server_brush_spacing);
        job->metadata += ",\"tuning_server_batch_limit\":" + std::to_string(job->server_batch_limit);
        job->metadata += ",\"tuning_server_batch_delay_ms\":" + std::to_string(job->server_batch_delay_ms);
        job->metadata += ",\"template_sampling_radius_policy\":\"fixed_brush_radius\"";
        job->metadata += ",\"template_sampling_brush_radius\":" + std::to_string(job->sampling_brush_radius);
        job->metadata += std::string(",\"research_artifacts_requested\":") + json_bool(job->research_artifacts);
        job->started = std::chrono::steady_clock::now();
        job->last_tick = job->started;

        {
            std::lock_guard<std::mutex> lock(g_template_uv_brush_mutex);
            g_template_uv_brush_job = job;
        }
        write_bridge_progress("template_phase0_begin",
                              "template phase0 source generation started",
                              0,
                              100,
                              0.0,
                              "\"color_source\":\"scene_capture_basecolor_bulk_readback\"");
        if (const auto thread_id = g_game_thread_id.load())
        {
            PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
        }
        return true;
    }

    auto tick_template_uv_brush_async_job() -> void
    {
        std::shared_ptr<TemplateUvBrushAsyncJob> job{};
        {
            std::lock_guard<std::mutex> lock(g_template_uv_brush_mutex);
            job = g_template_uv_brush_job;
        }
        if (!job)
        {
            return;
        }

        auto clear_server_timer = [&]() {
            if (job->server_batch_timer_id)
            {
                KillTimer(nullptr, job->server_batch_timer_id);
                job->server_batch_timer_id = 0;
            }
        };
        clear_server_timer();
        auto post_next_after = [&](int delay_ms) {
            if (const auto thread_id = g_game_thread_id.load())
            {
                if (delay_ms <= 0)
                {
                    PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
                    return;
                }
                const auto timer_id = SetTimer(nullptr, 0, static_cast<UINT>(std::max(1, delay_ms)), nullptr);
                if (timer_id)
                {
                    job->server_batch_timer_id = timer_id;
                    return;
                }
                PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
            }
        };
        auto post_next = [&]() {
            post_next_after(0);
        };
        auto cleanup_state = [&]() {
            clear_server_timer();
        };
        auto fail_job = [&](const char* stage, const std::string& message) {
            cleanup_state();
            complete_template_uv_brush_job(job,
                                           response_json(false,
                                                         stage,
                                                         job->server_strokes_sent,
                                                         1,
                                                         message,
                                                         job->metadata + ",\"route\":\"template_brush_paint\",\"async_phase_failed\":true"));
            {
                std::lock_guard<std::mutex> lock(g_template_uv_brush_mutex);
                if (g_template_uv_brush_job == job)
                {
                    g_template_uv_brush_job.reset();
                }
            }
        };
        auto job_elapsed_ms = [&]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->started).count();
        };
        auto capture_replication_snapshot = [&]() -> SdkReplicationSnapshot {
            Reflection ref{};
            std::string failure{};
            if (!ref.init(failure))
            {
                SdkReplicationSnapshot snapshot{};
                snapshot.failure = "reflection_unavailable:" + failure;
                return snapshot;
            }
            return sdk_capture_replication_snapshot(ref, job->component);
        };

        job->last_tick = std::chrono::steady_clock::now();

        switch (job->phase)
        {
        case TemplateUvBrushAsyncJob::Phase::Phase0BaseGrid:
        {
            constexpr int chunk = 256;
            const int total = job->base_cols * job->base_rows;
            if (job->silhouette_min_ix.empty() || job->silhouette_max_ix.empty())
            {
                job->silhouette_min_ix.assign(static_cast<std::size_t>(std::max(1, job->base_rows)), 1000000);
                job->silhouette_max_ix.assign(static_cast<std::size_t>(std::max(1, job->base_rows)), -1);
            }
            const int end = std::min(total, job->next_index + chunk);
            for (; job->next_index < end; ++job->next_index)
            {
                const int ix = job->next_index % job->base_cols;
                const int iy = job->next_index / job->base_cols;
                const double nx = 0.06 + ((static_cast<double>(ix) + 0.5) / static_cast<double>(job->base_cols)) * 0.88;
                const double ny = 0.02 + ((static_cast<double>(iy) + 0.5) / static_cast<double>(job->base_rows)) * 0.96;
                const double screen_x = nx * static_cast<double>(job->viewport_width);
                const double screen_y = ny * static_cast<double>(job->viewport_height);
                ++job->base_attempts;

                sdk::RuntimePaintableComponent_HitTestAtScreenPosition hit{};
                hit.MeshComponent = reinterpret_cast<void*>(job->mesh);
                hit.ScreenPosition = sdk::FVector2D{screen_x, screen_y};
                hit.PlayerController = reinterpret_cast<void*>(job->controller);
                hit.bUseCachedTriangles = true;
                std::string failure{};
                if (!process_event(job->component, job->hit_test_function, reinterpret_cast<std::uint8_t*>(&hit), failure) || !hit.ReturnValue.bSuccess)
                {
                    continue;
                }
                ++job->base_hits;
                if (iy >= 0 && iy < job->base_rows)
                {
                    auto row = static_cast<std::size_t>(iy);
                    job->silhouette_min_ix[row] = std::min(job->silhouette_min_ix[row], ix);
                    job->silhouette_max_ix[row] = std::max(job->silhouette_max_ix[row], ix);
                }
                job->bbox_min_nx = std::min(job->bbox_min_nx, nx);
                job->bbox_min_ny = std::min(job->bbox_min_ny, ny);
                job->bbox_max_nx = std::max(job->bbox_max_nx, nx);
                job->bbox_max_ny = std::max(job->bbox_max_ny, ny);
            }
            const int percent = total > 0 ? static_cast<int>((static_cast<long long>(job->next_index) * 8LL) / total) : 8;
            if (percent != job->progress_percent)
            {
                job->progress_percent = percent;
                write_bridge_progress("template_phase0_base_grid",
                                      "phase0 base grid bounds",
                                      percent,
                                      100,
                                      job_elapsed_ms(),
                                      "\"base_hits\":" + std::to_string(job->base_hits) +
                                          ",\"base_attempts\":" + std::to_string(job->base_attempts));
            }
            if (job->next_index >= total)
            {
                if (job->base_hits <= 0 || job->bbox_max_nx <= job->bbox_min_nx || job->bbox_max_ny <= job->bbox_min_ny)
                {
                    fail_job("template_phase0_no_surface_bounds", "template phase0 base grid found no surface bounds");
                    return;
                }
                const double span_x = std::max(0.04, job->bbox_max_nx - job->bbox_min_nx);
                const double span_y = std::max(0.04, job->bbox_max_ny - job->bbox_min_ny);
                job->bbox_min_nx = clamp01(job->bbox_min_nx - span_x * 0.16);
                job->bbox_max_nx = clamp01(job->bbox_max_nx + span_x * 0.16);
                job->bbox_min_ny = clamp01(job->bbox_min_ny - span_y * 0.08);
                job->bbox_max_ny = clamp01(job->bbox_max_ny + span_y * 0.14);
                template_build_dense_candidates(job);
                if (job->dense_candidates.empty())
                {
                    fail_job("template_phase0_no_dense_candidates", "template phase0 silhouette produced no dense candidates");
                    return;
                }
                job->next_index = 0;
                job->progress_percent = -1;
                job->phase = TemplateUvBrushAsyncJob::Phase::Phase0Dense;
            }
            post_next();
            return;
        }
        case TemplateUvBrushAsyncJob::Phase::Phase0Dense:
        {
            constexpr int chunk = 256;
            const int total = static_cast<int>(job->dense_candidates.size());
            const int end = std::min(total, job->next_index + chunk);
            for (; job->next_index < end; ++job->next_index)
            {
                const auto& candidate = job->dense_candidates[static_cast<std::size_t>(job->next_index)];
                ++job->dense_attempts;
                if (template_hit_to_point(job,
                                          candidate.nx * static_cast<double>(job->viewport_width),
                                          candidate.ny * static_cast<double>(job->viewport_height)))
                {
                    ++job->dense_hits;
                }
            }
            const int percent = 8 + (total > 0 ? static_cast<int>((static_cast<long long>(job->next_index) * 24LL) / total) : 24);
            if (percent != job->progress_percent)
            {
                job->progress_percent = percent;
                write_bridge_progress("template_phase0_dense",
                                      "phase0 dense surface samples",
                                      percent,
                                      100,
                                      job_elapsed_ms(),
                                      "\"points\":" + std::to_string(job->points.size()) +
                                          ",\"dense_hits\":" + std::to_string(job->dense_hits) +
                                          ",\"dense_attempts\":" + std::to_string(job->dense_attempts) +
                                          ",\"dense_candidates\":" + std::to_string(job->candidate_count));
            }
            if (job->next_index >= total)
            {
                job->sampler_probe_attempts = job->base_attempts + job->dense_attempts;
                job->sampler_probe_misses = std::max(0, job->sampler_probe_attempts - job->base_hits - job->dense_hits);
                job->sample_pool = job->points;
                job->sample_pool_points = static_cast<int>(job->sample_pool.size());
                template_downsample_points_to_target(job);
                job->template_point_elapsed_ms = job_elapsed_ms();
                job->progress_percent = -1;
                std::sort(job->points.begin(), job->points.end(), template_point_yx_less);
                job->source_sorted = true;
                write_bridge_progress("template_points_done",
                                      "phase0 direct surface samples complete",
                                      34,
                                      100,
                                      job_elapsed_ms(),
                                      "\"points\":" + std::to_string(job->points.size()) +
                                          ",\"sample_pool_points\":" + std::to_string(job->sample_pool_points) +
                                          ",\"points_before_downsample\":" + std::to_string(job->points_before_downsample) +
                                          ",\"downsample_removed\":" + std::to_string(job->downsample_removed) +
                                          ",\"sampler_probe_attempts\":" + std::to_string(job->sampler_probe_attempts) +
                                          ",\"sampler_probe_misses\":" + std::to_string(job->sampler_probe_misses));
                job->progress_percent = -1;
                job->phase = TemplateUvBrushAsyncJob::Phase::CaptureSource;
            }
            post_next();
            return;
        }
        case TemplateUvBrushAsyncJob::Phase::CaptureSource:
        {
            if (job->sample_pool.empty())
            {
                job->sample_pool = job->points;
                job->sample_pool_points = static_cast<int>(job->sample_pool.size());
            }
            if (job->sample_pool.empty())
            {
                fail_job("template_phase0_no_template_points", "template phase0 produced no template points");
                return;
            }
            Reflection ref{};
            std::string init_failure{};
            if (!ref.init(init_failure))
            {
                fail_job("sdk_update_required", init_failure.empty() ? "SDK reflection init failed" : init_failure);
                return;
            }
            SdkContext ctx{};
            try
            {
                ctx = sdk_resolve_context(ref);
            }
            catch (const SdkResolutionException& ex)
            {
                fail_job(ex.stage.c_str(), ex.what());
                return;
            }
            const auto live_viewport = sdk_get_viewport_info(ref, ctx);
            if (live_viewport.width > 0 && live_viewport.height > 0 &&
                (live_viewport.width != job->viewport_width || live_viewport.height != job->viewport_height))
            {
                const double sx = static_cast<double>(live_viewport.width) / static_cast<double>(std::max(1, job->viewport_width));
                const double sy = static_cast<double>(live_viewport.height) / static_cast<double>(std::max(1, job->viewport_height));
                for (auto& point : job->points)
                {
                    point.x *= sx;
                    point.y *= sy;
                }
                for (auto& point : job->sample_pool)
                {
                    point.x *= sx;
                    point.y *= sy;
                }
                job->viewport_width = live_viewport.width;
                job->viewport_height = live_viewport.height;
                job->metadata += ",\"template_viewport_refreshed_before_capture\":true";
                job->metadata += ",\"template_live_viewport_width\":" + std::to_string(live_viewport.width);
                job->metadata += ",\"template_live_viewport_height\":" + std::to_string(live_viewport.height);
            }

            SdkNativeFrontSampleResult native_front{};
            native_front.mesh = job->mesh;
            native_front.hit_test_function = job->hit_test_function;
            native_front.viewport_width = job->viewport_width;
            native_front.viewport_height = job->viewport_height;
            native_front.sampling_backend = "template_points_cached_hit_test";
            native_front.min_front_hits = std::max(64, std::min(2048, static_cast<int>(job->sample_pool.size())));
            native_front.target_front_hits = static_cast<int>(job->sample_pool.size());
            native_front.hard_attempt_budget = job->base_attempts + job->dense_attempts;
            native_front.samples.reserve(job->sample_pool.size());
            for (const auto& point : job->sample_pool)
            {
                FrontSample sample{};
                sample.u = clamp01(point.u);
                sample.v = clamp01(point.v);
                sample.screen_nx = clamp01(point.x / static_cast<double>(std::max(1, job->viewport_width)));
                sample.screen_ny = clamp01(point.y / static_cast<double>(std::max(1, job->viewport_height)));
                sample.metallic = 0.0;
                sample.roughness = 0.65;
                native_front.samples.push_back(sample);
            }

            write_bridge_progress("template_phase0_basecolor_capture",
                                  "capturing hidden-target SceneCapture BaseColor",
                                  52,
                                  100,
                                  job_elapsed_ms(),
                                  "\"template_points\":" + std::to_string(job->points.size()) +
                                      ",\"sample_pool_points\":" + std::to_string(job->sample_pool.size()) +
                                      ",\"color_source\":\"scene_capture_basecolor_bulk_readback\"");

            constexpr int kTemplateCaptureMaxDimension = 4096;
            int capture_request_width = std::max(1, job->viewport_width);
            int capture_request_height = std::max(1, job->viewport_height);
            const int request_max_dimension = std::max(capture_request_width, capture_request_height);
            if (request_max_dimension > kTemplateCaptureMaxDimension)
            {
                const double scale = static_cast<double>(kTemplateCaptureMaxDimension) / static_cast<double>(request_max_dimension);
                capture_request_width = std::max(1, static_cast<int>(std::round(static_cast<double>(capture_request_width) * scale)));
                capture_request_height = std::max(1, static_cast<int>(std::round(static_cast<double>(capture_request_height) * scale)));
            }
            const auto capture_started = std::chrono::steady_clock::now();
            const auto capture = sdk_capture_front_colors(ref, ctx, native_front, capture_request_width, capture_request_height);
            const auto capture_elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - capture_started).count();
            job->template_capture_elapsed_ms = capture_elapsed_ms;
            job->metadata += sdk_capture_metadata(capture);
            job->metadata += ",\"scene_capture_source\":\"BaseColor\"";
            job->metadata += ",\"template_capture_request_width\":" + std::to_string(capture_request_width);
            job->metadata += ",\"template_capture_request_height\":" + std::to_string(capture_request_height);
            job->metadata += ",\"template_capture_elapsed_ms\":" + std::to_string(capture_elapsed_ms);
            if (!capture.bulk_readback_used || !capture.image_bulk_calibration_ok || capture.samples.empty())
            {
                fail_job("template_basecolor_capture_unavailable", "SceneCapture BaseColor bulk capture failed: " + capture.failure);
                return;
            }
            job->sample_pool.clear();
            job->sample_pool.reserve(capture.samples.size());
            for (const auto& sample : capture.samples)
            {
                TemplateUvBrushAsyncJob::TemplatePoint point{};
                point.x = clamp01(sample.screen_nx) * static_cast<double>(job->viewport_width);
                point.y = clamp01(sample.screen_ny) * static_cast<double>(job->viewport_height);
                point.u = clamp01(sample.u);
                point.v = clamp01(sample.v);
                point.r = sdk_srgb_to_linear_component(sample.r);
                point.g = sdk_srgb_to_linear_component(sample.g);
                point.b = sdk_srgb_to_linear_component(sample.b);
                point.metallic = clamp01(sample.metallic);
                point.roughness = clamp01(std::max(0.35, sample.roughness));
                point.stroke_radius = job->brush_radius;
                point.paint_pass = 0;
                point.has_color = true;
                job->sample_pool.push_back(point);
                ++job->basecolor_samples;
                job->rgb_min = std::min(job->rgb_min, std::min(point.r, std::min(point.g, point.b)));
                job->rgb_max = std::max(job->rgb_max, std::max(point.r, std::max(point.g, point.b)));
                job->rgb_sum_r += point.r;
                job->rgb_sum_g += point.g;
                job->rgb_sum_b += point.b;
                job->metallic_sum += point.metallic;
                job->roughness_sum += point.roughness;
            }
            job->sample_pool_points = static_cast<int>(job->sample_pool.size());
            if (job->research_artifacts)
            {
                job->front_samples_artifact_written = write_front_samples_artifact(job, capture);
                job->front_samples_artifact_count = job->front_samples_artifact_written ? job->sample_pool_points : 0;
            }
            job->color_source = "scene_capture_basecolor_bulk_readback";
            auto paint_points = template_select_uniform_yx(job->sample_pool, job->point_target);
            for (auto& point : paint_points)
            {
                point.stroke_radius = job->brush_radius;
                point.paint_pass = 0;
            }
            job->coverage_strokes = static_cast<int>(paint_points.size());
            job->detail_strokes = 0;
            job->points = std::move(paint_points);
            job->paint_sample_attempts = static_cast<int>(job->points.size());
            job->paint_sample_success = static_cast<int>(job->points.size());
            job->paint_sample_failures = 0;
            write_bridge_progress("template_phase0_basecolor_capture_done",
                                  "SceneCapture BaseColor bulk capture done",
                                  60,
                                  100,
                                  job_elapsed_ms(),
                                  std::string("\"color_source\":\"scene_capture_basecolor_bulk_readback\"") +
                                      ",\"bulk_readback_used\":" + json_bool(capture.bulk_readback_used) +
                                      ",\"capture_samples\":" + std::to_string(capture.samples.size()) +
                                      ",\"coverage_strokes\":" + std::to_string(job->coverage_strokes) +
                                      ",\"detail_strokes\":" + std::to_string(job->detail_strokes) +
                                      ",\"bulk_backend\":\"" + json_escape(capture.bulk_backend) + "\"");
            job->phase = TemplateUvBrushAsyncJob::Phase::BeginPaint;
            job->progress_percent = -1;
            post_next();
            return;
        }
        case TemplateUvBrushAsyncJob::Phase::BeginPaint:
        {
            const int template_count = static_cast<int>(job->points.size());
            if (template_count <= 0)
            {
                fail_job("template_points_unavailable", "template route produced no direct template points");
                return;
            }
            job->brush_radius_raw = job->brush_radius;
            job->brush.Radius = static_cast<float>(job->brush_radius);
            job->brush.Spacing = static_cast<float>(job->server_brush_spacing);
            template_configure_server_batch_stream(job, template_count);
            job->server_batch_started = std::chrono::steady_clock::now();
            write_bridge_progress("template_server_batch_begin",
                                  "template server batch stream prepared",
                                  62,
                                  100,
                                  job_elapsed_ms(),
                                      "\"template_points\":" + std::to_string(job->points.size()) +
                                      ",\"sample_pool_points\":" + std::to_string(job->sample_pool_points) +
                                      ",\"fill_sample_target\":" + std::to_string(job->fill_sample_target) +
                                      ",\"detail_sample_target\":" + std::to_string(job->point_target) +
                                      ",\"coverage_strokes\":" + std::to_string(job->coverage_strokes) +
                                      ",\"detail_strokes\":" + std::to_string(job->detail_strokes) +
                                      ",\"basecolor_samples\":" + std::to_string(job->basecolor_samples) +
                                      ",\"color_source\":\"" + json_escape(job->color_source) + "\"" +
                                      ",\"albedo_transfer\":\"basecolor_srgb_to_linear_flinearcolor\"" +
                                      ",\"paint_target_channel\":\"Albedo\"" +
                                      ",\"material_channel_overwrite\":false" +
                                      ",\"local_paint_used\":false" +
                                      ",\"brush_radius_mode\":\"ui_tuning\"" +
                                      ",\"brush_radius_formula\":\"config.brush_radius\"" +
                                      ",\"brush_radius_raw\":" + std::to_string(job->brush_radius_raw) +
                                      ",\"brush_radius\":" + std::to_string(job->brush_radius) +
                                      ",\"brush_spacing\":" + std::to_string(job->brush_spacing) +
                                      ",\"server_brush_spacing\":" + std::to_string(job->server_brush_spacing) +
                                      ",\"two_pass_enabled\":false" +
                                      ",\"single_pass_enabled\":true" +
                                      ",\"paint_send_order\":\"single_pass_top_down\"" +
                                      ",\"server_rpc\":\"ServerPaintBatch\"" +
                                      ",\"server_batch_limit\":" + std::to_string(job->server_batch_limit) +
                                      ",\"server_batch_limit_formula\":\"config.server_batch_limit\"" +
                                      ",\"server_batch_delay_ms\":" + std::to_string(job->server_batch_delay_ms) +
                                      ",\"server_batch_delay_formula\":\"config.server_batch_delay_ms\"" +
                                      ",\"server_batch_pacing_profile\":\"ui_tuned\"");
            job->replicate_index = 0;
            job->phase = TemplateUvBrushAsyncJob::Phase::ReplicateStrokes;
            post_next();
            return;
        }
        case TemplateUvBrushAsyncJob::Phase::ReplicateStrokes:
        {
            const int total_points = static_cast<int>(job->points.size());
            if (job->replicate_index >= total_points)
            {
                if (job->server_batch_started.time_since_epoch().count() != 0)
                {
                    job->server_batch_elapsed_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - job->server_batch_started).count();
                }
                job->replication_after_explicit_batches = capture_replication_snapshot();
                job->phase = TemplateUvBrushAsyncJob::Phase::Finish;
                post_next();
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            if (job->server_next_batch_time.time_since_epoch().count() != 0 &&
                now < job->server_next_batch_time)
            {
                const int remaining_ms = std::max(
                    1,
                    static_cast<int>(std::ceil(std::chrono::duration<double, std::milli>(
                        job->server_next_batch_time - now).count())));
                post_next_after(remaining_ms);
                return;
            }
            job->server_next_batch_time = {};

            const int count = std::max(1, std::min(job->server_batch_limit, total_points - job->replicate_index));
            std::vector<sdk::FPaintStroke> strokes{};
            strokes.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                const auto& point = job->points[static_cast<std::size_t>(job->replicate_index + i)];
                auto stroke_brush = job->brush;
                stroke_brush.Radius = static_cast<float>(point.stroke_radius > 0.0 ? point.stroke_radius : job->brush_radius);
                const auto channel = sdk_make_channel(point.r,
                                                      point.g,
                                                      point.b,
                                                      point.metallic,
                                                      point.roughness,
                                                      sdk::EPaintChannelApplyMode::Override);
                strokes.push_back(sdk_make_uv_stroke(point.u,
                                                     point.v,
                                                     channel,
                                                     stroke_brush,
                                                     sdk::EPaintChannel::Albedo));
            }

            SdkContext ctx{};
            ctx.component = job->component;
            ctx.server_paint_batch_function = job->server_paint_batch_function;
            std::string failure{};
            bool sent = false;
            if (job->server_paint_batch_function)
            {
                sent = sdk_call_server_paint_batch(ctx, strokes, 0, strokes.size(), failure);
                if (sent)
                {
                    job->server_batch_rpc = "ServerPaintBatch";
                }
            }

            if (!sent)
            {
                ++job->server_batch_failures;
                if (job->first_failure.empty())
                {
                    job->first_failure = failure.empty() ? "ServerPaintBatch_failed" : failure;
                }
                fail_job("template_explicit_stroke_batch_failed",
                         "ServerPaintBatch failed: " + job->first_failure);
                return;
            }

            job->explicit_stroke_batch_used = true;
            ++job->server_batch_success;
            ++job->server_batch_calls;
            job->server_strokes_sent += count;
            job->replicate_index += count;
            job->server_next_batch_time = std::chrono::steady_clock::now() +
                                          std::chrono::milliseconds(std::max(1, job->server_batch_delay_ms));

            const int percent = 98 + static_cast<int>((static_cast<long long>(job->replicate_index) * 1LL) / std::max(1, total_points));
            if (percent != job->progress_percent)
            {
                job->progress_percent = percent;
                write_bridge_progress("template_explicit_stroke_batch",
                                      "template explicit stroke batch stream",
                                      percent,
                                      100,
                                      job_elapsed_ms(),
                                      "\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent) +
                                          ",\"points\":" + std::to_string(job->points.size()) +
                                          ",\"coverage_strokes\":" + std::to_string(job->coverage_strokes) +
                                          ",\"detail_strokes\":" + std::to_string(job->detail_strokes) +
                                          ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls) +
                                          ",\"server_batch_rpc\":\"" + json_escape(job->server_batch_rpc) + "\"" +
                                          ",\"server_batch_limit\":" + std::to_string(job->server_batch_limit) +
                                          ",\"server_batch_delay_ms\":" + std::to_string(job->server_batch_delay_ms));
            }
            post_next_after(job->server_batch_delay_ms);
            return;
        }
        case TemplateUvBrushAsyncJob::Phase::Finish:
        {
            cleanup_state();

            const double denom = std::max(1, job->basecolor_samples);
            const double total_elapsed_ms = job_elapsed_ms();
            std::string metadata = job->metadata;
            metadata += std::string(",\"template_artifacts_written\":") + json_bool(job->front_samples_artifact_written);
            metadata += ",\"front_samples_artifact_count\":" + std::to_string(job->front_samples_artifact_count);
            metadata += ",\"color_source\":\"" + json_escape(job->color_source) + "\"";
            metadata += ",\"template_base_probe_spacing_px\":" + std::to_string(job->base_probe_spacing_px);
            metadata += ",\"phase0_base_hits\":" + std::to_string(job->base_hits);
            metadata += ",\"phase0_base_attempts\":" + std::to_string(job->base_attempts);
            metadata += ",\"phase0_dense_hits\":" + std::to_string(job->dense_hits);
            metadata += ",\"phase0_dense_attempts\":" + std::to_string(job->dense_attempts);
            metadata += ",\"template_candidate_rows\":" + std::to_string(job->dense_rows);
            metadata += ",\"dense_candidate_count\":" + std::to_string(job->candidate_count);
            metadata += ",\"silhouette_area_px\":" + std::to_string(job->silhouette_area_px);
            metadata += ",\"template_sample_count_fixed\":false";
            metadata += ",\"template_sample_target_mode\":\"sampling_radius_dynamic\"";
            metadata += ",\"template_sample_target_formula\":\"ceil(silhouette_area_px / pow(min(viewport_width,viewport_height)*brush_radius*0.25,2))\"";
            metadata += ",\"fill_sample_target\":" + std::to_string(job->fill_sample_target);
            metadata += ",\"template_sample_target\":" + std::to_string(job->coverage_sample_target);
            metadata += ",\"template_candidate_target_formula\":\"ceil(template_sample_target / estimated_dense_acceptance)\"";
            metadata += ",\"template_candidate_target\":" + std::to_string(job->coverage_candidate_target);
            metadata += ",\"template_brush_screen_radius_px\":" + std::to_string(job->coverage_brush_screen_radius_px);
            metadata += ",\"fill_sample_spacing_px\":" + std::to_string(job->fill_sample_spacing_px);
            metadata += ",\"detail_sample_spacing_px\":" + std::to_string(job->detail_sample_spacing_px);
            metadata += ",\"template_sample_spacing_px\":" + std::to_string(job->coverage_sample_spacing_px);
            metadata += ",\"template_candidate_spacing_px\":" + std::to_string(job->coverage_candidate_spacing_px);
            metadata += ",\"template_estimated_dense_acceptance\":" + std::to_string(job->coverage_estimated_acceptance);
            metadata += ",\"sampler_probe_attempts\":" + std::to_string(job->sampler_probe_attempts);
            metadata += ",\"sampler_probe_misses\":" + std::to_string(job->sampler_probe_misses);
            metadata += ",\"runtime_hit_test_attempts\":" + std::to_string(job->runtime_hit_test_attempts);
            metadata += ",\"runtime_hit_test_hits\":" + std::to_string(job->runtime_hit_test_hits);
            metadata += ",\"runtime_hit_test_failures\":" + std::to_string(job->runtime_hit_test_failures);
            metadata += ",\"template_fill_enabled\":false";
            metadata += ",\"template_fill_strategy\":\"disabled\"";
            metadata += ",\"template_clone_enabled\":false";
            metadata += ",\"template_clone_strategy\":\"disabled\"";
            metadata += ",\"phase0_lower_rescan_used\":false";
            metadata += ",\"template_points\":" + std::to_string(job->points.size());
            metadata += ",\"paint_samples_total\":" + std::to_string(job->points.size());
            metadata += ",\"sample_pool_points\":" + std::to_string(job->sample_pool_points);
            metadata += ",\"template_points_before_downsample\":" + std::to_string(job->points_before_downsample);
            metadata += ",\"template_downsample_removed\":" + std::to_string(job->downsample_removed);
            metadata += ",\"template_downsample_strategy\":\"uniform_yx_after_full_candidate_scan\"";
            metadata += ",\"template_color_sample_target\":" + std::to_string(job->point_target);
            metadata += std::string(",\"template_dense_early_stopped\":") +
                        json_bool(job->next_index < job->candidate_count);
            metadata += ",\"two_pass_enabled\":false";
            metadata += ",\"single_pass_enabled\":true";
            metadata += ",\"single_pass_strategy\":\"fixed_radius_server_batch\"";
            metadata += ",\"coverage_strokes\":" + std::to_string(job->coverage_strokes);
            metadata += ",\"detail_strokes\":" + std::to_string(job->detail_strokes);
            metadata += ",\"paint_send_order\":\"single_pass_top_down\"";
            metadata += ",\"paint_samples\":" + std::to_string(job->paint_sample_success);
            metadata += ",\"paint_sample_attempts\":" + std::to_string(job->paint_sample_attempts);
            metadata += ",\"paint_sample_success\":" + std::to_string(job->paint_sample_success);
            metadata += ",\"paint_sample_failures\":" + std::to_string(job->paint_sample_failures);
            metadata += ",\"template_dedupe_skipped\":" + std::to_string(job->dedupe_skipped);
            metadata += ",\"template_brush_radius_mode\":\"ui_tuning\"";
            metadata += ",\"template_brush_radius_formula\":\"config.brush_radius\"";
            metadata += ",\"template_brush_radius_raw\":" + std::to_string(job->brush_radius_raw);
            metadata += ",\"template_brush_radius\":" + std::to_string(job->brush_radius);
            metadata += ",\"template_brush_spacing\":" + std::to_string(job->brush_spacing);
            metadata += ",\"server_brush_spacing\":" + std::to_string(job->server_brush_spacing);
            metadata += ",\"template_sampling_radius_policy\":\"fixed_brush_radius\"";
            metadata += ",\"template_sampling_brush_radius\":" + std::to_string(job->sampling_brush_radius);
            metadata += ",\"template_point_elapsed_ms\":" + std::to_string(job->template_point_elapsed_ms);
            metadata += ",\"capture_elapsed_ms\":" + std::to_string(job->template_capture_elapsed_ms);
            metadata += ",\"server_batch_elapsed_ms\":" + std::to_string(job->server_batch_elapsed_ms);
            metadata += ",\"total_elapsed_ms\":" + std::to_string(total_elapsed_ms);
            metadata += ",\"local_paint_used\":false";
            metadata += ",\"local_paint_success\":0";
            metadata += ",\"basecolor_samples\":" + std::to_string(job->basecolor_samples);
            metadata += ",\"paint_uv_success\":0";
            metadata += ",\"paint_process_failures\":0";
            metadata += ",\"template_commit_pulses\":" + std::to_string(job->commit_pulses);
            metadata += ",\"clear_recorded_strokes_called\":false";
            metadata += ",\"begin_stroke_called\":false";
            metadata += ",\"end_stroke_called\":false";
            metadata += ",\"flush_recorded_strokes_to_server_called\":false";
            metadata += ",\"send_stroke_batch_to_server_called\":false";
            metadata += std::string(",\"explicit_stroke_batch_used\":") + json_bool(job->explicit_stroke_batch_used);
            metadata += ",\"server_paint_batch_used\":true";
            metadata += ",\"server_batch_rpc\":\"" + json_escape(job->server_batch_rpc) + "\"";
            metadata += ",\"server_batch_limit\":" + std::to_string(job->server_batch_limit);
            metadata += ",\"server_batch_limit_formula\":\"config.server_batch_limit\"";
            metadata += ",\"server_batch_delay_ms\":" + std::to_string(job->server_batch_delay_ms);
            metadata += ",\"server_batch_delay_formula\":\"config.server_batch_delay_ms\"";
            metadata += ",\"server_batch_pacing_profile\":\"ui_tuned\"";
            metadata += ",\"server_batch_schedule\":\"timer_drained\"";
            metadata += ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls);
            metadata += ",\"server_batch_success\":" + std::to_string(job->server_batch_success);
            metadata += ",\"server_batch_failures\":" + std::to_string(job->server_batch_failures);
            metadata += ",\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent);
            metadata += ",\"recorded_stroke_replication_diagnostics\":false";
            metadata += sdk_replication_snapshot_metadata("rep_after_explicit_batches", job->replication_after_explicit_batches);
            metadata += ",\"basecolor_rgb_min\":" + std::to_string(job->rgb_min);
            metadata += ",\"basecolor_rgb_max\":" + std::to_string(job->rgb_max);
            metadata += ",\"basecolor_rgb_avg_r\":" + std::to_string(job->rgb_sum_r / denom);
            metadata += ",\"basecolor_rgb_avg_g\":" + std::to_string(job->rgb_sum_g / denom);
            metadata += ",\"basecolor_rgb_avg_b\":" + std::to_string(job->rgb_sum_b / denom);
            metadata += ",\"basecolor_metallic_avg\":" + std::to_string(job->metallic_sum / denom);
            metadata += ",\"basecolor_roughness_avg\":" + std::to_string(job->roughness_sum / denom);
            metadata += std::string(",\"first_failure\":\"") + json_escape(job->first_failure) + "\"";
            metadata += ",\"bridge_events\":[\"template_phase0_begin\",\"template_points_done\",\"template_load_done\"]";

            write_bridge_progress("template_load_done",
                                  "template brush paint completed",
                                  100,
                                  100,
                                  job_elapsed_ms(),
                                  "\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent) +
                                      ",\"basecolor_samples\":" + std::to_string(job->basecolor_samples) +
                                      ",\"coverage_strokes\":" + std::to_string(job->coverage_strokes) +
                                      ",\"points\":" + std::to_string(job->points.size()));
            const bool explicit_replication_ok = job->explicit_stroke_batch_used &&
                                                 job->server_strokes_sent > 0 &&
                                                 job->server_batch_failures == 0;
            const bool ok = explicit_replication_ok;
            complete_template_uv_brush_job(job,
                                           response_json(ok,
                                                         ok ? "template_brush_paint_done" : "template_commit_failed",
                                                         job->server_strokes_sent,
                                                         ok ? 0 : 1,
                                                         ok ? "template server batch paint dispatched" : "template server batch paint did not replicate",
                                                         metadata));
            {
                std::lock_guard<std::mutex> lock(g_template_uv_brush_mutex);
                if (g_template_uv_brush_job == job)
                {
                    g_template_uv_brush_job.reset();
                }
            }
            return;
        }
        }
    }


    auto paint_full_route_native_direct(const std::string& request) -> std::string
    {
        (void)request;
        return response_json(false,
                             "unsupported_route",
                             0,
                             1,
                             "unsupported native command",
                             "\"supported_native_apply_modes\":[\"mesh_first_paint\"]");
    }

    auto drain_paint_jobs_on_game_thread() -> void
    {
        tick_mesh_first_batch_async_job();
        tick_template_uv_brush_async_job();

        std::vector<std::shared_ptr<QueuedPaintJob>> jobs{};
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            jobs.swap(g_paint_jobs);
        }
        for (const auto& job : jobs)
        {
            if (!job)
            {
                continue;
            }
            if (is_mesh_first_paint_request(job->request))
            {
                start_mesh_first_paint_async_job(job->request, job);
                continue;
            }
            const auto response = paint_full_route_native_direct(job->request);
            complete_queued_paint_job(job, response);
        }

        tick_mesh_first_batch_async_job();
        tick_template_uv_brush_async_job();
    }

    void __fastcall hooked_process_event(void* object, void* function, void* params)
    {
        const auto original = g_original_process_event.load();
        if (!g_inside_process_event_hook)
        {
            g_inside_process_event_hook = true;
            __try
            {
                drain_paint_jobs_on_game_thread();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
            g_inside_process_event_hook = false;
        }
        if (original)
        {
            reinterpret_cast<ProcessEventFn>(original)(object, function, params);
        }
    }

    LRESULT CALLBACK message_hook_proc(int code, WPARAM wparam, LPARAM lparam)
    {
        if (code >= 0)
        {
            __try
            {
                drain_paint_jobs_on_game_thread();
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
        return CallNextHookEx(g_message_hook.load(), code, wparam, lparam);
    }

    auto paint_full_route_native(const std::string& request) -> std::string
    {
        std::string failure{};
        if (!install_process_event_hook(failure))
        {
            return response_json(false, failure.c_str(), 0, 1, failure);
        }
        auto job = std::make_shared<QueuedPaintJob>();
        job->request = request;
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            g_paint_jobs.push_back(job);
        }
        g_paint_jobs_cv.notify_all();
        if (const auto thread_id = g_game_thread_id.load())
        {
            PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
        }
        std::unique_lock<std::mutex> lock(g_paint_jobs_mutex);
        const bool completed = g_paint_jobs_cv.wait_for(lock, std::chrono::seconds(240), [&]() {
            return job->done;
        });
        if (!completed)
        {
            return response_json(false, "game_thread_dispatch_timeout", 0, 1, "game thread did not process paint job");
        }
        return job->response;
    }

    auto handle_request(const std::string& line) -> std::string
    {
        if (line.find("\"type\":\"ping\"") != std::string::npos)
        {
            return response_json(true, "ping", 0, 0, "pong");
        }
        if (line.find("\"type\":\"capabilities\"") != std::string::npos)
        {
            std::string commands = "[\"ping\",\"capabilities\",\"paint_full_route\",\"shutdown\"]";
            return std::string("{\"success\":true,\"stage\":\"capabilities\",\"applied\":0,\"failures\":0,") +
                   "\"message\":\"ok\",\"timing_ms\":{}," +
                   "\"metadata\":{\"commands\":" + commands + "," +
                   "\"sdk\":\"runtime_dynamic_reflection_min\"," +
                   "\"paint_full_route\":\"mesh_first_paint\"," +
                   "\"texture_import_used\":false," +
                   "\"local_paint_used\":false," +
                   "\"paint_at_uv_with_brush_used\":false," +
                   "\"replication\":\"server_paint_batch\"," +
                   "\"multiplayer_replicated\":true}}\n";
        }
        if (line.find("\"type\":\"shutdown\"") != std::string::npos)
        {
            uninstall_process_event_hook();
            g_running.store(false);
            return response_json(true, "shutdown", 0, 0, "bridge shutdown requested");
        }
        if (line.find("\"type\":\"paint_full_route\"") != std::string::npos)
        {
            return paint_full_route_native(line);
        }
        return response_json(false, "unknown_command", 0, 1, "unknown bridge command");
    }

    auto bridge_thread() -> void
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            return;
        }
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            WSACleanup();
            return;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(resolve_bridge_port()));
        const int yes = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR || listen(listener, 4) == SOCKET_ERROR)
        {
            closesocket(listener);
            WSACleanup();
            return;
        }
        auto last_activity = std::chrono::steady_clock::now();
        while (g_running.load())
        {
            fd_set read_set{};
            FD_ZERO(&read_set);
            FD_SET(listener, &read_set);
            timeval timeout{};
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            const int selected = select(0, &read_set, nullptr, nullptr, &timeout);
            if (selected == SOCKET_ERROR)
            {
                break;
            }
            if (selected == 0)
            {
                const auto idle_seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - last_activity).count();
                if (!g_process_event_hook_installed.load() && idle_seconds >= IdleShutdownSeconds)
                {
                    break;
                }
                continue;
            }
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                continue;
            }
            last_activity = std::chrono::steady_clock::now();
            const int timeout_ms = 5000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
            std::string request{};
            request.reserve(65536);
            char buffer[16384]{};
            while (request.size() < MaxRequestBytes)
            {
                const int received = recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
                if (received <= 0)
                {
                    break;
                }
                request.append(buffer, static_cast<std::size_t>(received));
                if (request.find('\n') != std::string::npos)
                {
                    break;
                }
            }
            if (!request.empty())
            {
                const std::string response = request.size() >= MaxRequestBytes
                                                 ? response_json(false, "request_too_large", 0, 1, "bridge request exceeded max size")
                                                 : handle_request(request);
                send(client, response.c_str(), static_cast<int>(response.size()), 0);
            }
            closesocket(client);
        }
        closesocket(listener);
        WSACleanup();
        uninstall_process_event_hook();
        HMODULE module = g_module;
        g_module = nullptr;
        if (module != nullptr)
        {
            FreeLibraryAndExitThread(module, 0);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        g_module = module;
        std::thread(bridge_thread).detach();
    }
    if (reason == DLL_PROCESS_DETACH)
    {
        g_running.store(false);
    }
    return TRUE;
}
