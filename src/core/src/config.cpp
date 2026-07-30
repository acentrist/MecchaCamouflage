#include <meccha/core/config.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

namespace meccha::core
{
namespace
{
auto key_value(FunctionKey key) -> std::uint8_t
{
    return static_cast<std::uint8_t>(key);
}

auto material_valid(const Material& material) -> bool
{
    const auto unit =
        [](double value)
        {
            return std::isfinite(value) &&
                   value >= 0.0 && value <= 1.0;
        };
    return unit(material.metallic) && unit(material.roughness) &&
           unit(material.emissive);
}
} // namespace

auto is_supported_locale(std::string_view locale) -> bool
{
    return std::ranges::find(SupportedLocales, locale) !=
           SupportedLocales.end();
}

auto validate(const ApplicationConfig& config)
    -> std::vector<ConfigurationField>
{
    auto errors = std::vector<ConfigurationField>{};
    if (config.schema_version != ConfigurationSchemaVersion)
    {
        errors.push_back(ConfigurationField::SchemaVersion);
    }
    if (!is_supported_locale(config.ui.language))
    {
        errors.push_back(ConfigurationField::Language);
    }
    if (!std::isfinite(config.ui.scale) ||
        config.ui.scale < 0.75 || config.ui.scale > 2.0)
    {
        errors.push_back(ConfigurationField::UiScale);
    }

    const std::array keys{
        config.ui.hotkeys.toggle_ui,
        config.ui.hotkeys.paint_start,
        config.ui.hotkeys.paint_preview,
        config.ui.hotkeys.paint_restore,
        config.ui.hotkeys.paint_cancel,
        config.ui.hotkeys.image_start,
        config.ui.hotkeys.image_preview,
        config.ui.hotkeys.image_restore,
        config.ui.hotkeys.image_cancel,
    };
    const auto key_in_range =
        [](FunctionKey key)
        {
            const auto value = key_value(key);
            return value >= key_value(FunctionKey::F1) &&
                   value <= key_value(FunctionKey::F24);
        };
    if (!std::ranges::all_of(keys, key_in_range))
    {
        errors.push_back(ConfigurationField::HotkeyRange);
    }
    auto unique_keys = std::set<std::uint8_t>{};
    for (const auto key : keys)
    {
        unique_keys.insert(key_value(key));
    }
    if (unique_keys.size() != keys.size())
    {
        errors.push_back(ConfigurationField::DuplicateHotkey);
    }

    if (!validate(config.paint).empty())
    {
        errors.push_back(ConfigurationField::Paint);
    }
    if (const auto image_errors = validate(config.image_paint);
        std::ranges::any_of(
            image_errors,
            [](ImageProjectError error)
            {
                return error == ImageProjectError::BodyProfile ||
                       error == ImageProjectError::Placement ||
                       error == ImageProjectError::AlphaMode ||
                       error == ImageProjectError::FaceMode;
            }))
    {
        errors.push_back(ConfigurationField::ImageSettings);
    }
    if (!std::isfinite(config.image_paint.brush_size_texels) ||
        config.image_paint.brush_size_texels < 1.0 ||
        config.image_paint.brush_size_texels > 10.0)
    {
        errors.push_back(ConfigurationField::ImageBrushSize);
    }
    if (!std::isfinite(
            config.image_paint
                .color_compression_tolerance_percent) ||
        config.image_paint.color_compression_tolerance_percent < 0.0 ||
        config.image_paint.color_compression_tolerance_percent > 10.0)
    {
        errors.push_back(
            ConfigurationField::ImageCompressionTolerance);
    }
    if (!material_valid(config.image_paint.image_material))
    {
        errors.push_back(ConfigurationField::ImageMaterial);
    }
    if (!material_valid(config.image_paint.fill_material))
    {
        errors.push_back(ConfigurationField::ImageFillMaterial);
    }
    return errors;
}
} // namespace meccha::core
