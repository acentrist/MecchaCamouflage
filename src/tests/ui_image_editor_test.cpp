#include <meccha/ui/image_editor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_image_editor: " << message << '\n';
    }
    return condition;
}

auto near(double left, double right) -> bool
{
    return std::abs(left - right) < 0.000001;
}

auto layer(
    std::string asset_id,
    double center_x,
    double center_y,
    double width,
    double height) -> meccha::core::ImageLayer
{
    return {
        std::move(asset_id),
        "layer.png",
        meccha::core::ImageMime::Png,
        128U,
        center_x,
        center_y,
        width,
        height,
        {},
        false,
        false,
    };
}

constexpr auto Viewport =
    meccha::ui::CanvasViewport{1280.0, 720.0, 1.0};
constexpr auto Atlas =
    meccha::ui::CanvasRect{100.0, 80.0, 1024.0, 512.0};
constexpr auto Clip =
    meccha::ui::CanvasRect{80.0, 60.0, 1080.0, 552.0};
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto passed = true;
    auto layers = std::vector{
        layer("bottom", 0.5, 0.5, 0.75, 0.75),
        layer("top", 0.5, 0.5, 0.5, 0.5),
    };

    const auto pressed = ui::update_image_editor_interaction(
        {},
        layers,
        Atlas,
        std::array{
            ui::ImageEditorPointerEvent{
                ui::ImageEditorPointerEventKind::Press,
                ui::CanvasPoint{612.0, 336.0},
            },
        });
    passed &= expect(
        pressed && pressed->state.selected_layer == 1U &&
            pressed->state.gesture &&
            pressed->state.gesture->kind ==
                ui::ImageGestureKind::Move &&
            !pressed->edit,
        "overlap press did not capture the topmost layer");

    const auto moved = ui::update_image_editor_interaction(
        pressed->state,
        layers,
        Atlas,
        std::array{
            ui::ImageEditorPointerEvent{
                ui::ImageEditorPointerEventKind::Move,
                ui::CanvasPoint{714.4, 284.8},
            },
        });
    passed &= expect(
        moved && moved->edit &&
            moved->edit->layer_index == 1U &&
            near(moved->edit->layer.center_x, 0.6) &&
            near(moved->edit->layer.center_y, 0.4) &&
            moved->changed && !moved->committed,
        "move gesture did not emit a normalized immutable edit");
    if (!moved || !moved->edit)
    {
        return 1;
    }
    layers[1] = moved->edit->layer;

    const auto released = ui::update_image_editor_interaction(
        moved->state,
        layers,
        Atlas,
        std::array{
            ui::ImageEditorPointerEvent{
                ui::ImageEditorPointerEventKind::Release,
                ui::CanvasPoint{714.4, 284.8},
            },
        });
    passed &= expect(
        released && !released->state.gesture &&
            released->state.selected_layer == 1U &&
            released->committed && !released->cancelled &&
            !released->changed,
        "release did not commit and clear pointer capture");

    auto resize_layers = std::vector{
        layer("back", 0.5, 0.5, 0.4, 0.4),
        layer("front", 0.7, 0.7, 0.4, 0.4),
    };
    const auto resize_press =
        ui::update_image_editor_interaction(
            {},
            resize_layers,
            Atlas,
            std::array{
                ui::ImageEditorPointerEvent{
                    ui::ImageEditorPointerEventKind::Press,
                    ui::CanvasPoint{816.8, 438.4},
                },
            });
    passed &= expect(
        resize_press &&
            resize_press->state.selected_layer == 0U &&
            resize_press->state.gesture &&
            resize_press->state.gesture->kind ==
                ui::ImageGestureKind::Resize &&
            resize_press->state.gesture->corner ==
                ui::ImageResizeCorner::BottomRight,
        "resize handle did not win over an overlapping layer body");
    const auto minimum_resize =
        ui::update_image_editor_interaction(
            resize_press->state,
            resize_layers,
            Atlas,
            std::array{
                ui::ImageEditorPointerEvent{
                    ui::ImageEditorPointerEventKind::Move,
                    ui::CanvasPoint{210.0, 140.0},
                },
            });
    passed &= expect(
        minimum_resize && minimum_resize->edit &&
            near(
                minimum_resize->edit->layer.width,
                24.0 / 1024.0) &&
            near(
                minimum_resize->edit->layer.height,
                24.0 / 512.0),
        "corner resize did not preserve the retained 24px minimum");

    const auto drag_press = ui::update_image_editor_interaction(
        {},
        layers,
        Atlas,
        std::array{
            ui::ImageEditorPointerEvent{
                ui::ImageEditorPointerEventKind::Press,
                ui::CanvasPoint{714.4, 284.8},
            },
        });
    const auto drag_move = ui::update_image_editor_interaction(
        drag_press->state,
        layers,
        Atlas,
        std::array{
            ui::ImageEditorPointerEvent{
                ui::ImageEditorPointerEventKind::Move,
                ui::CanvasPoint{816.8, 387.2},
            },
        });
    auto moved_layers = layers;
    moved_layers[1] = drag_move->edit->layer;
    moved_layers[1].wrap_atlas_seam = true;
    const auto cancelled = ui::update_image_editor_interaction(
        drag_move->state,
        moved_layers,
        Atlas,
        std::array{
            ui::ImageEditorPointerEvent{
                ui::ImageEditorPointerEventKind::Cancel,
                ui::CanvasPoint{},
            },
        });
    passed &= expect(
        cancelled && cancelled->cancelled &&
            !cancelled->state.gesture && cancelled->edit &&
            near(
                cancelled->edit->layer.center_x,
                layers[1].center_x) &&
            near(
                cancelled->edit->layer.center_y,
                layers[1].center_y) &&
            cancelled->edit->layer.wrap_atlas_seam &&
            cancelled->changed,
        "cancel did not restore placement without losing another field");

    const auto reordered =
        ui::reorder_image_layer(layers, 1U, 0U);
    passed &= expect(
        reordered && reordered->selected_layer == 0U &&
            reordered->layers[0].asset_id == "top" &&
            reordered->layers[1].asset_id == "bottom",
        "layer reorder did not preserve values and selection");
    passed &= expect(
        ui::reorder_image_layer(layers, 0U, layers.size()) ==
            std::unexpected(
                ui::ImageEditorInteractionError::InvalidState),
        "out-of-range layer reorder was accepted");

    const auto crop = ui::begin_image_crop(
        1U,
        layers[1],
        400U,
        300U);
    passed &= expect(
        crop && near(crop->base.x, 0.0) &&
            near(crop->base.y, 1.0 / 6.0) &&
            near(crop->base.width, 1.0) &&
            near(crop->base.height, 2.0 / 3.0),
        "crop session did not derive the source/target aspect base");
    const auto zoomed = ui::set_image_crop_zoom(*crop, 2.0);
    passed &= expect(
        zoomed && near(zoomed->draft.width, 0.5) &&
            near(zoomed->draft.height, 1.0 / 3.0),
        "crop zoom did not preserve base aspect");
    const auto positioned =
        ui::move_image_crop_center(*zoomed, 1.0, 0.0);
    passed &= expect(
        positioned && near(positioned->draft.x, 0.5) &&
            near(positioned->draft.y, 0.0),
        "crop movement did not clamp the selection inside the source");
    const auto cropped =
        ui::apply_image_crop(*positioned, layers[1]);
    passed &= expect(
        cropped && cropped->crop == positioned->draft,
        "crop apply did not publish the draft");
    const auto restored =
        ui::restore_image_crop(*positioned, *cropped);
    passed &= expect(
        restored && restored->crop == crop->original,
        "crop restore did not recover the exact original");
    auto wrong_crop_layer = layers[1];
    wrong_crop_layer.asset_id = "replacement";
    passed &= expect(
        ui::apply_image_crop(*positioned, wrong_crop_layer) ==
            std::unexpected(ui::ImageCropError::InvalidLayer),
        "crop session crossed an asset replacement");

    auto canvas = ui::CanvasFrameBuilder{Viewport};
    const auto guide_profile = core::expected_mesh_profile(
        core::BodyProfile::Round,
        core::MeshProfileRole::ImageReference);
    const auto drawn = ui::draw_image_editor(
        canvas,
        Atlas,
        Clip,
        ui::ImageEditorView{
            core::BodyProfile::Round,
            ui::CanvasTextureHandle{10U},
            layers,
            1U,
            ui::ImageGuideOverlay{
                ui::ImageGuideOverlaySchemaVersion,
                guide_profile,
                ui::CanvasTextureHandle{20U},
            },
            true,
        });
    const auto frame = std::move(canvas).finish();
    passed &= expect(
        drawn && drawn->layer_outlines == 2U &&
            drawn->guide_drawn && frame,
        "two-layer editor frame did not render");
    if (frame)
    {
        auto textures =
            std::vector<ui::CanvasTextureHandle>{};
        for (const auto& primitive : frame->primitives)
        {
            if (const auto* texture =
                    std::get_if<ui::CanvasTexturePrimitive>(
                        &primitive))
            {
                textures.push_back(texture->texture);
            }
        }
        passed &= expect(
            textures == std::vector{
                            ui::CanvasTextureHandle{10U},
                            ui::CanvasTextureHandle{20U},
                        },
            "guide was not a separate overlay above the atlas");
    }

    auto invalid_guide_canvas =
        ui::CanvasFrameBuilder{Viewport};
    const auto invalid_guide = ui::draw_image_editor(
        invalid_guide_canvas,
        Atlas,
        Clip,
        ui::ImageEditorView{
            core::BodyProfile::Cube,
            ui::CanvasTextureHandle{10U},
            layers,
            std::nullopt,
            ui::ImageGuideOverlay{
                ui::ImageGuideOverlaySchemaVersion,
                guide_profile,
                ui::CanvasTextureHandle{20U},
            },
            false,
        });
    passed &= expect(
        invalid_guide ==
            std::unexpected(ui::ImageEditorDrawError{
                ui::ImageEditorDrawValidationError::InvalidGuide}),
        "guide identity was not bound to the selected body profile");

    auto constrained_canvas =
        ui::CanvasFrameBuilder{Viewport, 1U};
    const auto constrained = ui::draw_image_editor(
        constrained_canvas,
        Atlas,
        Clip,
        ui::ImageEditorView{
            core::BodyProfile::Round,
            ui::CanvasTextureHandle{10U},
            layers,
            std::nullopt,
            ui::ImageGuideOverlay{
                ui::ImageGuideOverlaySchemaVersion,
                guide_profile,
                ui::CanvasTextureHandle{20U},
            },
            false,
        });
    passed &= expect(
        constrained ==
                std::unexpected(ui::ImageEditorDrawError{
                    ui::CanvasError::PrimitiveLimit}) &&
            std::move(constrained_canvas).finish(),
        "failed editor draw did not restore the Canvas clip stack");

    const auto invalid_sequence =
        ui::update_image_editor_interaction(
            pressed->state,
            layers,
            Atlas,
            std::array{
                ui::ImageEditorPointerEvent{
                    ui::ImageEditorPointerEventKind::Release,
                    ui::CanvasPoint{612.0, 336.0},
                },
                ui::ImageEditorPointerEvent{
                    ui::ImageEditorPointerEventKind::Move,
                    ui::CanvasPoint{620.0, 336.0},
                },
            });
    passed &= expect(
        invalid_sequence ==
            std::unexpected(
                ui::ImageEditorInteractionError::InvalidEvent),
        "pointer events after release were accepted");

    auto excessive_events =
        std::vector<ui::ImageEditorPointerEvent>(
            ui::MaximumImageEditorEventsPerFrame + 1U,
            ui::ImageEditorPointerEvent{
                ui::ImageEditorPointerEventKind::Move,
                ui::CanvasPoint{612.0, 336.0},
            });
    passed &= expect(
        ui::update_image_editor_interaction(
            {},
            layers,
            Atlas,
            excessive_events) ==
            std::unexpected(
                ui::ImageEditorInteractionError::EventLimit),
        "per-frame editor event bound was not enforced");

    auto replaced_layers = layers;
    replaced_layers[1].asset_id = "replacement";
    passed &= expect(
        ui::update_image_editor_interaction(
            pressed->state,
            replaced_layers,
            Atlas,
            {}) ==
            std::unexpected(
                ui::ImageEditorInteractionError::InvalidState),
        "active gesture survived an asset replacement");

    const auto invalid_pointer =
        ui::update_image_editor_interaction(
            {},
            layers,
            Atlas,
            std::array{
                ui::ImageEditorPointerEvent{
                    ui::ImageEditorPointerEventKind::Press,
                    ui::CanvasPoint{
                        std::numeric_limits<double>::quiet_NaN(),
                        0.0,
                    },
                },
            });
    passed &= expect(
        invalid_pointer ==
            std::unexpected(
                ui::ImageEditorInteractionError::InvalidEvent),
        "non-finite pointer input was accepted");

    if (passed)
    {
        std::cout << "PASS ui_image_editor\n";
    }
    return passed ? 0 : 1;
}
