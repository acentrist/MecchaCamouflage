#include "product_panel_esp.hpp"

#include <meccha/core/config.hpp>

#include <array>
#include <expected>
#include <string_view>
#include <utility>

namespace meccha::product_ui::detail
{
namespace
{
constexpr auto EspScrollIndex = std::size_t{2U};
constexpr auto EspRowCount = std::size_t{8U};
constexpr auto EspControlIds = std::array{
    ui::WidgetId{601U},
    ui::WidgetId{602U},
    ui::WidgetId{603U},
    ui::WidgetId{604U},
    ui::WidgetId{605U},
    ui::WidgetId{606U},
};
constexpr auto HiderColorIds = std::array{
    ui::WidgetId{621U},
    ui::WidgetId{622U},
    ui::WidgetId{623U},
};
constexpr auto HunterColorIds = std::array{
    ui::WidgetId{624U},
    ui::WidgetId{625U},
    ui::WidgetId{626U},
};
constexpr auto TextColor =
    ui::CanvasColor{220U, 224U, 232U, 255U};

auto intersects(
    const ui::CanvasRect& left,
    const ui::CanvasRect& right) -> bool
{
    return left.x < right.x + right.width &&
           left.x + left.width > right.x &&
           left.y < right.y + right.height &&
           left.y + left.height > right.y;
}

auto add_clipped_text(
    ui::CanvasFrameBuilder& canvas,
    ui::CanvasRect clip,
    ui::CanvasPoint anchor,
    std::string_view text,
    double scale)
    -> std::expected<void, ProductPanelError>
{
    const auto pushed = canvas.push_clip(clip);
    if (!pushed)
    {
        return std::unexpected(
            ProductPanelError{pushed.error()});
    }
    const auto added =
        canvas.add_text(anchor, text, TextColor, scale);
    const auto popped = canvas.pop_clip();
    if (!added)
    {
        return std::unexpected(
            ProductPanelError{added.error()});
    }
    if (!popped)
    {
        return std::unexpected(
            ProductPanelError{popped.error()});
    }
    return {};
}

auto publish_settings(
    core::ApplicationConfig config,
    const application::ProductUiModel& model,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    if (!core::validate(config).empty())
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidModel});
    }
    if (!action)
    {
        action = application::ProductUiActionEnvelope{
            model.source_revision,
            application::UiApplySettings{
                std::move(config),
            },
        };
    }
    return {};
}

auto scope_index(core::EspScope scope) -> std::size_t
{
    switch (scope)
    {
    case core::EspScope::All:
        return 0U;
    case core::EspScope::Hider:
        return 1U;
    case core::EspScope::Hunter:
        return 2U;
    }
    return 0U;
}

auto next_scope(core::EspScope scope) -> core::EspScope
{
    switch (scope)
    {
    case core::EspScope::All:
        return core::EspScope::Hider;
    case core::EspScope::Hider:
        return core::EspScope::Hunter;
    case core::EspScope::Hunter:
        return core::EspScope::All;
    }
    return core::EspScope::All;
}
} // namespace

