#include <meccha/launcher/game_discovery.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::launcher
{
namespace
{
namespace fs = std::filesystem;

class UniqueHandle
{
public:
    explicit UniqueHandle(HANDLE handle) : handle_{handle}
    {
    }

    UniqueHandle(const UniqueHandle&) = delete;
    auto operator=(const UniqueHandle&) -> UniqueHandle& = delete;

    ~UniqueHandle()
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return handle_;
    }

private:
    HANDLE handle_{};
};

template <class T>
class ComObject
{
public:
    ComObject() = default;
    ComObject(const ComObject&) = delete;
    auto operator=(const ComObject&) -> ComObject& = delete;

    ~ComObject()
    {
        if (value_ != nullptr)
        {
            value_->Release();
        }
    }

    [[nodiscard]] auto put() -> T**
    {
        return &value_;
    }

    [[nodiscard]] auto get() const -> T*
    {
        return value_;
    }

private:
    T* value_{};
};

class ComInitialization
{
public:
    ComInitialization()
        : result_{CoInitializeEx(
              nullptr,
              COINIT_APARTMENTTHREADED |
                  COINIT_DISABLE_OLE1DDE)},
          owns_{result_ == S_OK || result_ == S_FALSE}
    {
    }

    ComInitialization(const ComInitialization&) = delete;
    auto operator=(const ComInitialization&)
        -> ComInitialization& = delete;

    ~ComInitialization()
    {
        if (owns_)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] auto result() const -> HRESULT
    {
        return result_;
    }

private:
    HRESULT result_{};
    bool owns_{};
};

auto win32_error(
    GameDiscoveryErrorCode code,
    std::string detail,
    LSTATUS status = ERROR_SUCCESS)
    -> std::unexpected<GameDiscoveryError>
{
    if (status != ERROR_SUCCESS)
    {
        detail += " (Win32 ";
        detail += std::to_string(status);
        detail += ')';
    }
    return std::unexpected(GameDiscoveryError{code, std::move(detail)});
}

auto read_registry_path(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* value_name)
    -> std::expected<std::optional<fs::path>, GameDiscoveryError>
{
    DWORD bytes{};
    const auto size_status = RegGetValueW(
        root,
        subkey,
        value_name,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
        nullptr,
        nullptr,
        &bytes);
    if (size_status == ERROR_FILE_NOT_FOUND ||
        size_status == ERROR_PATH_NOT_FOUND)
    {
        return std::optional<fs::path>{};
    }
    if (size_status != ERROR_SUCCESS || bytes < sizeof(wchar_t))
    {
        return win32_error(
            GameDiscoveryErrorCode::Registry,
            "Steam registry value size could not be read",
            size_status);
    }

    auto buffer = std::vector<wchar_t>(
        static_cast<std::size_t>(bytes / sizeof(wchar_t)) + 1U,
        L'\0');
    auto read_bytes = bytes;
    const auto read_status = RegGetValueW(
        root,
        subkey,
        value_name,
        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
        nullptr,
        buffer.data(),
        &read_bytes);
    if (read_status != ERROR_SUCCESS)
    {
        return win32_error(
            GameDiscoveryErrorCode::Registry,
            "Steam registry value could not be read",
            read_status);
    }
    const auto terminator = std::ranges::find(buffer, L'\0');
    const auto length = static_cast<std::size_t>(
        std::distance(buffer.begin(), terminator));
    if (length == 0U || terminator == buffer.end())
    {
        return win32_error(
            GameDiscoveryErrorCode::Registry,
            "Steam registry path is empty or unterminated");
    }
    return std::optional<fs::path>{fs::path{
        std::wstring_view{buffer.data(), length}}};
}

auto read_registry_dword(
    HKEY root,
    const wchar_t* subkey,
    const wchar_t* value_name)
    -> std::expected<std::optional<std::uint32_t>, GameDiscoveryError>
{
    DWORD value{};
    DWORD bytes{sizeof(value)};
    const auto status = RegGetValueW(
        root,
        subkey,
        value_name,
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &bytes);
    if (status == ERROR_FILE_NOT_FOUND ||
        status == ERROR_PATH_NOT_FOUND)
    {
        return std::optional<std::uint32_t>{};
    }
    if (status != ERROR_SUCCESS || bytes != sizeof(value))
    {
        return win32_error(
            GameDiscoveryErrorCode::Registry,
            "Steam registry DWORD could not be read",
            status);
    }
    return std::optional<std::uint32_t>{value};
}

auto hresult_error(
    GameDiscoveryErrorCode code,
    std::string detail,
    HRESULT result)
    -> std::unexpected<GameDiscoveryError>
{
    detail += " (HRESULT ";
    detail += std::to_string(static_cast<long>(result));
    detail += ')';
    return std::unexpected(
        GameDiscoveryError{code, std::move(detail)});
}

auto append_unique(
    std::vector<fs::path>& paths,
    const fs::path& candidate) -> void
{
    std::error_code error{};
    const auto canonical = fs::canonical(candidate, error);
    if (error)
    {
        return;
    }
    for (const auto& existing : paths)
    {
        if (_wcsicmp(
                existing.c_str(),
                canonical.c_str()) == 0)
        {
            return;
        }
    }
    paths.push_back(canonical);
}
} // namespace

