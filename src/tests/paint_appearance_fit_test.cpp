#include <meccha/core/paint_appearance_fit.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
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
        std::cerr << "FAIL paint_appearance_fit: "
                  << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    constexpr auto Width = std::uint32_t{16U};
    constexpr auto Height = std::uint32_t{16U};
    constexpr auto PixelCount =
        static_cast<std::size_t>(Width) * Height;
    auto observations =
        std::vector<PaintAppearanceObservation>{};
    observations.reserve(PixelCount);
    for (auto pixel = std::size_t{};
         pixel < PixelCount;
         ++pixel)
    {
        observations.push_back(
            PaintAppearanceObservation{
                pixel,
                static_cast<double>(pixel % Width) /
                    static_cast<double>(Width - 1U),
                static_cast<double>(pixel / Width) /
                    static_cast<double>(Height - 1U),
                Rgb8{64U, 96U, 128U},
                AppearanceRgb{1.0, 0.5, 0.25},
                AppearanceRgb{0.5, 0.33, 0.2},
                true,
                AppearanceRgb{
                    appearance_srgb_to_linear(64.0 / 255.0),
                    appearance_srgb_to_linear(96.0 / 255.0),
                    appearance_srgb_to_linear(128.0 / 255.0),
                },
                true,
                AppearanceRgb{0.5, 0.5, 1.0},
                true,
                100.0,
                true,
                1.0,
                true,
                42U,
            });
    }

    const auto model = prepare_paint_appearance_model(
        Width,
        Height,
        observations,
        true);
    auto cancelled_source = std::stop_source{};
    cancelled_source.request_stop();
    const auto cancelled_model =
        prepare_paint_appearance_model(
            Width,
            Height,
            observations,
            true,
            std::nullopt,
            cancelled_source.get_token());
    auto passed = true;
    passed &= expect(
        model && model->supported_samples == PixelCount &&
            model->samples.size() == PixelCount &&
            model->clusters.size() == 4U &&
            model->emission_roi_samples == 0U &&
            model->samples.front().source_surface_key == 42U,
        "valid source evidence did not produce the bounded deterministic "
        "cluster model");
    passed &= expect(
        cancelled_model ==
            std::unexpected(
                PaintAppearanceFitError::Cancelled),
        "appearance model preparation ignored cancellation");

    const auto parameters = model
                                ? paint_appearance_parameters(
                                      *model)
                                : std::vector<double>{};
    auto base_colors =
        std::vector<Rgb8>(PixelCount, Rgb8{64U, 96U, 128U});
    auto scene_colors =
        std::vector<Rgb8>(PixelCount, Rgb8{180U, 150U, 120U});
    const auto raster = model
                            ? resolve_paint_appearance_raster(
                                  *model,
                                  base_colors,
                                  scene_colors,
                                  parameters)
                            : std::expected<
                                  std::vector<
                                      ResolvedPaintAppearance>,
                                  PaintAppearanceFitError>{
                                  std::unexpected(
                                      PaintAppearanceFitError::
                                          InvalidModel)};
    passed &= expect(
        parameters.size() == 16U && raster &&
            raster->size() == PixelCount,
        "cluster parameters did not resolve one bounded appearance raster");
    const auto trial = model && raster
                           ? build_paint_appearance_trial_plan(
                                 *model,
                                 *raster,
                                 5.0,
                                 1024U)
                           : std::expected<
                                 PaintPlan,
                                 PaintAppearanceFitError>{
                                 std::unexpected(
                                     PaintAppearanceFitError::
                                         InvalidModel)};
    passed &= expect(
        trial &&
            trial->strokes.size() == PixelCount &&
            trial->fill_end == 0U &&
            trial->fill_count == 0U &&
            trial->paint_count == PixelCount &&
            trial->source_paint_count == PixelCount &&
            trial->compressed_paint_count == 0U &&
            trial->texture_dimension == 1024U &&
            trial->strokes.front().include_scene_lighting,
        "the appearance candidate did not become one immutable preview-only "
        "Paint plan");

    const auto evaluation = model
                                ? evaluate_paint_appearance_response(
                                      *model,
                                      std::span<const AppearanceRgb>{
                                          &observations.front().final_hdr,
                                          0U},
                                      true,
                                      true,
                                      AppearanceReadbackTransform::
                                          Identity)
                                : std::expected<
                                      PaintAppearanceEvaluation,
                                      PaintAppearanceFitError>{
                                      std::unexpected(
                                          PaintAppearanceFitError::
                                              InvalidModel)};
    passed &= expect(
        !evaluation &&
            evaluation.error() ==
                PaintAppearanceFitError::InvalidResponse,
        "a response with the wrong raster size was accepted");

    auto exact_response =
        std::vector<AppearanceRgb>(
            PixelCount,
            AppearanceRgb{1.0, 0.5, 0.25});
    const auto exact_evaluation = model
                                      ? evaluate_paint_appearance_response(
                                            *model,
                                            exact_response,
                                            true,
                                            true,
                                            AppearanceReadbackTransform::
                                                Identity)
                                      : std::expected<
                                            PaintAppearanceEvaluation,
                                            PaintAppearanceFitError>{
                                            std::unexpected(
                                                PaintAppearanceFitError::
                                                    InvalidModel)};
    passed &= expect(
        exact_evaluation &&
            exact_evaluation->paired_samples ==
                static_cast<int>(PixelCount) &&
            exact_evaluation->loss == 0.0 &&
            exact_evaluation->median_delta_e == 0.0 &&
            exact_evaluation->clusters.size() ==
                model->clusters.size(),
        "exact target feedback did not evaluate as a zero-loss fit");

    const auto fallback_parameters = model
                                         ? paint_appearance_fallback_parameters(
                                               *model)
                                         : std::vector<double>{};
    auto fit_evaluation = [model](double loss)
    {
        auto output = PaintAppearanceEvaluation{};
        output.paired_samples = 256;
        output.camera_stable = true;
        output.readback_calibrated = true;
        output.loss = loss;
        output.median_delta_e = 0.04;
        output.median_chromaticity_delta = 0.01;
        output.maximum_chromaticity_delta = 0.02;
        output.clusters.resize(
            model ? model->clusters.size() : 0U);
        for (auto& cluster : output.clusters)
        {
            cluster.paired_samples = 64;
            cluster.loss = loss;
            cluster.median_delta_e = 0.04;
        }
        return output;
    };
    auto fit = model
                   ? begin_paint_appearance_fit(
                         *model,
                         fallback_parameters,
                         fit_evaluation(1.0))
                   : std::expected<
                         PaintAppearanceFitSession,
                         PaintAppearanceFitError>{
                         std::unexpected(
                             PaintAppearanceFitError::
                                 InvalidModel)};
    auto trial_count = 0;
    for (auto iteration = 0;
         fit && iteration < AppearanceSpsaIterations;
         ++iteration)
    {
        const auto plus = next_paint_appearance_trial(*fit);
        passed &= expect(
            plus && *plus &&
                (*plus)->iteration == iteration &&
                (*plus)->phase ==
                    PaintAppearanceTrialPhase::Plus,
            "fit session did not publish its next SPSA plus trial");
        if (!plus || !*plus)
        {
            break;
        }
        ++trial_count;
        passed &= expect(
            observe_paint_appearance_trial(
                *fit,
                fit_evaluation(0.80 - 0.10 * iteration))
                .has_value(),
            "fit session rejected a valid SPSA plus response");

        const auto minus = next_paint_appearance_trial(*fit);
        passed &= expect(
            minus && *minus &&
                (*minus)->iteration == iteration &&
                (*minus)->phase ==
                    PaintAppearanceTrialPhase::Minus &&
                (*minus)->parameters != (*plus)->parameters,
            "fit session did not publish its paired SPSA minus trial");
        if (!minus || !*minus)
        {
            break;
        }
        ++trial_count;
        passed &= expect(
            observe_paint_appearance_trial(
                *fit,
                fit_evaluation(0.90 - 0.10 * iteration))
                .has_value(),
            "fit session rejected a valid SPSA minus response");
    }
    const auto fitted = fit
                            ? finish_paint_appearance_fit(*fit)
                            : std::expected<
                                  PaintAppearanceFitResult,
                                  PaintAppearanceFitError>{
                                  std::unexpected(
                                      PaintAppearanceFitError::
                                          InvalidInput)};
    passed &= expect(
        fallback_parameters.size() == parameters.size() &&
            fallback_parameters[0] == 1.0 &&
            fallback_parameters[1] == 0.0 &&
            fallback_parameters[2] ==
                AppearanceFallbackRoughness &&
            fallback_parameters[3] == 0.0 &&
            trial_count == AppearanceSpsaIterations * 2 &&
            fitted && fitted->accepted &&
            fitted->iterations == AppearanceSpsaIterations &&
            std::abs(fitted->evaluation.loss - 0.60) < 1.0e-12 &&
            fitted->parameters != fallback_parameters,
        "bounded SPSA fit did not accept its best observed trial");

    auto rejected_fit = model
                            ? begin_paint_appearance_fit(
                                  *model,
                                  fallback_parameters,
                                  fit_evaluation(1.0))
                            : std::expected<
                                  PaintAppearanceFitSession,
                                  PaintAppearanceFitError>{
                                  std::unexpected(
                                      PaintAppearanceFitError::
                                          InvalidModel)};
    auto rejected_sequence_ok = rejected_fit.has_value();
    for (auto iteration = 0;
         rejected_sequence_ok &&
         iteration < AppearanceSpsaIterations;
         ++iteration)
    {
        const auto plus =
            next_paint_appearance_trial(*rejected_fit);
        if (!plus || !*plus ||
            !observe_paint_appearance_trial(
                 *rejected_fit,
                 fit_evaluation(0.90)))
        {
            rejected_sequence_ok = false;
            break;
        }
        const auto minus =
            next_paint_appearance_trial(*rejected_fit);
        if (!minus || !*minus ||
            !observe_paint_appearance_trial(
                 *rejected_fit,
                 fit_evaluation(0.91)))
        {
            rejected_sequence_ok = false;
            break;
        }
    }
    const auto rejected = rejected_sequence_ok
                              ? finish_paint_appearance_fit(
                                    *rejected_fit)
                              : std::expected<
                                    PaintAppearanceFitResult,
                                    PaintAppearanceFitError>{
                                    std::unexpected(
                                        PaintAppearanceFitError::
                                            InvalidInput)};
    passed &= expect(
        rejected && !rejected->accepted &&
            rejected->parameters == fallback_parameters &&
            rejected->evaluation.loss == 1.0,
        "an insufficient improvement did not retain the safe fallback");

    auto invalid_parameters = parameters;
    invalid_parameters.pop_back();
    passed &= expect(
        model &&
            resolve_paint_appearance_raster(
                *model,
                base_colors,
                scene_colors,
                invalid_parameters) ==
                std::unexpected(
                    PaintAppearanceFitError::
                        InvalidParameters),
        "a truncated cluster parameter vector was accepted");

    if (passed)
    {
        std::cout << "PASS paint_appearance_fit\n";
    }
    return passed ? 0 : 1;
}
