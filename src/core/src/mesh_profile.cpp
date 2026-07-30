#include <meccha/core/mesh_profile.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::core
{
namespace
{
struct FrozenProfile
{
    BodyProfile body{};
    std::string_view export_name{};
    std::string_view source_path{};
    std::string_view profile_hash{};
    std::size_t vertex_count{};
    std::size_t index_count{};
    std::size_t uv_island_count{};
};

constexpr std::array FrozenProfiles{
    FrozenProfile{
        BodyProfile::Round,
        "paintman",
        "Chameleon/Content/3Dmodel/cLeon/charactor/paintman/"
        "skeltal/paintman.uasset",
        "13e6049b7bb190b8b40fcbba982fe1398b042a28db6506e5595e7d90"
        "fff3b84b",
        1660U,
        8352U,
        12U,
    },
    FrozenProfile{
        BodyProfile::Cube,
        "paintman_cube",
        "Chameleon/Content/3Dmodel/cLeon/charactor/paintman/"
        "skeltal_cube/paintman_cube.uasset",
        "a2832e8cac1cd5d15ce9024cf142f9b8f5dbc321b851a9fd0f919e75"
        "c59d0489",
        452U,
        1080U,
        56U,
    },
    FrozenProfile{
        BodyProfile::Fukuyoka,
        "paintman_hukuyoka",
        "Chameleon/Content/3Dmodel/cLeon/charactor/paintman/"
        "skeltal/paintman_hukuyoka.uasset",
        "fbadd42e71585dfd62eac0d572d4434b70bfcb36a7619433ee1acd8c"
        "19e4a3e6",
        1508U,
        7584U,
        12U,
    },
};

constexpr std::size_t FrozenBoneCount = 28U;

auto frozen_profile(BodyProfile body) -> const FrozenProfile&
{
    for (const auto& profile : FrozenProfiles)
    {
        if (profile.body == body)
        {
            return profile;
        }
    }
    return FrozenProfiles.front();
}

auto profile_id(const FrozenProfile& profile) -> std::string
{
    return std::string{profile.export_name} + ":" +
           std::string{profile.source_path} + ":lod0:v" +
           std::to_string(profile.vertex_count) + ":i" +
           std::to_string(profile.index_count) + ":b" +
           std::to_string(FrozenBoneCount) + ":" +
           std::string{profile.profile_hash};
}
} // namespace

auto expected_mesh_profile(
    BodyProfile body,
    MeshProfileRole role) -> MeshProfileIdentity
{
    const auto& frozen = frozen_profile(body);
    const auto id = profile_id(frozen);
    const auto triangles = frozen.index_count / 3U;
    const auto image = role == MeshProfileRole::ImageReference;
    return MeshProfileIdentity{
        body,
        role,
        MeshProfileSchemaVersion,
        MeshProfileSchemaVersion,
        id,
        std::string{frozen.profile_hash},
        image ? id : std::string{},
        image ? std::string{frozen.profile_hash} : std::string{},
        std::string{frozen.source_path},
        std::string{frozen.export_name},
        MeshProfileTextureSize,
        0U,
        1U,
        1U,
        frozen.vertex_count,
        frozen.index_count,
        triangles,
        frozen.uv_island_count,
        FrozenBoneCount,
        frozen.vertex_count,
        frozen.index_count,
        triangles,
        frozen.vertex_count - 1U,
        image ? FrozenBoneCount : 0U,
    };
}

auto validate(const MeshProfileIdentity& profile)
    -> std::vector<MeshProfileField>
{
    const auto expected =
        expected_mesh_profile(profile.body, profile.role);
    auto errors = std::vector<MeshProfileField>{};
    if (profile.profile_schema_version !=
            MeshProfileSchemaVersion ||
        profile.schema_version != MeshProfileSchemaVersion)
    {
        errors.push_back(MeshProfileField::Schema);
    }
    if (profile.profile_id != expected.profile_id)
    {
        errors.push_back(MeshProfileField::ProfileId);
    }
    if (profile.profile_hash != expected.profile_hash)
    {
        errors.push_back(MeshProfileField::ProfileHash);
    }
    if (profile.source_path != expected.source_path ||
        profile.export_name != expected.export_name)
    {
        errors.push_back(MeshProfileField::SourceIdentity);
    }
    if (profile.lod_index != expected.lod_index ||
        profile.texture_coordinate_count !=
            expected.texture_coordinate_count ||
        profile.section_count != expected.section_count)
    {
        errors.push_back(MeshProfileField::Lod);
    }
    if (profile.texture_size != MeshProfileTextureSize)
    {
        errors.push_back(MeshProfileField::Dimensions);
    }
    if (profile.vertex_count != expected.vertex_count ||
        profile.index_count != expected.index_count ||
        profile.triangle_count != expected.triangle_count ||
        profile.uv_island_count != expected.uv_island_count ||
        profile.bone_count != expected.bone_count ||
        profile.index_count / 3U != profile.triangle_count ||
        profile.index_count % 3U != 0U)
    {
        errors.push_back(MeshProfileField::Counts);
    }
    if (profile.serialized_vertex_count !=
            profile.vertex_count ||
        profile.serialized_index_count != profile.index_count ||
        profile.serialized_triangle_count !=
            profile.triangle_count)
    {
        errors.push_back(MeshProfileField::SerializedCounts);
    }
    if (!profile.maximum_vertex_index ||
        *profile.maximum_vertex_index >= profile.vertex_count)
    {
        errors.push_back(MeshProfileField::IndexBounds);
    }
    if (profile.base_profile_id != expected.base_profile_id ||
        profile.base_profile_hash !=
            expected.base_profile_hash)
    {
        errors.push_back(MeshProfileField::BaseProfile);
    }
    if (profile.reference_pose_bone_count !=
        expected.reference_pose_bone_count)
    {
        errors.push_back(MeshProfileField::ReferencePose);
    }
    return errors;
}
} // namespace meccha::core
