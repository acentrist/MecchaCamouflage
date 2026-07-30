#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application::detail
{
enum class ManagedFileArea : std::uint8_t
{
    V2Root,
    ImageProjects,
};

enum class ManagedFileErrorCode : std::uint8_t
{
    Io,
    Conflict,
    TooLarge,
};

struct ManagedFileError
{
    ManagedFileErrorCode code{};
    std::string detail{};
};

[[nodiscard]] auto managed_file_root(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area) -> std::filesystem::path;

[[nodiscard]] auto read_managed_file(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area,
    std::string_view name,
    std::size_t maximum_bytes)
    -> std::expected<
        std::optional<std::vector<std::byte>>,
        ManagedFileError>;

[[nodiscard]] auto write_managed_file_atomic(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area,
    std::string_view name,
    std::span<const std::byte> bytes)
    -> std::expected<void, ManagedFileError>;

[[nodiscard]] auto remove_managed_file(
    const std::filesystem::path& local_app_data,
    ManagedFileArea area,
    std::string_view name)
    -> std::expected<bool, ManagedFileError>;
} // namespace meccha::application::detail
