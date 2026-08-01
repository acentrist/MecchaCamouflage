#include <meccha/application/paint_dispatch.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_dispatch: " << message << '\n';
    }
    return condition;
}

class RecordingExecutor final : public GameThreadExecutor
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return true;
    }

    auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> override
    {
        operations.push_back(operation);
        return {};
    }

    std::vector<GameThreadOperation> operations{};
};

auto make_plan(std::size_t count)
    -> std::shared_ptr<const core::PaintPlan>
{
    auto plan = core::PaintPlan{};
    plan.texture_dimension = 1024U;
    for (auto index = std::size_t{}; index < count; ++index)
    {
        const auto fill = index < 2U;
        plan.strokes.push_back(core::PaintStroke{
            index,
            fill ? core::ReplayPass::Fill
                 : core::ReplayPass::Paint,
            fill ? core::Region::Front : core::Region::Side,
            0.1 + static_cast<double>(index) * 0.1,
            0.2,
            fill ? core::PaintFillRadiusTexels : 5.0,
            fill ? core::Rgb8{255U, 255U, 255U}
                 : core::Rgb8{10U, 20U, 30U},
            fill ? core::Material{1.0, 0.0, 0.0}
                 : core::Material{0.0, 1.0, 0.0},
        });
    }
    plan.fill_end = std::min<std::size_t>(2U, count);
    plan.fill_count = plan.fill_end;
    plan.paint_count = count - plan.fill_end;
    plan.source_paint_count = plan.paint_count;
    return std::make_shared<const core::PaintPlan>(
        std::move(plan));
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto jobs = JobStateMachine{};
    auto scheduler = GameThreadScheduler{3U};
    auto dispatcher = PaintDispatchController{scheduler, jobs};
    const auto started = jobs.start(Feature::Paint, 41U);
    const auto generation = jobs.snapshot().generation;
    const auto pacing = core::ReplicationPacingPlan{
        100,
        10,
        100,
        100,
        100,
        2,
        2,
        10,
        10,
        20,
    };
    passed &= expect(
        started == JobMutationResult::Applied &&
            dispatcher
                .begin(
                    generation,
                    RuntimeObjectHandle{55U, 3U},
                    make_plan(5U),
                    pacing,
                    100U)
                .has_value() &&
            jobs.snapshot().phase == JobPhase::Dispatching,
        "a valid Paint plan did not enter dispatch");

    const auto first = dispatcher.tick(
        generation,
        100U,
        {});
    passed &= expect(
        first && first->admitted == 2U &&
            jobs.snapshot().progress.submitted == 2U &&
            scheduler.queued_paint_generation(generation) == 2U,
        "the first frame exceeded or missed its admission budget");

    auto executor = RecordingExecutor{};
    passed &= expect(
        scheduler.drain(executor, 1U).value() == 1U &&
            dispatcher.tick(generation, 105U, {})->admitted == 0U &&
            jobs.snapshot().progress.submitted == 2U,
        "Paint cadence admitted work before its next window");
    passed &= expect(
        dispatcher.tick(generation, 110U, {})->admitted == 1U &&
            jobs.snapshot().progress.submitted == 3U,
        "Paint backpressure did not preserve the remaining plan");

    passed &= expect(
        scheduler.drain(executor, 2U).value() == 2U &&
            dispatcher.tick(generation, 120U, {})->admitted == 2U &&
            jobs.snapshot().phase == JobPhase::Draining &&
            jobs.snapshot().progress.submitted == 5U,
        "the final Paint window did not enter drain");
    passed &= expect(
        scheduler.drain(executor, 2U).value() == 2U &&
            executor.operations.size() == 5U,
        "accepted Paint strokes did not execute exactly once");
    for (auto index = std::size_t{};
         index < executor.operations.size();
         ++index)
    {
        const auto* operation =
            std::get_if<PaintAtUvWithBrush>(
                &executor.operations[index]);
        passed &= expect(
            operation != nullptr &&
                operation->job_generation == generation &&
                operation->component ==
                    RuntimeObjectHandle{55U, 3U} &&
                operation->texture_dimension == 1024U &&
                operation->u ==
                    make_plan(5U)->strokes[index].u,
            "dispatch changed a stroke or lost its generation");
    }

    const auto pending_observation = PaintQueueObservation{
        true,
        true,
        0U,
        true,
        1U,
    };
    passed &= expect(
        dispatcher
                .tick(
                    generation,
                    150U,
                    pending_observation)
                ->state == PaintDispatchState::Draining &&
            jobs.snapshot().phase == JobPhase::Draining,
        "an outgoing game queue was treated as terminal");
    passed &= expect(
        dispatcher.tick(generation, 151U, {})->state ==
                PaintDispatchState::Completed &&
            jobs.snapshot().phase == JobPhase::Completed,
        "a fully drained and confirmed Paint plan did not complete");

    passed &= expect(
        jobs.start(Feature::Paint, 42U) ==
            JobMutationResult::Applied,
        "a second Paint job could not start");
    const auto cancelled_generation =
        jobs.snapshot().generation;
    passed &= expect(
        dispatcher
            .begin(
                cancelled_generation,
                RuntimeObjectHandle{77U, 4U},
                make_plan(4U),
                pacing,
                200U)
            .has_value() &&
            dispatcher.tick(
                cancelled_generation,
                200U,
                {})->admitted == 2U &&
            scheduler.queued_paint_generation(
                cancelled_generation) == 2U,
        "cancellation fixture did not admit Paint");
    static_cast<void>(scheduler.schedule(
        RestoreTransientState{99U}));
    passed &= expect(
        dispatcher.request_cancel(
            cancelled_generation,
            200U,
            {}) ==
                PaintDispatchMutation::PendingDrain &&
            scheduler.queued_paint_generation(
                cancelled_generation) == 0U &&
            scheduler.snapshot().queued == 1U &&
            jobs.snapshot().phase == JobPhase::Cancelling &&
            jobs.snapshot().progress.submitted == 2U,
        "cancellation did not remove only its Paint generation");
    passed &= expect(
        dispatcher.tick(
            cancelled_generation,
            219U,
            {})->state == PaintDispatchState::Cancelling &&
            dispatcher.tick(
                cancelled_generation,
                220U,
                {})->state == PaintDispatchState::Cancelled &&
            jobs.snapshot().phase == JobPhase::Cancelled,
        "cancellation ignored the final queue-confirmation window");

    const auto stale = dispatcher.tick(
        generation,
        221U,
        {});
    passed &= expect(
        !stale &&
            stale.error() == PaintDispatchError::StaleGeneration,
        "a stale dispatch generation mutated the current job");

    if (passed)
    {
        std::cout << "PASS paint_dispatch\n";
    }
    return passed ? 0 : 1;
}
