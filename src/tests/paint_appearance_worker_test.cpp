#include <meccha/application/paint_appearance_worker.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using namespace meccha::application;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_appearance_worker: " << message << '\n';
    }
    return condition;
}

auto wait_for(PaintAppearanceWorker& worker)
    -> std::optional<PaintAppearanceWorkCompletion>
{
    for (auto probe = 0; probe < 500; ++probe)
    {
        if (auto completion = worker.poll())
        {
            return completion;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return std::nullopt;
}

auto image() -> PaintTextureImage
{
    constexpr auto Dimension = std::uint32_t{16U};
    constexpr auto Bytes =
        static_cast<std::size_t>(Dimension) * Dimension * 4U;
    return PaintTextureImage{
        Dimension,
        std::make_shared<const std::vector<std::byte>>(
            Bytes,
            std::byte{0}),
        std::make_shared<const std::vector<std::byte>>(
            Bytes,
            std::byte{0}),
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::application;
    using namespace meccha::core;
    auto passed = true;
    auto worker = PaintAppearanceWorker{};

    auto model = PaintProjectiveModel{};
    model.width = 4U;
    model.height = 4U;
    model.replay_samples = 16U;
    model.calibration_samples = 16U;
    for (auto index = std::size_t{}; index < 16U; ++index)
    {
        model.samples.push_back(PaintProjectiveSample{
            index,
            index,
            Region::Front,
            0,
            (static_cast<double>(index % 4U) + 0.5) / 4.0,
            (static_cast<double>(index / 4U) + 0.5) / 4.0,
            0U,
            0U,
            1U,
            2U,
            1.0 / 3.0,
            1.0 / 3.0,
            1.0 / 3.0,
            true,
            true,
            true,
            {0.2, 0.2, 0.2},
            {0.3, 0.3, 0.3},
            {},
            {},
            1U,
        });
    }
    auto raster = PaintProjectiveRaster{
        std::vector<ResolvedPaintAppearance>(
            16U,
            ResolvedPaintAppearance{
                {64U, 96U, 128U},
                {0.0, 1.0, 0.0},
            }),
        std::vector<bool>(16U, true),
    };
    const auto owned_model =
        std::make_shared<const PaintProjectiveModel>(model);
    const auto owned_raster =
        std::make_shared<const PaintProjectiveRaster>(raster);

    passed &= expect(
        worker.start(
                  0U,
                  PaintAppearanceCandidateWork{
                      owned_model,
                      owned_raster,
                      image(),
                  })
                .error() ==
            PaintAppearanceWorkStartError::InvalidGeneration,
        "generation zero was accepted");
    passed &= expect(
        worker.start(
                  1U,
                  PaintAppearanceCandidateWork{
                      owned_model,
                      owned_raster,
                      image(),
                  })
            .has_value(),
        "projective candidate did not start");
    const auto completion = wait_for(worker);
    const auto* candidate = completion && completion->result
                                ? std::get_if<PaintAppearanceCandidate>(
                                      &*completion->result)
                                : nullptr;
    passed &= expect(
        completion && completion->generation == 1U && candidate &&
            candidate->preview &&
            candidate->preview->dimension == 16U &&
            candidate->readback_references &&
            candidate->readback_references->size() == 16U,
        "worker did not publish an immutable projective preview");

    passed &= expect(
        worker.start(2U, PaintAppearanceGeometryPrepareWork{}).has_value(),
        "invalid geometry request did not start for typed failure");
    const auto invalid = wait_for(worker);
    passed &= expect(
        invalid && !invalid->result &&
            invalid->result.error().kind ==
                PaintAppearanceWorkFailureKind::CaptureGeometry &&
            invalid->result.error().geometry_error ==
                PaintCaptureGeometryError::InvalidView,
        "invalid geometry did not retain its typed failure");

    worker.shutdown();
    passed &= expect(
        worker.start(
                  3U,
                  PaintAppearanceCandidateWork{
                      owned_model,
                      owned_raster,
                      image(),
                  })
                .error() == PaintAppearanceWorkStartError::Stopped,
        "appearance worker restarted after shutdown");

    if (passed)
    {
        std::cout << "PASS paint_appearance_worker\n";
    }
    return passed ? 0 : 1;
}
