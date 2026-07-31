#include <meccha/application/mesh_profile_codec.hpp>
#include <meccha/core/paint_capture_geometry.hpp>
#include <meccha/core/paint_deformation.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stop_token>
#include <string>
#include <string_view>

namespace
{
using namespace meccha;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_capture_geometry: "
                  << message << '\n';
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

auto finite(core::Vector3d value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;

    if (argc != 2)
    {
        std::cerr
            << "usage: paint_capture_geometry_test "
               "<profile-directory>\n";
        return 2;
    }
    const auto root = std::filesystem::path{argv[1]};
    const auto sampling =
        application::decode_paint_sampling_profile(
            read_file(root / "paintman.mesh-profile-v2.json"),
            core::BodyProfile::Round);
    const auto image =
        application::decode_canonical_image_profile(
            read_file(root / "paintman.image-profile-v2.json"),
            core::BodyProfile::Round);
    auto passed = expect(
        sampling && image,
        "the packaged round profiles could not be decoded");
    if (!sampling || !image)
    {
        return 1;
    }
    const auto current =
        core::compose_reference_bone_transforms(
            *sampling->bones,
            *sampling->reference_bone_transforms);
    passed &= expect(
        current.has_value(),
        "the packaged reference skeleton could not be composed");
    if (!current)
    {
        return 1;
    }

    const auto view = core::EspView{
        core::EspWorldPoint{0.0, -500.0, 60.0},
        0.0,
        90.0,
        0.0,
        90.0,
        16.0 / 9.0,
        core::EspAspectConstraint::MaintainYFov,
        1.0,
        1.0,
    };
    const auto viewport = core::EspViewport{1920.0, 1080.0};
    const auto geometry = core::build_paint_capture_geometry(
        *sampling,
        *image,
        *current,
        4.0,
        view,
        viewport);
    const auto front = geometry
                           ? std::ranges::count(
                                 *geometry,
                                 core::Region::Front,
                                 &core::PaintCaptureGeometrySample::
                                     region)
                           : 0;
    const auto side = geometry
                          ? std::ranges::count(
                                *geometry,
                                core::Region::Side,
                                &core::PaintCaptureGeometrySample::
                                    region)
                          : 0;
    const auto back = geometry
                          ? std::ranges::count(
                                *geometry,
                                core::Region::Back,
                                &core::PaintCaptureGeometrySample::
                                    region)
                          : 0;
    passed &= expect(
        geometry && geometry->size() == 39'214U &&
            front > 0 && side > 0 && back > 0 &&
            std::ranges::all_of(
                *geometry,
                [&](const auto& sample)
                {
                    return sample.uv_island >= 0 &&
                           sample.u >= 0.0 && sample.u <= 1.0 &&
                           sample.v >= 0.0 && sample.v <= 1.0 &&
                           finite(sample.world_position) &&
                           finite(sample.world_normal) &&
                           sample.projected &&
                           sample.screen.x >= 0.0 &&
                           sample.screen.x < viewport.width &&
                           sample.screen.y >= 0.0 &&
                           sample.screen.y < viewport.height;
                }),
        "reference-pose Paint capture geometry drifted");

    auto cancellation = std::stop_source{};
    cancellation.request_stop();
    passed &= expect(
        core::build_paint_capture_geometry(
            *sampling,
            *image,
            *current,
            4.0,
            view,
            viewport,
            cancellation.get_token()) ==
            std::unexpected(
                core::PaintCaptureGeometryError::Cancelled),
        "pre-cancelled Paint capture geometry published output");

    if (passed)
    {
        std::cout << "PASS paint_capture_geometry\n";
        return 0;
    }
    return 1;
}
