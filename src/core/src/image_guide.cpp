#include <meccha/core/image_guide.hpp>
#include <meccha/core/png_encoder.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <stop_token>
#include <utility>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto GeometryEpsilon = 0.000001;
constexpr auto SilhouetteAlpha = std::uint8_t{61U};
constexpr auto SkeletonAlpha = std::uint8_t{112U};
constexpr auto JointAlpha = std::uint8_t{148U};
constexpr auto BorderAlpha = std::uint8_t{92U};

struct GuidePoint
{
    double x{};
    double y{};
};

auto finite(const Vector3d& value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

auto subtract(const Vector3d& left, const Vector3d& right)
    -> Vector3d
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

auto cross(const Vector3d& left, const Vector3d& right)
    -> Vector3d
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

auto length(const Vector3d& value) -> double
{
    return std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z);
}

auto cube_face(const Vector3d& normal) -> ImageAtlasFace
{
    if (std::abs(normal.x) >= std::abs(normal.y))
    {
        return normal.x >= 0.0
                   ? ImageAtlasFace::Right
                   : ImageAtlasFace::Left;
    }
    return normal.y >= 0.0
               ? ImageAtlasFace::Back
               : ImageAtlasFace::Front;
}

auto face_tile(ImageAtlasFace face) -> std::uint32_t
{
    return static_cast<std::uint32_t>(face);
}

auto map_to_face(
    const CanonicalImageProfile& profile,
    const Vector3d& position,
    ImageAtlasFace face)
    -> std::expected<GuidePoint, ImageGuideError>
{
    if (!finite(position))
    {
        return std::unexpected(
            ImageGuideError::InvalidGeometry);
    }

    auto horizontal = 0.0;
    if (profile.geometry.identity.body == BodyProfile::Cube)
    {
        switch (face)
        {
        case ImageAtlasFace::Front:
            horizontal = position.x - profile.center_x;
            break;
        case ImageAtlasFace::Right:
            horizontal = position.y - profile.center_y;
            break;
        case ImageAtlasFace::Back:
            horizontal = profile.center_x - position.x;
            break;
        case ImageAtlasFace::Left:
            horizontal = profile.center_y - position.y;
            break;
        }
    }
    else
    {
        horizontal =
            profile.depth_is_y
                ? position.x - profile.center_x
                : position.y - profile.center_y;
        if (face == ImageAtlasFace::Right)
        {
            horizontal =
                profile.depth_is_y
                    ? position.y - profile.center_y
                    : position.x - profile.center_x;
        }
        else if (face == ImageAtlasFace::Back)
        {
            horizontal = -horizontal;
        }
        else if (face == ImageAtlasFace::Left)
        {
            horizontal =
                profile.depth_is_y
                    ? profile.center_y - position.y
                    : profile.center_x - position.x;
        }
    }

    const auto u =
        (static_cast<double>(
             face_tile(face) * CanonicalAtlasTileWidth) +
         static_cast<double>(CanonicalAtlasTileWidth) * 0.5 +
         horizontal * profile.pixels_per_unit) /
        static_cast<double>(CanonicalAtlasWidth);
    const auto v =
        0.5 +
        (position.z - profile.center_z) *
            profile.pixels_per_unit /
            static_cast<double>(CanonicalAtlasHeight);
    const auto tile_minimum =
        static_cast<double>(
            face_tile(face) * CanonicalAtlasTileWidth) /
        static_cast<double>(CanonicalAtlasWidth);
    const auto tile_maximum =
        static_cast<double>(
            (face_tile(face) + 1U) *
            CanonicalAtlasTileWidth) /
        static_cast<double>(CanonicalAtlasWidth);
    if (!std::isfinite(u) || !std::isfinite(v) ||
        u < tile_minimum - GeometryEpsilon ||
        u > tile_maximum + GeometryEpsilon ||
        v < -GeometryEpsilon ||
        v > 1.0 + GeometryEpsilon)
    {
        return std::unexpected(
            ImageGuideError::InvalidGeometry);
    }
    return GuidePoint{
        std::clamp(u, tile_minimum, tile_maximum) *
            static_cast<double>(CanonicalAtlasWidth - 1U),
        (1.0 - std::clamp(v, 0.0, 1.0)) *
            static_cast<double>(CanonicalAtlasHeight - 1U),
    };
}

