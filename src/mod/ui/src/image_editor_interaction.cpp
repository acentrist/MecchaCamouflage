#include <meccha/ui/image_editor.hpp>

#include <meccha/core/image_mapping.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace meccha::ui
{
namespace
{
constexpr auto RetainedMinimumLayerWidth =
    24.0 / static_cast<double>(core::CanonicalAtlasWidth);
constexpr auto RetainedMinimumLayerHeight =
    24.0 / static_cast<double>(core::CanonicalAtlasHeight);
constexpr auto RetainedHandleHitRadius = 20.0;

auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto valid_point(const CanvasPoint& point) -> bool
{
    return finite(point.x) && finite(point.y);
}

auto valid_event_kind(ImageEditorPointerEventKind kind) -> bool
{
    return kind == ImageEditorPointerEventKind::Press ||
           kind == ImageEditorPointerEventKind::Move ||
           kind == ImageEditorPointerEventKind::Release ||
           kind == ImageEditorPointerEventKind::Cancel;
}

auto valid_rect(const CanvasRect& rect) -> bool
{
    return finite(rect.x) && finite(rect.y) &&
           finite(rect.width) && finite(rect.height) &&
           rect.width > 0.0 && rect.height > 0.0 &&
           finite(rect.x + rect.width) &&
           finite(rect.y + rect.height);
}

auto contains(
    const CanvasRect& rect,
    const CanvasPoint& point) -> bool
{
    return point.x >= rect.x &&
           point.x <= rect.x + rect.width &&
           point.y >= rect.y &&
           point.y <= rect.y + rect.height;
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

auto normalized_point(
    const CanvasRect& atlas_rect,
    const CanvasPoint& point) -> CanvasPoint
{
    return {
        (point.x - atlas_rect.x) / atlas_rect.width,
        (point.y - atlas_rect.y) / atlas_rect.height,
    };
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
    -> std::array<
        std::pair<ImageResizeCorner, CanvasPoint>,
        4U>
{
    const auto left = layer.center_x - layer.width * 0.5;
    const auto right = layer.center_x + layer.width * 0.5;
    const auto top = layer.center_y - layer.height * 0.5;
    const auto bottom = layer.center_y + layer.height * 0.5;
    return {
        std::pair{
            ImageResizeCorner::TopLeft,
            canvas_point(atlas_rect, left, top),
        },
        std::pair{
            ImageResizeCorner::TopRight,
            canvas_point(atlas_rect, right, top),
        },
        std::pair{
            ImageResizeCorner::BottomLeft,
            canvas_point(atlas_rect, left, bottom),
        },
        std::pair{
            ImageResizeCorner::BottomRight,
            canvas_point(atlas_rect, right, bottom),
        },
    };
}

struct HitTarget
{
    std::size_t layer_index{};
    ImageGestureKind kind{ImageGestureKind::Move};
    ImageResizeCorner corner{ImageResizeCorner::BottomRight};
};

auto hit_target(
    std::span<const core::ImageLayer> layers,
    const CanvasRect& atlas_rect,
    const CanvasPoint& point) -> std::optional<HitTarget>
{
    if (!contains(atlas_rect, point))
    {
        return std::nullopt;
    }

    const auto radius_x =
        RetainedHandleHitRadius /
        static_cast<double>(core::CanonicalAtlasWidth) *
        atlas_rect.width;
    const auto radius_y =
        RetainedHandleHitRadius /
        static_cast<double>(core::CanonicalAtlasHeight) *
        atlas_rect.height;
    for (auto index = layers.size(); index > 0U; --index)
    {
        for (const auto& [corner, position] :
             corner_points(atlas_rect, layers[index - 1U]))
        {
            if (std::abs(point.x - position.x) <= radius_x &&
                std::abs(point.y - position.y) <= radius_y)
            {
                return HitTarget{
                    index - 1U,
                    ImageGestureKind::Resize,
                    corner,
                };
            }
        }
    }
    for (auto index = layers.size(); index > 0U; --index)
    {
        if (contains(
                layer_rect(atlas_rect, layers[index - 1U]),
                point))
        {
            return HitTarget{
                index - 1U,
                ImageGestureKind::Move,
                ImageResizeCorner::BottomRight,
            };
        }
    }
    return std::nullopt;
}

auto valid_gesture(
    const ImageEditorGesture& gesture,
    std::span<const core::ImageLayer> layers) -> bool
{
    return gesture.layer_index < layers.size() &&
           valid_layer(gesture.original) &&
           layers[gesture.layer_index].asset_id ==
               gesture.original.asset_id &&
           finite(gesture.origin_u) &&
           finite(gesture.origin_v) &&
           (gesture.kind == ImageGestureKind::Move ||
            gesture.kind == ImageGestureKind::Resize) &&
           (gesture.corner == ImageResizeCorner::TopLeft ||
            gesture.corner == ImageResizeCorner::TopRight ||
            gesture.corner == ImageResizeCorner::BottomLeft ||
            gesture.corner == ImageResizeCorner::BottomRight);
}

auto valid_interaction_state(
    const ImageEditorInteractionState& state,
    std::span<const core::ImageLayer> layers) -> bool
{
    return (!state.selected_layer ||
            *state.selected_layer < layers.size()) &&
           (!state.gesture ||
            valid_gesture(*state.gesture, layers));
}

auto moved_layer(
    const ImageEditorGesture& gesture,
    const core::ImageLayer& current,
    CanvasPoint pointer)
    -> std::expected<
        core::ImageLayer,
        ImageEditorInteractionError>
{
    auto result = current;
    const auto delta_u = pointer.x - gesture.origin_u;
    const auto delta_v = pointer.y - gesture.origin_v;
    if (gesture.kind == ImageGestureKind::Move)
    {
        result.center_x =
            gesture.original.center_x + delta_u;
        result.center_y =
            gesture.original.center_y + delta_v;
    }
    else
    {
        const auto original_left =
            gesture.original.center_x -
            gesture.original.width * 0.5;
        const auto original_right =
            gesture.original.center_x +
            gesture.original.width * 0.5;
        const auto original_top =
            gesture.original.center_y -
            gesture.original.height * 0.5;
        const auto original_bottom =
            gesture.original.center_y +
            gesture.original.height * 0.5;
        auto left = original_left;
        auto right = original_right;
        auto top = original_top;
        auto bottom = original_bottom;
        switch (gesture.corner)
        {
        case ImageResizeCorner::TopLeft:
            left = std::min(
                pointer.x,
                original_right -
                    RetainedMinimumLayerWidth);
            top = std::min(
                pointer.y,
                original_bottom -
                    RetainedMinimumLayerHeight);
            break;
        case ImageResizeCorner::TopRight:
            right = std::max(
                pointer.x,
                original_left +
                    RetainedMinimumLayerWidth);
            top = std::min(
                pointer.y,
                original_bottom -
                    RetainedMinimumLayerHeight);
            break;
        case ImageResizeCorner::BottomLeft:
            left = std::min(
                pointer.x,
                original_right -
                    RetainedMinimumLayerWidth);
            bottom = std::max(
                pointer.y,
                original_top +
                    RetainedMinimumLayerHeight);
            break;
        case ImageResizeCorner::BottomRight:
            right = std::max(
                pointer.x,
                original_left +
                    RetainedMinimumLayerWidth);
            bottom = std::max(
                pointer.y,
                original_top +
                    RetainedMinimumLayerHeight);
            break;
        }
        result.center_x = (left + right) * 0.5;
        result.center_y = (top + bottom) * 0.5;
        result.width = right - left;
        result.height = bottom - top;
    }
    if (!valid_layer(result))
    {
        return std::unexpected(
            ImageEditorInteractionError::InvalidLayer);
    }
    return result;
}

} // namespace

auto update_image_editor_interaction(
    ImageEditorInteractionState previous,
    std::span<const core::ImageLayer> layers,
    CanvasRect atlas_rect,
    std::span<const ImageEditorPointerEvent> events)
    -> std::expected<
        ImageEditorInteractionUpdate,
        ImageEditorInteractionError>
{
    if (!valid_rect(atlas_rect))
    {
        return std::unexpected(
            ImageEditorInteractionError::InvalidGeometry);
    }
    if (!valid_layers(layers))
    {
        return std::unexpected(
            ImageEditorInteractionError::InvalidLayer);
    }
    if (!valid_interaction_state(previous, layers))
    {
        return std::unexpected(
            ImageEditorInteractionError::InvalidState);
    }
    if (events.size() > MaximumImageEditorEventsPerFrame)
    {
        return std::unexpected(
            ImageEditorInteractionError::EventLimit);
    }

    auto result = ImageEditorInteractionUpdate{
        std::move(previous)};
    auto terminal = false;
    for (const auto& event : events)
    {
        if (terminal || !valid_event_kind(event.kind) ||
            !valid_point(event.position))
        {
            return std::unexpected(
                ImageEditorInteractionError::InvalidEvent);
        }
        switch (event.kind)
        {
        case ImageEditorPointerEventKind::Press:
        {
            if (result.state.gesture)
            {
                return std::unexpected(
                    ImageEditorInteractionError::InvalidEvent);
            }
            const auto target =
                hit_target(layers, atlas_rect, event.position);
            if (!target)
            {
                break;
            }
            const auto origin =
                normalized_point(atlas_rect, event.position);
            result.state.selected_layer =
                target->layer_index;
            result.state.gesture = ImageEditorGesture{
                target->kind,
                target->layer_index,
                target->corner,
                origin.x,
                origin.y,
                layers[target->layer_index],
            };
            break;
        }
        case ImageEditorPointerEventKind::Move:
        case ImageEditorPointerEventKind::Release:
            if (result.state.gesture)
            {
                const auto pointer =
                    normalized_point(atlas_rect, event.position);
                const auto changed_layer = moved_layer(
                    *result.state.gesture,
                    layers[result.state.gesture->layer_index],
                    pointer);
                if (!changed_layer)
                {
                    return std::unexpected(
                        changed_layer.error());
                }
                const auto index =
                    result.state.gesture->layer_index;
                result.changed =
                    *changed_layer != layers[index];
                result.edit = ImageLayerEdit{
                    index,
                    *changed_layer,
                };
                if (event.kind ==
                    ImageEditorPointerEventKind::Release)
                {
                    result.state.gesture.reset();
                    result.committed = true;
                    terminal = true;
                }
            }
            break;
        case ImageEditorPointerEventKind::Cancel:
            if (result.state.gesture)
            {
                const auto index =
                    result.state.gesture->layer_index;
                auto restored = layers[index];
                restored.center_x =
                    result.state.gesture->original.center_x;
                restored.center_y =
                    result.state.gesture->original.center_y;
                restored.width =
                    result.state.gesture->original.width;
                restored.height =
                    result.state.gesture->original.height;
                result.changed = restored != layers[index];
                result.edit =
                    ImageLayerEdit{index, std::move(restored)};
                result.state.gesture.reset();
                result.cancelled = true;
            }
            terminal = true;
            break;
        }
    }
    return result;
}

auto reorder_image_layer(
    std::span<const core::ImageLayer> layers,
    std::size_t selected_layer,
    std::size_t destination)
    -> std::expected<
        ImageLayerOrderUpdate,
        ImageEditorInteractionError>
{
    if (!valid_layers(layers))
    {
        return std::unexpected(
            ImageEditorInteractionError::InvalidLayer);
    }
    if (selected_layer >= layers.size() ||
        destination >= layers.size())
    {
        return std::unexpected(
            ImageEditorInteractionError::InvalidState);
    }
    auto result = std::vector<core::ImageLayer>{
        layers.begin(),
        layers.end()};
    auto selected = std::move(result[selected_layer]);
    result.erase(
        result.begin() +
        static_cast<std::ptrdiff_t>(selected_layer));
    result.insert(
        result.begin() +
            static_cast<std::ptrdiff_t>(destination),
        std::move(selected));
    return ImageLayerOrderUpdate{
        std::move(result),
        destination,
    };
}

} // namespace meccha::ui