auto discover_windows_steam_roots()
    -> std::expected<std::vector<fs::path>, GameDiscoveryError>
{
    struct RegistryCandidate
    {
        HKEY root{};
        const wchar_t* subkey{};
        const wchar_t* value{};
    };
    const RegistryCandidate candidates[]{
        {
            HKEY_CURRENT_USER,
            L"Software\\Valve\\Steam",
            L"SteamPath",
        },
        {
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
            L"InstallPath",
        },
        {
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Valve\\Steam",
            L"InstallPath",
        },
    };

    auto roots = std::vector<fs::path>{};
    for (const auto& candidate : candidates)
    {
        const auto path = read_registry_path(
            candidate.root,
            candidate.subkey,
            candidate.value);
        if (!path)
        {
            return std::unexpected(path.error());
        }
        if (*path)
        {
            append_unique(roots, **path);
        }
    }
    if (roots.empty())
    {
        return win32_error(
            GameDiscoveryErrorCode::Registry,
            "Steam was not found in the supported registry locations");
    }
    return roots;
}

auto read_windows_active_steam_launch_options()
    -> std::expected<std::optional<std::string>, GameDiscoveryError>
{
    const auto steam_path = read_registry_path(
        HKEY_CURRENT_USER,
        L"Software\\Valve\\Steam",
        L"SteamPath");
    if (!steam_path)
    {
        return std::unexpected(steam_path.error());
    }
    const auto active_user = read_registry_dword(
        HKEY_CURRENT_USER,
        L"Software\\Valve\\Steam\\ActiveProcess",
        L"ActiveUser");
    if (!active_user)
    {
        return std::unexpected(active_user.error());
    }
    if (!*steam_path || !*active_user || **active_user == 0U)
    {
        return win32_error(
            GameDiscoveryErrorCode::Registry,
            "The active Steam user could not be resolved");
    }

    std::error_code canonical_error{};
    const auto root = fs::canonical(
        **steam_path,
        canonical_error);
    if (canonical_error)
    {
        return win32_error(
            GameDiscoveryErrorCode::Io,
            "The active Steam user's root is unavailable");
    }
    const auto local_config =
        root / "userdata" / std::to_string(**active_user) /
        "config" / "localconfig.vdf";
    return read_steam_launch_options_file(local_config);
}

