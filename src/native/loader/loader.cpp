#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "../include/bridge_loader_abi.hpp"

namespace
{
    HMODULE g_loader_module = nullptr;
    std::mutex g_mutex;
    std::atomic<bool> g_pipe_running{false};
    std::thread g_pipe_thread{};

    std::wstring g_pipe_name{};
    std::wstring g_status_path{};
    std::wstring g_bridge_path{};
    std::wstring g_runtime_dir{};
    std::wstring g_log_dir{};
    std::string g_bridge_sha{};
    std::string g_bridge_build_id{"runtime-bridge"};
    std::uint16_t g_bridge_port{0};

    HMODULE g_bridge_module = nullptr;
    McBridgeApi g_bridge_api{};
    McBridgeHandle g_bridge_handle = nullptr;
    McBridgeStatus g_last_bridge_status{};
    std::uint32_t g_generation = 0;
    std::string g_loader_state{"Uninitialized"};
    std::string g_bridge_state{"Absent"};
    std::string g_last_result{"MC_OK"};
    std::string g_last_error{};
    bool g_restart_required = false;

    auto utf8_from_wide(const std::wstring& value) -> std::string
    {
        if (value.empty())
        {
            return {};
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    auto wide_from_utf8(const std::string& value) -> std::wstring
    {
        if (value.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0)
        {
            return {};
        }
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    auto json_escape(const std::string& value) -> std::string
    {
        std::string out;
        out.reserve(value.size() + 8);
        for (const char ch : value)
        {
            switch (ch)
            {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += static_cast<unsigned char>(ch) < 0x20 ? '?' : ch; break;
            }
        }
        return out;
    }

    auto result_name(McResult result) -> const char*
    {
        switch (result)
        {
        case MC_OK: return "MC_OK";
        case MC_E_INVALID_ARGUMENT: return "MC_E_INVALID_ARGUMENT";
        case MC_E_ABI_INCOMPATIBLE: return "MC_E_ABI_INCOMPATIBLE";
        case MC_E_ALREADY_STARTED: return "MC_E_ALREADY_STARTED";
        case MC_E_NOT_STARTED: return "MC_E_NOT_STARTED";
        case MC_E_START_FAILED: return "MC_E_START_FAILED";
        case MC_E_STOP_TIMED_OUT: return "MC_E_STOP_TIMED_OUT";
        case MC_E_UNLOAD_BLOCKED: return "MC_E_UNLOAD_BLOCKED";
        default: return "MC_E_INTERNAL";
        }
    }

    auto bridge_state_name(McBridgeRunState state) -> const char*
    {
        switch (state)
        {
        case MC_BRIDGE_CREATED: return "Created";
        case MC_BRIDGE_STARTING: return "Starting";
        case MC_BRIDGE_RUNNING_NOT_LISTENING: return "RunningNotListening";
        case MC_BRIDGE_RUNNING_LISTENING: return "RunningListening";
        case MC_BRIDGE_STOPPING: return "Stopping";
        case MC_BRIDGE_STOPPED: return "Stopped";
        case MC_BRIDGE_UNLOADABLE: return "Unloadable";
        case MC_BRIDGE_FAILED: return "Failed";
        default: return "Unknown";
        }
    }

    auto read_text_file(const std::wstring& path) -> std::string
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return {};
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    auto write_text_file(const std::wstring& path, const std::string& text) -> void
    {
        if (path.empty())
        {
            return;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
    }

    auto json_string_field(const std::string& json, const std::string& key) -> std::string
    {
        const std::string needle = "\"" + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos)
        {
            return {};
        }
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos)
        {
            return {};
        }
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos)
        {
            return {};
        }
        ++pos;
        std::string out;
        while (pos < json.size())
        {
            const char ch = json[pos++];
            if (ch == '"')
            {
                break;
            }
            if (ch == '\\' && pos < json.size())
            {
                const char escaped = json[pos++];
                switch (escaped)
                {
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                default: out += escaped; break;
                }
            }
            else
            {
                out += ch;
            }
        }
        return out;
    }

    auto json_int_field(const std::string& json, const std::string& key, int fallback = 0) -> int
    {
        const std::string needle = "\"" + key + "\"";
        auto pos = json.find(needle);
        if (pos == std::string::npos)
        {
            return fallback;
        }
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos)
        {
            return fallback;
        }
        ++pos;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        {
            ++pos;
        }
        char* end = nullptr;
        const long value = std::strtol(json.c_str() + pos, &end, 10);
        return end == json.c_str() + pos ? fallback : static_cast<int>(value);
    }

