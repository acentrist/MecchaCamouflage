#include <meccha/product_ui/product_panel.hpp>

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
               valid_label);
}

auto model_valid(const application::ProductUiModel& model) -> bool
{
    return model.sections == application::ProductUiSections &&
           core::validate(model.settings.config).empty() &&
           model.paint.settings ==
               model.settings.config.paint &&
           model.esp.settings ==
               model.settings.config.esp &&
           model.esp.enabled ==
               model.settings.config.esp.enabled;
}

auto state_valid(const ProductPanelState& state) -> bool
{
    return std::ranges::find(
               application::ProductUiSections,
               state.selected) !=
           application::ProductUiSections.end();
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
