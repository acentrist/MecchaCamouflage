#pragma once

#include <meccha/ui/canvas.hpp>
#include <meccha/ui/interaction.hpp>
#include <meccha/ui/text_edit.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace meccha::ui
{
struct WidgetPalette
{
    CanvasColor control{};
    CanvasColor hovered{};
    CanvasColor pressed{};
    CanvasColor selected{};
    CanvasColor disabled{};
    CanvasColor border{};
    CanvasColor text{};
    CanvasColor accent{};
    CanvasColor track{};

    auto operator==(const WidgetPalette&) const -> bool = default;
};

[[nodiscard]] auto default_widget_palette(CanvasColor accent)
    -> WidgetPalette;

enum class WidgetValidationError : std::uint8_t
{
    InvalidScale,
    InvalidGeometry,
    InvalidValue,
    InvalidRange,
};

using WidgetError = std::variant<
    CanvasError,
    InteractionError,
    WidgetValidationError,
    TextEditError>;

struct ToggleResult
{
    WidgetResponse interaction{};
    bool value{};
    bool changed{};
};

struct SliderResult
{
    WidgetResponse interaction{};
    double value{};
    bool changed{};
};

struct ColorControlResult
{
    CanvasColor value{};
    bool changed{};
};

struct TextFieldResult
{
    WidgetResponse interaction{};
    TextEditState state{};
    bool changed{};
    bool committed{};
    bool cancelled{};
};

class WidgetPainter
{
public:
    WidgetPainter(
        CanvasFrameBuilder& canvas,
        InteractionFrame& interaction,
        WidgetPalette palette,
        double scale);

    [[nodiscard]] auto button(
        WidgetId id,
        CanvasRect rect,
        CanvasRect clip,
        std::string_view label,
        bool enabled,
        bool selected)
        -> std::expected<WidgetResponse, WidgetError>;

    [[nodiscard]] auto toggle(
        WidgetId id,
        CanvasRect rect,
        CanvasRect clip,
        std::string_view label,
        bool value,
        bool enabled)
        -> std::expected<ToggleResult, WidgetError>;

    [[nodiscard]] auto slider(
        WidgetId id,
        CanvasRect rect,
        CanvasRect clip,
        double value,
        double minimum,
        double maximum,
        bool enabled)
        -> std::expected<SliderResult, WidgetError>;

    [[nodiscard]] auto color_control(
        std::array<WidgetId, 3> channel_ids,
        CanvasRect rect,
        CanvasRect clip,
        CanvasColor value,
        bool enabled)
        -> std::expected<ColorControlResult, WidgetError>;

    [[nodiscard]] auto text_field(
        WidgetId id,
        CanvasRect rect,
        CanvasRect clip,
        TextEditState state,
        std::span<const TextEditEvent> events,
        bool enabled,
        std::size_t maximum_bytes = 256U)
        -> std::expected<TextFieldResult, WidgetError>;

private:
    [[nodiscard]] auto validate() const
        -> std::expected<void, WidgetError>;

    CanvasFrameBuilder& canvas_;
    InteractionFrame& interaction_;
    WidgetPalette palette_{};
    double scale_{};
    std::optional<WidgetValidationError> initial_error_{};
};
} // namespace meccha::ui
