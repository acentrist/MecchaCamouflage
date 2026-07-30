#include "product_panel_image.hpp"

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
constexpr auto ImageScrollIndex = std::size_t{1U};
constexpr auto ImageRowCount = std::size_t{16U};
constexpr auto ImageControlIds = std::array{
    ui::WidgetId{601U},
    ui::WidgetId{602U},
    ui::WidgetId{603U},
    ui::WidgetId{604U},
    ui::WidgetId{605U},
    ui::WidgetId{606U},
    ui::WidgetId{607U},
    ui::WidgetId{608U},
    ui::WidgetId{609U},
    ui::WidgetId{610U},
    ui::WidgetId{611U},
    ui::WidgetId{612U},
    ui::WidgetId{613U},
    ui::WidgetId{614U},
    ui::WidgetId{615U},
    ui::WidgetId{616U},
};
constexpr auto FillColorIds = std::array{
    ui::WidgetId{621U},
    ui::WidgetId{622U},
    ui::WidgetId{623U},
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

auto number_text(double value, unsigned precision) -> std::string
{
    auto stream = std::ostringstream{};
    stream << std::fixed << std::setprecision(
        static_cast<int>(precision))
           << value;
    return std::move(stream).str();
}

auto publish_settings(
    core::ImageProjectSettings settings,
    const application::ProductUiModel& model,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    if (!model.image_paint.document ||
        settings == model.image_paint.settings ||
        !core::validate(settings).empty())
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidModel});
    }
    if (!action)
    {
        action = application::ProductUiActionEnvelope{
            model.source_revision,
            application::UiMutateCurrentImageProject{
                application::ReplaceImageProjectSettingsMutation{
                    std::move(settings),
                },
            },
        };
    }
    return {};
}

auto body_index(core::BodyProfile profile) -> std::size_t
{
    switch (profile)
    {
    case core::BodyProfile::Round:
        return 0U;
    case core::BodyProfile::Cube:
        return 1U;
    case core::BodyProfile::Fukuyoka:
        return 2U;
    }
    return 0U;
}

auto next_body(core::BodyProfile profile) -> core::BodyProfile
{
    switch (profile)
    {
    case core::BodyProfile::Round:
        return core::BodyProfile::Cube;
    case core::BodyProfile::Cube:
        return core::BodyProfile::Fukuyoka;
    case core::BodyProfile::Fukuyoka:
        return core::BodyProfile::Round;
    }
    return core::BodyProfile::Round;
}

auto placement_index(core::PlacementMode mode) -> std::size_t
{
    return mode == core::PlacementMode::Fill ? 1U : 0U;
}

auto next_placement(core::PlacementMode mode)
    -> core::PlacementMode
{
    return mode == core::PlacementMode::Fit
               ? core::PlacementMode::Fill
               : core::PlacementMode::Fit;
}

auto alpha_index(core::AlphaMode mode) -> std::size_t
{
    return mode == core::AlphaMode::Background ? 1U : 0U;
}

auto next_alpha(core::AlphaMode mode) -> core::AlphaMode
{
    return mode == core::AlphaMode::Skip
               ? core::AlphaMode::Background
               : core::AlphaMode::Skip;
}

auto face_index(core::FaceBaseMode mode) -> std::size_t
{
    return mode == core::FaceBaseMode::Skip ? 1U : 0U;
}

auto next_face(core::FaceBaseMode mode) -> core::FaceBaseMode
{
    return mode == core::FaceBaseMode::Fill
               ? core::FaceBaseMode::Skip
               : core::FaceBaseMode::Fill;
}
} // namespace

