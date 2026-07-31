#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace meccha::runtime
{
struct CanvasPointInput
{
    double x{};
    double y{};
};

struct CanvasColorInput
{
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255U};
};

struct CanvasLineInput
{
    CanvasPointInput start{};
    CanvasPointInput end{};
    CanvasColorInput color{};
    double thickness{1.0};
};

struct CanvasRectInput
{
    double x{};
    double y{};
    double width{};
    double height{};
};

struct CanvasBoxInput
{
    CanvasRectInput rect{};
    CanvasColorInput color{};
};

struct CanvasUvRectInput
{
    double left{};
    double top{};
    double right{1.0};
    double bottom{1.0};
};

struct CanvasTextureInput
{
    void* texture{};
    CanvasRectInput rect{};
    CanvasUvRectInput uv{};
    CanvasColorInput tint{};
};

struct RuntimeStringAbi
{
    const char16_t* data{};
    std::int32_t count{};
    std::int32_t capacity{};
};

struct CanvasTextInput
{
    void* font{};
    RuntimeStringAbi text{};
    CanvasPointInput anchor{};
    CanvasColorInput color{};
    double scale{1.0};
};

struct CanvasVector2dAbi
{
    double x{};
    double y{};
};

struct CanvasLinearColorAbi
{
    float red{};
    float green{};
    float blue{};
    float alpha{1.0F};
};

struct K2DrawLineParametersAbi
{
    CanvasVector2dAbi screen_position_a{};
    CanvasVector2dAbi screen_position_b{};
    float thickness{1.0F};
    CanvasLinearColorAbi render_color{};
    std::byte padding_34[0x04]{};
};

enum class CanvasBlendMode : std::uint8_t
{
    Translucent = 2U,
};

struct K2DrawTextureParametersAbi
{
    void* render_texture{};
    CanvasVector2dAbi screen_position{};
    CanvasVector2dAbi screen_size{};
    CanvasVector2dAbi coordinate_position{};
    CanvasVector2dAbi coordinate_size{1.0, 1.0};
    CanvasLinearColorAbi render_color{};
    CanvasBlendMode blend_mode{
        CanvasBlendMode::Translucent};
    std::byte padding_59[0x03]{};
    float rotation{};
    CanvasVector2dAbi pivot_point{0.5, 0.5};
};

struct K2DrawTextParametersAbi
{
    void* render_font{};
    RuntimeStringAbi render_text{};
    CanvasVector2dAbi screen_position{};
    CanvasVector2dAbi scale{1.0, 1.0};
    CanvasLinearColorAbi render_color{};
    float kerning{};
    CanvasLinearColorAbi shadow_color{
        0.0F,
        0.0F,
        0.0F,
        0.0F,
    };
    std::byte padding_5c[0x04]{};
    CanvasVector2dAbi shadow_offset{};
    bool centre_x{};
    bool centre_y{};
    bool outlined{};
    std::byte padding_73[0x01]{};
    CanvasLinearColorAbi outline_color{
        0.0F,
        0.0F,
        0.0F,
        0.0F,
    };
    std::byte padding_84[0x04]{};
};

static_assert(sizeof(CanvasVector2dAbi) == 0x10U);
static_assert(sizeof(CanvasLinearColorAbi) == 0x10U);
static_assert(sizeof(K2DrawLineParametersAbi) == 0x38U);
static_assert(
    offsetof(K2DrawLineParametersAbi, screen_position_b) ==
    0x10U);
static_assert(
    offsetof(K2DrawLineParametersAbi, thickness) == 0x20U);
static_assert(
    offsetof(K2DrawLineParametersAbi, render_color) == 0x24U);
static_assert(sizeof(K2DrawTextureParametersAbi) == 0x70U);
static_assert(
    offsetof(
        K2DrawTextureParametersAbi,
        screen_position) == 0x08U);
static_assert(
    offsetof(K2DrawTextureParametersAbi, screen_size) ==
    0x18U);
static_assert(
    offsetof(
        K2DrawTextureParametersAbi,
        coordinate_position) == 0x28U);
static_assert(
    offsetof(
        K2DrawTextureParametersAbi,
        coordinate_size) == 0x38U);
static_assert(
    offsetof(K2DrawTextureParametersAbi, render_color) ==
    0x48U);
static_assert(
    offsetof(K2DrawTextureParametersAbi, blend_mode) ==
    0x58U);
static_assert(
    offsetof(K2DrawTextureParametersAbi, rotation) == 0x5CU);
static_assert(
    offsetof(K2DrawTextureParametersAbi, pivot_point) ==
    0x60U);
static_assert(sizeof(RuntimeStringAbi) == 0x10U);
static_assert(sizeof(K2DrawTextParametersAbi) == 0x88U);
static_assert(
    offsetof(K2DrawTextParametersAbi, render_text) == 0x08U);
static_assert(
    offsetof(K2DrawTextParametersAbi, screen_position) ==
    0x18U);
static_assert(
    offsetof(K2DrawTextParametersAbi, scale) == 0x28U);
static_assert(
    offsetof(K2DrawTextParametersAbi, render_color) == 0x38U);
static_assert(
    offsetof(K2DrawTextParametersAbi, kerning) == 0x48U);
static_assert(
    offsetof(K2DrawTextParametersAbi, shadow_color) == 0x4CU);
static_assert(
    offsetof(K2DrawTextParametersAbi, shadow_offset) == 0x60U);
static_assert(
    offsetof(K2DrawTextParametersAbi, centre_x) == 0x70U);
static_assert(
    offsetof(K2DrawTextParametersAbi, outline_color) == 0x74U);

enum class CanvasCallCodecError : std::uint8_t
{
    InvalidGeometry,
    InvalidThickness,
    InvalidText,
};

[[nodiscard]] auto encode_canvas_utf16(std::string_view utf8)
    -> std::expected<
        std::vector<char16_t>,
        CanvasCallCodecError>;

[[nodiscard]] auto encode_canvas_line(
    const CanvasLineInput& line)
    -> std::expected<
        K2DrawLineParametersAbi,
        CanvasCallCodecError>;

[[nodiscard]] auto encode_canvas_filled_box(
    const CanvasBoxInput& box)
    -> std::expected<
        K2DrawTextureParametersAbi,
        CanvasCallCodecError>;

[[nodiscard]] auto encode_canvas_texture(
    const CanvasTextureInput& texture)
    -> std::expected<
        K2DrawTextureParametersAbi,
        CanvasCallCodecError>;

[[nodiscard]] auto encode_canvas_text(
    const CanvasTextInput& text)
    -> std::expected<
        K2DrawTextParametersAbi,
        CanvasCallCodecError>;
} // namespace meccha::runtime
