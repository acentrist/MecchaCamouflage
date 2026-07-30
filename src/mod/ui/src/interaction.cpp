#include <meccha/ui/interaction.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace meccha::ui
{
namespace
{
auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto valid_point(const CanvasPoint& point) -> bool
{
    return finite(point.x) && finite(point.y);
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

auto valid_id(const std::optional<WidgetId>& id) -> bool
{
    return !id || id->identity != 0U;
}

auto intersects_at(
    CanvasPoint point,
    const CanvasRect& rect,
    const CanvasRect& clip) -> bool
{
    const auto left = std::max(rect.x, clip.x);
    const auto top = std::max(rect.y, clip.y);
    const auto right = std::min(
        rect.x + rect.width,
        clip.x + clip.width);
    const auto bottom = std::min(
        rect.y + rect.height,
        clip.y + clip.height);
    return right > left && bottom > top &&
           point.x >= left && point.x < right &&
           point.y >= top && point.y < bottom;
}
} // namespace

InteractionFrame::InteractionFrame(
    InteractionState previous,
    PointerFrame pointer,
    bool panel_open,
    std::size_t widget_limit)
    : state_{std::move(previous)},
      pointer_{pointer},
      panel_open_{panel_open},
      widget_limit_{widget_limit}
{
    if (!valid_point(pointer_.position) ||
        !finite(pointer_.wheel_delta) ||
        (pointer_.primary_pressed &&
         !pointer_.primary_down &&
         !pointer_.primary_released) ||
        (pointer_.primary_released &&
         pointer_.primary_down))
    {
        error_ = InteractionError::InvalidPointer;
        return;
    }
    if (!valid_id(state_.active) || !valid_id(state_.focused))
    {
        error_ = InteractionError::InvalidState;
        return;
    }
    if (widget_limit_ == 0U ||
        widget_limit_ > MaximumWidgetsPerFrame)
    {
        error_ = InteractionError::WidgetLimit;
        return;
    }
    seen_.reserve(std::min<std::size_t>(widget_limit_, 128U));
    if (!panel_open_)
    {
        state_ = {};
    }
}

auto InteractionFrame::ready() const
    -> std::expected<void, InteractionError>
{
    if (error_)
    {
        return std::unexpected(*error_);
    }
    return {};
}

auto InteractionFrame::record_error(InteractionError error) -> void
{
    if (!error_)
    {
        error_ = error;
    }
}

auto InteractionFrame::control(
    WidgetId id,
    CanvasRect rect,
    CanvasRect clip,
    bool enabled,
    bool focusable)
    -> std::expected<WidgetResponse, InteractionError>
{
    if (const auto status = ready(); !status)
    {
        return std::unexpected(status.error());
    }
    if (id.identity == 0U)
    {
        record_error(InteractionError::InvalidWidget);
        return std::unexpected(*error_);
    }
    if (!valid_rect(rect, false) || !valid_rect(clip, true))
    {
        record_error(InteractionError::InvalidGeometry);
        return std::unexpected(*error_);
    }
    if (std::ranges::find(seen_, id) != seen_.end())
    {
        record_error(InteractionError::DuplicateWidget);
        return std::unexpected(*error_);
    }
    if (seen_.size() >= widget_limit_)
    {
        record_error(InteractionError::WidgetLimit);
        return std::unexpected(*error_);
    }
    seen_.push_back(id);

    auto response = WidgetResponse{};
    if (!panel_open_ || !enabled)
    {
        return response;
    }

    response.hovered = intersects_at(pointer_.position, rect, clip);
    if (pointer_.primary_pressed && response.hovered &&
        !state_.active && !press_claimed_)
    {
        state_.active = id;
        press_claimed_ = true;
        response.pressed = true;
    }

    const auto captured = state_.active == id;
    response.held = captured && pointer_.primary_down;
    if (captured && pointer_.primary_released)
    {
        response.released = true;
        response.activated = response.hovered;
        state_.active.reset();
        if (response.activated && focusable)
        {
            state_.focused = id;
        }
    }
    return response;
}

auto InteractionFrame::finish() &&
    -> std::expected<InteractionState, InteractionError>
{
    if (const auto status = ready(); !status)
    {
        return std::unexpected(status.error());
    }
    if (!panel_open_)
    {
        return InteractionState{};
    }
    if (pointer_.primary_released)
    {
        state_.active.reset();
    }
    if (pointer_.primary_pressed && !press_claimed_)
    {
        state_.focused.reset();
    }
    return std::move(state_);
}
} // namespace meccha::ui
