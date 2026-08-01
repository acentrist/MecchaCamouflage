#include <meccha/product_ui/product_panel.hpp>

#include "product_panel_diagnostics.hpp"
#include "product_panel_esp.hpp"
#include "product_panel_image.hpp"
#include "product_panel_paint.hpp"
#include "product_panel_settings.hpp"
#include "product_panel_status.hpp"

#include <meccha/core/image_compositor.hpp>
#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace meccha::product_ui
{
namespace
{
constexpr auto PanelBackground =
    ui::CanvasColor{18U, 20U, 26U, 238U};

constexpr auto TabIds = std::array{
    ui::WidgetId{1U},
    ui::WidgetId{2U},
    ui::WidgetId{3U},
    ui::WidgetId{4U},
    ui::WidgetId{5U},
};
constexpr auto PaintActionIds = std::array{
    ui::WidgetId{101U},
    ui::WidgetId{102U},
    ui::WidgetId{103U},
    ui::WidgetId{104U},
};
constexpr auto ImageActionIds = std::array{
    ui::WidgetId{201U},
    ui::WidgetId{202U},
    ui::WidgetId{203U},
    ui::WidgetId{204U},
};
constexpr auto EspToggleId = ui::WidgetId{301U};
constexpr auto EditActionIds = std::array{
    ui::WidgetId{701U},
    ui::WidgetId{702U},
    ui::WidgetId{703U},
};
constexpr auto FeatureActions = std::array{
    application::FeatureUiAction::Start,
    application::FeatureUiAction::Preview,
    application::FeatureUiAction::Restore,
    application::FeatureUiAction::Cancel,
};

auto valid_label(std::string_view label) -> bool
{
    return !label.empty() &&
           label.size() <= MaximumProductPanelLabelBytes &&
           core::valid_utf8(label);
}

auto valid_project_name(
    std::string_view name,
    bool allow_empty = false) -> bool
{
    return (allow_empty || !name.empty()) &&
           name.size() <=
               application::MaximumProductUiProjectNameBytes &&
           core::valid_utf8(name) &&
           std::ranges::none_of(
               name,
               [](unsigned char character)
               {
                   return character < 0x20U ||
                          character == 0x7FU;
               });
}

auto valid_project_name_state(
    const ui::TextEditState& state,
    bool allow_idle_empty) -> bool
{
    const auto cursor_boundary =
        state.cursor_byte <= state.value.size() &&
        (state.cursor_byte == state.value.size() ||
         (static_cast<unsigned char>(
              state.value[state.cursor_byte]) &
          0xC0U) != 0x80U);
    return valid_project_name(
               state.value,
               state.editing || allow_idle_empty) &&
           cursor_boundary &&
           (state.editing
                ? valid_project_name(state.original)
                : state.original.empty());
}

auto labels_valid(const ProductPanelLabels& labels) -> bool
{
    return std::ranges::all_of(
               labels.section_labels,
               valid_label) &&
           valid_label(labels.start) &&
           valid_label(labels.preview) &&
           valid_label(labels.restore) &&
           valid_label(labels.cancel) &&
           valid_label(labels.edit) &&
           valid_label(labels.save) &&
           valid_label(labels.language) &&
           valid_label(labels.theme_color) &&
           valid_label(labels.hotkey_capture_prompt) &&
           valid_label(labels.hotkey_duplicate_suffix) &&
           valid_label(labels.image_project_load) &&
           valid_label(labels.image_import) &&
           valid_label(labels.image_project_save) &&
           valid_label(labels.image_wrap) &&
           valid_label(labels.image_mirror) &&
           valid_label(labels.image_crop) &&
           valid_label(labels.image_remove) &&
           valid_label(labels.crop_zoom) &&
           valid_label(labels.crop_apply) &&
           valid_label(labels.crop_cancel) &&
           valid_label(labels.status_progress) &&
           valid_label(labels.status_elapsed) &&
           valid_label(labels.status_eta) &&
           valid_label(labels.status_queue) &&
           valid_label(labels.diagnostics_runtime) &&
           valid_label(labels.diagnostics_compatibility) &&
           valid_label(labels.diagnostics_command_queue) &&
           valid_label(labels.diagnostics_runtime_queue) &&
           valid_label(labels.diagnostics_empty) &&
           valid_label(labels.diagnostics_failure) &&
           std::ranges::all_of(
               labels.diagnostics_state_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.diagnostics_severity_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.hotkey_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.paint_setting_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.region_mode_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.image_setting_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.body_profile_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.placement_mode_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.alpha_mode_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.face_mode_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.esp_setting_labels,
               valid_label) &&
           std::ranges::all_of(
               labels.esp_scope_labels,
               valid_label);
}

auto runtime_phase_valid(
    application::ApplicationRuntimePhase phase) -> bool
{
    switch (phase)
    {
    case application::ApplicationRuntimePhase::Cold:
    case application::ApplicationRuntimePhase::Initializing:
    case application::ApplicationRuntimePhase::Compatible:
    case application::ApplicationRuntimePhase::Incompatible:
    case application::ApplicationRuntimePhase::ShuttingDown:
    case application::ApplicationRuntimePhase::Stopped:
        return true;
    }
    return false;
}

auto compatibility_status_valid(
    application::CompatibilityStatus status) -> bool
{
    switch (status)
    {
    case application::CompatibilityStatus::Unknown:
    case application::CompatibilityStatus::Compatible:
    case application::CompatibilityStatus::UnsupportedGame:
    case application::CompatibilityStatus::RuntimeError:
        return true;
    }
    return false;
}

auto compatibility_failure_valid(
    const application::CompatibilityFailure& failure) -> bool
{
    const auto contract_valid = [&failure]
    {
        switch (failure.contract)
        {
        case application::RuntimeContractId::RuntimeInitialization:
        case application::RuntimeContractId::HudCallback:
        case application::RuntimeContractId::World:
        case application::RuntimeContractId::PlayerController:
        case application::RuntimeContractId::Hud:
        case application::RuntimeContractId::Canvas:
        case application::RuntimeContractId::EspFrame:
        case application::RuntimeContractId::PaintAtUvWithBrush:
        case application::RuntimeContractId::PaintCapture:
        case application::RuntimeContractId::PaintQueueObservation:
        case application::RuntimeContractId::ImagePaintTexture:
        case application::RuntimeContractId::TextureMutation:
        case application::RuntimeContractId::InputControl:
        case application::RuntimeContractId::ImagePaintMeshProfile:
            return true;
        }
        return false;
    }();
    const auto kind_valid = [&failure]
    {
        switch (failure.kind)
        {
        case application::ContractFailureKind::MissingObject:
        case application::ContractFailureKind::WrongClass:
        case application::ContractFailureKind::MissingProperty:
        case application::ContractFailureKind::WrongPropertyKind:
        case application::ContractFailureKind::MissingFunction:
        case application::ContractFailureKind::ParameterSizeMismatch:
        case application::ContractFailureKind::StaleObject:
        case application::ContractFailureKind::InvalidValue:
        case application::ContractFailureKind::CallbackFailure:
        case application::ContractFailureKind::ExecutionFailure:
        case application::ContractFailureKind::UnsupportedGameBuild:
            return true;
        }
        return false;
    }();
    return contract_valid && kind_valid &&
           !failure.message_key.empty() &&
           failure.message_key.size() <=
               application::MaximumProductUiDiagnosticKeyBytes &&
           core::valid_utf8(failure.message_key);
}

auto queue_presentation_valid(
    const application::QueuePresentation& queue) -> bool
{
    if (queue.queued > queue.capacity ||
        !std::isfinite(queue.utilization) ||
        queue.utilization < 0.0 ||
        queue.utilization > 1.0)
    {
        return false;
    }
    const auto expected =
        queue.capacity == 0U
            ? 0.0
            : static_cast<double>(queue.queued) /
                  static_cast<double>(queue.capacity);
    return std::abs(queue.utilization - expected) <= 1.0e-12;
}

auto diagnostics_model_valid(
    const application::DiagnosticsPanelModel& diagnostics) -> bool
{
    if (!runtime_phase_valid(diagnostics.runtime_phase) ||
        !compatibility_status_valid(
            diagnostics.compatibility.status) ||
        !queue_presentation_valid(
            diagnostics.command_queue) ||
        !queue_presentation_valid(
            diagnostics.runtime_queue) ||
        diagnostics.entries.size() >
            application::MaximumProductUiDiagnostics)
    {
        return false;
    }
    const auto failure_expected =
        diagnostics.compatibility.status ==
            application::CompatibilityStatus::UnsupportedGame ||
        diagnostics.compatibility.status ==
            application::CompatibilityStatus::RuntimeError;
    if (diagnostics.compatibility.failure.has_value() !=
        failure_expected)
    {
        return false;
    }
    if (diagnostics.compatibility.failure &&
        !compatibility_failure_valid(
            *diagnostics.compatibility.failure))
    {
        return false;
    }

    auto previous_sequence = std::uint64_t{};
    for (const auto& entry : diagnostics.entries)
    {
        const auto severity_valid = [&entry]
        {
            switch (entry.severity)
            {
            case application::DiagnosticSeverity::Information:
            case application::DiagnosticSeverity::Warning:
            case application::DiagnosticSeverity::Error:
                return true;
            }
            return false;
        }();
        if (!severity_valid ||
            entry.sequence <= previous_sequence ||
            entry.message_key.empty() ||
            entry.message_key.size() >
                application::MaximumProductUiDiagnosticKeyBytes ||
            !core::valid_utf8(entry.message_key) ||
            (entry.compatibility_failure &&
             !compatibility_failure_valid(
                 *entry.compatibility_failure)))
        {
            return false;
        }
        previous_sequence = entry.sequence;
    }
    return true;
}

auto progress_model_valid(
    const application::ProgressPresentation& progress) -> bool
{
    if (progress.completed > progress.total ||
        !std::isfinite(progress.fraction) ||
        progress.fraction < 0.0 ||
        progress.fraction > 1.0 ||
        !std::isfinite(progress.queue_pressure) ||
        progress.queue_pressure < 0.0 ||
        progress.queue_pressure > 1.0)
    {
        return false;
    }
    const auto expected =
        progress.total == 0U
            ? 0.0
            : static_cast<double>(progress.completed) /
                  static_cast<double>(progress.total);
    return std::abs(progress.fraction - expected) <= 1.0e-12;
}

auto model_valid(const application::ProductUiModel& model) -> bool
{
    const auto image_settings_valid =
        core::validate(model.image_paint.settings).empty();
    const auto image_document_valid =
        model.image_paint.document
            ? core::valid_image_project_id(
                  model.image_paint.document->project_id) &&
                  valid_project_name(
                      model.image_paint.document
                          ->display_name) &&
                  model.image_paint.document->revision != 0U &&
                  model.image_paint.document->settings ==
                      model.image_paint.settings &&
                  std::ranges::all_of(
                      model.image_paint.document->layers,
                      [](const core::ImageLayer& layer)
                      {
                          return core::validate(layer).empty();
                      })
            : model.image_paint.settings ==
                  model.settings.config.image_paint;
    return model.sections == application::ProductUiSections &&
           core::validate(model.settings.config).empty() &&
           image_settings_valid &&
           image_document_valid &&
           model.paint.settings ==
               model.settings.config.paint &&
           model.esp.settings ==
               model.settings.config.esp &&
           model.esp.enabled ==
               model.settings.config.esp.enabled &&
           diagnostics_model_valid(model.diagnostics) &&
           progress_model_valid(model.progress);
}

auto state_valid(const ProductPanelState& state) -> bool
{
    const auto section_valid =
        std::ranges::find(
            application::ProductUiSections,
            state.selected) !=
        application::ProductUiSections.end();
    const auto editor_identity_valid =
        (state.image_editor.project_id.empty() &&
         state.image_editor.project_revision == 0U) ||
        (core::valid_image_project_id(
             state.image_editor.project_id) &&
         state.image_editor.project_revision != 0U);
    const auto capture_index_valid =
        !state.hotkey_capture.index ||
        *state.hotkey_capture.index < 9U;
    const auto rejected_key_valid =
        !state.hotkey_capture.rejected ||
        (state.hotkey_capture.index &&
         static_cast<unsigned>(
             *state.hotkey_capture.rejected) >= 1U &&
         static_cast<unsigned>(
             *state.hotkey_capture.rejected) <= 24U);
    return section_valid && editor_identity_valid &&
           capture_index_valid && rejected_key_valid &&
           (!state.edit_session ||
            (core::validate(state.edit_session->base).empty() &&
             core::validate(state.edit_session->draft).empty())) &&
           valid_project_name_state(
               state.image_editor.project_name,
               state.image_editor.project_id.empty()) &&
           (!state.image_editor.draft ||
            core::validate(
                state.image_editor.draft->layer)
                .empty());
}

auto image_assets_valid(
    const application::ProductUiModel& model,
    const ProductPanelInput& input) -> bool
{
    if (!input.image_editor)
    {
        return true;
    }
    if (!model.image_paint.document)
    {
        return false;
    }
    const auto& assets = *input.image_editor;
    const auto& document = *model.image_paint.document;
    const auto& pipeline = model.image_paint.pipeline;
    if (assets.atlas_texture.identity == 0U ||
        assets.project_id != document.project_id ||
        assets.project_revision != document.revision ||
        pipeline.phase !=
            application::ImageEditorPipelinePhase::Ready ||
        pipeline.project_id != document.project_id ||
        pipeline.project_revision != document.revision ||
        pipeline.pending || pipeline.failure ||
        assets.sources.size() > core::MaximumImageSources)
    {
        return false;
    }
    auto source_ids = std::vector<std::string_view>{};
    source_ids.reserve(assets.sources.size());
    for (const auto& source : assets.sources)
    {
        if (source.asset_id.empty() ||
            source.asset_id.size() >
                application::MaximumProductUiAssetIdBytes ||
            source.width == 0U || source.height == 0U ||
            source.width > core::MaximumDecodedImageDimension ||
            source.height > core::MaximumDecodedImageDimension ||
            source.texture.identity == 0U ||
            std::ranges::find(source_ids, source.asset_id) !=
                source_ids.end() ||
            std::ranges::none_of(
                document.layers,
                [&source](const core::ImageLayer& layer)
                {
                    return layer.asset_id == source.asset_id;
                }))
        {
            return false;
        }
        source_ids.push_back(source.asset_id);
    }
    return true;
}

auto accent(const core::Rgb8& color) -> ui::CanvasColor
{
    return {
        color.red,
        color.green,
        color.blue,
        255U,
    };
}

auto add_box(
    ui::CanvasFrameBuilder& canvas,
    ui::CanvasRect rect,
    ui::CanvasColor color)
    -> std::expected<void, ProductPanelError>
{
    const auto result = canvas.add_filled_box(rect, color);
    if (!result)
    {
        return std::unexpected(
            ProductPanelError{result.error()});
    }
    return {};
}

auto action_rects(
    const ui::PanelLayout& layout)
    -> std::array<ui::CanvasRect, 4U>
{
    const auto gap = 6.0 * layout.effective_scale;
    const auto width =
        (layout.content.width - 3.0 * gap) / 4.0;
    const auto height = std::min(
        38.0 * layout.effective_scale,
        layout.content.height);
    auto result = std::array<ui::CanvasRect, 4U>{};
    for (auto index = std::size_t{};
         index < result.size();
         ++index)
    {
        result[index] = {
            layout.content.x +
                static_cast<double>(index) * (width + gap),
            layout.content.y,
            width,
            height,
        };
    }
    return result;
}

auto feature_action(
    application::ProductUiSection section,
    std::size_t index)
    -> application::ProductUiAction
{
    const auto action = FeatureActions[index];
    if (section == application::ProductUiSection::Paint)
    {
        return application::UiPaintAction{action};
    }
    return application::UiImagePaintAction{action};
}

auto paint_feature_actions(
    ui::WidgetPainter& widgets,
    const ui::PanelLayout& layout,
    application::ProductUiSection section,
    const application::FeatureActionAvailability& availability,
    const ProductPanelLabels& labels,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    const auto ids =
        section == application::ProductUiSection::Paint
            ? PaintActionIds
            : ImageActionIds;
    const auto rects = action_rects(layout);
    const auto enabled = std::array{
        availability.start,
        availability.preview,
        availability.restore,
        availability.cancel,
    };
    const auto text = std::array<std::string_view, 4U>{
        labels.start,
        labels.preview,
        labels.restore,
        labels.cancel,
    };
    for (auto index = std::size_t{};
         index < rects.size();
         ++index)
    {
        const auto response = widgets.button(
            ids[index],
            rects[index],
            layout.content,
            text[index],
            enabled[index],
            false);
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (response->activated && !action)
        {
            action = application::ProductUiActionEnvelope{
                0U,
                feature_action(section, index),
            };
        }
    }
    return {};
}

auto edit_action_rects(const ui::PanelLayout& layout)
    -> std::array<ui::CanvasRect, 3U>
{
    const auto gap = 5.0 * layout.effective_scale;
    const auto width = std::min(
        86.0 * layout.effective_scale,
        (layout.status_strip.width - 4.0 * gap) / 3.0);
    const auto height = std::max(
        1.0,
        layout.status_strip.height - 2.0 * gap);
    const auto start =
        layout.status_strip.x + layout.status_strip.width -
        3.0 * width - 3.0 * gap;
    return {
        ui::CanvasRect{start, layout.status_strip.y + gap, width, height},
        ui::CanvasRect{start + width + gap, layout.status_strip.y + gap, width, height},
        ui::CanvasRect{start + 2.0 * (width + gap), layout.status_strip.y + gap, width, height},
    };
}

auto compose_edit_actions(
    ui::WidgetPainter& widgets,
    const ui::PanelLayout& layout,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels,
    ProductPanelState& state,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    if (state.selected == application::ProductUiSection::Diagnostics)
    {
        return {};
    }
    const auto rects = edit_action_rects(layout);
    const auto active = state.edit_session.has_value();
    const auto enabled = std::array{
        !active && model.settings.can_apply,
        active && model.settings.can_apply,
        active,
    };
    const auto selected = std::array{active, false, false};
    const auto text = std::array<std::string_view, 3U>{
        labels.edit,
        labels.save,
        labels.cancel,
    };
    for (auto index = std::size_t{}; index < rects.size(); ++index)
    {
        const auto response = widgets.button(
            EditActionIds[index],
            rects[index],
            layout.status_strip,
            text[index],
            enabled[index],
            selected[index]);
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (!response->activated)
        {
            continue;
        }
        if (index == 0U)
        {
            state.edit_session = ProductEditSession{
                model.settings.config,
                model.settings.config,
            };
        }
        else if (index == 1U)
        {
            const auto draft = state.edit_session->draft;
            if (draft != model.settings.config && !action)
            {
                action = application::ProductUiActionEnvelope{
                    model.source_revision,
                    application::UiApplySettings{draft},
                };
            }
            state.edit_session.reset();
            state.hotkey_capture = {};
        }
        else
        {
            state.edit_session.reset();
            state.hotkey_capture = {};
            state.image_editor.interaction = {};
            state.image_editor.draft.reset();
            state.image_editor.crop.reset();
            state.image_editor.crop_dragging = false;
        }
    }
    return {};
}

auto model_for_edit(
    const application::ProductUiModel& model,
    const ProductPanelState& state)
    -> application::ProductUiModel
{
    auto result = model;
    const auto active = state.edit_session.has_value();
    if (active)
    {
        const auto& draft = state.edit_session->draft;
        result.settings.config = draft;
        result.paint.settings = draft.paint;
        result.esp.enabled = draft.esp.enabled;
        result.esp.settings = draft.esp;
        if (!result.image_paint.document)
        {
            result.image_paint.settings = draft.image_paint;
        }
    }
    result.settings.can_apply =
        active && model.settings.can_apply;
    result.esp.can_toggle =
        active && model.esp.can_toggle;
    if (!active)
    {
        result.image_paint.project.edit = false;
        result.image_paint.project.load = false;
        result.image_paint.project.save = false;
        result.image_paint.project.rename = false;
        result.image_paint.project.remove = false;
    }
    return result;
}

} // namespace

auto build_product_panel_labels(
    const application::LocalizationCatalog& catalog,
    std::string_view locale) -> ProductPanelLabels
{
    const auto image_remove = catalog.format(
        locale,
        "image.action.remove",
        std::array<std::string_view, 1U>{
            catalog.text(locale, "settings.image"),
        });
    return {
        {
            std::string{catalog.text(locale, "settings.paint")},
            std::string{
                catalog.text(locale, "manual.image.paint")},
            "ESP",
            std::string{catalog.text(locale, "settings")},
            std::string{catalog.text(locale, "status.logs")},
        },
        std::string{catalog.text(locale, "button.start")},
        std::string{catalog.text(locale, "button.preview")},
        std::string{catalog.text(locale, "button.unpreview")},
        std::string{catalog.text(locale, "button.stop")},
        std::string{catalog.text(locale, "button.edit")},
        std::string{catalog.text(locale, "button.save")},
        std::string{catalog.text(locale, "language")},
        std::string{catalog.text(locale, "theme.color")},
        std::string{
            catalog.text(locale, "dialog.hotkey.title")},
        [&catalog, locale]
        {
            const auto formatted = catalog.format(
                locale,
                "toast.hotkey.duplicate",
                std::array<std::string_view, 1U>{""});
            return formatted ? *formatted : std::string{};
        }(),
        std::string{catalog.text(locale, "button.load.preset")},
        std::string{catalog.text(locale, "button.upload")},
        std::string{catalog.text(locale, "button.save.preset")},
        std::string{catalog.text(locale, "image.action.wrap")},
        std::string{catalog.text(locale, "image.action.mirror")},
        std::string{catalog.text(locale, "image.action.crop")},
        image_remove ? *image_remove : std::string{},
        std::string{catalog.text(locale, "dialog.crop.zoom")},
        std::string{catalog.text(locale, "dialog.crop.apply")},
        std::string{catalog.text(locale, "button.cancel")},
        std::string{catalog.text(locale, "metric.progress")},
        std::string{catalog.text(locale, "metric.elapsed")},
        std::string{catalog.text(locale, "metric.eta")},
        std::string{catalog.text(locale, "metric.queue")},
        std::string{catalog.text(locale, "status.service")},
        std::string{catalog.text(locale, "footer.game")},
        std::string{catalog.text(locale, "app.title")} + " " +
            std::string{catalog.text(locale, "metric.queue")},
        std::string{catalog.text(locale, "status.service")} + " " +
            std::string{catalog.text(locale, "metric.queue")},
        std::string{catalog.text(locale, "logs.empty")},
        std::string{
            catalog.text(locale, "error.operation.failed")},
        {
            std::string{catalog.text(locale, "state.waiting")},
            std::string{catalog.text(locale, "state.ready")},
            std::string{catalog.text(locale, "state.stopped")},
            std::string{catalog.text(locale, "state.failed")},
        },
        {
            std::string{catalog.text(locale, "log.info")},
            std::string{catalog.text(locale, "log.warn")},
            std::string{catalog.text(locale, "log.error")},
        },
        {
            std::string{catalog.text(locale, "app.title")},
            std::string{catalog.text(locale, "start.hotkey")},
            std::string{catalog.text(locale, "preview.hotkey")},
            std::string{
                catalog.text(locale, "unpreview.hotkey")},
            std::string{catalog.text(locale, "stop.hotkey")},
            std::string{
                catalog.text(locale, "hotkey.image.paint")},
            std::string{
                catalog.text(locale, "hotkey.image.preview")},
            std::string{
                catalog.text(
                    locale,
                    "hotkey.image.unpreview")},
            std::string{
                catalog.text(locale, "hotkey.image.stop")},
        },
        {
            std::string{catalog.text(locale, "brush.size")},
            std::string{catalog.text(locale, "region.front")},
            std::string{catalog.text(locale, "region.sides")},
            std::string{catalog.text(locale, "region.back")},
            std::string{catalog.text(locale, "metallic")},
            std::string{catalog.text(locale, "roughness")},
            std::string{catalog.text(locale, "emissive")},
            std::string{catalog.text(locale, "fill.material")} +
                " " +
                std::string{catalog.text(locale, "fill.color")},
            std::string{catalog.text(locale, "fill.metallic")},
            std::string{catalog.text(locale, "fill.roughness")},
            std::string{catalog.text(locale, "fill.material")} +
                " " +
                std::string{catalog.text(locale, "emissive")},
            std::string{
                catalog.text(
                    locale,
                    "color.compression.tolerance")},
        },
        {
            std::string{catalog.text(locale, "mode.paint")},
            std::string{catalog.text(locale, "mode.fill")},
            std::string{catalog.text(locale, "mode.skip")},
        },
        {
            std::string{catalog.text(locale, "image.body.type")},
            std::string{catalog.text(locale, "image.action.fit")},
            std::string{catalog.text(locale, "opacity")},
            std::string{catalog.text(locale, "region.front")},
            std::string{catalog.text(locale, "region.right")},
            std::string{catalog.text(locale, "region.back")},
            std::string{catalog.text(locale, "region.left")},
            std::string{catalog.text(locale, "brush.size")},
            std::string{
                catalog.text(
                    locale,
                    "color.compression.tolerance")},
            std::string{catalog.text(locale, "metallic")},
            std::string{catalog.text(locale, "roughness")},
            std::string{catalog.text(locale, "emissive")},
            std::string{catalog.text(locale, "fill.material")} +
                " " +
                std::string{catalog.text(locale, "fill.color")},
            std::string{catalog.text(locale, "fill.metallic")},
            std::string{catalog.text(locale, "fill.roughness")},
            std::string{catalog.text(locale, "fill.material")} +
                " " +
                std::string{catalog.text(locale, "emissive")},
        },
        {
            std::string{catalog.text(locale, "body.round")},
            std::string{catalog.text(locale, "body.cube")},
            std::string{catalog.text(locale, "body.fukuyoka")},
        },
        {
            std::string{catalog.text(locale, "image.action.fit")},
            std::string{catalog.text(locale, "mode.fill")},
        },
        {
            std::string{catalog.text(locale, "mode.skip")},
            std::string{catalog.text(locale, "mode.fill")},
        },
        {
            std::string{catalog.text(locale, "mode.fill")},
            std::string{catalog.text(locale, "mode.skip")},
        },
        {
            "Targets",
            "Boxes",
            "Skeletons",
            "Names",
            "Distance",
            "Snaplines",
            "Hider RGB",
            "Hunter RGB",
        },
        {
            std::string{catalog.text(locale, "log.all")},
            "Hider",
            "Hunter",
        },
    };
}

auto compose_product_panel(
    const application::ProductUiModel& model,
    ProductPanelState previous,
    ProductPanelInput input,
    const ProductPanelLabels& labels)
    -> std::expected<ProductPanelOutput, ProductPanelError>
{
    if (!model_valid(model))
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidModel});
    }
    if (!state_valid(previous))
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidState});
    }
    if (!labels_valid(labels))
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidLabels});
    }
    if (!image_assets_valid(model, input))
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidImageAssets});
    }
    if ((!input.function_key_input_available &&
         input.function_key_pressed) ||
        (input.function_key_pressed &&
         (static_cast<unsigned>(*input.function_key_pressed) < 1U ||
          static_cast<unsigned>(*input.function_key_pressed) > 24U)))
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidInput});
    }
    if (previous.edit_session &&
        previous.edit_session->base != model.settings.config)
    {
        previous.edit_session.reset();
        previous.hotkey_capture = {};
        previous.image_editor.interaction = {};
        previous.image_editor.draft.reset();
        previous.image_editor.crop.reset();
        previous.image_editor.crop_dragging = false;
    }

    auto canvas = ui::CanvasFrameBuilder{input.viewport};
    auto interaction = ui::InteractionFrame{
        std::move(previous.interaction),
        input.pointer,
        model.ui_open,
        input.keyboard,
    };

    if (!model.ui_open)
    {
        const auto next_interaction =
            std::move(interaction).finish();
        if (!next_interaction)
        {
            return std::unexpected(ProductPanelError{
                next_interaction.error()});
        }
        const auto frame = std::move(canvas).finish();
        if (!frame)
        {
            return std::unexpected(
                ProductPanelError{frame.error()});
        }
        previous.interaction = *next_interaction;
        previous.hotkey_capture = {};
        previous.image_editor = {};
        previous.edit_session.reset();
        return ProductPanelOutput{
            std::move(*frame),
            std::nullopt,
            std::move(previous),
            std::nullopt,
            std::nullopt,
        };
    }

    const auto presented_model = model_for_edit(model, previous);
    const auto layout = ui::build_panel_layout({
        input.viewport,
        input.safe_area,
        presented_model.settings.config.ui.scale,
    });
    if (!layout)
    {
        return std::unexpected(
            ProductPanelError{layout.error()});
    }
    if (const auto background =
            add_box(canvas, layout->panel, PanelBackground);
        !background)
    {
        return std::unexpected(background.error());
    }
    auto widgets = ui::WidgetPainter{
        canvas,
        interaction,
        ui::default_widget_palette(
            accent(presented_model.settings.config.ui.theme_color)),
        layout->effective_scale,
    };

    for (auto index = std::size_t{};
         index < model.sections.size();
         ++index)
    {
        const auto response = widgets.button(
            TabIds[index],
            layout->section_tabs[index],
            layout->tab_strip,
            labels.section_labels[index],
            true,
            previous.selected == model.sections[index]);
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (response->activated)
        {
            previous.selected = model.sections[index];
        }
    }

    if (previous.selected !=
        application::ProductUiSection::Settings)
    {
        previous.hotkey_capture = {};
    }

    auto action =
        std::optional<application::ProductUiActionEnvelope>{};
    auto effect =
        std::optional<application::ProductUiEffectEnvelope>{};
    if (previous.selected ==
        application::ProductUiSection::Paint)
    {
        const auto result = paint_feature_actions(
            widgets,
            *layout,
            previous.selected,
            presented_model.paint.actions,
            labels,
            action);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        const auto settings =
            detail::compose_paint_settings_section(
                canvas,
                widgets,
                *layout,
                presented_model,
                labels,
                input,
                previous);
        if (!settings)
        {
            return std::unexpected(settings.error());
        }
    }
    else if (
        previous.selected ==
        application::ProductUiSection::ImagePaint)
    {
        const auto result = paint_feature_actions(
            widgets,
            *layout,
            previous.selected,
            presented_model.image_paint.actions,
            labels,
            action);
        if (!result)
        {
            return std::unexpected(result.error());
        }
        const auto settings =
            detail::compose_image_settings_section(
                canvas,
                widgets,
                *layout,
                presented_model,
                labels,
                input,
                previous,
                action,
                effect);
        if (!settings)
        {
            return std::unexpected(settings.error());
        }
    }
    else if (
        previous.selected ==
        application::ProductUiSection::Esp)
    {
        const auto height = std::min(
            38.0 * layout->effective_scale,
            layout->content.height);
        const auto toggle = widgets.toggle(
            EspToggleId,
            ui::CanvasRect{
                layout->content.x,
                layout->content.y,
                layout->content.width,
                height,
            },
            layout->content,
            "ESP",
            presented_model.esp.enabled,
            presented_model.esp.can_toggle);
        if (!toggle)
        {
            return std::unexpected(
                ProductPanelError{toggle.error()});
        }
        if (toggle->changed)
        {
            previous.edit_session->draft.esp.enabled =
                toggle->value;
        }
        const auto settings =
            detail::compose_esp_settings_section(
                canvas,
                widgets,
                *layout,
                presented_model,
                labels,
                input,
                previous);
        if (!settings)
        {
            return std::unexpected(settings.error());
        }
    }
    else if (
        previous.selected ==
        application::ProductUiSection::Settings)
    {
        const auto result = detail::compose_settings_section(
            canvas,
            interaction,
            widgets,
            *layout,
            presented_model,
            labels,
            input,
            previous,
            action);
        if (!result)
        {
            return std::unexpected(result.error());
        }
    }
    else if (
        previous.selected ==
        application::ProductUiSection::Diagnostics)
    {
        const auto result =
            detail::compose_diagnostics_section(
                canvas,
                *layout,
                model,
                labels,
                input,
                previous);
        if (!result)
        {
            return std::unexpected(result.error());
        }
    }

    if (action)
    {
        action->expected_snapshot_revision =
            model.source_revision;
    }
    if (effect)
    {
        effect->expected_snapshot_revision =
            model.source_revision;
        action.reset();
    }

    if (const auto status = detail::compose_status_strip(
            canvas,
            *layout,
            model,
            labels);
        !status)
    {
        return std::unexpected(status.error());
    }
    const auto edit_actions = compose_edit_actions(
        widgets,
        *layout,
        model,
        labels,
        previous,
        action);
    if (!edit_actions)
    {
        return std::unexpected(edit_actions.error());
    }
    if (action)
    {
        action->expected_snapshot_revision = model.source_revision;
    }

    const auto next_interaction =
        std::move(interaction).finish();
    if (!next_interaction)
    {
        return std::unexpected(
            ProductPanelError{next_interaction.error()});
    }
    const auto frame = std::move(canvas).finish();
    if (!frame)
    {
        return std::unexpected(
            ProductPanelError{frame.error()});
    }
    previous.interaction = *next_interaction;
    return ProductPanelOutput{
        std::move(*frame),
        *layout,
        std::move(previous),
        std::move(action),
        std::move(effect),
    };
}
} // namespace meccha::product_ui
