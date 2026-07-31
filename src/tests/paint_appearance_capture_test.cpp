#include <meccha/core/paint_appearance_capture.hpp>

#include <cstddef>
#include <cstdint>
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

    const auto feedback_fingerprint =
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
            EspViewport{8.0, 8.0},
            4U,
            4U);
    if (!feedback_fingerprint)
    {
        return 1;
    }
    auto feedback_model = PaintAppearanceModel{};
    feedback_model.width = 4U;
    feedback_model.height = 4U;
    for (auto pixel = std::size_t{}; pixel < 16U; ++pixel)
    {
        auto sample = PaintAppearanceModelSample{};
        sample.raster_pixel = pixel;
        sample.u = static_cast<double>(pixel % 4U) / 3.0;
        sample.v = static_cast<double>(pixel / 4U) / 3.0;
        feedback_model.samples.push_back(sample);
    }
    auto preview_albedo = std::vector<std::byte>(
        16U * 4U,
        std::byte{0xFF});
    for (auto pixel = std::size_t{}; pixel < 16U; ++pixel)
    {
        const auto offset = pixel * 4U;
        preview_albedo[offset] = std::byte{200U};
        preview_albedo[offset + 1U] = std::byte{100U};
        preview_albedo[offset + 2U] = std::byte{20U};
    }
    const auto references =
        build_paint_appearance_readback_references(
            feedback_model,
            4U,
            preview_albedo);
    const auto expected_linear = AppearanceRgb{
        appearance_srgb_to_linear(200.0 / 255.0),
        appearance_srgb_to_linear(100.0 / 255.0),
        appearance_srgb_to_linear(20.0 / 255.0),
    };
    const auto target_hdr = std::make_shared<
        const std::vector<AppearanceRgb>>(
        16U,
        AppearanceRgb{1.0, 0.5, 0.25});
    const auto feedback = references
                              ? prepare_paint_appearance_feedback(
                                    *feedback_fingerprint,
                                    *references,
                                    PaintAppearanceFeedbackEvidence{
                                        PaintAppearanceCapturedPass<
                                            AppearanceRgb>{
                                            *feedback_fingerprint,
                                            std::make_shared<
                                                const std::vector<
                                                    AppearanceRgb>>(
                                                16U,
                                                expected_linear),
                                        },
                                        PaintAppearanceCapturedPass<
                                            AppearanceRgb>{
                                            *feedback_fingerprint,
                                            target_hdr,
                                        },
                                    })
                              : std::expected<
                                    PaintAppearanceFeedback,
                                    PaintAppearanceCaptureError>{
                                    std::unexpected(
                                        PaintAppearanceCaptureError::
                                            InvalidEvidence)};
    passed &= expect(
        references && references->size() == 16U &&
            feedback && feedback->target_hdr == target_hdr &&
            feedback->readback.ok &&
            feedback->readback.transform ==
                AppearanceReadbackTransform::Identity &&
            feedback->camera_stable,
        "target-visible feedback was not camera-checked and calibrated");

    auto swapped_base = std::vector<AppearanceRgb>(
        16U,
        AppearanceRgb{
            expected_linear.b,
            expected_linear.g,
            expected_linear.r,
        });
    auto swapped_evidence = PaintAppearanceFeedbackEvidence{
        PaintAppearanceCapturedPass<AppearanceRgb>{
            *feedback_fingerprint,
            std::make_shared<const std::vector<AppearanceRgb>>(
                std::move(swapped_base)),
        },
        PaintAppearanceCapturedPass<AppearanceRgb>{
            *feedback_fingerprint,
            target_hdr,
        },
    };
    const auto swapped_feedback = references
                                      ? prepare_paint_appearance_feedback(
                                            *feedback_fingerprint,
                                            *references,
                                            swapped_evidence)
                                      : std::expected<
                                            PaintAppearanceFeedback,
                                            PaintAppearanceCaptureError>{
                                            std::unexpected(
                                                PaintAppearanceCaptureError::
                                                    InvalidEvidence)};
    passed &= expect(
        swapped_feedback && swapped_feedback->readback.ok &&
            swapped_feedback->readback.transform ==
                AppearanceReadbackTransform::SwapRedBlue,
        "swapped target BaseColor readback was not calibrated");
    auto resized_viewport_evidence = swapped_evidence;
    resized_viewport_evidence.final_hdr.camera.viewport_width =
        9.0;
    passed &= expect(
        references &&
            prepare_paint_appearance_feedback(
                *feedback_fingerprint,
                *references,
                resized_viewport_evidence) ==
                std::unexpected(
                    PaintAppearanceCaptureError::CameraChanged),
        "target-visible feedback from a resized viewport was accepted");
    swapped_evidence.final_hdr.camera.location.x += 1.01;
    passed &= expect(
        references &&
            prepare_paint_appearance_feedback(
                *feedback_fingerprint,
                *references,
                swapped_evidence) ==
                std::unexpected(
                    PaintAppearanceCaptureError::CameraChanged),
        "target-visible feedback from a moved camera was accepted");

    constexpr auto E0Dimension = std::uint32_t{16U};
    constexpr auto E0PixelCount =
        static_cast<std::size_t>(E0Dimension) * E0Dimension;
    const auto e0_fingerprint =
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
            EspViewport{32.0, 32.0},
            E0Dimension,
            E0Dimension);
    auto e0_references =
        std::vector<PaintAppearanceReadbackReference>{};
    e0_references.reserve(E0PixelCount);
    for (auto pixel = std::size_t{};
         pixel < E0PixelCount;
         ++pixel)
    {
        e0_references.push_back(
            PaintAppearanceReadbackReference{
                pixel,
                AppearanceRgb{0.2, 0.3, 0.4},
            });
    }
    const auto swapped_base_linear =
        AppearanceRgb{0.4, 0.3, 0.2};
    const auto swapped_intrinsic_linear =
        AppearanceRgb{0.402, 0.302, 0.202};
    const auto e0 = e0_fingerprint
                        ? prepare_paint_appearance_target_e0(
                              *e0_fingerprint,
                              e0_references,
                              PaintAppearanceTargetE0Evidence{
                                  captured(
                                      *e0_fingerprint,
                                      std::vector<AppearanceRgb>(
                                          E0PixelCount,
                                          swapped_base_linear)),
                                  captured(
                                      *e0_fingerprint,
                                      std::vector<AppearanceRgb>(
                                          E0PixelCount,
                                          swapped_intrinsic_linear)),
                              },
                              AppearanceReadbackCalibration{
                                  true,
                                  AppearanceReadbackTransform::
                                      SwapRedBlue,
                                  0.0,
                                  0.2,
                              })
                        : std::expected<
                              PaintAppearanceTargetE0,
                              PaintAppearanceCaptureError>{
                              std::unexpected(
                                  PaintAppearanceCaptureError::
                                      InvalidEvidence)};
    passed &= expect(
        e0 && e0->camera_stable &&
            e0->paired_samples == E0PixelCount &&
            e0->noise.ok &&
            std::abs(e0->noise.median - 0.002) < 1.0e-12 &&
            std::abs(e0->noise.threshold - 0.012) < 1.0e-12,
        "target E0 captures did not produce calibrated emission noise");
    if (e0_fingerprint)
    {
        auto changed_e0 = PaintAppearanceTargetE0Evidence{
            captured(
                *e0_fingerprint,
                std::vector<AppearanceRgb>(
                    E0PixelCount,
                    swapped_base_linear)),
            captured(
                *e0_fingerprint,
                std::vector<AppearanceRgb>(
                    E0PixelCount,
                    swapped_intrinsic_linear)),
        };
        changed_e0.intrinsic_emission_hdr.camera.viewport_height =
            31.0;
        passed &= expect(
            prepare_paint_appearance_target_e0(
                *e0_fingerprint,
                e0_references,
                changed_e0,
                AppearanceReadbackCalibration{
                    true,
                    AppearanceReadbackTransform::Identity,
                    0.0,
                    0.2,
                }) ==
                std::unexpected(
                    PaintAppearanceCaptureError::CameraChanged),
            "target E0 evidence from a changed camera was accepted");
    }

    if (passed)
    {
        std::cout << "PASS paint_appearance_capture\n";
    }
    return passed ? 0 : 1;
}
