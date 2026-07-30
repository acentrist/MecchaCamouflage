#include <meccha/core/image_compositor.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto BytesPerPixel = std::uint64_t{4U};

struct DestinationRectangle
{
    double x{};
    double y{};
    double width{};
    double height{};
    bool flip{};
};

auto checked_rgba_bytes(
    std::uint32_t width,
    std::uint32_t height) -> std::optional<std::uint64_t>
{
    if (width == 0U || height == 0U ||
        width > MaximumDecodedImageDimension ||
        height > MaximumDecodedImageDimension)
    {
        return std::nullopt;
    }
    const auto wide_width = static_cast<std::uint64_t>(width);
    const auto wide_height = static_cast<std::uint64_t>(height);
    if (wide_width >
        std::numeric_limits<std::uint64_t>::max() / wide_height)
    {
        return std::nullopt;
    }
    const auto pixels = wide_width * wide_height;
    if (pixels >
        std::numeric_limits<std::uint64_t>::max() / BytesPerPixel)
    {
        return std::nullopt;
    }
    return pixels * BytesPerPixel;
}

auto byte_at(
    const std::vector<std::byte>& bytes,
    std::size_t index) -> std::uint8_t
{
    return std::to_integer<std::uint8_t>(bytes[index]);
}

auto interpolate(double first, double second, double amount) -> double
{
    return first + (second - first) * amount;
}

auto sample(
    const DecodedImageSource& source,
    const NormalizedCrop& crop,
    double u,
    double v) -> std::array<std::uint8_t, 4U>
{
    const auto source_width = static_cast<double>(source.width);
    const auto source_height = static_cast<double>(source.height);
    const auto crop_min_x = crop.x * source_width;
    const auto crop_min_y = crop.y * source_height;
    const auto crop_max_x = std::max(
        crop_min_x,
        (crop.x + crop.width) * source_width - 1.0);
    const auto crop_max_y = std::max(
        crop_min_y,
        (crop.y + crop.height) * source_height - 1.0);
    const auto source_x = std::clamp(
        crop_min_x + u * crop.width * source_width - 0.5,
        crop_min_x,
        crop_max_x);
    const auto source_y = std::clamp(
        crop_min_y + v * crop.height * source_height - 0.5,
        crop_min_y,
        crop_max_y);
    const auto x0 = std::clamp(
        static_cast<std::uint32_t>(std::floor(source_x)),
        0U,
        source.width - 1U);
    const auto y0 = std::clamp(
        static_cast<std::uint32_t>(std::floor(source_y)),
        0U,
        source.height - 1U);
    const auto x1 = std::min(x0 + 1U, source.width - 1U);
    const auto y1 = std::min(y0 + 1U, source.height - 1U);
    const auto amount_x =
        std::clamp(source_x - std::floor(source_x), 0.0, 1.0);
    const auto amount_y =
        std::clamp(source_y - std::floor(source_y), 0.0, 1.0);

    const auto pixel =
        [&source](std::uint32_t x, std::uint32_t y)
        {
            const auto offset =
                (static_cast<std::size_t>(y) * source.width + x) *
                4U;
            const auto alpha =
                static_cast<double>(
                    byte_at(*source.rgba, offset + 3U)) /
                255.0;
            return std::array<double, 4U>{
                static_cast<double>(
                    byte_at(*source.rgba, offset)) *
                    alpha,
                static_cast<double>(
                    byte_at(*source.rgba, offset + 1U)) *
                    alpha,
                static_cast<double>(
                    byte_at(*source.rgba, offset + 2U)) *
                    alpha,
                alpha,
            };
        };
    const auto top_left = pixel(x0, y0);
    const auto top_right = pixel(x1, y0);
    const auto bottom_left = pixel(x0, y1);
    const auto bottom_right = pixel(x1, y1);
    auto premultiplied = std::array<double, 4U>{};
    for (auto channel = std::size_t{};
         channel < premultiplied.size();
         ++channel)
    {
        const auto top = interpolate(
            top_left[channel],
            top_right[channel],
            amount_x);
        const auto bottom = interpolate(
            bottom_left[channel],
            bottom_right[channel],
            amount_x);
        premultiplied[channel] =
            interpolate(top, bottom, amount_y);
    }

    auto result = std::array<std::uint8_t, 4U>{};
    const auto alpha = std::clamp(premultiplied[3U], 0.0, 1.0);
    if (alpha > 0.0)
    {
        for (auto channel = std::size_t{};
             channel < 3U;
             ++channel)
        {
            result[channel] = static_cast<std::uint8_t>(
                std::lround(std::clamp(
                    premultiplied[channel] / alpha,
                    0.0,
                    255.0)));
        }
    }
    result[3U] = static_cast<std::uint8_t>(
        std::lround(alpha * 255.0));
    return result;
}

