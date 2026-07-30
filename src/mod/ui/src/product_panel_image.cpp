#include "product_panel_image.hpp"
#include "product_panel_image_project.hpp"

#include <meccha/core/config.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <iomanip>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
constexpr auto LayerToolbarIds = std::array{
    ui::WidgetId{631U},
    ui::WidgetId{632U},
    ui::WidgetId{633U},
    ui::WidgetId{634U},
    ui::WidgetId{635U},
    ui::WidgetId{636U},
};
constexpr auto CropZoomId = ui::WidgetId{641U};
constexpr auto CropApplyId = ui::WidgetId{642U};
constexpr auto CropCancelId = ui::WidgetId{643U};
constexpr auto TextColor =
    ui::CanvasColor{220U, 224U, 232U, 255U};
constexpr auto CropBorderColor =
    ui::CanvasColor{255U, 255U, 255U, 255U};

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

auto publish_layer_edit(
    const ui::ImageLayerEdit& edit,
    const application::ProductUiModel& model,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    if (!model.image_paint.document ||
        edit.layer_index >=
            model.image_paint.document->layers.size() ||
        !core::validate(edit.layer).empty())
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidModel});
    }
    const auto& original =
        model.image_paint.document->layers[edit.layer_index];
    if (edit.layer.asset_id != original.asset_id ||
        edit.layer == original)
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidModel});
    }
    if (!action)
    {
        action = application::ProductUiActionEnvelope{
            model.source_revision,
            application::UiMutateCurrentImageProject{
                application::ReplaceImageLayerMutation{
                    edit.layer_index,
                    original.asset_id,
                    edit.layer,
                },
            },
        };
    }
    return {};
}

auto publish_layer_reorder(
    std::size_t layer_index,
    std::size_t destination_index,
    const application::ProductUiModel& model,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    if (!model.image_paint.document ||
        layer_index >= model.image_paint.document->layers.size() ||
        destination_index >=
            model.image_paint.document->layers.size() ||
        layer_index == destination_index)
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidModel});
    }
    if (!action)
    {
        action = application::ProductUiActionEnvelope{
            model.source_revision,
            application::UiMutateCurrentImageProject{
                application::ReorderImageLayerMutation{
                    layer_index,
                    destination_index,
                    model.image_paint.document
                        ->layers[layer_index]
                        .asset_id,
                },
            },
        };
    }
    return {};
}

auto publish_layer_remove(
    std::size_t layer_index,
    std::string expected_asset_id,
    const application::ProductUiModel& model,
    std::optional<application::ProductUiActionEnvelope>& action)
    -> std::expected<void, ProductPanelError>
{
    if (expected_asset_id.empty() ||
        expected_asset_id.size() >
            application::MaximumProductUiAssetIdBytes)
    {
        return std::unexpected(ProductPanelError{
            ProductPanelValidationError::InvalidState});
    }
    if (!action)
    {
        action = application::ProductUiActionEnvelope{
            model.source_revision,
            application::UiMutateCurrentImageProject{
                application::RemoveImageLayerMutation{
                    layer_index,
                    std::move(expected_asset_id),
                },
            },
        };
    }
    return {};
}

auto contains(
    const ui::CanvasRect& rect,
    const ui::CanvasPoint& point) -> bool
{
    return point.x >= rect.x &&
           point.x < rect.x + rect.width &&
           point.y >= rect.y &&
           point.y < rect.y + rect.height;
}

auto source_asset(
    const ImageEditorFrameAssets& assets,
    std::string_view asset_id)
    -> const ImageSourceFrameAsset*
{
    const auto found = std::ranges::find_if(
        assets.sources,
        [asset_id](const ImageSourceFrameAsset& source)
        {
            return source.asset_id == asset_id;
        });
    return found == assets.sources.end()
               ? nullptr
               : &*found;
}

auto fitted_source_rect(
    const ui::CanvasRect& bounds,
    const ImageSourceFrameAsset& source) -> ui::CanvasRect
{
    const auto source_aspect =
        static_cast<double>(source.width) /
        static_cast<double>(source.height);
    const auto bounds_aspect = bounds.width / bounds.height;
    if (source_aspect >= bounds_aspect)
    {
        const auto height = bounds.width / source_aspect;
        return {
            bounds.x,
            bounds.y + (bounds.height - height) * 0.5,
            bounds.width,
            height,
        };
    }
    const auto width = bounds.height * source_aspect;
    return {
        bounds.x + (bounds.width - width) * 0.5,
        bounds.y,
        width,
        bounds.height,
    };
}

