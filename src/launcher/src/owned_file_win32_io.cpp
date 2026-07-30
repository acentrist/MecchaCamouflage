#include "owned_file_win32_io.hpp"

#include <meccha/launcher/hash.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace meccha::launcher::detail
{
namespace
{
namespace fs = std::filesystem;

constexpr std::size_t MaximumReceiptBytes = 64U * 1024U;
constexpr std::string_view AtomicSuffix{".meccha-next"};

class FileHandle
{
public:
    explicit FileHandle(HANDLE value) : value_(value) {}
    FileHandle(const FileHandle&) = delete;
    auto operator=(const FileHandle&) -> FileHandle& = delete;

    ~FileHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE)
        {
            static_cast<void>(CloseHandle(value_));
        }
    }

    [[nodiscard]] auto get() const noexcept -> HANDLE
    {
        return value_;
    }

private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

auto error(OwnedFileStoreErrorCode code, std::string detail)
    -> std::unexpected<OwnedFileStoreError>
{
    return std::unexpected(
        OwnedFileStoreError{code, std::move(detail)});
}

auto windows_error(std::string detail)
    -> std::unexpected<OwnedFileStoreError>
{
    return error(
        OwnedFileStoreErrorCode::Io,
        std::move(detail) + " (Windows error " +
            std::to_string(GetLastError()) + ")");
}

auto attributes(const fs::path& path)
    -> std::expected<std::optional<DWORD>, OwnedFileStoreError>
{
    const auto value = GetFileAttributesW(path.c_str());
    if (value == INVALID_FILE_ATTRIBUTES)
    {
        const auto last_error = GetLastError();
        if (last_error == ERROR_FILE_NOT_FOUND ||
            last_error == ERROR_PATH_NOT_FOUND)
        {
            return std::nullopt;
        }
        return windows_error("Could not inspect an owned-file path.");
    }
    return value;
}

auto read_plain_text(const fs::path& path)
    -> std::expected<std::optional<std::string>, OwnedFileStoreError>
{
    const auto value = attributes(path);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        return std::nullopt;
    }
    if ((**value & FILE_ATTRIBUTE_DIRECTORY) ||
        (**value & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file receipt is a directory or reparse point.");
    }

    FileHandle file{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return windows_error("Could not open an owned-file receipt.");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) >
            MaximumReceiptBytes)
    {
        return error(
            OwnedFileStoreErrorCode::InvalidData,
            "Owned-file receipt size is invalid.");
    }
    std::string result(
        static_cast<std::size_t>(size.QuadPart),
        '\0');
    auto remaining = std::span<char>{result};
    while (!remaining.empty())
    {
        DWORD read{};
        const auto chunk = std::min(
            remaining.size(),
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max()));
        if (!ReadFile(
                file.get(),
                remaining.data(),
                static_cast<DWORD>(chunk),
                &read,
                nullptr) ||
            read == 0)
        {
            return windows_error(
                "Could not read an owned-file receipt.");
        }
        remaining = remaining.subspan(read);
    }
    return result;
}
} // namespace

auto owned_file_staging_path(const fs::path& target) -> fs::path
{
    auto staging = target;
    staging += AtomicSuffix;
    return staging;
}

auto validate_absolute_file_path(
    const fs::path& path,
    std::string_view label)
    -> std::expected<void, OwnedFileStoreError>
{
    if (!path.is_absolute() || path.lexically_normal() != path ||
        path.filename().empty())
    {
        return error(
            OwnedFileStoreErrorCode::InvalidRequest,
            std::string{label} +
                " must be an absolute normalized file path.");
    }
    return {};
}

auto require_plain_directory_tree(const fs::path& path)
    -> std::expected<void, OwnedFileStoreError>
{
    auto current = path.root_path();
    for (const auto& component : path.relative_path())
    {
        current /= component;
        const auto value = attributes(current);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!*value || !(**value & FILE_ATTRIBUTE_DIRECTORY) ||
            (**value & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "Owned-file path traverses a missing, non-directory, "
                "or reparse entry: " +
                    current.string());
        }
    }
    return {};
}

auto inspect_plain_directory_tree(const fs::path& path)
    -> std::expected<bool, OwnedFileStoreError>
{
    auto current = path.root_path();
    auto missing = false;
    for (const auto& component : path.relative_path())
    {
        current /= component;
        if (missing)
        {
            continue;
        }
        const auto value = attributes(current);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!*value)
        {
            missing = true;
            continue;
        }
        if (!(**value & FILE_ATTRIBUTE_DIRECTORY) ||
            (**value & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "Ownership path traverses a non-directory or reparse "
                "entry: " +
                    current.string());
        }
    }
    return !missing;
}

