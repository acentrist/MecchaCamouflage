#include <meccha/product_ui/product_panel.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
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
    model.image_paint.project.edit = true;
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
    if (!image_tab || !image_tab->layout)
    {
        return 1;
    }

    auto atlas_input = default_input();
    atlas_input.image_editor = ImageEditorFrameAssets{
        model.image_paint.document->project_id,
        model.image_paint.document->revision,
        ui::CanvasTextureHandle{700U},
        std::nullopt,
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
        image_tab->layout->content.y + 46.0,
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
        (atlas_rect.width - 3.0 * LayerToolbarGap) / 4.0;
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
        50.0 * image_tab->layout->effective_scale;
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

    if (passed)
    {
        std::cout << "PASS product_panel\n";
    }
    return passed ? 0 : 1;
}
