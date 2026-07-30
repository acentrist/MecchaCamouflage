#pragma once

#include <meccha/ui/interaction.hpp>

#include <cstdint>
#include <expected>
#include <optional>

namespace meccha::ui
{
struct ScrollState
{
    double offset_y{};

    auto operator==(const ScrollState&) const -> bool = default;
};

struct ScrollContainerInput
{
    CanvasRect viewport{};
    CanvasRect clip{};
    double content_height{};
    double wheel_step{};
    PointerFrame pointer{};
};

struct ScrollContainerSnapshot
{
    ScrollState state{};
    std::optional<CanvasRect> visible_clip{};
    double content_origin_y{};
    double maximum_offset{};
    bool changed{};
};

enum class ScrollError : std::uint8_t
{
    InvalidState,
    InvalidGeometry,
    InvalidContent,
    InvalidWheelStep,
    InvalidPointer,
};

[[nodiscard]] auto update_scroll_container(
    ScrollState previous,
    ScrollContainerInput input)
    -> std::expected<ScrollContainerSnapshot, ScrollError>;
} // namespace meccha::ui
