#include "atomic_file_win32.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
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
#include <vector>

namespace meccha::application::detail
{
namespace
{
namespace fs = std::filesystem;

constexpr auto StorageMutexName =
    L"Local\\MecchaCamouflage-v2-managed-storage";
constexpr auto StorageMutexTimeoutMilliseconds = DWORD{10'000U};
constexpr auto StagingAttempts = std::size_t{8U};

class Handle
{
public:
    explicit Handle(HANDLE value)
        : value_{value}
    {
    }

    Handle(const Handle&) = delete;
    auto operator=(const Handle&) -> Handle& = delete;

    Handle(Handle&& other) noexcept
        : value_{std::exchange(
              other.value_,
              INVALID_HANDLE_VALUE)}
    {
    }

    auto operator=(Handle&& other) noexcept -> Handle&
    {
        if (this != &other)
        {
            reset();
            value_ = std::exchange(
                other.value_,
                INVALID_HANDLE_VALUE);
        }
        return *this;
    }

    ~Handle()
    {
        reset();
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return value_;
    }

    auto reset() -> void
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value_);
        }
        value_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

class StorageLock
{
public:
    StorageLock() = default;
    StorageLock(const StorageLock&) = delete;
    auto operator=(const StorageLock&) -> StorageLock& = delete;

    ~StorageLock()
    {
        if (locked_)
        {
            ReleaseMutex(handle_.get());
        }
    }

    [[nodiscard]] auto acquire()
        -> std::expected<void, ManagedFileError>;

private:
    Handle handle_{INVALID_HANDLE_VALUE};
    bool locked_{};
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

class FindHandle
{
public:
    explicit FindHandle(HANDLE value)
        : value_{value}
    {
    }

    FindHandle(const FindHandle&) = delete;
    auto operator=(const FindHandle&) -> FindHandle& = delete;

    ~FindHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE)
        {
            FindClose(value_);
        }
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return value_;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

auto error(
    ManagedFileErrorCode code,
    std::string detail) -> ManagedFileError
{
    return ManagedFileError{code, std::move(detail)};
}

auto windows_error(
    std::string_view operation,
    DWORD code = GetLastError()) -> ManagedFileError
{
    return error(
        ManagedFileErrorCode::Io,
        std::string{operation} + " Windows error " +
            std::to_string(code) + ".");
}

auto StorageLock::acquire()
    -> std::expected<void, ManagedFileError>
{
    handle_ = Handle{CreateMutexW(
        nullptr,
        FALSE,
        StorageMutexName)};
    if (handle_.get() == nullptr ||
        handle_.get() == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(
            windows_error("Could not create the storage mutex."));
    }
    const auto waited = WaitForSingleObject(
        handle_.get(),
        StorageMutexTimeoutMilliseconds);
    if (waited != WAIT_OBJECT_0 &&
        waited != WAIT_ABANDONED)
    {
        if (waited == WAIT_TIMEOUT)
        {
            return std::unexpected(error(
                ManagedFileErrorCode::Conflict,
                "Timed out waiting for another storage operation."));
        }
        return std::unexpected(
            windows_error("Could not acquire the storage mutex."));
    }
    locked_ = true;
    return {};
}

auto attributes(const fs::path& path)
    -> std::expected<std::optional<DWORD>, ManagedFileError>
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
        windows_error("Could not inspect a managed storage path.", code));
}

auto validate_directory(const fs::path&, DWORD value)
    -> std::expected<void, ManagedFileError>
{
    if (!(value & FILE_ATTRIBUTE_DIRECTORY) ||
        (value & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Conflict,
            "Managed storage path is not a plain directory."));
    }
    return {};
}

auto ensure_directory(const fs::path& path, bool create)
    -> std::expected<bool, ManagedFileError>
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
                    "Could not create a managed storage directory.",
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
                ManagedFileErrorCode::Io,
                "Created managed storage directory is missing."));
        }
    }
    auto valid = validate_directory(path, **value);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    return true;
}

