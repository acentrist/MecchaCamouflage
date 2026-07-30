#include <meccha/ui/image_editor.hpp>

#include <meccha/core/image_compositor.hpp>
#include <meccha/core/image_mapping.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>

namespace meccha::ui
{
namespace
{
constexpr auto CropZoomMinimum = 1.0;
constexpr auto CropZoomMaximum = 4.0;
constexpr auto ComparisonTolerance = 0.000001;

auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto unit(double value) -> bool
{
    return finite(value) && value >= 0.0 && value <= 1.0;
}

auto valid_layer(const core::ImageLayer& layer) -> bool
{
    return core::validate(layer).empty();
}

auto valid_crop(const core::NormalizedCrop& crop) -> bool
{
    return unit(crop.x) && unit(crop.y) &&
           finite(crop.width) && finite(crop.height) &&
           crop.width > 0.0 && crop.height > 0.0 &&
           crop.width <= 1.0 && crop.height <= 1.0 &&
           crop.x + crop.width <= 1.0 + ComparisonTolerance &&
           crop.y + crop.height <= 1.0 + ComparisonTolerance;
}

auto crop_at(
    const core::NormalizedCrop& base,
    double zoom,
    double center_x,
    double center_y) -> core::NormalizedCrop
{
    const auto width = base.width / zoom;
    const auto height = base.height / zoom;
    return {
        std::clamp(center_x - width * 0.5, 0.0, 1.0 - width),
        std::clamp(center_y - height * 0.5, 0.0, 1.0 - height),
        width,
        height,
    };
}

auto valid_crop_session(const ImageCropSession& session) -> bool
{
    if (session.asset_id.empty() ||
        !valid_crop(session.original) ||
        !valid_crop(session.base) ||
        !valid_crop(session.draft) ||
        !finite(session.zoom) ||
        session.zoom < CropZoomMinimum ||
        session.zoom > CropZoomMaximum)
    {
        return false;
    }
    return std::abs(
               session.draft.width -
               session.base.width / session.zoom) <=
               ComparisonTolerance &&
           std::abs(
               session.draft.height -
               session.base.height / session.zoom) <=
               ComparisonTolerance;
}
} // namespace

auto begin_image_crop(
    std::size_t layer_index,
    const core::ImageLayer& layer,
    std::uint32_t source_width,
    std::uint32_t source_height)
    -> std::expected<ImageCropSession, ImageCropError>
{
    if (!valid_layer(layer))
    {
        return std::unexpected(ImageCropError::InvalidLayer);
    }
    if (source_width == 0U || source_height == 0U ||
        source_width > core::MaximumDecodedImageDimension ||
        source_height > core::MaximumDecodedImageDimension)
    {
        return std::unexpected(ImageCropError::InvalidSource);
    }
    const auto source_aspect =
        static_cast<double>(source_width) /
        static_cast<double>(source_height);
    const auto target_width =
        layer.width *
        static_cast<double>(core::CanonicalAtlasWidth);
    const auto target_height =
        layer.height *
        static_cast<double>(core::CanonicalAtlasHeight);
    const auto target_aspect = target_width / target_height;
    if (!finite(source_aspect) || !finite(target_aspect) ||
        source_aspect <= 0.0 || target_aspect <= 0.0)
    {
        return std::unexpected(ImageCropError::InvalidSource);
    }

    auto base = core::NormalizedCrop{};
    if (source_aspect > target_aspect)
    {
        base.width = target_aspect / source_aspect;
        base.x = (1.0 - base.width) * 0.5;
    }
    else
    {
        base.height = source_aspect / target_aspect;
        base.y = (1.0 - base.height) * 0.5;
    }
    const auto original = layer.crop;
    const auto center_x = original.x + original.width * 0.5;
    const auto center_y = original.y + original.height * 0.5;
    const auto zoom = std::clamp(
        std::min(
            base.width / original.width,
            base.height / original.height),
        CropZoomMinimum,
        CropZoomMaximum);
    const auto draft =
        crop_at(base, zoom, center_x, center_y);
    auto result = ImageCropSession{
        layer_index,
        layer.asset_id,
        original,
        base,
        draft,
        zoom,
    };
    if (!valid_crop_session(result))
    {
        return std::unexpected(ImageCropError::InvalidSession);
    }
    return result;
}

auto set_image_crop_zoom(
    ImageCropSession session,
    double zoom)
    -> std::expected<ImageCropSession, ImageCropError>
{
    if (!valid_crop_session(session))
    {
        return std::unexpected(ImageCropError::InvalidSession);
    }
    if (!finite(zoom) || zoom < CropZoomMinimum ||
        zoom > CropZoomMaximum)
    {
        return std::unexpected(ImageCropError::InvalidZoom);
    }
    const auto center_x =
        session.draft.x + session.draft.width * 0.5;
    const auto center_y =
        session.draft.y + session.draft.height * 0.5;
    session.zoom = zoom;
    session.draft =
        crop_at(session.base, zoom, center_x, center_y);
    return session;
}

auto move_image_crop_center(
    ImageCropSession session,
    double center_x,
    double center_y)
    -> std::expected<ImageCropSession, ImageCropError>
{
    if (!valid_crop_session(session))
    {
        return std::unexpected(ImageCropError::InvalidSession);
    }
    if (!finite(center_x) || !finite(center_y))
    {
        return std::unexpected(ImageCropError::InvalidCenter);
    }
    session.draft = crop_at(
        session.base,
        session.zoom,
        center_x,
        center_y);
    return session;
}

auto apply_image_crop(
    const ImageCropSession& session,
    const core::ImageLayer& layer)
    -> std::expected<core::ImageLayer, ImageCropError>
{
    if (!valid_crop_session(session))
    {
        return std::unexpected(ImageCropError::InvalidSession);
    }
    if (!valid_layer(layer) ||
        layer.asset_id != session.asset_id)
    {
        return std::unexpected(ImageCropError::InvalidLayer);
    }
    auto result = layer;
    result.crop = session.draft;
    if (!valid_layer(result))
    {
        return std::unexpected(ImageCropError::InvalidSession);
    }
    return result;
}

auto restore_image_crop(
    const ImageCropSession& session,
    const core::ImageLayer& layer)
    -> std::expected<core::ImageLayer, ImageCropError>
{
    if (!valid_crop_session(session))
    {
        return std::unexpected(ImageCropError::InvalidSession);
    }
    if (!valid_layer(layer) ||
        layer.asset_id != session.asset_id)
    {
        return std::unexpected(ImageCropError::InvalidLayer);
    }
    auto result = layer;
    result.crop = session.original;
    if (!valid_layer(result))
    {
        return std::unexpected(ImageCropError::InvalidSession);
    }
    return result;
}
} // namespace meccha::ui
