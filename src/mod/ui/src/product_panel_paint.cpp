#include "product_panel_paint.hpp"

#include <meccha/core/config.hpp>

#include <array>
#include <expected>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::product_ui::detail
{
namespace
{
constexpr auto PaintScrollIndex = std::size_t{0U};
constexpr auto PaintRowCount = std::size_t{16U};
constexpr auto PaintControlIds = std::array{
    ui::WidgetId{501U},
    ui::WidgetId{502U},
    ui::WidgetId{503U},
    ui::WidgetId{504U},
    ui::WidgetId{505U},
    ui::WidgetId{506U},
    ui::WidgetId{507U},
    ui::WidgetId{508U},
    ui::WidgetId{509U},
    ui::WidgetId{510U},
    ui::WidgetId{511U},
    ui::WidgetId{512U},
    ui::WidgetId{513U},
    ui::WidgetId{514U},
    ui::WidgetId{515U},
    ui::WidgetId{516U},
};
constexpr auto FillColorIds = std::array{
    ui::WidgetId{521U},
    ui::WidgetId{522U},
    ui::WidgetId{523U},
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

auto number_text(double value, unsigned precision) -> std::string
{
    auto stream = std::ostringstream{};
    stream << std::fixed << std::setprecision(
        static_cast<int>(precision))
           << value;
    return std::move(stream).str();
}

auto mode_index(core::RegionMode mode) -> std::size_t
{
    switch (mode)
    {
    case core::RegionMode::Paint:
        return 0U;
    case core::RegionMode::Fill:
        return 1U;
    case core::RegionMode::Skip:
        return 2U;
    }
    return 0U;
}

auto next_mode(core::RegionMode mode) -> core::RegionMode
{
    switch (mode)
    {
    case core::RegionMode::Paint:
        return core::RegionMode::Fill;
    case core::RegionMode::Fill:
        return core::RegionMode::Skip;
    case core::RegionMode::Skip:
        return core::RegionMode::Paint;
    }
    return core::RegionMode::Paint;
}
} // namespace

auto compose_paint_settings_section(
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
    const auto action_height =
        38.0 * layout.effective_scale;
    const auto action_gap = 8.0 * layout.effective_scale;
    const auto viewport = ui::CanvasRect{
        layout.content.x,
        layout.content.y + action_height + action_gap,
        layout.content.width,
        layout.content.height - action_height - action_gap,
    };
    const auto row_height = 44.0 * layout.effective_scale;
    const auto scroll = ui::update_scroll_container(
        state.section_scroll[PaintScrollIndex],
        ui::ScrollContainerInput{
            viewport,
            layout.content,
            row_height * static_cast<double>(PaintRowCount),
            row_height,
            input.pointer,
        });
    if (!scroll)
    {
        return std::unexpected(
            ProductPanelError{scroll.error()});
    }
    state.section_scroll[PaintScrollIndex] = scroll->state;

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
    const auto label = [&](
                           std::size_t row,
                           std::string text)
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
            text,
            0.82 * layout.effective_scale);
    };
    const auto slider = [&](
                            std::size_t row,
                            double current,
                            double minimum,
                            double maximum,
                            unsigned precision,
                            auto&& assign)
        -> std::expected<void, ProductPanelError>
    {
        const auto text =
            labels.paint_setting_labels[row] + "  " +
            number_text(current, precision);
        if (const auto drawn = label(row, text); !drawn)
        {
            return drawn;
        }
        const auto response = widgets.slider(
            PaintControlIds[row],
            control(row),
            viewport,
            current,
            minimum,
            maximum,
            enabled(row));
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (response->changed)
        {
            auto config = model.settings.config;
            assign(config.paint, response->value);
            return publish_settings(
                std::move(config),
                model,
                action);
        }
        return {};
    };
    const auto toggle = [&](
                            std::size_t row,
                            bool current,
                            auto&& assign)
        -> std::expected<void, ProductPanelError>
    {
        if (const auto drawn =
                label(row, labels.paint_setting_labels[row]);
            !drawn)
        {
            return drawn;
        }
        const auto response = widgets.toggle(
            PaintControlIds[row],
            control(row),
            viewport,
            labels.paint_setting_labels[row],
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
            assign(config.paint, response->value);
            return publish_settings(
                std::move(config),
                model,
                action);
        }
        return {};
    };
    const auto region_mode = [&](
                                 std::size_t row,
                                 core::RegionMode current,
                                 auto&& assign)
        -> std::expected<void, ProductPanelError>
    {
        if (const auto drawn =
                label(row, labels.paint_setting_labels[row]);
            !drawn)
        {
            return drawn;
        }
        const auto response = widgets.button(
            PaintControlIds[row],
            control(row),
            viewport,
            labels.region_mode_labels[mode_index(current)],
            enabled(row),
            false);
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (response->activated)
        {
            auto config = model.settings.config;
            assign(config.paint, next_mode(current));
            return publish_settings(
                std::move(config),
                model,
                action);
        }
        return {};
    };

    if (const auto result = slider(
            0U,
            model.paint.settings.brush_size_texels,
            1.0,
            10.0,
            1U,
            [](core::PaintSettings& settings, double value)
            {
                settings.brush_size_texels = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            1U,
            model.paint.settings.side_source_max_uv,
            0.001,
            0.5,
            3U,
            [](core::PaintSettings& settings, double value)
            {
                settings.side_source_max_uv = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            2U,
            model.paint.settings.front_back_source_max_uv,
            0.001,
            2.0,
            3U,
            [](core::PaintSettings& settings, double value)
            {
                settings.front_back_source_max_uv = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = region_mode(
            3U,
            model.paint.settings.front_mode,
            [](core::PaintSettings& settings, core::RegionMode value)
            {
                settings.front_mode = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = region_mode(
            4U,
            model.paint.settings.side_mode,
            [](core::PaintSettings& settings, core::RegionMode value)
            {
                settings.side_mode = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = region_mode(
            5U,
            model.paint.settings.back_mode,
            [](core::PaintSettings& settings, core::RegionMode value)
            {
                settings.back_mode = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = toggle(
            6U,
            model.paint.settings.auto_material,
            [](core::PaintSettings& settings, bool value)
            {
                settings.auto_material = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = toggle(
            7U,
            model.paint.settings.include_scene_lighting,
            [](core::PaintSettings& settings, bool value)
            {
                settings.include_scene_lighting = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            8U,
            model.paint.settings.paint_material.metallic,
            0.0,
            1.0,
            2U,
            [](core::PaintSettings& settings, double value)
            {
                settings.paint_material.metallic = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            9U,
            model.paint.settings.paint_material.roughness,
            0.0,
            1.0,
            2U,
            [](core::PaintSettings& settings, double value)
            {
                settings.paint_material.roughness = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            10U,
            model.paint.settings.paint_material.emissive,
            0.0,
            1.0,
            2U,
            [](core::PaintSettings& settings, double value)
            {
                settings.paint_material.emissive = value;
            });
        !result)
    {
        return result;
    }

    if (const auto drawn =
            label(11U, labels.paint_setting_labels[11U]);
        !drawn)
    {
        return drawn;
    }
    const auto fill = model.paint.settings.fill_color;
    const auto fill_color = widgets.color_control(
        FillColorIds,
        control(11U),
        viewport,
        ui::CanvasColor{
            fill.red,
            fill.green,
            fill.blue,
            255U,
        },
        enabled(11U));
    if (!fill_color)
    {
        return std::unexpected(
            ProductPanelError{fill_color.error()});
    }
    if (fill_color->changed)
    {
        auto config = model.settings.config;
        config.paint.fill_color = {
            fill_color->value.red,
            fill_color->value.green,
            fill_color->value.blue,
        };
        if (const auto published = publish_settings(
                std::move(config),
                model,
                action);
            !published)
        {
            return published;
        }
    }

    if (const auto result = slider(
            12U,
            model.paint.settings.fill_material.metallic,
            0.0,
            1.0,
            2U,
            [](core::PaintSettings& settings, double value)
            {
                settings.fill_material.metallic = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            13U,
            model.paint.settings.fill_material.roughness,
            0.0,
            1.0,
            2U,
            [](core::PaintSettings& settings, double value)
            {
                settings.fill_material.roughness = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            14U,
            model.paint.settings.fill_material.emissive,
            0.0,
            1.0,
            2U,
            [](core::PaintSettings& settings, double value)
            {
                settings.fill_material.emissive = value;
            });
        !result)
    {
        return result;
    }
    return slider(
        15U,
        model.paint.settings.color_compression_tolerance_percent,
        0.0,
        10.0,
        1U,
        [](core::PaintSettings& settings, double value)
        {
            settings.color_compression_tolerance_percent = value;
        });
}
} // namespace meccha::product_ui::detail
