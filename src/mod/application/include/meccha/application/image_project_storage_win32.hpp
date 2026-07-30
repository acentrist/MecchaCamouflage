#pragma once

#include <meccha/application/image_project_store.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace meccha::application
{
class Win32AtomicProjectStorage final
    : public AtomicProjectStorage
{
public:
    explicit Win32AtomicProjectStorage(
        std::filesystem::path local_app_data);

    auto read(
        std::string_view name,
        std::size_t maximum_bytes)
        -> std::expected<
            std::optional<std::vector<std::byte>>,
            ProjectStorageError> override;

    auto write_atomic(
        std::string_view name,
        std::span<const std::byte> bytes)
        -> std::expected<void, ProjectStorageError> override;

    auto remove(std::string_view name)
        -> std::expected<bool, ProjectStorageError> override;

    [[nodiscard]] auto root() const
        -> const std::filesystem::path&;

private:
    std::filesystem::path local_app_data_{};
    std::filesystem::path root_{};
};
} // namespace meccha::application
