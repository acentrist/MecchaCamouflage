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

    auto read_small_text_file_w(const std::wstring& path, std::size_t max_bytes, std::string& text) -> bool
    {
        text.clear();
        if (path.empty() || max_bytes == 0)
        {
            return false;
        }
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || static_cast<std::uint64_t>(size.QuadPart) > max_bytes)
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

    auto trim_ascii_whitespace(std::string text) -> std::string
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        {
            text.pop_back();
        }
        std::size_t first = 0;
        while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])))
        {
            ++first;
        }
        return first == 0 ? text : text.substr(first);
    }

    auto utf8_to_wstring(const std::string& text) -> std::wstring
    {
        if (text.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (size <= 0)
        {
            return {};
        }
        std::wstring wide(static_cast<std::size_t>(size), L'\0');
        const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
        if (written != size)
        {
            return {};
        }
        return wide;
    }

    auto bridge_progress_path() -> std::wstring
    {
        const auto configured_sidecar = bridge_sidecar_path(L".progress.path");
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
        return bridge_sidecar_path(L".progress.json");
    }

    auto write_bridge_progress(const std::string& stage,
                               const std::string& message,
                               int step,
                               int total_steps,
                               double elapsed_ms,
                               const std::string& extra = "") -> void
    {
        const auto path = bridge_progress_path();
        if (path.empty())
        {
            return;
        }
        const double progress = total_steps > 0 ? std::max(0.0, std::min(1.0, static_cast<double>(step) / static_cast<double>(total_steps))) : 0.0;
        std::string json = "{\"stage\":\"" + json_escape(stage) +
                           "\",\"message\":\"" + json_escape(message) +
                           "\",\"step\":" + std::to_string(step) +
                           ",\"total_steps\":" + std::to_string(total_steps) +
                           ",\"progress\":" + std::to_string(progress) +
                           ",\"elapsed_ms\":" + std::to_string(elapsed_ms);
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
        const auto path = bridge_progress_path();
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

    auto write_bridge_listener_status(const std::string& stage, int port, int error = 0, const std::string& detail = "") -> void
    {
        std::string json = "{\"stage\":\"" + json_escape(stage) +
                           "\",\"pid\":" + std::to_string(GetCurrentProcessId()) +
                           ",\"port\":" + std::to_string(port) +
                           ",\"tick_ms\":" + std::to_string(GetTickCount64()) +
                           ",\"error\":" + std::to_string(error);
        if (!detail.empty())
        {
            json += ",\"detail\":\"" + json_escape(detail) + "\"";
        }
        json += "}\n";
        write_bridge_sidecar_text(L".listen.json", json);
    }
