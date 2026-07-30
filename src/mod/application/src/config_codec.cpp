#include <meccha/application/config_codec.hpp>

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>

#include <cstddef>
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
struct StrictJsonOptions : glz::opts
{
    bool validate_skipped = true;
    bool validate_trailing_whitespace = true;
};

constexpr StrictJsonOptions StrictJson{
    {.error_on_unknown_keys = true, .error_on_missing_keys = true},
    true,
    true,
};

auto count_object_members(const glz::generic_json<>& value) -> std::size_t
{
    using Generic = glz::generic_json<>;
    if (const auto* object = value.get_if<Generic::object_t>())
    {
        auto count = object->size();
        for (const auto& [key, child] : *object)
        {
            static_cast<void>(key);
            count += count_object_members(child);
        }
        return count;
    }
    if (const auto* array = value.get_if<Generic::array_t>())
    {
        auto count = std::size_t{};
        for (const auto& child : *array)
        {
            count += count_object_members(child);
        }
        return count;
    }
    return 0U;
}

auto count_object_separators(std::string_view json) -> std::size_t
{
    auto count = std::size_t{};
    auto in_string = false;
    auto escaped = false;
    for (const auto character : json)
    {
        if (in_string)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (character == '\\')
            {
                escaped = true;
            }
            else if (character == '"')
            {
                in_string = false;
            }
            continue;
        }
        if (character == '"')
        {
            in_string = true;
        }
        else if (character == ':')
        {
            ++count;
        }
    }
    return count;
}

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
    auto generic = glz::generic_json<>{};
    const auto parsed = glz::read<StrictJson>(generic, json);
    if (parsed)
    {
        return std::unexpected(
            malformed(glz::format_error(parsed, json)));
    }
    if (count_object_members(generic) !=
        count_object_separators(json))
    {
        return std::unexpected(
            malformed("Duplicate JSON object key."));
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
    const auto parsed = glz::read<StrictJson>(config, json);
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
