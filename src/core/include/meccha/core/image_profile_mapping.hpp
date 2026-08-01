#pragma once

#include <meccha/core/image_mapping.hpp>
#include <meccha/core/mesh_profile.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

namespace meccha::core
{
struct Vector3d
{
    double x{};
    double y{};
    double z{};

    auto operator==(const Vector3d&) const -> bool = default;
};

struct ImageReferenceBone
{
    std::optional<std::size_t> parent{};
    Vector3d position{};

    auto operator==(const ImageReferenceBone&) const -> bool = default;
};

struct ImageReferenceGeometry
{
    MeshProfileIdentity identity{};
    std::shared_ptr<const std::vector<Vector3d>> positions{};
    std::shared_ptr<const std::vector<std::uint32_t>> indices{};
    std::shared_ptr<const std::vector<ImageReferenceBone>> bones{};
};

struct CanonicalImageProfile
{
    ImageReferenceGeometry geometry{};
    bool depth_is_y{};
    double center_x{};
    double center_y{};
    double center_z{};
    double pixels_per_unit{};
};

struct ImageTriangleAnchor
{
    std::size_t triangle_index{};
    double barycentric_a{};
    double barycentric_b{};
    double barycentric_c{};

    auto operator==(const ImageTriangleAnchor&) const -> bool = default;
};

struct CanonicalImageCoordinate
{
    ImageAtlasFace face{ImageAtlasFace::Front};
    double u{0.125};
    double v{0.5};

    auto operator==(const CanonicalImageCoordinate&) const -> bool = default;
};

enum class ImageProfileMapError : std::uint8_t
{
    InvalidProfile,
    InvalidGeometry,
    InvalidAnchor,
    DegenerateTriangle,
    ProjectionOutsideAtlas,
    Cancelled,
};

[[nodiscard]] auto build_canonical_image_profile(
    ImageReferenceGeometry geometry,
    std::stop_token cancellation = {})
    -> std::expected<CanonicalImageProfile, ImageProfileMapError>;

[[nodiscard]] auto map_image_triangle(
    const CanonicalImageProfile& profile,
    const ImageTriangleAnchor& anchor)
    -> std::expected<CanonicalImageCoordinate, ImageProfileMapError>;
} // namespace meccha::core