auto ensure_area(
    const fs::path& local_app_data,
    ManagedFileArea area,
    bool create) -> std::expected<bool, ManagedFileError>
{
    auto base = attributes(local_app_data);
    if (!base)
    {
        return std::unexpected(base.error());
    }
    if (!*base || !(**base & FILE_ATTRIBUTE_DIRECTORY))
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Io,
            "Local application data directory is missing."));
    }

    const std::array managed{
        local_app_data / "MecchaCamouflage",
        local_app_data / "MecchaCamouflage" / "v2",
        local_app_data / "MecchaCamouflage" / "v2" /
            "image-projects",
    };
    const auto count =
        area == ManagedFileArea::ImageProjects ? 3U : 2U;
    for (auto index = std::size_t{}; index < count; ++index)
    {
        auto ready = ensure_directory(managed[index], create);
        if (!ready || !*ready)
        {
            return ready;
        }
    }
    return true;
}

auto safe_file_name(std::string_view name)
    -> std::expected<fs::path, ManagedFileError>
{
    const auto characters_safe =
        !name.empty() && name.size() <= 128U &&
        !name.starts_with('.') && !name.ends_with('.') &&
        !name.contains("..") &&
        std::ranges::all_of(
            name,
            [](unsigned char character)
            {
                return (character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') ||
                       character == '.' || character == '-' ||
                       character == '_';
            });
    if (!characters_safe)
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Conflict,
            "Managed file name is invalid."));
    }
    const auto path = fs::path{std::string{name}};
    if (path.is_absolute() || path.has_root_name() ||
        path.has_root_directory() || path.filename().empty() ||
        path != path.filename())
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Conflict,
            "Managed file name must be one safe component."));
    }
    return path;
}

auto reject_unsafe_file(
    const fs::path&,
    const std::optional<DWORD>& value)
    -> std::expected<void, ManagedFileError>
{
    if (value &&
        ((*value & FILE_ATTRIBUTE_DIRECTORY) ||
         (*value & FILE_ATTRIBUTE_REPARSE_POINT)))
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Conflict,
            "Managed path is a directory or reparse point."));
    }
    return {};
}

auto verify_open_plain_file(HANDLE file)
    -> std::expected<void, ManagedFileError>
{
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(
            file,
            FileAttributeTagInfo,
            &tag,
            sizeof(tag)))
    {
        return std::unexpected(
            windows_error("Could not verify a managed file."));
    }
    if ((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Conflict,
            "Opened managed path is not a plain file."));
    }
    return {};
}

auto guid_hex() -> std::expected<std::wstring, ManagedFileError>
{
    auto guid = GUID{};
    if (FAILED(CoCreateGuid(&guid)))
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Io,
            "Could not generate a unique staging identity."));
    }
    constexpr auto digits = std::wstring_view{L"0123456789abcdef"};
    const auto bytes = std::as_bytes(std::span{&guid, 1U});
    auto result = std::wstring{};
    result.reserve(bytes.size() * 2U);
    for (const auto byte : bytes)
    {
        const auto value = std::to_integer<std::uint8_t>(byte);
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0FU]);
    }
    return result;
}

auto staging_prefix(const fs::path& target) -> std::wstring
{
    return target.filename().wstring() + L".tmp.";
}

auto is_owned_staging_name(
    std::wstring_view file_name,
    std::wstring_view prefix) -> bool
{
    if (!file_name.starts_with(prefix) ||
        file_name.size() != prefix.size() + 32U)
    {
        return false;
    }
    return std::ranges::all_of(
        file_name.substr(prefix.size()),
        [](wchar_t character)
        {
            return (character >= L'0' && character <= L'9') ||
                   (character >= L'a' && character <= L'f');
        });
}

