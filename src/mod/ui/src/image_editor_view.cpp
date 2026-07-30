#include <meccha/ui/image_editor.hpp>

#include <meccha/core/image_mapping.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <utility>

namespace meccha::ui
{
namespace
{
constexpr auto RetainedHandleDrawSize = 20.0;

auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto valid_rect(const CanvasRect& rect) -> bool
{
    return finite(rect.x) && finite(rect.y) &&
           finite(rect.width) && finite(rect.height) &&
           rect.width > 0.0 && rect.height > 0.0 &&
           finite(rect.x + rect.width) &&
           finite(rect.y + rect.height);
}

auto valid_layer(const core::ImageLayer& layer) -> bool
{
    return core::validate(layer).empty();
}

auto valid_layers(
    std::span<const core::ImageLayer> layers) -> bool
{
    return layers.size() <= core::MaximumImageLayers &&
           std::ranges::all_of(layers, valid_layer);
}

auto canvas_point(
    const CanvasRect& atlas_rect,
    double u,
    double v) -> CanvasPoint
{
    return {
        atlas_rect.x + u * atlas_rect.width,
        atlas_rect.y + v * atlas_rect.height,
    };
}

auto layer_rect(
    const CanvasRect& atlas_rect,
    const core::ImageLayer& layer) -> CanvasRect
{
    const auto width = layer.width * atlas_rect.width;
    const auto height = layer.height * atlas_rect.height;
    return {
        atlas_rect.x +
            (layer.center_x - layer.width * 0.5) *
                atlas_rect.width,
        atlas_rect.y +
            (layer.center_y - layer.height * 0.5) *
                atlas_rect.height,
        width,
        height,
    };
}

auto corner_points(
    const CanvasRect& atlas_rect,
    const core::ImageLayer& layer)
    -> std::array<CanvasPoint, 4U>
{
    const auto left = layer.center_x - layer.width * 0.5;
    const auto right = layer.center_x + layer.width * 0.5;
    const auto top = layer.center_y - layer.height * 0.5;
    const auto bottom = layer.center_y + layer.height * 0.5;
    return {
        canvas_point(atlas_rect, left, top),
        canvas_point(atlas_rect, right, top),
        canvas_point(atlas_rect, left, bottom),
        canvas_point(atlas_rect, right, bottom),
    };
}

class CanvasClipLease
{
public:
    explicit CanvasClipLease(CanvasFrameBuilder& canvas)
        : canvas_{canvas}
    {
    }

    CanvasClipLease(const CanvasClipLease&) = delete;
    auto operator=(const CanvasClipLease&)
        -> CanvasClipLease& = delete;

    ~CanvasClipLease()
    {
        while (depth_ > 0U)
        {
            (void)canvas_.pop_clip();
            --depth_;
        }
    }

    auto push(CanvasRect clip) -> std::expected<void, CanvasError>
    {
        const auto result = canvas_.push_clip(clip);
        if (result)
        {
            ++depth_;
        }
        return result;
    }

