#pragma once

#include <meccha/core/image_guide.hpp>
#include <meccha/core/image_project.hpp>
#include <meccha/core/mesh_profile.hpp>
#include <meccha/ui/canvas.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace meccha::ui
{
inline constexpr std::size_t MaximumImageEditorEventsPerFrame = 16U;
inline constexpr std::uint32_t ImageGuideOverlaySchemaVersion =
    core::ImageGuideSchemaVersion;

enum class ImageResizeCorner : std::uint8_t
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

enum class ImageGestureKind : std::uint8_t
{
    Move,
    Resize,
};

struct ImageEditorGesture
{
    ImageGestureKind kind{ImageGestureKind::Move};
    std::size_t layer_index{};
    ImageResizeCorner corner{ImageResizeCorner::BottomRight};
    double origin_u{};
    double origin_v{};
    core::ImageLayer original{};

    auto operator==(const ImageEditorGesture&) const
        -> bool = default;
};

struct ImageEditorInteractionState
{
    std::optional<std::size_t> selected_layer{};
    std::optional<ImageEditorGesture> gesture{};

    auto operator==(const ImageEditorInteractionState&) const
        -> bool = default;
};

enum class ImageEditorPointerEventKind : std::uint8_t
{
    Press,
    Move,
    Release,
    Cancel,
};

struct ImageEditorPointerEvent
{
    ImageEditorPointerEventKind kind{
        ImageEditorPointerEventKind::Move};
    CanvasPoint position{};

    auto operator==(const ImageEditorPointerEvent&) const
        -> bool = default;
};

struct ImageLayerEdit
{
    std::size_t layer_index{};
    core::ImageLayer layer{};

    auto operator==(const ImageLayerEdit&) const -> bool = default;
};

struct ImageEditorInteractionUpdate
{
    ImageEditorInteractionState state{};
    std::optional<ImageLayerEdit> edit{};
    bool changed{};
    bool committed{};
    bool cancelled{};
};

enum class ImageEditorInteractionError : std::uint8_t
{
    InvalidGeometry,
    InvalidState,
    InvalidLayer,
    InvalidEvent,
    EventLimit,
};

[[nodiscard]] auto update_image_editor_interaction(
    ImageEditorInteractionState previous,
    std::span<const core::ImageLayer> layers,
    CanvasRect atlas_rect,
    std::span<const ImageEditorPointerEvent> events)
    -> std::expected<
        ImageEditorInteractionUpdate,
        ImageEditorInteractionError>;

struct ImageLayerOrderUpdate
{
    std::vector<core::ImageLayer> layers{};
    std::size_t selected_layer{};
};

[[nodiscard]] auto reorder_image_layer(
    std::span<const core::ImageLayer> layers,
    std::size_t selected_layer,
    std::size_t destination)
    -> std::expected<
        ImageLayerOrderUpdate,
        ImageEditorInteractionError>;

struct ImageCropSession
{
    std::size_t layer_index{};
    std::string asset_id{};
    core::NormalizedCrop original{};
    core::NormalizedCrop base{};
    core::NormalizedCrop draft{};
    double zoom{1.0};

    auto operator==(const ImageCropSession&) const -> bool = default;
};

enum class ImageCropError : std::uint8_t
{
    InvalidLayer,
    InvalidSource,
    InvalidSession,
    InvalidZoom,
    InvalidCenter,
};

[[nodiscard]] auto begin_image_crop(
    std::size_t layer_index,
    const core::ImageLayer& layer,
    std::uint32_t source_width,
    std::uint32_t source_height)
    -> std::expected<ImageCropSession, ImageCropError>;

[[nodiscard]] auto set_image_crop_zoom(
    ImageCropSession session,
    double zoom)
    -> std::expected<ImageCropSession, ImageCropError>;

[[nodiscard]] auto move_image_crop_center(
    ImageCropSession session,
    double center_x,
    double center_y)
    -> std::expected<ImageCropSession, ImageCropError>;

[[nodiscard]] auto apply_image_crop(
    const ImageCropSession& session,
    const core::ImageLayer& layer)
    -> std::expected<core::ImageLayer, ImageCropError>;

[[nodiscard]] auto restore_image_crop(
    const ImageCropSession& session,
    const core::ImageLayer& layer)
    -> std::expected<core::ImageLayer, ImageCropError>;

struct ImageGuideOverlay
{
    std::uint32_t schema_version{ImageGuideOverlaySchemaVersion};
    core::MeshProfileIdentity profile{};
    CanvasTextureHandle texture{};

    auto operator==(const ImageGuideOverlay&) const -> bool = default;
};

struct ImageEditorPalette
{
    CanvasColor layer_border{153U, 153U, 153U, 255U};
    CanvasColor selected_border{255U, 255U, 255U, 255U};
    CanvasColor resize_handle{255U, 255U, 255U, 255U};

    auto operator==(const ImageEditorPalette&) const -> bool = default;
};

struct ImageEditorView
{
    core::BodyProfile body{core::BodyProfile::Round};
    CanvasTextureHandle atlas_texture{};
    std::span<const core::ImageLayer> layers{};
    std::optional<std::size_t> selected_layer{};
    std::optional<ImageGuideOverlay> guide{};
    bool editing{};
    std::array<std::string_view, 4U> guide_labels{};
};

struct ImageEditorDrawResult
{
    std::size_t layer_outlines{};
    bool guide_drawn{};
    std::size_t guide_labels_drawn{};
};

enum class ImageEditorDrawValidationError : std::uint8_t
{
    InvalidGeometry,
    InvalidAtlas,
    InvalidLayer,
    InvalidSelection,
    InvalidGuide,
};

using ImageEditorDrawError = std::variant<
    CanvasError,
    ImageEditorDrawValidationError>;

[[nodiscard]] auto draw_image_editor(
    CanvasFrameBuilder& canvas,
    CanvasRect atlas_rect,
    CanvasRect clip,
    const ImageEditorView& view,
    ImageEditorPalette palette = {})
    -> std::expected<ImageEditorDrawResult, ImageEditorDrawError>;
} // namespace meccha::ui
