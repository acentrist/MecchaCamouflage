#include <meccha/product_ui/product_panel.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

auto frame_contains_text(
    const meccha::ui::CanvasFrame& frame,
    std::string_view needle) -> bool
{
    return std::ranges::any_of(
        frame.primitives,
        [needle](const meccha::ui::CanvasPrimitive& primitive)
        {
            const auto* text =
                std::get_if<meccha::ui::CanvasTextPrimitive>(
                    &primitive);
            return text &&
                   text->utf8.find(needle) != std::string::npos;
        });
}

auto ready_model() -> meccha::application::ProductUiModel
{
    using namespace meccha::application;

    auto model = ProductUiModel{};
    model.source_revision = 42U;
    model.ui_open = true;
    model.paint.actions = {true, true, false, false};
    model.image_paint.actions = {true, true, false, false};
    model.image_paint.document = ImageEditorDocumentSnapshot{
        "0123456789abcdef0123456789abcdef",
        "Current project",
        9U,
        meccha::core::ImageProjectSettings{},
        {
            meccha::core::ImageLayer{
                "source-image",
                "source.png",
                meccha::core::ImageMime::Png,
                128U,
                0.5,
                0.5,
                0.5,
                0.5,
                {},
                false,
                false,
            },
            meccha::core::ImageLayer{
                "overlay-image",
                "overlay.png",
                meccha::core::ImageMime::Png,
                64U,
                0.85,
                0.5,
                0.2,
                0.2,
                {},
                false,
                false,
            },
        },
    };
    model.image_paint.settings =
        model.image_paint.document->settings;
    model.image_paint.project = {
        true,
        true,
        true,
        true,
        true,
        false,
    };
    model.image_paint.pipeline = ImageEditorPipelineSnapshot{
        ImageEditorPipelinePhase::Ready,
        JobGeneration{3U},
        model.image_paint.document->project_id,
        model.image_paint.document->revision,
        false,
        std::nullopt,
    };
    model.esp.enabled = true;
    model.esp.can_toggle = true;
    model.settings.config = meccha::core::ApplicationConfig{};
    model.settings.can_apply = true;
    model.diagnostics.command_queue = {2U, 8U, 0.25, true};
    model.diagnostics.runtime_queue = {3U, 12U, 0.25, true};
    model.diagnostics.runtime_phase =
        ApplicationRuntimePhase::Compatible;
    model.diagnostics.compatibility.status =
        CompatibilityStatus::Compatible;
    model.diagnostics.entries = {
        DiagnosticEntry{
            40U,
            DiagnosticSeverity::Warning,
            "error.operation.failed",
            CommandId{17U},
            std::nullopt,
        },
        DiagnosticEntry{
            41U,
            DiagnosticSeverity::Error,
            "error.operation.failed",
            std::nullopt,
            CompatibilityFailure{
                RuntimeContractId::Canvas,
                ContractFailureKind::MissingFunction,
                "error.operation.failed",
            },
        },
    };
    model.diagnostics.omitted = 3U;
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
            english.image_project_load == "Load" &&
            english.image_import == "Upload" &&
            japanese.section_labels[0] == "ペイント" &&
            japanese.start == "開始" &&
            japanese.image_project_load == "読み込む" &&
            japanese.image_import == "アップロード",
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
    passed &= expect(
        frame_contains_text(
            initial->frame,
            "Progress 4 / 10 (40%)") &&
            frame_contains_text(
                initial->frame,
                "Elapsed 00:01.200") &&
            frame_contains_text(
                initial->frame,
                "ETA 00:00.800") &&
            frame_contains_text(
                initial->frame,
                "Queue 2 / 8"),
        "status strip omitted localized progress, timing, or queue state");

    const auto japanese_status = compose_product_panel(
        model,
        ProductPanelState{},
        default_input(),
        japanese);
    passed &= expect(
        japanese_status &&
            frame_contains_text(
                japanese_status->frame,
                "進捗 4 / 10 (40%)") &&
            frame_contains_text(
                japanese_status->frame,
                "経過 00:01.200") &&
            frame_contains_text(
                japanese_status->frame,
                "キュー 2 / 8"),
        "status strip labels did not follow the selected locale");

    auto unknown_eta_model = model;
    unknown_eta_model.progress.eta_ms.reset();
    const auto unknown_eta = compose_product_panel(
        unknown_eta_model,
        ProductPanelState{},
        default_input(),
        english);
    passed &= expect(
        unknown_eta &&
            frame_contains_text(
                unknown_eta->frame,
                "ETA —"),
        "status strip invented an unavailable ETA");

    auto invalid_status_model = model;
    invalid_status_model.progress.fraction =
        std::numeric_limits<double>::quiet_NaN();
    const auto invalid_status = compose_product_panel(
        invalid_status_model,
        ProductPanelState{},
        default_input(),
        english);
    passed &= expect(
        !invalid_status &&
            std::holds_alternative<
                ProductPanelValidationError>(
                invalid_status.error()) &&
            std::get<ProductPanelValidationError>(
                invalid_status.error()) ==
                ProductPanelValidationError::InvalidModel,
        "an invalid progress value entered status formatting");

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
    if (!image_tab || !image_tab->layout)
    {
        return 1;
    }

    constexpr auto ProjectToolbarRowHeight = 38.0;
    constexpr auto ProjectControlGap = 6.0;
    const auto project_picker_toolbar = ui::CanvasRect{
        image_tab->layout->content.x,
        image_tab->layout->content.y + 46.0,
        image_tab->layout->content.width,
        ProjectToolbarRowHeight,
    };
    const auto project_picker_button_width =
        (project_picker_toolbar.width - ProjectControlGap) /
        2.0;
    const auto project_load_rect = ui::CanvasRect{
        project_picker_toolbar.x,
        project_picker_toolbar.y,
        project_picker_button_width,
        project_picker_toolbar.height,
    };
    const auto project_import_rect = ui::CanvasRect{
        project_load_rect.x + project_load_rect.width +
            ProjectControlGap,
        project_picker_toolbar.y,
        project_picker_button_width,
        project_picker_toolbar.height,
    };
    const auto project_toolbar = ui::CanvasRect{
        project_picker_toolbar.x,
        project_picker_toolbar.y + ProjectToolbarRowHeight +
            ProjectControlGap,
        project_picker_toolbar.width,
        ProjectToolbarRowHeight,
    };
    const auto project_name_width =
        project_toolbar.width * 0.56;
    const auto project_button_width =
        (project_toolbar.width - project_name_width -
         2.0 * ProjectControlGap) /
        2.0;
    const auto project_name_rect = ui::CanvasRect{
        project_toolbar.x,
        project_toolbar.y,
        project_name_width,
        project_toolbar.height,
    };
    const auto project_save_rect = ui::CanvasRect{
        project_name_rect.x + project_name_rect.width +
            ProjectControlGap,
        project_toolbar.y,
        project_button_width,
        project_toolbar.height,
    };
    const auto project_remove_rect = ui::CanvasRect{
        project_save_rect.x + project_save_rect.width +
            ProjectControlGap,
        project_toolbar.y,
        project_button_width,
        project_toolbar.height,
    };

    auto load_picker_input = default_input();
    load_picker_input.pointer = ui::PointerFrame{
        center(project_load_rect),
        true,
        false,
        true,
        0.0,
    };
    const auto load_picker = compose_product_panel(
        model,
        image_tab->state,
        load_picker_input,
        english);
    const auto* load_effect =
        load_picker && load_picker->effect
            ? std::get_if<UiPickImageProject>(
                  &load_picker->effect->request)
            : nullptr;
    passed &= expect(
        load_effect && !load_picker->action &&
            load_picker->effect->expected_snapshot_revision ==
                model.source_revision,
        "Image Paint Load did not emit one revision-bound picker effect");

    auto import_picker_input = default_input();
    import_picker_input.pointer = ui::PointerFrame{
        center(project_import_rect),
        true,
        false,
        true,
        0.0,
    };
    const auto import_picker = compose_product_panel(
        model,
        image_tab->state,
        import_picker_input,
        english);
    const auto* import_effect =
        import_picker && import_picker->effect
            ? std::get_if<UiPickImageFiles>(
                  &import_picker->effect->request)
            : nullptr;
    passed &= expect(
        import_effect && !import_picker->action &&
            import_effect->project_id ==
                model.image_paint.document->project_id &&
            import_effect->expected_project_revision ==
                model.image_paint.document->revision &&
            import_picker->effect->expected_snapshot_revision ==
                model.source_revision,
        "Image Paint Upload did not capture the active project identity");

    auto unavailable_picker_model = model;
    unavailable_picker_model.image_paint.project.load = false;
    unavailable_picker_model.image_paint.project.edit = false;
    const auto unavailable_load = compose_product_panel(
        unavailable_picker_model,
        image_tab->state,
        load_picker_input,
        english);
    const auto unavailable_import = compose_product_panel(
        unavailable_picker_model,
        image_tab->state,
        import_picker_input,
        english);
    passed &= expect(
        unavailable_load && !unavailable_load->effect &&
            unavailable_import && !unavailable_import->effect,
        "unavailable Image Paint picker controls emitted effects");

    auto no_project_model = model;
    no_project_model.image_paint.document.reset();
    no_project_model.image_paint.pipeline = {};
    no_project_model.image_paint.settings =
        no_project_model.settings.config.image_paint;
    no_project_model.image_paint.project = {
        false,
        true,
        false,
        false,
        false,
        false,
    };
    const auto no_project_load = compose_product_panel(
        no_project_model,
        image_tab->state,
        load_picker_input,
        english);
    const auto* no_project_load_effect =
        no_project_load && no_project_load->effect
            ? std::get_if<UiPickImageProject>(
                  &no_project_load->effect->request)
            : nullptr;
    const auto no_project_import = compose_product_panel(
        no_project_model,
        image_tab->state,
        import_picker_input,
        english);
    passed &= expect(
        no_project_load_effect && !no_project_load->action &&
            no_project_load->state.image_editor ==
                ImageEditorPanelState{} &&
            no_project_import && !no_project_import->effect,
        "Image Paint did not preserve Load while disabling Upload without a project");

    auto rename_begin_input = default_input();
    rename_begin_input.pointer = ui::PointerFrame{
        center(project_name_rect),
        true,
        false,
        true,
        0.0,
    };
    const auto rename_begin = compose_product_panel(
        model,
        image_tab->state,
        rename_begin_input,
        english);
    passed &= expect(
        rename_begin &&
            rename_begin->state.image_editor.project_name
                .editing &&
            rename_begin->state.image_editor.project_name.value ==
                "Current project" &&
            !rename_begin->action,
        "Image Paint project name did not enter local text editing");
    if (!rename_begin)
    {
        return 1;
    }

    auto rename_input = default_input();
    rename_input.text_edit_events.push_back(
        ui::TextEditEvent{
            ui::TextEditEventKind::MoveHome,
            {},
        });
    for (auto index = std::size_t{}; index < 15U; ++index)
    {
        rename_input.text_edit_events.push_back(
            ui::TextEditEvent{
                ui::TextEditEventKind::DeleteForward,
                {},
            });
    }
    rename_input.text_edit_events.push_back(
        ui::TextEditEvent{
            ui::TextEditEventKind::Insert,
            "Renamed project",
        });
    rename_input.text_edit_events.push_back(
        ui::TextEditEvent{
            ui::TextEditEventKind::Commit,
            {},
        });
    const auto rename = compose_product_panel(
        model,
        rename_begin->state,
        rename_input,
        english);
    const auto* rename_action =
        rename && rename->action
            ? std::get_if<UiRenameCurrentImageProject>(
                  &rename->action->action)
            : nullptr;
    passed &= expect(
        rename_action &&
            rename_action->new_name == "Renamed project" &&
            rename->action->expected_snapshot_revision == 42U &&
            !rename->state.image_editor.project_name.editing,
        "Image Paint project rename did not publish one revision-bound action");

    auto blank_rename_input = default_input();
    blank_rename_input.text_edit_events.push_back(
        ui::TextEditEvent{
            ui::TextEditEventKind::MoveHome,
            {},
        });
    for (auto index = std::size_t{}; index < 15U; ++index)
    {
        blank_rename_input.text_edit_events.push_back(
            ui::TextEditEvent{
                ui::TextEditEventKind::DeleteForward,
                {},
            });
    }
    blank_rename_input.text_edit_events.push_back(
        ui::TextEditEvent{
            ui::TextEditEventKind::Commit,
            {},
        });
    const auto blank_rename = compose_product_panel(
        model,
        rename_begin->state,
        blank_rename_input,
        english);
    passed &= expect(
        blank_rename && !blank_rename->action &&
            !blank_rename->state.image_editor.project_name
                 .editing &&
            blank_rename->state.image_editor.project_name.value ==
                "Current project",
        "Image Paint project rename admitted an empty name");

    auto save_input = default_input();
    save_input.pointer = ui::PointerFrame{
        center(project_save_rect),
        true,
        false,
        true,
        0.0,
    };
    const auto save = compose_product_panel(
        model,
        image_tab->state,
        save_input,
        english);
    passed &= expect(
        save && save->action &&
            std::holds_alternative<UiSaveCurrentImageProject>(
                save->action->action) &&
            save->action->expected_snapshot_revision == 42U,
        "Image Paint project Save did not publish one typed action");

    auto remove_input = default_input();
    remove_input.pointer = ui::PointerFrame{
        center(project_remove_rect),
        true,
        false,
        true,
        0.0,
    };
    const auto remove_armed = compose_product_panel(
        model,
        image_tab->state,
        remove_input,
        english);
    passed &= expect(
        remove_armed &&
            remove_armed->state.image_editor
                .project_delete_armed &&
            !remove_armed->action,
        "Image Paint project removal did not require confirmation");
    if (!remove_armed)
    {
        return 1;
    }

    auto remove_cancel_input = default_input();
    remove_cancel_input.keyboard.cancel_pressed = true;
    const auto remove_cancelled = compose_product_panel(
        model,
        remove_armed->state,
        remove_cancel_input,
        english);
    passed &= expect(
        remove_cancelled &&
            !remove_cancelled->state.image_editor
                 .project_delete_armed &&
            !remove_cancelled->action,
        "Image Paint project removal confirmation ignored cancel");

    const auto remove_confirmed = compose_product_panel(
        model,
        remove_armed->state,
        remove_input,
        english);
    passed &= expect(
        remove_confirmed && remove_confirmed->action &&
            std::holds_alternative<UiDeleteCurrentImageProject>(
                remove_confirmed->action->action) &&
            !remove_confirmed->state.image_editor
                 .project_delete_armed &&
            remove_confirmed->action
                    ->expected_snapshot_revision ==
                42U,
        "Image Paint project removal did not publish one confirmed action");

    auto unavailable_project = model;
    unavailable_project.image_paint.project.save = false;
    unavailable_project.image_paint.project.rename = false;
    unavailable_project.image_paint.project.remove = false;
    const auto unavailable_save = compose_product_panel(
        unavailable_project,
        image_tab->state,
        save_input,
        english);
    const auto unavailable_remove = compose_product_panel(
        unavailable_project,
        image_tab->state,
        remove_input,
        english);
    const auto unavailable_rename = compose_product_panel(
        unavailable_project,
        image_tab->state,
        rename_begin_input,
        english);
    passed &= expect(
        unavailable_save && !unavailable_save->action &&
            unavailable_remove && !unavailable_remove->action &&
            !unavailable_remove->state.image_editor
                 .project_delete_armed &&
            unavailable_rename && !unavailable_rename->action &&
            !unavailable_rename->state.image_editor
                 .project_name.editing,
        "unavailable Image Paint project controls emitted an action");

    auto invalid_project_name = model;
    invalid_project_name.image_paint.document->display_name.clear();
    const auto invalid_project_name_panel =
        compose_product_panel(
            invalid_project_name,
            image_tab->state,
            default_input(),
            english);
    passed &= expect(
        !invalid_project_name_panel &&
            std::holds_alternative<
                ProductPanelValidationError>(
                invalid_project_name_panel.error()) &&
            std::get<ProductPanelValidationError>(
                invalid_project_name_panel.error()) ==
                ProductPanelValidationError::InvalidModel,
        "an invalid project name entered the Canvas presentation");

    auto atlas_input = default_input();
    atlas_input.image_editor = ImageEditorFrameAssets{
        model.image_paint.document->project_id,
        model.image_paint.document->revision,
        ui::CanvasTextureHandle{700U},
        std::nullopt,
        {
            ImageSourceFrameAsset{
                "source-image",
                400U,
                300U,
                ui::CanvasTextureHandle{701U},
            },
            ImageSourceFrameAsset{
                "overlay-image",
                320U,
                240U,
                ui::CanvasTextureHandle{702U},
            },
        },
    };
    const auto atlas_frame = compose_product_panel(
        model,
        image_tab->state,
        atlas_input,
        english);
    const auto atlas_drawn =
        atlas_frame &&
        std::ranges::any_of(
            atlas_frame->frame.primitives,
            [](const ui::CanvasPrimitive& primitive)
            {
                const auto* texture =
                    std::get_if<ui::CanvasTexturePrimitive>(
                        &primitive);
                return texture &&
                       texture->texture ==
                           ui::CanvasTextureHandle{700U};
            });
    passed &= expect(
        atlas_drawn,
        "exact project/revision atlas assets were not composed");

    const auto atlas_width = std::min(
        image_tab->layout->content.width,
        600.0 * image_tab->layout->effective_scale);
    const auto atlas_rect = ui::CanvasRect{
        image_tab->layout->content.x +
            (image_tab->layout->content.width - atlas_width) *
                0.5,
        image_tab->layout->content.y + 136.0,
        atlas_width,
        atlas_width * 0.5,
    };
    auto atlas_select_input = atlas_input;
    atlas_select_input.pointer = ui::PointerFrame{
        center(atlas_rect),
        true,
        false,
        true,
        0.0,
    };
    const auto atlas_select = compose_product_panel(
        model,
        image_tab->state,
        atlas_select_input,
        english);
    passed &= expect(
        atlas_select &&
            atlas_select->state.image_editor.interaction
                    .selected_layer ==
                0U &&
            !atlas_select->action,
        "Image Paint atlas click did not select one layer");
    if (!atlas_select)
    {
        return 1;
    }

    constexpr auto LayerToolbarGap = 6.0;
    const auto layer_toolbar_y =
        atlas_rect.y + atlas_rect.height + 8.0;
    const auto layer_toolbar_width =
        (atlas_rect.width - 5.0 * LayerToolbarGap) / 6.0;
    auto wrap_input = atlas_input;
    wrap_input.pointer = ui::PointerFrame{
        {
            atlas_rect.x +
                2.0 *
                    (layer_toolbar_width + LayerToolbarGap) +
                layer_toolbar_width * 0.5,
            layer_toolbar_y + 17.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto wrap_output = compose_product_panel(
        model,
        atlas_select->state,
        wrap_input,
        english);
    const auto* wrap_ui_mutation =
        wrap_output && wrap_output->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &wrap_output->action->action)
            : nullptr;
    const auto* wrap_mutation =
        wrap_ui_mutation
            ? std::get_if<ReplaceImageLayerMutation>(
                  &wrap_ui_mutation->mutation)
            : nullptr;
    passed &= expect(
        wrap_mutation &&
            wrap_mutation->layer_index == 0U &&
            wrap_mutation->expected_asset_id ==
                "source-image" &&
            wrap_mutation->layer.wrap_atlas_seam &&
            !wrap_mutation->layer.mirror_front_back &&
            wrap_output->state.image_editor.awaiting_revision,
        "Image Paint Wrap did not emit one guarded layer mutation");

    auto mirror_input = wrap_input;
    mirror_input.pointer.position.x =
        atlas_rect.x +
        3.0 * (layer_toolbar_width + LayerToolbarGap) +
        layer_toolbar_width * 0.5;
    const auto mirror_output = compose_product_panel(
        model,
        atlas_select->state,
        mirror_input,
        english);
    const auto* mirror_ui_mutation =
        mirror_output && mirror_output->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &mirror_output->action->action)
            : nullptr;
    const auto* mirror_mutation =
        mirror_ui_mutation
            ? std::get_if<ReplaceImageLayerMutation>(
                  &mirror_ui_mutation->mutation)
            : nullptr;
    passed &= expect(
        mirror_mutation &&
            mirror_mutation->layer_index == 0U &&
            mirror_mutation->expected_asset_id ==
                "source-image" &&
            !mirror_mutation->layer.wrap_atlas_seam &&
            mirror_mutation->layer.mirror_front_back,
        "Image Paint Mirror did not isolate one guarded layer field");

    auto forward_input = wrap_input;
    forward_input.pointer.position.x =
        atlas_rect.x +
        layer_toolbar_width + LayerToolbarGap +
        layer_toolbar_width * 0.5;
    const auto forward_output = compose_product_panel(
        model,
        atlas_select->state,
        forward_input,
        english);
    const auto* forward_ui_mutation =
        forward_output && forward_output->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &forward_output->action->action)
            : nullptr;
    const auto* forward_mutation =
        forward_ui_mutation
            ? std::get_if<ReorderImageLayerMutation>(
                  &forward_ui_mutation->mutation)
            : nullptr;
    passed &= expect(
        forward_mutation &&
            forward_mutation->layer_index == 0U &&
            forward_mutation->destination_index == 1U &&
            forward_mutation->expected_asset_id ==
                "source-image",
        "Image Paint forward ordering did not emit one guarded reorder");

    auto overlay_select_input = atlas_input;
    overlay_select_input.pointer = ui::PointerFrame{
        {
            atlas_rect.x + atlas_rect.width * 0.85,
            atlas_rect.y + atlas_rect.height * 0.5,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto overlay_select = compose_product_panel(
        model,
        image_tab->state,
        overlay_select_input,
        english);
    if (!overlay_select)
    {
        return 1;
    }
    auto back_input = wrap_input;
    back_input.pointer.position.x =
        atlas_rect.x + layer_toolbar_width * 0.5;
    const auto back_output = compose_product_panel(
        model,
        overlay_select->state,
        back_input,
        english);
    const auto* back_ui_mutation =
        back_output && back_output->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &back_output->action->action)
            : nullptr;
    const auto* back_mutation =
        back_ui_mutation
            ? std::get_if<ReorderImageLayerMutation>(
                  &back_ui_mutation->mutation)
            : nullptr;
    passed &= expect(
        overlay_select->state.image_editor.interaction
                    .selected_layer ==
                1U &&
            back_mutation &&
            back_mutation->layer_index == 1U &&
            back_mutation->destination_index == 0U &&
            back_mutation->expected_asset_id ==
                "overlay-image",
        "Image Paint backward ordering did not emit one guarded reorder");

    auto layer_remove_input = wrap_input;
    layer_remove_input.pointer.position.x =
        atlas_rect.x +
        5.0 * (layer_toolbar_width + LayerToolbarGap) +
        layer_toolbar_width * 0.5;
    const auto layer_remove = compose_product_panel(
        model,
        atlas_select->state,
        layer_remove_input,
        english);
    const auto* layer_remove_ui_mutation =
        layer_remove && layer_remove->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &layer_remove->action->action)
            : nullptr;
    const auto* layer_remove_mutation =
        layer_remove_ui_mutation
            ? std::get_if<RemoveImageLayerMutation>(
                  &layer_remove_ui_mutation->mutation)
            : nullptr;
    passed &= expect(
        layer_remove_mutation &&
            layer_remove_mutation->layer_index == 0U &&
            layer_remove_mutation->expected_asset_id ==
                "source-image" &&
            layer_remove->state.image_editor.awaiting_revision,
        "Image Paint Remove did not emit one guarded layer mutation");

    auto one_layer_model = model;
    one_layer_model.image_paint.document->layers.pop_back();
    auto one_layer_input = atlas_input;
    one_layer_input.image_editor->sources.pop_back();
    auto one_layer_select_input = one_layer_input;
    one_layer_select_input.pointer = atlas_select_input.pointer;
    const auto one_layer_select = compose_product_panel(
        one_layer_model,
        image_tab->state,
        one_layer_select_input,
        english);
    if (!one_layer_select)
    {
        return 1;
    }
    auto last_layer_remove_input = one_layer_input;
    last_layer_remove_input.pointer = layer_remove_input.pointer;
    const auto last_layer_remove = compose_product_panel(
        one_layer_model,
        one_layer_select->state,
        last_layer_remove_input,
        english);
    passed &= expect(
        last_layer_remove &&
            !last_layer_remove->action &&
            !last_layer_remove->state.image_editor
                 .awaiting_revision,
        "Image Paint Remove admitted deletion of the final layer");

    auto crop_input = wrap_input;
    crop_input.pointer.position.x =
        atlas_rect.x +
        4.0 * (layer_toolbar_width + LayerToolbarGap) +
        layer_toolbar_width * 0.5;
    const auto crop_output = compose_product_panel(
        model,
        atlas_select->state,
        crop_input,
        english);
    passed &= expect(
        crop_output &&
            crop_output->state.image_editor.crop &&
            crop_output->state.image_editor.crop->layer_index ==
                0U &&
            crop_output->state.image_editor.crop->asset_id ==
                "source-image" &&
            !crop_output->action,
        "Image Paint Crop did not open a source-bound local session");
    if (!crop_output || !crop_output->state.image_editor.crop)
    {
        return 1;
    }

    const auto crop_frame = compose_product_panel(
        model,
        crop_output->state,
        atlas_input,
        english);
    const auto crop_source_drawn =
        crop_frame &&
        std::ranges::any_of(
            crop_frame->frame.primitives,
            [](const ui::CanvasPrimitive& primitive)
            {
                const auto* texture =
                    std::get_if<ui::CanvasTexturePrimitive>(
                        &primitive);
                return texture &&
                       texture->texture.identity == 701U;
            });
    const auto crop_atlas_hidden =
        crop_frame &&
        std::ranges::none_of(
            crop_frame->frame.primitives,
            [](const ui::CanvasPrimitive& primitive)
            {
                const auto* texture =
                    std::get_if<ui::CanvasTexturePrimitive>(
                        &primitive);
                return texture &&
                       texture->texture.identity == 700U;
            });
    const auto crop_border_count =
        crop_frame
            ? std::ranges::count_if(
                  crop_frame->frame.primitives,
                  [](const ui::CanvasPrimitive& primitive)
                  {
                      const auto* line =
                          std::get_if<ui::CanvasLinePrimitive>(
                              &primitive);
                      return line && line->thickness == 3.0 &&
                             line->color ==
                                 ui::CanvasColor{
                                     255U,
                                     255U,
                                     255U,
                                     255U,
                                 };
                  })
            : 0;
    passed &= expect(
        crop_source_drawn && crop_atlas_hidden &&
            crop_border_count == 4,
        "Crop did not replace the atlas with its bounded source view");

    const auto crop_unit_width =
        (atlas_rect.width - 4.0 * LayerToolbarGap) / 5.0;
    const auto crop_slider = ui::CanvasRect{
        atlas_rect.x + crop_unit_width + LayerToolbarGap,
        layer_toolbar_y,
        2.0 * crop_unit_width + LayerToolbarGap,
        34.0,
    };
    auto crop_zoom_input = atlas_input;
    crop_zoom_input.pointer = ui::PointerFrame{
        {
            crop_slider.x + crop_slider.width * 0.75,
            crop_slider.y + crop_slider.height * 0.5,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto crop_zoom = compose_product_panel(
        model,
        crop_output->state,
        crop_zoom_input,
        english);
    passed &= expect(
        crop_zoom && crop_zoom->state.image_editor.crop &&
            crop_zoom->state.image_editor.crop->zoom > 3.0 &&
            !crop_zoom->action,
        "Crop zoom did not remain a local source-bound edit");
    if (!crop_zoom || !crop_zoom->state.image_editor.crop)
    {
        return 1;
    }

    const auto crop_source_height = atlas_rect.height;
    const auto crop_source_width =
        crop_source_height * 400.0 / 300.0;
    const auto crop_source_rect = ui::CanvasRect{
        atlas_rect.x +
            (atlas_rect.width - crop_source_width) * 0.5,
        atlas_rect.y,
        crop_source_width,
        crop_source_height,
    };
    auto crop_press_input = atlas_input;
    crop_press_input.pointer = ui::PointerFrame{
        {
            crop_source_rect.x + crop_source_rect.width * 0.7,
            crop_source_rect.y + crop_source_rect.height * 0.6,
        },
        true,
        true,
        false,
        0.0,
    };
    const auto crop_press = compose_product_panel(
        model,
        crop_zoom->state,
        crop_press_input,
        english);
    passed &= expect(
        crop_press && crop_press->state.image_editor.crop &&
            crop_press->state.image_editor.crop_dragging &&
            crop_press->state.image_editor.crop->draft.x >
                crop_zoom->state.image_editor.crop->draft.x &&
            !crop_press->action,
        "Crop source press did not retain a local draft");
    if (!crop_press || !crop_press->state.image_editor.crop)
    {
        return 1;
    }

    auto crop_move_input = atlas_input;
    crop_move_input.pointer = ui::PointerFrame{
        {
            crop_source_rect.x + crop_source_rect.width * 0.75,
            crop_source_rect.y + crop_source_rect.height * 0.65,
        },
        false,
        true,
        false,
        -3.0,
    };
    const auto crop_move = compose_product_panel(
        model,
        crop_press->state,
        crop_move_input,
        english);
    passed &= expect(
        crop_move && crop_move->state.image_editor.crop &&
            crop_move->state.image_editor.crop_dragging &&
            crop_move->state.image_editor.crop->draft.x >=
                crop_press->state.image_editor.crop->draft.x &&
            crop_move->state.section_scroll[1U].offset_y == 0.0 &&
            !crop_move->action,
        "Crop source drag did not remain local or froze scrolling");
    if (!crop_move || !crop_move->state.image_editor.crop)
    {
        return 1;
    }

    auto crop_release_input = atlas_input;
    crop_release_input.pointer = ui::PointerFrame{
        {
            crop_source_rect.x + crop_source_rect.width * 0.8,
            crop_source_rect.y + crop_source_rect.height * 0.7,
        },
        false,
        false,
        true,
        0.0,
    };
    const auto crop_release = compose_product_panel(
        model,
        crop_move->state,
        crop_release_input,
        english);
    passed &= expect(
        crop_release && crop_release->state.image_editor.crop &&
            !crop_release->state.image_editor.crop_dragging &&
            !crop_release->action,
        "Crop source release did not retain the local draft");
    if (!crop_release || !crop_release->state.image_editor.crop)
    {
        return 1;
    }

    const auto expected_crop =
        crop_release->state.image_editor.crop->draft;
    auto crop_apply_input = atlas_input;
    crop_apply_input.pointer = ui::PointerFrame{
        {
            atlas_rect.x +
                3.0 *
                    (crop_unit_width + LayerToolbarGap) +
                crop_unit_width * 0.5,
            layer_toolbar_y + 17.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto crop_apply = compose_product_panel(
        model,
        crop_release->state,
        crop_apply_input,
        english);
    const auto* crop_ui_mutation =
        crop_apply && crop_apply->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &crop_apply->action->action)
            : nullptr;
    const auto* crop_mutation =
        crop_ui_mutation
            ? std::get_if<ReplaceImageLayerMutation>(
                  &crop_ui_mutation->mutation)
            : nullptr;
    passed &= expect(
        crop_mutation &&
            crop_mutation->layer_index == 0U &&
            crop_mutation->expected_asset_id ==
                "source-image" &&
            crop_mutation->layer.crop == expected_crop &&
            crop_mutation->layer.center_x ==
                model.image_paint.document->layers[0U].center_x &&
            crop_apply->state.image_editor.awaiting_revision &&
            !crop_apply->state.image_editor.crop,
        "Crop Apply did not emit one isolated guarded layer mutation");

    auto crop_cancel_input = atlas_input;
    crop_cancel_input.pointer = ui::PointerFrame{
        {
            atlas_rect.x +
                4.0 *
                    (crop_unit_width + LayerToolbarGap) +
                crop_unit_width * 0.5,
            layer_toolbar_y + 17.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto crop_cancel_button = compose_product_panel(
        model,
        crop_output->state,
        crop_cancel_input,
        english);
    passed &= expect(
        crop_cancel_button &&
            !crop_cancel_button->state.image_editor.crop &&
            !crop_cancel_button->action,
        "Crop Cancel button did not discard the local session");

    auto crop_keyboard_input = atlas_input;
    crop_keyboard_input.keyboard.cancel_pressed = true;
    const auto crop_keyboard_cancel = compose_product_panel(
        model,
        crop_output->state,
        crop_keyboard_input,
        english);
    passed &= expect(
        crop_keyboard_cancel &&
            !crop_keyboard_cancel->state.image_editor.crop &&
            !crop_keyboard_cancel->action,
        "Crop keyboard cancel did not discard the local session");

    auto missing_crop_source_input = atlas_input;
    missing_crop_source_input.image_editor->sources.erase(
        missing_crop_source_input.image_editor->sources.begin());
    const auto missing_crop_source = compose_product_panel(
        model,
        crop_output->state,
        missing_crop_source_input,
        english);
    passed &= expect(
        missing_crop_source &&
            !missing_crop_source->state.image_editor.crop &&
            !missing_crop_source->action,
        "a missing Crop source did not fail closed to the atlas");
    auto missing_crop_button_input = missing_crop_source_input;
    missing_crop_button_input.pointer = crop_input.pointer;
    const auto missing_crop_button = compose_product_panel(
        model,
        atlas_select->state,
        missing_crop_button_input,
        english);
    passed &= expect(
        missing_crop_button &&
            !missing_crop_button->state.image_editor.crop &&
            !missing_crop_button->action,
        "Crop admitted a layer without a matching source texture");

    auto invalid_crop_source_input = atlas_input;
    invalid_crop_source_input.image_editor->sources[0U].width = 0U;
    const auto invalid_crop_source = compose_product_panel(
        model,
        atlas_select->state,
        invalid_crop_source_input,
        english);
    passed &= expect(
        !invalid_crop_source &&
            std::holds_alternative<ProductPanelValidationError>(
                invalid_crop_source.error()) &&
            std::get<ProductPanelValidationError>(
                invalid_crop_source.error()) ==
                ProductPanelValidationError::InvalidImageAssets,
        "an invalid Crop source entered the Canvas frame");

    auto atlas_press_input = atlas_input;
    atlas_press_input.pointer = ui::PointerFrame{
        center(atlas_rect),
        true,
        true,
        false,
        0.0,
    };
    const auto atlas_press = compose_product_panel(
        model,
        image_tab->state,
        atlas_press_input,
        english);
    passed &= expect(
        atlas_press &&
            atlas_press->state.image_editor.interaction.gesture &&
            atlas_press->state.image_editor.interaction
                    .selected_layer ==
                0U &&
            !atlas_press->action,
        "Image Paint atlas press did not retain a local gesture");
    if (!atlas_press)
    {
        return 1;
    }

    auto atlas_move_input = atlas_input;
    atlas_move_input.pointer = ui::PointerFrame{
        {
            center(atlas_rect).x + atlas_rect.width * 0.1,
            center(atlas_rect).y,
        },
        false,
        true,
        false,
        -3.0,
    };
    const auto atlas_move = compose_product_panel(
        model,
        atlas_press->state,
        atlas_move_input,
        english);
    passed &= expect(
        atlas_move && atlas_move->state.image_editor.draft &&
            atlas_move->state.image_editor.draft->layer_index ==
                0U &&
            atlas_move->state.image_editor.draft->layer.center_x >
                model.image_paint.document->layers[0U].center_x &&
            atlas_move->state.section_scroll[1U].offset_y == 0.0 &&
            !atlas_move->action,
        "Image Paint atlas move did not remain local or froze scrolling");
    if (!atlas_move)
    {
        return 1;
    }

    auto atlas_release_input = atlas_input;
    atlas_release_input.pointer = ui::PointerFrame{
        atlas_move_input.pointer.position,
        false,
        false,
        true,
        0.0,
    };
    const auto atlas_release = compose_product_panel(
        model,
        atlas_move->state,
        atlas_release_input,
        english);
    const auto* atlas_release_mutation =
        atlas_release && atlas_release->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &atlas_release->action->action)
            : nullptr;
    const auto* atlas_layer_mutation =
        atlas_release_mutation
            ? std::get_if<ReplaceImageLayerMutation>(
                  &atlas_release_mutation->mutation)
            : nullptr;
    passed &= expect(
        atlas_layer_mutation &&
            atlas_layer_mutation->layer_index == 0U &&
            atlas_layer_mutation->expected_asset_id ==
                "source-image" &&
            atlas_layer_mutation->layer.center_x >
                model.image_paint.document->layers[0U].center_x &&
            atlas_release->action->expected_snapshot_revision ==
                model.source_revision &&
            atlas_release->state.image_editor.awaiting_revision &&
            !atlas_release->state.image_editor.interaction.gesture,
        "Image Paint atlas release did not emit one guarded layer mutation");
    if (!atlas_release || !atlas_layer_mutation)
    {
        return 1;
    }

    const auto awaiting_atlas = compose_product_panel(
        model,
        atlas_release->state,
        atlas_press_input,
        english);
    passed &= expect(
        awaiting_atlas &&
            awaiting_atlas->state.image_editor.awaiting_revision &&
            !awaiting_atlas->state.image_editor.interaction.gesture &&
            !awaiting_atlas->action,
        "an atlas awaiting project revision admitted another gesture");

    auto revised_model = model;
    revised_model.source_revision += 1U;
    revised_model.image_paint.document->revision += 1U;
    revised_model.image_paint.document->layers[0U] =
        atlas_layer_mutation->layer;
    revised_model.image_paint.pipeline.project_revision += 1U;
    auto revised_atlas_input = atlas_input;
    revised_atlas_input.image_editor->project_revision += 1U;
    const auto revised_atlas = compose_product_panel(
        revised_model,
        atlas_release->state,
        revised_atlas_input,
        english);
    passed &= expect(
        revised_atlas &&
            revised_atlas->state.image_editor.project_revision ==
                revised_model.image_paint.document->revision &&
            !revised_atlas->state.image_editor.awaiting_revision &&
            !revised_atlas->state.image_editor.draft,
        "a published project revision did not retire the local atlas draft");

    auto atlas_cancel_input = atlas_input;
    atlas_cancel_input.keyboard.cancel_pressed = true;
    const auto atlas_cancel = compose_product_panel(
        model,
        atlas_move->state,
        atlas_cancel_input,
        english);
    passed &= expect(
        atlas_cancel &&
            !atlas_cancel->state.image_editor.draft &&
            !atlas_cancel->state.image_editor.interaction.gesture &&
            !atlas_cancel->action,
        "Image Paint atlas cancel did not discard the local draft");

    auto closed_atlas_model = model;
    closed_atlas_model.ui_open = false;
    const auto closed_atlas = compose_product_panel(
        closed_atlas_model,
        atlas_move->state,
        default_input(),
        english);
    passed &= expect(
        closed_atlas &&
            closed_atlas->state.image_editor ==
                ImageEditorPanelState{} &&
            !closed_atlas->action,
        "panel close retained an Image Paint local gesture/draft");

    auto stale_atlas_input = atlas_input;
    stale_atlas_input.image_editor->project_revision += 1U;
    const auto stale_atlas = compose_product_panel(
        model,
        image_tab->state,
        stale_atlas_input,
        english);
    passed &= expect(
        !stale_atlas &&
            std::holds_alternative<ProductPanelValidationError>(
                stale_atlas.error()) &&
            std::get<ProductPanelValidationError>(
                stale_atlas.error()) ==
                ProductPanelValidationError::InvalidImageAssets,
        "stale Image Paint atlas assets entered the Canvas frame");

    auto image_settings_input = default_input();
    const auto image_editor_inset =
        std::min(
            image_tab->layout->content.width,
            600.0 * image_tab->layout->effective_scale) *
            0.5 +
        140.0 * image_tab->layout->effective_scale;
    image_settings_input.pointer = ui::PointerFrame{
        {
            image_tab->layout->content.x +
                image_tab->layout->content.width * 0.75,
            image_tab->layout->content.y + 46.0 +
                image_editor_inset + 22.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto image_settings = compose_product_panel(
        model,
        image_tab->state,
        image_settings_input,
        english);
    const auto* image_mutation =
        image_settings && image_settings->action
            ? std::get_if<UiMutateCurrentImageProject>(
                  &image_settings->action->action)
            : nullptr;
    const auto* image_settings_mutation =
        image_mutation
            ? std::get_if<ReplaceImageProjectSettingsMutation>(
                  &image_mutation->mutation)
            : nullptr;
    passed &= expect(
        image_settings_mutation &&
            image_settings_mutation->settings.body ==
                core::BodyProfile::Cube &&
            image_settings_mutation->settings.placement ==
                model.image_paint.settings.placement &&
            image_settings->action->expected_snapshot_revision ==
                model.source_revision,
        "Image Paint body control did not emit one revision-bound project mutation");

    const auto invoke_image_control =
        [&](std::size_t row,
            double horizontal_fraction,
            double vertical_in_row = 22.0)
        -> std::optional<core::ImageProjectSettings>
    {
        constexpr auto RowHeight = 44.0;
        constexpr auto ActionInset = 46.0;
        constexpr auto RowCount = 16.0;
        const auto viewport_height =
            image_tab->layout->content.height - ActionInset;
        const auto maximum_offset =
            std::max(
                0.0,
                image_editor_inset + RowCount * RowHeight -
                    viewport_height);
        const auto offset = std::clamp(
            image_editor_inset +
                static_cast<double>(row) * RowHeight -
                viewport_height * 0.5,
            0.0,
            maximum_offset);
        auto next_state = image_tab->state;
        next_state.section_scroll[1U].offset_y = offset;

        const auto control_x =
            image_tab->layout->content.x +
            image_tab->layout->content.width * 0.42 + 8.0;
        const auto control_width =
            image_tab->layout->content.width * 0.58 - 8.0;
        auto next_input = default_input();
        next_input.pointer = ui::PointerFrame{
            {
                control_x +
                    control_width * horizontal_fraction,
                image_tab->layout->content.y + ActionInset +
                    image_editor_inset +
                    static_cast<double>(row) * RowHeight -
                    offset + vertical_in_row,
            },
            true,
            false,
            true,
            0.0,
        };
        const auto output = compose_product_panel(
            model,
            next_state,
            next_input,
            english);
        const auto* mutation =
            output && output->action &&
                    output->action->expected_snapshot_revision ==
                        model.source_revision
                ? std::get_if<UiMutateCurrentImageProject>(
                      &output->action->action)
                : nullptr;
        const auto* settings =
            mutation
                ? std::get_if<ReplaceImageProjectSettingsMutation>(
                      &mutation->mutation)
                : nullptr;
        return settings
                   ? std::optional{settings->settings}
                   : std::nullopt;
    };
    const auto image_changed_only =
        [&](const std::optional<core::ImageProjectSettings>& settings,
            auto&& changed,
            auto&& restore) -> bool
    {
        if (!settings || !core::validate(*settings).empty() ||
            !changed(*settings))
        {
            return false;
        }
        auto restored = *settings;
        restore(restored, model.image_paint.settings);
        return restored == model.image_paint.settings;
    };

    passed &= expect(
        image_changed_only(
            invoke_image_control(0U, 0.5),
            [](const core::ImageProjectSettings& settings)
            {
                return settings.body == core::BodyProfile::Cube;
            },
            [](core::ImageProjectSettings& changed,
               const core::ImageProjectSettings& original)
            {
                changed.body = original.body;
            }) &&
            image_changed_only(
                invoke_image_control(1U, 0.5),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.placement ==
                           core::PlacementMode::Fill;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.placement = original.placement;
                }) &&
            image_changed_only(
                invoke_image_control(2U, 0.5),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.alpha ==
                           core::AlphaMode::Background;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.alpha = original.alpha;
                }),
        "an Image Paint mapping control changed an invalid or unrelated field");

    const auto front_face = invoke_image_control(3U, 0.5);
    const auto right_face = invoke_image_control(4U, 0.5);
    const auto back_face = invoke_image_control(5U, 0.5);
    const auto left_face = invoke_image_control(6U, 0.5);
    passed &= expect(
        image_changed_only(
            front_face,
            [](const core::ImageProjectSettings& settings)
            {
                return settings.front ==
                       core::FaceBaseMode::Fill;
            },
            [](core::ImageProjectSettings& changed,
               const core::ImageProjectSettings& original)
            {
                changed.front = original.front;
            }) &&
            image_changed_only(
                right_face,
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.right ==
                           core::FaceBaseMode::Fill;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.right = original.right;
                }) &&
            image_changed_only(
                back_face,
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.back ==
                           core::FaceBaseMode::Fill;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.back = original.back;
                }) &&
            image_changed_only(
                left_face,
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.left ==
                           core::FaceBaseMode::Fill;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.left = original.left;
                }),
        "an Image Paint face control changed an invalid or unrelated field");

    passed &= expect(
        image_changed_only(
            invoke_image_control(7U, 0.8),
            [](const core::ImageProjectSettings& settings)
            {
                return settings.brush_size_texels > 5.0;
            },
            [](core::ImageProjectSettings& changed,
               const core::ImageProjectSettings& original)
            {
                changed.brush_size_texels =
                    original.brush_size_texels;
            }) &&
            image_changed_only(
                invoke_image_control(8U, 0.8),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings
                               .color_compression_tolerance_percent >
                           0.0;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.color_compression_tolerance_percent =
                        original
                            .color_compression_tolerance_percent;
                }),
        "an Image Paint brush/compression control changed an invalid or unrelated field");

    passed &= expect(
        image_changed_only(
            invoke_image_control(9U, 0.8),
            [](const core::ImageProjectSettings& settings)
            {
                return settings.image_material.metallic > 0.0;
            },
            [](core::ImageProjectSettings& changed,
               const core::ImageProjectSettings& original)
            {
                changed.image_material.metallic =
                    original.image_material.metallic;
            }) &&
            image_changed_only(
                invoke_image_control(10U, 0.2),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.image_material.roughness < 1.0;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.image_material.roughness =
                        original.image_material.roughness;
                }) &&
            image_changed_only(
                invoke_image_control(11U, 0.8),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.image_material.emissive > 0.0;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.image_material.emissive =
                        original.image_material.emissive;
                }),
        "an Image Paint material control changed an invalid or unrelated field");

    passed &= expect(
        image_changed_only(
            invoke_image_control(12U, 0.5, 8.0),
            [](const core::ImageProjectSettings& settings)
            {
                return settings.fill_color.red < 255U;
            },
            [](core::ImageProjectSettings& changed,
               const core::ImageProjectSettings& original)
            {
                changed.fill_color = original.fill_color;
            }) &&
            image_changed_only(
                invoke_image_control(13U, 0.2),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.fill_material.metallic < 1.0;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.fill_material.metallic =
                        original.fill_material.metallic;
                }) &&
            image_changed_only(
                invoke_image_control(14U, 0.8),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.fill_material.roughness > 0.0;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.fill_material.roughness =
                        original.fill_material.roughness;
                }) &&
            image_changed_only(
                invoke_image_control(15U, 0.8),
                [](const core::ImageProjectSettings& settings)
                {
                    return settings.fill_material.emissive > 0.0;
                },
                [](core::ImageProjectSettings& changed,
                   const core::ImageProjectSettings& original)
                {
                    changed.fill_material.emissive =
                        original.fill_material.emissive;
                }),
        "an Image Paint Fill control changed an invalid or unrelated field");

    model.image_paint.project.edit = false;
    const auto unavailable_image_settings = compose_product_panel(
        model,
        image_tab->state,
        image_settings_input,
        english);
    passed &= expect(
        unavailable_image_settings &&
            !unavailable_image_settings->action,
        "unavailable Image Paint settings emitted a mutation");
    const auto unavailable_atlas = compose_product_panel(
        model,
        image_tab->state,
        atlas_press_input,
        english);
    passed &= expect(
        unavailable_atlas &&
            !unavailable_atlas->state.image_editor.interaction.gesture &&
            !unavailable_atlas->action,
        "unavailable Image Paint editor admitted an atlas gesture");
    model.image_paint.project.edit = true;

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

    auto paint_settings_input = default_input();
    paint_settings_input.pointer = ui::PointerFrame{
        {
            initial->layout->content.x +
                initial->layout->content.width * 0.8,
            initial->layout->content.y + 68.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto paint_settings = compose_product_panel(
        model,
        initial->state,
        paint_settings_input,
        english);
    const auto* paint_settings_action =
        paint_settings && paint_settings->action
            ? std::get_if<UiApplySettings>(
                  &paint_settings->action->action)
            : nullptr;
    passed &= expect(
        paint_settings_action &&
            paint_settings_action->settings.paint.brush_size_texels !=
                model.settings.config.paint.brush_size_texels &&
            paint_settings_action->settings.image_paint ==
                model.settings.config.image_paint &&
            core::validate(paint_settings_action->settings).empty(),
        "Paint brush control did not emit a validated complete config");

    const auto invoke_paint_control =
        [&](std::size_t row,
            double horizontal_fraction,
            double vertical_in_row = 22.0)
        -> std::optional<core::ApplicationConfig>
    {
        constexpr auto RowHeight = 44.0;
        constexpr auto ActionInset = 46.0;
        constexpr auto RowCount = 16.0;
        const auto viewport_height =
            initial->layout->content.height - ActionInset;
        const auto maximum_offset =
            std::max(
                0.0,
                RowCount * RowHeight - viewport_height);
        const auto offset = std::clamp(
            static_cast<double>(row) * RowHeight -
                viewport_height * 0.5,
            0.0,
            maximum_offset);
        auto next_state = initial->state;
        next_state.section_scroll[0U].offset_y = offset;

        const auto control_x =
            initial->layout->content.x +
            initial->layout->content.width * 0.42 + 8.0;
        const auto control_width =
            initial->layout->content.width * 0.58 - 8.0;
        auto next_input = default_input();
        next_input.pointer = ui::PointerFrame{
            {
                control_x +
                    control_width * horizontal_fraction,
                initial->layout->content.y + ActionInset +
                    static_cast<double>(row) * RowHeight -
                    offset + vertical_in_row,
            },
            true,
            false,
            true,
            0.0,
        };
        const auto output = compose_product_panel(
            model,
            next_state,
            next_input,
            english);
        const auto* settings =
            output && output->action
                ? std::get_if<UiApplySettings>(
                      &output->action->action)
                : nullptr;
        return settings
                   ? std::optional{settings->settings}
                   : std::nullopt;
    };
    const auto changed_only =
        [&](const std::optional<core::ApplicationConfig>& config,
            auto&& changed,
            auto&& restore) -> bool
    {
        if (!config || !core::validate(*config).empty() ||
            !changed(config->paint))
        {
            return false;
        }
        auto restored = *config;
        restore(restored.paint, model.paint.settings);
        return restored == model.settings.config;
    };

    passed &= expect(
        changed_only(
            invoke_paint_control(1U, 0.8),
            [](const core::PaintSettings& settings)
            {
                return settings.side_source_max_uv != 0.08;
            },
            [](core::PaintSettings& changed,
               const core::PaintSettings& original)
            {
                changed.side_source_max_uv =
                    original.side_source_max_uv;
            }) &&
            changed_only(
                invoke_paint_control(2U, 0.8),
                [](const core::PaintSettings& settings)
                {
                    return settings.front_back_source_max_uv !=
                           0.45;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.front_back_source_max_uv =
                        original.front_back_source_max_uv;
                }),
        "Paint source-range controls changed an invalid or unrelated field");

    const auto front = invoke_paint_control(3U, 0.5);
    const auto side = invoke_paint_control(4U, 0.5);
    const auto back = invoke_paint_control(5U, 0.5);
    passed &= expect(
        changed_only(
            front,
            [](const core::PaintSettings& settings)
            {
                return settings.front_mode ==
                       core::RegionMode::Paint;
            },
            [](core::PaintSettings& changed,
               const core::PaintSettings& original)
            {
                changed.front_mode = original.front_mode;
            }) &&
            changed_only(
                side,
                [](const core::PaintSettings& settings)
                {
                    return settings.side_mode ==
                           core::RegionMode::Fill;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.side_mode = original.side_mode;
                }) &&
            changed_only(
                back,
                [](const core::PaintSettings& settings)
                {
                    return settings.back_mode ==
                           core::RegionMode::Fill;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.back_mode = original.back_mode;
                }),
        "Paint region controls did not cycle one routing mode");

    passed &= expect(
        changed_only(
            invoke_paint_control(6U, 0.5),
            [](const core::PaintSettings& settings)
            {
                return settings.auto_material;
            },
            [](core::PaintSettings& changed,
               const core::PaintSettings& original)
            {
                changed.auto_material = original.auto_material;
            }) &&
            changed_only(
                invoke_paint_control(7U, 0.5),
                [](const core::PaintSettings& settings)
                {
                    return settings.include_scene_lighting;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.include_scene_lighting =
                        original.include_scene_lighting;
                }),
        "Paint appearance toggles changed an invalid or unrelated field");

    const auto paint_metallic = invoke_paint_control(8U, 0.8);
    const auto paint_roughness = invoke_paint_control(9U, 0.8);
    const auto paint_emissive = invoke_paint_control(10U, 0.8);
    passed &= expect(
        changed_only(
            paint_metallic,
            [](const core::PaintSettings& settings)
            {
                return settings.paint_material.metallic > 0.0;
            },
            [](core::PaintSettings& changed,
               const core::PaintSettings& original)
            {
                changed.paint_material.metallic =
                    original.paint_material.metallic;
            }) &&
            changed_only(
                paint_roughness,
                [](const core::PaintSettings& settings)
                {
                    return settings.paint_material.roughness < 1.0;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.paint_material.roughness =
                        original.paint_material.roughness;
                }) &&
            changed_only(
                paint_emissive,
                [](const core::PaintSettings& settings)
                {
                    return settings.paint_material.emissive > 0.0;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.paint_material.emissive =
                        original.paint_material.emissive;
                }),
        "Paint material controls changed an invalid or unrelated field");

    const auto fill_color = invoke_paint_control(
        11U,
        0.5,
        8.0);
    passed &= expect(
        changed_only(
            fill_color,
            [](const core::PaintSettings& settings)
            {
                return settings.fill_color.red < 255U;
            },
            [](core::PaintSettings& changed,
               const core::PaintSettings& original)
            {
                changed.fill_color = original.fill_color;
            }),
        "Paint Fill color control changed an invalid or unrelated field");

    const auto fill_metallic = invoke_paint_control(12U, 0.8);
    const auto fill_roughness = invoke_paint_control(13U, 0.8);
    const auto fill_emissive = invoke_paint_control(14U, 0.8);
    const auto compression = invoke_paint_control(15U, 0.8);
    passed &= expect(
        changed_only(
            fill_metallic,
            [](const core::PaintSettings& settings)
            {
                return settings.fill_material.metallic < 1.0;
            },
            [](core::PaintSettings& changed,
               const core::PaintSettings& original)
            {
                changed.fill_material.metallic =
                    original.fill_material.metallic;
            }) &&
            changed_only(
                fill_roughness,
                [](const core::PaintSettings& settings)
                {
                    return settings.fill_material.roughness > 0.0;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.fill_material.roughness =
                        original.fill_material.roughness;
                }) &&
            changed_only(
                fill_emissive,
                [](const core::PaintSettings& settings)
                {
                    return settings.fill_material.emissive > 0.0;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.fill_material.emissive =
                        original.fill_material.emissive;
                }) &&
            changed_only(
                compression,
                [](const core::PaintSettings& settings)
                {
                    return settings
                               .color_compression_tolerance_percent >
                           5.0;
                },
                [](core::PaintSettings& changed,
                   const core::PaintSettings& original)
                {
                    changed.color_compression_tolerance_percent =
                        original
                            .color_compression_tolerance_percent;
                }),
        "Paint Fill material or compression control changed an invalid or unrelated field");

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

    auto esp_settings_input = default_input();
    esp_settings_input.pointer = ui::PointerFrame{
        {
            esp_shell->layout->content.x +
                esp_shell->layout->content.width * 0.75,
            esp_shell->layout->content.y + 68.0,
        },
        true,
        false,
        true,
        0.0,
    };
    const auto esp_scope = compose_product_panel(
        model,
        esp_shell->state,
        esp_settings_input,
        english);
    const auto* esp_scope_action =
        esp_scope && esp_scope->action
            ? std::get_if<UiApplySettings>(
                  &esp_scope->action->action)
            : nullptr;
    auto expected_esp_scope = model.settings.config;
    expected_esp_scope.esp.scope = core::EspScope::Hider;
    passed &= expect(
        esp_scope_action &&
            esp_scope_action->settings == expected_esp_scope &&
            core::validate(esp_scope_action->settings).empty(),
        "ESP scope control did not emit a validated complete config");

    const auto invoke_esp_control =
        [&](std::size_t row,
            double horizontal_fraction,
            double vertical_in_row = 22.0)
        -> std::optional<core::ApplicationConfig>
    {
        constexpr auto RowHeight = 44.0;
        constexpr auto ToggleInset = 46.0;
        constexpr auto RowCount = 8.0;
        const auto viewport_height =
            esp_shell->layout->content.height - ToggleInset;
        const auto maximum_offset =
            std::max(
                0.0,
                RowCount * RowHeight - viewport_height);
        const auto offset = std::clamp(
            static_cast<double>(row) * RowHeight -
                viewport_height * 0.5,
            0.0,
            maximum_offset);
        auto next_state = esp_shell->state;
        next_state.section_scroll[2U].offset_y = offset;

        const auto control_x =
            esp_shell->layout->content.x +
            esp_shell->layout->content.width * 0.42 + 8.0;
        const auto control_width =
            esp_shell->layout->content.width * 0.58 - 8.0;
        auto next_input = default_input();
        next_input.pointer = ui::PointerFrame{
            {
                control_x +
                    control_width * horizontal_fraction,
                esp_shell->layout->content.y + ToggleInset +
                    static_cast<double>(row) * RowHeight -
                    offset + vertical_in_row,
            },
            true,
            false,
            true,
            0.0,
        };
        const auto output = compose_product_panel(
            model,
            next_state,
            next_input,
            english);
        const auto* settings =
            output && output->action
                ? std::get_if<UiApplySettings>(
                      &output->action->action)
                : nullptr;
        return settings
                   ? std::optional{settings->settings}
                   : std::nullopt;
    };
    const auto esp_changed_only =
        [&](const std::optional<core::ApplicationConfig>& config,
            auto&& changed,
            auto&& restore) -> bool
    {
        if (!config || !core::validate(*config).empty() ||
            !changed(config->esp))
        {
            return false;
        }
        auto restored = *config;
        restore(restored.esp, model.esp.settings);
        return restored == model.settings.config;
    };

    passed &= expect(
        esp_changed_only(
            invoke_esp_control(1U, 0.5),
            [](const core::EspSettings& settings)
            {
                return !settings.boxes;
            },
            [](core::EspSettings& changed,
               const core::EspSettings& original)
            {
                changed.boxes = original.boxes;
            }) &&
            esp_changed_only(
                invoke_esp_control(2U, 0.5),
                [](const core::EspSettings& settings)
                {
                    return !settings.skeletons;
                },
                [](core::EspSettings& changed,
                   const core::EspSettings& original)
                {
                    changed.skeletons = original.skeletons;
                }) &&
            esp_changed_only(
                invoke_esp_control(3U, 0.5),
                [](const core::EspSettings& settings)
                {
                    return !settings.names;
                },
                [](core::EspSettings& changed,
                   const core::EspSettings& original)
                {
                    changed.names = original.names;
                }) &&
            esp_changed_only(
                invoke_esp_control(4U, 0.5),
                [](const core::EspSettings& settings)
                {
                    return !settings.distance;
                },
                [](core::EspSettings& changed,
                   const core::EspSettings& original)
                {
                    changed.distance = original.distance;
                }) &&
            esp_changed_only(
                invoke_esp_control(5U, 0.5),
                [](const core::EspSettings& settings)
                {
                    return !settings.snaplines;
                },
                [](core::EspSettings& changed,
                   const core::EspSettings& original)
                {
                    changed.snaplines = original.snaplines;
                }),
        "an ESP primitive toggle changed an invalid or unrelated field");

    passed &= expect(
        esp_changed_only(
            invoke_esp_control(6U, 0.5, 8.0),
            [](const core::EspSettings& settings)
            {
                return settings.hider_color.red > 0U;
            },
            [](core::EspSettings& changed,
               const core::EspSettings& original)
            {
                changed.hider_color = original.hider_color;
            }) &&
            esp_changed_only(
                invoke_esp_control(7U, 0.5, 8.0),
                [](const core::EspSettings& settings)
                {
                    return settings.hunter_color.red < 255U;
                },
                [](core::EspSettings& changed,
                   const core::EspSettings& original)
                {
                    changed.hunter_color = original.hunter_color;
                }),
        "an ESP role-color control changed an invalid or unrelated field");

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
    const auto hotkey_capture = compose_product_panel(
        model,
        settings_shell->state,
        settings_input,
        english);
    passed &= expect(
        hotkey_capture && !hotkey_capture->action &&
            hotkey_capture->state.hotkey_capture.index ==
                std::optional<std::size_t>{0U} &&
            !hotkey_capture->state.hotkey_capture.rejected,
        "hotkey control did not enter direct capture mode");
    const auto waiting_hotkey = compose_product_panel(
        model,
        hotkey_capture ? hotkey_capture->state
                       : settings_shell->state,
        default_input(),
        english);
    passed &= expect(
        waiting_hotkey && !waiting_hotkey->action &&
            frame_contains_text(
                waiting_hotkey->frame,
                "Press a function key"),
        "hotkey capture prompt was not rendered");

    auto captured_hotkey_input = default_input();
    captured_hotkey_input.function_key_pressed =
        core::FunctionKey::F12;
    const auto hotkey = compose_product_panel(
        model,
        waiting_hotkey ? waiting_hotkey->state
                       : hotkey_capture ? hotkey_capture->state
                       : settings_shell->state,
        captured_hotkey_input,
        english);
    const auto* hotkey_action =
        hotkey && hotkey->action
            ? std::get_if<UiApplySettings>(
                  &hotkey->action->action)
            : nullptr;
    passed &= expect(
        hotkey_action &&
            hotkey_action->settings.ui.hotkeys.toggle_ui ==
                core::FunctionKey::F12 &&
            hotkey_action->settings.ui.hotkeys.paint_start ==
                core::FunctionKey::F1 &&
            !hotkey->state.hotkey_capture.index &&
            !hotkey->state.hotkey_capture.rejected &&
            core::validate(hotkey_action->settings).empty(),
        "direct hotkey capture changed the wrong mapping");

    auto duplicate_input = default_input();
    duplicate_input.function_key_pressed =
        core::FunctionKey::F1;
    const auto duplicate_hotkey = compose_product_panel(
        model,
        waiting_hotkey ? waiting_hotkey->state
                       : hotkey_capture ? hotkey_capture->state
                       : settings_shell->state,
        duplicate_input,
        english);
    passed &= expect(
        duplicate_hotkey && !duplicate_hotkey->action &&
            duplicate_hotkey->state.hotkey_capture.index ==
                std::optional<std::size_t>{0U} &&
            duplicate_hotkey->state.hotkey_capture.rejected ==
                std::optional<core::FunctionKey>{
                    core::FunctionKey::F1} &&
            frame_contains_text(
                duplicate_hotkey->frame,
                "F1 is already assigned."),
        "duplicate hotkey capture did not fail closed in capture mode");

    auto unchanged_input = default_input();
    unchanged_input.function_key_pressed =
        core::FunctionKey::F9;
    const auto unchanged_hotkey = compose_product_panel(
        model,
        waiting_hotkey ? waiting_hotkey->state
                       : hotkey_capture ? hotkey_capture->state
                                        : settings_shell->state,
        unchanged_input,
        english);
    passed &= expect(
        unchanged_hotkey && !unchanged_hotkey->action &&
            !unchanged_hotkey->state.hotkey_capture.index &&
            !unchanged_hotkey->state.hotkey_capture.rejected,
        "unchanged hotkey capture published a redundant settings action");

    auto cancel_capture_input = default_input();
    cancel_capture_input.keyboard.cancel_pressed = true;
    const auto cancelled_hotkey = compose_product_panel(
        model,
        duplicate_hotkey ? duplicate_hotkey->state
                         : settings_shell->state,
        cancel_capture_input,
        english);
    passed &= expect(
        cancelled_hotkey && !cancelled_hotkey->action &&
            !cancelled_hotkey->state.hotkey_capture.index &&
            !cancelled_hotkey->state.hotkey_capture.rejected,
        "hotkey capture did not cancel without changing settings");

    auto lost_input = default_input();
    lost_input.function_key_input_available = false;
    const auto lost_hotkey = compose_product_panel(
        model,
        waiting_hotkey ? waiting_hotkey->state
                       : hotkey_capture ? hotkey_capture->state
                       : settings_shell->state,
        lost_input,
        english);
    passed &= expect(
        lost_hotkey && !lost_hotkey->action &&
            !lost_hotkey->state.hotkey_capture.index &&
            !lost_hotkey->state.hotkey_capture.rejected,
        "hotkey capture survived function-key input loss");

    auto invalid_hotkey_input = default_input();
    invalid_hotkey_input.function_key_pressed =
        static_cast<core::FunctionKey>(0U);
    const auto invalid_hotkey = compose_product_panel(
        model,
        waiting_hotkey ? waiting_hotkey->state
                       : hotkey_capture ? hotkey_capture->state
                       : settings_shell->state,
        invalid_hotkey_input,
        english);
    passed &= expect(
        !invalid_hotkey &&
            std::holds_alternative<ProductPanelValidationError>(
                invalid_hotkey.error()) &&
            std::get<ProductPanelValidationError>(
                invalid_hotkey.error()) ==
                ProductPanelValidationError::InvalidInput,
        "out-of-range captured function key was not rejected");

    model.settings.can_apply = false;
    const auto unavailable_settings = compose_product_panel(
        model,
        waiting_hotkey ? waiting_hotkey->state
                       : hotkey_capture ? hotkey_capture->state
                       : settings_shell->state,
        default_input(),
        english);
    passed &= expect(
        unavailable_settings && !unavailable_settings->action &&
            !unavailable_settings->state.hotkey_capture.index &&
            !unavailable_settings->state.hotkey_capture.rejected,
        "unavailable Settings controls retained hotkey capture");
    model.settings.can_apply = true;

    auto closed_model = model;
    closed_model.ui_open = false;
    const auto closed_hotkey = compose_product_panel(
        closed_model,
        waiting_hotkey ? waiting_hotkey->state
                       : hotkey_capture ? hotkey_capture->state
                       : settings_shell->state,
        default_input(),
        english);
    passed &= expect(
        closed_hotkey &&
            !closed_hotkey->state.hotkey_capture.index &&
            !closed_hotkey->state.hotkey_capture.rejected,
        "closed panel retained hotkey capture");

    auto departed_settings_state =
        waiting_hotkey ? waiting_hotkey->state
                       : settings_shell->state;
    departed_settings_state.selected =
        ProductUiSection::Paint;
    const auto departed_settings = compose_product_panel(
        model,
        departed_settings_state,
        default_input(),
        english);
    passed &= expect(
        departed_settings &&
            !departed_settings->state.hotkey_capture.index &&
            !departed_settings->state.hotkey_capture.rejected,
        "leaving Settings retained hotkey capture");

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

    auto compact_image_state = compact->state;
    compact_image_state.selected = ProductUiSection::ImagePaint;
    compact_input.pointer = ui::PointerFrame{
        center(compact->layout->content),
        false,
        false,
        false,
        -3.0,
    };
    const auto scrolled_image = compose_product_panel(
        model,
        compact_image_state,
        compact_input,
        english);
    passed &= expect(
        scrolled_image &&
            scrolled_image->state.section_scroll[1U].offset_y >
                0.0,
        "compact Image Paint settings did not retain section-local scrolling");

    auto diagnostics_input = default_input();
    diagnostics_input.pointer = ui::PointerFrame{
        center(initial->layout->section_tabs[4U]),
        true,
        false,
        true,
        0.0,
    };
    const auto diagnostics = compose_product_panel(
        model,
        initial->state,
        diagnostics_input,
        english);
    passed &= expect(
        diagnostics &&
            diagnostics->state.selected ==
                ProductUiSection::Diagnostics &&
            frame_contains_text(
                diagnostics->frame,
                "The operation failed") &&
            frame_contains_text(diagnostics->frame, "17") &&
            frame_contains_text(
                diagnostics->frame,
                "Canvas") &&
            frame_contains_text(
                diagnostics->frame,
                "MissingFunction") &&
            frame_contains_text(diagnostics->frame, "+3"),
        "Diagnostics did not render localized bounded runtime evidence");

    const auto japanese_diagnostics = compose_product_panel(
        model,
        ProductPanelState{ProductUiSection::Diagnostics},
        default_input(),
        japanese);
    passed &= expect(
        japanese_diagnostics &&
            frame_contains_text(
                japanese_diagnostics->frame,
                japanese.diagnostics_failure),
        "Diagnostics did not use the selected localization catalog");

    auto empty_diagnostics_model = model;
    empty_diagnostics_model.diagnostics.entries.clear();
    empty_diagnostics_model.diagnostics.omitted = 0U;
    const auto empty_diagnostics = compose_product_panel(
        empty_diagnostics_model,
        ProductPanelState{ProductUiSection::Diagnostics},
        default_input(),
        english);
    passed &= expect(
        empty_diagnostics &&
            frame_contains_text(
                empty_diagnostics->frame,
                english.diagnostics_empty) &&
            !empty_diagnostics->action,
        "an empty Diagnostics section did not render its bounded empty state");

    auto long_diagnostics_model = model;
    long_diagnostics_model.diagnostics.entries.clear();
    for (auto sequence = std::uint64_t{1U};
         sequence <= MaximumProductUiDiagnostics;
         ++sequence)
    {
        long_diagnostics_model.diagnostics.entries.push_back(
            DiagnosticEntry{
                sequence,
                DiagnosticSeverity::Information,
                "error.operation.failed",
                std::nullopt,
                std::nullopt,
            });
    }
    auto compact_diagnostics_input = default_input();
    compact_diagnostics_input.viewport = {
        640.0,
        360.0,
        1.0,
    };
    const auto compact_diagnostics = compose_product_panel(
        long_diagnostics_model,
        ProductPanelState{ProductUiSection::Diagnostics},
        compact_diagnostics_input,
        english);
    if (!compact_diagnostics || !compact_diagnostics->layout)
    {
        return 1;
    }
    compact_diagnostics_input.pointer = ui::PointerFrame{
        center(compact_diagnostics->layout->content),
        false,
        false,
        false,
        -3.0,
    };
    const auto scrolled_diagnostics = compose_product_panel(
        long_diagnostics_model,
        compact_diagnostics->state,
        compact_diagnostics_input,
        english);
    passed &= expect(
        scrolled_diagnostics &&
            scrolled_diagnostics->state.section_scroll[4U]
                    .offset_y > 0.0,
        "compact Diagnostics content did not retain bounded scrolling");

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

    auto incoherent_image_model = model;
    incoherent_image_model.image_paint.settings.body =
        core::BodyProfile::Cube;
    const auto incoherent_image = compose_product_panel(
        incoherent_image_model,
        ProductPanelState{},
        default_input(),
        english);
    passed &= expect(
        !incoherent_image &&
            std::holds_alternative<ProductPanelValidationError>(
                incoherent_image.error()) &&
            std::get<ProductPanelValidationError>(
                incoherent_image.error()) ==
                ProductPanelValidationError::InvalidModel,
        "divergent Image Paint document settings entered the panel");

    auto incoherent_diagnostics_model = model;
    incoherent_diagnostics_model.diagnostics.command_queue
        .utilization = 0.5;
    const auto incoherent_diagnostics = compose_product_panel(
        incoherent_diagnostics_model,
        ProductPanelState{ProductUiSection::Diagnostics},
        default_input(),
        english);
    passed &= expect(
        !incoherent_diagnostics &&
            std::holds_alternative<ProductPanelValidationError>(
                incoherent_diagnostics.error()) &&
            std::get<ProductPanelValidationError>(
                incoherent_diagnostics.error()) ==
                ProductPanelValidationError::InvalidModel,
        "incoherent Diagnostics queue state entered the panel");

    if (passed)
    {
        std::cout << "PASS product_panel\n";
    }
    return passed ? 0 : 1;
}
