#include <meccha/application/config_storage_win32.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::application
{
namespace
{
namespace fs = std::filesystem;

class FileHandle
{
public:
    explicit FileHandle(HANDLE handle)
        : handle_{handle}
    {
    }

    FileHandle(const FileHandle&) = delete;
    auto operator=(const FileHandle&) -> FileHandle& = delete;

    ~FileHandle()
    {
        reset();
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return handle_;
    }

    auto reset() -> void
    {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
        handle_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_{INVALID_HANDLE_VALUE};
};

class TemporaryFileGuard
{
public:
    explicit TemporaryFileGuard(fs::path path)
        : path_{std::move(path)}
    {
    }

    TemporaryFileGuard(const TemporaryFileGuard&) = delete;
    auto operator=(const TemporaryFileGuard&)
        -> TemporaryFileGuard& = delete;

    ~TemporaryFileGuard()
    {
        if (active_)
        {
            DeleteFileW(path_.c_str());
        }
    }

    auto activate() -> void
    {
        active_ = true;
    }

    auto release() -> void
    {
        active_ = false;
    }

private:
    fs::path path_{};
    bool active_{};
};

auto error(
    TextStorageErrorCode code,
    std::string detail) -> TextStorageError
{
    return TextStorageError{code, std::move(detail)};
}

auto windows_error(
    std::string_view operation,
    DWORD code = GetLastError()) -> TextStorageError
{
    return error(
        TextStorageErrorCode::Io,
        std::string{operation} + " Windows error " +
            std::to_string(code) + ".");
}

auto attributes(const fs::path& path)
    -> std::expected<std::optional<DWORD>, TextStorageError>
{
    const auto value = GetFileAttributesW(path.c_str());
    if (value != INVALID_FILE_ATTRIBUTES)
    {
        return value;
    }
    const auto code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND ||
        code == ERROR_PATH_NOT_FOUND)
    {
        return std::nullopt;
    }
    return std::unexpected(
        windows_error("Could not inspect configuration path.", code));
}

auto validate_directory(
    const fs::path& path,
    DWORD value) -> std::expected<void, TextStorageError>
{
    if (!(value & FILE_ATTRIBUTE_DIRECTORY) ||
        (value & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return std::unexpected(error(
            TextStorageErrorCode::Conflict,
            "Managed configuration directory is not a plain directory: " +
                path.string()));
    }
    return {};
}

auto ensure_directory(
    const fs::path& path,
    bool create) -> std::expected<bool, TextStorageError>
{
    auto value = attributes(path);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        if (!create)
        {
            return false;
        }
        if (!CreateDirectoryW(path.c_str(), nullptr))
        {
            const auto code = GetLastError();
            if (code != ERROR_ALREADY_EXISTS)
            {
                return std::unexpected(windows_error(
                    "Could not create configuration directory.",
                    code));
            }
        }
        value = attributes(path);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!*value)
        {
            return std::unexpected(error(
                TextStorageErrorCode::Io,
                "Created configuration directory is missing."));
        }
    }
    auto valid = validate_directory(path, **value);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    return true;
}

auto ensure_root(
    const fs::path& local_app_data,
    const fs::path& root,
    bool create) -> std::expected<bool, TextStorageError>
{
    auto base = attributes(local_app_data);
    if (!base)
    {
        return std::unexpected(base.error());
    }
    if (!*base || !(**base & FILE_ATTRIBUTE_DIRECTORY))
    {
        return std::unexpected(error(
            TextStorageErrorCode::Io,
            "Local application data directory is missing."));
    }

    const auto product = local_app_data / "MecchaCamouflage";
    auto product_ready = ensure_directory(product, create);
    if (!product_ready || !*product_ready)
    {
        return product_ready;
    }
    auto root_ready = ensure_directory(root, create);
    if (!root_ready)
    {
        return std::unexpected(root_ready.error());
    }
    return *root_ready;
}

auto relative_file(std::string_view name)
    -> std::expected<fs::path, TextStorageError>
{
    if (name.empty() ||
        name.find('\0') != std::string_view::npos)
    {
        return std::unexpected(error(
            TextStorageErrorCode::Conflict,
            "Configuration file name is invalid."));
    }
    const auto path = fs::path{std::string{name}};
    if (path.is_absolute() || path.has_root_name() ||
        path.has_root_directory() || path.filename().empty() ||
        path.filename() == "." || path.filename() == ".." ||
        path != path.filename())
    {
        return std::unexpected(error(
            TextStorageErrorCode::Conflict,
            "Configuration file name must be one safe component."));
    }
    return path;
}

auto reject_unsafe_file(
    const fs::path& path,
    const std::optional<DWORD>& value)
    -> std::expected<void, TextStorageError>
{
    if (value &&
        ((*value & FILE_ATTRIBUTE_DIRECTORY) ||
         (*value & FILE_ATTRIBUTE_REPARSE_POINT)))
    {
        return std::unexpected(error(
            TextStorageErrorCode::Conflict,
            "Configuration path is a directory or reparse point: " +
                path.string()));
    }
    return {};
}
} // namespace

