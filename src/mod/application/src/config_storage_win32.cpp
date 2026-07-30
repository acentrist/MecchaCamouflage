#include <meccha/application/config_storage_win32.hpp>

#include "atomic_file_win32.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::application
{
namespace
{
auto text_error(detail::ManagedFileError error)
    -> TextStorageError
{
    auto code = TextStorageErrorCode::Io;
    switch (error.code)
    {
    case detail::ManagedFileErrorCode::Io:
        code = TextStorageErrorCode::Io;
        break;
    case detail::ManagedFileErrorCode::Conflict:
        code = TextStorageErrorCode::Conflict;
        break;
    case detail::ManagedFileErrorCode::TooLarge:
        code = TextStorageErrorCode::TooLarge;
        break;
    }
    return TextStorageError{code, std::move(error.detail)};
}
} // namespace

auto resolve_local_app_data()
    -> std::expected<std::filesystem::path, TextStorageError>
{
    auto* value = static_cast<PWSTR>(nullptr);
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
        return std::unexpected(TextStorageError{
            TextStorageErrorCode::Io,
            "Windows LocalAppData could not be resolved.",
        });
    }
    auto path = std::filesystem::path{value};
    CoTaskMemFree(value);
    return path;
}

Win32AtomicTextStorage::Win32AtomicTextStorage(
    std::filesystem::path local_app_data)
    : local_app_data_{std::move(local_app_data)},
      root_{detail::managed_file_root(
          local_app_data_,
          detail::ManagedFileArea::V2Root)}
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
    auto bytes = detail::read_managed_file(
        local_app_data_,
        detail::ManagedFileArea::V2Root,
        name,
        maximum_bytes);
    if (!bytes)
    {
        return std::unexpected(text_error(bytes.error()));
    }
    if (!*bytes)
    {
        return std::nullopt;
    }
    if ((*bytes)->empty())
    {
        return std::optional<std::string>{std::string{}};
    }
    return std::optional<std::string>{std::string{
        reinterpret_cast<const char*>((*bytes)->data()),
        (*bytes)->size(),
    }};
}

auto Win32AtomicTextStorage::write_text_atomic(
    std::string_view name,
    std::string_view text)
    -> std::expected<void, TextStorageError>
{
    if (text.size() > MaximumConfigBytes)
    {
        return std::unexpected(TextStorageError{
            TextStorageErrorCode::TooLarge,
            "Configuration write exceeds its byte limit.",
        });
    }
    const auto bytes = std::as_bytes(std::span{text});
    auto written = detail::write_managed_file_atomic(
        local_app_data_,
        detail::ManagedFileArea::V2Root,
        name,
        bytes);
    if (!written)
    {
        return std::unexpected(text_error(written.error()));
    }
    return {};
}
} // namespace meccha::application
