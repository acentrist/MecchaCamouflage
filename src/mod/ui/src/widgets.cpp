#include <meccha/ui/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <expected>
#include <optional>
#include <utility>

namespace meccha::ui
{
namespace
{
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

auto consume(std::expected<bool, CanvasError> result)
    -> std::expected<void, WidgetError>
{
    if (!result)
    {
        return std::unexpected(WidgetError{result.error()});
    }
    return {};
}

template <typename Draw>
auto inside_clip(
    CanvasFrameBuilder& canvas,
    CanvasRect clip,
    Draw&& draw) -> std::expected<void, WidgetError>
{
    const auto pushed = canvas.push_clip(clip);
    if (!pushed)
    {
        return std::unexpected(WidgetError{pushed.error()});
    }

    auto result = std::forward<Draw>(draw)();
    const auto popped = canvas.pop_clip();
    if (!result)
    {
        return result;
    }
    if (!popped)
    {
        return std::unexpected(WidgetError{popped.error()});
    }
    return {};
}

auto paint_border(
    CanvasFrameBuilder& canvas,
    CanvasRect rect,
    CanvasColor color,
    double thickness) -> std::expected<void, WidgetError>
{
    const auto top = consume(canvas.add_line(
        CanvasPoint{rect.x, rect.y},
        CanvasPoint{rect.x + rect.width, rect.y},
        color,
        thickness));
    if (!top)
    {
        return top;
    }
    const auto right = consume(canvas.add_line(
        CanvasPoint{rect.x + rect.width, rect.y},
        CanvasPoint{
            rect.x + rect.width,
            rect.y + rect.height,
        },
        color,
        thickness));
    if (!right)
    {
        return right;
    }
    const auto bottom = consume(canvas.add_line(
        CanvasPoint{
            rect.x + rect.width,
            rect.y + rect.height,
        },
        CanvasPoint{rect.x, rect.y + rect.height},
        color,
        thickness));
    if (!bottom)
    {
        return bottom;
    }
    return consume(canvas.add_line(
        CanvasPoint{rect.x, rect.y + rect.height},
        CanvasPoint{rect.x, rect.y},
        color,
        thickness));
}

auto channel(double value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(value), 0L, 255L));
}
} // namespace

auto default_widget_palette(CanvasColor accent) -> WidgetPalette
{
    return WidgetPalette{
        CanvasColor{32U, 35U, 43U, 235U},
        CanvasColor{48U, 54U, 66U, 245U},
        CanvasColor{22U, 25U, 31U, 250U},
        CanvasColor{42U, 64U, 74U, 245U},
        CanvasColor{50U, 50U, 54U, 170U},
        CanvasColor{108U, 114U, 128U, 255U},
        CanvasColor{245U, 247U, 250U, 255U},
        accent,
        CanvasColor{72U, 78U, 90U, 255U},
    };
}

WidgetPainter::WidgetPainter(
    CanvasFrameBuilder& canvas,
    InteractionFrame& interaction,
    WidgetPalette palette,
    double scale)
    : canvas_{canvas},
      interaction_{interaction},
      palette_{palette},
      scale_{scale}
{
    if (!finite(scale_) || scale_ < 0.25 ||
        scale_ > MaximumCanvasTextScale)
    {
        initial_error_ = WidgetValidationError::InvalidScale;
    }
}

auto WidgetPainter::validate() const
    -> std::expected<void, WidgetError>
{
    if (initial_error_)
    {
        return std::unexpected(WidgetError{*initial_error_});
    }
    return {};
}

