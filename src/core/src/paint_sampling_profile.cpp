#include <meccha/core/paint_sampling_profile.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <vector>

namespace meccha::core
{
namespace
{
auto add_once(
    std::vector<PaintSamplingProfileField>& fields,
    PaintSamplingProfileField field) -> void
{
    if (!std::ranges::contains(fields, field))
    {
        fields.push_back(field);
    }
}

auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto finite(const Vector3d& value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

auto length_squared(const Vector3d& value) -> double
{
    return value.x * value.x +
           value.y * value.y +
           value.z * value.z;
}
} // namespace

auto validate(const PaintSamplingProfile& profile)
    -> std::vector<PaintSamplingProfileField>
{
    auto fields = std::vector<PaintSamplingProfileField>{};
    const auto& identity = profile.identity;
    if (!validate(identity).empty() ||
        identity.role != MeshProfileRole::Raw)
    {
        add_once(fields, PaintSamplingProfileField::Identity);
    }
    if (!profile.vertices ||
        profile.vertices->size() != identity.vertex_count ||
        (profile.vertices &&
         !std::ranges::all_of(
             *profile.vertices,
             [](const PaintSamplingVertex& vertex)
             {
                 return unit(vertex.u) && unit(vertex.v);
             })))
    {
        add_once(fields, PaintSamplingProfileField::Vertices);
    }
    if (!profile.triangles ||
        profile.triangles->size() != identity.triangle_count ||
        identity.index_count % 3U != 0U ||
        identity.index_count / 3U != identity.triangle_count)
    {
        add_once(fields, PaintSamplingProfileField::Triangles);
    }
    if (!profile.bones ||
        profile.bones->size() != identity.bone_count)
    {
        add_once(fields, PaintSamplingProfileField::Bones);
    }
    else
    {
        for (auto position = std::size_t{};
             position < profile.bones->size();
             ++position)
        {
            const auto& bone = (*profile.bones)[position];
            const auto duplicate =
                std::ranges::find_if(
                    *profile.bones,
                    [&](const PaintSamplingBone& candidate)
                    {
                        return &candidate != &bone &&
                               candidate.name == bone.name;
                    }) != profile.bones->end();
            if (bone.name.empty() || bone.name.size() > 128U ||
                !valid_utf8(bone.name) || duplicate ||
                (position == 0U && bone.parent) ||
                (position != 0U &&
                 (!bone.parent || *bone.parent >= position)))
            {
                add_once(fields, PaintSamplingProfileField::Bones);
                break;
            }
        }
    }
    if (profile.vertices && profile.triangles)
    {
        for (const auto& triangle : *profile.triangles)
        {
            if (triangle.first >= profile.vertices->size() ||
                triangle.second >= profile.vertices->size() ||
                triangle.third >= profile.vertices->size() ||
                triangle.uv_island >= identity.uv_island_count)
            {
                add_once(
                    fields,
                    PaintSamplingProfileField::Topology);
                break;
            }
        }
    }
    return fields;
}

auto validate_deformation(const PaintSamplingProfile& profile)
    -> std::vector<PaintSamplingProfileField>
{
    auto fields = std::vector<PaintSamplingProfileField>{};
    const auto& identity = profile.identity;
    if (!profile.deformation_vertices ||
        profile.deformation_vertices->size() !=
            identity.vertex_count)
    {
        add_once(
            fields,
            PaintSamplingProfileField::DeformationVertices);
    }
    else
    {
        for (const auto& vertex : *profile.deformation_vertices)
        {
            if (!finite(vertex.position) ||
                !finite(vertex.normal) ||
                length_squared(vertex.normal) <= 1.0e-12 ||
                vertex.influence_count == 0U ||
                vertex.influence_count >
                    MaximumPaintBoneInfluences)
            {
                add_once(
                    fields,
                    PaintSamplingProfileField::
                        DeformationVertices);
                break;
            }
            auto raw_sum = std::uint32_t{};
            auto weight_sum = 0.0;
            auto valid = true;
            for (auto index = std::size_t{};
                 index < vertex.influence_count;
                 ++index)
            {
                const auto& influence =
                    vertex.influences[index];
                if (influence.bone >= identity.bone_count ||
                    influence.raw_weight == 0U ||
                    !std::isfinite(influence.weight) ||
                    influence.weight <= 0.0 ||
                    influence.weight > 1.0)
                {
                    valid = false;
                    break;
                }
                for (auto previous = std::size_t{};
                     previous < index;
                     ++previous)
                {
                    if (vertex.influences[previous].bone ==
                        influence.bone)
                    {
                        valid = false;
                        break;
                    }
                }
                raw_sum += influence.raw_weight;
                weight_sum += influence.weight;
            }
            if (!valid || raw_sum != 255U ||
                std::abs(weight_sum - 1.0) > 1.0e-4)
            {
                add_once(
                    fields,
                    PaintSamplingProfileField::
                        DeformationVertices);
                break;
            }
        }
    }

    if (!profile.deformation_triangles ||
        profile.deformation_triangles->size() !=
            identity.triangle_count)
    {
        add_once(
            fields,
            PaintSamplingProfileField::DeformationTriangles);
    }
    else
    {
        for (const auto& triangle :
             *profile.deformation_triangles)
        {
            if (triangle.dominant_bone >= identity.bone_count ||
                triangle.body_region.empty() ||
                triangle.body_region.size() > 128U ||
                !valid_utf8(triangle.body_region) ||
                !finite(triangle.local_normal) ||
                length_squared(triangle.local_normal) <=
                    1.0e-12)
            {
                add_once(
                    fields,
                    PaintSamplingProfileField::
                        DeformationTriangles);
                break;
            }
        }
    }

    if (!profile.reference_bone_transforms ||
        profile.reference_bone_transforms->size() !=
            identity.bone_count)
    {
        add_once(
            fields,
            PaintSamplingProfileField::
                ReferenceBoneTransforms);
    }
    else
    {
        for (const auto& transform :
             *profile.reference_bone_transforms)
        {
            const auto& rotation = transform.rotation;
            const auto rotation_length_squared =
                rotation.x * rotation.x +
                rotation.y * rotation.y +
                rotation.z * rotation.z +
                rotation.w * rotation.w;
            if (!finite(transform.translation) ||
                !finite(transform.scale) ||
                !std::isfinite(rotation.x) ||
                !std::isfinite(rotation.y) ||
                !std::isfinite(rotation.z) ||
                !std::isfinite(rotation.w) ||
                rotation_length_squared <= 1.0e-12 ||
                std::abs(rotation_length_squared - 1.0) >
                    1.0e-3 ||
                std::abs(transform.scale.x) <= 1.0e-12 ||
                std::abs(transform.scale.y) <= 1.0e-12 ||
                std::abs(transform.scale.z) <= 1.0e-12)
            {
                add_once(
                    fields,
                    PaintSamplingProfileField::
                        ReferenceBoneTransforms);
                break;
            }
        }
    }
    return fields;
}

auto validate_pair(
    const PaintSamplingProfile& sampling,
    const CanonicalImageProfile& image)
    -> std::vector<PaintSamplingProfileField>
{
    auto fields = validate(sampling);
    const auto& raw = sampling.identity;
    const auto& reference = image.geometry.identity;
    if (!validate(reference).empty() ||
        reference.role != MeshProfileRole::ImageReference ||
        raw.body != reference.body ||
        reference.base_profile_id != raw.profile_id ||
        reference.base_profile_hash != raw.profile_hash ||
        raw.vertex_count != reference.vertex_count ||
        raw.index_count != reference.index_count ||
        raw.triangle_count != reference.triangle_count)
    {
        add_once(fields, PaintSamplingProfileField::PairIdentity);
    }
    if (!image.geometry.indices ||
        image.geometry.indices->size() != raw.index_count ||
        !sampling.bones ||
        sampling.bones->size() != raw.bone_count ||
        !image.geometry.bones ||
        image.geometry.bones->size() != raw.bone_count ||
        !sampling.triangles ||
        sampling.triangles->size() != raw.triangle_count ||
        raw.index_count % 3U != 0U ||
        raw.index_count / 3U != raw.triangle_count)
    {
        add_once(fields, PaintSamplingProfileField::PairTopology);
        return fields;
    }
    for (auto position = std::size_t{};
         position < sampling.bones->size();
         ++position)
    {
        if ((*sampling.bones)[position].parent !=
            (*image.geometry.bones)[position].parent)
        {
            add_once(
                fields,
                PaintSamplingProfileField::PairTopology);
            return fields;
        }
    }
    for (auto position = std::size_t{};
         position < sampling.triangles->size();
         ++position)
    {
        const auto& triangle = (*sampling.triangles)[position];
        const auto base = position * 3U;
        if ((*image.geometry.indices)[base] != triangle.first ||
            (*image.geometry.indices)[base + 1U] !=
                triangle.second ||
            (*image.geometry.indices)[base + 2U] !=
                triangle.third)
        {
            add_once(
                fields,
                PaintSamplingProfileField::PairTopology);
            break;
        }
    }
    return fields;
}
} // namespace meccha::core