auto pick_windows_game_installation()
    -> std::expected<GameInstallation, GameDiscoveryError>
{
    const ComInitialization com{};
    if (FAILED(com.result()) &&
        com.result() != RPC_E_CHANGED_MODE)
    {
        return hresult_error(
            GameDiscoveryErrorCode::Io,
            "COM could not be initialized for the folder picker",
            com.result());
    }

    ComObject<IFileOpenDialog> dialog{};
    auto result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(dialog.put()));
    if (FAILED(result))
    {
        return hresult_error(
            GameDiscoveryErrorCode::Io,
            "The Windows folder picker could not be created",
            result);
    }
    FILEOPENDIALOGOPTIONS options{};
    result = dialog.get()->GetOptions(&options);
    if (FAILED(result))
    {
        return hresult_error(
            GameDiscoveryErrorCode::Io,
            "The Windows folder picker options could not be read",
            result);
    }
    result = dialog.get()->SetOptions(
        options |
        FOS_PICKFOLDERS |
        FOS_FORCEFILESYSTEM |
        FOS_PATHMUSTEXIST);
    if (FAILED(result))
    {
        return hresult_error(
            GameDiscoveryErrorCode::Io,
            "The Windows folder picker options could not be set",
            result);
    }
    result = dialog.get()->Show(nullptr);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        return hresult_error(
            GameDiscoveryErrorCode::UserCancelled,
            "Game folder selection was cancelled",
            result);
    }
    if (FAILED(result))
    {
        return hresult_error(
            GameDiscoveryErrorCode::Io,
            "The Windows folder picker failed",
            result);
    }

    ComObject<IShellItem> selection{};
    result = dialog.get()->GetResult(selection.put());
    if (FAILED(result))
    {
        return hresult_error(
            GameDiscoveryErrorCode::Io,
            "The selected game folder could not be read",
            result);
    }
    PWSTR selected_path{};
    result = selection.get()->GetDisplayName(
        SIGDN_FILESYSPATH,
        &selected_path);
    if (FAILED(result) || selected_path == nullptr)
    {
        if (selected_path != nullptr)
        {
            CoTaskMemFree(selected_path);
        }
        return hresult_error(
            GameDiscoveryErrorCode::Io,
            "The selected game folder is not a filesystem path",
            result);
    }
    const auto path = fs::path{selected_path};
    CoTaskMemFree(selected_path);
    return validate_game_directory(path);
}

auto is_process_running_by_image_name(std::wstring_view image_name)
    -> std::expected<bool, GameDiscoveryError>
{
    if (image_name.empty() ||
        image_name.find_first_of(L"\\/") != std::wstring_view::npos)
    {
        return win32_error(
            GameDiscoveryErrorCode::ProcessEnumeration,
            "process image name is invalid");
    }

    const auto expected_name = std::wstring{image_name};
    const UniqueHandle snapshot{
        CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.get() == INVALID_HANDLE_VALUE)
    {
        return win32_error(
            GameDiscoveryErrorCode::ProcessEnumeration,
            "process snapshot could not be created",
            static_cast<LSTATUS>(GetLastError()));
    }

    auto entry = PROCESSENTRY32W{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.get(), &entry) == FALSE)
    {
        return win32_error(
            GameDiscoveryErrorCode::ProcessEnumeration,
            "process snapshot could not be enumerated",
            static_cast<LSTATUS>(GetLastError()));
    }
    do
    {
        if (_wcsicmp(entry.szExeFile, expected_name.c_str()) == 0)
        {
            return true;
        }
    } while (Process32NextW(snapshot.get(), &entry) != FALSE);

    const auto last_error = GetLastError();
    if (last_error != ERROR_NO_MORE_FILES)
    {
        return win32_error(
            GameDiscoveryErrorCode::ProcessEnumeration,
            "process snapshot enumeration failed",
            static_cast<LSTATUS>(last_error));
    }
    return false;
}

auto is_target_game_running()
    -> std::expected<bool, GameDiscoveryError>
{
    return is_process_running_by_image_name(
        L"PenguinHotel-Win64-Shipping.exe");
}
} // namespace meccha::launcher
