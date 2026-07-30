#pragma once

#include <meccha/core/esp.hpp>
#include <meccha/core/image_project.hpp>
#include <meccha/core/paint.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::core
{
inline constexpr std::uint32_t ConfigurationSchemaVersion = 1U;
inline constexpr std::array<std::string_view, 16> SupportedLocales{
    "en",
    "id",
    "de",
    "es",
    "fr",
    "it",
    "nl",
    "pl",
    "pt-BR",
    "vi",
    "tr",
    "ru",
    "ja",
    "ko",
    "zh-Hans",
    "zh-Hant",
};

enum class FunctionKey : std::uint8_t
{
    F1 = 1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
};

struct HotkeySettings
{
    FunctionKey toggle_ui{FunctionKey::F9};
    FunctionKey paint_start{FunctionKey::F1};
    FunctionKey paint_preview{FunctionKey::F2};
    FunctionKey paint_restore{FunctionKey::F3};
    FunctionKey paint_cancel{FunctionKey::F4};
    FunctionKey image_start{FunctionKey::F5};
    FunctionKey image_preview{FunctionKey::F6};
    FunctionKey image_restore{FunctionKey::F7};
    FunctionKey image_cancel{FunctionKey::F8};

    auto operator==(const HotkeySettings&) const -> bool = default;
};

struct UiSettings
{
    std::string language{"en"};
    double scale{1.0};
    Rgb8 theme_color{255U, 255U, 255U};
    HotkeySettings hotkeys{};

    auto operator==(const UiSettings&) const -> bool = default;
};

struct ApplicationConfig
{
    std::uint32_t schema_version{ConfigurationSchemaVersion};
    UiSettings ui{};
    PaintSettings paint{};
    ImageProjectSettings image_paint{};
    EspSettings esp{};

    auto operator==(const ApplicationConfig&) const -> bool = default;
};

enum class ConfigurationField : std::uint8_t
{
    SchemaVersion,
    Language,
    UiScale,
    HotkeyRange,
    DuplicateHotkey,
    Paint,
    ImageBrushSize,
    ImageCompressionTolerance,
    ImageMaterial,
    ImageFillMaterial,
};

[[nodiscard]] auto is_supported_locale(std::string_view locale) -> bool;

[[nodiscard]] auto validate(const ApplicationConfig& config)
    -> std::vector<ConfigurationField>;
} // namespace meccha::core
