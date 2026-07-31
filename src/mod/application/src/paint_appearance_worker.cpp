#include <meccha/application/paint_appearance_worker.hpp>

#include <cmath>
#include <cstddef>
#include <expected>
#include <memory>
#include <limits>
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
auto core_failure(core::PaintAppearanceFitError error)
    -> PaintAppearanceWorkResult
{
    return std::unexpected(PaintAppearanceWorkFailure{
        PaintAppearanceWorkFailureKind::Core,
        error,
        std::nullopt,
    });
}

auto invalid_request() -> PaintAppearanceWorkResult
{
    return std::unexpected(PaintAppearanceWorkFailure{
        PaintAppearanceWorkFailureKind::InvalidRequest,
        std::nullopt,
        std::nullopt,
    });
}

auto capture_geometry_failure(
    core::PaintCaptureGeometryError error)
    -> PaintAppearanceWorkResult
{
    return std::unexpected(PaintAppearanceWorkFailure{
        PaintAppearanceWorkFailureKind::CaptureGeometry,
        std::nullopt,
        std::nullopt,
        error,
        std::nullopt,
    });
}

auto capture_evidence_failure(
    core::PaintAppearanceCaptureError error)
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
    -> std::expected<
        void,
        PaintAppearanceWorkStartError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(
            PaintAppearanceWorkStartError::Stopped);
    }
    if (generation == 0U)
    {
        return std::unexpected(
            PaintAppearanceWorkStartError::
                InvalidGeneration);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(
            PaintAppearanceWorkStartError::Busy);
    }
    active_generation_ = generation;
    state_ = State::Running;
    try
    {
        worker_ = std::jthread{
            [this,
             generation,
             request = std::move(request)](
                std::stop_token cancellation) mutable
            {
                run(
                    generation,
                    std::move(request),
                    cancellation);
            }};
    }
    catch (...)
    {
        active_generation_ = 0U;
        state_ = State::Idle;
        return std::unexpected(
            PaintAppearanceWorkStartError::ThreadStart);
    }
    return {};
}

auto PaintAppearanceWorker::request_cancel(
    JobGeneration generation)
    -> PaintAppearanceWorkCancelResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Idle)
    {
        return PaintAppearanceWorkCancelResult::Idle;
    }
    if (generation == 0U ||
        generation != active_generation_)
    {
        return PaintAppearanceWorkCancelResult::
            StaleGeneration;
    }
    if (state_ == State::Completed)
    {
        return PaintAppearanceWorkCancelResult::
            AlreadyCompleted;
    }
    static_cast<void>(worker_.request_stop());
    return PaintAppearanceWorkCancelResult::Requested;
}

