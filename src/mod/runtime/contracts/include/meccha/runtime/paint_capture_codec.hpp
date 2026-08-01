#pragma once

#include <meccha/core/paint_appearance.hpp>
#include <meccha/core/paint_capture_request.hpp>
#include <meccha/core/paint_sampling_profile.hpp>
#include <meccha/runtime/esp_capture_codec.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace meccha::runtime
{
struct RuntimeName
{
    std::uint32_t comparison_index{};
    std::uint32_t number{};
};

enum class RuntimeRelativeTransformSpace : std::uint8_t
{
    World = 0U,
    Actor = 1U,
    Component = 2U,
    ParentBone = 3U,
};

struct alignas(16) RuntimeQuaternion
{
    double x{};
    double y{};
    double z{};
    double w{1.0};
};

struct alignas(16) RuntimeVector3d
{
    double x{};
    double y{};
    double z{};
    double padding{};
};

struct RuntimeTransform
{
    RuntimeQuaternion rotation{};
    RuntimeVector3d translation{};
    RuntimeVector3d scale{1.0, 1.0, 1.0, 0.0};
};

struct GetSocketTransformParameters
{
    RuntimeName socket_name{};
    RuntimeRelativeTransformSpace transform_space{
        RuntimeRelativeTransformSpace::World};
    std::uint8_t padding_09[0x07]{};
    RuntimeTransform return_value{};
};

static_assert(sizeof(RuntimeName) == 0x08U);
static_assert(sizeof(RuntimeQuaternion) == 0x20U);
static_assert(sizeof(RuntimeVector3d) == 0x20U);
static_assert(sizeof(RuntimeTransform) == 0x60U);
static_assert(
    offsetof(RuntimeTransform, translation) == 0x20U);
static_assert(offsetof(RuntimeTransform, scale) == 0x40U);
static_assert(sizeof(GetSocketTransformParameters) == 0x70U);
static_assert(
    offsetof(GetSocketTransformParameters, return_value) ==
    0x10U);

enum class PaintCaptureRenderTargetFormat : std::uint8_t
{
    Rgba8Srgb = 3U,
    Rgba16Float = 6U,
};

enum class PaintSceneCaptureSource : std::uint8_t
{
    SceneColorHdr = 0U,
    SceneColorHdrNoAlpha = 1U,
    FinalColorLdr = 2U,
    SceneColorSceneDepth = 3U,
    SceneDepth = 4U,
    DeviceDepth = 5U,
    Normal = 6U,
    BaseColor = 7U,
    FinalColorHdr = 8U,
    FinalToneCurveHdr = 9U,
};

enum class PaintSceneCaptureProjection : std::uint8_t
{
    Perspective = 0U,
    Orthographic = 1U,
};

enum class PaintSceneCapturePassKind : std::uint8_t
{
    BaseColor,
    FinalColorHdr,
    IntrinsicEmissionHdr,
    IntrinsicEmissionRepeatHdr,
    FinalToneCurveHdr,
    Normal,
    SceneDepth,
    FinalColorLdr,
};

enum class PaintSceneCaptureProfile : std::uint8_t
{
    Standard,
    IntrinsicEmission,
};

enum class PaintSceneCaptureSubject : std::uint8_t
{
    BackgroundOnly,
    TargetVisible,
};

struct PaintSceneCapturePass
{
    PaintSceneCapturePassKind kind{};
    PaintSceneCaptureSource source{};
    PaintCaptureRenderTargetFormat format{
        PaintCaptureRenderTargetFormat::Rgba8Srgb};
    PaintSceneCaptureProfile profile{
        PaintSceneCaptureProfile::Standard};
    bool normalize_readback{};
    bool preserve_hdr{};
    PaintSceneCaptureSubject subject{
        PaintSceneCaptureSubject::BackgroundOnly};

    auto operator==(const PaintSceneCapturePass&) const
        -> bool = default;
};

struct PaintSceneCapturePlan
{
    std::vector<PaintSceneCapturePass> passes{};
    bool requires_preview_feedback{};

    auto operator==(const PaintSceneCapturePlan&) const
        -> bool = default;
};

struct PaintSceneCaptureCamera
{
    EspVector3dAbi location{};
    EspRotatorAbi rotation{};
    float field_of_view_degrees{};
    std::uint32_t width{};
    std::uint32_t height{};

    auto operator==(const PaintSceneCaptureCamera&) const
        -> bool = default;
};

enum class PaintBrushPlaneVisualKind : std::uint8_t
{
    StaticMesh,
    Niagara,
};

struct PaintBrushPlaneVisualComponent
{
    std::string_view name{};
    PaintBrushPlaneVisualKind kind{};

    auto operator==(const PaintBrushPlaneVisualComponent&) const
        -> bool = default;
};

struct PaintBrushPlaneVisualContract
{
    std::string_view actor_class_path{};
    std::array<PaintBrushPlaneVisualComponent, 3U> components{};

    auto operator==(const PaintBrushPlaneVisualContract&) const
        -> bool = default;
};

struct PaintShowFlagSetting
{
    std::string_view name{};
    bool enabled{};

    auto operator==(const PaintShowFlagSetting&) const
        -> bool = default;
};

struct PaintCaptureLinearColor
{
    float red{};
    float green{};
    float blue{};
    float alpha{1.0F};
};

struct PaintCaptureLinearColorArray
{
    PaintCaptureLinearColor* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct PaintCaptureRenderTargetInput
{
    void* world_context_object{};
    std::uint32_t width{};
    std::uint32_t height{};
    PaintCaptureRenderTargetFormat format{
        PaintCaptureRenderTargetFormat::Rgba8Srgb};
};

struct CreatePaintCaptureRenderTargetParameters
{
    void* world_context_object{};
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t slices{1};
    PaintCaptureRenderTargetFormat format{
        PaintCaptureRenderTargetFormat::Rgba8Srgb};
    std::uint8_t padding_15[0x03]{};
    PaintCaptureLinearColor clear_color{};
    bool auto_generate_mip_maps{};
    bool support_uavs{};
    std::uint8_t padding_2a[0x06]{};
    void* return_value{};
};

struct ReadPaintCaptureRenderTargetParameters
{
    void* world_context_object{};
    void* texture_render_target{};
    PaintCaptureLinearColorArray out_linear_samples{};
    bool normalize{};
    bool return_value{};
    std::uint8_t padding_22[0x06]{};
};

static_assert(sizeof(PaintCaptureLinearColor) == 0x10U);
static_assert(sizeof(PaintCaptureLinearColorArray) == 0x10U);
static_assert(
    sizeof(CreatePaintCaptureRenderTargetParameters) == 0x38U);
static_assert(
    offsetof(
        CreatePaintCaptureRenderTargetParameters,
        return_value) == 0x30U);
static_assert(
    sizeof(ReadPaintCaptureRenderTargetParameters) == 0x28U);

enum class PaintCaptureEncodingError : std::uint8_t
{
    InvalidTransform,
    InvalidRenderTarget,
    InvalidReadback,
    InvalidSettings,
    InvalidCamera,
    InvalidColor,
};

[[nodiscard]] auto decode_runtime_transform(
    const RuntimeTransform& transform)
    -> std::expected<
        core::PaintReferenceBoneTransform,
        PaintCaptureEncodingError>;

[[nodiscard]] auto encode_create_paint_capture_render_target(
    const PaintCaptureRenderTargetInput& input)
    -> std::expected<
        CreatePaintCaptureRenderTargetParameters,
        PaintCaptureEncodingError>;

[[nodiscard]] auto encode_read_paint_capture_render_target(
    void* world_context_object,
    void* texture_render_target,
    bool normalize)
    -> std::expected<
        ReadPaintCaptureRenderTargetParameters,
        PaintCaptureEncodingError>;

[[nodiscard]] auto decode_paint_capture_linear_colors(
    const ReadPaintCaptureRenderTargetParameters& parameters,
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<
        std::vector<PaintCaptureLinearColor>,
        PaintCaptureEncodingError>;

[[nodiscard]] auto build_paint_scene_capture_plan(
    const core::PaintSettings& settings)
    -> std::expected<
        PaintSceneCapturePlan,
        PaintCaptureEncodingError>;

[[nodiscard]] auto paint_appearance_feedback_capture_plan()
    -> const std::array<PaintSceneCapturePass, 3U>&;

[[nodiscard]] auto encode_paint_scene_capture_camera(
    const core::EspView& view,
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<
        PaintSceneCaptureCamera,
        PaintCaptureEncodingError>;

[[nodiscard]] auto
convert_paint_capture_linear_colors_to_srgb8(
    std::span<const PaintCaptureLinearColor> colors)
    -> std::expected<
        std::vector<core::Rgb8>,
        PaintCaptureEncodingError>;

[[nodiscard]] auto
convert_paint_capture_linear_colors_to_hdr(
    std::span<const PaintCaptureLinearColor> colors)
    -> std::expected<
        std::vector<core::AppearanceRgb>,
        PaintCaptureEncodingError>;

[[nodiscard]] auto
convert_paint_capture_linear_colors_to_depth(
    std::span<const PaintCaptureLinearColor> colors)
    -> std::expected<
        std::vector<double>,
        PaintCaptureEncodingError>;

[[nodiscard]] auto paint_brush_plane_visual_contract()
    -> PaintBrushPlaneVisualContract;

[[nodiscard]] auto paint_intrinsic_emission_show_flags()
    -> const std::array<PaintShowFlagSetting, 33U>&;
} // namespace meccha::runtime
