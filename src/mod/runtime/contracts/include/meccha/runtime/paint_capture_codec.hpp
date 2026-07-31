#pragma once

#include <meccha/core/paint_capture_request.hpp>
#include <meccha/core/paint_sampling_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
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
} // namespace meccha::runtime
