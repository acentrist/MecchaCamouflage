#pragma once

#include <meccha/application/localization.hpp>
#include <meccha/application/product_ui_actions.hpp>
#include <meccha/application/product_ui_model.hpp>
#include <meccha/ui/interaction.hpp>
#include <meccha/ui/layout.hpp>
#include <meccha/ui/scroll.hpp>
#include <meccha/ui/widgets.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace meccha::product_ui
{
inline constexpr std::size_t MaximumProductPanelLabelBytes = 256U;

struct ProductPanelLabels
{
    std::array<std::string, application::ProductUiSections.size()>
        section_labels{};
    std::string start{};
    std::string preview{};
    std::string restore{};
    std::string cancel{};
    std::string language{};
    std::string theme_color{};
    std::array<std::string, 9U> hotkey_labels{};
    std::array<std::string, 16U> paint_setting_labels{};
    std::array<std::string, 3U> region_mode_labels{};

    auto operator==(const ProductPanelLabels&) const -> bool = default;
};

struct ProductPanelState
{
    application::ProductUiSection selected{
        application::ProductUiSection::Paint};
    ui::InteractionState interaction{};
    std::array<ui::ScrollState, application::ProductUiSections.size()>
        section_scroll{};

    auto operator==(const ProductPanelState&) const -> bool = default;
};

struct ProductPanelInput
{
    ui::CanvasViewport viewport{};
    ui::CanvasInsets safe_area{};
    ui::PointerFrame pointer{};
    ui::KeyboardNavigationFrame keyboard{};

    auto operator==(const ProductPanelInput&) const -> bool = default;
};

struct ProductPanelOutput
{
    ui::CanvasFrame frame{};
    std::optional<ui::PanelLayout> layout{};
    ProductPanelState state{};
    std::optional<application::ProductUiActionEnvelope> action{};
};

enum class ProductPanelValidationError : std::uint8_t
{
    InvalidModel,
    InvalidState,
    InvalidLabels,
};

using ProductPanelError = std::variant<
    ProductPanelValidationError,
    ui::PanelLayoutError,
    ui::CanvasError,
    ui::InteractionError,
    ui::ScrollError,
    ui::WidgetError>;

[[nodiscard]] auto build_product_panel_labels(
    const application::LocalizationCatalog& catalog,
    std::string_view locale) -> ProductPanelLabels;

[[nodiscard]] auto compose_product_panel(
    const application::ProductUiModel& model,
    ProductPanelState previous,
    ProductPanelInput input,
    const ProductPanelLabels& labels)
    -> std::expected<ProductPanelOutput, ProductPanelError>;
} // namespace meccha::product_ui
