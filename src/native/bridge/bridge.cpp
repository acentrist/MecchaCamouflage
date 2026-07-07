#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
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
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../include/sdk.hpp"
#include "../include/bridge_loader_abi.hpp"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Gdi32.lib")

namespace
{
    // =============================================================================
    // Section: Runtime globals, diagnostics, and bridge-side state
    // Risk: high. These values are shared by the injected runtime, listener thread,
    // Win32 hooks, progress sidecars, and C# host diagnostics.
    // =============================================================================

    constexpr int DefaultBridgePort = 47800;
    constexpr std::size_t MaxRequestBytes = 8 * 1024 * 1024;
    constexpr int ProcessEventVtableIndex = 0x4C;
    constexpr int AutoEventWatchSampleBytes = 8192;
    constexpr UINT PaintDispatchMessage = WM_APP + 0x4D43;
    constexpr int PackedReplicationDefaultBatchLimit = 50;
    constexpr int PackedReplicationMaxBatchLimit = 50;
    constexpr int PackedReplicationDefaultPacingMs = 75;
    constexpr int PackedReplicationMinPacingMs = 50;
    constexpr int PackedReplicationFallbackMaxStrokesPerTick = 24;
    constexpr int PackedReplicationResolvedPacingMinMs = 1;
    constexpr int PackedReplicationBatchSize = 20;
    constexpr int PackedReplicationFallbackOutgoingBatchesPerSecond = 20;
    constexpr int PackedReplicationMaxPacingMs = 500;
    constexpr int MeshFirstFastApplyStrokesPerTick = 0;
    constexpr int MeshFirstFastApplyRenderTargetWritesPerFrame = 0;
    constexpr int MeshFirstServerTextureSyncPollMs = 50;
    constexpr int MeshFirstServerTextureSyncMaxPolls = 40;
    constexpr int MeshFirstTextureSyncObserverPollMs = 50;
    constexpr int MeshFirstTextureSyncObserverMaxPolls = 40;
    constexpr bool MeshFirstPostImportTextureSyncEnabled = false;
    constexpr double MeshFirstRuntimeCoordinateMaxAvgErrorCm = 50.0;
    constexpr std::uintptr_t RuntimePaintableComponentPackedSourceIdOffset = 0x2A8;

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
    constexpr std::uintptr_t OffFStructPropertyStruct = 0x70;

    HMODULE g_module = nullptr;
    std::atomic<bool> g_running{false};
    std::atomic<int> g_bridge_state{MC_BRIDGE_CREATED};
    std::atomic<DWORD> g_bridge_last_win32{0};
    std::mutex g_bridge_thread_mutex;
    std::unique_ptr<std::thread> g_bridge_thread{};
    std::atomic<bool> g_bridge_thread_done{true};
    std::atomic<int> g_active_client_handlers{0};
    std::atomic<bool> g_process_event_hook_installed{false};
    std::atomic<std::uintptr_t> g_original_process_event{0};
    std::atomic<HHOOK> g_message_hook{nullptr};
    std::atomic<std::uint32_t> g_active_hook_callbacks{0};
    std::atomic<std::uint32_t> g_active_ue_calls{0};
    std::atomic<DWORD> g_game_thread_id{0};
    std::atomic<HWND> g_game_window{nullptr};
    std::atomic<std::uintptr_t> g_observed_sync_channel_function{0};
    std::atomic<std::uintptr_t> g_observed_sync_compressed_channel_function{0};
    std::atomic<int> g_observed_sync_channel_calls{0};
    std::atomic<int> g_observed_sync_compressed_channel_calls{0};
    std::atomic<int> g_observed_sync_channel_last_channel{-1};
    std::atomic<int> g_observed_sync_compressed_channel_last_channel{-1};
    std::atomic<std::int64_t> g_observed_sync_channel_bytes{0};
    std::atomic<std::int64_t> g_observed_sync_compressed_channel_bytes{0};
    std::atomic<std::int64_t> g_observed_sync_compressed_channel_uncompressed_bytes{0};
    std::mutex g_hook_mutex;
    std::mutex g_auto_event_watch_sample_mutex;
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> g_process_event_hook_slots;
    thread_local bool g_inside_process_event_hook = false;

    struct AutoEventWatchEntry
    {
        const char* name;
        std::atomic<std::uintptr_t> function;
        std::atomic<int> calls;
        std::atomic<int> last_array0_num;
        std::atomic<int> last_array8_num;
        std::atomic<int> total_array0_num;
        std::atomic<int> total_array8_num;
        std::atomic<int> last_i32_16;
        std::atomic<int> last_i32_24;
        std::atomic<std::int64_t> total_i32_16{0};
        std::atomic<std::int64_t> total_i32_24{0};
        int last_array0_sample_len{0};
        int last_array0_sample_total_len{0};
        int last_array0_sample_element_size{1};
        std::array<std::uint8_t, AutoEventWatchSampleBytes> last_array0_sample{};
        int last_array8_sample_len{0};
        int last_array8_sample_total_len{0};
        int last_array8_sample_element_size{1};
        std::array<std::uint8_t, AutoEventWatchSampleBytes> last_array8_sample{};
    };