auto blend_white(
    std::vector<std::byte>& rgba,
    int x,
    int y,
    std::uint8_t alpha) -> void
{
    if (x < 0 || y < 0 ||
        x >= static_cast<int>(CanonicalAtlasWidth) ||
        y >= static_cast<int>(CanonicalAtlasHeight))
    {
        return;
    }
    const auto offset =
        (static_cast<std::size_t>(y) *
             CanonicalAtlasWidth +
         static_cast<std::size_t>(x)) *
        4U;
    const auto destination =
        std::to_integer<std::uint8_t>(rgba[offset + 3U]);
    const auto combined = static_cast<std::uint8_t>(
        alpha +
        (static_cast<unsigned int>(destination) *
         (255U - alpha) +
         127U) /
            255U);
    rgba[offset] = std::byte{0xFF};
    rgba[offset + 1U] = std::byte{0xFF};
    rgba[offset + 2U] = std::byte{0xFF};
    rgba[offset + 3U] =
        static_cast<std::byte>(combined);
}

auto edge(
    const GuidePoint& first,
    const GuidePoint& second,
    double x,
    double y) -> double
{
    return (x - first.x) * (second.y - first.y) -
           (y - first.y) * (second.x - first.x);
}

auto fill_triangle(
    std::vector<std::byte>& rgba,
    const std::array<GuidePoint, 3U>& points,
    std::uint64_t& operations,
    std::stop_token cancellation)
    -> std::expected<bool, ImageGuideError>
{
    const auto area =
        edge(points[0U], points[1U], points[2U].x, points[2U].y);
    if (!std::isfinite(area))
    {
        return std::unexpected(
            ImageGuideError::InvalidGeometry);
    }
    if (std::abs(area) <= GeometryEpsilon)
    {
        return false;
    }

    const auto minimum_x = std::max(
        0,
        static_cast<int>(std::floor(std::min(
            {points[0U].x, points[1U].x, points[2U].x}))));
    const auto maximum_x = std::min(
        static_cast<int>(CanonicalAtlasWidth) - 1,
        static_cast<int>(std::ceil(std::max(
            {points[0U].x, points[1U].x, points[2U].x}))));
    const auto minimum_y = std::max(
        0,
        static_cast<int>(std::floor(std::min(
            {points[0U].y, points[1U].y, points[2U].y}))));
    const auto maximum_y = std::min(
        static_cast<int>(CanonicalAtlasHeight) - 1,
        static_cast<int>(std::ceil(std::max(
            {points[0U].y, points[1U].y, points[2U].y}))));
    if (minimum_x > maximum_x || minimum_y > maximum_y)
    {
        return std::unexpected(
            ImageGuideError::InvalidGeometry);
    }

    const auto positive = area > 0.0;
    for (auto y = minimum_y; y <= maximum_y; ++y)
    {
        if ((y & 31) == 0 && cancellation.stop_requested())
        {
            return std::unexpected(ImageGuideError::Cancelled);
        }
        const auto row_width =
            static_cast<std::uint64_t>(
                maximum_x - minimum_x + 1);
        if (operations >
            MaximumImageGuidePixelOperations - row_width)
        {
            return std::unexpected(
                ImageGuideError::ResourceLimit);
        }
        operations += row_width;
        for (auto x = minimum_x; x <= maximum_x; ++x)
        {
            const auto sample_x =
                static_cast<double>(x) + 0.5;
            const auto sample_y =
                static_cast<double>(y) + 0.5;
            const auto first = edge(
                points[0U],
                points[1U],
                sample_x,
                sample_y);
            const auto second = edge(
                points[1U],
                points[2U],
                sample_x,
                sample_y);
            const auto third = edge(
                points[2U],
                points[0U],
                sample_x,
                sample_y);
            if (positive
                    ? first >= -GeometryEpsilon &&
                          second >= -GeometryEpsilon &&
                          third >= -GeometryEpsilon
                    : first <= GeometryEpsilon &&
                          second <= GeometryEpsilon &&
                          third <= GeometryEpsilon)
            {
                blend_white(
                    rgba,
                    x,
                    y,
                    SilhouetteAlpha);
            }
        }
    }
    return true;
}

auto draw_disc(
    std::vector<std::byte>& rgba,
    const GuidePoint& point,
    int radius,
    std::uint8_t alpha) -> void
{
    const auto center_x = static_cast<int>(std::lround(point.x));
    const auto center_y = static_cast<int>(std::lround(point.y));
    for (auto y = -radius; y <= radius; ++y)
    {
        for (auto x = -radius; x <= radius; ++x)
        {
            if (x * x + y * y <= radius * radius)
            {
                blend_white(
                    rgba,
                    center_x + x,
                    center_y + y,
                    alpha);
            }
        }
    }
}

