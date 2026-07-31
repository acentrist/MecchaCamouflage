#include <meccha/ui/esp_canvas_frame.hpp>

#include <meccha/core/esp.hpp>
#include <meccha/ui/canvas.hpp>

#include <iostream>
#include <string_view>
#include <variant>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL esp_canvas_frame: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto frame = core::EspPrimitiveFrame{};
    frame.lines.push_back(core::EspLinePrimitive{
        11U,
        core::EspPrimitiveKind::Skeleton,
        {12.5, 24.0},
        {700.0, 480.25},
        {1U, 2U, 3U},
        2.5,
    });
    frame.texts.push_back(core::EspTextPrimitive{
        11U,
        {300.0, 120.0},
        "隠れる人",
        {4U, 5U, 6U},
    });

    const auto encoded = ui::encode_esp_canvas_frame(
        ui::CanvasViewport{1920.0, 1080.0, 1.0},
        frame);
    auto passed = expect(
        encoded && encoded->viewport ==
                       ui::CanvasViewport{
                           1920.0,
                           1080.0,
                           1.0} &&
            encoded->primitives.size() == 2U,
        "a valid ESP frame did not become one Canvas frame");
    if (!encoded || encoded->primitives.size() != 2U)
    {
        return 1;
    }

    const auto* line = std::get_if<ui::CanvasLinePrimitive>(
        &encoded->primitives[0]);
    passed &= expect(
        line != nullptr &&
            line->start == ui::CanvasPoint{12.5, 24.0} &&
            line->end == ui::CanvasPoint{700.0, 480.25} &&
            line->color ==
                ui::CanvasColor{1U, 2U, 3U, 255U} &&
            line->thickness == 2.5,
        "the ESP line changed geometry, color, or thickness");

    const auto* text = std::get_if<ui::CanvasTextPrimitive>(
        &encoded->primitives[1]);
    passed &= expect(
        text != nullptr &&
            text->anchor == ui::CanvasPoint{300.0, 120.0} &&
            text->utf8 == "隠れる人" &&
            text->color ==
                ui::CanvasColor{4U, 5U, 6U, 255U} &&
            text->scale == 1.0,
        "the ESP text changed position, UTF-8, color, or scale");

    return passed ? 0 : 1;
}