auto update_crop_center(
    ui::ImageCropSession session,
    const ui::CanvasRect& source_rect,
    ui::CanvasPoint pointer)
    -> std::expected<ui::ImageCropSession, ProductPanelError>
{
    const auto updated = ui::move_image_crop_center(
        std::move(session),
        (pointer.x - source_rect.x) / source_rect.width,
        (pointer.y - source_rect.y) / source_rect.height);
    if (!updated)
    {
        return std::unexpected(
            ProductPanelError{updated.error()});
    }
    return *updated;
}

auto draw_crop_source(
    ui::CanvasFrameBuilder& canvas,
    const ui::CanvasRect& viewport,
    const ui::CanvasRect& source_rect,
    const ImageSourceFrameAsset& source,
    const ui::ImageCropSession& crop)
    -> std::expected<void, ProductPanelError>
{
    const auto pushed = canvas.push_clip(viewport);
    if (!pushed)
    {
        return std::unexpected(
            ProductPanelError{pushed.error()});
    }
    const auto texture = canvas.add_texture(
        source.texture,
        source_rect,
        ui::CanvasUvRect{},
        ui::CanvasColor{255U, 255U, 255U, 255U});
    auto error = std::optional<ProductPanelError>{};
    if (!texture)
    {
        error = ProductPanelError{texture.error()};
    }

    const auto selection = ui::CanvasRect{
        source_rect.x + crop.draft.x * source_rect.width,
        source_rect.y + crop.draft.y * source_rect.height,
        crop.draft.width * source_rect.width,
        crop.draft.height * source_rect.height,
    };
    const auto right = selection.x + selection.width;
    const auto bottom = selection.y + selection.height;
    const auto edges = std::array{
        std::pair{
            ui::CanvasPoint{selection.x, selection.y},
            ui::CanvasPoint{right, selection.y},
        },
        std::pair{
            ui::CanvasPoint{right, selection.y},
            ui::CanvasPoint{right, bottom},
        },
        std::pair{
            ui::CanvasPoint{right, bottom},
            ui::CanvasPoint{selection.x, bottom},
        },
        std::pair{
            ui::CanvasPoint{selection.x, bottom},
            ui::CanvasPoint{selection.x, selection.y},
        },
    };
    if (!error)
    {
        for (const auto& [start, end] : edges)
        {
            const auto line = canvas.add_line(
                start,
                end,
                CropBorderColor,
                3.0);
            if (!line)
            {
                error = ProductPanelError{line.error()};
                break;
            }
        }
    }
    const auto popped = canvas.pop_clip();
    if (error)
    {
        return std::unexpected(*error);
    }
    if (!popped)
    {
        return std::unexpected(
            ProductPanelError{popped.error()});
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
    std::optional<application::ProductUiActionEnvelope>& action,
    std::optional<application::ProductUiEffectEnvelope>& effect)
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
    const auto toolbar_gap = 8.0 * layout.effective_scale;
    const auto toolbar_height = 34.0 * layout.effective_scale;
    const auto project_inset = image_project_toolbar_inset(
        layout.effective_scale);
    const auto atlas_width = model.image_paint.document
                                 ? std::min(
                                       viewport.width,
                                       600.0 *
                                           layout.effective_scale)
                                 : 0.0;
    const auto atlas_height = atlas_width * 0.5;
    const auto editor_inset = model.image_paint.document
                                  ? project_inset + atlas_height +
                                        toolbar_gap +
                                        toolbar_height +
                                        toolbar_gap
                                  : project_inset;
    auto scroll_pointer = input.pointer;
    if (state.image_editor.interaction.gesture ||
        state.image_editor.crop_dragging)
    {
        scroll_pointer.wheel_delta = 0.0;
    }
    const auto scroll = ui::update_scroll_container(
        state.section_scroll[ImageScrollIndex],
        ui::ScrollContainerInput{
            viewport,
            layout.content,
            editor_inset +
                row_height * static_cast<double>(ImageRowCount),
            row_height,
            scroll_pointer,
        });
    if (!scroll)
    {
        return std::unexpected(
            ProductPanelError{scroll.error()});
    }
    state.section_scroll[ImageScrollIndex] = scroll->state;

    const auto atlas_rect = ui::CanvasRect{
        viewport.x + (viewport.width - atlas_width) * 0.5,
        scroll->content_origin_y + project_inset,
        atlas_width,
        atlas_height,
    };
    const auto settings_origin_y =
        scroll->content_origin_y + editor_inset;
    if (model.image_paint.document)
    {
        const auto& document = *model.image_paint.document;
        if (state.image_editor.project_id !=
                document.project_id ||
            state.image_editor.project_revision !=
                document.revision)
        {
            state.image_editor = {};
            state.image_editor.project_id =
                document.project_id;
            state.image_editor.project_revision =
                document.revision;
            state.image_editor.project_name.value =
                document.display_name;
            state.image_editor.project_name.cursor_byte =
                document.display_name.size();
        }
        else if (
            !state.image_editor.project_name.editing &&
            !state.image_editor.awaiting_revision &&
            state.image_editor.project_name.value !=
                document.display_name)
        {
            state.image_editor.project_name.value =
                document.display_name;
            state.image_editor.project_name.original.clear();
            state.image_editor.project_name.cursor_byte =
                document.display_name.size();
        }
        if (state.image_editor.draft)
        {
            const auto& draft = *state.image_editor.draft;
            if (draft.layer_index >= document.layers.size() ||
                draft.layer.asset_id !=
                    document.layers[draft.layer_index].asset_id)
            {
                return std::unexpected(ProductPanelError{
                    ProductPanelValidationError::InvalidState});
            }
        }

        const ImageSourceFrameAsset* crop_source = nullptr;
        if (state.image_editor.crop)
        {
            const auto& crop = *state.image_editor.crop;
            if (crop.layer_index >= document.layers.size() ||
                crop.asset_id !=
                    document.layers[crop.layer_index].asset_id)
            {
                return std::unexpected(ProductPanelError{
                    ProductPanelValidationError::InvalidState});
            }
            const auto restored = ui::restore_image_crop(
                crop,
                document.layers[crop.layer_index]);
            if (!restored)
            {
                return std::unexpected(
                    ProductPanelError{restored.error()});
            }
            if (input.image_editor &&
                model.image_paint.project.edit)
            {
                crop_source = source_asset(
                    *input.image_editor,
                    crop.asset_id);
            }
            if (!crop_source)
            {
                state.image_editor.crop.reset();
                state.image_editor.crop_dragging = false;
            }
        }

        auto working_layers = document.layers;
        if (state.image_editor.draft)
        {
            working_layers[state.image_editor.draft->layer_index] =
                state.image_editor.draft->layer;
        }

        if (!input.image_editor ||
            !model.image_paint.project.edit)
        {
            state.image_editor.interaction.gesture.reset();
            state.image_editor.draft.reset();
            state.image_editor.crop.reset();
            state.image_editor.crop_dragging = false;
            crop_source = nullptr;
            working_layers = document.layers;
        }

        const auto project_controls =
            compose_image_project_toolbar(
                widgets,
                viewport,
                scroll->content_origin_y,
                layout.effective_scale,
                model,
                labels,
                input,
                state,
                action,
                effect);
        if (!project_controls)
        {
            return project_controls;
        }

        if (input.image_editor &&
            model.image_paint.project.edit &&
            !state.image_editor.awaiting_revision &&
            !state.image_editor.crop)
        {
            auto events =
                std::array<ui::ImageEditorPointerEvent, 2U>{};
            auto event_count = std::size_t{};
            const auto visible_pointer =
                contains(viewport, input.pointer.position) &&
                contains(atlas_rect, input.pointer.position);
            if (input.keyboard.cancel_pressed &&
                state.image_editor.interaction.gesture)
            {
                events[event_count++] = {
                    ui::ImageEditorPointerEventKind::Cancel,
                    input.pointer.position,
                };
            }
            else
            {
                if (input.pointer.primary_pressed &&
                    (visible_pointer ||
                     state.image_editor.interaction.gesture))
                {
                    events[event_count++] = {
                        ui::ImageEditorPointerEventKind::Press,
                        input.pointer.position,
                    };
                }
                if (input.pointer.primary_released &&
                    (visible_pointer ||
                     state.image_editor.interaction.gesture))
                {
                    events[event_count++] = {
                        ui::ImageEditorPointerEventKind::Release,
                        input.pointer.position,
                    };
                }
                else if (
                    input.pointer.primary_down &&
                    state.image_editor.interaction.gesture)
                {
                    events[event_count++] = {
                        ui::ImageEditorPointerEventKind::Move,
                        input.pointer.position,
                    };
                }
            }

            const auto update =
                ui::update_image_editor_interaction(
                    std::move(
                        state.image_editor.interaction),
                    working_layers,
                    atlas_rect,
                    std::span{events}.first(event_count));
            if (!update)
            {
                return std::unexpected(
                    ProductPanelError{update.error()});
            }
            state.image_editor.interaction =
                update->state;
            if (update->cancelled)
            {
                state.image_editor.draft.reset();
                working_layers = document.layers;
            }
            else if (update->edit && update->changed)
            {
                state.image_editor.draft = update->edit;
                working_layers[update->edit->layer_index] =
                    update->edit->layer;
            }
            if (update->committed &&
                state.image_editor.draft)
            {
                if (state.image_editor.draft->layer !=
                    document.layers[
                        state.image_editor.draft->layer_index])
                {
                    const auto published = publish_layer_edit(
                        *state.image_editor.draft,
                        model,
                        action);
                    if (!published)
                    {
                        return published;
                    }
                    state.image_editor.awaiting_revision = true;
                }
                else
                {
                    state.image_editor.draft.reset();
                }
            }
        }

        auto crop_source_rect = ui::CanvasRect{};
        if (state.image_editor.crop && crop_source)
        {
            crop_source_rect =
                fitted_source_rect(atlas_rect, *crop_source);
            if (input.keyboard.cancel_pressed)
            {
                state.image_editor.crop.reset();
                state.image_editor.crop_dragging = false;
                crop_source = nullptr;
            }
            else
            {
                const auto pointer_over_source =
                    contains(viewport, input.pointer.position) &&
                    contains(
                        crop_source_rect,
                        input.pointer.position);
                if (input.pointer.primary_pressed &&
                    pointer_over_source)
                {
                    state.image_editor.crop_dragging = true;
                    const auto updated = update_crop_center(
                        *state.image_editor.crop,
                        crop_source_rect,
                        input.pointer.position);
                    if (!updated)
                    {
                        return std::unexpected(updated.error());
                    }
                    state.image_editor.crop = *updated;
                }
                else if (
                    input.pointer.primary_down &&
                    state.image_editor.crop_dragging)
                {
                    const auto updated = update_crop_center(
                        *state.image_editor.crop,
                        crop_source_rect,
                        input.pointer.position);
                    if (!updated)
                    {
                        return std::unexpected(updated.error());
                    }
                    state.image_editor.crop = *updated;
                }
                if (input.pointer.primary_released &&
                    state.image_editor.crop_dragging)
                {
                    const auto updated = update_crop_center(
                        *state.image_editor.crop,
                        crop_source_rect,
                        input.pointer.position);
                    if (!updated)
                    {
                        return std::unexpected(updated.error());
                    }
                    state.image_editor.crop = *updated;
                    state.image_editor.crop_dragging = false;
                }
            }
        }

        if (state.image_editor.crop && crop_source)
        {
            const auto drawn = draw_crop_source(
                canvas,
                viewport,
                crop_source_rect,
                *crop_source,
                *state.image_editor.crop);
            if (!drawn)
            {
                return drawn;
            }
        }
        else if (input.image_editor)
        {
            const auto drawn = ui::draw_image_editor(
                canvas,
                atlas_rect,
                viewport,
                ui::ImageEditorView{
                    model.image_paint.settings.body,
                    input.image_editor->atlas_texture,
                    working_layers,
                    state.image_editor.interaction.selected_layer,
                    input.image_editor->guide,
                    model.image_paint.project.edit,
                    input.image_editor->guide
                        ? std::array<std::string_view, 4U>{
                              labels.image_setting_labels[3U],
                              labels.image_setting_labels[4U],
                              labels.image_setting_labels[5U],
                              labels.image_setting_labels[6U],
                          }
                        : std::array<std::string_view, 4U>{},
                });
            if (!drawn)
            {
                return std::unexpected(
                    ProductPanelError{drawn.error()});
            }
        }

        const auto toolbar = ui::CanvasRect{
            atlas_rect.x,
            atlas_rect.y + atlas_rect.height + toolbar_gap,
            atlas_rect.width,
            toolbar_height,
        };
        const auto button_gap = 6.0 * layout.effective_scale;
        const auto selected =
            state.image_editor.interaction.selected_layer;
        const auto toolbar_visible =
            intersects(toolbar, viewport);
        const auto toolbar_enabled =
            input.image_editor.has_value() &&
            model.image_paint.project.edit &&
            !state.image_editor.awaiting_revision &&
            selected &&
            *selected < document.layers.size() &&
            toolbar_visible;
        const ImageSourceFrameAsset* selected_source = nullptr;
        if (input.image_editor && selected &&
            *selected < document.layers.size())
        {
            selected_source = source_asset(
                *input.image_editor,
                document.layers[*selected].asset_id);
        }

        if (state.image_editor.crop && crop_source)
        {
            const auto unit_width =
                (toolbar.width - 4.0 * button_gap) / 5.0;
            const auto label_rect = ui::CanvasRect{
                toolbar.x,
                toolbar.y,
                unit_width,
                toolbar.height,
            };
            const auto label = labels.crop_zoom + "  " +
                               number_text(
                                   state.image_editor.crop->zoom,
                                   2U);
            const auto label_drawn = add_clipped_text(
                canvas,
                label_rect,
                {
                    label_rect.x,
                    label_rect.y +
                        10.0 * layout.effective_scale,
                },
                label,
                0.78 * layout.effective_scale);
            if (!label_drawn)
            {
                return label_drawn;
            }

            const auto crop_controls_enabled =
                toolbar_visible &&
                model.image_paint.project.edit &&
                !state.image_editor.awaiting_revision;
            const auto zoom = widgets.slider(
                CropZoomId,
                ui::CanvasRect{
                    toolbar.x + unit_width + button_gap,
                    toolbar.y,
                    2.0 * unit_width + button_gap,
                    toolbar.height,
                },
                viewport,
                state.image_editor.crop->zoom,
                1.0,
                4.0,
                crop_controls_enabled);
            if (!zoom)
            {
                return std::unexpected(
                    ProductPanelError{zoom.error()});
            }
            if (zoom->changed)
            {
                const auto updated = ui::set_image_crop_zoom(
                    *state.image_editor.crop,
                    zoom->value);
                if (!updated)
                {
                    return std::unexpected(
                        ProductPanelError{updated.error()});
                }
                state.image_editor.crop = *updated;
            }

            const auto apply_button = widgets.button(
                CropApplyId,
                ui::CanvasRect{
                    toolbar.x +
                        3.0 * (unit_width + button_gap),
                    toolbar.y,
                    unit_width,
                    toolbar.height,
                },
                viewport,
                labels.crop_apply,
                crop_controls_enabled,
                false);
            if (!apply_button)
            {
                return std::unexpected(
                    ProductPanelError{apply_button.error()});
            }
            if (apply_button->activated)
            {
                const auto layer_index =
                    state.image_editor.crop->layer_index;
                const auto edited = ui::apply_image_crop(
                    *state.image_editor.crop,
                    document.layers[layer_index]);
                if (!edited)
                {
                    return std::unexpected(
                        ProductPanelError{edited.error()});
                }
                if (*edited != document.layers[layer_index])
                {
                    const auto published = publish_layer_edit(
                        ui::ImageLayerEdit{
                            layer_index,
                            *edited,
                        },
                        model,
                        action);
                    if (!published)
                    {
                        return published;
                    }
                    state.image_editor.awaiting_revision = true;
                }
                state.image_editor.crop.reset();
                state.image_editor.crop_dragging = false;
                crop_source = nullptr;
            }

            const auto cancel_button = widgets.button(
                CropCancelId,
                ui::CanvasRect{
                    toolbar.x +
                        4.0 * (unit_width + button_gap),
                    toolbar.y,
                    unit_width,
                    toolbar.height,
                },
                viewport,
                labels.crop_cancel,
                crop_controls_enabled,
                false);
            if (!cancel_button)
            {
                return std::unexpected(
                    ProductPanelError{cancel_button.error()});
            }
            if (cancel_button->activated)
            {
                state.image_editor.crop.reset();
                state.image_editor.crop_dragging = false;
                crop_source = nullptr;
            }
        }
        else
        {
            const auto button_width =
                (toolbar.width - 5.0 * button_gap) / 6.0;
            const auto toolbar_button = [&](
                                            std::size_t index,
                                            std::string_view text,
                                            bool enabled,
                                            bool active)
                -> std::expected<
                    ui::WidgetResponse,
                    ProductPanelError>
            {
                const auto response = widgets.button(
                    LayerToolbarIds[index],
                    ui::CanvasRect{
                        toolbar.x +
                            static_cast<double>(index) *
                                (button_width + button_gap),
                        toolbar.y,
                        button_width,
                        toolbar.height,
                    },
                    viewport,
                    text,
                    enabled,
                    active);
                if (!response)
                {
                    return std::unexpected(
                        ProductPanelError{response.error()});
                }
                return *response;
            };

            const auto can_move_back =
                toolbar_enabled && *selected > 0U;
            const auto move_back = toolbar_button(
                0U,
                "↓",
                can_move_back,
                false);
            if (!move_back)
            {
                return std::unexpected(move_back.error());
            }
            if (move_back->activated)
            {
                const auto published = publish_layer_reorder(
                    *selected,
                    *selected - 1U,
                    model,
                    action);
                if (!published)
                {
                    return published;
                }
                state.image_editor.awaiting_revision = true;
            }

            const auto can_move_forward =
                toolbar_enabled &&
                *selected + 1U < document.layers.size();
            const auto move_forward = toolbar_button(
                1U,
                "↑",
                can_move_forward,
                false);
            if (!move_forward)
            {
                return std::unexpected(move_forward.error());
            }
            if (move_forward->activated)
            {
                const auto published = publish_layer_reorder(
                    *selected,
                    *selected + 1U,
                    model,
                    action);
                if (!published)
                {
                    return published;
                }
                state.image_editor.awaiting_revision = true;
            }

            const auto selected_wrap =
                selected
                    ? document.layers[*selected].wrap_atlas_seam
                    : false;
            const auto wrap = toolbar_button(
                2U,
                labels.image_wrap,
                toolbar_enabled,
                selected_wrap);
            if (!wrap)
            {
                return std::unexpected(wrap.error());
            }
            if (wrap->activated)
            {
                auto edited = document.layers[*selected];
                edited.wrap_atlas_seam =
                    !edited.wrap_atlas_seam;
                const auto published = publish_layer_edit(
                    ui::ImageLayerEdit{
                        *selected,
                        std::move(edited),
                    },
                    model,
                    action);
                if (!published)
                {
                    return published;
                }
                state.image_editor.awaiting_revision = true;
            }

            const auto selected_mirror =
                selected
                    ? document.layers[*selected]
                          .mirror_front_back
                    : false;
            const auto mirror = toolbar_button(
                3U,
                labels.image_mirror,
                toolbar_enabled,
                selected_mirror);
            if (!mirror)
            {
                return std::unexpected(mirror.error());
            }
            if (mirror->activated)
            {
                auto edited = document.layers[*selected];
                edited.mirror_front_back =
                    !edited.mirror_front_back;
                const auto published = publish_layer_edit(
                    ui::ImageLayerEdit{
                        *selected,
                        std::move(edited),
                    },
                    model,
                    action);
                if (!published)
                {
                    return published;
                }
                state.image_editor.awaiting_revision = true;
            }

            const auto crop = toolbar_button(
                4U,
                labels.image_crop,
                toolbar_enabled && selected_source,
                false);
            if (!crop)
            {
                return std::unexpected(crop.error());
            }
            if (crop->activated)
            {
                const auto started = ui::begin_image_crop(
                    *selected,
                    document.layers[*selected],
                    selected_source->width,
                    selected_source->height);
                if (!started)
                {
                    return std::unexpected(
                        ProductPanelError{started.error()});
                }
                state.image_editor.crop = *started;
                state.image_editor.crop_dragging = false;
            }

            const auto remove = toolbar_button(
                5U,
                labels.image_remove,
                toolbar_enabled &&
                    document.layers.size() > 1U,
                false);
            if (!remove)
            {
                return std::unexpected(remove.error());
            }
            if (remove->activated)
            {
                const auto published = publish_layer_remove(
                    *selected,
                    document.layers[*selected].asset_id,
                    model,
                    action);
                if (!published)
                {
                    return published;
                }
                state.image_editor.awaiting_revision = true;
            }
        }
    }
    else
    {
        state.image_editor = {};
        const auto project_controls =
            compose_image_project_toolbar(
                widgets,
                viewport,
                scroll->content_origin_y,
                layout.effective_scale,
                model,
                labels,
                input,
                state,
                action,
                effect);
        if (!project_controls)
        {
            return project_controls;
        }
    }

    const auto label_width = viewport.width * 0.42;
    const auto gap = 8.0 * layout.effective_scale;
    const auto control = [&](std::size_t row)
    {
        return ui::CanvasRect{
            viewport.x + label_width + gap,
            settings_origin_y +
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
               !state.image_editor.awaiting_revision &&
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
                settings_origin_y +
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
