#include <meccha/application/image_project_storage_win32.hpp>

#include "atomic_file_win32.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
auto project_storage_error(detail::ManagedFileError error)
    -> ProjectStorageError
{
    auto code = ProjectStorageErrorCode::Io;
    switch (error.code)
    {
    case detail::ManagedFileErrorCode::Io:
        code = ProjectStorageErrorCode::Io;
        break;
    case detail::ManagedFileErrorCode::Conflict:
        code = ProjectStorageErrorCode::Conflict;
        break;
    case detail::ManagedFileErrorCode::TooLarge:
        code = ProjectStorageErrorCode::TooLarge;
        break;
    }
    return ProjectStorageError{code, std::move(error.detail)};
}
} // namespace

Win32AtomicProjectStorage::Win32AtomicProjectStorage(
    std::filesystem::path local_app_data)
    : local_app_data_{std::move(local_app_data)},
      root_{detail::managed_file_root(
          local_app_data_,
          detail::ManagedFileArea::ImageProjects)}
{
}

auto Win32AtomicProjectStorage::root() const
    -> const std::filesystem::path&
{
    return root_;
}

auto Win32AtomicProjectStorage::read(
    std::string_view name,
    std::size_t maximum_bytes)
    -> std::expected<
        std::optional<std::vector<std::byte>>,
        ProjectStorageError>
{
    auto bytes = detail::read_managed_file(
        local_app_data_,
        detail::ManagedFileArea::ImageProjects,
        name,
        maximum_bytes);
    if (!bytes)
    {
        return std::unexpected(
            project_storage_error(bytes.error()));
    }
    return std::move(*bytes);
}

auto Win32AtomicProjectStorage::write_atomic(
    std::string_view name,
    std::span<const std::byte> bytes)
    -> std::expected<void, ProjectStorageError>
{
    if (bytes.size() > MaximumPresetContainerBytes)
    {
        return std::unexpected(ProjectStorageError{
            ProjectStorageErrorCode::TooLarge,
            "Image project write exceeds its byte limit.",
        });
    }
    auto written = detail::write_managed_file_atomic(
        local_app_data_,
        detail::ManagedFileArea::ImageProjects,
        name,
        bytes);
    if (!written)
    {
        return std::unexpected(
            project_storage_error(written.error()));
    }
    return {};
}

auto Win32AtomicProjectStorage::remove(std::string_view name)
    -> std::expected<bool, ProjectStorageError>
{
    auto removed = detail::remove_managed_file(
        local_app_data_,
        detail::ManagedFileArea::ImageProjects,
        name);
    if (!removed)
    {
        return std::unexpected(
            project_storage_error(removed.error()));
    }
    return *removed;
}
} // namespace meccha::application
