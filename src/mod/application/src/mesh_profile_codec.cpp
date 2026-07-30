#include <meccha/application/mesh_profile_codec.hpp>

#include "strict_json.hpp"

#include <glaze/glaze.hpp>
#include <glaze/json/generic.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::application
{
namespace
{
using Json = glz::generic;
using Object = Json::object_t;
using Array = Json::array_t;

auto malformed(std::string detail)
    -> std::unexpected<MeshProfileCodecError>
{
    return std::unexpected(MeshProfileCodecError{
        MeshProfileCodecErrorCode::MalformedJson,
        {},
        std::move(detail),
    });
}

auto invalid_profile(
    core::MeshProfileField field,
    std::string detail)
    -> std::unexpected<MeshProfileCodecError>
{
    return std::unexpected(MeshProfileCodecError{
        MeshProfileCodecErrorCode::InvalidProfile,
        {field},
        std::move(detail),
    });
}

class ProfileReader
{
public:
    explicit ProfileReader(const Object& root)
        : root_{root}
    {
    }

    auto root_string(std::string_view key) const
        -> std::expected<std::string, MeshProfileCodecError>
    {
        const auto value = member(root_, key);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        const auto* text = (*value)->get_if<std::string>();
        if (text == nullptr)
        {
            return malformed(
                "Mesh profile field is not a string: " +
                std::string{key});
        }
        return *text;
    }

    auto optional_root_string(std::string_view key) const
        -> std::expected<std::string, MeshProfileCodecError>
    {
        const auto found = root_.find(key);
        if (found == root_.end())
        {
            return std::string{};
        }
        const auto* text = found->second.get_if<std::string>();
        if (text == nullptr)
        {
            return malformed(
                "Mesh profile field is not a string: " +
                std::string{key});
        }
        return *text;
    }

    auto root_size(std::string_view key) const
        -> std::expected<std::size_t, MeshProfileCodecError>
    {
        return size(root_, key);
    }

    auto root_array(std::string_view key) const
        -> std::expected<
            std::reference_wrapper<const Array>,
            MeshProfileCodecError>
    {
        return array(root_, key);
    }

    auto root_object(std::string_view key) const
        -> std::expected<
            std::reference_wrapper<const Object>,
            MeshProfileCodecError>
    {
        return object(root_, key);
    }

    auto size(const Object& object_value, std::string_view key) const
        -> std::expected<std::size_t, MeshProfileCodecError>
    {
        const auto value = member(object_value, key);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!(*value)->is_number())
        {
            return malformed(
                "Mesh profile field is not a number: " +
                std::string{key});
        }
        const auto number = (*value)->get<double>();
        if (!std::isfinite(number) || number < 0.0 ||
            std::floor(number) != number ||
            number >
                static_cast<double>(
                    std::numeric_limits<std::size_t>::max()))
        {
            return malformed(
                "Mesh profile field is not a bounded integer: " +
                std::string{key});
        }
        return static_cast<std::size_t>(number);
    }

    auto integer(const Object& object_value, std::string_view key) const
        -> std::expected<std::int64_t, MeshProfileCodecError>
    {
        const auto value = member(object_value, key);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!(*value)->is_number())
        {
            return malformed(
                "Mesh profile field is not a number: " +
                std::string{key});
        }
        const auto number = (*value)->get<double>();
        if (!std::isfinite(number) ||
            std::floor(number) != number ||
            number <
                static_cast<double>(
                    std::numeric_limits<std::int64_t>::min()) ||
            number >
                static_cast<double>(
                    std::numeric_limits<std::int64_t>::max()))
        {
            return malformed(
                "Mesh profile field is not a bounded integer: " +
                std::string{key});
        }
        return static_cast<std::int64_t>(number);
    }

    auto number(const Object& object_value, std::string_view key) const
        -> std::expected<double, MeshProfileCodecError>
    {
        const auto value = member(object_value, key);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        if (!(*value)->is_number())
        {
            return malformed(
                "Mesh profile field is not a number: " +
                std::string{key});
        }
        const auto number = (*value)->get<double>();
        if (!std::isfinite(number))
        {
            return malformed(
                "Mesh profile field is not finite: " +
                std::string{key});
        }
        return number;
    }

    auto array(const Object& object_value, std::string_view key) const
        -> std::expected<
            std::reference_wrapper<const Array>,
            MeshProfileCodecError>
    {
        const auto value = member(object_value, key);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        const auto* result = (*value)->get_if<Array>();
        if (result == nullptr)
        {
            return malformed(
                "Mesh profile field is not an array: " +
                std::string{key});
        }
        return std::cref(*result);
    }

    auto object(const Object& object_value, std::string_view key) const
        -> std::expected<
            std::reference_wrapper<const Object>,
            MeshProfileCodecError>
    {
        const auto value = member(object_value, key);
        if (!value)
        {
            return std::unexpected(value.error());
        }
        const auto* result = (*value)->get_if<Object>();
        if (result == nullptr)
        {
            return malformed(
                "Mesh profile field is not an object: " +
                std::string{key});
        }
        return std::cref(*result);
    }

private:
    static auto member(
        const Object& object_value,
        std::string_view key)
        -> std::expected<const Json*, MeshProfileCodecError>
    {
        const auto found = object_value.find(key);
        if (found == object_value.end())
        {
            return malformed(
                "Mesh profile is missing field: " +
                std::string{key});
        }
        return &found->second;
    }

    const Object& root_;
};

