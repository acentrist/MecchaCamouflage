#include <meccha/application/paint_appearance_worker.hpp>
#include <meccha/application/mesh_profile_codec.hpp>
#include <meccha/core/paint_deformation.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_appearance_worker: "
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

template <typename Pixel>
auto captured(
    const core::PaintAppearanceCameraFingerprint& camera,
    std::vector<Pixel> pixels)
    -> core::PaintAppearanceCapturedPass<Pixel>
{
    return core::PaintAppearanceCapturedPass<Pixel>{
        camera,
        std::make_shared<const std::vector<Pixel>>(
            std::move(pixels)),
    };
}

auto observations()
    -> std::vector<core::PaintAppearanceObservation>
{
    auto output =
        std::vector<core::PaintAppearanceObservation>{};
    output.reserve(256U);
    for (auto pixel = std::size_t{}; pixel < 256U; ++pixel)
    {
        output.push_back(
            core::PaintAppearanceObservation{
                pixel,
                static_cast<double>(pixel % 16U) / 15.0,
                static_cast<double>(pixel / 16U) / 15.0,
                core::Rgb8{64U, 96U, 128U},
                core::AppearanceRgb{1.0, 0.5, 0.25},
                core::AppearanceRgb{0.5, 0.33, 0.2},
                true,
                core::AppearanceRgb{
                    core::appearance_srgb_to_linear(
                        64.0 / 255.0),
                    core::appearance_srgb_to_linear(
                        96.0 / 255.0),
                    core::appearance_srgb_to_linear(
                        128.0 / 255.0),
                },
                true,
                core::AppearanceRgb{0.5, 0.5, 1.0},
                true,
                100.0,
                true,
                1.0,
                true,
            });
    }
    return output;
}

auto image() -> PaintTextureImage
{
    constexpr auto bytes = std::size_t{16U * 16U * 4U};
    return PaintTextureImage{
        16U,
        std::make_shared<const std::vector<std::byte>>(
            bytes,
            std::byte{0x44}),
        std::make_shared<const std::vector<std::byte>>(
            bytes,
            std::byte{0x55}),
    };
}

