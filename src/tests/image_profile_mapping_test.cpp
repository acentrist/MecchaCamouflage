#include <meccha/core/image_profile_mapping.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stop_token>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_profile_mapping: "
                  << message << '\n';
    }
    return condition;
}

auto close(double left, double right) -> bool
{
    return std::abs(left - right) < 0.000001;
}

auto geometry(BodyProfile body) -> ImageReferenceGeometry
{
    const auto identity = expected_mesh_profile(
        body,
        MeshProfileRole::ImageReference);
    auto positions = std::vector<Vector3d>(
        identity.vertex_count,
        Vector3d{});
    positions[0U] = Vector3d{-1.0, 0.0, -1.0};
    positions[1U] = Vector3d{1.0, 0.0, -1.0};
    positions[2U] = Vector3d{0.0, 0.0, 1.0};
    positions[identity.vertex_count - 2U] =
        Vector3d{0.0, -0.5, 0.0};
    positions[identity.vertex_count - 1U] =
        Vector3d{0.0, 0.5, 0.0};

    auto indices = std::vector<std::uint32_t>(
        identity.index_count,
        0U);
    indices[0U] = 0U;
    indices[1U] = 1U;
    indices[2U] = 2U;
    indices[3U] = 1U;
    indices[4U] = 0U;
    indices[5U] = 2U;
    indices.back() =
        static_cast<std::uint32_t>(identity.vertex_count - 1U);
    return ImageReferenceGeometry{
        identity,
        std::make_shared<const std::vector<Vector3d>>(
            std::move(positions)),
        std::make_shared<const std::vector<std::uint32_t>>(
            std::move(indices)),
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    for (const auto body : {
             BodyProfile::Round,
             BodyProfile::Cube,
             BodyProfile::Fukuyoka,
         })
    {
        const auto built =
            build_canonical_image_profile(geometry(body));
        passed &= expect(
            built &&
                built->geometry.identity.body == body &&
                close(built->center_x, 0.0) &&
                close(built->center_y, 0.0) &&
                close(built->center_z, 0.0) &&
                close(built->pixels_per_unit, 112.0),
            "an accepted immutable reference profile did not build");
        if (!built)
        {
            continue;
        }
        const auto front = map_image_triangle(
            *built,
            ImageTriangleAnchor{0U, 0.25, 0.5, 0.25});
        const auto back = map_image_triangle(
            *built,
            ImageTriangleAnchor{1U, 0.5, 0.25, 0.25});
        passed &= expect(
            front &&
                front->face == ImageAtlasFace::Front &&
                front->u >= 0.0 && front->u <= 0.25 &&
                front->v >= 0.0 && front->v <= 1.0,
            "front triangle projection left its canonical tile");
        passed &= expect(
            back &&
                back->face == ImageAtlasFace::Back &&
                back->u >= 0.5 && back->u <= 0.75 &&
                back->v >= 0.0 && back->v <= 1.0,
            "back triangle projection left its canonical tile");
    }

    auto raw = geometry(BodyProfile::Round);
    raw.identity = expected_mesh_profile(
        BodyProfile::Round,
        MeshProfileRole::Raw);
    passed &= expect(
        build_canonical_image_profile(raw) ==
            std::unexpected(ImageProfileMapError::InvalidProfile),
        "a raw profile was accepted as image-reference geometry");

    auto wrong_count = geometry(BodyProfile::Round);
    wrong_count.positions =
        std::make_shared<const std::vector<Vector3d>>(3U);
    passed &= expect(
        build_canonical_image_profile(wrong_count) ==
            std::unexpected(ImageProfileMapError::InvalidGeometry),
        "a truncated reference vertex array was accepted");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        build_canonical_image_profile(
            geometry(BodyProfile::Round),
            cancelled.get_token()) ==
            std::unexpected(ImageProfileMapError::Cancelled),
        "a pre-cancelled canonical profile was published");

    const auto built =
        build_canonical_image_profile(geometry(BodyProfile::Round));
    if (built)
    {
        passed &= expect(
            map_image_triangle(
                *built,
                ImageTriangleAnchor{0U, 0.2, 0.2, 0.2}) ==
                std::unexpected(
                    ImageProfileMapError::InvalidAnchor),
            "barycentric weights that do not sum to one were accepted");
        passed &= expect(
            map_image_triangle(
                *built,
                ImageTriangleAnchor{
                    built->geometry.identity.triangle_count,
                    1.0,
                    0.0,
                    0.0}) ==
                std::unexpected(
                    ImageProfileMapError::InvalidAnchor),
            "an out-of-range triangle was accepted");
    }

    auto degenerate_geometry = geometry(BodyProfile::Round);
    auto degenerate_positions =
        *degenerate_geometry.positions;
    degenerate_positions[1U] = degenerate_positions[0U];
    degenerate_positions[2U] = degenerate_positions[0U];
    degenerate_geometry.positions =
        std::make_shared<const std::vector<Vector3d>>(
            std::move(degenerate_positions));
    const auto degenerate_profile =
        build_canonical_image_profile(degenerate_geometry);
    passed &= expect(
        degenerate_profile &&
            map_image_triangle(
                *degenerate_profile,
                ImageTriangleAnchor{0U, 1.0, 0.0, 0.0}) ==
                std::unexpected(
                    ImageProfileMapError::DegenerateTriangle),
        "a degenerate captured triangle produced an atlas coordinate");

    if (passed)
    {
        std::cout << "PASS image_profile_mapping\n";
    }
    return passed ? 0 : 1;
}