auto draw_line(
    std::vector<std::byte>& rgba,
    const GuidePoint& first,
    const GuidePoint& second,
    std::uint8_t alpha) -> void
{
    auto x0 = static_cast<int>(std::lround(first.x));
    auto y0 = static_cast<int>(std::lround(first.y));
    const auto x1 = static_cast<int>(std::lround(second.x));
    const auto y1 = static_cast<int>(std::lround(second.y));
    const auto delta_x = std::abs(x1 - x0);
    const auto step_x = x0 < x1 ? 1 : -1;
    const auto delta_y = -std::abs(y1 - y0);
    const auto step_y = y0 < y1 ? 1 : -1;
    auto error = delta_x + delta_y;
    while (true)
    {
        blend_white(rgba, x0, y0, alpha);
        blend_white(rgba, x0 + 1, y0, alpha);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        const auto doubled = 2 * error;
        if (doubled >= delta_y)
        {
            error += delta_y;
            x0 += step_x;
        }
        if (doubled <= delta_x)
        {
            error += delta_x;
            y0 += step_y;
        }
    }
}

auto valid_bones(const ImageReferenceGeometry& geometry) -> bool
{
    if (!geometry.bones ||
        geometry.bones->size() != geometry.identity.bone_count ||
        geometry.bones->size() !=
            geometry.identity.reference_pose_bone_count)
    {
        return false;
    }
    for (auto index = std::size_t{};
         index < geometry.bones->size();
         ++index)
    {
        const auto& bone = geometry.bones->at(index);
        if (!finite(bone.position) ||
            (bone.parent && *bone.parent >= index))
        {
            return false;
        }
    }
    return true;
}
} // namespace