    auto publish_status_locked() -> void
    {
        if (g_bridge_api.GetStatus && g_bridge_handle)
        {
            McBridgeStatus status{};
            status.size = sizeof(status);
            if (g_bridge_api.GetStatus(g_bridge_handle, &status) == MC_OK)
            {
                g_last_bridge_status = status;
                g_bridge_state = bridge_state_name(status.state);
            }
        }

        std::ostringstream out;
        out << "{\n";
        out << "  \"schema\": 1,\n";
        out << "  \"pid\": " << GetCurrentProcessId() << ",\n";
        out << "  \"generation\": " << g_generation << ",\n";
        out << "  \"loaderState\": \"" << json_escape(g_loader_state) << "\",\n";
        out << "  \"bridgeState\": \"" << json_escape(g_bridge_state) << "\",\n";
        out << "  \"result\": \"" << json_escape(g_last_result) << "\",\n";
        out << "  \"restartRequired\": " << (g_restart_required ? "true" : "false") << ",\n";
        out << "  \"pipeName\": \"" << json_escape(utf8_from_wide(g_pipe_name)) << "\",\n";
        out << "  \"bridgePath\": \"" << json_escape(utf8_from_wide(g_bridge_path)) << "\",\n";
        out << "  \"bridgeBuildId\": \"" << json_escape(g_bridge_build_id) << "\",\n";
        out << "  \"tcpPort\": " << g_last_bridge_status.tcpPort << ",\n";
        out << "  \"unloadBlockers\": " << g_last_bridge_status.unloadBlockers << ",\n";
        out << "  \"lastWin32\": " << g_last_bridge_status.lastWin32 << ",\n";
        out << "  \"lastError\": \"" << json_escape(g_last_error) << "\"\n";
        out << "}\n";
        write_text_file(g_status_path, out.str());
    }