auto blend(
    std::vector<std::byte>& destination,
    std::size_t offset,
    const std::array<std::uint8_t, 4U>& source) -> bool
{
    const auto source_alpha =
        static_cast<double>(source[3U]) / 255.0;
    if (source_alpha <= 0.0)
    {
        return false;
    }
    const auto destination_alpha =
        static_cast<double>(byte_at(destination, offset + 3U)) /
        255.0;
    const auto output_alpha =
        source_alpha +
        destination_alpha * (1.0 - source_alpha);
    if (output_alpha <= 0.0)
    {
        return false;
    }
    for (auto channel = std::size_t{}; channel < 3U; ++channel)
    {
        const auto source_premultiplied =
            static_cast<double>(source[channel]) * source_alpha;
        const auto destination_premultiplied =
            static_cast<double>(
                byte_at(destination, offset + channel)) *
            destination_alpha;
        const auto output =
            (source_premultiplied +
             destination_premultiplied *
                 (1.0 - source_alpha)) /
            output_alpha;
        destination[offset + channel] = static_cast<std::byte>(
            static_cast<std::uint8_t>(std::lround(
                std::clamp(output, 0.0, 255.0))));
    }
    destination[offset + 3U] = static_cast<std::byte>(
        static_cast<std::uint8_t>(std::lround(
            std::clamp(output_alpha, 0.0, 1.0) * 255.0)));
    return true;
}

auto positive_modulo(double value, double modulus) -> double
{
    auto result = std::fmod(value, modulus);
    if (result < 0.0)
    {
        result += modulus;
    }
    return result;
}

struct RectangleSet
{
    std::array<DestinationRectangle, 4U> values{};
    std::size_t count{};
};

auto layer_rectangles(const ImageLayer& layer) -> RectangleSet
{
    const auto width =
        layer.width * static_cast<double>(CanonicalAtlasWidth);
    const auto height =
        layer.height * static_cast<double>(CanonicalAtlasHeight);
    const auto x =
        layer.center_x *
            static_cast<double>(CanonicalAtlasWidth) -
        width * 0.5;
    const auto y =
        layer.center_y *
            static_cast<double>(CanonicalAtlasHeight) -
        height * 0.5;
    auto result = RectangleSet{};
    result.values[result.count++] =
        DestinationRectangle{x, y, width, height, false};
    if (layer.wrap_atlas_seam)
    {
        result.values[result.count++] = DestinationRectangle{
            x - static_cast<double>(CanonicalAtlasWidth),
            y,
            width,
            height,
            false,
        };
        result.values[result.count++] = DestinationRectangle{
            x + static_cast<double>(CanonicalAtlasWidth),
            y,
            width,
            height,
            false,
        };
    }
    if (layer.mirror_front_back)
    {
        result.values[result.count++] = DestinationRectangle{
            positive_modulo(
                x + static_cast<double>(
                        CanonicalAtlasWidth / 2U),
                static_cast<double>(CanonicalAtlasWidth)),
            y,
            width,
            height,
            true,
        };
    }
    return result;
}

auto clipped_bounds(
    const DestinationRectangle& rectangle)
    -> std::optional<std::array<int, 4U>>
{
    const auto right = rectangle.x + rectangle.width;
    const auto bottom = rectangle.y + rectangle.height;
    if (!std::isfinite(rectangle.x) ||
        !std::isfinite(rectangle.y) ||
        !std::isfinite(right) ||
        !std::isfinite(bottom) ||
        rectangle.width <= 0.0 ||
        rectangle.height <= 0.0)
    {
        return std::nullopt;
    }
    const auto minimum_x = static_cast<int>(std::floor(
        std::clamp(
            rectangle.x,
            0.0,
            static_cast<double>(CanonicalAtlasWidth))));
    const auto minimum_y = static_cast<int>(std::floor(
        std::clamp(
            rectangle.y,
            0.0,
            static_cast<double>(CanonicalAtlasHeight))));
    const auto maximum_x = static_cast<int>(std::ceil(
        std::clamp(
            right,
            0.0,
            static_cast<double>(CanonicalAtlasWidth))));
    const auto maximum_y = static_cast<int>(std::ceil(
        std::clamp(
            bottom,
            0.0,
            static_cast<double>(CanonicalAtlasHeight))));
    return std::array<int, 4U>{
        minimum_x,
        minimum_y,
        maximum_x,
        maximum_y,
    };
}

