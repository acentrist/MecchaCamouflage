#include "product_panel_settings.hpp"

#include <meccha/core/config.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>

namespace meccha::product_ui::detail
{
namespace
{
constexpr auto SettingsScrollIndex = std::size_t{3U};
constexpr auto SettingsRowCount = std::size_t{12U};
constexpr auto LanguageId = ui::WidgetId{401U};
constexpr auto ScaleId = ui::WidgetId{402U};
constexpr auto ThemeIds = std::array{
    ui::WidgetId{403U},
    ui::WidgetId{404U},
    ui::WidgetId{405U},
};
constexpr auto HotkeyIds = std::array{
    ui::WidgetId{411U},
    ui::WidgetId{412U},
    ui::WidgetId{413U},
    ui::WidgetId{414U},
    ui::WidgetId{415U},
    ui::WidgetId{416U},
    ui::WidgetId{417U},
    ui::WidgetId{418U},
    ui::WidgetId{419U},
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

auto config_hotkeys(const core::HotkeySettings& hotkeys)
    -> std::array<core::FunctionKey, 9U>
{
    return {
        hotkeys.toggle_ui,
        hotkeys.paint_start,
        hotkeys.paint_preview,
        hotkeys.paint_restore,
        hotkeys.paint_cancel,
        hotkeys.image_start,
        hotkeys.image_preview,
        hotkeys.image_restore,
        hotkeys.image_cancel,
    };
}

auto set_hotkey(
    core::HotkeySettings& hotkeys,
    std::size_t index,
    core::FunctionKey key) -> void
{
    switch (index)
    {
    case 0U:
        hotkeys.toggle_ui = key;
        break;
    case 1U:
        hotkeys.paint_start = key;
        break;
    case 2U:
        hotkeys.paint_preview = key;
        break;
    case 3U:
        hotkeys.paint_restore = key;
        break;
    case 4U:
        hotkeys.paint_cancel = key;
        break;
    case 5U:
        hotkeys.image_start = key;
        break;
    case 6U:
        hotkeys.image_preview = key;
        break;
    case 7U:
        hotkeys.image_restore = key;
        break;
    case 8U:
        hotkeys.image_cancel = key;
        break;
    default:
        break;
    }
}

auto key_used_elsewhere(
    const core::HotkeySettings& hotkeys,
    std::size_t changed_index,
    core::FunctionKey candidate) -> bool
{
    const auto keys = config_hotkeys(hotkeys);
    for (auto index = std::size_t{};
         index < keys.size();
         ++index)
    {
        if (index != changed_index &&
            keys[index] == candidate)
        {
            return true;
        }
    }
    return false;
}

auto function_key_text(core::FunctionKey key) -> std::string
{
    return "F" +
           std::to_string(static_cast<unsigned>(key));
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
    ProductPanelState& state)
    -> std::expected<void, ProductPanelError>
{
    if (!core::validate(config).empty())
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidModel});
    }
    if (!state.edit_session)
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidState});
    }
    state.edit_session->draft = std::move(config);
    return {};
}

auto next_locale(std::string_view current) -> std::string
{
    const auto found = std::ranges::find(
        core::SupportedLocales,
        current);
    if (found == core::SupportedLocales.end() ||
        std::next(found) == core::SupportedLocales.end())
    {
        return std::string{core::SupportedLocales.front()};
    }
    return std::string{*std::next(found)};
}
} // namespace

