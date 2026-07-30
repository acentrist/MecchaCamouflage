#include <meccha/application/config_store.hpp>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace meccha::application
{
namespace
{
constexpr auto ConfigFileName = "config.json";

auto store_error(const TextStorageError& error) -> ConfigStoreError
{
    return ConfigStoreError{
        ConfigStoreErrorCode::Storage,
        error.detail,
    };
}

auto codec_error(const ConfigCodecError& error) -> ConfigStoreError
{
    return ConfigStoreError{
        ConfigStoreErrorCode::Codec,
        error.detail,
    };
}
} // namespace

auto v2_data_root(const std::filesystem::path& local_app_data)
    -> std::filesystem::path
{
    return local_app_data / "MecchaCamouflage" / "v2";
}

ConfigStore::ConfigStore(AtomicTextStorage& storage)
    : storage_{storage}
{
}

auto ConfigStore::load()
    -> std::expected<ConfigLoadResult, ConfigStoreError>
{
    auto text = storage_.read_text(ConfigFileName, MaximumConfigBytes);
    if (!text)
    {
        return std::unexpected(store_error(text.error()));
    }
    if (!*text)
    {
        return ConfigLoadResult{
            core::ApplicationConfig{},
            ConfigLoadSource::Defaults,
        };
    }

    auto decoded = decode_config(**text);
    if (!decoded)
    {
        return std::unexpected(codec_error(decoded.error()));
    }
    return ConfigLoadResult{
        std::move(*decoded),
        ConfigLoadSource::Persisted,
    };
}

auto ConfigStore::save(const core::ApplicationConfig& config)
    -> std::expected<void, ConfigStoreError>
{
    auto encoded = encode_config(config);
    if (!encoded)
    {
        return std::unexpected(codec_error(encoded.error()));
    }
    auto written =
        storage_.write_text_atomic(ConfigFileName, *encoded);
    if (!written)
    {
        return std::unexpected(store_error(written.error()));
    }
    return {};
}
} // namespace meccha::application