auto build_image_guide_bitmap(
    const CanonicalImageProfile& profile,
    std::stop_token cancellation)
    -> std::expected<ImageGuideBitmap, ImageGuideError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageGuideError::Cancelled);
    }
    const auto& geometry = profile.geometry;
    if (!validate(geometry.identity).empty() ||
        geometry.identity.role !=
            MeshProfileRole::ImageReference)
    {
        return std::unexpected(
            ImageGuideError::InvalidProfile);
    }
    if (!geometry.positions || !geometry.indices ||
        geometry.positions->size() !=
            geometry.identity.vertex_count ||
        geometry.indices->size() !=
            geometry.identity.index_count ||
        geometry.indices->size() % 3U != 0U ||
        !valid_bones(geometry) ||
        !std::isfinite(profile.center_x) ||
        !std::isfinite(profile.center_y) ||
        !std::isfinite(profile.center_z) ||
        !std::isfinite(profile.pixels_per_unit) ||
        profile.pixels_per_unit <= GeometryEpsilon)
    {
        return std::unexpected(
            ImageGuideError::InvalidGeometry);
    }

    auto rgba = std::vector<std::byte>(
        CanonicalAtlasByteLength,
        std::byte{});
    auto operations = std::uint64_t{};
    auto projected_triangles = std::size_t{};
    constexpr auto Faces = std::array{
        ImageAtlasFace::Front,
        ImageAtlasFace::Right,
        ImageAtlasFace::Back,
        ImageAtlasFace::Left,
    };
    for (auto triangle = std::size_t{};
         triangle < geometry.identity.triangle_count;
         ++triangle)
    {
        if ((triangle % 64U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(ImageGuideError::Cancelled);
        }
        const auto base = triangle * 3U;
        const auto first_index = geometry.indices->at(base);
        const auto second_index = geometry.indices->at(base + 1U);
        const auto third_index = geometry.indices->at(base + 2U);
        if (first_index >= geometry.positions->size() ||
            second_index >= geometry.positions->size() ||
            third_index >= geometry.positions->size())
        {
            return std::unexpected(
                ImageGuideError::InvalidGeometry);
        }
        const auto& first = geometry.positions->at(first_index);
        const auto& second = geometry.positions->at(second_index);
        const auto& third = geometry.positions->at(third_index);
        const auto normal = cross(
            subtract(second, first),
            subtract(third, first));
        const auto normal_length = length(normal);
        if (!std::isfinite(normal_length) ||
            normal_length <= GeometryEpsilon)
        {
            continue;
        }

        const auto render_face =
            [&](ImageAtlasFace face)
                -> std::expected<void, ImageGuideError>
            {
                const auto mapped_first =
                    map_to_face(profile, first, face);
                const auto mapped_second =
                    map_to_face(profile, second, face);
                const auto mapped_third =
                    map_to_face(profile, third, face);
                if (!mapped_first || !mapped_second ||
                    !mapped_third)
                {
                    return std::unexpected(
                        ImageGuideError::InvalidGeometry);
                }
                const auto filled = fill_triangle(
                    rgba,
                    {*mapped_first, *mapped_second, *mapped_third},
                    operations,
                    cancellation);
                if (!filled)
                {
                    return std::unexpected(filled.error());
                }
                if (*filled)
                {
                    ++projected_triangles;
                }
                return {};
            };
        if (geometry.identity.body == BodyProfile::Cube)
        {
            const auto rendered =
                render_face(cube_face(normal));
            if (!rendered)
            {
                return std::unexpected(rendered.error());
            }
        }
        else
        {
            for (const auto face : Faces)
            {
                const auto rendered = render_face(face);
                if (!rendered)
                {
                    return std::unexpected(
                        rendered.error());
                }
            }
        }
    }
    if (projected_triangles == 0U)
    {
        return std::unexpected(
            ImageGuideError::InvalidGeometry);
    }

    auto bone_segments = std::size_t{};
    for (const auto face : Faces)
    {
        for (auto index = std::size_t{};
             index < geometry.bones->size();
             ++index)
        {
            if ((index % 16U) == 0U &&
                cancellation.stop_requested())
            {
                return std::unexpected(
                    ImageGuideError::Cancelled);
            }
            const auto& bone = geometry.bones->at(index);
            const auto point =
                map_to_face(profile, bone.position, face);
            if (!point)
            {
                return std::unexpected(point.error());
            }
            draw_disc(rgba, *point, 2, JointAlpha);
            if (!bone.parent)
            {
                continue;
            }
            const auto parent = map_to_face(
                profile,
                geometry.bones->at(*bone.parent).position,
                face);
            if (!parent)
            {
                return std::unexpected(parent.error());
            }
            draw_line(
                rgba,
                *parent,
                *point,
                SkeletonAlpha);
            ++bone_segments;
        }
    }

    for (auto face = std::uint32_t{};
         face < 4U;
         ++face)
    {
        const auto left =
            static_cast<int>(
                face * CanonicalAtlasTileWidth) +
            1;
        const auto right =
            static_cast<int>(
                (face + 1U) *
                CanonicalAtlasTileWidth) -
            2;
        const auto top = 1;
        const auto bottom =
            static_cast<int>(CanonicalAtlasHeight) - 2;
        draw_line(
            rgba,
            GuidePoint{
                static_cast<double>(left),
                static_cast<double>(top),
            },
            GuidePoint{
                static_cast<double>(right),
                static_cast<double>(top),
            },
            BorderAlpha);
        draw_line(
            rgba,
            GuidePoint{
                static_cast<double>(right),
                static_cast<double>(top),
            },
            GuidePoint{
                static_cast<double>(right),
                static_cast<double>(bottom),
            },
            BorderAlpha);
        draw_line(
            rgba,
            GuidePoint{
                static_cast<double>(right),
                static_cast<double>(bottom),
            },
            GuidePoint{
                static_cast<double>(left),
                static_cast<double>(bottom),
            },
            BorderAlpha);
        draw_line(
            rgba,
            GuidePoint{
                static_cast<double>(left),
                static_cast<double>(bottom),
            },
            GuidePoint{
                static_cast<double>(left),
                static_cast<double>(top),
            },
            BorderAlpha);
    }

    auto encoded_png = encode_png_rgba8(
        CanonicalAtlasWidth,
        CanonicalAtlasHeight,
        rgba,
        cancellation);
    if (!encoded_png)
    {
        return std::unexpected(
            encoded_png.error() == PngEncodeError::Cancelled
                ? ImageGuideError::Cancelled
                : ImageGuideError::ResourceLimit);
    }
    return ImageGuideBitmap{
        ImageGuideSchemaVersion,
        geometry.identity,
        CanonicalAtlasWidth,
        CanonicalAtlasHeight,
        std::make_shared<const std::vector<std::byte>>(
            std::move(rgba)),
        projected_triangles,
        bone_segments,
        std::make_shared<const std::vector<std::byte>>(
            std::move(*encoded_png)),
    };
}
} // namespace meccha::core
