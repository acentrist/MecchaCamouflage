#include <meccha/application/paint_appearance_worker.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

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
        std::vector<core::Rgb8>(
            256U,
            core::Rgb8{64U, 96U, 128U});
    auto scene =
        std::vector<core::Rgb8>(
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
    base.clear();
    scene.clear();
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
            candidate->preview->dimension == 16U,
        "candidate work did not publish an immutable raster and preview");
    if (!candidate)
    {
        return 1;
    }

    passed &= expect(
        worker.start(
                  3U,
                  PaintAppearanceEvaluateWork{
                      prepared->model,
                      std::vector<core::AppearanceRgb>(
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

    worker.shutdown();
    const auto stopped = worker.start(
        4U,
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
