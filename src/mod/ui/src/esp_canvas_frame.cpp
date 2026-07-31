#include <meccha/ui/esp_canvas_frame.hpp>

#include <cstddef>
#include <expected>
#include <utility>

namespace meccha::ui
{
auto encode_esp_canvas_frame(
    CanvasViewport viewport,
    const core::EspPrimitiveFrame& frame)
    -> std::expected<CanvasFrame, CanvasError>
{
    if (frame.lines.size() > MaximumCanvasPrimitives ||
        frame.texts.size() >
            MaximumCanvasPrimitives - frame.lines.size())
    {
        return std::unexpected(CanvasError::PrimitiveLimit);
    }

    auto builder = CanvasFrameBuilder{viewport};
    for (const auto& line : frame.lines)
    {
        const auto added = builder.add_line(
            {line.start.x, line.start.y},
            {line.end.x, line.end.y},
            {
                line.color.red,
                line.color.green,
                line.color.blue,
                255U,
            },
            line.thickness);
        if (!added)
        {
            return std::unexpected(added.error());
        }
    }
    for (const auto& text : frame.texts)
    {
        const auto added = builder.add_text(
            {text.anchor.x, text.anchor.y},
            text.utf8,
            {
                text.color.red,
                text.color.green,
                text.color.blue,
                255U,
            },
            1.0);
        if (!added)
        {
            return std::unexpected(added.error());
        }
    }
    return std::move(builder).finish();
}
} // namespace meccha::ui