auto WidgetPainter::button(
    WidgetId id,
    CanvasRect rect,
    CanvasRect clip,
    std::string_view label,
    bool enabled,
    bool selected)
    -> std::expected<WidgetResponse, WidgetError>
{
    if (const auto state = validate(); !state)
    {
        return std::unexpected(state.error());
    }
    const auto response =
        interaction_.control(id, rect, clip, enabled, true);
    if (!response)
    {
        return std::unexpected(WidgetError{response.error()});
    }

    auto background = palette_.control;
    if (!enabled)
    {
        background = palette_.disabled;
    }
    else if (response->held)
    {
        background = palette_.pressed;
    }
    else if (response->hovered)
    {
        background = palette_.hovered;
    }
    else if (selected)
    {
        background = palette_.selected;
    }

    const auto painted = inside_clip(
        canvas_,
        clip,
        [&]() -> std::expected<void, WidgetError>
        {
            if (const auto fill = consume(
                    canvas_.add_filled_box(rect, background));
                !fill)
            {
                return fill;
            }
            if (const auto border = paint_border(
                    canvas_,
                    rect,
                    selected ? palette_.accent : palette_.border,
                    std::clamp(scale_, 0.5, 4.0));
                !border)
            {
                return border;
            }
            return consume(canvas_.add_text(
                CanvasPoint{
                    rect.x + 10.0 * scale_,
                    rect.y +
                        std::max(
                            2.0 * scale_,
                            (rect.height - 14.0 * scale_) * 0.5),
                },
                label,
                enabled ? palette_.text : palette_.border,
                std::max(0.25, 0.85 * scale_)));
        });
    if (!painted)
    {
        return std::unexpected(painted.error());
    }
    return *response;
}

auto WidgetPainter::toggle(
    WidgetId id,
    CanvasRect rect,
    CanvasRect clip,
    std::string_view label,
    bool value,
    bool enabled)
    -> std::expected<ToggleResult, WidgetError>
{
    const auto response =
        button(id, rect, clip, label, enabled, value);
    if (!response)
    {
        return std::unexpected(response.error());
    }
    const auto indicator_size = std::min({
        18.0 * scale_,
        rect.height - 8.0 * scale_,
        rect.width * 0.2,
    });
    if (!finite(indicator_size) || indicator_size <= 0.0)
    {
        return std::unexpected(WidgetError{
            WidgetValidationError::InvalidGeometry});
    }
    const auto next_value =
        response->activated ? !value : value;
    const auto indicator = CanvasRect{
        rect.x + rect.width -
            indicator_size - 8.0 * scale_,
        rect.y + (rect.height - indicator_size) * 0.5,
        indicator_size,
        indicator_size,
    };
    const auto painted = inside_clip(
        canvas_,
        clip,
        [&]() -> std::expected<void, WidgetError>
        {
            return consume(canvas_.add_filled_box(
                indicator,
                next_value ? palette_.accent : palette_.track));
        });
    if (!painted)
    {
        return std::unexpected(painted.error());
    }
    return ToggleResult{
        *response,
        next_value,
        next_value != value,
    };
}