auto decode_mesh_profile(
    std::string_view json,
    core::BodyProfile body,
    core::MeshProfileRole role,
    core::ImageReferenceGeometry* geometry_output)
    -> std::expected<
        core::MeshProfileIdentity,
        MeshProfileCodecError>
{
    if (json.size() > MaximumMeshProfileBytes)
    {
        return std::unexpected(MeshProfileCodecError{
            MeshProfileCodecErrorCode::TooLarge,
            {},
            "Mesh profile exceeds its byte limit.",
        });
    }
    constexpr auto Utf8Bom = std::string_view{"\xEF\xBB\xBF", 3U};
    if (json.starts_with(Utf8Bom))
    {
        json.remove_prefix(Utf8Bom.size());
    }
    const auto strict =
        detail::validate_strict_json_document(json);
    if (!strict)
    {
        return malformed(strict.error().detail);
    }

    auto document = Json{};
    const auto parsed = glz::read_json(document, json);
    if (parsed)
    {
        return malformed(glz::format_error(parsed, json));
    }
    const auto* root = document.get_if<Object>();
    if (root == nullptr)
    {
        return malformed("Mesh profile root is not an object.");
    }
    const auto reader = ProfileReader{*root};

    const auto profile_schema =
        reader.root_size("ProfileSchemaVersion");
    const auto schema = reader.root_size("SchemaVersion");
    const auto profile_id = reader.root_string("ProfileId");
    const auto profile_hash = reader.root_string("ProfileHash");
    const auto base_profile_id =
        reader.optional_root_string("BaseProfileId");
    const auto base_profile_hash =
        reader.optional_root_string("BaseProfileHash");
    const auto source_path = reader.root_string("SourcePath");
    const auto export_name = reader.root_string("Export");
    const auto texture_size = reader.root_size("TextureSize");
    const auto vertex_count = reader.root_size("VertexCount");
    const auto index_count = reader.root_size("IndexCount");
    const auto triangle_count = reader.root_size("TriangleCount");
    const auto uv_island_count = reader.root_size("UvIslandCount");
    const auto bones = reader.root_array("Bones");
    const auto triangles = reader.root_array("Triangles");
    const auto lod = reader.root_object("Lod0");
    if (!profile_schema || !schema || !profile_id ||
        !profile_hash || !base_profile_id ||
        !base_profile_hash || !source_path || !export_name ||
        !texture_size || !vertex_count || !index_count ||
        !triangle_count || !uv_island_count || !bones ||
        !triangles || !lod)
    {
        const auto first_error =
            [&]() -> MeshProfileCodecError
            {
                if (!profile_schema)
                    return profile_schema.error();
                if (!schema) return schema.error();
                if (!profile_id) return profile_id.error();
                if (!profile_hash) return profile_hash.error();
                if (!base_profile_id)
                    return base_profile_id.error();
                if (!base_profile_hash)
                    return base_profile_hash.error();
                if (!source_path) return source_path.error();
                if (!export_name) return export_name.error();
                if (!texture_size) return texture_size.error();
                if (!vertex_count) return vertex_count.error();
                if (!index_count) return index_count.error();
                if (!triangle_count) return triangle_count.error();
                if (!uv_island_count)
                    return uv_island_count.error();
                if (!bones) return bones.error();
                if (!triangles) return triangles.error();
                return lod.error();
            }();
        return std::unexpected(first_error);
    }

    const auto& lod_object = lod->get();
    const auto lod_index = reader.size(lod_object, "LodIndex");
    const auto texture_coordinates =
        reader.size(lod_object, "NumTexCoords");
    const auto section_count =
        reader.size(lod_object, "SectionCount");
    const auto lod_vertex_count =
        reader.size(lod_object, "VertexCount");
    const auto lod_index_count =
        reader.size(lod_object, "IndexCount");
    const auto vertices = reader.array(lod_object, "Vertices");
    const auto indices = reader.array(lod_object, "Indices");
    if (!lod_index || !texture_coordinates || !section_count ||
        !lod_vertex_count || !lod_index_count || !vertices ||
        !indices)
    {
        if (!lod_index)
            return std::unexpected(lod_index.error());
        if (!texture_coordinates)
            return std::unexpected(texture_coordinates.error());
        if (!section_count)
            return std::unexpected(section_count.error());
        if (!lod_vertex_count)
            return std::unexpected(lod_vertex_count.error());
        if (!lod_index_count)
            return std::unexpected(lod_index_count.error());
        if (!vertices)
            return std::unexpected(vertices.error());
        return std::unexpected(indices.error());
    }
    constexpr auto Uint32Maximum =
        static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max());
    if (*profile_schema > Uint32Maximum ||
        *schema > Uint32Maximum ||
        *texture_size > Uint32Maximum ||
        *lod_index > Uint32Maximum ||
        *texture_coordinates > Uint32Maximum ||
        *section_count > Uint32Maximum)
    {
        return malformed(
            "Mesh profile integer exceeds its destination type.");
    }
    if (*lod_vertex_count != *vertex_count ||
        *lod_index_count != *index_count)
    {
        return invalid_profile(
            core::MeshProfileField::Counts,
            "LOD counts do not match the profile header.");
    }

    auto maximum_index = std::optional<std::size_t>{};
    auto decoded_indices = std::vector<std::uint32_t>{};
    if (geometry_output != nullptr)
    {
        decoded_indices.reserve(indices->get().size());
    }
    for (const auto& index : indices->get())
    {
        if (!index.is_number())
        {
            return malformed(
                "Mesh profile index is not numeric.");
        }
        const auto value = index.get<double>();
        if (!std::isfinite(value) || value < 0.0 ||
            std::floor(value) != value ||
            value >
                static_cast<double>(
                    std::numeric_limits<std::size_t>::max()))
        {
            return malformed(
                "Mesh profile index is not a bounded integer.");
        }
        const auto converted = static_cast<std::size_t>(value);
        maximum_index = maximum_index
                            ? std::max(*maximum_index, converted)
                            : converted;
        if (geometry_output != nullptr)
        {
            decoded_indices.push_back(
                static_cast<std::uint32_t>(converted));
        }
    }

    auto bone_parents =
        std::vector<std::optional<std::size_t>>{};
    bone_parents.reserve(bones->get().size());
    const auto validate_nested_indices =
        [&]() -> std::expected<void, MeshProfileCodecError>
        {
            const auto bone_count = bones->get().size();
            for (auto position = std::size_t{};
                 position < bone_count;
                 ++position)
            {
                const auto* bone =
                    bones->get()[position].get_if<Object>();
                if (bone == nullptr)
                {
                    return malformed(
                        "Mesh profile bone is not an object.");
                }
                const auto index = reader.size(*bone, "Index");
                const auto parent =
                    reader.integer(*bone, "ParentIndex");
                if (!index || !parent)
                {
                    return std::unexpected(
                        !index ? index.error() : parent.error());
                }
                if (*index != position || *parent < -1 ||
                    (*parent >= 0 &&
                     static_cast<std::size_t>(*parent) >= position))
                {
                    return invalid_profile(
                        core::MeshProfileField::IndexBounds,
                        "Mesh profile bone hierarchy is invalid.");
                }
                bone_parents.push_back(
                    *parent < 0
                        ? std::nullopt
                        : std::optional<std::size_t>{
                              static_cast<std::size_t>(
                                  *parent)});
            }

            for (auto position = std::size_t{};
                 position < triangles->get().size();
                 ++position)
            {
                const auto* triangle =
                    triangles->get()[position].get_if<Object>();
                if (triangle == nullptr)
                {
                    return malformed(
                        "Mesh profile triangle is not an object.");
                }
                const auto index =
                    reader.size(*triangle, "Index");
                const auto first = reader.size(*triangle, "I0");
                const auto second = reader.size(*triangle, "I1");
                const auto third = reader.size(*triangle, "I2");
                const auto island =
                    reader.size(*triangle, "UvIsland");
                const auto dominant_bone =
                    reader.size(*triangle, "DominantBone");
                if (!index || !first || !second || !third ||
                    !island || !dominant_bone)
                {
                    if (!index)
                        return std::unexpected(index.error());
                    if (!first)
                        return std::unexpected(first.error());
                    if (!second)
                        return std::unexpected(second.error());
                    if (!third)
                        return std::unexpected(third.error());
                    if (!island)
                        return std::unexpected(island.error());
                    return std::unexpected(
                        dominant_bone.error());
                }
                if (*index != position ||
                    *first >= *vertex_count ||
                    *second >= *vertex_count ||
                    *third >= *vertex_count ||
                    *island >= *uv_island_count ||
                    *dominant_bone >= bone_count)
                {
                    return invalid_profile(
                        core::MeshProfileField::IndexBounds,
                        "Mesh profile triangle index is out of bounds.");
                }
            }

            for (const auto& vertex : vertices->get())
            {
                const auto* vertex_object =
                    vertex.get_if<Object>();
                if (vertex_object == nullptr)
                {
                    return malformed(
                        "Mesh profile vertex is not an object.");
                }
                const auto influences = reader.array(
                    *vertex_object,
                    "Influences");
                if (!influences)
                {
                    return std::unexpected(influences.error());
                }
                if (influences->get().empty())
                {
                    return invalid_profile(
                        core::MeshProfileField::IndexBounds,
                        "Mesh profile vertex has no bone influence.");
                }
                for (const auto& influence : influences->get())
                {
                    const auto* influence_object =
                        influence.get_if<Object>();
                    if (influence_object == nullptr)
                    {
                        return malformed(
                            "Mesh profile influence is not an object.");
                    }
                    const auto bone =
                        reader.size(*influence_object, "Bone");
                    if (!bone)
                    {
                        return std::unexpected(bone.error());
                    }
                    if (*bone >= bone_count)
                    {
                        return invalid_profile(
                            core::MeshProfileField::IndexBounds,
                            "Mesh profile influence is out of bounds.");
                    }
                }
            }
            return {};
        }();
    if (!validate_nested_indices)
    {
        return std::unexpected(
            validate_nested_indices.error());
    }

    auto reference_pose_count = std::size_t{};
    auto reference_positions = std::vector<core::Vector3d>{};
    auto reference_bones =
        std::vector<core::ImageReferenceBone>{};
    if (role == core::MeshProfileRole::ImageReference)
    {
        const auto profile_role =
            reader.root_string("ProfileRole");
        const auto pose = reader.root_object("ImageReferencePose");
        if (!profile_role || !pose)
        {
            return std::unexpected(
                !profile_role
                    ? profile_role.error()
                    : pose.error());
        }
        if (*profile_role != "image_reference")
        {
            return malformed(
                "Derived mesh profile role is invalid.");
        }
        const auto transforms = reader.array(
            pose->get(),
            "ComponentTransforms");
        const auto reference_vertices = reader.array(
            pose->get(),
            "Vertices");
        if (!transforms || !reference_vertices)
        {
            return std::unexpected(
                !transforms
                    ? transforms.error()
                    : reference_vertices.error());
        }
        reference_pose_count = transforms->get().size();
        if (reference_pose_count != bones->get().size() ||
            bone_parents.size() != bones->get().size())
        {
            return invalid_profile(
                core::MeshProfileField::ReferencePose,
                "Image-reference transform count does not match the skeleton.");
        }
        reference_bones.reserve(reference_pose_count);
        for (auto position = std::size_t{};
             position < transforms->get().size();
             ++position)
        {
            const auto* transform =
                transforms->get()[position].get_if<Object>();
            if (transform == nullptr)
            {
                return malformed(
                    "Image-reference transform is not an object.");
            }
            const auto index = reader.size(*transform, "Index");
            const auto x = reader.number(*transform, "X");
            const auto y = reader.number(*transform, "Y");
            const auto z = reader.number(*transform, "Z");
            const auto rotation_x =
                reader.number(*transform, "RotationX");
            const auto rotation_y =
                reader.number(*transform, "RotationY");
            const auto rotation_z =
                reader.number(*transform, "RotationZ");
            const auto rotation_w =
                reader.number(*transform, "RotationW");
            const auto scale_x =
                reader.number(*transform, "ScaleX");
            const auto scale_y =
                reader.number(*transform, "ScaleY");
            const auto scale_z =
                reader.number(*transform, "ScaleZ");
            if (!index || !x || !y || !z ||
                !rotation_x || !rotation_y || !rotation_z ||
                !rotation_w || !scale_x || !scale_y ||
                !scale_z)
            {
                if (!index)
                    return std::unexpected(index.error());
                if (!x) return std::unexpected(x.error());
                if (!y) return std::unexpected(y.error());
                if (!z) return std::unexpected(z.error());
                if (!rotation_x)
                    return std::unexpected(
                        rotation_x.error());
                if (!rotation_y)
                    return std::unexpected(
                        rotation_y.error());
                if (!rotation_z)
                    return std::unexpected(
                        rotation_z.error());
                if (!rotation_w)
                    return std::unexpected(
                        rotation_w.error());
                if (!scale_x)
                    return std::unexpected(scale_x.error());
                if (!scale_y)
                    return std::unexpected(scale_y.error());
                return std::unexpected(scale_z.error());
            }
            const auto rotation_length =
                std::sqrt(
                    *rotation_x * *rotation_x +
                    *rotation_y * *rotation_y +
                    *rotation_z * *rotation_z +
                    *rotation_w * *rotation_w);
            if (*index != position ||
                !std::isfinite(rotation_length) ||
                rotation_length <= 0.000001 ||
                std::abs(*scale_x) <= 0.000001 ||
                std::abs(*scale_y) <= 0.000001 ||
                std::abs(*scale_z) <= 0.000001)
            {
                return invalid_profile(
                    core::MeshProfileField::ReferencePose,
                    "Image-reference transform is invalid.");
            }
            reference_bones.push_back(
                core::ImageReferenceBone{
                    bone_parents[position],
                    core::Vector3d{*x, *y, *z},
                });
        }
        if (reference_vertices->get().size() != *vertex_count)
        {
            return invalid_profile(
                core::MeshProfileField::ReferencePose,
                "Image-reference vertex count does not match the profile.");
        }
        reference_positions.reserve(
            reference_vertices->get().size());
        for (auto position = std::size_t{};
             position < reference_vertices->get().size();
             ++position)
        {
            const auto* vertex =
                reference_vertices->get()[position].get_if<Object>();
            if (vertex == nullptr)
            {
                return malformed(
                    "Image-reference vertex is not an object.");
            }
            const auto index = reader.size(*vertex, "Index");
            const auto x = reader.number(*vertex, "X");
            const auto y = reader.number(*vertex, "Y");
            const auto z = reader.number(*vertex, "Z");
            if (!index || !x || !y || !z)
            {
                if (!index)
                    return std::unexpected(index.error());
                if (!x) return std::unexpected(x.error());
                if (!y) return std::unexpected(y.error());
                return std::unexpected(z.error());
            }
            if (*index != position)
            {
                return invalid_profile(
                    core::MeshProfileField::ReferencePose,
                    "Image-reference vertex order is invalid.");
            }
            reference_positions.push_back(
                core::Vector3d{*x, *y, *z});
        }
    }
    else if (
        root->contains("ProfileRole") ||
        root->contains("BaseProfileId") ||
        root->contains("BaseProfileHash") ||
        root->contains("ImageReferencePose"))
    {
        return malformed(
            "Raw mesh profile contains derived-profile fields.");
    }

    auto identity = core::MeshProfileIdentity{
        body,
        role,
        static_cast<std::uint32_t>(*profile_schema),
        static_cast<std::uint32_t>(*schema),
        std::move(*profile_id),
        std::move(*profile_hash),
        std::move(*base_profile_id),
        std::move(*base_profile_hash),
        std::move(*source_path),
        std::move(*export_name),
        static_cast<std::uint32_t>(*texture_size),
        static_cast<std::uint32_t>(*lod_index),
        static_cast<std::uint32_t>(*texture_coordinates),
        static_cast<std::uint32_t>(*section_count),
        *vertex_count,
        *index_count,
        *triangle_count,
        *uv_island_count,
        bones->get().size(),
        vertices->get().size(),
        indices->get().size(),
        triangles->get().size(),
        maximum_index,
        reference_pose_count,
    };
    const auto fields = core::validate(identity);
    if (!fields.empty())
    {
        return std::unexpected(MeshProfileCodecError{
            MeshProfileCodecErrorCode::InvalidProfile,
            fields,
            "Mesh profile does not match the frozen body contract.",
        });
    }
    if (geometry_output != nullptr)
    {
        *geometry_output = core::ImageReferenceGeometry{
            identity,
            std::make_shared<
                const std::vector<core::Vector3d>>(
                std::move(reference_positions)),
            std::make_shared<
                const std::vector<std::uint32_t>>(
                std::move(decoded_indices)),
            std::make_shared<
                const std::vector<core::ImageReferenceBone>>(
                std::move(reference_bones)),
        };
    }
    return identity;
}
} // namespace

auto decode_mesh_profile_identity(
    std::string_view json,
    core::BodyProfile body,
    core::MeshProfileRole role)
    -> std::expected<
        core::MeshProfileIdentity,
        MeshProfileCodecError>
{
    return decode_mesh_profile(json, body, role, nullptr);
}

auto decode_canonical_image_profile(
    std::string_view json,
    core::BodyProfile body)
    -> std::expected<
        core::CanonicalImageProfile,
        MeshProfileCodecError>
{
    auto geometry = core::ImageReferenceGeometry{};
    const auto identity = decode_mesh_profile(
        json,
        body,
        core::MeshProfileRole::ImageReference,
        &geometry);
    if (!identity)
    {
        return std::unexpected(identity.error());
    }
    auto profile =
        core::build_canonical_image_profile(std::move(geometry));
    if (!profile)
    {
        return std::unexpected(MeshProfileCodecError{
            MeshProfileCodecErrorCode::InvalidProfile,
            {core::MeshProfileField::ReferencePose},
            "Image-reference geometry cannot build a canonical profile.",
        });
    }
    return std::move(*profile);
}
} // namespace meccha::application
