#pragma once

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace meccha::application::detail
{
struct StrictJsonOptions : glz::opts
{
    bool validate_skipped = true;
    bool validate_trailing_whitespace = true;
};

inline constexpr StrictJsonOptions StrictJson{
    {.error_on_unknown_keys = true, .error_on_missing_keys = true},
    true,
    true,
};

enum class JsonDocumentErrorCode : std::uint8_t
{
    Malformed,
    DuplicateKey,
};

struct JsonDocumentError
{
    JsonDocumentErrorCode code{};
    std::string detail{};
};

inline auto count_object_members(const glz::generic_json<>& value)
    -> std::size_t
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

inline auto count_object_separators(std::string_view json)
    -> std::size_t
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

inline auto validate_strict_json_document(std::string_view json)
    -> std::expected<void, JsonDocumentError>
{
    auto generic = glz::generic_json<>{};
    const auto parsed = glz::read<StrictJson>(generic, json);
    if (parsed)
    {
        return std::unexpected(JsonDocumentError{
            JsonDocumentErrorCode::Malformed,
            glz::format_error(parsed, json),
        });
    }
    if (count_object_members(generic) !=
        count_object_separators(json))
    {
        return std::unexpected(JsonDocumentError{
            JsonDocumentErrorCode::DuplicateKey,
            "Duplicate JSON object key.",
        });
    }
    return {};
}
} // namespace meccha::application::detail
