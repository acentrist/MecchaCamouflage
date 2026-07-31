#include <meccha/application/paint_planning_worker.hpp>

#include <expected>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace meccha::application
{
auto CorePaintPlanBuilder::build(
    const core::PaintPlanRequest& request,
    std::stop_token cancellation)
    -> std::expected<core::PaintPlan, core::PaintPlanError>
{
    return core::build_paint_plan(request, cancellation);
}

PaintPlanningWorker::PaintPlanningWorker(PaintPlanBuilder& builder)
    : builder_{builder}
{
}

PaintPlanningWorker::~PaintPlanningWorker()
{
    shutdown();
}

auto PaintPlanningWorker::start(
    JobGeneration generation,
    PaintPlanningRequest request)
    -> std::expected<void, PaintPlanningStartError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(
            PaintPlanningStartError::Stopped);
    }
    if (generation == 0U)
    {
        return std::unexpected(
            PaintPlanningStartError::InvalidGeneration);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(PaintPlanningStartError::Busy);
    }

    active_generation_ = generation;
    state_ = State::Running;
    try
    {
        worker_ = std::jthread{
            [this,
             generation,
             request = std::move(request)](
                std::stop_token cancellation) mutable {
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
            PaintPlanningStartError::ThreadStart);
    }
    return {};
}

auto PaintPlanningWorker::request_cancel(
    JobGeneration generation) -> PaintPlanningCancelResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Idle)
    {
        return PaintPlanningCancelResult::Idle;
    }
    if (generation == 0U || generation != active_generation_)
    {
        return PaintPlanningCancelResult::StaleGeneration;
    }
    if (state_ == State::Completed)
    {
        return PaintPlanningCancelResult::AlreadyCompleted;
    }
    static_cast<void>(worker_.request_stop());
    return PaintPlanningCancelResult::Requested;
}

auto PaintPlanningWorker::poll()
    -> std::optional<PaintPlanningCompletion>
{
    auto completed_thread = std::jthread{};
    auto result = std::optional<PaintPlanningCompletion>{};
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

auto PaintPlanningWorker::shutdown() noexcept -> void
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

auto PaintPlanningWorker::run(
    JobGeneration generation,
    PaintPlanningRequest request,
    std::stop_token cancellation) noexcept -> void
{
    auto completion =
        std::optional<PaintPlanningCompletion>{};
    try
    {
        auto plan_request =
            std::optional<core::PaintPlanRequest>{};
        if (auto* captured =
                std::get_if<core::PaintCaptureInput>(
                    &request.value))
        {
            auto built = core::build_paint_capture_request(
                *captured,
                cancellation);
            if (!built)
            {
                completion = PaintPlanningCompletion{
                    generation,
                    std::unexpected(PaintPlanningFailure{
                        PaintPlanningFailureKind::Capture,
                        std::nullopt,
                        built.error(),
                    }),
                };
            }
            else
            {
                plan_request.emplace(std::move(*built));
            }
        }
        else
        {
            plan_request.emplace(std::move(
                std::get<core::PaintPlanRequest>(
                    request.value)));
        }
        if (completion)
        {
            const auto lock = std::scoped_lock{mutex_};
            if (state_ == State::Running &&
                active_generation_ == generation)
            {
                completion_ = std::move(completion);
                state_ = State::Completed;
            }
            return;
        }

        auto planned =
            builder_.build(*plan_request, cancellation);
        if (planned)
        {
            completion = PaintPlanningCompletion{
                generation,
                std::make_shared<const core::PaintPlan>(
                    std::move(*planned)),
            };
        }
        else
        {
            completion = PaintPlanningCompletion{
                generation,
                std::unexpected(PaintPlanningFailure{
                    PaintPlanningFailureKind::Planner,
                    planned.error(),
                    std::nullopt,
                }),
            };
        }
    }
    catch (...)
    {
        completion = PaintPlanningCompletion{
            generation,
            std::unexpected(PaintPlanningFailure{
                PaintPlanningFailureKind::WorkerException,
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
