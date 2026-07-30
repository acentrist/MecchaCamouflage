#include <meccha/core/image_project.hpp>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace meccha::core
{
namespace
{
auto finite_positive(double value) -> bool
{
    return std::isfinite(value) && value > 0.0;
}

auto unit(double value) -> bool
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

auto material_valid(const Material& material) -> bool
{
    return unit(material.metallic) && unit(material.roughness) &&
           unit(material.emissive);
}
} // namespace

auto validate(const ImageLayer& layer)
    -> std::vector<ImageLayerField>
{
    auto errors = std::vector<ImageLayerField>{};
    if (layer.asset_id.empty())
    {
        errors.push_back(ImageLayerField::AssetId);
    }
    if (layer.file_name.empty() || layer.file_name.size() > 260U)
    {
        errors.push_back(ImageLayerField::FileName);
    }
    if (layer.source_bytes == 0U ||
        layer.source_bytes > MaximumImageSourceBytes)
    {
        errors.push_back(ImageLayerField::SourceSize);
    }
    if (!std::isfinite(layer.center_x) ||
        !std::isfinite(layer.center_y) ||
        !finite_positive(layer.width) ||
        !finite_positive(layer.height))
    {
        errors.push_back(ImageLayerField::Placement);
    }
    const auto& crop = layer.crop;
    if (!unit(crop.x) || !unit(crop.y) ||
        !finite_positive(crop.width) ||
        !finite_positive(crop.height) ||
        crop.width > 1.0 || crop.height > 1.0 ||
        crop.x + crop.width > 1.000001 ||
        crop.y + crop.height > 1.000001)
    {
        errors.push_back(ImageLayerField::Crop);
    }
    return errors;
}

auto validate(
    const ImageProjectSettings& settings,
    const std::vector<ImageLayer>& layers)
    -> std::vector<ImageProjectError>
{
    auto errors = std::vector<ImageProjectError>{};
    if (layers.empty())
    {
        errors.push_back(ImageProjectError::Empty);
    }

    auto total_bytes = std::size_t{};
    auto overflow = false;
    auto invalid_layer = false;
    for (const auto& layer : layers)
    {
        invalid_layer = invalid_layer || !validate(layer).empty();
        if (layer.source_bytes >
            std::numeric_limits<std::size_t>::max() - total_bytes)
        {
            overflow = true;
        }
        else
        {
            total_bytes += layer.source_bytes;
        }
    }
    if (invalid_layer)
    {
        errors.push_back(ImageProjectError::InvalidLayer);
    }
    if (overflow)
    {
        errors.push_back(ImageProjectError::SourceSizeOverflow);
    }
    else if (total_bytes > MaximumProjectSourceBytes)
    {
        errors.push_back(ImageProjectError::SourceSizeLimit);
    }
    if (!std::isfinite(settings.brush_size_texels) ||
        settings.brush_size_texels < 1.0 ||
        settings.brush_size_texels > 10.0)
    {
        errors.push_back(ImageProjectError::BrushSize);
    }
    if (!std::isfinite(
            settings.color_compression_tolerance_percent) ||
        settings.color_compression_tolerance_percent < 0.0 ||
        settings.color_compression_tolerance_percent > 10.0)
    {
        errors.push_back(ImageProjectError::CompressionTolerance);
    }
    if (!material_valid(settings.image_material))
    {
        errors.push_back(ImageProjectError::ImageMaterial);
    }
    if (!material_valid(settings.fill_material))
    {
        errors.push_back(ImageProjectError::FillMaterial);
    }
    return errors;
}
} // namespace meccha::core
