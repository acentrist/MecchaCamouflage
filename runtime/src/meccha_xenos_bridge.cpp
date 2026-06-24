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
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "../sdk/meccha_mesh_layout.hpp"
#include "../sdk/meccha_sdk_min.hpp"

#pragma comment(lib, "Ws2_32.lib")

namespace
{
    constexpr int DefaultBridgePort = 47654;
    constexpr int IdleShutdownSeconds = 15;
    constexpr std::size_t MaxRequestBytes = 8 * 1024 * 1024;
    constexpr int PaintChannelAlbedoMetallicRoughness = 5;
    constexpr int ProcessEventVtableIndex = meccha_sdk::Offsets::ProcessEventIdx;
    constexpr UINT PaintDispatchMessage = WM_APP + 0x4D43;

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
        out += "\",\"timing_ms\":{},\"metadata\":{\"bridge\":\"meccha-xenos-bridge\"";
        if (!metadata.empty())
        {
            out += ",";
            out += metadata;
        }
        out += "}}\n";
        return out;
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

    auto write_runtime_artifact_binary(const wchar_t* filename, const std::string& bytes) -> bool
    {
        const auto dir = runtime_log_dir_path();
        if (dir.empty())
        {
            return false;
        }
        std::wstring path = dir + L"\\";
        path += filename;
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

    auto write_runtime_artifact_text(const wchar_t* filename, const std::string& text) -> bool
    {
        return write_runtime_artifact_binary(filename, text);
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
            auto out = names.resolve(safe_read<std::uint32_t>(object + OffName));
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

    struct PaintCallStats
    {
        int server_success{0};
        int server_failure{0};
        int local_success{0};
        int local_failure{0};
        std::string first_failure{};
    };

    struct ScriptArrayParam
    {
        void* data{nullptr};
        int num{0};
        int max{0};
    };

    struct ChannelBuffer
    {
        bool ok{false};
        int channel{0};
        int width{0};
        int height{0};
        int bytes_per_pixel{1};
        std::uint64_t hash{1469598103934665603ULL};
        std::string failure{};
        std::vector<std::uint8_t> bytes{};
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
        double radius{0.012};
        bool floor_like{false};
        int atlas_priority{0};
        int atlas_radius{2};
        double atlas_weight{0.0};
        double screen_nx{0.5};
        double screen_ny{0.5};
        double capture_nx{-1.0};
        double capture_ny{-1.0};
        bool has_world_position{false};
        meccha_sdk::FVector world_position{};
        meccha_sdk::FVector normal{};
    };

    auto clamp01(double value) -> double;
    auto prop_offset(std::uintptr_t prop) -> int;
    auto prop_element_size(std::uintptr_t prop) -> int;
    auto write_number(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, double value) -> bool;
    auto process_event(std::uintptr_t object, std::uintptr_t function, std::uint8_t* params, std::string& failure) -> bool;
    auto read_return_bool(Reflection& ref, std::uintptr_t function, std::uint8_t* params) -> bool;

    auto fnv1a_update(std::uint64_t hash, const void* data, std::size_t size) -> std::uint64_t
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<std::uint64_t>(bytes[i]);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    auto hash_bytes(const std::vector<std::uint8_t>& bytes) -> std::uint64_t
    {
        return bytes.empty() ? 1469598103934665603ULL : fnv1a_update(1469598103934665603ULL, bytes.data(), bytes.size());
    }

    auto infer_channel_dimensions(std::size_t byte_count) -> std::pair<int, int>
    {
        if (byte_count == 0)
        {
            return {0, 0};
        }
        const auto pixels_rgba = byte_count % 4 == 0 ? byte_count / 4 : byte_count;
        int width = static_cast<int>(std::sqrt(static_cast<double>(pixels_rgba)));
        width = std::max(1, width);
        while (width > 1 && pixels_rgba % static_cast<std::size_t>(width) != 0)
        {
            --width;
        }
        const int height = static_cast<int>(pixels_rgba / static_cast<std::size_t>(width));
        return {width, std::max(1, height)};
    }

    auto find_array_param(Reflection& ref, std::uintptr_t function) -> std::uintptr_t
    {
        std::uintptr_t fallback = 0;
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (name == "returnvalue" || contains_text(name, "channel"))
            {
                continue;
            }
            const auto offset = prop_offset(prop);
            if (offset >= 0 && params_size > 0 && offset + static_cast<int>(sizeof(ScriptArrayParam)) <= params_size)
            {
                if (!fallback)
                {
                    fallback = prop;
                }
                if (contains_text(name, "byte") || contains_text(name, "data") || contains_text(name, "buffer") ||
                    contains_text(name, "array") || contains_text(name, "out"))
                {
                    return prop;
                }
            }
        }
        return fallback;
    }

    auto write_channel_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params, int channel) -> bool
    {
        bool wrote = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (contains_text(name, "channel"))
            {
                wrote = write_number(ref, prop, params, static_cast<double>(channel)) || wrote;
            }
        }
        return wrote;
    }

    auto export_channel_bytes(Reflection& ref, std::uintptr_t component, int channel) -> ChannelBuffer
    {
        ChannelBuffer out{};
        out.channel = channel;
        const auto function = ref.find_function(component, "ExportChannelToBytes");
        if (!function)
        {
            out.failure = "export_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 8192)
        {
            out.failure = "export_params_size_invalid";
            return out;
        }
        const auto array_prop = find_array_param(ref, function);
        if (!array_prop)
        {
            out.failure = "export_array_param_unavailable";
            return out;
        }
        const auto array_offset = prop_offset(array_prop);
        if (array_offset < 0)
        {
            out.failure = "export_array_offset_invalid";
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        write_channel_param(ref, function, params.data(), channel);
        std::string failure{};
        if (!process_event(component, function, params.data(), failure))
        {
            out.failure = "export_process_event_failed:" + failure;
            return out;
        }
        const bool return_ok = read_return_bool(ref, function, params.data());
        const auto array = safe_read<ScriptArrayParam>(reinterpret_cast<std::uintptr_t>(params.data() + array_offset));
        if (!array.data || array.num <= 0 || array.num > 128 * 1024 * 1024)
        {
            out.failure = return_ok ? "export_array_invalid" : "export_return_false_array_invalid";
            return out;
        }
        out.bytes.resize(static_cast<std::size_t>(array.num));
        if (!safe_copy(out.bytes.data(), array.data, out.bytes.size()))
        {
            out.failure = "export_array_copy_failed";
            out.bytes.clear();
            return out;
        }
        out.hash = hash_bytes(out.bytes);
        const auto [width, height] = infer_channel_dimensions(out.bytes.size());
        out.width = width;
        out.height = height;
        out.bytes_per_pixel = (width > 0 && height > 0 && out.bytes.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) ? 4 : 1;
        out.ok = true;
        if (!return_ok)
        {
            out.failure = "export_return_false_but_array_observed";
        }
        return out;
    }

    auto import_channel_bytes(Reflection& ref, std::uintptr_t component, int channel, const std::vector<std::uint8_t>& bytes, std::string& failure) -> bool
    {
        failure.clear();
        const auto function = ref.find_function(component, "ImportChannelFromBytes");
        if (!function)
        {
            failure = "import_unavailable";
            return false;
        }
        if (bytes.empty() || bytes.size() > 128 * 1024 * 1024)
        {
            failure = "import_bytes_invalid";
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 8192)
        {
            failure = "import_params_size_invalid";
            return false;
        }
        const auto array_prop = find_array_param(ref, function);
        if (!array_prop)
        {
            failure = "import_array_param_unavailable";
            return false;
        }
        const auto array_offset = prop_offset(array_prop);
        if (array_offset < 0)
        {
            failure = "import_array_offset_invalid";
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        write_channel_param(ref, function, params.data(), channel);
        auto* array = reinterpret_cast<ScriptArrayParam*>(params.data() + array_offset);
        array->data = const_cast<std::uint8_t*>(bytes.data());
        array->num = static_cast<int>(bytes.size());
        array->max = static_cast<int>(bytes.size());
        if (!process_event(component, function, params.data(), failure))
        {
            failure = "import_process_event_failed:" + failure;
            return false;
        }
        if (!read_return_bool(ref, function, params.data()))
        {
            failure = "import_return_false_observation_required";
        }
        return true;
    }

    auto parse_front_samples(const std::string& request, int limit = 4096) -> std::vector<FrontSample>
    {
        std::vector<FrontSample> samples{};
        std::size_t pos = request.find("\"front_samples\"");
        if (pos == std::string::npos)
        {
            return samples;
        }
        while (static_cast<int>(samples.size()) < limit)
        {
            const auto up = request.find("\"u\":", pos);
            if (up == std::string::npos)
            {
                break;
            }
            const auto vp = request.find("\"v\":", up);
            const auto rp = request.find("\"r\":", vp == std::string::npos ? up : vp);
            const auto gp = request.find("\"g\":", rp == std::string::npos ? up : rp);
            const auto bp = request.find("\"b\":", gp == std::string::npos ? up : gp);
            if (vp == std::string::npos || rp == std::string::npos || gp == std::string::npos || bp == std::string::npos)
            {
                break;
            }
            const auto sample_end = request.find('}', bp);
            char* end = nullptr;
            FrontSample sample{};
            sample.u = clamp01(std::strtod(request.c_str() + up + 4, &end));
            sample.v = clamp01(std::strtod(request.c_str() + vp + 4, &end));
            sample.r = clamp01(std::strtod(request.c_str() + rp + 4, &end));
            sample.g = clamp01(std::strtod(request.c_str() + gp + 4, &end));
            sample.b = clamp01(std::strtod(request.c_str() + bp + 4, &end));
            const auto roughp = request.find("\"roughness\":", bp);
            if (roughp != std::string::npos && sample_end != std::string::npos && roughp < sample_end)
            {
                sample.roughness = clamp01(std::strtod(request.c_str() + roughp + 12, &end));
            }
            const auto radiusp = request.find("\"radius\":", bp);
            if (radiusp != std::string::npos && sample_end != std::string::npos && radiusp < sample_end)
            {
                sample.radius = std::min(0.08, std::max(0.001, std::strtod(request.c_str() + radiusp + 9, &end)));
            }
            const auto wxp = request.find("\"world_x\":", bp);
            const auto wyp = request.find("\"world_y\":", bp);
            const auto wzp = request.find("\"world_z\":", bp);
            if (sample_end != std::string::npos &&
                wxp != std::string::npos && wyp != std::string::npos && wzp != std::string::npos &&
                wxp < sample_end && wyp < sample_end && wzp < sample_end)
            {
                sample.world_position.X = std::strtod(request.c_str() + wxp + 10, &end);
                sample.world_position.Y = std::strtod(request.c_str() + wyp + 10, &end);
                sample.world_position.Z = std::strtod(request.c_str() + wzp + 10, &end);
                sample.has_world_position = true;
            }
            samples.push_back(sample);
            pos = bp + 4;
        }
        return samples;
    }

    auto fill_channel(std::vector<std::uint8_t>& bytes, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) -> void
    {
        if (bytes.empty())
        {
            return;
        }
        if (bytes.size() % 4 == 0)
        {
            for (std::size_t i = 0; i + 3 < bytes.size(); i += 4)
            {
                bytes[i + 0] = r;
                bytes[i + 1] = g;
                bytes[i + 2] = b;
                bytes[i + 3] = a;
            }
            return;
        }
        std::fill(bytes.begin(), bytes.end(), r);
    }

    auto paint_disc(std::vector<std::uint8_t>& bytes, int width, int height, const FrontSample& sample, bool albedo) -> void
    {
        if (bytes.empty() || width <= 0 || height <= 0)
        {
            return;
        }
        const bool rgba = bytes.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        const int cx = std::min(width - 1, std::max(0, static_cast<int>(sample.u * static_cast<double>(width))));
        const int cy = std::min(height - 1, std::max(0, static_cast<int>((1.0 - sample.v) * static_cast<double>(height))));
        const int radius = std::max(1, static_cast<int>(sample.radius * static_cast<double>(std::min(width, height))));
        const int r2 = radius * radius;
        const auto rb = static_cast<std::uint8_t>(std::round(clamp01(sample.r) * 255.0));
        const auto gb = static_cast<std::uint8_t>(std::round(clamp01(sample.g) * 255.0));
        const auto bb = static_cast<std::uint8_t>(std::round(clamp01(sample.b) * 255.0));
        const auto scalar = static_cast<std::uint8_t>(std::round(clamp01(sample.roughness) * 255.0));
        for (int y = std::max(0, cy - radius); y <= std::min(height - 1, cy + radius); ++y)
        {
            for (int x = std::max(0, cx - radius); x <= std::min(width - 1, cx + radius); ++x)
            {
                const int dx = x - cx;
                const int dy = y - cy;
                if ((dx * dx + dy * dy) > r2)
                {
                    continue;
                }
                const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                if (rgba)
                {
                    const auto offset = pixel * 4;
                    bytes[offset + 0] = albedo ? rb : scalar;
                    bytes[offset + 1] = albedo ? gb : scalar;
                    bytes[offset + 2] = albedo ? bb : scalar;
                    bytes[offset + 3] = 255;
                }
                else if (pixel < bytes.size())
                {
                    bytes[pixel] = albedo ? rb : scalar;
                }
            }
        }
    }

    auto normalize_material_channel_layout(ChannelBuffer& channel, const ChannelBuffer& albedo) -> void
    {
        if (!channel.ok || !albedo.ok || albedo.width <= 0 || albedo.height <= 0)
        {
            return;
        }
        const auto pixels = static_cast<std::size_t>(albedo.width) * static_cast<std::size_t>(albedo.height);
        if (pixels == 0)
        {
            return;
        }
        if (channel.bytes.size() == pixels)
        {
            channel.width = albedo.width;
            channel.height = albedo.height;
            channel.bytes_per_pixel = 1;
        }
        else if (channel.bytes.size() == pixels * 4)
        {
            channel.width = albedo.width;
            channel.height = albedo.height;
            channel.bytes_per_pixel = 4;
        }
    }

    auto fill_material_channel(std::vector<std::uint8_t>& bytes, int bytes_per_pixel, std::uint8_t value) -> void
    {
        if (bytes.empty())
        {
            return;
        }
        if (bytes_per_pixel >= 4)
        {
            for (std::size_t i = 0; i + 3 < bytes.size(); i += 4)
            {
                bytes[i + 0] = value;
                bytes[i + 1] = value;
                bytes[i + 2] = value;
                bytes[i + 3] = 255;
            }
            return;
        }
        std::fill(bytes.begin(), bytes.end(), value);
    }

    auto paint_material_disc(std::vector<std::uint8_t>& bytes,
                             int width,
                             int height,
                             int bytes_per_pixel,
                             const FrontSample& sample,
                             std::uint8_t value) -> void
    {
        if (bytes.empty() || width <= 0 || height <= 0)
        {
            return;
        }
        bytes_per_pixel = bytes_per_pixel >= 4 ? 4 : 1;
        const int cx = std::min(width - 1, std::max(0, static_cast<int>(sample.u * static_cast<double>(width))));
        const int cy = std::min(height - 1, std::max(0, static_cast<int>((1.0 - sample.v) * static_cast<double>(height))));
        const int radius = std::max(1, static_cast<int>(sample.radius * static_cast<double>(std::min(width, height))));
        const int r2 = radius * radius;
        for (int y = std::max(0, cy - radius); y <= std::min(height - 1, cy + radius); ++y)
        {
            for (int x = std::max(0, cx - radius); x <= std::min(width - 1, cx + radius); ++x)
            {
                const int dx = x - cx;
                const int dy = y - cy;
                if ((dx * dx + dy * dy) > r2)
                {
                    continue;
                }
                const auto pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                const auto offset = pixel * static_cast<std::size_t>(bytes_per_pixel);
                if (offset >= bytes.size())
                {
                    continue;
                }
                if (bytes_per_pixel >= 4 && offset + 3 < bytes.size())
                {
                    bytes[offset + 0] = value;
                    bytes[offset + 1] = value;
                    bytes[offset + 2] = value;
                    bytes[offset + 3] = 255;
                }
                else
                {
                    bytes[offset] = value;
                }
            }
        }
    }

    auto clamp01(double value) -> double
    {
        return std::max(0.0, std::min(1.0, value));
    }

    auto sdk_luma(const Color& color) -> double
    {
        return color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
    }

    auto sdk_is_red_paint_artifact(const Color& color) -> bool
    {
        return color.r > 0.78 && color.g < 0.22 && color.b < 0.22;
    }

    auto sdk_infer_surface_material(Color color, bool floor_like) -> Color
    {
        if (floor_like)
        {
            color.roughness = std::max(0.86, std::min(0.99, std::max(color.roughness, 0.86)));
            color.metallic = std::max(0.0, std::min(0.12, color.metallic));
            return color;
        }
        color.roughness = std::max(0.35, std::min(0.99, color.roughness <= 0.0 ? 0.92 : color.roughness));
        color.metallic = clamp01(color.metallic);
        return color;
    }

    auto sdk_sanitize_background_color(const Color& captured, const Color& material_hint) -> Color
    {
        if (!sdk_is_red_paint_artifact(captured))
        {
            return captured;
        }
        Color fallback = material_hint;
        if (sdk_is_red_paint_artifact(fallback))
        {
            fallback = Color{0.34, 0.37, 0.31, 0.94, 0.0};
        }
        fallback.r = std::max(0.05, std::min(0.72, fallback.r));
        fallback.g = std::max(0.08, std::min(0.76, fallback.g));
        fallback.b = std::max(0.06, std::min(0.70, fallback.b));
        fallback.roughness = std::max(0.72, std::min(0.98, fallback.roughness));
        fallback.metallic = 0.0;
        return fallback;
    }

    auto sdk_compensate_projected_albedo_preserve_material(Color color, bool floor_like) -> Color
    {
        const auto roughness = clamp01(color.roughness);
        const auto metallic = clamp01(color.metallic);
        const auto lum = sdk_luma(color);
        double lift = 1.02;
        if (lum < 0.18)
        {
            lift = 1.10;
        }
        else if (lum < 0.34)
        {
            lift = 1.06;
        }
        else if (lum > 0.58)
        {
            lift = 1.00;
        }
        color.r = std::max(0.018, std::min(0.98, color.r * lift));
        color.g = std::max(0.018, std::min(0.98, color.g * lift));
        color.b = std::max(0.018, std::min(0.98, color.b * lift));
        color.roughness = floor_like ? std::max(0.86, std::min(0.99, std::max(roughness, 0.86))) : roughness;
        color.metallic = floor_like ? std::max(0.0, std::min(0.12, metallic)) : metallic;
        return color;
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

    auto function_param_schema(Reflection& ref, std::uintptr_t function) -> std::string
    {
        std::string out{};
        int count = 0;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop && count < 32; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext), ++count)
        {
            if (!out.empty())
            {
                out += ";";
            }
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            out += name + "@" + std::to_string(prop_offset(prop)) + "#" + std::to_string(prop_element_size(prop));
        }
        return out;
    }

    auto early_hex_address(std::uintptr_t value) -> std::string
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
        return buffer;
    }

    auto early_json_bool(bool value) -> const char*
    {
        return value ? "true" : "false";
    }

    auto property_list_json(Reflection& ref, std::uintptr_t object, int max_props = 96) -> std::string
    {
        std::string out = "[";
        int count = 0;
        for (auto cls = ref.class_ptr(object); cls && count < max_props; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
        {
            for (auto prop = safe_read<std::uintptr_t>(cls + OffChildProperties); prop && count < max_props; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext), ++count)
            {
                if (count > 0)
                {
                    out += ",";
                }
                out += "{\"name\":\"" + json_escape(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName))) + "\"";
                out += ",\"offset\":" + std::to_string(prop_offset(prop));
                out += ",\"element_size\":" + std::to_string(prop_element_size(prop));
                out += "}";
            }
        }
        out += "]";
        return out;
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

    auto function_list_json(Reflection& ref, std::uintptr_t object, int max_functions = 96) -> std::string
    {
        std::string out = "[";
        int count = 0;
        for (auto cls = ref.class_ptr(object); cls && count < max_functions; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
        {
            for (auto child = safe_read<std::uintptr_t>(cls + OffChildren); child && count < max_functions; child = safe_read<std::uintptr_t>(child + OffUFieldNext))
            {
                const auto name = ref.object_name(child);
                if (name.empty())
                {
                    continue;
                }
                if (count > 0)
                {
                    out += ",";
                }
                out += "{\"name\":\"" + json_escape(name) + "\"";
                out += ",\"params_size\":" + std::to_string(safe_read<int>(child + OffPropertiesSize, 0));
                out += ",\"schema\":\"" + json_escape(function_param_schema(ref, child)) + "\"}";
                ++count;
            }
        }
        out += "]";
        return out;
    }

    auto array_header_scan_json(std::uintptr_t object, int scan_bytes = 0x1000, int max_entries = 96) -> std::string
    {
        std::string out = "[";
        int count = 0;
        for (int offset = 0; object && offset + 16 <= scan_bytes && count < max_entries; offset += 8)
        {
            const auto data = safe_read<std::uintptr_t>(object + static_cast<std::uintptr_t>(offset), 0);
            const auto num = safe_read<int>(object + static_cast<std::uintptr_t>(offset + 8), 0);
            const auto max = safe_read<int>(object + static_cast<std::uintptr_t>(offset + 12), 0);
            if (!data || num <= 0 || max < num || max > 2000000)
            {
                continue;
            }
            const auto first = safe_read<std::uintptr_t>(data, 0);
            if (count > 0)
            {
                out += ",";
            }
            out += "{\"offset\":" + std::to_string(offset);
            out += ",\"data\":\"" + early_hex_address(data) + "\"";
            out += ",\"num\":" + std::to_string(num);
            out += ",\"max\":" + std::to_string(max);
            out += ",\"first_qword\":\"" + early_hex_address(first) + "\"}";
            ++count;
        }
        out += "]";
        return out;
    }

    auto pointer_scan_json(std::uintptr_t object, int scan_bytes = 0x1000, int max_entries = 128) -> std::string
    {
        std::string out = "[";
        int count = 0;
        for (int offset = 0; object && offset + 8 <= scan_bytes && count < max_entries; offset += 8)
        {
            const auto ptr = safe_read<std::uintptr_t>(object + static_cast<std::uintptr_t>(offset), 0);
            if (!ptr || address_in_main_module(ptr))
            {
                continue;
            }
            const auto first = safe_read<std::uintptr_t>(ptr, 0);
            if (!first)
            {
                continue;
            }
            if (count > 0)
            {
                out += ",";
            }
            out += "{\"offset\":" + std::to_string(offset);
            out += ",\"ptr\":\"" + early_hex_address(ptr) + "\"";
            out += ",\"first_qword\":\"" + early_hex_address(first) + "\"";
            out += ",\"live_uobject\":" + std::string(early_json_bool(live_uobject(ptr))) + "}";
            ++count;
        }
        out += "]";
        return out;
    }

    auto json_float_or_null(float value) -> std::string
    {
        if (!std::isfinite(static_cast<double>(value)))
        {
            return "null";
        }
        return std::to_string(value);
    }

    auto bytes_preview_hex(std::uintptr_t data, int byte_count) -> std::string
    {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out{};
        out.reserve(static_cast<std::size_t>(std::max(0, byte_count) * 2));
        for (int i = 0; data && i < byte_count; ++i)
        {
            const auto value = safe_read<std::uint8_t>(data + static_cast<std::uintptr_t>(i), 0);
            out.push_back(digits[(value >> 4) & 0x0f]);
            out.push_back(digits[value & 0x0f]);
        }
        return out;
    }

    auto nonzero_preview_bytes(std::uintptr_t data, int byte_count) -> int
    {
        int count = 0;
        for (int i = 0; data && i < byte_count; ++i)
        {
            if (safe_read<std::uint8_t>(data + static_cast<std::uintptr_t>(i), 0) != 0)
            {
                ++count;
            }
        }
        return count;
    }

    auto float_preview_json(std::uintptr_t data, int float_count) -> std::string
    {
        std::string out = "[";
        for (int i = 0; data && i < float_count; ++i)
        {
            if (i > 0)
            {
                out += ",";
            }
            out += json_float_or_null(safe_read<float>(data + static_cast<std::uintptr_t>(i * 4), 0.0f));
        }
        out += "]";
        return out;
    }

    auto u16_preview_json(std::uintptr_t data, int count) -> std::string
    {
        std::string out = "[";
        for (int i = 0; data && i < count; ++i)
        {
            if (i > 0)
            {
                out += ",";
            }
            out += std::to_string(safe_read<std::uint16_t>(data + static_cast<std::uintptr_t>(i * 2), 0));
        }
        out += "]";
        return out;
    }

    auto u32_preview_json(std::uintptr_t data, int count) -> std::string
    {
        std::string out = "[";
        for (int i = 0; data && i < count; ++i)
        {
            if (i > 0)
            {
                out += ",";
            }
            out += std::to_string(safe_read<std::uint32_t>(data + static_cast<std::uintptr_t>(i * 4), 0));
        }
        out += "]";
        return out;
    }

    auto array_candidate_kind(int num, int max) -> std::string
    {
        if (num >= 8192 && max <= 1000000)
        {
            return "large_mesh_buffer_candidate";
        }
        if (num >= 256 && max <= 1000000)
        {
            return "mesh_buffer_candidate";
        }
        return "array";
    }

    auto array_candidate_score(int num,
                               int max,
                               std::uintptr_t first_qword,
                               std::uint32_t first_dword,
                               float first_float,
                               int nonzero_bytes,
                               bool pointer_table_suspect) -> int
    {
        int score = 0;
        if (num >= 8192)
        {
            score += 40;
        }
        else if (num >= 2048)
        {
            score += 28;
        }
        else if (num >= 256)
        {
            score += 14;
        }
        if (max >= num && max <= num + std::max(64, num / 4))
        {
            score += 12;
        }
        if (first_qword != 0)
        {
            score += 8;
        }
        if (first_dword < 1000000)
        {
            score += 6;
        }
        if (std::isfinite(static_cast<double>(first_float)) && first_float > -100000.0f && first_float < 100000.0f)
        {
            score += 6;
        }
        if (nonzero_bytes >= 24)
        {
            score += 36;
        }
        else if (nonzero_bytes >= 8)
        {
            score += 18;
        }
        else if (nonzero_bytes == 0)
        {
            score -= 48;
        }
        if (pointer_table_suspect)
        {
            score -= 64;
        }
        return score;
    }

    auto pointer_target_array_scan_json(std::uintptr_t object,
                                        int owner_scan_bytes = 0x1800,
                                        int target_scan_bytes = 0x800,
                                        int max_targets = 96,
                                        int max_arrays_per_target = 32) -> std::string
    {
        std::string out = "[";
        int target_count = 0;
        for (int owner_offset = 0; object && owner_offset + 8 <= owner_scan_bytes && target_count < max_targets; owner_offset += 8)
        {
            const auto target = safe_read<std::uintptr_t>(object + static_cast<std::uintptr_t>(owner_offset), 0);
            if (!target || address_in_main_module(target))
            {
                continue;
            }
            std::string arrays = "[";
            int array_count = 0;
            for (int target_offset = 0; target_offset + 16 <= target_scan_bytes && array_count < max_arrays_per_target; target_offset += 8)
            {
                const auto data = safe_read<std::uintptr_t>(target + static_cast<std::uintptr_t>(target_offset), 0);
                const auto num = safe_read<int>(target + static_cast<std::uintptr_t>(target_offset + 8), 0);
                const auto max = safe_read<int>(target + static_cast<std::uintptr_t>(target_offset + 12), 0);
                if (!data || num <= 0 || max < num || max > 4000000)
                {
                    continue;
                }
                if (array_count > 0)
                {
                    arrays += ",";
                }
                const auto first_qword = safe_read<std::uintptr_t>(data, 0);
                const auto first_dword = safe_read<std::uint32_t>(data, 0);
                const auto first_float = safe_read<float>(data, 0.0f);
                const auto nonzero_bytes = nonzero_preview_bytes(data, 64);
                const auto candidate_kind = array_candidate_kind(num, max);
                arrays += "{\"offset\":" + std::to_string(target_offset);
                arrays += ",\"data\":\"" + early_hex_address(data) + "\"";
                arrays += ",\"num\":" + std::to_string(num);
                arrays += ",\"max\":" + std::to_string(max);
                arrays += ",\"first_qword\":\"" + early_hex_address(first_qword) + "\"";
                arrays += ",\"first_dword\":" + std::to_string(first_dword);
                arrays += ",\"first_float\":" + json_float_or_null(first_float);
                arrays += ",\"preview_nonzero_bytes\":" + std::to_string(nonzero_bytes);
                arrays += ",\"candidate_kind\":\"" + candidate_kind + "\"";
                arrays += "}";
                ++array_count;
            }
            arrays += "]";
            if (array_count <= 0)
            {
                continue;
            }
            if (target_count > 0)
            {
                out += ",";
            }
            out += "{\"owner_offset\":" + std::to_string(owner_offset);
            out += ",\"target\":\"" + early_hex_address(target) + "\"";
            out += ",\"target_first_qword\":\"" + early_hex_address(safe_read<std::uintptr_t>(target, 0)) + "\"";
            out += ",\"target_live_uobject\":" + std::string(early_json_bool(live_uobject(target)));
            out += ",\"arrays\":" + arrays + "}";
            ++target_count;
        }
        out += "]";
        return out;
    }

    auto mesh_buffer_candidates_json(Reflection& ref,
                                     std::uintptr_t object,
                                     int owner_scan_bytes = 0x1800,
                                     int target_scan_bytes = 0x900,
                                     int max_candidates = 80) -> std::string
    {
        struct Candidate
        {
            int score{0};
            int owner_offset{0};
            int target_offset{0};
            std::uintptr_t target{0};
            bool target_live_uobject{false};
            bool pointer_table_suspect{false};
            std::uintptr_t data{0};
            int num{0};
            int max{0};
            std::uintptr_t first_qword{0};
            std::uint32_t first_dword{0};
            float first_float{0.0f};
            int nonzero_bytes{0};
            std::string kind{};
        };
        std::vector<Candidate> candidates{};
        for (int owner_offset = 0; object && owner_offset + 8 <= owner_scan_bytes; owner_offset += 8)
        {
            const auto target = safe_read<std::uintptr_t>(object + static_cast<std::uintptr_t>(owner_offset), 0);
            if (!target || address_in_main_module(target))
            {
                continue;
            }
            for (int target_offset = 0; target_offset + 16 <= target_scan_bytes; target_offset += 8)
            {
                const auto data = safe_read<std::uintptr_t>(target + static_cast<std::uintptr_t>(target_offset), 0);
                const auto num = safe_read<int>(target + static_cast<std::uintptr_t>(target_offset + 8), 0);
                const auto max = safe_read<int>(target + static_cast<std::uintptr_t>(target_offset + 12), 0);
                if (!data || num <= 0 || max < num || max > 4000000)
                {
                    continue;
                }
                const auto kind = array_candidate_kind(num, max);
                if (kind == "array")
                {
                    continue;
                }
                const auto first_qword = safe_read<std::uintptr_t>(data, 0);
                const auto first_dword = safe_read<std::uint32_t>(data, 0);
                const auto first_float = safe_read<float>(data, 0.0f);
                const auto nonzero_bytes = nonzero_preview_bytes(data, 64);
                const bool pointer_table_suspect = address_in_main_module(first_qword) || live_uobject(first_qword);
                if (nonzero_bytes == 0)
                {
                    continue;
                }
                candidates.push_back(Candidate{
                    array_candidate_score(num, max, first_qword, first_dword, first_float, nonzero_bytes, pointer_table_suspect),
                    owner_offset,
                    target_offset,
                    target,
                    live_uobject(target),
                    pointer_table_suspect,
                    data,
                    num,
                    max,
                    first_qword,
                    first_dword,
                    first_float,
                    nonzero_bytes,
                    kind});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score)
            {
                return a.score > b.score;
            }
            return a.num > b.num;
        });
        std::string out = "[";
        const auto count = std::min<int>(static_cast<int>(candidates.size()), max_candidates);
        for (int i = 0; i < count; ++i)
        {
            const auto& c = candidates[static_cast<std::size_t>(i)];
            if (i > 0)
            {
                out += ",";
            }
            out += "{\"rank\":" + std::to_string(i + 1);
            out += ",\"score\":" + std::to_string(c.score);
            out += ",\"kind\":\"" + c.kind + "\"";
            out += ",\"owner_offset\":" + std::to_string(c.owner_offset);
            out += ",\"owner_property\":\"" + json_escape(property_name_at_or_before_offset(ref, object, c.owner_offset)) + "\"";
            out += ",\"target\":\"" + early_hex_address(c.target) + "\"";
            out += ",\"target_name\":\"" + json_escape(c.target_live_uobject ? ref.object_name(c.target) : std::string{}) + "\"";
            out += ",\"target_class\":\"" + json_escape(c.target_live_uobject ? ref.class_name(c.target) : std::string{}) + "\"";
            out += ",\"target_live_uobject\":" + std::string(early_json_bool(c.target_live_uobject));
            out += ",\"target_offset\":" + std::to_string(c.target_offset);
            out += ",\"data\":\"" + early_hex_address(c.data) + "\"";
            out += ",\"num\":" + std::to_string(c.num);
            out += ",\"max\":" + std::to_string(c.max);
            out += ",\"first_qword\":\"" + early_hex_address(c.first_qword) + "\"";
            out += ",\"first_dword\":" + std::to_string(c.first_dword);
            out += ",\"first_float\":" + json_float_or_null(c.first_float);
            out += ",\"preview_nonzero_bytes\":" + std::to_string(c.nonzero_bytes);
            out += ",\"pointer_table_suspect\":" + std::string(early_json_bool(c.pointer_table_suspect));
            out += ",\"first_bytes_hex\":\"" + bytes_preview_hex(c.data, 64) + "\"";
            out += ",\"first_floats\":" + float_preview_json(c.data, 8);
            out += ",\"first_u16\":" + u16_preview_json(c.data, 16);
            out += ",\"first_u32\":" + u32_preview_json(c.data, 12);
            out += "}";
        }
        out += "]";
        return out;
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

    auto write_vector2(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, double u, double v) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* dest = container + offset;
        const auto st = struct_type(ref, prop, {"X", "Y"});
        bool wrote = false;
        if (st)
        {
            if (const auto x = find_property_any(ref, st, {"X"}))
            {
                wrote = write_number(ref, x, dest, u) || wrote;
            }
            if (const auto y = find_property_any(ref, st, {"Y"}))
            {
                wrote = write_number(ref, y, dest, v) || wrote;
            }
            if (wrote)
            {
                return true;
            }
        }
        const auto size = prop_element_size(prop);
        if (size >= 16)
        {
            const double values[2]{u, v};
            std::memcpy(dest, values, sizeof(values));
            return true;
        }
        if (size >= 8)
        {
            const float values[2]{static_cast<float>(u), static_cast<float>(v)};
            std::memcpy(dest, values, sizeof(values));
            return true;
        }
        return false;
    }

    auto write_linear_color(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const Color& color) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* dest = container + offset;
        const auto st = struct_type(ref, prop, {"R", "G", "B", "A"});
        bool wrote = false;
        if (st)
        {
            if (const auto p = find_property_any(ref, st, {"R"})) wrote = write_number(ref, p, dest, color.r) || wrote;
            if (const auto p = find_property_any(ref, st, {"G"})) wrote = write_number(ref, p, dest, color.g) || wrote;
            if (const auto p = find_property_any(ref, st, {"B"})) wrote = write_number(ref, p, dest, color.b) || wrote;
            if (const auto p = find_property_any(ref, st, {"A"})) wrote = write_number(ref, p, dest, 1.0) || wrote;
            if (wrote) return true;
        }
        const float values[4]{static_cast<float>(color.r), static_cast<float>(color.g), static_cast<float>(color.b), 1.0f};
        std::memcpy(dest, values, std::min<std::size_t>(16, static_cast<std::size_t>(std::max(0, prop_element_size(prop)))));
        return true;
    }

    auto write_channel_data(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const Color& color, bool include_material) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref, prop, {"AlbedoColor", "Metallic", "Roughness", "Height", "ApplyMode"});
        if (!st)
        {
            return false;
        }
        bool wrote = false;
        if (const auto p = find_property_any(ref, st, {"AlbedoColor"})) wrote = write_linear_color(ref, p, base, color) || wrote;
        if (include_material)
        {
            if (const auto p = find_property_any(ref, st, {"Metallic"})) wrote = write_number(ref, p, base, clamp01(color.metallic)) || wrote;
            if (const auto p = find_property_any(ref, st, {"Roughness"})) wrote = write_number(ref, p, base, clamp01(color.roughness)) || wrote;
        }
        if (const auto p = find_property_any(ref, st, {"Height"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"ApplyMode"})) wrote = write_number(ref, p, base, static_cast<double>(color.apply_mode)) || wrote;
        return wrote;
    }

    auto write_brush_settings(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, std::uintptr_t component, double radius) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        bool wrote = false;
        if (component)
        {
            const auto current = find_object_property(ref, component, "CurrentBrushSettings");
            const auto current_offset = current ? prop_offset(current) : -1;
            const auto dest_size = prop_element_size(prop);
            const auto source_size = current ? prop_element_size(current) : 0;
            const auto copy_size = source_size > 0 ? std::min(dest_size, source_size) : dest_size;
            if (current_offset >= 0 && copy_size > 0 && copy_size <= 1024 &&
                safe_copy(base, reinterpret_cast<void*>(component + current_offset), static_cast<std::size_t>(copy_size)))
            {
                wrote = true;
            }
        }
        const auto st = struct_type(ref, prop, {"Radius", "Hardness", "Opacity", "Spacing", "Falloff", "BlendMode", "Rotation"});
        if (!st)
        {
            return wrote;
        }
        if (const auto p = find_property_any(ref, st, {"Radius"})) wrote = write_number(ref, p, base, radius) || wrote;
        if (const auto p = find_property_any(ref, st, {"Hardness"})) wrote = write_number(ref, p, base, 1.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"Opacity"})) wrote = write_number(ref, p, base, 1.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"Spacing"})) wrote = write_number(ref, p, base, 0.25) || wrote;
        if (const auto p = find_property_any(ref, st, {"Falloff"})) wrote = write_number(ref, p, base, 2.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"BlendMode"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"Rotation"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        return wrote;
    }

    auto write_paint_stroke_data(Reflection& ref,
                                 std::uintptr_t prop,
                                 std::uint8_t* container,
                                 std::uintptr_t component,
                                 double u,
                                 double v,
                                 const Color& color,
                                 double radius) -> bool
    {
        const auto offset = prop_offset(prop);
        if (offset < 0)
        {
            return false;
        }
        auto* base = container + offset;
        const auto st = struct_type(ref,
                                    prop,
                                    {"Uv",
                                     "UV",
                                     "PaintUv",
                                     "PaintUV",
                                     "BrushSettings",
                                     "ChannelData",
                                     "TargetChannel",
                                     "EffectiveBrushWorldRadius"});
        if (!st)
        {
            return false;
        }
        bool wrote = false;
        if (const auto p = find_property_any(ref, st, {"Uv", "UV", "PaintUv", "PaintUV"})) wrote = write_vector2(ref, p, base, u, v) || wrote;
        if (const auto p = find_property_any(ref, st, {"BrushSettings"})) wrote = write_brush_settings(ref, p, base, component, radius) || wrote;
        if (const auto p = find_property_any(ref, st, {"ChannelData"})) wrote = write_channel_data(ref, p, base, color, true) || wrote;
        if (const auto p = find_property_any(ref, st, {"TargetChannel", "Channel", "PaintChannel"})) wrote = write_number(ref, p, base, PaintChannelAlbedoMetallicRoughness) || wrote;
        if (const auto p = find_property_any(ref, st, {"EffectiveBrushWorldRadius"})) wrote = write_number(ref, p, base, radius) || wrote;
        if (const auto p = find_property_any(ref, st, {"EffectiveSubdivisionLevel"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"EffectiveSubdivisionPixelSize"})) wrote = write_number(ref, p, base, 1.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"EffectiveTemplateResolution"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"EffectiveMaxGeneratedBrushTriangles"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"EffectiveGutterExpandPixels"})) wrote = write_number(ref, p, base, 0.0) || wrote;
        if (const auto p = find_property_any(ref, st, {"bHasWorldPosition"})) wrote = write_bool(p, base, false) || wrote;
        if (const auto p = find_property_any(ref, st, {"bHasLocalPosition"})) wrote = write_bool(p, base, false) || wrote;
        if (const auto p = find_property_any(ref, st, {"bHasSkeletalTriangleAnchor"})) wrote = write_bool(p, base, false) || wrote;
        return wrote;
    }

    auto process_event(std::uintptr_t object, std::uintptr_t function, std::uint8_t* params, std::string& failure) -> bool
    {
        auto target = g_original_process_event.load();
        if (!target)
        {
            const auto module = main_module_range();
            const auto sdk_target = module.base ? module.base + meccha_sdk::Offsets::ProcessEvent : 0;
            if (sdk_target && address_in_main_module(sdk_target))
            {
                target = sdk_target;
            }
        }
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

    auto call_uv_paint_function(Reflection& ref,
                                std::uintptr_t component,
                                const char* function_name,
                                double u,
                                double v,
                                const Color& color,
                                double radius,
                                bool allow_brush_settings,
                                std::string& failure) -> bool
    {
        const auto function = ref.find_function(component, function_name);
        if (!function)
        {
            failure = std::string(function_name) + "_unavailable";
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 8192)
        {
            failure = "paint_params_size_invalid";
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote_uv = false;
        bool wrote_channel_data = false;
        bool wrote_channel = false;
        bool brush_required = false;
        bool wrote_brush = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto raw_name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            const auto name = lower_copy(raw_name);
            if (name == "returnvalue")
            {
                continue;
            }
            if (name == "uv" || contains_text(name, "paintuv"))
            {
                wrote_uv = write_vector2(ref, prop, params.data(), u, v) || wrote_uv;
                continue;
            }
            if (contains_text(name, "channeldata"))
            {
                wrote_channel_data = write_channel_data(ref, prop, params.data(), color, true) || wrote_channel_data;
                continue;
            }
            if (contains_text(name, "brushsettings"))
            {
                brush_required = true;
                if (allow_brush_settings)
                {
                    wrote_brush = write_brush_settings(ref, prop, params.data(), component, radius) || wrote_brush;
                }
                continue;
            }
            if (name == "channel" || name == "targetchannel" || contains_text(name, "paintchannel"))
            {
                wrote_channel = write_number(ref, prop, params.data(), PaintChannelAlbedoMetallicRoughness) || wrote_channel;
                continue;
            }
        }
        if (!wrote_uv || !wrote_channel_data || !wrote_channel || (brush_required && !wrote_brush))
        {
            failure = "paint_param_write_failed uv=" + std::to_string(wrote_uv ? 1 : 0) + " channel_data=" +
                      std::to_string(wrote_channel_data ? 1 : 0) + " channel=" + std::to_string(wrote_channel ? 1 : 0) +
                      " brush=" + std::to_string((!brush_required || wrote_brush) ? 1 : 0);
            return false;
        }
        if (!process_event(component, function, params.data(), failure))
        {
            return false;
        }
        if (!read_return_bool(ref, function, params.data()))
        {
            failure = "paint_return_false";
            return false;
        }
        return true;
    }

    auto call_stroke_paint_function(Reflection& ref,
                                    std::uintptr_t component,
                                    const char* function_name,
                                    double u,
                                    double v,
                                    const Color& color,
                                    double radius,
                                    std::string& failure) -> bool
    {
        const auto function = ref.find_function(component, function_name);
        if (!function)
        {
            failure = std::string(function_name) + "_unavailable";
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 8192)
        {
            failure = std::string(function_name) + "_params_size_invalid";
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote_stroke = false;
        bool wrote_uv = false;
        bool wrote_channel_data = false;
        bool wrote_channel = false;
        bool wrote_brush = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto raw_name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            const auto name = lower_copy(raw_name);
            if (name == "returnvalue")
            {
                continue;
            }
            wrote_stroke = write_paint_stroke_data(ref, prop, params.data(), component, u, v, color, radius) || wrote_stroke;
            if (name == "uv" || contains_text(name, "paintuv"))
            {
                wrote_uv = write_vector2(ref, prop, params.data(), u, v) || wrote_uv;
                continue;
            }
            if (contains_text(name, "channeldata"))
            {
                wrote_channel_data = write_channel_data(ref, prop, params.data(), color, true) || wrote_channel_data;
                continue;
            }
            if (contains_text(name, "brushsettings"))
            {
                wrote_brush = write_brush_settings(ref, prop, params.data(), component, radius) || wrote_brush;
                continue;
            }
            if (name == "channel" || name == "targetchannel" || contains_text(name, "paintchannel"))
            {
                wrote_channel = write_number(ref, prop, params.data(), PaintChannelAlbedoMetallicRoughness) || wrote_channel;
                continue;
            }
        }
        if (!wrote_stroke && (!wrote_uv || !wrote_channel_data))
        {
            failure = std::string(function_name) + "_param_write_failed stroke=" + std::to_string(wrote_stroke ? 1 : 0) +
                      " uv=" + std::to_string(wrote_uv ? 1 : 0) + " channel_data=" + std::to_string(wrote_channel_data ? 1 : 0) +
                      " channel=" + std::to_string(wrote_channel ? 1 : 0) + " brush=" + std::to_string(wrote_brush ? 1 : 0);
            return false;
        }
        if (!process_event(component, function, params.data(), failure))
        {
            failure = std::string(function_name) + "_" + failure;
            return false;
        }
        if (!read_return_bool(ref, function, params.data()))
        {
            failure = std::string(function_name) + "_return_false";
            return false;
        }
        return true;
    }

    auto call_replicated_paint_at_uv(Reflection& ref,
                                     std::uintptr_t component,
                                     double u,
                                     double v,
                                     const Color& color,
                                     double radius,
                                     std::string& failure,
                                     PaintCallStats& stats) -> bool
    {
        bool called = false;
        std::string first_failure{};
        const char* server_rpcs[]{"ServerSendPaint", "ServerPaint", "SendPaintToServer", "RequestPaintOnServer"};
        for (const auto* rpc : server_rpcs)
        {
            std::string rpc_failure{};
            if (call_stroke_paint_function(ref, component, rpc, u, v, color, radius, rpc_failure))
            {
                called = true;
                ++stats.server_success;
                break;
            }
            ++stats.server_failure;
            if (first_failure.empty())
            {
                first_failure = rpc_failure;
            }
        }

        bool local_called = false;
        const char* local_stroke_rpcs[]{"PaintStrokeUV"};
        for (const auto* rpc : local_stroke_rpcs)
        {
            std::string rpc_failure{};
            if (call_stroke_paint_function(ref, component, rpc, u, v, color, radius, rpc_failure))
            {
                local_called = true;
                ++stats.local_success;
                break;
            }
            ++stats.local_failure;
            if (first_failure.empty())
            {
                first_failure = rpc_failure;
            }
        }

        const char* local_uv_rpcs[]{"PaintAtUVWithBrush", "PaintAtUV"};
        for (const auto* rpc : local_uv_rpcs)
        {
            std::string rpc_failure{};
            if (call_uv_paint_function(ref, component, rpc, u, v, color, radius, true, rpc_failure))
            {
                local_called = true;
                ++stats.local_success;
                break;
            }
            ++stats.local_failure;
            if (first_failure.empty())
            {
                first_failure = rpc_failure;
            }
        }

        if (called || local_called)
        {
            return true;
        }
        failure = !first_failure.empty() ? first_failure : "replicated_paint_rpc_unavailable";
        if (stats.first_failure.empty())
        {
            stats.first_failure = failure;
        }
        return false;
    }

    auto parse_average_color(const std::string& request) -> Color
    {
        Color out{};
        out.r = 0.42;
        out.g = 0.42;
        out.b = 0.36;
        out.roughness = 0.65;
        out.metallic = 0.0;
        out.apply_mode = 1;

        double sr = 0.0, sg = 0.0, sb = 0.0;
        int count = 0;
        std::size_t pos = request.find("\"front_samples\"");
        if (pos == std::string::npos)
        {
            pos = 0;
        }
        while (count < 256)
        {
            const auto rp = request.find("\"r\":", pos);
            if (rp == std::string::npos)
            {
                break;
            }
            const auto gp = request.find("\"g\":", rp);
            const auto bp = request.find("\"b\":", gp == std::string::npos ? rp : gp);
            if (gp == std::string::npos || bp == std::string::npos)
            {
                break;
            }
            char* end = nullptr;
            const double r = std::strtod(request.c_str() + rp + 4, &end);
            const double g = std::strtod(request.c_str() + gp + 4, &end);
            const double b = std::strtod(request.c_str() + bp + 4, &end);
            if (std::isfinite(r) && std::isfinite(g) && std::isfinite(b))
            {
                sr += clamp01(r);
                sg += clamp01(g);
                sb += clamp01(b);
                ++count;
            }
            pos = bp + 4;
        }
        if (count > 0)
        {
            out.r = sr / count;
            out.g = sg / count;
            out.b = sb / count;
        }
        return out;
    }

    struct ComponentSelection
    {
        std::uintptr_t component{0};
        std::uintptr_t pawn{0};
        std::uintptr_t target{0};
        std::uintptr_t target_mesh{0};
        std::uintptr_t owner{0};
        std::string source{};
        std::string target_source{};
        std::string mesh_source{};
    };

    auto hex_address(std::uintptr_t value) -> std::string
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
        return buffer;
    }

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
        std::string first_export_probe_failure{};
        std::uintptr_t first_export_probe_component = 0;
        std::string first_export_probe_class{};

        auto check_component = [&](std::uintptr_t obj, const char* source, bool require_owner) -> bool {
            if (!live_object(obj))
            {
                return false;
            }
            const auto cls = lower_copy(ref.class_name(obj));
            if ((contains_text(cls, "runtimepaint") || contains_text(cls, "paint")) && ref.find_function(obj, "PaintAtUVWithBrush"))
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
                if (require_owner)
                {
                    const auto export_probe = export_channel_bytes(ref, obj, 0);
                    if (!export_probe.ok)
                    {
                        if (first_export_probe_failure.empty())
                        {
                            first_export_probe_failure = export_probe.failure;
                            first_export_probe_component = obj;
                            first_export_probe_class = ref.class_name(obj);
                        }
                        return false;
                    }
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
                !ref.find_function(obj, "PaintAtUVWithBrush") ||
                !ref.find_function(obj, "ExportChannelToBytes") ||
                !ref.find_function(obj, "ImportChannelFromBytes"))
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
                      " mesh_matches=" + std::to_string(mesh_match_count) +
                      " export_probe_component=" + hex_address(first_export_probe_component) +
                      " export_probe_class=" + first_export_probe_class +
                      " export_probe_failure=" + first_export_probe_failure;
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
        const auto hook = SetWindowsHookExW(WH_GETMESSAGE, message_hook_proc, g_module, thread_id);
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

    auto first_bytes_hex(const std::vector<std::uint8_t>& bytes, std::size_t limit = 16) -> std::string
    {
        static const char* hex = "0123456789abcdef";
        std::string out{};
        const auto count = std::min(limit, bytes.size());
        out.reserve(count * 2);
        for (std::size_t i = 0; i < count; ++i)
        {
            out.push_back(hex[(bytes[i] >> 4) & 0xF]);
            out.push_back(hex[bytes[i] & 0xF]);
        }
        return out;
    }

    struct SdkContext
    {
        bool ok{false};
        std::string stage{"sdk_unavailable"};
        std::string message{"sdk unavailable"};
        std::uintptr_t module_base{0};
        std::uintptr_t expected_guobject_array{0};
        std::uintptr_t actual_guobject_array{0};
        std::uintptr_t expected_gworld{0};
        std::uintptr_t resolved_gworld_address{0};
        bool guobject_runtime_address_used{false};
        std::string gworld_source{"sdk_offset"};
        std::uintptr_t process_event{0};
        std::uintptr_t world{0};
        std::uintptr_t game_instance{0};
        int local_players_count{0};
        std::uintptr_t local_player{0};
        std::uintptr_t controller{0};
        std::uintptr_t k2_get_pawn_function{0};
        std::uintptr_t pawn{0};
        std::uintptr_t k2_get_actor_location_function{0};
        meccha_sdk::FVector body_world_position{};
        std::uintptr_t component{0};
        std::uintptr_t relay_component{0};
        std::uintptr_t relay_stroke_batch_to_server_function{0};
        std::uintptr_t relay_paint_to_server_function{0};
        std::uintptr_t relay_texture_sync_to_server_function{0};
        std::uintptr_t server_relay_texture_sync_function{0};
        std::uintptr_t export_function{0};
        std::uintptr_t import_function{0};
        std::uintptr_t get_render_target_function{0};
        std::uintptr_t request_full_texture_sync_function{0};
        std::uintptr_t server_request_texture_sync_function{0};
        std::uintptr_t multicast_sync_channel_data_function{0};
        std::uintptr_t multicast_sync_compressed_channel_data_function{0};
        std::uintptr_t server_paint_batch_function{0};
        std::uintptr_t server_send_stroke_batch_function{0};
        std::uintptr_t server_send_paint_function{0};
        std::uintptr_t server_paint_function{0};
        std::uintptr_t paint_at_uv_with_brush_function{0};
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
        meccha_sdk::FVector location{};
        meccha_sdk::FVector direction{};
    };

    struct SdkBrushQueryHit
    {
        bool params_ok{false};
        bool success{false};
        bool has_uv{false};
        double u{0.0};
        double v{0.0};
        meccha_sdk::FVector world_position{};
        meccha_sdk::FVector normal{};
        std::uintptr_t actor{0};
        std::uintptr_t component{0};
        std::string failure{};
    };

    auto sdk_vec_add(const meccha_sdk::FVector& a, const meccha_sdk::FVector& b) -> meccha_sdk::FVector
    {
        return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
    }

    auto sdk_vec_sub(const meccha_sdk::FVector& a, const meccha_sdk::FVector& b) -> meccha_sdk::FVector
    {
        return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
    }

    auto sdk_vec_mul(const meccha_sdk::FVector& a, double scale) -> meccha_sdk::FVector
    {
        return {a.X * scale, a.Y * scale, a.Z * scale};
    }

    auto sdk_vec_dot(const meccha_sdk::FVector& a, const meccha_sdk::FVector& b) -> double
    {
        return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
    }

    auto sdk_vec_cross(const meccha_sdk::FVector& a, const meccha_sdk::FVector& b) -> meccha_sdk::FVector
    {
        return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
    }

    auto sdk_vec_len(const meccha_sdk::FVector& a) -> double
    {
        return std::sqrt(a.X * a.X + a.Y * a.Y + a.Z * a.Z);
    }

    auto sdk_vec_normalize(const meccha_sdk::FVector& a) -> meccha_sdk::FVector
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

    auto sdk_write_vector3(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const meccha_sdk::FVector& value) -> bool
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

    auto sdk_read_vector3(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, meccha_sdk::FVector& out) -> bool
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
        const auto guobject_delta = ctx.expected_guobject_array >= ctx.actual_guobject_array
            ? static_cast<long long>(ctx.expected_guobject_array - ctx.actual_guobject_array)
            : -static_cast<long long>(ctx.actual_guobject_array - ctx.expected_guobject_array);
        const bool guobject_offsets_match = ctx.expected_guobject_array != 0 && ctx.expected_guobject_array == ctx.actual_guobject_array;
        const bool guobject_offsets_compatible = guobject_offsets_match || guobject_delta == 0x10 || guobject_delta == -0x10 ||
            ctx.guobject_runtime_address_used;
        return "\"sdk_version\":\"chameleonEsp_dumper7_1_7_0_min\"" +
               std::string(",\"sdk_route\":\"sdk_server_paint_batch_strokes\"") +
               ",\"module_base\":\"" + hex_address(ctx.module_base) + "\"" +
               ",\"expected_guobject_array\":\"" + hex_address(ctx.expected_guobject_array) + "\"" +
               ",\"actual_guobject_array\":\"" + hex_address(ctx.actual_guobject_array) + "\"" +
               ",\"guobject_offset_delta\":" + std::to_string(guobject_delta) +
               ",\"guobject_offsets_match\":" + std::string(json_bool(guobject_offsets_match)) +
               ",\"guobject_offsets_compatible\":" + std::string(json_bool(guobject_offsets_compatible)) +
               ",\"guobject_runtime_address_used\":" + std::string(json_bool(ctx.guobject_runtime_address_used)) +
               ",\"expected_gworld\":\"" + hex_address(ctx.expected_gworld) + "\"" +
               ",\"resolved_gworld_address\":\"" + hex_address(ctx.resolved_gworld_address) + "\"" +
               ",\"gworld_source\":\"" + json_escape(ctx.gworld_source) + "\"" +
               ",\"process_event\":\"" + hex_address(ctx.process_event) + "\"" +
               ",\"process_event_offset\":\"" + hex_address(meccha_sdk::Offsets::ProcessEvent) + "\"" +
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
               ",\"runtime_paint_relay_offset\":\"0x770\"" +
               ",\"relay_component\":\"" + hex_address(ctx.relay_component) + "\"" +
               ",\"relay_component_class\":\"" + json_escape(ref.class_name(ctx.relay_component)) + "\"" +
               ",\"function_relay_stroke_batch_to_server_available\":" + std::string(json_bool(ctx.relay_stroke_batch_to_server_function != 0)) +
               ",\"function_relay_paint_to_server_available\":" + std::string(json_bool(ctx.relay_paint_to_server_function != 0)) +
               ",\"function_relay_texture_sync_to_server_available\":" + std::string(json_bool(ctx.relay_texture_sync_to_server_function != 0)) +
               ",\"function_server_relay_texture_sync_available\":" + std::string(json_bool(ctx.server_relay_texture_sync_function != 0)) +
               ",\"function_export_available\":" + std::string(json_bool(ctx.export_function != 0)) +
               ",\"function_import_available\":" + std::string(json_bool(ctx.import_function != 0)) +
               ",\"function_get_render_target_available\":" + std::string(json_bool(ctx.get_render_target_function != 0)) +
               ",\"function_request_full_texture_sync_available\":" + std::string(json_bool(ctx.request_full_texture_sync_function != 0)) +
               ",\"function_server_request_texture_sync_available\":" + std::string(json_bool(ctx.server_request_texture_sync_function != 0)) +
               ",\"function_multicast_sync_channel_data_available\":" + std::string(json_bool(ctx.multicast_sync_channel_data_function != 0)) +
               ",\"function_multicast_sync_compressed_channel_data_available\":" + std::string(json_bool(ctx.multicast_sync_compressed_channel_data_function != 0)) +
               ",\"function_server_paint_batch_available\":" + std::string(json_bool(ctx.server_paint_batch_function != 0)) +
               ",\"function_server_send_stroke_batch_available\":" + std::string(json_bool(ctx.server_send_stroke_batch_function != 0)) +
               ",\"function_server_send_paint_available\":" + std::string(json_bool(ctx.server_send_paint_function != 0)) +
               ",\"function_server_paint_available\":" + std::string(json_bool(ctx.server_paint_function != 0)) +
               ",\"function_paint_at_uv_with_brush_available\":" + std::string(json_bool(ctx.paint_at_uv_with_brush_function != 0)) +
               ",\"function_relay_stroke_batch_to_server\":\"" + hex_address(ctx.relay_stroke_batch_to_server_function) + "\"" +
               ",\"function_relay_paint_to_server\":\"" + hex_address(ctx.relay_paint_to_server_function) + "\"" +
               ",\"function_relay_texture_sync_to_server\":\"" + hex_address(ctx.relay_texture_sync_to_server_function) + "\"" +
               ",\"function_server_relay_texture_sync\":\"" + hex_address(ctx.server_relay_texture_sync_function) + "\"" +
               ",\"function_export\":\"" + hex_address(ctx.export_function) + "\"" +
               ",\"function_import\":\"" + hex_address(ctx.import_function) + "\"" +
               ",\"function_get_render_target\":\"" + hex_address(ctx.get_render_target_function) + "\"" +
               ",\"function_request_full_texture_sync\":\"" + hex_address(ctx.request_full_texture_sync_function) + "\"" +
               ",\"function_server_request_texture_sync\":\"" + hex_address(ctx.server_request_texture_sync_function) + "\"" +
               ",\"function_multicast_sync_channel_data\":\"" + hex_address(ctx.multicast_sync_channel_data_function) + "\"" +
               ",\"function_multicast_sync_compressed_channel_data\":\"" + hex_address(ctx.multicast_sync_compressed_channel_data_function) + "\"" +
               ",\"function_server_paint_batch\":\"" + hex_address(ctx.server_paint_batch_function) + "\"" +
               ",\"function_server_send_stroke_batch\":\"" + hex_address(ctx.server_send_stroke_batch_function) + "\"" +
               ",\"function_server_send_paint\":\"" + hex_address(ctx.server_send_paint_function) + "\"" +
               ",\"function_server_paint\":\"" + hex_address(ctx.server_paint_function) + "\"" +
               ",\"function_paint_at_uv_with_brush\":\"" + hex_address(ctx.paint_at_uv_with_brush_function) + "\"" +
               ",\"param_schema\":\"FPaintStroke{Uv@0,WorldPosition@16,bHasWorldPosition@40,BrushSettings@104,ChannelData@144,TargetChannel@176};ServerPaintBatch{Batch@0};PaintAtUVWithBrush{Uv@0,ChannelData@16,BrushSettings@48,Channel@88};RelayTextureSyncToServer{PaintComponent@0};MulticastSyncChannelData{Channel@0,Data@8}\"" +
               std::string(",\"replication\":\"component_server_paint_batch\"") +
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
        ctx.expected_guobject_array = module.base + meccha_sdk::Offsets::GObjects;
        ctx.expected_gworld = module.base + meccha_sdk::Offsets::GWorld;
        ctx.process_event = module.base + meccha_sdk::Offsets::ProcessEvent;
        const auto guobject_delta = ctx.expected_guobject_array >= ctx.actual_guobject_array
            ? static_cast<long long>(ctx.expected_guobject_array - ctx.actual_guobject_array)
            : -static_cast<long long>(ctx.actual_guobject_array - ctx.expected_guobject_array);
        const bool guobject_offsets_compatible = ctx.expected_guobject_array == ctx.actual_guobject_array ||
            guobject_delta == 0x10 || guobject_delta == -0x10;
        ctx.guobject_runtime_address_used = !guobject_offsets_compatible && ctx.actual_guobject_array != 0;
        if (!address_in_main_module(ctx.process_event))
        {
            ctx.stage = "sdk_mismatch";
            ctx.message = "Dumper7 ProcessEvent offset is outside the main module";
            return ctx;
        }

        auto read_world_from_gworld = [&](std::uintptr_t gworld_address) -> std::uintptr_t {
            if (!address_in_main_module(gworld_address))
            {
                return 0;
            }
            const auto world = safe_read<std::uintptr_t>(gworld_address);
            return live_uobject(world) ? world : 0;
        };
        auto world_has_local_context = [&](std::uintptr_t world) -> bool {
            if (!live_uobject(world))
            {
                return false;
            }
            const auto game_instance = safe_read<std::uintptr_t>(world + meccha_sdk::FieldOffsets::UWorld_OwningGameInstance);
            if (!live_uobject(game_instance))
            {
                return false;
            }
            const auto local_players = safe_read<meccha_sdk::TArray<std::uintptr_t>>(game_instance + meccha_sdk::FieldOffsets::UGameInstance_LocalPlayers);
            return local_players.Data && local_players.Num > 0 && local_players.Num <= 8;
        };

        ctx.resolved_gworld_address = ctx.expected_gworld;
        ctx.gworld_source = "sdk_offset";
        ctx.world = read_world_from_gworld(ctx.expected_gworld);
        if (!world_has_local_context(ctx.world) && ctx.expected_guobject_array && ctx.actual_guobject_array)
        {
            const auto runtime_shift = static_cast<std::intptr_t>(ctx.actual_guobject_array) -
                static_cast<std::intptr_t>(ctx.expected_guobject_array);
            const auto shifted_gworld = static_cast<std::uintptr_t>(
                static_cast<std::intptr_t>(ctx.expected_gworld) + runtime_shift);
            const auto shifted_world = read_world_from_gworld(shifted_gworld);
            if (world_has_local_context(shifted_world))
            {
                ctx.resolved_gworld_address = shifted_gworld;
                ctx.gworld_source = "gobjects_delta_shift";
                ctx.world = shifted_world;
            }
        }
        if (!live_uobject(ctx.world))
        {
            ctx.stage = "world_unavailable";
            ctx.message = "UWorld::GetWorld returned null or invalid object";
            return ctx;
        }
        ctx.game_instance = safe_read<std::uintptr_t>(ctx.world + meccha_sdk::FieldOffsets::UWorld_OwningGameInstance);
        if (!live_uobject(ctx.game_instance))
        {
            ctx.stage = "world_unavailable";
            ctx.message = "UWorld::OwningGameInstance unavailable";
            return ctx;
        }

        const auto local_players = safe_read<meccha_sdk::TArray<std::uintptr_t>>(ctx.game_instance + meccha_sdk::FieldOffsets::UGameInstance_LocalPlayers);
        ctx.local_players_count = local_players.Num;
        if (!local_players.Data || local_players.Num <= 0 || local_players.Num > 8)
        {
            ctx.stage = "local_pawn_unavailable";
            ctx.message = "GameInstance.LocalPlayers is empty or invalid";
            return ctx;
        }
        ctx.local_player = safe_read<std::uintptr_t>(reinterpret_cast<std::uintptr_t>(local_players.Data));
        if (!live_uobject(ctx.local_player))
        {
            ctx.stage = "local_pawn_unavailable";
            ctx.message = "LocalPlayers[0] unavailable";
            return ctx;
        }
        ctx.controller = safe_read<std::uintptr_t>(ctx.local_player + meccha_sdk::FieldOffsets::UPlayer_PlayerController);
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
        meccha_sdk::Controller_K2_GetPawn pawn_params{};
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
            meccha_sdk::Actor_K2_GetActorLocation location_params{};
            std::string location_failure{};
            if (process_event(ctx.pawn, ctx.k2_get_actor_location_function, reinterpret_cast<std::uint8_t*>(&location_params), location_failure))
            {
                ctx.body_world_position = location_params.ReturnValue;
            }
        }
        ctx.component = safe_read<std::uintptr_t>(ctx.pawn + meccha_sdk::FieldOffsets::BP_FirstPersonCharacter_RuntimePaintable);
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
        ctx.relay_component = safe_read<std::uintptr_t>(ctx.controller + meccha_sdk::FieldOffsets::BP_PlayerController_RuntimePaintRelay);
        if (live_uobject(ctx.relay_component))
        {
            ctx.relay_stroke_batch_to_server_function = ref.find_function(ctx.relay_component, "RelayStrokeBatchToServer");
            ctx.relay_paint_to_server_function = ref.find_function(ctx.relay_component, "RelayPaintToServer");
            ctx.relay_texture_sync_to_server_function = ref.find_function(ctx.relay_component, "RelayTextureSyncToServer");
            ctx.server_relay_texture_sync_function = ref.find_function(ctx.relay_component, "ServerRelayTextureSync");
        }
        ctx.export_function = ref.find_function(ctx.component, "ExportChannelToBytes");
        ctx.import_function = ref.find_function(ctx.component, "ImportChannelFromBytes");
        ctx.get_render_target_function = ref.find_function(ctx.component, "GetRenderTarget");
        ctx.request_full_texture_sync_function = ref.find_function(ctx.component, "RequestFullTextureSync");
        ctx.server_request_texture_sync_function = ref.find_function(ctx.component, "ServerRequestTextureSync");
        ctx.multicast_sync_channel_data_function = ref.find_function(ctx.component, "MulticastSyncChannelData");
        ctx.multicast_sync_compressed_channel_data_function = ref.find_function(ctx.component, "MulticastSyncCompressedChannelData");
        ctx.server_paint_batch_function = ref.find_function(ctx.component, "ServerPaintBatch");
        ctx.server_send_stroke_batch_function = ref.find_function(ctx.component, "ServerSendStrokeBatch");
        ctx.server_send_paint_function = ref.find_function(ctx.component, "ServerSendPaint");
        ctx.server_paint_function = ref.find_function(ctx.component, "ServerPaint");
        ctx.paint_at_uv_with_brush_function = ref.find_function(ctx.component, "PaintAtUVWithBrush");
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
        std::uintptr_t query{0};
        std::uintptr_t hit_test_function{0};
        std::uintptr_t deproject_function{0};
        std::uintptr_t query_function{0};
        std::string sampling_backend{"unset"};
        std::string hit_test_schema{};
        std::string deproject_schema{};
        std::string query_schema{};
        int viewport_width{0};
        int viewport_height{0};
        int hit_test_calls{0};
        int hit_test_success{0};
        int hit_test_failed{0};
        int deproject_calls{0};
        int deproject_ok{0};
        int deproject_return_false{0};
        int deproject_direction_invalid{0};
        int deproject_process_failed{0};
        int query_calls{0};
        int query_param_failed{0};
        int query_result_failed{0};
        int owner_matches{0};
        int owner_unknown_accepted{0};
        int owner_mismatch_rejected{0};
        int min_front_hits{2048};
        int target_front_hits{220000};
        int target_unique_atlas_texels{500000};
        int unique_atlas_texels{0};
        int coarse_grid_x{96};
        int coarse_grid_y{72};
        int refine_grid_x{640};
        int refine_grid_y{520};
        int coarse_seeds{0};
        int refine_seeds{0};
        int hard_attempt_budget{600000};
        int adaptive_passes{0};
        int adaptive_seeds{0};
        int adaptive_attempts{0};
        int adaptive_hits{0};
        double bbox_min_nx{1.0};
        double bbox_min_ny{1.0};
        double bbox_max_nx{0.0};
        double bbox_max_ny{0.0};
        int attempts{0};
        int query_success{0};
        int uv_hits{0};
        int duplicate_uv{0};
        int rejected{0};
        double first_screen_x{0.0};
        double first_screen_y{0.0};
        meccha_sdk::FVector first_ray_location{};
        meccha_sdk::FVector first_ray_direction{};
        double first_hit_u{0.0};
        double first_hit_v{0.0};
        meccha_sdk::FVector first_hit_world_position{};
        std::uintptr_t first_hit_actor{0};
        std::uintptr_t first_hit_component{0};
        std::string first_deproject_failure{};
        std::string first_query_failure{};
    };

    struct SdkFrontCaptureProbe
    {
        bool ok{false};
        std::string failure{"front_capture_unavailable"};
        int requested_samples{0};
        std::uintptr_t project_world_to_screen_function{0};
        std::uintptr_t scene_capture_class{0};
        std::uintptr_t create_render_target_function{0};
        std::uintptr_t read_render_target_raw_pixel_function{0};
        std::uintptr_t read_render_target_pixel_function{0};
        std::uintptr_t read_render_target_raw_function{0};
        std::uintptr_t read_render_target_function{0};
    };

    struct SdkFrontCaptureResult
    {
        bool ok{false};
        std::string failure{"front_capture_unavailable"};
        std::vector<FrontSample> samples{};
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
        meccha_sdk::FVector capture_location{};
        meccha_sdk::FVector capture_direction{};
        int project_attempts{0};
        int project_success{0};
        int project_failed{0};
        int project_out_of_view{0};
        double project_delta_sum_px{0.0};
        double project_delta_max_px{0.0};
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

    struct SdkUvGapFillStats
    {
        int candidates{0};
        int sent{0};
        int bounded{0};
        int edge_extended{0};
        int rejected_unbounded{0};
        int rejected_normal{0};
        int rejected_occupied{0};
        int direct_cells{0};
        int considered_cells{0};
        double coverage_before{0.0};
        double coverage_after{0.0};
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

    auto sdk_probe_front_capture_backend(Reflection& ref,
                                         const SdkContext& ctx,
                                         const SdkNativeFrontSampleResult& samples) -> SdkFrontCaptureProbe
    {
        SdkFrontCaptureProbe out{};
        out.requested_samples = static_cast<int>(samples.samples.size());
        out.project_world_to_screen_function = ref.find_function(ctx.controller, "ProjectWorldLocationToScreen");
        out.scene_capture_class = ref.find_class("SceneCapture2D");
        out.create_render_target_function = sdk_find_object_named(ref, "CreateRenderTarget2D");
        out.read_render_target_raw_pixel_function = sdk_find_object_named(ref, "ReadRenderTargetRawPixel");
        out.read_render_target_pixel_function = sdk_find_object_named(ref, "ReadRenderTargetPixel");
        out.read_render_target_raw_function = sdk_find_object_named(ref, "ReadRenderTargetRaw");
        out.read_render_target_function = sdk_find_object_named(ref, "ReadRenderTarget");

        if (out.requested_samples <= 0)
        {
            out.failure = "front_capture_no_surface_samples";
            return out;
        }
        if (!out.project_world_to_screen_function)
        {
            out.failure = "front_capture_project_world_to_screen_unavailable";
            return out;
        }
        if (!out.scene_capture_class)
        {
            out.failure = "front_capture_scene_capture_class_unavailable";
            return out;
        }
        if (!out.create_render_target_function)
        {
            out.failure = "front_capture_create_render_target_unavailable";
            return out;
        }
        if (!out.read_render_target_raw_pixel_function && !out.read_render_target_pixel_function &&
            !out.read_render_target_raw_function && !out.read_render_target_function)
        {
            out.failure = "front_capture_read_render_target_unavailable";
            return out;
        }

        out.ok = true;
        out.failure = "ok";
        return out;
    }

    auto sdk_native_front_metadata(const SdkNativeFrontSampleResult& native_front) -> std::string
    {
        return ",\"front_sample_count\":" + std::to_string(native_front.samples.size()) +
               ",\"front_sample_source\":\"native_ue4ss_surface_samples\"" +
               ",\"front_mapping_required\":\"game_surface_trace_world_position\"" +
               ",\"front_native_min_surface_samples\":" + std::to_string(native_front.min_front_hits) +
               ",\"front_native_target_surface_samples\":" + std::to_string(native_front.target_front_hits) +
               ",\"front_native_target_unique_atlas_texels\":" + std::to_string(native_front.target_unique_atlas_texels) +
               ",\"front_native_unique_atlas_texels\":" + std::to_string(native_front.unique_atlas_texels) +
               ",\"front_native_coarse_grid_x\":" + std::to_string(native_front.coarse_grid_x) +
               ",\"front_native_coarse_grid_y\":" + std::to_string(native_front.coarse_grid_y) +
               ",\"front_native_refine_grid_x\":" + std::to_string(native_front.refine_grid_x) +
               ",\"front_native_refine_grid_y\":" + std::to_string(native_front.refine_grid_y) +
               ",\"front_native_coarse_seeds\":" + std::to_string(native_front.coarse_seeds) +
               ",\"front_native_refine_seeds\":" + std::to_string(native_front.refine_seeds) +
               ",\"front_native_hard_attempt_budget\":" + std::to_string(native_front.hard_attempt_budget) +
               ",\"front_native_adaptive_passes\":" + std::to_string(native_front.adaptive_passes) +
               ",\"front_native_adaptive_seeds\":" + std::to_string(native_front.adaptive_seeds) +
               ",\"front_native_adaptive_attempts\":" + std::to_string(native_front.adaptive_attempts) +
               ",\"front_native_adaptive_hits\":" + std::to_string(native_front.adaptive_hits) +
               ",\"front_native_bbox_min_nx\":" + std::to_string(native_front.bbox_min_nx) +
               ",\"front_native_bbox_min_ny\":" + std::to_string(native_front.bbox_min_ny) +
               ",\"front_native_bbox_max_nx\":" + std::to_string(native_front.bbox_max_nx) +
               ",\"front_native_bbox_max_ny\":" + std::to_string(native_front.bbox_max_ny) +
               ",\"front_native_sampling_backend\":\"" + json_escape(native_front.sampling_backend) + "\"" +
               ",\"front_native_query\":\"" + hex_address(native_front.query) + "\"" +
               ",\"front_native_mesh\":\"" + hex_address(native_front.mesh) + "\"" +
               ",\"front_native_hit_test_function\":\"" + hex_address(native_front.hit_test_function) + "\"" +
               ",\"front_native_deproject_function\":\"" + hex_address(native_front.deproject_function) + "\"" +
               ",\"front_native_query_function\":\"" + hex_address(native_front.query_function) + "\"" +
               ",\"front_native_hit_test_calls\":" + std::to_string(native_front.hit_test_calls) +
               ",\"front_native_hit_test_success\":" + std::to_string(native_front.hit_test_success) +
               ",\"front_native_hit_test_failed\":" + std::to_string(native_front.hit_test_failed) +
               ",\"front_native_viewport_width\":" + std::to_string(native_front.viewport_width) +
               ",\"front_native_viewport_height\":" + std::to_string(native_front.viewport_height) +
               ",\"front_native_deproject_calls\":" + std::to_string(native_front.deproject_calls) +
               ",\"front_native_deproject_ok\":" + std::to_string(native_front.deproject_ok) +
               ",\"front_native_query_calls\":" + std::to_string(native_front.query_calls) +
               ",\"front_native_owner_matches\":" + std::to_string(native_front.owner_matches) +
               ",\"front_native_query_success\":" + std::to_string(native_front.query_success) +
               ",\"front_native_uv_hits\":" + std::to_string(native_front.uv_hits) +
               ",\"front_native_duplicate_uv\":" + std::to_string(native_front.duplicate_uv) +
               ",\"front_native_rejected\":" + std::to_string(native_front.rejected) +
               ",\"front_native_first_hit_u\":" + std::to_string(native_front.first_hit_u) +
               ",\"front_native_first_hit_v\":" + std::to_string(native_front.first_hit_v) +
               ",\"front_native_first_hit_world_x\":" + std::to_string(native_front.first_hit_world_position.X) +
               ",\"front_native_first_hit_world_y\":" + std::to_string(native_front.first_hit_world_position.Y) +
               ",\"front_native_first_hit_world_z\":" + std::to_string(native_front.first_hit_world_position.Z) +
               ",\"front_native_first_hit_actor\":\"" + hex_address(native_front.first_hit_actor) + "\"" +
               ",\"front_native_first_hit_component\":\"" + hex_address(native_front.first_hit_component) + "\"" +
               ",\"front_native_hit_test_schema\":\"" + json_escape(native_front.hit_test_schema) + "\"" +
               ",\"front_native_first_deproject_failure\":\"" + json_escape(native_front.first_deproject_failure) + "\"" +
               ",\"front_native_first_query_failure\":\"" + json_escape(native_front.first_query_failure) + "\"" +
               ",\"front_native_failure\":\"" + json_escape(native_front.failure) + "\"";
    }

    auto sdk_front_capture_metadata(const SdkFrontCaptureProbe& capture) -> std::string
    {
        return ",\"capture_backend\":\"native_scene_capture_required\"" +
               std::string(",\"srgb\":true") +
               ",\"front_capture_requested_samples\":" + std::to_string(capture.requested_samples) +
               ",\"front_capture_project_world_to_screen_function\":\"" + hex_address(capture.project_world_to_screen_function) + "\"" +
               ",\"front_capture_scene_capture_class\":\"" + hex_address(capture.scene_capture_class) + "\"" +
               ",\"front_capture_create_render_target_function\":\"" + hex_address(capture.create_render_target_function) + "\"" +
               ",\"front_capture_read_raw_pixel_function\":\"" + hex_address(capture.read_render_target_raw_pixel_function) + "\"" +
               ",\"front_capture_read_pixel_function\":\"" + hex_address(capture.read_render_target_pixel_function) + "\"" +
               ",\"front_capture_read_raw_function\":\"" + hex_address(capture.read_render_target_raw_function) + "\"" +
               ",\"front_capture_read_function\":\"" + hex_address(capture.read_render_target_function) + "\"" +
               ",\"front_capture_failure\":\"" + json_escape(capture.failure) + "\"";
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

    auto sdk_find_front_mesh(Reflection& ref, const SdkContext& ctx) -> std::uintptr_t
    {
        if (const auto mesh = read_object_property_by_names(ref,
                                                            ctx.pawn,
                                                            {"Mesh",
                                                             "FirstPersonMesh",
                                                             "BodyMesh",
                                                             "CharacterMesh",
                                                             "SkeletalMeshComponent",
                                                             "TargetMeshComponent"}))
        {
            return mesh;
        }
        std::uintptr_t found = 0;
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
                found = object;
                return true;
            }
            return false;
        });
        return found;
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
                meccha_sdk::FVector direction{};
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

    auto sdk_decode_brush_query_result(Reflection& ref, std::uintptr_t function, std::uint8_t* params) -> SdkBrushQueryHit
    {
        SdkBrushQueryHit out{};
        std::uintptr_t return_prop = 0;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            if (ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)) == "ReturnValue")
            {
                return_prop = prop;
                break;
            }
        }
        if (!return_prop)
        {
            out.failure = "brush_query_return_struct_unavailable";
            return out;
        }
        const auto offset = prop_offset(return_prop);
        const auto structure = struct_type(ref, return_prop, {"bSuccess", "HitUV", "WorldPosition", "HitComponent", "HitActor"});
        if (offset < 0 || !structure)
        {
            out.failure = "brush_query_return_struct_invalid";
            return out;
        }
        auto* base = params + offset;
        out.params_ok = true;
        bool has_success_property = false;
        if (const auto p = find_property_any(ref, structure, {"bSuccess"}))
        {
            has_success_property = true;
            out.success = sdk_read_bool(p, base);
        }
        if (const auto p = find_property_any(ref, structure, {"bHasUV"}))
        {
            out.has_uv = sdk_read_bool(p, base);
        }
        if (const auto p = find_property_any(ref, structure, {"HitUV"}))
        {
            out.has_uv = sdk_read_vector2(ref, p, base, out.u, out.v) || out.has_uv;
        }
        if (const auto p = find_property_any(ref, structure, {"WorldPosition", "HitWorldPosition"}))
        {
            sdk_read_vector3(ref, p, base, out.world_position);
        }
        if (const auto p = find_property_any(ref, structure, {"WorldNormal", "HitNormal"}))
        {
            sdk_read_vector3(ref, p, base, out.normal);
        }
        if (const auto p = find_property_any(ref, structure, {"HitActor", "Actor"}))
        {
            out.actor = sdk_read_object(p, base);
        }
        if (const auto p = find_property_any(ref, structure, {"HitComponent", "Component"}))
        {
            out.component = sdk_read_object(p, base);
        }
        if (!has_success_property)
        {
            out.success = out.has_uv;
        }
        if (!out.success && out.failure.empty())
        {
            out.failure = "brush_query_result_unsuccessful";
        }
        return out;
    }

    auto sdk_query_brush_from_world_ray(Reflection& ref,
                                        std::uintptr_t query,
                                        const meccha_sdk::FVector& origin,
                                        const meccha_sdk::FVector& direction) -> SdkBrushQueryHit
    {
        SdkBrushQueryHit out{};
        const auto function = ref.find_function(query, "QueryFromWorldRay");
        if (!function)
        {
            out.failure = "query_from_world_ray_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 4096)
        {
            out.failure = "query_from_world_ray_params_size_invalid";
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote_origin = false;
        bool wrote_direction = false;
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = lower_copy(ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName)));
            if (name == "returnvalue")
            {
                continue;
            }
            if (contains_text(name, "origin"))
            {
                wrote_origin = sdk_write_vector3(ref, prop, params.data(), origin) || wrote_origin;
            }
            else if (contains_text(name, "direction"))
            {
                wrote_direction = sdk_write_vector3(ref, prop, params.data(), sdk_vec_normalize(direction)) || wrote_direction;
            }
        }
        if (!wrote_origin || !wrote_direction)
        {
            out.failure = !wrote_origin ? "brush_query_origin_param_unresolved" : "brush_query_direction_param_unresolved";
            return out;
        }
        std::string failure{};
        if (!process_event(query, function, params.data(), failure))
        {
            out.failure = "brush_query_process_event_failed:" + failure;
            return out;
        }
        return sdk_decode_brush_query_result(ref, function, params.data());
    }

    auto sdk_hit_test_at_screen_position(Reflection& ref,
                                         const SdkContext& ctx,
                                         std::uintptr_t mesh,
                                         double screen_x,
                                         double screen_y) -> SdkBrushQueryHit
    {
        SdkBrushQueryHit out{};
        const auto function = ref.find_function(ctx.component, "HitTestAtScreenPosition");
        if (!function)
        {
            out.failure = "hit_test_at_screen_position_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size != static_cast<int>(sizeof(meccha_sdk::RuntimePaintableComponent_HitTestAtScreenPosition)))
        {
            out.failure = "hit_test_params_size_mismatch";
            return out;
        }

        meccha_sdk::RuntimePaintableComponent_HitTestAtScreenPosition params{};
        params.MeshComponent = reinterpret_cast<void*>(mesh);
        params.ScreenPosition = {screen_x, screen_y};
        params.PlayerController = reinterpret_cast<void*>(ctx.controller);
        params.bUseCachedTriangles = true;

        std::string failure{};
        if (!process_event(ctx.component, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            out.failure = "hit_test_process_event_failed:" + failure;
            return out;
        }

        out.params_ok = true;
        out.success = params.ReturnValue.bSuccess;
        out.has_uv = params.ReturnValue.bSuccess;
        out.u = params.ReturnValue.HitUV.X;
        out.v = params.ReturnValue.HitUV.Y;
        out.world_position = params.ReturnValue.HitWorldPosition;
        out.normal = params.ReturnValue.HitNormal;
        out.actor = ctx.pawn;
        out.component = mesh;
        if (!out.success)
        {
            out.failure = "hit_test_result_unsuccessful";
        }
        return out;
    }

    auto sdk_collect_native_front_samples(Reflection& ref,
                                          const SdkContext& ctx,
                                          const std::vector<FrontSample>& color_source) -> SdkNativeFrontSampleResult
    {
        SdkNativeFrontSampleResult result{};
        result.mesh = sdk_find_front_mesh(ref, ctx);
        result.query = sdk_find_screen_space_brush_query(ref, ctx);
        result.hit_test_function = ref.find_function(ctx.component, "HitTestAtScreenPosition");
        result.deproject_function = ref.find_function(ctx.controller, "DeprojectScreenPositionToWorld");
        result.query_function = result.query ? ref.find_function(result.query, "QueryFromWorldRay") : 0;
        result.hit_test_schema = function_param_schema(ref, result.hit_test_function);
        result.deproject_schema = function_param_schema(ref, result.deproject_function);
        result.query_schema = function_param_schema(ref, result.query_function);
        const bool use_hit_test = result.mesh && result.hit_test_function;
        result.sampling_backend = use_hit_test ? "runtime_paintable_hit_test_cached_triangles" : "screenspace_brush_query_world_ray";
        if (!use_hit_test && !result.query)
        {
            result.failure = "screen_space_brush_query_unavailable";
            return result;
        }
        if (!use_hit_test && !sdk_configure_screen_space_brush_query(ref, result.query, ctx.pawn, result.mesh))
        {
            result.failure = "screen_space_brush_query_config_failed";
            return result;
        }
        const auto viewport = sdk_get_viewport_info(ref, ctx);
        result.viewport_width = viewport.width;
        result.viewport_height = viewport.height;
        if (viewport.width <= 0 || viewport.height <= 0)
        {
            result.failure = "viewport_unavailable";
            return result;
        }

        const int target_samples = std::max(result.min_front_hits, result.target_front_hits);
        const int hard_attempts = std::max(result.hard_attempt_budget, target_samples * 2);
        result.hard_attempt_budget = hard_attempts;
        const auto collect_start = std::chrono::steady_clock::now();
        constexpr double collect_timeout_ms = 210000.0;
        bool stop_sampling = false;
        int last_collect_progress = -1;
        auto total_surface_attempts = [&]() {
            return result.deproject_calls + result.hit_test_calls;
        };
        auto sampling_goal_met = [&]() {
            return static_cast<int>(result.samples.size()) >= target_samples &&
                   result.unique_atlas_texels >= result.target_unique_atlas_texels;
        };
        auto collect_elapsed_ms = [&]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - collect_start).count();
        };
        auto emit_collect_progress = [&](bool force) {
            const auto attempt_progress = hard_attempts > 0
                ? static_cast<int>((static_cast<double>(total_surface_attempts()) / static_cast<double>(hard_attempts)) * 100.0)
                : 0;
            const auto sample_progress = target_samples > 0
                ? static_cast<int>((static_cast<double>(result.samples.size()) / static_cast<double>(target_samples)) * 100.0)
                : 0;
            const auto unique_progress = result.target_unique_atlas_texels > 0
                ? static_cast<int>((static_cast<double>(result.unique_atlas_texels) / static_cast<double>(result.target_unique_atlas_texels)) * 100.0)
                : 0;
            const auto progress = std::max(0, std::min(99, std::max({attempt_progress, sample_progress, unique_progress})));
            if (!force && progress == last_collect_progress)
            {
                return;
            }
            last_collect_progress = progress;
            write_bridge_progress("front_sampling_collecting",
                                  "collecting native front surface samples",
                                  progress,
                                  100,
                                  collect_elapsed_ms(),
                                  "\"front_hits\":" + std::to_string(result.samples.size()) +
                                      ",\"unique_atlas_texels\":" + std::to_string(result.unique_atlas_texels) +
                                      ",\"deproject_calls\":" + std::to_string(result.deproject_calls) +
                                      ",\"hit_test_calls\":" + std::to_string(result.hit_test_calls) +
                                      ",\"sampling_backend\":\"" + json_escape(result.sampling_backend) + "\"" +
                                      ",\"hard_attempt_budget\":" + std::to_string(hard_attempts) +
                                      ",\"target_front_hits\":" + std::to_string(target_samples) +
                                      ",\"target_unique_atlas_texels\":" + std::to_string(result.target_unique_atlas_texels));
        };
        std::unordered_set<std::uint64_t> unique_texels{};
        unique_texels.reserve(static_cast<std::size_t>(target_samples) * 2U);
        result.samples.reserve(static_cast<std::size_t>(target_samples));

        auto cell_jitter = [](int x, int y, int salt) -> double {
            std::uint32_t n = static_cast<std::uint32_t>(x) * 0x9E3779B1u ^
                              static_cast<std::uint32_t>(y) * 0x85EBCA77u ^
                              static_cast<std::uint32_t>(salt) * 0xC2B2AE3Du;
            n ^= n >> 16;
            n *= 0x7FEB352Du;
            n ^= n >> 15;
            n *= 0x846CA68Bu;
            n ^= n >> 16;
            return static_cast<double>(n & 0xFFFFu) / 65535.0;
        };

        auto record_bbox = [&](double nx, double ny) {
            result.bbox_min_nx = std::min(result.bbox_min_nx, nx);
            result.bbox_min_ny = std::min(result.bbox_min_ny, ny);
            result.bbox_max_nx = std::max(result.bbox_max_nx, nx);
            result.bbox_max_ny = std::max(result.bbox_max_ny, ny);
        };

        auto try_sample = [&](double nx, double ny, bool refined) -> bool {
            if (stop_sampling || total_surface_attempts() >= hard_attempts || sampling_goal_met())
            {
                return false;
            }
            if (collect_elapsed_ms() >= collect_timeout_ms)
            {
                result.failure = "front_sampling_timeout_before_bridge_timeout";
                stop_sampling = true;
                emit_collect_progress(true);
                return false;
            }
            if ((total_surface_attempts() % 2500) == 0)
            {
                emit_collect_progress(false);
            }
            nx = clamp01(nx);
            ny = clamp01(ny);
            const auto screen_x = nx * static_cast<double>(viewport.width);
            const auto screen_y = ny * static_cast<double>(viewport.height);
            SdkBrushQueryHit hit{};
            if (use_hit_test)
            {
                ++result.hit_test_calls;
                hit = sdk_hit_test_at_screen_position(ref, ctx, result.mesh, screen_x, screen_y);
                if (result.hit_test_calls == 1)
                {
                    result.first_screen_x = screen_x;
                    result.first_screen_y = screen_y;
                }
            }
            else
            {
                const auto ray = sdk_deproject_screen_position(ref, ctx, screen_x, screen_y);
                ++result.deproject_calls;
                if (result.deproject_calls == 1)
                {
                    result.first_screen_x = screen_x;
                    result.first_screen_y = screen_y;
                    result.first_ray_location = ray.location;
                    result.first_ray_direction = ray.direction;
                }
                if (!ray.ok)
                {
                    if (result.failure.empty())
                    {
                        result.failure = ray.failure;
                    }
                    if (result.first_deproject_failure.empty())
                    {
                        result.first_deproject_failure = ray.failure;
                    }
                    if (contains_text(ray.failure, "return_false"))
                    {
                        ++result.deproject_return_false;
                    }
                    else if (contains_text(ray.failure, "direction_invalid"))
                    {
                        ++result.deproject_direction_invalid;
                    }
                    else if (contains_text(ray.failure, "process_event"))
                    {
                        ++result.deproject_process_failed;
                    }
                    ++result.rejected;
                    return false;
                }
                ++result.deproject_ok;
                hit = sdk_query_brush_from_world_ray(ref, result.query, ray.location, ray.direction);
                ++result.query_calls;
            }
            ++result.attempts;
            if (!hit.params_ok)
            {
                if (result.failure.empty())
                {
                    result.failure = hit.failure;
                }
                if (result.first_query_failure.empty())
                {
                    result.first_query_failure = hit.failure;
                }
                if (use_hit_test)
                {
                    ++result.hit_test_failed;
                }
                ++result.query_param_failed;
                ++result.rejected;
                return false;
            }
            if (!hit.success || !hit.has_uv || !std::isfinite(hit.u) || !std::isfinite(hit.v) ||
                !std::isfinite(hit.world_position.X) || !std::isfinite(hit.world_position.Y) || !std::isfinite(hit.world_position.Z))
            {
                if (result.samples.empty() && result.first_query_failure.empty())
                {
                    result.first_query_failure = hit.failure.empty() ? "brush_query_no_hit" : hit.failure;
                }
                ++result.query_result_failed;
                if (use_hit_test)
                {
                    ++result.hit_test_failed;
                }
                ++result.rejected;
                return false;
            }
            if (use_hit_test)
            {
                ++result.hit_test_success;
            }
            ++result.query_success;
            if (result.query_success == 1)
            {
                result.first_hit_u = hit.u;
                result.first_hit_v = hit.v;
                result.first_hit_world_position = hit.world_position;
                result.first_hit_actor = hit.actor;
                result.first_hit_component = hit.component;
            }
            const bool owner_hit = (hit.component && hit.component == result.mesh) ||
                                   sdk_object_is_or_belongs_to(ref, hit.actor, ctx.pawn) ||
                                   sdk_object_is_or_belongs_to(ref, hit.component, ctx.pawn);
            if (!owner_hit)
            {
                if (result.samples.empty() && result.first_query_failure.empty())
                {
                    result.first_query_failure = (!hit.actor && !hit.component)
                        ? "brush_query_owner_unavailable"
                        : "brush_query_owner_mismatch";
                }
                if (!hit.actor && !hit.component)
                {
                    ++result.owner_unknown_accepted;
                }
                else
                {
                    ++result.owner_mismatch_rejected;
                }
                ++result.rejected;
                return false;
            }
            ++result.owner_matches;
            ++result.uv_hits;
            const auto ux = static_cast<std::uint64_t>(std::max(0, std::min(4095, static_cast<int>(clamp01(hit.u) * 4096.0))));
            const auto uy = static_cast<std::uint64_t>(std::max(0, std::min(4095, static_cast<int>(clamp01(hit.v) * 4096.0))));
            const auto key = (uy << 32) | ux;
            if (!unique_texels.insert(key).second)
            {
                ++result.duplicate_uv;
                return false;
            }
            result.unique_atlas_texels = static_cast<int>(unique_texels.size());

            FrontSample sample{};
            sample.u = clamp01(hit.u);
            sample.v = clamp01(hit.v);
            sample.r = 0.34;
            sample.g = 0.36;
            sample.b = 0.32;
            sample.roughness = 0.65;
            sample.radius = 0.0025;
            sample.screen_nx = nx;
            sample.screen_ny = ny;
            sample.has_world_position = true;
            sample.world_position = hit.world_position;
            sample.normal = sdk_vec_normalize(hit.normal);
            result.samples.push_back(sample);
            record_bbox(nx, ny);
            if (refined)
            {
                ++result.refine_seeds;
            }
            else
            {
                ++result.coarse_seeds;
            }
            return true;
        };

        for (int y = 0; y < result.coarse_grid_y && !stop_sampling && !sampling_goal_met(); ++y)
        {
            for (int x = 0; x < result.coarse_grid_x && !stop_sampling && !sampling_goal_met(); ++x)
            {
                const auto jx = (cell_jitter(x, y, 11) - 0.5) * 0.72;
                const auto jy = (cell_jitter(x, y, 23) - 0.5) * 0.72;
                const auto nx = 0.24 + ((static_cast<double>(x) + 0.5 + jx) / static_cast<double>(result.coarse_grid_x)) * 0.52;
                const auto ny = 0.08 + ((static_cast<double>(y) + 0.5 + jy) / static_cast<double>(result.coarse_grid_y)) * 0.84;
                try_sample(nx, ny, false);
            }
        }

        const bool have_bbox = result.bbox_max_nx >= result.bbox_min_nx && result.bbox_max_ny >= result.bbox_min_ny;
        const auto span_x = have_bbox ? std::max(0.02, result.bbox_max_nx - result.bbox_min_nx) : 0.52;
        const auto span_y = have_bbox ? std::max(0.02, result.bbox_max_ny - result.bbox_min_ny) : 0.84;
        const auto query_min_nx = have_bbox ? clamp01(result.bbox_min_nx - span_x * 0.35) : 0.24;
        const auto query_max_nx = have_bbox ? clamp01(result.bbox_max_nx + span_x * 0.35) : 0.76;
        const auto query_min_ny = have_bbox ? clamp01(result.bbox_min_ny - span_y * 0.18) : 0.08;
        const auto query_max_ny = have_bbox ? clamp01(result.bbox_max_ny + span_y * 0.55) : 0.92;

        for (int y = 0; y < result.refine_grid_y && !stop_sampling && !sampling_goal_met(); ++y)
        {
            for (int x = 0; x < result.refine_grid_x && !stop_sampling && !sampling_goal_met(); ++x)
            {
                const auto jx = (cell_jitter(x, y, 37) - 0.5) * 0.84;
                const auto jy = (cell_jitter(x, y, 41) - 0.5) * 0.84;
                const auto local_nx = (static_cast<double>(x) + 0.5 + jx) / static_cast<double>(result.refine_grid_x);
                const auto local_ny = (static_cast<double>(y) + 0.5 + jy) / static_cast<double>(result.refine_grid_y);
                const auto nx = query_min_nx + local_nx * (query_max_nx - query_min_nx);
                const auto ny = query_min_ny + local_ny * (query_max_ny - query_min_ny);
                try_sample(nx, ny, true);
            }
        }

        const double adaptive_base_dx = std::max(0.00045, (query_max_nx - query_min_nx) / static_cast<double>(std::max(64, result.refine_grid_x)));
        const double adaptive_base_dy = std::max(0.00045, (query_max_ny - query_min_ny) / static_cast<double>(std::max(64, result.refine_grid_y)));
        const double directions[][2] = {
            {1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0},
            {0.7071, 0.7071}, {-0.7071, 0.7071}, {0.7071, -0.7071}, {-0.7071, -0.7071},
            {1.0, 0.45}, {-1.0, 0.45}, {1.0, -0.45}, {-1.0, -0.45},
        };
        for (int pass = 0;
             pass < 8 && !stop_sampling && total_surface_attempts() < hard_attempts && !sampling_goal_met();
             ++pass)
        {
            const auto seeds = result.samples;
            if (seeds.empty())
            {
                break;
            }
            ++result.adaptive_passes;
            result.adaptive_seeds += static_cast<int>(seeds.size());
            const auto ring = static_cast<double>(pass + 1);
            const auto pass_dx = adaptive_base_dx * ring;
            const auto pass_dy = adaptive_base_dy * ring;
            for (std::size_t i = 0;
                 i < seeds.size() && !stop_sampling && total_surface_attempts() < hard_attempts && !sampling_goal_met();
                 ++i)
            {
                const auto& seed = seeds[i];
                for (int d = 0;
                     d < static_cast<int>(sizeof(directions) / sizeof(directions[0])) &&
                     !stop_sampling &&
                     total_surface_attempts() < hard_attempts &&
                     !sampling_goal_met();
                     ++d)
                {
                    const auto jitter_scale = 0.82 + cell_jitter(static_cast<int>(i), pass, d) * 0.36;
                    const auto nx = seed.screen_nx + directions[d][0] * pass_dx * jitter_scale;
                    const auto ny = seed.screen_ny + directions[d][1] * pass_dy * jitter_scale;
                    const auto before = result.samples.size();
                    ++result.adaptive_attempts;
                    if (try_sample(nx, ny, true) && result.samples.size() > before)
                    {
                        ++result.adaptive_hits;
                    }
                }
            }
        }

        if (result.samples.empty() && result.failure.empty())
        {
            result.failure = "native_front_surface_samples_empty";
        }
        emit_collect_progress(true);
        return result;
    }

    auto sdk_export_channel_bytes(Reflection& ref, const SdkContext& ctx, int channel) -> ChannelBuffer
    {
        (void)ref;
        ChannelBuffer out{};
        out.channel = channel;
        if (!ctx.export_function)
        {
            out.failure = "export_unavailable";
            return out;
        }
        meccha_sdk::RuntimePaintableComponent_ExportChannelToBytes params{};
        params.Channel = static_cast<meccha_sdk::EPaintChannel>(channel);
        std::string failure{};
        if (!process_event(ctx.component, ctx.export_function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            out.failure = "export_process_event_failed:" + failure;
            return out;
        }
        if (!params.ReturnValue)
        {
            out.failure = "export_return_false";
            return out;
        }
        if (!params.OutData.Data || params.OutData.Num <= 0 || params.OutData.Num > 128 * 1024 * 1024 || params.OutData.Max < params.OutData.Num)
        {
            out.failure = "export_array_invalid";
            return out;
        }
        out.bytes.resize(static_cast<std::size_t>(params.OutData.Num));
        if (!safe_copy(out.bytes.data(), params.OutData.Data, out.bytes.size()))
        {
            out.failure = "export_array_copy_failed";
            out.bytes.clear();
            return out;
        }
        out.hash = hash_bytes(out.bytes);
        const auto [width, height] = infer_channel_dimensions(out.bytes.size());
        out.width = width;
        out.height = height;
        out.bytes_per_pixel = (width > 0 && height > 0 && out.bytes.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) ? 4 : 1;
        out.ok = true;
        return out;
    }

    auto sdk_import_channel_bytes(const SdkContext& ctx, int channel, const std::vector<std::uint8_t>& bytes, std::string& failure) -> bool
    {
        failure.clear();
        if (!ctx.import_function)
        {
            failure = "import_unavailable";
            return false;
        }
        if (bytes.empty() || bytes.size() > 128 * 1024 * 1024)
        {
            failure = "import_bytes_invalid";
            return false;
        }
        meccha_sdk::RuntimePaintableComponent_ImportChannelFromBytes params{};
        params.Channel = static_cast<meccha_sdk::EPaintChannel>(channel);
        params.Data.Data = const_cast<std::uint8_t*>(bytes.data());
        params.Data.Num = static_cast<std::int32_t>(bytes.size());
        params.Data.Max = static_cast<std::int32_t>(bytes.size());
        std::string process_failure{};
        if (!process_event(ctx.component, ctx.import_function, reinterpret_cast<std::uint8_t*>(&params), process_failure))
        {
            failure = "import_process_event_failed:" + process_failure;
            return false;
        }
        if (!params.ReturnValue)
        {
            failure = "import_return_false";
            return false;
        }
        return true;
    }

    auto sdk_get_render_target(const SdkContext& ctx, int channel) -> std::uintptr_t
    {
        if (!ctx.get_render_target_function)
        {
            return 0;
        }
        meccha_sdk::RuntimePaintableComponent_GetRenderTarget params{};
        params.Channel = static_cast<meccha_sdk::EPaintChannel>(channel);
        std::string failure{};
        if (!process_event(ctx.component, ctx.get_render_target_function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return 0;
        }
        return reinterpret_cast<std::uintptr_t>(params.ReturnValue);
    }

    auto sdk_channel_metadata(const char* prefix, const ChannelBuffer& channel) -> std::string
    {
        std::string name(prefix);
        return ",\"" + name + "_ok\":" + json_bool(channel.ok) +
               ",\"" + name + "_channel\":" + std::to_string(channel.channel) +
               ",\"" + name + "_width\":" + std::to_string(channel.width) +
               ",\"" + name + "_height\":" + std::to_string(channel.height) +
               ",\"" + name + "_bytes_per_pixel\":" + std::to_string(channel.bytes_per_pixel) +
               ",\"" + name + "_bytes\":" + std::to_string(channel.bytes.size()) +
               ",\"" + name + "_hash\":\"" + std::to_string(channel.hash) + "\"" +
               ",\"" + name + "_first_bytes\":\"" + first_bytes_hex(channel.bytes) + "\"" +
               ",\"" + name + "_failure\":\"" + json_escape(channel.failure) + "\"";
    }

    struct ChannelRgbStats
    {
        bool ok{false};
        double min{0.0};
        double max{0.0};
        double avg{0.0};
    };

    auto sdk_channel_rgb_stats(const ChannelBuffer& channel) -> ChannelRgbStats
    {
        ChannelRgbStats stats{};
        if (!channel.ok || channel.bytes.empty())
        {
            return stats;
        }
        const int bpp = std::max(1, channel.bytes_per_pixel);
        const std::size_t step = static_cast<std::size_t>(bpp);
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t offset = 0; offset < channel.bytes.size(); offset += step)
        {
            if (bpp >= 3 && offset + 2 < channel.bytes.size())
            {
                const double r = static_cast<double>(channel.bytes[offset + 0]) / 255.0;
                const double g = static_cast<double>(channel.bytes[offset + 1]) / 255.0;
                const double b = static_cast<double>(channel.bytes[offset + 2]) / 255.0;
                const double values[3]{r, g, b};
                for (double value : values)
                {
                    if (!stats.ok)
                    {
                        stats.min = value;
                        stats.max = value;
                        stats.ok = true;
                    }
                    stats.min = std::min(stats.min, value);
                    stats.max = std::max(stats.max, value);
                    sum += value;
                    ++count;
                }
            }
            else
            {
                const double value = static_cast<double>(channel.bytes[offset]) / 255.0;
                if (!stats.ok)
                {
                    stats.min = value;
                    stats.max = value;
                    stats.ok = true;
                }
                stats.min = std::min(stats.min, value);
                stats.max = std::max(stats.max, value);
                sum += value;
                ++count;
            }
        }
        stats.avg = count > 0 ? sum / static_cast<double>(count) : 0.0;
        return stats;
    }

    auto sdk_channel_rgb_stats_metadata(const char* prefix, const ChannelBuffer& channel) -> std::string
    {
        const auto stats = sdk_channel_rgb_stats(channel);
        std::string name(prefix);
        return ",\"" + name + "_rgb_stats_ok\":" + json_bool(stats.ok) +
               ",\"" + name + "_rgb_min\":" + std::to_string(stats.min) +
               ",\"" + name + "_rgb_avg\":" + std::to_string(stats.avg) +
               ",\"" + name + "_rgb_max\":" + std::to_string(stats.max);
    }

    struct SdkReplicatedStats
    {
        int requested{0};
        int server_sent{0};
        int server_failed{0};
        int batch_calls{0};
        int single_calls{0};
        int client_mirror_sent{0};
        int client_mirror_failed{0};
        std::string server_rpc{"<none>"};
        std::string client_mirror_rpc{"<none>"};
        std::string first_failure{};
    };

    auto sdk_has_replicated_api(const SdkContext& ctx) -> bool
    {
        return ctx.server_paint_batch_function != 0;
    }

    auto sdk_copy_current_brush(const SdkContext& ctx, double radius) -> meccha_sdk::FRuntimeBrushSettings
    {
        meccha_sdk::FRuntimeBrushSettings brush{};
        safe_copy(&brush,
                  reinterpret_cast<void*>(ctx.component + meccha_sdk::FieldOffsets::RuntimePaintable_CurrentBrushSettings),
                  sizeof(brush));
        brush.Radius = static_cast<float>(std::max(0.001, std::min(0.12, radius)));
        brush.Hardness = 1.0f;
        brush.Opacity = 1.0f;
        brush.Spacing = std::min(brush.Spacing > 0.0f ? brush.Spacing : 0.25f, 0.25f);
        brush.Falloff = meccha_sdk::EBrushFalloff::Spherical;
        brush.BlendMode = meccha_sdk::EPaintBlendMode::Normal;
        brush.Rotation = 0.0f;
        return brush;
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
                          meccha_sdk::EPaintChannelApplyMode apply_mode) -> meccha_sdk::FPaintChannelData
    {
        meccha_sdk::FPaintChannelData data{};
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
                         const meccha_sdk::FPaintChannelData& channel,
                         const meccha_sdk::FRuntimeBrushSettings& brush,
                         meccha_sdk::EPaintChannel target_channel,
                         const meccha_sdk::FVector& world_position) -> meccha_sdk::FPaintStroke
    {
        meccha_sdk::FPaintStroke stroke{};
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
                            const meccha_sdk::FPaintChannelData& channel,
                            const meccha_sdk::FRuntimeBrushSettings& brush,
                            meccha_sdk::EPaintChannel target_channel) -> meccha_sdk::FPaintStroke
    {
        auto stroke = sdk_make_stroke(u, v, channel, brush, target_channel, {});
        stroke.bHasWorldPosition = false;
        stroke.WorldPosition = {};
        return stroke;
    }

    auto sdk_call_server_paint_batch(const SdkContext& ctx,
                                     const std::vector<meccha_sdk::FPaintStroke>& strokes,
                                     std::size_t offset,
                                     std::size_t count,
                                     std::string& failure) -> bool
    {
        if (!ctx.server_paint_batch_function || count == 0)
        {
            failure = "server_paint_batch_unavailable";
            return false;
        }
        if (!live_uobject(ctx.component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        meccha_sdk::RuntimePaintableComponent_ServerPaintBatch params{};
        params.Batch.Strokes.Data = const_cast<meccha_sdk::FPaintStroke*>(strokes.data() + offset);
        params.Batch.Strokes.Num = static_cast<std::int32_t>(count);
        params.Batch.Strokes.Max = static_cast<std::int32_t>(count);
        if (!process_event(ctx.component, ctx.server_paint_batch_function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        return true;
    }

    auto sdk_call_client_mirror(const SdkContext& ctx,
                                const meccha_sdk::FPaintStroke& stroke,
                                std::string& failure) -> bool
    {
        if (!ctx.paint_at_uv_with_brush_function)
        {
            failure = "PaintAtUVWithBrush_unavailable";
            return false;
        }
        if (!live_uobject(ctx.component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        meccha_sdk::RuntimePaintableComponent_PaintAtUVWithBrush params{};
        params.Uv = stroke.Uv;
        params.ChannelData = stroke.ChannelData;
        params.BrushSettings = stroke.BrushSettings;
        params.Channel = stroke.TargetChannel;
        if (!process_event(ctx.component, ctx.paint_at_uv_with_brush_function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        return true;
    }

    auto sdk_call_relay_batch_rpc(const SdkContext& ctx,
                                  std::uintptr_t function,
                                  const std::vector<meccha_sdk::FPaintStroke>& strokes,
                                  std::size_t offset,
                                  std::size_t count,
                                  std::string& failure) -> bool
    {
        if (!function || count == 0)
        {
            failure = "relay_batch_rpc_unavailable";
            return false;
        }
        if (!live_uobject(ctx.relay_component) || !live_uobject(ctx.component))
        {
            failure = "relay_or_paint_component_unavailable";
            return false;
        }
        meccha_sdk::RuntimePaintRelayComponent_RelayStrokeBatchToServer params{};
        params.PaintComponent = reinterpret_cast<void*>(ctx.component);
        params.Batch.Strokes.Data = const_cast<meccha_sdk::FPaintStroke*>(strokes.data() + offset);
        params.Batch.Strokes.Num = static_cast<std::int32_t>(count);
        params.Batch.Strokes.Max = static_cast<std::int32_t>(count);
        if (!process_event(ctx.relay_component, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        return true;
    }

    auto sdk_call_relay_single_rpc(const SdkContext& ctx,
                                   std::uintptr_t function,
                                   const meccha_sdk::FPaintStroke& stroke,
                                   std::string& failure) -> bool
    {
        if (!function)
        {
            failure = "relay_single_rpc_unavailable";
            return false;
        }
        if (!live_uobject(ctx.relay_component) || !live_uobject(ctx.component))
        {
            failure = "relay_or_paint_component_unavailable";
            return false;
        }
        meccha_sdk::RuntimePaintRelayComponent_RelayPaintToServer params{};
        params.PaintComponent = reinterpret_cast<void*>(ctx.component);
        params.Stroke = stroke;
        if (!process_event(ctx.relay_component, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        return true;
    }

    auto sdk_call_relay_texture_sync_rpc(const SdkContext& ctx,
                                         std::uintptr_t function,
                                         const char* unavailable_stage,
                                         std::string& failure) -> bool
    {
        if (!function)
        {
            failure = unavailable_stage;
            return false;
        }
        if (!live_uobject(ctx.relay_component) || !live_uobject(ctx.component))
        {
            failure = "relay_or_paint_component_unavailable";
            return false;
        }
        meccha_sdk::RuntimePaintRelayComponent_RelayTextureSyncToServer params{};
        params.PaintComponent = reinterpret_cast<void*>(ctx.component);
        if (!process_event(ctx.relay_component, function, reinterpret_cast<std::uint8_t*>(&params), failure))
        {
            return false;
        }
        return true;
    }

    auto sdk_call_component_no_param_rpc(const SdkContext& ctx,
                                         std::uintptr_t function,
                                         const char* unavailable_stage,
                                         std::string& failure) -> bool
    {
        if (!function)
        {
            failure = unavailable_stage;
            return false;
        }
        if (!live_uobject(ctx.component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        std::uint8_t params[1]{};
        if (!process_event(ctx.component, function, params, failure))
        {
            return false;
        }
        return true;
    }

    template <typename ProgressCallback>
    auto sdk_dispatch_replicated_strokes_with_progress(const SdkContext& ctx,
                                                       const std::vector<meccha_sdk::FPaintStroke>& strokes,
                                                       SdkReplicatedStats& stats,
                                                       ProgressCallback&& progress_callback,
                                                       int batch_limit_override = 0,
                                                       int sleep_ms = 16) -> bool
    {
        stats.requested = static_cast<int>(strokes.size());
        if (strokes.empty())
        {
            return true;
        }

        const int raw_limit = safe_read<int>(ctx.component + meccha_sdk::FieldOffsets::RuntimePaintable_MaxReplicatedPaintStrokesPerTick, 24);
        const int effective_limit = batch_limit_override > 0 ? batch_limit_override : (raw_limit > 0 ? raw_limit : 24);
        const auto batch_limit = static_cast<std::size_t>(std::max(1, std::min(effective_limit, 256)));
        for (std::size_t offset = 0; offset < strokes.size(); offset += batch_limit)
        {
            const auto count = std::min(batch_limit, strokes.size() - offset);
            std::string failure{};
            if (!sdk_call_server_paint_batch(ctx, strokes, offset, count, failure))
            {
                stats.server_failed += static_cast<int>(strokes.size() - offset);
                if (stats.first_failure.empty())
                {
                    stats.first_failure = failure.empty() ? "server_paint_batch_failed" : failure;
                }
                return false;
            }
            stats.server_sent += static_cast<int>(count);
            ++stats.batch_calls;
            stats.server_rpc = "ServerPaintBatch";
            if (!progress_callback(stats, stats.server_sent, static_cast<int>(strokes.size())))
            {
                stats.server_failed += std::max(0, static_cast<int>(strokes.size()) - stats.server_sent);
                if (stats.first_failure.empty())
                {
                    stats.first_failure = "server_stream_timeout_before_bridge_timeout";
                }
                return false;
            }
            if (sleep_ms > 0)
            {
                Sleep(static_cast<DWORD>(sleep_ms));
            }
        }
        return stats.server_sent == static_cast<int>(strokes.size());
    }

    auto sdk_dispatch_replicated_strokes(const SdkContext& ctx,
                                         const std::vector<meccha_sdk::FPaintStroke>& strokes,
                                         SdkReplicatedStats& stats) -> bool
    {
        return sdk_dispatch_replicated_strokes_with_progress(ctx, strokes, stats, [](const SdkReplicatedStats&, int, int) {
            return true;
        });
    }

    auto sdk_apply_local_echo_strokes(const SdkContext& ctx,
                                      const std::vector<meccha_sdk::FPaintStroke>& strokes,
                                      SdkReplicatedStats& stats) -> bool
    {
        if (strokes.empty())
        {
            return true;
        }
        for (std::size_t i = 0; i < strokes.size(); ++i)
        {
            std::string failure{};
            if (sdk_call_client_mirror(ctx, strokes[i], failure))
            {
                ++stats.client_mirror_sent;
                stats.client_mirror_rpc = "PaintAtUVWithBrush";
            }
            else
            {
                ++stats.client_mirror_failed;
                if (stats.first_failure.empty())
                {
                    stats.first_failure = failure.empty() ? "PaintAtUVWithBrush_failed" : failure;
                }
            }
            if ((i % 64) == 63)
            {
                Sleep(1);
            }
        }
        return stats.client_mirror_sent > 0;
    }

    auto sdk_stats_metadata(const char* prefix, const SdkReplicatedStats& stats) -> std::string
    {
        std::string name(prefix);
        return ",\"" + name + "_requested\":" + std::to_string(stats.requested) +
               ",\"" + name + "_server_sent\":" + std::to_string(stats.server_sent) +
               ",\"" + name + "_server_failed\":" + std::to_string(stats.server_failed) +
               ",\"" + name + "_batch_calls\":" + std::to_string(stats.batch_calls) +
               ",\"" + name + "_single_calls\":" + std::to_string(stats.single_calls) +
               ",\"" + name + "_client_mirror_sent\":" + std::to_string(stats.client_mirror_sent) +
               ",\"" + name + "_client_mirror_failed\":" + std::to_string(stats.client_mirror_failed) +
               ",\"" + name + "_server_rpc\":\"" + json_escape(stats.server_rpc) + "\"" +
               ",\"" + name + "_client_mirror_rpc\":\"" + json_escape(stats.client_mirror_rpc) + "\"" +
               ",\"" + name + "_first_failure\":\"" + json_escape(stats.first_failure) + "\"";
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

    auto sdk_front_color_stats(const std::vector<meccha_sdk::FPaintStroke>& strokes) -> SdkFrontColorStats
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

    auto sdk_read_rotator(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, meccha_sdk::FRotator& out) -> bool
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

    auto sdk_read_return_vector3_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params, meccha_sdk::FVector& value) -> bool
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

    auto sdk_read_return_rotator_param(Reflection& ref, std::uintptr_t function, std::uint8_t* params, meccha_sdk::FRotator& value) -> bool
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

    auto sdk_call_no_params_return_vector3(Reflection& ref, std::uintptr_t object, const char* function_name, meccha_sdk::FVector& value) -> bool
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

    auto sdk_call_no_params_return_rotator(Reflection& ref, std::uintptr_t object, const char* function_name, meccha_sdk::FRotator& value) -> bool
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

    auto sdk_write_transform(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const meccha_sdk::FVector& location) -> bool
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
            meccha_sdk::FVector scale{};
            scale.X = 1.0;
            scale.Y = 1.0;
            scale.Z = 1.0;
            wrote = sdk_write_vector3(ref, p, base, scale) || wrote;
        }
        return wrote;
    }

    auto sdk_write_rotator(Reflection& ref, std::uintptr_t prop, std::uint8_t* container, const meccha_sdk::FVector& direction) -> bool
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

    auto sdk_make_rotator(const meccha_sdk::FVector& direction) -> meccha_sdk::FRotator
    {
        const auto horizontal = std::sqrt(direction.X * direction.X + direction.Y * direction.Y);
        meccha_sdk::FRotator rot{};
        rot.Pitch = std::atan2(direction.Z, std::max(0.000001, horizontal)) * 180.0 / 3.14159265358979323846;
        rot.Yaw = std::atan2(direction.Y, direction.X) * 180.0 / 3.14159265358979323846;
        rot.Roll = 0.0;
        return rot;
    }

    auto sdk_rotator_forward(const meccha_sdk::FRotator& rotator) -> meccha_sdk::FVector
    {
        const auto pitch = rotator.Pitch * 3.14159265358979323846 / 180.0;
        const auto yaw = rotator.Yaw * 3.14159265358979323846 / 180.0;
        const auto cp = std::cos(pitch);
        return sdk_vec_normalize({cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch)});
    }

    auto sdk_make_transform(const meccha_sdk::FVector& location) -> meccha_sdk::FTransform
    {
        meccha_sdk::FTransform transform{};
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
        if (params_size != static_cast<int>(sizeof(meccha_sdk::KismetRenderingLibrary_CreateRenderTarget2D)))
        {
            failure = "create_render_target_typed_params_size_mismatch";
            return 0;
        }
        meccha_sdk::KismetRenderingLibrary_CreateRenderTarget2D params{};
        params.WorldContextObject = reinterpret_cast<void*>(ctx.pawn);
        params.Width = width;
        params.Height = height;
        params.Format = meccha_sdk::ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
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
                                    const meccha_sdk::FVector& location,
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
        if (params_size != static_cast<int>(sizeof(meccha_sdk::GameplayStatics_BeginDeferredActorSpawnFromClass)))
        {
            failure = "begin_deferred_spawn_typed_params_size_mismatch";
            return 0;
        }
        const auto transform = sdk_make_transform(location);
        meccha_sdk::GameplayStatics_BeginDeferredActorSpawnFromClass params{};
        params.WorldContextObject = reinterpret_cast<void*>(ctx.pawn);
        params.ActorClass = reinterpret_cast<void*>(actor_class);
        params.SpawnTransform = transform;
        params.CollisionHandlingOverride = meccha_sdk::ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        params.Owner = reinterpret_cast<void*>(ctx.pawn);
        params.TransformScaleMethod = meccha_sdk::ESpawnActorScaleMethod::SelectDefaultAtRuntime;
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
        if (finish && finish_caller && finish_size == static_cast<int>(sizeof(meccha_sdk::GameplayStatics_FinishSpawningActor)))
        {
            meccha_sdk::GameplayStatics_FinishSpawningActor finish_params{};
            finish_params.Actor = reinterpret_cast<void*>(actor);
            finish_params.SpawnTransform = transform;
            finish_params.TransformScaleMethod = meccha_sdk::ESpawnActorScaleMethod::SelectDefaultAtRuntime;
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
                                         const meccha_sdk::FVector& location,
                                         const meccha_sdk::FVector& direction) -> bool
    {
        bool ok = false;
        if (const auto function = ref.find_function(actor, "K2_SetActorLocation"))
        {
            const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
            if (params_size == static_cast<int>(sizeof(meccha_sdk::Actor_K2_SetActorLocation)))
            {
                meccha_sdk::Actor_K2_SetActorLocation params{};
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
            if (params_size == static_cast<int>(sizeof(meccha_sdk::Actor_K2_SetActorRotation)))
            {
                meccha_sdk::Actor_K2_SetActorRotation params{};
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
                                     const meccha_sdk::FVector& world,
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
        if (params_size != static_cast<int>(sizeof(meccha_sdk::KismetRenderingLibrary_ReadRenderTargetPixel)))
        {
            return false;
        }
        meccha_sdk::KismetRenderingLibrary_ReadRenderTargetPixel params{};
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
                std::vector<meccha_sdk::FColor> raw(static_cast<std::size_t>(expected));
                if (safe_copy(raw.data(), reinterpret_cast<void*>(data), raw.size() * sizeof(meccha_sdk::FColor)))
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
                std::vector<meccha_sdk::FLinearColor> raw(static_cast<std::size_t>(expected));
                if (safe_copy(raw.data(), reinterpret_cast<void*>(data), raw.size() * sizeof(meccha_sdk::FLinearColor)))
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
        const char* function_names[]{"ReadRenderTargetRaw", "ReadRenderTarget"};
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
                    if (offset < 0 || offset + static_cast<int>(sizeof(meccha_sdk::TArray<std::uint8_t>)) > params_size)
                    {
                        continue;
                    }
                    const auto array = *reinterpret_cast<meccha_sdk::TArray<std::uint8_t>*>(params.data() + offset);
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
                    out.insert(out.end(), std::make_move_iterator(images.begin()), std::make_move_iterator(images.end()));
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
            *reinterpret_cast<std::uintptr_t*>(capture_component + meccha_sdk::FieldOffsets::SceneCaptureComponent2D_TextureTarget) = render_target;
            *reinterpret_cast<std::uint8_t*>(capture_component + meccha_sdk::FieldOffsets::SceneCaptureComponent_CaptureSource) =
                static_cast<std::uint8_t>(meccha_sdk::ESceneCaptureSource::FinalColorLDR);
            auto* capture_flags = reinterpret_cast<std::uint8_t*>(capture_component + meccha_sdk::FieldOffsets::SceneCaptureComponent_CaptureFlags);
            *capture_flags = static_cast<std::uint8_t>(*capture_flags & ~0x03);
            *reinterpret_cast<bool*>(capture_component + meccha_sdk::FieldOffsets::SceneCaptureComponent_bAlwaysPersistRenderingState) = true;
            *reinterpret_cast<std::uint8_t*>(capture_component + meccha_sdk::FieldOffsets::SceneCaptureComponent2D_ProjectionType) =
                static_cast<std::uint8_t>(meccha_sdk::ECameraProjectionMode::Perspective);
            const auto fov = std::isfinite(fov_degrees) ? std::max(10.0, std::min(150.0, fov_degrees)) : 90.0;
            *reinterpret_cast<float*>(capture_component + meccha_sdk::FieldOffsets::SceneCaptureComponent2D_FOVAngle) = static_cast<float>(fov);
            return *reinterpret_cast<std::uintptr_t*>(capture_component + meccha_sdk::FieldOffsets::SceneCaptureComponent2D_TextureTarget) == render_target;
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
            int capture_width = std::max(1, viewport_width);
            int capture_height = std::max(1, viewport_height);
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
                ctx.controller + meccha_sdk::FieldOffsets::PlayerController_PlayerCameraManager,
                0);
            if (live_uobject(field_camera_manager))
            {
                out.camera_manager = field_camera_manager;
                camera_manager_source = "field:APlayerController.PlayerCameraManager@0x360";
            }
        }
        if (live_uobject(out.camera_manager))
        {
            meccha_sdk::FVector camera_location{};
            if (sdk_call_no_params_return_vector3(ref, out.camera_manager, "GetCameraLocation", camera_location))
            {
                capture_location = camera_location;
                out.camera_location_used = true;
                out.camera_location_source = "player_camera_manager";
            }
            meccha_sdk::FRotator camera_rotation{};
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
            const auto control_rotation = safe_read<meccha_sdk::FRotator>(
                ctx.controller + meccha_sdk::FieldOffsets::Controller_ControlRotation,
                meccha_sdk::FRotator{});
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
        out.capture_component = safe_read<std::uintptr_t>(out.capture_actor + meccha_sdk::FieldOffsets::SceneCapture2D_CaptureComponent2D, 0);
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
        Sleep(50);

        struct ProjectedFrontSample
        {
            FrontSample surface{};
            int x{0};
            int y{0};
            Color pixel_color{};
            bool has_pixel{false};
        };
        std::vector<ProjectedFrontSample> projected{};
        projected.reserve(native_front.samples.size());
        double sum = 0.0;
        int channels = 0;
        bool initialized = false;
        double raw_sum = 0.0;
        int raw_channels = 0;
        bool raw_initialized = false;
        double resolved_delta_sum = 0.0;
        double resolved_delta_max = 0.0;
        int resolved_delta_samples = 0;
        for (const auto& surface : native_front.samples)
        {
            ++out.project_attempts;
            const double original_sx = clamp01(surface.screen_nx) * static_cast<double>(viewport_width);
            const double original_sy = clamp01(surface.screen_ny) * static_cast<double>(viewport_height);
            double sx = original_sx;
            double sy = original_sy;
            if (surface.has_world_position)
            {
                double projected_x = 0.0;
                double projected_y = 0.0;
                if (sdk_project_world_to_screen(ref, ctx, surface.world_position, projected_x, projected_y))
                {
                    sx = projected_x;
                    sy = projected_y;
                    const auto dx = sx - original_sx;
                    const auto dy = sy - original_sy;
                    const auto delta = std::sqrt(dx * dx + dy * dy);
                    out.project_delta_sum_px += delta;
                    out.project_delta_max_px = std::max(out.project_delta_max_px, delta);
                }
                else
                {
                    ++out.project_failed;
                    continue;
                }
            }
            const bool outside = sx < 0.0 || sy < 0.0 ||
                                 sx >= static_cast<double>(viewport_width) ||
                                 sy >= static_cast<double>(viewport_height);
            if (outside)
            {
                ++out.project_out_of_view;
                continue;
            }
            ++out.project_success;
            const auto px = std::max(0, std::min(out.width - 1, static_cast<int>(std::round(sx * capture_scale_x))));
            const auto py = std::max(0, std::min(out.height - 1, static_cast<int>(std::round(sy * capture_scale_y))));
            auto projected_surface = surface;
            projected_surface.capture_nx = clamp01(sx / static_cast<double>(std::max(1, viewport_width)));
            projected_surface.capture_ny = clamp01(sy / static_cast<double>(std::max(1, viewport_height)));
            projected.push_back(ProjectedFrontSample{projected_surface, px, py, {}, false});
        }
        if (projected.empty())
        {
            out.failure = "front_capture_project_world_to_screen_failed";
            sdk_call_no_params(ref, out.capture_actor, "K2_DestroyActor");
            return out;
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
        const SdkBulkColorTransform color_candidates[]{
            SdkBulkColorTransform::Identity,
            SdkBulkColorTransform::SwapRedBlue,
            SdkBulkColorTransform::SrgbToLinear,
            SdkBulkColorTransform::LinearToSrgb,
            SdkBulkColorTransform::SwapRedBlueSrgbToLinear,
            SdkBulkColorTransform::SwapRedBlueLinearToSrgb};
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

        const auto& bulk = bulk_candidates[static_cast<std::size_t>(best_candidate)];
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

        out.samples.reserve(projected.size());
        for (const auto& projected_sample : projected)
        {
            const int bx = best_flip_x ? (out.width - 1 - projected_sample.x) : projected_sample.x;
            const int by = best_flip_y ? (out.height - 1 - projected_sample.y) : projected_sample.y;
            const auto pixel_index = static_cast<std::size_t>(by) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(bx);
            if (pixel_index >= bulk.pixels.size())
            {
                ++out.missing_color;
                continue;
            }
            const auto raw_color = sdk_apply_bulk_color_transform(bulk.pixels[pixel_index], best_transform);
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
            const auto floor_like = projected_sample.surface.floor_like;
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
            sample.radius = std::max(0.0015, std::min(0.0035, 2.5 / static_cast<double>(std::max(out.width, out.height))));
            sample.floor_like = floor_like;
            sample.atlas_priority = 11;
            sample.atlas_radius = 3;
            sample.atlas_weight = 72.0;
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
               ",\"front_capture_project_attempts\":" + std::to_string(capture.project_attempts) +
               ",\"front_capture_project_success\":" + std::to_string(capture.project_success) +
               ",\"front_capture_project_failed\":" + std::to_string(capture.project_failed) +
               ",\"front_capture_project_out_of_view\":" + std::to_string(capture.project_out_of_view) +
               ",\"front_capture_project_delta_avg_px\":" + std::to_string(capture.project_success > 0 ? capture.project_delta_sum_px / static_cast<double>(capture.project_success) : 0.0) +
               ",\"front_capture_project_delta_max_px\":" + std::to_string(capture.project_delta_max_px) +
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

    struct SdkAtlasSideBackResult
    {
        std::vector<FrontSample> samples{};
        int attempts{0};
        int success{0};
        int owner_hits{0};
        int uv_hits{0};
        int side_hits{0};
        int back_hits{0};
        int duplicate_texels{0};
        int nearest_sources{0};
        int front_reuse_samples{0};
        bool front_reuse_used{false};
        int attempt_budget{0};
        double time_budget_ms{0.0};
        double elapsed_ms{0.0};
        bool timed_out{false};
        bool attempt_budget_exhausted{false};
        int splat_radius{0};
        std::string backend{"not_run"};
        std::string failure{"not_run"};
    };

    struct SdkTextureAtlasStats
    {
        int width{0};
        int height{0};
        int direct_texels{0};
        int filled_by_extension{0};
        int filled_by_full_nearest{0};
        int preserved_original{0};
        int source_samples{0};
        int worker_threads{1};
        int gap_fill_radius{0};
    };

    struct SdkTextureAtlas
    {
        bool ok{false};
        std::string failure{"atlas_not_built"};
        std::vector<std::uint8_t> albedo{};
        std::vector<std::uint8_t> metallic{};
        std::vector<std::uint8_t> roughness{};
        std::vector<std::uint8_t> painted_mask{};
        std::uint64_t hash{1469598103934665603ULL};
        std::uint64_t metallic_hash{1469598103934665603ULL};
        std::uint64_t roughness_hash{1469598103934665603ULL};
        SdkTextureAtlasStats stats{};
    };

    struct SdkAtlasStrokePlan
    {
        std::vector<meccha_sdk::FPaintStroke> strokes{};
        int stroke_cap{600000};
        int target_strokes{30000};
        int source_texels{0};
        int merged_8{0};
        int merged_4{0};
        int merged_2{0};
        int singles{0};
        double merge_error_max{2.0 / 255.0};
        bool cap_exceeded{false};
        std::string failure{"not_run"};
    };

    auto sdk_byte_from_unit(double value) -> std::uint8_t
    {
        return static_cast<std::uint8_t>(std::round(clamp01(value) * 255.0));
    }

    auto sdk_texel_key(double u, double v, int width, int height) -> std::uint64_t
    {
        const auto x = static_cast<std::uint64_t>(std::max(0, std::min(std::max(0, width - 1), static_cast<int>(clamp01(u) * static_cast<double>(std::max(1, width))))));
        const auto y = static_cast<std::uint64_t>(std::max(0, std::min(std::max(0, height - 1), static_cast<int>(clamp01(v) * static_cast<double>(std::max(1, height))))));
        return (y << 32) | x;
    }

    auto sdk_collect_atlas_side_back_samples(Reflection& ref,
                                             const SdkContext& ctx,
                                             const SdkNativeFrontSampleResult& native_front,
                                             const std::vector<FrontSample>& direct_samples,
                                             int width,
                                             int height,
                                             bool side_only = false) -> SdkAtlasSideBackResult
    {
        SdkAtlasSideBackResult out{};
        out.failure = "side_back_unavailable";
        if (direct_samples.empty())
        {
            out.failure = "side_back_no_direct_samples";
            return out;
        }
        auto query = native_front.query ? native_front.query : sdk_find_screen_space_brush_query(ref, ctx);
        if (!query || !sdk_configure_screen_space_brush_query(ref, query, ctx.pawn, native_front.mesh))
        {
            out.failure = "side_back_query_unavailable";
            return out;
        }
        out.backend = side_only ? "screen_space_brush_query_static_hybrid_front_side_probe"
                                : "screen_space_brush_query_virtual_views_bounded";
        out.time_budget_ms = side_only ? 16000.0 : 36000.0;
        out.attempt_budget = side_only ? 70000 : 140000;
        const auto side_back_started = std::chrono::steady_clock::now();
        auto side_back_elapsed_ms = [&]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - side_back_started).count();
        };
        auto side_back_should_stop = [&]() {
            out.elapsed_ms = side_back_elapsed_ms();
            if (out.elapsed_ms >= out.time_budget_ms)
            {
                out.timed_out = true;
                if (out.failure == "side_back_unavailable" || out.failure == "ok")
                {
                    out.failure = out.samples.empty() ? "side_back_time_budget_exhausted_no_samples" : "ok_time_budget_exhausted_partial";
                }
                return true;
            }
            if (out.attempts >= out.attempt_budget)
            {
                out.attempt_budget_exhausted = true;
                if (out.failure == "side_back_unavailable" || out.failure == "ok")
                {
                    out.failure = out.samples.empty() ? "side_back_attempt_budget_exhausted_no_samples" : "ok_attempt_budget_exhausted_partial";
                }
                return true;
            }
            return false;
        };

        std::unordered_set<std::uint64_t> unique_texels{};
        unique_texels.reserve(direct_samples.size() + 32768);
        meccha_sdk::FVector min_world = direct_samples.front().world_position;
        meccha_sdk::FVector max_world = direct_samples.front().world_position;
        meccha_sdk::FVector sum_world{};
        for (const auto& sample : direct_samples)
        {
            unique_texels.insert(sdk_texel_key(sample.u, sample.v, width, height));
            sum_world = sdk_vec_add(sum_world, sample.world_position);
            min_world.X = std::min(min_world.X, sample.world_position.X);
            min_world.Y = std::min(min_world.Y, sample.world_position.Y);
            min_world.Z = std::min(min_world.Z, sample.world_position.Z);
            max_world.X = std::max(max_world.X, sample.world_position.X);
            max_world.Y = std::max(max_world.Y, sample.world_position.Y);
            max_world.Z = std::max(max_world.Z, sample.world_position.Z);
        }
        const auto center = sdk_vec_mul(sum_world, 1.0 / static_cast<double>(std::max<std::size_t>(1, direct_samples.size())));
        const auto extent = sdk_vec_sub(max_world, min_world);
        const auto half_width = std::max(90.0, std::min(300.0, std::max(std::abs(extent.X), std::abs(extent.Y)) * 0.86));
        const auto half_height = std::max(120.0, std::min(360.0, std::abs(extent.Z) * 0.86));
        const auto ray_distance = std::max(260.0, std::min(820.0, std::max({std::abs(extent.X), std::abs(extent.Y), std::abs(extent.Z), 160.0}) * 2.8));
        auto base_outward = sdk_vec_sub(native_front.first_ray_location, center);
        base_outward.Z = 0.0;
        if (sdk_vec_len(base_outward) < 0.01)
        {
            base_outward = {1.0, 0.0, 0.0};
        }
        base_outward = sdk_vec_normalize(base_outward);
        auto frame_forward = sdk_vec_normalize(sdk_vec_sub(center, native_front.first_ray_location));
        if (sdk_vec_len(frame_forward) < 0.01)
        {
            frame_forward = sdk_vec_normalize(native_front.first_ray_direction);
        }
        auto frame_right = sdk_vec_normalize(sdk_vec_cross({0.0, 0.0, 1.0}, frame_forward));
        if (sdk_vec_len(frame_right) < 0.01)
        {
            frame_right = {1.0, 0.0, 0.0};
        }
        auto frame_up = sdk_vec_normalize(sdk_vec_cross(frame_forward, frame_right));
        if (sdk_vec_len(frame_up) < 0.01)
        {
            frame_up = {0.0, 0.0, 1.0};
        }
        auto rotate_z = [](const meccha_sdk::FVector& v, double degrees) {
            const auto radians = degrees * 3.14159265358979323846 / 180.0;
            const auto c = std::cos(radians);
            const auto s = std::sin(radians);
            return meccha_sdk::FVector{v.X * c - v.Y * s, v.X * s + v.Y * c, v.Z};
        };
        auto rotate_yaw_pitch = [&](const meccha_sdk::FVector& v, double yaw_degrees, double pitch_degrees) {
            auto yawed = sdk_vec_normalize(rotate_z(v, yaw_degrees));
            yawed.Z = 0.0;
            if (sdk_vec_len(yawed) < 0.01)
            {
                yawed = base_outward;
            }
            yawed = sdk_vec_normalize(yawed);
            const auto pitch = pitch_degrees * 3.14159265358979323846 / 180.0;
            const auto c = std::cos(pitch);
            const auto s = std::sin(pitch);
            return sdk_vec_normalize({yawed.X * c, yawed.Y * c, s});
        };
        auto nearest_direct = [&](const meccha_sdk::FVector& world) -> const FrontSample* {
            const FrontSample* best = nullptr;
            double best_score = 1000000000000.0;
            for (const auto& sample : direct_samples)
            {
                const auto delta = sdk_vec_sub(sample.world_position, world);
                const auto right_delta = sdk_vec_dot(delta, frame_right);
                const auto up_delta = sdk_vec_dot(delta, frame_up);
                const auto forward_delta = sdk_vec_dot(delta, frame_forward);
                const auto score = right_delta * right_delta +
                                   up_delta * up_delta * 1.35 +
                                   forward_delta * forward_delta * 0.16;
                if (score < best_score)
                {
                    best_score = score;
                    best = &sample;
                }
            }
            return best;
        };
        constexpr std::size_t centerline_bin_count = 96;
        std::vector<const FrontSample*> centerline_bins(centerline_bin_count, nullptr);
        std::vector<double> centerline_scores(centerline_bin_count, 1000000000000.0);
        double min_up = 1000000000000.0;
        double max_up = -1000000000000.0;
        for (const auto& sample : direct_samples)
        {
            const auto delta = sdk_vec_sub(sample.world_position, center);
            const auto up_delta = sdk_vec_dot(delta, frame_up);
            min_up = std::min(min_up, up_delta);
            max_up = std::max(max_up, up_delta);
        }
        const auto up_span = std::max(1.0, max_up - min_up);
        for (const auto& sample : direct_samples)
        {
            const auto delta = sdk_vec_sub(sample.world_position, center);
            const auto right_delta = sdk_vec_dot(delta, frame_right);
            const auto up_delta = sdk_vec_dot(delta, frame_up);
            const auto forward_delta = sdk_vec_dot(delta, frame_forward);
            const auto normalized_up = std::max(0.0, std::min(0.999999, (up_delta - min_up) / up_span));
            const auto bin = std::min(centerline_bin_count - 1, static_cast<std::size_t>(normalized_up * static_cast<double>(centerline_bin_count)));
            const auto score = std::abs(right_delta) + std::abs(forward_delta) * 0.04;
            if (score < centerline_scores[bin])
            {
                centerline_scores[bin] = score;
                centerline_bins[bin] = &sample;
            }
        }
        std::vector<const FrontSample*> centerline_targets{};
        centerline_targets.reserve(centerline_bin_count);
        for (const auto* sample : centerline_bins)
        {
            if (sample)
            {
                centerline_targets.push_back(sample);
            }
        }
        auto try_ray = [&](const meccha_sdk::FVector& origin, const meccha_sdk::FVector& ray_dir, bool back_view) {
            if (side_back_should_stop())
            {
                return;
            }
            ++out.attempts;
            const auto hit = sdk_query_brush_from_world_ray(ref, query, origin, ray_dir);
            if (!hit.params_ok || !hit.success)
            {
                if (out.failure == "side_back_unavailable")
                {
                    out.failure = hit.failure.empty() ? "side_back_query_no_hit" : hit.failure;
                }
                return;
            }
            ++out.success;
            const bool owner_hit = (hit.component && hit.component == native_front.mesh) ||
                                   sdk_object_is_or_belongs_to(ref, hit.actor, ctx.pawn) ||
                                   sdk_object_is_or_belongs_to(ref, hit.component, ctx.pawn);
            if (!owner_hit || !hit.has_uv || !std::isfinite(hit.u) || !std::isfinite(hit.v))
            {
                return;
            }
            ++out.owner_hits;
            ++out.uv_hits;
            const auto key = sdk_texel_key(hit.u, hit.v, width, height);
            if (!unique_texels.insert(key).second)
            {
                ++out.duplicate_texels;
                return;
            }
            const auto* nearest = nearest_direct(hit.world_position);
            if (!nearest)
            {
                return;
            }
            ++out.nearest_sources;
            FrontSample sample = *nearest;
            sample.u = clamp01(hit.u);
            sample.v = clamp01(hit.v);
            sample.world_position = hit.world_position;
            sample.normal = sdk_vec_normalize(hit.normal);
            sample.has_world_position = true;
            sample.radius = std::max(0.0015, nearest->radius);
            sample.floor_like = nearest->floor_like;
            sample.atlas_priority = sample.floor_like ? 8 : 7;
            sample.atlas_radius = 2;
            sample.atlas_weight = sample.floor_like ? 56.0 : 48.0;
            out.samples.push_back(sample);
            if (back_view)
            {
                ++out.back_hits;
            }
            else
            {
                ++out.side_hits;
            }
        };

        const std::vector<double> yaw_offsets = side_only
            ? std::vector<double>{-60.0, -45.0, -30.0, -15.0, 15.0, 30.0, 45.0, 60.0}
            : std::vector<double>{-165.0, -150.0, -135.0, -120.0, -105.0, -90.0, -75.0, -60.0, 60.0, 75.0, 90.0, 105.0, 120.0, 135.0, 150.0, 165.0, 180.0};
        const double pitch_offsets[]{-30.0, -15.0, 0.0, 15.0, 30.0};
        constexpr int grid_x = 32;
        constexpr int grid_y = 48;
        std::size_t virtual_view_index = 0;
        for (const auto yaw : yaw_offsets)
        {
            for (const auto pitch : pitch_offsets)
            {
                if (side_back_should_stop())
                {
                    break;
                }
                const auto outward = rotate_yaw_pitch(base_outward, yaw, pitch);
                const auto origin = sdk_vec_add(center, sdk_vec_mul(outward, ray_distance));
                const auto view_forward = sdk_vec_normalize(sdk_vec_sub(center, origin));
                auto view_right = sdk_vec_normalize(sdk_vec_cross({0.0, 0.0, 1.0}, view_forward));
                if (sdk_vec_len(view_right) < 0.01)
                {
                    view_right = {1.0, 0.0, 0.0};
                }
                auto view_up = sdk_vec_normalize(sdk_vec_cross(view_forward, view_right));
                if (sdk_vec_len(view_up) < 0.01)
                {
                    view_up = {0.0, 0.0, 1.0};
                }
                for (int y = 0; y < grid_y; ++y)
                {
                    for (int x = 0; x < grid_x; ++x)
                    {
                        if (side_back_should_stop())
                        {
                            break;
                        }
                        const auto lx = ((static_cast<double>(x) + 0.5) / static_cast<double>(grid_x) - 0.5) * 2.0;
                        const auto ly = ((static_cast<double>(y) + 0.5) / static_cast<double>(grid_y) - 0.5) * 2.0;
                        const auto target = sdk_vec_add(sdk_vec_add(center, sdk_vec_mul(view_right, lx * half_width)), sdk_vec_mul(view_up, ly * half_height));
                        try_ray(origin, sdk_vec_normalize(sdk_vec_sub(target, origin)), !side_only && std::abs(yaw) > 165.0);
                    }
                }
                constexpr int direct_target_rays_per_view = 1536;
                const auto seed_count = direct_samples.size();
                const auto stride = std::max<std::size_t>(1, seed_count / static_cast<std::size_t>(direct_target_rays_per_view));
                const auto view_offset = (virtual_view_index * 131u) % std::max<std::size_t>(1, seed_count);
                const auto target_rays = std::min<std::size_t>(static_cast<std::size_t>(direct_target_rays_per_view), seed_count);
                for (std::size_t i = 0; i < target_rays; ++i)
                {
                    const auto seed_index = (view_offset + i * stride) % seed_count;
                    const auto jitter_x = (static_cast<double>((i * 17u + virtual_view_index * 7u) % 11u) - 5.0) * 1.4;
                    const auto jitter_y = (static_cast<double>((i * 23u + virtual_view_index * 5u) % 13u) - 6.0) * 1.2;
                    const auto target = sdk_vec_add(sdk_vec_add(direct_samples[seed_index].world_position, sdk_vec_mul(view_right, jitter_x)),
                                                    sdk_vec_mul(view_up, jitter_y));
                    try_ray(origin, sdk_vec_normalize(sdk_vec_sub(target, origin)), !side_only && std::abs(yaw) > 165.0);
                    if (side_back_should_stop())
                    {
                        break;
                    }
                }
                ++virtual_view_index;
            }
        }
        if (!side_only)
        {
            const double centerline_yaw_offsets[]{-60.0, -45.0, -30.0, -15.0, 0.0, 15.0, 30.0, 45.0, 60.0};
            const double centerline_pitch_offsets[]{-45.0, -25.0, 0.0, 25.0, 45.0};
            const double centerline_jitter[][2]{{0.0, 0.0}, {4.0, 0.0}, {-4.0, 0.0}, {0.0, 5.0}, {0.0, -5.0}};
            for (const auto yaw : centerline_yaw_offsets)
            {
                for (const auto pitch : centerline_pitch_offsets)
                {
                    if (side_back_should_stop())
                    {
                        break;
                    }
                    const auto outward = rotate_yaw_pitch(base_outward, yaw, pitch);
                    const auto origin = sdk_vec_add(center, sdk_vec_mul(outward, ray_distance));
                    const auto view_forward = sdk_vec_normalize(sdk_vec_sub(center, origin));
                    auto view_right = sdk_vec_normalize(sdk_vec_cross({0.0, 0.0, 1.0}, view_forward));
                    if (sdk_vec_len(view_right) < 0.01)
                    {
                        view_right = frame_right;
                    }
                    auto view_up = sdk_vec_normalize(sdk_vec_cross(view_forward, view_right));
                    if (sdk_vec_len(view_up) < 0.01)
                    {
                        view_up = frame_up;
                    }
                    for (const auto* target_seed : centerline_targets)
                    {
                        for (const auto& jitter : centerline_jitter)
                        {
                            if (side_back_should_stop())
                            {
                                break;
                            }
                            const auto target = sdk_vec_add(sdk_vec_add(target_seed->world_position, sdk_vec_mul(view_right, jitter[0])),
                                                            sdk_vec_mul(view_up, jitter[1]));
                            try_ray(origin, sdk_vec_normalize(sdk_vec_sub(target, origin)), std::abs(yaw) > 40.0);
                        }
                    }
                }
            }
        }
        out.elapsed_ms = side_back_elapsed_ms();
        if (!out.samples.empty())
        {
            const auto pixels = static_cast<double>(std::max(1, width) * std::max(1, height));
            const auto density_radius = static_cast<int>(std::ceil(std::sqrt(pixels / static_cast<double>(out.samples.size())) * 0.18));
            out.splat_radius = side_only ? 1 : std::max(2, std::min(6, density_radius));
            for (auto& sample : out.samples)
            {
                sample.atlas_radius = side_only ? std::min(std::max(sample.atlas_radius, 0), out.splat_radius)
                                                : std::max(sample.atlas_radius, out.splat_radius);
                sample.atlas_weight = std::max(sample.atlas_weight, sample.floor_like ? 70.0 : 60.0);
            }
        }
        if (!out.timed_out && !out.attempt_budget_exhausted)
        {
            out.failure = out.samples.empty() ? out.failure : "ok";
        }
        return out;
    }

    auto sdk_build_front_uv_mirror_side_back_samples(const std::vector<FrontSample>& direct_samples,
                                                     int width,
                                                     int height) -> SdkAtlasSideBackResult
    {
        SdkAtlasSideBackResult out{};
        out.failure = "front_uv_mirror_fast";
        out.backend = "front_uv_mirror_fast";
        if (direct_samples.empty())
        {
            out.failure = "front_uv_mirror_no_direct_samples";
            return out;
        }

        std::unordered_set<std::uint64_t> unique_texels{};
        unique_texels.reserve(direct_samples.size() * 2U + 1024U);
        constexpr int uv_grid = 128;
        std::vector<std::uint8_t> occupied(static_cast<std::size_t>(uv_grid * uv_grid), 0);
        auto uv_cell_index = [&](double u, double v) -> int {
            const auto x = std::max(0, std::min(uv_grid - 1, static_cast<int>(clamp01(u) * static_cast<double>(uv_grid))));
            const auto y = std::max(0, std::min(uv_grid - 1, static_cast<int>(clamp01(v) * static_cast<double>(uv_grid))));
            return y * uv_grid + x;
        };
        for (const auto& sample : direct_samples)
        {
            unique_texels.insert(sdk_texel_key(sample.u, sample.v, width, height));
            occupied[static_cast<std::size_t>(uv_cell_index(sample.u, sample.v))] = 1;
        }

        struct UvIslandBounds
        {
            double min_u{1.0};
            double max_u{0.0};
            double min_v{1.0};
            double max_v{0.0};
            int cells{0};
        };
        std::vector<int> component_ids(static_cast<std::size_t>(uv_grid * uv_grid), -1);
        std::vector<UvIslandBounds> islands{};
        std::vector<int> stack{};
        stack.reserve(uv_grid * uv_grid);
        for (int cell = 0; cell < uv_grid * uv_grid; ++cell)
        {
            if (!occupied[static_cast<std::size_t>(cell)] || component_ids[static_cast<std::size_t>(cell)] >= 0)
            {
                continue;
            }
            const auto component_id = static_cast<int>(islands.size());
            islands.push_back({});
            stack.clear();
            stack.push_back(cell);
            component_ids[static_cast<std::size_t>(cell)] = component_id;
            while (!stack.empty())
            {
                const auto current = stack.back();
                stack.pop_back();
                const auto cx = current % uv_grid;
                const auto cy = current / uv_grid;
                auto& bounds = islands.back();
                bounds.min_u = std::min(bounds.min_u, static_cast<double>(cx) / static_cast<double>(uv_grid));
                bounds.max_u = std::max(bounds.max_u, static_cast<double>(cx + 1) / static_cast<double>(uv_grid));
                bounds.min_v = std::min(bounds.min_v, static_cast<double>(cy) / static_cast<double>(uv_grid));
                bounds.max_v = std::max(bounds.max_v, static_cast<double>(cy + 1) / static_cast<double>(uv_grid));
                ++bounds.cells;
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        if (dx == 0 && dy == 0)
                        {
                            continue;
                        }
                        const auto nx = cx + dx;
                        const auto ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= uv_grid || ny >= uv_grid)
                        {
                            continue;
                        }
                        const auto next = ny * uv_grid + nx;
                        if (!occupied[static_cast<std::size_t>(next)] || component_ids[static_cast<std::size_t>(next)] >= 0)
                        {
                            continue;
                        }
                        component_ids[static_cast<std::size_t>(next)] = component_id;
                        stack.push_back(next);
                    }
                }
            }
        }

        out.attempts = static_cast<int>(direct_samples.size());
        auto add_mirror_sample = [&](const FrontSample& source,
                                     double u,
                                     double v,
                                     int priority,
                                     double weight,
                                     const char* kind) {
            FrontSample sample = source;
            sample.u = clamp01(u);
            sample.v = clamp01(v);
            sample.atlas_priority = priority;
            sample.atlas_radius = std::max(1, source.atlas_radius);
            sample.atlas_weight = weight;
            const auto key = sdk_texel_key(sample.u, sample.v, width, height);
            if (!unique_texels.insert(key).second)
            {
                ++out.duplicate_texels;
                return;
            }
            out.samples.push_back(sample);
            ++out.success;
            ++out.owner_hits;
            ++out.uv_hits;
            ++out.back_hits;
            ++out.nearest_sources;
            (void)kind;
        };
        for (const auto& source : direct_samples)
        {
            const auto component_id = component_ids[static_cast<std::size_t>(uv_cell_index(source.u, source.v))];
            if (component_id >= 0 && component_id < static_cast<int>(islands.size()))
            {
                const auto& island = islands[static_cast<std::size_t>(component_id)];
                if (island.cells >= 2 && island.max_u > island.min_u && island.max_v > island.min_v)
                {
                    add_mirror_sample(source,
                                      island.min_u + island.max_u - source.u,
                                      source.v,
                                      source.floor_like ? 8 : 7,
                                      source.floor_like ? 48.0 : 42.0,
                                      "island_u");
                }
            }
            add_mirror_sample(source,
                              1.0 - source.u,
                              source.v,
                              source.floor_like ? 6 : 5,
                              source.floor_like ? 34.0 : 30.0,
                              "global_u");
            add_mirror_sample(source,
                              source.u,
                              1.0 - source.v,
                              source.floor_like ? 4 : 3,
                              source.floor_like ? 26.0 : 22.0,
                              "global_v");
            add_mirror_sample(source,
                              1.0 - source.u,
                              1.0 - source.v,
                              source.floor_like ? 4 : 3,
                              source.floor_like ? 22.0 : 18.0,
                              "global_uv");
        }
        out.failure = out.samples.empty() ? "front_uv_mirror_no_new_texels" : "ok_front_uv_mirror_fast";
        return out;
    }

    auto sdk_side_back_metadata(const SdkAtlasSideBackResult& side) -> std::string
    {
        return ",\"side_back_attempts\":" + std::to_string(side.attempts) +
               ",\"side_back_success\":" + std::to_string(side.success) +
               ",\"side_back_owner_hits\":" + std::to_string(side.owner_hits) +
               ",\"side_back_uv_hits\":" + std::to_string(side.uv_hits) +
               ",\"side_hits\":" + std::to_string(side.side_hits) +
               ",\"back_hits\":" + std::to_string(side.back_hits) +
               ",\"side_back_duplicate_texels\":" + std::to_string(side.duplicate_texels) +
               ",\"side_back_nearest_sources\":" + std::to_string(side.nearest_sources) +
               ",\"side_back_front_reuse_used\":" + json_bool(side.front_reuse_used) +
               ",\"side_back_front_reuse_samples\":" + std::to_string(side.front_reuse_samples) +
               ",\"side_back_front_reuse_disabled_reason\":\"front UV mirror/reuse is not UV island aware and can move colors between body parts\"" +
               ",\"side_back_attempt_budget\":" + std::to_string(side.attempt_budget) +
               ",\"side_back_time_budget_ms\":" + std::to_string(side.time_budget_ms) +
               ",\"side_back_elapsed_ms\":" + std::to_string(side.elapsed_ms) +
               ",\"side_back_timed_out\":" + json_bool(side.timed_out) +
               ",\"side_back_attempt_budget_exhausted\":" + json_bool(side.attempt_budget_exhausted) +
               ",\"side_back_splat_radius\":" + std::to_string(side.splat_radius) +
               ",\"side_back_backend\":\"" + json_escape(side.backend) + "\"" +
               ",\"side_back_cached_triangle_candidate\":false" +
               ",\"side_back_cached_triangle_blocker\":\"HitTestAtScreenPosition is current-view screen-space only and FScreenSpacePaintResult has no FaceIndex; virtual back coverage still requires ScreenSpaceBrushQuery or exact mesh render-data layout\"" +
               ",\"side_back_failure\":\"" + json_escape(side.failure) + "\"";
    }

    auto sdk_assemble_texture_atlas(const ChannelBuffer& before_albedo,
                                    const ChannelBuffer& before_metallic,
                                    const ChannelBuffer& before_roughness,
                                    const std::vector<FrontSample>& samples) -> SdkTextureAtlas
    {
        SdkTextureAtlas out{};
        out.stats.width = before_albedo.width;
        out.stats.height = before_albedo.height;
        out.stats.source_samples = static_cast<int>(samples.size());
        const auto width = std::max(1, before_albedo.width);
        const auto height = std::max(1, before_albedo.height);
        const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        if (before_albedo.bytes.size() < pixels * 4)
        {
            out.failure = "atlas_albedo_bytes_invalid";
            return out;
        }
        if (before_metallic.bytes.empty() || before_roughness.bytes.empty())
        {
            out.failure = "atlas_material_channel_bytes_invalid";
            return out;
        }
        struct Texel
        {
            double r{0.0};
            double g{0.0};
            double b{0.0};
            double roughness{0.0};
            double metallic{0.0};
            double weight{0.0};
            int priority{0};
            bool floor_like{false};
        };
        std::vector<Texel> texels(pixels);
        std::vector<std::uint8_t> direct_mask(pixels, 0);
        auto splat = [&](const FrontSample& sample) {
            const auto priority = sample.atlas_priority > 0 ? sample.atlas_priority : (sample.floor_like ? 12 : 11);
            const auto weight = sample.atlas_weight > 0.0 ? sample.atlas_weight : (sample.floor_like ? 88.0 : 72.0);
            const auto cx = std::max(0, std::min(width - 1, static_cast<int>(std::round(clamp01(sample.u) * static_cast<double>(width - 1)))));
            const auto cy = std::max(0, std::min(height - 1, static_cast<int>(std::round(clamp01(sample.v) * static_cast<double>(height - 1)))));
            const auto radius = std::max(0, sample.atlas_radius);
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    if (dx * dx + dy * dy > radius * radius)
                    {
                        continue;
                    }
                    const auto x = cx + dx;
                    const auto y = cy + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height)
                    {
                        continue;
                    }
                    const auto index = static_cast<std::size_t>(y * width + x);
                    auto& texel = texels[index];
                    if (priority < texel.priority)
                    {
                        continue;
                    }
                    if (priority > texel.priority)
                    {
                        texel = Texel{};
                        texel.priority = priority;
                    }
                    const auto dist_sq = static_cast<double>(dx * dx + dy * dy);
                    const auto radius_sq = static_cast<double>(std::max(1, radius) * std::max(1, radius));
                    const auto local_weight = radius <= 0
                        ? weight
                        : weight * std::max(0.15, std::min(1.0, 1.0 - dist_sq / (radius_sq + 1.0)));
                    texel.r += clamp01(sample.r) * local_weight;
                    texel.g += clamp01(sample.g) * local_weight;
                    texel.b += clamp01(sample.b) * local_weight;
                    texel.roughness += clamp01(sample.roughness) * local_weight;
                    texel.metallic += clamp01(sample.metallic) * local_weight;
                    texel.weight += local_weight;
                    texel.floor_like = texel.floor_like || sample.floor_like;
                    direct_mask[index] = 1;
                }
            }
        };
        for (const auto& sample : samples)
        {
            splat(sample);
        }
        for (const auto mask : direct_mask)
        {
            if (mask)
            {
                ++out.stats.direct_texels;
            }
        }

        std::vector<Texel> extended_texels = texels;
        std::vector<std::uint8_t> painted_mask = direct_mask;
        const auto estimate_seed_radius_for_density = [](int texture_width, int texture_height, int seed_count) -> int {
            if (texture_width <= 0 || texture_height <= 0 || seed_count <= 0)
            {
                return 1;
            }
            const auto pixels_per_seed = (static_cast<double>(std::max(1, texture_width)) *
                                          static_cast<double>(std::max(1, texture_height))) /
                                         static_cast<double>(std::max(1, seed_count));
            const auto radius = static_cast<int>(std::round(std::sqrt(pixels_per_seed) * 0.48));
            return std::max(1, std::min(24, radius));
        };
        constexpr bool kEnableAtlasGapFill = false;
        if constexpr (kEnableAtlasGapFill)
        {
            const int front_gap_fill_radius = std::max(
                8,
                std::min(
                    24,
                    estimate_seed_radius_for_density(width, height, static_cast<int>(std::max<std::size_t>(1, samples.size()))) * 3));
            out.stats.gap_fill_radius = front_gap_fill_radius;
            std::vector<int> extension_counts(static_cast<std::size_t>(std::max(1, static_cast<int>(sdk_worker_count_for_items(pixels)))), 0);
            sdk_parallel_ranges(pixels, [&](std::size_t begin, std::size_t end, unsigned worker) {
                auto& local_filled = extension_counts[static_cast<std::size_t>(worker)];
                for (std::size_t index = begin; index < end; ++index)
                {
                    if (direct_mask[index])
                    {
                        continue;
                    }
                    const auto y = static_cast<int>(index / static_cast<std::size_t>(width));
                    const auto x = static_cast<int>(index - static_cast<std::size_t>(y * width));
                    int best_distance_sq = front_gap_fill_radius * front_gap_fill_radius + 1;
                    int best_priority = -1;
                    std::size_t best_index = static_cast<std::size_t>(-1);
                    for (int dy = -front_gap_fill_radius; dy <= front_gap_fill_radius; ++dy)
                    {
                        const auto sy = y + dy;
                        if (sy < 0 || sy >= height)
                        {
                            continue;
                        }
                        for (int dx = -front_gap_fill_radius; dx <= front_gap_fill_radius; ++dx)
                        {
                            const auto sx = x + dx;
                            if (sx < 0 || sx >= width)
                            {
                                continue;
                            }
                            const auto distance_sq = dx * dx + dy * dy;
                            if (distance_sq > front_gap_fill_radius * front_gap_fill_radius)
                            {
                                continue;
                            }
                            const auto source_index = static_cast<std::size_t>(sy * width + sx);
                            if (!direct_mask[source_index] || texels[source_index].weight <= 0.000001)
                            {
                                continue;
                            }
                            const auto priority = texels[source_index].priority;
                            if (priority > best_priority ||
                                (priority == best_priority && distance_sq < best_distance_sq))
                            {
                                best_priority = priority;
                                best_distance_sq = distance_sq;
                                best_index = source_index;
                            }
                        }
                    }
                    if (best_index != static_cast<std::size_t>(-1))
                    {
                        extended_texels[index] = texels[best_index];
                        painted_mask[index] = 1;
                        ++local_filled;
                    }
                }
            });
            for (const auto count : extension_counts)
            {
                out.stats.filled_by_extension += count;
            }
            constexpr bool kEnableFullNearestGapFill = false;
            if constexpr (kEnableFullNearestGapFill)
            {
                int full_nearest_filled = 0;
                std::vector<std::size_t> fill_queue{};
                fill_queue.reserve(pixels);
                for (std::size_t index = 0; index < pixels; ++index)
                {
                    if (painted_mask[index] && index < extended_texels.size() && extended_texels[index].weight > 0.000001)
                    {
                        fill_queue.push_back(index);
                    }
                }
                for (std::size_t head = 0; head < fill_queue.size(); ++head)
                {
                    const auto source_index = fill_queue[head];
                    if (source_index >= extended_texels.size() || extended_texels[source_index].weight <= 0.000001)
                    {
                        continue;
                    }
                    const auto y = static_cast<int>(source_index / static_cast<std::size_t>(width));
                    const auto x = static_cast<int>(source_index - static_cast<std::size_t>(y * width));
                    for (int dy = -1; dy <= 1; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0)
                            {
                                continue;
                            }
                            const auto nx = x + dx;
                            const auto ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                            {
                                continue;
                            }
                            const auto target_index = static_cast<std::size_t>(ny * width + nx);
                            if (target_index >= painted_mask.size() || painted_mask[target_index])
                            {
                                continue;
                            }
                            extended_texels[target_index] = extended_texels[source_index];
                            painted_mask[target_index] = 1;
                            fill_queue.push_back(target_index);
                            ++full_nearest_filled;
                        }
                    }
                }
                out.stats.filled_by_extension += full_nearest_filled;
                out.stats.filled_by_full_nearest = full_nearest_filled;
            }
        }

        out.albedo = before_albedo.bytes;
        out.metallic = before_metallic.bytes;
        out.roughness = before_roughness.bytes;
        out.painted_mask = std::move(painted_mask);
        out.stats.worker_threads = static_cast<int>(sdk_worker_count_for_items(pixels));
        auto write_scalar = [](std::vector<std::uint8_t>& bytes,
                               int bytes_per_pixel,
                               std::size_t index,
                               std::uint8_t value) {
            if (bytes.empty())
            {
                return;
            }
            if (bytes_per_pixel >= 4)
            {
                const auto offset = index * 4;
                if (offset + 3 >= bytes.size())
                {
                    return;
                }
                bytes[offset + 0] = value;
                bytes[offset + 1] = value;
                bytes[offset + 2] = value;
                bytes[offset + 3] = 255;
                return;
            }
            if (index < bytes.size())
            {
                bytes[index] = value;
            }
        };
        std::vector<int> preserved_counts(static_cast<std::size_t>(std::max(1, out.stats.worker_threads)), 0);
        sdk_parallel_ranges(pixels, [&](std::size_t begin, std::size_t end, unsigned worker) {
            auto& local_preserved = preserved_counts[static_cast<std::size_t>(worker)];
            for (std::size_t index = begin; index < end; ++index)
            {
                if (!out.painted_mask[index] || extended_texels[index].weight <= 0.000001)
                {
                    ++local_preserved;
                    continue;
                }
                const auto inv = 1.0 / extended_texels[index].weight;
                const auto offset = index * 4;
                out.albedo[offset + 0] = sdk_byte_from_unit(clamp01(extended_texels[index].r * inv));
                out.albedo[offset + 1] = sdk_byte_from_unit(clamp01(extended_texels[index].g * inv));
                out.albedo[offset + 2] = sdk_byte_from_unit(clamp01(extended_texels[index].b * inv));
                out.albedo[offset + 3] = before_albedo.bytes[offset + 3];
                write_scalar(out.metallic, before_metallic.bytes_per_pixel, index, sdk_byte_from_unit(clamp01(extended_texels[index].metallic * inv)));
                write_scalar(out.roughness, before_roughness.bytes_per_pixel, index, sdk_byte_from_unit(clamp01(extended_texels[index].roughness * inv)));
            }
        });
        for (const auto count : preserved_counts)
        {
            out.stats.preserved_original += count;
        }
        out.hash = hash_bytes(out.albedo);
        out.metallic_hash = hash_bytes(out.metallic);
        out.roughness_hash = hash_bytes(out.roughness);
        out.ok = out.stats.direct_texels > 0;
        out.failure = out.ok ? "ok" : "atlas_no_direct_texels";
        return out;
    }

    auto sdk_atlas_metadata(const SdkTextureAtlas& atlas) -> std::string
    {
        const auto pixels = static_cast<double>(std::max(1, atlas.stats.width) * std::max(1, atlas.stats.height));
        return ",\"atlas_source\":\"cpu_intermediate\"" +
               std::string(",\"albedo_width\":") + std::to_string(atlas.stats.width) +
               ",\"albedo_height\":" + std::to_string(atlas.stats.height) +
               ",\"srgb\":true" +
               ",\"direct_texels\":" + std::to_string(atlas.stats.direct_texels) +
               ",\"filled_by_extension\":" + std::to_string(atlas.stats.filled_by_extension) +
               ",\"filled_by_full_nearest\":" + std::to_string(atlas.stats.filled_by_full_nearest) +
               ",\"preserved_original\":" + std::to_string(atlas.stats.preserved_original) +
               ",\"atlas_gap_fill_radius\":" + std::to_string(atlas.stats.gap_fill_radius) +
               ",\"direct_texel_ratio\":" + std::to_string(static_cast<double>(atlas.stats.direct_texels) / pixels) +
               ",\"filled_by_extension_ratio\":" + std::to_string(static_cast<double>(atlas.stats.filled_by_extension) / pixels) +
               ",\"filled_by_full_nearest_ratio\":" + std::to_string(static_cast<double>(atlas.stats.filled_by_full_nearest) / pixels) +
               ",\"preserved_original_ratio\":" + std::to_string(static_cast<double>(atlas.stats.preserved_original) / pixels) +
               ",\"gap_fill_disabled\":true" +
               ",\"gap_fill_mode\":\"disabled_until_uv_chart_aware_fill\"" +
               ",\"gap_fill_disabled_reason\":\"local UV extension can cross mesh UV islands and move colors between body parts\"" +
               ",\"material_channel_write_disabled\":false" +
               ",\"material_channel_mode\":\"painted_texels_only\"" +
               ",\"atlas_source_samples\":" + std::to_string(atlas.stats.source_samples) +
               ",\"atlas_hash\":\"" + std::to_string(atlas.hash) + "\"" +
               ",\"atlas_metallic_hash\":\"" + std::to_string(atlas.metallic_hash) + "\"" +
               ",\"atlas_roughness_hash\":\"" + std::to_string(atlas.roughness_hash) + "\"" +
               ",\"atlas_failure\":\"" + json_escape(atlas.failure) + "\"";
    }

    auto sdk_write_atlas_debug_artifacts(const SdkTextureAtlas& atlas) -> std::string
    {
        const int width = atlas.stats.width;
        const int height = atlas.stats.height;
        const std::size_t pixels = width > 0 && height > 0
            ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            : 0U;
        bool coverage_json_written = false;
        bool mask_written = false;
        bool preview_written = false;

        std::string coverage = "{";
        coverage += "\"stage\":\"uv_coverage\",";
        coverage += "\"width\":" + std::to_string(width) + ",";
        coverage += "\"height\":" + std::to_string(height) + ",";
        coverage += "\"direct_texels\":" + std::to_string(atlas.stats.direct_texels) + ",";
        coverage += "\"filled_by_extension\":" + std::to_string(atlas.stats.filled_by_extension) + ",";
        coverage += "\"preserved_original\":" + std::to_string(atlas.stats.preserved_original) + ",";
        coverage += "\"source_samples\":" + std::to_string(atlas.stats.source_samples) + ",";
        coverage += "\"worker_threads\":" + std::to_string(atlas.stats.worker_threads) + ",";
        coverage += "\"hash\":\"" + std::to_string(atlas.hash) + "\",";
        coverage += "\"metallic_hash\":\"" + std::to_string(atlas.metallic_hash) + "\",";
        coverage += "\"roughness_hash\":\"" + std::to_string(atlas.roughness_hash) + "\",";
        coverage += "\"failure\":\"" + json_escape(atlas.failure) + "\"";
        coverage += "}\n";
        coverage_json_written = write_runtime_artifact_text(L"uv_coverage.json", coverage);

        if (pixels > 0 && atlas.painted_mask.size() >= pixels)
        {
            std::string pgm = "P5\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
            pgm.reserve(pgm.size() + pixels);
            for (std::size_t index = 0; index < pixels; ++index)
            {
                pgm.push_back(atlas.painted_mask[index] ? static_cast<char>(255) : static_cast<char>(0));
            }
            mask_written = write_runtime_artifact_binary(L"atlas_coverage_mask.pgm", pgm);
        }

        if (pixels > 0 && atlas.albedo.size() >= pixels * 4U)
        {
            std::string ppm = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
            ppm.reserve(ppm.size() + pixels * 3U);
            for (std::size_t index = 0; index < pixels; ++index)
            {
                const auto offset = index * 4U;
                ppm.push_back(static_cast<char>(atlas.albedo[offset + 0]));
                ppm.push_back(static_cast<char>(atlas.albedo[offset + 1]));
                ppm.push_back(static_cast<char>(atlas.albedo[offset + 2]));
            }
            preview_written = write_runtime_artifact_binary(L"atlas_preview.ppm", ppm);
        }

        return std::string(",\"uv_coverage_dump_written\":") + json_bool(coverage_json_written) +
               ",\"atlas_coverage_mask_written\":" + json_bool(mask_written) +
               ",\"atlas_preview_written\":" + json_bool(preview_written) +
               ",\"runtime_artifact_dir\":\"%LOCALAPPDATA%\\\\MecchaCamouflage\\\\runtime\"";
    }

    auto sdk_write_static_hybrid_artifacts(const SdkTextureAtlas& atlas,
                                           int front_hits,
                                           const SdkAtlasSideBackResult& side) -> std::string
    {
        const int width = atlas.stats.width;
        const int height = atlas.stats.height;
        const std::size_t pixels = width > 0 && height > 0
            ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            : 0U;
        bool atlas_preview_written = false;
        bool edge_weight_written = false;
        bool chart_map_written = false;
        bool coverage_written = false;
        bool quality_written = false;

        if (pixels > 0 && atlas.albedo.size() >= pixels * 4U)
        {
            std::string ppm = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
            ppm.reserve(ppm.size() + pixels * 3U);
            for (std::size_t index = 0; index < pixels; ++index)
            {
                const auto offset = index * 4U;
                ppm.push_back(static_cast<char>(atlas.albedo[offset + 0]));
                ppm.push_back(static_cast<char>(atlas.albedo[offset + 1]));
                ppm.push_back(static_cast<char>(atlas.albedo[offset + 2]));
            }
            atlas_preview_written = write_runtime_artifact_binary(L"hybrid_atlas_preview.ppm", ppm);
        }

        if (pixels > 0 && atlas.painted_mask.size() >= pixels)
        {
            std::string edge = "P5\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
            edge.reserve(edge.size() + pixels);
            for (std::size_t index = 0; index < pixels; ++index)
            {
                edge.push_back(atlas.painted_mask[index] ? static_cast<char>(255) : static_cast<char>(0));
            }
            edge_weight_written = write_runtime_artifact_binary(L"hybrid_edge_weight.pgm", edge);

            std::string chart = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
            chart.reserve(chart.size() + pixels * 3U);
            for (std::size_t index = 0; index < pixels; ++index)
            {
                if (atlas.painted_mask[index])
                {
                    chart.push_back(static_cast<char>(64));
                    chart.push_back(static_cast<char>(196));
                    chart.push_back(static_cast<char>(96));
                }
                else
                {
                    chart.push_back(static_cast<char>(0));
                    chart.push_back(static_cast<char>(0));
                    chart.push_back(static_cast<char>(0));
                }
            }
            chart_map_written = write_runtime_artifact_binary(L"hybrid_uv_chart_map.ppm", chart);
        }

        const auto pixel_count = static_cast<double>(std::max<std::size_t>(1U, pixels));
        std::string coverage = "{";
        coverage += "\"stage\":\"hybrid_view_coverage\",";
        coverage += "\"route\":\"static_hybrid_front_side_probe\",";
        coverage += "\"width\":" + std::to_string(width) + ",";
        coverage += "\"height\":" + std::to_string(height) + ",";
        coverage += "\"front_hits\":" + std::to_string(front_hits) + ",";
        coverage += "\"side_hits\":" + std::to_string(side.side_hits) + ",";
        coverage += "\"back_hits\":0,";
        coverage += "\"side_attempts\":" + std::to_string(side.attempts) + ",";
        coverage += "\"side_success\":" + std::to_string(side.success) + ",";
        coverage += "\"side_owner_hits\":" + std::to_string(side.owner_hits) + ",";
        coverage += "\"side_uv_hits\":" + std::to_string(side.uv_hits) + ",";
        coverage += "\"direct_texels\":" + std::to_string(atlas.stats.direct_texels) + ",";
        coverage += "\"direct_texel_ratio\":" + std::to_string(static_cast<double>(atlas.stats.direct_texels) / pixel_count) + ",";
        coverage += "\"side_backend\":\"" + json_escape(side.backend) + "\",";
        coverage += "\"side_failure\":\"" + json_escape(side.failure) + "\",";
        coverage += "\"views\":\"front_dominant_plus_side_-60_-45_-30_-15_15_30_45_60\"";
        coverage += "}\n";
        coverage_written = write_runtime_artifact_text(L"hybrid_view_coverage.json", coverage);

        std::string quality = "{";
        quality += "\"stage\":\"hybrid_quality\",";
        quality += "\"route\":\"static_hybrid_front_side_probe\",";
        quality += "\"mesh_chart_available\":false,";
        quality += "\"chart_aware_fill_enabled\":false,";
        quality += "\"chart_aware_fill_failure\":\"mesh_chart_unavailable\",";
        quality += "\"fallback_used\":false,";
        quality += "\"paint_used\":false,";
        quality += "\"texture_import_used\":false,";
        quality += "\"texture_sync_used\":false,";
        quality += "\"back_views_used\":false,";
        quality += "\"front_hits\":" + std::to_string(front_hits) + ",";
        quality += "\"side_hits\":" + std::to_string(side.side_hits) + ",";
        quality += "\"back_hits\":0,";
        quality += "\"direct_texels\":" + std::to_string(atlas.stats.direct_texels) + ",";
        quality += "\"direct_texel_ratio\":" + std::to_string(static_cast<double>(atlas.stats.direct_texels) / pixel_count) + ",";
        quality += "\"metallic\":0.0,";
        quality += "\"roughness_interior\":0.88,";
        quality += "\"roughness_side\":0.94,";
        quality += "\"roughness_edge\":0.98,";
        quality += "\"albedo_space\":\"srgb_preview_linear_calculation_not_yet_enabled\",";
        quality += "\"hybrid_stage\":\"front_side_probe_artifacts_only\"";
        quality += "}\n";
        quality_written = write_runtime_artifact_text(L"hybrid_quality.json", quality);

        return std::string(",\"hybrid_atlas_preview_written\":") + json_bool(atlas_preview_written) +
               ",\"hybrid_edge_weight_written\":" + json_bool(edge_weight_written) +
               ",\"hybrid_view_coverage_written\":" + json_bool(coverage_written) +
               ",\"hybrid_uv_chart_map_written\":" + json_bool(chart_map_written) +
               ",\"hybrid_quality_written\":" + json_bool(quality_written) +
               ",\"hybrid_runtime_artifact_dir\":\"%LOCALAPPDATA%\\\\MecchaCamouflage\\\\runtime\"";
    }

    auto sdk_build_atlas_strokes(const SdkContext& ctx,
                                 const SdkTextureAtlas& atlas,
                                 meccha_sdk::EPaintChannel target_channel,
                                 bool use_world_position,
                                 int stroke_cap = 600000) -> SdkAtlasStrokePlan
    {
        SdkAtlasStrokePlan plan{};
        plan.stroke_cap = std::max(1, stroke_cap);
        if (!atlas.ok || atlas.albedo.empty() || atlas.painted_mask.empty() || atlas.stats.width <= 0 || atlas.stats.height <= 0)
        {
            plan.failure = "atlas_unavailable";
            return plan;
        }
        const auto width = atlas.stats.width;
        const auto height = atlas.stats.height;
        const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<std::uint8_t> used(pixels, 0);
        auto block_ok = [&](int x, int y, int size, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) -> bool {
            if (x + size > width || y + size > height)
            {
                return false;
            }
            int r_min = 255, g_min = 255, b_min = 255;
            int r_max = 0, g_max = 0, b_max = 0;
            bool any = false;
            for (int by = 0; by < size; ++by)
            {
                for (int bx = 0; bx < size; ++bx)
                {
                    const auto index = static_cast<std::size_t>((y + by) * width + (x + bx));
                    if (index >= pixels || used[index] || !atlas.painted_mask[index])
                    {
                        return false;
                    }
                    const auto offset = index * 4;
                    const int cr = atlas.albedo[offset + 0];
                    const int cg = atlas.albedo[offset + 1];
                    const int cb = atlas.albedo[offset + 2];
                    r_min = std::min(r_min, cr); r_max = std::max(r_max, cr);
                    g_min = std::min(g_min, cg); g_max = std::max(g_max, cg);
                    b_min = std::min(b_min, cb); b_max = std::max(b_max, cb);
                    any = true;
                }
            }
            if (!any || r_max - r_min > 2 || g_max - g_min > 2 || b_max - b_min > 2)
            {
                return false;
            }
            r = static_cast<std::uint8_t>((r_min + r_max) / 2);
            g = static_cast<std::uint8_t>((g_min + g_max) / 2);
            b = static_cast<std::uint8_t>((b_min + b_max) / 2);
            return true;
        };
        auto mark_used = [&](int x, int y, int size) {
            for (int by = 0; by < size; ++by)
            {
                for (int bx = 0; bx < size; ++bx)
                {
                    used[static_cast<std::size_t>((y + by) * width + (x + bx))] = 1;
                }
            }
        };
        auto push_stroke = [&](int x, int y, int size, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            const auto radius_uv = std::max(0.75 / static_cast<double>(std::max(width, height)),
                                            (static_cast<double>(size) * 0.45) / static_cast<double>(std::max(width, height)));
            auto brush = sdk_copy_current_brush(ctx, radius_uv);
            const auto channel = sdk_make_channel(sdk_srgb_to_linear_unit(static_cast<double>(r) / 255.0),
                                                  sdk_srgb_to_linear_unit(static_cast<double>(g) / 255.0),
                                                  sdk_srgb_to_linear_unit(static_cast<double>(b) / 255.0),
                                                  0.0,
                                                  0.65,
                                                  meccha_sdk::EPaintChannelApplyMode::Override);
            const auto u = (static_cast<double>(x) + static_cast<double>(size) * 0.5) / static_cast<double>(width);
            const auto v = (static_cast<double>(y) + static_cast<double>(size) * 0.5) / static_cast<double>(height);
            if (use_world_position)
            {
                plan.strokes.push_back(sdk_make_stroke(u, v, channel, brush, target_channel, ctx.body_world_position));
            }
            else
            {
                plan.strokes.push_back(sdk_make_uv_stroke(u, v, channel, brush, target_channel));
            }
            if (size >= 8) ++plan.merged_8;
            else if (size >= 4) ++plan.merged_4;
            else if (size >= 2) ++plan.merged_2;
            else ++plan.singles;
        };
        const int block_sizes[]{2, 1};
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto index = static_cast<std::size_t>(y * width + x);
                if (used[index] || !atlas.painted_mask[index])
                {
                    continue;
                }
                ++plan.source_texels;
                bool emitted = false;
                for (const auto size : block_sizes)
                {
                    std::uint8_t r = 0, g = 0, b = 0;
                    if (block_ok(x, y, size, r, g, b))
                    {
                        push_stroke(x, y, size, r, g, b);
                        mark_used(x, y, size);
                        emitted = true;
                        break;
                    }
                }
                if (!emitted)
                {
                    const auto offset = index * 4;
                    push_stroke(x, y, 1, atlas.albedo[offset + 0], atlas.albedo[offset + 1], atlas.albedo[offset + 2]);
                    used[index] = 1;
                }
                if (static_cast<int>(plan.strokes.size()) > plan.stroke_cap)
                {
                    plan.cap_exceeded = true;
                    plan.failure = "atlas_stroke_budget_exceeded";
                    return plan;
                }
            }
        }
        plan.failure = plan.strokes.empty() ? "atlas_strokes_empty" : "ok";
        return plan;
    }

    auto sdk_atlas_stroke_metadata(const SdkAtlasStrokePlan& plan) -> std::string
    {
        return ",\"stroke_count\":" + std::to_string(plan.strokes.size()) +
               ",\"stroke_cap\":" + std::to_string(plan.stroke_cap) +
               ",\"stroke_target\":" + std::to_string(plan.target_strokes) +
               ",\"stroke_source_texels\":" + std::to_string(plan.source_texels) +
               ",\"stroke_merged_8\":" + std::to_string(plan.merged_8) +
               ",\"stroke_merged_4\":" + std::to_string(plan.merged_4) +
               ",\"stroke_merged_2\":" + std::to_string(plan.merged_2) +
               ",\"stroke_singles\":" + std::to_string(plan.singles) +
               ",\"merge_error_max\":" + std::to_string(plan.merge_error_max) +
               ",\"stroke_color_space\":\"srgb_to_linear_flinearcolor\"" +
               ",\"stroke_radius_factor\":0.45" +
               ",\"stroke_block_sizes\":\"2,1\"" +
               ",\"atlas_stroke_cap_exceeded\":" + std::string(json_bool(plan.cap_exceeded)) +
               ",\"atlas_stroke_failure\":\"" + json_escape(plan.failure) + "\"";
    }

    auto sdk_append_uv_gap_fill(std::vector<FrontSample>& samples, double brush_radius, SdkUvGapFillStats& stats) -> void
    {
        if (samples.empty())
        {
            return;
        }
        struct Cell
        {
            bool covered{false};
            bool direct{false};
            FrontSample sample{};
        };
        const auto cell_size = std::max(0.000001, brush_radius * 0.5);
        const auto grid_edge = std::max(8, std::min(512, static_cast<int>(std::ceil(1.0 / cell_size))));
        const auto actual_cell = 1.0 / static_cast<double>(grid_edge);
        const auto footprint = std::max(0, static_cast<int>(std::floor(brush_radius / actual_cell)));
        std::vector<Cell> cells(static_cast<std::size_t>(grid_edge * grid_edge));
        auto index_for = [grid_edge](int x, int y) { return static_cast<std::size_t>(y * grid_edge + x); };
        auto cell_for = [grid_edge](double value) {
            return std::max(0, std::min(grid_edge - 1, static_cast<int>(std::floor(clamp01(value) * static_cast<double>(grid_edge)))));
        };
        int min_x = grid_edge - 1;
        int min_y = grid_edge - 1;
        int max_x = 0;
        int max_y = 0;
        for (const auto& sample : samples)
        {
            const auto cx = cell_for(sample.u);
            const auto cy = cell_for(sample.v);
            min_x = std::min(min_x, cx);
            min_y = std::min(min_y, cy);
            max_x = std::max(max_x, cx);
            max_y = std::max(max_y, cy);
            for (int dy = -footprint; dy <= footprint; ++dy)
            {
                for (int dx = -footprint; dx <= footprint; ++dx)
                {
                    if (footprint > 0 && dx * dx + dy * dy > footprint * footprint)
                    {
                        continue;
                    }
                    const auto x = cx + dx;
                    const auto y = cy + dy;
                    if (x < 0 || y < 0 || x >= grid_edge || y >= grid_edge)
                    {
                        continue;
                    }
                    auto& cell = cells[index_for(x, y)];
                    cell.covered = true;
                    cell.direct = true;
                    cell.sample = sample;
                }
            }
        }
        const auto margin = footprint + 4;
        min_x = std::max(0, min_x - margin);
        min_y = std::max(0, min_y - margin);
        max_x = std::min(grid_edge - 1, max_x + margin);
        max_y = std::min(grid_edge - 1, max_y + margin);
        stats.considered_cells = std::max(1, (max_x - min_x + 1) * (max_y - min_y + 1));
        auto covered_count = [&]() {
            int count = 0;
            for (int y = min_y; y <= max_y; ++y)
            {
                for (int x = min_x; x <= max_x; ++x)
                {
                    if (cells[index_for(x, y)].covered)
                    {
                        ++count;
                    }
                }
            }
            return count;
        };
        stats.direct_cells = covered_count();
        stats.coverage_before = static_cast<double>(stats.direct_cells) / static_cast<double>(stats.considered_cells);
        auto source_at = [&](int x, int y, int dx, int dy, FrontSample& out, int& distance) {
            for (int step = 1; step <= 5; ++step)
            {
                const auto sx = x + dx * step;
                const auto sy = y + dy * step;
                if (sx < 0 || sy < 0 || sx >= grid_edge || sy >= grid_edge)
                {
                    continue;
                }
                const auto& cell = cells[index_for(sx, sy)];
                if (cell.direct)
                {
                    out = cell.sample;
                    distance = step;
                    return true;
                }
            }
            return false;
        };
        auto normal_dot = [](const FrontSample& a, const FrontSample& b) {
            return a.normal.X * b.normal.X + a.normal.Y * b.normal.Y + a.normal.Z * b.normal.Z;
        };
        const auto max_fill = std::min(4096, static_cast<int>(samples.size()) * 3);
        std::vector<FrontSample> fill{};
        fill.reserve(static_cast<std::size_t>(max_fill));
        for (int y = min_y; y <= max_y && static_cast<int>(fill.size()) < max_fill; ++y)
        {
            for (int x = min_x; x <= max_x && static_cast<int>(fill.size()) < max_fill; ++x)
            {
                if (cells[index_for(x, y)].covered)
                {
                    ++stats.rejected_occupied;
                    continue;
                }
                FrontSample left{}, right{}, up{}, down{};
                int dl = 0, dr = 0, du = 0, dd = 0;
                const bool has_lr = source_at(x, y, -1, 0, left, dl) && source_at(x, y, 1, 0, right, dr);
                const bool has_ud = source_at(x, y, 0, -1, up, du) && source_at(x, y, 0, 1, down, dd);
                if (!has_lr && !has_ud)
                {
                    ++stats.rejected_unbounded;
                    continue;
                }
                std::vector<FrontSample> sources{};
                if (has_lr)
                {
                    sources.push_back(left);
                    sources.push_back(right);
                }
                if (has_ud)
                {
                    sources.push_back(up);
                    sources.push_back(down);
                }
                bool normal_ok = true;
                for (std::size_t i = 0; i < sources.size() && normal_ok; ++i)
                {
                    for (std::size_t j = i + 1; j < sources.size(); ++j)
                    {
                        if (normal_dot(sources[i], sources[j]) < 0.62)
                        {
                            normal_ok = false;
                            break;
                        }
                    }
                }
                ++stats.candidates;
                if (!normal_ok)
                {
                    ++stats.rejected_normal;
                    continue;
                }
                FrontSample out{};
                out.u = (static_cast<double>(x) + 0.5) * actual_cell;
                out.v = (static_cast<double>(y) + 0.5) * actual_cell;
                out.radius = brush_radius;
                out.has_world_position = true;
                const auto inv = 1.0 / static_cast<double>(sources.size());
                for (const auto& source : sources)
                {
                    out.r += source.r * inv;
                    out.g += source.g * inv;
                    out.b += source.b * inv;
                    out.roughness += source.roughness * inv;
                    out.metallic += source.metallic * inv;
                    out.world_position.X += source.world_position.X * inv;
                    out.world_position.Y += source.world_position.Y * inv;
                    out.world_position.Z += source.world_position.Z * inv;
                    out.normal.X += source.normal.X * inv;
                    out.normal.Y += source.normal.Y * inv;
                    out.normal.Z += source.normal.Z * inv;
                }
                out.normal = sdk_vec_normalize(out.normal);
                fill.push_back(out);
                cells[index_for(x, y)].covered = true;
                ++stats.bounded;
            }
        }
        stats.sent = static_cast<int>(fill.size());
        stats.coverage_after = static_cast<double>(covered_count()) / static_cast<double>(stats.considered_cells);
        samples.insert(samples.end(), fill.begin(), fill.end());
    }

    auto sdk_gap_fill_metadata(const SdkUvGapFillStats& stats) -> std::string
    {
        return ",\"uv_gap_fill_candidates\":" + std::to_string(stats.candidates) +
               ",\"uv_gap_fill_sent\":" + std::to_string(stats.sent) +
               ",\"uv_gap_fill_bounded\":" + std::to_string(stats.bounded) +
               ",\"uv_gap_fill_edge_extended\":" + std::to_string(stats.edge_extended) +
               ",\"uv_gap_fill_rejected_unbounded\":" + std::to_string(stats.rejected_unbounded) +
               ",\"uv_gap_fill_rejected_normal\":" + std::to_string(stats.rejected_normal) +
               ",\"uv_gap_fill_rejected_occupied\":" + std::to_string(stats.rejected_occupied) +
               ",\"uv_gap_fill_direct_cells\":" + std::to_string(stats.direct_cells) +
               ",\"uv_gap_fill_considered_cells\":" + std::to_string(stats.considered_cells) +
               ",\"uv_gap_fill_coverage_before\":" + std::to_string(stats.coverage_before) +
               ",\"uv_gap_fill_coverage_after\":" + std::to_string(stats.coverage_after);
    }

    auto sdk_front_world_position_count(const std::vector<FrontSample>& samples) -> int
    {
        int count = 0;
        for (const auto& sample : samples)
        {
            if (sample.has_world_position)
            {
                ++count;
            }
        }
        return count;
    }

    struct SdkMetallicBasePlan
    {
        std::vector<meccha_sdk::FPaintStroke> strokes{};
        int grid{0};
        int texture_width{0};
        int texture_height{0};
        double brush_radius{0.0};
        double brush_footprint_texels{0.0};
    };

    auto sdk_build_metallic_base_plan(const SdkContext& ctx, const ChannelBuffer& albedo) -> SdkMetallicBasePlan
    {
        SdkMetallicBasePlan plan{};
        plan.texture_width = albedo.width > 0 ? albedo.width : 1024;
        plan.texture_height = albedo.height > 0 ? albedo.height : 1024;
        const auto texture_edge = std::max(1, std::max(plan.texture_width, plan.texture_height));
        constexpr double texture_min_radius = 0.02;
        constexpr double texture_max_radius = 0.20;
        plan.brush_radius = std::min(texture_max_radius, std::max(0.001, std::max(0.02, texture_min_radius)));
        plan.brush_footprint_texels = std::max(1.0, plan.brush_radius * static_cast<double>(texture_edge) * 2.0);
        const auto spacing_uv = std::min(1.0 / 16.0, std::max(1.0 / 96.0, plan.brush_radius * 1.25));
        plan.grid = std::max(16, std::min(96, static_cast<int>(std::ceil(1.0 / spacing_uv))));
        const auto brush = sdk_copy_current_brush(ctx, plan.brush_radius);
        const auto channel = sdk_make_channel(1.0,
                                              1.0,
                                              1.0,
                                              1.0,
                                              0.0,
                                              meccha_sdk::EPaintChannelApplyMode::Override);
        plan.strokes.reserve(static_cast<std::size_t>(plan.grid) * static_cast<std::size_t>(plan.grid));
        for (int y = 0; y < plan.grid; ++y)
        {
            for (int x = 0; x < plan.grid; ++x)
            {
                plan.strokes.push_back(sdk_make_uv_stroke((static_cast<double>(x) + 0.5) / static_cast<double>(plan.grid),
                                                          (static_cast<double>(y) + 0.5) / static_cast<double>(plan.grid),
                                                          channel,
                                                          brush,
                                                          meccha_sdk::EPaintChannel::AlbedoMetallicRoughness));
            }
        }
        return plan;
    }

    auto sdk_build_front_strokes(const SdkContext& ctx, const std::vector<FrontSample>& samples) -> std::vector<meccha_sdk::FPaintStroke>
    {
        std::vector<meccha_sdk::FPaintStroke> strokes{};
        strokes.reserve(samples.size());
        for (const auto& sample : samples)
        {
            const auto brush = sdk_copy_current_brush(ctx, sample.radius);
            const auto channel = sdk_make_channel(sdk_srgb_to_linear_unit(sample.r),
                                                  sdk_srgb_to_linear_unit(sample.g),
                                                  sdk_srgb_to_linear_unit(sample.b),
                                                  0.0,
                                                  0.65,
                                                  meccha_sdk::EPaintChannelApplyMode::AlphaBlend);
            const auto world_position = sample.has_world_position ? sample.world_position : ctx.body_world_position;
            strokes.push_back(sdk_make_stroke(sample.u,
                                              sample.v,
                                              channel,
                                              brush,
                                              meccha_sdk::EPaintChannel::AlbedoMetallicRoughness,
                                              world_position));
        }
        return strokes;
    }

    auto sdk_colorize_native_front_samples(const std::vector<FrontSample>& native_surface_samples,
                                           const std::vector<FrontSample>& payload_samples)
        -> std::vector<FrontSample>
    {
        std::vector<FrontSample> samples{};
        if (native_surface_samples.empty())
        {
            return samples;
        }
        samples.reserve(native_surface_samples.size());
        for (const auto& surface : native_surface_samples)
        {
            FrontSample sample = surface;
            const FrontSample* nearest_payload = nullptr;
            double best_distance = 1000000.0;
            for (const auto& payload : payload_samples)
            {
                const auto du = clamp01(payload.u) - clamp01(surface.u);
                const auto dv = clamp01(payload.v) - clamp01(surface.v);
                const auto distance = du * du + dv * dv;
                if (!nearest_payload || distance < best_distance)
                {
                    nearest_payload = &payload;
                    best_distance = distance;
                }
            }
            if (nearest_payload)
            {
                sample.r = clamp01(nearest_payload->r);
                sample.g = clamp01(nearest_payload->g);
                sample.b = clamp01(nearest_payload->b);
                sample.radius = std::max(0.012, std::min(0.035, nearest_payload->radius > 0.0 ? nearest_payload->radius : 0.018));
            }
            else
            {
                sample.r = 0.34;
                sample.g = 0.36;
                sample.b = 0.32;
                sample.radius = 0.018;
            }
            sample.u = clamp01(surface.u);
            sample.v = clamp01(surface.v);
            sample.roughness = 0.65;
            sample.has_world_position = true;
            sample.world_position = surface.world_position;
            samples.push_back(sample);
        }
        return samples;
    }

    auto sdk_paint_full_route_native_direct(const std::string& request) -> std::string
    {
        const auto start = std::chrono::steady_clock::now();
        auto elapsed_now_ms = [&]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        };
        Reflection* progress_ref = nullptr;
        const SdkContext* progress_ctx = nullptr;
        auto emit_progress = [&](const std::string& stage,
                                 const std::string& message,
                                 int step,
                                 int total_steps,
                                 double elapsed_ms,
                                 const std::string& extra = "") {
            write_bridge_progress(stage, message, step, total_steps, elapsed_ms, extra);
        };
        const bool is_probe = request.find("\"type\":\"sdk_probe\"") != std::string::npos;
        const bool is_deep_probe = request.find("\"type\":\"sdk_deep_probe\"") != std::string::npos ||
                                   request.find("\"native_apply_mode\":\"sdk_deep_probe_only\"") != std::string::npos;
        const bool legacy_diagnostic_import = false;
        const bool texture_sync_probe = request.find("\"native_apply_mode\":\"texture_sync_strict_probe\"") != std::string::npos ||
                                        request.find("\"route\":\"f10_texture_sync_strict_probe\"") != std::string::npos;
        const bool static_hybrid_probe = request.find("\"native_apply_mode\":\"static_hybrid_front_side_probe\"") != std::string::npos ||
                                         request.find("\"route\":\"f10_static_hybrid_front_side_probe\"") != std::string::npos;
        const bool color_transfer_probe = false;
        const bool front_texture_import = texture_sync_probe;
        const bool strict_38923_front_texture_import = texture_sync_probe;
        const bool diagnostic_import = texture_sync_probe;
        const bool disabled_full_stream = false;
        const bool disabled_paint_stream = false;
        const bool disabled_sample_stream = false;
        const bool disabled_metallic_stream = false;
        const bool disabled_mesh_probe_only = false;
        const bool disabled_mesh_texture_stream = false;
        const bool disabled_mesh_route = false;
        const bool front_metallic_texture_route = disabled_metallic_stream;
        const bool front_paint_route = false;
        const std::string route_name = static_hybrid_probe ? "static_hybrid_front_side_probe" :
            (texture_sync_probe ? "texture_sync_strict_probe" : "unsupported_route");
        if (!is_probe && !is_deep_probe && !texture_sync_probe && !static_hybrid_probe)
        {
            return response_json(false,
                                 "unsupported_route",
                                 0,
                                 1,
                                 "unsupported native apply mode; only texture_sync_strict_probe and static_hybrid_front_side_probe are available",
                                 std::string("\"route\":\"") + json_escape(route_name) + "\"" +
                                     ",\"supported_native_apply_modes\":[\"texture_sync_strict_probe\",\"static_hybrid_front_side_probe\"]");
        }
        static volatile LONG paint_busy = 0;
        static volatile LONG dump_busy = 0;
        struct BusyGuard
        {
            volatile LONG* flag{nullptr};
            bool active{false};
            ~BusyGuard()
            {
                if (active && flag)
                {
                    InterlockedExchange(flag, 0);
                }
            }
        } busy_guard{&paint_busy, false};
        struct DumpGuard
        {
            volatile LONG* flag{nullptr};
            bool active{false};
            ~DumpGuard()
            {
                if (active && flag)
                {
                    InterlockedExchange(flag, 0);
                }
            }
        } dump_guard{&dump_busy, false};
        if (is_deep_probe)
        {
            if (paint_busy != 0)
            {
                return response_json(false,
                                     "sdk_deep_probe_deferred",
                                     0,
                                     1,
                                     "SDK deep probe deferred because paint is in progress",
                                     "\"dump_deferred\":true,\"dump_deferred_reason\":\"paint_in_progress\"");
            }
            if (InterlockedCompareExchange(&dump_busy, 1, 0) != 0)
            {
                return response_json(false,
                                     "sdk_deep_probe_deferred",
                                     0,
                                     1,
                                     "SDK deep probe deferred because another dump is in progress",
                                     "\"dump_deferred\":true,\"dump_deferred_reason\":\"dump_in_progress\"");
            }
            g_dump_cancel_requested.store(false);
            dump_guard.active = true;
        }
        if (!is_probe && !is_deep_probe)
        {
            if (dump_busy != 0)
            {
                g_dump_cancel_requested.store(true);
            }
            clear_bridge_progress();
            emit_progress("paint_started", "native paint request accepted", 0, 8, 0.0);
            if (InterlockedCompareExchange(&paint_busy, 1, 0) != 0)
            {
                return response_json(false, "paint_ignored_busy", 0, 1, "paint request ignored because another paint is in progress");
            }
            busy_guard.active = true;
        }
        Reflection ref{};
        std::string failure{};
        if (!ref.init(failure))
        {
            return response_json(false, "sdk_unavailable", 0, 1, failure);
        }
        const auto ctx = sdk_resolve_context(ref);
        progress_ref = &ref;
        progress_ctx = &ctx;
        std::string metadata = sdk_context_metadata(ref, ctx);
        if (!ctx.ok)
        {
            const auto stage = is_probe ? ctx.stage : std::string("sdk_context_unavailable");
            return response_json(false, stage.c_str(), 0, 1, ctx.message, metadata);
        }
        if (!front_texture_import && !static_hybrid_probe && !is_probe && !is_deep_probe && !sdk_has_replicated_api(ctx))
        {
            return response_json(false,
                                 "replicated_api_unavailable",
                                 0,
                                 1,
                                 "ServerPaintBatch replicated paint RPC is unavailable",
                                 metadata + ",\"bridge_events\":[\"replicated_api_unavailable\"]");
        }
        if (is_deep_probe)
        {
            write_bridge_progress("mesh_snapshot_started", "probing SDK mesh/component snapshot inputs", 0, 2, 0.0);
            const auto mesh = sdk_find_front_mesh(ref, ctx);
            const auto query = sdk_find_screen_space_brush_query(ref, ctx);
            const auto viewport = sdk_get_viewport_info(ref, ctx);
            const auto deproject_function = ref.find_function(ctx.controller, "DeprojectScreenPositionToWorld");
            const auto query_function = query ? ref.find_function(query, "QueryFromWorldRay") : 0;
            const auto hit_test_function = ref.find_function(ctx.component, "HitTestAtScreenPosition");
            const auto get_skeletal_mesh_asset = mesh ? ref.find_function(mesh, "GetSkeletalMeshAsset") : 0;
            const auto get_skeletal_mesh = mesh ? ref.find_function(mesh, "GetSkeletalMesh") : 0;
            const auto get_static_mesh = mesh ? ref.find_function(mesh, "GetStaticMesh") : 0;
            const auto get_mesh_paint_uv_index = mesh ? ref.find_function(mesh, "GetMeshPaintTextureCoordinateIndex") : 0;
            auto property_offset_for_object = [&](std::uintptr_t object, const char* name) -> int {
                for (auto cls = ref.class_ptr(object); cls; cls = safe_read<std::uintptr_t>(cls + OffSuperStruct))
                {
                    const auto prop = ref.find_property(cls, name);
                    if (prop)
                    {
                        return prop_offset(prop);
                    }
                }
                return -1;
            };
            const auto component_to_world_offset = property_offset_for_object(mesh, "ComponentToWorld");
            const auto bounds_offset = property_offset_for_object(mesh, "Bounds");
            const auto skeletal_mesh_property_offset = property_offset_for_object(mesh, "SkeletalMesh");
            const auto static_mesh_property_offset = property_offset_for_object(mesh, "StaticMesh");
            const bool mesh_component_available = mesh && live_uobject(mesh);
            const bool mesh_asset_function_available = get_skeletal_mesh_asset || get_skeletal_mesh || get_static_mesh;
            const auto skeletal_mesh_from_function = get_skeletal_mesh_asset ? call_no_params_return_object(ref, mesh, "GetSkeletalMeshAsset") : 0;
            const auto skeletal_mesh_from_property = skeletal_mesh_property_offset >= 0
                ? safe_read<std::uintptr_t>(mesh + static_cast<std::uintptr_t>(skeletal_mesh_property_offset))
                : 0;
            const auto selected_mesh_asset = live_uobject(skeletal_mesh_from_function)
                ? skeletal_mesh_from_function
                : (live_uobject(skeletal_mesh_from_property) ? skeletal_mesh_from_property : 0);
            const bool mesh_asset_available = selected_mesh_asset && live_uobject(selected_mesh_asset);
            const auto render_data_offset = selected_mesh_asset ? property_offset_for_object(selected_mesh_asset, "RenderData") : -1;
            const auto imported_model_offset = selected_mesh_asset ? property_offset_for_object(selected_mesh_asset, "ImportedModel") : -1;
            const auto lod_info_offset = selected_mesh_asset ? property_offset_for_object(selected_mesh_asset, "LODInfo") : -1;
            const auto materials_offset = selected_mesh_asset ? property_offset_for_object(selected_mesh_asset, "Materials") : -1;
            const auto skeleton_offset = selected_mesh_asset ? property_offset_for_object(selected_mesh_asset, "Skeleton") : -1;
            const bool mesh_render_data_available = render_data_offset >= 0;
            const bool mesh_snapshot_ready = false;
            g_mesh_snapshot_ready.store(mesh_snapshot_ready);
            std::string deep = metadata;
            deep += ",\"deep_probe_version\":2";
            deep += ",\"deep_probe_paint_mutation\":false";
            deep += ",\"deep_probe_import_mutation\":false";
            deep += ",\"deep_probe_sidecar\":\"meccha-xenos-bridge.dll.deep_probe.json\"";
            deep += ",\"deep_probe_runtime_artifact\":\"sdk_deep_probe.json\"";
            deep += ",\"viewport_width\":" + std::to_string(viewport.width);
            deep += ",\"viewport_height\":" + std::to_string(viewport.height);
            deep += std::string(",\"mesh_component_available\":") + json_bool(mesh_component_available);
            deep += std::string(",\"mesh_component\":\"") + hex_address(mesh) + "\"";
            deep += std::string(",\"mesh_component_class\":\"") + json_escape(ref.class_name(mesh)) + "\"";
            deep += ",\"mesh_component_to_world_offset\":" + std::to_string(component_to_world_offset);
            deep += ",\"mesh_bounds_offset\":" + std::to_string(bounds_offset);
            deep += ",\"mesh_skeletal_mesh_property_offset\":" + std::to_string(skeletal_mesh_property_offset);
            deep += ",\"mesh_static_mesh_property_offset\":" + std::to_string(static_mesh_property_offset);
            deep += std::string(",\"mesh_asset_available\":") + json_bool(mesh_asset_available);
            deep += std::string(",\"mesh_asset\":\"") + hex_address(selected_mesh_asset) + "\"";
            deep += std::string(",\"mesh_asset_class\":\"") + json_escape(ref.class_name(selected_mesh_asset)) + "\"";
            deep += std::string(",\"mesh_asset_from_function\":\"") + hex_address(skeletal_mesh_from_function) + "\"";
            deep += std::string(",\"mesh_asset_from_property\":\"") + hex_address(skeletal_mesh_from_property) + "\"";
            deep += ",\"mesh_asset_render_data_offset\":" + std::to_string(render_data_offset);
            deep += ",\"mesh_asset_imported_model_offset\":" + std::to_string(imported_model_offset);
            deep += ",\"mesh_asset_lod_info_offset\":" + std::to_string(lod_info_offset);
            deep += ",\"mesh_asset_materials_offset\":" + std::to_string(materials_offset);
            deep += ",\"mesh_asset_skeleton_offset\":" + std::to_string(skeleton_offset);
            deep += std::string(",\"function_get_skeletal_mesh_asset_available\":") + json_bool(get_skeletal_mesh_asset != 0);
            deep += std::string(",\"function_get_skeletal_mesh_available\":") + json_bool(get_skeletal_mesh != 0);
            deep += std::string(",\"function_get_static_mesh_available\":") + json_bool(get_static_mesh != 0);
            deep += std::string(",\"function_get_mesh_paint_uv_index_available\":") + json_bool(get_mesh_paint_uv_index != 0);
            deep += std::string(",\"function_deproject_available\":") + json_bool(deproject_function != 0);
            deep += std::string(",\"function_deproject_schema\":\"") + json_escape(function_param_schema(ref, deproject_function)) + "\"";
            deep += std::string(",\"function_hit_test_at_screen_position_available\":") + json_bool(hit_test_function != 0);
            deep += std::string(",\"function_hit_test_at_screen_position_schema\":\"") + json_escape(function_param_schema(ref, hit_test_function)) + "\"";
            deep += std::string(",\"runtime_paint_cached_triangle_front_candidate\":") + json_bool(hit_test_function != 0 && mesh_component_available);
            deep += ",\"runtime_paint_cached_triangle_back_candidate\":false";
            deep += ",\"runtime_paint_cached_triangle_back_blocker\":\"HitTestAtScreenPosition uses current PlayerController screen projection and returns FScreenSpacePaintResult without FaceIndex\"";
            deep += ",\"brush_query_face_index_available\":true";
            deep += ",\"side_back_generation_game_thread_bound\":true";
            deep += ",\"side_back_generation_stable_strategy\":\"bounded_screen_space_brush_query_until_exact_mesh_render_data_layout\"";
            deep += std::string(",\"screen_space_brush_query_available\":") + json_bool(query != 0);
            deep += std::string(",\"screen_space_brush_query\":\"") + hex_address(query) + "\"";
            deep += std::string(",\"screen_space_brush_query_class\":\"") + json_escape(ref.class_name(query)) + "\"";
            deep += std::string(",\"function_query_from_world_ray_available\":") + json_bool(query_function != 0);
            deep += std::string(",\"function_query_from_world_ray_schema\":\"") + json_escape(function_param_schema(ref, query_function)) + "\"";
            deep += std::string(",\"function_export_schema\":\"") + json_escape(function_param_schema(ref, ctx.export_function)) + "\"";
            deep += std::string(",\"function_import_schema\":\"") + json_escape(function_param_schema(ref, ctx.import_function)) + "\"";
            deep += std::string(",\"function_server_paint_batch_schema\":\"") + json_escape(function_param_schema(ref, ctx.server_paint_batch_function)) + "\"";
            deep += std::string(",\"function_paint_at_uv_with_brush_schema\":\"") + json_escape(function_param_schema(ref, ctx.paint_at_uv_with_brush_function)) + "\"";
            deep += ",\"mesh_snapshot_probe_implemented\":false";
            deep += std::string(",\"mesh_snapshot_ready\":") + json_bool(mesh_snapshot_ready);
            deep += ",\"mesh_snapshot_runtime_artifact\":\"mesh_snapshot.json\"";
            deep += ",\"mesh_snapshot_v2_runtime_artifact\":\"mesh_snapshot_v2.json\"";
            deep += ",\"cpu_raycast_validation_runtime_artifact\":\"cpu_raycast_validation.json\"";
            deep += ",\"raycast_profile_runtime_artifact\":\"raycast_profile.json\"";
            deep += ",\"uv_hit_heatmap_runtime_artifact\":\"uv_hit_heatmap.pgm\"";
            deep += ",\"atlas_preview_runtime_artifact\":\"atlas_preview.ppm\"";
            deep += std::string(",\"exact_layout_source\":\"") + meccha_mesh_layout::LayoutSource + "\"";
            deep += std::string(",\"exact_layout_profile\":\"") + meccha_mesh_layout::LayoutProfile + "\"";
            deep += std::string(",\"exact_layout_version\":\"") + meccha_mesh_layout::LayoutVersion + "\"";
            deep += std::string(",\"generated_cppsdk_available\":") + json_bool(meccha_mesh_layout::GeneratedCppSdkAvailable);
            deep += std::string(",\"generated_cppsdk_path\":\"") + json_escape(meccha_mesh_layout::GeneratedCppSdkPath) + "\"";
            deep += std::string(",\"generated_sdk_version\":\"") + json_escape(meccha_mesh_layout::GeneratedSdkVersion) + "\"";
            deep += std::string(",\"runtime_paint_sdk_available\":") + json_bool(meccha_mesh_layout::RuntimePaintSdkAvailable);
            deep += std::string(",\"screen_space_brush_query_sdk_available\":") + json_bool(meccha_mesh_layout::ScreenSpaceBrushQuerySdkAvailable);
            deep += std::string(",\"exact_layout_available\":") + json_bool(meccha_mesh_layout::ExactRenderDataLayoutAvailable);
            deep += std::string(",\"sdk_layout_stage\":\"") + meccha_mesh_layout::FailureStage + "\"";
            deep += std::string(",\"sdk_layout_failure_reason\":\"") + meccha_mesh_layout::FailureReason + "\"";
            deep += std::string(",\"mesh_render_data_available\":") + json_bool(mesh_render_data_available);
            deep += ",\"mesh_vertex_buffer_available\":false";
            deep += ",\"mesh_index_buffer_available\":false";
            deep += ",\"mesh_uv_buffer_available\":false";
            deep += ",\"skinned_position_source_available\":false";
            deep += std::string(",\"mesh_raycast_candidate\":") + json_bool(mesh_component_available && mesh_asset_function_available);
            deep += std::string(",\"mesh_raycast_blocker\":\"") + (mesh_asset_available ? "vertex_index_uv_decode_not_implemented" : "mesh_component_or_asset_function_unavailable") + "\"";
            deep += ",\"bridge_events\":[\"sdk_deep_probe\"]";
            const std::string deep_payload = std::string("{\"success\":true,\"stage\":\"sdk_deep_probe\",\"metadata\":{\"bridge\":\"meccha-xenos-bridge\",") + deep + "}}\n";
            std::string mesh_snapshot = "{\"success\":false,\"stage\":\"mesh_snapshot_unavailable\",\"metadata\":{";
            mesh_snapshot += std::string("\"mesh_component_available\":") + json_bool(mesh_component_available) + ",";
            mesh_snapshot += std::string("\"mesh_component\":\"") + hex_address(mesh) + "\",";
            mesh_snapshot += std::string("\"mesh_component_class\":\"") + json_escape(ref.class_name(mesh)) + "\",";
            mesh_snapshot += "\"mesh_component_properties\":" + property_list_json(ref, mesh, 96) + ",";
            mesh_snapshot += "\"mesh_component_functions\":" + function_list_json(ref, mesh, 96) + ",";
            mesh_snapshot += "\"mesh_component_array_headers\":" + array_header_scan_json(mesh, 0x1000, 96) + ",";
            mesh_snapshot += "\"mesh_component_pointer_scan\":" + pointer_scan_json(mesh, 0x1000, 128) + ",";
            mesh_snapshot += "\"mesh_component_pointer_target_arrays\":" + pointer_target_array_scan_json(mesh, 0x1000, 0x600, 64, 24) + ",";
            mesh_snapshot += "\"mesh_component_to_world_offset\":" + std::to_string(component_to_world_offset) + ",";
            mesh_snapshot += "\"mesh_bounds_offset\":" + std::to_string(bounds_offset) + ",";
            mesh_snapshot += "\"mesh_skeletal_mesh_property_offset\":" + std::to_string(skeletal_mesh_property_offset) + ",";
            mesh_snapshot += "\"mesh_static_mesh_property_offset\":" + std::to_string(static_mesh_property_offset) + ",";
            mesh_snapshot += std::string("\"mesh_asset_available\":") + json_bool(mesh_asset_available) + ",";
            mesh_snapshot += std::string("\"mesh_asset\":\"") + hex_address(selected_mesh_asset) + "\",";
            mesh_snapshot += std::string("\"mesh_asset_class\":\"") + json_escape(ref.class_name(selected_mesh_asset)) + "\",";
            mesh_snapshot += std::string("\"mesh_asset_from_function\":\"") + hex_address(skeletal_mesh_from_function) + "\",";
            mesh_snapshot += std::string("\"mesh_asset_from_property\":\"") + hex_address(skeletal_mesh_from_property) + "\",";
            mesh_snapshot += "\"mesh_asset_properties\":" + property_list_json(ref, selected_mesh_asset, 128) + ",";
            mesh_snapshot += "\"mesh_asset_functions\":" + function_list_json(ref, selected_mesh_asset, 128) + ",";
            mesh_snapshot += "\"mesh_asset_array_headers\":" + array_header_scan_json(selected_mesh_asset, 0x1800, 128) + ",";
            mesh_snapshot += "\"mesh_asset_pointer_scan\":" + pointer_scan_json(selected_mesh_asset, 0x1800, 160) + ",";
            mesh_snapshot += "\"mesh_asset_pointer_target_arrays\":" + pointer_target_array_scan_json(selected_mesh_asset, 0x1800, 0x900, 128, 32) + ",";
            mesh_snapshot += "\"mesh_asset_render_data_offset\":" + std::to_string(render_data_offset) + ",";
            mesh_snapshot += "\"mesh_asset_imported_model_offset\":" + std::to_string(imported_model_offset) + ",";
            mesh_snapshot += "\"mesh_asset_lod_info_offset\":" + std::to_string(lod_info_offset) + ",";
            mesh_snapshot += "\"mesh_asset_materials_offset\":" + std::to_string(materials_offset) + ",";
            mesh_snapshot += "\"mesh_asset_skeleton_offset\":" + std::to_string(skeleton_offset) + ",";
            mesh_snapshot += std::string("\"function_get_skeletal_mesh_asset_available\":") + json_bool(get_skeletal_mesh_asset != 0) + ",";
            mesh_snapshot += std::string("\"function_get_skeletal_mesh_available\":") + json_bool(get_skeletal_mesh != 0) + ",";
            mesh_snapshot += std::string("\"function_get_static_mesh_available\":") + json_bool(get_static_mesh != 0) + ",";
            mesh_snapshot += std::string("\"function_get_mesh_paint_uv_index_available\":") + json_bool(get_mesh_paint_uv_index != 0) + ",";
            mesh_snapshot += std::string("\"mesh_render_data_available\":") + json_bool(mesh_render_data_available) + ",";
            mesh_snapshot += "\"mesh_vertex_buffer_available\":false,";
            mesh_snapshot += "\"mesh_index_buffer_available\":false,";
            mesh_snapshot += "\"mesh_uv_buffer_available\":false,";
            mesh_snapshot += std::string("\"mesh_raycast_candidate\":") + json_bool(mesh_component_available && mesh_asset_function_available) + ",";
            mesh_snapshot += std::string("\"mesh_raycast_blocker\":\"") + (mesh_asset_available ? "vertex_index_uv_decode_not_implemented" : "mesh_component_or_asset_function_unavailable") + "\"";
            mesh_snapshot += "}}\n";
            std::string mesh_buffer_candidates = "{\"success\":true,\"stage\":\"mesh_buffer_candidates_dumped\",\"metadata\":{";
            mesh_buffer_candidates += std::string("\"mesh_component\":\"") + hex_address(mesh) + "\",";
            mesh_buffer_candidates += std::string("\"mesh_component_class\":\"") + json_escape(ref.class_name(mesh)) + "\",";
            mesh_buffer_candidates += std::string("\"mesh_asset\":\"") + hex_address(selected_mesh_asset) + "\",";
            mesh_buffer_candidates += std::string("\"mesh_asset_class\":\"") + json_escape(ref.class_name(selected_mesh_asset)) + "\",";
            mesh_buffer_candidates += "\"mesh_component_candidates\":" + mesh_buffer_candidates_json(ref, mesh, 0x1000, 0x600, 40) + ",";
            mesh_buffer_candidates += "\"mesh_asset_candidates\":" + mesh_buffer_candidates_json(ref, selected_mesh_asset, 0x1800, 0x900, 80);
            mesh_buffer_candidates += "}}\n";
            std::string mesh_snapshot_v2 = "{\"success\":false,\"stage\":\"";
            mesh_snapshot_v2 += meccha_mesh_layout::FailureStage;
            mesh_snapshot_v2 += "\",\"metadata\":{";
            mesh_snapshot_v2 += "\"schema_version\":2,";
            mesh_snapshot_v2 += std::string("\"generated_cppsdk_available\":") + json_bool(meccha_mesh_layout::GeneratedCppSdkAvailable) + ",";
            mesh_snapshot_v2 += std::string("\"generated_cppsdk_path\":\"") + json_escape(meccha_mesh_layout::GeneratedCppSdkPath) + "\",";
            mesh_snapshot_v2 += std::string("\"generated_sdk_version\":\"") + json_escape(meccha_mesh_layout::GeneratedSdkVersion) + "\",";
            mesh_snapshot_v2 += std::string("\"runtime_paint_sdk_available\":") + json_bool(meccha_mesh_layout::RuntimePaintSdkAvailable) + ",";
            mesh_snapshot_v2 += std::string("\"screen_space_brush_query_sdk_available\":") + json_bool(meccha_mesh_layout::ScreenSpaceBrushQuerySdkAvailable) + ",";
            mesh_snapshot_v2 += std::string("\"exact_layout_source\":\"") + meccha_mesh_layout::LayoutSource + "\",";
            mesh_snapshot_v2 += std::string("\"exact_layout_profile\":\"") + meccha_mesh_layout::LayoutProfile + "\",";
            mesh_snapshot_v2 += std::string("\"exact_layout_version\":\"") + meccha_mesh_layout::LayoutVersion + "\",";
            mesh_snapshot_v2 += std::string("\"exact_layout_available\":") + json_bool(meccha_mesh_layout::ExactRenderDataLayoutAvailable) + ",";
            mesh_snapshot_v2 += std::string("\"layout_failure_reason\":\"") + meccha_mesh_layout::FailureReason + "\",";
            mesh_snapshot_v2 += "\"memory_scan_fallback_used\":false,";
            mesh_snapshot_v2 += "\"screen_space_brush_query_generation_used\":false,";
            mesh_snapshot_v2 += "\"mesh_component_available\":" + std::string(mesh_component_available ? "true" : "false") + ",";
            mesh_snapshot_v2 += std::string("\"mesh_component\":\"") + hex_address(mesh) + "\",";
            mesh_snapshot_v2 += std::string("\"mesh_component_class\":\"") + json_escape(ref.class_name(mesh)) + "\",";
            mesh_snapshot_v2 += "\"mesh_asset_available\":" + std::string(mesh_asset_available ? "true" : "false") + ",";
            mesh_snapshot_v2 += std::string("\"mesh_asset\":\"") + hex_address(selected_mesh_asset) + "\",";
            mesh_snapshot_v2 += std::string("\"mesh_asset_class\":\"") + json_escape(ref.class_name(selected_mesh_asset)) + "\",";
            mesh_snapshot_v2 += "\"mesh_variant\":\"unknown_until_exact_layout\",";
            mesh_snapshot_v2 += "\"lod_index\":0,";
            mesh_snapshot_v2 += "\"paint_uv_channel\":0,";
            mesh_snapshot_v2 += "\"vertices\":0,";
            mesh_snapshot_v2 += "\"indices\":0,";
            mesh_snapshot_v2 += "\"triangles\":0,";
            mesh_snapshot_v2 += "\"sections\":0,";
            mesh_snapshot_v2 += "\"bones\":0,";
            mesh_snapshot_v2 += "\"uv_channels\":0,";
            mesh_snapshot_v2 += "\"skin_weight_available\":false,";
            mesh_snapshot_v2 += "\"component_transform_available\":false,";
            mesh_snapshot_v2 += "\"bone_transform_available\":false,";
            mesh_snapshot_v2 += "\"mesh_raycast_route_ready\":false";
            mesh_snapshot_v2 += "}}\n";
            std::string cpu_validation = "{\"success\":false,\"stage\":\"mesh_snapshot_failed\",\"metadata\":{";
            cpu_validation += "\"schema_version\":1,\"cpu_validation_executed\":false,\"reason\":\"mesh_snapshot_v2_not_ready\",";
            cpu_validation += "\"cpu_vs_query_uv_delta_avg\":null,\"cpu_vs_query_uv_delta_p95\":null,\"cpu_vs_query_uv_delta_max\":null,";
            cpu_validation += "\"cpu_vs_query_world_delta_avg\":null,\"cpu_vs_query_world_delta_p95\":null,\"cpu_vs_query_world_delta_max\":null";
            cpu_validation += "}}\n";
            std::string raycast_profile = "{\"success\":false,\"stage\":\"mesh_snapshot_failed\",\"metadata\":{";
            raycast_profile += "\"worker_threads\":0,\"bvh_ms\":0,\"raycast_ms\":0,\"atlas_ms\":0,";
            raycast_profile += "\"raycast_hits\":0,\"misses\":0,\"front_hits\":0,\"back_hits\":0";
            raycast_profile += "}}\n";
            const std::string uv_heatmap = std::string("P5\n1 1\n255\n") + std::string(1, '\0');
            const std::string atlas_preview = std::string("P6\n1 1\n255\n") + std::string(3, '\0');
            const bool wrote_sidecar = write_bridge_sidecar_text(
                L".deep_probe.json",
                deep_payload);
            const bool wrote_runtime_probe = write_runtime_artifact_text(L"sdk_deep_probe.json", deep_payload);
            const bool wrote_mesh_snapshot = write_runtime_artifact_text(L"mesh_snapshot.json", mesh_snapshot);
            const bool wrote_mesh_buffer_candidates = write_runtime_artifact_text(L"mesh_buffer_candidates.json", mesh_buffer_candidates);
            const bool wrote_mesh_snapshot_v2 = write_runtime_artifact_text(L"mesh_snapshot_v2.json", mesh_snapshot_v2);
            const bool wrote_cpu_validation = write_runtime_artifact_text(L"cpu_raycast_validation.json", cpu_validation);
            const bool wrote_raycast_profile = write_runtime_artifact_text(L"raycast_profile.json", raycast_profile);
            const bool wrote_uv_heatmap = write_runtime_artifact_text(L"uv_hit_heatmap.pgm", uv_heatmap);
            const bool wrote_atlas_preview = write_runtime_artifact_text(L"atlas_preview.ppm", atlas_preview);
            deep += std::string(",\"deep_probe_sidecar_written\":") + json_bool(wrote_sidecar);
            deep += std::string(",\"sdk_deep_probe_runtime_written\":") + json_bool(wrote_runtime_probe);
            deep += std::string(",\"mesh_snapshot_runtime_written\":") + json_bool(wrote_mesh_snapshot);
            deep += std::string(",\"mesh_buffer_candidates_runtime_written\":") + json_bool(wrote_mesh_buffer_candidates);
            deep += std::string(",\"mesh_snapshot_v2_runtime_written\":") + json_bool(wrote_mesh_snapshot_v2);
            deep += std::string(",\"cpu_raycast_validation_runtime_written\":") + json_bool(wrote_cpu_validation);
            deep += std::string(",\"raycast_profile_runtime_written\":") + json_bool(wrote_raycast_profile);
            deep += std::string(",\"uv_hit_heatmap_runtime_written\":") + json_bool(wrote_uv_heatmap);
            deep += std::string(",\"atlas_preview_runtime_written\":") + json_bool(wrote_atlas_preview);
            deep += ",\"mesh_buffer_candidates_runtime_artifact\":\"mesh_buffer_candidates.json\"";
            write_bridge_progress("mesh_snapshot_done", mesh_snapshot_ready ? "mesh snapshot is ready" : "mesh snapshot render data unavailable", 2, 2, 1.0,
                                  "\"mesh_snapshot_ready\":" + std::string(mesh_snapshot_ready ? "true" : "false"));
            return response_json(true, "sdk_deep_probe", 0, 0, "SDK deep probe completed without paint/import mutation", deep);
        }

        metadata += ",\"render_target_albedo\":\"" + hex_address(sdk_get_render_target(ctx, 0)) + "\"" +
                    ",\"route\":\"" + route_name + "\"" +
                    ",\"replication\":\"component_server_paint_batch\"" +
                    ",\"replicated_paint_used\":" + json_bool(!front_texture_import && !static_hybrid_probe) +
                    ",\"front_paint_stream_used\":" + json_bool(disabled_paint_stream || disabled_sample_stream || disabled_metallic_stream) +
                    ",\"disabled_paint_stream_used\":" + json_bool(disabled_paint_stream) +
                    ",\"disabled_sample_stream_used\":" + json_bool(disabled_sample_stream) +
                    ",\"front_texture_import_used\":" + json_bool(texture_sync_probe) +
                    ",\"front_texture_source_expected\":\"" + std::string(strict_38923_front_texture_import ? "bulk_calibrated_direct_texture" :
                                                                           (front_texture_import ? "sampled_pixel_front_atlas_legacy_explicit_only" : "not_applicable")) + "\"" +
                    ",\"front_texture_parity_target\":\"38923cc_assemble_direct_texture\"" +
                    ",\"bulk_readback_used\":false" +
                    ",\"capture_transform_backend\":\"project_world_to_screen_scaled\"" +
                    ",\"image_bulk_calibration_ok\":false" +
                    ",\"texture_source_verified\":false" +
                    ",\"bulk_readback_failure\":\"pending_capture\"" +
                    ",\"temporary_diagnostic_only\":" + json_bool(texture_sync_probe || static_hybrid_probe) +
                    ",\"diagnostic_import_channels\":" + std::string(front_texture_import ? "[\"albedo\"]" : "[]") +
                    ",\"texture_sync_probe\":" + json_bool(texture_sync_probe) +
                    ",\"static_hybrid_front_side_probe\":" + json_bool(static_hybrid_probe) +
                    ",\"static_hybrid_artifact_only\":" + json_bool(static_hybrid_probe) +
                    ",\"static_hybrid_target_views\":\"front_dominant_plus_side_-60_-45_-30_-15_15_30_45_60\"" +
                    ",\"static_hybrid_back_skipped\":true" +
                    ",\"static_hybrid_apply_skipped\":true" +
                    ",\"color_transfer_probe\":" + json_bool(color_transfer_probe) +
                    ",\"multiplayer_sync_unverified\":" + json_bool(texture_sync_probe) +
                    ",\"front_payload_placement_used\":false" +
                    ",\"front_payload_color_used\":false" +
                    ",\"front_only\":" + json_bool(front_paint_route && !front_texture_import) +
                    ",\"side_back_skipped\":" + json_bool(front_paint_route && !front_texture_import) +
                    ",\"front_texture_back_skipped\":false" +
                    ",\"front_texture_material_channels_overlaid\":" + json_bool(front_texture_import) +
                    ",\"metallic_base_used\":" + json_bool(front_metallic_texture_route && !front_texture_import) +
                    ",\"metallic_base_skipped\":" + json_bool(!front_metallic_texture_route || front_texture_import) +
                    ",\"metallic_base_skip_reason\":\"" + std::string(front_texture_import ? "skipped_for_38923_texture_parity" : "not_applicable") + "\"" +
                    ",\"target_channel\":\"Albedo\"";

        if (!is_probe && !is_deep_probe && disabled_mesh_route && !g_mesh_snapshot_ready.load())
        {
            const char* cpu_stage = meccha_mesh_layout::ExactRenderDataLayoutAvailable ? "mesh_snapshot_failed" : meccha_mesh_layout::FailureStage;
            emit_progress(cpu_stage, "CPU mesh snapshot is unavailable; exact render data layout is not ready", 1, 8, elapsed_now_ms());
            metadata += ",\"mesh_snapshot_ready\":false" +
                        std::string(",\"mesh_raycast_route_ready\":false") +
                        std::string(",\"generated_cppsdk_available\":") + json_bool(meccha_mesh_layout::GeneratedCppSdkAvailable) +
                        std::string(",\"generated_cppsdk_path\":\"") + json_escape(meccha_mesh_layout::GeneratedCppSdkPath) + "\"" +
                        std::string(",\"generated_sdk_version\":\"") + json_escape(meccha_mesh_layout::GeneratedSdkVersion) + "\"" +
                        std::string(",\"runtime_paint_sdk_available\":") + json_bool(meccha_mesh_layout::RuntimePaintSdkAvailable) +
                        std::string(",\"screen_space_brush_query_sdk_available\":") + json_bool(meccha_mesh_layout::ScreenSpaceBrushQuerySdkAvailable) +
                        std::string(",\"exact_layout_source\":\"") + meccha_mesh_layout::LayoutSource + "\"" +
                        std::string(",\"exact_layout_profile\":\"") + meccha_mesh_layout::LayoutProfile + "\"" +
                        std::string(",\"exact_layout_version\":\"") + meccha_mesh_layout::LayoutVersion + "\"" +
                        std::string(",\"exact_layout_available\":") + json_bool(meccha_mesh_layout::ExactRenderDataLayoutAvailable) +
                        std::string(",\"layout_failure_reason\":\"") + meccha_mesh_layout::FailureReason + "\"" +
                        std::string(",\"disabled_mesh_probe_only\":") + json_bool(disabled_mesh_probe_only) +
                        std::string(",\"mesh_texture_paint_stream\":") + json_bool(disabled_mesh_texture_stream) +
                        ",\"screen_space_brush_query_fallback_used\":false" +
                        ",\"front_sampling_ue_query_skipped\":true" +
                        ",\"runtime_artifact_dir\":\"%LOCALAPPDATA%\\\\MecchaCamouflage\\\\runtime\"" +
                        std::string(",\"bridge_events\":[\"") + cpu_stage + "\"]";
            return response_json(false,
                                 cpu_stage,
                                 0,
                                 1,
                                 "CPU mesh snapshot is unavailable; cpu_mesh route requires exact Dumper7 render data layout",
                                 metadata);
        }

        auto before0 = sdk_export_channel_bytes(ref, ctx, 0);
        auto before1 = sdk_export_channel_bytes(ref, ctx, 1);
        auto before2 = sdk_export_channel_bytes(ref, ctx, 2);
        const auto pre_metallic_albedo = before0;
        const auto pre_metallic_metallic = before1;
        const auto pre_metallic_roughness = before2;
        metadata += sdk_channel_metadata("before_albedo", before0);
        metadata += sdk_channel_metadata("before_metallic", before1);
        metadata += sdk_channel_metadata("before_roughness", before2);

        if (!before0.ok)
        {
            return response_json(false,
                                 "export_failed",
                                 0,
                                 1,
                                 "SDK albedo export failed: " + before0.failure,
                                 metadata);
        }
        if (is_probe)
        {
            return response_json(true,
                                 "sdk_probe",
                                 0,
                                 0,
                                 "SDK probe ok; replicated paint API ready",
                                 metadata + ",\"bridge_events\":[\"replicated_api_ready\"]");
        }

        meccha_sdk::EPaintChannel atlas_target_channel = meccha_sdk::EPaintChannel::Albedo;
        bool atlas_use_world_position = false;
        std::string paint_api_probe_selected = "";
        if (front_texture_import)
        {
            metadata += std::string(",\"metallic_base_apply_backend\":\"skipped_38923_texture_parity\"") +
                        ",\"metallic_base_requested\":0" +
                        ",\"metallic_base_server_sent\":0" +
                        ",\"metallic_base_server_failed\":0" +
                        ",\"metallic_base_batch_calls\":0" +
                        ",\"metallic_base_single_calls\":0" +
                        ",\"metallic_base_client_mirror_sent\":0" +
                        ",\"metallic_base_client_mirror_failed\":0" +
                        ",\"metallic_base_server_rpc\":\"none\"" +
                        ",\"metallic_base_client_mirror_rpc\":\"none\"" +
                        ",\"metallic_base_import_albedo_ok\":false" +
                        ",\"metallic_base_import_metallic_ok\":false" +
                        ",\"metallic_base_import_roughness_ok\":false" +
                        ",\"metallic_base_import_albedo_failure\":\"skipped\"" +
                        ",\"metallic_base_import_metallic_failure\":\"skipped\"" +
                        ",\"metallic_base_import_roughness_failure\":\"skipped\"" +
                        sdk_channel_metadata("after_metallic_albedo", before0) +
                        sdk_channel_metadata("after_metallic_metallic", before1) +
                        sdk_channel_metadata("after_metallic_roughness", before2) +
                        ",\"metallic_base_hash_changed\":false" +
                        ",\"metallic_base_visible\":false" +
                        ",\"metallic_base_observed\":false" +
                        ",\"metallic_base_channel0_observed\":false" +
                        ",\"metallic_base_channel1_observed\":false" +
                        ",\"metallic_base_channel2_observed\":false" +
                        ",\"metallic_base_settle_checks\":0" +
                        ",\"metallic_base_settled_before_front\":true" +
                        ",\"metallic_base_changed_during_settle\":false" +
                        ",\"paint_api_probe_skipped\":true";
            paint_api_probe_selected = "skipped_texture_sync_strict_probe_not_paint_route";
            atlas_target_channel = meccha_sdk::EPaintChannel::Albedo;
            atlas_use_world_position = false;
        }
        else if (front_metallic_texture_route)
        {
            SdkReplicatedStats metallic_stats{};
            auto metallic_plan = sdk_build_metallic_base_plan(ctx, before0);
            const auto& metallic_strokes = metallic_plan.strokes;
            emit_progress("metallic_base_prepared", "prepared full-body metallic white UV grid", 1, 9, elapsed_now_ms(),
                          "\"stroke_count\":" + std::to_string(metallic_strokes.size()) +
                          ",\"grid\":" + std::to_string(metallic_plan.grid) +
                          ",\"brush_radius\":" + std::to_string(metallic_plan.brush_radius));
            emit_progress("metallic_base_apply_tick", "dispatching metallic ServerPaintBatch and PaintAtUVWithBrush echo", 1, 9, elapsed_now_ms(),
                          "\"stroke_count\":" + std::to_string(metallic_strokes.size()));
            const bool server_ok = sdk_dispatch_replicated_strokes(ctx, metallic_strokes, metallic_stats);
            const bool local_echo_ok = sdk_apply_local_echo_strokes(ctx, metallic_strokes, metallic_stats);
            if (!server_ok || !local_echo_ok)
            {
                metadata += sdk_stats_metadata("metallic_base", metallic_stats) +
                            ",\"metallic_base_grid\":" + std::to_string(metallic_plan.grid) +
                            ",\"metallic_base_texture_width\":" + std::to_string(metallic_plan.texture_width) +
                            ",\"metallic_base_texture_height\":" + std::to_string(metallic_plan.texture_height) +
                            ",\"metallic_base_brush_radius\":" + std::to_string(metallic_plan.brush_radius) +
                            ",\"metallic_base_brush_footprint_texels\":" + std::to_string(metallic_plan.brush_footprint_texels) +
                            ",\"metallic_base_visible\":false" +
                            ",\"bridge_events\":[\"replicated_api_ready\",\"metallic_base_prepared\",\"metallic_base_apply_tick\",\"metallic_base_not_visible\"]";
                return response_json(false,
                                     "metallic_base_not_visible",
                                     metallic_stats.server_sent,
                                     std::max(1, metallic_stats.server_failed + metallic_stats.client_mirror_failed),
                                     "metallic base did not reach required replicated+local visible path: " + metallic_stats.first_failure,
                                     metadata);
            }
            Sleep(120);
            auto after_metallic0 = sdk_export_channel_bytes(ref, ctx, 0);
            auto after_metallic1 = sdk_export_channel_bytes(ref, ctx, 1);
            auto after_metallic2 = sdk_export_channel_bytes(ref, ctx, 2);
            const bool metallic_base_visible = metallic_stats.server_sent > 0 && metallic_stats.client_mirror_sent > 0;
            const bool metallic_base_observed = metallic_base_visible &&
                                                after_metallic0.ok &&
                                                after_metallic1.ok &&
                                                after_metallic2.ok;
            metadata += sdk_stats_metadata("metallic_base", metallic_stats) +
                        sdk_channel_metadata("after_metallic_albedo", after_metallic0) +
                        sdk_channel_metadata("after_metallic_metallic", after_metallic1) +
                        sdk_channel_metadata("after_metallic_roughness", after_metallic2) +
                        ",\"metallic_base_grid\":" + std::to_string(metallic_plan.grid) +
                        ",\"metallic_base_texture_width\":" + std::to_string(metallic_plan.texture_width) +
                        ",\"metallic_base_texture_height\":" + std::to_string(metallic_plan.texture_height) +
                        ",\"metallic_base_brush_radius\":" + std::to_string(metallic_plan.brush_radius) +
                        ",\"metallic_base_brush_footprint_texels\":" + std::to_string(metallic_plan.brush_footprint_texels) +
                        ",\"metallic_base_hash_changed\":" + json_bool((after_metallic0.ok && after_metallic0.hash != before0.hash) ||
                                                                        (after_metallic1.ok && after_metallic1.hash != before1.hash) ||
                                                                        (after_metallic2.ok && after_metallic2.hash != before2.hash)) +
                        ",\"metallic_base_visible\":" + json_bool(metallic_base_visible) +
                        ",\"metallic_base_observed\":" + json_bool(metallic_base_observed) +
                        ",\"metallic_base_channel0_observed\":" + json_bool(after_metallic0.ok) +
                        ",\"metallic_base_channel1_observed\":" + json_bool(after_metallic1.ok) +
                        ",\"metallic_base_channel2_observed\":" + json_bool(after_metallic2.ok);
            if (!metallic_base_observed)
            {
                metadata += ",\"bridge_events\":[\"replicated_api_ready\",\"metallic_base_prepared\",\"metallic_base_apply_tick\",\"metallic_base_not_visible\"]";
                return response_json(false,
                                     "metallic_base_not_visible",
                                     metallic_stats.server_sent,
                                     1,
                                     "metallic base dispatch completed but required visible local echo/export observation failed",
                                     metadata);
            }
            if (after_metallic0.ok)
            {
                before0 = after_metallic0;
            }
            if (after_metallic1.ok)
            {
                before1 = after_metallic1;
            }
            if (after_metallic2.ok)
            {
                before2 = after_metallic2;
            }
            bool metallic_base_settled = false;
            bool metallic_base_changed_during_settle = false;
            int metallic_base_settle_checks = 0;
            auto settled_metallic0 = before0;
            auto settled_metallic1 = before1;
            auto settled_metallic2 = before2;
            for (int settle_index = 0; settle_index < 8; ++settle_index)
            {
                Sleep(250);
                auto check0 = sdk_export_channel_bytes(ref, ctx, 0);
                auto check1 = sdk_export_channel_bytes(ref, ctx, 1);
                auto check2 = sdk_export_channel_bytes(ref, ctx, 2);
                ++metallic_base_settle_checks;
                const bool same_as_previous = check0.ok && check1.ok && check2.ok &&
                                              settled_metallic0.ok && settled_metallic1.ok && settled_metallic2.ok &&
                                              check0.hash == settled_metallic0.hash &&
                                              check1.hash == settled_metallic1.hash &&
                                              check2.hash == settled_metallic2.hash;
                if (check0.ok && check1.ok && check2.ok)
                {
                    metallic_base_changed_during_settle = metallic_base_changed_during_settle ||
                                                         check0.hash != settled_metallic0.hash ||
                                                         check1.hash != settled_metallic1.hash ||
                                                         check2.hash != settled_metallic2.hash;
                    settled_metallic0 = check0;
                    settled_metallic1 = check1;
                    settled_metallic2 = check2;
                }
                if (same_as_previous)
                {
                    metallic_base_settled = true;
                    break;
                }
            }
            if (settled_metallic0.ok)
            {
                before0 = settled_metallic0;
            }
            if (settled_metallic1.ok)
            {
                before1 = settled_metallic1;
            }
            if (settled_metallic2.ok)
            {
                before2 = settled_metallic2;
            }
            metadata += sdk_channel_metadata("settled_metallic_albedo", settled_metallic0) +
                        sdk_channel_metadata("settled_metallic_metallic", settled_metallic1) +
                        sdk_channel_metadata("settled_metallic_roughness", settled_metallic2) +
                        ",\"metallic_base_settle_checks\":" + std::to_string(metallic_base_settle_checks) +
                        ",\"metallic_base_settled_before_front\":" + json_bool(metallic_base_settled) +
                        ",\"metallic_base_changed_during_settle\":" + json_bool(metallic_base_changed_during_settle);
            emit_progress("metallic_base_visible", "metallic base visible path completed", 2, 9, elapsed_now_ms(),
                          "\"server_sent\":" + std::to_string(metallic_stats.server_sent) +
                          ",\"local_echo_strokes\":" + std::to_string(metallic_stats.client_mirror_sent));
            emit_progress("metallic_base_done", "metallic base replicated+local echo completed", 2, 9, elapsed_now_ms(),
                          "\"server_sent\":" + std::to_string(metallic_stats.server_sent) +
                          ",\"local_echo_strokes\":" + std::to_string(metallic_stats.client_mirror_sent));
            paint_api_probe_selected = "skipped_disabled_metallic_stream";
            atlas_target_channel = meccha_sdk::EPaintChannel::Albedo;
            atlas_use_world_position = false;
            metadata += ",\"paint_api_probe_skipped\":true";
        }
        if (disabled_full_stream)
        {
            struct PaintApiProbeAttempt
            {
                const char* name;
                meccha_sdk::EPaintChannel target_channel;
                bool use_world_position;
                double r;
                double g;
                double b;
            };
            const PaintApiProbeAttempt attempts[]{
                {"uv_albedo", meccha_sdk::EPaintChannel::Albedo, false, 1.0, 0.0, 1.0},
                {"world_albedo", meccha_sdk::EPaintChannel::Albedo, true, 0.0, 1.0, 1.0},
                {"world_amr", meccha_sdk::EPaintChannel::AlbedoMetallicRoughness, true, 1.0, 1.0, 0.0},
            };
            bool any_probe_sent = false;
            int probe_server_sent = 0;
            int probe_server_failed = 0;
            for (const auto& attempt : attempts)
            {
                SdkReplicatedStats probe_stats{};
                auto probe_channel = sdk_make_channel(attempt.r,
                                                      attempt.g,
                                                      attempt.b,
                                                      0.0,
                                                      0.65,
                                                      meccha_sdk::EPaintChannelApplyMode::Override);
                auto probe_brush = sdk_copy_current_brush(ctx, 0.006);
                std::vector<meccha_sdk::FPaintStroke> probe_strokes{};
                if (attempt.use_world_position)
                {
                    probe_strokes.push_back(sdk_make_stroke(0.5,
                                                            0.5,
                                                            probe_channel,
                                                            probe_brush,
                                                            attempt.target_channel,
                                                            ctx.body_world_position));
                }
                else
                {
                    probe_strokes.push_back(sdk_make_uv_stroke(0.5,
                                                               0.5,
                                                               probe_channel,
                                                               probe_brush,
                                                               attempt.target_channel));
                }
                const bool probe_sent = sdk_dispatch_replicated_strokes(ctx, probe_strokes, probe_stats);
                Sleep(160);
                auto probe_after0 = sdk_export_channel_bytes(ref, ctx, 0);
                const bool probe_hash_changed = probe_after0.ok && probe_after0.hash != before0.hash;
                any_probe_sent = any_probe_sent || probe_sent;
                probe_server_sent += probe_stats.server_sent;
                probe_server_failed += probe_stats.server_failed;
                const std::string probe_prefix = std::string("paint_api_probe_") + attempt.name;
                metadata += sdk_stats_metadata(probe_prefix.c_str(), probe_stats);
                metadata += ",\"" + probe_prefix + "_sent\":" + json_bool(probe_sent) +
                            ",\"" + probe_prefix + "_hash_changed\":" + json_bool(probe_hash_changed) +
                            ",\"" + probe_prefix + "_after_hash\":\"" + std::to_string(probe_after0.hash) + "\"";
                if (probe_sent && probe_hash_changed)
                {
                    paint_api_probe_selected = attempt.name;
                    atlas_target_channel = attempt.target_channel;
                    atlas_use_world_position = attempt.use_world_position;
                    before0 = probe_after0;
                    break;
                }
            }
            metadata += std::string(",\"paint_api_probe_sent\":") + json_bool(any_probe_sent) +
                        ",\"paint_api_probe_hash_changed\":" + json_bool(!paint_api_probe_selected.empty()) +
                        ",\"paint_api_probe_selected\":\"" + json_escape(paint_api_probe_selected) + "\"" +
                        ",\"paint_api_probe_server_sent\":" + std::to_string(probe_server_sent) +
                        ",\"paint_api_probe_server_failed\":" + std::to_string(probe_server_failed) +
                        ",\"atlas_use_world_position\":" + json_bool(atlas_use_world_position) +
                        ",\"atlas_target_channel\":\"" + std::string(atlas_target_channel == meccha_sdk::EPaintChannel::Albedo ? "Albedo" : "AlbedoMetallicRoughness") + "\"";
            if (!any_probe_sent)
            {
                emit_progress("paint_api_probe_failed", "all ServerPaintBatch probe dispatches failed", 1, 8, elapsed_now_ms());
                metadata += ",\"bridge_events\":[\"replicated_api_ready\",\"paint_api_probe_failed\"]";
                return response_json(false,
                                     "paint_api_probe_failed",
                                     probe_server_sent,
                                     std::max(1, probe_server_failed),
                                     "all ServerPaintBatch probe dispatches failed",
                                     metadata);
            }
            if (paint_api_probe_selected.empty())
            {
                paint_api_probe_selected = "visual_unverified_world_albedo";
                atlas_target_channel = meccha_sdk::EPaintChannel::Albedo;
                atlas_use_world_position = true;
                metadata += std::string(",\"paint_api_probe_observation_mismatch\":true") +
                            ",\"paint_api_probe_note\":\"ServerPaintBatch probes were dispatched but ExportChannelToBytes did not observe albedo hash changes; continuing with world-position albedo stream because visual probe may still draw\"";
                emit_progress("paint_api_probe_unverified", "ServerPaintBatch probes sent; export hash unchanged; continuing with visual-unverified stream", 1, 8, elapsed_now_ms());
            }
            metadata += std::string(",\"paint_api_probe_selected_final\":\"") + json_escape(paint_api_probe_selected) + "\"" +
                        ",\"atlas_use_world_position_final\":" + json_bool(atlas_use_world_position) +
                        ",\"atlas_target_channel_final\":\"" + std::string(atlas_target_channel == meccha_sdk::EPaintChannel::Albedo ? "Albedo" : "AlbedoMetallicRoughness") + "\"";
            emit_progress("paint_api_probe_done", "ServerPaintBatch probe route selected", 1, 8, elapsed_now_ms(),
                                  "\"paint_api_probe_selected\":\"" + json_escape(paint_api_probe_selected) + "\"");
        }

        SdkNativeFrontSampleResult native_front{};
        SdkFrontCaptureProbe front_capture{};
        SdkReplicatedStats atlas_stats{};
        const std::string sampling_started_stage = static_hybrid_probe ? "hybrid_front_sampling_started" : (front_paint_route ? "front_sampling_started" : "atlas_sampling_started");
        const std::string sampling_done_stage = static_hybrid_probe ? "hybrid_front_sampling_done" : (front_paint_route ? "front_sampling_done" : "atlas_sampling_done");
        const std::string capture_started_stage = static_hybrid_probe ? "hybrid_front_capture_started" : (front_paint_route ? "front_capture_started" : "atlas_capture_started");
        const std::string capture_done_stage = static_hybrid_probe ? "hybrid_front_capture_done" : (front_paint_route ? "front_capture_done" : "atlas_capture_done");
        const std::string atlas_assembled_stage = static_hybrid_probe ? "hybrid_atlas_assembled" : (front_paint_route ? "front_atlas_assembled" : "atlas_assembled");
        const std::string strokes_generated_stage = front_paint_route ? "front_strokes_generated" : "atlas_strokes_generated";
        const std::string stroke_budget_stage = front_paint_route ? "front_stroke_budget_exceeded" : "atlas_stroke_budget_exceeded";
        std::string bridge_events = static_hybrid_probe
                                        ? "\"bridge_events\":[\"static_hybrid_probe_ready\""
                                        : (front_texture_import
                                        ? "\"bridge_events\":[\"texture_import_api_ready\""
                                        : "\"bridge_events\":[\"replicated_api_ready\"");
        if (disabled_metallic_stream)
        {
            bridge_events += ",\"metallic_base_prepared\",\"metallic_base_apply_tick\",\"metallic_base_visible\",\"metallic_base_done\"";
        }
        else if (disabled_full_stream)
        {
            bridge_events += ",\"paint_api_probe_done\"";
        }
        bridge_events += ",\"" + sampling_started_stage + "\"";
        if (!kEnableNativeSceneCaptureForF10)
        {
            emit_progress(front_paint_route ? "front_capture_unavailable" : "atlas_capture_unavailable", "native capture backend is disabled", 1, 8, elapsed_now_ms());
            metadata += std::string(",\"front_capture_backend_enabled\":false") +
                        ",\"front_capture_failure\":\"front_capture_backend_disabled_after_d3d12_crash\"" +
                        ",\"stroke_count\":0" +
                        "," + bridge_events + ",\"atlas_capture_unavailable\"]";
            return response_json(false,
                                 "atlas_capture_unavailable",
                                 0,
                                 1,
                                 "native SceneCapture2D/CreateRenderTarget2D backend disabled; no paint was dispatched",
                                 metadata);
        }
        emit_progress(sampling_started_stage, "collecting native front surface samples", front_paint_route ? 3 : 2, front_paint_route ? 9 : 8, elapsed_now_ms());
        native_front = sdk_collect_native_front_samples(ref, ctx, {});
        const auto front_bbox_width_px = std::max(0.0, (native_front.bbox_max_nx - native_front.bbox_min_nx) * static_cast<double>(native_front.viewport_width));
        const auto front_bbox_height_px = std::max(0.0, (native_front.bbox_max_ny - native_front.bbox_min_ny) * static_cast<double>(native_front.viewport_height));
        const auto front_bbox_area_px = front_bbox_width_px * front_bbox_height_px;
        metadata += sdk_native_front_metadata(native_front) +
                    ",\"front_bbox\":\"" + std::to_string(native_front.bbox_min_nx) + "," + std::to_string(native_front.bbox_min_ny) +
                    "-" + std::to_string(native_front.bbox_max_nx) + "," + std::to_string(native_front.bbox_max_ny) + "\"" +
                    ",\"front_bbox_width_px\":" + std::to_string(front_bbox_width_px) +
                    ",\"front_bbox_height_px\":" + std::to_string(front_bbox_height_px) +
                    ",\"front_bbox_area_px\":" + std::to_string(front_bbox_area_px) +
                    ",\"viewport\":\"" + std::to_string(native_front.viewport_width) + "x" + std::to_string(native_front.viewport_height) + "\"" +
                    ",\"capture_fov_hint\":\"deproject_horizontal\"";
        if (native_front.failure == "front_sampling_timeout_before_bridge_timeout")
        {
            emit_progress("front_sampling_timeout_before_bridge_timeout", "front sampling hit native time budget", front_paint_route ? 3 : 2, front_paint_route ? 9 : 8, elapsed_now_ms(),
                          "\"front_hits\":" + std::to_string(native_front.samples.size()) +
                              ",\"deproject_calls\":" + std::to_string(native_front.deproject_calls) +
                              ",\"hit_test_calls\":" + std::to_string(native_front.hit_test_calls) +
                              ",\"sampling_backend\":\"" + json_escape(native_front.sampling_backend) + "\"");
            metadata += std::string(",\"stroke_count\":0") +
                        "," + bridge_events + ",\"front_sampling_timeout_before_bridge_timeout\"]";
            return response_json(false,
                                 "front_sampling_timeout_before_bridge_timeout",
                                 0,
                                 1,
                                 "front sampling exceeded native time budget before bridge timeout; no import/paint was dispatched",
                                 metadata);
        }
        if (static_cast<int>(native_front.samples.size()) < native_front.min_front_hits)
        {
            emit_progress("atlas_sampling_insufficient", "not enough valid surface hits", front_paint_route ? 3 : 2, front_paint_route ? 9 : 8, elapsed_now_ms(),
                                  "\"front_hits\":" + std::to_string(native_front.samples.size()));
            metadata += std::string(",\"stroke_count\":0") +
                        "," + bridge_events + ",\"atlas_sampling_insufficient\"]";
            return response_json(false,
                                 "atlas_sampling_insufficient",
                                 0,
                                 1,
                                 "native surface samples are insufficient; no paint was dispatched",
                                 metadata);
        }

        bridge_events += ",\"" + sampling_done_stage + "\",\"" + capture_started_stage + "\"";
        emit_progress(sampling_done_stage, "native front surface sampling completed", front_paint_route ? 4 : 3, front_paint_route ? 9 : 8, elapsed_now_ms(),
                              "\"front_hits\":" + std::to_string(native_front.samples.size()));
        front_capture = sdk_probe_front_capture_backend(ref, ctx, native_front);
        emit_progress(capture_started_stage, "capturing hidden/background colors", front_paint_route ? 5 : 4, front_paint_route ? 9 : 8, elapsed_now_ms(),
                              "\"front_hits\":" + std::to_string(native_front.samples.size()));
        metadata += sdk_front_capture_metadata(front_capture);
        auto captured_front = sdk_capture_front_colors(ref, ctx, native_front, before0.width, before0.height);
        metadata += sdk_capture_metadata(captured_front) +
                    ",\"front_capture_resolution_source\":\"" + json_escape(captured_front.capture_resolution_source) + "\"" +
                    ",\"front_capture_target_width\":" + std::to_string(before0.width) +
                    ",\"front_capture_target_height\":" + std::to_string(before0.height);
        if (!captured_front.ok)
        {
            const std::string failure_stage = captured_front.failure == "front_texture_bulk_calibration_unavailable"
                                                  ? "front_texture_bulk_calibration_unavailable"
                                                  : (front_paint_route ? "front_capture_unavailable" : "atlas_capture_unavailable");
            emit_progress(failure_stage, "front capture/readback failed", front_paint_route ? 5 : 4, front_paint_route ? 9 : 8, elapsed_now_ms());
            metadata += std::string(",\"stroke_count\":0") +
                        "," + bridge_events + ",\"" + failure_stage + "\"]";
            return response_json(false,
                                 failure_stage.c_str(),
                                 0,
                                 1,
                                 "front capture/readback failed; no paint was dispatched: " + captured_front.failure,
                                 metadata);
        }
        bridge_events += ",\"" + capture_done_stage + "\"";
        emit_progress(capture_done_stage, "front capture/readback completed", front_paint_route ? 6 : 5, front_paint_route ? 9 : 8, elapsed_now_ms(),
                              "\"front_hits\":" + std::to_string(captured_front.samples.size()));

        if (disabled_sample_stream)
        {
            constexpr int sample_stroke_cap = 120000;
            std::vector<meccha_sdk::FPaintStroke> sample_strokes{};
            sample_strokes.reserve(captured_front.samples.size());
            for (const auto& sample : captured_front.samples)
            {
                if (static_cast<int>(sample_strokes.size()) >= sample_stroke_cap)
                {
                    break;
                }
                auto brush = sdk_copy_current_brush(ctx, std::max(0.0015, std::min(0.0060, sample.radius)));
                const auto channel = sdk_make_channel(clamp01(sample.r),
                                                      clamp01(sample.g),
                                                      clamp01(sample.b),
                                                      0.0,
                                                      0.65,
                                                      meccha_sdk::EPaintChannelApplyMode::Override);
                sample_strokes.push_back(sdk_make_uv_stroke(clamp01(sample.u),
                                                            clamp01(sample.v),
                                                            channel,
                                                            brush,
                                                            meccha_sdk::EPaintChannel::Albedo));
            }
            metadata += std::string(",\"front_sample_paint_source\":\"bulk_capture_front_samples_no_texture_atlas\"") +
                        ",\"front_sample_paint_target_channel\":\"Albedo\"" +
                        ",\"front_sample_paint_metallic_overwrite\":false" +
                        ",\"front_sample_paint_roughness_overwrite\":false" +
                        ",\"front_sample_paint_texture_import_used\":false" +
                        ",\"front_sample_paint_atlas_assembly_used\":false" +
                        ",\"front_sample_paint_requested_strokes\":" + std::to_string(sample_strokes.size()) +
                        ",\"front_sample_paint_stroke_cap\":" + std::to_string(sample_stroke_cap);
            if (sample_strokes.empty() || static_cast<int>(captured_front.samples.size()) > sample_stroke_cap)
            {
                const char* failure_stage = sample_strokes.empty() ? "front_sample_paint_no_strokes" : "front_sample_paint_budget_exceeded";
                emit_progress(failure_stage, "front sample paint stream cannot dispatch", 8, 9, elapsed_now_ms(),
                              "\"sample_strokes\":" + std::to_string(sample_strokes.size()) +
                                  ",\"stroke_cap\":" + std::to_string(sample_stroke_cap));
                metadata += "," + bridge_events + ",\"" + failure_stage + "\"]";
                return response_json(false,
                                     failure_stage,
                                     0,
                                     1,
                                     "front sample paint stream did not dispatch; no texture/import fallback was used",
                                     metadata);
            }

            SdkReplicatedStats sample_stats{};
            bridge_events += ",\"front_sample_strokes_generated\",\"replicated_stream_started\"";
            emit_progress("front_sample_strokes_generated", "front sample paint strokes generated", 8, 9, elapsed_now_ms(),
                          "\"stroke_count\":" + std::to_string(sample_strokes.size()));
            emit_progress("replicated_stream_started", "dispatching front sample ServerPaintBatch stream", 8, 9, elapsed_now_ms(),
                          "\"stroke_count\":" + std::to_string(sample_strokes.size()));

            const int raw_batch_limit = safe_read<int>(ctx.component + meccha_sdk::FieldOffsets::RuntimePaintable_MaxReplicatedPaintStrokesPerTick, 24);
            const int batch_limit = std::max(1, std::min(raw_batch_limit > 0 ? raw_batch_limit : 24, 256));
            const int server_batches_total = static_cast<int>((sample_strokes.size() + static_cast<std::size_t>(batch_limit) - 1) / static_cast<std::size_t>(batch_limit));
            int last_stream_percent = -1;
            const auto stream_started = std::chrono::steady_clock::now();
            auto stream_progress = [&](const SdkReplicatedStats& stats, int server_sent, int total) -> bool {
                const int percent = total > 0 ? std::max(0, std::min(100, static_cast<int>((static_cast<long long>(server_sent) * 100LL) / total))) : 100;
                if (percent != last_stream_percent)
                {
                    last_stream_percent = percent;
                    const auto elapsed_ms = elapsed_now_ms();
                    const auto stream_elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stream_started).count();
                    const double eta_ms = percent > 0 ? std::max(0.0, (stream_elapsed_ms / (static_cast<double>(percent) / 100.0)) - stream_elapsed_ms) : 0.0;
                    emit_progress("replicated_stream_progress",
                                  "dispatching front sample ServerPaintBatch stream",
                                  percent,
                                  100,
                                  elapsed_ms,
                                  "\"server_batches\":" + std::to_string(server_batches_total) +
                                      ",\"server_batch_index\":" + std::to_string(stats.batch_calls) +
                                      ",\"server_sent\":" + std::to_string(server_sent) +
                                      ",\"stroke_count\":" + std::to_string(total) +
                                      ",\"eta_ms\":" + std::to_string(eta_ms));
                }
                return elapsed_now_ms() < 210000.0;
            };
            if (!sdk_dispatch_replicated_strokes_with_progress(ctx, sample_strokes, sample_stats, stream_progress))
            {
                const std::string failure_stage = sample_stats.first_failure == "server_stream_timeout_before_bridge_timeout" ?
                    "server_stream_timeout_before_bridge_timeout" :
                    "server_batch_failed";
                emit_progress(failure_stage, "front sample ServerPaintBatch stream failed", 8, 9, elapsed_now_ms(),
                              "\"server_sent\":" + std::to_string(sample_stats.server_sent) +
                                  ",\"server_failed\":" + std::to_string(sample_stats.server_failed));
                metadata += sdk_stats_metadata("front_sample_paint", sample_stats) +
                            "," + bridge_events + ",\"" + failure_stage + "\"]";
                return response_json(false,
                                     failure_stage.c_str(),
                                     sample_stats.server_sent,
                                     std::max(1, sample_stats.server_failed),
                                     "front sample paint ServerPaintBatch failed: " + sample_stats.first_failure,
                                     metadata);
            }

            bridge_events += ",\"replicated_stream_done\"";
            emit_progress("replicated_stream_done", "front sample ServerPaintBatch stream dispatched", 9, 9, elapsed_now_ms(),
                          "\"server_sent\":" + std::to_string(sample_stats.server_sent));
            Sleep(100);
            auto after0 = sdk_export_channel_bytes(ref, ctx, 0);
            const bool hash_changed = after0.ok && after0.hash != before0.hash;
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            metadata += sdk_stats_metadata("front_sample_paint", sample_stats) +
                        sdk_channel_metadata("after_albedo", after0) +
                        ",\"hash_before\":\"" + std::to_string(before0.hash) + "\"" +
                        ",\"hash_after\":\"" + std::to_string(after0.hash) + "\"" +
                        ",\"hash_changed\":" + json_bool(hash_changed) +
                        ",\"server_rpc\":\"ServerPaintBatch\"" +
                        ",\"server_batches\":" + std::to_string(sample_stats.batch_calls) +
                        ",\"server_sent\":" + std::to_string(sample_stats.server_sent) +
                        ",\"server_failed\":" + std::to_string(sample_stats.server_failed) +
                        ",\"elapsed_ms\":" + std::to_string(elapsed);
            metadata += "," + bridge_events + ",\"paint_done\"]";
            return response_json(true,
                                 "paint_done",
                                 sample_stats.server_sent,
                                 0,
                                 "front sample paint stream dispatched without texture import",
                                 metadata);
        }

        if (front_texture_import)
        {
            int material_samples = 0;
            int material_valid = 0;
            int mirror_suspect = 0;
            double metallic_min = 1.0;
            double metallic_max = 0.0;
            double metallic_sum = 0.0;
            double source_metallic_min = 1.0;
            double source_metallic_max = 0.0;
            double source_metallic_sum = 0.0;
            double roughness_min = 1.0;
            double roughness_max = 0.0;
            double roughness_sum = 0.0;
            for (auto& sample : captured_front.samples)
            {
                ++material_samples;
                const auto source_metallic_value = clamp01(sample.metallic);
                const auto metallic_value = sample.metallic;
                const auto roughness_value = sample.roughness;
                ++material_valid;
                if (metallic_value >= 0.95 && roughness_value <= 0.05)
                {
                    ++mirror_suspect;
                }
                source_metallic_min = std::min(source_metallic_min, source_metallic_value);
                source_metallic_max = std::max(source_metallic_max, source_metallic_value);
                source_metallic_sum += source_metallic_value;
                metallic_min = std::min(metallic_min, metallic_value);
                metallic_max = std::max(metallic_max, metallic_value);
                metallic_sum += metallic_value;
                roughness_min = std::min(roughness_min, roughness_value);
                roughness_max = std::max(roughness_max, roughness_value);
                roughness_sum += roughness_value;
            }
            const auto denom = static_cast<double>(std::max(1, material_samples));
            metadata += std::string(",\"front_albedo_source\":\"bulk_calibrated_srgb_raw_exact\"") +
                        ",\"front_material_source\":\"preserve_existing_channels_no_material_import\"" +
                        ",\"front_material_trace_evidence_ported\":false" +
                        ",\"front_material_compensation_passes\":0" +
                        ",\"front_material_compensation_backend\":\"disabled_until_trace_evidence_ported\"" +
                        ",\"front_material_channels_imported\":false" +
                        ",\"front_color_sanitize_used\":false" +
                        ",\"front_color_lift_used\":false" +
                        ",\"front_color_raw_srgb_preserved\":true" +
                        ",\"front_material_metallic_override\":\"none\"" +
                        ",\"front_material_samples\":" + std::to_string(material_samples) +
                        ",\"front_material_valid\":" + std::to_string(material_valid) +
                        ",\"front_material_mirror_suspect\":" + json_bool(material_samples > 0 && mirror_suspect * 4 >= material_samples * 3) +
                        ",\"front_material_mirror_suspect_samples\":" + std::to_string(mirror_suspect) +
                        ",\"front_source_metallic_min\":" + std::to_string(material_samples > 0 ? source_metallic_min : 0.0) +
                        ",\"front_source_metallic_avg\":" + std::to_string(source_metallic_sum / denom) +
                        ",\"front_source_metallic_max\":" + std::to_string(material_samples > 0 ? source_metallic_max : 0.0) +
                        ",\"front_metallic_min\":" + std::to_string(material_samples > 0 ? metallic_min : 0.0) +
                        ",\"front_metallic_avg\":" + std::to_string(metallic_sum / denom) +
                        ",\"front_metallic_max\":" + std::to_string(material_samples > 0 ? metallic_max : 0.0) +
                        ",\"front_roughness_min\":" + std::to_string(material_samples > 0 ? roughness_min : 0.0) +
                        ",\"front_roughness_avg\":" + std::to_string(roughness_sum / denom) +
                        ",\"front_roughness_max\":" + std::to_string(material_samples > 0 ? roughness_max : 0.0);
        }

        SdkAtlasSideBackResult side_back{};
        std::vector<FrontSample> atlas_samples = captured_front.samples;
        const bool mirror_side_back_for_texture = false;
        const bool collect_side_back_for_texture = texture_sync_probe;
        const bool collect_static_hybrid_side = static_hybrid_probe;
        if (mirror_side_back_for_texture)
        {
            side_back = sdk_build_front_uv_mirror_side_back_samples(captured_front.samples, before0.width, before0.height);
            metadata += sdk_side_back_metadata(side_back) +
                        ",\"side_back_query_skipped\":true";
            bridge_events += ",\"atlas_side_back_done\"";
            emit_progress("atlas_side_back_done", "side/back UV mirror extension completed", 6, 8, elapsed_now_ms(),
                                  "\"side_back_hits\":" + std::to_string(side_back.samples.size()) +
                                      ",\"side_back_backend\":\"front_uv_mirror_fast\"");
            atlas_samples.insert(atlas_samples.end(), side_back.samples.begin(), side_back.samples.end());
        }
        else if (collect_side_back_for_texture)
        {
            side_back = sdk_collect_atlas_side_back_samples(ref, ctx, native_front, captured_front.samples, before0.width, before0.height);
            constexpr bool kEnableUnsafeFrontTextureReuseForBack = false;
            if (kEnableUnsafeFrontTextureReuseForBack && texture_sync_probe && side_back.back_hits == 0)
            {
                auto front_reuse = sdk_build_front_uv_mirror_side_back_samples(captured_front.samples, before0.width, before0.height);
                side_back.front_reuse_used = !front_reuse.samples.empty();
                side_back.front_reuse_samples = static_cast<int>(front_reuse.samples.size());
                for (auto& sample : front_reuse.samples)
                {
                    sample.atlas_priority = std::max(sample.atlas_priority, sample.floor_like ? 9 : 8);
                    sample.atlas_radius = std::max(sample.atlas_radius, 4);
                    sample.atlas_weight = std::max(sample.atlas_weight, sample.floor_like ? 58.0 : 50.0);
                    side_back.samples.push_back(sample);
                }
                side_back.back_hits += side_back.front_reuse_samples;
                if (side_back.front_reuse_used && (side_back.failure == "side_back_unavailable" ||
                                                   side_back.failure == "side_back_query_no_hit" ||
                                                   side_back.failure == "side_back_time_budget_exhausted_no_samples" ||
                                                   side_back.failure == "side_back_attempt_budget_exhausted_no_samples"))
                {
                    side_back.failure = "ok_front_texture_reuse_for_back";
                }
            }
            metadata += sdk_side_back_metadata(side_back) +
                        ",\"side_back_query_skipped\":false";
            constexpr int min_side_back_hits = 64;
            if (!diagnostic_import && static_cast<int>(side_back.samples.size()) < min_side_back_hits)
            {
                emit_progress("atlas_side_back_insufficient", "side/back samples are insufficient", 5, 8, elapsed_now_ms(),
                                      "\"side_back_hits\":" + std::to_string(side_back.samples.size()));
                metadata += ",\"front_hits\":" + std::to_string(captured_front.samples.size()) +
                            ",\"stroke_count\":0" +
                            "," + bridge_events + ",\"atlas_side_back_insufficient\"]";
                return response_json(false,
                                     "atlas_side_back_insufficient",
                                     0,
                                     1,
                                     "side/back samples are insufficient; no paint was dispatched",
                                     metadata);
            }
            bridge_events += ",\"atlas_side_back_done\"";
            emit_progress("atlas_side_back_done", "side/back sample extension completed", 6, 8, elapsed_now_ms(),
                                  "\"side_back_hits\":" + std::to_string(side_back.samples.size()) +
                                      ",\"side_back_backend\":\"screen_space_brush_query_virtual_views_dense_small_splat\"");
            atlas_samples.insert(atlas_samples.end(), side_back.samples.begin(), side_back.samples.end());
        }
        else if (collect_static_hybrid_side)
        {
            side_back = sdk_collect_atlas_side_back_samples(ref, ctx, native_front, captured_front.samples, before0.width, before0.height, true);
            metadata += sdk_side_back_metadata(side_back) +
                        ",\"static_hybrid_side_only\":true" +
                        ",\"static_hybrid_back_skipped\":true" +
                        ",\"static_hybrid_side_color_source\":\"front_nearest_proxy_until_inverse_matte_capture\"";
            bridge_events += ",\"hybrid_side_sampling_done\"";
            emit_progress("hybrid_side_sampling_done", "static hybrid side samples collected", 6, 8, elapsed_now_ms(),
                              "\"front_hits\":" + std::to_string(captured_front.samples.size()) +
                                  ",\"side_hits\":" + std::to_string(side_back.side_hits) +
                                  ",\"back_hits\":0" +
                                  ",\"side_attempts\":" + std::to_string(side_back.attempts));
            atlas_samples.insert(atlas_samples.end(), side_back.samples.begin(), side_back.samples.end());
        }
        else
        {
            metadata += std::string(",\"side_back_attempts\":0,\"side_back_success\":0,\"side_back_owner_hits\":0,\"side_back_uv_hits\":0") +
                        ",\"side_hits\":0,\"back_hits\":0,\"side_back_duplicate_texels\":0,\"side_back_nearest_sources\":0" +
                        ",\"side_back_failure\":\"" + std::string(color_transfer_probe ? "skipped_color_transfer_probe" :
                                                                  (front_texture_import ? "skipped_front_texture_route" : "skipped_front_only_route")) + "\"";
        }

        auto make_filled_channel = [](const ChannelBuffer& source, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            auto bytes = source.bytes;
            if (bytes.empty())
            {
                return bytes;
            }
            if (source.bytes_per_pixel >= 4 || bytes.size() % 4 == 0)
            {
                for (std::size_t offset = 0; offset + 3 < bytes.size(); offset += 4)
                {
                    bytes[offset + 0] = r;
                    bytes[offset + 1] = g;
                    bytes[offset + 2] = b;
                    bytes[offset + 3] = 255;
                }
            }
            else
            {
                std::fill(bytes.begin(), bytes.end(), r);
            }
            return bytes;
        };
        ChannelBuffer atlas_base_albedo = before0;
        ChannelBuffer atlas_base_metallic = before1;
        ChannelBuffer atlas_base_roughness = before2;
        if (front_texture_import)
        {
            atlas_base_albedo = pre_metallic_albedo;
            atlas_base_metallic = pre_metallic_metallic;
            atlas_base_roughness = pre_metallic_roughness;
        }
        if (color_transfer_probe)
        {
            atlas_base_albedo.bytes = make_filled_channel(atlas_base_albedo, 255, 0, 255);
        }
        auto atlas = sdk_assemble_texture_atlas(atlas_base_albedo, atlas_base_metallic, atlas_base_roughness, atlas_samples);
        metadata += sdk_atlas_metadata(atlas) +
                    sdk_write_atlas_debug_artifacts(atlas) +
                    ",\"atlas_base_source\":\"" + std::string(color_transfer_probe ? "magenta_unpainted_sentinel_color_transfer_probe" :
                                                              (front_texture_import ? "pre_metallic_exported_channels_preserve_unpainted_texture_diagnostic" : "current_channels")) + "\"" +
                    ",\"color_probe_unpainted_sentinel\":\"" + std::string(color_transfer_probe ? "magenta_ff00ff" : "none") + "\"" +
                    ",\"front_texture_back_metallic_white\":false" +
                    ",\"front_texture_side_back_texture_skipped\":" + json_bool(front_texture_import && !texture_sync_probe) +
                    ",\"front_texture_unpainted_source\":\"" + std::string(front_texture_import ? "pre_metallic_exported_channels" : "current_channels") + "\"" +
                    ",\"front_texture_white_base_used\":false" +
                    ",\"atlas_base_hash\":\"" + std::to_string(atlas_base_albedo.hash) + "\"" +
                    ",\"atlas_base_metallic_hash\":\"" + std::to_string(atlas_base_metallic.hash) + "\"" +
                    ",\"atlas_base_roughness_hash\":\"" + std::to_string(atlas_base_roughness.hash) + "\"" +
                    ",\"front_hits\":" + std::to_string(captured_front.samples.size()) +
                    ",\"unique_atlas_texels\":" + std::to_string(atlas.stats.direct_texels) +
                    ",\"target_unique_atlas_texels\":500000" +
                    ",\"precision_profile\":\"max_texture_probe\"" +
                    ",\"side_back_hits\":" + std::to_string(side_back.samples.size());
        if (static_hybrid_probe)
        {
            metadata += sdk_write_static_hybrid_artifacts(atlas, static_cast<int>(captured_front.samples.size()), side_back) +
                        ",\"static_hybrid_output\":\"artifacts_only\"" +
                        ",\"static_hybrid_import_used\":false" +
                        ",\"static_hybrid_texture_sync_used\":false" +
                        ",\"static_hybrid_paint_used\":false" +
                        ",\"static_hybrid_mesh_chart_available\":false" +
                        ",\"static_hybrid_mesh_chart_failure\":\"mesh_chart_unavailable\"" +
                        ",\"static_hybrid_back_applied\":false";
            bridge_events += ",\"hybrid_artifacts_done\"";
            emit_progress("hybrid_artifacts_done", "static hybrid front/side artifacts generated", 8, 8, elapsed_now_ms(),
                          "\"front_hits\":" + std::to_string(captured_front.samples.size()) +
                              ",\"side_hits\":" + std::to_string(side_back.side_hits) +
                              ",\"direct_texels\":" + std::to_string(atlas.stats.direct_texels));
            metadata += "," + bridge_events + ",\"hybrid_artifacts_done\"]";
            return response_json(true,
                                 "static_hybrid_front_side_probe_done",
                                 0,
                                 0,
                                 "static hybrid front/side artifacts generated; no paint/import/sync was applied",
                                 metadata);
        }
        if (strict_38923_front_texture_import && atlas.stats.direct_texels < 500000)
        {
            emit_progress("front_texture_quality_warning", "front texture direct coverage is below 38923 parity floor; diagnostic import will continue", front_metallic_texture_route ? 7 : 6, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                          "\"direct_texels\":" + std::to_string(atlas.stats.direct_texels) +
                              ",\"min_direct_texels\":500000");
            metadata += std::string(",\"front_texture_direct_coverage_below_parity\":true") +
                        ",\"front_texture_min_direct_texels\":500000";
        }
        if (!atlas.ok || atlas.stats.direct_texels < 2048)
        {
            emit_progress("atlas_quality_failed", "atlas assembly quality failed", front_metallic_texture_route ? 7 : 6, front_metallic_texture_route ? 9 : 8, elapsed_now_ms());
            metadata += std::string(",\"stroke_count\":0") +
                        "," + bridge_events + ",\"atlas_quality_failed\"]";
            return response_json(false,
                                 "atlas_quality_failed",
                                 0,
                                 1,
                                 "atlas assembly quality failed: " + atlas.failure,
                                 metadata);
        }
        bridge_events += ",\"" + atlas_assembled_stage + "\"";
        emit_progress(atlas_assembled_stage, "front CPU atlas assembled", front_metallic_texture_route ? 7 : 7, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                              "\"direct_texels\":" + std::to_string(atlas.stats.direct_texels) +
                                  ",\"target_unique_atlas_texels\":500000");

        if (diagnostic_import)
        {
            const std::string import_started_stage = front_texture_import ? "front_texture_import_started" : "diagnostic_import_started";
            const std::string import_done_stage = front_texture_import ? "front_texture_import_done" : "diagnostic_import_done";
            const std::string import_failed_stage = front_texture_import ? "front_texture_import_failed" : "diagnostic_import_failed";
            const std::string import_not_observed_stage = front_texture_import ? "front_texture_import_not_observed" : "diagnostic_import_not_observed";
            const bool texture_sync_api_available = ctx.relay_texture_sync_to_server_function != 0 ||
                                                    ctx.request_full_texture_sync_function != 0 ||
                                                    ctx.server_request_texture_sync_function != 0 ||
                                                    ctx.server_relay_texture_sync_function != 0;
            if (texture_sync_probe && !texture_sync_api_available)
            {
                emit_progress("texture_sync_api_unavailable", "no runtime texture sync RPC is available; local import skipped", 8, 9, elapsed_now_ms());
                metadata += std::string(",\"texture_sync_relay_available\":") + json_bool(ctx.relay_texture_sync_to_server_function != 0) +
                            ",\"texture_sync_request_full_available\":" + json_bool(ctx.request_full_texture_sync_function != 0) +
                            ",\"texture_sync_server_request_available\":" + json_bool(ctx.server_request_texture_sync_function != 0) +
                            ",\"texture_sync_server_relay_available\":" + json_bool(ctx.server_relay_texture_sync_function != 0) +
                            "," + bridge_events + ",\"texture_sync_api_unavailable\"]";
                return response_json(false,
                                     "texture_sync_api_unavailable",
                                     0,
                                     1,
                                     "runtime texture sync API is unavailable; local texture import was skipped",
                                     metadata);
            }
            bridge_events += ",\"" + import_started_stage + "\"";
            emit_progress(import_started_stage, "importing texture only for strict sync probe", front_metallic_texture_route ? 8 : 7, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                          "\"front_hits\":" + std::to_string(captured_front.samples.size()));
            std::string import_failure0{};
            std::string import_failure1{};
            std::string import_failure2{};
            enum class SdkAlbedoTransfer
            {
                IdentitySrgb,
                SrgbToLinear,
                IdentitySrgbNoCompensation,
            };
            auto transfer_label = [](SdkAlbedoTransfer transfer) -> const char* {
                switch (transfer)
                {
                case SdkAlbedoTransfer::IdentitySrgb: return "identity_srgb";
                case SdkAlbedoTransfer::SrgbToLinear: return "srgb_to_linear";
                case SdkAlbedoTransfer::IdentitySrgbNoCompensation: return "identity_srgb_no_compensation";
                }
                return "unknown";
            };
            auto make_albedo_import_bytes = [&](SdkAlbedoTransfer transfer) {
                auto bytes = atlas.albedo;
                if (transfer == SdkAlbedoTransfer::SrgbToLinear)
                {
                    auto srgb_to_linear_byte = [](std::uint8_t value) -> std::uint8_t {
                        const double srgb = static_cast<double>(value) / 255.0;
                        const double linear = srgb <= 0.04045
                                                  ? srgb / 12.92
                                                  : std::pow((srgb + 0.055) / 1.055, 2.4);
                        return static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, std::round(linear * 255.0))));
                    };
                    for (std::size_t offset = 0; offset + 3 < bytes.size(); offset += 4)
                    {
                        bytes[offset + 0] = srgb_to_linear_byte(bytes[offset + 0]);
                        bytes[offset + 1] = srgb_to_linear_byte(bytes[offset + 1]);
                        bytes[offset + 2] = srgb_to_linear_byte(bytes[offset + 2]);
                    }
                }
                return bytes;
            };
            auto write_albedo_ppm = [&](const wchar_t* filename, const std::vector<std::uint8_t>& bytes) -> bool {
                const int width = atlas.stats.width;
                const int height = atlas.stats.height;
                const std::size_t pixels = width > 0 && height > 0
                    ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                    : 0U;
                if (pixels == 0 || bytes.size() < pixels * 4U)
                {
                    return false;
                }
                std::string ppm = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
                ppm.reserve(ppm.size() + pixels * 3U);
                for (std::size_t index = 0; index < pixels; ++index)
                {
                    const auto offset = index * 4U;
                    ppm.push_back(static_cast<char>(bytes[offset + 0]));
                    ppm.push_back(static_cast<char>(bytes[offset + 1]));
                    ppm.push_back(static_cast<char>(bytes[offset + 2]));
                }
                return write_runtime_artifact_binary(filename, ppm);
            };
            const auto albedo_import_bytes = front_texture_import ? make_albedo_import_bytes(SdkAlbedoTransfer::SrgbToLinear) : atlas.albedo;
            const auto albedo_import_hash = hash_bytes(albedo_import_bytes);
            const bool import_material_channels = texture_sync_probe;
            metadata += std::string(",\"front_texture_albedo_import_byte_order\":\"") +
                        "rgba_identity" + "\"" +
                        ",\"front_texture_albedo_import_transfer\":\"" + std::string(front_texture_import ? "srgb_to_linear_for_runtime_material" : "identity") + "\"" +
                        ",\"atlas_import_albedo_hash\":\"" + std::to_string(albedo_import_hash) + "\"" +
                        ",\"atlas_preview_albedo_hash\":\"" + std::to_string(atlas.hash) + "\"" +
                        ",\"front_texture_import_material_channels\":" + json_bool(import_material_channels) +
                        ",\"front_texture_material_channel_mode\":\"preserve_existing\"";
            const bool import0_ok = sdk_import_channel_bytes(ctx, 0, albedo_import_bytes, import_failure0);
            const bool import1_ok = front_texture_import && import_material_channels ? sdk_import_channel_bytes(ctx, 1, atlas.metallic, import_failure1) : true;
            const bool import2_ok = front_texture_import && import_material_channels ? sdk_import_channel_bytes(ctx, 2, atlas.roughness, import_failure2) : true;
            Sleep(100);
            auto after0 = sdk_export_channel_bytes(ref, ctx, 0);
            auto after1 = front_texture_import ? sdk_export_channel_bytes(ref, ctx, 1) : before1;
            auto after2 = front_texture_import ? sdk_export_channel_bytes(ref, ctx, 2) : before2;
            const bool hash_changed = (after0.ok && after0.hash != before0.hash) ||
                                      (front_texture_import && import_material_channels && after1.ok && after1.hash != before1.hash) ||
                                      (front_texture_import && import_material_channels && after2.ok && after2.hash != before2.hash);
            const bool import_observed = import0_ok && after0.ok && after0.hash == albedo_import_hash &&
                                         (!front_texture_import ||
                                          (import_material_channels
                                               ? (import1_ok && import2_ok &&
                                                  after1.ok && after1.hash == atlas.metallic_hash &&
                                                  after2.ok && after2.hash == atlas.roughness_hash)
                                               : (after1.ok && after2.ok))) &&
                                         hash_changed;
            Sleep(1500);
            auto after_settle0 = sdk_export_channel_bytes(ref, ctx, 0);
            auto after_settle1 = front_texture_import ? sdk_export_channel_bytes(ref, ctx, 1) : after1;
            auto after_settle2 = front_texture_import ? sdk_export_channel_bytes(ref, ctx, 2) : after2;
            const bool import_still_observed_after_settle = import_observed &&
                                                            after_settle0.ok && after_settle0.hash == albedo_import_hash &&
                                                            (!front_texture_import ||
                                                             (import_material_channels
                                                                  ? (after_settle1.ok && after_settle1.hash == atlas.metallic_hash &&
                                                                     after_settle2.ok && after_settle2.hash == atlas.roughness_hash)
                                                                  : (after_settle1.ok && after_settle2.ok)));
            const bool import_overwritten_after_settle = import_observed &&
                                                         (after_settle0.ok && after_settle0.hash != albedo_import_hash ||
                                                          (front_texture_import && import_material_channels && after_settle1.ok && after_settle1.hash != atlas.metallic_hash) ||
                                                          (front_texture_import && import_material_channels && after_settle2.ok && after_settle2.hash != atlas.roughness_hash));
            std::string final_import_failure0{};
            std::string final_import_failure1{};
            std::string final_import_failure2{};
            const bool final_import0_ok = front_texture_import
                                              ? sdk_import_channel_bytes(ctx, 0, albedo_import_bytes, final_import_failure0)
                                              : true;
            const bool final_import1_ok = front_texture_import && import_material_channels
                                              ? sdk_import_channel_bytes(ctx, 1, atlas.metallic, final_import_failure1)
                                              : true;
            const bool final_import2_ok = front_texture_import && import_material_channels
                                              ? sdk_import_channel_bytes(ctx, 2, atlas.roughness, final_import_failure2)
                                              : true;
            const bool final_import_ok = final_import0_ok && final_import1_ok && final_import2_ok;
            Sleep(front_texture_import ? 120 : 0);
            auto after_final_import0 = front_texture_import ? sdk_export_channel_bytes(ref, ctx, 0) : after_settle0;
            auto after_final_import1 = front_texture_import ? sdk_export_channel_bytes(ref, ctx, 1) : after_settle1;
            auto after_final_import2 = front_texture_import ? sdk_export_channel_bytes(ref, ctx, 2) : after_settle2;
            const bool final_import_observed = final_import_ok &&
                                               after_final_import0.ok && after_final_import0.hash == albedo_import_hash &&
                                               (!front_texture_import ||
                                                (import_material_channels
                                                     ? (after_final_import1.ok && after_final_import1.hash == atlas.metallic_hash &&
                                                        after_final_import2.ok && after_final_import2.hash == atlas.roughness_hash)
                                                     : (after_final_import1.ok && after_final_import2.ok)));
            bool texture_sync_attempted = false;
            bool texture_sync_relay_ok = false;
            bool texture_sync_request_full_ok = false;
            bool texture_sync_server_request_ok = false;
            bool texture_sync_server_relay_ok = false;
            std::string texture_sync_relay_failure{"not_run"};
            std::string texture_sync_request_full_failure{"not_run"};
            std::string texture_sync_server_request_failure{"not_run"};
            std::string texture_sync_server_relay_failure{"not_run"};
            std::string texture_sync_success_candidate{};
            ChannelBuffer after_texture_sync0{};
            ChannelBuffer after_texture_sync1{};
            ChannelBuffer after_texture_sync2{};
            if (texture_sync_probe && final_import_observed)
            {
                texture_sync_attempted = true;
                bridge_events += ",\"front_texture_import_done_local_only\"";
                emit_progress("front_texture_import_done_local_only",
                              "front texture import observed locally before sync",
                              front_metallic_texture_route ? 9 : 8,
                              front_metallic_texture_route ? 9 : 8,
                              elapsed_now_ms(),
                              "\"pre_sync_albedo_hash\":\"" + std::to_string(after_final_import0.hash) + "\"");
                bridge_events += ",\"texture_sync_started\"";
                emit_progress("texture_sync_started", "dispatching runtime texture sync RPC candidates", front_metallic_texture_route ? 9 : 8, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                              "\"front_hits\":" + std::to_string(captured_front.samples.size()));
                texture_sync_relay_ok = sdk_call_relay_texture_sync_rpc(ctx,
                                                                        ctx.relay_texture_sync_to_server_function,
                                                                        "RelayTextureSyncToServer_unavailable",
                                                                        texture_sync_relay_failure);
                if (texture_sync_relay_ok && texture_sync_success_candidate.empty())
                {
                    texture_sync_success_candidate = "RelayTextureSyncToServer";
                }
                texture_sync_request_full_ok = sdk_call_component_no_param_rpc(ctx,
                                                                              ctx.request_full_texture_sync_function,
                                                                              "RequestFullTextureSync_unavailable",
                                                                              texture_sync_request_full_failure);
                if (texture_sync_request_full_ok && texture_sync_success_candidate.empty())
                {
                    texture_sync_success_candidate = "RequestFullTextureSync";
                }
                texture_sync_server_request_ok = sdk_call_component_no_param_rpc(ctx,
                                                                                ctx.server_request_texture_sync_function,
                                                                                "ServerRequestTextureSync_unavailable",
                                                                                texture_sync_server_request_failure);
                if (texture_sync_server_request_ok && texture_sync_success_candidate.empty())
                {
                    texture_sync_success_candidate = "ServerRequestTextureSync";
                }
                texture_sync_server_relay_ok = sdk_call_relay_texture_sync_rpc(ctx,
                                                                              ctx.server_relay_texture_sync_function,
                                                                              "ServerRelayTextureSync_unavailable",
                                                                              texture_sync_server_relay_failure);
                if (texture_sync_server_relay_ok && texture_sync_success_candidate.empty())
                {
                    texture_sync_success_candidate = "ServerRelayTextureSync";
                }
                Sleep(250);
                after_texture_sync0 = sdk_export_channel_bytes(ref, ctx, 0);
                after_texture_sync1 = sdk_export_channel_bytes(ref, ctx, 1);
                after_texture_sync2 = sdk_export_channel_bytes(ref, ctx, 2);
                bridge_events += ",\"" + std::string(texture_sync_success_candidate.empty() ? "texture_sync_failed" : "texture_sync_done") + "\"";
                emit_progress(texture_sync_success_candidate.empty() ? "texture_sync_failed" : "texture_sync_done",
                              texture_sync_success_candidate.empty() ? "runtime texture sync RPC candidates failed" : "runtime texture sync RPC dispatched",
                              front_metallic_texture_route ? 9 : 8,
                              front_metallic_texture_route ? 9 : 8,
                              elapsed_now_ms(),
                              "\"texture_sync_success_candidate\":\"" + json_escape(texture_sync_success_candidate) + "\"");
            }
            const bool front_texture_quality_ok = front_texture_import &&
                                                  captured_front.bulk_readback_used &&
                                                  captured_front.image_bulk_calibration_ok &&
                                                  captured_front.texture_source == "bulk_calibrated_direct_texture";
            const std::string front_texture_quality_failure = front_texture_quality_ok
                                                                  ? std::string("")
                                                                  : std::string("bulk_calibrated_direct_texture_not_observed");
            const auto pre_sync_albedo_stats = sdk_channel_rgb_stats(after_final_import0);
            const auto post_sync_albedo_stats = sdk_channel_rgb_stats(after_texture_sync0);
            const bool texture_sync_hash_changed = texture_sync_probe && after_texture_sync0.ok &&
                                                   (after_texture_sync0.hash != after_final_import0.hash ||
                                                    after_texture_sync1.hash != after_final_import1.hash ||
                                                    after_texture_sync2.hash != after_final_import2.hash);
            const bool texture_sync_color_shift_suspected = texture_sync_probe && post_sync_albedo_stats.ok && pre_sync_albedo_stats.ok &&
                                                            (texture_sync_hash_changed ||
                                                             std::abs(post_sync_albedo_stats.avg - pre_sync_albedo_stats.avg) > 0.01 ||
                                                             std::abs(post_sync_albedo_stats.min - pre_sync_albedo_stats.min) > 0.01 ||
                                                             std::abs(post_sync_albedo_stats.max - pre_sync_albedo_stats.max) > 0.01);
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            metadata += sdk_channel_metadata("after_albedo", after0) +
                        sdk_channel_metadata("after_metallic", after1) +
                        sdk_channel_metadata("after_roughness", after2) +
                        sdk_channel_metadata("after_import_settle_albedo", after_settle0) +
                        sdk_channel_metadata("after_import_settle_metallic", after_settle1) +
                        sdk_channel_metadata("after_import_settle_roughness", after_settle2) +
                        sdk_channel_metadata("after_final_import_albedo", after_final_import0) +
                        sdk_channel_metadata("after_final_import_metallic", after_final_import1) +
                        sdk_channel_metadata("after_final_import_roughness", after_final_import2) +
                        sdk_channel_rgb_stats_metadata("pre_sync_albedo", after_final_import0) +
                        sdk_channel_rgb_stats_metadata("pre_sync_metallic", after_final_import1) +
                        sdk_channel_rgb_stats_metadata("pre_sync_roughness", after_final_import2) +
                        ",\"atlas_hash\":\"" + std::to_string(atlas.hash) + "\"" +
                        ",\"atlas_metallic_hash\":\"" + std::to_string(atlas.metallic_hash) + "\"" +
                        ",\"atlas_roughness_hash\":\"" + std::to_string(atlas.roughness_hash) + "\"" +
                        ",\"before_albedo_hash\":\"" + std::to_string(before0.hash) + "\"" +
                        ",\"before_metallic_hash\":\"" + std::to_string(before1.hash) + "\"" +
                        ",\"before_roughness_hash\":\"" + std::to_string(before2.hash) + "\"" +
                        ",\"after_albedo_hash\":\"" + std::to_string(after0.hash) + "\"" +
                        ",\"after_metallic_hash\":\"" + std::to_string(after1.hash) + "\"" +
                        ",\"after_roughness_hash\":\"" + std::to_string(after2.hash) + "\"" +
                        ",\"after_import_settle_albedo_hash\":\"" + std::to_string(after_settle0.hash) + "\"" +
                        ",\"after_import_settle_metallic_hash\":\"" + std::to_string(after_settle1.hash) + "\"" +
                        ",\"after_import_settle_roughness_hash\":\"" + std::to_string(after_settle2.hash) + "\"" +
                        ",\"after_final_import_albedo_hash\":\"" + std::to_string(after_final_import0.hash) + "\"" +
                        ",\"after_final_import_metallic_hash\":\"" + std::to_string(after_final_import1.hash) + "\"" +
                        ",\"after_final_import_roughness_hash\":\"" + std::to_string(after_final_import2.hash) + "\"" +
                        ",\"pre_sync_albedo_hash\":\"" + std::to_string(after_final_import0.hash) + "\"" +
                        ",\"pre_sync_metallic_hash\":\"" + std::to_string(after_final_import1.hash) + "\"" +
                        ",\"pre_sync_roughness_hash\":\"" + std::to_string(after_final_import2.hash) + "\"" +
                        ",\"hash_changed\":" + json_bool(hash_changed) +
                        ",\"diagnostic_import_ok\":" + json_bool(import0_ok && import1_ok && import2_ok) +
                        ",\"diagnostic_import_albedo_ok\":" + json_bool(import0_ok) +
                        ",\"diagnostic_import_metallic_ok\":" + json_bool(import1_ok) +
                        ",\"diagnostic_import_roughness_ok\":" + json_bool(import2_ok) +
                        ",\"diagnostic_import_failure\":\"" + json_escape(import_failure0) + "\"" +
                        ",\"diagnostic_import_albedo_failure\":\"" + json_escape(import_failure0) + "\"" +
                        ",\"diagnostic_import_metallic_failure\":\"" + json_escape(import_failure1) + "\"" +
                        ",\"diagnostic_import_roughness_failure\":\"" + json_escape(import_failure2) + "\"" +
                        ",\"diagnostic_import_observed\":" + json_bool(import_observed) +
                        ",\"diagnostic_import_still_observed_after_settle\":" + json_bool(import_still_observed_after_settle) +
                        ",\"diagnostic_import_overwritten_after_settle\":" + json_bool(import_overwritten_after_settle) +
                        ",\"front_texture_reimport_after_settle\":" + json_bool(front_texture_import) +
                        ",\"front_texture_final_import_ok\":" + json_bool(final_import_ok) +
                        ",\"front_texture_final_import_failure\":\"" + json_escape(final_import_failure0.empty() ? (final_import_failure1.empty() ? final_import_failure2 : final_import_failure1) : final_import_failure0) + "\"" +
                        ",\"front_texture_final_import_albedo_ok\":" + json_bool(final_import0_ok) +
                        ",\"front_texture_final_import_metallic_ok\":" + json_bool(final_import1_ok) +
                        ",\"front_texture_final_import_roughness_ok\":" + json_bool(final_import2_ok) +
                        ",\"front_texture_final_import_observed\":" + json_bool(final_import_observed) +
                        ",\"front_texture_quality_ok\":" + json_bool(front_texture_quality_ok) +
                        ",\"front_texture_quality_failure\":\"" + json_escape(front_texture_quality_failure) + "\"" +
                        sdk_channel_metadata("after_texture_sync_albedo", after_texture_sync0) +
                        sdk_channel_metadata("after_texture_sync_metallic", after_texture_sync1) +
                        sdk_channel_metadata("after_texture_sync_roughness", after_texture_sync2) +
                        sdk_channel_rgb_stats_metadata("post_sync_albedo", after_texture_sync0) +
                        sdk_channel_rgb_stats_metadata("post_sync_metallic", after_texture_sync1) +
                        sdk_channel_rgb_stats_metadata("post_sync_roughness", after_texture_sync2) +
                        ",\"texture_sync_strict_probe\":" + json_bool(texture_sync_probe) +
                        ",\"texture_sync_attempted\":" + json_bool(texture_sync_attempted) +
                        ",\"texture_sync_relay_available\":" + json_bool(ctx.relay_texture_sync_to_server_function != 0) +
                        ",\"texture_sync_request_full_available\":" + json_bool(ctx.request_full_texture_sync_function != 0) +
                        ",\"texture_sync_server_request_available\":" + json_bool(ctx.server_request_texture_sync_function != 0) +
                        ",\"texture_sync_server_relay_available\":" + json_bool(ctx.server_relay_texture_sync_function != 0) +
                        ",\"texture_sync_multicast_available\":" + json_bool(ctx.multicast_sync_channel_data_function != 0) +
                        ",\"texture_sync_compressed_multicast_available\":" + json_bool(ctx.multicast_sync_compressed_channel_data_function != 0) +
                        ",\"texture_sync_relay_ok\":" + json_bool(texture_sync_relay_ok) +
                        ",\"texture_sync_request_full_ok\":" + json_bool(texture_sync_request_full_ok) +
                        ",\"texture_sync_server_request_ok\":" + json_bool(texture_sync_server_request_ok) +
                        ",\"texture_sync_server_relay_ok\":" + json_bool(texture_sync_server_relay_ok) +
                        ",\"texture_sync_relay_failure\":\"" + json_escape(texture_sync_relay_failure) + "\"" +
                        ",\"texture_sync_request_full_failure\":\"" + json_escape(texture_sync_request_full_failure) + "\"" +
                        ",\"texture_sync_server_request_failure\":\"" + json_escape(texture_sync_server_request_failure) + "\"" +
                        ",\"texture_sync_server_relay_failure\":\"" + json_escape(texture_sync_server_relay_failure) + "\"" +
                        ",\"texture_sync_success_candidate\":\"" + json_escape(texture_sync_success_candidate) + "\"" +
                        ",\"texture_sync_after_albedo_hash\":\"" + std::to_string(after_texture_sync0.hash) + "\"" +
                        ",\"texture_sync_after_metallic_hash\":\"" + std::to_string(after_texture_sync1.hash) + "\"" +
                        ",\"texture_sync_after_roughness_hash\":\"" + std::to_string(after_texture_sync2.hash) + "\"" +
                        ",\"post_sync_albedo_hash\":\"" + std::to_string(after_texture_sync0.hash) + "\"" +
                        ",\"post_sync_metallic_hash\":\"" + std::to_string(after_texture_sync1.hash) + "\"" +
                        ",\"post_sync_roughness_hash\":\"" + std::to_string(after_texture_sync2.hash) + "\"" +
                        ",\"texture_sync_hash_changed\":" + json_bool(texture_sync_hash_changed) +
                        ",\"texture_sync_color_shift_suspected\":" + json_bool(texture_sync_color_shift_suspected) +
                        ",\"multiplayer_sync_unverified\":" + json_bool(texture_sync_probe) +
                        ",\"front_paint_stream_skipped\":" + json_bool(front_texture_import) +
                        ",\"server_rpc\":\"" + std::string(texture_sync_success_candidate.empty() ? "none" : texture_sync_success_candidate) + "\"" +
                        ",\"server_batches\":0" +
                        ",\"server_sent\":" + std::to_string(texture_sync_success_candidate.empty() ? 0 : 1) +
                        ",\"server_failed\":" + std::to_string(texture_sync_probe && texture_sync_success_candidate.empty() ? 1 : 0) +
                        ",\"stroke_count\":0" +
                        ",\"elapsed_ms\":" + std::to_string(elapsed);

            if (!(import0_ok && import1_ok && import2_ok))
            {
                metadata += "," + bridge_events + ",\"" + import_failed_stage + "\"]";
                return response_json(false,
                                     import_failed_stage.c_str(),
                                     0,
                                     1,
                                     "diagnostic texture channel import failed",
                                     metadata);
            }
            if (!import_observed)
            {
                metadata += "," + bridge_events + ",\"" + import_not_observed_stage + "\"]";
                return response_json(false,
                                     import_not_observed_stage.c_str(),
                                     0,
                                     1,
                                     "diagnostic texture import was not observed by export/hash verification",
                                     metadata);
            }
            if (!import_still_observed_after_settle)
            {
                metadata += "," + bridge_events + ",\"" + import_done_stage + "\",\"front_texture_import_overwritten_after_settle\"]";
                return response_json(false,
                                     "front_texture_import_overwritten_after_settle",
                                     1,
                                     1,
                                     "front texture import was observed immediately, but was not stable after settle",
                                     metadata);
            }
            if (!final_import_observed)
            {
                metadata += "," + bridge_events + ",\"" + import_done_stage + "\",\"front_texture_final_import_not_observed\"]";
                return response_json(false,
                                     "front_texture_final_import_not_observed",
                                     1,
                                     1,
                                     "front texture final import was not observed on all channels",
                                     metadata);
            }

            emit_progress(import_done_stage, front_texture_import ? "front texture channel import observed" : "front albedo texture import observed", front_metallic_texture_route ? 9 : 8, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                          "\"front_hits\":" + std::to_string(captured_front.samples.size()));
            if (front_texture_import && !front_texture_quality_ok)
            {
                metadata += "," + bridge_events + ",\"" + import_done_stage + "\",\"front_texture_quality_failed\"]";
                return response_json(false,
                                     "front_texture_quality_failed",
                                     1,
                                     1,
                                     "front texture import observed, but texture source is sampled_pixel_front_atlas and does not meet 38923 direct-texture parity",
                                     metadata);
            }
            if (texture_sync_probe && texture_sync_success_candidate.empty())
            {
                metadata += "," + bridge_events + ",\"" + import_done_stage + "\",\"texture_sync_failed\"]";
                return response_json(false,
                                     "texture_sync_failed",
                                     1,
                                     1,
                                     "front texture import observed, but no runtime texture sync RPC candidate dispatched",
                                     metadata);
            }
            metadata += "," + bridge_events + ",\"" + import_done_stage + "\"]";
            return response_json(true,
                                 texture_sync_probe ? "texture_sync_strict_probe_done" : import_done_stage.c_str(),
                                 1,
                                 0,
                                 texture_sync_probe ? "texture sync strict probe dispatched; remote unverified" :
                                                      (front_texture_import ? "front texture import observed on albedo/metallic/roughness" : "temporary diagnostic albedo import observed"),
                                 metadata);
        }

        const int stroke_cap = disabled_paint_stream ? 450000 : (front_paint_route ? 120000 : 600000);
        auto stroke_plan = sdk_build_atlas_strokes(ctx, atlas, atlas_target_channel, atlas_use_world_position, stroke_cap);
        metadata += sdk_atlas_stroke_metadata(stroke_plan);
        if (stroke_plan.cap_exceeded || stroke_plan.strokes.empty() || static_cast<int>(stroke_plan.strokes.size()) > stroke_plan.stroke_cap)
        {
            emit_progress(stroke_budget_stage, "stroke count exceeded cap; no paint dispatched", front_metallic_texture_route ? 8 : 7, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                                  "\"stroke_count\":" + std::to_string(stroke_plan.strokes.size()) +
                                  ",\"stroke_cap\":" + std::to_string(stroke_plan.stroke_cap));
            metadata += "," + bridge_events + ",\"" + stroke_budget_stage + "\"]";
            return response_json(false,
                                 stroke_budget_stage.c_str(),
                                 0,
                                 1,
                                 "atlas stroke generation exceeded the hard cap; no paint was dispatched",
                                 metadata);
        }
        bridge_events += ",\"" + strokes_generated_stage + "\",\"replicated_stream_started\"";
        emit_progress(strokes_generated_stage, "front atlas strokes generated", front_metallic_texture_route ? 8 : 7, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                              "\"stroke_count\":" + std::to_string(stroke_plan.strokes.size()));
        emit_progress("replicated_stream_started", "dispatching ServerPaintBatch stroke stream", front_metallic_texture_route ? 8 : 7, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                              "\"stroke_count\":" + std::to_string(stroke_plan.strokes.size()));

        const int raw_batch_limit = safe_read<int>(ctx.component + meccha_sdk::FieldOffsets::RuntimePaintable_MaxReplicatedPaintStrokesPerTick, 24);
        const int batch_limit = std::max(1, std::min(raw_batch_limit > 0 ? raw_batch_limit : 24, 256));
        const int server_batches_total = static_cast<int>((stroke_plan.strokes.size() + static_cast<std::size_t>(batch_limit) - 1) / static_cast<std::size_t>(batch_limit));
        int last_stream_percent = -1;
        const auto stream_started = std::chrono::steady_clock::now();
        auto stream_progress = [&](const SdkReplicatedStats& stats, int server_sent, int total) -> bool {
            const int percent = total > 0 ? std::max(0, std::min(100, static_cast<int>((static_cast<long long>(server_sent) * 100LL) / total))) : 100;
            if (percent != last_stream_percent)
            {
                last_stream_percent = percent;
                const auto elapsed_ms = elapsed_now_ms();
                const auto stream_elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stream_started).count();
                const double eta_ms = percent > 0 ? std::max(0.0, (stream_elapsed_ms / (static_cast<double>(percent) / 100.0)) - stream_elapsed_ms) : 0.0;
                emit_progress("replicated_stream_progress",
                              "dispatching ServerPaintBatch stroke stream",
                              percent,
                              100,
                              elapsed_ms,
                              "\"server_batches\":" + std::to_string(server_batches_total) +
                                  ",\"server_batch_index\":" + std::to_string(stats.batch_calls) +
                                  ",\"server_sent\":" + std::to_string(server_sent) +
                                  ",\"stroke_count\":" + std::to_string(total) +
                                  ",\"eta_ms\":" + std::to_string(eta_ms));
            }
            return elapsed_now_ms() < 210000.0;
        };

        const int atlas_stream_batch_override = disabled_paint_stream ? 256 : 0;
        const int atlas_stream_sleep_ms = disabled_paint_stream ? 0 : 16;
        metadata += ",\"server_stream_batch_override\":" + std::to_string(atlas_stream_batch_override) +
                    ",\"server_stream_sleep_ms\":" + std::to_string(atlas_stream_sleep_ms);
        if (!sdk_dispatch_replicated_strokes_with_progress(ctx,
                                                           stroke_plan.strokes,
                                                           atlas_stats,
                                                           stream_progress,
                                                           atlas_stream_batch_override,
                                                           atlas_stream_sleep_ms))
        {
            const std::string failure_stage = atlas_stats.first_failure == "server_stream_timeout_before_bridge_timeout" ?
                "server_stream_timeout_before_bridge_timeout" :
                "server_batch_failed";
            emit_progress(failure_stage, "ServerPaintBatch stream failed", front_metallic_texture_route ? 8 : 7, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                                  "\"server_sent\":" + std::to_string(atlas_stats.server_sent) +
                                  ",\"server_failed\":" + std::to_string(atlas_stats.server_failed));
            metadata += sdk_stats_metadata("atlas_paint", atlas_stats) +
                        "," + bridge_events + ",\"" + failure_stage + "\"]";
            return response_json(false,
                                 failure_stage.c_str(),
                                 atlas_stats.server_sent,
                                 std::max(1, atlas_stats.server_failed),
                                 "atlas paint ServerPaintBatch failed: " + atlas_stats.first_failure,
                                 metadata);
        }
        bridge_events += ",\"replicated_stream_done\"";
        emit_progress("replicated_stream_done", "ServerPaintBatch stream dispatched", front_metallic_texture_route ? 9 : 8, front_metallic_texture_route ? 9 : 8, elapsed_now_ms(),
                              "\"server_sent\":" + std::to_string(atlas_stats.server_sent));
        Sleep(100);

        auto after0 = sdk_export_channel_bytes(ref, ctx, 0);
        const bool hash_changed = after0.ok && after0.hash != before0.hash;
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        metadata += sdk_stats_metadata("atlas_paint", atlas_stats) +
                    sdk_channel_metadata("after_albedo", after0) +
                    ",\"hash_before\":\"" + std::to_string(before0.hash) + "\"" +
                    ",\"hash_after\":\"" + std::to_string(after0.hash) + "\"" +
                    ",\"hash_changed\":" + json_bool(hash_changed) +
                    ",\"server_rpc\":\"ServerPaintBatch\"" +
                    ",\"server_batches\":" + std::to_string(atlas_stats.batch_calls) +
                    ",\"server_sent\":" + std::to_string(atlas_stats.server_sent) +
                    ",\"server_failed\":" + std::to_string(atlas_stats.server_failed) +
                    ",\"elapsed_ms\":" + std::to_string(elapsed);

        if (!hash_changed && !front_metallic_texture_route)
        {
            metadata += "," + bridge_events + ",\"hash_unchanged\"]";
            return response_json(false,
                                 "hash_unchanged",
                                 atlas_stats.server_sent,
                                 1,
                                 "atlas paint server RPC dispatched, but albedo hash did not change",
                                 metadata);
        }

        metadata += "," + bridge_events + ",\"paint_done\"]";
        return response_json(true,
                             "paint_done",
                             atlas_stats.server_sent,
                             0,
                             front_metallic_texture_route ? "front texture paint stream dispatched after metallic base" : "texture-atlas sourced replicated paint applied",
                             metadata);
    }

    auto paint_full_route_native_direct(const std::string& request) -> std::string
    {
        return sdk_paint_full_route_native_direct(request);
    }

    auto drain_paint_jobs_on_game_thread() -> void
    {
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
            const auto response = paint_full_route_native_direct(job->request);
            {
                std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
                job->response = response;
                job->done = true;
            }
            g_paint_jobs_cv.notify_all();
        }
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
            return "{\"success\":true,\"stage\":\"capabilities\",\"applied\":0,\"failures\":0,"
                   "\"message\":\"ok\",\"timing_ms\":{},"
                   "\"metadata\":{\"commands\":[\"ping\",\"capabilities\",\"sdk_probe\",\"sdk_deep_probe\",\"paint_full_route\",\"shutdown\"],"
                   "\"sdk\":\"chameleonEsp_dumper7_1_7_0_min\","
                   "\"paint_full_route\":\"texture_sync_strict_probe\","
                   "\"replication\":\"texture_sync_rpc\","
                   "\"multiplayer_replicated\":true}}\n";
        }
        if (line.find("\"type\":\"shutdown\"") != std::string::npos)
        {
            uninstall_process_event_hook();
            g_running.store(false);
            return response_json(true, "shutdown", 0, 0, "bridge shutdown requested");
        }
        if (line.find("\"type\":\"sdk_probe\"") != std::string::npos)
        {
            return paint_full_route_native(line);
        }
        if (line.find("\"type\":\"sdk_deep_probe\"") != std::string::npos)
        {
            return paint_full_route_native(line);
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
