#pragma once

#include <meccha/application/image_paint_profile_catalog.hpp>
#include <meccha/application/localization.hpp>
#include <meccha/core/fallback_glyph_atlas.hpp>
#include <meccha/core/image_guide.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace meccha::application
{
struct ProductionResources
{
    std::shared_ptr<const ImagePaintProfileCatalog>
        image_paint_profiles{};
    LocalizationCatalog localization;
    std::shared_ptr<const core::FallbackGlyphAtlas>
        fallback_glyph_atlas{};
    std::array<core::ImageGuideBitmap, 3U> image_guides{};
};

enum class ProductionResourceErrorCode : std::uint8_t
{
    InvalidModulePath,
    InvalidRoot,
    ProfileCatalog,
    LocalizationRead,
    LocalizationParse,
    FallbackGlyphAtlasRead,
    FallbackGlyphAtlasParse,
    FallbackGlyphAtlas,
    ImageGuide,
    Construction,
};

struct ProductionResourceError
{
    ProductionResourceErrorCode code{};
    std::string detail{};

    auto operator==(const ProductionResourceError&) const
        -> bool = default;
};

[[nodiscard]] auto derive_production_resource_root(
    const std::filesystem::path& module_file)
    -> std::expected<
        std::filesystem::path,
        ProductionResourceError>;

[[nodiscard]] auto load_production_resources(
    const std::filesystem::path& resource_root)
    -> std::expected<
        ProductionResources,
        ProductionResourceError>;
} // namespace meccha::application
