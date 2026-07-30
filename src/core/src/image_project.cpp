#include <meccha/core/image_project.hpp>
#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <string_view>
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

auto mime_valid(ImageMime mime) -> bool
{
    return mime == ImageMime::Png || mime == ImageMime::Jpeg ||
           mime == ImageMime::WebP;
}

auto lowercase_hex(std::string_view value, std::size_t length) -> bool
{
    return value.size() == length &&
           std::ranges::all_of(
               value,
               [](unsigned char character)
               {
                   return (character >= '0' && character <= '9') ||
                          (character >= 'a' && character <= 'f');
               });
}

auto valid_metadata_name(
    std::string_view value,
    std::size_t maximum_bytes) -> bool
{
    return !value.empty() && value.size() <= maximum_bytes &&
           valid_utf8(value) &&
           std::ranges::none_of(
               value,
               [](unsigned char character)
               {
                   return character < 0x20U || character == 0x7FU;
               });
}
} // namespace

auto valid_image_project_id(std::string_view project_id) -> bool
{
    return lowercase_hex(project_id, 32U);
}

auto validate(const ImageLayer& layer)
    -> std::vector<ImageLayerField>
{
    auto errors = std::vector<ImageLayerField>{};
    if (layer.asset_id.empty())
    {
        errors.push_back(ImageLayerField::AssetId);
    }
    if (!valid_metadata_name(layer.file_name, 260U))
    {
        errors.push_back(ImageLayerField::FileName);
    }
    if (!mime_valid(layer.mime))
    {
        errors.push_back(ImageLayerField::Mime);
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
    const ImageProjectSettings& settings)
    -> std::vector<ImageProjectError>
{
    auto errors = std::vector<ImageProjectError>{};
    if (settings.body != BodyProfile::Round &&
        settings.body != BodyProfile::Cube &&
        settings.body != BodyProfile::Fukuyoka)
    {
        errors.push_back(ImageProjectError::BodyProfile);
    }
    if (settings.placement != PlacementMode::Fit &&
        settings.placement != PlacementMode::Fill)
    {
        errors.push_back(ImageProjectError::Placement);
    }
    if (settings.alpha != AlphaMode::Skip &&
        settings.alpha != AlphaMode::Background)
    {
        errors.push_back(ImageProjectError::AlphaMode);
    }
    const auto valid_face =
        [](FaceBaseMode mode)
        {
            return mode == FaceBaseMode::Fill ||
                   mode == FaceBaseMode::Skip;
        };
    if (!valid_face(settings.front) ||
        !valid_face(settings.right) ||
        !valid_face(settings.back) ||
        !valid_face(settings.left))
    {
        errors.push_back(ImageProjectError::FaceMode);
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
    const auto setting_errors = validate(settings);
    errors.insert(
        errors.end(),
        setting_errors.begin(),
        setting_errors.end());
    return errors;
}

auto validate(const ImageProject& project)
    -> std::vector<ImageProjectField>
{
    auto errors = std::vector<ImageProjectField>{};
    if (project.schema_version != ImageProjectSchemaVersion)
    {
        errors.push_back(ImageProjectField::SchemaVersion);
    }
    if (!valid_image_project_id(project.project_id))
    {
        errors.push_back(ImageProjectField::ProjectId);
    }
    if (!valid_metadata_name(project.display_name, 256U))
    {
        errors.push_back(ImageProjectField::DisplayName);
    }
    if (project.revision == 0U)
    {
        errors.push_back(ImageProjectField::Revision);
    }
    if (!validate(project.settings).empty())
    {
        errors.push_back(ImageProjectField::Settings);
    }
    if (project.layers.empty() ||
        project.layers.size() > MaximumImageLayers)
    {
        errors.push_back(ImageProjectField::LayerCount);
    }
    if (std::ranges::any_of(
            project.layers,
            [](const ImageLayer& layer)
            {
                return !validate(layer).empty();
            }))
    {
        errors.push_back(ImageProjectField::Layers);
    }
    if (project.sources.empty() ||
        project.sources.size() > MaximumImageSources)
    {
        errors.push_back(ImageProjectField::SourceCount);
    }

    auto source_ids = std::set<std::string, std::less<>>{};
    auto source_bytes = std::size_t{};
    auto source_overflow = false;
    auto invalid_source_identity = false;
    auto invalid_source_codec = false;
    auto invalid_source_content = false;
    for (const auto& source : project.sources)
    {
        if (!lowercase_hex(source.asset_id, 64U) ||
            !source_ids.insert(source.asset_id).second)
        {
            invalid_source_identity = true;
        }
        if (!mime_valid(source.mime))
        {
            invalid_source_codec = true;
        }
        if (!source.bytes || source.bytes->empty() ||
            source.bytes->size() > MaximumImageSourceBytes)
        {
            invalid_source_content = true;
        }
        else if (
            source.bytes->size() >
            std::numeric_limits<std::size_t>::max() - source_bytes)
        {
            source_overflow = true;
        }
        else
        {
            source_bytes += source.bytes->size();
        }
    }
    if (invalid_source_identity)
    {
        errors.push_back(ImageProjectField::SourceIdentity);
    }
    if (invalid_source_codec)
    {
        errors.push_back(ImageProjectField::SourceCodec);
    }
    if (invalid_source_content)
    {
        errors.push_back(ImageProjectField::SourceContent);
    }
    if (source_overflow ||
        source_bytes > MaximumProjectSourceBytes)
    {
        errors.push_back(ImageProjectField::SourceSize);
    }

    auto invalid_reference = false;
    auto referenced_ids = std::set<std::string, std::less<>>{};
    for (const auto& layer : project.layers)
    {
        const auto source = std::ranges::find_if(
            project.sources,
            [&layer](const ImageSourceAsset& candidate)
            {
                return candidate.asset_id == layer.asset_id;
            });
        if (source == project.sources.end() ||
            source->mime != layer.mime || !source->bytes ||
            source->bytes->size() != layer.source_bytes)
        {
            invalid_reference = true;
        }
        else
        {
            referenced_ids.insert(source->asset_id);
        }
    }
    if (invalid_reference ||
        referenced_ids.size() != project.sources.size())
    {
        errors.push_back(ImageProjectField::SourceReference);
    }
    if (!project.canonical_atlas ||
        project.canonical_atlas->size() !=
            CanonicalAtlasByteLength)
    {
        errors.push_back(ImageProjectField::CanonicalAtlas);
    }
    return errors;
}

auto ImageSourceAsset::operator==(
    const ImageSourceAsset& other) const -> bool
{
    return asset_id == other.asset_id && mime == other.mime &&
           ((!bytes && !other.bytes) ||
            (bytes && other.bytes && *bytes == *other.bytes));
}

auto ImageProject::operator==(const ImageProject& other) const -> bool
{
    return schema_version == other.schema_version &&
           project_id == other.project_id &&
           display_name == other.display_name &&
           revision == other.revision &&
           settings == other.settings &&
           layers == other.layers &&
           sources == other.sources &&
           ((!canonical_atlas && !other.canonical_atlas) ||
            (canonical_atlas && other.canonical_atlas &&
             *canonical_atlas == *other.canonical_atlas));
}
} // namespace meccha::core
