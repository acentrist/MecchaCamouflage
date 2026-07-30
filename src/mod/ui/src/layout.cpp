#include <meccha/ui/layout.hpp>

#include <algorithm>
#include <cmath>

namespace meccha::ui
{
namespace
{
inline constexpr double MinimumSafeWidth = 160.0;
inline constexpr double MinimumSafeHeight = 100.0;
inline constexpr double LogicalFitWidth = 320.0;
inline constexpr double LogicalFitHeight = 200.0;

auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto valid_viewport(const CanvasViewport& viewport) -> bool
{
    return finite(viewport.width) && finite(viewport.height) &&
           finite(viewport.dpi_scale) &&
           viewport.width >= 320.0 &&
           viewport.width <= 32'768.0 &&
           viewport.height >= 200.0 &&
           viewport.height <= 32'768.0 &&
           viewport.dpi_scale >= 0.5 &&
           viewport.dpi_scale <= 4.0;
}

auto valid_insets(const CanvasInsets& insets) -> bool
{
    return finite(insets.left) && finite(insets.top) &&
           finite(insets.right) && finite(insets.bottom) &&
           insets.left >= 0.0 && insets.top >= 0.0 &&
           insets.right >= 0.0 && insets.bottom >= 0.0;
}
} // namespace

auto build_panel_layout(PanelLayoutInput input)
    -> std::expected<PanelLayout, PanelLayoutError>
{
    if (!valid_viewport(input.viewport))
    {
        return std::unexpected(PanelLayoutError::InvalidViewport);
    }
    if (!finite(input.user_scale) ||
        input.user_scale < MinimumUiScale ||
        input.user_scale > MaximumUiScale)
    {
        return std::unexpected(PanelLayoutError::InvalidUiScale);
    }
    if (!valid_insets(input.safe_area) ||
        input.safe_area.left + input.safe_area.right >=
            input.viewport.width ||
        input.safe_area.top + input.safe_area.bottom >=
            input.viewport.height)
    {
        return std::unexpected(PanelLayoutError::InvalidSafeArea);
    }

    const auto safe = CanvasRect{
        input.safe_area.left,
        input.safe_area.top,
        input.viewport.width -
            input.safe_area.left - input.safe_area.right,
        input.viewport.height -
            input.safe_area.top - input.safe_area.bottom,
    };
    if (safe.width < MinimumSafeWidth ||
        safe.height < MinimumSafeHeight)
    {
        return std::unexpected(PanelLayoutError::InsufficientSafeArea);
    }

    const auto requested_scale =
        input.user_scale * input.viewport.dpi_scale;
    const auto fit_scale = std::min(
        safe.width / LogicalFitWidth,
        safe.height / LogicalFitHeight);
    const auto scale = std::min(requested_scale, fit_scale);
    if (!finite(scale) || scale <= 0.0)
    {
        return std::unexpected(PanelLayoutError::InsufficientSafeArea);
    }

    const auto margin = std::min(
        16.0 * scale,
        0.04 * std::min(safe.width, safe.height));
    const auto usable = CanvasRect{
        safe.x + margin,
        safe.y + margin,
        safe.width - 2.0 * margin,
        safe.height - 2.0 * margin,
    };
    if (usable.width <= 0.0 || usable.height <= 0.0)
    {
        return std::unexpected(PanelLayoutError::InsufficientSafeArea);
    }

    const auto minimum_panel_width =
        std::min(360.0 * scale, usable.width);
    const auto maximum_panel_width =
        std::min(720.0 * scale, usable.width);
    const auto panel_width = std::clamp(
        usable.width * 0.58,
        minimum_panel_width,
        maximum_panel_width);
    const auto panel_height =
        std::min(720.0 * scale, usable.height);
    const auto panel = CanvasRect{
        usable.x,
        usable.y + (usable.height - panel_height) * 0.5,
        panel_width,
        panel_height,
    };

    const auto tab_height = 44.0 * scale;
    const auto status_height = 48.0 * scale;
    const auto padding = 12.0 * scale;
    const auto content_height =
        panel.height - tab_height - status_height - 2.0 * padding;
    const auto content_width = panel.width - 2.0 * padding;
    if (content_width <= 0.0 || content_height <= 0.0)
    {
        return std::unexpected(PanelLayoutError::InsufficientSafeArea);
    }

    auto result = PanelLayout{};
    result.effective_scale = scale;
    result.compact_tabs = panel.width < 520.0 * scale;
    result.safe_bounds = safe;
    result.panel = panel;
    result.tab_strip = CanvasRect{
        panel.x,
        panel.y,
        panel.width,
        tab_height,
    };
    result.status_strip = CanvasRect{
        panel.x,
        panel.y + panel.height - status_height,
        panel.width,
        status_height,
    };
    result.content = CanvasRect{
        panel.x + padding,
        panel.y + tab_height + padding,
        content_width,
        content_height,
    };

    const auto tab_width =
        result.tab_strip.width /
        static_cast<double>(result.section_tabs.size());
    for (auto index = std::size_t{};
         index < result.section_tabs.size();
         ++index)
    {
        const auto x = result.tab_strip.x +
                       tab_width * static_cast<double>(index);
        const auto next_x =
            index + 1U == result.section_tabs.size()
                ? result.tab_strip.x + result.tab_strip.width
                : result.tab_strip.x +
                      tab_width * static_cast<double>(index + 1U);
        result.section_tabs[index] = CanvasRect{
            x,
            result.tab_strip.y,
            next_x - x,
            result.tab_strip.height,
        };
    }
    return result;
}
} // namespace meccha::ui
