#include <meccha/application/config_codec.hpp>

#include "strict_json.hpp"

#include <glaze/glaze.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

template <>
struct glz::meta<meccha::core::FunctionKey>
{
    using enum meccha::core::FunctionKey;
    static constexpr auto value = glz::enumerate(
        "F1", F1,
        "F2", F2,
        "F3", F3,
        "F4", F4,
        "F5", F5,
        "F6", F6,
        "F7", F7,
        "F8", F8,
        "F9", F9,
        "F10", F10,
        "F11", F11,
        "F12", F12,
        "F13", F13,
        "F14", F14,
        "F15", F15,
        "F16", F16,
        "F17", F17,
        "F18", F18,
        "F19", F19,
        "F20", F20,
        "F21", F21,
        "F22", F22,
        "F23", F23,
        "F24", F24);
};

template <>
struct glz::meta<meccha::core::RegionMode>
{
    using enum meccha::core::RegionMode;
    static constexpr auto value =
        glz::enumerate("paint", Paint, "fill", Fill, "skip", Skip);
};

template <>
struct glz::meta<meccha::core::BodyProfile>
{
    using enum meccha::core::BodyProfile;
    static constexpr auto value = glz::enumerate(
        "round", Round,
        "cube", Cube,
        "fukuyoka", Fukuyoka);
};

template <>
struct glz::meta<meccha::core::PlacementMode>
{
    using enum meccha::core::PlacementMode;
    static constexpr auto value =
        glz::enumerate("fit", Fit, "fill", Fill);
};

template <>
struct glz::meta<meccha::core::FaceBaseMode>
{
    using enum meccha::core::FaceBaseMode;
    static constexpr auto value =
        glz::enumerate("fill", Fill, "skip", Skip);
};

template <>
struct glz::meta<meccha::core::EspScope>
{
    using enum meccha::core::EspScope;
    static constexpr auto value =
        glz::enumerate("all", All, "hider", Hider, "hunter", Hunter);
};

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
