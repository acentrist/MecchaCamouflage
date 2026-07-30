#include <meccha/core/image_profile_mapping.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <stop_token>
#include <utility>

namespace meccha::core
{
namespace
{
constexpr auto GeometryEpsilon = 0.000001;
constexpr auto BarycentricTolerance = 0.0001;

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

auto valid_barycentric(const ImageTriangleAnchor& anchor) -> bool
{
    const auto weight =
        [](double value)
        {
            return std::isfinite(value) &&
                   value >= -BarycentricTolerance &&
                   value <= 1.0 + BarycentricTolerance;
        };
    return weight(anchor.barycentric_a) &&
           weight(anchor.barycentric_b) &&
           weight(anchor.barycentric_c) &&
           std::abs(
               anchor.barycentric_a +
                   anchor.barycentric_b +
                   anchor.barycentric_c -
               1.0) <= BarycentricTolerance;
}

auto coordinate_valid(double value) -> bool
{
    return std::isfinite(value) &&
           value >= -GeometryEpsilon &&
           value <= 1.0 + GeometryEpsilon;
}

auto face_valid(ImageAtlasFace face) -> bool
{
    return face == ImageAtlasFace::Front ||
           face == ImageAtlasFace::Right ||
           face == ImageAtlasFace::Back ||
           face == ImageAtlasFace::Left;
}
} // namespace

auto build_canonical_image_profile(
    ImageReferenceGeometry geometry,
    std::stop_token cancellation)
    -> std::expected<CanonicalImageProfile, ImageProfileMapError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(ImageProfileMapError::Cancelled);
    }
    if (!validate(geometry.identity).empty() ||
        geometry.identity.role != MeshProfileRole::ImageReference)
    {
        return std::unexpected(
            ImageProfileMapError::InvalidProfile);
    }
    if (!geometry.positions || !geometry.indices ||
        geometry.positions->size() !=
            geometry.identity.vertex_count ||
        geometry.indices->size() !=
            geometry.identity.index_count ||
        geometry.positions->empty() ||
        geometry.indices->empty())
    {
        return std::unexpected(
            ImageProfileMapError::InvalidGeometry);
    }

    auto minimum = Vector3d{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    auto maximum = Vector3d{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (auto index = std::size_t{};
         index < geometry.positions->size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(ImageProfileMapError::Cancelled);
        }
        const auto& position = geometry.positions->at(index);
        if (!finite(position))
        {
            return std::unexpected(
                ImageProfileMapError::InvalidGeometry);
        }
        minimum.x = std::min(minimum.x, position.x);
        minimum.y = std::min(minimum.y, position.y);
        minimum.z = std::min(minimum.z, position.z);
        maximum.x = std::max(maximum.x, position.x);
        maximum.y = std::max(maximum.y, position.y);
        maximum.z = std::max(maximum.z, position.z);
    }

    auto maximum_index = std::uint32_t{};
    for (auto position = std::size_t{};
         position < geometry.indices->size();
         ++position)
    {
        if ((position % 1024U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(ImageProfileMapError::Cancelled);
        }
        const auto index = geometry.indices->at(position);
        if (index >= geometry.positions->size())
        {
            return std::unexpected(
                ImageProfileMapError::InvalidGeometry);
        }
        maximum_index = std::max(maximum_index, index);
    }
    if (!geometry.identity.maximum_vertex_index ||
        maximum_index != *geometry.identity.maximum_vertex_index)
    {
        return std::unexpected(
            ImageProfileMapError::InvalidGeometry);
    }

    const auto range_x = maximum.x - minimum.x;
    const auto range_y = maximum.y - minimum.y;
    const auto range_z = maximum.z - minimum.z;
    if (!std::isfinite(range_x) ||
        !std::isfinite(range_y) ||
        !std::isfinite(range_z) ||
        range_z <= GeometryEpsilon)
    {
        return std::unexpected(
            ImageProfileMapError::InvalidGeometry);
    }
    const auto cube =
        geometry.identity.body == BodyProfile::Cube;
    if ((!cube &&
         (range_x <= GeometryEpsilon ||
          range_y <= GeometryEpsilon)) ||
        (cube &&
         std::max(range_x, range_y) <= GeometryEpsilon))
    {
        return std::unexpected(
            ImageProfileMapError::InvalidGeometry);
    }

    const auto horizontal_span = std::max(range_x, range_y);
    const auto pixels_per_unit = std::min(
        (static_cast<double>(CanonicalAtlasTileWidth) - 32.0) /
            horizontal_span,
        (static_cast<double>(CanonicalAtlasHeight) - 64.0) /
            range_z);
    if (!std::isfinite(pixels_per_unit) ||
        pixels_per_unit <= GeometryEpsilon)
    {
        return std::unexpected(
            ImageProfileMapError::InvalidGeometry);
    }
    return CanonicalImageProfile{
        std::move(geometry),
        !cube && range_y > 0.001 && range_y < range_x,
        (minimum.x + maximum.x) * 0.5,
        (minimum.y + maximum.y) * 0.5,
        (minimum.z + maximum.z) * 0.5,
        pixels_per_unit,
    };
}

auto map_image_triangle(
    const CanonicalImageProfile& profile,
    const ImageTriangleAnchor& anchor)
    -> std::expected<CanonicalImageCoordinate, ImageProfileMapError>
{
    const auto& geometry = profile.geometry;
    if (!validate(geometry.identity).empty() ||
        geometry.identity.role != MeshProfileRole::ImageReference)
    {
        return std::unexpected(ImageProfileMapError::InvalidProfile);
    }
    if (!geometry.positions || !geometry.indices ||
        geometry.positions->size() !=
            geometry.identity.vertex_count ||
        geometry.indices->size() != geometry.identity.index_count ||
        !std::isfinite(profile.center_x) ||
        !std::isfinite(profile.center_y) ||
        !std::isfinite(profile.center_z) ||
        !std::isfinite(profile.pixels_per_unit) ||
        profile.pixels_per_unit <= GeometryEpsilon)
    {
        return std::unexpected(ImageProfileMapError::InvalidGeometry);
    }
    if (anchor.triangle_index >=
            geometry.identity.triangle_count ||
        !valid_barycentric(anchor))
    {
        return std::unexpected(ImageProfileMapError::InvalidAnchor);
    }
    const auto base = anchor.triangle_index * 3U;
    if (base + 2U >= geometry.indices->size())
    {
        return std::unexpected(ImageProfileMapError::InvalidAnchor);
    }
    const auto first_index = geometry.indices->at(base);
    const auto second_index = geometry.indices->at(base + 1U);
    const auto third_index = geometry.indices->at(base + 2U);
    if (first_index >= geometry.positions->size() ||
        second_index >= geometry.positions->size() ||
        third_index >= geometry.positions->size())
    {
        return std::unexpected(ImageProfileMapError::InvalidAnchor);
    }
    const auto& first = geometry.positions->at(first_index);
    const auto& second = geometry.positions->at(second_index);
    const auto& third = geometry.positions->at(third_index);
    if (!finite(first) || !finite(second) || !finite(third))
    {
        return std::unexpected(ImageProfileMapError::InvalidAnchor);
    }
    auto normal = cross(
        subtract(second, first),
        subtract(third, first));
    const auto normal_length = length(normal);
    if (!std::isfinite(normal_length) ||
        normal_length <= GeometryEpsilon)
    {
        return std::unexpected(
            ImageProfileMapError::DegenerateTriangle);
    }
    normal.x /= normal_length;
    normal.y /= normal_length;
    normal.z /= normal_length;
    const auto position = Vector3d{
        first.x * anchor.barycentric_a +
            second.x * anchor.barycentric_b +
            third.x * anchor.barycentric_c,
        first.y * anchor.barycentric_a +
            second.y * anchor.barycentric_b +
            third.y * anchor.barycentric_c,
        first.z * anchor.barycentric_a +
            second.z * anchor.barycentric_b +
            third.z * anchor.barycentric_c,
    };
    if (!finite(position))
    {
        return std::unexpected(ImageProfileMapError::InvalidAnchor);
    }

    auto result = CanonicalImageCoordinate{};
    if (geometry.identity.body == BodyProfile::Cube)
    {
        const auto mapped = map_cube_coordinate(CubeProjectionInput{
            position.x,
            position.y,
            position.z,
            normal.x,
            normal.y,
            profile.center_x,
            profile.center_y,
            profile.center_z,
            profile.pixels_per_unit,
        });
        switch (mapped.face)
        {
        case CubeFace::Front:
            result.face = ImageAtlasFace::Front;
            break;
        case CubeFace::Right:
            result.face = ImageAtlasFace::Right;
            break;
        case CubeFace::Back:
            result.face = ImageAtlasFace::Back;
            break;
        case CubeFace::Left:
            result.face = ImageAtlasFace::Left;
            break;
        }
        result.u = mapped.u;
        result.v = mapped.v;
    }
    else
    {
        const auto depth_normal =
            profile.depth_is_y ? normal.y : normal.x;
        const auto region =
            depth_normal <= -0.35
                ? Region::Front
                : (depth_normal >= 0.35
                       ? Region::Back
                       : Region::Side);
        const auto mapped = map_round_coordinate(
            RoundProjectionInput{
                region,
                profile.depth_is_y,
                position.x,
                position.y,
                position.z,
                profile.center_x,
                profile.center_y,
                profile.center_z,
                profile.pixels_per_unit,
            });
        result.face = static_cast<ImageAtlasFace>(mapped.tile);
        result.u = mapped.u;
        result.v = mapped.v;
    }
    if (!face_valid(result.face) ||
        !coordinate_valid(result.u) ||
        !coordinate_valid(result.v))
    {
        return std::unexpected(
            ImageProfileMapError::ProjectionOutsideAtlas);
    }
    result.u = std::clamp(result.u, 0.0, 1.0);
    result.v = std::clamp(result.v, 0.0, 1.0);
    return result;
}
} // namespace meccha::core
