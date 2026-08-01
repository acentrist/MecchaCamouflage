#include <meccha/application/mesh_profile_codec.hpp>
#include <meccha/core/paint_capture_request.hpp>
#include <meccha/core/paint_deformation.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stop_token>
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
        std::cerr << "FAIL paint_capture_request: "
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
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;

    if (argc != 2)
    {
        std::cerr
            << "usage: paint_capture_request_test "
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
    const auto transforms =
        core::compose_reference_bone_transforms(
            *sampling->bones,
            *sampling->reference_bone_transforms);
    if (!transforms)
    {
        std::cerr << "reference skeleton composition failed\n";
        return 1;
    }

    constexpr auto Width = std::uint32_t{320U};
    constexpr auto Height = std::uint32_t{180U};
    constexpr auto Pixels =
        static_cast<std::size_t>(Width) * Height;
    const auto projected = std::make_shared<
        const std::vector<core::ResolvedPaintAppearance>>(
        Pixels,
        core::ResolvedPaintAppearance{
            core::Rgb8{70U, 80U, 90U},
            core::Material{0.2, 0.7, 0.1},
        });
    auto settings = core::PaintSettings{};
    settings.front_mode = core::RegionMode::Paint;
    settings.side_mode = core::RegionMode::Paint;
    settings.back_mode = core::RegionMode::Paint;
    settings.brush_size_texels = 4.0;
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
    const auto input = core::PaintCaptureInput{
        *sampling,
        *image,
        *transforms,
        settings,
        view,
        core::EspViewport{
            static_cast<double>(Width),
            static_cast<double>(Height),
        },
        core::PaintCaptureRaster{
            Width,
            Height,
            projected,
        },
    };
    const auto request =
        core::build_paint_capture_request(input);
    passed &= expect(
        request && request->samples.size() == 39'214U &&
            request->profile == sampling->identity &&
            request->settings == settings &&
            std::ranges::all_of(
                request->samples,
                [](const auto& sample)
                {
                    return sample.safe &&
                           sample.has_current_view_position &&
                           sample.projected_appearance_available &&
                           sample.projected_appearance ==
                               core::ResolvedPaintAppearance{
                                   core::Rgb8{70U, 80U, 90U},
                                   core::Material{0.2, 0.7, 0.1},
                               };
                }),
        "immutable projected appearances did not map to Paint samples");

    auto missing_projected = input;
    missing_projected.raster.projected_appearances.reset();
    passed &= expect(
        core::build_paint_capture_request(missing_projected) ==
            std::unexpected(
                core::PaintCaptureRequestError::
                    MissingProjectedAppearance),
        "normal Paint accepted a missing projected appearance raster");

    auto malformed = input;
    malformed.raster.width = 0U;
    passed &= expect(
        core::build_paint_capture_request(malformed) ==
            std::unexpected(
                core::PaintCaptureRequestError::InvalidRaster),
        "a malformed capture raster was accepted");

    auto cancellation = std::stop_source{};
    cancellation.request_stop();
    passed &= expect(
        core::build_paint_capture_request(
            input,
            cancellation.get_token()) ==
            std::unexpected(
                core::PaintCaptureRequestError::Cancelled),
        "pre-cancelled Paint capture request published output");

    if (passed)
    {
        std::cout << "PASS paint_capture_request\n";
        return 0;
    }
    return 1;
}
