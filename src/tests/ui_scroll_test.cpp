#include <meccha/ui/scroll.hpp>

#include <iostream>
#include <limits>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_scroll: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    const auto scrolled = update_scroll_container(
        ScrollState{},
        ScrollContainerInput{
            CanvasRect{20.0, 20.0, 200.0, 160.0},
            CanvasRect{0.0, 0.0, 300.0, 200.0},
            600.0,
            40.0,
            PointerFrame{
                CanvasPoint{100.0, 100.0},
                false,
                false,
                false,
                -2.0,
            },
        });
    passed &= expect(
        scrolled &&
            scrolled->state.offset_y == 80.0 &&
            scrolled->maximum_offset == 440.0 &&
            scrolled->content_origin_y == -60.0 &&
            scrolled->visible_clip ==
                CanvasRect{20.0, 20.0, 200.0, 160.0} &&
            scrolled->changed,
        "wheel input inside the clipped viewport did not scroll");

    const auto outside = update_scroll_container(
        ScrollState{80.0},
        ScrollContainerInput{
            CanvasRect{20.0, 20.0, 200.0, 160.0},
            CanvasRect{0.0, 0.0, 300.0, 200.0},
            600.0,
            40.0,
            PointerFrame{
                CanvasPoint{250.0, 100.0},
                false,
                false,
                false,
                -5.0,
            },
        });
    passed &= expect(
        outside && outside->state.offset_y == 80.0 &&
            !outside->changed,
        "wheel input outside the viewport changed scroll");

    const auto shrunk = update_scroll_container(
        ScrollState{300.0},
        ScrollContainerInput{
            CanvasRect{20.0, 20.0, 200.0, 160.0},
            CanvasRect{0.0, 0.0, 300.0, 200.0},
            200.0,
            40.0,
            PointerFrame{},
        });
    passed &= expect(
        shrunk &&
            shrunk->state.offset_y == 40.0 &&
            shrunk->maximum_offset == 40.0 &&
            shrunk->changed,
        "content shrink did not clamp retained scroll");

    const auto clipped_out = update_scroll_container(
        ScrollState{20.0},
        ScrollContainerInput{
            CanvasRect{400.0, 400.0, 100.0, 100.0},
            CanvasRect{0.0, 0.0, 300.0, 200.0},
            300.0,
            40.0,
            PointerFrame{
                CanvasPoint{450.0, 450.0},
                false,
                false,
                false,
                -1.0,
            },
        });
    passed &= expect(
        clipped_out && !clipped_out->visible_clip &&
            clipped_out->state.offset_y == 20.0 &&
            !clipped_out->changed,
        "a fully clipped container consumed wheel input");

    passed &= expect(
        update_scroll_container(
            ScrollState{-1.0},
            ScrollContainerInput{
                CanvasRect{0.0, 0.0, 100.0, 100.0},
                CanvasRect{0.0, 0.0, 100.0, 100.0},
                200.0,
                20.0,
                PointerFrame{},
            }) ==
            std::unexpected(ScrollError::InvalidState),
        "negative retained offset was accepted");
    passed &= expect(
        update_scroll_container(
            ScrollState{},
            ScrollContainerInput{
                CanvasRect{0.0, 0.0, 100.0, 100.0},
                CanvasRect{0.0, 0.0, 100.0, 100.0},
                std::numeric_limits<double>::infinity(),
                20.0,
                PointerFrame{},
            }) ==
            std::unexpected(ScrollError::InvalidContent),
        "non-finite content height was accepted");

    if (passed)
    {
        std::cout << "PASS ui_scroll\n";
    }
    return passed ? 0 : 1;
}