auto compose_esp_settings_section(
    ui::CanvasFrameBuilder& canvas,
    ui::WidgetPainter& widgets,
    const ui::PanelLayout& layout,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels,
    const ProductPanelInput& input,
    ProductPanelState& state,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    const auto toggle_height =
        38.0 * layout.effective_scale;
    const auto toggle_gap = 8.0 * layout.effective_scale;
    const auto viewport = ui::CanvasRect{
        layout.content.x,
        layout.content.y + toggle_height + toggle_gap,
        layout.content.width,
        layout.content.height - toggle_height - toggle_gap,
    };
    const auto row_height = 44.0 * layout.effective_scale;
    const auto scroll = ui::update_scroll_container(
        state.section_scroll[EspScrollIndex],
        ui::ScrollContainerInput{
            viewport,
            layout.content,
            row_height * static_cast<double>(EspRowCount),
            row_height,
            input.pointer,
        });
    if (!scroll)
    {
        return std::unexpected(
            ProductPanelError{scroll.error()});
    }
    state.section_scroll[EspScrollIndex] = scroll->state;

    const auto label_width = viewport.width * 0.42;
    const auto gap = 8.0 * layout.effective_scale;
    const auto control = [&](std::size_t row)
    {
        return ui::CanvasRect{
            viewport.x + label_width + gap,
            scroll->content_origin_y +
                static_cast<double>(row) * row_height +
                3.0 * layout.effective_scale,
            viewport.width - label_width - gap,
            row_height - 6.0 * layout.effective_scale,
        };
    };
    const auto enabled = [&](std::size_t row)
    {
        return model.settings.can_apply &&
               intersects(control(row), viewport);
    };
    const auto label = [&](std::size_t row)
        -> std::expected<void, ProductPanelError>
    {
        return add_clipped_text(
            canvas,
            viewport,
            {
                viewport.x,
                scroll->content_origin_y +
                    static_cast<double>(row) * row_height +
                    14.0 * layout.effective_scale,
            },
            labels.esp_setting_labels[row],
            0.82 * layout.effective_scale);
    };
    const auto toggle = [&](
                            std::size_t row,
                            bool current,
                            auto&& assign)
        -> std::expected<void, ProductPanelError>
    {
        if (const auto drawn = label(row); !drawn)
        {
            return drawn;
        }
        const auto response = widgets.toggle(
            EspControlIds[row],
            control(row),
            viewport,
            labels.esp_setting_labels[row],
            current,
            enabled(row));
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (response->changed)
        {
            auto config = model.settings.config;
            assign(config.esp, response->value);
            return publish_settings(
                std::move(config),
                model,
                action);
        }
        return {};
    };

    if (const auto drawn = label(0U); !drawn)
    {
        return drawn;
    }
    const auto scope = widgets.button(
        EspControlIds[0U],
        control(0U),
        viewport,
        labels.esp_scope_labels[
            scope_index(model.esp.settings.scope)],
        enabled(0U),
        false);
    if (!scope)
    {
        return std::unexpected(
            ProductPanelError{scope.error()});
    }
    if (scope->activated)
    {
        auto config = model.settings.config;
        config.esp.scope = next_scope(config.esp.scope);
        if (const auto published = publish_settings(
                std::move(config),
                model,
                action);
            !published)
        {
            return published;
        }
    }

    if (const auto result = toggle(
            1U,
            model.esp.settings.boxes,
            [](core::EspSettings& settings, bool value)
            {
                settings.boxes = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = toggle(
            2U,
            model.esp.settings.skeletons,
            [](core::EspSettings& settings, bool value)
            {
                settings.skeletons = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = toggle(
            3U,
            model.esp.settings.names,
            [](core::EspSettings& settings, bool value)
            {
                settings.names = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = toggle(
            4U,
            model.esp.settings.distance,
            [](core::EspSettings& settings, bool value)
            {
                settings.distance = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = toggle(
            5U,
            model.esp.settings.snaplines,
            [](core::EspSettings& settings, bool value)
            {
                settings.snaplines = value;
            });
        !result)
    {
        return result;
    }

    const auto color = [&](
                           std::size_t row,
                           std::array<ui::WidgetId, 3U> ids,
                           core::Rgb8 current,
                           auto&& assign)
        -> std::expected<void, ProductPanelError>
    {
        if (const auto drawn = label(row); !drawn)
        {
            return drawn;
        }
        const auto response = widgets.color_control(
            ids,
            control(row),
            viewport,
            ui::CanvasColor{
                current.red,
                current.green,
                current.blue,
                255U,
            },
            enabled(row));
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (response->changed)
        {
            auto config = model.settings.config;
            assign(
                config.esp,
                core::Rgb8{
                    response->value.red,
                    response->value.green,
                    response->value.blue,
                });
            return publish_settings(
                std::move(config),
                model,
                action);
        }
        return {};
    };

    if (const auto result = color(
            6U,
            HiderColorIds,
            model.esp.settings.hider_color,
            [](core::EspSettings& settings, core::Rgb8 value)
            {
                settings.hider_color = value;
            });
        !result)
    {
        return result;
    }
    return color(
        7U,
        HunterColorIds,
        model.esp.settings.hunter_color,
        [](core::EspSettings& settings, core::Rgb8 value)
        {
            settings.hunter_color = value;
        });
}
} // namespace meccha::product_ui::detail
