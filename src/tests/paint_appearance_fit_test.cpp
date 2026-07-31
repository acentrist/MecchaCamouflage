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
    auto feedback_albedo = std::vector<
        PaintAppearanceFeedbackAlbedo>(
            PixelCount,
            PaintAppearanceFeedbackAlbedo{
                true,
                AppearanceRgb{0.70, 0.10, 0.05},
            });
    const auto feedback_raster = model
        ? resolve_paint_appearance_raster(
              *model,
              base_colors,
              scene_colors,
              parameters,
              feedback_albedo)
        : std::expected<
              std::vector<ResolvedPaintAppearance>,
              PaintAppearanceFitError>{
              std::unexpected(
                  PaintAppearanceFitError::InvalidModel)};
    passed &= expect(
        parameters.size() == 16U && raster && feedback_raster &&
            raster->size() == PixelCount &&
            feedback_raster->size() == PixelCount &&
            feedback_raster->front().color.red >
                feedback_raster->front().color.green &&
            feedback_raster->front().color.green >
                feedback_raster->front().color.blue,
        "cluster parameters and calibrated feedback albedo did not resolve "
        "one bounded appearance raster");
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
            exact_evaluation->target_hdr_by_sample.size() ==
                PixelCount &&
            exact_evaluation->target_hdr_by_sample.front().finite &&
            !exact_evaluation->target_hdr_by_sample.front().clipped &&
            exact_evaluation->emission_roi_paired_samples == 0 &&
            exact_evaluation->non_emission_paired_samples ==
                static_cast<int>(PixelCount) &&
            exact_evaluation->non_emission_loss == 0.0 &&
            exact_evaluation->clusters.size() ==
                model->clusters.size(),
        "exact target feedback did not retain its per-sample response and "
        "non-emission loss");

    const auto emissive_endpoint = model
                                       ? paint_appearance_emissive_endpoint_parameters(
                                             *model)
                                       : std::expected<
                                             std::vector<double>,
                                             PaintAppearanceFitError>{
                                             std::unexpected(
                                                 PaintAppearanceFitError::
                                                     InvalidModel)};
    passed &= expect(
        emissive_endpoint &&
            emissive_endpoint->size() == parameters.size() &&
            (*emissive_endpoint)[0] == 1.0 &&
            (*emissive_endpoint)[1] == 0.0 &&
            (*emissive_endpoint)[2] ==
                AppearanceFallbackRoughness &&
            (*emissive_endpoint)[3] == 1.0,
        "the exact E=1 response endpoint was not derived from the safe "
        "fallback tuple");

    auto endpoint_baseline =
        std::vector<AppearanceRgb>(
            PixelCount,
            AppearanceRgb{0.25, 0.15, 0.10});
    auto endpoint_response =
        std::vector<AppearanceRgb>(
            PixelCount,
            AppearanceRgb{2.0, 1.0, 0.50});
    const auto endpoint_baseline_evaluation = model
        ? evaluate_paint_appearance_response(
              *model,
              endpoint_baseline,
              true,
              true,
              AppearanceReadbackTransform::Identity)
        : std::expected<
              PaintAppearanceEvaluation,
              PaintAppearanceFitError>{
              std::unexpected(
                  PaintAppearanceFitError::InvalidModel)};
    const auto endpoint_response_evaluation = model
        ? evaluate_paint_appearance_response(
              *model,
              endpoint_response,
              true,
              true,
              AppearanceReadbackTransform::Identity)
        : std::expected<
              PaintAppearanceEvaluation,
              PaintAppearanceFitError>{
              std::unexpected(
                  PaintAppearanceFitError::InvalidModel)};
    const auto calibrated_emissive =
        model && endpoint_baseline_evaluation &&
                endpoint_response_evaluation
            ? calibrate_paint_appearance_emissive_endpoint(
                  *model,
                  parameters,
                  *endpoint_baseline_evaluation,
                  *endpoint_response_evaluation)
            : std::expected<
                  PaintAppearanceEmissiveCalibration,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidResponse)};
    passed &= expect(
        calibrated_emissive &&
            calibrated_emissive->parameters.size() ==
                parameters.size() &&
            calibrated_emissive->responsive_samples == 0 &&
            calibrated_emissive->active_clusters == 0 &&
            calibrated_emissive->parameters[3] == 0.0,
        "non-emission source samples acquired an emissive response from the "
        "E=1 endpoint");

    auto emission_model = model
                              ? std::optional<PaintAppearanceModel>{
                                    *model}
                              : std::nullopt;
    if (emission_model)
    {
        for (auto& sample : emission_model->samples)
        {
            sample.emission_roi = true;
        }
        emission_model->emission_roi_samples =
            emission_model->samples.size();
    }
    const auto emission_fallback = emission_model
        ? resolve_paint_appearance_raster(
              *emission_model,
              base_colors,
              scene_colors,
              paint_appearance_fallback_parameters(
                  *emission_model),
              {},
              true)
        : std::expected<
              std::vector<ResolvedPaintAppearance>,
              PaintAppearanceFitError>{
              std::unexpected(
                  PaintAppearanceFitError::InvalidModel)};
    const auto encode_display = [](double channel)
    {
        return static_cast<std::uint8_t>(std::lround(
            appearance_linear_to_srgb(channel) * 255.0));
    };
    passed &= expect(
        emission_fallback &&
            emission_fallback->front().color == Rgb8{
                encode_display(0.5),
                encode_display(0.33),
                encode_display(0.2),
            } &&
            emission_fallback->front().material.emissive == 0.0,
        "the exact safe fallback did not retain display color with E=0 for "
        "an intrinsic-emission sample");
    const auto responsive_emissive =
        emission_model && endpoint_baseline_evaluation &&
                endpoint_response_evaluation
            ? calibrate_paint_appearance_emissive_endpoint(
                  *emission_model,
                  parameters,
                  *endpoint_baseline_evaluation,
                  *endpoint_response_evaluation)
            : std::expected<
                  PaintAppearanceEmissiveCalibration,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidResponse)};
    const auto retained_emissive =
        emission_model && responsive_emissive
            ? paint_appearance_non_emission_parameters(
                  *emission_model,
                  responsive_emissive->parameters)
            : std::expected<
                  std::vector<double>,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidResponse)};
    passed &= expect(
        responsive_emissive && retained_emissive,
        "a valid intrinsic-emission endpoint calibration failed");
    if (responsive_emissive && retained_emissive)
    {
        passed &= expect(
            responsive_emissive->responsive_samples ==
                    static_cast<int>(PixelCount) &&
                responsive_emissive->active_clusters > 0 &&
                responsive_emissive->parameters[3] > 0.0,
            "the responsive intrinsic-emission endpoint was not calibrated");
        passed &= expect(
            *retained_emissive ==
                responsive_emissive->parameters,
            "the calibrated emissive tuple was zeroed despite a non-empty "
            "intrinsic-emission ROI");
    }
    auto gate_model = emission_model;
    if (gate_model)
    {
        for (auto& sample : gate_model->samples)
        {
            sample.intrinsic_emission =
                AppearanceRgb{1.0, 1.0, 1.0};
        }
    }
    auto gate_fallback = endpoint_baseline_evaluation
                             ? std::optional<
                                   PaintAppearanceEvaluation>{
                                   *endpoint_baseline_evaluation}
                             : std::nullopt;
    auto gate_candidate = endpoint_response_evaluation
                              ? std::optional<
                                    PaintAppearanceEvaluation>{
                                    *endpoint_response_evaluation}
                              : std::nullopt;
    if (gate_fallback && gate_candidate)
    {
        gate_fallback->emission_roi_loss = 1.0;
        gate_fallback->non_emission_loss = 0.0;
        gate_candidate->emission_roi_loss = 0.80;
        gate_candidate->non_emission_loss = 0.0;
        for (auto& cluster : gate_fallback->clusters)
        {
            cluster.loss = 1.0;
        }
        for (auto& cluster : gate_candidate->clusters)
        {
            cluster.loss = 0.80;
        }
    }
    const auto gated_emissive =
        gate_model && responsive_emissive && gate_fallback &&
                gate_candidate
            ? gate_paint_appearance_emissive_calibration(
                  *gate_model,
                  *responsive_emissive,
                  *gate_fallback,
                  *gate_candidate)
            : std::expected<
                  PaintAppearanceEmissiveCalibration,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidResponse)};
    passed &= expect(
        gated_emissive && gated_emissive->active_clusters > 0 &&
            gated_emissive->rejected_clusters <
                static_cast<int>(gate_model->clusters.size()) &&
            paint_appearance_emission_candidate_accepted(
                *gate_model,
                *gated_emissive,
                gated_emissive->parameters,
                true,
                *gate_fallback,
                *gate_candidate) &&
            !paint_appearance_emission_candidate_accepted(
                *gate_model,
                *gated_emissive,
                gated_emissive->parameters,
                false,
                *gate_fallback,
                *gate_candidate),
        "the calibrated cluster or final emission-ROI acceptance gate was "
        "not enforced");

    const auto albedo_endpoint =
        model && calibrated_emissive
            ? paint_appearance_albedo_endpoint_parameters(
                  *model,
                  calibrated_emissive->parameters)
            : std::expected<
                  std::vector<double>,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidModel)};
    const auto calibrated_albedo =
        model && exact_evaluation && endpoint_response_evaluation &&
                calibrated_emissive
            ? calibrate_paint_appearance_albedo_endpoint(
                  *model,
                  calibrated_emissive->parameters,
                  *exact_evaluation,
                  *endpoint_response_evaluation)
            : std::expected<
                  PaintAppearanceAlbedoCalibration,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidResponse)};
    passed &= expect(
        albedo_endpoint &&
            (*albedo_endpoint)[0] == 0.0 &&
            (*albedo_endpoint)[1] == 0.0 &&
            (*albedo_endpoint)[2] ==
                AppearanceFallbackRoughness &&
            (*albedo_endpoint)[3] == 0.0 &&
            calibrated_albedo &&
            calibrated_albedo->parameters.size() ==
                parameters.size() &&
            calibrated_albedo->responsive_samples ==
                static_cast<int>(PixelCount) &&
            calibrated_albedo->feedback_albedo_by_sample.size() ==
                PixelCount &&
            calibrated_albedo->calibrated_samples ==
                static_cast<int>(PixelCount) &&
            calibrated_albedo->responsive_channels > 0 &&
            calibrated_albedo->parameters[0] >= 0.0 &&
            calibrated_albedo->parameters[0] <= 1.0,
        "the bounded albedo endpoint response did not preserve the calibrated "
        "emissive/M/R tuple or retain RGB feedback calibration");

    auto accepted_non_emission_evaluation =
        *endpoint_response_evaluation;
    accepted_non_emission_evaluation.loss = 0.70;
    accepted_non_emission_evaluation.median_delta_e = 0.04;
    accepted_non_emission_evaluation.maximum_chromaticity_delta =
        endpoint_response_evaluation->maximum_chromaticity_delta;
    auto accepted_fallback_evaluation =
        accepted_non_emission_evaluation;
    accepted_fallback_evaluation.loss = 1.0;
    const auto non_emission_candidate =
        model && calibrated_albedo
            ? paint_appearance_non_emission_parameters(
                  *model,
                  calibrated_albedo->parameters)
            : std::expected<
                  std::vector<double>,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidModel)};
    passed &= expect(
        non_emission_candidate &&
            (*non_emission_candidate)[3] == 0.0 &&
            model && albedo_endpoint &&
            paint_appearance_non_emission_candidate_accepted(
                *model,
                *non_emission_candidate,
                true,
                accepted_fallback_evaluation,
                *endpoint_response_evaluation,
                accepted_non_emission_evaluation) &&
            !paint_appearance_non_emission_candidate_accepted(
                *model,
                *non_emission_candidate,
                false,
                accepted_fallback_evaluation,
                *endpoint_response_evaluation,
                accepted_non_emission_evaluation),
        "the non-emission candidate did not enforce packed-B and retained "
        "fit/chromaticity acceptance");
    passed &= expect(
        model && calibrated_albedo &&
            paint_appearance_feedback_albedo_authoritative(
                *model,
                *calibrated_albedo,
                accepted_fallback_evaluation,
                *endpoint_response_evaluation,
                accepted_non_emission_evaluation),
        "accepted RGB feedback albedo did not become the authoritative "
        "candidate epoch");
    if (model && calibrated_albedo)
    {
        auto unavailable_feedback = *calibrated_albedo;
        unavailable_feedback.calibrated_samples = 0;
        passed &= expect(
            !paint_appearance_feedback_albedo_authoritative(
                *model,
                unavailable_feedback,
                accepted_fallback_evaluation,
                *endpoint_response_evaluation,
                accepted_non_emission_evaluation),
            "an empty RGB feedback calibration became authoritative");
    }

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
    auto frozen_fit =
        emission_model && responsive_emissive
            ? begin_paint_appearance_fit(
                  *emission_model,
                  paint_appearance_fallback_parameters(
                      *emission_model),
                  fit_evaluation(1.0),
                  responsive_emissive->parameters,
                  fit_evaluation(0.70),
                  true)
            : std::expected<
                  PaintAppearanceFitSession,
                  PaintAppearanceFitError>{
                  std::unexpected(
                      PaintAppearanceFitError::InvalidModel)};
    const auto frozen_plus = frozen_fit
                                 ? next_paint_appearance_trial(
                                       *frozen_fit)
                                 : std::expected<
                                       std::optional<
                                           PaintAppearanceTrial>,
                                       PaintAppearanceFitError>{
                                       std::unexpected(
                                           PaintAppearanceFitError::
                                               InvalidInput)};
    passed &= expect(
        frozen_fit && frozen_fit->freeze_emissive &&
            frozen_plus && *frozen_plus &&
            (*frozen_plus)->parameters[3] ==
                responsive_emissive->parameters[3] &&
            (*frozen_plus)->parameters[7] ==
                responsive_emissive->parameters[7],
        "endpoint-calibrated emissive parameters were perturbed by the SPSA "
        "plus trial");
    if (frozen_fit && frozen_plus && *frozen_plus)
    {
        passed &= expect(
            observe_paint_appearance_trial(
                *frozen_fit,
                fit_evaluation(0.65))
                .has_value(),
            "the frozen-emissive plus trial was rejected");
        const auto frozen_minus =
            next_paint_appearance_trial(*frozen_fit);
        passed &= expect(
            frozen_minus && *frozen_minus &&
                (*frozen_minus)->parameters[3] ==
                    responsive_emissive->parameters[3] &&
                (*frozen_minus)->parameters[7] ==
                    responsive_emissive->parameters[7],
            "endpoint-calibrated emissive parameters were perturbed by the "
            "SPSA minus trial");
    }
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
