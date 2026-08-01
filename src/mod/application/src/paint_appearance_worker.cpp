#include <meccha/application/paint_appearance_worker.hpp>

#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace meccha::application
{
namespace
{
auto projective_failure(core::PaintProjectiveError error)
    -> PaintAppearanceWorkResult
{
    return std::unexpected(PaintAppearanceWorkFailure{
        PaintAppearanceWorkFailureKind::Projective,
        error,
    });
}

auto invalid_request() -> PaintAppearanceWorkResult
{
    return std::unexpected(PaintAppearanceWorkFailure{
        PaintAppearanceWorkFailureKind::InvalidRequest,
    });
}

auto capture_geometry_failure(core::PaintCaptureGeometryError error)
    -> PaintAppearanceWorkResult
{
    return std::unexpected(PaintAppearanceWorkFailure{
        PaintAppearanceWorkFailureKind::CaptureGeometry,
        std::nullopt,
        std::nullopt,
        error,
    });
}

auto capture_evidence_failure(core::PaintAppearanceCaptureError error)
    -> PaintAppearanceWorkResult
{
    return std::unexpected(PaintAppearanceWorkFailure{
        PaintAppearanceWorkFailureKind::CaptureEvidence,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        error,
    });
}
} // namespace

PaintAppearanceWorker::~PaintAppearanceWorker()
{
    shutdown();
}

auto PaintAppearanceWorker::start(
    JobGeneration generation,
    PaintAppearanceWorkRequest request)
    -> std::expected<void, PaintAppearanceWorkStartError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(PaintAppearanceWorkStartError::Stopped);
    }
    if (generation == 0U)
    {
        return std::unexpected(
            PaintAppearanceWorkStartError::InvalidGeneration);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(PaintAppearanceWorkStartError::Busy);
    }
    active_generation_ = generation;
    state_ = State::Running;
    try
    {
        worker_ = std::jthread{
            [this, generation, request = std::move(request)](
                std::stop_token cancellation) mutable {
                run(generation, std::move(request), cancellation);
            }};
    }
    catch (...)
    {
        active_generation_ = 0U;
        state_ = State::Idle;
        return std::unexpected(PaintAppearanceWorkStartError::ThreadStart);
    }
    return {};
}

auto PaintAppearanceWorker::request_cancel(JobGeneration generation)
    -> PaintAppearanceWorkCancelResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Idle)
    {
        return PaintAppearanceWorkCancelResult::Idle;
    }
    if (generation == 0U || generation != active_generation_)
    {
        return PaintAppearanceWorkCancelResult::StaleGeneration;
    }
    if (state_ == State::Completed)
    {
        return PaintAppearanceWorkCancelResult::AlreadyCompleted;
    }
    static_cast<void>(worker_.request_stop());
    return PaintAppearanceWorkCancelResult::Requested;
}

auto PaintAppearanceWorker::poll()
    -> std::optional<PaintAppearanceWorkCompletion>
{
    auto completed_thread = std::jthread{};
    auto result = std::optional<PaintAppearanceWorkCompletion>{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (state_ != State::Completed || !completion_)
        {
            return std::nullopt;
        }
        result = std::move(completion_);
        completion_.reset();
        completed_thread = std::move(worker_);
        active_generation_ = 0U;
        state_ = State::Idle;
    }
    if (completed_thread.joinable())
    {
        completed_thread.join();
    }
    return result;
}

auto PaintAppearanceWorker::shutdown() noexcept -> void
{
    auto stopped_thread = std::jthread{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (stopped_ && !worker_.joinable())
        {
            return;
        }
        stopped_ = true;
        if (worker_.joinable())
        {
            static_cast<void>(worker_.request_stop());
            stopped_thread = std::move(worker_);
        }
    }
    if (stopped_thread.joinable())
    {
        stopped_thread.join();
    }
    const auto lock = std::scoped_lock{mutex_};
    completion_.reset();
    active_generation_ = 0U;
    state_ = State::Idle;
}

