#pragma once

#include <meccha/core/paint.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::core
{
inline constexpr std::size_t MaximumImageSourceBytes =
    12U * 1024U * 1024U;
inline constexpr std::size_t MaximumProjectSourceBytes =
    64U * 1024U * 1024U;
inline constexpr std::size_t MaximumImageLayers = 256U;
inline constexpr std::size_t MaximumImageSources = 256U;
inline constexpr std::size_t CanonicalAtlasByteLength =
    1024U * 512U * 4U;
inline constexpr std::uint32_t ImageProjectSchemaVersion = 1U;

enum class ImageMime : std::uint8_t
{
    Png,
    Jpeg,
    WebP,
};

enum class BodyProfile : std::uint8_t
{
    Round,
    Cube,
    Fukuyoka,
};

enum class PlacementMode : std::uint8_t
{
    Fit,
    Fill,
};

enum class FaceBaseMode : std::uint8_t
{
    Fill,
    Skip,
};

enum class AlphaMode : std::uint8_t
{
    Skip,
    Background,
};

struct NormalizedCrop
{
    double x{};
    double y{};
    double width{1.0};
    double height{1.0};

    auto operator==(const NormalizedCrop&) const -> bool = default;
};

struct ImageLayer
{
    std::string asset_id{};
    std::string file_name{};
    ImageMime mime{ImageMime::Png};
    std::size_t source_bytes{};
    double center_x{0.5};
    double center_y{0.5};
    double width{1.0};
    double height{1.0};
    NormalizedCrop crop{};
    bool wrap_atlas_seam{};
    bool mirror_front_back{};

    auto operator==(const ImageLayer&) const -> bool = default;
};

enum class ImageLayerField : std::uint8_t
{
    AssetId,
    FileName,
    Mime,
    SourceSize,
    Placement,
    Crop,
};

[[nodiscard]] auto validate(const ImageLayer& layer)
    -> std::vector<ImageLayerField>;

struct ImageProjectSettings
{
    BodyProfile body{BodyProfile::Round};
    PlacementMode placement{PlacementMode::Fit};
    AlphaMode alpha{AlphaMode::Skip};
    FaceBaseMode front{FaceBaseMode::Skip};
    FaceBaseMode right{FaceBaseMode::Skip};
    FaceBaseMode back{FaceBaseMode::Skip};
    FaceBaseMode left{FaceBaseMode::Skip};
    double brush_size_texels{5.0};
    double color_compression_tolerance_percent{};
    Material image_material{};
    Rgb8 fill_color{255U, 255U, 255U};
    Material fill_material{1.0, 0.0, 0.0};

    auto operator==(const ImageProjectSettings&) const -> bool = default;
};

enum class ImageProjectError : std::uint8_t
{
    Empty,
    InvalidLayer,
    SourceSizeOverflow,
    SourceSizeLimit,
    BodyProfile,
    Placement,
    AlphaMode,
    FaceMode,
    BrushSize,
    CompressionTolerance,
    ImageMaterial,
    FillMaterial,
};

[[nodiscard]] auto validate(const ImageProjectSettings& settings)
    -> std::vector<ImageProjectError>;

[[nodiscard]] auto validate(
    const ImageProjectSettings& settings,
    const std::vector<ImageLayer>& layers)
    -> std::vector<ImageProjectError>;

struct ImageSourceAsset
{
    std::string asset_id{};
    ImageMime mime{ImageMime::Png};
    std::shared_ptr<const std::vector<std::byte>> bytes{};

    auto operator==(const ImageSourceAsset& other) const -> bool;
};

struct ImageProject
{
    std::uint32_t schema_version{ImageProjectSchemaVersion};
    std::string project_id{};
    std::string display_name{};
    std::uint64_t revision{};
    ImageProjectSettings settings{};
    std::vector<ImageLayer> layers{};
    std::vector<ImageSourceAsset> sources{};
    std::shared_ptr<const std::vector<std::byte>> canonical_atlas{};

    auto operator==(const ImageProject& other) const -> bool;
};

[[nodiscard]] auto valid_image_project_id(
    std::string_view project_id) -> bool;

enum class ImageProjectField : std::uint8_t
{
    SchemaVersion,
    ProjectId,
    DisplayName,
    Revision,
    Settings,
    LayerCount,
    Layers,
    SourceCount,
    SourceIdentity,
    SourceCodec,
    SourceContent,
    SourceReference,
    SourceSize,
    CanonicalAtlas,
};

[[nodiscard]] auto validate(const ImageProject& project)
    -> std::vector<ImageProjectField>;
} // namespace meccha::core
