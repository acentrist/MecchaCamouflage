#include <meccha/core/paint_appearance_capture.hpp>

#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_appearance_capture: "
                  << message << '\n';
    }
    return condition;
}

template <typename Pixel>
auto captured(
    const PaintAppearanceCameraFingerprint& camera,
    std::vector<Pixel> pixels)
    -> PaintAppearanceCapturedPass<Pixel>
{
    return PaintAppearanceCapturedPass<Pixel>{
        camera,
        std::make_shared<const std::vector<Pixel>>(
            std::move(pixels)),
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    const auto fingerprint =
        make_paint_appearance_camera_fingerprint(
            EspView{
                EspWorldPoint{0.0, -10.0, 0.0},
                0.0,
                90.0,
                0.0,
                90.0,
                1.0,
                EspAspectConstraint::MaintainXFov,
                1.0,
                1.0,
            },
            EspViewport{2.0, 2.0},
            2U,
            2U);
    auto passed = expect(
        fingerprint &&
            std::abs(fingerprint->direction.x) < 1.0e-12 &&
            std::abs(fingerprint->direction.y - 1.0) < 1.0e-12 &&
            std::abs(fingerprint->direction.z) < 1.0e-12,
        "the shared view did not produce its exact camera fingerprint");
    if (!fingerprint)
    {
        return 1;
    }
    const auto camera = *fingerprint;
    const auto evidence = PaintAppearanceCaptureEvidence{
        captured(
            camera,
            std::vector<Rgb8>{
                Rgb8{}, Rgb8{64U, 96U, 128U},
                Rgb8{}, Rgb8{},
            }),
        captured(
            camera,
            std::vector<AppearanceRgb>{
                {}, {1.0, 0.5, 0.25}, {}, {},
            }),
        captured(
            camera,
            std::vector<AppearanceRgb>{
                {}, {0.5, 0.3, 0.2}, {}, {},
            }),
        captured(
            camera,
            std::vector<AppearanceRgb>{
                {}, {0.4, 0.3, 0.2}, {}, {},
            }),
        captured(
            camera,
            std::vector<AppearanceRgb>{
                {}, {0.5, 0.5, 1.0}, {}, {},
            }),
        captured(
            camera,
            std::vector<double>{0.0, 10.0, 0.0, 0.0}),
        captured(
            camera,
            std::vector<Rgb8>{
                Rgb8{}, Rgb8{180U, 150U, 120U},
                Rgb8{}, Rgb8{},
            }),
        std::make_shared<
            const std::vector<PaintAppearanceSourcePixel>>(
            std::vector<PaintAppearanceSourcePixel>{
                {}, {true, 77U}, {}, {},
            }),
    };
    const auto observations =
        build_paint_appearance_observations(
            std::vector<PaintCaptureGeometrySample>{
                PaintCaptureGeometrySample{
                    Region::Front,
                    3,
                    0.25,
                    0.75,
                    Vector3d{0.0, 0.0, 0.0},
                    Vector3d{0.0, -1.0, 0.0},
                    true,
                    EspScreenPoint{1.2, 0.7},
                    0.0,
                    0.0,
                },
            },
            evidence);
    const auto query_pixels =
        build_paint_appearance_source_query_pixels(
            std::vector<PaintCaptureGeometrySample>{
                PaintCaptureGeometrySample{
                    Region::Front,
                    3,
                    0.25,
                    0.75,
                    Vector3d{},
                    Vector3d{0.0, -1.0, 0.0},
                    true,
                    EspScreenPoint{1.2, 0.7},
                    0.0,
                    0.0,
                },
                PaintCaptureGeometrySample{
                    Region::Side,
                    4,
                    0.5,
                    0.5,
                    Vector3d{},
                    Vector3d{0.0, -1.0, 0.0},
                    true,
                    EspScreenPoint{1.8, 0.2},
                    0.0,
                    0.0,
                },
            },
            2U,
            2U);
    passed &= expect(
        observations && observations->size() == 1U,
        "valid immutable pass evidence did not produce one observation");
    passed &= expect(
        query_pixels &&
            *query_pixels == std::vector<std::size_t>{1U},
        "projected geometry did not produce a deduplicated query pixel set");
    if (observations && observations->size() == 1U)
    {
        const auto& observation = observations->front();
        passed &= expect(
            observation.raster_pixel == 1U &&
                observation.u == 0.25 &&
                observation.v == 0.75 &&
                observation.base_color ==
                    Rgb8{64U, 96U, 128U} &&
                observation.final_hdr ==
                    AppearanceRgb{1.0, 0.5, 0.25} &&
                observation.tone_curve_available &&
                observation.intrinsic_emission_available &&
                observation.normal_available &&
                observation.depth_available &&
                observation.scene_depth == 10.0 &&
                observation.facing == -1.0 &&
                observation.safe &&
                observation.source_surface_key == 77U,
            "the observation did not retain exact projected pass values");
    }

    auto moved_camera = evidence;
    moved_camera.final_hdr.camera.location.x += 1.01;
    passed &= expect(
        build_paint_appearance_observations(
            std::vector<PaintCaptureGeometrySample>{
                PaintCaptureGeometrySample{
                    Region::Front,
                    3,
                    0.25,
                    0.75,
                    Vector3d{0.0, 0.0, 0.0},
                    Vector3d{0.0, -1.0, 0.0},
                    true,
                    EspScreenPoint{1.2, 0.7},
                    0.0,
                    0.0,
                },
            },
            moved_camera) ==
            std::unexpected(
                PaintAppearanceCaptureError::CameraChanged),
        "capture passes from a moved camera were combined");

    if (passed)
    {
        std::cout << "PASS paint_appearance_capture\n";
    }
    return passed ? 0 : 1;
}