auto compose_settings_section(
    ui::CanvasFrameBuilder& canvas,
    ui::InteractionFrame&,
    ui::WidgetPainter& widgets,
    const ui::PanelLayout& layout,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels,
    const ProductPanelInput& input,
    ProductPanelState& state,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    const auto row_height = 44.0 * layout.effective_scale;
    const auto scroll = ui::update_scroll_container(
        state.section_scroll[SettingsScrollIndex],
        ui::ScrollContainerInput{
            layout.content,
            layout.content,
            row_height * static_cast<double>(SettingsRowCount),
            row_height,
            input.pointer,
        });
    if (!scroll)
    {
        return std::unexpected(
            ProductPanelError{scroll.error()});
    }
    state.section_scroll[SettingsScrollIndex] = scroll->state;

    const auto label_width = layout.content.width * 0.42;
    const auto gap = 8.0 * layout.effective_scale;
    const auto control = [&](std::size_t row)
    {
        return ui::CanvasRect{
            layout.content.x + label_width + gap,
            scroll->content_origin_y +
                static_cast<double>(row) * row_height +
                3.0 * layout.effective_scale,
            layout.content.width - label_width - gap,
            row_height - 6.0 * layout.effective_scale,
        };
    };
    const auto label = [&](
                           std::size_t row,
                           std::string_view text)
        -> std::expected<void, ProductPanelError>
    {
        return add_clipped_text(
            canvas,
            layout.content,
            {
                layout.content.x,
                scroll->content_origin_y +
                    static_cast<double>(row) * row_height +
                    14.0 * layout.effective_scale,
            },
            text,
            0.82 * layout.effective_scale);
    };
    const auto enabled = [&](std::size_t row)
    {
        return model.settings.can_apply &&
               intersects(control(row), layout.content);
    };

    if (!model.settings.can_apply ||
        !input.function_key_input_available ||
        input.keyboard.cancel_pressed)
    {
        state.hotkey_capture = {};
    }
    else if (
        state.hotkey_capture.index &&
        input.function_key_pressed)
    {
        const auto index = *state.hotkey_capture.index;
        const auto candidate = *input.function_key_pressed;
        if (config_hotkeys(
                model.settings.config.ui.hotkeys)[index] ==
            candidate)
        {
            state.hotkey_capture = {};
        }
        else if (key_used_elsewhere(
                model.settings.config.ui.hotkeys,
                index,
                candidate))
        {
            state.hotkey_capture.rejected = candidate;
        }
        else
        {
            auto config = model.settings.config;
            set_hotkey(config.ui.hotkeys, index, candidate);
            if (const auto published = publish_settings(
                    std::move(config),
                    state);
                !published)
            {
                return published;
            }
            state.hotkey_capture = {};
        }
    }

    if (const auto text = label(0U, labels.language); !text)
    {
        return text;
    }
    const auto language = widgets.button(
        LanguageId,
        control(0U),
        layout.content,
        model.settings.config.ui.language,
        enabled(0U),
        false);
    if (!language)
    {
        return std::unexpected(
            ProductPanelError{language.error()});
    }
    if (language->activated)
    {
        auto config = model.settings.config;
        config.ui.language =
            next_locale(config.ui.language);
        if (const auto published = publish_settings(
                std::move(config),
                state);
            !published)
        {
            return published;
        }
    }

    const auto scale_text =
        "UI " +
        std::to_string(static_cast<unsigned>(
            std::lround(
                model.settings.config.ui.scale * 100.0))) +
        "%";
    if (const auto text = label(1U, scale_text); !text)
    {
        return text;
    }
    const auto scale = widgets.slider(
        ScaleId,
        control(1U),
        layout.content,
        model.settings.config.ui.scale,
        ui::MinimumUiScale,
        ui::MaximumUiScale,
        enabled(1U));
    if (!scale)
    {
        return std::unexpected(
            ProductPanelError{scale.error()});
    }
    if (scale->changed)
    {
        auto config = model.settings.config;
        config.ui.scale = scale->value;
        if (const auto published = publish_settings(
                std::move(config),
                state);
            !published)
        {
            return published;
        }
    }

    if (const auto text = label(2U, labels.theme_color); !text)
    {
        return text;
    }
    const auto theme = model.settings.config.ui.theme_color;
    const auto color = widgets.color_control(
        ThemeIds,
        control(2U),
        layout.content,
        ui::CanvasColor{
            theme.red,
            theme.green,
            theme.blue,
            255U,
        },
        enabled(2U));
    if (!color)
    {
        return std::unexpected(
            ProductPanelError{color.error()});
    }
    if (color->changed)
    {
        auto config = model.settings.config;
        config.ui.theme_color = {
            color->value.red,
            color->value.green,
            color->value.blue,
        };
        if (const auto published = publish_settings(
                std::move(config),
                state);
            !published)
        {
            return published;
        }
    }

    const auto hotkeys =
        config_hotkeys(model.settings.config.ui.hotkeys);
    for (auto index = std::size_t{};
         index < hotkeys.size();
         ++index)
    {
        const auto row = 3U + index;
        if (const auto text =
                label(row, labels.hotkey_labels[index]);
            !text)
        {
            return text;
        }
        auto button_text = function_key_text(hotkeys[index]);
        if (state.hotkey_capture.index ==
            std::optional<std::size_t>{index})
        {
            button_text = state.hotkey_capture.rejected
                              ? function_key_text(
                                    *state.hotkey_capture.rejected) +
                                    labels.hotkey_duplicate_suffix
                              : labels.hotkey_capture_prompt;
        }
        const auto button = widgets.button(
            HotkeyIds[index],
            control(row),
            layout.content,
            button_text,
            enabled(row),
            false);
        if (!button)
        {
            return std::unexpected(
                ProductPanelError{button.error()});
        }
        if (button->activated && !action)
        {
            if (state.hotkey_capture.index ==
                std::optional<std::size_t>{index})
            {
                state.hotkey_capture = {};
            }
            else
            {
                state.hotkey_capture = {
                    std::optional<std::size_t>{index},
                    std::nullopt,
                };
            }
        }
    }
    return {};
}
} // namespace meccha::product_ui::detail
