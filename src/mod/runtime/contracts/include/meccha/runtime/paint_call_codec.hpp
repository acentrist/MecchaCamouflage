#pragma once

#include <meccha/application/game_thread_scheduler.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>

namespace meccha::runtime
{
enum class RuntimePaintChannel : std::uint8_t
{
    Albedo = 0U,
    Metallic = 1U,
    Roughness = 2U,
    Height = 3U,
    All = 4U,
    AlbedoMetallicRoughness = 5U,
    Emissive = 6U,
    AlbedoMetallicRoughnessEmissive = 7U,
};

enum class PaintChannelApplyMode : std::uint8_t
{
    Override = 0U,
};

enum class RuntimeBrushFalloff : std::uint8_t
{
    Spherical = 2U,
};

enum class RuntimePaintBlendMode : std::uint8_t
{
    Normal = 0U,
};

struct RuntimeVector2d
{
    double x{};
    double y{};
};

struct RuntimeLinearColor
{
    float red{};
    float green{};
    float blue{};
    float alpha{1.0F};
};

struct RuntimeBrushSettings
{
    float radius{};
    float hardness{1.0F};
    float opacity{1.0F};
    float spacing{1.0F};
    RuntimeBrushFalloff falloff{
        RuntimeBrushFalloff::Spherical};
    RuntimePaintBlendMode blend_mode{
        RuntimePaintBlendMode::Normal};
    std::uint8_t padding_12[0x06]{};
    void* brush_texture{};
    float rotation{};
    std::uint8_t padding_24[0x04]{};
};

struct RuntimePaintChannelData
{
    RuntimeLinearColor albedo{};
    float metallic{};
    float roughness{};
    float height{};
    float emissive{};
    PaintChannelApplyMode apply_mode{
        PaintChannelApplyMode::Override};
    std::uint8_t padding_21[0x03]{};
};

struct PaintAtUvWithBrushParameters
{
    RuntimeVector2d uv{};
    RuntimePaintChannelData channel_data{};
    RuntimeBrushSettings brush_settings{};
    RuntimePaintChannel channel{
        RuntimePaintChannel::
            AlbedoMetallicRoughnessEmissive};
    std::uint8_t padding_61[0x07]{};
};

struct InitializePaintParameters
{
    void* mesh_component{};
    bool return_value{};
    std::uint8_t padding_09[0x07]{};
};

struct IsPaintInitializedParameters
{
    bool return_value{};
};

struct GetInitializedPaintMeshParameters
{
    void* return_value{};
};

static_assert(sizeof(void*) == 0x08U);
static_assert(sizeof(InitializePaintParameters) == 0x10U);
static_assert(
    offsetof(InitializePaintParameters, return_value) == 0x08U);
static_assert(sizeof(IsPaintInitializedParameters) == 0x01U);
static_assert(sizeof(GetInitializedPaintMeshParameters) == 0x08U);
static_assert(sizeof(RuntimeVector2d) == 0x10U);
static_assert(sizeof(RuntimeLinearColor) == 0x10U);
static_assert(sizeof(RuntimeBrushSettings) == 0x28U);
static_assert(
    offsetof(RuntimeBrushSettings, brush_texture) == 0x18U);
static_assert(sizeof(RuntimePaintChannelData) == 0x24U);
static_assert(
    offsetof(RuntimePaintChannelData, metallic) == 0x10U);
static_assert(
    offsetof(RuntimePaintChannelData, roughness) == 0x14U);
static_assert(
    offsetof(RuntimePaintChannelData, emissive) == 0x1CU);
static_assert(
    offsetof(RuntimePaintChannelData, apply_mode) == 0x20U);
static_assert(sizeof(PaintAtUvWithBrushParameters) == 0x68U);
static_assert(
    offsetof(PaintAtUvWithBrushParameters, channel_data) ==
    0x10U);
static_assert(
    offsetof(PaintAtUvWithBrushParameters, brush_settings) ==
    0x38U);
static_assert(
    offsetof(PaintAtUvWithBrushParameters, channel) == 0x60U);

enum class PaintCallEncodingError : std::uint8_t
{
    InvalidRequest,
    InvalidDimension,
    InvalidCoordinates,
    InvalidBrush,
    InvalidMaterial,
};

[[nodiscard]] auto encode_initialize_paint(
    void* mesh_component)
    -> std::expected<
        InitializePaintParameters,
        PaintCallEncodingError>;

[[nodiscard]] auto encode_paint_call(
    const application::PaintAtUvWithBrush& request)
    -> std::expected<
        PaintAtUvWithBrushParameters,
        PaintCallEncodingError>;
} // namespace meccha::runtime
