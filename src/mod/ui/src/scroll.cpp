#include <meccha/ui/scroll.hpp>

#include <algorithm>
#include <cmath>

namespace meccha::ui
{
namespace
{
auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto valid_rect(const CanvasRect& rect, bool allow_empty) -> bool
{
    if (!finite(rect.x) || !finite(rect.y) ||
        !finite(rect.width) || !finite(rect.height))
    {
        return false;
    }
    const auto valid_extent = allow_empty
                                  ? rect.width >= 0.0 &&
                                        rect.height >= 0.0
                                  : rect.width > 0.0 &&
                                        rect.height > 0.0;
    return valid_extent &&
           finite(rect.x + rect.width) &&
           finite(rect.y + rect.height);
}

auto intersect(
    const CanvasRect& left,
    const CanvasRect& right) -> std::optional<CanvasRect>
{
    const auto x0 = std::max(left.x, right.x);
    const auto y0 = std::max(left.y, right.y);
    const auto x1 = std::min(
        left.x + left.width,
        right.x + right.width);
    const auto y1 = std::min(
        left.y + left.height,
        right.y + right.height);
    if (x1 <= x0 || y1 <= y0)
    {
        return std::nullopt;
    }
    return CanvasRect{x0, y0, x1 - x0, y1 - y0};
}

auto contains(const CanvasRect& rect, CanvasPoint point) -> bool
{
    return point.x >= rect.x &&
           point.x < rect.x + rect.width &&
           point.y >= rect.y &&
           point.y < rect.y + rect.height;
}

auto valid_pointer(const PointerFrame& pointer) -> bool
{
    return finite(pointer.position.x) &&
           finite(pointer.position.y) &&
           finite(pointer.wheel_delta) &&
           !(pointer.primary_pressed &&
             !pointer.primary_down &&
             !pointer.primary_released) &&
           !(pointer.primary_released &&
             pointer.primary_down);
}
} // namespace

auto update_scroll_container(
    ScrollState previous,
    ScrollContainerInput input)
    -> std::expected<ScrollContainerSnapshot, ScrollError>
{
    if (!finite(previous.offset_y) || previous.offset_y < 0.0)
    {
        return std::unexpected(ScrollError::InvalidState);
    }
    if (!valid_rect(input.viewport, false) ||
        !valid_rect(input.clip, true))
    {
        return std::unexpected(ScrollError::InvalidGeometry);
    }
    if (!finite(input.content_height) ||
        input.content_height < 0.0)
    {
        return std::unexpected(ScrollError::InvalidContent);
    }
    if (!finite(input.wheel_step) ||
        input.wheel_step <= 0.0 ||
        input.wheel_step > 1'024.0)
    {
        return std::unexpected(ScrollError::InvalidWheelStep);
    }
    if (!valid_pointer(input.pointer))
    {
        return std::unexpected(ScrollError::InvalidPointer);
    }

    const auto maximum = std::max(
        0.0,
        input.content_height - input.viewport.height);
    auto offset = std::clamp(previous.offset_y, 0.0, maximum);
    const auto visible = intersect(input.viewport, input.clip);
    if (visible && contains(*visible, input.pointer.position) &&
        input.pointer.wheel_delta != 0.0)
    {
        offset = std::clamp(
            offset -
                input.pointer.wheel_delta * input.wheel_step,
            0.0,
            maximum);
    }

    return ScrollContainerSnapshot{
        ScrollState{offset},
        visible,
        input.viewport.y - offset,
        maximum,
        offset != previous.offset_y,
    };
}
} // namespace meccha::ui
