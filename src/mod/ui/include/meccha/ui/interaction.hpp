#pragma once

#include <meccha/ui/canvas.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace meccha::ui
{
inline constexpr std::size_t MaximumWidgetsPerFrame = 2'048U;

struct WidgetId
{
    std::uint64_t identity{};

    auto operator==(const WidgetId&) const -> bool = default;
};

struct PointerFrame
{
    CanvasPoint position{};
    bool primary_pressed{};
    bool primary_down{};
    bool primary_released{};
    double wheel_delta{};

    auto operator==(const PointerFrame&) const -> bool = default;
};

struct InteractionState
{
    std::optional<WidgetId> active{};
    std::optional<WidgetId> focused{};

    auto operator==(const InteractionState&) const -> bool = default;
};

struct WidgetResponse
{
    bool hovered{};
    bool pressed{};
    bool held{};
    bool released{};
    bool activated{};

    auto operator==(const WidgetResponse&) const -> bool = default;
};

enum class InteractionError : std::uint8_t
{
    InvalidPointer,
    InvalidState,
    InvalidWidget,
    InvalidGeometry,
    DuplicateWidget,
    WidgetLimit,
};

class InteractionFrame
{
public:
    InteractionFrame(
        InteractionState previous,
        PointerFrame pointer,
        bool panel_open,
        std::size_t widget_limit = MaximumWidgetsPerFrame);

    [[nodiscard]] auto control(
        WidgetId id,
        CanvasRect rect,
        CanvasRect clip,
        bool enabled,
        bool focusable)
        -> std::expected<WidgetResponse, InteractionError>;

    [[nodiscard]] auto finish() &&
        -> std::expected<InteractionState, InteractionError>;

    [[nodiscard]] auto pointer() const -> const PointerFrame&;

private:
    [[nodiscard]] auto ready() const
        -> std::expected<void, InteractionError>;
    auto record_error(InteractionError error) -> void;

    InteractionState state_{};
    PointerFrame pointer_{};
    bool panel_open_{};
    bool press_claimed_{};
    std::size_t widget_limit_{};
    std::optional<InteractionError> error_{};
    std::vector<WidgetId> seen_{};
};
} // namespace meccha::ui
