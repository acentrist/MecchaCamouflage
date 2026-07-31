#pragma once

#include <meccha/core/paint_sampling_profile.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
struct DeformedPaintVertex
{
    Vector3d position{};
    Vector3d normal{};

    auto operator==(const DeformedPaintVertex&) const
        -> bool = default;
};

enum class PaintDeformationError : std::uint8_t
{
    InvalidSkeleton,
    InvalidVertex,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto compose_reference_bone_transforms(
    std::span<const PaintSamplingBone> bones,
    std::span<const PaintReferenceBoneTransform>
        reference_local_transforms)
    -> std::expected<
        std::vector<PaintReferenceBoneTransform>,
        PaintDeformationError>;

[[nodiscard]] auto deform_paint_vertices(
    std::span<const PaintDeformationVertex> vertices,
    std::span<const PaintSamplingBone> bones,
    std::span<const PaintReferenceBoneTransform>
        reference_local_transforms,
    std::span<const PaintReferenceBoneTransform>
        current_world_transforms,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<DeformedPaintVertex>,
        PaintDeformationError>;
} // namespace meccha::core