auto compose_image_settings_section(
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
        state.section_scroll[ImageScrollIndex],
        ui::ScrollContainerInput{
            viewport,
            layout.content,
            row_height * static_cast<double>(ImageRowCount),
            row_height,
            input.pointer,
        });
    if (!scroll)
    {
        return std::unexpected(
            ProductPanelError{scroll.error()});
    }
    state.section_scroll[ImageScrollIndex] = scroll->state;

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
        return model.image_paint.project.edit &&
               model.image_paint.document.has_value() &&
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
            labels.image_setting_labels[row] + "  " +
            number_text(current, precision);
        if (const auto drawn = label(row, text); !drawn)
        {
            return drawn;
        }
        const auto response = widgets.slider(
            ImageControlIds[row],
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
            auto settings = model.image_paint.settings;
            assign(settings, response->value);
            return publish_settings(
                std::move(settings),
                model,
                action);
        }
        return {};
    };
    const auto choice = [&](
                            std::size_t row,
                            std::string_view value,
                            auto&& assign)
        -> std::expected<void, ProductPanelError>
    {
        if (const auto drawn =
                label(row, labels.image_setting_labels[row]);
            !drawn)
        {
            return drawn;
        }
        const auto response = widgets.button(
            ImageControlIds[row],
            control(row),
            viewport,
            value,
            enabled(row),
            false);
        if (!response)
        {
            return std::unexpected(
                ProductPanelError{response.error()});
        }
        if (response->activated)
        {
            auto settings = model.image_paint.settings;
            assign(settings);
            return publish_settings(
                std::move(settings),
                model,
                action);
        }
        return {};
    };

    if (const auto result = choice(
            0U,
            labels.body_profile_labels[
                body_index(model.image_paint.settings.body)],
            [&](core::ImageProjectSettings& settings)
            {
                settings.body =
                    next_body(model.image_paint.settings.body);
            });
        !result)
    {
        return result;
    }
    if (const auto result = choice(
            1U,
            labels.placement_mode_labels[
                placement_index(
                    model.image_paint.settings.placement)],
            [&](core::ImageProjectSettings& settings)
            {
                settings.placement = next_placement(
                    model.image_paint.settings.placement);
            });
        !result)
    {
        return result;
    }
    if (const auto result = choice(
            2U,
            labels.alpha_mode_labels[
                alpha_index(model.image_paint.settings.alpha)],
            [&](core::ImageProjectSettings& settings)
            {
                settings.alpha =
                    next_alpha(model.image_paint.settings.alpha);
            });
        !result)
    {
        return result;
    }
    const auto face = [&](
                          std::size_t row,
                          core::FaceBaseMode current,
                          auto&& assign)
        -> std::expected<void, ProductPanelError>
    {
        return choice(
            row,
            labels.face_mode_labels[face_index(current)],
            [current, &assign](
                core::ImageProjectSettings& settings)
            {
                assign(settings, next_face(current));
            });
    };
    if (const auto result = face(
            3U,
            model.image_paint.settings.front,
            [](core::ImageProjectSettings& settings,
               core::FaceBaseMode value)
            {
                settings.front = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = face(
            4U,
            model.image_paint.settings.right,
            [](core::ImageProjectSettings& settings,
               core::FaceBaseMode value)
            {
                settings.right = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = face(
            5U,
            model.image_paint.settings.back,
            [](core::ImageProjectSettings& settings,
               core::FaceBaseMode value)
            {
                settings.back = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = face(
            6U,
            model.image_paint.settings.left,
            [](core::ImageProjectSettings& settings,
               core::FaceBaseMode value)
            {
                settings.left = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            7U,
            model.image_paint.settings.brush_size_texels,
            1.0,
            10.0,
            1U,
            [](core::ImageProjectSettings& settings, double value)
            {
                settings.brush_size_texels = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            8U,
            model.image_paint.settings
                .color_compression_tolerance_percent,
            0.0,
            10.0,
            1U,
            [](core::ImageProjectSettings& settings, double value)
            {
                settings.color_compression_tolerance_percent =
                    value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            9U,
            model.image_paint.settings.image_material.metallic,
            0.0,
            1.0,
            2U,
            [](core::ImageProjectSettings& settings, double value)
            {
                settings.image_material.metallic = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            10U,
            model.image_paint.settings.image_material.roughness,
            0.0,
            1.0,
            2U,
            [](core::ImageProjectSettings& settings, double value)
            {
                settings.image_material.roughness = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            11U,
            model.image_paint.settings.image_material.emissive,
            0.0,
            1.0,
            2U,
            [](core::ImageProjectSettings& settings, double value)
            {
                settings.image_material.emissive = value;
            });
        !result)
    {
        return result;
    }

    if (const auto drawn =
            label(12U, labels.image_setting_labels[12U]);
        !drawn)
    {
        return drawn;
    }
    const auto fill = model.image_paint.settings.fill_color;
    const auto fill_color = widgets.color_control(
        FillColorIds,
        control(12U),
        viewport,
        ui::CanvasColor{
            fill.red,
            fill.green,
            fill.blue,
            255U,
        },
        enabled(12U));
    if (!fill_color)
    {
        return std::unexpected(
            ProductPanelError{fill_color.error()});
    }
    if (fill_color->changed)
    {
        auto settings = model.image_paint.settings;
        settings.fill_color = {
            fill_color->value.red,
            fill_color->value.green,
            fill_color->value.blue,
        };
        if (const auto published = publish_settings(
                std::move(settings),
                model,
                action);
            !published)
        {
            return published;
        }
    }

    if (const auto result = slider(
            13U,
            model.image_paint.settings.fill_material.metallic,
            0.0,
            1.0,
            2U,
            [](core::ImageProjectSettings& settings, double value)
            {
                settings.fill_material.metallic = value;
            });
        !result)
    {
        return result;
    }
    if (const auto result = slider(
            14U,
            model.image_paint.settings.fill_material.roughness,
            0.0,
            1.0,
            2U,
            [](core::ImageProjectSettings& settings, double value)
            {
                settings.fill_material.roughness = value;
            });
        !result)
    {
        return result;
    }
    return slider(
        15U,
        model.image_paint.settings.fill_material.emissive,
        0.0,
        1.0,
        2U,
        [](core::ImageProjectSettings& settings, double value)
        {
            settings.fill_material.emissive = value;
        });
}
} // namespace meccha::product_ui::detail