auto PaintAppearanceWorker::run(
    JobGeneration generation,
    PaintAppearanceWorkRequest request,
    std::stop_token cancellation) noexcept -> void
{
    auto completion = std::optional<PaintAppearanceWorkCompletion>{};
    try
    {
        auto result = std::visit(
            [cancellation](auto&& work) -> PaintAppearanceWorkResult {
                using Work = std::decay_t<decltype(work)>;
                if constexpr (std::is_same_v<
                                  Work,
                                  PaintAppearanceGeometryPrepareWork>)
                {
                    auto replay = core::build_paint_capture_geometry(
                        work.sampling_profile,
                        work.image_profile,
                        work.current_world_transforms,
                        work.replay_brush_size_texels,
                        work.view,
                        work.viewport,
                        cancellation);
                    if (!replay)
                    {
                        return capture_geometry_failure(replay.error());
                    }
                    for (auto& sample : *replay)
                    {
                        sample.replay_relevant = true;
                        sample.calibration_sample =
                            std::abs(work.replay_brush_size_texels -
                                     core::AppearanceCalibrationStepTexels) <=
                            0.000001;
                    }
                    const auto replay_count = replay->size();
                    if (std::abs(work.replay_brush_size_texels -
                                 core::AppearanceCalibrationStepTexels) >
                        0.000001)
                    {
                        auto calibration = core::build_paint_capture_geometry(
                            work.sampling_profile,
                            work.image_profile,
                            work.current_world_transforms,
                            core::AppearanceCalibrationStepTexels,
                            work.view,
                            work.viewport,
                            cancellation);
                        if (!calibration)
                        {
                            return capture_geometry_failure(
                                calibration.error());
                        }
                        if (calibration->size() >
                            core::MaximumPaintProjectiveSamples -
                                replay->size())
                        {
                            return projective_failure(
                                core::PaintProjectiveError::ResourceLimit);
                        }
                        for (auto& sample : *calibration)
                        {
                            sample.replay_relevant = false;
                            sample.calibration_sample = true;
                        }
                        replay->insert(
                            replay->end(),
                            std::make_move_iterator(calibration->begin()),
                            std::make_move_iterator(calibration->end()));
                    }
                    if (!std::isfinite(work.viewport.width) ||
                        !std::isfinite(work.viewport.height) ||
                        work.viewport.width != std::floor(work.viewport.width) ||
                        work.viewport.height !=
                            std::floor(work.viewport.height) ||
                        work.viewport.width >
                            static_cast<double>(
                                std::numeric_limits<std::uint32_t>::max()) ||
                        work.viewport.height >
                            static_cast<double>(
                                std::numeric_limits<std::uint32_t>::max()))
                    {
                        return capture_evidence_failure(
                            core::PaintAppearanceCaptureError::InvalidEvidence);
                    }
                    auto source_queries =
                        core::build_paint_appearance_source_queries(
                            *replay,
                            work.sampling_profile,
                            static_cast<std::uint32_t>(work.viewport.width),
                            static_cast<std::uint32_t>(work.viewport.height),
                            cancellation);
                    if (!source_queries)
                    {
                        return capture_evidence_failure(
                            source_queries.error());
                    }
                    const auto calibration_count =
                        replay->size() - replay_count +
                        (replay_count == replay->size() ? replay_count : 0U);
                    return PaintAppearanceWorkValue{
                        PaintAppearanceGeometryPrepared{
                            std::make_shared<const std::vector<
                                core::PaintCaptureGeometrySample>>(
                                std::move(*replay)),
                            std::make_shared<const std::vector<
                                core::PaintAppearanceSourceQuery>>(
                                std::move(*source_queries)),
                            replay_count,
                            calibration_count,
                        }};
                }
                else if constexpr (std::is_same_v<
                                       Work,
                                       PaintAppearanceCapturePrepareWork>)
                {
                    if (!work.geometry)
                    {
                        return invalid_request();
                    }
                    auto observations =
                        core::build_paint_projective_observations(
                            *work.geometry,
                            work.evidence,
                            cancellation);
                    if (!observations)
                    {
                        return capture_evidence_failure(
                            observations.error());
                    }
                    auto model = core::prepare_paint_projective_model(
                        work.evidence.base_color.camera.width,
                        work.evidence.base_color.camera.height,
                        *observations,
                        cancellation);
                    if (!model)
                    {
                        return projective_failure(model.error());
                    }
                    auto owned_model =
                        std::make_shared<const core::PaintProjectiveModel>(
                            std::move(*model));
                    auto baseline = core::build_paint_projective_baseline(
                        *owned_model,
                        work.settings,
                        cancellation);
                    if (!baseline)
                    {
                        return projective_failure(baseline.error());
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearancePrepared{
                            std::move(owned_model),
                            std::make_shared<const core::PaintProjectiveRaster>(
                                std::move(*baseline)),
                        }};
                }
                else if constexpr (std::is_same_v<
                                       Work,
                                       PaintAppearanceCandidateWork>)
                {
                    if (!work.model || !work.raster ||
                        !work.original.albedo_rgba ||
                        !work.original.packed_pbr_rgba)
                    {
                        return invalid_request();
                    }
                    auto plan = core::build_paint_projective_trial_plan(
                        *work.model,
                        work.raster->appearances,
                        work.original.dimension,
                        cancellation);
                    if (!plan)
                    {
                        return projective_failure(plan.error());
                    }
                    auto composed = core::compose_paint_preview(
                        work.original.dimension,
                        *work.original.albedo_rgba,
                        *work.original.packed_pbr_rgba,
                        *plan,
                        cancellation);
                    if (!composed)
                    {
                        return std::unexpected(PaintAppearanceWorkFailure{
                            PaintAppearanceWorkFailureKind::Composer,
                            std::nullopt,
                            composed.error(),
                        });
                    }
                    auto references =
                        core::build_paint_appearance_readback_references(
                            *work.model,
                            composed->dimension,
                            composed->albedo_rgba,
                            cancellation);
                    if (!references)
                    {
                        return capture_evidence_failure(references.error());
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearanceCandidate{
                            std::make_shared<const PaintTextureImage>(
                                PaintTextureImage{
                                    composed->dimension,
                                    std::make_shared<const std::vector<
                                        std::byte>>(
                                        std::move(composed->albedo_rgba)),
                                    std::make_shared<const std::vector<
                                        std::byte>>(
                                        std::move(composed->packed_pbr_rgba)),
                                }),
                            std::make_shared<const std::vector<
                                core::PaintAppearanceReadbackReference>>(
                                std::move(*references)),
                        }};
                }
                else if constexpr (std::is_same_v<
                                       Work,
                                       PaintAppearanceBaselineCalibrateWork>)
                {
                    if (!work.model || !work.readback_references)
                    {
                        return invalid_request();
                    }
                    auto feedback = core::prepare_paint_appearance_feedback(
                        work.source_camera,
                        *work.readback_references,
                        work.feedback_evidence,
                        cancellation);
                    if (!feedback)
                    {
                        return capture_evidence_failure(feedback.error());
                    }
                    auto target_e0 = core::prepare_paint_appearance_target_e0(
                        work.source_camera,
                        *work.readback_references,
                        work.target_e0_evidence,
                        feedback->readback,
                        cancellation);
                    if (!target_e0)
                    {
                        return capture_evidence_failure(target_e0.error());
                    }
                    auto evaluated = core::evaluate_paint_projective_feedback(
                        *work.model,
                        *feedback->target_hdr,
                        feedback->camera_stable,
                        feedback->readback.ok,
                        feedback->readback.transform,
                        cancellation);
                    if (!evaluated)
                    {
                        return projective_failure(evaluated.error());
                    }
                    auto calibration =
                        core::calibrate_paint_projective_baseline(
                            *work.model,
                            *evaluated,
                            target_e0->noise,
                            work.settings,
                            work.packed_b_verified,
                            cancellation);
                    if (!calibration)
                    {
                        return projective_failure(calibration.error());
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearanceBaselineCalibrated{
                            std::move(*evaluated),
                            std::move(*target_e0),
                            std::make_shared<const core::
                                PaintProjectiveCalibration>(
                                std::move(*calibration)),
                        }};
                }
                else if constexpr (std::is_same_v<
                                       Work,
                                       PaintAppearanceFinalizeWork>)
                {
                    if (!work.model || !work.calibration ||
                        !work.endpoint_final_hdr.pixels ||
                        !core::paint_appearance_camera_matches(
                            work.source_camera,
                            work.endpoint_final_hdr.camera))
                    {
                        return invalid_request();
                    }
                    auto endpoint = core::evaluate_paint_projective_feedback(
                        *work.model,
                        *work.endpoint_final_hdr.pixels,
                        true,
                        work.baseline.readback_calibrated,
                        work.baseline.readback_transform,
                        cancellation);
                    if (!endpoint)
                    {
                        return projective_failure(endpoint.error());
                    }
                    auto resolved = core::finalize_paint_projective_raster(
                        *work.model,
                        *work.calibration,
                        work.baseline,
                        *endpoint,
                        work.target_e0_noise,
                        work.settings,
                        work.packed_b_verified,
                        cancellation);
                    if (!resolved)
                    {
                        return projective_failure(resolved.error());
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearanceResolved{
                            std::make_shared<const std::vector<
                                core::ResolvedPaintAppearance>>(
                                std::move(resolved->raster.appearances)),
                            std::make_shared<const std::vector<bool>>(
                                std::move(resolved->raster.available)),
                            resolved->local_albedo_acceptances,
                            resolved->physical_emission_components,
                            resolved->physical_emission_samples,
                        }};
                }
                return invalid_request();
            },
            std::move(request));
        completion = PaintAppearanceWorkCompletion{
            generation,
            std::move(result),
        };
    }
    catch (...)
    {
        completion = PaintAppearanceWorkCompletion{
            generation,
            std::unexpected(PaintAppearanceWorkFailure{
                PaintAppearanceWorkFailureKind::WorkerException,
            }),
        };
    }

    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Running && active_generation_ == generation)
    {
        completion_ = std::move(completion);
        state_ = State::Completed;
    }
}
} // namespace meccha::application
