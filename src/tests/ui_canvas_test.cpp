#include <meccha/ui/canvas.hpp>

#include <iostream>
#include <string_view>
#include <utility>
#include <variant>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_canvas: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    auto builder = CanvasFrameBuilder{
        CanvasViewport{800.0, 600.0, 1.0}};
    passed &= expect(
        builder.push_clip(
            CanvasRect{100.0, 100.0, 200.0, 100.0}).has_value(),
        "valid nested clip was rejected");

    const auto line = builder.add_line(
        CanvasPoint{0.0, 125.0},
        CanvasPoint{400.0, 125.0},
        CanvasColor{255U, 0U, 0U, 255U},
        2.0);
    passed &= expect(
        line && *line,
        "crossing line was not clipped into the active region");

    const auto box = builder.add_filled_box(
        CanvasRect{50.0, 50.0, 100.0, 100.0},
        CanvasColor{0U, 255U, 0U, 128U});
    passed &= expect(
        box && *box,
        "partially visible box was not emitted");

    const auto texture = builder.add_texture(
        CanvasTextureHandle{42U},
        CanvasRect{250.0, 50.0, 100.0, 100.0},
        CanvasUvRect{0.0, 0.0, 1.0, 1.0},
        CanvasColor{255U, 255U, 255U, 255U});
    passed &= expect(
        texture && *texture,
        "partially visible texture was not emitted");

    const auto text = builder.add_text(
        CanvasPoint{120.0, 120.0},
        "日本語",
        CanvasColor{255U, 255U, 255U, 255U},
        1.0);
    passed &= expect(text && *text, "valid localized text was rejected");
    passed &= expect(
        builder.add_text(
            CanvasPoint{20.0, 20.0},
            "outside",
            CanvasColor{255U, 255U, 255U, 255U},
            1.0) ==
            false,
        "text outside the active clip was emitted");

    passed &= expect(
        builder.pop_clip().has_value(),
        "nested clip did not pop");
    const auto frame = std::move(builder).finish();
    passed &= expect(
        frame && frame->primitives.size() == 4U,
        "valid frame did not preserve exact primitive count");
    if (!frame)
    {
        return 1;
    }

    const auto& line_primitive =
        std::get<CanvasLinePrimitive>(frame->primitives[0]);
    passed &= expect(
        line_primitive.start == CanvasPoint{100.0, 125.0} &&
            line_primitive.end == CanvasPoint{300.0, 125.0} &&
            line_primitive.clip ==
                CanvasRect{100.0, 100.0, 200.0, 100.0},
        "line clipping geometry drifted");

    const auto& box_primitive =
        std::get<CanvasBoxPrimitive>(frame->primitives[1]);
    passed &= expect(
        box_primitive.rect ==
            CanvasRect{100.0, 100.0, 50.0, 50.0},
        "filled box intersection drifted");

    const auto& texture_primitive =
        std::get<CanvasTexturePrimitive>(frame->primitives[2]);
    passed &= expect(
        texture_primitive.rect ==
                CanvasRect{250.0, 100.0, 50.0, 50.0} &&
            texture_primitive.uv ==
                CanvasUvRect{0.0, 0.5, 0.5, 1.0},
        "texture clipping did not preserve matching UVs");

    const auto& text_primitive =
        std::get<CanvasTextPrimitive>(frame->primitives[3]);
    passed &= expect(
        text_primitive.utf8 == "日本語" &&
            text_primitive.clip ==
                CanvasRect{100.0, 100.0, 200.0, 100.0},
        "localized text lost its active clip");

    auto bounded = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 1.0},
        1U};
    passed &= expect(
        bounded.add_filled_box(
            CanvasRect{0.0, 0.0, 10.0, 10.0},
            CanvasColor{1U, 2U, 3U, 255U}) == true,
        "first bounded primitive was rejected");
    const auto overflow = bounded.add_filled_box(
        CanvasRect{20.0, 20.0, 10.0, 10.0},
        CanvasColor{1U, 2U, 3U, 255U});
    passed &= expect(
        !overflow &&
            overflow.error() == CanvasError::PrimitiveLimit &&
            std::move(bounded).finish()->primitives.size() == 1U,
        "primitive overflow partially mutated the frame");

    auto invalid = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 1.0}};
    const auto invalid_text = std::string{
        "bad"} + static_cast<char>(0xC0);
    passed &= expect(
        invalid.add_text(
            CanvasPoint{1.0, 1.0},
            invalid_text,
            CanvasColor{255U, 255U, 255U, 255U},
            1.0) ==
            std::unexpected(CanvasError::InvalidText),
        "invalid UTF-8 text was accepted");
    passed &= expect(
        !invalid.push_clip(CanvasRect{0.0, 0.0, -1.0, 2.0}) &&
            std::move(invalid).finish()->primitives.empty(),
        "invalid geometry mutated the frame");

    auto empty_clip = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 1.0}};
    passed &= expect(
        empty_clip.push_clip(
            CanvasRect{700.0, 500.0, 10.0, 10.0}).has_value() &&
            empty_clip.add_line(
                CanvasPoint{0.0, 0.0},
                CanvasPoint{800.0, 600.0},
                CanvasColor{255U, 255U, 255U, 255U},
                1.0) == false &&
            empty_clip.add_line(
                CanvasPoint{0.0, 500.0},
                CanvasPoint{800.0, 500.0},
                CanvasColor{255U, 255U, 255U, 255U},
                1.0) == false,
        "an empty nested clip emitted a line or degenerate point");
    passed &= expect(
        std::move(empty_clip).finish() ==
            std::unexpected(CanvasError::UnbalancedClip),
        "an unbalanced clip stack produced a frame");

    auto invalid_viewport = CanvasFrameBuilder{
        CanvasViewport{0.0, 480.0, 1.0}};
    passed &= expect(
        std::move(invalid_viewport).finish() ==
            std::unexpected(CanvasError::InvalidViewport),
        "an invalid viewport produced a frame");

    if (passed)
    {
        std::cout << "PASS ui_canvas\n";
    }
    return passed ? 0 : 1;
}
