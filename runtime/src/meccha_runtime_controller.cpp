#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <filesystem>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "User32.lib")

namespace
{
    constexpr int BridgeResourceId = 101;
    constexpr wchar_t DefaultGameProcessName[] = L"PenguinHotel-Win64-Shipping.exe";
    constexpr char DefaultBridgeHost[] = "127.0.0.1";
    constexpr int DefaultBridgePort = 0;
    constexpr UINT HotkeyId = 0x4D43;
    constexpr UINT HotkeyVk = VK_F10;

    struct Config
    {
        std::wstring mode{L"service"};
        std::wstring game_process_name{DefaultGameProcessName};
        std::wstring log_dir{};
        std::string bridge_host{DefaultBridgeHost};
        int bridge_port{DefaultBridgePort};
        double bridge_timeout_seconds{240.0};
        double process_poll_seconds{1.0};
        double status_interval_seconds{2.0};
        double frame_delay_ms{16.0};
        int service_max_frames{0};
        double service_max_duration_seconds{0.0};
        std::wstring service_stop_file{};
        std::string native_apply_mode{"texture_sync_strict_probe"};
        bool auto_sdk_probe{false};
        bool auto_sdk_deep_probe{false};
        bool print_summary{false};
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

    auto extract_json_bool(const std::string& text, const std::string& key) -> bool
    {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = text.find(needle);
        if (start == std::string::npos)
            return false;
        const auto pos = start + needle.size();
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
        explicit Diagnostics(std::filesystem::path log_dir)
            : log_dir_(std::move(log_dir)),
              events_path_(log_dir_ / L"events.jsonl"),
              runtime_log_path_(log_dir_ / L"runtime.log"),
              status_path_(log_dir_ / L"last_status.json")
        {
            std::error_code ec;
            std::filesystem::create_directories(log_dir_, ec);
        }

        auto log_dir() const -> const std::filesystem::path& { return log_dir_; }
        auto status_path() const -> const std::filesystem::path& { return status_path_; }

        void set_process(std::string json) { process_json_ = std::move(json); write_status(); }
        void set_bridge(std::string json) { bridge_json_ = std::move(json); write_status(); }
        void set_hotkey(std::string json) { hotkey_json_ = std::move(json); write_status(); }
        void set_last_run(std::string json) { last_run_json_ = std::move(json); write_status(); }
        void clear_error() { last_error_json_ = "null"; write_status(); }

        void event(const std::string& name,
                   const std::string& level,
                   const std::string& stage,
                   const std::string& message,
                   const std::string& details_json = "{}",
                   const std::string& run_id = "")
        {
            const std::string ts = now_utc_iso();
            const std::string details = details_json.empty() ? "{}" : details_json;
            const std::string entry = std::string("{\"timestamp\":") + json_string(ts) +
                                      ",\"level\":" + json_string(level) +
                                      ",\"event\":" + json_string(name) +
                                      ",\"run_id\":" + json_string(run_id) +
                                      ",\"stage\":" + json_string(stage) +
                                      ",\"message\":" + json_string(message) +
                                      ",\"details\":" + details + "}";
            append_text_file(events_path_, entry + "\n");
            append_text_file(runtime_log_path_, ts + " " + upper(level) + " " + name + " stage=" + stage + " run_id=" + run_id + " " + message + " " + details + "\n");
            const auto level_tag = level == "warning" ? "WARN" : upper(level);
            if (name == "paint_progress")
            {
                if (progress_line_active_ && stage != active_progress_stage_)
                {
                    std::cout << "\r" << active_progress_line_ << " 100% done" << std::string(12, ' ') << std::endl;
                }
                active_progress_stage_ = stage;
                active_progress_line_ = "[" + std::string(level_tag) + "] progress " + pretty(stage) + " " + message;
                std::cout << "\r" << active_progress_line_ << "          " << std::flush;
                progress_line_active_ = true;
                return;
            }
            if (progress_line_active_)
            {
                std::cout << "\r" << active_progress_line_ << " 100% done" << std::string(12, ' ') << std::endl;
                progress_line_active_ = false;
                active_progress_stage_.clear();
                active_progress_line_.clear();
            }
            if (name == "service_idle")
            {
                return;
            }
            if (name == "waiting_for_hotkey")
            {
                std::cout << "[READY] waiting for user to press F10..." << std::endl;
                return;
            }
            if (name == "paint_done")
            {
                std::cout << "[DONE] " << message << std::endl;
                return;
            }
            std::cout << "[" << level_tag << "] " << pretty(name) << " stage=" << pretty(stage) << " " << message << std::endl;
            if (level == "error")
            {
                std::cout << "  log_dir=" << wide_to_utf8(log_dir_.wstring()) << std::endl;
                std::cout << "  status=" << wide_to_utf8(status_path_.wstring()) << std::endl;
                std::cout << "  details=events.jsonl" << std::endl;
            }
        }

        void record_error(const std::string& stage, const std::string& message, const std::string& details_json = "{}", const std::string& run_id = "")
        {
            last_error_json_ = std::string("{\"timestamp\":") + json_string(now_utc_iso()) +
                               ",\"stage\":" + json_string(stage) +
                               ",\"message\":" + json_string(message) +
                               ",\"details\":" + (details_json.empty() ? "{}" : details_json) +
                               ",\"run_id\":" + json_string(run_id) + "}";
            write_status();
            event("runtime_error", "error", stage, message, details_json, run_id);
        }

        void write_status()
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

    private:
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

        std::filesystem::path log_dir_;
        std::filesystem::path events_path_;
        std::filesystem::path runtime_log_path_;
        std::filesystem::path status_path_;
        std::string process_json_{};
        std::string bridge_json_{};
        std::string hotkey_json_{};
        std::string last_run_json_{};
        std::string last_error_json_{"null"};
        bool progress_line_active_{false};
        std::string active_progress_stage_{};
        std::string active_progress_line_{};
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
               ",\"adapter\":\"xenos\"" +
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
        const auto bridge_path = native_dir / utf8_to_wide("meccha-xenos-bridge-" + hash + "-" + std::to_string(port) + ".dll");
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

    class Hotkey
    {
    public:
        Hotkey()
        {
            registered_ = RegisterHotKey(nullptr, HotkeyId, MOD_NOREPEAT, HotkeyVk) != FALSE;
            last_error_ = registered_ ? 0 : GetLastError();
        }
        ~Hotkey()
        {
            if (registered_)
                UnregisterHotKey(nullptr, HotkeyId);
        }
        auto backend() const -> std::string
        {
            if (registered_)
                return "register_hotkey";
            return std::string("async_state(register_hotkey_failed win32=") + std::to_string(last_error_) + ")";
        }
        auto consume() -> bool
        {
            if (registered_)
            {
                MSG msg{};
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_HOTKEY && msg.wParam == HotkeyId)
                        return true;
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                return false;
            }
            const bool down = (GetAsyncKeyState(HotkeyVk) & 0x8000) != 0;
            const bool edge = down && !last_async_down_;
            last_async_down_ = down;
            return edge;
        }

    private:
        bool registered_{false};
        DWORD last_error_{0};
        bool last_async_down_{false};
    };

