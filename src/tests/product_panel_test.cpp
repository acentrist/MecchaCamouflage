#include <meccha/product_ui/product_panel.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <variant>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL product_panel: " << message << '\n';
    }
    return condition;
}

auto read_file(const std::filesystem::path& path) -> std::string
{
    auto stream = std::ifstream{path, std::ios::binary};
    return {
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

auto center(const meccha::ui::CanvasRect& rect)
    -> meccha::ui::CanvasPoint
{
    return {
        rect.x + rect.width * 0.5,
        rect.y + rect.height * 0.5,
    };
}

auto ready_model() -> meccha::application::ProductUiModel
{
    using namespace meccha::application;

    auto model = ProductUiModel{};
    model.source_revision = 42U;
    model.ui_open = true;
    model.paint.actions = {true, true, false, false};
    model.image_paint.actions = {true, true, false, false};
    model.esp.enabled = true;
    model.esp.can_toggle = true;
    model.settings.config = meccha::core::ApplicationConfig{};
    model.settings.can_apply = true;
    model.diagnostics.command_queue = {2U, 8U, 0.25, true};
    model.diagnostics.runtime_queue = {3U, 12U, 0.25, true};
    model.progress = {4U, 10U, 0.4, 0.5, 1'200U, 800U};
    return model;
}

auto default_input() -> meccha::product_ui::ProductPanelInput
{
    return {
        meccha::ui::CanvasViewport{1920.0, 1080.0, 1.0},
        meccha::ui::CanvasInsets{},
        {},
        {},
    };
}
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;
    using namespace meccha::application;
    using namespace meccha::product_ui;

    auto passed = true;
    passed &= expect(argc == 2, "localization resource path is missing");
    if (argc != 2)
    {
        return 1;
    }
    const auto catalog = LocalizationCatalog::parse(read_file(argv[1]));
    passed &= expect(catalog.has_value(), "localization catalog did not parse");
    if (!catalog)
    {
        return 1;
    }

    const auto english =
        build_product_panel_labels(*catalog, "en");
    const auto japanese =
        build_product_panel_labels(*catalog, "ja");
    passed &= expect(
        english.section_labels[0] == "Paint" &&
            english.section_labels[1] == "Image Paint" &&
            english.section_labels[2] == "ESP" &&
            japanese.section_labels[0] == "ペイント" &&
            japanese.start == "開始",
        "panel labels did not use the selected localization catalog");

    auto model = ready_model();
    for (const auto locale : core::SupportedLocales)
    {
        model.settings.config.ui.language = locale;
        const auto localized = compose_product_panel(
            model,
            ProductPanelState{},
            default_input(),
            build_product_panel_labels(*catalog, locale));
        passed &= expect(
            localized && !localized->frame.primitives.empty(),
            "a shipped locale did not compose through the panel boundary");
    }
    model.settings.config.ui.language = "en";

    auto state = ProductPanelState{};
    const auto initial = compose_product_panel(
        model,
        state,
        default_input(),
        english);
    passed &= expect(
        initial && initial->layout &&
            initial->frame.primitives.size() > 5U &&
            initial->state.selected == ProductUiSection::Paint &&
            !initial->action,
        "open panel did not compose its initial Paint shell");
    if (!initial || !initial->layout)
    {
        return 1;
    }

    auto tab_input = default_input();
    tab_input.pointer = ui::PointerFrame{
        center(initial->layout->section_tabs[1]),
        true,
        false,
        true,
        0.0,
    };
    const auto image_tab = compose_product_panel(
        model,
        initial->state,
        tab_input,
        english);
    passed &= expect(
        image_tab &&
            image_tab->state.selected ==
                ProductUiSection::ImagePaint &&
            !image_tab->action,
        "section activation emitted a command or selected the wrong tab");

    auto start_input = default_input();
    start_input.pointer = ui::PointerFrame{
        {
            initial->layout->content.x + 10.0,
            initial->layout->content.y + 10.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto paint_start = compose_product_panel(
        model,
        initial->state,
        start_input,
        english);
    const auto* paint_envelope =
        paint_start && paint_start->action
            ? &*paint_start->action
            : nullptr;
    const auto* paint_action =
        paint_envelope
            ? std::get_if<UiPaintAction>(
                  &paint_envelope->action)
            : nullptr;
    passed &= expect(
        paint_action &&
            paint_envelope->expected_snapshot_revision == 42U &&
            paint_action->action == FeatureUiAction::Start,
        "enabled Paint Start did not emit one revision-bound action");

    model.paint.actions.start = false;
    const auto disabled_start = compose_product_panel(
        model,
        initial->state,
        start_input,
        english);
    passed &= expect(
        disabled_start && !disabled_start->action,
        "disabled Paint Start emitted an action");

    auto esp_state = initial->state;
    esp_state.selected = ProductUiSection::Esp;
    const auto esp_shell = compose_product_panel(
        model,
        esp_state,
        default_input(),
        english);
    if (!esp_shell || !esp_shell->layout)
    {
        return 1;
    }
    auto esp_input = default_input();
    esp_input.pointer = ui::PointerFrame{
        {
            esp_shell->layout->content.x + 10.0,
            esp_shell->layout->content.y + 10.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto esp_toggle = compose_product_panel(
        model,
        esp_shell->state,
        esp_input,
        english);
    passed &= expect(
        esp_toggle && esp_toggle->action &&
            std::holds_alternative<UiToggleEsp>(
                esp_toggle->action->action),
        "ESP toggle did not emit its typed action");

    auto settings_state = initial->state;
    settings_state.selected = ProductUiSection::Settings;
    const auto settings_shell = compose_product_panel(
        model,
        settings_state,
        default_input(),
        english);
    if (!settings_shell || !settings_shell->layout)
    {
        return 1;
    }
    constexpr auto SettingsRowHeight = 44.0;
    auto settings_input = default_input();
    settings_input.pointer = ui::PointerFrame{
        {
            settings_shell->layout->content.x +
                settings_shell->layout->content.width * 0.75,
            settings_shell->layout->content.y +
                SettingsRowHeight * 0.5,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto language = compose_product_panel(
        model,
        settings_shell->state,
        settings_input,
        english);
    const auto* language_action =
        language && language->action
            ? std::get_if<UiApplySettings>(
                  &language->action->action)
            : nullptr;
    passed &= expect(
        language_action &&
            language_action->settings.ui.language == "id" &&
            language_action->settings.paint ==
                model.settings.config.paint &&
            language_action->settings.active_image_project ==
                model.settings.config.active_image_project,
        "language control did not copy and change exactly one config field");

    settings_input.pointer.position.y += SettingsRowHeight;
    const auto scale = compose_product_panel(
        model,
        settings_shell->state,
        settings_input,
        english);
    const auto* scale_action =
        scale && scale->action
            ? std::get_if<UiApplySettings>(
                  &scale->action->action)
            : nullptr;
    passed &= expect(
        scale_action &&
            scale_action->settings.ui.scale >=
                ui::MinimumUiScale &&
            scale_action->settings.ui.scale <=
                ui::MaximumUiScale &&
            scale_action->settings.ui.scale !=
                model.settings.config.ui.scale,
        "scale control did not emit a bounded complete config");

    const auto theme_control_x =
        settings_shell->layout->content.x +
        settings_shell->layout->content.width * 0.42 +
        8.0;
    const auto theme_control_width =
        settings_shell->layout->content.width * 0.58 -
        8.0;
    const auto theme_channel_x =
        theme_control_x + 52.0;
    const auto theme_channel_width =
        theme_control_x + theme_control_width -
        theme_channel_x;
    settings_input.pointer.position = {
        theme_channel_x + theme_channel_width * 0.25,
        settings_shell->layout->content.y +
            2.0 * SettingsRowHeight + 8.0,
    };
    const auto theme = compose_product_panel(
        model,
        settings_shell->state,
        settings_input,
        english);
    const auto* theme_action =
        theme && theme->action
            ? std::get_if<UiApplySettings>(
                  &theme->action->action)
            : nullptr;
    passed &= expect(
        theme_action &&
            theme_action->settings.ui.theme_color.red <
                model.settings.config.ui.theme_color.red &&
            theme_action->settings.ui.theme_color.green ==
                model.settings.config.ui.theme_color.green &&
            theme_action->settings.ui.theme_color.blue ==
                model.settings.config.ui.theme_color.blue,
        "theme control did not isolate the selected RGB channel");

    settings_input.pointer.position = {
        settings_shell->layout->content.x +
            settings_shell->layout->content.width * 0.75,
        settings_shell->layout->content.y +
            3.0 * SettingsRowHeight +
            SettingsRowHeight * 0.5,
    };
    const auto hotkey = compose_product_panel(
        model,
        settings_shell->state,
        settings_input,
        english);
    const auto* hotkey_action =
        hotkey && hotkey->action
            ? std::get_if<UiApplySettings>(
                  &hotkey->action->action)
            : nullptr;
    passed &= expect(
        hotkey_action &&
            hotkey_action->settings.ui.hotkeys.toggle_ui ==
                core::FunctionKey::F10 &&
            hotkey_action->settings.ui.hotkeys.paint_start ==
                core::FunctionKey::F1 &&
            core::validate(hotkey_action->settings).empty(),
        "hotkey control introduced a duplicate or changed another mapping");

    model.settings.can_apply = false;
    const auto unavailable_settings = compose_product_panel(
        model,
        settings_shell->state,
        settings_input,
        english);
    passed &= expect(
        unavailable_settings && !unavailable_settings->action,
        "unavailable Settings controls emitted a config action");
    model.settings.can_apply = true;

    auto compact_input = ProductPanelInput{
        ui::CanvasViewport{640.0, 360.0, 1.0},
        ui::CanvasInsets{},
        {},
        {},
    };
    const auto compact = compose_product_panel(
        model,
        settings_state,
        compact_input,
        english);
    if (!compact || !compact->layout)
    {
        return 1;
    }
    compact_input.pointer = ui::PointerFrame{
        center(compact->layout->content),
        false,
        false,
        false,
        -3.0,
    };
    const auto scrolled = compose_product_panel(
        model,
        compact->state,
        compact_input,
        english);
    passed &= expect(
        scrolled &&
            scrolled->state.section_scroll[3U].offset_y > 0.0,
        "compact Settings content did not retain section-local scrolling");

    model.ui_open = false;
    const auto closed = compose_product_panel(
        model,
        ProductPanelState{
            ProductUiSection::Diagnostics,
            ui::InteractionState{
                ui::WidgetId{1U},
                ui::WidgetId{2U},
            },
        },
        default_input(),
        english);
    passed &= expect(
        closed && !closed->layout &&
            closed->frame.primitives.empty() &&
            closed->state.selected ==
                ProductUiSection::Diagnostics &&
            !closed->state.interaction.active &&
            !closed->state.interaction.focused &&
            !closed->action,
        "closed panel rendered or retained pointer/focus state");

    auto invalid_labels = english;
    invalid_labels.start.clear();
    model.ui_open = true;
    const auto invalid = compose_product_panel(
        model,
        ProductPanelState{},
        default_input(),
        invalid_labels);
    passed &= expect(
        !invalid &&
            std::holds_alternative<ProductPanelValidationError>(
                invalid.error()) &&
            std::get<ProductPanelValidationError>(
                invalid.error()) ==
                ProductPanelValidationError::InvalidLabels,
        "invalid localized labels entered the Canvas boundary");

    const auto invalid_state = compose_product_panel(
        model,
        ProductPanelState{
            static_cast<ProductUiSection>(255U),
            {},
        },
        default_input(),
        english);
    passed &= expect(
        !invalid_state &&
            std::holds_alternative<ProductPanelValidationError>(
                invalid_state.error()) &&
            std::get<ProductPanelValidationError>(
                invalid_state.error()) ==
                ProductPanelValidationError::InvalidState,
        "an unknown selected section entered the Canvas boundary");

    auto incoherent_model = model;
    incoherent_model.paint.settings.brush_size_texels = 6.0;
    const auto incoherent = compose_product_panel(
        incoherent_model,
        ProductPanelState{},
        default_input(),
        english);
    passed &= expect(
        !incoherent &&
            std::holds_alternative<ProductPanelValidationError>(
                incoherent.error()) &&
            std::get<ProductPanelValidationError>(
                incoherent.error()) ==
                ProductPanelValidationError::InvalidModel,
        "divergent presentation/config settings entered the panel");

    if (passed)
    {
        std::cout << "PASS product_panel\n";
    }
    return passed ? 0 : 1;
}