auto cleanup_interrupted_staging(const fs::path& target)
    -> std::expected<void, ManagedFileError>
{
    const auto prefix = staging_prefix(target);
    const auto pattern = target.parent_path() / (prefix + L"*");
    auto data = WIN32_FIND_DATAW{};
    auto find = FindHandle{
        FindFirstFileW(pattern.c_str(), &data)};
    if (find.get() == INVALID_HANDLE_VALUE)
    {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND)
        {
            return {};
        }
        return std::unexpected(windows_error(
            "Could not enumerate interrupted staging files.",
            code));
    }

    while (true)
    {
        const auto name = std::wstring_view{data.cFileName};
        if (is_owned_staging_name(name, prefix))
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes &
                 FILE_ATTRIBUTE_REPARSE_POINT))
            {
                return std::unexpected(error(
                    ManagedFileErrorCode::Conflict,
                    "Interrupted staging path is not a plain file."));
            }
            const auto path = target.parent_path() / data.cFileName;
            if (!DeleteFileW(path.c_str()))
            {
                return std::unexpected(windows_error(
                    "Could not remove an interrupted staging file."));
            }
        }

        if (!FindNextFileW(find.get(), &data))
        {
            const auto code = GetLastError();
            if (code == ERROR_NO_MORE_FILES)
            {
                break;
            }
            return std::unexpected(windows_error(
                "Could not continue staging-file enumeration.",
                code));
        }
    }
    return {};
}

auto create_staging_file(const fs::path& target)
    -> std::expected<std::pair<fs::path, HANDLE>, ManagedFileError>
{
    for (auto attempt = std::size_t{};
         attempt < StagingAttempts;
         ++attempt)
    {
        const auto suffix = guid_hex();
        if (!suffix)
        {
            return std::unexpected(suffix.error());
        }
        const auto path =
            target.parent_path() /
            (staging_prefix(target) + *suffix);
        const auto handle = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            return std::pair{path, handle};
        }
        const auto code = GetLastError();
        if (code != ERROR_FILE_EXISTS &&
            code != ERROR_ALREADY_EXISTS)
        {
            return std::unexpected(windows_error(
                "Could not create a unique staging file.",
                code));
        }
    }
    return std::unexpected(error(
        ManagedFileErrorCode::Conflict,
        "Could not reserve a unique staging file."));
}
} // namespace

auto managed_file_root(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area) -> std::filesystem::path
{
    auto root =
        local_app_data / "MecchaCamouflage" / "v2";
    if (area == ManagedFileArea::ImageProjects)
    {
        root /= "image-projects";
    }
    return root;
}

auto read_managed_file(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area,
    std::string_view name,
    std::size_t maximum_bytes)
    -> std::expected<
        std::optional<std::vector<std::byte>>,
        ManagedFileError>
{
    const auto relative = safe_file_name(name);
    if (!relative)
    {
        return std::unexpected(relative.error());
    }
    auto ready = ensure_area(local_app_data, area, false);
    if (!ready)
    {
        return std::unexpected(ready.error());
    }
    if (!*ready)
    {
        return std::nullopt;
    }

    const auto target =
        managed_file_root(local_app_data, area) / *relative;
    auto value = attributes(target);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        return std::nullopt;
    }
    auto safe = reject_unsafe_file(target, *value);
    if (!safe)
    {
        return std::unexpected(safe.error());
    }

    auto file = Handle{CreateFileW(
        target.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(
            windows_error("Could not open a managed file."));
    }
    auto verified = verify_open_plain_file(file.get());
    if (!verified)
    {
        return std::unexpected(verified.error());
    }

    auto size = LARGE_INTEGER{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0)
    {
        return std::unexpected(
            windows_error("Could not measure a managed file."));
    }
    const auto unsigned_size =
        static_cast<std::uint64_t>(size.QuadPart);
    if (unsigned_size > maximum_bytes ||
        unsigned_size >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()))
    {
        return std::unexpected(error(
            ManagedFileErrorCode::TooLarge,
            "Managed file exceeds its byte limit."));
    }

    auto bytes =
        std::vector<std::byte>(
            static_cast<std::size_t>(unsigned_size));
    auto remaining = std::span<std::byte>{bytes};
    while (!remaining.empty())
    {
        const auto chunk = std::min(
            remaining.size(),
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max()));
        auto read = DWORD{};
        if (!ReadFile(
                file.get(),
                remaining.data(),
                static_cast<DWORD>(chunk),
                &read,
                nullptr) ||
            read == 0U)
        {
            return std::unexpected(
                windows_error("Could not read a managed file."));
        }
        remaining = remaining.subspan(read);
    }
    return std::optional<std::vector<std::byte>>{
        std::move(bytes)};
}