auto wait_for(PaintAppearanceWorker& worker)
    -> std::optional<PaintAppearanceWorkCompletion>
{
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        if (auto completed = worker.poll())
        {
            return completed;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;
    using namespace meccha::application;

    if (argc != 2)
    {
        std::cerr << "usage: paint_appearance_worker_test "
                     "<profile-directory>\n";
        return 2;
    }
    const auto profile_root = std::filesystem::path{argv[1]};
    auto passed = true;
    auto worker = PaintAppearanceWorker{};
    const auto invalid = worker.start(
        0U,
        PaintAppearancePrepareWork{});
    passed &= expect(
        !invalid &&
            invalid.error() ==
                PaintAppearanceWorkStartError::
                    InvalidGeneration,
        "generation zero was accepted");

    auto source_observations = observations();
    passed &= expect(
        worker.start(
                  1U,
                  PaintAppearancePrepareWork{
                      16U,
                      16U,
                      source_observations,
                      true,
                      std::nullopt,
                  })
                .has_value(),
        "model preparation did not start");
    source_observations.clear();
    const auto busy = worker.start(
        2U,
        PaintAppearancePrepareWork{});
    passed &= expect(
        !busy &&
            busy.error() ==
                PaintAppearanceWorkStartError::Busy,
        "a second concurrent appearance task was accepted");
    const auto prepared_completion = wait_for(worker);
    const auto* prepared =
        prepared_completion &&
                prepared_completion->result
            ? std::get_if<PaintAppearancePrepared>(
                  &*prepared_completion->result)
            : nullptr;
    passed &= expect(
        prepared_completion &&
            prepared_completion->generation == 1U &&
            prepared && prepared->model &&
            prepared->model->samples.size() == 256U &&
            prepared->parameters.size() == 16U,
        "immutable source observations did not publish a tagged model");
    if (!prepared)
    {
        return 1;
    }

    auto base =
        std::make_shared<const std::vector<core::Rgb8>>(
            256U,
            core::Rgb8{64U, 96U, 128U});
    auto scene =
        std::make_shared<const std::vector<core::Rgb8>>(
            256U,
            core::Rgb8{180U, 150U, 120U});
    passed &= expect(
        worker.start(
                  2U,
                  PaintAppearanceCandidateWork{
                      prepared->model,
                      base,
                      scene,
                      prepared->parameters,
                      5.0,
                      1024U,
                      image(),
                  })
                .has_value(),
        "candidate composition did not start");
    base.reset();
    scene.reset();
    const auto candidate_completion = wait_for(worker);
    const auto* candidate =
        candidate_completion &&
                candidate_completion->result
            ? std::get_if<PaintAppearanceCandidate>(
                  &*candidate_completion->result)
            : nullptr;
    passed &= expect(
        candidate_completion &&
            candidate_completion->generation == 2U &&
            candidate && candidate->appearances &&
            candidate->appearances->size() == 256U &&
            candidate->preview &&
            candidate->preview->dimension == 16U &&
            candidate->readback_references &&
            candidate->readback_references->size() == 256U,
        "candidate work did not publish an immutable raster and preview");
    if (!candidate)
    {
        return 1;
    }

    const auto feedback_camera =
        core::PaintAppearanceCameraFingerprint{
            16U,
            16U,
            32.0,
            32.0,
            core::EspWorldPoint{0.0, -10.0, 0.0},
            core::EspWorldPoint{0.0, 1.0, 0.0},
            90.0,
        };
    auto target_base = std::vector<core::AppearanceRgb>(256U);
    auto target_intrinsic =
        std::vector<core::AppearanceRgb>(256U);
    for (const auto& reference :
         *candidate->readback_references)
    {
        target_base[reference.raster_pixel] =
            reference.expected_linear;
        target_intrinsic[reference.raster_pixel] =
            core::AppearanceRgb{
                reference.expected_linear.r + 0.002,
                reference.expected_linear.g + 0.002,
                reference.expected_linear.b + 0.002,
            };
    }
    passed &= expect(
        worker.start(
                  30U,
                  PaintAppearanceTargetE0PrepareWork{
                      feedback_camera,
                      candidate->readback_references,
                      core::PaintAppearanceFeedbackEvidence{
                          captured(
                              feedback_camera,
                              target_base),
                          captured(
                              feedback_camera,
                              std::vector<
                                  core::AppearanceRgb>(
                                  256U,
                                  core::AppearanceRgb{
                                      1.0,
                                      0.5,
                                      0.25,
                                  })),
                      },
                      core::PaintAppearanceTargetE0Evidence{
                          captured(
                              feedback_camera,
                              std::move(target_base)),
                          captured(
                              feedback_camera,
                              std::move(target_intrinsic)),
                      },
                  })
                .has_value(),
        "target E0 preparation did not start");
    const auto target_e0_completion = wait_for(worker);
    const auto* target_e0 =
        target_e0_completion && target_e0_completion->result
            ? std::get_if<PaintAppearanceTargetE0Prepared>(
                  &*target_e0_completion->result)
            : nullptr;
    passed &= expect(
        target_e0_completion &&
            target_e0_completion->generation == 30U &&
            target_e0 && target_e0->feedback.camera_stable &&
            target_e0->feedback.readback.ok &&
            target_e0->target_e0.camera_stable &&
            target_e0->target_e0.paired_samples == 256U &&
            target_e0->target_e0.noise.ok,
        "worker did not publish calibrated target E0 feedback");

    passed &= expect(
        worker.start(
                  3U,
                  PaintAppearanceEvaluateWork{
                      prepared->model,
                      std::make_shared<const std::vector<
                          core::AppearanceRgb>>(
                          256U,
                          core::AppearanceRgb{
                              1.0,
                              0.5,
                              0.25,
                          }),
                      true,
                      true,
                      core::AppearanceReadbackTransform::
                          Identity,
                  })
                .has_value(),
        "feedback evaluation did not start");
    const auto evaluated_completion = wait_for(worker);
    const auto* evaluated =
        evaluated_completion &&
                evaluated_completion->result
            ? std::get_if<PaintAppearanceEvaluated>(
                  &*evaluated_completion->result)
            : nullptr;
    passed &= expect(
        evaluated_completion &&
            evaluated_completion->generation == 3U &&
            evaluated &&
            evaluated->evaluation.paired_samples == 256 &&
            evaluated->evaluation.loss == 0.0,
        "feedback evaluation did not publish its tagged zero-loss result");

    passed &= expect(
        worker.start(
                  31U,
                  PaintAppearanceResolveWork{
                      prepared->model,
                      std::make_shared<const std::vector<
                          core::Rgb8>>(
                          256U,
                          core::Rgb8{64U, 96U, 128U}),
                      std::make_shared<const std::vector<
                          core::Rgb8>>(
                          256U,
                          core::Rgb8{180U, 150U, 120U}),
                      core::paint_appearance_fallback_parameters(
                          *prepared->model),
                  })
                .has_value(),
        "final appearance resolution did not start");
    const auto resolved_completion = wait_for(worker);
    const auto* resolved =
        resolved_completion && resolved_completion->result
            ? std::get_if<PaintAppearanceResolved>(
                  &*resolved_completion->result)
            : nullptr;
    passed &= expect(
        resolved_completion &&
            resolved_completion->generation == 31U && resolved &&
            resolved->appearances &&
            resolved->appearances->size() == 256U &&
            resolved->parameters.size() == 16U,
        "worker did not publish final immutable appearances");

    passed &= expect(
        worker.start(
                  4U,
                  PaintAppearanceGeometryPrepareWork{})
                .has_value(),
        "immutable geometry preparation did not start");
    const auto invalid_capture_completion = wait_for(worker);
    passed &= expect(
        invalid_capture_completion &&
            invalid_capture_completion->generation == 4U &&
            !invalid_capture_completion->result &&
            invalid_capture_completion->result.error().kind ==
                PaintAppearanceWorkFailureKind::
                    CaptureGeometry &&
            invalid_capture_completion->result.error()
                    .geometry_error ==
                core::PaintCaptureGeometryError::InvalidView,
        "an invalid geometry seed did not retain its typed failure");

    const auto sampling = decode_paint_sampling_profile(
        read_file(
            profile_root /
            "paintman.mesh-profile-v2.json"),
        core::BodyProfile::Round);
    const auto image_profile =
        decode_canonical_image_profile(
            read_file(
                profile_root /
                "paintman.image-profile-v2.json"),
            core::BodyProfile::Round);
    const auto transforms = sampling
                                ? core::compose_reference_bone_transforms(
                                      *sampling->bones,
                                      *sampling
                                           ->reference_bone_transforms)
                                : std::expected<
                                      std::vector<core::
                                          PaintReferenceBoneTransform>,
                                      core::PaintDeformationError>{
                                      std::unexpected(
                                          core::PaintDeformationError::
                                              InvalidSkeleton)};
    passed &= expect(
        sampling && image_profile && transforms,
        "packaged capture profiles did not decode");
    if (!sampling || !image_profile || !transforms)
    {
        return 1;
    }
    constexpr auto CaptureWidth = std::uint32_t{64U};
    constexpr auto CaptureHeight = std::uint32_t{36U};
    constexpr auto CapturePixels =
        static_cast<std::size_t>(CaptureWidth) *
        CaptureHeight;
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
    auto geometry_work = PaintAppearanceGeometryPrepareWork{
        *sampling,
        *image_profile,
        *transforms,
        4.0,
        view,
        core::EspViewport{
            static_cast<double>(CaptureWidth),
            static_cast<double>(CaptureHeight),
        },
    };
    passed &= expect(
        worker.start(5U, geometry_work).has_value(),
        "packaged immutable geometry seed did not start");
    geometry_work.current_world_transforms.clear();
    const auto geometry_completion = wait_for(worker);
    const auto* geometry_prepared =
        geometry_completion && geometry_completion->result
            ? std::get_if<PaintAppearanceGeometryPrepared>(
                  &*geometry_completion->result)
            : nullptr;
    passed &= expect(
        geometry_completion &&
            geometry_completion->generation == 5U &&
            geometry_prepared && geometry_prepared->geometry &&
            geometry_prepared->source_query_pixels &&
            !geometry_prepared->geometry->empty() &&
            !geometry_prepared->source_query_pixels->empty() &&
            geometry_prepared->source_query_pixels->size() <=
                geometry_prepared->geometry->size(),
        "worker did not publish owned geometry and bounded query pixels");
    if (!geometry_prepared)
    {
        return 1;
    }
    const auto camera =
        core::PaintAppearanceCameraFingerprint{
            CaptureWidth,
            CaptureHeight,
            static_cast<double>(CaptureWidth),
            static_cast<double>(CaptureHeight),
            view.location,
            core::EspWorldPoint{0.0, 1.0, 0.0},
            view.field_of_view_degrees,
        };
    const auto linear_base = core::AppearanceRgb{
        core::appearance_srgb_to_linear(64.0 / 255.0),
        core::appearance_srgb_to_linear(96.0 / 255.0),
        core::appearance_srgb_to_linear(128.0 / 255.0),
    };
    auto capture_work = PaintAppearanceCapturePrepareWork{
        geometry_prepared->geometry,
        core::PaintAppearanceCaptureEvidence{
            captured(
                camera,
                std::vector<core::Rgb8>(
                    CapturePixels,
                    core::Rgb8{64U, 96U, 128U})),
            captured(
                camera,
                std::vector<core::AppearanceRgb>(
                    CapturePixels,
                    core::AppearanceRgb{1.0, 0.5, 0.25})),
            captured(
                camera,
                std::vector<core::AppearanceRgb>(
                    CapturePixels,
                    core::AppearanceRgb{0.5, 0.33, 0.2})),
            captured(
                camera,
                std::vector<core::AppearanceRgb>(
                    CapturePixels,
                    linear_base)),
            captured(
                camera,
                std::vector<core::AppearanceRgb>(
                    CapturePixels,
                    core::AppearanceRgb{0.5, 0.5, 1.0})),
            captured(
                camera,
                std::vector<double>(CapturePixels, 100.0)),
            captured(
                camera,
                std::vector<core::Rgb8>(
                    CapturePixels,
                    core::Rgb8{180U, 150U, 120U})),
            std::make_shared<const std::vector<
                core::PaintAppearanceSourcePixel>>(
                CapturePixels,
                core::PaintAppearanceSourcePixel{true, 77U}),
        },
        true,
        std::nullopt,
    };
    passed &= expect(
        worker.start(6U, capture_work).has_value(),
        "packaged immutable capture seed did not start");
    capture_work.geometry.reset();
    capture_work.evidence.source_pixels.reset();
    const auto captured_completion = wait_for(worker);
    const auto* captured_prepared =
        captured_completion && captured_completion->result
            ? std::get_if<PaintAppearancePrepared>(
                  &*captured_completion->result)
            : nullptr;
    passed &= expect(
        captured_completion &&
            captured_completion->generation == 6U &&
            captured_prepared && captured_prepared->model &&
            captured_prepared->model->supported_samples > 0U &&
            !captured_prepared->parameters.empty(),
        "worker did not deform, project, materialize, and fit its owned "
        "capture seed");

    worker.shutdown();
    const auto stopped = worker.start(
        7U,
        PaintAppearancePrepareWork{});
    passed &= expect(
        !stopped &&
            stopped.error() ==
                PaintAppearanceWorkStartError::Stopped,
        "appearance work restarted after terminal shutdown");

    if (passed)
    {
        std::cout << "PASS paint_appearance_worker\n";
    }
    return passed ? 0 : 1;
}
