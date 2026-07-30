#include <meccha/ui/interaction.hpp>

#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_interaction: " << message << '\n';
    }
    return condition;
}

constexpr auto Clip =
    meccha::ui::CanvasRect{0.0, 0.0, 300.0, 200.0};
constexpr auto Button =
    meccha::ui::CanvasRect{20.0, 20.0, 100.0, 40.0};
constexpr auto Primary = meccha::ui::WidgetId{1U};
constexpr auto Secondary = meccha::ui::WidgetId{2U};
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    auto state = InteractionState{};

    auto pressed_frame = InteractionFrame{
        state,
        PointerFrame{
            CanvasPoint{40.0, 40.0},
            true,
            true,
            false,
            0.0,
        },
        true};
    const auto pressed =
        pressed_frame.control(Primary, Button, Clip, true, true);
    const auto overlapped = pressed_frame.control(
        Secondary,
        Button,
        Clip,
        true,
        true);
    const auto pressed_result = std::move(pressed_frame).finish();
    passed &= expect(
        pressed && pressed->hovered && pressed->pressed &&
            pressed->held && !pressed->activated &&
            overlapped && overlapped->hovered &&
            !overlapped->pressed &&
            pressed_result &&
            pressed_result->active == Primary,
        "press capture was not exclusive or persistent");
    if (!pressed_result)
    {
        return 1;
    }
    state = *pressed_result;

    auto released_frame = InteractionFrame{
        state,
        PointerFrame{
            CanvasPoint{40.0, 40.0},
            false,
            false,
            true,
            0.0,
        },
        true};
    const auto released =
        released_frame.control(Primary, Button, Clip, true, true);
    const auto released_result =
        std::move(released_frame).finish();
    passed &= expect(
        released && released->released &&
            released->activated &&
            released_result &&
            !released_result->active &&
            released_result->focused == Primary,
        "release inside did not activate and focus the captured control");
    if (!released_result)
    {
        return 1;
    }
    state = *released_result;

    auto clipped_frame = InteractionFrame{
        state,
        PointerFrame{
            CanvasPoint{110.0, 40.0},
            true,
            true,
            false,
            0.0,
        },
        true};
    const auto clipped = clipped_frame.control(
        Primary,
        Button,
        CanvasRect{20.0, 20.0, 50.0, 40.0},
        true,
        true);
    passed &= expect(
        clipped && !clipped->hovered && !clipped->pressed &&
            std::move(clipped_frame).finish(),
        "hit testing ignored the active clip");

    auto outside_release = InteractionFrame{
        InteractionState{Primary, Primary},
        PointerFrame{
            CanvasPoint{250.0, 150.0},
            false,
            false,
            true,
            0.0,
        },
        true};
    const auto outside = outside_release.control(
        Primary,
        Button,
        Clip,
        true,
        true);
    const auto outside_result =
        std::move(outside_release).finish();
    passed &= expect(
        outside && outside->released &&
            !outside->activated &&
            outside_result &&
            !outside_result->active &&
            outside_result->focused == Primary,
        "release outside activated or retained pointer capture");

    auto disabled_frame = InteractionFrame{
        InteractionState{},
        PointerFrame{
            CanvasPoint{40.0, 40.0},
            true,
            true,
            false,
            0.0,
        },
        true};
    const auto disabled = disabled_frame.control(
        Primary,
        Button,
        Clip,
        false,
        true);
    passed &= expect(
        disabled && !disabled->hovered &&
            !disabled->pressed &&
            std::move(disabled_frame).finish()->active ==
                std::nullopt,
        "disabled control accepted pointer input");

    auto duplicate_frame = InteractionFrame{
        InteractionState{},
        PointerFrame{},
        true};
    passed &= expect(
        duplicate_frame.control(
            Primary,
            Button,
            Clip,
            true,
            true) &&
            duplicate_frame.control(
                Primary,
                Button,
                Clip,
                true,
                true) ==
                std::unexpected(
                    InteractionError::DuplicateWidget) &&
            std::move(duplicate_frame).finish() ==
                std::unexpected(
                    InteractionError::DuplicateWidget),
        "duplicate widget IDs did not fail the complete frame");

    auto limited_frame = InteractionFrame{
        InteractionState{},
        PointerFrame{},
        true,
        1U};
    passed &= expect(
        limited_frame.control(
            Primary,
            Button,
            Clip,
            true,
            true) &&
            limited_frame.control(
                Secondary,
                Button,
                Clip,
                true,
                true) ==
                std::unexpected(InteractionError::WidgetLimit) &&
            std::move(limited_frame).finish() ==
                std::unexpected(InteractionError::WidgetLimit),
        "the per-frame widget bound was not enforced");

    auto closed_frame = InteractionFrame{
        InteractionState{Primary, Secondary},
        PointerFrame{},
        false};
    const auto closed =
        std::move(closed_frame).finish();
    passed &= expect(
        closed && !closed->active && !closed->focused,
        "closed panel retained capture or focus");

    auto click_frame = InteractionFrame{
        InteractionState{},
        PointerFrame{
            CanvasPoint{40.0, 40.0},
            true,
            false,
            true,
            0.0,
        },
        true};
    const auto click =
        click_frame.control(Primary, Button, Clip, true, true);
    passed &= expect(
        click && click->pressed && click->released &&
            click->activated &&
            std::move(click_frame).finish()->focused == Primary,
        "same-frame click was not handled deterministically");

    auto background_frame = InteractionFrame{
        InteractionState{std::nullopt, Primary},
        PointerFrame{
            CanvasPoint{250.0, 150.0},
            true,
            true,
            false,
            0.0,
        },
        true};
    passed &= expect(
        std::move(background_frame).finish()->focused ==
            std::nullopt,
        "an unclaimed background press retained keyboard focus");

    auto invalid_frame = InteractionFrame{
        InteractionState{},
        PointerFrame{
            CanvasPoint{
                std::numeric_limits<double>::infinity(),
                0.0,
            },
            false,
            false,
            false,
            0.0,
        },
        true};
    passed &= expect(
        std::move(invalid_frame).finish() ==
            std::unexpected(InteractionError::InvalidPointer),
        "non-finite pointer input was accepted");

    if (passed)
    {
        std::cout << "PASS ui_interaction\n";
    }
    return passed ? 0 : 1;
}
