#include "product_panel_image_project.hpp"

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <expected>
#include <ranges>
#include <string_view>
#include <utility>

namespace meccha::product_ui::detail
{
namespace
{
constexpr auto ProjectNameId = ui::WidgetId{651U};
constexpr auto ProjectSaveId = ui::WidgetId{652U};
constexpr auto ProjectRemoveId = ui::WidgetId{653U};
constexpr auto ProjectLoadId = ui::WidgetId{654U};
constexpr auto ProjectImportId = ui::WidgetId{655U};
constexpr auto ProjectToolbarRowHeight = 38.0;
constexpr auto ProjectToolbarGap = 8.0;
constexpr auto ProjectControlGap = 6.0;
constexpr auto ProjectNameWidthFraction = 0.56;

auto intersects(
    const ui::CanvasRect& left,
    const ui::CanvasRect& right) -> bool
{
    return left.x < right.x + right.width &&
           left.x + left.width > right.x &&
           left.y < right.y + right.height &&
           left.y + left.height > right.y;
}

auto valid_project_name(std::string_view name) -> bool
{
    return !name.empty() &&
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
} // namespace

auto image_project_toolbar_inset(
    double effective_scale) -> double
{
    return (2.0 * ProjectToolbarRowHeight +
            ProjectControlGap + ProjectToolbarGap) *
           effective_scale;
}

auto compose_image_project_toolbar(
    ui::WidgetPainter& widgets,
    ui::CanvasRect viewport,
    double content_origin_y,
    double effective_scale,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels,
    const ProductPanelInput& input,
    ProductPanelState& state,
    std::optional<application::ProductUiActionEnvelope>& action,
    std::optional<application::ProductUiEffectEnvelope>& effect)
    -> std::expected<void, ProductPanelError>
{
    const auto picker_toolbar = ui::CanvasRect{
        viewport.x,
        content_origin_y,
        viewport.width,
        ProjectToolbarRowHeight * effective_scale,
    };
    const auto project_control_gap =
        ProjectControlGap * effective_scale;
    const auto picker_button_width =
        (picker_toolbar.width - project_control_gap) / 2.0;
    const auto load_rect = ui::CanvasRect{
        picker_toolbar.x,
        picker_toolbar.y,
        picker_button_width,
        picker_toolbar.height,
    };
    const auto import_rect = ui::CanvasRect{
        load_rect.x + load_rect.width + project_control_gap,
        picker_toolbar.y,
        picker_button_width,
        picker_toolbar.height,
    };
    const auto picker_toolbar_visible =
        intersects(picker_toolbar, viewport);
    const auto load = widgets.button(
        ProjectLoadId,
        load_rect,
        viewport,
        labels.image_project_load,
        picker_toolbar_visible &&
            model.image_paint.project.load,
        false);
    if (!load)
    {
        return std::unexpected(
            ProductPanelError{load.error()});
    }
    if (load->activated && !action && !effect)
    {
        effect = application::ProductUiEffectEnvelope{
            model.source_revision,
            application::UiPickImageProject{},
        };
    }

    const auto can_import =
        picker_toolbar_visible &&
        model.image_paint.project.edit &&
        model.image_paint.document &&
        !state.image_editor.awaiting_revision;
    const auto import_images = widgets.button(
        ProjectImportId,
        import_rect,
        viewport,
        labels.image_import,
        can_import,
        false);
    if (!import_images)
    {
        return std::unexpected(
            ProductPanelError{import_images.error()});
    }
    if (import_images->activated && !action && !effect)
    {
        const auto& document = *model.image_paint.document;
        effect = application::ProductUiEffectEnvelope{
            model.source_revision,
            application::UiPickImageFiles{
                document.project_id,
                document.revision,
            },
        };
    }

    if (!model.image_paint.document)
    {
        state.image_editor.project_delete_armed = false;
        return {};
    }

    const auto& document = *model.image_paint.document;
    const auto project_toolbar = ui::CanvasRect{
        viewport.x,
        content_origin_y + ProjectToolbarRowHeight *
                               effective_scale +
            project_control_gap,
        viewport.width,
        ProjectToolbarRowHeight * effective_scale,
    };
    const auto project_name_width =
        project_toolbar.width * ProjectNameWidthFraction;
    const auto project_button_width =
        (project_toolbar.width - project_name_width -
         2.0 * project_control_gap) /
        2.0;
    const auto project_name_rect = ui::CanvasRect{
        project_toolbar.x,
        project_toolbar.y,
        project_name_width,
        project_toolbar.height,
    };
    const auto project_save_rect = ui::CanvasRect{
        project_name_rect.x + project_name_rect.width +
            project_control_gap,
        project_toolbar.y,
        project_button_width,
        project_toolbar.height,
    };
    const auto project_remove_rect = ui::CanvasRect{
        project_save_rect.x + project_save_rect.width +
            project_control_gap,
        project_toolbar.y,
        project_button_width,
        project_toolbar.height,
    };
    const auto project_toolbar_visible =
        intersects(project_toolbar, viewport);
    const auto can_rename =
        project_toolbar_visible &&
        model.image_paint.project.rename &&
        !state.image_editor.awaiting_revision;
    const auto project_name = widgets.text_field(
        ProjectNameId,
        project_name_rect,
        viewport,
        std::move(state.image_editor.project_name),
        input.text_edit_events,
        can_rename,
        application::MaximumProductUiProjectNameBytes);
    if (!project_name)
    {
        return std::unexpected(
            ProductPanelError{project_name.error()});
    }
    state.image_editor.project_name = project_name->state;
    if (project_name->committed &&
        state.image_editor.project_name.value !=
            document.display_name)
    {
        if (can_rename &&
            valid_project_name(
                state.image_editor.project_name.value) &&
            !action)
        {
            action = application::ProductUiActionEnvelope{
                model.source_revision,
                application::UiRenameCurrentImageProject{
                    state.image_editor.project_name.value,
                },
            };
            state.image_editor.awaiting_revision = true;
        }
        else
        {
            state.image_editor.project_name.value =
                document.display_name;
            state.image_editor.project_name.cursor_byte =
                document.display_name.size();
        }
    }

    const auto save = widgets.button(
        ProjectSaveId,
        project_save_rect,
        viewport,
        labels.image_project_save,
        project_toolbar_visible &&
            model.image_paint.project.save &&
            !state.image_editor.awaiting_revision,
        false);
    if (!save)
    {
        return std::unexpected(
            ProductPanelError{save.error()});
    }
    if (save->activated && !action)
    {
        action = application::ProductUiActionEnvelope{
            model.source_revision,
            application::UiSaveCurrentImageProject{},
        };
        state.image_editor.project_delete_armed = false;
    }

    if (!model.image_paint.project.remove ||
        input.keyboard.cancel_pressed)
    {
        state.image_editor.project_delete_armed = false;
    }
    const auto remove_project = widgets.button(
        ProjectRemoveId,
        project_remove_rect,
        viewport,
        labels.image_remove,
        project_toolbar_visible &&
            model.image_paint.project.remove &&
            !state.image_editor.awaiting_revision,
        state.image_editor.project_delete_armed);
    if (!remove_project)
    {
        return std::unexpected(
            ProductPanelError{remove_project.error()});
    }
    if (remove_project->activated && !action)
    {
        if (state.image_editor.project_delete_armed)
        {
            action = application::ProductUiActionEnvelope{
                model.source_revision,
                application::UiDeleteCurrentImageProject{},
            };
            state.image_editor.project_delete_armed = false;
        }
        else
        {
            state.image_editor.project_delete_armed = true;
        }
    }
    return {};
}
} // namespace meccha::product_ui::detail