    auto mode_to_route(const std::string& native_apply_mode) -> std::string
    {
        if (native_apply_mode == "texture_sync_strict_probe") return "f10_texture_sync_strict_probe";
        if (native_apply_mode == "static_hybrid_front_side_probe") return "f10_static_hybrid_front_side_probe";
        return "unsupported_route";
    }

    auto is_supported_native_apply_mode(const std::string& native_apply_mode) -> bool
    {
        return native_apply_mode == "texture_sync_strict_probe" ||
               native_apply_mode == "static_hybrid_front_side_probe";
    }

    auto paint_payload(const Config& config, const ProcessInfo& process) -> std::string
    {
        return std::string("{\"native_apply_mode\":") + json_string(config.native_apply_mode) +
               ",\"route\":" + json_string(mode_to_route(config.native_apply_mode)) +
               ",\"process\":{\"pid\":" + std::to_string(process.pid) +
               ",\"name\":" + json_string(wide_to_utf8(process.name)) + "}}";
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
            return false;
        diagnostics.event("inject_started", "info", "inject", "attempting native bridge injection",
                          std::string("{\"process\":") + process_json(process, config.game_process_name) +
                          ",\"bridge_dll\":" + json_string(wide_to_utf8(bridge_path.wstring())) + "}");
        write_bridge_port_file(bridge_path, config.bridge_port);
        const auto [ok, message] = inject_dll(process.pid, bridge_path);
        injected_pid = process.pid;
        diagnostics.event(ok ? "inject_done" : "inject_failed", ok ? "info" : "error", "inject",
                          ok ? "native bridge injection completed" : "native bridge injection failed",
                          std::string("{\"message\":") + json_string(message) +
                          ",\"bridge_port\":" + std::to_string(config.bridge_port) + "}");
        if (!ok)
            return false;
        return wait_for_bridge_ready(config, bridge_path, diagnostics, 5.0);
    }