    auto publish_status() -> void
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        publish_status_locked();
    }

    auto stop_bridge_locked(std::uint32_t timeout_ms) -> McResult
    {
        if (!g_bridge_module || !g_bridge_handle || !g_bridge_api.RequestStop || !g_bridge_api.JoinStop)
        {
            return MC_OK;
        }
        g_loader_state = "BridgeStopping";
        g_bridge_state = "Stopping";
        McResult result = g_bridge_api.RequestStop(g_bridge_handle, 0);
        if (result != MC_OK)
        {
            g_last_result = result_name(result);
            g_last_error = "bridge RequestStop failed";
            g_restart_required = true;
            publish_status_locked();
            return result;
        }
        result = g_bridge_api.JoinStop(g_bridge_handle, timeout_ms);
        g_last_result = result_name(result);
        if (result != MC_OK)
        {
            g_last_error = "bridge did not become unloadable";
            g_restart_required = true;
        }
        publish_status_locked();
        return result;
    }

    auto unload_bridge_locked(std::uint32_t timeout_ms) -> McResult
    {
        const McResult stopped = stop_bridge_locked(timeout_ms);
        if (stopped != MC_OK)
        {
            return stopped;
        }
        if (g_bridge_api.Destroy && g_bridge_handle)
        {
            const McResult destroyed = g_bridge_api.Destroy(g_bridge_handle);
            if (destroyed != MC_OK)
            {
                g_last_result = result_name(destroyed);
                g_last_error = "bridge Destroy refused unload";
                g_restart_required = true;
                publish_status_locked();
                return destroyed;
            }
        }
        if (g_bridge_module)
        {
            const HMODULE module = g_bridge_module;
            g_bridge_module = nullptr;
            g_bridge_handle = nullptr;
            std::memset(&g_bridge_api, 0, sizeof(g_bridge_api));
            if (!FreeLibrary(module))
            {
                g_last_result = "MC_E_UNLOAD_BLOCKED";
                g_last_error = "FreeLibrary failed";
                g_restart_required = true;
                publish_status_locked();
                return MC_E_UNLOAD_BLOCKED;
            }
        }
        g_loader_state = "Ready";
        g_bridge_state = "Absent";
        g_last_result = "MC_OK";
        g_last_error.clear();
        ++g_generation;
        publish_status_locked();
        return MC_OK;
    }

    auto load_and_start_bridge_locked() -> McResult
    {
        if (g_bridge_module)
        {
            wchar_t loaded_path[MAX_PATH]{};
            GetModuleFileNameW(g_bridge_module, loaded_path, MAX_PATH);
            if (_wcsicmp(loaded_path, g_bridge_path.c_str()) == 0)
            {
                if (g_bridge_api.GetStatus && g_bridge_handle)
                {
                    McBridgeStatus status{};
                    status.size = sizeof(status);
                    if (g_bridge_api.GetStatus(g_bridge_handle, &status) == MC_OK &&
                        (status.state == MC_BRIDGE_STARTING ||
                         status.state == MC_BRIDGE_RUNNING_NOT_LISTENING ||
                         status.state == MC_BRIDGE_RUNNING_LISTENING))
                    {
                        g_last_bridge_status = status;
                        g_bridge_state = bridge_state_name(status.state);
                        g_last_result = "MC_OK";
                        publish_status_locked();
                        return MC_OK;
                    }
                }
                const McResult unloaded = unload_bridge_locked(5000);
                if (unloaded != MC_OK)
                {
                    return unloaded;
                }
            }
            else
            {
                const McResult unloaded = unload_bridge_locked(5000);
                if (unloaded != MC_OK)
                {
                    return unloaded;
                }
            }
        }

        g_loader_state = "BridgeLoading";
        g_bridge_state = "Loading";
        ++g_generation;
        publish_status_locked();

        HMODULE module = LoadLibraryExW(g_bridge_path.c_str(), nullptr,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module)
        {
            const DWORD error = GetLastError();
            g_last_result = "BridgeLoadFailed";
            g_last_error = "LoadLibraryExW failed win32=" + std::to_string(error);
            g_restart_required = false;
            g_loader_state = "BridgeFailed";
            g_bridge_state = "Failed";
            g_last_bridge_status.lastWin32 = error;
            publish_status_locked();
            return MC_E_START_FAILED;
        }

        auto get_api = reinterpret_cast<McBridgeGetApiFn>(GetProcAddress(module, "McBridge_GetApi"));
        if (!get_api)
        {
            g_bridge_module = module;
            g_last_result = "BridgeAbiIncompatible";
            g_last_error = "McBridge_GetApi export missing";
            g_restart_required = false;
            publish_status_locked();
            FreeLibrary(module);
            g_bridge_module = nullptr;
            return MC_E_ABI_INCOMPATIBLE;
        }

        McBridgeApi api{};
        api.size = sizeof(api);
        McResult result = get_api(McLoaderAbiMajor, McLoaderAbiMinor, &api);
        if (result != MC_OK)
        {
            g_last_result = result_name(result);
            g_last_error = "McBridge_GetApi rejected ABI";
            publish_status_locked();
            FreeLibrary(module);
            return result;
        }

        McBridgeStartInfo start{};
        start.size = sizeof(start);
        start.bridgePath = g_bridge_path.c_str();
        start.runtimeDir = g_runtime_dir.c_str();
        start.logDir = g_log_dir.c_str();
        start.bridgeBuildIdUtf8 = g_bridge_build_id.c_str();
        start.expectedBridgeSha256Utf8 = g_bridge_sha.c_str();
        start.tcpBindHostUtf8 = "127.0.0.1";
        start.tcpPortHint = g_bridge_port;
        start.appProtocolMajor = 1;

        McBridgeHandle handle = nullptr;
        result = api.Create(&start, nullptr, &handle);
        if (result == MC_OK)
        {
            g_loader_state = "BridgeStarting";
            g_bridge_state = "Starting";
            publish_status_locked();
            result = api.Start(handle);
        }
        if (result != MC_OK && result != MC_E_ALREADY_STARTED)
        {
            g_last_result = result_name(result);
            g_last_error = "bridge Create/Start failed";
            g_restart_required = false;
            publish_status_locked();
            FreeLibrary(module);
            return result;
        }

        g_bridge_module = module;
        g_bridge_api = api;
        g_bridge_handle = handle;
        g_loader_state = "BridgeRunning";
        g_last_result = "MC_OK";
        g_last_error.clear();
        g_restart_required = false;
        publish_status_locked();
        return MC_OK;
    }

    auto status_json_locked(bool ok, const std::string& result) -> std::string
    {
        publish_status_locked();
        std::ostringstream out;
        out << "{\"ok\":" << (ok ? "true" : "false")
            << ",\"result\":\"" << json_escape(result) << "\""
            << ",\"restartRequired\":" << (g_restart_required ? "true" : "false")
            << ",\"generation\":" << g_generation
            << ",\"loaderState\":\"" << json_escape(g_loader_state) << "\""
            << ",\"bridgeState\":\"" << json_escape(g_bridge_state) << "\""
            << ",\"tcpPort\":" << g_last_bridge_status.tcpPort
            << ",\"lastError\":\"" << json_escape(g_last_error) << "\"}\n";
        return out.str();
    }

    auto handle_pipe_request(const std::string& request) -> std::string
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (request.find("\"cmd\":\"status\"") != std::string::npos ||
            request.find("\"cmd\":\"hello\"") != std::string::npos)
        {
            return status_json_locked(true, "MC_OK");
        }
        if (request.find("\"cmd\":\"stopBridge\"") != std::string::npos)
        {
            const McResult result = stop_bridge_locked(5000);
            return status_json_locked(result == MC_OK, result_name(result));
        }
        if (request.find("\"cmd\":\"unloadBridge\"") != std::string::npos)
        {
            const McResult result = unload_bridge_locked(5000);
            return status_json_locked(result == MC_OK, result_name(result));
        }
        if (request.find("\"cmd\":\"loadAndStartBridge\"") != std::string::npos ||
            request.find("\"cmd\":\"switchBridge\"") != std::string::npos)
        {
            const std::string status_path = json_string_field(request, "statusPath");
            if (!status_path.empty())
            {
                g_status_path = wide_from_utf8(status_path);
            }
            const std::string path = json_string_field(request, "path");
            if (!path.empty())
            {
                g_bridge_path = wide_from_utf8(path);
            }
            const std::string sha = json_string_field(request, "sha256");
            if (!sha.empty())
            {
                g_bridge_sha = sha;
            }
            const std::string build_id = json_string_field(request, "buildId");
            if (!build_id.empty())
            {
                g_bridge_build_id = build_id;
            }
            const std::string runtime_dir = json_string_field(request, "runtimeDir");
            if (!runtime_dir.empty())
            {
                g_runtime_dir = wide_from_utf8(runtime_dir);
            }
            const std::string log_dir = json_string_field(request, "logDir");
            if (!log_dir.empty())
            {
                g_log_dir = wide_from_utf8(log_dir);
            }
            const int port = json_int_field(request, "port", 0);
            if (port > 0)
            {
                g_bridge_port = static_cast<std::uint16_t>(port);
            }
            const McResult result = load_and_start_bridge_locked();
            return status_json_locked(result == MC_OK, result_name(result));
        }
        return status_json_locked(false, "MC_E_INVALID_ARGUMENT");
    }

    auto pipe_server() -> void
    {
        while (g_pipe_running.load())
        {
            HANDLE pipe = CreateNamedPipeW(g_pipe_name.c_str(),
                                           PIPE_ACCESS_DUPLEX,
                                           PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                           1,
                                           64 * 1024,
                                           64 * 1024,
                                           2000,
                                           nullptr);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                Sleep(1000);
                continue;
            }
            const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
            if (connected)
            {
                std::uint32_t length = 0;
                DWORD read = 0;
                std::string request;
                if (ReadFile(pipe, &length, sizeof(length), &read, nullptr) && read == sizeof(length) && length > 0 && length <= 1024 * 1024)
                {
                    request.resize(length);
                    DWORD body_read = 0;
                    if (!ReadFile(pipe, request.data(), length, &body_read, nullptr) || body_read != length)
                    {
                        request.clear();
                    }
                }
                const std::string response = request.empty()
                                                 ? "{\"ok\":false,\"result\":\"MC_E_INVALID_ARGUMENT\"}\n"
                                                 : handle_pipe_request(request);
                const std::uint32_t response_length = static_cast<std::uint32_t>(response.size());
                DWORD written = 0;
                WriteFile(pipe, &response_length, sizeof(response_length), &written, nullptr);
                WriteFile(pipe, response.data(), response_length, &written, nullptr);
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
    }

    auto start_pipe_locked() -> void
    {
        if (g_pipe_running.load() || g_pipe_name.empty())
        {
            return;
        }
        g_pipe_running.store(true);
        g_pipe_thread = std::thread(pipe_server);
        g_pipe_thread.detach();
    }

    auto load_config_locked(const std::wstring& path) -> bool
    {
        const std::string json = read_text_file(path);
        if (json.empty())
        {
            g_loader_state = "BridgeFailed";
            g_last_result = "ConfigMissing";
            g_last_error = "loader config missing or empty";
            g_restart_required = false;
            publish_status_locked();
            return false;
        }
        g_pipe_name = wide_from_utf8(json_string_field(json, "pipeName"));
        g_status_path = wide_from_utf8(json_string_field(json, "statusPath"));
        g_bridge_path = wide_from_utf8(json_string_field(json, "path"));
        g_bridge_sha = json_string_field(json, "sha256");
        g_bridge_build_id = json_string_field(json, "buildId");
        if (g_bridge_build_id.empty())
        {
            g_bridge_build_id = "runtime-bridge";
        }
        g_runtime_dir = wide_from_utf8(json_string_field(json, "runtimeDir"));
        g_log_dir = wide_from_utf8(json_string_field(json, "logDir"));
        g_bridge_port = static_cast<std::uint16_t>(json_int_field(json, "port", 0));
        if (g_pipe_name.empty() || g_status_path.empty() || g_bridge_path.empty())
        {
            g_loader_state = "BridgeFailed";
            g_last_result = "ConfigInvalid";
            g_last_error = "loader config did not include pipeName, statusPath, or bridge path";
            g_restart_required = false;
            publish_status_locked();
            return false;
        }
        g_loader_state = "Ready";
        g_last_result = "MC_OK";
        g_last_error.clear();
        publish_status_locked();
        return true;
    }
}

extern "C" __declspec(dllexport) DWORD WINAPI McLoader_RemoteMain(void* remoteUtf16ConfigPath)
{
    const auto* path = reinterpret_cast<const wchar_t*>(remoteUtf16ConfigPath);
    if (!path || path[0] == L'\0')
    {
        return ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!load_config_locked(path))
    {
        return ERROR_BAD_CONFIGURATION;
    }
    start_pipe_locked();
    const McResult result = load_and_start_bridge_locked();
    return result == MC_OK ? ERROR_SUCCESS : static_cast<DWORD>(result);
}

extern "C" __declspec(dllexport) DWORD WINAPI McLoader_GetAbiVersion()
{
    return (McLoaderAbiMajor << 16) | McLoaderAbiMinor;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_loader_module = module;
        DisableThreadLibraryCalls(module);
    }
    if (reason == DLL_PROCESS_DETACH)
    {
        g_pipe_running.store(false);
    }
    return TRUE;
}