auto PaintAppearanceWorker::poll()
    -> std::optional<PaintAppearanceWorkCompletion>
{
    auto completed_thread = std::jthread{};
    auto result =
        std::optional<PaintAppearanceWorkCompletion>{};
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
    auto completion =
        std::optional<PaintAppearanceWorkCompletion>{};
    try
    {
        auto result = std::visit(
            [cancellation](auto&& work)
                -> PaintAppearanceWorkResult
            {
                using Work =
                    std::decay_t<decltype(work)>;
                if constexpr (
                    std::is_same_v<
                        Work,
                        PaintAppearancePrepareWork>)
                {
                    auto model =
                        core::prepare_paint_appearance_model(
                            work.width,
                            work.height,
                            work.observations,
                            work.include_scene_lighting,
                            work.target_e0_noise,
                            cancellation);
                    if (!model)
                    {
                        return core_failure(model.error());
                    }
                    auto owned_model =
                        std::make_shared<
                            const core::PaintAppearanceModel>(
                            std::move(*model));
                    auto parameters =
                        core::paint_appearance_parameters(
                            *owned_model);
                    if (parameters.empty())
                    {
                        return invalid_request();
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearancePrepared{
                            std::move(owned_model),
                            std::move(parameters),
                        }};
                }
                else if constexpr (
                    std::is_same_v<
                        Work,
                        PaintAppearanceGeometryPrepareWork>)
                {
                    auto geometry =
                        core::build_paint_capture_geometry(
                            work.sampling_profile,
                            work.image_profile,
                            work.current_world_transforms,
                            work.brush_size_texels,
                            work.view,
                            work.viewport,
                            cancellation);
                    if (!geometry)
                    {
                        return capture_geometry_failure(
                            geometry.error());
                    }
                    if (!std::isfinite(work.viewport.width) ||
                        !std::isfinite(work.viewport.height) ||
                        work.viewport.width !=
                            std::floor(work.viewport.width) ||
                        work.viewport.height !=
                            std::floor(work.viewport.height) ||
                        work.viewport.width >
                            static_cast<double>(
                                std::numeric_limits<std::uint32_t>::
                                    max()) ||
                        work.viewport.height >
                            static_cast<double>(
                                std::numeric_limits<std::uint32_t>::
                                    max()))
                    {
                        return capture_evidence_failure(
                            core::PaintAppearanceCaptureError::
                                InvalidEvidence);
                    }
                    auto query_pixels =
                        core::build_paint_appearance_source_query_pixels(
                            *geometry,
                            static_cast<std::uint32_t>(
                                work.viewport.width),
                            static_cast<std::uint32_t>(
                                work.viewport.height),
                            cancellation);
                    if (!query_pixels)
                    {
                        return capture_evidence_failure(
                            query_pixels.error());
                    }
                    auto owned_geometry =
                        std::make_shared<const std::vector<
                            core::PaintCaptureGeometrySample>>(
                            std::move(*geometry));
                    auto owned_query_pixels =
                        std::make_shared<
                            const std::vector<std::size_t>>(
                            std::move(*query_pixels));
                    return PaintAppearanceWorkValue{
                        PaintAppearanceGeometryPrepared{
                            std::move(owned_geometry),
                            std::move(owned_query_pixels),
                        }};
                }
                else if constexpr (
                    std::is_same_v<
                        Work,
                        PaintAppearanceCapturePrepareWork>)
                {
                    if (!work.geometry)
                    {
                        return invalid_request();
                    }
                    auto observations =
                        core::build_paint_appearance_observations(
                            *work.geometry,
                            work.evidence,
                            cancellation);
                    if (!observations)
                    {
                        return capture_evidence_failure(
                            observations.error());
                    }
                    auto model =
                        core::prepare_paint_appearance_model(
                            work.evidence.base_color.camera.width,
                            work.evidence.base_color.camera.height,
                            *observations,
                            work.include_scene_lighting,
                            work.target_e0_noise,
                            cancellation);
                    if (!model)
                    {
                        return core_failure(model.error());
                    }
                    auto owned_model =
                        std::make_shared<
                            const core::PaintAppearanceModel>(
                            std::move(*model));
                    auto parameters =
                        core::paint_appearance_parameters(
                            *owned_model);
                    if (parameters.empty())
                    {
                        return invalid_request();
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearancePrepared{
                            std::move(owned_model),
                            std::move(parameters),
                        }};
                }
                else if constexpr (
                    std::is_same_v<
                        Work,
                        PaintAppearanceCandidateWork>)
                {
                    if (!work.model ||
                        !work.base_colors ||
                        !work.scene_colors ||
                        !work.original.albedo_rgba ||
                        !work.original.packed_pbr_rgba)
                    {
                        return invalid_request();
                    }
                    auto appearances =
                        core::resolve_paint_appearance_raster(
                            *work.model,
                            *work.base_colors,
                            *work.scene_colors,
                            work.parameters,
                            cancellation);
                    if (!appearances)
                    {
                        return core_failure(
                            appearances.error());
                    }
                    auto plan =
                        core::build_paint_appearance_trial_plan(
                            *work.model,
                            *appearances,
                            work.brush_size_texels,
                            work.texture_dimension,
                            cancellation);
                    if (!plan)
                    {
                        return core_failure(plan.error());
                    }
                    auto composed =
                        core::compose_paint_preview(
                            work.original.dimension,
                            std::span<const std::byte>{
                                *work.original.albedo_rgba},
                            std::span<const std::byte>{
                                *work.original
                                     .packed_pbr_rgba},
                            *plan,
                            cancellation);
                    if (!composed)
                    {
                        return std::unexpected(
                            PaintAppearanceWorkFailure{
                                PaintAppearanceWorkFailureKind::
                                    Composer,
                                std::nullopt,
                                composed.error(),
                            });
                    }
                    auto readback_references =
                        core::build_paint_appearance_readback_references(
                            *work.model,
                            composed->dimension,
                            composed->albedo_rgba,
                            cancellation);
                    if (!readback_references)
                    {
                        return capture_evidence_failure(
                            readback_references.error());
                    }
                    auto owned_appearances =
                        std::make_shared<
                            const std::vector<
                                core::ResolvedPaintAppearance>>(
                            std::move(*appearances));
                    auto preview =
                        std::make_shared<
                            const PaintTextureImage>(
                            PaintTextureImage{
                                composed->dimension,
                                std::make_shared<
                                    const std::vector<std::byte>>(
                                    std::move(
                                        composed
                                            ->albedo_rgba)),
                                std::make_shared<
                                    const std::vector<std::byte>>(
                                    std::move(
                                        composed
                                            ->packed_pbr_rgba)),
                            });
                    auto owned_readback_references =
                        std::make_shared<const std::vector<
                            core::PaintAppearanceReadbackReference>>(
                            std::move(*readback_references));
                    return PaintAppearanceWorkValue{
                        PaintAppearanceCandidate{
                            std::move(owned_appearances),
                            std::move(preview),
                            std::move(
                                owned_readback_references),
                            std::move(work.parameters),
                        }};
                }
                else if constexpr (
                    std::is_same_v<
                        Work,
                        PaintAppearanceTargetE0PrepareWork>)
                {
                    if (!work.readback_references)
                    {
                        return invalid_request();
                    }
                    auto feedback =
                        core::prepare_paint_appearance_feedback(
                            work.source_camera,
                            *work.readback_references,
                            work.feedback_evidence,
                            cancellation);
                    if (!feedback)
                    {
                        return capture_evidence_failure(
                            feedback.error());
                    }
                    auto target_e0 =
                        core::prepare_paint_appearance_target_e0(
                            work.source_camera,
                            *work.readback_references,
                            work.target_e0_evidence,
                            feedback->readback,
                            cancellation);
                    if (!target_e0)
                    {
                        return capture_evidence_failure(
                            target_e0.error());
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearanceTargetE0Prepared{
                            std::move(*feedback),
                            std::move(*target_e0),
                        }};
                }
                else if constexpr (
                    std::is_same_v<
                        Work,
                        PaintAppearanceResolveWork>)
                {
                    if (!work.model || !work.base_colors ||
                        !work.scene_colors)
                    {
                        return invalid_request();
                    }
                    auto appearances =
                        core::resolve_paint_appearance_raster(
                            *work.model,
                            *work.base_colors,
                            *work.scene_colors,
                            work.parameters,
                            cancellation);
                    if (!appearances)
                    {
                        return core_failure(
                            appearances.error());
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearanceResolved{
                            std::make_shared<const std::vector<
                                core::ResolvedPaintAppearance>>(
                                std::move(*appearances)),
                            std::move(work.parameters),
                        }};
                }
                else
                {
                    if (!work.model || !work.target_hdr)
                    {
                        return invalid_request();
                    }
                    auto evaluation =
                        core::evaluate_paint_appearance_response(
                            *work.model,
                            *work.target_hdr,
                            work.camera_stable,
                            work.readback_calibrated,
                            work.transform,
                            cancellation);
                    if (!evaluation)
                    {
                        return core_failure(
                            evaluation.error());
                    }
                    return PaintAppearanceWorkValue{
                        PaintAppearanceEvaluated{
                            std::move(*evaluation),
                        }};
                }
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
                PaintAppearanceWorkFailureKind::
                    WorkerException,
                std::nullopt,
                std::nullopt,
            }),
        };
    }

    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Running &&
        active_generation_ == generation)
    {
        completion_ = std::move(completion);
        state_ = State::Completed;
    }
}
} // namespace meccha::application
