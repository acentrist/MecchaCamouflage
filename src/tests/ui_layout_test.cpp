#include <meccha/ui/layout.hpp>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_layout: " << message << '\n';
    }
    return condition;
}

auto inside(
    const meccha::ui::CanvasRect& outer,
    const meccha::ui::CanvasRect& inner) -> bool
{
    return inner.x >= outer.x &&
           inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    const auto desktop = build_panel_layout(PanelLayoutInput{
        CanvasViewport{1920.0, 1080.0, 1.0},
        CanvasInsets{},
        1.0,
    });
    passed &= expect(
        desktop &&
            desktop->effective_scale == 1.0 &&
            desktop->section_tabs.size() == 5U &&
            inside(desktop->safe_bounds, desktop->panel) &&
            inside(desktop->panel, desktop->tab_strip) &&
            inside(desktop->panel, desktop->content) &&
            inside(desktop->panel, desktop->status_strip),
        "desktop layout did not preserve the bounded panel regions");
    if (!desktop)
    {
        return 1;
    }

    auto tab_width = 0.0;
    for (const auto& tab : desktop->section_tabs)
    {
        passed &= expect(
            inside(desktop->tab_strip, tab),
            "a section tab escaped the tab strip");
        tab_width += tab.width;
    }
    passed &= expect(
        std::abs(tab_width - desktop->tab_strip.width) < 0.000001,
        "section tabs did not exactly partition the tab strip");

    const auto constrained = build_panel_layout(PanelLayoutInput{
        CanvasViewport{640.0, 360.0, 2.0},
        CanvasInsets{12.0, 8.0, 20.0, 16.0},
        2.0,
    });
    passed &= expect(
        constrained &&
            constrained->effective_scale < 4.0 &&
            constrained->effective_scale > 0.0 &&
            constrained->compact_tabs &&
            constrained->safe_bounds ==
                CanvasRect{12.0, 8.0, 608.0, 336.0} &&
            inside(constrained->safe_bounds, constrained->panel) &&
            constrained->content.width > 0.0 &&
            constrained->content.height > 0.0,
        "small high-DPI layout did not fit into the safe area");

    const auto ultrawide = build_panel_layout(PanelLayoutInput{
        CanvasViewport{3440.0, 1440.0, 1.25},
        CanvasInsets{40.0, 20.0, 80.0, 20.0},
        1.5,
    });
    passed &= expect(
        ultrawide &&
            ultrawide->effective_scale == 1.875 &&
            ultrawide->panel.width <=
                720.0 * ultrawide->effective_scale &&
            inside(ultrawide->safe_bounds, ultrawide->panel),
        "ultrawide layout ignored DPI, user scale, or maximum width");

    passed &= expect(
        build_panel_layout(PanelLayoutInput{
            CanvasViewport{1920.0, 1080.0, 1.0},
            CanvasInsets{},
            0.5,
        }) ==
            std::unexpected(PanelLayoutError::InvalidUiScale),
        "out-of-range user scale was accepted");

    passed &= expect(
        build_panel_layout(PanelLayoutInput{
            CanvasViewport{640.0, 360.0, 1.0},
            CanvasInsets{400.0, 0.0, 300.0, 0.0},
            1.0,
        }) ==
            std::unexpected(PanelLayoutError::InvalidSafeArea),
        "an inverted safe area was accepted");

    if (passed)
    {
        std::cout << "PASS ui_layout\n";
    }
    return passed ? 0 : 1;
}