    auto run_bridge_command(const Config& config, const std::filesystem::path& bridge_path, const std::string& command, const std::string& payload = "{}") -> BridgeResponse
    {
        (void)bridge_path;
        BridgeClient client(config.bridge_host, config.bridge_port, config.bridge_timeout_seconds);
        return client.request(command, payload);
    }

    void log_probe_result(Diagnostics& diagnostics, const std::string& event_name, const BridgeResponse& response)
    {
        const bool context_pending = response.stage == "world_unavailable" ||
                                     response.stage == "local_pawn_unavailable" ||
                                     response.stage == "paint_component_unavailable" ||
                                     response.stage == "sdk_context_unavailable";
        diagnostics.event(response.success ? event_name : event_name + "_failed",
                          response.success ? "info" : (context_pending ? "warning" : "error"),
                          response.stage.empty() ? event_name : response.stage,
                          "dev_context_check " + event_name + ": " + response.message,
                          std::string("{\"probe\":") + json_string(event_name) +
                          ",\"response\":" + (response.raw.empty() ? "{}" : response.raw) +
                          ",\"will_retry\":" + (response.success ? "false" : "true") + "}");
    }

    auto run_paint(const Config& config,
                   const std::filesystem::path& bridge_path,
                   const ProcessInfo& process,
                   Diagnostics& diagnostics) -> bool
    {
        const std::string run_id = hex64(GetTickCount64()) + hex64(process.pid);
        const double start = seconds_now();
        if (!is_supported_native_apply_mode(config.native_apply_mode))
        {
            const std::string details = std::string("{\"native_apply_mode\":") + json_string(config.native_apply_mode) +
                                        ",\"supported_native_apply_modes\":[\"texture_sync_strict_probe\",\"static_hybrid_front_side_probe\"]}";
            diagnostics.record_error("unsupported_route", "unsupported native apply mode", details, run_id);
            diagnostics.event("paint_failed", "error", "unsupported_route", "unsupported native apply mode", details, run_id);
            return false;
        }
        diagnostics.event("plan_generated", "info", "plan", "native paint payload generated",
                          std::string("{\"native_apply_mode\":") + json_string(config.native_apply_mode) + "}", run_id);
        diagnostics.set_last_run(std::string("{\"run_id\":") + json_string(run_id) +
                                 ",\"stage\":\"paint_started\",\"success\":false,\"process\":" + process_json(process, config.game_process_name) + "}");
        diagnostics.event("paint_started", "info", "paint", "paint_full_route started",
                          std::string("{\"adapter\":\"xenos\",\"native_apply_mode\":") + json_string(config.native_apply_mode) + "}", run_id);
        const std::string payload = paint_payload(config, process);
        auto future = std::async(std::launch::async, [&]() {
            return run_bridge_command(config, bridge_path, "paint_full_route", payload);
        });
        const auto progress_path = bridge_progress_file(bridge_path);
        std::string last_signature;
        while (future.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready)
        {
            const std::string progress = read_text_file(progress_path);
            if (!progress.empty())
            {
                const std::string stage = extract_json_string(progress, "stage");
                const int percent = static_cast<int>(extract_json_number(progress, "progress", 0.0) * 100.0 + 0.5);
                const std::string signature = stage + ":" + std::to_string(percent);
                if (!stage.empty() && signature != last_signature)
                {
                    last_signature = signature;
                    std::string summary = std::to_string(percent) + "%";
                    const auto front_hits = extract_json_number(progress, "front_hits", -1.0);
                    const auto unique_texels = extract_json_number(progress, "unique_atlas_texels", -1.0);
                    const auto side_back_hits = extract_json_number(progress, "side_back_hits", -1.0);
                    const auto side_hits = extract_json_number(progress, "side_hits", -1.0);
                    const auto back_hits = extract_json_number(progress, "back_hits", -1.0);
                    const auto back_attempts = extract_json_number(progress, "back_attempts", -1.0);
                    const auto back_views = extract_json_number(progress, "back_views", -1.0);
                    const auto source_conflicts = extract_json_number(progress, "source_conflict_texels", -1.0);
                    const auto direct_texels = extract_json_number(progress, "direct_texels", -1.0);
                    const auto back_direct_texels = extract_json_number(progress, "back_direct_texels", -1.0);
                    const auto hit_test_calls = extract_json_number(progress, "hit_test_calls", -1.0);
                    if (front_hits >= 0.0)
                    {
                        summary += " front=" + std::to_string(static_cast<long long>(front_hits));
                    }
                    if (side_back_hits >= 0.0)
                    {
                        summary += " side/back=" + std::to_string(static_cast<long long>(side_back_hits));
                    }
                    if (side_hits >= 0.0 || back_hits >= 0.0)
                    {
                        summary += " side=" + std::to_string(static_cast<long long>(std::max(0.0, side_hits)));
                        summary += " back=" + std::to_string(static_cast<long long>(std::max(0.0, back_hits)));
                    }
                    if (back_attempts >= 0.0)
                    {
                        summary += " back_attempts=" + std::to_string(static_cast<long long>(back_attempts));
                    }
                    if (back_views >= 0.0)
                    {
                        summary += " back_views=" + std::to_string(static_cast<long long>(back_views));
                    }
                    if (unique_texels >= 0.0)
                    {
                        summary += " unique=" + std::to_string(static_cast<long long>(unique_texels));
                    }
                    if (direct_texels >= 0.0)
                    {
                        summary += " direct=" + std::to_string(static_cast<long long>(direct_texels));
                    }
                    if (back_direct_texels >= 0.0)
                    {
                        summary += " back_direct=" + std::to_string(static_cast<long long>(back_direct_texels));
                    }
                    if (source_conflicts >= 0.0)
                    {
                        summary += " conflicts=" + std::to_string(static_cast<long long>(source_conflicts));
                    }
                    if (hit_test_calls >= 0.0)
                    {
                        summary += " calls=" + std::to_string(static_cast<long long>(hit_test_calls));
                    }
                    diagnostics.event("paint_progress", "info", stage, summary, progress, run_id);
                    diagnostics.set_last_run(std::string("{\"run_id\":") + json_string(run_id) +
                                             ",\"stage\":" + json_string(stage) +
                                             ",\"success\":false,\"progress\":" + progress + "}");
                }
            }
        }
        BridgeResponse response = future.get();
        const double elapsed_ms = (seconds_now() - start) * 1000.0;
        for (const auto& event_name : extract_bridge_events(response.raw))
        {
            if (event_name == "paint_done" || event_name == "paint_failed")
                continue;
            diagnostics.event(event_name, "info", event_name, event_name,
                              std::string("{\"elapsed_ms\":") + std::to_string(elapsed_ms) +
                              ",\"bridge_response\":" + (response.raw.empty() ? "{}" : response.raw) + "}", run_id);
        }
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
        diagnostics.record_error(stage, message, details, run_id);
        diagnostics.event("paint_failed", "error", stage, message, details, run_id);
        return false;
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
            else if (key == L"--process-poll-seconds") config.process_poll_seconds = std::max(0.1, _wtof(next_w().c_str()));
            else if (key == L"--status-interval-seconds") config.status_interval_seconds = std::max(0.5, _wtof(next_w().c_str()));
            else if (key == L"--frame-delay-ms") config.frame_delay_ms = std::max(0.0, _wtof(next_w().c_str()));
            else if (key == L"--service-max-frames") config.service_max_frames = std::max(0, _wtoi(next_w().c_str()));
            else if (key == L"--service-max-duration-seconds") config.service_max_duration_seconds = std::max(0.0, _wtof(next_w().c_str()));
            else if (key == L"--service-stop-file") config.service_stop_file = next_w();
            else if (key == L"--native-apply-mode") config.native_apply_mode = next_s();
            else if (key == L"--auto-sdk-probe") config.auto_sdk_probe = true;
            else if (key == L"--auto-sdk-deep-probe") { config.auto_sdk_probe = true; config.auto_sdk_deep_probe = true; }
            else if (key == L"--print-summary") config.print_summary = true;
            else if (key == L"--adapter" || key == L"--service-trigger-key" || key == L"--service-stop-key" || key == L"--service-trigger-file" || key == L"--native-root" || key == L"--bridge-path" || key == L"--bridge-transport") { if (has_value()) ++i; }
            else if (key == L"--service-apply-on-start" || key == L"--service-apply-every-frame" || key == L"--quick" || key == L"--dry-run") {}
            else if (key.rfind(L"--", 0) == 0 && has_value()) { ++i; }
        }
        if (config.bridge_port <= 0 && config.mode != L"shutdown")
            config.bridge_port = choose_bridge_port(config.bridge_host);
        return config;
    }

