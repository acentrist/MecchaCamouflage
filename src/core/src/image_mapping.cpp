#include <meccha/core/image_mapping.hpp>

#include <algorithm>
#include <cmath>

namespace meccha::core
{
namespace
{
constexpr double Pi = 3.14159265358979323846;

auto normalized(double value, double minimum, double maximum) -> double
{
    const auto range = maximum - minimum;
    return std::abs(range) <= 0.000001
               ? 0.5
               : std::clamp((value - minimum) / range, 0.0, 1.0);
}

auto safe_pixels_per_unit(double value) -> double
{
    return std::isfinite(value) && value > 0.000001
               ? value
               : 1.0;
}

auto classify_cube_face(double normal_x, double normal_y) -> CubeFace
{
    if (std::abs(normal_x) >= std::abs(normal_y))
    {
        return normal_x >= 0.0 ? CubeFace::Right : CubeFace::Left;
    }
    return normal_y >= 0.0 ? CubeFace::Back : CubeFace::Front;
}
} // namespace

auto map_atlas_coordinate(const AtlasMappingInput& input)
    -> AtlasMappingResult
{
    const auto horizontal =
        input.depth_is_y
            ? normalized(input.local_x, input.min_x, input.max_x)
            : normalized(input.local_y, input.min_y, input.max_y);
    const auto depth =
        input.depth_is_y
            ? normalized(input.local_y, input.min_y, input.max_y)
            : normalized(input.local_x, input.min_x, input.max_x);

    AtlasMappingResult result{};
    result.v = normalized(input.local_z, input.min_z, input.max_z);
    const auto normal_x = std::abs(input.normal_x);
    const auto normal_y = std::abs(input.normal_y);
    const auto normal_z = std::abs(input.normal_z);
    if (input.cube && normal_z > normal_x && normal_z > normal_y)
    {
        const auto centered_x =
            input.local_x - (input.min_x + input.max_x) * 0.5;
        const auto centered_y =
            input.local_y - (input.min_y + input.max_y) * 0.5;
        result.u = std::clamp(
            std::atan2(centered_y, centered_x) / (2.0 * Pi) + 0.5,
            0.0,
            1.0);
        result.v = input.normal_z >= 0.0 ? 0.0 : 1.0;
        result.cube_edge = true;
        return result;
    }
    if (input.region == Region::Side)
    {
        const auto side_coordinate =
            input.depth_is_y ? input.local_x : input.local_y;
        const auto midpoint =
            input.depth_is_y
                ? (input.min_x + input.max_x) * 0.5
                : (input.min_y + input.max_y) * 0.5;
        result.u =
            side_coordinate > midpoint
                ? 0.25 + depth * 0.25
                : 0.75 + (1.0 - depth) * 0.25;
        result.cube_side = input.cube;
    }
    else if (input.region == Region::Back)
    {
        result.u = 0.5 + (1.0 - horizontal) * 0.25;
    }
    else
    {
        result.u = horizontal * 0.25;
    }
    return result;
}

auto map_cube_coordinate(const CubeProjectionInput& input)
    -> CubeProjectionResult
{
    CubeProjectionResult result{};
    result.face =
        classify_cube_face(input.normal_x, input.normal_y);
    const auto scale = safe_pixels_per_unit(input.pixels_per_unit);
    auto horizontal = input.local_x - input.center_x;
    auto tile = 0;
    switch (result.face)
    {
    case CubeFace::Front:
        break;
    case CubeFace::Right:
        tile = 1;
        horizontal = input.local_y - input.center_y;
        break;
    case CubeFace::Back:
        tile = 2;
        horizontal = input.center_x - input.local_x;
        break;
    case CubeFace::Left:
        tile = 3;
        horizontal = input.center_y - input.local_y;
        break;
    }
    result.u =
        (static_cast<double>(tile * CanonicalAtlasTileWidth) +
         static_cast<double>(CanonicalAtlasTileWidth) * 0.5 +
         horizontal * scale) /
        static_cast<double>(CanonicalAtlasWidth);
    result.v =
        0.5 + (input.local_z - input.center_z) * scale /
                  static_cast<double>(CanonicalAtlasHeight);
    return result;
}

auto map_round_coordinate(const RoundProjectionInput& input)
    -> RoundProjectionResult
{
    RoundProjectionResult result{};
    const auto scale = safe_pixels_per_unit(input.pixels_per_unit);
    const auto front_horizontal =
        input.depth_is_y
            ? input.local_x - input.center_x
            : input.local_y - input.center_y;
    auto horizontal = front_horizontal;
    switch (input.region)
    {
    case Region::Front:
        break;
    case Region::Back:
        result.tile = 2;
        horizontal = -front_horizontal;
        break;
    case Region::Side:
    {
        const auto side_coordinate =
            input.depth_is_y
                ? input.local_x - input.center_x
                : input.local_y - input.center_y;
        const auto side_horizontal =
            input.depth_is_y
                ? input.local_y - input.center_y
                : input.local_x - input.center_x;
        result.tile = side_coordinate > 0.0 ? 1 : 3;
        horizontal =
            result.tile == 1 ? side_horizontal : -side_horizontal;
        break;
    }
    }
    result.u =
        (static_cast<double>(
             result.tile * CanonicalAtlasTileWidth) +
         static_cast<double>(CanonicalAtlasTileWidth) * 0.5 +
         horizontal * scale) /
        static_cast<double>(CanonicalAtlasWidth);
    result.v =
        0.5 + (input.local_z - input.center_z) * scale /
                  static_cast<double>(CanonicalAtlasHeight);
    return result;
}
} // namespace meccha::core
