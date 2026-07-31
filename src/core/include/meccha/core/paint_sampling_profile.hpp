#pragma once

#include <meccha/core/image_profile_mapping.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace meccha::core
{
inline constexpr std::size_t MaximumPaintBoneInfluences = 8U;

struct PaintBoneInfluence
{
    std::uint32_t bone{};
    std::uint8_t raw_weight{};
    double weight{};

    auto operator==(const PaintBoneInfluence&) const -> bool = default;
};

struct PaintDeformationVertex
{
    Vector3d position{};
    Vector3d normal{};
    std::array<
        PaintBoneInfluence,
        MaximumPaintBoneInfluences>
        influences{};
    std::size_t influence_count{};

    auto operator==(const PaintDeformationVertex&) const
        -> bool = default;
};

struct PaintDeformationTriangle
{
    std::uint32_t dominant_bone{};
    std::string body_region{};
    Vector3d local_normal{};

    auto operator==(const PaintDeformationTriangle&) const
        -> bool = default;
};

struct PaintQuaternion
{
    double x{};
    double y{};
    double z{};
    double w{1.0};

    auto operator==(const PaintQuaternion&) const -> bool = default;
};

struct PaintReferenceBoneTransform
{
    Vector3d translation{};
    PaintQuaternion rotation{};
    Vector3d scale{1.0, 1.0, 1.0};

    auto operator==(const PaintReferenceBoneTransform&) const
        -> bool = default;
};

struct PaintSamplingVertex
{
    double u{};
    double v{};

    auto operator==(const PaintSamplingVertex&) const -> bool = default;
};

struct PaintSamplingTriangle
{
    std::uint32_t first{};
    std::uint32_t second{};
    std::uint32_t third{};
    std::uint32_t uv_island{};

    auto operator==(const PaintSamplingTriangle&) const -> bool = default;
};

struct PaintSamplingBone
{
    std::string name{};
    std::optional<std::size_t> parent{};

    auto operator==(const PaintSamplingBone&) const -> bool = default;
};

struct PaintSamplingProfile
{
    MeshProfileIdentity identity{};
    std::shared_ptr<const std::vector<PaintSamplingVertex>> vertices{};
    std::shared_ptr<const std::vector<PaintSamplingTriangle>> triangles{};
    std::shared_ptr<const std::vector<PaintSamplingBone>> bones{};
    std::shared_ptr<const std::vector<PaintDeformationVertex>>
        deformation_vertices{};
    std::shared_ptr<const std::vector<PaintDeformationTriangle>>
        deformation_triangles{};
    std::shared_ptr<const std::vector<PaintReferenceBoneTransform>>
        reference_bone_transforms{};
};

enum class PaintSamplingProfileField : std::uint8_t
{
    Identity,
    Vertices,
    Triangles,
    Bones,
    Topology,
    DeformationVertices,
    DeformationTriangles,
    ReferenceBoneTransforms,
    PairIdentity,
    PairTopology,
};

[[nodiscard]] auto validate(const PaintSamplingProfile& profile)
    -> std::vector<PaintSamplingProfileField>;

[[nodiscard]] auto validate_deformation(
    const PaintSamplingProfile& profile)
    -> std::vector<PaintSamplingProfileField>;

[[nodiscard]] auto validate_pair(
    const PaintSamplingProfile& sampling,
    const CanonicalImageProfile& image)
    -> std::vector<PaintSamplingProfileField>;
} // namespace meccha::core
