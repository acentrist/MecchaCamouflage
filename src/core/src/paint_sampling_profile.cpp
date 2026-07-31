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
