#include <meccha/product_ui/product_panel.hpp>

#include "product_panel_esp.hpp"
#include "product_panel_image.hpp"
#include "product_panel_paint.hpp"
#include "product_panel_settings.hpp"

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <string>
#include <utility>

namespace meccha::product_ui
{
namespace
{
constexpr auto PanelBackground =
    ui::CanvasColor{18U, 20U, 26U, 238U};
constexpr auto StatusBackground =
    ui::CanvasColor{25U, 28U, 35U, 245U};
constexpr auto StatusText =
    ui::CanvasColor{220U, 224U, 232U, 255U};

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

auto labels_valid(const ProductPanelLabels& labels) -> bool
{
    return std::ranges::all_of(
               labels.section_labels,
               valid_label) &&
           valid_label(labels.start) &&
           valid_label(labels.preview) &&
           valid_label(labels.restore) &&
           valid_label(labels.cancel) &&
           valid_label(labels.language) &&
           valid_label(labels.theme_color) &&
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

auto model_valid(const application::ProductUiModel& model) -> bool
{
    const auto image_settings_valid =
        core::validate(model.image_paint.settings).empty();
    const auto image_document_valid =
        model.image_paint.document
            ? core::valid_image_project_id(
                  model.image_paint.document->project_id) &&
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
               model.settings.config.esp.enabled;
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
    return section_valid && editor_identity_valid &&
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
    return assets.atlas_texture.identity != 0U &&
           assets.project_id == document.project_id &&
           assets.project_revision == document.revision &&
           pipeline.phase ==
               application::ImageEditorPipelinePhase::Ready &&
           pipeline.project_id == document.project_id &&
           pipeline.project_revision == document.revision &&
           !pipeline.pending && !pipeline.failure;
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

auto add_text(
    ui::CanvasFrameBuilder& canvas,
    ui::CanvasPoint anchor,
    std::string_view text,
    ui::CanvasColor color,
    double scale)
    -> std::expected<void, ProductPanelError>
{
    const auto result =
        canvas.add_text(anchor, text, color, scale);
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

auto status_line(const application::ProductUiModel& model)
    -> std::string
{
    return std::to_string(model.progress.completed) + " / " +
           std::to_string(model.progress.total) + "   |   " +
           std::to_string(
               model.diagnostics.command_queue.queued) +
           " / " +
           std::to_string(
               model.diagnostics.command_queue.capacity);
}
} // namespace

auto build_product_panel_labels(
    const application::LocalizationCatalog& catalog,
    std::string_view locale) -> ProductPanelLabels
{
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
        std::string{catalog.text(locale, "language")},
        std::string{catalog.text(locale, "theme.color")},
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
            std::string{catalog.text(locale, "region.sides")} +
                " UV",
            std::string{catalog.text(locale, "region.front")} +
                "/" +
                std::string{catalog.text(locale, "region.back")} +
                " UV",
            std::string{catalog.text(locale, "region.front")},
            std::string{catalog.text(locale, "region.sides")},
            std::string{catalog.text(locale, "region.back")},
            std::string{catalog.text(locale, "auto.material")},
            std::string{catalog.text(locale, "include.shadows")},
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
        previous.image_editor = {};
        return ProductPanelOutput{
            std::move(*frame),
            std::nullopt,
            std::move(previous),
            std::nullopt,
        };
    }

    const auto layout = ui::build_panel_layout({
        input.viewport,
        input.safe_area,
        model.settings.config.ui.scale,
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
    if (const auto status =
            add_box(canvas, layout->status_strip, StatusBackground);
        !status)
    {
        return std::unexpected(status.error());
    }

    auto widgets = ui::WidgetPainter{
        canvas,
        interaction,
        ui::default_widget_palette(
            accent(model.settings.config.ui.theme_color)),
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

    auto action =
        std::optional<application::ProductUiActionEnvelope>{};
    if (previous.selected ==
        application::ProductUiSection::Paint)
    {
        const auto result = paint_feature_actions(
            widgets,
            *layout,
            previous.selected,
            model.paint.actions,
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
                model,
                labels,
                input,
                previous,
                action);
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
            model.image_paint.actions,
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
                model,
                labels,
                input,
                previous,
                action);
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
            model.esp.enabled,
            model.esp.can_toggle);
        if (!toggle)
        {
            return std::unexpected(
                ProductPanelError{toggle.error()});
        }
        if (toggle->changed)
        {
            action = application::ProductUiActionEnvelope{
                0U,
                application::UiToggleEsp{},
            };
        }
        const auto settings =
            detail::compose_esp_settings_section(
                canvas,
                widgets,
                *layout,
                model,
                labels,
                input,
                previous,
                action);
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
            model,
            labels,
            input,
            previous,
            action);
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

    const auto status = status_line(model);
    if (const auto result = add_text(
            canvas,
            {
                layout->status_strip.x +
                    12.0 * layout->effective_scale,
                layout->status_strip.y +
                    14.0 * layout->effective_scale,
            },
            status,
            StatusText,
            0.85 * layout->effective_scale);
        !result)
    {
        return std::unexpected(result.error());
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
    };
}
} // namespace meccha::product_ui
