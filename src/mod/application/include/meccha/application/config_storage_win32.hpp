#pragma once

#include <meccha/application/config_store.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace meccha::application
{
[[nodiscard]] auto resolve_local_app_data()
    -> std::expected<std::filesystem::path, TextStorageError>;

class Win32AtomicTextStorage final : public AtomicTextStorage
{
public:
    explicit Win32AtomicTextStorage(
        std::filesystem::path local_app_data);

    auto read_text(
        std::string_view name,
        std::size_t maximum_bytes)
        -> std::expected<
            std::optional<std::string>,
            TextStorageError> override;

    auto write_text_atomic(
        std::string_view name,
        std::string_view text)
        -> std::expected<void, TextStorageError> override;

    [[nodiscard]] auto root() const
        -> const std::filesystem::path&;

private:
    std::filesystem::path local_app_data_{};
    std::filesystem::path root_{};
};
} // namespace meccha::application
