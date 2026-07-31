#include <meccha/runtime/canvas_call_codec.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <expected>
#include <limits>
#include <string_view>
#include <vector>

namespace meccha::runtime
{
namespace
{
auto finite_point(const CanvasPointInput& point) -> bool
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

auto srgb_to_linear(std::uint8_t encoded) -> float
{
    const auto value =
        static_cast<double>(encoded) / 255.0;
    const auto linear =
        value <= 0.04045
            ? value / 12.92
            : std::pow((value + 0.055) / 1.055, 2.4);
    return static_cast<float>(
        std::clamp(linear, 0.0, 1.0));
}

auto encode_color(const CanvasColorInput& color)
    -> CanvasLinearColorAbi
{
    return CanvasLinearColorAbi{
        srgb_to_linear(color.red),
        srgb_to_linear(color.green),
        srgb_to_linear(color.blue),
        static_cast<float>(color.alpha) / 255.0F,
    };
}
} // namespace

auto encode_canvas_utf16(std::string_view utf8)
    -> std::expected<
        std::vector<char16_t>,
        CanvasCallCodecError>
{
    constexpr auto maximum_utf8_bytes = std::size_t{4'096U};
    if (utf8.empty() || utf8.size() > maximum_utf8_bytes)
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidText);
    }

    const auto codepoints = core::decode_utf8(utf8);
    if (!codepoints)
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidText);
    }

    auto encoded = std::vector<char16_t>{};
    encoded.reserve(codepoints->size() + 1U);
    for (const auto codepoint : *codepoints)
    {
        if (codepoint == U'\0')
        {
            return std::unexpected(
                CanvasCallCodecError::InvalidText);
        }
        if (codepoint <= 0xFFFFU)
        {
            encoded.push_back(
                static_cast<char16_t>(codepoint));
            continue;
        }

        const auto supplementary =
            static_cast<std::uint32_t>(codepoint) - 0x10000U;
        encoded.push_back(static_cast<char16_t>(
            0xD800U + (supplementary >> 10U)));
        encoded.push_back(static_cast<char16_t>(
            0xDC00U + (supplementary & 0x3FFU)));
    }
    encoded.push_back(u'\0');
    return encoded;
}

auto encode_canvas_line(const CanvasLineInput& line)
    -> std::expected<
        K2DrawLineParametersAbi,
        CanvasCallCodecError>
{
    if (!finite_point(line.start) ||
        !finite_point(line.end) ||
        (line.start.x == line.end.x &&
         line.start.y == line.end.y))
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidGeometry);
    }
    if (!std::isfinite(line.thickness) ||
        line.thickness < 0.25 ||
        line.thickness > 16.0)
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidThickness);
    }

    auto parameters = K2DrawLineParametersAbi{};
    parameters.screen_position_a = {
        line.start.x,
        line.start.y,
    };
    parameters.screen_position_b = {
        line.end.x,
        line.end.y,
    };
    parameters.thickness =
        static_cast<float>(line.thickness);
    parameters.render_color = encode_color(line.color);
    return parameters;
}

auto encode_canvas_filled_box(const CanvasBoxInput& box)
    -> std::expected<
        K2DrawTextureParametersAbi,
        CanvasCallCodecError>
{
    if (!std::isfinite(box.rect.x) ||
        !std::isfinite(box.rect.y) ||
        !std::isfinite(box.rect.width) ||
        !std::isfinite(box.rect.height) ||
        box.rect.width <= 0.0 ||
        box.rect.height <= 0.0)
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidGeometry);
    }

    auto parameters = K2DrawTextureParametersAbi{};
    parameters.screen_position = {
        box.rect.x,
        box.rect.y,
    };
    parameters.screen_size = {
        box.rect.width,
        box.rect.height,
    };
    parameters.coordinate_position = {0.0, 0.0};
    parameters.coordinate_size = {1.0, 1.0};
    parameters.render_color = encode_color(box.color);
    parameters.blend_mode = CanvasBlendMode::Translucent;
    parameters.rotation = 0.0F;
    parameters.pivot_point = {0.5, 0.5};
    return parameters;
}

auto encode_canvas_texture(const CanvasTextureInput& texture)
    -> std::expected<
        K2DrawTextureParametersAbi,
        CanvasCallCodecError>
{
    if (texture.texture == nullptr ||
        !std::isfinite(texture.rect.x) ||
        !std::isfinite(texture.rect.y) ||
        !std::isfinite(texture.rect.width) ||
        !std::isfinite(texture.rect.height) ||
        texture.rect.width <= 0.0 ||
        texture.rect.height <= 0.0 ||
        !std::isfinite(texture.uv.left) ||
        !std::isfinite(texture.uv.top) ||
        !std::isfinite(texture.uv.right) ||
        !std::isfinite(texture.uv.bottom) ||
        texture.uv.left < 0.0 ||
        texture.uv.top < 0.0 ||
        texture.uv.right > 1.0 ||
        texture.uv.bottom > 1.0 ||
        texture.uv.right <= texture.uv.left ||
        texture.uv.bottom <= texture.uv.top)
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidGeometry);
    }

    auto parameters = K2DrawTextureParametersAbi{};
    parameters.render_texture = texture.texture;
    parameters.screen_position = {
        texture.rect.x,
        texture.rect.y,
    };
    parameters.screen_size = {
        texture.rect.width,
        texture.rect.height,
    };
    parameters.coordinate_position = {
        texture.uv.left,
        texture.uv.top,
    };
    parameters.coordinate_size = {
        texture.uv.right - texture.uv.left,
        texture.uv.bottom - texture.uv.top,
    };
    parameters.render_color = encode_color(texture.tint);
    parameters.blend_mode = CanvasBlendMode::Translucent;
    parameters.rotation = 0.0F;
    parameters.pivot_point = {0.5, 0.5};
    return parameters;
}

auto encode_canvas_text(const CanvasTextInput& text)
    -> std::expected<
        K2DrawTextParametersAbi,
        CanvasCallCodecError>
{
    if (text.font == nullptr || !finite_point(text.anchor))
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidGeometry);
    }
    if (text.text.data == nullptr ||
        text.text.count < 2 ||
        text.text.capacity < text.text.count ||
        text.text.data[text.text.count - 1] != u'\0')
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidText);
    }
    if (!std::isfinite(text.scale) ||
        text.scale < 0.25 ||
        text.scale > 8.0)
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidGeometry);
    }
    if (text.text.count >
            static_cast<std::int32_t>(
                std::numeric_limits<std::uint16_t>::max()))
    {
        return std::unexpected(
            CanvasCallCodecError::InvalidText);
    }

    auto parameters = K2DrawTextParametersAbi{};
    parameters.render_font = text.font;
    parameters.render_text = text.text;
    parameters.screen_position = {
        text.anchor.x,
        text.anchor.y,
    };
    parameters.scale = {text.scale, text.scale};
    parameters.render_color = encode_color(text.color);
    parameters.kerning = 0.0F;
    parameters.shadow_color = {
        0.0F,
        0.0F,
        0.0F,
        0.0F,
    };
    parameters.shadow_offset = {};
    parameters.centre_x = false;
    parameters.centre_y = false;
    parameters.outlined = false;
    parameters.outline_color = {
        0.0F,
        0.0F,
        0.0F,
        0.0F,
    };
    return parameters;
}
} // namespace meccha::runtime
