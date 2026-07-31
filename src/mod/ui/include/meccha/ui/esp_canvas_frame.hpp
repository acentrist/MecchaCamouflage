#pragma once

#include <meccha/core/esp.hpp>
#include <meccha/ui/canvas.hpp>

#include <expected>

namespace meccha::ui
{
[[nodiscard]] auto encode_esp_canvas_frame(
    CanvasViewport viewport,
    const core::EspPrimitiveFrame& frame)
    -> std::expected<CanvasFrame, CanvasError>;
} // namespace meccha::ui
