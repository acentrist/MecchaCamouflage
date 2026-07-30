#pragma once

#include <meccha/application/config_codec.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace meccha::application
{
enum class TextStorageErrorCode : std::uint8_t
{
    Io,
    Conflict,
    TooLarge,
};

struct TextStorageError
{
    TextStorageErrorCode code{};
    std::string detail{};

    auto operator==(const TextStorageError&) const -> bool = default;
};

class AtomicTextStorage
{
public:
    AtomicTextStorage() = default;
    AtomicTextStorage(const AtomicTextStorage&) = delete;
    auto operator=(const AtomicTextStorage&) -> AtomicTextStorage& = delete;
    AtomicTextStorage(AtomicTextStorage&&) = delete;
    auto operator=(AtomicTextStorage&&) -> AtomicTextStorage& = delete;
    virtual ~AtomicTextStorage() = default;

    virtual auto read_text(
        std::string_view name,
        std::size_t maximum_bytes)
        -> std::expected<std::optional<std::string>, TextStorageError> = 0;

    virtual auto write_text_atomic(
        std::string_view name,
        std::string_view text)
        -> std::expected<void, TextStorageError> = 0;
};

enum class ConfigLoadSource : std::uint8_t
{
    Defaults,
    Persisted,
};

struct ConfigLoadResult
{
    core::ApplicationConfig config{};
    ConfigLoadSource source{ConfigLoadSource::Defaults};

    auto operator==(const ConfigLoadResult&) const -> bool = default;
};

enum class ConfigStoreErrorCode : std::uint8_t
{
    Storage,
    Codec,
};

struct ConfigStoreError
{
    ConfigStoreErrorCode code{};
    std::string detail{};

    auto operator==(const ConfigStoreError&) const -> bool = default;
};

[[nodiscard]] auto v2_data_root(
    const std::filesystem::path& local_app_data)
    -> std::filesystem::path;

class ConfigStore
{
public:
    explicit ConfigStore(AtomicTextStorage& storage);

    [[nodiscard]] auto load()
        -> std::expected<ConfigLoadResult, ConfigStoreError>;

    [[nodiscard]] auto save(const core::ApplicationConfig& config)
        -> std::expected<void, ConfigStoreError>;

private:
    AtomicTextStorage& storage_;
};
} // namespace meccha::application
