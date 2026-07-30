#include <meccha/ui/widgets.hpp>

#include <iostream>
#include <string_view>
#include <utility>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_widgets: " << message << '\n';
    }
    return condition;
}

constexpr auto Viewport =
    meccha::ui::CanvasViewport{800.0, 600.0, 1.0};
constexpr auto Clip =
    meccha::ui::CanvasRect{0.0, 0.0, 800.0, 600.0};
constexpr auto Control =
    meccha::ui::CanvasRect{20.0, 20.0, 200.0, 40.0};
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    auto canvas = CanvasFrameBuilder{Viewport};
    auto interaction = InteractionFrame{
        InteractionState{},
        PointerFrame{
            CanvasPoint{50.0, 40.0},
            true,
            false,
            true,
            0.0,
        },
        true};
    auto widgets = WidgetPainter{
        canvas,
        interaction,
        default_widget_palette(
            CanvasColor{40U, 180U, 220U, 255U}),
        1.0};

    const auto button = widgets.button(
        WidgetId{1U},
        Control,
        Clip,
        "Start",
        true,
        false);
    const auto toggle = widgets.toggle(
        WidgetId{2U},
        CanvasRect{20.0, 80.0, 200.0, 40.0},
        Clip,
        "ESP",
        true,
        true);
    passed &= expect(
        button && button->activated &&
            toggle && toggle->value && !toggle->changed,
        "button activation or stable toggle value drifted");

    const auto interaction_result =
        std::move(interaction).finish();
    const auto canvas_result = std::move(canvas).finish();
    passed &= expect(
        interaction_result &&
            interaction_result->focused == WidgetId{1U} &&
            canvas_result &&
            canvas_result->primitives.size() >= 8U,
        "widget frame did not publish focus and Canvas primitives");

    auto toggle_canvas = CanvasFrameBuilder{Viewport};
    auto toggle_interaction = InteractionFrame{
        InteractionState{},
        PointerFrame{
            CanvasPoint{50.0, 100.0},
            true,
            false,
            true,
            0.0,
        },
        true};
    auto toggle_widgets = WidgetPainter{
        toggle_canvas,
        toggle_interaction,
        default_widget_palette(
            CanvasColor{40U, 180U, 220U, 255U}),
        1.0};
    const auto toggled = toggle_widgets.toggle(
        WidgetId{2U},
        CanvasRect{20.0, 80.0, 200.0, 40.0},
        Clip,
        "ESP",
        true,
        true);
    passed &= expect(
        toggled && !toggled->value && toggled->changed &&
            std::move(toggle_interaction).finish() &&
            std::move(toggle_canvas).finish(),
        "toggle activation did not emit the next value");

    auto slider_canvas = CanvasFrameBuilder{Viewport};
    auto slider_interaction = InteractionFrame{
        InteractionState{},
        PointerFrame{
            CanvasPoint{170.0, 160.0},
            true,
            true,
            false,
            0.0,
        },
        true};
    auto slider_widgets = WidgetPainter{
        slider_canvas,
        slider_interaction,
        default_widget_palette(
            CanvasColor{40U, 180U, 220U, 255U}),
        1.0};
    const auto slider = slider_widgets.slider(
        WidgetId{3U},
        CanvasRect{20.0, 140.0, 200.0, 40.0},
        Clip,
        0.0,
        0.0,
        100.0,
        true);
    passed &= expect(
        slider && slider->changed &&
            slider->value == 75.0 &&
            std::move(slider_interaction).finish()->active ==
                WidgetId{3U} &&
            std::move(slider_canvas).finish(),
        "slider capture did not map the pointer to its value");

    auto color_canvas = CanvasFrameBuilder{Viewport};
    auto color_interaction = InteractionFrame{
        InteractionState{},
        PointerFrame{},
        true};
    auto color_widgets = WidgetPainter{
        color_canvas,
        color_interaction,
        default_widget_palette(
            CanvasColor{40U, 180U, 220U, 255U}),
        1.0};
    const auto color = color_widgets.color_control(
        std::array{
            WidgetId{10U},
            WidgetId{11U},
            WidgetId{12U},
        },
        CanvasRect{300.0, 20.0, 240.0, 132.0},
        Clip,
        CanvasColor{10U, 20U, 30U, 200U},
        true);
    passed &= expect(
        color &&
            color->value ==
                CanvasColor{10U, 20U, 30U, 200U} &&
            !color->changed &&
            std::move(color_interaction).finish() &&
            std::move(color_canvas).finish(),
        "stable color control changed channels or alpha");

    auto duplicate_canvas = CanvasFrameBuilder{Viewport};
    auto duplicate_interaction = InteractionFrame{
        InteractionState{},
        PointerFrame{},
        true};
    auto duplicate_widgets = WidgetPainter{
        duplicate_canvas,
        duplicate_interaction,
        default_widget_palette(
            CanvasColor{40U, 180U, 220U, 255U}),
        1.0};
    passed &= expect(
        duplicate_widgets.button(
            WidgetId{20U},
            Control,
            Clip,
            "First",
            true,
            false) &&
            !duplicate_widgets.button(
                WidgetId{20U},
                Control,
                Clip,
                "Second",
                true,
                false) &&
            std::move(duplicate_interaction).finish() ==
                std::unexpected(
                    InteractionError::DuplicateWidget),
        "widget painting hid a duplicate interaction ID");

    if (passed)
    {
        std::cout << "PASS ui_widgets\n";
    }
    return passed ? 0 : 1;
}
