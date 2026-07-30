#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application
{
inline constexpr std::size_t MaximumLocalizationBytes =
    1024U * 1024U;
inline constexpr std::size_t MaximumLocalizationKeys = 512U;
inline constexpr std::size_t MaximumLocalizedValueBytes = 4096U;

struct LocaleInfo
{
    std::string_view code{};
    std::string_view native_name{};

    auto operator==(const LocaleInfo&) const -> bool = default;
};

inline constexpr std::array<LocaleInfo, 16> SupportedLocaleInfo{
    LocaleInfo{"en", "English"},
    LocaleInfo{"id", "Bahasa Indonesia"},
    LocaleInfo{"de", "Deutsch"},
    LocaleInfo{"es", "Español"},
    LocaleInfo{"fr", "Français"},
    LocaleInfo{"it", "Italiano"},
    LocaleInfo{"nl", "Nederlands"},
    LocaleInfo{"pl", "Polski"},
    LocaleInfo{"pt-BR", "Português (Brasil)"},
    LocaleInfo{"vi", "Tiếng Việt"},
    LocaleInfo{"tr", "Türkçe"},
    LocaleInfo{"ru", "Русский"},
    LocaleInfo{"ja", "日本語"},
    LocaleInfo{"ko", "한국어"},
    LocaleInfo{"zh-Hans", "简体中文"},
    LocaleInfo{"zh-Hant", "繁體中文"},
};

enum class LocalizationErrorCode : std::uint8_t
{
    TooLarge,
    InvalidUtf8,
    MalformedJson,
    LocaleSet,
    KeySet,
    InvalidKey,
    EmptyValue,
    PlaceholderSyntax,
    PlaceholderSet,
    ResourceLimit,
};

struct LocalizationError
{
    LocalizationErrorCode code{};
    std::string detail{};

    auto operator==(const LocalizationError&) const -> bool = default;
};

enum class LocalizationFormatErrorCode : std::uint8_t
{
    MissingArgument,
    InvalidTemplate,
};

struct LocalizationFormatError
{
    LocalizationFormatErrorCode code{};
    std::string detail{};

    auto operator==(const LocalizationFormatError&) const -> bool = default;
};

class LocalizationCatalog
{
public:
    using TranslationMap =
        std::map<std::string, std::string, std::less<>>;
    using CatalogMap =
        std::map<std::string, TranslationMap, std::less<>>;

    [[nodiscard]] static auto parse(std::string_view json)
        -> std::expected<LocalizationCatalog, LocalizationError>;

    [[nodiscard]] auto locale_count() const -> std::size_t;
    [[nodiscard]] auto key_count() const -> std::size_t;

    [[nodiscard]] auto text(
        std::string_view locale,
        std::string_view key) const -> std::string_view;

    [[nodiscard]] auto format(
        std::string_view locale,
        std::string_view key,
        std::span<const std::string_view> arguments) const
        -> std::expected<std::string, LocalizationFormatError>;

    [[nodiscard]] auto codepoints() const
        -> std::span<const char32_t>;

private:
    LocalizationCatalog(
        CatalogMap catalogs,
        std::vector<char32_t> codepoints);

    CatalogMap catalogs_{};
    std::vector<char32_t> codepoints_{};
};
} // namespace meccha::application
