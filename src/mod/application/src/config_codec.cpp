#include <meccha/application/config_codec.hpp>

#include "strict_json.hpp"
#include "json_domain_meta.hpp"

#include <glaze/glaze.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
auto malformed(std::string detail) -> ConfigCodecError
{
    return ConfigCodecError{
        ConfigCodecErrorCode::MalformedJson,
        {},
        std::move(detail),
    };
}

auto validate_document_shape(std::string_view json)
    -> std::expected<void, ConfigCodecError>
{
    const auto document =
        detail::validate_strict_json_document(json);
    if (!document)
    {
        return std::unexpected(malformed(document.error().detail));
    }
    return {};
}
} // namespace

auto decode_config(std::string_view json)
    -> std::expected<core::ApplicationConfig, ConfigCodecError>
{
    if (json.size() > MaximumConfigBytes)
    {
        return std::unexpected(ConfigCodecError{
            ConfigCodecErrorCode::TooLarge,
            {},
            "Configuration exceeds its byte limit.",
        });
    }

    const auto document = validate_document_shape(json);
    if (!document)
    {
        return std::unexpected(document.error());
    }

    auto config = core::ApplicationConfig{};
    const auto parsed = glz::read<detail::StrictJson>(config, json);
    if (parsed)
    {
        return std::unexpected(
            malformed(glz::format_error(parsed, json)));
    }

    auto fields = core::validate(config);
    if (!fields.empty())
    {
        const auto code =
            fields.front() == core::ConfigurationField::SchemaVersion
            ? ConfigCodecErrorCode::UnsupportedSchema
            : ConfigCodecErrorCode::InvalidValue;
        return std::unexpected(ConfigCodecError{
            code,
            std::move(fields),
            code == ConfigCodecErrorCode::UnsupportedSchema
                ? "Configuration schema is not supported."
                : "Configuration contains invalid values.",
        });
    }
    return config;
}

auto encode_config(const core::ApplicationConfig& config)
    -> std::expected<std::string, ConfigCodecError>
{
    auto fields = core::validate(config);
    if (!fields.empty())
    {
        const auto code =
            fields.front() == core::ConfigurationField::SchemaVersion
            ? ConfigCodecErrorCode::UnsupportedSchema
            : ConfigCodecErrorCode::InvalidValue;
        return std::unexpected(ConfigCodecError{
            code,
            std::move(fields),
            code == ConfigCodecErrorCode::UnsupportedSchema
                ? "Configuration schema is not supported."
                : "Configuration contains invalid values.",
        });
    }

    auto json = glz::write_json(config);
    if (!json)
    {
        return std::unexpected(ConfigCodecError{
            ConfigCodecErrorCode::Serialization,
            {},
            "Configuration could not be serialized.",
        });
    }
    if (json->size() > MaximumConfigBytes)
    {
        return std::unexpected(ConfigCodecError{
            ConfigCodecErrorCode::TooLarge,
            {},
            "Serialized configuration exceeds its byte limit.",
        });
    }
    return *json;
}
} // namespace meccha::application
