#pragma once

#include <meccha/application/localization.hpp>
#include <meccha/application/product_ui_actions.hpp>
#include <meccha/application/product_ui_model.hpp>
#include <meccha/ui/image_editor.hpp>
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
#include <vector>

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
    std::string hotkey_capture_prompt{};
    std::string hotkey_duplicate_suffix{};
    std::string image_project_save{};
    std::string image_wrap{};
    std::string image_mirror{};
    std::string image_crop{};
    std::string image_remove{};
    std::string crop_zoom{};
    std::string crop_apply{};
    std::string crop_cancel{};
    std::string status_progress{};
    std::string status_elapsed{};
    std::string status_eta{};
    std::string status_queue{};
    std::string diagnostics_runtime{};
    std::string diagnostics_compatibility{};
    std::string diagnostics_command_queue{};
    std::string diagnostics_runtime_queue{};
    std::string diagnostics_empty{};
    std::string diagnostics_failure{};
    std::array<std::string, 4U> diagnostics_state_labels{};
    std::array<std::string, 3U> diagnostics_severity_labels{};
    std::array<std::string, 9U> hotkey_labels{};
    std::array<std::string, 16U> paint_setting_labels{};
    std::array<std::string, 3U> region_mode_labels{};
    std::array<std::string, 16U> image_setting_labels{};
    std::array<std::string, 3U> body_profile_labels{};
    std::array<std::string, 2U> placement_mode_labels{};
    std::array<std::string, 2U> alpha_mode_labels{};
    std::array<std::string, 2U> face_mode_labels{};
    std::array<std::string, 8U> esp_setting_labels{};
    std::array<std::string, 3U> esp_scope_labels{};

    auto operator==(const ProductPanelLabels&) const -> bool = default;
};

struct ImageEditorPanelState
{
    std::string project_id{};
    std::uint64_t project_revision{};
    ui::TextEditState project_name{};
    bool project_delete_armed{};
    ui::ImageEditorInteractionState interaction{};
    std::optional<ui::ImageLayerEdit> draft{};
    std::optional<ui::ImageCropSession> crop{};
    bool crop_dragging{};
    bool awaiting_revision{};

    auto operator==(const ImageEditorPanelState&) const -> bool = default;
};

struct HotkeyCaptureState
{
    std::optional<std::size_t> index{};
    std::optional<core::FunctionKey> rejected{};

    auto operator==(const HotkeyCaptureState&) const -> bool = default;
};

struct ProductPanelState
{
    application::ProductUiSection selected{
        application::ProductUiSection::Paint};
    ui::InteractionState interaction{};
    std::array<ui::ScrollState, application::ProductUiSections.size()>
        section_scroll{};
    HotkeyCaptureState hotkey_capture{};
    ImageEditorPanelState image_editor{};

    auto operator==(const ProductPanelState&) const -> bool = default;
};

struct ImageSourceFrameAsset
{
    std::string asset_id{};
    std::uint32_t width{};
    std::uint32_t height{};
    ui::CanvasTextureHandle texture{};

    auto operator==(const ImageSourceFrameAsset&) const -> bool = default;
};

struct ImageEditorFrameAssets
{
    std::string project_id{};
    std::uint64_t project_revision{};
    ui::CanvasTextureHandle atlas_texture{};
    std::optional<ui::ImageGuideOverlay> guide{};
    std::vector<ImageSourceFrameAsset> sources{};

    auto operator==(const ImageEditorFrameAssets&) const -> bool = default;
};

struct ProductPanelInput
{
    ui::CanvasViewport viewport{};
    ui::CanvasInsets safe_area{};
    ui::PointerFrame pointer{};
    ui::KeyboardNavigationFrame keyboard{};
    std::vector<ui::TextEditEvent> text_edit_events{};
    std::optional<ImageEditorFrameAssets> image_editor{};
    bool function_key_input_available{true};
    std::optional<core::FunctionKey> function_key_pressed{};

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
    InvalidImageAssets,
    InvalidInput,
};

using ProductPanelError = std::variant<
    ProductPanelValidationError,
    ui::PanelLayoutError,
    ui::CanvasError,
    ui::InteractionError,
    ui::ScrollError,
    ui::WidgetError,
    ui::ImageEditorInteractionError,
    ui::ImageCropError,
    ui::ImageEditorDrawError>;

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
