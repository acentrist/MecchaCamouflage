#include <meccha/product_ui/product_ui_pointer_capture.hpp>

#include <iostream>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto passed = true;
    auto capture = product_ui::ProductUiPointerCapture{};
    const auto first = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            640.0,
            360.0,
            1.5,
            0x1234U,
            true,
            false,
        });
    passed &= expect(
        first &&
            first->viewport ==
                ui::CanvasViewport{1920.0, 1080.0, 1.5} &&
            first->pointer.position ==
                ui::CanvasPoint{960.0, 540.0} &&
            !first->pointer.primary_pressed &&
            !first->pointer.primary_down &&
            !first->pointer.primary_released &&
            first->function_key_input_available &&
            first->owner_window == 0x1234U,
        "the first focused observation did not synchronize");

    auto initially_held_capture =
        product_ui::ProductUiPointerCapture{};
    const auto initially_held = initially_held_capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            640.0,
            360.0,
            1.5,
            0x1234U,
            true,
            true,
        });
    const auto initial_release = initially_held_capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            640.0,
            360.0,
            1.5,
            0x1234U,
            true,
            false,
        });
    const auto initial_press = initially_held_capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            640.0,
            360.0,
            1.5,
            0x1234U,
            true,
            true,
        });
    passed &= expect(
        initially_held &&
            !initially_held->pointer.primary_pressed &&
            !initially_held->pointer.primary_down &&
            !initially_held->pointer.primary_released &&
            initial_release &&
            !initial_release->pointer.primary_released &&
            initial_press &&
            initial_press->pointer.primary_pressed &&
            initial_press->pointer.primary_down,
        "an initially held button synthesized a partial drag");

    const auto pressed = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            320.0,
            180.0,
            1.5,
            0x1234U,
            true,
            true,
        });
    const auto held = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            400.0,
            200.0,
            1.5,
            0x1234U,
            true,
            true,
        });
    const auto released = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            400.0,
            200.0,
            1.5,
            0x1234U,
            true,
            false,
        });
    passed &= expect(
        pressed && pressed->pointer.primary_pressed &&
            pressed->pointer.primary_down &&
            !pressed->pointer.primary_released &&
            held && !held->pointer.primary_pressed &&
            held->pointer.primary_down &&
            released && released->pointer.primary_released &&
            !released->pointer.primary_down,
        "press, hold, and release edges were not exact");

    auto owner_capture = product_ui::ProductUiPointerCapture{};
    static_cast<void>(owner_capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            100.0,
            100.0,
            1.5,
            0x1234U,
            true,
            false,
        }));
    static_cast<void>(owner_capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            100.0,
            100.0,
            1.5,
            0x1234U,
            true,
            true,
        }));
    const auto owner_changed = owner_capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            100.0,
            100.0,
            1.5,
            0x5678U,
            true,
            true,
        });
    const auto owner_held = owner_capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            100.0,
            100.0,
            1.5,
            0x5678U,
            true,
            true,
        });
    passed &= expect(
        owner_changed &&
            owner_changed->pointer.primary_released &&
            !owner_changed->pointer.primary_down &&
            owner_held &&
            !owner_held->pointer.primary_pressed &&
            !owner_held->pointer.primary_down,
        "an owner replacement carried an active drag forward");

    static_cast<void>(capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            10.0,
            10.0,
            1.5,
            0x1234U,
            true,
            true,
        }));
    const auto focus_lost = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            10.0,
            10.0,
            1.5,
            0x1234U,
            false,
            true,
        });
    const auto returned_held = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            10.0,
            10.0,
            1.5,
            0x1234U,
            true,
            true,
        });
    const auto returned_released = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            10.0,
            10.0,
            1.5,
            0x1234U,
            true,
            false,
        });
    const auto pressed_again = capture.update(
        product_ui::ProductUiPointerObservation{
            1920.0,
            1080.0,
            1280.0,
            720.0,
            10.0,
            10.0,
            1.5,
            0x1234U,
            true,
            true,
        });
    passed &= expect(
        focus_lost &&
            focus_lost->pointer.position ==
                ui::CanvasPoint{-1.0, -1.0} &&
            focus_lost->pointer.primary_released &&
            !focus_lost->function_key_input_available &&
            returned_held &&
            !returned_held->pointer.primary_pressed &&
            !returned_held->pointer.primary_down &&
            returned_released &&
            !returned_released->pointer.primary_released &&
            pressed_again &&
            pressed_again->pointer.primary_pressed,
        "focus loss did not release and suppress a held return");

    const auto before_invalid = capture.snapshot();
    const auto invalid = capture.update(
        product_ui::ProductUiPointerObservation{
            0.0,
            1080.0,
            1280.0,
            720.0,
            10.0,
            10.0,
            1.5,
            0x1234U,
            true,
            false,
        });
    passed &= expect(
        !invalid &&
            invalid.error() ==
                product_ui::ProductUiPointerCaptureError::
                    InvalidViewport &&
            capture.snapshot() == before_invalid,
        "an invalid observation partially mutated capture state");

    return passed ? 0 : 1;
}
