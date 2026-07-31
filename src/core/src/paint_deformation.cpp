#include <meccha/core/paint_deformation.hpp>

#include <meccha/core/paint.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto TransformEpsilon = 1.0e-12;
constexpr auto MaximumPaintDeformationBones = 256U;

auto finite(Vector3d value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

auto finite(PaintQuaternion value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z) &&
           std::isfinite(value.w);
}

auto add(Vector3d left, Vector3d right) -> Vector3d
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

auto subtract(Vector3d left, Vector3d right) -> Vector3d
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

auto multiply(Vector3d value, double scalar) -> Vector3d
{
    return {
        value.x * scalar,
        value.y * scalar,
        value.z * scalar,
    };
}

auto component_multiply(Vector3d left, Vector3d right)
    -> Vector3d
{
    return {
        left.x * right.x,
        left.y * right.y,
        left.z * right.z,
    };
}

auto component_divide(Vector3d left, Vector3d right)
    -> Vector3d
{
    return {
        left.x / right.x,
        left.y / right.y,
        left.z / right.z,
    };
}

auto length_squared(Vector3d value) -> double
{
    return value.x * value.x +
           value.y * value.y +
           value.z * value.z;
}

auto normalized(Vector3d value) -> Vector3d
{
    const auto length = std::sqrt(length_squared(value));
    return length > TransformEpsilon
               ? multiply(value, 1.0 / length)
               : Vector3d{};
}

auto normalized(PaintQuaternion value) -> PaintQuaternion
{
    const auto length = std::sqrt(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z +
        value.w * value.w);
    return length > TransformEpsilon
               ? PaintQuaternion{
                     value.x / length,
                     value.y / length,
                     value.z / length,
                     value.w / length,
                 }
               : PaintQuaternion{};
}

auto conjugate(PaintQuaternion value) -> PaintQuaternion
{
    return {-value.x, -value.y, -value.z, value.w};
}

auto multiply(
    PaintQuaternion left,
    PaintQuaternion right) -> PaintQuaternion
{
    return normalized(PaintQuaternion{
        left.w * right.x + left.x * right.w +
            left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z +
            left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y -
            left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x -
            left.y * right.y - left.z * right.z,
    });
}

auto rotate(PaintQuaternion rotation, Vector3d value)
    -> Vector3d
{
    rotation = normalized(rotation);
    const auto axis = Vector3d{
        rotation.x,
        rotation.y,
        rotation.z,
    };
    const auto twice_cross = multiply(
        Vector3d{
            axis.y * value.z - axis.z * value.y,
            axis.z * value.x - axis.x * value.z,
            axis.x * value.y - axis.y * value.x,
        },
        2.0);
    const auto axis_cross_twice = Vector3d{
        axis.y * twice_cross.z -
            axis.z * twice_cross.y,
        axis.z * twice_cross.x -
            axis.x * twice_cross.z,
        axis.x * twice_cross.y -
            axis.y * twice_cross.x,
    };
    return add(
        add(value, multiply(twice_cross, rotation.w)),
        axis_cross_twice);
}

auto valid_transform(const PaintReferenceBoneTransform& value)
    -> bool
{
    const auto rotation_length_squared =
        value.rotation.x * value.rotation.x +
        value.rotation.y * value.rotation.y +
        value.rotation.z * value.rotation.z +
        value.rotation.w * value.rotation.w;
    return finite(value.translation) &&
           finite(value.rotation) &&
           finite(value.scale) &&
           std::isfinite(rotation_length_squared) &&
           rotation_length_squared > TransformEpsilon &&
           std::abs(value.scale.x) > TransformEpsilon &&
           std::abs(value.scale.y) > TransformEpsilon &&
           std::abs(value.scale.z) > TransformEpsilon;
}

auto transform_point(
    const PaintReferenceBoneTransform& transform,
    Vector3d point) -> Vector3d
{
    return add(
        rotate(
            transform.rotation,
            component_multiply(point, transform.scale)),
        transform.translation);
}

auto inverse_transform_point(
    const PaintReferenceBoneTransform& transform,
    Vector3d point) -> Vector3d
{
    return component_divide(
        rotate(
            conjugate(normalized(transform.rotation)),
            subtract(point, transform.translation)),
        transform.scale);
}

auto compose(
    const PaintReferenceBoneTransform& parent,
    const PaintReferenceBoneTransform& local)
    -> PaintReferenceBoneTransform
{
    return PaintReferenceBoneTransform{
        transform_point(parent, local.translation),
        multiply(parent.rotation, local.rotation),
        component_multiply(parent.scale, local.scale),
    };
}

