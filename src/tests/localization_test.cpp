#include <meccha/application/localization.hpp>

#include <meccha/core/config.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

auto read_file(const std::filesystem::path& path) -> std::string
{
    auto stream = std::ifstream{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

auto replace_once(
    std::string value,
    std::string_view from,
    std::string_view to) -> std::string
{
    const auto position = value.find(from);
    if (position != std::string::npos)
    {
        value.replace(position, from.size(), to);
    }
    return value;
}
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;

    auto passed = true;
    passed &= expect(argc == 2, "localization resource path is missing");
    if (argc != 2)
    {
        return 1;
    }
    const auto json = read_file(argv[1]);
    passed &= expect(!json.empty(), "localization resource is empty");

    const auto catalog = application::LocalizationCatalog::parse(json);
    passed &= expect(catalog.has_value(), "shipped catalog did not parse");
    if (!catalog)
    {
        std::cerr << catalog.error().detail << '\n';
        return 1;
    }

    passed &= expect(
        catalog->locale_count() == core::SupportedLocales.size() &&
            catalog->key_count() == 149U,
        "locale or retained key inventory drifted");
    for (std::size_t index = 0U;
         index < core::SupportedLocales.size();
         ++index)
    {
        passed &= expect(
            application::SupportedLocaleInfo[index].code ==
                core::SupportedLocales[index],
            "locale order differs from the config contract");
    }

    passed &= expect(
        catalog->text("ja", "settings.paint") == "ペイント" &&
            catalog->text("not-supported", "settings.paint") ==
                "Paint" &&
            catalog->text("ja", "missing.key") == "missing.key",
        "localized lookup or English/key fallback failed");

    const std::array arguments{std::string_view{"WARN"}};
    const auto formatted = catalog->format(
        "ja",
        "logs.empty.filtered",
        std::span{arguments});
    passed &= expect(
        formatted && *formatted == "WARN ログはありません。",
        "localized placeholder formatting failed");
    const auto missing_argument = catalog->format(
        "en",
        "logs.empty.filtered",
        std::span<const std::string_view>{});
    passed &= expect(
        !missing_argument &&
            missing_argument.error().code ==
                application::LocalizationFormatErrorCode::MissingArgument,
        "missing localization argument was accepted");

    const auto codepoints = catalog->codepoints();
    passed &= expect(
        std::ranges::find(codepoints, U'日') != codepoints.end() &&
            std::ranges::find(codepoints, U'한') != codepoints.end() &&
            std::ranges::find(codepoints, U'Д') != codepoints.end(),
        "catalog glyph inventory omitted a shipped script");

    const auto wrong_locales = replace_once(
        json,
        "\n  \"vi\": {",
        "\n  \"xx\": {");
    const auto wrong_locale_result =
        application::LocalizationCatalog::parse(wrong_locales);
    passed &= expect(
        !wrong_locale_result &&
            wrong_locale_result.error().code ==
                application::LocalizationErrorCode::LocaleSet,
        "unknown/missing locale pair was accepted");

    const auto placeholder_mismatch = replace_once(
        json,
        "\"logs.empty.filtered\": \"{0} ログはありません。\"",
        "\"logs.empty.filtered\": \"{1} ログはありません。\"");
    const auto placeholder_result =
        application::LocalizationCatalog::parse(placeholder_mismatch);
    passed &= expect(
        !placeholder_result &&
            placeholder_result.error().code ==
                application::LocalizationErrorCode::PlaceholderSet,
        "translation placeholder mismatch was accepted");

    const auto duplicate_locale = replace_once(
        json,
        "\n  \"ja\": {",
        "\n  \"en\": {");
    const auto duplicate_result =
        application::LocalizationCatalog::parse(duplicate_locale);
    passed &= expect(
        !duplicate_result &&
            duplicate_result.error().code ==
                application::LocalizationErrorCode::MalformedJson,
        "duplicate locale object key was accepted");

    auto invalid_utf8 = json;
    const auto title = invalid_utf8.find("Meccha Camouflage");
    if (title != std::string::npos)
    {
        invalid_utf8[title] = static_cast<char>(0xC0);
    }
    const auto invalid_utf8_result =
        application::LocalizationCatalog::parse(invalid_utf8);
    passed &= expect(
        !invalid_utf8_result &&
            invalid_utf8_result.error().code ==
                application::LocalizationErrorCode::InvalidUtf8,
        "invalid UTF-8 catalog value was accepted");

    const auto oversized = application::LocalizationCatalog::parse(
        std::string(
            application::MaximumLocalizationBytes + 1U,
            ' '));
    passed &= expect(
        !oversized &&
            oversized.error().code ==
                application::LocalizationErrorCode::TooLarge,
        "oversized localization catalog was accepted");

    if (passed)
    {
        std::cout << "PASS localization\n";
    }
    return passed ? 0 : 1;
}
