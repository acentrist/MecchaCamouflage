#include <meccha/application/paint_appearance_worker.hpp>

#include <cstddef>
#include <expected>
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
                        PaintAppearanceCandidateWork>)
                {
                    if (!work.model ||
                        !work.original.albedo_rgba ||
                        !work.original.packed_pbr_rgba)
                    {
                        return invalid_request();
                    }
                    auto appearances =
                        core::resolve_paint_appearance_raster(
                            *work.model,
                            work.base_colors,
                            work.scene_colors,
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
                    return PaintAppearanceWorkValue{
                        PaintAppearanceCandidate{
                            std::move(owned_appearances),
                            std::move(preview),
                            std::move(work.parameters),
                        }};
                }
                else
                {
                    if (!work.model)
                    {
                        return invalid_request();
                    }
                    auto evaluation =
                        core::evaluate_paint_appearance_response(
                            *work.model,
                            work.target_hdr,
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