auto ensure_plain_directory_tree(const fs::path& path)
    -> std::expected<void, OwnedFileStoreError>
{
    auto current = path.root_path();
    for (const auto& component : path.relative_path())
    {
        current /= component;
        auto value = attributes(current);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!*value)
        {
            if (!CreateDirectoryW(current.c_str(), nullptr))
            {
                return windows_error(
                    "Could not create an ownership directory.");
            }
            value = attributes(current);
            if (!value)
            {
                return std::unexpected(value.error());
            }
        }
        if (!(**value & FILE_ATTRIBUTE_DIRECTORY) ||
            (**value & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "Ownership path traverses a non-directory or reparse "
                "entry: " +
                    current.string());
        }
    }
    return {};
}

auto measure_plain_file(const fs::path& path)
    -> std::expected<
        std::optional<FileMeasurement>,
        OwnedFileStoreError>
{
    const auto value = attributes(path);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        return std::nullopt;
    }
    if ((**value & FILE_ATTRIBUTE_DIRECTORY) ||
        (**value & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Owned-file target is a directory or reparse point: " +
                path.string());
    }
    const auto measured = sha256_file(path);
    if (!measured)
    {
        return error(
            OwnedFileStoreErrorCode::Io,
            "Could not hash owned-file target: " + path.string());
    }
    return FileMeasurement{measured->size, measured->sha256};
}

auto delete_plain_file(const fs::path& path)
    -> std::expected<void, OwnedFileStoreError>
{
    const auto value = attributes(path);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    if (!*value)
    {
        return {};
    }
    if ((**value & FILE_ATTRIBUTE_DIRECTORY) ||
        (**value & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        return error(
            OwnedFileStoreErrorCode::Conflict,
            "Refusing to delete a directory or reparse point.");
    }
    if (!DeleteFileW(path.c_str()))
    {
        return windows_error("Could not delete an owned file.");
    }
    return {};
}

auto write_new_durable(
    const fs::path& path,
    std::span<const std::byte> bytes)
    -> std::expected<void, OwnedFileStoreError>
{
    FileHandle file{CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr)};
    if (file.get() == INVALID_HANDLE_VALUE)
    {
        return windows_error("Could not create an owned file.");
    }
    auto remaining = bytes;
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
            written != chunk)
        {
            return windows_error("Could not write an owned file.");
        }
        remaining = remaining.subspan(written);
    }
    if (!FlushFileBuffers(file.get()))
    {
        return windows_error("Could not flush an owned file.");
    }
    return {};
}

auto publish_staged_file(
    const fs::path& staging,
    const fs::path& target,
    bool replace_existing)
    -> std::expected<void, OwnedFileStoreError>
{
    const auto flags =
        MOVEFILE_WRITE_THROUGH |
        (replace_existing ? MOVEFILE_REPLACE_EXISTING : 0U);
    if (!MoveFileExW(staging.c_str(), target.c_str(), flags))
    {
        return windows_error(
            "Could not publish an owned-file target.");
    }
    return {};
}

auto write_owned_file_receipt(
    const fs::path& path,
    const OwnedFileReceipt& receipt)
    -> std::expected<void, OwnedFileStoreError>
{
    const auto json = serialize_owned_file_receipt(receipt);
    if (!json)
    {
        return std::unexpected(json.error());
    }
    const auto temporary = owned_file_staging_path(path);
    auto written = write_new_durable(
        temporary,
        std::as_bytes(std::span{*json}));
    if (!written)
    {
        return std::unexpected(written.error());
    }
    if (!MoveFileExW(
            temporary.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return windows_error(
            "Could not publish an owned-file receipt.");
    }
    return {};
}

auto read_owned_file_receipt(
    const fs::path& path,
    std::string_view manifest_path,
    FileRole role,
    bool discard_atomic_temporary)
    -> std::expected<
        std::optional<OwnedFileReceipt>,
        OwnedFileStoreError>
{
    const auto temporary = owned_file_staging_path(path);
    auto temporary_text = read_plain_text(temporary);
    if (!temporary_text)
    {
        return std::unexpected(temporary_text.error());
    }
    if (*temporary_text)
    {
        const auto parsed = parse_owned_file_receipt(
            **temporary_text,
            manifest_path,
            role);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        if (!discard_atomic_temporary)
        {
            return error(
                OwnedFileStoreErrorCode::Conflict,
                "Owned-file receipt recovery is required.");
        }
        auto removed = delete_plain_file(temporary);
        if (!removed)
        {
            return std::unexpected(removed.error());
        }
    }

    auto text = read_plain_text(path);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    if (!*text)
    {
        return std::nullopt;
    }
    auto parsed = parse_owned_file_receipt(
        **text,
        manifest_path,
        role);
    if (!parsed)
    {
        return std::unexpected(parsed.error());
    }
    return *parsed;
}
} // namespace meccha::launcher::detail