    auto run_probe_sequence(const Config& config, const std::filesystem::path& bridge_path, Diagnostics& diagnostics, bool deep) -> bool
    {
        auto probe = run_bridge_command(config, bridge_path, "sdk_probe");
        log_probe_result(diagnostics, "sdk_probe", probe);
        if (!probe.success)
            return false;
        if (deep)
        {
            auto deep_probe = run_bridge_command(config, bridge_path, "sdk_deep_probe");
            log_probe_result(diagnostics, "sdk_deep_probe", deep_probe);
            return deep_probe.success;
        }
        return true;
    }

    auto run_apply_once(const Config& config, const std::filesystem::path& bridge_path, Diagnostics& diagnostics, bool paint) -> int
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
        if (!paint)
            return run_probe_sequence(config, bridge_path, diagnostics, true) ? 0 : 3;
        if ((config.auto_sdk_probe || config.auto_sdk_deep_probe) &&
            !run_probe_sequence(config, bridge_path, diagnostics, config.auto_sdk_deep_probe))
            return 3;
        if (paint)
            return run_paint(config, bridge_path, process, diagnostics) ? 0 : 4;
        return 0;
    }

    auto run_service(const Config& config, const std::filesystem::path& bridge_path, Diagnostics& diagnostics) -> int
    {
        Hotkey hotkey{};
        diagnostics.set_hotkey(std::string("{\"trigger_key\":\"f10\",\"trigger_backend\":") + json_string(hotkey.backend()) + "}");
        diagnostics.event("hotkey_ready", "info", "startup", "hotkey backend=" + hotkey.backend(),
                          std::string("{\"trigger_key\":\"f10\",\"trigger_backend\":") + json_string(hotkey.backend()) + "}");
        diagnostics.event("runtime_start", "info", "startup", "service started",
                          std::string("{\"pid\":") + std::to_string(GetCurrentProcessId()) +
                          ",\"log_dir\":" + json_string(wide_to_utf8(diagnostics.log_dir().wstring())) +
                          ",\"game_process_name\":" + json_string(wide_to_utf8(config.game_process_name)) +
                          ",\"bridge_host\":" + json_string(config.bridge_host) +
                          ",\"bridge_port\":" + std::to_string(config.bridge_port) + "}");
        ProcessInfo attached{};
        DWORD injected_pid = 0;
        DWORD sdk_probe_pid = 0;
        DWORD sdk_deep_probe_pid = 0;
        double last_process_log = 0.0;
        double last_bridge_check = 0.0;
        double last_sdk_probe = 0.0;
        double last_sdk_deep_probe = 0.0;
        double last_idle = seconds_now();
        bool waiting_for_hotkey_logged = false;
        int frame = 0;
        const double deadline = config.service_max_duration_seconds > 0.0 ? seconds_now() + config.service_max_duration_seconds : 0.0;
        while (true)
        {
            if (config.service_max_frames > 0 && frame >= config.service_max_frames)
                return 0;
            if (deadline > 0.0 && seconds_now() >= deadline)
                return 0;
            if (!config.service_stop_file.empty() && std::filesystem::exists(config.service_stop_file))
            {
                diagnostics.event("service_stop", "info", "shutdown", "stop-file detected");
                return 0;
            }

            const double now = seconds_now();
            ProcessInfo process = find_process_by_name(config.game_process_name);
            if (process.pid == 0)
            {
                attached = {};
                injected_pid = sdk_probe_pid = sdk_deep_probe_pid = 0;
                waiting_for_hotkey_logged = false;
                diagnostics.set_process(process_json(process, config.game_process_name));
                if (now - last_process_log >= config.status_interval_seconds)
                {
                    diagnostics.event("waiting_for_process", "info", "process_wait", "waiting for " + wide_to_utf8(config.game_process_name),
                                      std::string("{\"game_process_name\":") + json_string(wide_to_utf8(config.game_process_name)) + "}");
                    last_process_log = now;
                }
                Sleep(static_cast<DWORD>(config.process_poll_seconds * 1000.0));
                ++frame;
                continue;
            }
            if (attached.pid != process.pid)
            {
                attached = process;
                injected_pid = sdk_probe_pid = sdk_deep_probe_pid = 0;
                waiting_for_hotkey_logged = false;
                last_bridge_check = last_sdk_probe = last_sdk_deep_probe = 0.0;
                diagnostics.set_process(process_json(process, config.game_process_name));
                diagnostics.event("process_attached", "info", "process", "attached to " + wide_to_utf8(process.name), process_json(process, config.game_process_name));
            }
            if (now - last_bridge_check >= config.status_interval_seconds)
            {
                ensure_bridge(config, bridge_path, process, diagnostics, injected_pid);
                last_bridge_check = now;
                if (injected_pid == process.pid && !waiting_for_hotkey_logged)
                {
                    diagnostics.event("waiting_for_hotkey", "info", "ready", "waiting for user to press F10");
                    waiting_for_hotkey_logged = true;
                }
            }
            if (config.auto_sdk_probe && injected_pid == process.pid && sdk_probe_pid != process.pid && now - last_sdk_probe >= 10.0)
            {
                last_sdk_probe = now;
                auto probe = run_bridge_command(config, bridge_path, "sdk_probe");
                log_probe_result(diagnostics, "sdk_probe", probe);
                if (probe.success)
                    sdk_probe_pid = process.pid;
            }
            if (config.auto_sdk_deep_probe && sdk_probe_pid == process.pid && sdk_deep_probe_pid != process.pid && now - last_sdk_deep_probe >= 10.0)
            {
                last_sdk_deep_probe = now;
                auto deep = run_bridge_command(config, bridge_path, "sdk_deep_probe");
                log_probe_result(diagnostics, "sdk_deep_probe", deep);
                if (deep.success)
                    sdk_deep_probe_pid = process.pid;
            }
            if (hotkey.consume())
            {
                diagnostics.event("f10_triggered", "info", "hotkey", "F10 trigger detected");
                const bool probe_required = config.auto_sdk_probe && sdk_probe_pid != process.pid;
                const bool deep_probe_required = config.auto_sdk_deep_probe && sdk_deep_probe_pid != process.pid;
                if (probe_required || deep_probe_required)
                {
                    diagnostics.event("paint_ignored_sdk_not_ready", "warning", "sdk_context_pending", "paint trigger ignored until SDK probes complete",
                                      std::string("{\"pid\":") + std::to_string(process.pid) +
                                      ",\"sdk_probe_ready\":" + (sdk_probe_pid == process.pid ? "true" : "false") +
                                      ",\"sdk_deep_probe_ready\":" + (sdk_deep_probe_pid == process.pid ? "true" : "false") + "}");
                }
                else
                {
                    run_paint(config, bridge_path, process, diagnostics);
                    diagnostics.event("waiting_for_hotkey", "info", "ready", "waiting for user to press F10");
                    waiting_for_hotkey_logged = true;
                }
            }
            if (now - last_idle >= 60.0)
            {
                last_idle = now;
            }
            Sleep(static_cast<DWORD>(std::max(1.0, config.frame_delay_ms)));
            ++frame;
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 2;
    }
    try
    {
        Config config = parse_config(argc, argv);
        const std::filesystem::path log_dir = config.log_dir.empty() ? default_log_dir() : std::filesystem::path(config.log_dir);
        Diagnostics diagnostics(log_dir);
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
                WSACleanup();
                return 1;
            }
            BridgeClient client(config.bridge_host, config.bridge_port, config.bridge_timeout_seconds);
            auto response = client.request("shutdown");
            diagnostics.event(response.success ? "bridge_shutdown" : "bridge_shutdown_failed",
                              response.success ? "info" : "warning",
                              response.stage,
                              response.message.empty() ? response.transport_error : response.message,
                              std::string("{\"response\":") + (response.raw.empty() ? "{}" : response.raw) + "}");
            WSACleanup();
            return response.success ? 0 : 1;
        }
        if (config.bridge_port <= 0)
            config.bridge_port = choose_bridge_port(config.bridge_host);
        const auto bridge_path = extract_embedded_bridge(log_dir, config.bridge_port);
        write_bridge_port_file(bridge_path, config.bridge_port);
        if (config.mode == L"apply")
        {
            const int code = run_apply_once(config, bridge_path, diagnostics, true);
            WSACleanup();
            return code;
        }
        if (config.mode == L"probe")
        {
            const int code = run_apply_once(config, bridge_path, diagnostics, false);
            WSACleanup();
            return code;
        }
        const int code = run_service(config, bridge_path, diagnostics);
        WSACleanup();
        return code;
    }
    catch (const std::exception& exc)
    {
        std::cerr << "fatal: " << exc.what() << "\n";
        WSACleanup();
        return 1;
    }
}