auto resolve_local_app_data()
    -> std::expected<std::filesystem::path, TextStorageError>
{
    PWSTR value{};
    const auto result = SHGetKnownFolderPath(
        FOLDERID_LocalAppData,
        KF_FLAG_DEFAULT,
        nullptr,
        &value);
    if (FAILED(result) || value == nullptr)
    {
        if (value != nullptr)
        {
            CoTaskMemFree(value);
        }
        return std::unexpected(error(
            TextStorageErrorCode::Io,
            "Windows LocalAppData could not be resolved."));
    }
    auto path = fs::path{value};
    CoTaskMemFree(value);
    return path;
}

Win32AtomicTextStorage::Win32AtomicTextStorage(
    std::filesystem::path local_app_data)
    : local_app_data_{std::move(local_app_data)},
      root_{v2_data_root(local_app_data_)}
{
}

auto Win32AtomicTextStorage::root() const
    -> const std::filesystem::path&
{
    return root_;
}

auto Win32AtomicTextStorage::read_text(
    std::string_view name,
    std::size_t maximum_bytes)
    -> std::expected<std::optional<std::string>, TextStorageError>
{
    const auto relative = relative_file(name);
    if (!relative)
    {
        return std::unexpected(relative.error());
    }
    auto root_ready = ensure_root(local_app_data_, root_, false);
    if (!root_ready)
    {
        return std::unexpected(root_ready.error());
    }
    if (!*root_ready)
    {
        return std::nullopt;
    }

    const auto path = root_ / *relative;
    auto value = attributes(path);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        return std::nullopt;
    }
    auto safe = reject_unsafe_file(path, *value);
    if (!safe)
    {
        return std::unexpected(safe.error());
    }

    auto file = FileHandle{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(
            windows_error("Could not open configuration file."));
    }

    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(
            file.get(),
            FileAttributeTagInfo,
            &tag,
            sizeof(tag)))
    {
        return std::unexpected(
            windows_error("Could not verify configuration file."));
    }
    if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return std::unexpected(error(
            TextStorageErrorCode::Conflict,
            "Opened configuration is not a plain file."));
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0)
    {
        return std::unexpected(
            windows_error("Could not measure configuration file."));
    }
    if (static_cast<std::uint64_t>(size.QuadPart) >
        maximum_bytes)
    {
        return std::unexpected(error(
            TextStorageErrorCode::TooLarge,
            "Configuration file exceeds its byte limit."));
    }

    auto result =
        std::string(static_cast<std::size_t>(size.QuadPart), '\0');
    auto remaining = std::span<char>{result};
    while (!remaining.empty())
    {
        const auto chunk = std::min(
            remaining.size(),
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max()));
        DWORD read{};
        if (!ReadFile(
                file.get(),
                remaining.data(),
                static_cast<DWORD>(chunk),
                &read,
                nullptr) ||
            read == 0U)
        {
            return std::unexpected(
                windows_error("Could not read configuration file."));
        }
        remaining = remaining.subspan(read);
    }
    return std::optional<std::string>{std::move(result)};
}

auto Win32AtomicTextStorage::write_text_atomic(
    std::string_view name,
    std::string_view text)
    -> std::expected<void, TextStorageError>
{
    if (text.size() >
        static_cast<std::size_t>(
            std::numeric_limits<DWORD>::max()))
    {
        return std::unexpected(error(
            TextStorageErrorCode::TooLarge,
            "Configuration write exceeds the Windows I/O limit."));
    }
    const auto relative = relative_file(name);
    if (!relative)
    {
        return std::unexpected(relative.error());
    }
    auto root_ready = ensure_root(local_app_data_, root_, true);
    if (!root_ready)
    {
        return std::unexpected(root_ready.error());
    }

    const auto target = root_ / *relative;
    auto target_value = attributes(target);
    if (!target_value)
    {
        return std::unexpected(target_value.error());
    }
    auto target_safe = reject_unsafe_file(target, *target_value);
    if (!target_safe)
    {
        return std::unexpected(target_safe.error());
    }

    auto temporary = target;
    temporary += L".tmp";
    auto temporary_value = attributes(temporary);
    if (!temporary_value)
    {
        return std::unexpected(temporary_value.error());
    }
    auto temporary_safe =
        reject_unsafe_file(temporary, *temporary_value);
    if (!temporary_safe)
    {
        return std::unexpected(temporary_safe.error());
    }
    if (*temporary_value &&
        !DeleteFileW(temporary.c_str()))
    {
        return std::unexpected(windows_error(
            "Could not remove interrupted configuration staging file."));
    }

    auto cleanup = TemporaryFileGuard{temporary};
    auto file = FileHandle{CreateFileW(
        temporary.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(windows_error(
            "Could not create configuration staging file."));
    }
    cleanup.activate();

    auto remaining = std::span{text};
    while (!remaining.empty())
    {
        const auto chunk = std::min(
            remaining.size(),
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max()));
        DWORD written{};
        if (!WriteFile(
                file.get(),
                remaining.data(),
                static_cast<DWORD>(chunk),
                &written,
                nullptr) ||
            static_cast<std::size_t>(written) != chunk)
        {
            return std::unexpected(windows_error(
                "Could not write configuration staging file."));
        }
        remaining = remaining.subspan(written);
    }
    if (!FlushFileBuffers(file.get()))
    {
        return std::unexpected(windows_error(
            "Could not flush configuration staging file."));
    }
    file.reset();

    if (!MoveFileExW(
            temporary.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return std::unexpected(windows_error(
            "Could not publish configuration file."));
    }
    cleanup.release();
    return {};
}
} // namespace meccha::application