    AutoEventWatchEntry g_auto_event_watch[] = {
        {"ServerPaintBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerCompactPaintBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerPackedPaintBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"SendStrokeBatchToServer", 0, 0, -1, -1, 0, 0, -1, -1},
        {"SendCustomStrokeBatchToServer", 0, 0, -1, -1, 0, 0, -1, -1},
        {"RequestStrokeBatchOnServer", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerSendStrokeBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"FlushRecordedStrokesToServer", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastPaintBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastCompactPaintBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastPackedPaintBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastPaintBatchToOthers", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastCompactPaintBatchToOthers", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastPackedPaintBatchToOthers", 0, 0, -1, -1, 0, 0, -1, -1},
        {"RelayStrokeBatchToServer", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerRelayStrokeBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerRelayCompactStrokeBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerRelayPackedStrokeBatch", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerRelayCompactPaint", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastSyncChannelData", 0, 0, -1, -1, 0, 0, -1, -1},
        {"MulticastSyncCompressedChannelData", 0, 0, -1, -1, 0, 0, -1, -1},
        {"RelayTextureSyncToServer", 0, 0, -1, -1, 0, 0, -1, -1},
        {"ServerRelayTextureSync", 0, 0, -1, -1, 0, 0, -1, -1},
    };
    std::atomic<bool> g_auto_event_watch_enabled{false};
    std::atomic<bool> g_auto_event_watch_writer_running{false};
    std::wstring g_auto_event_watch_path{};

    using ProcessEventFn = void(__fastcall*)(void*, void*, void*);

    struct QueuedPaintJob
    {
        std::string request{};
        std::string response{};
        bool dispatched{false};
        bool done{false};
    };

    struct TextureSyncObserverSnapshot
    {
        int sync_channel_calls{0};
        int sync_compressed_channel_calls{0};
        int sync_channel_last_channel{-1};
        int sync_compressed_channel_last_channel{-1};
        std::int64_t sync_channel_bytes{0};
        std::int64_t sync_compressed_channel_bytes{0};
        std::int64_t sync_compressed_channel_uncompressed_bytes{0};
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
    auto is_paint_replication_probe_request(const std::string& request) -> bool;
    auto paint_replication_probe_on_game_thread(const std::string& request) -> std::string;
    auto is_paint_packed_replay_probe_request(const std::string& request) -> bool;
    auto paint_packed_replay_probe_on_game_thread(const std::string& request) -> std::string;
    auto auto_event_watch_record(std::uintptr_t function_address, std::uint8_t* params_bytes) -> void;
    void __fastcall hooked_process_event(void* object, void* function, void* params);
    LRESULT CALLBACK message_hook_proc(int code, WPARAM wparam, LPARAM lparam);

    auto reset_texture_sync_observer(std::uintptr_t sync_channel_function,
                                     std::uintptr_t sync_compressed_channel_function) -> void
    {
        g_observed_sync_channel_function.store(sync_channel_function);
        g_observed_sync_compressed_channel_function.store(sync_compressed_channel_function);
        g_observed_sync_channel_calls.store(0);
        g_observed_sync_compressed_channel_calls.store(0);
        g_observed_sync_channel_last_channel.store(-1);
        g_observed_sync_compressed_channel_last_channel.store(-1);
        g_observed_sync_channel_bytes.store(0);
        g_observed_sync_compressed_channel_bytes.store(0);
        g_observed_sync_compressed_channel_uncompressed_bytes.store(0);
    }

    auto texture_sync_observer_snapshot() -> TextureSyncObserverSnapshot
    {
        TextureSyncObserverSnapshot out{};
        out.sync_channel_calls = g_observed_sync_channel_calls.load();
        out.sync_compressed_channel_calls = g_observed_sync_compressed_channel_calls.load();
        out.sync_channel_last_channel = g_observed_sync_channel_last_channel.load();
        out.sync_compressed_channel_last_channel = g_observed_sync_compressed_channel_last_channel.load();
        out.sync_channel_bytes = g_observed_sync_channel_bytes.load();
        out.sync_compressed_channel_bytes = g_observed_sync_compressed_channel_bytes.load();
        out.sync_compressed_channel_uncompressed_bytes = g_observed_sync_compressed_channel_uncompressed_bytes.load();
        return out;
    }

    auto texture_sync_observer_has_activity(const TextureSyncObserverSnapshot& snapshot) -> bool
    {
        return snapshot.sync_channel_calls > 0 || snapshot.sync_compressed_channel_calls > 0;
    }

    auto texture_sync_observer_metadata(const char* prefix, const TextureSyncObserverSnapshot& snapshot) -> std::string
    {
        const std::string key(prefix ? prefix : "texture_sync_observer");
        return ",\"" + key + "_sync_channel_calls\":" + std::to_string(snapshot.sync_channel_calls) +
               ",\"" + key + "_sync_compressed_channel_calls\":" + std::to_string(snapshot.sync_compressed_channel_calls) +
               ",\"" + key + "_sync_channel_last_channel\":" + std::to_string(snapshot.sync_channel_last_channel) +
               ",\"" + key + "_sync_compressed_channel_last_channel\":" + std::to_string(snapshot.sync_compressed_channel_last_channel) +
               ",\"" + key + "_sync_channel_bytes\":" + std::to_string(snapshot.sync_channel_bytes) +
               ",\"" + key + "_sync_compressed_channel_bytes\":" + std::to_string(snapshot.sync_compressed_channel_bytes) +
               ",\"" + key + "_sync_compressed_channel_uncompressed_bytes\":" +
                   std::to_string(snapshot.sync_compressed_channel_uncompressed_bytes);
    }

    auto post_paint_dispatch_message() -> void
    {
        if (const auto hwnd = g_game_window.load())
        {
            PostMessageW(hwnd, PaintDispatchMessage, 0, 0);
        }
        if (const auto thread_id = g_game_thread_id.load())
        {
            PostThreadMessageW(thread_id, PaintDispatchMessage, 0, 0);
        }
    }

    void CALLBACK paint_dispatch_timer_proc(HWND, UINT, UINT_PTR timer_id, DWORD)
    {
        if (timer_id)
        {
            KillTimer(nullptr, timer_id);
        }
        post_paint_dispatch_message();
    }

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

#include "bridge_json.inc"

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

    auto mark_queued_paint_job_dispatched(const std::shared_ptr<QueuedPaintJob>& job) -> bool
    {
        if (!job)
        {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            if (job->done)
            {
                return false;
            }
            job->dispatched = true;
        }
        g_paint_jobs_cv.notify_all();
        return true;
    }

#include "bridge_sidecar.inc"

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

    auto address_in_bridge_module(std::uintptr_t address) -> bool
    {
        if (!address)
        {
            return false;
        }
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(address),
                                &module) ||
            module == nullptr)
        {
            return false;
        }
        wchar_t path[MAX_PATH]{};
        if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
        {
            return false;
        }
        std::wstring lower_path = path;
        for (auto& ch : lower_path)
        {
            if (ch >= L'A' && ch <= L'Z')
            {
                ch = static_cast<wchar_t>(ch - L'A' + L'a');
            }
        }
        return lower_path.find(L"runtime-bridge") != std::wstring::npos;
    }

    auto trusted_process_event_target(std::uintptr_t address) -> bool
    {
        return address_in_main_module(address) || address_in_bridge_module(address);
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

    // =============================================================================
    // Section: UE reflection and ProcessEvent surface
    // Risk: very high. Static references do not prove reachability here because
    // functions and properties are resolved by Unreal object names at runtime.
    // =============================================================================

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
                if (class_ptr(obj) == cls && object_name_raw(obj).rfind("Default__", 0) != 0)
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
    auto json_bool(bool value) -> const char*;

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

    auto bytes_to_hex(const std::uint8_t* data, int length) -> std::string
    {
        if (!data || length <= 0)
        {
            return {};
        }
        static constexpr char digits[] = "0123456789abcdef";
        std::string out{};
        out.reserve(static_cast<std::size_t>(length) * 2);
        for (int i = 0; i < length; ++i)
        {
            const auto value = data[i];
            out.push_back(digits[(value >> 4) & 0x0f]);
            out.push_back(digits[value & 0x0f]);
        }
        return out;
    }

    auto hex_nibble(char ch, std::uint8_t& value) -> bool
    {
        if (ch >= '0' && ch <= '9')
        {
            value = static_cast<std::uint8_t>(ch - '0');
            return true;
        }
        if (ch >= 'a' && ch <= 'f')
        {
            value = static_cast<std::uint8_t>(10 + ch - 'a');
            return true;
        }
        if (ch >= 'A' && ch <= 'F')
        {
            value = static_cast<std::uint8_t>(10 + ch - 'A');
            return true;
        }
        return false;
    }

    auto hex_to_bytes(const std::string& hex, std::vector<std::uint8_t>& bytes, std::string& failure) -> bool
    {
        bytes.clear();
        if (hex.empty() || (hex.size() % 2) != 0)
        {
            failure = "packed_hex_invalid_length";
            return false;
        }
        if (hex.size() > 1024 * 1024)
        {
            failure = "packed_hex_too_large";
            return false;
        }
        bytes.reserve(hex.size() / 2);
        for (std::size_t i = 0; i < hex.size(); i += 2)
        {
            std::uint8_t high = 0;
            std::uint8_t low = 0;
            if (!hex_nibble(hex[i], high) || !hex_nibble(hex[i + 1], low))
            {
                failure = "packed_hex_invalid_character offset=" + std::to_string(i);
                bytes.clear();
                return false;
            }
            bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
        }
        return true;
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

    auto read_object_i32_property(Reflection& ref, std::uintptr_t object, const char* property_name, int fallback = -1) -> int
    {
        const auto prop = live_uobject(object) ? find_object_property(ref, object, property_name) : 0;
        if (!prop)
        {
            return fallback;
        }
        const int offset = prop_offset(prop);
        if (offset < 0)
        {
            return fallback;
        }
        return safe_read<std::int32_t>(object + static_cast<std::uintptr_t>(offset), fallback);
    }

    auto read_object_u8_property(Reflection& ref, std::uintptr_t object, const char* property_name, std::uint8_t fallback = 0) -> std::uint8_t
    {
        const auto prop = live_uobject(object) ? find_object_property(ref, object, property_name) : 0;
        if (!prop)
        {
            return fallback;
        }
        const int offset = prop_offset(prop);
        if (offset < 0)
        {
            return fallback;
        }
        return safe_read<std::uint8_t>(object + static_cast<std::uintptr_t>(offset), fallback);
    }

    auto paint_probe_property_schema(Reflection& ref, std::uintptr_t structure, int max_fields = 16) -> std::string
    {
        if (!structure)
        {
            return "";
        }
        std::string out{};
        int count = 0;
        for (auto prop = safe_read<std::uintptr_t>(structure + OffChildProperties);
             prop && count < max_fields;
             prop = safe_read<std::uintptr_t>(prop + OffFFieldNext), ++count)
        {
            if (!out.empty())
            {
                out += ";";
            }
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            out += name + "@" + std::to_string(prop_offset(prop)) + "#" + std::to_string(prop_element_size(prop));
            const auto struct_type_ptr = safe_read<std::uintptr_t>(prop + OffFStructPropertyStruct);
            if (live_uobject(struct_type_ptr))
            {
                out += ":" + ref.object_name(struct_type_ptr);
            }
        }
        if (safe_read<std::uintptr_t>(structure + OffChildProperties) && count >= max_fields)
        {
            out += ";...";
        }
        return out;
    }

    auto paint_probe_function_schema(Reflection& ref, std::uintptr_t function) -> std::string
    {
        if (!function)
        {
            return "";
        }
        return paint_probe_property_schema(ref, function, 24);
    }

    auto paint_probe_struct_schema_for_param(Reflection& ref, std::uintptr_t function, const char* param_name) -> std::string
    {
        if (!function || !param_name)
        {
            return "";
        }
        const auto prop = ref.find_property(function, param_name);
        if (!prop)
        {
            return "";
        }
        const auto structure = safe_read<std::uintptr_t>(prop + OffFStructPropertyStruct);
        if (!live_uobject(structure))
        {
            return "";
        }
        return ref.object_name(structure) + "[" + paint_probe_property_schema(ref, structure, 24) + "]";
    }

    auto paint_probe_plausible_name(const std::string& name) -> bool
    {
        if (name.empty() || name.size() > 96)
        {
            return false;
        }
        for (const char ch : name)
        {
            const auto value = static_cast<unsigned char>(ch);
            if (value < 0x20 || value > 0x7e)
            {
                return false;
            }
        }
        return true;
    }

    auto paint_probe_property_pointer_scan(Reflection& ref, std::uintptr_t prop) -> std::string
    {
        if (!prop)
        {
            return "";
        }
        std::string out{};
        for (int offset = 0; offset <= 0xc0; offset += 8)
        {
            const auto candidate = safe_read<std::uintptr_t>(prop + static_cast<std::uintptr_t>(offset));
            if (!candidate)
            {
                continue;
            }
            std::string entry{};
            if (live_uobject(candidate))
            {
                entry = hex_address(candidate) + ":" + ref.object_path(candidate) + "<" + ref.class_name(candidate) + ">";
            }
            else
            {
                const auto name_at_field_name =
                    ref.names.resolve(safe_read<std::uint32_t>(candidate + OffFFieldName));
                const auto name_at_28 =
                    ref.names.resolve(safe_read<std::uint32_t>(candidate + 0x28));
                const auto name_at_30 =
                    ref.names.resolve(safe_read<std::uint32_t>(candidate + 0x30));
                if (paint_probe_plausible_name(name_at_field_name))
                {
                    entry = hex_address(candidate) + ":ffield@" + hex_address(OffFFieldName) + "=" + name_at_field_name;
                }
                else if (paint_probe_plausible_name(name_at_28))
                {
                    entry = hex_address(candidate) + ":ffield@0x28=" + name_at_28;
                }
                else if (paint_probe_plausible_name(name_at_30))
                {
                    entry = hex_address(candidate) + ":ffield@0x30=" + name_at_30;
                }
            }
            if (!entry.empty())
            {
                if (!out.empty())
                {
                    out += ";";
                }
                out += "ptr+" + hex_address(static_cast<std::uintptr_t>(offset)) + "=" + entry;
            }
        }
        return out;
    }

    auto paint_probe_function_deep_schema(Reflection& ref, std::uintptr_t function) -> std::string
    {
        if (!function)
        {
            return "";
        }
        std::string out{};
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties);
             prop;
             prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (!out.empty())
            {
                out += "|";
            }
            out += name + "@" + std::to_string(prop_offset(prop)) + "#" + std::to_string(prop_element_size(prop)) +
                   " prop=" + hex_address(prop) + " [" + paint_probe_property_pointer_scan(ref, prop) + "]";
        }
        return out;
    }

    auto paint_replication_function_probe_metadata(Reflection& ref,
                                                   std::uintptr_t object,
                                                   const char* prefix,
                                                   const std::vector<const char*>& function_names) -> std::string
    {
        std::string metadata{};
        int available_count = 0;
        const std::string key_prefix(prefix ? prefix : "paint_probe");
        metadata += ",\"" + key_prefix + "_object\":\"" + hex_address(object) + "\"";
        metadata += ",\"" + key_prefix + "_object_available\":" + std::string(json_bool(live_uobject(object)));
        metadata += ",\"" + key_prefix + "_class\":\"" + json_escape(ref.class_name(object)) + "\"";
        for (const auto* function_name : function_names)
        {
            if (!function_name)
            {
                continue;
            }
            const auto function = live_uobject(object) ? ref.find_function(object, function_name) : 0;
            const bool available = function != 0;
            available_count += available ? 1 : 0;
            const std::string name_key = key_prefix + "_" + function_name;
            metadata += ",\"" + name_key + "_available\":" + std::string(json_bool(available));
            metadata += ",\"" + name_key + "\":\"" + hex_address(function) + "\"";
            if (available)
            {
                metadata += ",\"" + name_key + "_path\":\"" + json_escape(ref.object_path(function)) + "\"";
                metadata += ",\"" + name_key + "_params_size\":" + std::to_string(safe_read<int>(function + OffPropertiesSize, -1));
                metadata += ",\"" + name_key + "_schema\":\"" + json_escape(paint_probe_function_schema(ref, function)) + "\"";
                metadata += ",\"" + name_key + "_deep_schema\":\"" + json_escape(paint_probe_function_deep_schema(ref, function)) + "\"";
                metadata += ",\"" + name_key + "_batch_schema\":\"" + json_escape(paint_probe_struct_schema_for_param(ref, function, "Batch")) + "\"";
                metadata += ",\"" + name_key + "_stroke_schema\":\"" + json_escape(paint_probe_struct_schema_for_param(ref, function, "Stroke")) + "\"";
            }
        }
        metadata += ",\"" + key_prefix + "_available_count\":" + std::to_string(available_count);
        return metadata;
    }

    auto paint_replication_property_probe_metadata(Reflection& ref,
                                                   std::uintptr_t object,
                                                   const char* prefix,
                                                   const std::vector<const char*>& property_names) -> std::string
    {
        std::string metadata{};
        const std::string key_prefix(prefix ? prefix : "paint_property_probe");
        metadata += ",\"" + key_prefix + "_object\":\"" + hex_address(object) + "\"";
        metadata += ",\"" + key_prefix + "_object_available\":" + std::string(json_bool(live_uobject(object)));
        metadata += ",\"" + key_prefix + "_class\":\"" + json_escape(ref.class_name(object)) + "\"";
        for (const auto* property_name : property_names)
        {
            if (!property_name)
            {
                continue;
            }
            const auto prop = live_uobject(object) ? find_object_property(ref, object, property_name) : 0;
            const bool available = prop != 0;
            const std::string name_key = key_prefix + "_" + property_name;
            metadata += ",\"" + name_key + "_available\":" + std::string(json_bool(available));
            if (available)
            {
                const int offset = prop_offset(prop);
                const int size = prop_element_size(prop);
                metadata += ",\"" + name_key + "_offset\":" + std::to_string(offset);
                metadata += ",\"" + name_key + "_size\":" + std::to_string(size);
                if (offset >= 0)
                {
                    const auto address = object + static_cast<std::uintptr_t>(offset);
                    metadata += ",\"" + name_key + "_raw_u32\":" +
                                std::to_string(safe_read<std::uint32_t>(address, 0));
                    metadata += ",\"" + name_key + "_raw_i32\":" +
                                std::to_string(safe_read<std::int32_t>(address, 0));
                    metadata += ",\"" + name_key + "_raw_f32\":" +
                                std::to_string(safe_read<float>(address, 0.0f));
                    metadata += ",\"" + name_key + "_raw_u8\":" +
                                std::to_string(static_cast<unsigned>(safe_read<std::uint8_t>(address, 0)));
                }
            }
        }
        return metadata;
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
        if (!trusted_process_event_target(target))
        {
            failure = "process_event_target_untrusted";
            return false;
        }
        auto_event_watch_record(function, params);
        g_active_ue_calls.fetch_add(1);
        bool ok = false;
        __try
        {
            reinterpret_cast<ProcessEventFn>(target)(reinterpret_cast<void*>(object), reinterpret_cast<void*>(function), params);
            ok = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            failure = "process_event_exception";
            ok = false;
        }
        g_active_ue_calls.fetch_sub(1);
        return ok;
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

    struct SdkBoolCallResult
    {
        bool available{false};
        bool process_ok{false};
        bool value{false};
        std::string failure{};
    };

    auto call_no_params_return_bool_detail(Reflection& ref, std::uintptr_t object, const char* function_name) -> SdkBoolCallResult
    {
        SdkBoolCallResult out{};
        const auto function = ref.find_function(object, function_name);
        out.available = function != 0;
        if (!function)
        {
            out.failure = std::string(function_name) + "_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 1024)
        {
            out.failure = std::string(function_name) + "_params_size_invalid";
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(1, params_size)), 0);
        if (!process_event(object, function, params.data(), out.failure))
        {
            out.failure = std::string(function_name) + "_process_event_failed:" + out.failure;
            return out;
        }
        out.process_ok = true;
        out.value = read_return_bool(ref, function, params.data());
        return out;
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
            const bool packed_ready = ref.find_function(obj, "ServerPackedPaintBatch") != 0;
            if (paint_component && packed_ready)
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
                !ref.find_function(obj, "ServerPackedPaintBatch"))
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
        struct HookTarget
        {
            DWORD thread_id{0};
            HWND hwnd{nullptr};
        };
        auto resolve_target = []() -> HookTarget {
            HookTarget target{};
            EnumWindows(
                [](HWND hwnd, LPARAM lparam) -> BOOL {
                    DWORD owner_pid = 0;
                    const DWORD tid = GetWindowThreadProcessId(hwnd, &owner_pid);
                    if (owner_pid == GetCurrentProcessId() && tid != 0 && IsWindowVisible(hwnd))
                    {
                        auto* out = reinterpret_cast<HookTarget*>(lparam);
                        out->thread_id = tid;
                        out->hwnd = hwnd;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&target));
            return target;
        };
        const DWORD process_id = GetCurrentProcessId();
        const HookTarget target = resolve_target();
        if (target.thread_id == 0)
        {
            failure = "game_window_thread_unavailable pid=" + std::to_string(process_id);
            return false;
        }
        const auto old_hook = g_message_hook.exchange(nullptr);
        if (old_hook)
        {
            UnhookWindowsHookEx(old_hook);
        }
        g_process_event_hook_installed.store(false);
        const auto hook = SetWindowsHookExW(WH_GETMESSAGE, message_hook_proc, nullptr, target.thread_id);
        if (!hook)
        {
            failure = "message_hook_install_failed win32=" + std::to_string(GetLastError()) + " thread=" + std::to_string(target.thread_id);
            return false;
        }
        g_message_hook.store(hook);
        g_game_thread_id.store(target.thread_id);
        g_game_window.store(target.hwnd);
        g_process_event_hook_installed.store(true);
        post_paint_dispatch_message();
        return true;
    }

    auto install_process_event_vtable_hook_for_object(std::uintptr_t object, std::string& failure) -> bool
    {
        if (!live_uobject(object))
        {
            failure = "hook_object_unavailable";
            return false;
        }
        const auto vtable = safe_read<std::uintptr_t>(object);
        if (!vtable)
        {
            failure = "hook_vtable_unavailable";
            return false;
        }
        const auto slot_address = vtable + static_cast<std::uintptr_t>(ProcessEventVtableIndex) * sizeof(std::uintptr_t);
        const auto original = safe_read<std::uintptr_t>(slot_address);
        if (!trusted_process_event_target(original))
        {
            failure = "hook_process_event_target_untrusted";
            return false;
        }

        const auto hook = reinterpret_cast<std::uintptr_t>(&hooked_process_event);
        std::lock_guard<std::mutex> hook_lock(g_hook_mutex);
        for (const auto& entry : g_process_event_hook_slots)
        {
            if (entry.first == slot_address)
            {
                return true;
            }
        }
        const auto existing_original = g_original_process_event.load();
        if (existing_original && existing_original != original)
        {
            failure = "hook_process_event_original_mismatch";
            return false;
        }

        DWORD old_protect = 0;
        auto* slot = reinterpret_cast<std::uintptr_t*>(slot_address);
        if (!VirtualProtect(slot, sizeof(std::uintptr_t), PAGE_EXECUTE_READWRITE, &old_protect))
        {
            failure = "hook_virtualprotect_failed win32=" + std::to_string(GetLastError());
            return false;
        }
        *slot = hook;
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(std::uintptr_t));
        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(std::uintptr_t), old_protect, &ignored);
        g_original_process_event.store(original);
        g_process_event_hook_slots.push_back({slot_address, original});
        return true;
    }

    auto uninstall_message_hook() -> void
    {
        const auto message_hook = g_message_hook.exchange(nullptr);
        if (message_hook)
        {
            UnhookWindowsHookEx(message_hook);
        }
        g_game_thread_id.store(0);
        g_game_window.store(nullptr);
        g_process_event_hook_installed.store(false);
    }

    auto uninstall_process_event_vtable_hooks() -> void
    {
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
    }

    auto uninstall_process_event_hook() -> void
    {
        uninstall_message_hook();
        uninstall_process_event_vtable_hooks();
        g_process_event_hook_installed.store(false);
    }

    auto write_text_file_w(const std::wstring& path, const std::string& text) -> bool
    {
        if (path.empty())
        {
            return false;
        }
        HANDLE file = CreateFileW(path.c_str(),
                                  GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        DWORD written = 0;
        const BOOL ok = WriteFile(file,
                                  text.data(),
                                  static_cast<DWORD>(text.size()),
                                  &written,
                                  nullptr);
        CloseHandle(file);
        return ok && written == text.size();
    }

    auto auto_event_watch_output_path() -> std::wstring
    {
        const auto configured_sidecar = bridge_sidecar_path(L".eventwatch.path");
        std::string configured{};
        if (!configured_sidecar.empty() && read_small_text_file_w(configured_sidecar, 4096, configured))
        {
            if (configured.size() >= 3 &&
                static_cast<unsigned char>(configured[0]) == 0xef &&
                static_cast<unsigned char>(configured[1]) == 0xbb &&
                static_cast<unsigned char>(configured[2]) == 0xbf)
            {
                configured.erase(0, 3);
            }
            const auto wide = utf8_to_wstring(trim_ascii_whitespace(configured));
            if (!wide.empty())
            {
                return wide;
            }
        }
        const auto enable_sidecar = bridge_sidecar_path(L".eventwatch");
        if (!enable_sidecar.empty() && GetFileAttributesW(enable_sidecar.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return bridge_sidecar_path(L".eventwatch.json");
        }
        return {};
    }

    auto auto_event_watch_snapshot_json(const char* stage) -> std::string
    {
        int available = 0;
        int observed = 0;
        std::string out = "{\"stage\":\"";
        out += stage ? stage : "event_watch";
        out += "\",\"pid\":";
        out += std::to_string(GetCurrentProcessId());
        out += ",\"tick_ms\":";
        out += std::to_string(GetTickCount64());
        out += ",\"entries\":{";
        bool first = true;
        for (auto& entry : g_auto_event_watch)
        {
            if (!first)
            {
                out += ",";
            }
            first = false;
            const auto function = entry.function.load();
            const int calls = entry.calls.load();
            if (function)
            {
                ++available;
            }
            if (calls > 0)
            {
                ++observed;
            }
            out += "\"" + json_escape(entry.name) + "\":{";
            out += "\"function\":\"" + hex_address(function) + "\"";
            out += ",\"calls\":" + std::to_string(calls);
            out += ",\"last_array0_num\":" + std::to_string(entry.last_array0_num.load());
            out += ",\"last_array8_num\":" + std::to_string(entry.last_array8_num.load());
            out += ",\"total_array0_num\":" + std::to_string(entry.total_array0_num.load());
            out += ",\"total_array8_num\":" + std::to_string(entry.total_array8_num.load());
            out += ",\"last_i32_16\":" + std::to_string(entry.last_i32_16.load());
            out += ",\"last_i32_24\":" + std::to_string(entry.last_i32_24.load());
            out += ",\"total_i32_16\":" + std::to_string(entry.total_i32_16.load());
            out += ",\"total_i32_24\":" + std::to_string(entry.total_i32_24.load());
            {
                std::lock_guard<std::mutex> sample_lock(g_auto_event_watch_sample_mutex);
                out += ",\"last_array0_sample_len\":" + std::to_string(entry.last_array0_sample_len);
                out += ",\"last_array0_sample_total_len\":" + std::to_string(entry.last_array0_sample_total_len);
                out += ",\"last_array0_sample_element_size\":" + std::to_string(entry.last_array0_sample_element_size);
                out += ",\"last_array0_sample_hex\":\"" +
                       bytes_to_hex(entry.last_array0_sample.data(), entry.last_array0_sample_len) + "\"";
                out += ",\"last_array8_sample_len\":" + std::to_string(entry.last_array8_sample_len);
                out += ",\"last_array8_sample_total_len\":" + std::to_string(entry.last_array8_sample_total_len);
                out += ",\"last_array8_sample_element_size\":" + std::to_string(entry.last_array8_sample_element_size);
                out += ",\"last_array8_sample_hex\":\"" +
                       bytes_to_hex(entry.last_array8_sample.data(), entry.last_array8_sample_len) + "\"";
            }
            out += "}";
        }
        out += "},\"available_count\":" + std::to_string(available);
        out += ",\"observed_count\":" + std::to_string(observed);
        out += ",\"hook_slots\":" + std::to_string(g_process_event_hook_slots.size());
        out += "}\n";
        return out;
    }

    auto auto_event_watch_record(std::uintptr_t function_address, std::uint8_t* params_bytes) -> void
    {
        if (!g_auto_event_watch_enabled.load() || !params_bytes)
        {
            return;
        }
        auto read_array_num = [&](std::size_t offset) -> int {
            const auto* array = reinterpret_cast<const sdk::TArray<std::uint8_t>*>(params_bytes + offset);
            const int num = array ? array->Num : -1;
            return (num >= 0 && num <= 1000000) ? num : -1;
        };
        auto read_i32 = [&](std::size_t offset) -> int {
            return *reinterpret_cast<const int*>(params_bytes + offset);
        };
        auto array_element_size = [](const char* name, std::size_t offset) -> int {
            const std::string function_name = name ? name : "";
            if (contains_text(function_name, "Packed") || contains_text(function_name, "Texture") ||
                contains_text(function_name, "SyncChannel"))
            {
                return 1;
            }
            if (contains_text(function_name, "Compact"))
            {
                if (contains_text(function_name, "Relay") && offset != 8)
                {
                    return 1;
                }
                return 0x38;
            }
            if (contains_text(function_name, "StrokeBatch") || contains_text(function_name, "PaintBatch") ||
                function_name == "ServerPaintBatch" || function_name == "FlushRecordedStrokesToServer")
            {
                if (contains_text(function_name, "Relay") && offset != 8)
                {
                    return 1;
                }
                return static_cast<int>(sizeof(sdk::FPaintStroke));
            }
            return 1;
        };
        auto capture_array_sample = [&](std::size_t offset,
                                        std::array<std::uint8_t, AutoEventWatchSampleBytes>& sample,
                                        int& sample_len,
                                        int& total_len,
                                        int& sample_element_size,
                                        const char* entry_name) {
            const auto* array = reinterpret_cast<const sdk::TArray<std::uint8_t>*>(params_bytes + offset);
            const int num = array ? array->Num : -1;
            const auto* data = array ? array->Data : nullptr;
            if (num <= 0 || num > 1000000 || !data)
            {
                return;
            }
            const int element_size = std::max(1, array_element_size(entry_name, offset));
            const std::uint64_t total_bytes_u64 =
                static_cast<std::uint64_t>(num) * static_cast<std::uint64_t>(element_size);
            const int total_bytes = total_bytes_u64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                                        ? std::numeric_limits<int>::max()
                                        : static_cast<int>(total_bytes_u64);
            std::array<std::uint8_t, AutoEventWatchSampleBytes> local_sample{};
            const int len = std::min(total_bytes, AutoEventWatchSampleBytes);
            if (!safe_copy(local_sample.data(), data, static_cast<std::size_t>(len)))
            {
                return;
            }
            std::lock_guard<std::mutex> sample_lock(g_auto_event_watch_sample_mutex);
            sample = local_sample;
            sample_len = len;
            total_len = total_bytes;
            sample_element_size = element_size;
        };
        for (auto& entry : g_auto_event_watch)
        {
            if (function_address != entry.function.load())
            {
                continue;
            }
            entry.calls.fetch_add(1);
            const int array0 = read_array_num(0);
            const int array8 = read_array_num(8);
            const int i32_16 = read_i32(16);
            const int i32_24 = read_i32(24);
            entry.last_array0_num.store(array0);
            entry.last_array8_num.store(array8);
            if (array0 > 0)
            {
                entry.total_array0_num.fetch_add(array0);
            }
            if (array8 > 0)
            {
                entry.total_array8_num.fetch_add(array8);
            }
            if (i32_16 > 0 && i32_16 <= 1000000)
            {
                entry.total_i32_16.fetch_add(i32_16);
            }
            if (i32_24 > 0 && i32_24 <= 1000000)
            {
                entry.total_i32_24.fetch_add(i32_24);
            }
            capture_array_sample(0,
                                 entry.last_array0_sample,
                                 entry.last_array0_sample_len,
                                 entry.last_array0_sample_total_len,
                                 entry.last_array0_sample_element_size,
                                 entry.name);
            capture_array_sample(8,
                                 entry.last_array8_sample,
                                 entry.last_array8_sample_len,
                                 entry.last_array8_sample_total_len,
                                 entry.last_array8_sample_element_size,
                                 entry.name);
            entry.last_i32_16.store(i32_16);
            entry.last_i32_24.store(i32_24);
            return;
        }
    }

    auto start_auto_event_watch_if_configured() -> void
    {
        const auto output_path = auto_event_watch_output_path();
        if (output_path.empty() || g_auto_event_watch_enabled.load())
        {
            return;
        }

        Reflection ref{};
        std::string failure{};
        if (!ref.init(failure))
        {
            write_text_file_w(output_path,
                              "{\"stage\":\"event_watch_init_failed\",\"failure\":\"" +
                                  json_escape(failure) + "\"}\n");
            return;
        }

        int hook_slots = 0;
        ref.for_each_object([&](std::uintptr_t object) {
            if (!live_uobject(object))
            {
                return false;
            }
            const auto class_name_text = lower_copy(ref.class_name(object));
            if (contains_text(class_name_text, "runtimepaint"))
            {
                std::string hook_failure{};
                if (install_process_event_vtable_hook_for_object(object, hook_failure))
                {
                    ++hook_slots;
                }
            }
            const auto name = ref.object_name(object);
            for (auto& entry : g_auto_event_watch)
            {
                if (name != entry.name)
                {
                    continue;
                }
                const auto path = lower_copy(ref.object_path(object));
                if (contains_text(path, "runtimepaintablecomponent") ||
                    contains_text(path, "runtimepaintrelaycomponent"))
                {
                    entry.function.store(object);
                }
            }
            return false;
        });

        g_auto_event_watch_path = output_path;
        g_auto_event_watch_enabled.store(true);
        g_auto_event_watch_writer_running.store(true);
        write_text_file_w(output_path, auto_event_watch_snapshot_json("event_watch_started"));
        std::thread([]() {
            while (g_auto_event_watch_enabled.load())
            {
                write_text_file_w(g_auto_event_watch_path, auto_event_watch_snapshot_json("event_watch_running"));
                Sleep(1000);
            }
            write_text_file_w(g_auto_event_watch_path, auto_event_watch_snapshot_json("event_watch_stopped"));
            g_auto_event_watch_writer_running.store(false);
        }).detach();
        (void)hook_slots;
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
        std::uintptr_t relay_component{0};
        std::uintptr_t server_packed_paint_batch_function{0};
        std::uintptr_t server_relay_packed_stroke_batch_function{0};
        std::uintptr_t local_paint_at_uv_function{0};
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

    struct SdkCallDetail
    {
        bool object_available{false};
        bool function_available{false};
        bool wrote_params{false};
        bool process_ok{false};
        std::string failure{};
    };

    auto sdk_call_no_params_detail(Reflection& ref, std::uintptr_t object, const char* function_name) -> SdkCallDetail
    {
        SdkCallDetail out{};
        out.object_available = live_uobject(object);
        if (!out.object_available)
        {
            out.failure = "object_unavailable";
            return out;
        }
        const auto function = ref.find_function(object, function_name);
        out.function_available = function != 0;
        if (!function)
        {
            out.failure = std::string(function_name) + "_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size < 0 || params_size > 1024)
        {
            out.failure = std::string(function_name) + "_params_size_invalid";
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(1, params_size)), 0);
        out.wrote_params = true;
        out.process_ok = process_event(object, function, params.data(), out.failure);
        if (!out.process_ok)
        {
            out.failure = std::string(function_name) + "_failed:" + out.failure;
        }
        return out;
    }

    auto sdk_call_object_param_detail(Reflection& ref,
                                      std::uintptr_t object,
                                      const char* function_name,
                                      std::uintptr_t value) -> SdkCallDetail
    {
        SdkCallDetail out{};
        out.object_available = live_uobject(object);
        if (!out.object_available)
        {
            out.failure = "object_unavailable";
            return out;
        }
        if (!live_uobject(value))
        {
            out.failure = "param_object_unavailable";
            return out;
        }
        const auto function = ref.find_function(object, function_name);
        out.function_available = function != 0;
        if (!function)
        {
            out.failure = std::string(function_name) + "_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 1024)
        {
            out.failure = std::string(function_name) + "_params_size_invalid";
            return out;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        for (auto prop = safe_read<std::uintptr_t>(function + OffChildProperties); prop; prop = safe_read<std::uintptr_t>(prop + OffFFieldNext))
        {
            const auto name = ref.names.resolve(safe_read<std::uint32_t>(prop + OffFFieldName));
            if (name != "ReturnValue")
            {
                out.wrote_params = sdk_write_object(prop, params.data(), value) || out.wrote_params;
            }
        }
        if (!out.wrote_params)
        {
            out.failure = std::string(function_name) + "_schema_unmatched";
            return out;
        }
        out.process_ok = process_event(object, function, params.data(), out.failure);
        if (!out.process_ok)
        {
            out.failure = std::string(function_name) + "_failed:" + out.failure;
        }
        return out;
    }

    auto sdk_call_detail_metadata(const char* prefix, const SdkCallDetail& detail) -> std::string
    {
        const std::string key(prefix ? prefix : "sdk_call");
        return ",\"" + key + "_object_available\":" + json_bool(detail.object_available) +
               ",\"" + key + "_function_available\":" + json_bool(detail.function_available) +
               ",\"" + key + "_wrote_params\":" + json_bool(detail.wrote_params) +
               ",\"" + key + "_process_ok\":" + json_bool(detail.process_ok) +
               ",\"" + key + "_failure\":\"" + json_escape(detail.failure) + "\"";
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
               ",\"relay_component\":\"" + hex_address(ctx.relay_component) + "\"" +
               ",\"relay_component_class\":\"" + json_escape(ref.class_name(ctx.relay_component)) + "\"" +
               ",\"function_server_packed_paint_batch_available\":" + std::string(json_bool(ctx.server_packed_paint_batch_function != 0)) +
               ",\"function_server_packed_paint_batch\":\"" + hex_address(ctx.server_packed_paint_batch_function) + "\"" +
               ",\"function_server_relay_packed_stroke_batch_available\":" + std::string(json_bool(ctx.server_relay_packed_stroke_batch_function != 0)) +
               ",\"function_server_relay_packed_stroke_batch\":\"" + hex_address(ctx.server_relay_packed_stroke_batch_function) + "\"" +
               ",\"function_paint_at_uv_with_brush_available\":" + std::string(json_bool(ctx.local_paint_at_uv_function != 0)) +
               ",\"function_paint_at_uv_with_brush\":\"" + hex_address(ctx.local_paint_at_uv_function) + "\"" +
               ",\"param_schema\":\"PackedPaintBatch{PackedData@0,StrokeCount@16}\"" +
               std::string(",\"sdk_replication_api\":\"component_server_packed_paint_batch\"") +
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
        const auto controller_class_name = ref.class_name(ctx.controller);
        const auto relay_offset = ref.resolve_property_offset(controller_class_name.c_str(), "RuntimePaintRelay");
        if (relay_offset >= 0)
        {
            const auto relay = safe_read<std::uintptr_t>(ctx.controller + static_cast<std::uintptr_t>(relay_offset));
            if (live_uobject(relay) && contains_text(lower_copy(ref.class_name(relay)), "runtimepaintrelay"))
            {
                ctx.relay_component = relay;
            }
        }
        if (!live_uobject(ctx.relay_component))
        {
            const auto relay = ref.find_first_instance("RuntimePaintRelayComponent");
            if (live_uobject(relay))
            {
                ctx.relay_component = relay;
            }
        }
        ctx.server_packed_paint_batch_function = ref.find_function(ctx.component, "ServerPackedPaintBatch");
        ctx.server_relay_packed_stroke_batch_function = ref.find_function(ctx.relay_component, "ServerRelayPackedStrokeBatch");
        ctx.local_paint_at_uv_function = ref.find_function(ctx.component, "PaintAtUVWithBrush");
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
    auto sdk_strokes_are_packed_compatible(const std::vector<sdk::FPaintStroke>& strokes) -> bool;
    auto sdk_call_paint_at_uv_with_brush(std::uintptr_t component,
                                         std::uintptr_t function,
                                         const sdk::FPaintStroke& stroke,
                                         std::string& failure) -> bool;
    auto sdk_write_number_property_by_name(Reflection& ref, std::uintptr_t object, const char* name, double value) -> bool;
    auto sdk_read_component_packed_source_id(std::uintptr_t component,
                                             sdk::FGuid& id,
                                             std::string& failure) -> bool;
    auto sdk_call_packed_paint_batch_from_strokes(std::uintptr_t component,
                                                  std::uintptr_t relay_component,
                                                  std::uintptr_t component_function,
                                                  std::uintptr_t relay_function,
                                                  bool use_relay,
                                                  const std::vector<sdk::FPaintStroke>& strokes,
                                                  std::size_t offset,
                                                  std::size_t count,
                                                  const sdk::FGuid& source_id,
                                                  int texture_size,
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
        bool has_identity{false};
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

    // =============================================================================
    // Section: Mesh profile loading and mesh identity matching
    // Risk: medium/high. Profiles are game-derived safety inputs for paint planning.
    // =============================================================================

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
        profile.has_identity = !profile.source_path.empty() && !profile.export_name.empty();
        profile.has_vertices = text.find("\"Vertices\"") != std::string::npos;
        profile.has_indices = text.find("\"Indices\"") != std::string::npos;

        if (profile.schema_version != 2)
        {
            profile.stage = "mesh_profile_schema_mismatch";
            profile.message = "mesh profile must use ProfileSchemaVersion 2";
            return profile;
        }
        if (!profile.has_identity)
        {
            profile.stage = "mesh_profile_invalid";
            profile.message = "mesh profile is missing SourcePath or Export identity";
            return profile;
        }
        if (profile.vertex_count <= 0 || profile.index_count <= 0 || profile.index_count % 3 != 0 || profile.bone_count <= 0)
        {
            profile.stage = "mesh_profile_shape_invalid";
            profile.message = "mesh profile has invalid vertex, index, or bone counts";
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
            failure = "required mesh profile catalog is empty; unverified runtime scan fallback is disabled";
        }
        else
        {
            failure = "no required mesh profile matched the live mesh identity; unverified runtime scan fallback is disabled";
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
               ",\"mesh_profile_has_identity\":" + json_bool(profile.has_identity) +
               ",\"mesh_profile_expected_export\":" + json_bool(profile.has_identity) +
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

    struct MeshFirstChannelBytes
    {
        bool ok{false};
        std::vector<std::uint8_t> bytes{};
        std::string failure{"not_run"};
    };

    struct MeshFirstPreviewSnapshot
    {
        bool available{false};
        std::uintptr_t component{0};
        int texture_size{0};
        std::uint64_t hash{1469598103934665603ULL};
        std::vector<std::uint8_t> albedo_bytes{};
        std::vector<std::uint8_t> metallic_bytes{};
        std::vector<std::uint8_t> roughness_bytes{};
    };

    struct MeshFirstPaintMaterialPattern
    {
        sdk::FLinearColor albedo_color{};
        float metallic{0.0f};
        float roughness{0.65f};
        float coverage_ratio{0.0f};
        std::int32_t sample_count{0};
    };

    static_assert(sizeof(MeshFirstPaintMaterialPattern) == 0x20, "PaintMaterialPattern layout mismatch");
    static_assert(offsetof(MeshFirstPaintMaterialPattern, metallic) == 0x10, "PaintMaterialPattern Metallic offset mismatch");
    static_assert(offsetof(MeshFirstPaintMaterialPattern, roughness) == 0x14, "PaintMaterialPattern Roughness offset mismatch");
    static_assert(offsetof(MeshFirstPaintMaterialPattern, coverage_ratio) == 0x18, "PaintMaterialPattern CoverageRatio offset mismatch");
    static_assert(offsetof(MeshFirstPaintMaterialPattern, sample_count) == 0x1C, "PaintMaterialPattern SampleCount offset mismatch");

    struct MeshFirstMaterialProperties
    {
        bool ok{false};
        double metallic{0.0};
        double roughness{0.65};
        double coverage_ratio{0.0};
        int sample_count{0};
        int patterns{0};
        std::string failure{"not_run"};
    };

    // =============================================================================
    // Section: Preview/unpreview texture channel IO
    // Risk: high. Snapshot state is local-only but affects paint preview recovery.
    // =============================================================================

    std::mutex g_mesh_first_preview_mutex;
    MeshFirstPreviewSnapshot g_mesh_first_preview_snapshot{};

    auto mesh_first_store_preview_snapshot(std::uintptr_t component,
                                           int texture_size,
                                           const std::vector<std::uint8_t>& albedo_bytes,
                                           const std::vector<std::uint8_t>& metallic_bytes,
                                           const std::vector<std::uint8_t>& roughness_bytes) -> void
    {
        if (!component || albedo_bytes.empty() || metallic_bytes.empty() || roughness_bytes.empty())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(g_mesh_first_preview_mutex);
        g_mesh_first_preview_snapshot.available = true;
        g_mesh_first_preview_snapshot.component = component;
        g_mesh_first_preview_snapshot.texture_size = texture_size;
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto value : albedo_bytes)
        {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ULL;
        }
        for (const auto value : metallic_bytes)
        {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ULL;
        }
        for (const auto value : roughness_bytes)
        {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ULL;
        }
        g_mesh_first_preview_snapshot.hash = hash;
        g_mesh_first_preview_snapshot.albedo_bytes = albedo_bytes;
        g_mesh_first_preview_snapshot.metallic_bytes = metallic_bytes;
        g_mesh_first_preview_snapshot.roughness_bytes = roughness_bytes;
    }

    auto mesh_first_clear_preview_snapshot() -> void
    {
        std::lock_guard<std::mutex> lock(g_mesh_first_preview_mutex);
        g_mesh_first_preview_snapshot = {};
    }

    auto mesh_first_preview_snapshot_copy() -> MeshFirstPreviewSnapshot
    {
        std::lock_guard<std::mutex> lock(g_mesh_first_preview_mutex);
        return g_mesh_first_preview_snapshot;
    }

    auto mesh_first_get_dominant_material_properties(Reflection& ref, std::uintptr_t component) -> MeshFirstMaterialProperties
    {
        MeshFirstMaterialProperties out{};
        if (!live_uobject(component))
        {
            out.failure = "paint_component_unavailable";
            return out;
        }
        const auto function = ref.find_function(component, "GetDominantPaintMaterialPatterns");
        if (!function)
        {
            out.failure = "GetDominantPaintMaterialPatterns_unavailable";
            return out;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 512)
        {
            out.failure = "GetDominantPaintMaterialPatterns_params_size_invalid";
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
            if (name == "maxpatterns" && offset + static_cast<int>(sizeof(std::int32_t)) <= params_size)
            {
                *reinterpret_cast<std::int32_t*>(params.data() + offset) = 4;
            }
            else if (name == "samplestep" && offset + static_cast<int>(sizeof(std::int32_t)) <= params_size)
            {
                *reinterpret_cast<std::int32_t*>(params.data() + offset) = 8;
            }
            else if (name == "alphathreshold" && offset + static_cast<int>(sizeof(float)) <= params_size)
            {
                *reinterpret_cast<float*>(params.data() + offset) = 0.01f;
            }
        }

        std::string failure{};
        if (!process_event(component, function, params.data(), failure))
        {
            out.failure = "GetDominantPaintMaterialPatterns_failed:" + failure;
            return out;
        }

        bool return_value = true;
        sdk::TArray<MeshFirstPaintMaterialPattern> patterns{};
        bool found_patterns = false;
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
            else if (name == "outpatterns" && offset + static_cast<int>(sizeof(sdk::TArray<MeshFirstPaintMaterialPattern>)) <= params_size)
            {
                patterns = *reinterpret_cast<sdk::TArray<MeshFirstPaintMaterialPattern>*>(params.data() + offset);
                found_patterns = true;
            }
        }
        if (!return_value)
        {
            out.failure = "GetDominantPaintMaterialPatterns_return_false";
            return out;
        }
        if (!found_patterns || !patterns.Data || patterns.Num <= 0 || patterns.Max < patterns.Num || patterns.Num > 64)
        {
            out.failure = "GetDominantPaintMaterialPatterns_no_patterns";
            return out;
        }

        std::vector<MeshFirstPaintMaterialPattern> copied(static_cast<std::size_t>(patterns.Num));
        if (!safe_copy(copied.data(), patterns.Data, copied.size() * sizeof(MeshFirstPaintMaterialPattern)))
        {
            out.failure = "GetDominantPaintMaterialPatterns_copy_failed";
            return out;
        }

        const MeshFirstPaintMaterialPattern* best = nullptr;
        for (const auto& pattern : copied)
        {
            if (!best ||
                pattern.sample_count > best->sample_count ||
                (pattern.sample_count == best->sample_count && pattern.coverage_ratio > best->coverage_ratio))
            {
                best = &pattern;
            }
        }
        if (!best)
        {
            out.failure = "GetDominantPaintMaterialPatterns_empty_after_copy";
            return out;
        }

        out.ok = true;
        out.metallic = clamp01(best->metallic);
        out.roughness = clamp01(best->roughness);
        out.coverage_ratio = clamp01(best->coverage_ratio);
        out.sample_count = std::max(0, static_cast<int>(best->sample_count));
        out.patterns = static_cast<int>(copied.size());
        out.failure = "ok";
        return out;
    }

    auto mesh_first_hash_channel_bytes(const std::vector<std::uint8_t>& bytes) -> std::uint64_t
    {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto value : bytes)
        {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    auto mesh_first_export_channel_bytes(Reflection& ref,
                                         std::uintptr_t component,
                                         sdk::EPaintChannel channel) -> MeshFirstChannelBytes
    {
        MeshFirstChannelBytes out{};
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
        out.bytes.resize(static_cast<std::size_t>(bytes.Num));
        if (!safe_copy(out.bytes.data(), bytes.Data, out.bytes.size()))
        {
            out.bytes.clear();
            out.failure = "ExportChannelToBytes_copy_failed";
            return out;
        }
        out.ok = true;
        out.failure = "ok";
        return out;
    }

    auto mesh_first_export_channel_checksum(Reflection& ref,
                                            std::uintptr_t component,
                                            sdk::EPaintChannel channel) -> MeshFirstChannelChecksum
    {
        MeshFirstChannelChecksum out{};
        const auto bytes = mesh_first_export_channel_bytes(ref, component, channel);
        if (!bytes.ok)
        {
            out.failure = bytes.failure;
            return out;
        }
        out.ok = true;
        out.failure.clear();
        out.bytes = static_cast<int>(bytes.bytes.size());
        out.hash = mesh_first_hash_channel_bytes(bytes.bytes);
        return out;
    }

    auto mesh_first_import_channel_bytes(Reflection& ref,
                                         std::uintptr_t component,
                                         sdk::EPaintChannel channel,
                                         std::vector<std::uint8_t>& bytes,
                                         std::string& failure) -> bool
    {
        if (!live_uobject(component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        if (bytes.empty() || bytes.size() > 64 * 1024 * 1024)
        {
            failure = "import_bytes_invalid";
            return false;
        }
        const auto function = ref.find_function(component, "ImportChannelFromBytes");
        if (!function)
        {
            failure = "ImportChannelFromBytes_unavailable";
            return false;
        }
        const auto params_size = safe_read<int>(function + OffPropertiesSize, 0);
        if (params_size <= 0 || params_size > 256)
        {
            failure = "ImportChannelFromBytes_params_size_invalid";
            return false;
        }
        std::vector<std::uint8_t> params(static_cast<std::size_t>(params_size), 0);
        bool wrote_channel = false;
        bool wrote_data = false;
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
                wrote_channel = true;
            }
            else if ((name == "data" || name == "indata" || name == "bytes" || name == "channeldata") &&
                     offset + static_cast<int>(sizeof(sdk::TArray<std::uint8_t>)) <= params_size)
            {
                auto* array = reinterpret_cast<sdk::TArray<std::uint8_t>*>(params.data() + offset);
                array->Data = bytes.data();
                array->Num = static_cast<std::int32_t>(bytes.size());
                array->Max = static_cast<std::int32_t>(bytes.size());
                wrote_data = true;
            }
        }
        if (!wrote_channel || !wrote_data)
        {
            failure = "ImportChannelFromBytes_schema_unmatched";
            return false;
        }
        if (!process_event(component, function, params.data(), failure))
        {
            failure = "ImportChannelFromBytes_failed:" + failure;
            return false;
        }
        if (!read_return_bool(ref, function, params.data()))
        {
            failure = "ImportChannelFromBytes_return_false";
            return false;
        }
        failure = "ok";
        return true;
    }

    struct MeshFirstLocalTextureImportResult
    {
        bool ok{false};
        bool export_ok{false};
        bool import_ok{false};
        int texture_size{0};
        int source_bytes{0};
        int strokes_considered{0};
        int strokes_painted{0};
        int pixels_touched{0};
        int pixels_changed{0};
        std::uint64_t before_hash{1469598103934665603ULL};
        std::uint64_t preview_hash{1469598103934665603ULL};
        double elapsed_ms{0.0};
        std::string failure{"not_run"};
    };

    auto mesh_first_apply_local_material_import_preview(Reflection& ref,
                                                        std::uintptr_t component,
                                                        const std::vector<sdk::FPaintStroke>& strokes,
                                                        int texture_size,
                                                        const std::vector<std::uint8_t>* base_albedo_bytes = nullptr,
                                                        const std::vector<std::uint8_t>* base_metallic_bytes = nullptr,
                                                        const std::vector<std::uint8_t>* base_roughness_bytes = nullptr) -> MeshFirstLocalTextureImportResult
    {
        const auto started = std::chrono::steady_clock::now();
        MeshFirstLocalTextureImportResult out{};
        out.texture_size = texture_size;
        const int size = std::max(1, texture_size);
        const std::size_t expected_bytes = static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4U;
        MeshFirstChannelBytes albedo{};
        MeshFirstChannelBytes metallic{};
        MeshFirstChannelBytes roughness{};
        auto prepare_channel = [&](sdk::EPaintChannel paint_channel,
                                   const std::vector<std::uint8_t>* base_bytes,
                                   const char* label,
                                   MeshFirstChannelBytes& channel) -> bool {
            if (base_bytes && !base_bytes->empty())
            {
                channel.ok = true;
                channel.bytes = *base_bytes;
                channel.failure = "ok";
            }
            else
            {
                channel = mesh_first_export_channel_bytes(ref, component, paint_channel);
            }
            if (!channel.ok)
            {
                out.failure = std::string(label) + "_export_failed:" + channel.failure;
                return false;
            }
            if (channel.bytes.size() != expected_bytes)
            {
                out.failure = std::string(label) + "_export_size_mismatch";
                return false;
            }
            out.source_bytes += static_cast<int>(channel.bytes.size());
            return true;
        };

        out.export_ok =
            prepare_channel(sdk::EPaintChannel::Albedo, base_albedo_bytes, "albedo", albedo) &&
            prepare_channel(sdk::EPaintChannel::Metallic, base_metallic_bytes, "metallic", metallic) &&
            prepare_channel(sdk::EPaintChannel::Roughness, base_roughness_bytes, "roughness", roughness);
        if (!out.export_ok)
        {
            return out;
        }
        out.before_hash = mesh_first_hash_channel_bytes(albedo.bytes);
        out.before_hash ^= mesh_first_hash_channel_bytes(metallic.bytes);
        out.before_hash *= 1099511628211ULL;
        out.before_hash ^= mesh_first_hash_channel_bytes(roughness.bytes);
        out.before_hash *= 1099511628211ULL;

        for (const auto& stroke : strokes)
        {
            const bool paint_albedo = stroke.TargetChannel == sdk::EPaintChannel::Albedo ||
                                      stroke.TargetChannel == sdk::EPaintChannel::All;
            const bool paint_metallic = stroke.TargetChannel == sdk::EPaintChannel::Metallic ||
                                        stroke.TargetChannel == sdk::EPaintChannel::All;
            const bool paint_roughness = stroke.TargetChannel == sdk::EPaintChannel::Roughness ||
                                         stroke.TargetChannel == sdk::EPaintChannel::All;
            if (!paint_albedo && !paint_metallic && !paint_roughness)
            {
                continue;
            }
            ++out.strokes_considered;
            const double u = clamp01(stroke.Uv.X);
            const double v = clamp01(stroke.Uv.Y);
            const int cx = std::max(0, std::min(size - 1, static_cast<int>(std::lround(u * static_cast<double>(size - 1)))));
            const int cy = std::max(0, std::min(size - 1, static_cast<int>(std::lround(v * static_cast<double>(size - 1)))));
            const int radius = std::max(1, static_cast<int>(std::lround(std::max(0.0, static_cast<double>(stroke.BrushSettings.Radius)) * static_cast<double>(size))));
            const int radius_sq = radius * radius;
            const auto r = static_cast<std::uint8_t>(std::lround(clamp01(stroke.ChannelData.AlbedoColor.R) * 255.0));
            const auto g = static_cast<std::uint8_t>(std::lround(clamp01(stroke.ChannelData.AlbedoColor.G) * 255.0));
            const auto b = static_cast<std::uint8_t>(std::lround(clamp01(stroke.ChannelData.AlbedoColor.B) * 255.0));
            const auto m = static_cast<std::uint8_t>(std::lround(clamp01(stroke.ChannelData.Metallic) * 255.0));
            const auto ro = static_cast<std::uint8_t>(std::lround(clamp01(stroke.ChannelData.Roughness) * 255.0));
            bool painted = false;
            const int min_y = std::max(0, cy - radius);
            const int max_y = std::min(size - 1, cy + radius);
            const int min_x = std::max(0, cx - radius);
            const int max_x = std::min(size - 1, cx + radius);
            for (int y = min_y; y <= max_y; ++y)
            {
                const int dy = y - cy;
                for (int x = min_x; x <= max_x; ++x)
                {
                    const int dx = x - cx;
                    if (dx * dx + dy * dy > radius_sq)
                    {
                        continue;
                    }
                    const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                                         static_cast<std::size_t>(x)) * 4U;
                    bool changed = false;
                    if (paint_albedo)
                    {
                        changed = changed ||
                                  albedo.bytes[offset + 0] != r ||
                                  albedo.bytes[offset + 1] != g ||
                                  albedo.bytes[offset + 2] != b ||
                                  albedo.bytes[offset + 3] != 255;
                        albedo.bytes[offset + 0] = r;
                        albedo.bytes[offset + 1] = g;
                        albedo.bytes[offset + 2] = b;
                        albedo.bytes[offset + 3] = 255;
                    }
                    if (paint_metallic)
                    {
                        changed = changed ||
                                  metallic.bytes[offset + 0] != m ||
                                  metallic.bytes[offset + 1] != m ||
                                  metallic.bytes[offset + 2] != m ||
                                  metallic.bytes[offset + 3] != 255;
                        metallic.bytes[offset + 0] = m;
                        metallic.bytes[offset + 1] = m;
                        metallic.bytes[offset + 2] = m;
                        metallic.bytes[offset + 3] = 255;
                    }
                    if (paint_roughness)
                    {
                        changed = changed ||
                                  roughness.bytes[offset + 0] != ro ||
                                  roughness.bytes[offset + 1] != ro ||
                                  roughness.bytes[offset + 2] != ro ||
                                  roughness.bytes[offset + 3] != 255;
                        roughness.bytes[offset + 0] = ro;
                        roughness.bytes[offset + 1] = ro;
                        roughness.bytes[offset + 2] = ro;
                        roughness.bytes[offset + 3] = 255;
                    }
                    ++out.pixels_touched;
                    if (changed)
                    {
                        ++out.pixels_changed;
                    }
                    painted = true;
                }
            }
            if (painted)
            {
                ++out.strokes_painted;
            }
        }

        out.preview_hash = mesh_first_hash_channel_bytes(albedo.bytes);
        out.preview_hash ^= mesh_first_hash_channel_bytes(metallic.bytes);
        out.preview_hash *= 1099511628211ULL;
        out.preview_hash ^= mesh_first_hash_channel_bytes(roughness.bytes);
        out.preview_hash *= 1099511628211ULL;
        if (out.pixels_changed <= 0 || out.preview_hash == out.before_hash)
        {
            out.import_ok = true;
            out.ok = true;
            out.failure.clear();
            out.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            return out;
        }

        std::string import_failure{};
        auto import_channel = [&](sdk::EPaintChannel paint_channel,
                                  std::vector<std::uint8_t>& bytes,
                                  const char* label) -> bool {
            std::string channel_failure{};
            if (!mesh_first_import_channel_bytes(ref, component, paint_channel, bytes, channel_failure))
            {
                import_failure = std::string(label) + "_import_failed:" + channel_failure;
                return false;
            }
            return true;
        };
        out.import_ok =
            import_channel(sdk::EPaintChannel::Albedo, albedo.bytes, "albedo") &&
            import_channel(sdk::EPaintChannel::Metallic, metallic.bytes, "metallic") &&
            import_channel(sdk::EPaintChannel::Roughness, roughness.bytes, "roughness");
        if (!out.import_ok)
        {
            out.failure = import_failure;
            return out;
        }
        out.ok = true;
        out.failure.clear();
        out.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
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

    enum class MeshFirstRegionMode
    {
        Paint,
        Fill,
        Skip,
    };

    auto mesh_first_region_mode_name(MeshFirstRegionMode mode) -> const char*
    {
        if (mode == MeshFirstRegionMode::Fill)
            return "fill";
        if (mode == MeshFirstRegionMode::Skip)
            return "skip";
        return "paint";
    }

    auto mesh_first_parse_region_mode(const std::string& request,
                                      const char* mode_key,
                                      const char* legacy_enable_key) -> MeshFirstRegionMode
    {
        const auto mode = lower_copy(json_string_field(request, mode_key, ""));
        if (mode == "fill")
            return MeshFirstRegionMode::Fill;
        if (mode == "skip")
            return MeshFirstRegionMode::Skip;
        if (mode == "paint")
            return MeshFirstRegionMode::Paint;
        return json_bool_field(request, legacy_enable_key, true) ? MeshFirstRegionMode::Paint : MeshFirstRegionMode::Fill;
    }

    auto mesh_first_region_mode_for_sample(MeshFirstRegion region,
                                           MeshFirstRegionMode front,
                                           MeshFirstRegionMode side,
                                           MeshFirstRegionMode back) -> MeshFirstRegionMode
    {
        if (region == MeshFirstRegion::Side)
            return side;
        if (region == MeshFirstRegion::Back)
            return back;
        return front;
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
        std::string mode{"stable_runtime_coordinates"};
        int samples{0};
        int source_candidates{0};
        int project_ok{0};
        int inside_view{0};
        int best_score{0};
        std::string summary{};
    };

    struct MeshFirstRuntimeTriangleWorldRebuild
    {
        bool applied{false};
        int samples{0};
        double avg_delta{0.0};
        double max_delta{0.0};
        std::string mode{"not_run"};
    };

    struct MeshFirstRuntimePaintWarmup
    {
        bool attempted{false};
        std::string reason{};
        bool is_initialized_available{false};
        bool is_initialized_before_ok{false};
        bool is_initialized_before{false};
        bool is_initialized_after_ok{false};
        bool is_initialized_after{false};
        bool initialize_available{false};
        bool initialize_called{false};
        bool initialize_ok{false};
        std::string initialize_skip_reason{};
        std::uintptr_t initialized_mesh_before{0};
        std::uintptr_t initialized_mesh_after{0};
        bool hit_test_available{false};
        bool hit_test_uncached_called{false};
        bool hit_test_uncached_ok{false};
        bool hit_test_uncached_hit{false};
        bool hit_test_cached_called{false};
        bool hit_test_cached_ok{false};
        bool hit_test_cached_hit{false};
        std::string failure{};
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
        int unsafe_body_region{0};
        int unsafe_limb_group{0};
        int unsafe_source_distance{0};
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

    auto mesh_first_transfer_group_for_bone(const MeshFirstProfile* profile, int bone_index) -> std::string
    {
        if (!profile || bone_index < 0 || bone_index >= static_cast<int>(profile->bones.size()))
        {
            return {};
        }
        const auto name = lower_copy(profile->bones[static_cast<std::size_t>(bone_index)].name);
        if (name.empty())
        {
            return {};
        }
        const bool left = name.size() >= 2 &&
                          (name.rfind("_l") == name.size() - 2 ||
                           name.rfind(".l") == name.size() - 2 ||
                           name.rfind("-l") == name.size() - 2);
        const bool right = name.size() >= 2 &&
                           (name.rfind("_r") == name.size() - 2 ||
                            name.rfind(".r") == name.size() - 2 ||
                            name.rfind("-r") == name.size() - 2);
        if (!left && !right)
        {
            return {};
        }
        if (contains_text(name, "leg") || contains_text(name, "foot") || contains_text(name, "hip"))
        {
            return left ? "leg_l" : "leg_r";
        }
        if (contains_text(name, "arm") || contains_text(name, "hand") || contains_text(name, "shoulder"))
        {
            return left ? "arm_l" : "arm_r";
        }
        return {};
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

    // =============================================================================
    // Section: Runtime triangle cache and mesh-first paint planning
    // Risk: high. Planner guards block unsafe samples instead of guessing.
    // =============================================================================

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

    auto mesh_first_select_runtime_triangle_projection_coordinates(Reflection& ref,
                                                                  const SdkContext& ctx,
                                                                  std::vector<MeshFirstRuntimeTriangle>& triangles,
                                                                  const sdk::FVector& camera_location,
                                                                  const sdk::FVector& camera_direction,
                                                                  const SdkViewportInfo& viewport,
                                                                  const std::string& mode = "stable_runtime_coordinates") -> MeshFirstRuntimeTriangleProjectionSelection
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
            {mode},
        };
        const int step = std::max(1, static_cast<int>(triangles.size() / 256));
        const auto view_direction = sdk_vec_normalize(camera_direction);
        for (auto& candidate : candidates)
        {
            for (std::size_t tri = 0; tri < triangles.size(); tri += static_cast<std::size_t>(step))
            {
                const auto& triangle = triangles[tri];
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
        return out;
    }

    auto mesh_first_rebuild_runtime_triangle_world_from_local(std::vector<MeshFirstRuntimeTriangle>& triangles,
                                                              const sdk::FTransform& component_to_world) -> MeshFirstRuntimeTriangleWorldRebuild
    {
        MeshFirstRuntimeTriangleWorldRebuild out{};
        if (triangles.empty())
        {
            out.mode = "empty";
            return out;
        }

        double delta_sum = 0.0;
        double delta_max = 0.0;
        int samples = 0;
        for (auto& triangle : triangles)
        {
            for (int vertex = 0; vertex < 3; ++vertex)
            {
                const auto rebuilt_world = mesh_first_transform_apply_point(component_to_world, triangle.local[vertex]);
                const double delta = sdk_vec_len(sdk_vec_sub(triangle.world[vertex], rebuilt_world));
                if (std::isfinite(delta) && mesh_first_finite_vector(rebuilt_world))
                {
                    delta_sum += delta;
                    delta_max = std::max(delta_max, delta);
                    ++samples;
                    triangle.world[vertex] = rebuilt_world;
                }
            }
        }

        out.applied = samples > 0;
        out.samples = samples;
        out.avg_delta = samples > 0 ? delta_sum / static_cast<double>(samples) : 0.0;
        out.max_delta = delta_max;
        out.mode = out.applied ? "local_component_world" : "unavailable";
        return out;
    }

    auto mesh_first_warm_runtime_paint_cache(Reflection& ref,
                                             const SdkContext& ctx,
                                             std::uintptr_t mesh,
                                             const SdkViewportInfo& viewport,
                                             const char* reason) -> MeshFirstRuntimePaintWarmup
    {
        MeshFirstRuntimePaintWarmup out{};
        out.attempted = true;
        out.reason = reason ? reason : "unknown";
        out.initialized_mesh_before = call_no_params_return_object(ref, ctx.component, "GetInitializedPaintMesh");

        const auto initialized_before = call_no_params_return_bool_detail(ref, ctx.component, "IsInitialized");
        out.is_initialized_available = initialized_before.available;
        out.is_initialized_before_ok = initialized_before.process_ok;
        out.is_initialized_before = initialized_before.value;
        if (!initialized_before.failure.empty())
        {
            out.failure += "is_initialized_before:" + initialized_before.failure;
        }

        out.initialize_available = ref.find_function(ctx.component, "InitializePaint") != 0;
        out.initialize_called = false;
        out.initialize_ok = false;
        out.initialize_skip_reason = out.initialize_available
            ? "skipped_non_destructive_hittest_warmup"
            : "initialize_paint_unavailable";

        const auto hit_test_function = ref.find_function(ctx.component, "HitTestAtScreenPosition");
        out.hit_test_available = hit_test_function != 0;
        auto run_hit_test = [&](bool use_cached, bool& called, bool& ok, bool& hit) {
            if (!hit_test_function || !live_uobject(mesh) || !live_uobject(ctx.controller) ||
                viewport.width <= 0 || viewport.height <= 0)
            {
                return;
            }
            called = true;
            sdk::RuntimePaintableComponent_HitTestAtScreenPosition params{};
            params.MeshComponent = reinterpret_cast<void*>(mesh);
            params.ScreenPosition = sdk::FVector2D{static_cast<double>(viewport.width) * 0.5,
                                                   static_cast<double>(viewport.height) * 0.5};
            params.PlayerController = reinterpret_cast<void*>(ctx.controller);
            params.bUseCachedTriangles = use_cached;
            std::string failure{};
            ok = process_event(ctx.component, hit_test_function, reinterpret_cast<std::uint8_t*>(&params), failure);
            hit = ok && params.ReturnValue.bSuccess;
            if (!ok && !failure.empty())
            {
                if (!out.failure.empty())
                {
                    out.failure += ";";
                }
                out.failure += std::string(use_cached ? "hit_test_cached:" : "hit_test_uncached:") + failure;
            }
        };
        run_hit_test(false, out.hit_test_uncached_called, out.hit_test_uncached_ok, out.hit_test_uncached_hit);
        run_hit_test(true, out.hit_test_cached_called, out.hit_test_cached_ok, out.hit_test_cached_hit);

        out.initialized_mesh_after = call_no_params_return_object(ref, ctx.component, "GetInitializedPaintMesh");
        const auto initialized_after = call_no_params_return_bool_detail(ref, ctx.component, "IsInitialized");
        out.is_initialized_after_ok = initialized_after.process_ok;
        out.is_initialized_after = initialized_after.value;
        if (!initialized_after.failure.empty())
        {
            if (!out.failure.empty())
            {
                out.failure += ";";
            }
            out.failure += "is_initialized_after:" + initialized_after.failure;
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
        samples.reserve(std::min<std::size_t>(static_cast<std::size_t>(std::max(1, expected_triangles)) * 8, 100000));

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

    auto mesh_first_assign_colors(const MeshFirstProfile* profile,
                                  std::vector<MeshFirstPlanSample>& samples,
                                  const SdkFrontCaptureResult& capture,
                                  bool enable_front,
                                  bool enable_side,
                                  bool enable_back,
                                  double side_source_max_uv,
                                  double front_back_source_max_uv,
                                  MeshFirstPlanStats& stats) -> void
    {
        const auto& source_samples = capture.samples;
        std::vector<double> uv_distances{};
        std::vector<double> component_distances{};
        uv_distances.reserve(samples.size());
        component_distances.reserve(samples.size());
        auto unknown_body = [](const std::string& value) -> bool {
            return value.empty() || value == "unknown" || value == "runtime";
        };
        auto transfer_key = [&](const std::string& body, const std::string& transfer_group) -> std::string {
            if (!transfer_group.empty())
            {
                return transfer_group;
            }
            if (!unknown_body(body))
            {
                return body;
            }
            return "__unknown";
        };
        std::vector<std::string> source_keys{};
        std::vector<std::vector<int>> source_bins{};
        auto source_bin_for_key = [&](const std::string& key) -> int {
            for (int i = 0; i < static_cast<int>(source_keys.size()); ++i)
            {
                if (source_keys[static_cast<std::size_t>(i)] == key)
                {
                    return i;
                }
            }
            source_keys.push_back(key);
            source_bins.emplace_back();
            return static_cast<int>(source_bins.size() - 1);
        };
        for (int i = 0; i < static_cast<int>(source_samples.size()); ++i)
        {
            const auto& source = source_samples[static_cast<std::size_t>(i)];
            const auto source_body = lower_copy(source.body_region);
            const auto source_group = mesh_first_transfer_group_for_bone(profile, source.dominant_bone);
            source_bins[static_cast<std::size_t>(source_bin_for_key(transfer_key(source_body, source_group)))].push_back(i);
        }
        std::vector<const FrontSample*> direct_source_by_plan_index(samples.size(), nullptr);
        for (const auto& source : source_samples)
        {
            if (source.plan_index >= 0 && source.plan_index < static_cast<int>(direct_source_by_plan_index.size()))
            {
                direct_source_by_plan_index[static_cast<std::size_t>(source.plan_index)] = &source;
            }
        }
        const double side_component_distance_limit = clamp_range(side_source_max_uv * 500.0, 20.0, 80.0);
        const double front_back_component_distance_limit = clamp_range(front_back_source_max_uv * 500.0, 20.0, 200.0);
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
                if (profile)
                {
                    const auto sample_body = lower_copy(sample.body_region);
                    const auto sample_transfer_group = mesh_first_transfer_group_for_bone(profile, sample.dominant_bone);
                    const auto sample_key = transfer_key(sample_body, sample_transfer_group);
                    int sample_bin = -1;
                    for (int i = 0; i < static_cast<int>(source_keys.size()); ++i)
                    {
                        if (source_keys[static_cast<std::size_t>(i)] == sample_key)
                        {
                            sample_bin = i;
                            break;
                        }
                    }
                    bool saw_candidate = false;
                    double best_distance_sq = std::numeric_limits<double>::infinity();
                    double best_uv_distance_sq = std::numeric_limits<double>::infinity();
                    const FrontSample* best = nullptr;
                    const auto* candidate_indices = sample_bin >= 0 ? &source_bins[static_cast<std::size_t>(sample_bin)] : nullptr;
                    if (!candidate_indices || candidate_indices->empty())
                    {
                        for (int i = 0; i < static_cast<int>(source_samples.size()); ++i)
                        {
                            const auto& source = source_samples[static_cast<std::size_t>(i)];
                            if (!source.has_component_position)
                            {
                                continue;
                            }
                            saw_candidate = true;
                            const auto component_delta = sdk_vec_sub(sample.local_position, source.component_position);
                            const double component_distance_sq = sdk_vec_dot(component_delta, component_delta);
                            if (!std::isfinite(component_distance_sq))
                            {
                                continue;
                            }
                            const double du = sample.u - source.u;
                            const double dv = sample.v - source.v;
                            const double uv_distance_sq = du * du + dv * dv;
                            if (component_distance_sq < best_distance_sq ||
                                (component_distance_sq == best_distance_sq && uv_distance_sq < best_uv_distance_sq))
                            {
                                best_distance_sq = component_distance_sq;
                                best_uv_distance_sq = uv_distance_sq;
                                best = &source;
                            }
                        }
                    }
                    else
                    {
                        for (const int source_index : *candidate_indices)
                        {
                            const auto& source = source_samples[static_cast<std::size_t>(source_index)];
                            if (!source.has_component_position)
                            {
                                continue;
                            }
                            saw_candidate = true;
                            const auto component_delta = sdk_vec_sub(sample.local_position, source.component_position);
                            const double component_distance_sq = sdk_vec_dot(component_delta, component_delta);
                            if (!std::isfinite(component_distance_sq))
                            {
                                continue;
                            }
                            const double du = sample.u - source.u;
                            const double dv = sample.v - source.v;
                            const double uv_distance_sq = du * du + dv * dv;
                            if (component_distance_sq < best_distance_sq ||
                                (component_distance_sq == best_distance_sq && uv_distance_sq < best_uv_distance_sq))
                            {
                                best_distance_sq = component_distance_sq;
                                best_uv_distance_sq = uv_distance_sq;
                                best = &source;
                            }
                        }
                    }
                    const double region_distance_limit = sample.region == MeshFirstRegion::Side
                                                             ? side_component_distance_limit
                                                             : front_back_component_distance_limit;
                    if (best)
                    {
                        sample.source_distance_component = std::sqrt(best_distance_sq);
                        sample.source_distance_uv = std::sqrt(best_uv_distance_sq);
                        component_distances.push_back(sample.source_distance_component);
                        uv_distances.push_back(sample.source_distance_uv);
                        sample.r = clamp01(best->r);
                        sample.g = clamp01(best->g);
                        sample.b = clamp01(best->b);
                        sample.roughness = clamp01(std::max(0.35, best->roughness));
                        sample.metallic = clamp01(best->metallic);
                        sample.unsafe = !std::isfinite(sample.source_distance_component) ||
                                        sample.source_distance_component > region_distance_limit;
                        if (sample.unsafe)
                        {
                            ++stats.unsafe_source_distance;
                        }
                    }
                    else
                    {
                        sample.source_distance_uv = std::numeric_limits<double>::infinity();
                        sample.source_distance_component = std::numeric_limits<double>::infinity();
                        sample.unsafe = true;
                        if (!saw_candidate)
                        {
                            if (!sample_transfer_group.empty())
                            {
                                ++stats.unsafe_limb_group;
                            }
                            else
                            {
                                ++stats.unsafe_body_region;
                            }
                        }
                    }
                    if (!sample.unsafe)
                    {
                        ++stats.source_projection_assignments;
                        ++stats.enabled_samples;
                        continue;
                    }
                }
                {
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

    auto mesh_first_write_projection_debug_artifact(const std::vector<MeshFirstPlanSample>& samples,
                                                    const SdkFrontCaptureResult& capture,
                                                    bool enable_front,
                                                    bool enable_side,
                                                    bool enable_back,
                                                    std::string& metadata) -> void
    {
        const auto dir = runtime_log_dir_path();
        if (dir.empty() || capture.width <= 0 || capture.height <= 0)
        {
            metadata += ",\"mesh_debug_projection_written\":false";
            metadata += ",\"mesh_debug_projection_failure\":\"runtime_log_dir_or_capture_unavailable\"";
            return;
        }
        const int width = capture.width;
        const int height = capture.height;
        const auto stamp = std::to_wstring(GetTickCount64());
        const auto projection_path = dir + L"\\mesh-first-screen-projection-" + stamp + L".bmp";
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3, 8);
        if (capture.capture_pixels_available &&
            capture.capture_pixels.size() >= static_cast<std::size_t>(width) * static_cast<std::size_t>(height))
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                    const auto& pixel = capture.capture_pixels[index];
                    const auto out = index * 3;
                    rgb[out + 0] = static_cast<std::uint8_t>(std::round(clamp01(pixel.r) * 255.0));
                    rgb[out + 1] = static_cast<std::uint8_t>(std::round(clamp01(pixel.g) * 255.0));
                    rgb[out + 2] = static_cast<std::uint8_t>(std::round(clamp01(pixel.b) * 255.0));
                }
            }
        }

        auto draw_disc = [&](int cx, int cy, int radius, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    if (dx * dx + dy * dy > radius * radius)
                    {
                        continue;
                    }
                    const int x = cx + dx;
                    const int y = cy + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height)
                    {
                        continue;
                    }
                    const auto out = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3;
                    rgb[out + 0] = r;
                    rgb[out + 1] = g;
                    rgb[out + 2] = b;
                }
            }
        };

        auto project_to_capture = [&](const sdk::FVector& world_position, int& out_x, int& out_y) -> bool {
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
            const double sx = (ndc_x * 0.5 + 0.5) * static_cast<double>(width);
            const double sy = (0.5 - ndc_y * 0.5) * static_cast<double>(height);
            if (sx < 0.0 || sy < 0.0 || sx >= static_cast<double>(width) || sy >= static_cast<double>(height))
            {
                return false;
            }
            out_x = std::max(0, std::min(width - 1, static_cast<int>(std::round(sx))));
            out_y = std::max(0, std::min(height - 1, static_cast<int>(std::round(sy))));
            return true;
        };

        int source_drawn = 0;
        for (const auto& source : capture.samples)
        {
            const int x = std::max(0, std::min(width - 1, static_cast<int>(std::round(clamp01(source.screen_nx) * static_cast<double>(width - 1)))));
            const int y = std::max(0, std::min(height - 1, static_cast<int>(std::round(clamp01(source.screen_ny) * static_cast<double>(height - 1)))));
            draw_disc(x, y, 2, 255, 255, 255);
            ++source_drawn;
        }

        int plan_drawn = 0;
        int plan_project_failed = 0;
        for (const auto& sample : samples)
        {
            const bool enabled = (sample.region == MeshFirstRegion::Front && enable_front) ||
                                 (sample.region == MeshFirstRegion::Side && enable_side) ||
                                 (sample.region == MeshFirstRegion::Back && enable_back);
            if (!enabled || sample.unsafe)
            {
                continue;
            }
            int x = 0;
            int y = 0;
            if (!project_to_capture(sample.world_position, x, y))
            {
                ++plan_project_failed;
                continue;
            }
            if (sample.region == MeshFirstRegion::Front)
            {
                draw_disc(x, y, 1, 255, 70, 70);
            }
            else if (sample.region == MeshFirstRegion::Side)
            {
                draw_disc(x, y, 1, 80, 230, 110);
            }
            else
            {
                draw_disc(x, y, 1, 100, 150, 255);
            }
            ++plan_drawn;
        }

        const bool projection_ok = mesh_first_write_bmp_rgb(projection_path, width, height, rgb);
        auto narrow = [](const std::wstring& value) {
            std::string out{};
            out.reserve(value.size());
            for (const auto ch : value)
            {
                out.push_back(ch >= 0 && ch < 128 ? static_cast<char>(ch) : '?');
            }
            return out;
        };
        metadata += ",\"mesh_debug_projection_written\":" + std::string(json_bool(projection_ok));
        metadata += ",\"mesh_debug_projection_source_samples\":" + std::to_string(source_drawn);
        metadata += ",\"mesh_debug_projection_plan_samples\":" + std::to_string(plan_drawn);
        metadata += ",\"mesh_debug_projection_failed\":" + std::to_string(plan_project_failed);
        metadata += ",\"mesh_debug_screen_projection_bmp\":\"" + json_escape(narrow(projection_path)) + "\"";
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
               ",\"unsafe_body_region\":" + std::to_string(stats.unsafe_body_region) +
               ",\"unsafe_limb_group\":" + std::to_string(stats.unsafe_limb_group) +
               ",\"unsafe_source_distance\":" + std::to_string(stats.unsafe_source_distance) +
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

    struct MeshFirstRuntimePaintReplicationPressure
    {
        int QueuedBatchCount{0};
        int QueuedStrokeCount{0};
        int MaxStrokesPerTick{0};
        float EstimatedTicksToDrain{0.0f};
    };
    static_assert(sizeof(MeshFirstRuntimePaintReplicationPressure) == 0x10,
                  "RuntimePaintReplicationPressure layout mismatch");

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
        bool manager_pressure_available{false};
        MeshFirstRuntimePaintReplicationPressure pressure{};
        int component_max_replicated_strokes_per_tick{-1};
        std::uint8_t component_use_compact_replication{0};
        std::uint8_t component_use_packed_replication{0};
        int manager_max_replicated_strokes_per_tick{-1};
        int manager_max_render_target_writes_per_frame{-1};
        int manager_max_outgoing_strokes_per_batch{-1};
        int manager_max_outgoing_network_batches_per_second{-1};
        std::uint8_t manager_coalesce_outgoing_strokes{0};
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

    struct MeshFirstGetReplicationPressureParams
    {
        MeshFirstRuntimePaintReplicationPressure ReturnValue{};
    };
    static_assert(sizeof(MeshFirstGetReplicationPressureParams) == 0x10,
                  "GetReplicationPressure params layout mismatch");

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
            snapshot.component_max_replicated_strokes_per_tick =
                read_object_i32_property(ref, component, "MaxReplicatedPaintStrokesPerTick", -1);
            snapshot.component_use_compact_replication =
                read_object_u8_property(ref, component, "bUseCompactPaintReplication", 0);
            snapshot.component_use_packed_replication =
                read_object_u8_property(ref, component, "bUseExperimentalPackedPaintReplication", 0);
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

        if (const auto function = ref.find_function(manager, "GetReplicationPressure"))
        {
            MeshFirstGetReplicationPressureParams params{};
            std::string failure{};
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

        snapshot.manager_max_replicated_strokes_per_tick =
            read_object_i32_property(ref, manager, "MaxReplicatedPaintStrokesPerTick", -1);
        snapshot.manager_max_render_target_writes_per_frame =
            read_object_i32_property(ref, manager, "MaxReplicatedPaintRenderTargetWritesPerFrame", -1);
        snapshot.manager_max_outgoing_strokes_per_batch =
            read_object_i32_property(ref, manager, "MaxOutgoingStrokesPerBatch", -1);
        snapshot.manager_max_outgoing_network_batches_per_second =
            read_object_i32_property(ref, manager, "MaxOutgoingNetworkBatchesPerSecond", -1);
        snapshot.manager_coalesce_outgoing_strokes =
            read_object_u8_property(ref, manager, "bCoalesceOutgoingStrokes", 0);

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
               ",\"" + key + "_manager_pressure_available\":" + json_bool(snapshot.manager_pressure_available) +
               ",\"" + key + "_queued_batch_count\":" + std::to_string(snapshot.pressure.QueuedBatchCount) +
               ",\"" + key + "_queued_stroke_count\":" + std::to_string(snapshot.pressure.QueuedStrokeCount) +
               ",\"" + key + "_max_strokes_per_tick\":" + std::to_string(snapshot.pressure.MaxStrokesPerTick) +
               ",\"" + key + "_estimated_ticks_to_drain\":" + std::to_string(snapshot.pressure.EstimatedTicksToDrain) +
               ",\"" + key + "_component_max_replicated_strokes_per_tick\":" + std::to_string(snapshot.component_max_replicated_strokes_per_tick) +
               ",\"" + key + "_component_use_compact_replication\":" + std::to_string(static_cast<unsigned>(snapshot.component_use_compact_replication)) +
               ",\"" + key + "_component_use_packed_replication\":" + std::to_string(static_cast<unsigned>(snapshot.component_use_packed_replication)) +
               ",\"" + key + "_manager_max_replicated_strokes_per_tick\":" + std::to_string(snapshot.manager_max_replicated_strokes_per_tick) +
               ",\"" + key + "_manager_max_render_target_writes_per_frame\":" + std::to_string(snapshot.manager_max_render_target_writes_per_frame) +
               ",\"" + key + "_manager_max_outgoing_strokes_per_batch\":" + std::to_string(snapshot.manager_max_outgoing_strokes_per_batch) +
               ",\"" + key + "_manager_max_outgoing_network_batches_per_second\":" + std::to_string(snapshot.manager_max_outgoing_network_batches_per_second) +
               ",\"" + key + "_manager_coalesce_outgoing_strokes\":" + std::to_string(static_cast<unsigned>(snapshot.manager_coalesce_outgoing_strokes)) +
               ",\"" + key + "_failure\":\"" + json_escape(snapshot.failure) + "\"";
    }

    auto mesh_first_pending_replication_strokes(const MeshFirstReplicationSnapshot& snapshot) -> int
    {
        if (snapshot.manager_component_queued_count_available && snapshot.manager_component_queued_count >= 0)
        {
            return snapshot.manager_component_queued_count;
        }
        if (snapshot.manager_queued_count_available && snapshot.manager_queued_count >= 0)
        {
            return snapshot.manager_queued_count;
        }
        return -1;
    }

    auto mesh_first_has_replication_pressure_signal(const MeshFirstReplicationSnapshot& snapshot) -> bool
    {
        return snapshot.manager_pressure_available ||
               snapshot.manager_component_queued_count_available ||
               snapshot.manager_queued_count_available;
    }

    auto mesh_first_pressure_queued_batch_count(const MeshFirstReplicationSnapshot& snapshot) -> int
    {
        if (snapshot.manager_pressure_available)
        {
            return std::max(0, snapshot.pressure.QueuedBatchCount);
        }
        return -1;
    }

    auto mesh_first_pressure_queued_stroke_count(const MeshFirstReplicationSnapshot& snapshot) -> int
    {
        if (snapshot.manager_component_queued_count_available && snapshot.manager_component_queued_count >= 0)
        {
            return snapshot.manager_component_queued_count;
        }
        if (snapshot.manager_pressure_available)
        {
            return std::max(0, snapshot.pressure.QueuedStrokeCount);
        }
        if (snapshot.manager_queued_count_available && snapshot.manager_queued_count >= 0)
        {
            return snapshot.manager_queued_count;
        }
        return -1;
    }

    auto mesh_first_pressure_max_strokes_per_tick(const MeshFirstReplicationSnapshot& snapshot) -> int
    {
        int limit = 0;
        if (snapshot.manager_pressure_available && snapshot.pressure.MaxStrokesPerTick > 0)
        {
            limit = snapshot.pressure.MaxStrokesPerTick;
        }
        if (snapshot.component_max_replicated_strokes_per_tick > 0)
        {
            limit = limit > 0 ? std::min(limit, snapshot.component_max_replicated_strokes_per_tick)
                              : snapshot.component_max_replicated_strokes_per_tick;
        }
        if (snapshot.manager_max_replicated_strokes_per_tick > 0)
        {
            limit = limit > 0 ? std::min(limit, snapshot.manager_max_replicated_strokes_per_tick)
                              : snapshot.manager_max_replicated_strokes_per_tick;
        }
        return limit > 0 ? limit : PackedReplicationFallbackMaxStrokesPerTick;
    }

    auto mesh_first_pressure_max_outgoing_strokes_per_batch(const MeshFirstReplicationSnapshot& snapshot) -> int
    {
        if (snapshot.manager_max_outgoing_strokes_per_batch > 0)
        {
            return snapshot.manager_max_outgoing_strokes_per_batch;
        }
        return PackedReplicationBatchSize;
    }

    auto mesh_first_pressure_outgoing_batches_per_second(const MeshFirstReplicationSnapshot& snapshot) -> int
    {
        if (snapshot.manager_max_outgoing_network_batches_per_second > 0)
        {
            return snapshot.manager_max_outgoing_network_batches_per_second;
        }
        return PackedReplicationFallbackOutgoingBatchesPerSecond;
    }

    auto mesh_first_compact_replication_enabled(const MeshFirstReplicationSnapshot& snapshot) -> bool
    {
        return snapshot.component_use_compact_replication != 0;
    }

    auto mesh_first_pressure_reported_max_strokes_per_tick(const MeshFirstReplicationSnapshot& snapshot) -> int
    {
        if (snapshot.manager_pressure_available)
        {
            return std::max(0, snapshot.pressure.MaxStrokesPerTick);
        }
        return -1;
    }

    auto mesh_first_pressure_estimated_ticks_to_drain(const MeshFirstReplicationSnapshot& snapshot) -> double
    {
        if (snapshot.manager_pressure_available &&
            std::isfinite(static_cast<double>(snapshot.pressure.EstimatedTicksToDrain)))
        {
            return std::max(0.0, static_cast<double>(snapshot.pressure.EstimatedTicksToDrain));
        }
        const int queued_strokes = mesh_first_pressure_queued_stroke_count(snapshot);
        const int max_strokes = mesh_first_pressure_max_strokes_per_tick(snapshot);
        if (queued_strokes >= 0 && max_strokes > 0)
        {
            return std::ceil(static_cast<double>(queued_strokes) / static_cast<double>(max_strokes));
        }
        return -1.0;
    }

    auto mesh_first_pressure_reported_estimated_ticks_to_drain(const MeshFirstReplicationSnapshot& snapshot) -> double
    {
        if (snapshot.manager_pressure_available &&
            std::isfinite(static_cast<double>(snapshot.pressure.EstimatedTicksToDrain)))
        {
            return std::max(0.0, static_cast<double>(snapshot.pressure.EstimatedTicksToDrain));
        }
        return -1.0;
    }

    enum class MeshFirstReplicationPacingPressureLevel
    {
        Unknown,
        Low,
        Moderate,
        High,
        Critical
    };

    auto mesh_first_replication_pacing_pressure_level_name(MeshFirstReplicationPacingPressureLevel level) -> const char*
    {
        switch (level)
        {
        case MeshFirstReplicationPacingPressureLevel::Low:
            return "low";
        case MeshFirstReplicationPacingPressureLevel::Moderate:
            return "moderate";
        case MeshFirstReplicationPacingPressureLevel::High:
            return "high";
        case MeshFirstReplicationPacingPressureLevel::Critical:
            return "critical";
        case MeshFirstReplicationPacingPressureLevel::Unknown:
        default:
            return "unknown";
        }
    }

    // =============================================================================
    // Section: Async mesh-first paint lifecycle
    // Risk: very high. This owns queued paint progress, cancellation, pacing, and
    // pawn/component-change guards while RPC batches are in flight.
    // =============================================================================

    enum class MeshFirstBatchPhase
    {
        Planning,
        ServerBatch,
        LocalTextureImport,
        TextureSyncObserve,
        ServerTextureSync,
        LocalSync,
        Done,
        Cancelled,
        Failed
    };

    struct MeshFirstServerBatchAsyncJob
    {
        std::shared_ptr<QueuedPaintJob> queued{};
        std::uintptr_t controller{0};
        std::uintptr_t pawn{0};
        std::uintptr_t component{0};
        std::uintptr_t relay_component{0};
        std::uintptr_t k2_get_pawn_function{0};
        std::uintptr_t server_packed_paint_batch_function{0};
        std::uintptr_t server_relay_packed_stroke_batch_function{0};
        std::uintptr_t local_paint_at_uv_function{0};
        bool server_packed_paint_batch_enabled{false};
        bool server_packed_paint_batch_use_relay{true};
        sdk::FGuid server_packed_paint_source_id{};
        std::string server_batch_rpc{"ServerPackedPaintBatch"};
        std::vector<sdk::FPaintStroke> strokes{};
        std::string metadata{};
        MeshFirstChannelChecksum albedo_before{};
        std::vector<std::uint8_t> albedo_before_bytes{};
        MeshFirstReplicationSnapshot replication_before{};
        int server_batch_limit{PackedReplicationDefaultBatchLimit};
        int server_batch_delay_ms{PackedReplicationDefaultPacingMs};
        int local_visual_sync_batch_limit{PackedReplicationDefaultBatchLimit};
        int local_visual_sync_delay_ms{PackedReplicationDefaultPacingMs};
        bool replication_pacing_enabled{true};
        int replication_pacing_requested_batch_limit{PackedReplicationDefaultBatchLimit};
        int replication_pacing_resolved_batch_limit{PackedReplicationDefaultBatchLimit};
        int replication_pacing_requested_delay_ms{PackedReplicationDefaultPacingMs};
        int replication_pacing_resolved_pacing_ms{PackedReplicationDefaultPacingMs};
        int replication_pacing_resolved_outgoing_strokes_per_batch{-1};
        int replication_pacing_resolved_outgoing_batches_per_second{-1};
        int replication_pacing_backoff_count{0};
        std::string replication_pacing_pressure_level{"unknown"};
        MeshFirstReplicationSnapshot replication_pacing_pre_pressure{};
        MeshFirstReplicationSnapshot replication_pacing_post_pressure{};
        double replication_pacing_last_rpc_ms{0.0};
        double replication_pacing_last_timer_drift_ms{0.0};
        bool replication_pacing_model_sample_valid{false};
        std::chrono::steady_clock::time_point replication_pacing_model_sample_at{};
        int replication_pacing_model_sample_sent{0};
        int replication_pacing_model_sample_queue{0};
        double replication_pacing_queue_drain_strokes_per_ms{-1.0};
        double replication_pacing_send_strokes_per_ms{-1.0};
        double replication_pacing_model_eta_ms{-1.0};
        std::uintptr_t replication_manager{0};
        std::uintptr_t replication_recorded_count_function{0};
        std::uintptr_t replication_queued_count_function{0};
        std::uintptr_t replication_component_queued_count_function{0};
        std::uintptr_t replication_pressure_function{0};
        int replication_component_max_replicated_strokes_per_tick{-1};
        std::uint8_t replication_component_use_compact_replication{0};
        std::uint8_t replication_component_use_packed_replication{0};
        int replication_manager_max_replicated_strokes_per_tick{-1};
        int replication_manager_max_render_target_writes_per_frame{-1};
        int replication_manager_max_outgoing_strokes_per_batch{-1};
        int replication_manager_max_outgoing_network_batches_per_second{-1};
        std::uint8_t replication_manager_coalesce_outgoing_strokes{0};
        int replay_front{0};
        int replay_side{0};
        int replay_back{0};
        int server_batch_calls{0};
        int server_batch_success{0};
        int server_batch_failures{0};
        int server_strokes_sent{0};
        double server_batch_elapsed_ms{-1.0};
        std::size_t offset{0};
        std::string first_failure{};
        bool local_visual_sync_enabled{false};
        bool local_sync_started{false};
        bool local_texture_import_started{false};
        bool local_texture_import_ok{false};
        bool local_texture_import_export_ok{false};
        bool local_texture_import_import_ok{false};
        int local_texture_import_texture_size{0};
        int local_texture_import_source_bytes{0};
        int local_texture_import_strokes_considered{0};
        int local_texture_import_strokes_painted{0};
        int local_texture_import_pixels_touched{0};
        int local_texture_import_pixels_changed{0};
        std::uint64_t local_texture_import_before_hash{1469598103934665603ULL};
        std::uint64_t local_texture_import_preview_hash{1469598103934665603ULL};
        double local_texture_import_elapsed_ms{-1.0};
        std::string local_texture_import_failure{};
        bool server_texture_sync_started{false};
        bool server_texture_sync_request_full_available{false};
        bool server_texture_sync_server_request_available{false};
        bool server_texture_sync_request_full_called{false};
        bool server_texture_sync_server_request_called{false};
        bool server_texture_sync_albedo_changed{false};
        bool server_texture_sync_timed_out{false};
        bool server_texture_sync_after_import_started{false};
        std::string server_texture_sync_after_import_route{"none"};
        SdkCallDetail server_texture_sync_after_import_server_relay{};
        SdkCallDetail server_texture_sync_after_import_relay{};
        SdkCallDetail server_texture_sync_after_import_request_full{};
        SdkCallDetail server_texture_sync_after_import_server_request{};
        int server_texture_sync_polls{0};
        int server_texture_sync_poll_ms{MeshFirstServerTextureSyncPollMs};
        int server_texture_sync_max_polls{MeshFirstServerTextureSyncMaxPolls};
        std::string server_texture_sync_failure{};
        bool texture_sync_observer_wait_started{false};
        bool texture_sync_observer_wait_observed{false};
        int texture_sync_observer_wait_polls{0};
        int texture_sync_observer_wait_poll_ms{MeshFirstTextureSyncObserverPollMs};
        int texture_sync_observer_wait_max_polls{MeshFirstTextureSyncObserverMaxPolls};
        double texture_sync_observer_wait_elapsed_ms{-1.0};
        int texture_size{1024};
        std::size_t local_offset{0};
        int local_stroke_calls{0};
        int local_stroke_success{0};
        int local_stroke_failures{0};
        int local_batch_calls{0};
        std::string local_visual_sync_failure{};
        std::chrono::steady_clock::time_point started{};
        std::chrono::steady_clock::time_point local_sync_started_at{};
        std::chrono::steady_clock::time_point server_texture_sync_started_at{};
        std::chrono::steady_clock::time_point texture_sync_observer_wait_started_at{};
        double local_visual_sync_elapsed_ms{0.0};
        double server_texture_sync_elapsed_ms{-1.0};
        std::chrono::steady_clock::time_point next_dispatch_time{};
        UINT_PTR dispatch_timer_id{0};
        MeshFirstBatchPhase phase{MeshFirstBatchPhase::Planning};
        std::atomic<bool> cancel_requested{false};
        std::atomic<bool> completed{false};
        std::string cancel_reason{"cancelled"};
    };

    auto mesh_first_capture_cached_replication_snapshot(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job)
        -> MeshFirstReplicationSnapshot
    {
        MeshFirstReplicationSnapshot snapshot{};
        if (!job || !live_uobject(job->component))
        {
            snapshot.failure = "paint_component_unavailable";
            return snapshot;
        }
        snapshot.component_max_replicated_strokes_per_tick =
            job->replication_component_max_replicated_strokes_per_tick;
        snapshot.component_use_compact_replication = job->replication_component_use_compact_replication;
        snapshot.component_use_packed_replication = job->replication_component_use_packed_replication;

        if (job->replication_recorded_count_function)
        {
            MeshFirstRecordedStrokeCountParams params{};
            std::string failure{};
            if (process_event(job->component,
                              job->replication_recorded_count_function,
                              reinterpret_cast<std::uint8_t*>(&params),
                              failure))
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

        snapshot.manager = job->replication_manager;
        snapshot.manager_available = live_uobject(snapshot.manager);
        if (!snapshot.manager_available)
        {
            if (snapshot.failure.empty())
            {
                snapshot.failure = "RuntimePaintReplicationManager_unavailable";
            }
            return snapshot;
        }
        snapshot.manager_max_replicated_strokes_per_tick =
            job->replication_manager_max_replicated_strokes_per_tick;
        snapshot.manager_max_render_target_writes_per_frame =
            job->replication_manager_max_render_target_writes_per_frame;
        snapshot.manager_max_outgoing_strokes_per_batch =
            job->replication_manager_max_outgoing_strokes_per_batch;
        snapshot.manager_max_outgoing_network_batches_per_second =
            job->replication_manager_max_outgoing_network_batches_per_second;
        snapshot.manager_coalesce_outgoing_strokes = job->replication_manager_coalesce_outgoing_strokes;

        if (job->replication_queued_count_function)
        {
            MeshFirstQueuedStrokeCountParams params{};
            std::string failure{};
            if (process_event(snapshot.manager,
                              job->replication_queued_count_function,
                              reinterpret_cast<std::uint8_t*>(&params),
                              failure))
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

        if (job->replication_component_queued_count_function)
        {
            MeshFirstQueuedStrokeCountForComponentParams params{};
            params.PaintComponent = reinterpret_cast<void*>(job->component);
            std::string failure{};
            if (process_event(snapshot.manager,
                              job->replication_component_queued_count_function,
                              reinterpret_cast<std::uint8_t*>(&params),
                              failure))
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

        if (job->replication_pressure_function)
        {
            MeshFirstGetReplicationPressureParams params{};
            std::string failure{};
            if (process_event(snapshot.manager,
                              job->replication_pressure_function,
                              reinterpret_cast<std::uint8_t*>(&params),
                              failure))
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

    auto mesh_first_replication_pacing_requested_delay(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> int
    {
        return std::clamp(job ? job->replication_pacing_requested_delay_ms : PackedReplicationDefaultPacingMs,
                          PackedReplicationMinPacingMs,
                          PackedReplicationMaxPacingMs);
    }

    auto mesh_first_replication_pacing_requested_batch(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> int
    {
        return std::max(1, job ? job->replication_pacing_requested_batch_limit : PackedReplicationDefaultBatchLimit);
    }

    auto mesh_first_replication_pacing_resolved_batch(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> int
    {
        const int requested = mesh_first_replication_pacing_requested_batch(job);
        if (!job || !job->replication_pacing_enabled)
        {
            return requested;
        }
        const int resolved = job->replication_pacing_resolved_batch_limit > 0
                                 ? job->replication_pacing_resolved_batch_limit
                                 : std::min(requested, PackedReplicationFallbackMaxStrokesPerTick);
        return std::clamp(resolved, 1, requested);
    }

    auto mesh_first_replication_pacing_resolved_pacing(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> int
    {
        if (!job || !job->replication_pacing_enabled)
        {
            return mesh_first_replication_pacing_requested_delay(job);
        }
        if (job->replication_pacing_resolved_pacing_ms > 0)
        {
            return std::clamp(job->replication_pacing_resolved_pacing_ms, PackedReplicationResolvedPacingMinMs, PackedReplicationMaxPacingMs);
        }
        const int fallback = static_cast<int>(
            std::ceil(1000.0 / static_cast<double>(std::max(1, PackedReplicationFallbackOutgoingBatchesPerSecond))));
        return std::clamp(fallback, PackedReplicationResolvedPacingMinMs, PackedReplicationMaxPacingMs);
    }

    void mesh_first_update_replication_pacing_resolved_batch(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                                   const MeshFirstReplicationSnapshot& pressure)
    {
        if (!job || !job->replication_pacing_enabled)
        {
            return;
        }
        const int requested = mesh_first_replication_pacing_requested_batch(job);
        const int max_strokes_per_tick = mesh_first_pressure_max_strokes_per_tick(pressure);
        const int max_outgoing_strokes_per_batch = mesh_first_pressure_max_outgoing_strokes_per_batch(pressure);
        const int outgoing_batches_per_second = mesh_first_pressure_outgoing_batches_per_second(pressure);
        int resolved = requested;
        if (max_strokes_per_tick > 0)
        {
            resolved = std::min(resolved, max_strokes_per_tick);
        }
        if (max_outgoing_strokes_per_batch > 0)
        {
            resolved = std::min(resolved, max_outgoing_strokes_per_batch);
        }
        job->replication_pacing_resolved_batch_limit = std::clamp(resolved, 1, requested);
        job->replication_pacing_resolved_outgoing_strokes_per_batch = max_outgoing_strokes_per_batch;
        job->replication_pacing_resolved_outgoing_batches_per_second = outgoing_batches_per_second;
        const int pacing = outgoing_batches_per_second > 0
                               ? static_cast<int>(std::ceil(1000.0 / static_cast<double>(outgoing_batches_per_second)))
                               : static_cast<int>(std::ceil(1000.0 / static_cast<double>(PackedReplicationFallbackOutgoingBatchesPerSecond)));
        job->replication_pacing_resolved_pacing_ms = std::clamp(pacing, 1, PackedReplicationMaxPacingMs);
        if (job->server_batch_limit > job->replication_pacing_resolved_batch_limit)
        {
            job->server_batch_limit = job->replication_pacing_resolved_batch_limit;
            job->local_visual_sync_batch_limit = job->server_batch_limit;
        }
    }

    auto mesh_first_replication_pacing_clamp_batch(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job, int value) -> int
    {
        return std::clamp(value, 1, mesh_first_replication_pacing_resolved_batch(job));
    }

    auto mesh_first_replication_pacing_clamp_delay(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job, int value) -> int
    {
        const int requested = std::clamp(value, PackedReplicationMinPacingMs, PackedReplicationMaxPacingMs);
        const int resolved = mesh_first_replication_pacing_resolved_pacing(job);
        return std::clamp(std::max(requested, resolved), PackedReplicationMinPacingMs, PackedReplicationMaxPacingMs);
    }

    void mesh_first_set_replication_pacing_effective(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                           int batch_limit,
                                           int delay_ms)
    {
        if (!job)
        {
            return;
        }
        job->server_batch_limit = mesh_first_replication_pacing_clamp_batch(job, batch_limit);
        job->server_batch_delay_ms = mesh_first_replication_pacing_clamp_delay(job, delay_ms);
        job->local_visual_sync_batch_limit = job->server_batch_limit;
        job->local_visual_sync_delay_ms = job->server_batch_delay_ms;
    }

    auto mesh_first_replication_pacing_latest_pressure(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job)
        -> const MeshFirstReplicationSnapshot&
    {
        if (job && mesh_first_has_replication_pressure_signal(job->replication_pacing_post_pressure))
        {
            return job->replication_pacing_post_pressure;
        }
        if (job && mesh_first_has_replication_pressure_signal(job->replication_pacing_pre_pressure))
        {
            return job->replication_pacing_pre_pressure;
        }
        if (job)
        {
            return job->replication_before;
        }
        static const MeshFirstReplicationSnapshot empty{};
        return empty;
    }

    void mesh_first_update_replication_pacing_model(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                          const MeshFirstReplicationSnapshot& pressure)
    {
        if (!job)
        {
            return;
        }
        const int queue = mesh_first_pressure_queued_stroke_count(pressure);
        if (queue < 0)
        {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!job->replication_pacing_model_sample_valid ||
            job->replication_pacing_model_sample_at.time_since_epoch().count() == 0)
        {
            job->replication_pacing_model_sample_valid = true;
            job->replication_pacing_model_sample_at = now;
            job->replication_pacing_model_sample_sent = std::max(0, job->server_strokes_sent);
            job->replication_pacing_model_sample_queue = queue;
            return;
        }

        const double delta_ms =
            std::chrono::duration<double, std::milli>(now - job->replication_pacing_model_sample_at).count();
        if (delta_ms < 250.0 || !std::isfinite(delta_ms))
        {
            return;
        }

        const int sent_delta = std::max(0, job->server_strokes_sent - job->replication_pacing_model_sample_sent);
        const int drained = std::max(0, job->replication_pacing_model_sample_queue + sent_delta - queue);
        auto update_rate = [](double& target, double sample) {
            if (!std::isfinite(sample) || sample <= 0.0)
            {
                return;
            }
            target = target <= 0.0 ? sample : (target * 0.70) + (sample * 0.30);
        };
        update_rate(job->replication_pacing_queue_drain_strokes_per_ms,
                    static_cast<double>(drained) / delta_ms);
        update_rate(job->replication_pacing_send_strokes_per_ms,
                    static_cast<double>(sent_delta) / delta_ms);

        job->replication_pacing_model_sample_valid = true;
        job->replication_pacing_model_sample_at = now;
        job->replication_pacing_model_sample_sent = std::max(0, job->server_strokes_sent);
        job->replication_pacing_model_sample_queue = queue;
    }

    auto mesh_first_replication_pacing_queue_gate_limit(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> int
    {
        return std::max(1, mesh_first_replication_pacing_resolved_batch(job));
    }

    auto mesh_first_replication_pacing_batch_queue_gate_limit(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> int
    {
        const int outgoing_batches_per_second =
            job && job->replication_pacing_resolved_outgoing_batches_per_second > 0
                ? job->replication_pacing_resolved_outgoing_batches_per_second
                : PackedReplicationFallbackOutgoingBatchesPerSecond;
        return std::max(1, outgoing_batches_per_second * 2);
    }

    auto mesh_first_replication_pacing_queue_gate_open(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                             const MeshFirstReplicationSnapshot& pressure) -> bool
    {
        const int queued_strokes = mesh_first_pressure_queued_stroke_count(pressure);
        const int queued_batches = mesh_first_pressure_queued_batch_count(pressure);
        if (queued_strokes < 0 && queued_batches < 0)
        {
            return true;
        }
        const bool strokes_ok = queued_strokes < 0 ||
                                queued_strokes <= mesh_first_replication_pacing_queue_gate_limit(job);
        const bool batches_ok = queued_batches < 0 ||
                                queued_batches <= mesh_first_replication_pacing_batch_queue_gate_limit(job);
        return strokes_ok && batches_ok;
    }

    auto mesh_first_replication_pacing_queue_gate_pressure_level(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                                       const MeshFirstReplicationSnapshot& pressure)
        -> MeshFirstReplicationPacingPressureLevel
    {
        if (!mesh_first_has_replication_pressure_signal(pressure))
        {
            return MeshFirstReplicationPacingPressureLevel::Unknown;
        }
        const int queued_strokes = mesh_first_pressure_queued_stroke_count(pressure);
        const int queued_batches = mesh_first_pressure_queued_batch_count(pressure);
        if (queued_strokes < 0 && queued_batches < 0)
        {
            return MeshFirstReplicationPacingPressureLevel::Unknown;
        }
        const int stroke_gate = mesh_first_replication_pacing_queue_gate_limit(job);
        const int batch_gate = mesh_first_replication_pacing_batch_queue_gate_limit(job);
        const int stroke_pressure = queued_strokes < 0 ? 0 : (queued_strokes + stroke_gate - 1) / stroke_gate;
        const int batch_pressure = queued_batches < 0 ? 0 : (queued_batches + batch_gate - 1) / batch_gate;
        const int pressure_ratio = std::max(stroke_pressure, batch_pressure);
        if (pressure_ratio <= 1)
        {
            return MeshFirstReplicationPacingPressureLevel::Low;
        }
        if (pressure_ratio <= 2)
        {
            return MeshFirstReplicationPacingPressureLevel::Moderate;
        }
        if (pressure_ratio <= 4)
        {
            return MeshFirstReplicationPacingPressureLevel::High;
        }
        return MeshFirstReplicationPacingPressureLevel::Critical;
    }

    auto mesh_first_replication_pacing_metadata(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> std::string
    {
        const auto& pressure = mesh_first_replication_pacing_latest_pressure(job);
        std::string out{};
        out += ",\"replication_pacing_enabled\":" + std::string(json_bool(job && job->replication_pacing_enabled));
        out += ",\"replication_pacing_requested_batch_limit\":" +
               std::to_string(mesh_first_replication_pacing_requested_batch(job));
        out += ",\"replication_pacing_resolved_batch_limit\":" +
               std::to_string(mesh_first_replication_pacing_resolved_batch(job));
        out += ",\"replication_pacing_requested_delay_ms\":" +
               std::to_string(job ? mesh_first_replication_pacing_requested_delay(job) : PackedReplicationDefaultPacingMs);
        out += ",\"replication_pacing_resolved_pacing_ms\":" +
               std::to_string(job ? mesh_first_replication_pacing_resolved_pacing(job) : PackedReplicationDefaultPacingMs);
        out += ",\"replication_pacing_batch_limit\":" + std::to_string(job ? std::max(1, job->server_batch_limit) : PackedReplicationDefaultBatchLimit);
        out += ",\"replication_pacing_delay_ms\":" + std::to_string(job ? std::max(0, job->server_batch_delay_ms) : PackedReplicationDefaultPacingMs);
        out += ",\"replication_pacing_pressure_level\":\"" + json_escape(job ? job->replication_pacing_pressure_level : "unknown") + "\"";
        out += ",\"replication_pacing_backoff_count\":" + std::to_string(job ? job->replication_pacing_backoff_count : 0);
        out += ",\"replication_pacing_queue_wait_count\":" + std::to_string(job ? job->replication_pacing_backoff_count : 0);
        out += ",\"replication_pacing_queue_gate_limit\":" + std::to_string(mesh_first_replication_pacing_queue_gate_limit(job));
        out += ",\"replication_pacing_batch_queue_gate_limit\":" +
               std::to_string(mesh_first_replication_pacing_batch_queue_gate_limit(job));
        out += ",\"replication_pacing_max_outgoing_strokes_per_batch\":" +
               std::to_string(job ? job->replication_pacing_resolved_outgoing_strokes_per_batch : -1);
        out += ",\"replication_pacing_max_outgoing_network_batches_per_second\":" +
               std::to_string(job ? job->replication_pacing_resolved_outgoing_batches_per_second : -1);
        out += ",\"replication_pacing_queue_drain_strokes_per_sec\":" +
               std::to_string(job && job->replication_pacing_queue_drain_strokes_per_ms > 0.0
                                  ? job->replication_pacing_queue_drain_strokes_per_ms * 1000.0
                                  : -1.0);
        out += ",\"replication_pacing_send_strokes_per_sec\":" +
               std::to_string(job && job->replication_pacing_send_strokes_per_ms > 0.0
                                  ? job->replication_pacing_send_strokes_per_ms * 1000.0
                                  : -1.0);
        out += ",\"replication_pacing_model_eta_ms\":" +
               std::to_string(job && job->replication_pacing_model_eta_ms >= 0.0 ? job->replication_pacing_model_eta_ms : -1.0);
        out += ",\"replication_queued_batch_count\":" + std::to_string(mesh_first_pressure_queued_batch_count(pressure));
        out += ",\"replication_queued_stroke_count\":" + std::to_string(mesh_first_pressure_queued_stroke_count(pressure));
        out += ",\"replication_max_strokes_per_tick\":" +
               std::to_string(mesh_first_pressure_reported_max_strokes_per_tick(pressure));
        out += ",\"replication_estimated_ticks_to_drain\":" +
               std::to_string(mesh_first_pressure_reported_estimated_ticks_to_drain(pressure));
        out += ",\"replication_component_max_replicated_strokes_per_tick\":" +
               std::to_string(pressure.component_max_replicated_strokes_per_tick);
        out += ",\"replication_manager_max_replicated_strokes_per_tick\":" +
               std::to_string(pressure.manager_max_replicated_strokes_per_tick);
        out += ",\"replication_manager_max_outgoing_strokes_per_batch\":" +
               std::to_string(pressure.manager_max_outgoing_strokes_per_batch);
        out += ",\"replication_manager_max_outgoing_network_batches_per_second\":" +
               std::to_string(pressure.manager_max_outgoing_network_batches_per_second);
        if (job && metadata_contains_bool(job->metadata, "research_artifacts_requested", true))
        {
            out += ",\"replication_pacing_last_rpc_ms\":" + std::to_string(job->replication_pacing_last_rpc_ms);
            out += ",\"replication_pacing_last_timer_drift_ms\":" + std::to_string(job->replication_pacing_last_timer_drift_ms);
            out += mesh_first_replication_snapshot_metadata("replication_pacing_pre_pressure", job->replication_pacing_pre_pressure);
            out += mesh_first_replication_snapshot_metadata("replication_pacing_post_pressure", job->replication_pacing_post_pressure);
        }
        return out;
    }

    auto mesh_first_request_texture_sync_after_import(Reflection& ref,
                                                      const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> void
    {
        if (!job || job->server_texture_sync_after_import_started)
        {
            return;
        }
        job->server_texture_sync_after_import_started = true;
        if (!MeshFirstPostImportTextureSyncEnabled)
        {
            job->server_texture_sync_after_import_route = "disabled";
            return;
        }

        job->server_texture_sync_after_import_server_relay =
            sdk_call_object_param_detail(ref,
                                         job->relay_component,
                                         "ServerRelayTextureSync",
                                         job->component);

        job->server_texture_sync_after_import_relay =
            sdk_call_object_param_detail(ref,
                                         job->relay_component,
                                         "RelayTextureSyncToServer",
                                         job->component);

        job->server_texture_sync_after_import_request_full =
            sdk_call_no_params_detail(ref, job->component, "RequestFullTextureSync");

        job->server_texture_sync_after_import_server_request =
            sdk_call_no_params_detail(ref, job->component, "ServerRequestTextureSync");

        std::vector<std::string> routes{};
        if (job->server_texture_sync_after_import_server_relay.process_ok)
        {
            routes.emplace_back("server_relay_texture_sync");
        }
        if (job->server_texture_sync_after_import_relay.process_ok)
        {
            routes.emplace_back("relay_texture_sync_to_server");
        }
        if (job->server_texture_sync_after_import_request_full.process_ok)
        {
            routes.emplace_back("request_full_texture_sync");
        }
        if (job->server_texture_sync_after_import_server_request.process_ok)
        {
            routes.emplace_back("server_request_texture_sync");
        }

        if (!routes.empty())
        {
            std::string route;
            for (std::size_t i = 0; i < routes.size(); ++i)
            {
                if (i > 0)
                {
                    route += '+';
                }
                route += routes[i];
            }
            job->server_texture_sync_after_import_route = route;
            return;
        }

        job->server_texture_sync_after_import_route = "unavailable";
    }

    auto mesh_first_phase_name(MeshFirstBatchPhase phase) -> const char*
    {
        switch (phase)
        {
        case MeshFirstBatchPhase::Planning:
            return "planning";
        case MeshFirstBatchPhase::ServerBatch:
            return "server_batch";
        case MeshFirstBatchPhase::LocalTextureImport:
            return "local_texture_import";
        case MeshFirstBatchPhase::TextureSyncObserve:
            return "texture_sync_observe";
        case MeshFirstBatchPhase::ServerTextureSync:
            return "server_texture_sync";
        case MeshFirstBatchPhase::LocalSync:
            return "local_sync";
        case MeshFirstBatchPhase::Done:
            return "done";
        case MeshFirstBatchPhase::Cancelled:
            return "cancelled";
        case MeshFirstBatchPhase::Failed:
            return "failed";
        }
        return "unknown";
    }

    auto mesh_first_div_ceil(int value, int divisor) -> int
    {
        if (value <= 0 || divisor <= 0)
        {
            return 0;
        }
        return (value + divisor - 1) / divisor;
    }

    auto mesh_first_elapsed_ms(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> double
    {
        if (!job || job->started.time_since_epoch().count() == 0)
        {
            return 0.0;
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->started).count();
    }

    auto mesh_first_local_elapsed_ms(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> double
    {
        if (!job || !job->local_sync_started || job->local_sync_started_at.time_since_epoch().count() == 0)
        {
            return 0.0;
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->local_sync_started_at).count();
    }

    auto mesh_first_observed_stroke_eta_ms(double elapsed_ms,
                                           int completed_strokes,
                                           int total_strokes,
                                           int remaining_batches,
                                           int pacing_ms) -> double
    {
        if (total_strokes <= 0 || completed_strokes >= total_strokes)
        {
            return 0.0;
        }
        const double pacing_floor_ms =
            static_cast<double>(std::max(0, remaining_batches - 1)) *
            static_cast<double>(std::max(0, pacing_ms));
        if (completed_strokes <= 0 || elapsed_ms <= 0.0 || !std::isfinite(elapsed_ms))
        {
            return pacing_floor_ms;
        }
        const int remaining_strokes = std::max(0, total_strokes - completed_strokes);
        const double observed_ms_per_stroke = elapsed_ms / static_cast<double>(std::max(1, completed_strokes));
        return std::max(pacing_floor_ms, observed_ms_per_stroke * static_cast<double>(remaining_strokes));
    }

    auto mesh_first_server_model_eta_ms(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                        bool terminal,
                                        int remaining_server_strokes,
                                        int remaining_server_batches,
                                        int server_batch_delay_ms) -> double
    {
        if (terminal || remaining_server_strokes <= 0)
        {
            if (job)
            {
                job->replication_pacing_model_eta_ms = 0.0;
            }
            return 0.0;
        }

        const double scheduled_send_ms =
            static_cast<double>(std::max(0, remaining_server_batches - 1)) *
            static_cast<double>(std::max(0, server_batch_delay_ms));
        double raw_eta_ms = scheduled_send_ms;

        if (job && job->replication_pacing_enabled)
        {
            const auto& pressure = mesh_first_replication_pacing_latest_pressure(job);
            const int queued_strokes = mesh_first_pressure_queued_stroke_count(pressure);
            const bool drain_rate_available =
                job->replication_pacing_queue_drain_strokes_per_ms > 0.0 &&
                std::isfinite(job->replication_pacing_queue_drain_strokes_per_ms);
            if (queued_strokes > mesh_first_replication_pacing_queue_gate_limit(job) && !drain_rate_available)
            {
                job->replication_pacing_model_eta_ms = -1.0;
                return -1.0;
            }
        }

        if (job && job->replication_pacing_send_strokes_per_ms > 0.0 &&
            std::isfinite(job->replication_pacing_send_strokes_per_ms))
        {
            raw_eta_ms = std::max(raw_eta_ms,
                                  static_cast<double>(remaining_server_strokes) /
                                      job->replication_pacing_send_strokes_per_ms);
        }

        if (job && job->replication_pacing_queue_drain_strokes_per_ms > 0.0 &&
            std::isfinite(job->replication_pacing_queue_drain_strokes_per_ms))
        {
            const auto& pressure = mesh_first_replication_pacing_latest_pressure(job);
            const int queued_strokes = mesh_first_pressure_queued_stroke_count(pressure);
            if (queued_strokes >= 0)
            {
                const int expected_queue_after_sends =
                    std::max(0, queued_strokes + std::max(0, remaining_server_strokes));
                raw_eta_ms = std::max(raw_eta_ms,
                                      static_cast<double>(expected_queue_after_sends) /
                                          job->replication_pacing_queue_drain_strokes_per_ms);
            }
        }

        if (!std::isfinite(raw_eta_ms) || raw_eta_ms < 0.0)
        {
            if (job)
            {
                job->replication_pacing_model_eta_ms = -1.0;
            }
            return -1.0;
        }

        if (!job)
        {
            return raw_eta_ms;
        }

        if (job->replication_pacing_model_eta_ms < 0.0 || !std::isfinite(job->replication_pacing_model_eta_ms))
        {
            job->replication_pacing_model_eta_ms = raw_eta_ms;
        }
        else if (raw_eta_ms > job->replication_pacing_model_eta_ms)
        {
            job->replication_pacing_model_eta_ms = (job->replication_pacing_model_eta_ms * 0.70) + (raw_eta_ms * 0.30);
        }
        else
        {
            job->replication_pacing_model_eta_ms = (job->replication_pacing_model_eta_ms * 0.45) + (raw_eta_ms * 0.55);
        }
        return job->replication_pacing_model_eta_ms;
    }

    auto mesh_first_progress_extra(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                   MeshFirstBatchPhase phase,
                                   bool terminal,
                                   const char* result,
                                   const std::string& extra = "") -> std::string
    {
        const int total_strokes = job ? static_cast<int>(job->strokes.size()) : 0;
        const int server_batch_limit = std::max(1, job ? job->server_batch_limit : PackedReplicationDefaultBatchLimit);
        const int server_batch_delay_ms = std::max(0, job ? job->server_batch_delay_ms : PackedReplicationDefaultPacingMs);
        const int local_batch_limit = std::max(1, job ? job->local_visual_sync_batch_limit : PackedReplicationDefaultBatchLimit);
        const int local_batch_delay_ms = std::max(0, job ? job->local_visual_sync_delay_ms : PackedReplicationDefaultPacingMs);
        const int server_batches_total = mesh_first_div_ceil(total_strokes, server_batch_limit);
        const int local_batches_total = mesh_first_div_ceil(total_strokes, local_batch_limit);
        const int server_batches_done = job ? std::max(0, job->server_batch_success) : 0;
        const int local_batches_done = job ? std::max(0, job->local_batch_calls) : 0;
        const int server_strokes_sent = job ? std::max(0, job->server_strokes_sent) : 0;
        const int local_strokes_synced = job ? std::max(0, job->local_stroke_success) : 0;
        const double paint_elapsed_ms = mesh_first_elapsed_ms(job);
        const double server_elapsed_ms =
            job && job->server_batch_elapsed_ms >= 0.0 ? job->server_batch_elapsed_ms : paint_elapsed_ms;
        const double local_elapsed_ms = job && job->local_sync_started ? mesh_first_local_elapsed_ms(job) : 0.0;
        const double server_texture_sync_elapsed_ms =
            job && job->server_texture_sync_started && job->server_texture_sync_started_at.time_since_epoch().count() != 0
                ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->server_texture_sync_started_at).count()
                : (job ? job->server_texture_sync_elapsed_ms : -1.0);

        auto eta_from_observed_rate = [](double elapsed_ms,
                                         int completed_units,
                                         int total_units,
                                         int remaining_delay_units,
                                         int delay_ms) -> double {
            if (total_units <= 0 || completed_units >= total_units)
            {
                return 0.0;
            }
            const double delay_floor_ms =
                static_cast<double>(std::max(0, remaining_delay_units)) *
                static_cast<double>(std::max(0, delay_ms));
            if (completed_units <= 0 || elapsed_ms <= 0.0 || !std::isfinite(elapsed_ms))
            {
                return delay_floor_ms;
            }
            const int remaining_units = std::max(0, total_units - completed_units);
            const double observed_ms_per_unit = elapsed_ms / static_cast<double>(std::max(1, completed_units));
            return std::max(delay_floor_ms, observed_ms_per_unit * static_cast<double>(remaining_units));
        };

        const bool lockstep_local_sync = job && job->local_visual_sync_enabled;
        const int remaining_server_strokes = std::max(0, total_strokes - server_strokes_sent);
        const int remaining_local_strokes = std::max(0, total_strokes - local_strokes_synced);
        const int remaining_server_batches = mesh_first_div_ceil(remaining_server_strokes, server_batch_limit);
        const int remaining_local_batches = mesh_first_div_ceil(remaining_local_strokes, local_batch_limit);
        const int paint_strokes_done = lockstep_local_sync ? std::min(server_strokes_sent, local_strokes_synced)
                                                           : std::max(server_strokes_sent, local_strokes_synced);
        const int paint_delay_ms = lockstep_local_sync ? std::max(server_batch_delay_ms, local_batch_delay_ms)
                                                       : std::max(server_batch_delay_ms, local_batch_delay_ms);
        const double server_observed_elapsed_ms = lockstep_local_sync ? paint_elapsed_ms : server_elapsed_ms;
        const double server_observed_ms_per_stroke =
            server_strokes_sent > 0 ? server_observed_elapsed_ms / static_cast<double>(server_strokes_sent) : -1.0;
        const double paint_observed_ms_per_stroke =
            paint_strokes_done > 0 ? paint_elapsed_ms / static_cast<double>(paint_strokes_done) : -1.0;

        double server_eta_ms = 0.0;
        double local_eta_ms = 0.0;
        double paint_eta_ms = 0.0;
        std::string server_eta_source{"scheduled_delay_floor"};
        std::string paint_eta_source{"scheduled_delay_floor"};
        if (phase == MeshFirstBatchPhase::Planning)
        {
            server_eta_ms = std::max(0, server_batches_total - 1) * static_cast<double>(server_batch_delay_ms);
            if (job && job->local_visual_sync_enabled)
            {
                local_eta_ms = lockstep_local_sync ? server_eta_ms
                                                   : std::max(0, local_batches_total - 1) * static_cast<double>(local_batch_delay_ms);
                paint_eta_ms = lockstep_local_sync ? server_eta_ms : server_eta_ms + local_eta_ms;
            }
            else
            {
                paint_eta_ms = server_eta_ms;
            }
        }
        else if (phase == MeshFirstBatchPhase::ServerBatch)
        {
            const double observed_server_eta_ms =
                mesh_first_observed_stroke_eta_ms(server_observed_elapsed_ms,
                                                  server_strokes_sent,
                                                  total_strokes,
                                                  remaining_server_batches,
                                                  server_batch_delay_ms);
            if (job && job->replication_pacing_enabled)
            {
                server_eta_ms = mesh_first_server_model_eta_ms(job,
                                                               terminal,
                                                               remaining_server_strokes,
                                                               remaining_server_batches,
                                                               server_batch_delay_ms);
                if (server_strokes_sent > 0 &&
                    observed_server_eta_ms >= 0.0 &&
                    std::isfinite(observed_server_eta_ms))
                {
                    server_eta_ms = server_eta_ms >= 0.0
                                        ? std::max(server_eta_ms, observed_server_eta_ms)
                                        : observed_server_eta_ms;
                }
                server_eta_source = "queue_drain_model";
            }
            else
            {
                server_eta_ms = observed_server_eta_ms >= 0.0 && std::isfinite(observed_server_eta_ms)
                                    ? observed_server_eta_ms
                                    : std::max(0, remaining_server_batches - 1) * static_cast<double>(server_batch_delay_ms);
                server_eta_source = "observed_rate";
            }
            if (lockstep_local_sync)
            {
                local_eta_ms = server_eta_ms;
                const double observed_paint_eta_ms =
                    mesh_first_observed_stroke_eta_ms(paint_elapsed_ms,
                                                      paint_strokes_done,
                                                      total_strokes,
                                                      remaining_server_batches,
                                                      server_batch_delay_ms);
                if (job && job->replication_pacing_enabled && paint_strokes_done <= 0 && server_eta_ms < 0.0)
                {
                    paint_eta_ms = -1.0;
                }
                else
                {
                    paint_eta_ms = server_eta_ms >= 0.0 ? server_eta_ms : 0.0;
                    if (observed_paint_eta_ms >= 0.0 && std::isfinite(observed_paint_eta_ms))
                    {
                        paint_eta_ms = std::max(paint_eta_ms, observed_paint_eta_ms);
                    }
                }
                paint_eta_source = job && job->replication_pacing_enabled ? "queue_drain_model" : "observed_rate";
            }
            else
            {
                local_eta_ms = job && job->local_visual_sync_enabled
                                   ? std::max(0, local_batches_total - 1) * static_cast<double>(local_batch_delay_ms)
                                   : 0.0;
                paint_eta_ms = server_eta_ms + local_eta_ms;
                paint_eta_source = job && job->replication_pacing_enabled ? "queue_drain_model" : "observed_rate";
            }
        }
        else if (phase == MeshFirstBatchPhase::LocalSync)
        {
            local_eta_ms = eta_from_observed_rate(local_elapsed_ms,
                                                  local_strokes_synced,
                                                  total_strokes,
                                                  remaining_local_batches,
                                                  local_batch_delay_ms);
            paint_eta_ms = local_eta_ms;
        }
        if (terminal)
        {
            server_eta_ms = 0.0;
            local_eta_ms = 0.0;
            paint_eta_ms = 0.0;
            if (job)
            {
                job->replication_pacing_model_eta_ms = 0.0;
            }
        }
        std::string out = "\"progress_schema_version\":2";
        out += ",\"phase\":\"" + std::string(mesh_first_phase_name(phase)) + "\"";
        out += ",\"terminal\":" + std::string(json_bool(terminal));
        out += ",\"result\":\"" + std::string(result && *result ? result : (terminal ? "done" : "running")) + "\"";
        out += ",\"total_strokes\":" + std::to_string(total_strokes);
        out += ",\"server_batch_limit\":" + std::to_string(server_batch_limit);
        out += ",\"server_batch_delay_ms\":" + std::to_string(server_batch_delay_ms);
        out += mesh_first_replication_pacing_metadata(job);
        out += ",\"server_batch_rpc\":\"" + json_escape(job ? job->server_batch_rpc : "ServerPackedPaintBatch") + "\"";
        out += ",\"server_packed_paint_batch_enabled\":" +
               std::string(json_bool(job && job->server_packed_paint_batch_enabled));
        out += ",\"server_packed_paint_batch_use_relay\":" +
               std::string(json_bool(job && job->server_packed_paint_batch_use_relay));
        out += ",\"server_batches_total\":" + std::to_string(server_batches_total);
        out += ",\"server_batches_done\":" + std::to_string(server_batches_done);
        out += ",\"server_batch_calls\":" + std::to_string(job ? job->server_batch_calls : 0);
        out += ",\"server_batch_success\":" + std::to_string(job ? job->server_batch_success : 0);
        out += ",\"server_batch_failures\":" + std::to_string(job ? job->server_batch_failures : 0);
        out += ",\"server_strokes_sent\":" + std::to_string(server_strokes_sent);
        out += ",\"server_strokes_total\":" + std::to_string(total_strokes);
        out += ",\"server_elapsed_ms\":" + std::to_string(server_elapsed_ms);
        out += ",\"server_batch_elapsed_ms\":" + std::to_string(server_elapsed_ms);
        out += ",\"server_eta_ms\":" + std::to_string(server_eta_ms);
        out += ",\"server_eta_source\":\"" + server_eta_source + "\"";
        out += ",\"server_observed_ms_per_stroke\":" + std::to_string(server_observed_ms_per_stroke);
        out += ",\"local_batch_limit\":" + std::to_string(local_batch_limit);
        out += ",\"local_batch_delay_ms\":" + std::to_string(local_batch_delay_ms);
        out += ",\"local_batches_total\":" + std::to_string(local_batches_total);
        out += ",\"local_batches_done\":" + std::to_string(local_batches_done);
        out += ",\"local_strokes_synced\":" + std::to_string(local_strokes_synced);
        out += ",\"local_strokes_total\":" + std::to_string(total_strokes);
        out += ",\"local_visual_sync_used\":" + std::string(json_bool(job && job->local_visual_sync_enabled));
        out += ",\"local_visual_sync_started\":" + std::string(json_bool(job && job->local_sync_started));
        out += ",\"local_visual_sync_elapsed_ms\":" + std::to_string(local_elapsed_ms);
        out += ",\"local_elapsed_ms\":" + std::to_string(local_elapsed_ms);
        out += ",\"local_eta_ms\":" + std::to_string(local_eta_ms);
        out += ",\"paint_eta_source\":\"" + paint_eta_source + "\"";
        out += ",\"paint_observed_ms_per_stroke\":" + std::to_string(paint_observed_ms_per_stroke);
        out += ",\"server_texture_sync_started\":" + std::string(json_bool(job && job->server_texture_sync_started));
        out += ",\"server_texture_sync_polls\":" + std::to_string(job ? job->server_texture_sync_polls : 0);
        out += ",\"server_texture_sync_max_polls\":" + std::to_string(job ? job->server_texture_sync_max_polls : 0);
        out += ",\"server_texture_sync_elapsed_ms\":" + std::to_string(server_texture_sync_elapsed_ms);
        out += ",\"server_texture_sync_albedo_changed\":" + std::string(json_bool(job && job->server_texture_sync_albedo_changed));
        out += ",\"server_texture_sync_timed_out\":" + std::string(json_bool(job && job->server_texture_sync_timed_out));
        out += ",\"texture_sync_observer_wait_started\":" + std::string(json_bool(job && job->texture_sync_observer_wait_started));
        out += ",\"texture_sync_observer_wait_polls\":" + std::to_string(job ? job->texture_sync_observer_wait_polls : 0);
        out += ",\"texture_sync_observer_wait_max_polls\":" + std::to_string(job ? job->texture_sync_observer_wait_max_polls : 0);
        out += ",\"texture_sync_observer_wait_observed\":" + std::string(json_bool(job && job->texture_sync_observer_wait_observed));
        out += ",\"paint_elapsed_ms\":" + std::to_string(paint_elapsed_ms);
        out += ",\"paint_eta_ms\":" + std::to_string(paint_eta_ms);
        if (job)
        {
            out += ",\"cancel_requested\":" + std::string(json_bool(job->cancel_requested.load()));
            out += ",\"cancel_reason\":\"" + json_escape(job->cancel_reason) + "\"";
        }
        if (!extra.empty())
        {
            out += ",";
            out += extra;
        }
        return metadata_contains_bool(job ? job->metadata : std::string{}, "research_artifacts_requested", true)
                   ? out
                   : compact_mesh_progress_metadata(out);
    }

    auto mesh_first_remaining_strokes(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job) -> int
    {
        if (!job)
        {
            return 0;
        }
        const int total = static_cast<int>(job->strokes.size());
        if (job->phase == MeshFirstBatchPhase::LocalSync)
        {
            return std::max(0, total - job->local_stroke_success);
        }
        return std::max(0, total - job->server_strokes_sent);
    }

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
        const auto front_region_mode = mesh_first_parse_region_mode(request, "front_region_mode", "enable_front_paint");
        const auto side_region_mode = mesh_first_parse_region_mode(request, "side_region_mode", "enable_side_paint");
        const auto back_region_mode = mesh_first_parse_region_mode(request, "back_region_mode", "enable_back_paint");
        const bool enable_front = front_region_mode == MeshFirstRegionMode::Paint;
        const bool enable_side = side_region_mode == MeshFirstRegionMode::Paint;
        const bool enable_back = back_region_mode == MeshFirstRegionMode::Paint;
        const bool replay_front_enabled = front_region_mode != MeshFirstRegionMode::Skip;
        const bool replay_side_enabled = side_region_mode != MeshFirstRegionMode::Skip;
        const bool replay_back_enabled = back_region_mode != MeshFirstRegionMode::Skip;
        const bool any_paint_region = enable_front || enable_side || enable_back;
        const bool preview_only = json_bool_field(request, "preview_only", false);
        const bool unpreview_only = json_bool_field(request, "unpreview_only", false);
        const bool normal_paint_requires_packed = !preview_only && !unpreview_only;
        const int packed_server_batch_limit = PackedReplicationBatchSize;
        const bool research_artifacts = json_bool_field(request, "research_artifacts", false);
        const double tuning_stroke_size_texels = clamp_range(json_number_field(request, "stroke_size_texels", 9.0), 1.0, 12.0);
        const double tuning_coverage_step_texels = clamp_range(json_number_field(request, "coverage_step_texels", 9.0), 1.0, 12.0);
        const double tuning_side_source_max_uv = clamp_range(json_number_field(request, "side_source_max_uv", 0.08), 0.001, 0.50);
        const double tuning_front_back_source_max_uv = clamp_range(json_number_field(request, "front_back_source_max_uv", 0.45), 0.001, 2.00);
        const bool tuning_auto_material_properties = json_bool_field(request, "auto_material_properties", true);
        const double tuning_metallic = clamp_range(json_number_field(request, "metallic", 0.0), 0.0, 1.0);
        const double tuning_roughness = clamp_range(json_number_field(request, "roughness", 1.0), 0.0, 1.0);
        const double fill_color_r = clamp_range(json_number_field(request, "fill_color_r", 1.0), 0.0, 1.0);
        const double fill_color_g = clamp_range(json_number_field(request, "fill_color_g", 1.0), 0.0, 1.0);
        const double fill_color_b = clamp_range(json_number_field(request, "fill_color_b", 1.0), 0.0, 1.0);
        const double fill_metallic = clamp_range(json_number_field(request, "fill_metallic", 1.0), 0.0, 1.0);
        const double fill_roughness = clamp_range(json_number_field(request, "fill_roughness", 0.0), 0.0, 1.0);
        const bool tuning_replication_pacing_enabled =
            json_bool_field(request, "replication_pacing_enabled", json_bool_field(request, "adaptive_batch_enabled", true));
        const int tuning_server_batch_limit = json_int_field(request, "server_batch_limit", PackedReplicationDefaultBatchLimit, 1, PackedReplicationMaxBatchLimit);
        const int tuning_server_batch_delay_ms = json_int_field(request, "server_batch_delay_ms", PackedReplicationDefaultPacingMs, 50, 100);
        const int packed_server_batch_seed_delay_ms = tuning_server_batch_delay_ms;
        const std::string requested_server_batch_rpc = json_string_field(request, "server_batch_rpc", "");
        const std::string requested_server_batch_rpc_normalized = lower_copy(requested_server_batch_rpc);
        const std::string requested_packed_route = lower_copy(json_string_field(request, "packed_route", "component"));
        const bool packed_route_requested =
            json_bool_field(request, "experimental_packed", false) ||
            requested_server_batch_rpc_normalized.empty() ||
            requested_server_batch_rpc_normalized == "packed" ||
            requested_server_batch_rpc_normalized == "serverpackedpaintbatch" ||
            requested_server_batch_rpc_normalized == "server_packed_paint_batch";

        std::string metadata = "\"route\":\"mesh_first_paint\"";
        const std::string mesh_first_pipeline =
            unpreview_only ? "local_preview_restore"
                           : (preview_only ? "profile_v2_pose_uv_atlas_local_preview"
                                           : "profile_v2_pose_uv_atlas_single_server_strokes");
        metadata += ",\"mesh_first_pipeline\":\"" + mesh_first_pipeline + "\"";
        metadata += ",\"preview_only\":" + std::string(json_bool(preview_only));
        metadata += ",\"unpreview_only\":" + std::string(json_bool(unpreview_only));
        metadata += ",\"mesh_region_model\":\"mesh_local_normal\"";
        metadata += ",\"mesh_source_model\":\"projected_visible_zbuffer\"";
        metadata += ",\"mesh_back_color_source\":\"shared_camera_facing_source\"";
        metadata += ",\"old_dense_hittest_fallback_used\":false";
        metadata += ",\"runtime_hit_test_used\":false";
        metadata += ",\"server_paint_batch_required\":" + std::string(json_bool(normal_paint_requires_packed));
        metadata += ",\"server_packed_paint_batch_required\":" + std::string(json_bool(normal_paint_requires_packed));
        metadata += ",\"research_artifacts_requested\":" + std::string(json_bool(research_artifacts));
        metadata += ",\"enable_front_paint\":" + std::string(json_bool(enable_front));
        metadata += ",\"enable_side_paint\":" + std::string(json_bool(enable_side));
        metadata += ",\"enable_back_paint\":" + std::string(json_bool(enable_back));
        metadata += ",\"front_region_mode\":\"" + std::string(mesh_first_region_mode_name(front_region_mode)) + "\"";
        metadata += ",\"side_region_mode\":\"" + std::string(mesh_first_region_mode_name(side_region_mode)) + "\"";
        metadata += ",\"back_region_mode\":\"" + std::string(mesh_first_region_mode_name(back_region_mode)) + "\"";
        metadata += ",\"front_region_active\":" + std::string(json_bool(replay_front_enabled));
        metadata += ",\"side_region_active\":" + std::string(json_bool(replay_side_enabled));
        metadata += ",\"back_region_active\":" + std::string(json_bool(replay_back_enabled));
        metadata += ",\"paint_region_count\":" + std::to_string((enable_front ? 1 : 0) + (enable_side ? 1 : 0) + (enable_back ? 1 : 0));
        metadata += ",\"fill_region_count\":" + std::to_string((front_region_mode == MeshFirstRegionMode::Fill ? 1 : 0) +
                                                               (side_region_mode == MeshFirstRegionMode::Fill ? 1 : 0) +
                                                               (back_region_mode == MeshFirstRegionMode::Fill ? 1 : 0));
        metadata += ",\"skip_region_count\":" + std::to_string((front_region_mode == MeshFirstRegionMode::Skip ? 1 : 0) +
                                                               (side_region_mode == MeshFirstRegionMode::Skip ? 1 : 0) +
                                                               (back_region_mode == MeshFirstRegionMode::Skip ? 1 : 0));
        metadata += ",\"server_batch_rpc_requested\":\"" + json_escape(requested_server_batch_rpc) + "\"";
        metadata += ",\"packed_route_requested_valid\":" + std::string(json_bool(packed_route_requested));
        metadata += ",\"packed_route_requested\":\"" + json_escape(requested_packed_route) + "\"";
        metadata += ",\"stroke_size_texels\":" + std::to_string(tuning_stroke_size_texels);
        metadata += ",\"coverage_step_texels\":" + std::to_string(tuning_coverage_step_texels);
        metadata += ",\"side_source_max_uv\":" + std::to_string(tuning_side_source_max_uv);
        metadata += ",\"front_back_source_max_uv\":" + std::to_string(tuning_front_back_source_max_uv);
        metadata += ",\"auto_material_properties\":" + std::string(json_bool(tuning_auto_material_properties));
        metadata += ",\"material_properties_mode\":\"" + std::string(tuning_auto_material_properties ? "auto" : "manual") + "\"";
        metadata += ",\"metallic\":" + std::to_string(tuning_metallic);
        metadata += ",\"roughness\":" + std::to_string(tuning_roughness);
        metadata += ",\"fill_color_space\":\"srgb\"";
        metadata += ",\"fill_color_r\":" + std::to_string(fill_color_r);
        metadata += ",\"fill_color_g\":" + std::to_string(fill_color_g);
        metadata += ",\"fill_color_b\":" + std::to_string(fill_color_b);
        metadata += ",\"fill_metallic\":" + std::to_string(fill_metallic);
        metadata += ",\"fill_roughness\":" + std::to_string(fill_roughness);
        metadata += ",\"replication_pacing_requested_enabled\":" + std::string(json_bool(tuning_replication_pacing_enabled));
        metadata += ",\"replication_pacing_enabled\":" + std::string(json_bool(normal_paint_requires_packed));
        metadata += ",\"server_batch_limit\":" +
                    std::to_string(normal_paint_requires_packed ? packed_server_batch_limit : tuning_server_batch_limit);
        metadata += ",\"server_batch_delay_ms\":" +
                    std::to_string(normal_paint_requires_packed ? packed_server_batch_seed_delay_ms : tuning_server_batch_delay_ms);
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

        if (!replay_front_enabled && !replay_side_enabled && !replay_back_enabled)
        {
            return response_json(false,
                                 "mesh_regions_skipped",
                                 0,
                                 1,
                                 "all mesh-first paint regions are skipped",
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
        if (!ctx.ok)
        {
            return response_json(false, ctx.stage.c_str(), 0, 1, ctx.message, metadata);
        }
        if (normal_paint_requires_packed && !ctx.server_packed_paint_batch_function)
        {
            return response_json(false,
                                 "mesh_server_packed_batch_unavailable",
                                 0,
                                 1,
                                 "ServerPackedPaintBatch is unavailable; paint cannot replay through the packed component route",
                                 metadata + ",\"server_packed_paint_batch_function\":\"" +
                                     hex_address(ctx.server_packed_paint_batch_function) + "\",\"replay_blocked\":true");
        }
        if (unpreview_only)
        {
            const auto snapshot = mesh_first_preview_snapshot_copy();
            metadata += ",\"unpreview_snapshot_available\":" + std::string(json_bool(snapshot.available));
            metadata += ",\"unpreview_snapshot_component\":\"" + hex_address(snapshot.component) + "\"";
            metadata += ",\"unpreview_snapshot_albedo_bytes\":" + std::to_string(snapshot.albedo_bytes.size());
            metadata += ",\"unpreview_snapshot_metallic_bytes\":" + std::to_string(snapshot.metallic_bytes.size());
            metadata += ",\"unpreview_snapshot_roughness_bytes\":" + std::to_string(snapshot.roughness_bytes.size());
            metadata += ",\"unpreview_snapshot_texture_size\":" + std::to_string(snapshot.texture_size);
            metadata += ",\"unpreview_snapshot_hash\":\"" + std::to_string(snapshot.hash) + "\"";
            if (!snapshot.available || snapshot.albedo_bytes.empty() || snapshot.metallic_bytes.empty() || snapshot.roughness_bytes.empty())
            {
                return response_json(false,
                                     "mesh_unpreview_snapshot_unavailable",
                                     0,
                                     1,
                                     "No local preview snapshot is available to restore.",
                                     metadata);
            }
            if (snapshot.component != ctx.component)
            {
                return response_json(false,
                                     "mesh_unpreview_component_mismatch",
                                     0,
                                     1,
                                     "The local preview snapshot belongs to a different paint component.",
                                     metadata + ",\"current_component\":\"" + hex_address(ctx.component) + "\"");
            }

            write_bridge_progress("mesh_unpreview_restore",
                                  "Restoring local preview texture",
                                  0,
                                  1,
                                  0.0,
                                  "\"phase\":\"local_unpreview\",\"terminal\":false,\"result\":\"running\"");
            const auto started = std::chrono::steady_clock::now();
            std::string import_failure{};
            auto restore_albedo_bytes = snapshot.albedo_bytes;
            auto restore_metallic_bytes = snapshot.metallic_bytes;
            auto restore_roughness_bytes = snapshot.roughness_bytes;
            auto restore_channel = [&](sdk::EPaintChannel channel,
                                       std::vector<std::uint8_t>& bytes,
                                       const char* label) -> bool {
                std::string channel_failure{};
                if (!mesh_first_import_channel_bytes(ref, ctx.component, channel, bytes, channel_failure))
                {
                    import_failure = std::string(label) + "_restore_failed:" + channel_failure;
                    return false;
                }
                return true;
            };
            const bool restored =
                restore_channel(sdk::EPaintChannel::Albedo, restore_albedo_bytes, "albedo") &&
                restore_channel(sdk::EPaintChannel::Metallic, restore_metallic_bytes, "metallic") &&
                restore_channel(sdk::EPaintChannel::Roughness, restore_roughness_bytes, "roughness");
            const double elapsed_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (restored)
            {
                mesh_first_clear_preview_snapshot();
            }
            metadata += ",\"unpreview_import_ok\":" + std::string(json_bool(restored));
            metadata += ",\"unpreview_import_failure\":\"" + json_escape(import_failure) + "\"";
            metadata += ",\"paint_elapsed_ms\":" + std::to_string(elapsed_ms);
            metadata += ",\"server_eta_ms\":0,\"local_eta_ms\":0,\"paint_eta_ms\":0";
            write_bridge_progress(restored ? "mesh_unpreview_done" : "mesh_unpreview_failed",
                                  restored ? "local preview material texture restored" : "local preview material restore failed",
                                  restored ? 1 : 0,
                                  1,
                                  elapsed_ms,
                                  "\"phase\":\"local_unpreview\",\"terminal\":true,\"result\":\"" +
                                      std::string(restored ? "done" : "failed") + "\"");
            return response_json(restored,
                                 restored ? "mesh_unpreview_done" : "mesh_unpreview_failed",
                                 restored ? 1 : 0,
                                 restored ? 0 : 1,
                                 restored ? "local preview material texture restored" : "local preview material restore failed: " + import_failure,
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
                                 "no live mesh candidate matched a required mesh profile",
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
                              "Loading required mesh profile",
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
            const std::string profile_stage = profile_catalog.empty() ? "mesh_profile_missing" : "mesh_profile_identity_mismatch";
            metadata += ",\"mesh_profile_stage\":\"" + profile_stage + "\"";
            metadata += ",\"mesh_profile_ok\":false";
            metadata += ",\"mesh_profile_failure\":\"" + json_escape(profile_failure.empty() ? "required mesh profile is unavailable or does not match the live mesh" : profile_failure) + "\"";
            metadata += ",\"mesh_identity_match\":false";
            metadata += ",\"profile_required\":true";
            metadata += ",\"dynamic_runtime_scan_fallback_enabled\":false";
            return response_json(false,
                                 profile_stage.c_str(),
                                 0,
                                 1,
                                 "mesh profile is required; dynamic runtime scan fallback is disabled",
                                 metadata + ",\"replay_blocked\":true");
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
            pose.message = "profile-free runtime triangle planning is disabled";
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
        std::string runtime_triangle_profile_cache_failure{};
        auto resolve_runtime_triangle_cache_once = [&]() {
            MeshFirstRuntimeTriangleCache cache{};
            std::string mode{"profile_verified"};
            std::string profile_cache_failure{};
            cache = mesh_first_resolve_runtime_triangle_cache(ctx.component, profile);
            if (!cache.ok)
            {
                profile_cache_failure = cache.failure;
                mode = "profile_verified_failed";
            }
            return std::make_tuple(std::move(cache), std::move(mode), std::move(profile_cache_failure));
        };
        auto runtime_coordinate_probe = [&](const MeshFirstRuntimeTriangleCache& cache) {
            MeshFirstRuntimeTriangleCoordinateSelection selection{};
            if (cache.ok)
            {
                auto triangles = cache.triangles;
                selection = mesh_first_select_runtime_triangle_coordinates(triangles, component_to_world);
            }
            return selection;
        };

        {
            auto resolved = resolve_runtime_triangle_cache_once();
            runtime_triangle_cache = std::move(std::get<0>(resolved));
            runtime_triangle_cache_mode = std::move(std::get<1>(resolved));
            runtime_triangle_profile_cache_failure = std::move(std::get<2>(resolved));
        }

        const auto runtime_coordinate_pre_warm = runtime_coordinate_probe(runtime_triangle_cache);
        MeshFirstRuntimePaintWarmup runtime_cache_warmup{};
        MeshFirstRuntimeTriangleCoordinateSelection runtime_coordinate_post_warm{};
        const bool runtime_cache_unstable_before_warmup =
            runtime_triangle_cache.ok &&
            (runtime_coordinate_pre_warm.samples <= 0 ||
             !std::isfinite(runtime_coordinate_pre_warm.selected_avg_error) ||
             runtime_coordinate_pre_warm.selected_avg_error > MeshFirstRuntimeCoordinateMaxAvgErrorCm);
        if (runtime_cache_unstable_before_warmup)
        {
            const auto warmup_viewport = sdk_get_viewport_info(ref, ctx);
            runtime_cache_warmup = mesh_first_warm_runtime_paint_cache(ref,
                                                                       ctx,
                                                                       selected_mesh.mesh,
                                                                       warmup_viewport,
                                                                       "runtime_triangle_coordinate_cache_unstable");
            auto resolved = resolve_runtime_triangle_cache_once();
            runtime_triangle_cache = std::move(std::get<0>(resolved));
            runtime_triangle_cache_mode = std::move(std::get<1>(resolved));
            runtime_triangle_profile_cache_failure = std::move(std::get<2>(resolved));
            runtime_coordinate_post_warm = runtime_coordinate_probe(runtime_triangle_cache);
        }
        metadata += ",\"runtime_triangle_cache_warmup_attempted\":" + std::string(json_bool(runtime_cache_warmup.attempted));
        metadata += ",\"runtime_triangle_cache_warmup_reason\":\"" + json_escape(runtime_cache_warmup.reason) + "\"";
        metadata += ",\"runtime_triangle_cache_warmup_pre_avg_error\":" + std::to_string(runtime_coordinate_pre_warm.selected_avg_error);
        metadata += ",\"runtime_triangle_cache_warmup_pre_samples\":" + std::to_string(runtime_coordinate_pre_warm.samples);
        metadata += ",\"runtime_triangle_cache_warmup_post_avg_error\":" + std::to_string(runtime_coordinate_post_warm.selected_avg_error);
        metadata += ",\"runtime_triangle_cache_warmup_post_samples\":" + std::to_string(runtime_coordinate_post_warm.samples);
        metadata += ",\"runtime_triangle_cache_warmup_is_initialized_available\":" + std::string(json_bool(runtime_cache_warmup.is_initialized_available));
        metadata += ",\"runtime_triangle_cache_warmup_is_initialized_before_ok\":" + std::string(json_bool(runtime_cache_warmup.is_initialized_before_ok));
        metadata += ",\"runtime_triangle_cache_warmup_is_initialized_before\":" + std::string(json_bool(runtime_cache_warmup.is_initialized_before));
        metadata += ",\"runtime_triangle_cache_warmup_is_initialized_after_ok\":" + std::string(json_bool(runtime_cache_warmup.is_initialized_after_ok));
        metadata += ",\"runtime_triangle_cache_warmup_is_initialized_after\":" + std::string(json_bool(runtime_cache_warmup.is_initialized_after));
        metadata += ",\"runtime_triangle_cache_warmup_initialize_available\":" + std::string(json_bool(runtime_cache_warmup.initialize_available));
        metadata += ",\"runtime_triangle_cache_warmup_initialize_called\":" + std::string(json_bool(runtime_cache_warmup.initialize_called));
        metadata += ",\"runtime_triangle_cache_warmup_initialize_ok\":" + std::string(json_bool(runtime_cache_warmup.initialize_ok));
        metadata += ",\"runtime_triangle_cache_warmup_initialize_skip_reason\":\"" + json_escape(runtime_cache_warmup.initialize_skip_reason) + "\"";
        metadata += ",\"runtime_triangle_cache_warmup_initialized_mesh_before\":\"" + hex_address(runtime_cache_warmup.initialized_mesh_before) + "\"";
        metadata += ",\"runtime_triangle_cache_warmup_initialized_mesh_after\":\"" + hex_address(runtime_cache_warmup.initialized_mesh_after) + "\"";
        metadata += ",\"runtime_triangle_cache_warmup_hit_test_available\":" + std::string(json_bool(runtime_cache_warmup.hit_test_available));
        metadata += ",\"runtime_triangle_cache_warmup_hit_test_uncached_called\":" + std::string(json_bool(runtime_cache_warmup.hit_test_uncached_called));
        metadata += ",\"runtime_triangle_cache_warmup_hit_test_uncached_ok\":" + std::string(json_bool(runtime_cache_warmup.hit_test_uncached_ok));
        metadata += ",\"runtime_triangle_cache_warmup_hit_test_uncached_hit\":" + std::string(json_bool(runtime_cache_warmup.hit_test_uncached_hit));
        metadata += ",\"runtime_triangle_cache_warmup_hit_test_cached_called\":" + std::string(json_bool(runtime_cache_warmup.hit_test_cached_called));
        metadata += ",\"runtime_triangle_cache_warmup_hit_test_cached_ok\":" + std::string(json_bool(runtime_cache_warmup.hit_test_cached_ok));
        metadata += ",\"runtime_triangle_cache_warmup_hit_test_cached_hit\":" + std::string(json_bool(runtime_cache_warmup.hit_test_cached_hit));
        metadata += ",\"runtime_triangle_cache_warmup_failure\":\"" + json_escape(runtime_cache_warmup.failure) + "\"";
        if (!runtime_triangle_profile_cache_failure.empty())
        {
            metadata += ",\"runtime_triangle_profile_cache_failure\":\"" + json_escape(runtime_triangle_profile_cache_failure) + "\"";
        }
        metadata += ",\"runtime_triangle_cache_used\":" + std::string(json_bool(runtime_triangle_cache.ok));
        metadata += ",\"runtime_triangle_cache_mode\":\"" + json_escape(runtime_triangle_cache_mode) + "\"";
        metadata += ",\"runtime_triangle_cache_offset\":\"" + (runtime_triangle_cache.owner_offset >= 0 ? hex_address(static_cast<std::uintptr_t>(runtime_triangle_cache.owner_offset)) : std::string("none")) + "\"";
        metadata += ",\"runtime_triangle_cache_stride\":" + std::to_string(runtime_triangle_cache.stride);
        metadata += ",\"runtime_triangle_cache_triangles\":" + std::to_string(runtime_triangle_cache.triangle_count);
        metadata += ",\"runtime_triangle_cache_profile_uv_avg_error\":" + std::to_string(runtime_triangle_cache.profile_uv_avg_error);
        metadata += ",\"runtime_triangle_cache_failure\":\"" + json_escape(runtime_triangle_cache.failure) + "\"";
        const bool runtime_uses_profile_component_world =
            profile_available && runtime_triangle_cache_mode == "profile_verified";
        metadata += ",\"planner_position_source\":\"" +
                    std::string(runtime_uses_profile_component_world
                                    ? "runtime_paintable_cached_local_component_world"
                                    : "runtime_paintable_cached_world_uv_only") +
                    "\"";
        metadata += ",\"pose_used_for_projection\":" + std::string(json_bool(runtime_uses_profile_component_world));
        metadata += ",\"pose_used_for_replay_anchor\":" + std::string(json_bool(runtime_uses_profile_component_world));
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
        auto runtime_coordinate_selection =
            mesh_first_select_runtime_triangle_coordinates(runtime_triangle_cache.triangles, component_to_world);
        metadata += ",\"runtime_triangle_coordinate_mode\":\"" + json_escape(runtime_coordinate_selection.mode) + "\"";
        metadata += ",\"runtime_triangle_coordinates_swapped\":" + std::string(runtime_coordinate_selection.swapped ? "true" : "false");
        metadata += ",\"runtime_triangle_coordinate_samples\":" + std::to_string(runtime_coordinate_selection.samples);
        metadata += ",\"runtime_triangle_coordinate_direct_avg_error\":" + std::to_string(runtime_coordinate_selection.direct_avg_error);
        metadata += ",\"runtime_triangle_coordinate_swapped_avg_error\":" + std::to_string(runtime_coordinate_selection.swapped_avg_error);
        metadata += ",\"runtime_triangle_coordinate_selected_avg_error\":" + std::to_string(runtime_coordinate_selection.selected_avg_error);
        metadata += ",\"component_world_transform_effective_source\":\"" + json_escape(component_transform_source) + "\"";
        const int active_texture_size = profile_available ? profile.texture_size : 1024;
        const char region_axis = profile_available ? mesh_first_region_axis(profile)
                                                   : mesh_first_region_axis_from_runtime_triangles(runtime_triangle_cache.triangles);
        metadata += ",\"mesh_region_axis\":\"" + std::string(mesh_first_region_axis_label(region_axis)) + "\"";
        metadata += ",\"mesh_region_axis_selection\":\"" + std::string(profile_available ? "profile_min_horizontal_extent" : "runtime_triangle_min_horizontal_extent") + "\"";
        metadata += ",\"mesh_region_normal_source\":\"" + std::string(profile_available ? "profile_v2_triangle_local_normal" : "runtime_triangle_local_normal") + "\"";
        metadata += ",\"texture_size\":" + std::to_string(active_texture_size);

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

        auto raw_runtime_triangles = runtime_triangle_cache.triangles;
        const auto raw_runtime_projection_selection =
            mesh_first_select_runtime_triangle_projection_coordinates(ref,
                                                                     ctx,
                                                                     raw_runtime_triangles,
                                                                     center_ray.location,
                                                                     camera_direction,
                                                                     viewport,
                                                                     "cached_runtime_world");
        metadata += ",\"runtime_triangle_raw_projection_mode\":\"" + json_escape(raw_runtime_projection_selection.mode) + "\"";
        metadata += ",\"runtime_triangle_raw_projection_samples\":" + std::to_string(raw_runtime_projection_selection.samples);
        metadata += ",\"runtime_triangle_raw_projection_source_candidates\":" + std::to_string(raw_runtime_projection_selection.source_candidates);
        metadata += ",\"runtime_triangle_raw_projection_project_ok\":" + std::to_string(raw_runtime_projection_selection.project_ok);
        metadata += ",\"runtime_triangle_raw_projection_inside_view\":" + std::to_string(raw_runtime_projection_selection.inside_view);
        metadata += ",\"runtime_triangle_raw_projection_best_score\":" + std::to_string(raw_runtime_projection_selection.best_score);
        metadata += ",\"runtime_triangle_raw_projection_summary\":\"" + json_escape(raw_runtime_projection_selection.summary) + "\"";

        const bool runtime_world_rebuild_required = runtime_uses_profile_component_world;
        auto runtime_world_rebuild = MeshFirstRuntimeTriangleWorldRebuild{};
        MeshFirstRuntimeTriangleProjectionSelection runtime_projection_selection{};
        if (runtime_world_rebuild_required)
        {
            runtime_world_rebuild =
                mesh_first_rebuild_runtime_triangle_world_from_local(runtime_triangle_cache.triangles, component_to_world);
            runtime_projection_selection =
                mesh_first_select_runtime_triangle_projection_coordinates(ref,
                                                                         ctx,
                                                                         runtime_triangle_cache.triangles,
                                                                         center_ray.location,
                                                                         camera_direction,
                                                                         viewport,
                                                                         "local_component_world");
        }
        else
        {
            auto runtime_world_rebuild_probe_triangles = runtime_triangle_cache.triangles;
            runtime_world_rebuild =
                mesh_first_rebuild_runtime_triangle_world_from_local(runtime_world_rebuild_probe_triangles, component_to_world);
            runtime_world_rebuild.applied = false;
            runtime_world_rebuild.mode = "diagnostic_skipped_uv_only_runtime";
            runtime_projection_selection = raw_runtime_projection_selection;
        }
        metadata += ",\"runtime_triangle_world_rebuild_required\":" + std::string(json_bool(runtime_world_rebuild_required));
        metadata += ",\"runtime_triangle_world_rebuild_mode\":\"" + json_escape(runtime_world_rebuild.mode) + "\"";
        metadata += ",\"runtime_triangle_world_rebuild_applied\":" + std::string(json_bool(runtime_world_rebuild.applied));
        metadata += ",\"runtime_triangle_world_rebuild_samples\":" + std::to_string(runtime_world_rebuild.samples);
        metadata += ",\"runtime_triangle_world_rebuild_avg_delta\":" + std::to_string(runtime_world_rebuild.avg_delta);
        metadata += ",\"runtime_triangle_world_rebuild_max_delta\":" + std::to_string(runtime_world_rebuild.max_delta);
        metadata += ",\"runtime_triangle_projection_active_source\":\"" +
                    std::string(runtime_world_rebuild_required ? "local_component_world" : "cached_runtime_world") + "\"";
        metadata += ",\"runtime_triangle_projection_mode\":\"" + json_escape(runtime_projection_selection.mode) + "\"";
        metadata += ",\"runtime_triangle_projection_samples\":" + std::to_string(runtime_projection_selection.samples);
        metadata += ",\"runtime_triangle_projection_source_candidates\":" + std::to_string(runtime_projection_selection.source_candidates);
        metadata += ",\"runtime_triangle_projection_project_ok\":" + std::to_string(runtime_projection_selection.project_ok);
        metadata += ",\"runtime_triangle_projection_inside_view\":" + std::to_string(runtime_projection_selection.inside_view);
        metadata += ",\"runtime_triangle_projection_best_score\":" + std::to_string(runtime_projection_selection.best_score);
        metadata += ",\"runtime_triangle_projection_summary\":\"" + json_escape(runtime_projection_selection.summary) + "\"";
        metadata += ",\"runtime_triangle_coordinate_max_avg_error\":" + std::to_string(MeshFirstRuntimeCoordinateMaxAvgErrorCm);
        if (runtime_world_rebuild_required &&
            (runtime_world_rebuild.samples <= 0 ||
             !std::isfinite(runtime_world_rebuild.avg_delta) ||
             runtime_world_rebuild.avg_delta > MeshFirstRuntimeCoordinateMaxAvgErrorCm))
        {
            return response_json(false,
                                 "runtime_triangle_coordinate_cache_unstable",
                                 0,
                                 1,
                                 "Runtime triangle cache local/world coordinates are not stable for the current component transform",
                                 metadata + ",\"replay_blocked\":true");
        }
        if (runtime_projection_selection.inside_view <= 0)
        {
            return response_json(false,
                                 "runtime_triangle_coordinate_projection_unavailable",
                                 0,
                                 1,
                                 runtime_world_rebuild_required
                                     ? "runtime triangle local-component coordinates do not project camera-facing samples into the current viewport"
                                     : "runtime triangle cached world coordinates do not project camera-facing samples into the current viewport",
                                 metadata + ",\"replay_blocked\":true");
        }

        MeshFirstPlanStats plan_stats{};
        std::vector<MeshFirstPlanSample> plan_samples{};
        std::string planner_failure{};
        metadata += ",\"mesh_region_threshold\":0.350000";
        metadata += ",\"mesh_region_threshold_source\":\"fixed_mesh_local_normal\"";
        if (!mesh_first_generate_plan_samples_from_runtime_cache(profile_available ? &profile : nullptr,
                                                                 runtime_triangle_cache.triangles,
                                                                 active_texture_size,
                                                                 center_ray.location,
                                                                 camera_direction,
                                                                 region_axis,
                                                                 tuning_coverage_step_texels,
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
        if (any_paint_region && native_front.samples.empty())
        {
            metadata += ",";
            metadata += mesh_first_plan_stats_metadata(plan_stats);
            return response_json(false,
                                 "planner_source_unavailable",
                                 0,
                                 1,
                                 "mesh-first planner produced no camera-facing source samples; paint is blocked because no reliable source color can be captured",
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

        SdkFrontCaptureResult capture{};
        metadata += ",\"mesh_capture_required\":" + std::string(json_bool(any_paint_region));
        if (any_paint_region)
        {
            write_bridge_progress("mesh_basecolor_capture",
                                  "Capturing mesh-first source BaseColor",
                                  3,
                                  4,
                                  0.0,
                                  "\"source_samples\":" + std::to_string(native_front.samples.size()));
            const auto capture_started = std::chrono::steady_clock::now();
            capture = sdk_capture_front_colors(ref, ctx, native_front, capture_request_width, capture_request_height);
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

            mesh_first_assign_colors(profile_available ? &profile : nullptr,
                                     plan_samples,
                                     capture,
                                     enable_front,
                                     enable_side,
                                     enable_back,
                                     tuning_side_source_max_uv,
                                     tuning_front_back_source_max_uv,
                                     plan_stats);
        }
        else
        {
            capture.failure = "skipped_no_paint_regions";
            metadata += ",\"mesh_capture_skipped\":true";
            metadata += ",\"mesh_capture_skip_reason\":\"no_paint_regions\"";
            metadata += ",\"mesh_capture_elapsed_ms\":0";
            metadata += ",\"mesh_capture_request_width\":" + std::to_string(capture_request_width);
            metadata += ",\"mesh_capture_request_height\":" + std::to_string(capture_request_height);
        }

        metadata += ",";
        metadata += mesh_first_plan_stats_metadata(plan_stats);
        metadata += ",\"planner_coverage_step_texels\":" + std::to_string(tuning_coverage_step_texels);
        metadata += ",\"source_distance_policy\":\"" + std::string(profile_available ? "side_visible_pose_component_nearest_front_back_projection_pixel" : "camera_projection_pixel_dynamic") + "\"";
        metadata += ",\"source_distance_side_max_uv\":" + std::to_string(tuning_side_source_max_uv);
        metadata += ",\"source_distance_front_back_max_uv\":" + std::to_string(tuning_front_back_source_max_uv);
        metadata += ",\"source_distance_side_max_component\":" + std::to_string(clamp_range(tuning_side_source_max_uv * 500.0, 20.0, 80.0));
        metadata += ",\"source_projection_color_available\":" + std::string(json_bool(capture.capture_pixels_available));
        metadata += ",\"source_samples\":" + std::to_string(capture.samples.size());
        metadata += ",\"unsafe_enabled\":" + std::to_string(plan_stats.unsafe_enabled);
        if (research_artifacts)
        {
            mesh_first_write_uv_debug_artifacts(plan_samples,
                                                active_texture_size,
                                                replay_front_enabled,
                                                replay_side_enabled,
                                                replay_back_enabled,
                                                metadata);
            mesh_first_write_projection_debug_artifact(plan_samples,
                                                       capture,
                                                       replay_front_enabled,
                                                       replay_side_enabled,
                                                       replay_back_enabled,
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

        const bool any_fill_region = front_region_mode == MeshFirstRegionMode::Fill ||
                                     side_region_mode == MeshFirstRegionMode::Fill ||
                                     back_region_mode == MeshFirstRegionMode::Fill;
        sdk::FRuntimeBrushSettings fill_brush = brush;
        const double fill_stroke_radius_texels =
            any_fill_region ? clamp_range(std::max(tuning_stroke_size_texels * 4.0, 32.0),
                                          tuning_stroke_size_texels,
                                          96.0)
                            : stroke_radius_texels;
        const double fill_stroke_radius_uv = fill_stroke_radius_texels / static_cast<double>(std::max(1, active_texture_size));
        const double fill_cell_uv = std::max(fill_stroke_radius_uv * 0.75, stroke_radius_uv);
        fill_brush.Radius = static_cast<float>(fill_stroke_radius_uv);
        metadata += ",\"fill_stroke_radius_texels\":" + std::to_string(fill_stroke_radius_texels);
        metadata += ",\"fill_stroke_radius_uv\":" + std::to_string(fill_stroke_radius_uv);
        metadata += ",\"fill_stroke_coarse_cell_uv\":" + std::to_string(fill_cell_uv);

        std::vector<sdk::FPaintStroke> strokes{};
        strokes.reserve(plan_samples.size());
        const bool use_mesh_anchors = runtime_triangle_cache.ok && runtime_uses_profile_component_world;
        const std::string replay_anchor_policy =
            use_mesh_anchors
                ? "profile_verified_triangle_anchor"
                : "uv_only_dynamic_runtime";
        metadata += ",\"replay_anchor_policy\":\"" + replay_anchor_policy + "\"";
        int replay_front = 0;
        int replay_side = 0;
        int replay_back = 0;
        int replay_paint = 0;
        int replay_fill = 0;
        int replay_front_paint = 0;
        int replay_side_paint = 0;
        int replay_back_paint = 0;
        int replay_front_fill = 0;
        int replay_side_fill = 0;
        int replay_back_fill = 0;
        int replay_fill_candidates = 0;
        int replay_fill_coarse_skipped = 0;
        int replay_world_anchors = 0;
        int replay_local_anchors = 0;
        int replay_triangle_anchors = 0;
        constexpr auto paint_target_channel = sdk::EPaintChannel::All;
        metadata += ",\"paint_target_channel\":\"all\"";
        metadata += ",\"paint_target_channel_value\":" + std::to_string(static_cast<int>(paint_target_channel));
        MeshFirstMaterialProperties material_properties{};
        if (any_paint_region && tuning_auto_material_properties)
        {
            material_properties = mesh_first_get_dominant_material_properties(ref, ctx.component);
        }
        metadata += ",\"material_properties_source\":\"" +
                    std::string(!any_paint_region
                                    ? "fill_material_only"
                                    : (!tuning_auto_material_properties
                                    ? "manual_tuning"
                                    : (material_properties.ok ? "dominant_paint_material_patterns" : "source_samples_fallback"))) +
                    "\"";
        metadata += ",\"material_properties_auto_ok\":" + std::string(json_bool(material_properties.ok));
        metadata += ",\"material_properties_failure\":\"" + json_escape(material_properties.failure) + "\"";
        metadata += ",\"material_properties_patterns\":" + std::to_string(material_properties.patterns);
        metadata += ",\"material_properties_sample_count\":" + std::to_string(material_properties.sample_count);
        metadata += ",\"material_properties_coverage_ratio\":" + std::to_string(material_properties.coverage_ratio);
        metadata += ",\"material_properties_metallic\":" + std::to_string(material_properties.metallic);
        metadata += ",\"material_properties_roughness\":" + std::to_string(material_properties.roughness);
        int material_properties_auto_samples = 0;
        int material_properties_source_sample_fallbacks = 0;
        std::unordered_set<std::uint64_t> fill_cells{};
        const auto fill_cell_key = [&](const MeshFirstPlanSample& sample) -> std::uint64_t {
            const int region = mesh_first_region_code(sample.region) & 0xFF;
            const int island = (sample.uv_island + 1) & 0xFFFF;
            const int u_cell = static_cast<int>(std::floor(clamp01(sample.u) / std::max(fill_cell_uv, 0.000001))) & 0xFFFFF;
            const int v_cell = static_cast<int>(std::floor(clamp01(sample.v) / std::max(fill_cell_uv, 0.000001))) & 0xFFFFF;
            return (static_cast<std::uint64_t>(region) << 56) |
                   (static_cast<std::uint64_t>(island) << 40) |
                   (static_cast<std::uint64_t>(u_cell) << 20) |
                   static_cast<std::uint64_t>(v_cell);
        };
        const MeshFirstRegion replay_region_order[]{MeshFirstRegion::Back, MeshFirstRegion::Side, MeshFirstRegion::Front};
        const MeshFirstRegionMode replay_mode_order[]{MeshFirstRegionMode::Fill, MeshFirstRegionMode::Paint};
        metadata += ",\"replay_pass_order\":\"fill_then_paint\"";
        metadata += ",\"replay_region_order\":\"back,side,front\"";
        for (const auto target_mode : replay_mode_order)
        {
            for (const auto target_region : replay_region_order)
            {
                const auto region_mode = mesh_first_region_mode_for_sample(target_region,
                                                                           front_region_mode,
                                                                           side_region_mode,
                                                                           back_region_mode);
                if (region_mode == MeshFirstRegionMode::Skip || region_mode != target_mode)
                {
                    continue;
                }
                for (const auto& sample : plan_samples)
                {
                    if (sample.region != target_region)
                    {
                        continue;
                    }
                    sdk::FPaintChannelData channel{};
                    const bool fill_mode = target_mode == MeshFirstRegionMode::Fill;
                    if (fill_mode)
                    {
                        ++replay_fill_candidates;
                        if (!fill_cells.insert(fill_cell_key(sample)).second)
                        {
                            ++replay_fill_coarse_skipped;
                            continue;
                        }
                        channel = sdk_make_channel(sdk_srgb_to_linear_unit(fill_color_r),
                                                   sdk_srgb_to_linear_unit(fill_color_g),
                                                   sdk_srgb_to_linear_unit(fill_color_b),
                                                   fill_metallic,
                                                   fill_roughness,
                                                   sdk::EPaintChannelApplyMode::Override);
                    }
                    else
                    {
                        double stroke_metallic = tuning_metallic;
                        double stroke_roughness = tuning_roughness;
                        if (tuning_auto_material_properties)
                        {
                            if (material_properties.ok)
                            {
                                stroke_metallic = material_properties.metallic;
                                stroke_roughness = material_properties.roughness;
                                ++material_properties_auto_samples;
                            }
                            else
                            {
                                stroke_metallic = clamp01(sample.metallic);
                                stroke_roughness = clamp01(sample.roughness);
                                ++material_properties_source_sample_fallbacks;
                            }
                        }
                        channel = sdk_make_channel(sdk_srgb_to_linear_unit(sample.r),
                                                   sdk_srgb_to_linear_unit(sample.g),
                                                   sdk_srgb_to_linear_unit(sample.b),
                                                   stroke_metallic,
                                                   stroke_roughness,
                                                   sdk::EPaintChannelApplyMode::Override);
                    }
                    const auto& stroke_brush = fill_mode ? fill_brush : brush;
                    auto stroke = use_mesh_anchors
                                      ? sdk_make_mesh_anchor_stroke(sample.u,
                                                                    sample.v,
                                                                    channel,
                                                                    stroke_brush,
                                                                    paint_target_channel,
                                                                    sample.world_position,
                                                                    sample.local_position,
                                                                    sample.triangle_index,
                                                                    sample.barycentric_a,
                                                                    sample.barycentric_b,
                                                                    sample.barycentric_c)
                                      : sdk_make_uv_stroke(sample.u,
                                                           sample.v,
                                                           channel,
                                                           stroke_brush,
                                                           paint_target_channel);
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
                    if (fill_mode)
                    {
                        ++replay_fill;
                    }
                    else
                    {
                        ++replay_paint;
                    }
                    if (sample.region == MeshFirstRegion::Front)
                    {
                        ++replay_front;
                        if (fill_mode)
                        {
                            ++replay_front_fill;
                        }
                        else
                        {
                            ++replay_front_paint;
                        }
                    }
                    else if (sample.region == MeshFirstRegion::Side)
                    {
                        ++replay_side;
                        if (fill_mode)
                        {
                            ++replay_side_fill;
                        }
                        else
                        {
                            ++replay_side_paint;
                        }
                    }
                    else
                    {
                        ++replay_back;
                        if (fill_mode)
                        {
                            ++replay_back_fill;
                        }
                        else
                        {
                            ++replay_back_paint;
                        }
                    }
                }
            }
        }
        metadata += ",\"material_properties_auto_samples\":" + std::to_string(material_properties_auto_samples);
        metadata += ",\"material_properties_source_sample_fallbacks\":" + std::to_string(material_properties_source_sample_fallbacks);
        metadata += ",\"replay_strokes_paint\":" + std::to_string(replay_paint);
        metadata += ",\"replay_strokes_fill\":" + std::to_string(replay_fill);
        metadata += ",\"replay_strokes_fill_candidates\":" + std::to_string(replay_fill_candidates);
        metadata += ",\"replay_strokes_fill_coarse_skipped\":" + std::to_string(replay_fill_coarse_skipped);
        metadata += ",\"replay_strokes_front_paint\":" + std::to_string(replay_front_paint);
        metadata += ",\"replay_strokes_side_paint\":" + std::to_string(replay_side_paint);
        metadata += ",\"replay_strokes_back_paint\":" + std::to_string(replay_back_paint);
        metadata += ",\"replay_strokes_front_fill\":" + std::to_string(replay_front_fill);
        metadata += ",\"replay_strokes_side_fill\":" + std::to_string(replay_side_fill);
        metadata += ",\"replay_strokes_back_fill\":" + std::to_string(replay_back_fill);
        metadata += ",\"planner_strokes_paint\":" + std::to_string(replay_paint);
        metadata += ",\"planner_strokes_fill\":" + std::to_string(replay_fill);
        if (strokes.empty())
        {
            return response_json(false,
                                 "mesh_replay_empty",
                                 0,
                                 1,
                                 "mesh-first planner produced no strokes for active regions",
                                 metadata + ",\"replay_blocked\":true");
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
        metadata += ",\"first_stroke_metallic\":" + std::to_string(first_stroke.ChannelData.Metallic);
        metadata += ",\"first_stroke_roughness\":" + std::to_string(first_stroke.ChannelData.Roughness);
        metadata += ",\"first_stroke_target_channel\":" + std::to_string(static_cast<int>(first_stroke.TargetChannel));
        metadata += ",\"replay_strokes_front\":" + std::to_string(replay_front);
        metadata += ",\"replay_strokes_side\":" + std::to_string(replay_side);
        metadata += ",\"replay_strokes_back\":" + std::to_string(replay_back);
        metadata += ",\"replay_strokes_total\":" + std::to_string(strokes.size());
        metadata += ",\"planner_strokes_front\":" + std::to_string(replay_front);
        metadata += ",\"planner_strokes_side\":" + std::to_string(replay_side);
        metadata += ",\"planner_strokes_back\":" + std::to_string(replay_back);
        metadata += ",\"planner_strokes_total\":" + std::to_string(strokes.size());
        const int effective_replay_server_batch_limit =
            normal_paint_requires_packed ? packed_server_batch_limit : tuning_server_batch_limit;
        const int effective_server_batch_delay_ms =
            normal_paint_requires_packed ? packed_server_batch_seed_delay_ms
                                         : std::max(PackedReplicationMinPacingMs, tuning_server_batch_delay_ms);
        const int estimated_batches = (static_cast<int>(strokes.size()) + std::max(1, effective_replay_server_batch_limit) - 1) /
                                      std::max(1, effective_replay_server_batch_limit);
        const int estimated_replay_ms = std::max(0, estimated_batches - 1) * effective_server_batch_delay_ms;
        metadata += ",\"server_batch_estimated_calls\":" + std::to_string(estimated_batches);
        metadata += ",\"estimated_replay_ms\":" + std::to_string(estimated_replay_ms);
        metadata += ",\"server_batch_delay_requested_ms\":" + std::to_string(tuning_server_batch_delay_ms);
        metadata += ",\"server_batch_delay_effective_ms\":" + std::to_string(effective_server_batch_delay_ms);
        metadata += ",\"skeletal_triangle_anchor_used\":" + std::string(json_bool(replay_triangle_anchors > 0));
        metadata += ",\"replay_anchor_mode\":\"" + std::string(use_mesh_anchors ? "skeletal_triangle" : "uv_only") + "\"";
        metadata += ",\"replay_world_anchors\":" + std::to_string(replay_world_anchors);
        metadata += ",\"replay_local_anchors\":" + std::to_string(replay_local_anchors);
        metadata += ",\"replay_triangle_anchors\":" + std::to_string(replay_triangle_anchors);
        const bool packed_batch_compatible = sdk_strokes_are_packed_compatible(strokes);
        const bool packed_component_available = ctx.server_packed_paint_batch_function != 0;
        const bool packed_relay_available = ctx.server_relay_packed_stroke_batch_function != 0 && live_uobject(ctx.relay_component);
        const bool use_packed_relay_route = false;
        sdk::FGuid packed_source_id{};
        std::string packed_source_id_failure = normal_paint_requires_packed ? "not_checked" : "not_required";
        bool packed_source_id_available = false;
        if (normal_paint_requires_packed && packed_component_available && packed_batch_compatible)
        {
            packed_source_id_available = sdk_read_component_packed_source_id(ctx.component,
                                                                             packed_source_id,
                                                                             packed_source_id_failure);
        }
        const bool use_packed_server_batch =
            normal_paint_requires_packed && packed_component_available && packed_batch_compatible && packed_source_id_available;
        std::string packed_ignored_reason = "none";
        if (!use_packed_server_batch)
        {
            if (preview_only)
            {
                packed_ignored_reason = "preview_only";
            }
            else if (unpreview_only)
            {
                packed_ignored_reason = "unpreview_only";
            }
            else if (!packed_component_available)
            {
                packed_ignored_reason = "component_route_unavailable";
            }
            else if (!packed_batch_compatible)
            {
                packed_ignored_reason = "incompatible_strokes";
            }
            else if (!packed_source_id_available)
            {
                packed_ignored_reason = "source_id_unavailable";
            }
            else
            {
                packed_ignored_reason = "not_required";
            }
        }
        metadata += ",\"server_batch_rpc\":\"" + std::string(normal_paint_requires_packed ? "ServerPackedPaintBatch" : "none") + "\"";
        metadata += ",\"server_paint_batch_used\":" + std::string(json_bool(normal_paint_requires_packed));
        metadata += ",\"server_paint_batch_single_stroke_mode\":" +
                    std::string(json_bool(normal_paint_requires_packed && effective_replay_server_batch_limit <= 1));
        metadata += ",\"server_packed_paint_batch_available\":" + std::string(json_bool(packed_component_available));
        metadata += ",\"server_relay_packed_stroke_batch_available\":" + std::string(json_bool(packed_relay_available));
        metadata += ",\"server_packed_paint_batch_function\":\"" + hex_address(ctx.server_packed_paint_batch_function) + "\"";
        metadata += ",\"server_relay_packed_stroke_batch_function\":\"" + hex_address(ctx.server_relay_packed_stroke_batch_function) + "\"";
        metadata += ",\"server_packed_paint_batch_used\":" + std::string(json_bool(use_packed_server_batch));
        metadata += ",\"server_packed_paint_batch_route\":\"component\"";
        metadata += ",\"server_packed_source_id_offset\":\"" + hex_address(RuntimePaintableComponentPackedSourceIdOffset) + "\"";
        metadata += ",\"server_packed_source_id_available\":" + std::string(json_bool(packed_source_id_available));
        metadata += ",\"server_packed_source_id_failure\":\"" + json_escape(packed_source_id_failure) + "\"";
        metadata += ",\"server_packed_batch_limit_cap\":" + std::to_string(PackedReplicationBatchSize);
        metadata += ",\"server_batch_limit_requested\":" + std::to_string(tuning_server_batch_limit);
        metadata += ",\"server_batch_limit_ignored_for_packed\":" + std::string(json_bool(normal_paint_requires_packed));
        metadata += ",\"server_batch_limit_effective\":" + std::to_string(effective_replay_server_batch_limit);
        metadata += ",\"server_packed_paint_batch_ignored\":\"" + json_escape(packed_ignored_reason) + "\"";
        if (normal_paint_requires_packed && !packed_component_available)
        {
            return response_json(false,
                                 "mesh_server_packed_batch_unavailable",
                                 0,
                                 1,
                                 "ServerPackedPaintBatch is unavailable; paint cannot replay through the packed component route",
                                 metadata + ",\"replay_blocked\":true");
        }
        if (normal_paint_requires_packed && !packed_batch_compatible)
        {
            return response_json(false,
                                 "mesh_server_packed_batch_incompatible",
                                 0,
                                 1,
                                 "ServerPackedPaintBatch requires skeletal triangle anchors for every stroke",
                                 metadata + ",\"replay_blocked\":true");
        }
        if (normal_paint_requires_packed && !packed_source_id_available)
        {
            return response_json(false,
                                 "mesh_server_packed_source_id_unavailable",
                                 0,
                                 1,
                                 "ServerPackedPaintBatch source id is unavailable: " + packed_source_id_failure,
                                 metadata + ",\"replay_blocked\":true");
        }
        const auto replication_manager = ref.find_first_instance("RuntimePaintReplicationManager");
        const bool fast_apply_component_strokes = false;
        const bool fast_apply_manager_strokes = false;
        const bool fast_apply_manager_writes = false;
        metadata += ",\"server_paint_target_channel\":\"all\"";
        if (preview_only)
        {
            metadata += ",\"local_paint_rpc\":\"ImportChannelFromBytes\"";
            metadata += ",\"local_visual_sync_mode\":\"local_albedo_channel_import_preview\"";
            metadata += ",\"local_batch_strategy\":\"single_channel_import_preview\"";
            metadata += ",\"local_paint_target_channel\":\"albedo\"";
            metadata += ",\"local_texture_import_byte_order\":\"rgba\"";
            metadata += ",\"local_visual_sync_required\":false";
            metadata += ",\"local_visual_sync_after_server_success\":false";
            metadata += ",\"local_visual_sync_lockstep_with_server_batch\":false";
            metadata += ",\"local_texture_import_required\":true";
            metadata += ",\"authoritative_replay\":\"local_texture_preview_only\"";
        }
        else
        {
            metadata += ",\"local_paint_rpc\":\"PaintAtUVWithBrush\"";
            metadata += ",\"local_paint_available\":" + std::string(json_bool(ctx.local_paint_at_uv_function != 0));
            metadata += ",\"local_visual_sync_mode\":\"paint_at_uv_with_brush_lockstep\"";
            metadata += ",\"local_batch_strategy\":\"single_stroke_lockstep\"";
            metadata += ",\"local_paint_target_channel\":\"all\"";
            metadata += ",\"local_visual_sync_required\":true";
            metadata += ",\"local_visual_sync_after_server_success\":true";
            metadata += ",\"local_visual_sync_after_each_server_stroke\":true";
            metadata += ",\"local_visual_sync_lockstep_with_server_batch\":true";
            metadata += ",\"local_texture_import_required\":false";
            metadata += ",\"authoritative_replay\":\"" +
                        std::string(use_packed_server_batch
                                        ? (use_packed_relay_route
                                               ? "server_relay_packed_replay_with_local_lockstep"
                                               : "server_packed_replay_with_local_lockstep")
                                        : "blocked") + "\"";
            if (!ctx.local_paint_at_uv_function)
            {
                return response_json(false,
                                     "mesh_local_visual_sync_unavailable",
                                     0,
                                     1,
                                     "PaintAtUVWithBrush is unavailable; local stroke sync cannot run",
                                     metadata + ",\"replay_blocked\":true");
            }
        }
        metadata += ",\"server_texture_sync_mode\":\"disabled\"";
        metadata += ",\"post_import_texture_sync_enabled\":" + std::string(json_bool(MeshFirstPostImportTextureSyncEnabled));
        metadata += ",\"server_texture_sync_poll_ms\":" + std::to_string(MeshFirstServerTextureSyncPollMs);
        metadata += ",\"server_texture_sync_max_polls\":" + std::to_string(MeshFirstServerTextureSyncMaxPolls);
        metadata += ",\"fast_apply_manager\":\"" + hex_address(replication_manager) + "\"";
        metadata += ",\"fast_apply_strokes_per_tick\":" + std::to_string(MeshFirstFastApplyStrokesPerTick);
        metadata += ",\"fast_apply_render_target_writes_per_frame\":" + std::to_string(MeshFirstFastApplyRenderTargetWritesPerFrame);
        metadata += ",\"fast_apply_component_strokes_written\":" + std::string(json_bool(fast_apply_component_strokes));
        metadata += ",\"fast_apply_manager_strokes_written\":" + std::string(json_bool(fast_apply_manager_strokes));
        metadata += ",\"fast_apply_manager_writes_written\":" + std::string(json_bool(fast_apply_manager_writes));
        metadata += ",\"fast_apply_property_writes_disabled\":true";
        const auto sync_channel_function = ref.find_function(ctx.component, "MulticastSyncChannelData");
        const auto sync_compressed_channel_function = ref.find_function(ctx.component, "MulticastSyncCompressedChannelData");
        reset_texture_sync_observer(sync_channel_function, sync_compressed_channel_function);
        metadata += ",\"texture_sync_hidden_route\":\"disabled_after_local_import_preview\"";
        metadata += ",\"texture_sync_relay_component\":\"" + hex_address(ctx.relay_component) + "\"";
        metadata += ",\"texture_sync_relay_component_class\":\"" + json_escape(ref.class_name(ctx.relay_component)) + "\"";
        metadata += ",\"function_server_relay_texture_sync_available\":" +
                    std::string(json_bool(live_uobject(ctx.relay_component) &&
                                          ref.find_function(ctx.relay_component, "ServerRelayTextureSync") != 0));
        metadata += ",\"function_relay_texture_sync_to_server_available\":" +
                    std::string(json_bool(live_uobject(ctx.relay_component) &&
                                          ref.find_function(ctx.relay_component, "RelayTextureSyncToServer") != 0));
        metadata += ",\"function_request_full_texture_sync_available\":" +
                    std::string(json_bool(ref.find_function(ctx.component, "RequestFullTextureSync") != 0));
        metadata += ",\"function_server_request_texture_sync_available\":" +
                    std::string(json_bool(ref.find_function(ctx.component, "ServerRequestTextureSync") != 0));
        metadata += ",\"function_multicast_sync_channel_data_available\":" +
                    std::string(json_bool(sync_channel_function != 0));
        metadata += ",\"function_multicast_sync_channel_data\":\"" + hex_address(sync_channel_function) + "\"";
        metadata += ",\"function_multicast_sync_compressed_channel_data_available\":" +
                    std::string(json_bool(sync_compressed_channel_function != 0));
        metadata += ",\"function_multicast_sync_compressed_channel_data\":\"" + hex_address(sync_compressed_channel_function) + "\"";
        const std::vector<const char*> component_paint_replication_candidates{
            "ServerPackedPaintBatch",
            "MulticastPackedPaintBatch",
            "MulticastPackedPaintBatchToOthers",
        };
        const std::vector<const char*> relay_paint_replication_candidates{
            "ServerRelayCompactPaint",
            "ServerRelayCompactStrokeBatch",
            "ServerRelayPackedStrokeBatch",
        };
        const std::vector<const char*> paint_replication_property_candidates{
            "bUseCompactPaintReplication",
            "bUseExperimentalPackedPaintReplication",
            "MaxOutgoingStrokesPerBatch",
            "MaxOutgoingNetworkBatchesPerSecond",
            "bCoalesceOutgoingStrokes",
        };
        metadata += paint_replication_function_probe_metadata(ref,
                                                              ctx.component,
                                                              "paint_replication_component_probe",
                                                              component_paint_replication_candidates);
        metadata += paint_replication_function_probe_metadata(ref,
                                                              ctx.relay_component,
                                                              "paint_replication_relay_probe",
                                                              relay_paint_replication_candidates);
        metadata += paint_replication_property_probe_metadata(ref,
                                                              ctx.component,
                                                              "paint_replication_component_property_probe",
                                                              paint_replication_property_candidates);
        metadata += paint_replication_property_probe_metadata(ref,
                                                              replication_manager,
                                                              "paint_replication_manager_property_probe",
                                                              paint_replication_property_candidates);
        metadata += texture_sync_observer_metadata("texture_sync_observer_before", texture_sync_observer_snapshot());

        MeshFirstChannelBytes albedo_before_bytes{};
        MeshFirstChannelBytes metallic_before_bytes{};
        MeshFirstChannelBytes roughness_before_bytes{};
        MeshFirstChannelChecksum albedo_before{};
        MeshFirstPreviewSnapshot existing_preview_snapshot{};
        bool preview_snapshot_reused = false;
        bool preview_snapshot_component_mismatch = false;
        if (preview_only)
        {
            existing_preview_snapshot = mesh_first_preview_snapshot_copy();
            if (existing_preview_snapshot.available && !existing_preview_snapshot.albedo_bytes.empty() &&
                !existing_preview_snapshot.metallic_bytes.empty() &&
                !existing_preview_snapshot.roughness_bytes.empty() &&
                existing_preview_snapshot.component == ctx.component)
            {
                albedo_before_bytes.ok = true;
                albedo_before_bytes.bytes = existing_preview_snapshot.albedo_bytes;
                albedo_before_bytes.failure = "ok";
                metallic_before_bytes.ok = true;
                metallic_before_bytes.bytes = existing_preview_snapshot.metallic_bytes;
                metallic_before_bytes.failure = "ok";
                roughness_before_bytes.ok = true;
                roughness_before_bytes.bytes = existing_preview_snapshot.roughness_bytes;
                roughness_before_bytes.failure = "ok";
                preview_snapshot_reused = true;
            }
            else
            {
                preview_snapshot_component_mismatch =
                    existing_preview_snapshot.available && existing_preview_snapshot.component != ctx.component;
                albedo_before_bytes = mesh_first_export_channel_bytes(ref, ctx.component, sdk::EPaintChannel::Albedo);
                metallic_before_bytes = mesh_first_export_channel_bytes(ref, ctx.component, sdk::EPaintChannel::Metallic);
                roughness_before_bytes = mesh_first_export_channel_bytes(ref, ctx.component, sdk::EPaintChannel::Roughness);
            }
        }
        if (preview_only && albedo_before_bytes.ok && metallic_before_bytes.ok && roughness_before_bytes.ok)
        {
            albedo_before.ok = true;
            albedo_before.bytes = static_cast<int>(albedo_before_bytes.bytes.size());
            albedo_before.hash = mesh_first_hash_channel_bytes(albedo_before_bytes.bytes);
            albedo_before.hash ^= mesh_first_hash_channel_bytes(metallic_before_bytes.bytes);
            albedo_before.hash *= 1099511628211ULL;
            albedo_before.hash ^= mesh_first_hash_channel_bytes(roughness_before_bytes.bytes);
            albedo_before.hash *= 1099511628211ULL;
            albedo_before.failure.clear();
        }
        else
        {
            albedo_before.failure = preview_only
                                        ? ("albedo:" + albedo_before_bytes.failure +
                                           ";metallic:" + metallic_before_bytes.failure +
                                           ";roughness:" + roughness_before_bytes.failure)
                                        : "skipped_for_server_stroke_stream";
        }
        metadata += ",\"albedo_export_before_ok\":" + std::string(json_bool(albedo_before.ok));
        metadata += ",\"albedo_export_before_bytes\":" + std::to_string(albedo_before.bytes);
        metadata += ",\"albedo_export_before_hash\":\"" + std::to_string(albedo_before.hash) + "\"";
        metadata += ",\"metallic_export_before_ok\":" + std::string(json_bool(metallic_before_bytes.ok));
        metadata += ",\"metallic_export_before_bytes\":" + std::to_string(metallic_before_bytes.bytes.size());
        metadata += ",\"roughness_export_before_ok\":" + std::string(json_bool(roughness_before_bytes.ok));
        metadata += ",\"roughness_export_before_bytes\":" + std::to_string(roughness_before_bytes.bytes.size());
        metadata += ",\"preview_snapshot_available_before\":" + std::string(json_bool(existing_preview_snapshot.available));
        metadata += ",\"preview_snapshot_reused\":" + std::string(json_bool(preview_snapshot_reused));
        metadata += ",\"preview_snapshot_component_mismatch_before\":" + std::string(json_bool(preview_snapshot_component_mismatch));
        metadata += ",\"albedo_before_source\":\"" +
                    std::string(preview_snapshot_reused ? "preview_snapshot" : (preview_only ? "export_channel" : "skipped")) + "\"";
        if (!albedo_before.ok)
        {
            metadata += ",\"albedo_export_before_failure\":\"" + json_escape(albedo_before.failure) + "\"";
        }

        const auto replication_before = mesh_first_capture_replication_snapshot(ref, ctx.component);

        if (preview_only)
        {
            const auto preview_started = std::chrono::steady_clock::now();
            write_bridge_progress("mesh_local_texture_import",
                                  "Importing local preview material texture",
                                  0,
                                  static_cast<int>(strokes.size()),
                                  0.0,
                                  "\"phase\":\"local_texture_import\",\"preview_only\":true,\"terminal\":false,\"result\":\"running\"");
            const auto* base_bytes = albedo_before_bytes.ok ? &albedo_before_bytes.bytes : nullptr;
            const auto* base_metallic_bytes = metallic_before_bytes.ok ? &metallic_before_bytes.bytes : nullptr;
            const auto* base_roughness_bytes = roughness_before_bytes.ok ? &roughness_before_bytes.bytes : nullptr;
            const auto result = mesh_first_apply_local_material_import_preview(ref,
                                                                               ctx.component,
                                                                               strokes,
                                                                               active_texture_size,
                                                                               base_bytes,
                                                                               base_metallic_bytes,
                                                                               base_roughness_bytes);
            if (result.ok && albedo_before_bytes.ok && metallic_before_bytes.ok && roughness_before_bytes.ok)
            {
                mesh_first_store_preview_snapshot(ctx.component,
                                                  active_texture_size,
                                                  albedo_before_bytes.bytes,
                                                  metallic_before_bytes.bytes,
                                                  roughness_before_bytes.bytes);
            }
            const double preview_elapsed_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - preview_started).count();
            metadata += ",\"server_batch_calls\":0";
            metadata += ",\"server_strokes_sent\":0";
            metadata += ",\"local_texture_import_started\":true";
            metadata += ",\"local_texture_import_ok\":" + std::string(json_bool(result.ok));
            metadata += ",\"local_texture_import_export_ok\":" + std::string(json_bool(result.export_ok));
            metadata += ",\"local_texture_import_import_ok\":" + std::string(json_bool(result.import_ok));
            metadata += ",\"local_texture_import_texture_size\":" + std::to_string(result.texture_size);
            metadata += ",\"local_texture_import_source_bytes\":" + std::to_string(result.source_bytes);
            metadata += ",\"local_texture_import_strokes_considered\":" + std::to_string(result.strokes_considered);
            metadata += ",\"local_texture_import_strokes_painted\":" + std::to_string(result.strokes_painted);
            metadata += ",\"local_texture_import_pixels_touched\":" + std::to_string(result.pixels_touched);
            metadata += ",\"local_texture_import_pixels_changed\":" + std::to_string(result.pixels_changed);
            metadata += ",\"local_texture_import_before_hash\":\"" + std::to_string(result.before_hash) + "\"";
            metadata += ",\"local_texture_import_preview_hash\":\"" + std::to_string(result.preview_hash) + "\"";
            metadata += ",\"local_texture_import_elapsed_ms\":" + std::to_string(result.elapsed_ms);
            metadata += ",\"local_texture_import_failure\":\"" + json_escape(result.failure) + "\"";
            metadata += ",\"unpreview_snapshot_stored\":" + std::string(json_bool(result.ok &&
                                                                                  albedo_before_bytes.ok &&
                                                                                  metallic_before_bytes.ok &&
                                                                                  roughness_before_bytes.ok));
            metadata += ",\"total_replay_elapsed_ms\":" + std::to_string(preview_elapsed_ms);
            metadata += ",\"paint_elapsed_ms\":" + std::to_string(preview_elapsed_ms);
            metadata += mesh_first_replication_snapshot_metadata("mesh_rep_before", replication_before);
            write_bridge_progress(result.ok ? "mesh_preview_done" : "mesh_preview_failed",
                                  result.ok ? "local preview material texture imported" : "local preview material texture import failed",
                                  result.ok ? static_cast<int>(strokes.size()) : result.strokes_painted,
                                  static_cast<int>(strokes.size()),
                                  preview_elapsed_ms,
                                  "\"phase\":\"local_texture_import\",\"preview_only\":true,\"terminal\":true,\"result\":\"" +
                                      std::string(result.ok ? "done" : "failed") + "\"");
            return response_json(result.ok,
                                 result.ok ? "mesh_preview_done" : "mesh_preview_failed",
                                 result.ok ? static_cast<int>(strokes.size()) : result.strokes_painted,
                                 result.ok ? 0 : 1,
                                 result.ok ? "local preview material texture imported" : "local preview material texture import failed: " + result.failure,
                                 metadata);
        }

        if (queued_job)
        {
            auto async_job = std::make_shared<MeshFirstServerBatchAsyncJob>();
            async_job->queued = queued_job;
            async_job->controller = ctx.controller;
            async_job->pawn = ctx.pawn;
            async_job->component = ctx.component;
            async_job->relay_component = ctx.relay_component;
            async_job->k2_get_pawn_function = ctx.k2_get_pawn_function;
            async_job->server_packed_paint_batch_function = ctx.server_packed_paint_batch_function;
            async_job->server_relay_packed_stroke_batch_function = ctx.server_relay_packed_stroke_batch_function;
            async_job->local_paint_at_uv_function = ctx.local_paint_at_uv_function;
            async_job->replication_manager = replication_manager;
            async_job->replication_recorded_count_function = ref.find_function(ctx.component, "GetRecordedStrokeCount");
            async_job->replication_queued_count_function = live_uobject(replication_manager)
                                                               ? ref.find_function(replication_manager, "GetQueuedStrokeCount")
                                                               : 0;
            async_job->replication_component_queued_count_function = live_uobject(replication_manager)
                                                                         ? ref.find_function(replication_manager, "GetQueuedStrokeCountForComponent")
                                                                         : 0;
            async_job->replication_pressure_function = live_uobject(replication_manager)
                                                           ? ref.find_function(replication_manager, "GetReplicationPressure")
                                                           : 0;
            async_job->server_packed_paint_batch_enabled = use_packed_server_batch;
            async_job->server_packed_paint_batch_use_relay = use_packed_relay_route;
            async_job->server_packed_paint_source_id = packed_source_id;
            async_job->server_batch_rpc = "ServerPackedPaintBatch";
            async_job->local_visual_sync_enabled = true;
            async_job->strokes = std::move(strokes);
            async_job->metadata = metadata + ",\"server_batch_schedule\":\"timer_drained\"";
            async_job->albedo_before = albedo_before;
            async_job->replication_before = replication_before;
            async_job->replication_component_max_replicated_strokes_per_tick =
                replication_before.component_max_replicated_strokes_per_tick;
            async_job->replication_component_use_compact_replication =
                replication_before.component_use_compact_replication;
            async_job->replication_component_use_packed_replication =
                replication_before.component_use_packed_replication;
            async_job->replication_manager_max_replicated_strokes_per_tick =
                replication_before.manager_max_replicated_strokes_per_tick;
            async_job->replication_manager_max_render_target_writes_per_frame =
                replication_before.manager_max_render_target_writes_per_frame;
            async_job->replication_manager_max_outgoing_strokes_per_batch =
                replication_before.manager_max_outgoing_strokes_per_batch;
            async_job->replication_manager_max_outgoing_network_batches_per_second =
                replication_before.manager_max_outgoing_network_batches_per_second;
            async_job->replication_manager_coalesce_outgoing_strokes =
                replication_before.manager_coalesce_outgoing_strokes;
            async_job->replication_pacing_enabled = normal_paint_requires_packed;
            async_job->replication_pacing_requested_batch_limit = effective_replay_server_batch_limit;
            async_job->replication_pacing_requested_delay_ms = effective_server_batch_delay_ms;
            mesh_first_update_replication_pacing_model(async_job, replication_before);
            if (async_job->replication_pacing_enabled)
            {
                mesh_first_update_replication_pacing_resolved_batch(async_job, replication_before);
                mesh_first_set_replication_pacing_effective(async_job,
                                                  mesh_first_replication_pacing_resolved_batch(async_job),
                                                  effective_server_batch_delay_ms);
                const auto initial_level =
                    mesh_first_replication_pacing_queue_gate_pressure_level(async_job, replication_before);
                async_job->replication_pacing_pressure_level = mesh_first_replication_pacing_pressure_level_name(initial_level);
            }
            else
            {
                async_job->replication_pacing_resolved_batch_limit = effective_replay_server_batch_limit;
                mesh_first_set_replication_pacing_effective(async_job, effective_replay_server_batch_limit, effective_server_batch_delay_ms);
                async_job->replication_pacing_pressure_level = "disabled";
            }
            async_job->server_texture_sync_poll_ms = MeshFirstServerTextureSyncPollMs;
            async_job->server_texture_sync_max_polls = MeshFirstServerTextureSyncMaxPolls;
            async_job->texture_size = active_texture_size;
            async_job->replay_front = replay_front;
            async_job->replay_side = replay_side;
            async_job->replay_back = replay_back;
            async_job->started = std::chrono::steady_clock::now();
            async_job->phase = MeshFirstBatchPhase::ServerBatch;
            {
                std::lock_guard<std::mutex> lock(g_mesh_first_batch_mutex);
                g_mesh_first_batch_job = async_job;
            }
            write_bridge_progress("mesh_server_batch_begin",
                                  "mesh-first " + async_job->server_batch_rpc + " stream prepared",
                                  0,
                                  static_cast<int>(async_job->strokes.size()),
                                  0.0,
                                  mesh_first_progress_extra(async_job,
                                                            MeshFirstBatchPhase::ServerBatch,
                                                            false,
                                                            "running"));
            post_paint_dispatch_message();
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
        if (job->completed.exchange(true))
        {
            return;
        }
        if (job->dispatch_timer_id)
        {
            KillTimer(nullptr, job->dispatch_timer_id);
            job->dispatch_timer_id = 0;
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
        if (!mark_queued_paint_job_dispatched(queued_job))
        {
            return false;
        }
        const auto response = paint_mesh_first_on_game_thread(request, queued_job);
        if (!response.empty())
        {
            complete_queued_paint_job(queued_job, response);
        }
        return true;
    }

    auto mesh_first_cancel_response(const std::shared_ptr<MeshFirstServerBatchAsyncJob>& job,
                                    const std::string& cancel_reason,
                                    const std::string& cancel_phase = {}) -> std::string
    {
        const int local_synced = job ? job->local_stroke_success : 0;
        const int local_total = job ? static_cast<int>(job->strokes.size()) : 0;
        const int server_sent = job ? job->server_strokes_sent : 0;
        const int remaining_strokes = job ? std::max(0, local_total - server_sent) : 0;
        const double local_elapsed_ms = job && job->local_sync_started ? job->local_visual_sync_elapsed_ms : -1.0;
        return response_json(false,
                             "mesh_paint_cancelled",
                             server_sent,
                             1,
                             "mesh-first paint cancelled: " + cancel_reason,
                             (job ? job->metadata : std::string{}) +
                                 ",\"cancelled\":true" +
                                 ",\"cancel_reason\":\"" + json_escape(cancel_reason) + "\"" +
                                 ",\"cancel_phase\":\"" + json_escape(cancel_phase.empty()
                                                                        ? std::string(job ? mesh_first_phase_name(job->phase) : "unknown")
                                                                        : cancel_phase) + "\"" +
                                 ",\"server_batch_calls\":" + std::to_string(job ? job->server_batch_calls : 0) +
                                 ",\"server_batch_success\":" + std::to_string(job ? job->server_batch_success : 0) +
                                 ",\"server_batch_failures\":" + std::to_string(job ? job->server_batch_failures : 0) +
                                 ",\"server_strokes_sent\":" + std::to_string(server_sent) +
                                 ",\"server_batch_elapsed_ms\":" + std::to_string(job ? job->server_batch_elapsed_ms : -1.0) +
                                 ",\"server_elapsed_ms\":" + std::to_string(job ? job->server_batch_elapsed_ms : -1.0) +
                                 ",\"server_eta_ms\":0" +
                                 ",\"local_visual_sync_used\":" + std::string(json_bool(job && job->local_visual_sync_enabled)) +
                                 ",\"local_visual_sync_started\":" + std::string(json_bool(job && job->local_sync_started)) +
                                 ",\"local_visual_sync_elapsed_ms\":" + std::to_string(local_elapsed_ms) +
                                     ",\"local_visual_sync_failure\":\"" + json_escape(job ? job->local_visual_sync_failure : std::string{}) + "\"" +
                                     ",\"local_batch_limit\":" + std::to_string(job ? job->local_visual_sync_batch_limit : 0) +
                                     ",\"local_batch_delay_ms\":" + std::to_string(job ? job->local_visual_sync_delay_ms : 0) +
                                     ",\"local_batch_calls\":" + std::to_string(job ? job->local_batch_calls : 0) +
                                 ",\"local_stroke_success\":" + std::to_string(local_synced) +
                                 ",\"local_stroke_failures\":" + std::to_string(job ? job->local_stroke_failures : 0) +
                                 ",\"local_strokes_synced\":" + std::to_string(local_synced) +
                                 ",\"local_strokes_total\":" + std::to_string(local_total) +
                                 ",\"local_elapsed_ms\":" + std::to_string(local_elapsed_ms) +
                                 ",\"local_eta_ms\":0" +
                                 mesh_first_replication_pacing_metadata(job) +
                                 ",\"paint_elapsed_ms\":" + std::to_string(job ? mesh_first_elapsed_ms(job) : -1.0) +
                                 ",\"paint_eta_ms\":0" +
                                 ",\"remaining_strokes\":" + std::to_string(remaining_strokes));
    }

    auto cancel_active_mesh_first_batch_job(const char* reason) -> int
    {
        std::shared_ptr<MeshFirstServerBatchAsyncJob> job{};
        {
            std::lock_guard<std::mutex> lock(g_mesh_first_batch_mutex);
            job = g_mesh_first_batch_job;
        }
        if (!job)
        {
            return 0;
        }
        const std::string cancel_reason = reason && *reason ? reason : "cancelled";
        job->cancel_reason = cancel_reason;
        job->cancel_requested.store(true);
        if (job->dispatch_timer_id)
        {
            KillTimer(nullptr, job->dispatch_timer_id);
            job->dispatch_timer_id = 0;
        }
        job->next_dispatch_time = {};
        post_paint_dispatch_message();
        return 1;
    }

    auto force_cancel_active_mesh_first_batch_job(const char* reason) -> int
    {
        std::shared_ptr<MeshFirstServerBatchAsyncJob> job{};
        {
            std::lock_guard<std::mutex> lock(g_mesh_first_batch_mutex);
            job = g_mesh_first_batch_job;
        }
        if (!job)
        {
            return 0;
        }
        const std::string cancel_reason = reason && *reason ? reason : "cancelled";
        const std::string cancel_phase = mesh_first_phase_name(job->phase);
        const int remaining_strokes = mesh_first_remaining_strokes(job);
        job->cancel_reason = cancel_reason;
        job->cancel_requested.store(true);
        if (job->dispatch_timer_id)
        {
            KillTimer(nullptr, job->dispatch_timer_id);
            job->dispatch_timer_id = 0;
        }
        job->next_dispatch_time = {};
        job->phase = MeshFirstBatchPhase::Cancelled;
        if (job->server_batch_elapsed_ms < 0.0)
        {
            job->server_batch_elapsed_ms = mesh_first_elapsed_ms(job);
        }
        if (job->local_sync_started)
        {
            job->local_visual_sync_elapsed_ms = mesh_first_local_elapsed_ms(job);
        }
        write_bridge_progress("mesh_paint_cancelled",
                              "mesh-first paint cancelled by shutdown",
                              job->local_stroke_success,
                              std::max(1, static_cast<int>(job->strokes.size())),
                              mesh_first_elapsed_ms(job),
                              mesh_first_progress_extra(job,
                                                        MeshFirstBatchPhase::Cancelled,
                                                        true,
                                                        "cancelled",
                                                        "\"remaining_strokes\":" + std::to_string(remaining_strokes) +
                                                            ",\"cancel_phase\":\"" + json_escape(cancel_phase) + "\""));
        complete_mesh_first_batch_job(job, mesh_first_cancel_response(job, cancel_reason, cancel_phase));
        return 1;
    }

    auto cancel_queued_paint_jobs(const char* reason) -> int
    {
        std::vector<std::shared_ptr<QueuedPaintJob>> jobs{};
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            jobs.swap(g_paint_jobs);
        }
        const std::string cancel_reason = reason && *reason ? reason : "cancelled";
        for (const auto& job : jobs)
        {
            complete_queued_paint_job(job,
                                      response_json(false,
                                                    "paint_cancelled",
                                                    0,
                                                    1,
                                                    "paint cancelled: " + cancel_reason,
                                                    "\"cancelled\":true,\"cancel_reason\":\"" + json_escape(cancel_reason) + "\""));
        }
        return static_cast<int>(jobs.size());
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
        if (job->completed.load())
        {
            return;
        }

        auto clear_dispatch_timer = [&]() {
            if (job->dispatch_timer_id)
            {
                KillTimer(nullptr, job->dispatch_timer_id);
                job->dispatch_timer_id = 0;
            }
        };
        clear_dispatch_timer();

        auto post_next_after = [&](int delay_ms) {
            if (g_game_thread_id.load() || g_game_window.load())
            {
                if (delay_ms <= 0)
                {
                    post_paint_dispatch_message();
                    return;
                }
                const auto timer_id = SetTimer(nullptr,
                                               0,
                                               static_cast<UINT>(std::max(1, delay_ms)),
                                               paint_dispatch_timer_proc);
                if (timer_id)
                {
                    job->dispatch_timer_id = timer_id;
                    return;
                }
                post_paint_dispatch_message();
            }
        };

        auto elapsed_ms = [&]() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->started).count();
        };

        auto write_mesh_progress = [&](const std::string& stage,
                                       const std::string& message,
                                       int step,
                                       int total,
                                       MeshFirstBatchPhase phase,
                                       bool terminal,
                                       const char* result,
                                       const std::string& extra = "") {
            write_bridge_progress(stage,
                                  message,
                                  step,
                                  std::max(1, total),
                                  elapsed_ms(),
                                  mesh_first_progress_extra(job, phase, terminal, result, extra));
        };

        auto finish_cancelled = [&]() {
            const std::string cancelled_from_phase = mesh_first_phase_name(job->phase);
            const int remaining_strokes = mesh_first_remaining_strokes(job);
            job->phase = MeshFirstBatchPhase::Cancelled;
            if (job->server_batch_elapsed_ms < 0.0)
            {
                job->server_batch_elapsed_ms = elapsed_ms();
            }
            if (job->local_sync_started)
            {
                job->local_visual_sync_elapsed_ms = mesh_first_local_elapsed_ms(job);
            }
            const std::string cancel_reason = job->cancel_reason.empty() ? "cancelled" : job->cancel_reason;
            write_mesh_progress("mesh_paint_cancelled",
                                "mesh-first paint cancelled",
                                job->local_stroke_success,
                                static_cast<int>(job->strokes.size()),
                                MeshFirstBatchPhase::Cancelled,
                                true,
                                "cancelled",
                                "\"remaining_strokes\":" + std::to_string(remaining_strokes) +
                                    ",\"cancel_phase\":\"" + json_escape(cancelled_from_phase) + "\"");
            complete_mesh_first_batch_job(job, mesh_first_cancel_response(job, cancel_reason, cancelled_from_phase));
        };

        auto finish_failed = [&](const std::string& stage,
                                 const std::string& message,
                                 const std::string& failure) {
            job->phase = MeshFirstBatchPhase::Failed;
            if (job->server_batch_elapsed_ms < 0.0)
            {
                job->server_batch_elapsed_ms = elapsed_ms();
            }
            if (job->local_sync_started)
            {
                job->local_visual_sync_elapsed_ms = mesh_first_local_elapsed_ms(job);
            }
            const double total_elapsed_ms = elapsed_ms();
            std::string metadata = job->metadata;
            metadata += ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls);
            metadata += ",\"server_batch_success\":" + std::to_string(job->server_batch_success);
            metadata += ",\"server_batch_failures\":" + std::to_string(job->server_batch_failures);
            metadata += ",\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent);
            metadata += ",\"server_batch_elapsed_ms\":" + std::to_string(job->server_batch_elapsed_ms);
            metadata += ",\"server_elapsed_ms\":" + std::to_string(job->server_batch_elapsed_ms);
            metadata += ",\"server_eta_ms\":0";
            metadata += mesh_first_replication_pacing_metadata(job);
            metadata += ",\"local_batch_limit\":" + std::to_string(job->local_visual_sync_batch_limit);
            metadata += ",\"local_batch_delay_ms\":" + std::to_string(job->local_visual_sync_delay_ms);
            metadata += ",\"local_batch_calls\":" + std::to_string(job->local_batch_calls);
            metadata += ",\"local_stroke_calls\":" + std::to_string(job->local_stroke_calls);
            metadata += ",\"local_stroke_success\":" + std::to_string(job->local_stroke_success);
            metadata += ",\"local_stroke_failures\":" + std::to_string(job->local_stroke_failures);
            metadata += ",\"local_strokes_synced\":" + std::to_string(job->local_stroke_success);
            metadata += ",\"local_strokes_total\":" + std::to_string(job->strokes.size());
            metadata += ",\"local_visual_sync_used\":" + std::string(json_bool(job->local_visual_sync_enabled));
            metadata += ",\"local_visual_sync_started\":" + std::string(json_bool(job->local_sync_started));
            metadata += ",\"local_visual_sync_elapsed_ms\":" + std::to_string(job->local_sync_started ? job->local_visual_sync_elapsed_ms : -1.0);
            metadata += ",\"local_elapsed_ms\":" + std::to_string(job->local_sync_started ? job->local_visual_sync_elapsed_ms : -1.0);
            metadata += ",\"local_eta_ms\":0";
            metadata += ",\"local_visual_sync_failure\":\"" + json_escape(job->local_visual_sync_failure) + "\"";
            metadata += ",\"total_replay_elapsed_ms\":" + std::to_string(total_elapsed_ms);
            metadata += ",\"paint_elapsed_ms\":" + std::to_string(total_elapsed_ms);
            metadata += ",\"paint_eta_ms\":0";
            metadata += ",\"first_failure\":\"" + json_escape(failure) + "\"";
            metadata += mesh_first_replication_snapshot_metadata("mesh_rep_before", job->replication_before);
            write_mesh_progress("mesh_paint_failed",
                                message,
                                job->local_stroke_success,
                                static_cast<int>(job->strokes.size()),
                                MeshFirstBatchPhase::Failed,
                                true,
                                "failed",
                                "\"failure_stage\":\"" + json_escape(stage) + "\",\"first_failure\":\"" + json_escape(failure) + "\"");
            complete_mesh_first_batch_job(job,
                                          response_json(false,
                                                        stage.c_str(),
                                                        job->server_strokes_sent,
                                                        1,
                                                        message,
                                                        metadata));
        };

        auto finish_done = [&]() {
            job->phase = MeshFirstBatchPhase::Done;
            if (job->server_batch_elapsed_ms < 0.0)
            {
                job->server_batch_elapsed_ms = elapsed_ms();
            }
            if (job->local_sync_started)
            {
                job->local_visual_sync_elapsed_ms = mesh_first_local_elapsed_ms(job);
            }
            if (job->server_texture_sync_started && job->server_texture_sync_started_at.time_since_epoch().count() != 0)
            {
                job->server_texture_sync_elapsed_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->server_texture_sync_started_at).count();
            }
            const double total_elapsed_ms = elapsed_ms();
            const bool local_visual_sync_ok =
                !job->local_visual_sync_enabled ||
                (job->local_visual_sync_failure.empty() &&
                 job->local_stroke_success == static_cast<int>(job->strokes.size()));
            const bool server_texture_sync_ok =
                job->server_texture_sync_started &&
                job->server_texture_sync_albedo_changed &&
                job->server_texture_sync_failure.empty();
            const bool local_texture_import_ok =
                job->local_texture_import_started &&
                job->local_texture_import_ok &&
                job->local_texture_import_failure.empty();
            const bool server_only_replay_ok =
                !job->local_visual_sync_enabled &&
                !job->local_texture_import_started &&
                !job->server_texture_sync_started &&
                job->server_batch_failures == 0 &&
                job->server_strokes_sent == static_cast<int>(job->strokes.size());
            const bool visual_sync_ok =
                server_only_replay_ok ||
                (job->local_visual_sync_enabled ? local_visual_sync_ok : (local_texture_import_ok || server_texture_sync_ok));
            const bool paint_ok = local_visual_sync_ok && visual_sync_ok;
            std::string metadata = job->metadata;
            metadata += ",\"server_only_replay_ok\":" + std::string(json_bool(server_only_replay_ok));
            metadata += ",\"server_batch_calls\":" + std::to_string(job->server_batch_calls);
            metadata += ",\"server_batch_success\":" + std::to_string(job->server_batch_success);
            metadata += ",\"server_batch_failures\":" + std::to_string(job->server_batch_failures);
            metadata += ",\"server_strokes_sent\":" + std::to_string(job->server_strokes_sent);
            metadata += ",\"server_batch_elapsed_ms\":" + std::to_string(job->server_batch_elapsed_ms);
            metadata += ",\"server_elapsed_ms\":" + std::to_string(job->server_batch_elapsed_ms);
            metadata += ",\"server_eta_ms\":0";
            metadata += mesh_first_replication_pacing_metadata(job);
            metadata += ",\"local_batch_limit\":" + std::to_string(job->local_visual_sync_batch_limit);
            metadata += ",\"local_batch_delay_ms\":" + std::to_string(job->local_visual_sync_delay_ms);
            metadata += ",\"local_batch_calls\":" + std::to_string(job->local_batch_calls);
            metadata += ",\"local_stroke_calls\":" + std::to_string(job->local_stroke_calls);
            metadata += ",\"local_stroke_success\":" + std::to_string(job->local_stroke_success);
            metadata += ",\"local_stroke_failures\":" + std::to_string(job->local_stroke_failures);
            metadata += ",\"local_strokes_synced\":" + std::to_string(job->local_stroke_success);
            metadata += ",\"local_strokes_total\":" + std::to_string(job->strokes.size());
            metadata += ",\"local_visual_sync_used\":" + std::string(json_bool(job->local_visual_sync_enabled));
            metadata += ",\"local_visual_sync_ok\":" + std::string(json_bool(local_visual_sync_ok));
            metadata += ",\"local_visual_sync_failure\":\"" + json_escape(job->local_visual_sync_failure) + "\"";
            metadata += ",\"local_visual_sync_started\":" + std::string(json_bool(job->local_sync_started));
            metadata += ",\"local_visual_sync_elapsed_ms\":" + std::to_string(job->local_sync_started ? job->local_visual_sync_elapsed_ms : -1.0);
            metadata += ",\"local_elapsed_ms\":" + std::to_string(job->local_sync_started ? job->local_visual_sync_elapsed_ms : -1.0);
            metadata += ",\"local_eta_ms\":0";
            metadata += ",\"local_texture_import_started\":" + std::string(json_bool(job->local_texture_import_started));
            metadata += ",\"local_texture_import_ok\":" + std::string(json_bool(job->local_texture_import_ok));
            metadata += ",\"local_texture_import_export_ok\":" + std::string(json_bool(job->local_texture_import_export_ok));
            metadata += ",\"local_texture_import_import_ok\":" + std::string(json_bool(job->local_texture_import_import_ok));
            metadata += ",\"local_texture_import_texture_size\":" + std::to_string(job->local_texture_import_texture_size);
            metadata += ",\"local_texture_import_source_bytes\":" + std::to_string(job->local_texture_import_source_bytes);
            metadata += ",\"local_texture_import_strokes_considered\":" + std::to_string(job->local_texture_import_strokes_considered);
            metadata += ",\"local_texture_import_strokes_painted\":" + std::to_string(job->local_texture_import_strokes_painted);
            metadata += ",\"local_texture_import_pixels_touched\":" + std::to_string(job->local_texture_import_pixels_touched);
            metadata += ",\"local_texture_import_pixels_changed\":" + std::to_string(job->local_texture_import_pixels_changed);
            metadata += ",\"local_texture_import_before_hash\":\"" + std::to_string(job->local_texture_import_before_hash) + "\"";
            metadata += ",\"local_texture_import_preview_hash\":\"" + std::to_string(job->local_texture_import_preview_hash) + "\"";
            metadata += ",\"local_texture_import_elapsed_ms\":" + std::to_string(job->local_texture_import_elapsed_ms);
            metadata += ",\"local_texture_import_failure\":\"" + json_escape(job->local_texture_import_failure) + "\"";
            metadata += ",\"server_texture_sync_started\":" + std::string(json_bool(job->server_texture_sync_started));
            metadata += ",\"server_texture_sync_request_full_available\":" + std::string(json_bool(job->server_texture_sync_request_full_available));
            metadata += ",\"server_texture_sync_server_request_available\":" + std::string(json_bool(job->server_texture_sync_server_request_available));
            metadata += ",\"server_texture_sync_request_full_called\":" + std::string(json_bool(job->server_texture_sync_request_full_called));
            metadata += ",\"server_texture_sync_server_request_called\":" + std::string(json_bool(job->server_texture_sync_server_request_called));
            metadata += ",\"server_texture_sync_polls\":" + std::to_string(job->server_texture_sync_polls);
            metadata += ",\"server_texture_sync_poll_ms\":" + std::to_string(job->server_texture_sync_poll_ms);
            metadata += ",\"server_texture_sync_max_polls\":" + std::to_string(job->server_texture_sync_max_polls);
            metadata += ",\"server_texture_sync_elapsed_ms\":" + std::to_string(job->server_texture_sync_elapsed_ms);
            metadata += ",\"server_texture_sync_albedo_changed\":" + std::string(json_bool(job->server_texture_sync_albedo_changed));
            metadata += ",\"server_texture_sync_timed_out\":" + std::string(json_bool(job->server_texture_sync_timed_out));
            metadata += ",\"server_texture_sync_ok\":" + std::string(json_bool(server_texture_sync_ok));
            metadata += ",\"server_texture_sync_failure\":\"" + json_escape(job->server_texture_sync_failure) + "\"";
            metadata += ",\"texture_sync_observer_wait_started\":" + std::string(json_bool(job->texture_sync_observer_wait_started));
            metadata += ",\"texture_sync_observer_wait_polls\":" + std::to_string(job->texture_sync_observer_wait_polls);
            metadata += ",\"texture_sync_observer_wait_poll_ms\":" + std::to_string(job->texture_sync_observer_wait_poll_ms);
            metadata += ",\"texture_sync_observer_wait_max_polls\":" + std::to_string(job->texture_sync_observer_wait_max_polls);
            metadata += ",\"texture_sync_observer_wait_observed\":" + std::string(json_bool(job->texture_sync_observer_wait_observed));
            metadata += ",\"texture_sync_observer_wait_elapsed_ms\":" + std::to_string(job->texture_sync_observer_wait_elapsed_ms);
            metadata += ",\"server_texture_sync_after_import_started\":" +
                        std::string(json_bool(job->server_texture_sync_after_import_started));
            metadata += ",\"server_texture_sync_after_import_route\":\"" +
                        json_escape(job->server_texture_sync_after_import_route) + "\"";
            metadata += ",\"server_texture_sync_after_import_relay_component\":\"" +
                        hex_address(job->relay_component) + "\"";
            metadata += sdk_call_detail_metadata("server_texture_sync_after_import_server_relay",
                                                 job->server_texture_sync_after_import_server_relay);
            metadata += sdk_call_detail_metadata("server_texture_sync_after_import_relay",
                                                 job->server_texture_sync_after_import_relay);
            metadata += sdk_call_detail_metadata("server_texture_sync_after_import_request_full",
                                                 job->server_texture_sync_after_import_request_full);
            metadata += sdk_call_detail_metadata("server_texture_sync_after_import_server_request",
                                                 job->server_texture_sync_after_import_server_request);
            metadata += texture_sync_observer_metadata("texture_sync_observer_after", texture_sync_observer_snapshot());
            metadata += ",\"total_replay_elapsed_ms\":" + std::to_string(total_elapsed_ms);
            metadata += ",\"paint_elapsed_ms\":" + std::to_string(total_elapsed_ms);
            metadata += ",\"paint_eta_ms\":0";
            metadata += ",\"first_failure\":\"" + json_escape(job->first_failure) + "\"";
            Reflection ref{};
            std::string init_failure{};
            if (job->albedo_before.ok && ref.init(init_failure))
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
                metadata += ",\"albedo_export_after_failure\":\"" +
                            json_escape(job->albedo_before.ok ? "reflection_unavailable:" + init_failure : "skipped_no_before_export") + "\"";
                metadata += ",\"albedo_export_changed\":false";
            }
            metadata += mesh_first_replication_snapshot_metadata("mesh_rep_before", job->replication_before);
            write_mesh_progress("mesh_paint_done",
                                "mesh-first paint completed",
                                static_cast<int>(job->strokes.size()),
                                static_cast<int>(job->strokes.size()),
                                MeshFirstBatchPhase::Done,
                                true,
                                "done",
                                "\"front_strokes\":" + std::to_string(job->replay_front) +
                                    ",\"side_strokes\":" + std::to_string(job->replay_side) +
                                    ",\"back_strokes\":" + std::to_string(job->replay_back));
            complete_mesh_first_batch_job(job,
                                          response_json(paint_ok,
                                                        paint_ok ? "mesh_first_paint_done" : "mesh_local_visual_sync_failed",
                                                        job->server_strokes_sent,
                                                        paint_ok ? 0 : 1,
                                                        paint_ok
                                                            ? "Paint completed."
                                                            : job->server_batch_rpc + " succeeded but local visual sync did not complete",
                                                        metadata));
        };

        auto active_context_still_matches = [&]() -> bool {
            if (!live_uobject(job->component))
            {
                finish_failed("mesh_paint_context_changed",
                              "Paint stopped because the game paint component is no longer available",
                              "paint_component_unavailable");
                return false;
            }
            if (!live_uobject(job->controller) || !job->k2_get_pawn_function)
            {
                // Freecam and spectator-like states can detach the local pawn while
                // the paint component and packed RPC target are still valid.
                return true;
            }

            sdk::Controller_K2_GetPawn pawn_params{};
            std::string process_failure{};
            if (!process_event(job->controller, job->k2_get_pawn_function, reinterpret_cast<std::uint8_t*>(&pawn_params), process_failure))
            {
                return true;
            }
            const auto current_pawn = reinterpret_cast<std::uintptr_t>(pawn_params.ReturnValue);
            if (!live_uobject(current_pawn))
            {
                return true;
            }
            return true;
        };

        if (job->strokes.empty())
        {
            finish_failed("mesh_replay_empty",
                          "mesh-first async replay has no strokes",
                          "mesh_replay_empty");
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (job->cancel_requested.load())
        {
            finish_cancelled();
            return;
        }

        if (job->next_dispatch_time.time_since_epoch().count() != 0 &&
            now < job->next_dispatch_time)
        {
            const int remaining_ms = std::max(
                1,
                static_cast<int>(std::ceil(std::chrono::duration<double, std::milli>(
                    job->next_dispatch_time - now).count())));
            post_next_after(remaining_ms);
            return;
        }
        job->next_dispatch_time = {};

        if (!active_context_still_matches())
        {
            return;
        }

        if (job->phase == MeshFirstBatchPhase::Planning)
        {
            job->phase = MeshFirstBatchPhase::ServerBatch;
        }

        auto begin_server_texture_sync = [&]() {
            job->phase = MeshFirstBatchPhase::ServerTextureSync;
            job->next_dispatch_time = {};
            post_next_after(0);
        };

        auto begin_texture_sync_observe = [&]() {
            job->phase = MeshFirstBatchPhase::TextureSyncObserve;
            job->texture_sync_observer_wait_started = true;
            job->texture_sync_observer_wait_started_at = std::chrono::steady_clock::now();
            job->next_dispatch_time = job->texture_sync_observer_wait_started_at +
                                      std::chrono::milliseconds(std::max(1, job->texture_sync_observer_wait_poll_ms));
            write_mesh_progress("mesh_texture_sync_observe",
                                "Waiting for texture sync multicast observer",
                                job->server_strokes_sent,
                                static_cast<int>(job->strokes.size()),
                                MeshFirstBatchPhase::TextureSyncObserve,
                                false,
                                "running");
            post_next_after(job->texture_sync_observer_wait_poll_ms);
        };

        auto begin_local_texture_import = [&]() {
            job->phase = MeshFirstBatchPhase::LocalTextureImport;
            job->next_dispatch_time = {};
            post_next_after(0);
        };

        if (job->phase == MeshFirstBatchPhase::ServerBatch && job->offset >= job->strokes.size())
        {
            if (job->server_batch_elapsed_ms < 0.0)
            {
                job->server_batch_elapsed_ms = elapsed_ms();
            }
            finish_done();
            return;
        }

        if (job->phase == MeshFirstBatchPhase::LocalTextureImport)
        {
            if (job->local_texture_import_started)
            {
                finish_done();
                return;
            }
            job->local_texture_import_started = true;
            write_mesh_progress("mesh_local_texture_import",
                                "Importing local material preview texture",
                                job->server_strokes_sent,
                                static_cast<int>(job->strokes.size()),
                                MeshFirstBatchPhase::LocalTextureImport,
                                false,
                                "running");
            Reflection ref{};
            std::string init_failure{};
            if (!ref.init(init_failure))
            {
                job->local_texture_import_failure = "reflection_unavailable:" + init_failure;
                begin_server_texture_sync();
                return;
            }
            const auto result = mesh_first_apply_local_material_import_preview(ref,
                                                                               job->component,
                                                                               job->strokes,
                                                                               job->texture_size,
                                                                               &job->albedo_before_bytes);
            job->albedo_before_bytes.clear();
            job->albedo_before_bytes.shrink_to_fit();
            job->local_texture_import_ok = result.ok;
            job->local_texture_import_export_ok = result.export_ok;
            job->local_texture_import_import_ok = result.import_ok;
            job->local_texture_import_texture_size = result.texture_size;
            job->local_texture_import_source_bytes = result.source_bytes;
            job->local_texture_import_strokes_considered = result.strokes_considered;
            job->local_texture_import_strokes_painted = result.strokes_painted;
            job->local_texture_import_pixels_touched = result.pixels_touched;
            job->local_texture_import_pixels_changed = result.pixels_changed;
            job->local_texture_import_before_hash = result.before_hash;
            job->local_texture_import_preview_hash = result.preview_hash;
            job->local_texture_import_elapsed_ms = result.elapsed_ms;
            job->local_texture_import_failure = result.failure;
            if (result.ok)
            {
                mesh_first_request_texture_sync_after_import(ref, job);
                if (job->server_texture_sync_after_import_route != "unavailable" &&
                    job->server_texture_sync_after_import_route != "disabled")
                {
                    begin_texture_sync_observe();
                    return;
                }
                // Post-import probe may be disabled, but peers still need the
                // normal server texture sync request after the local preview.
                begin_server_texture_sync();
                return;
            }
            begin_server_texture_sync();
            return;
        }

        if (job->phase == MeshFirstBatchPhase::TextureSyncObserve)
        {
            if (!job->texture_sync_observer_wait_started)
            {
                begin_texture_sync_observe();
                return;
            }

            const auto snapshot = texture_sync_observer_snapshot();
            if (texture_sync_observer_has_activity(snapshot))
            {
                job->texture_sync_observer_wait_observed = true;
                job->texture_sync_observer_wait_elapsed_ms =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - job->texture_sync_observer_wait_started_at)
                        .count();
                finish_done();
                return;
            }

            ++job->texture_sync_observer_wait_polls;
            if (job->texture_sync_observer_wait_polls >= std::max(1, job->texture_sync_observer_wait_max_polls))
            {
                job->texture_sync_observer_wait_elapsed_ms =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - job->texture_sync_observer_wait_started_at)
                        .count();
                finish_done();
                return;
            }

            write_mesh_progress("mesh_texture_sync_observe",
                                "Waiting for texture sync multicast observer",
                                job->server_strokes_sent,
                                static_cast<int>(job->strokes.size()),
                                MeshFirstBatchPhase::TextureSyncObserve,
                                false,
                                "running");
            job->next_dispatch_time = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(std::max(1, job->texture_sync_observer_wait_poll_ms));
            post_next_after(job->texture_sync_observer_wait_poll_ms);
            return;
        }

        if (job->phase == MeshFirstBatchPhase::ServerTextureSync)
        {
            if (!job->server_texture_sync_started)
            {
                job->server_texture_sync_started = true;
                job->server_texture_sync_started_at = std::chrono::steady_clock::now();
                Reflection ref{};
                std::string init_failure{};
                if (!ref.init(init_failure))
                {
                    job->server_texture_sync_failure = "reflection_unavailable:" + init_failure;
                    finish_done();
                    return;
                }
                job->server_texture_sync_request_full_available =
                    ref.find_function(job->component, "RequestFullTextureSync") != 0;
                job->server_texture_sync_server_request_available =
                    ref.find_function(job->component, "ServerRequestTextureSync") != 0;
                if (job->server_texture_sync_request_full_available)
                {
                    job->server_texture_sync_request_full_called =
                        sdk_call_no_params(ref, job->component, "RequestFullTextureSync");
                }
                if (!job->server_texture_sync_request_full_called &&
                    job->server_texture_sync_server_request_available)
                {
                    job->server_texture_sync_server_request_called =
                        sdk_call_no_params(ref, job->component, "ServerRequestTextureSync");
                }
                if (!job->server_texture_sync_request_full_called &&
                    !job->server_texture_sync_server_request_called)
                {
                    job->server_texture_sync_failure = "server_texture_sync_request_unavailable_or_failed";
                    finish_done();
                    return;
                }
                write_mesh_progress("mesh_server_texture_sync",
                                    "Requesting server texture sync",
                                    job->server_strokes_sent,
                                    static_cast<int>(job->strokes.size()),
                                    MeshFirstBatchPhase::ServerTextureSync,
                                    false,
                                    "running");
                job->next_dispatch_time = std::chrono::steady_clock::now() +
                                          std::chrono::milliseconds(std::max(1, job->server_texture_sync_poll_ms));
                post_next_after(job->server_texture_sync_poll_ms);
                return;
            }

            ++job->server_texture_sync_polls;
            Reflection ref{};
            std::string init_failure{};
            if (!ref.init(init_failure))
            {
                job->server_texture_sync_failure = "reflection_unavailable:" + init_failure;
                finish_done();
                return;
            }
            const auto albedo_now = mesh_first_export_channel_checksum(ref, job->component, sdk::EPaintChannel::Albedo);
            if (job->albedo_before.ok && albedo_now.ok && job->albedo_before.hash != albedo_now.hash)
            {
                job->server_texture_sync_albedo_changed = true;
                job->server_texture_sync_elapsed_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->server_texture_sync_started_at).count();
                finish_done();
                return;
            }
            if (job->server_texture_sync_polls >= std::max(1, job->server_texture_sync_max_polls))
            {
                job->server_texture_sync_timed_out = true;
                job->server_texture_sync_elapsed_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->server_texture_sync_started_at).count();
                job->server_texture_sync_failure = albedo_now.ok ? "texture_sync_no_local_channel_change"
                                                                  : "texture_sync_export_failed:" + albedo_now.failure;
                finish_done();
                return;
            }

            write_mesh_progress("mesh_server_texture_sync",
                                "Waiting for server texture sync",
                                job->server_strokes_sent,
                                static_cast<int>(job->strokes.size()),
                                MeshFirstBatchPhase::ServerTextureSync,
                                false,
                                "running",
                                "\"server_texture_sync_last_export_ok\":" + std::string(json_bool(albedo_now.ok)) +
                                    ",\"server_texture_sync_last_export_failure\":\"" + json_escape(albedo_now.failure) + "\"");
            job->next_dispatch_time = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(std::max(1, job->server_texture_sync_poll_ms));
            post_next_after(job->server_texture_sync_poll_ms);
            return;
        }

        if (job->next_dispatch_time.time_since_epoch().count() != 0)
        {
            job->replication_pacing_last_timer_drift_ms =
                std::max(0.0, std::chrono::duration<double, std::milli>(now - job->next_dispatch_time).count());
        }

        auto capture_replication_pacing_pressure = [&]() -> MeshFirstReplicationSnapshot {
            return mesh_first_capture_cached_replication_snapshot(job);
        };

        job->replication_pacing_pre_pressure = capture_replication_pacing_pressure();
        mesh_first_update_replication_pacing_model(job, job->replication_pacing_pre_pressure);

        if (job->replication_pacing_enabled)
        {
            mesh_first_update_replication_pacing_resolved_batch(job, job->replication_pacing_pre_pressure);
            mesh_first_set_replication_pacing_effective(job,
                                              mesh_first_replication_pacing_resolved_batch(job),
                                              mesh_first_replication_pacing_requested_delay(job));
            auto pre_level = mesh_first_replication_pacing_queue_gate_pressure_level(job, job->replication_pacing_pre_pressure);
            job->replication_pacing_pressure_level = mesh_first_replication_pacing_pressure_level_name(pre_level);
            if (!mesh_first_replication_pacing_queue_gate_open(job, job->replication_pacing_pre_pressure))
            {
                ++job->replication_pacing_backoff_count;
                write_mesh_progress("mesh_server_batch_throttle",
                                    "Waiting for paint replication queue to drain",
                                    job->server_strokes_sent,
                                    static_cast<int>(job->strokes.size()),
                                    MeshFirstBatchPhase::ServerBatch,
                                    false,
                                    "running");
                job->next_dispatch_time = std::chrono::steady_clock::now() +
                                          std::chrono::milliseconds(std::max(1, job->server_batch_delay_ms));
                post_next_after(job->server_batch_delay_ms);
                return;
            }
        }

        const std::size_t chunk_offset = job->offset;
        const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(std::max(1, job->server_batch_limit)),
                                                        job->strokes.size() - chunk_offset);
        SdkContext ctx{};
        ctx.component = job->component;
        ctx.relay_component = job->relay_component;
        ctx.server_packed_paint_batch_function = job->server_packed_paint_batch_function;
        ctx.server_relay_packed_stroke_batch_function = job->server_relay_packed_stroke_batch_function;
        std::string batch_failure{};
        ++job->server_batch_calls;
        const auto rpc_started = std::chrono::steady_clock::now();
        const bool batch_ok = sdk_call_packed_paint_batch_from_strokes(ctx.component,
                                                                       ctx.relay_component,
                                                                       ctx.server_packed_paint_batch_function,
                                                                       ctx.server_relay_packed_stroke_batch_function,
                                                                       job->server_packed_paint_batch_use_relay,
                                                                       job->strokes,
                                                                       chunk_offset,
                                                                       count,
                                                                       job->server_packed_paint_source_id,
                                                                       job->texture_size,
                                                                       batch_failure);
        job->replication_pacing_last_rpc_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rpc_started).count();
        if (!batch_ok)
        {
            ++job->server_batch_failures;
            job->first_failure = batch_failure.empty() ? job->server_batch_rpc + "_failed" : batch_failure;
            job->server_batch_elapsed_ms = elapsed_ms();
            finish_failed("mesh_server_batch_failed",
                          job->server_batch_rpc + " failed: " + job->first_failure,
                          job->first_failure);
            return;
        }

        ++job->server_batch_success;
        job->server_strokes_sent += static_cast<int>(count);
        job->offset += count;
        job->server_batch_elapsed_ms = elapsed_ms();

        job->replication_pacing_post_pressure = capture_replication_pacing_pressure();
        mesh_first_update_replication_pacing_model(job, job->replication_pacing_post_pressure);

        if (job->replication_pacing_enabled)
        {
            mesh_first_update_replication_pacing_resolved_batch(job, job->replication_pacing_post_pressure);
            mesh_first_set_replication_pacing_effective(job,
                                              mesh_first_replication_pacing_resolved_batch(job),
                                              mesh_first_replication_pacing_requested_delay(job));
            job->replication_pacing_pressure_level =
                mesh_first_replication_pacing_pressure_level_name(mesh_first_replication_pacing_queue_gate_pressure_level(job, job->replication_pacing_post_pressure));
        }

        if (job->local_visual_sync_enabled)
        {
            if (!job->local_paint_at_uv_function)
            {
                job->local_visual_sync_failure = "PaintAtUVWithBrush_unavailable";
                finish_failed("mesh_local_visual_sync_failed",
                              "PaintAtUVWithBrush is unavailable",
                              job->local_visual_sync_failure);
                return;
            }
            if (!job->local_sync_started)
            {
                job->local_sync_started = true;
                job->local_sync_started_at = std::chrono::steady_clock::now();
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                std::string local_failure{};
                ++job->local_stroke_calls;
                if (!sdk_call_paint_at_uv_with_brush(job->component,
                                                     job->local_paint_at_uv_function,
                                                     job->strokes[chunk_offset + index],
                                                     local_failure))
                {
                    ++job->local_stroke_failures;
                    job->local_visual_sync_failure = local_failure.empty() ? "PaintAtUVWithBrush_failed" : local_failure;
                    finish_failed("mesh_local_visual_sync_failed",
                                  job->server_batch_rpc + " succeeded but local visual sync failed: " + job->local_visual_sync_failure,
                                  job->local_visual_sync_failure);
                    return;
                }
                ++job->local_stroke_success;
                ++job->local_offset;
            }
            ++job->local_batch_calls;
            job->local_visual_sync_elapsed_ms = mesh_first_local_elapsed_ms(job);
        }

        write_mesh_progress("mesh_server_batch",
                            "mesh-first " + job->server_batch_rpc + " stream",
                            job->server_strokes_sent,
                            static_cast<int>(job->strokes.size()),
                            MeshFirstBatchPhase::ServerBatch,
                            false,
                            "running");
        if (job->offset < job->strokes.size())
        {
            job->next_dispatch_time = std::chrono::steady_clock::now() +
                                      std::chrono::milliseconds(std::max(0, job->server_batch_delay_ms));
            post_next_after(job->server_batch_delay_ms);
            return;
        }
        if (job->local_visual_sync_enabled)
        {
            finish_done();
            return;
        }
        finish_done();
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

    // =============================================================================
    // Section: Paint replication pressure and packed paint RPC route
    // Risk: very high. ServerPackedPaintBatch is the normal multiplayer path.
    // Do not change payload layout or RPC parameter structs during cleanup.
    // =============================================================================

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

    auto paint_replication_global_probe_metadata(Reflection& ref) -> std::string
    {
        std::string metadata{};
        const auto replication_manager = ref.find_first_instance("RuntimePaintReplicationManager");
        const auto paint_component = ref.find_first_instance("RuntimePaintableComponent");
        metadata += ",\"global_paint_replication_manager\":\"" + hex_address(replication_manager) + "\"";
        metadata += ",\"global_paint_replication_manager_class\":\"" + json_escape(ref.class_name(replication_manager)) + "\"";
        metadata += ",\"global_runtime_paintable_component\":\"" + hex_address(paint_component) + "\"";
        metadata += ",\"global_runtime_paintable_component_class\":\"" + json_escape(ref.class_name(paint_component)) + "\"";
        metadata += sdk_replication_snapshot_metadata("global_replication", sdk_capture_replication_snapshot(ref, paint_component));

        const std::vector<const char*> component_paint_replication_candidates{
            "ServerPackedPaintBatch",
            "MulticastPackedPaintBatch",
            "MulticastPackedPaintBatchToOthers",
        };
        const std::vector<const char*> manager_replication_candidates{
            "GetQueuedStrokeCount",
            "GetQueuedStrokeCountForComponent",
            "GetReplicationPressure",
            "Flush",
            "FlushReplicationQueue",
            "ProcessReplicationQueue",
            "TickReplication",
        };
        const std::vector<const char*> paint_replication_property_candidates{
            "bUseCompactPaintReplication",
            "bUseExperimentalPackedPaintReplication",
            "MaxOutgoingStrokesPerBatch",
            "MaxOutgoingNetworkBatchesPerSecond",
            "bCoalesceOutgoingStrokes",
            "MaxReplicatedPaintStrokesPerTick",
            "MaxReplicatedPaintRenderTargetWritesPerFrame",
            "MinRemotePaintFramesAfterLocalPaint",
            "MaxAdaptiveRemotePaintFrameInterval",
            "bEnableAdaptiveRemotePaintInterval",
            "AdaptiveTargetFPS",
            "AdaptiveFpsDropRatio",
            "MaxStrokesPerTick",
            "ReplicationInterval",
            "ReplicationTickInterval",
            "BatchFlushInterval",
            "QueuedStrokeCount",
            "QueuedBatchCount",
        };
        metadata += paint_replication_function_probe_metadata(ref,
                                                              paint_component,
                                                              "global_component_probe",
                                                              component_paint_replication_candidates);
        metadata += paint_replication_function_probe_metadata(ref,
                                                              replication_manager,
                                                              "global_manager_probe",
                                                              manager_replication_candidates);
        metadata += paint_replication_property_probe_metadata(ref,
                                                              paint_component,
                                                              "global_component_property_probe",
                                                              paint_replication_property_candidates);
        metadata += paint_replication_property_probe_metadata(ref,
                                                              replication_manager,
                                                              "global_manager_property_probe",
                                                              paint_replication_property_candidates);
        return metadata;
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

    auto sdk_unit_to_byte(double value) -> std::uint8_t
    {
        return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(clamp01(value) * 255.0)), 0, 255));
    }

    auto sdk_unit_to_u16(double value) -> std::uint16_t
    {
        return static_cast<std::uint16_t>(std::clamp<int>(static_cast<int>(std::lround(clamp01(value) * 65535.0)), 0, 65535));
    }

    auto sdk_append_u16_le(std::vector<std::uint8_t>& bytes, std::uint16_t value) -> void
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }

    auto sdk_append_i32_le(std::vector<std::uint8_t>& bytes, std::int32_t value) -> void
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(value));
    }

    auto sdk_append_f32_le(std::vector<std::uint8_t>& bytes, float value) -> void
    {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(value));
    }

    auto sdk_guid_is_zero(const sdk::FGuid& id) -> bool
    {
        return id.A == 0 && id.B == 0 && id.C == 0 && id.D == 0;
    }

    auto sdk_read_component_packed_source_id(std::uintptr_t component,
                                             sdk::FGuid& id,
                                             std::string& failure) -> bool
    {
        id = {};
        if (!live_uobject(component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        if (!safe_copy(&id,
                       reinterpret_cast<const void*>(component + RuntimePaintableComponentPackedSourceIdOffset),
                       sizeof(id)))
        {
            failure = "source_id_read_failed";
            return false;
        }
        if (sdk_guid_is_zero(id))
        {
            failure = "source_id_zero";
            return false;
        }
        failure = "ok";
        return true;
    }

    auto sdk_make_packed_paint_data(const std::vector<sdk::FPaintStroke>& strokes,
                                    std::size_t offset,
                                    std::size_t count,
                                    const sdk::FGuid& source_id,
                                    int texture_size,
                                    std::vector<std::uint8_t>& packed,
                                    std::string& failure) -> bool
    {
        if (offset > strokes.size() || count > strokes.size() - offset || count <= 0)
        {
            failure = "packed_paint_range_invalid";
            return false;
        }
        if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        {
            failure = "packed_paint_count_too_large";
            return false;
        }
        const int effective_texture_size = std::max(1, texture_size);
        packed.clear();
        packed.reserve(21 + count * 27);
        packed.push_back(1);
        const auto* source_bytes = reinterpret_cast<const std::uint8_t*>(&source_id);
        packed.insert(packed.end(), source_bytes, source_bytes + sizeof(source_id));
        sdk_append_i32_le(packed, static_cast<std::int32_t>(count));

        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& stroke = strokes[offset + i];
            if (!stroke.bHasSkeletalTriangleAnchor || stroke.SkeletalTriangleIndex < 0)
            {
                failure = "packed_paint_requires_skeletal_triangle_anchor index=" + std::to_string(offset + i);
                packed.clear();
                return false;
            }
            sdk_append_i32_le(packed, stroke.SkeletalTriangleIndex);
            sdk_append_u16_le(packed, sdk_unit_to_u16(stroke.SkeletalTriangleBarycentric.X));
            sdk_append_u16_le(packed, sdk_unit_to_u16(stroke.SkeletalTriangleBarycentric.Y));
            sdk_append_f32_le(packed, stroke.BrushSettings.Radius);
            packed.push_back(sdk_unit_to_byte(stroke.ChannelData.AlbedoColor.R));
            packed.push_back(sdk_unit_to_byte(stroke.ChannelData.AlbedoColor.G));
            packed.push_back(sdk_unit_to_byte(stroke.ChannelData.AlbedoColor.B));
            packed.push_back(sdk_unit_to_byte(stroke.ChannelData.AlbedoColor.A));
            packed.push_back(sdk_unit_to_byte(stroke.ChannelData.Metallic));
            packed.push_back(sdk_unit_to_byte(stroke.ChannelData.Roughness));
            packed.push_back(static_cast<std::uint8_t>(static_cast<std::uint8_t>(stroke.TargetChannel) + 1));
            sdk_append_f32_le(packed, stroke.EffectiveBrushWorldRadius);
            const int diameter_texels = std::clamp(static_cast<int>(std::lround(
                                                       static_cast<double>(stroke.BrushSettings.Radius) *
                                                       static_cast<double>(effective_texture_size) * 2.0)),
                                                   0,
                                                   255);
            const int subdivision_pixel_size = std::clamp(static_cast<int>(std::lround(stroke.EffectiveSubdivisionPixelSize)),
                                                          0,
                                                          255);
            const int subdivision_level = std::clamp(stroke.EffectiveSubdivisionLevel, 0, 255);
            packed.push_back(static_cast<std::uint8_t>(diameter_texels));
            packed.push_back(static_cast<std::uint8_t>(subdivision_pixel_size));
            packed.push_back(static_cast<std::uint8_t>(subdivision_level));
            packed.push_back(static_cast<std::uint8_t>(stroke.TargetChannel));
        }
        return true;
    }

    auto sdk_strokes_are_packed_compatible(const std::vector<sdk::FPaintStroke>& strokes) -> bool
    {
        if (strokes.empty())
        {
            return false;
        }
        return std::all_of(strokes.begin(), strokes.end(), [](const sdk::FPaintStroke& stroke) {
            return stroke.bHasSkeletalTriangleAnchor && stroke.SkeletalTriangleIndex >= 0;
        });
    }

    struct RuntimePaintableComponent_ServerPackedPaintBatchParams
    {
        sdk::TArray<std::uint8_t> PackedData{};
        std::int32_t StrokeCount{0};
        std::uint8_t Pad_14[0x4]{};
    };

    struct RuntimePaintRelayComponent_ServerRelayPackedStrokeBatchParams
    {
        void* PaintComponent{nullptr};
        sdk::TArray<std::uint8_t> PackedData{};
        std::int32_t StrokeCount{0};
        std::uint8_t Pad_1C[0x4]{};
    };

    static_assert(sizeof(RuntimePaintableComponent_ServerPackedPaintBatchParams) == 0x18,
                  "ServerPackedPaintBatch params layout mismatch");
    static_assert(sizeof(RuntimePaintRelayComponent_ServerRelayPackedStrokeBatchParams) == 0x20,
                  "ServerRelayPackedStrokeBatch params layout mismatch");

    auto sdk_call_server_packed_paint_batch(std::uintptr_t component,
                                            std::uintptr_t function,
                                            const std::vector<std::uint8_t>& packed_data,
                                            int stroke_count,
                                            std::string& failure) -> bool
    {
        if (!function || packed_data.empty() || stroke_count <= 0)
        {
            failure = "server_packed_paint_batch_unavailable_or_empty";
            return false;
        }
        if (!live_uobject(component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        RuntimePaintableComponent_ServerPackedPaintBatchParams params{};
        params.PackedData.Data = const_cast<std::uint8_t*>(packed_data.data());
        params.PackedData.Num = static_cast<std::int32_t>(packed_data.size());
        params.PackedData.Max = static_cast<std::int32_t>(packed_data.size());
        params.StrokeCount = stroke_count;
        return process_event(component, function, reinterpret_cast<std::uint8_t*>(&params), failure);
    }

    auto sdk_call_server_relay_packed_stroke_batch(std::uintptr_t relay_component,
                                                   std::uintptr_t paint_component,
                                                   std::uintptr_t function,
                                                   const std::vector<std::uint8_t>& packed_data,
                                                   int stroke_count,
                                                   std::string& failure) -> bool
    {
        if (!function || packed_data.empty() || stroke_count <= 0)
        {
            failure = "server_relay_packed_stroke_batch_unavailable_or_empty";
            return false;
        }
        if (!live_uobject(relay_component))
        {
            failure = "relay_component_unavailable";
            return false;
        }
        if (!live_uobject(paint_component))
        {
            failure = "paint_component_unavailable";
            return false;
        }
        RuntimePaintRelayComponent_ServerRelayPackedStrokeBatchParams params{};
        params.PaintComponent = reinterpret_cast<void*>(paint_component);
        params.PackedData.Data = const_cast<std::uint8_t*>(packed_data.data());
        params.PackedData.Num = static_cast<std::int32_t>(packed_data.size());
        params.PackedData.Max = static_cast<std::int32_t>(packed_data.size());
        params.StrokeCount = stroke_count;
        return process_event(relay_component, function, reinterpret_cast<std::uint8_t*>(&params), failure);
    }

    auto sdk_call_packed_paint_batch_from_strokes(std::uintptr_t component,
                                                  std::uintptr_t relay_component,
                                                  std::uintptr_t component_function,
                                                  std::uintptr_t relay_function,
                                                  bool use_relay,
                                                  const std::vector<sdk::FPaintStroke>& strokes,
                                                  std::size_t offset,
                                                  std::size_t count,
                                                  const sdk::FGuid& source_id,
                                                  int texture_size,
                                                  std::string& failure) -> bool
    {
        std::vector<std::uint8_t> packed_data{};
        if (!sdk_make_packed_paint_data(strokes, offset, count, source_id, texture_size, packed_data, failure))
        {
            return false;
        }
        if (use_relay)
        {
            return sdk_call_server_relay_packed_stroke_batch(relay_component,
                                                             component,
                                                             relay_function,
                                                             packed_data,
                                                             static_cast<int>(count),
                                                             failure);
        }
        return sdk_call_server_packed_paint_batch(component,
                                                  component_function,
                                                  packed_data,
                                                  static_cast<int>(count),
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

    // =============================================================================
    // Section: Research and probe commands
    // Risk: medium/high. These helpers are not normal paint behavior, but they are
    // important for game-update recovery and multiplayer replication investigation.
    // =============================================================================

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

    auto is_paint_replication_probe_request(const std::string& request) -> bool
    {
        return request.find("\"type\":\"paint_replication_probe\"") != std::string::npos ||
               request.find("\"type\":\"paint_replication_pressure_probe\"") != std::string::npos;
    }

    auto paint_replication_probe_metadata_for_context(Reflection& ref, const SdkContext& ctx) -> std::string
    {
        std::string metadata = "\"route\":\"paint_replication_probe\"";
        metadata += ",";
        metadata += sdk_context_metadata(ref, ctx);

        const auto replication_manager = ref.find_first_instance("RuntimePaintReplicationManager");
        metadata += ",\"paint_replication_manager\":\"" + hex_address(replication_manager) + "\"";
        metadata += ",\"paint_replication_manager_class\":\"" + json_escape(ref.class_name(replication_manager)) + "\"";
        metadata += ",\"function_request_full_texture_sync_available\":" +
                    std::string(json_bool(ref.find_function(ctx.component, "RequestFullTextureSync") != 0));
        metadata += ",\"function_server_request_texture_sync_available\":" +
                    std::string(json_bool(ref.find_function(ctx.component, "ServerRequestTextureSync") != 0));
        metadata += ",\"function_multicast_sync_channel_data_available\":" +
                    std::string(json_bool(ref.find_function(ctx.component, "MulticastSyncChannelData") != 0));
        metadata += ",\"function_multicast_sync_compressed_channel_data_available\":" +
                    std::string(json_bool(ref.find_function(ctx.component, "MulticastSyncCompressedChannelData") != 0));
        metadata += ",\"function_server_relay_texture_sync_available\":" +
                    std::string(json_bool(live_uobject(ctx.relay_component) &&
                                          ref.find_function(ctx.relay_component, "ServerRelayTextureSync") != 0));
        metadata += ",\"function_relay_texture_sync_to_server_available\":" +
                    std::string(json_bool(live_uobject(ctx.relay_component) &&
                                          ref.find_function(ctx.relay_component, "RelayTextureSyncToServer") != 0));

        const std::vector<const char*> component_paint_replication_candidates{
            "ServerPackedPaintBatch",
            "MulticastPackedPaintBatch",
            "MulticastPackedPaintBatchToOthers",
        };
        const std::vector<const char*> relay_paint_replication_candidates{
            "ServerRelayCompactPaint",
            "ServerRelayCompactStrokeBatch",
            "ServerRelayPackedStrokeBatch",
        };
        const std::vector<const char*> paint_replication_property_candidates{
            "bUseCompactPaintReplication",
            "bUseExperimentalPackedPaintReplication",
            "MaxOutgoingStrokesPerBatch",
            "MaxOutgoingNetworkBatchesPerSecond",
            "bCoalesceOutgoingStrokes",
            "MaxReplicatedPaintStrokesPerTick",
            "MaxReplicatedPaintRenderTargetWritesPerFrame",
        };
        metadata += paint_replication_function_probe_metadata(ref,
                                                              ctx.component,
                                                              "paint_replication_component_probe",
                                                              component_paint_replication_candidates);
        metadata += paint_replication_function_probe_metadata(ref,
                                                              ctx.relay_component,
                                                              "paint_replication_relay_probe",
                                                              relay_paint_replication_candidates);
        metadata += paint_replication_property_probe_metadata(ref,
                                                              ctx.component,
                                                              "paint_replication_component_property_probe",
                                                              paint_replication_property_candidates);
        metadata += paint_replication_property_probe_metadata(ref,
                                                              replication_manager,
                                                              "paint_replication_manager_property_probe",
                                                              paint_replication_property_candidates);
        return metadata;
    }

    auto paint_replication_probe_on_game_thread(const std::string& request) -> std::string
    {
        Reflection ref{};
        std::string failure{};
        if (!ref.init(failure))
        {
            return response_json(false,
                                 "sdk_update_required",
                                 0,
                                 1,
                                 failure.empty() ? "SDK reflection init failed" : failure,
                                 "\"route\":\"paint_replication_probe\",\"sdk_resolution_exception\":true");
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
                                 "\"route\":\"paint_replication_probe\",\"sdk_resolution_exception\":true");
        }

        if (request.find("\"type\":\"paint_replication_pressure_probe\"") != std::string::npos)
        {
            std::string pressure_metadata = "\"route\":\"paint_replication_pressure_probe\"";
            pressure_metadata += paint_replication_global_probe_metadata(ref);
            if (!ctx.ok)
            {
                return response_json(false, ctx.stage.c_str(), 0, 1, ctx.message, pressure_metadata);
            }
            pressure_metadata += ",\"component\":\"" + hex_address(ctx.component) + "\"";
            pressure_metadata += ",\"component_class\":\"" + json_escape(ref.class_name(ctx.component)) + "\"";
            pressure_metadata += sdk_replication_snapshot_metadata("replication", sdk_capture_replication_snapshot(ref, ctx.component));
            return response_json(true,
                                 "paint_replication_pressure_probe",
                                 0,
                                 0,
                                 "paint replication pressure probe complete",
                                 pressure_metadata);
        }
        const auto metadata = paint_replication_probe_metadata_for_context(ref, ctx);
        if (!ctx.ok)
        {
            return response_json(false, ctx.stage.c_str(), 0, 1, ctx.message, metadata + paint_replication_global_probe_metadata(ref));
        }
        return response_json(true, "paint_replication_probe", 0, 0, "paint replication probe complete", metadata);
    }

    auto is_paint_packed_replay_probe_request(const std::string& request) -> bool
    {
        return request.find("\"type\":\"paint_packed_replay_probe\"") != std::string::npos;
    }

    auto paint_packed_replay_probe_on_game_thread(const std::string& request) -> std::string
    {
        Reflection ref{};
        std::string failure{};
        if (!ref.init(failure))
        {
            return response_json(false,
                                 "sdk_update_required",
                                 0,
                                 1,
                                 failure.empty() ? "SDK reflection init failed" : failure,
                                 "\"route\":\"paint_packed_replay_probe\",\"sdk_resolution_exception\":true");
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
                                 "\"route\":\"paint_packed_replay_probe\",\"sdk_resolution_exception\":true");
        }
        if (!ctx.ok)
        {
            return response_json(false, ctx.stage.c_str(), 0, 1, ctx.message, "\"route\":\"paint_packed_replay_probe\"");
        }

        std::vector<std::uint8_t> packed_data{};
        const std::string packed_hex = json_string_field(request, "packed_hex", "");
        if (!hex_to_bytes(packed_hex, packed_data, failure))
        {
            return response_json(false,
                                 failure.c_str(),
                                 0,
                                 1,
                                 "packed replay probe requires valid packed_hex",
                                 "\"route\":\"paint_packed_replay_probe\"");
        }
        const int stroke_count = json_int_field(request, "stroke_count", 0, 1, 10000);
        const std::string requested_route = lower_copy(json_string_field(request, "packed_route", "component"));
        const bool use_relay = requested_route == "relay" || json_bool_field(request, "use_relay", false);
        const auto function = use_relay
                                  ? ref.find_function(ctx.relay_component, "ServerRelayPackedStrokeBatch")
                                  : ref.find_function(ctx.component, "ServerPackedPaintBatch");

        const auto before = sdk_capture_replication_snapshot(ref, ctx.component);
        const auto started = std::chrono::steady_clock::now();
        const bool ok = use_relay
                            ? sdk_call_server_relay_packed_stroke_batch(ctx.relay_component,
                                                                        ctx.component,
                                                                        function,
                                                                        packed_data,
                                                                        stroke_count,
                                                                        failure)
                            : sdk_call_server_packed_paint_batch(ctx.component,
                                                                 function,
                                                                 packed_data,
                                                                 stroke_count,
                                                                 failure);
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        const auto after = sdk_capture_replication_snapshot(ref, ctx.component);
        std::string metadata = "\"route\":\"paint_packed_replay_probe\"";
        metadata += ",\"packed_route\":\"" + std::string(use_relay ? "relay" : "component") + "\"";
        metadata += ",\"packed_function\":\"" + hex_address(function) + "\"";
        metadata += ",\"packed_bytes\":" + std::to_string(packed_data.size());
        metadata += ",\"stroke_count\":" + std::to_string(stroke_count);
        metadata += ",\"packed_call_elapsed_ms\":" + std::to_string(elapsed_ms);
        metadata += ",\"component\":\"" + hex_address(ctx.component) + "\"";
        metadata += ",\"relay_component\":\"" + hex_address(ctx.relay_component) + "\"";
        metadata += sdk_replication_snapshot_metadata("packed_probe_before", before);
        metadata += sdk_replication_snapshot_metadata("packed_probe_after", after);
        if (!failure.empty())
        {
            metadata += ",\"failure\":\"" + json_escape(failure) + "\"";
        }
        return response_json(ok,
                             ok ? "paint_packed_replay_probe_done" : "paint_packed_replay_probe_failed",
                             ok ? stroke_count : 0,
                             ok ? 0 : 1,
                             ok ? "packed replay probe submitted" : "packed replay probe failed: " + failure,
                             metadata);
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
            if (is_paint_packed_replay_probe_request(job->request))
            {
                mark_queued_paint_job_dispatched(job);
                const auto response = paint_packed_replay_probe_on_game_thread(job->request);
                complete_queued_paint_job(job, response);
                continue;
            }
            if (is_paint_replication_probe_request(job->request))
            {
                mark_queued_paint_job_dispatched(job);
                const auto response = paint_replication_probe_on_game_thread(job->request);
                complete_queued_paint_job(job, response);
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
    }

    void __fastcall hooked_process_event(void* object, void* function, void* params)
    {
        g_active_hook_callbacks.fetch_add(1);
        const auto original = g_original_process_event.load();
        (void)object;
        if (function && params)
        {
            __try
            {
                const auto function_address = reinterpret_cast<std::uintptr_t>(function);
                const auto params_bytes = reinterpret_cast<std::uint8_t*>(params);
                auto_event_watch_record(function_address, params_bytes);
                if (function_address == g_observed_sync_channel_function.load())
                {
                    const auto channel = *reinterpret_cast<std::uint8_t*>(params_bytes);
                    const auto* array = reinterpret_cast<const sdk::TArray<std::uint8_t>*>(params_bytes + 0x8);
                    const int bytes = array ? std::max(0, array->Num) : 0;
                    g_observed_sync_channel_last_channel.store(static_cast<int>(channel));
                    g_observed_sync_channel_calls.fetch_add(1);
                    g_observed_sync_channel_bytes.fetch_add(bytes);
                }
                if (function_address == g_observed_sync_compressed_channel_function.load())
                {
                    const auto channel = *reinterpret_cast<std::uint8_t*>(params_bytes);
                    const auto* array = reinterpret_cast<const sdk::TArray<std::uint8_t>*>(params_bytes + 0x8);
                    const int compressed_bytes = array ? std::max(0, array->Num) : 0;
                    const int uncompressed_bytes = *reinterpret_cast<const int*>(params_bytes + 0x18);
                    g_observed_sync_compressed_channel_last_channel.store(static_cast<int>(channel));
                    g_observed_sync_compressed_channel_calls.fetch_add(1);
                    g_observed_sync_compressed_channel_bytes.fetch_add(compressed_bytes);
                    g_observed_sync_compressed_channel_uncompressed_bytes.fetch_add(std::max(0, uncompressed_bytes));
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }
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
        g_active_hook_callbacks.fetch_sub(1);
    }

    LRESULT CALLBACK message_hook_proc(int code, WPARAM wparam, LPARAM lparam)
    {
        g_active_hook_callbacks.fetch_add(1);
        if (code >= 0)
        {
            const auto* msg = reinterpret_cast<const MSG*>(lparam);
            if (msg && msg->message == PaintDispatchMessage)
            {
                __try
                {
                    drain_paint_jobs_on_game_thread();
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
        }
        const auto result = CallNextHookEx(g_message_hook.load(), code, wparam, lparam);
        g_active_hook_callbacks.fetch_sub(1);
        return result;
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
        post_paint_dispatch_message();
        std::unique_lock<std::mutex> lock(g_paint_jobs_mutex);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(240);
        const auto dispatch_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        auto finish_response = [&](std::string response) -> std::string {
            if (lock.owns_lock())
            {
                lock.unlock();
            }
            uninstall_message_hook();
            return response;
        };
        bool completed = job->done;
        while (!completed)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                break;
            }
            if (!job->dispatched && now >= dispatch_deadline)
            {
                break;
            }
            const auto next_wake = job->dispatched
                                       ? std::min(deadline, now + std::chrono::milliseconds(1000))
                                       : std::min({deadline, dispatch_deadline, now + std::chrono::milliseconds(1000)});
            completed = g_paint_jobs_cv.wait_until(lock,
                                                   next_wake,
                                                   [&]() { return job->done; });
            if (completed)
            {
                break;
            }
            lock.unlock();
            post_paint_dispatch_message();
            lock.lock();
        }
        if (!completed)
        {
            g_paint_jobs.erase(std::remove(g_paint_jobs.begin(), g_paint_jobs.end(), job), g_paint_jobs.end());
            if (!job->dispatched)
            {
                job->response = response_json(false,
                                              "game_thread_dispatch_timeout",
                                              0,
                                              1,
                                              "game thread did not start paint job",
                                              "\"queued_paint_removed\":true");
                job->done = true;
                return finish_response(job->response);
            }
            job->response = response_json(false, "game_thread_dispatch_timeout", 0, 1, "game thread did not process paint job");
            job->done = true;
            return finish_response(job->response);
        }
        return finish_response(job->response);
    }

    // =============================================================================
    // Section: Bridge IPC command dispatch and listener lifecycle
    // Risk: very high. C# and WebView2 reach native behavior by command strings.
    // =============================================================================

    auto handle_request(const std::string& line) -> std::string
    {
        if (line.find("\"type\":\"ping\"") != std::string::npos)
        {
            return response_json(true,
                                 "ping",
                                 0,
                                 0,
                                 "pong",
                                 "\"pid\":" + std::to_string(GetCurrentProcessId()) +
                                     ",\"port\":" + std::to_string(resolve_bridge_port()));
        }
        if (line.find("\"type\":\"capabilities\"") != std::string::npos)
        {
            std::string commands = "[\"ping\",\"capabilities\",\"paint_full_route\",\"paint_replication_probe\",\"paint_replication_pressure_probe\",\"paint_packed_replay_probe\",\"cancel_paint\",\"shutdown\"]";
            return std::string("{\"success\":true,\"stage\":\"capabilities\",\"applied\":0,\"failures\":0,") +
                   "\"message\":\"ok\",\"timing_ms\":{}," +
                   "\"metadata\":{\"commands\":" + commands + "," +
                   "\"cancel_paint\":true," +
                   "\"single_injection_per_pid\":true," +
                   "\"sdk\":\"runtime_dynamic_reflection_min\"," +
                   "\"paint_full_route\":\"mesh_first_paint\"," +
                   "\"texture_import_used\":false," +
                   "\"local_paint_used\":true," +
                   "\"paint_at_uv_with_brush_used\":true," +
                   "\"replication\":\"server_paint_batch\"," +
                   "\"multiplayer_replicated\":true}}\n";
        }
        if (line.find("\"type\":\"cancel_paint\"") != std::string::npos)
        {
            const int cancelled_active = cancel_active_mesh_first_batch_job("cancel_paint");
            const int cancelled_queued = cancel_queued_paint_jobs("cancel_paint");
            return response_json(true,
                                 "paint_cancel_requested",
                                 0,
                                 0,
                                 "paint cancel requested",
                                 "\"cancelled_active_paint_jobs\":" + std::to_string(cancelled_active) +
                                     ",\"cancelled_queued_paint_jobs\":" + std::to_string(cancelled_queued));
        }
        if (line.find("\"type\":\"shutdown\"") != std::string::npos)
        {
            const int cancelled_active = force_cancel_active_mesh_first_batch_job("shutdown");
            const int cancelled_queued = cancel_queued_paint_jobs("shutdown");
            uninstall_process_event_hook();
            g_running.store(false);
            return response_json(true,
                                 "shutdown",
                                 0,
                                 0,
                                 "bridge shutdown requested",
                                 "\"cancelled_active_paint_jobs\":" + std::to_string(cancelled_active) +
                                     ",\"cancelled_queued_paint_jobs\":" + std::to_string(cancelled_queued));
        }
        if (line.find("\"type\":\"paint_full_route\"") != std::string::npos)
        {
            return paint_full_route_native(line);
        }
        if (line.find("\"type\":\"paint_replication_probe\"") != std::string::npos ||
            line.find("\"type\":\"paint_replication_pressure_probe\"") != std::string::npos)
        {
            return paint_full_route_native(line);
        }
        if (line.find("\"type\":\"paint_packed_replay_probe\"") != std::string::npos)
        {
            return paint_full_route_native(line);
        }
        return response_json(false, "unknown_command", 0, 1, "unknown bridge command");
    }

    auto handle_bridge_client(SOCKET client) -> void
    {
        struct ClientGuard
        {
            SOCKET socket{INVALID_SOCKET};
            ~ClientGuard()
            {
                if (socket != INVALID_SOCKET)
                {
                    closesocket(socket);
                }
                g_active_client_handlers.fetch_sub(1);
            }
        } guard{client};

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
        if (request.empty())
        {
            return;
        }

        const std::string response = request.size() >= MaxRequestBytes
                                         ? response_json(false, "request_too_large", 0, 1, "bridge request exceeded max size")
                                         : handle_request(request);
        send(client, response.c_str(), static_cast<int>(response.size()), 0);
    }

    auto bridge_thread() -> void
    {
        g_bridge_thread_done.store(false);
        g_bridge_state.store(MC_BRIDGE_STARTING);
        start_auto_event_watch_if_configured();

        const int bridge_port = resolve_bridge_port();
        write_bridge_listener_status("starting", bridge_port);

        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            const DWORD error = WSAGetLastError();
            g_bridge_last_win32.store(error);
            g_bridge_state.store(MC_BRIDGE_FAILED);
            write_bridge_listener_status("wsa_startup_failed", bridge_port, error);
            g_running.store(false);
            g_bridge_thread_done.store(true);
            return;
        }
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            const DWORD error = WSAGetLastError();
            g_bridge_last_win32.store(error);
            g_bridge_state.store(MC_BRIDGE_FAILED);
            write_bridge_listener_status("socket_failed", bridge_port, error);
            WSACleanup();
            g_running.store(false);
            g_bridge_thread_done.store(true);
            return;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(bridge_port));
        const int yes = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            const DWORD error = WSAGetLastError();
            g_bridge_last_win32.store(error);
            g_bridge_state.store(MC_BRIDGE_FAILED);
            write_bridge_listener_status("bind_failed", bridge_port, error);
            closesocket(listener);
            WSACleanup();
            g_running.store(false);
            g_bridge_thread_done.store(true);
            return;
        }
        if (listen(listener, 4) == SOCKET_ERROR)
        {
            const DWORD error = WSAGetLastError();
            g_bridge_last_win32.store(error);
            g_bridge_state.store(MC_BRIDGE_FAILED);
            write_bridge_listener_status("listen_failed", bridge_port, error);
            closesocket(listener);
            WSACleanup();
            g_running.store(false);
            g_bridge_thread_done.store(true);
            return;
        }
        g_bridge_last_win32.store(0);
        g_bridge_state.store(MC_BRIDGE_RUNNING_LISTENING);
        write_bridge_listener_status("listening", bridge_port);
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
                const DWORD error = WSAGetLastError();
                g_bridge_last_win32.store(error);
                g_bridge_state.store(MC_BRIDGE_FAILED);
                write_bridge_listener_status("select_failed", bridge_port, error);
                break;
            }
            if (selected == 0)
            {
                continue;
            }
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                continue;
            }
            g_active_client_handlers.fetch_add(1);
            std::thread(handle_bridge_client, client).detach();
        }
        g_running.store(false);
        write_bridge_listener_status("stopping", bridge_port);
        g_bridge_state.store(MC_BRIDGE_STOPPING);
        closesocket(listener);
        while (g_active_client_handlers.load() > 0)
        {
            Sleep(50);
        }
        WSACleanup();
        write_bridge_listener_status("stopped", bridge_port);
        uninstall_process_event_hook();
        g_bridge_state.store(MC_BRIDGE_UNLOADABLE);
        g_bridge_thread_done.store(true);
    }
}

namespace
{
    std::mutex g_bridge_api_mutex;
    std::string g_bridge_build_id{"runtime-bridge"};

    auto copy_status_text(char* target, std::size_t target_size, const std::string& text) -> void
    {
        if (!target || target_size == 0)
        {
            return;
        }
        const auto count = std::min(target_size - 1, text.size());
        std::memcpy(target, text.data(), count);
        target[count] = '\0';
    }

    auto bridge_unload_blockers() -> std::uint32_t
    {
        std::uint32_t blockers = MC_BLOCK_NONE;
        if (g_running.load())
        {
            blockers |= MC_BLOCK_LISTENER;
        }
        if (g_active_client_handlers.load() > 0)
        {
            blockers |= MC_BLOCK_CLIENTS;
        }
        bool hook_slots_present = false;
        {
            std::lock_guard<std::mutex> lock(g_hook_mutex);
            hook_slots_present = !g_process_event_hook_slots.empty();
        }
        if (g_process_event_hook_installed.load() || g_message_hook.load() != nullptr || hook_slots_present)
        {
            blockers |= MC_BLOCK_HOOKS;
        }
        if (g_active_hook_callbacks.load() > 0)
        {
            blockers |= MC_BLOCK_HOOK_CALLBACKS;
        }
        if (g_active_ue_calls.load() > 0)
        {
            blockers |= MC_BLOCK_UE_CALLS;
        }
        bool paint_jobs_present = false;
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            paint_jobs_present = !g_paint_jobs.empty();
        }
        if (paint_jobs_present)
        {
            blockers |= MC_BLOCK_PAINT_QUEUE;
        }
        if (mesh_first_preview_snapshot_copy().available)
        {
            blockers |= MC_BLOCK_PREVIEW_STATE;
        }
        if (!g_bridge_thread_done.load())
        {
            blockers |= MC_BLOCK_WORKERS;
        }
        return blockers;
    }

    auto bridge_fill_status(McBridgeStatus* outStatus) -> McResult
    {
        if (!outStatus || outStatus->size < sizeof(McBridgeStatus))
        {
            return MC_E_INVALID_ARGUMENT;
        }
        const auto state = static_cast<McBridgeRunState>(g_bridge_state.load());
        const auto blockers = bridge_unload_blockers();
        std::memset(outStatus, 0, sizeof(McBridgeStatus));
        outStatus->size = sizeof(McBridgeStatus);
        outStatus->state = state;
        outStatus->lastResult = blockers == MC_BLOCK_NONE ? MC_OK : MC_E_UNLOAD_BLOCKED;
        outStatus->lastWin32 = g_bridge_last_win32.load();
        outStatus->unloadBlockers = blockers;
        outStatus->activeHookCallbacks = g_active_hook_callbacks.load();
        outStatus->activeUeCalls = g_active_ue_calls.load();
        outStatus->activeWorkers = g_bridge_thread_done.load() ? 0 : 1;
        outStatus->activeClients = static_cast<std::uint32_t>(std::max(0, g_active_client_handlers.load()));
        {
            std::lock_guard<std::mutex> lock(g_paint_jobs_mutex);
            outStatus->queuedPaintBatches = static_cast<std::uint32_t>(g_paint_jobs.size());
        }
        outStatus->tcpPort = static_cast<std::uint16_t>(resolve_bridge_port());
        copy_status_text(outStatus->bridgeBuildIdUtf8, sizeof(outStatus->bridgeBuildIdUtf8), g_bridge_build_id);
        switch (state)
        {
        case MC_BRIDGE_RUNNING_LISTENING:
            copy_status_text(outStatus->lastStepUtf8, sizeof(outStatus->lastStepUtf8), "listening");
            break;
        case MC_BRIDGE_UNLOADABLE:
            copy_status_text(outStatus->lastStepUtf8, sizeof(outStatus->lastStepUtf8), "unloadable");
            break;
        case MC_BRIDGE_FAILED:
            copy_status_text(outStatus->lastStepUtf8, sizeof(outStatus->lastStepUtf8), "failed");
            break;
        default:
            copy_status_text(outStatus->lastStepUtf8, sizeof(outStatus->lastStepUtf8), "lifecycle");
            break;
        }
        if (blockers != MC_BLOCK_NONE)
        {
            copy_status_text(outStatus->lastErrorUtf8, sizeof(outStatus->lastErrorUtf8), "bridge still has unload blockers");
        }
        return MC_OK;
    }

    McResult WINAPI bridge_api_create(const McBridgeStartInfo* startInfo, const McBridgeHostApi*, McBridgeHandle* outHandle)
    {
        if (!outHandle || !startInfo || startInfo->size < sizeof(McBridgeStartInfo))
        {
            return MC_E_INVALID_ARGUMENT;
        }
        if (startInfo->bridgeBuildIdUtf8 && startInfo->bridgeBuildIdUtf8[0] != '\0')
        {
            g_bridge_build_id = startInfo->bridgeBuildIdUtf8;
        }
        *outHandle = reinterpret_cast<McBridgeHandle>(&g_module);
        return MC_OK;
    }

    McResult WINAPI bridge_api_start(McBridgeHandle handle)
    {
        if (!handle)
        {
            return MC_E_INVALID_ARGUMENT;
        }
        std::lock_guard<std::mutex> lock(g_bridge_api_mutex);
        if (g_bridge_thread && g_bridge_thread->joinable() && g_bridge_thread_done.load())
        {
            g_bridge_thread->join();
            g_bridge_thread.reset();
        }
        if (g_bridge_thread && !g_bridge_thread_done.load())
        {
            return MC_E_ALREADY_STARTED;
        }
        try
        {
            g_running.store(true);
            g_bridge_last_win32.store(0);
            g_bridge_state.store(MC_BRIDGE_STARTING);
            g_bridge_thread_done.store(false);
            g_bridge_thread = std::make_unique<std::thread>(bridge_thread);
            return MC_OK;
        }
        catch (...)
        {
            g_running.store(false);
            g_bridge_thread_done.store(true);
            g_bridge_state.store(MC_BRIDGE_FAILED);
            return MC_E_START_FAILED;
        }
    }

    McResult WINAPI bridge_api_request_stop(McBridgeHandle handle, std::uint32_t)
    {
        if (!handle)
        {
            return MC_E_INVALID_ARGUMENT;
        }
        g_bridge_state.store(MC_BRIDGE_STOPPING);
        force_cancel_active_mesh_first_batch_job("loader_stop");
        cancel_queued_paint_jobs("loader_stop");
        uninstall_process_event_hook();
        g_running.store(false);
        return MC_OK;
    }

    McResult WINAPI bridge_api_join_stop(McBridgeHandle handle, std::uint32_t timeoutMs)
    {
        if (!handle)
        {
            return MC_E_INVALID_ARGUMENT;
        }
        const auto start = GetTickCount64();
        while (!g_bridge_thread_done.load())
        {
            if (timeoutMs > 0 && GetTickCount64() - start >= timeoutMs)
            {
                return MC_E_STOP_TIMED_OUT;
            }
            Sleep(25);
        }
        std::lock_guard<std::mutex> lock(g_bridge_api_mutex);
        if (g_bridge_thread && g_bridge_thread->joinable())
        {
            g_bridge_thread->join();
            g_bridge_thread.reset();
        }
        const auto blockers = bridge_unload_blockers();
        g_bridge_state.store(blockers == MC_BLOCK_NONE ? MC_BRIDGE_UNLOADABLE : MC_BRIDGE_STOPPED);
        return blockers == MC_BLOCK_NONE ? MC_OK : MC_E_UNLOAD_BLOCKED;
    }

    McResult WINAPI bridge_api_get_status(McBridgeHandle handle, McBridgeStatus* outStatus)
    {
        if (!handle)
        {
            return MC_E_INVALID_ARGUMENT;
        }
        return bridge_fill_status(outStatus);
    }

    McResult WINAPI bridge_api_destroy(McBridgeHandle handle)
    {
        if (!handle)
        {
            return MC_E_INVALID_ARGUMENT;
        }
        if (!g_bridge_thread_done.load() || bridge_unload_blockers() != MC_BLOCK_NONE)
        {
            return MC_E_UNLOAD_BLOCKED;
        }
        return MC_OK;
    }

    McBridgeApi g_bridge_api{
        sizeof(McBridgeApi),
        McLoaderAbiMajor,
        McLoaderAbiMinor,
        0,
        bridge_api_create,
        bridge_api_start,
        bridge_api_request_stop,
        bridge_api_join_stop,
        bridge_api_get_status,
        bridge_api_destroy,
    };
}

extern "C" __declspec(dllexport) McResult WINAPI McBridge_GetApi(std::uint32_t loaderAbiMajor,
                                                                  std::uint32_t,
                                                                  McBridgeApi* outApi)
{
    if (!outApi || outApi->size < sizeof(McBridgeApi))
    {
        return MC_E_INVALID_ARGUMENT;
    }
    if (loaderAbiMajor != McLoaderAbiMajor)
    {
        return MC_E_ABI_INCOMPATIBLE;
    }
    *outApi = g_bridge_api;
    return MC_OK;
}

// =============================================================================
// Section: Injected DLL entry point
// Risk: very high. This is reached by LoadLibrary in the target game process.
// =============================================================================

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        g_module = module;
    }
    if (reason == DLL_PROCESS_DETACH)
    {
        g_running.store(false);
    }
    return TRUE;
}