auto WidgetPainter::slider(
    WidgetId id,
    CanvasRect rect,
    CanvasRect clip,
    double value,
    double minimum,
    double maximum,
    bool enabled)
    -> std::expected<SliderResult, WidgetError>
{
    if (const auto state = validate(); !state)
    {
        return std::unexpected(state.error());
    }
    if (!valid_rect(rect))
    {
        return std::unexpected(WidgetError{
            WidgetValidationError::InvalidGeometry});
    }
    if (!finite(minimum) || !finite(maximum) ||
        minimum >= maximum)
    {
        return std::unexpected(WidgetError{
            WidgetValidationError::InvalidRange});
    }
    if (!finite(value) || value < minimum || value > maximum)
    {
        return std::unexpected(WidgetError{
            WidgetValidationError::InvalidValue});
    }

    const auto response =
        interaction_.control(id, rect, clip, enabled, true);
    if (!response)
    {
        return std::unexpected(WidgetError{response.error()});
    }
    auto next_value = value;
    if (enabled &&
        (response->pressed || response->held ||
         response->released))
    {
        const auto ratio = std::clamp(
            (interaction_.pointer().position.x - rect.x) /
                rect.width,
            0.0,
            1.0);
        next_value = minimum + ratio * (maximum - minimum);
    }

    const auto inset = std::min(8.0 * scale_, rect.width * 0.2);
    const auto track_width = rect.width - 2.0 * inset;
    const auto track_height =
        std::min(4.0 * scale_, rect.height * 0.25);
    const auto thumb_width =
        std::min(10.0 * scale_, rect.width * 0.2);
    const auto thumb_height =
        std::max(1.0, rect.height - 8.0 * scale_);
    if (track_width <= 0.0 || track_height <= 0.0 ||
        thumb_width <= 0.0 || thumb_height <= 0.0)
    {
        return std::unexpected(WidgetError{
            WidgetValidationError::InvalidGeometry});
    }
    const auto ratio =
        (next_value - minimum) / (maximum - minimum);
    const auto track = CanvasRect{
        rect.x + inset,
        rect.y + (rect.height - track_height) * 0.5,
        track_width,
        track_height,
    };
    const auto thumb_center =
        track.x + track.width * ratio;
    const auto thumb = CanvasRect{
        thumb_center - thumb_width * 0.5,
        rect.y + (rect.height - thumb_height) * 0.5,
        thumb_width,
        thumb_height,
    };

    const auto painted = inside_clip(
        canvas_,
        clip,
        [&]() -> std::expected<void, WidgetError>
        {
            if (const auto background = consume(
                    canvas_.add_filled_box(
                        rect,
                        enabled
                            ? palette_.control
                            : palette_.disabled));
                !background)
            {
                return background;
            }
            if (const auto track_result = consume(
                    canvas_.add_filled_box(track, palette_.track));
                !track_result)
            {
                return track_result;
            }
            return consume(canvas_.add_filled_box(
                thumb,
                enabled ? palette_.accent : palette_.border));
        });
    if (!painted)
    {
        return std::unexpected(painted.error());
    }
    return SliderResult{
        *response,
        next_value,
        next_value != value,
    };
}

auto WidgetPainter::color_control(
    std::array<WidgetId, 3> channel_ids,
    CanvasRect rect,
    CanvasRect clip,
    CanvasColor value,
    bool enabled)
    -> std::expected<ColorControlResult, WidgetError>
{
    if (const auto state = validate(); !state)
    {
        return std::unexpected(state.error());
    }
    if (!valid_rect(rect))
    {
        return std::unexpected(WidgetError{
            WidgetValidationError::InvalidGeometry});
    }
    const auto gap = 4.0 * scale_;
    const auto swatch_width =
        std::min(44.0 * scale_, rect.width * 0.25);
    const auto channel_x =
        rect.x + swatch_width + 8.0 * scale_;
    const auto channel_width =
        rect.x + rect.width - channel_x;
    const auto channel_height =
        (rect.height - 2.0 * gap) / 3.0;
    if (channel_width <= 0.0 || channel_height <= 0.0)
    {
        return std::unexpected(WidgetError{
            WidgetValidationError::InvalidGeometry});
    }

    const auto swatch = inside_clip(
        canvas_,
        clip,
        [&]() -> std::expected<void, WidgetError>
        {
            return consume(canvas_.add_filled_box(
                CanvasRect{
                    rect.x,
                    rect.y,
                    swatch_width,
                    rect.height,
                },
                value));
        });
    if (!swatch)
    {
        return std::unexpected(swatch.error());
    }

    auto channels = std::array<double, 3>{
        static_cast<double>(value.red),
        static_cast<double>(value.green),
        static_cast<double>(value.blue),
    };
    auto changed = false;
    for (auto index = std::size_t{}; index < channels.size(); ++index)
    {
        const auto result = slider(
            channel_ids[index],
            CanvasRect{
                channel_x,
                rect.y +
                    static_cast<double>(index) *
                        (channel_height + gap),
                channel_width,
                channel_height,
            },
            clip,
            channels[index],
            0.0,
            255.0,
            enabled);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        channels[index] = result->value;
        changed = changed || result->changed;
    }

    return ColorControlResult{
        CanvasColor{
            channel(channels[0]),
            channel(channels[1]),
            channel(channels[2]),
            value.alpha,
        },
        changed,
    };
}
} // namespace meccha::ui