auto compose_rectangle(
    ImageAtlasComposition& output,
    const ImageProjectSettings& settings,
    const ImageLayer& layer,
    const DecodedImageSource& source,
    const DestinationRectangle& rectangle,
    std::stop_token cancellation)
    -> std::expected<bool, ImageComposeError>
{
    const auto bounds = clipped_bounds(rectangle);
    if (!bounds)
    {
        return std::unexpected(ImageComposeError::InvalidLayer);
    }
    const auto width = static_cast<std::uint64_t>(
        (*bounds)[2U] - (*bounds)[0U]);
    const auto height = static_cast<std::uint64_t>(
        (*bounds)[3U] - (*bounds)[1U]);
    const auto candidates = width * height;
    if (candidates >
        MaximumImageCompositionPixelOperations -
            output.pixel_operations)
    {
        return std::unexpected(ImageComposeError::ResourceLimit);
    }
    output.pixel_operations += candidates;
    if (candidates == 0U)
    {
        return false;
    }

    const auto source_width = std::max(
        1.0,
        static_cast<double>(source.width) * layer.crop.width);
    const auto source_height = std::max(
        1.0,
        static_cast<double>(source.height) * layer.crop.height);
    const auto scale_x = rectangle.width / source_width;
    const auto scale_y = rectangle.height / source_height;
    const auto scale =
        settings.placement == PlacementMode::Fit
            ? std::min(scale_x, scale_y)
            : std::max(scale_x, scale_y);
    const auto draw_width = source_width * scale;
    const auto draw_height = source_height * scale;
    const auto draw_x =
        rectangle.x + (rectangle.width - draw_width) * 0.5;
    const auto draw_y =
        rectangle.y + (rectangle.height - draw_height) * 0.5;
    if (!std::isfinite(draw_width) ||
        !std::isfinite(draw_height) ||
        !std::isfinite(draw_x) ||
        !std::isfinite(draw_y) ||
        draw_width <= 0.0 || draw_height <= 0.0)
    {
        return std::unexpected(ImageComposeError::InvalidLayer);
    }

    auto blended = false;
    for (auto y = (*bounds)[1U]; y < (*bounds)[3U]; ++y)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ImageComposeError::Cancelled);
        }
        const auto center_y = static_cast<double>(y) + 0.5;
        if (center_y < draw_y ||
            center_y >= draw_y + draw_height)
        {
            continue;
        }
        const auto v = (center_y - draw_y) / draw_height;
        for (auto x = (*bounds)[0U]; x < (*bounds)[2U]; ++x)
        {
            const auto center_x = static_cast<double>(x) + 0.5;
            if (center_x < draw_x ||
                center_x >= draw_x + draw_width)
            {
                continue;
            }
            auto u = (center_x - draw_x) / draw_width;
            if (rectangle.flip)
            {
                u = 1.0 - u;
            }
            const auto sampled = sample(source, layer.crop, u, v);
            const auto offset =
                (static_cast<std::size_t>(y) *
                     CanonicalAtlasWidth +
                 static_cast<std::size_t>(x)) *
                4U;
            if (blend(output.rgba, offset, sampled))
            {
                ++output.pixels_blended;
                blended = true;
            }
        }
    }
    ++output.rectangles_composed;
    return blended;
}
} // namespace