auto write_managed_file_atomic(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area,
    std::string_view name,
    std::span<const std::byte> bytes)
    -> std::expected<void, ManagedFileError>
{
    const auto relative = safe_file_name(name);
    if (!relative)
    {
        return std::unexpected(relative.error());
    }
    auto lock = StorageLock{};
    auto acquired = lock.acquire();
    if (!acquired)
    {
        return std::unexpected(acquired.error());
    }
    auto ready = ensure_area(local_app_data, area, true);
    if (!ready)
    {
        return std::unexpected(ready.error());
    }
    if (!*ready)
    {
        return std::unexpected(error(
            ManagedFileErrorCode::Io,
            "Managed storage directory was not created."));
    }

    const auto target =
        managed_file_root(local_app_data, area) / *relative;
    auto target_value = attributes(target);
    if (!target_value)
    {
        return std::unexpected(target_value.error());
    }
    auto target_safe =
        reject_unsafe_file(target, *target_value);
    if (!target_safe)
    {
        return std::unexpected(target_safe.error());
    }
    auto cleaned = cleanup_interrupted_staging(target);
    if (!cleaned)
    {
        return std::unexpected(cleaned.error());
    }

    auto staging = create_staging_file(target);
    if (!staging)
    {
        return std::unexpected(staging.error());
    }
    auto cleanup = TemporaryFileGuard{staging->first};
    cleanup.activate();
    auto file = Handle{staging->second};

    auto remaining = bytes;
    while (!remaining.empty())
    {
        const auto chunk = std::min(
            remaining.size(),
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max()));
        auto written = DWORD{};
        if (!WriteFile(
                file.get(),
                remaining.data(),
                static_cast<DWORD>(chunk),
                &written,
                nullptr) ||
            static_cast<std::size_t>(written) != chunk)
        {
            return std::unexpected(
                windows_error("Could not write a staging file."));
        }
        remaining = remaining.subspan(written);
    }
    if (!FlushFileBuffers(file.get()))
    {
        return std::unexpected(
            windows_error("Could not flush a staging file."));
    }
    file.reset();

    target_value = attributes(target);
    if (!target_value)
    {
        return std::unexpected(target_value.error());
    }
    target_safe = reject_unsafe_file(target, *target_value);
    if (!target_safe)
    {
        return std::unexpected(target_safe.error());
    }
    if (!MoveFileExW(
            staging->first.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return std::unexpected(
            windows_error("Could not publish a managed file."));
    }
    cleanup.release();
    return {};
}

auto remove_managed_file(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area,
    std::string_view name)
    -> std::expected<bool, ManagedFileError>
{
    const auto relative = safe_file_name(name);
    if (!relative)
    {
        return std::unexpected(relative.error());
    }
    auto lock = StorageLock{};
    auto acquired = lock.acquire();
    if (!acquired)
    {
        return std::unexpected(acquired.error());
    }
    auto ready = ensure_area(local_app_data, area, false);
    if (!ready)
    {
        return std::unexpected(ready.error());
    }
    if (!*ready)
    {
        return false;
    }

    const auto target =
        managed_file_root(local_app_data, area) / *relative;
    auto cleaned = cleanup_interrupted_staging(target);
    if (!cleaned)
    {
        return std::unexpected(cleaned.error());
    }
    auto value = attributes(target);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        return false;
    }
    auto safe = reject_unsafe_file(target, *value);
    if (!safe)
    {
        return std::unexpected(safe.error());
    }
    if (!DeleteFileW(target.c_str()))
    {
        const auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND ||
            code == ERROR_PATH_NOT_FOUND)
        {
            return false;
        }
        return std::unexpected(
            windows_error("Could not remove a managed file.", code));
    }
    return true;
}
} // namespace meccha::application::detail
