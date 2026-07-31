#include <meccha/runtime/paint_capture_codec.hpp>

#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <vector>

namespace meccha::runtime
{
namespace
{
auto valid_color(const PaintCaptureLinearColor& color) -> bool
{
    return std::isfinite(color.red) &&
           std::isfinite(color.green) &&
           std::isfinite(color.blue) &&
           std::isfinite(color.alpha);
}
} // namespace

auto decode_runtime_transform(
    const RuntimeTransform& transform)
    -> std::expected<
        core::PaintReferenceBoneTransform,
        PaintCaptureEncodingError>
{
    const auto& rotation = transform.rotation;
    const auto& translation = transform.translation;
    const auto& scale = transform.scale;
    const auto rotation_length = std::sqrt(
        rotation.x * rotation.x +
        rotation.y * rotation.y +
        rotation.z * rotation.z +
        rotation.w * rotation.w);
    if (!std::isfinite(rotation_length) ||
        rotation_length <= 1.0e-12 ||
        !std::isfinite(translation.x) ||
        !std::isfinite(translation.y) ||
        !std::isfinite(translation.z) ||
        !std::isfinite(scale.x) ||
        !std::isfinite(scale.y) ||
        !std::isfinite(scale.z) ||
        std::abs(scale.x) <= 1.0e-12 ||
        std::abs(scale.y) <= 1.0e-12 ||
        std::abs(scale.z) <= 1.0e-12)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidTransform);
    }
    return core::PaintReferenceBoneTransform{
        core::Vector3d{
            translation.x,
            translation.y,
            translation.z,
        },
        core::PaintQuaternion{
            rotation.x / rotation_length,
            rotation.y / rotation_length,
            rotation.z / rotation_length,
            rotation.w / rotation_length,
        },
        core::Vector3d{scale.x, scale.y, scale.z},
    };
}

auto encode_create_paint_capture_render_target(
    const PaintCaptureRenderTargetInput& input)
    -> std::expected<
        CreatePaintCaptureRenderTargetParameters,
        PaintCaptureEncodingError>
{
    if (input.world_context_object == nullptr ||
        input.width == 0U || input.height == 0U ||
        input.width > core::MaximumPaintCaptureDimension ||
        input.height > core::MaximumPaintCaptureDimension ||
        (input.format !=
             PaintCaptureRenderTargetFormat::Rgba8Srgb &&
         input.format !=
             PaintCaptureRenderTargetFormat::Rgba16Float) ||
        input.width >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()) ||
        input.height >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()))
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidRenderTarget);
    }
    return CreatePaintCaptureRenderTargetParameters{
        input.world_context_object,
        static_cast<std::int32_t>(input.width),
        static_cast<std::int32_t>(input.height),
        1,
        input.format,
        {},
        PaintCaptureLinearColor{},
        false,
        false,
        {},
        nullptr,
    };
}

auto encode_read_paint_capture_render_target(
    void* world_context_object,
    void* texture_render_target,
    bool normalize)
    -> std::expected<
        ReadPaintCaptureRenderTargetParameters,
        PaintCaptureEncodingError>
{
    if (world_context_object == nullptr ||
        texture_render_target == nullptr)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidRenderTarget);
    }
    return ReadPaintCaptureRenderTargetParameters{
        world_context_object,
        texture_render_target,
        {},
        normalize,
        false,
        {},
    };
}

auto decode_paint_capture_linear_colors(
    const ReadPaintCaptureRenderTargetParameters& parameters,
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<
        std::vector<PaintCaptureLinearColor>,
        PaintCaptureEncodingError>
{
    if (!parameters.return_value ||
        width == 0U || height == 0U ||
        width > core::MaximumPaintCaptureDimension ||
        height > core::MaximumPaintCaptureDimension ||
        width > std::numeric_limits<std::size_t>::max() /
                    height)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidReadback);
    }
    const auto expected_count =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);
    const auto& array = parameters.out_linear_samples;
    if (array.data == nullptr || array.count < 0 ||
        array.capacity < array.count ||
        static_cast<std::size_t>(array.count) !=
            expected_count)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidReadback);
    }
    auto output = std::vector<PaintCaptureLinearColor>{
        array.data,
        array.data + expected_count};
    for (const auto& color : output)
    {
        if (!valid_color(color))
        {
            return std::unexpected(
                PaintCaptureEncodingError::InvalidReadback);
        }
    }
    return output;
}
} // namespace meccha::runtime
