#include <meccha/application/localization.hpp>

#include "strict_json.hpp"

#include <meccha/core/config.hpp>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
auto localization_error(
    LocalizationErrorCode code,
    std::string detail) -> LocalizationError
{
    return LocalizationError{code, std::move(detail)};
}

auto append_utf8(
    std::string_view text,
    std::vector<char32_t>& output) -> bool
{
    auto index = std::size_t{};
    while (index < text.size())
    {
        const auto first =
            static_cast<std::uint8_t>(text[index]);
        if (first <= 0x7FU)
        {
            output.push_back(static_cast<char32_t>(first));
            ++index;
            continue;
        }

        auto length = std::size_t{};
        auto codepoint = std::uint32_t{};
        auto minimum = std::uint32_t{};
        if (first >= 0xC2U && first <= 0xDFU)
        {
            length = 2U;
            codepoint = first & 0x1FU;
            minimum = 0x80U;
        }
        else if (first >= 0xE0U && first <= 0xEFU)
        {
            length = 3U;
            codepoint = first & 0x0FU;
            minimum = 0x800U;
        }
        else if (first >= 0xF0U && first <= 0xF4U)
        {
            length = 4U;
            codepoint = first & 0x07U;
            minimum = 0x10000U;
        }
        else
        {
            return false;
        }
        if (index + length > text.size())
        {
            return false;
        }
        for (auto offset = std::size_t{1U};
             offset < length;
             ++offset)
        {
            const auto continuation = static_cast<std::uint8_t>(
                text[index + offset]);
            if ((continuation & 0xC0U) != 0x80U)
            {
                return false;
            }
            codepoint =
                (codepoint << 6U) | (continuation & 0x3FU);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFU ||
            (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
        {
            return false;
        }
        output.push_back(static_cast<char32_t>(codepoint));
        index += length;
    }
    return true;
}

auto key_valid(std::string_view key) -> bool
{
    if (key.empty() || key.size() > 128U)
    {
        return false;
    }
    return std::ranges::all_of(
        key,
        [](char character)
        {
            return (character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9') ||
                   character == '.' || character == '-';
        });
}

auto placeholders(std::string_view value)
    -> std::expected<std::vector<std::size_t>, LocalizationError>
{
    auto result = std::vector<std::size_t>{};
    auto index = std::size_t{};
    while (index < value.size())
    {
        if (value[index] == '}')
        {
            return std::unexpected(localization_error(
                LocalizationErrorCode::PlaceholderSyntax,
                "Localized value contains an unmatched closing brace."));
        }
        if (value[index] != '{')
        {
            ++index;
            continue;
        }

        ++index;
        if (index == value.size() ||
            value[index] < '0' || value[index] > '9')
        {
            return std::unexpected(localization_error(
                LocalizationErrorCode::PlaceholderSyntax,
                "Localized placeholders must use numeric indexes."));
        }
        auto placeholder = std::size_t{};
        while (index < value.size() &&
               value[index] >= '0' && value[index] <= '9')
        {
            const auto digit =
                static_cast<std::size_t>(value[index] - '0');
            if (placeholder >
                (std::numeric_limits<std::size_t>::max() - digit) /
                    10U)
            {
                return std::unexpected(localization_error(
                    LocalizationErrorCode::PlaceholderSyntax,
                    "Localized placeholder index overflows."));
            }
            placeholder = placeholder * 10U + digit;
            ++index;
        }
        if (index == value.size() || value[index] != '}' ||
            placeholder > 15U)
        {
            return std::unexpected(localization_error(
                LocalizationErrorCode::PlaceholderSyntax,
                "Localized placeholder is malformed or too large."));
        }
        result.push_back(placeholder);
        ++index;
    }
    std::ranges::sort(result);
    return result;
}

auto validate_locale_set(
    const LocalizationCatalog::CatalogMap& catalogs)
    -> std::expected<void, LocalizationError>
{
    if (catalogs.size() != SupportedLocaleInfo.size())
    {
        return std::unexpected(localization_error(
            LocalizationErrorCode::LocaleSet,
            "Localization catalog must contain exactly 16 locales."));
    }
    for (const auto& locale : SupportedLocaleInfo)
    {
        if (!catalogs.contains(locale.code))
        {
            return std::unexpected(localization_error(
                LocalizationErrorCode::LocaleSet,
                "Localization catalog is missing locale " +
                    std::string{locale.code} + "."));
        }
    }
    return {};
}

auto collect_and_validate(
    const LocalizationCatalog::CatalogMap& catalogs)
    -> std::expected<std::vector<char32_t>, LocalizationError>
{
    const auto english = catalogs.find("en");
    if (english == catalogs.end() || english->second.empty() ||
        english->second.size() > MaximumLocalizationKeys)
    {
        return std::unexpected(localization_error(
            LocalizationErrorCode::ResourceLimit,
            "English localization key inventory is invalid."));
    }

    auto glyphs = std::vector<char32_t>{};
    for (const auto& locale : SupportedLocaleInfo)
    {
        if (!append_utf8(locale.native_name, glyphs))
        {
            return std::unexpected(localization_error(
                LocalizationErrorCode::InvalidUtf8,
                "Locale display name is not valid UTF-8."));
        }
    }

    for (const auto& [locale, translations] : catalogs)
    {
        if (translations.size() != english->second.size())
        {
            return std::unexpected(localization_error(
                LocalizationErrorCode::KeySet,
                "Localization key count differs for " + locale + "."));
        }
        for (const auto& [key, english_value] : english->second)
        {
            if (!key_valid(key))
            {
                return std::unexpected(localization_error(
                    LocalizationErrorCode::InvalidKey,
                    "Localization key is invalid: " + key + "."));
            }
            const auto translated = translations.find(key);
            if (translated == translations.end())
            {
                return std::unexpected(localization_error(
                    LocalizationErrorCode::KeySet,
                    "Localization key is missing for " + locale +
                        ": " + key + "."));
            }
            if (translated->second.empty())
            {
                return std::unexpected(localization_error(
                    LocalizationErrorCode::EmptyValue,
                    "Localized value is empty for " + locale +
                        ": " + key + "."));
            }
            if (translated->second.size() >
                MaximumLocalizedValueBytes)
            {
                return std::unexpected(localization_error(
                    LocalizationErrorCode::ResourceLimit,
                    "Localized value exceeds its byte limit."));
            }
            auto expected = placeholders(english_value);
            if (!expected)
            {
                return std::unexpected(expected.error());
            }
            auto actual = placeholders(translated->second);
            if (!actual)
            {
                return std::unexpected(actual.error());
            }
            if (*expected != *actual)
            {
                return std::unexpected(localization_error(
                    LocalizationErrorCode::PlaceholderSet,
                    "Localized placeholder set differs for " + locale +
                        ": " + key + "."));
            }
            if (!append_utf8(translated->second, glyphs))
            {
                return std::unexpected(localization_error(
                    LocalizationErrorCode::InvalidUtf8,
                    "Localized value is not valid UTF-8."));
            }
        }
    }

    std::ranges::sort(glyphs);
    const auto unique = std::ranges::unique(glyphs);
    glyphs.erase(unique.begin(), unique.end());
    return glyphs;
}
} // namespace

LocalizationCatalog::LocalizationCatalog(
    CatalogMap catalogs,
    std::vector<char32_t> codepoints)
    : catalogs_{std::move(catalogs)},
      codepoints_{std::move(codepoints)}
{
}

auto LocalizationCatalog::parse(std::string_view json)
    -> std::expected<LocalizationCatalog, LocalizationError>
{
    if (json.size() > MaximumLocalizationBytes)
    {
        return std::unexpected(localization_error(
            LocalizationErrorCode::TooLarge,
            "Localization catalog exceeds its byte limit."));
    }

    auto raw_codepoints = std::vector<char32_t>{};
    if (!append_utf8(json, raw_codepoints))
    {
        return std::unexpected(localization_error(
            LocalizationErrorCode::InvalidUtf8,
            "Localization catalog is not valid UTF-8."));
    }

    const auto document =
        detail::validate_strict_json_document(json);
    if (!document)
    {
        return std::unexpected(localization_error(
            LocalizationErrorCode::MalformedJson,
            document.error().detail));
    }

    auto catalogs = CatalogMap{};
    const auto parsed =
        glz::read<detail::StrictJson>(catalogs, json);
    if (parsed)
    {
        return std::unexpected(localization_error(
            LocalizationErrorCode::MalformedJson,
            glz::format_error(parsed, json)));
    }
    auto locale_set = validate_locale_set(catalogs);
    if (!locale_set)
    {
        return std::unexpected(locale_set.error());
    }
    auto glyphs = collect_and_validate(catalogs);
    if (!glyphs)
    {
        return std::unexpected(glyphs.error());
    }
    return LocalizationCatalog{
        std::move(catalogs),
        std::move(*glyphs),
    };
}

auto LocalizationCatalog::locale_count() const -> std::size_t
{
    return catalogs_.size();
}

auto LocalizationCatalog::key_count() const -> std::size_t
{
    const auto english = catalogs_.find("en");
    return english == catalogs_.end() ? 0U : english->second.size();
}

auto LocalizationCatalog::text(
    std::string_view locale,
    std::string_view key) const -> std::string_view
{
    const auto selected = catalogs_.find(locale);
    if (selected != catalogs_.end())
    {
        const auto value = selected->second.find(key);
        if (value != selected->second.end())
        {
            return value->second;
        }
    }
    const auto english = catalogs_.find("en");
    if (english != catalogs_.end())
    {
        const auto fallback = english->second.find(key);
        if (fallback != english->second.end())
        {
            return fallback->second;
        }
    }
    return key;
}

auto LocalizationCatalog::format(
    std::string_view locale,
    std::string_view key,
    std::span<const std::string_view> arguments) const
    -> std::expected<std::string, LocalizationFormatError>
{
    const auto pattern = text(locale, key);
    auto output = std::string{};
    output.reserve(pattern.size());
    auto index = std::size_t{};
    while (index < pattern.size())
    {
        if (pattern[index] != '{')
        {
            output.push_back(pattern[index]);
            ++index;
            continue;
        }
        ++index;
        if (index == pattern.size() ||
            pattern[index] < '0' || pattern[index] > '9')
        {
            return std::unexpected(LocalizationFormatError{
                LocalizationFormatErrorCode::InvalidTemplate,
                "Localized placeholder is invalid.",
            });
        }
        auto placeholder = std::size_t{};
        while (index < pattern.size() &&
               pattern[index] >= '0' && pattern[index] <= '9')
        {
            placeholder =
                placeholder * 10U +
                static_cast<std::size_t>(pattern[index] - '0');
            ++index;
        }
        if (index == pattern.size() || pattern[index] != '}')
        {
            return std::unexpected(LocalizationFormatError{
                LocalizationFormatErrorCode::InvalidTemplate,
                "Localized placeholder is invalid.",
            });
        }
        if (placeholder >= arguments.size())
        {
            return std::unexpected(LocalizationFormatError{
                LocalizationFormatErrorCode::MissingArgument,
                "Localized placeholder argument is missing.",
            });
        }
        output.append(arguments[placeholder]);
        ++index;
    }
    return output;
}

auto LocalizationCatalog::codepoints() const
    -> std::span<const char32_t>
{
    return codepoints_;
}
} // namespace meccha::application
