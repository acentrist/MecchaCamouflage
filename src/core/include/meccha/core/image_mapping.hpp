#pragma once

#include <meccha/core/paint.hpp>

#include <cstdint>

namespace meccha::core
{
inline constexpr std::uint32_t CanonicalAtlasWidth = 1024U;
inline constexpr std::uint32_t CanonicalAtlasHeight = 512U;
inline constexpr std::uint32_t CanonicalAtlasTileWidth = 256U;

enum class ImageAtlasFace : std::uint8_t
{
    Front,
    Right,
    Back,
    Left,
};

struct AtlasMappingInput
{
    bool cube{};
    Region region{Region::Front};
    bool depth_is_y{};
    double local_x{};
    double local_y{};
    double local_z{};
    double normal_x{};
    double normal_y{};
    double normal_z{};
    double min_x{};
    double max_x{1.0};
    double min_y{};
    double max_y{1.0};
    double min_z{};
    double max_z{1.0};
};

struct AtlasMappingResult
{
    double u{0.125};
    double v{0.5};
    bool cube_side{};
    bool cube_edge{};
};

[[nodiscard]] auto map_atlas_coordinate(const AtlasMappingInput& input)
    -> AtlasMappingResult;

enum class CubeFace : std::uint8_t
{
    Front,
    Right,
    Back,
    Left,
};

struct CubeProjectionInput
{
    double local_x{};
    double local_y{};
    double local_z{};
    double normal_x{};
    double normal_y{-1.0};
    double center_x{};
    double center_y{};
    double center_z{};
    double pixels_per_unit{1.0};
};

struct CubeProjectionResult
{
    CubeFace face{CubeFace::Front};
    double u{0.125};
    double v{0.5};
};

[[nodiscard]] auto map_cube_coordinate(const CubeProjectionInput& input)
    -> CubeProjectionResult;

struct RoundProjectionInput
{
    Region region{Region::Front};
    bool depth_is_y{true};
    double local_x{};
    double local_y{};
    double local_z{};
    double center_x{};
    double center_y{};
    double center_z{};
    double pixels_per_unit{1.0};
};

struct RoundProjectionResult
{
    int tile{};
    double u{0.125};
    double v{0.5};
};

[[nodiscard]] auto map_round_coordinate(const RoundProjectionInput& input)
    -> RoundProjectionResult;
} // namespace meccha::core
