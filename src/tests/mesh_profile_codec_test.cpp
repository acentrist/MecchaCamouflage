#include <meccha/application/mesh_profile_codec.hpp>

#include <meccha/core/image_project.hpp>
#include <meccha/core/mesh_profile.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL mesh_profile_codec: " << message << '\n';
    }
    return condition;
}

auto read_file(const std::filesystem::path& path) -> std::string
{
    auto input = std::ifstream{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;

    if (argc != 2)
    {
        std::cerr << "usage: mesh_profile_codec_test <profile-directory>\n";
        return 2;
    }
    const auto root = std::filesystem::path{argv[1]};
    struct Case
    {
        std::string_view file{};
        core::BodyProfile body{};
        core::MeshProfileRole role{};
    };
    constexpr std::array cases{
        Case{
            "paintman.mesh-profile-v2.json",
            core::BodyProfile::Round,
            core::MeshProfileRole::Raw,
        },
        Case{
            "paintman.image-profile-v2.json",
            core::BodyProfile::Round,
            core::MeshProfileRole::ImageReference,
        },
        Case{
            "paintman_cube.mesh-profile-v2.json",
            core::BodyProfile::Cube,
            core::MeshProfileRole::Raw,
        },
        Case{
            "paintman_cube.image-profile-v2.json",
            core::BodyProfile::Cube,
            core::MeshProfileRole::ImageReference,
        },
        Case{
            "paintman_hukuyoka.mesh-profile-v2.json",
            core::BodyProfile::Fukuyoka,
            core::MeshProfileRole::Raw,
        },
        Case{
            "paintman_hukuyoka.image-profile-v2.json",
            core::BodyProfile::Fukuyoka,
            core::MeshProfileRole::ImageReference,
        },
    };

    auto passed = true;
    for (const auto& test : cases)
    {
        const auto json = read_file(root / test.file);
        const auto decoded =
            application::decode_mesh_profile_identity(
                json,
                test.body,
                test.role);
        if (!decoded)
        {
            std::cerr << test.file << ": "
                      << decoded.error().detail << " fields:";
            for (const auto field : decoded.error().fields)
            {
                std::cerr << ' ' << static_cast<int>(field);
            }
            std::cerr << '\n';
        }
        passed &= expect(
            decoded &&
                *decoded ==
                    core::expected_mesh_profile(
                        test.body,
                        test.role),
            "a packaged mesh profile failed its frozen identity");
        if (test.role == core::MeshProfileRole::Raw)
        {
            const auto sampling =
                application::decode_paint_sampling_profile(
                    json,
                    test.body);
            passed &= expect(
                sampling &&
                    sampling->identity ==
                        core::expected_mesh_profile(
                            test.body,
                            core::MeshProfileRole::Raw) &&
                    sampling->vertices &&
                    sampling->vertices->size() ==
                        sampling->identity.vertex_count &&
                    sampling->triangles &&
                    sampling->triangles->size() ==
                        sampling->identity.triangle_count &&
                    sampling->bones &&
                    sampling->bones->size() ==
                        sampling->identity.bone_count &&
                    sampling->bones->front().name == "amm" &&
                    !sampling->bones->front().parent &&
                    (*sampling->bones)[1U].name == "loot" &&
                    (*sampling->bones)[1U].parent ==
                        std::optional<std::size_t>{0U} &&
                    sampling->bones->back().name ==
                        "foot_R_end" &&
                    sampling->bones->back().parent ==
                        std::optional<std::size_t>{26U} &&
                    sampling->deformation_vertices &&
                    sampling->deformation_vertices->size() ==
                        sampling->identity.vertex_count &&
                    sampling->deformation_triangles &&
                    sampling->deformation_triangles->size() ==
                        sampling->identity.triangle_count &&
                    sampling->reference_bone_transforms &&
                    sampling->reference_bone_transforms->size() ==
                        sampling->identity.bone_count &&
                    (test.body != core::BodyProfile::Round ||
                     (std::abs(
                          sampling->deformation_vertices->front()
                                  .position.x -
                              -14.917195) <
                          0.000001 &&
                      sampling->deformation_vertices->front()
                              .influence_count ==
                          3U &&
                      sampling->deformation_vertices->front()
                              .influences[0U]
                              .bone ==
                          13U &&
                      sampling->deformation_vertices->front()
                              .influences[0U]
                              .raw_weight ==
                          152U &&
                      std::abs(
                          sampling->deformation_vertices->front()
                                  .influences[0U]
                                  .weight -
                              0.59607846) <
                          0.000001 &&
                      sampling->deformation_triangles->front()
                              .dominant_bone ==
                          14U &&
                      sampling->deformation_triangles->front()
                              .body_region ==
                          "arm" &&
                      std::abs(
                          (*sampling->reference_bone_transforms)[13U]
                                  .translation.y -
                              -18.170628) <
                          0.000001)) &&
                    core::validate_deformation(*sampling).empty() &&
                    std::ranges::all_of(
                        *sampling->vertices,
                        [](const auto& vertex)
                        {
                            return std::isfinite(vertex.u) &&
                                   vertex.u >= 0.0 &&
                                   vertex.u <= 1.0 &&
                                   std::isfinite(vertex.v) &&
                                   vertex.v >= 0.0 &&
                                   vertex.v <= 1.0;
                        }),
                "a packaged raw profile did not produce complete "
                "immutable Paint sampling topology");
        }
        else
        {
            const auto canonical =
                application::decode_canonical_image_profile(
                    json,
                    test.body);
            auto face_counts = std::array<std::size_t, 4U>{};
            auto all_triangles_mapped = canonical.has_value();
            if (canonical)
            {
                for (auto triangle = std::size_t{};
                     triangle <
                     canonical->geometry.identity.triangle_count;
                     ++triangle)
                {
                    const auto mapped = core::map_image_triangle(
                        *canonical,
                        core::ImageTriangleAnchor{
                            triangle,
                            1.0 / 3.0,
                            1.0 / 3.0,
                            1.0 / 3.0});
                    if (!mapped)
                    {
                        all_triangles_mapped = false;
                        break;
                    }
                    ++face_counts[static_cast<std::size_t>(
                        mapped->face)];
                }
            }
            passed &= expect(
                canonical &&
                    canonical->geometry.identity ==
                        core::expected_mesh_profile(
                            test.body,
                            core::MeshProfileRole::ImageReference) &&
                    canonical->geometry.positions &&
                    canonical->geometry.positions->size() ==
                        canonical->geometry.identity.vertex_count &&
                    canonical->geometry.indices &&
                    canonical->geometry.indices->size() ==
                        canonical->geometry.identity.index_count &&
                    all_triangles_mapped &&
                    std::ranges::all_of(
                        face_counts,
                        [](std::size_t count)
                        {
                            return count > 0U;
                        }),
                "a packaged image-reference profile did not produce "
                "complete four-face canonical geometry");
        }
    }

    auto aliased = read_file(
        root / "paintman_hukuyoka.mesh-profile-v2.json");
    auto position = std::size_t{};
    while ((position = aliased.find("hukuyoka", position)) !=
           std::string::npos)
    {
        aliased.replace(position, 8U, "fukuyoka");
        position += 8U;
    }
    const auto rejected_alias =
        application::decode_mesh_profile_identity(
            aliased,
            core::BodyProfile::Fukuyoka,
            core::MeshProfileRole::Raw);
    passed &= expect(
        !rejected_alias &&
            rejected_alias.error().code ==
                application::MeshProfileCodecErrorCode::InvalidProfile,
        "a profile using the UI alias as an asset name was accepted");

    auto invalid_index =
        read_file(root / "paintman.mesh-profile-v2.json");
    const auto index_position =
        invalid_index.find(R"("I0":  0)");
    if (index_position != std::string::npos)
    {
        invalid_index.replace(
            index_position,
            std::string_view{R"("I0":  0)"}.size(),
            R"("I0":  1660)");
    }
    const auto rejected_index =
        application::decode_mesh_profile_identity(
            invalid_index,
            core::BodyProfile::Round,
            core::MeshProfileRole::Raw);
    passed &= expect(
        index_position != std::string::npos &&
            !rejected_index &&
            rejected_index.error().code ==
                application::MeshProfileCodecErrorCode::InvalidProfile &&
            rejected_index.error().fields ==
                std::vector{core::MeshProfileField::IndexBounds},
        "an out-of-bounds packaged triangle index was accepted");

    auto invalid_uv =
        read_file(root / "paintman.mesh-profile-v2.json");
    const auto uv_position =
        invalid_uv.find(R"("U":  0.5292969)");
    if (uv_position != std::string::npos)
    {
        invalid_uv.replace(
            uv_position,
            std::string_view{R"("U":  0.5292969)"}.size(),
            R"("U":  1.5)");
    }
    const auto rejected_uv =
        application::decode_paint_sampling_profile(
            invalid_uv,
            core::BodyProfile::Round);
    passed &= expect(
        uv_position != std::string::npos &&
            !rejected_uv &&
            rejected_uv.error().code ==
                application::MeshProfileCodecErrorCode::InvalidProfile &&
            rejected_uv.error().fields ==
                std::vector{core::MeshProfileField::Dimensions},
        "an out-of-range packaged Paint UV was accepted");

    const auto oversized =
        application::decode_mesh_profile_identity(
            std::string(
                application::MaximumMeshProfileBytes + 1U,
                ' '),
            core::BodyProfile::Round,
            core::MeshProfileRole::Raw);
    passed &= expect(
        !oversized &&
            oversized.error().code ==
                application::MeshProfileCodecErrorCode::TooLarge,
        "an oversized profile was accepted");

    auto invalid_reference_vertex =
        read_file(root / "paintman.image-profile-v2.json");
    const auto pose_position =
        invalid_reference_vertex.find("\"ImageReferencePose\"");
    const auto vertices_position =
        invalid_reference_vertex.find(
            "\"Vertices\"",
            pose_position);
    const auto reference_index_position =
        invalid_reference_vertex.find(
            R"("Index":  0)",
            vertices_position);
    if (reference_index_position != std::string::npos)
    {
        invalid_reference_vertex.replace(
            reference_index_position,
            std::string_view{R"("Index":  0)"}.size(),
            R"("Index":  1)");
    }
    const auto rejected_reference_vertex =
        application::decode_canonical_image_profile(
            invalid_reference_vertex,
            core::BodyProfile::Round);
    passed &= expect(
        reference_index_position != std::string::npos &&
            !rejected_reference_vertex &&
            rejected_reference_vertex.error().code ==
                application::MeshProfileCodecErrorCode::InvalidProfile,
        "an out-of-order image-reference vertex was accepted");

    auto invalid_reference_transform =
        read_file(root / "paintman.image-profile-v2.json");
    const auto scale_position =
        invalid_reference_transform.find(
            R"("ScaleX":  0.0316935182)");
    if (scale_position != std::string::npos)
    {
        invalid_reference_transform.replace(
            scale_position,
            std::string_view{
                R"("ScaleX":  0.0316935182)"}
                .size(),
            R"("ScaleX":  0)");
    }
    const auto rejected_reference_transform =
        application::decode_canonical_image_profile(
            invalid_reference_transform,
            core::BodyProfile::Round);
    passed &= expect(
        scale_position != std::string::npos &&
            !rejected_reference_transform &&
            rejected_reference_transform.error().code ==
                application::MeshProfileCodecErrorCode::
                    InvalidProfile &&
            rejected_reference_transform.error().fields ==
                std::vector{
                    core::MeshProfileField::ReferencePose},
        "an invalid image-reference skeleton transform was accepted");

    if (passed)
    {
        std::cout << "PASS mesh_profile_codec\n";
        return 0;
    }
    return 1;
}
