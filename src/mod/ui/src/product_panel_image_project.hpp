#pragma once

#include <meccha/product_ui/product_panel.hpp>

namespace meccha::product_ui::detail
{
[[nodiscard]] auto image_project_toolbar_inset(
    double effective_scale) -> double;

[[nodiscard]] auto compose_image_project_toolbar(
    ui::WidgetPainter& widgets,
    ui::CanvasRect viewport,
    double content_origin_y,
    double effective_scale,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels,
    const ProductPanelInput& input,
    ProductPanelState& state,
    std::optional<application::ProductUiActionEnvelope>& action,
    std::optional<application::ProductUiEffectEnvelope>& effect)
    -> std::expected<void, ProductPanelError>;
} // namespace meccha::product_ui::detail