    auto close() -> std::expected<void, CanvasError>
    {
        while (depth_ > 0U)
        {
            const auto result = canvas_.pop_clip();
            if (!result)
            {
                return result;
            }
            --depth_;
        }
        return {};
    }

private:
    CanvasFrameBuilder& canvas_;
    std::size_t depth_{};
};

auto add_border(
    CanvasFrameBuilder& canvas,
    const CanvasRect& rect,
    CanvasColor color,
    double thickness)
    -> std::expected<void, CanvasError>
{
    const auto right = rect.x + rect.width;
    const auto bottom = rect.y + rect.height;
    const std::array edges{
        std::pair{
            CanvasPoint{rect.x, rect.y},
            CanvasPoint{right, rect.y},
        },
        std::pair{
            CanvasPoint{right, rect.y},
            CanvasPoint{right, bottom},
        },
        std::pair{
            CanvasPoint{right, bottom},
            CanvasPoint{rect.x, bottom},
        },
        std::pair{
            CanvasPoint{rect.x, bottom},
            CanvasPoint{rect.x, rect.y},
        },
    };
    for (const auto& [start, end] : edges)
    {
        if (const auto result =
                canvas.add_line(start, end, color, thickness);
            !result)
        {
            return std::unexpected(result.error());
        }
    }
    return {};
}
} // namespace

auto draw_image_editor(
    CanvasFrameBuilder& canvas,
    CanvasRect atlas_rect,
    CanvasRect clip,
    const ImageEditorView& view,
    ImageEditorPalette palette)
    -> std::expected<ImageEditorDrawResult, ImageEditorDrawError>
{
    if (!valid_rect(atlas_rect) || !valid_rect(clip))
    {
        return std::unexpected(ImageEditorDrawError{
            ImageEditorDrawValidationError::InvalidGeometry});
    }
    if (view.atlas_texture.identity == 0U ||
        (view.body != core::BodyProfile::Round &&
         view.body != core::BodyProfile::Cube &&
         view.body != core::BodyProfile::Fukuyoka))
    {
        return std::unexpected(ImageEditorDrawError{
            ImageEditorDrawValidationError::InvalidAtlas});
    }
    if (!valid_layers(view.layers))
    {
        return std::unexpected(ImageEditorDrawError{
            ImageEditorDrawValidationError::InvalidLayer});
    }
    if (view.selected_layer &&
        *view.selected_layer >= view.layers.size())
    {
        return std::unexpected(ImageEditorDrawError{
            ImageEditorDrawValidationError::InvalidSelection});
    }
    if (view.guide &&
        (view.guide->schema_version !=
             ImageGuideOverlaySchemaVersion ||
         view.guide->texture.identity == 0U ||
         view.guide->profile.body != view.body ||
         view.guide->profile.role !=
             core::MeshProfileRole::ImageReference ||
         !core::validate(view.guide->profile).empty()))
    {
        return std::unexpected(ImageEditorDrawError{
            ImageEditorDrawValidationError::InvalidGuide});
    }

    auto clips = CanvasClipLease{canvas};
    if (const auto outer = clips.push(clip); !outer)
    {
        return std::unexpected(
            ImageEditorDrawError{outer.error()});
    }
    if (const auto editor = clips.push(atlas_rect); !editor)
    {
        return std::unexpected(
            ImageEditorDrawError{editor.error()});
    }
    const auto atlas = canvas.add_texture(
        view.atlas_texture,
        atlas_rect,
        CanvasUvRect{},
        CanvasColor{255U, 255U, 255U, 255U});
    if (!atlas)
    {
        return std::unexpected(
            ImageEditorDrawError{atlas.error()});
    }

    auto guide_drawn = false;
    if (view.guide)
    {
        const auto guide = canvas.add_texture(
            view.guide->texture,
            atlas_rect,
            CanvasUvRect{},
            CanvasColor{255U, 255U, 255U, 255U});
        if (!guide)
        {
            return std::unexpected(
                ImageEditorDrawError{guide.error()});
        }
        guide_drawn = *guide;
    }

    auto outlines = std::size_t{};
    if (view.editing)
    {
        for (auto index = std::size_t{};
             index < view.layers.size();
             ++index)
        {
            const auto rect =
                layer_rect(atlas_rect, view.layers[index]);
            if (!valid_rect(rect))
            {
                return std::unexpected(ImageEditorDrawError{
                    ImageEditorDrawValidationError::InvalidLayer});
            }
            const auto selected =
                view.selected_layer == index;
            const auto border = add_border(
                canvas,
                rect,
                selected
                    ? palette.selected_border
                    : palette.layer_border,
                selected ? 4.0 : 2.0);
            if (!border)
            {
                return std::unexpected(
                    ImageEditorDrawError{border.error()});
            }
            ++outlines;
            if (!selected)
            {
                continue;
            }

            const auto handle_width =
                RetainedHandleDrawSize /
                static_cast<double>(
                    core::CanonicalAtlasWidth) *
                atlas_rect.width;
            const auto handle_height =
                RetainedHandleDrawSize /
                static_cast<double>(
                    core::CanonicalAtlasHeight) *
                atlas_rect.height;
            for (const auto& point :
                 corner_points(atlas_rect, view.layers[index]))
            {
                const auto handle = canvas.add_filled_box(
                    CanvasRect{
                        point.x - handle_width * 0.5,
                        point.y - handle_height * 0.5,
                        handle_width,
                        handle_height,
                    },
                    palette.resize_handle);
                if (!handle)
                {
                    return std::unexpected(
                        ImageEditorDrawError{handle.error()});
                }
            }
        }
    }
    if (const auto closed = clips.close(); !closed)
    {
        return std::unexpected(
            ImageEditorDrawError{closed.error()});
    }
    return ImageEditorDrawResult{outlines, guide_drawn};
}
} // namespace meccha::ui
