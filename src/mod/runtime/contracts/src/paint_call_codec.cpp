#include <meccha/runtime/paint_call_codec.hpp>

#include <algorithm>
#include <cmath>
#include <expected>

namespace meccha::runtime
{
namespace
{
constexpr std::uint32_t MaximumTextureDimension = 4096U;

auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
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
} // namespace

auto encode_paint_call(
    const application::PaintAtUvWithBrush& request)
    -> std::expected<
        PaintAtUvWithBrushParameters,
        PaintCallEncodingError>
{
    if (request.request_id == 0U ||
        request.job_generation == 0U ||
        !request.component.valid())
    {
        return std::unexpected(
            PaintCallEncodingError::InvalidRequest);
    }
    if (request.texture_dimension == 0U ||
        request.texture_dimension > MaximumTextureDimension)
    {
        return std::unexpected(
            PaintCallEncodingError::InvalidDimension);
    }
    if (!unit(request.u) || !unit(request.v))
    {
        return std::unexpected(
            PaintCallEncodingError::InvalidCoordinates);
    }
    if (!std::isfinite(request.brush_size_texels) ||
        request.brush_size_texels < 1.0 ||
        request.brush_size_texels >
            static_cast<double>(request.texture_dimension))
    {
        return std::unexpected(
            PaintCallEncodingError::InvalidBrush);
    }
    if (!unit(request.material.metallic) ||
        !unit(request.material.roughness) ||
        !unit(request.material.emissive))
    {
        return std::unexpected(
            PaintCallEncodingError::InvalidMaterial);
    }

    auto parameters = PaintAtUvWithBrushParameters{};
    parameters.uv = RuntimeVector2d{request.u, request.v};
    parameters.channel_data.albedo = RuntimeLinearColor{
        srgb_to_linear(request.color.red),
        srgb_to_linear(request.color.green),
        srgb_to_linear(request.color.blue),
        1.0F,
    };
    parameters.channel_data.metallic =
        static_cast<float>(request.material.metallic);
    parameters.channel_data.roughness =
        static_cast<float>(request.material.roughness);
    parameters.channel_data.height = 0.0F;
    parameters.channel_data.emissive =
        static_cast<float>(request.material.emissive);
    parameters.channel_data.apply_mode =
        PaintChannelApplyMode::Override;
    parameters.brush_settings.radius =
        static_cast<float>(
            request.brush_size_texels /
            static_cast<double>(request.texture_dimension));
    parameters.brush_settings.hardness = 1.0F;
    parameters.brush_settings.opacity = 1.0F;
    parameters.brush_settings.spacing = 1.0F;
    parameters.brush_settings.falloff =
        RuntimeBrushFalloff::Spherical;
    parameters.brush_settings.blend_mode =
        RuntimePaintBlendMode::Normal;
    parameters.brush_settings.brush_texture = nullptr;
    parameters.brush_settings.rotation = 0.0F;
    parameters.channel =
        RuntimePaintChannel::
            AlbedoMetallicRoughnessEmissive;
    return parameters;
}
} // namespace meccha::runtime
