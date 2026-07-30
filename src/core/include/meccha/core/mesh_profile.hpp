#pragma once

#include <meccha/core/image_project.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace meccha::core
{
inline constexpr std::uint32_t MeshProfileSchemaVersion = 2U;
inline constexpr std::uint32_t MeshProfileTextureSize = 1024U;

enum class MeshProfileRole : std::uint8_t
{
    Raw,
    ImageReference,
};

struct MeshProfileIdentity
{
    BodyProfile body{BodyProfile::Round};
    MeshProfileRole role{MeshProfileRole::Raw};
    std::uint32_t profile_schema_version{};
    std::uint32_t schema_version{};
    std::string profile_id{};
    std::string profile_hash{};
    std::string base_profile_id{};
    std::string base_profile_hash{};
    std::string source_path{};
    std::string export_name{};
    std::uint32_t texture_size{};
    std::uint32_t lod_index{};
    std::uint32_t texture_coordinate_count{};
    std::uint32_t section_count{};
    std::size_t vertex_count{};
    std::size_t index_count{};
    std::size_t triangle_count{};
    std::size_t uv_island_count{};
    std::size_t bone_count{};
    std::size_t serialized_vertex_count{};
    std::size_t serialized_index_count{};
    std::size_t serialized_triangle_count{};
    std::optional<std::size_t> maximum_vertex_index{};
    std::size_t reference_pose_bone_count{};

    auto operator==(const MeshProfileIdentity&) const -> bool = default;
};

enum class MeshProfileField : std::uint8_t
{
    Schema,
    ProfileId,
    ProfileHash,
    SourceIdentity,
    Lod,
    Dimensions,
    Counts,
    SerializedCounts,
    IndexBounds,
    BaseProfile,
    ReferencePose,
};

[[nodiscard]] auto expected_mesh_profile(
    BodyProfile body,
    MeshProfileRole role) -> MeshProfileIdentity;

[[nodiscard]] auto validate(const MeshProfileIdentity& profile)
    -> std::vector<MeshProfileField>;
} // namespace meccha::core