auto compose_image_atlas(
    const ImageProjectSettings& settings,
    std::span<const ImageLayer> layers,
    std::span<const DecodedImageSource> sources,
    std::stop_token cancellation)
    -> std::expected<ImageAtlasComposition, ImageComposeError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageComposeError::Cancelled);
    }
    if (!validate(settings).empty())
    {
        return std::unexpected(ImageComposeError::InvalidSettings);
    }
    if (layers.empty())
    {
        return std::unexpected(ImageComposeError::EmptyLayers);
    }
    if (layers.size() > MaximumImageLayers ||
        sources.empty() ||
        sources.size() > MaximumImageSources)
    {
        return std::unexpected(ImageComposeError::ResourceLimit);
    }

    auto referenced_source_bytes = std::uint64_t{};
    for (const auto& item : layers)
    {
        if (!validate(item).empty())
        {
            return std::unexpected(ImageComposeError::InvalidLayer);
        }
        if (item.source_bytes >
            MaximumProjectSourceBytes -
                referenced_source_bytes)
        {
            return std::unexpected(ImageComposeError::ResourceLimit);
        }
        referenced_source_bytes += item.source_bytes;
    }

    auto source_ids = std::set<std::string, std::less<>>{};
    auto decoded_bytes = std::uint64_t{};
    for (const auto& item : sources)
    {
        const auto expected =
            checked_rgba_bytes(item.width, item.height);
        if (item.asset_id.empty() ||
            !source_ids.insert(item.asset_id).second ||
            !expected || *expected > MaximumDecodedImageBytes ||
            !item.rgba || item.rgba->size() != *expected)
        {
            return std::unexpected(ImageComposeError::InvalidSource);
        }
        if (*expected >
            MaximumDecodedProjectBytes - decoded_bytes)
        {
            return std::unexpected(ImageComposeError::ResourceLimit);
        }
        decoded_bytes += *expected;
    }

    for (const auto& item : layers)
    {
        if (!source_ids.contains(item.asset_id))
        {
            return std::unexpected(ImageComposeError::SourceMismatch);
        }
    }
    for (const auto& id : source_ids)
    {
        if (std::ranges::none_of(
                layers,
                [&id](const ImageLayer& item)
                {
                    return item.asset_id == id;
                }))
        {
            return std::unexpected(ImageComposeError::SourceMismatch);
        }
    }

    auto preflight_pixel_operations = std::uint64_t{};
    for (const auto& item : layers)
    {
        const auto rectangles = layer_rectangles(item);
        for (auto index = std::size_t{};
             index < rectangles.count;
             ++index)
        {
            const auto bounds =
                clipped_bounds(rectangles.values[index]);
            if (!bounds)
            {
                return std::unexpected(
                    ImageComposeError::InvalidLayer);
            }
            const auto width = static_cast<std::uint64_t>(
                (*bounds)[2U] - (*bounds)[0U]);
            const auto height = static_cast<std::uint64_t>(
                (*bounds)[3U] - (*bounds)[1U]);
            const auto candidates = width * height;
            if (candidates >
                MaximumImageCompositionPixelOperations -
                    preflight_pixel_operations)
            {
                return std::unexpected(
                    ImageComposeError::ResourceLimit);
            }
            preflight_pixel_operations += candidates;
        }
    }

    auto output = ImageAtlasComposition{
        std::vector<std::byte>(
            CanonicalAtlasByteLength,
            std::byte{}),
        0U,
        0U,
        0U,
        0U,
    };
    for (const auto& item : layers)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ImageComposeError::Cancelled);
        }
        const auto source = std::ranges::find_if(
            sources,
            [&item](const DecodedImageSource& candidate)
            {
                return candidate.asset_id == item.asset_id;
            });
        if (source == sources.end())
        {
            return std::unexpected(ImageComposeError::SourceMismatch);
        }

        const auto rectangles = layer_rectangles(item);

        auto layer_composed = false;
        for (auto index = std::size_t{};
             index < rectangles.count;
             ++index)
        {
            auto composed = compose_rectangle(
                output,
                settings,
                item,
                *source,
                rectangles.values[index],
                cancellation);
            if (!composed)
            {
                return std::unexpected(composed.error());
            }
            layer_composed = layer_composed || *composed;
        }
        if (layer_composed)
        {
            ++output.layers_composed;
        }
    }

    for (auto y = std::uint32_t{};
         y < CanonicalAtlasHeight;
         ++y)
    {
        if (cancellation.stop_requested())
        {
            return std::unexpected(ImageComposeError::Cancelled);
        }
        for (auto x = std::uint32_t{};
             x < CanonicalAtlasWidth;
             ++x)
        {
            const auto offset =
                (static_cast<std::size_t>(y) *
                     CanonicalAtlasWidth +
                 x) *
                4U;
            const auto alpha = byte_at(output.rgba, offset + 3U);
            if (alpha < ImageOpaqueAlphaThreshold)
            {
                if (settings.alpha == AlphaMode::Background)
                {
                    output.rgba[offset] =
                        static_cast<std::byte>(
                            settings.fill_color.red);
                    output.rgba[offset + 1U] =
                        static_cast<std::byte>(
                            settings.fill_color.green);
                    output.rgba[offset + 2U] =
                        static_cast<std::byte>(
                            settings.fill_color.blue);
                    output.rgba[offset + 3U] =
                        static_cast<std::byte>(
                            ImageBackgroundAlphaMarker);
                }
                else
                {
                    output.rgba[offset] = std::byte{};
                    output.rgba[offset + 1U] = std::byte{};
                    output.rgba[offset + 2U] = std::byte{};
                    output.rgba[offset + 3U] = std::byte{};
                }
            }
            else
            {
                output.rgba[offset + 3U] = std::byte{0xFF};
            }
        }
    }
    return output;
}
} // namespace meccha::core
