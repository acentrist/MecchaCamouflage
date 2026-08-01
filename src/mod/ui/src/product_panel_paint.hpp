#pragma once

#include <meccha/product_ui/product_panel.hpp>

namespace meccha::product_ui::detail
{
[[nodiscard]] auto compose_paint_settings_section(
    ui::CanvasFrameBuilder& canvas,
    ui::WidgetPainter& widgets,
    const ui::PanelLayout& layout,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels,
    const ProductPanelInput& input,
    ProductPanelState& state)
    -> std::expected<void, ProductPanelError>;
} // namespace meccha::product_ui::detail