auto vertex_valid(
    const PaintDeformationVertex& vertex,
    std::size_t bone_count) -> bool
{
    if (!finite(vertex.position) ||
        !finite(vertex.normal) ||
        length_squared(vertex.normal) <= TransformEpsilon ||
        vertex.influence_count == 0U ||
        vertex.influence_count >
            MaximumPaintBoneInfluences)
    {
        return false;
    }
    auto raw_sum = std::uint32_t{};
    auto weight_sum = 0.0;
    for (auto index = std::size_t{};
         index < vertex.influence_count;
         ++index)
    {
        const auto& influence = vertex.influences[index];
        if (influence.bone >= bone_count ||
            influence.raw_weight == 0U ||
            !std::isfinite(influence.weight) ||
            influence.weight <= 0.0 ||
            influence.weight > 1.0)
        {
            return false;
        }
        for (auto previous = std::size_t{};
             previous < index;
             ++previous)
        {
            if (vertex.influences[previous].bone ==
                influence.bone)
            {
                return false;
            }
        }
        raw_sum += influence.raw_weight;
        weight_sum += influence.weight;
    }
    return raw_sum == 255U &&
           std::abs(weight_sum - 1.0) <= 1.0e-4;
}
} // namespace

auto compose_reference_bone_transforms(
    std::span<const PaintSamplingBone> bones,
    std::span<const PaintReferenceBoneTransform>
        reference_local_transforms)
    -> std::expected<
        std::vector<PaintReferenceBoneTransform>,
        PaintDeformationError>
{
    if (bones.empty() ||
        bones.size() > MaximumPaintDeformationBones ||
        reference_local_transforms.size() != bones.size())
    {
        return std::unexpected(
            PaintDeformationError::InvalidSkeleton);
    }
    auto reference_component =
        std::vector<PaintReferenceBoneTransform>{};
    reference_component.reserve(bones.size());
    for (auto index = std::size_t{};
         index < bones.size();
         ++index)
    {
        const auto& bone = bones[index];
        const auto& local = reference_local_transforms[index];
        if (!valid_transform(local) ||
            (index == 0U && bone.parent) ||
            (index != 0U &&
             (!bone.parent || *bone.parent >= index)))
        {
            return std::unexpected(
                PaintDeformationError::InvalidSkeleton);
        }
        reference_component.push_back(
            bone.parent
                ? compose(
                      reference_component[*bone.parent],
                      local)
                : local);
    }
    return reference_component;
}

auto deform_paint_vertices(
    std::span<const PaintDeformationVertex> vertices,
    std::span<const PaintSamplingBone> bones,
    std::span<const PaintReferenceBoneTransform>
        reference_local_transforms,
    std::span<const PaintReferenceBoneTransform>
        current_world_transforms,
    std::stop_token cancellation)
    -> std::expected<
        std::vector<DeformedPaintVertex>,
        PaintDeformationError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintDeformationError::Cancelled);
    }
    if (bones.empty() ||
        bones.size() > MaximumPaintDeformationBones ||
        reference_local_transforms.size() != bones.size() ||
        current_world_transforms.size() != bones.size())
    {
        return std::unexpected(
            PaintDeformationError::InvalidSkeleton);
    }
    if (vertices.empty() ||
        vertices.size() > MaximumAdaptivePaintSamples)
    {
        return std::unexpected(
            vertices.empty()
                ? PaintDeformationError::InvalidVertex
                : PaintDeformationError::ResourceLimit);
    }

    const auto reference_component =
        compose_reference_bone_transforms(
            bones,
            reference_local_transforms);
    if (!reference_component)
    {
        return std::unexpected(reference_component.error());
    }
    for (auto index = std::size_t{};
         index < bones.size();
         ++index)
    {
        const auto& current = current_world_transforms[index];
        if (!valid_transform(current))
        {
            return std::unexpected(
                PaintDeformationError::InvalidSkeleton);
        }
    }

    auto output = std::vector<DeformedPaintVertex>{};
    output.reserve(vertices.size());
    for (auto index = std::size_t{};
         index < vertices.size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintDeformationError::Cancelled);
        }
        const auto& vertex = vertices[index];
        if (!vertex_valid(vertex, bones.size()))
        {
            return std::unexpected(
                PaintDeformationError::InvalidVertex);
        }

        auto position = Vector3d{};
        auto normal = Vector3d{};
        for (auto influence_index = std::size_t{};
             influence_index < vertex.influence_count;
             ++influence_index)
        {
            const auto& influence =
                vertex.influences[influence_index];
            const auto bone = static_cast<std::size_t>(
                influence.bone);
            const auto reference_local_position =
                inverse_transform_point(
                    (*reference_component)[bone],
                    vertex.position);
            const auto deformed_position = transform_point(
                current_world_transforms[bone],
                reference_local_position);
            const auto reference_local_normal = rotate(
                conjugate(normalized(
                    (*reference_component)[bone].rotation)),
                vertex.normal);
            const auto deformed_normal = rotate(
                current_world_transforms[bone].rotation,
                reference_local_normal);
            position = add(
                position,
                multiply(
                    deformed_position,
                    influence.weight));
            normal = add(
                normal,
                multiply(
                    deformed_normal,
                    influence.weight));
        }
        normal = normalized(normal);
        if (!finite(position) || !finite(normal) ||
            length_squared(normal) <= TransformEpsilon)
        {
            return std::unexpected(
                PaintDeformationError::InvalidVertex);
        }
        output.push_back(
            DeformedPaintVertex{position, normal});
    }
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintDeformationError::Cancelled);
    }
    return output;
}
} // namespace meccha::core
