#pragma once

#include <meccha/application/image_paint_profile_catalog.hpp>
#include <meccha/application/localization.hpp>
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
    std::array<core::ImageGuideBitmap, 3U> image_guides{};
};

enum class ProductionResourceErrorCode : std::uint8_t
{
    InvalidRoot,
    ProfileCatalog,
    LocalizationRead,
    LocalizationParse,
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

[[nodiscard]] auto load_production_resources(
    const std::filesystem::path& resource_root)
    -> std::expected<
        ProductionResources,
        ProductionResourceError>;
} // namespace meccha::application
