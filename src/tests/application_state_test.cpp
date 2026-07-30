#include <meccha/application/application_snapshot.hpp>
#include <meccha/application/commands.hpp>
#include <meccha/application/job_state.hpp>

#include <iostream>
#include <string_view>
#include <type_traits>
#include <variant>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL application_state: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::application;

    bool passed = true;
    JobStateMachine jobs{};
    passed &= expect(
        jobs.start(Feature::Paint, 100U) ==
                JobMutationResult::Applied &&
            jobs.snapshot().phase == JobPhase::Planning &&
            jobs.snapshot().feature == Feature::Paint &&
            jobs.snapshot().generation == 1U,
        "Paint did not enter planning with its own generation");
    passed &= expect(
        jobs.start(Feature::ImagePaint, 101U) ==
            JobMutationResult::Busy,
        "Image Paint started while Paint was active");

    const auto first_generation = jobs.snapshot().generation;
    passed &= expect(
        jobs.planning_ready(first_generation, 3U, 7U) ==
                JobMutationResult::Applied &&
            jobs.snapshot().phase == JobPhase::Dispatching &&
            jobs.snapshot().progress.total == 10U,
        "the immutable plan did not enter dispatch");
    passed &= expect(
        jobs.dispatch_progress(
            first_generation,
            6U,
            2U,
            1U,
            0.5,
            50U,
            40U) == JobMutationResult::Applied &&
            jobs.snapshot().progress.submitted == 6U &&
            jobs.snapshot().progress.eta_ms == 40U,
        "dispatch progress was not published");
    passed &= expect(
        jobs.begin_drain(first_generation) ==
            JobMutationResult::InvalidProgress,
        "dispatch entered drain before every item was submitted");

    passed &= expect(
        jobs.request_cancel(first_generation) ==
                JobMutationResult::Applied &&
            jobs.snapshot().phase == JobPhase::Cancelling,
        "cancellation did not remain pending");
    passed &= expect(
        jobs.acknowledge_cancel(first_generation, true, 0U, 0U) ==
                JobMutationResult::PendingAdmission &&
            jobs.acknowledge_cancel(first_generation, false, 1U, 0U) ==
                JobMutationResult::PendingQueueDrain &&
            jobs.acknowledge_cancel(first_generation, false, 0U, 0U) ==
                JobMutationResult::Applied &&
            jobs.snapshot().phase == JobPhase::Cancelled,
        "cancellation became terminal before admission and queues drained");

    passed &= expect(
        jobs.start(Feature::ImagePaint, 102U) ==
                JobMutationResult::Applied &&
            jobs.snapshot().generation == first_generation + 1U,
        "a terminal job did not release mutual exclusion");
    const auto second_generation = jobs.snapshot().generation;
    passed &= expect(
        jobs.planning_ready(first_generation, 1U, 1U) ==
                JobMutationResult::StaleGeneration &&
            jobs.snapshot().phase == JobPhase::Planning,
        "a late worker result mutated a newer job");
    passed &= expect(
        jobs.planning_ready(second_generation, 2U, 2U) ==
                JobMutationResult::Applied &&
            jobs.dispatch_progress(
                second_generation,
                4U,
                1U,
                0U,
                0.25,
                20U,
                std::nullopt) == JobMutationResult::Applied &&
            jobs.begin_drain(second_generation) ==
                JobMutationResult::Applied,
        "the second job did not reach queue drain");
    passed &= expect(
        jobs.complete_if_drained(second_generation, false) ==
                JobMutationResult::PendingQueueDrain &&
            jobs.complete_if_drained(second_generation, true) ==
                JobMutationResult::PendingQueueDrain,
        "visual confirmation bypassed a nonzero queue");
    passed &= expect(
        jobs.dispatch_progress(
            second_generation,
            4U,
            0U,
            0U,
            0.0,
            40U,
            0U) == JobMutationResult::Applied &&
            jobs.complete_if_drained(second_generation, true) ==
                JobMutationResult::Applied &&
            jobs.snapshot().phase == JobPhase::Completed,
        "a fully drained job did not complete");

    PreviewStateMachine preview{};
    passed &= expect(
        preview.acquire(Feature::Paint, 500U) ==
                PreviewAcquireResult::Created &&
            preview.acquire(Feature::Paint, 500U) ==
                PreviewAcquireResult::Reused &&
            preview.snapshot().feature == Feature::Paint,
        "same-component preview ownership was not reused");
    const auto first_lease = preview.snapshot().lease_generation;
    passed &= expect(
        preview.acquire(Feature::ImagePaint, 600U) ==
                PreviewAcquireResult::Replaced &&
            preview.snapshot().lease_generation == first_lease + 1U &&
            preview.restore(500U) ==
                PreviewRestoreResult::WrongComponent &&
            preview.snapshot().feature == Feature::ImagePaint,
        "preview replacement or wrong-component guard failed");
    passed &= expect(
        preview.restore(600U) == PreviewRestoreResult::Restored &&
            preview.restore(600U) == PreviewRestoreResult::NoLease,
        "preview restore was not exactly-once");

    const ApplicationCommand typed_command{
        MutateImageProject{
            200U,
            "0123456789abcdef0123456789abcdef",
            7U,
            ReplaceImageProjectSettingsMutation{},
        }};
    passed &= expect(
        std::holds_alternative<MutateImageProject>(
            typed_command) &&
            std::variant_size_v<ApplicationCommand> == 17U,
        "the internal typed command surface is incomplete");

    BoundedDiagnostics diagnostics{2U};
    const auto canvas_failure = CompatibilityFailure{
        RuntimeContractId::Canvas,
        ContractFailureKind::WrongClass,
        "runtime.canvas.wrong-class",
    };
    CompatibilityState compatibility{};
    compatibility.fail(canvas_failure);
    passed &= expect(
        compatibility.snapshot() == CompatibilitySnapshot{
            CompatibilityStatus::RuntimeError,
            canvas_failure,
        },
        "a contract failure did not fail closed as a runtime error");
    compatibility.fail(CompatibilityFailure{
        RuntimeContractId::RuntimeInitialization,
        ContractFailureKind::UnsupportedGameBuild,
        "runtime.game.unsupported",
    });
    passed &= expect(
        compatibility.snapshot().status ==
                CompatibilityStatus::UnsupportedGame &&
            compatibility.snapshot().failure->contract ==
                RuntimeContractId::RuntimeInitialization,
        "an unsupported game build was not classified separately");
    compatibility.mark_compatible();
    passed &= expect(
        compatibility.snapshot() == CompatibilitySnapshot{
            CompatibilityStatus::Compatible,
            std::nullopt,
        },
        "a compatible runtime retained stale failure context");

    diagnostics.push(
        DiagnosticSeverity::Information,
        "runtime.ready");
    diagnostics.push(
        DiagnosticSeverity::Warning,
        "paint.queue-pressure",
        100U);
    diagnostics.push(
        DiagnosticSeverity::Error,
        "runtime.contract",
        102U,
        canvas_failure);
    passed &= expect(
        diagnostics.entries().size() == 2U &&
            diagnostics.entries().front().sequence == 2U &&
            diagnostics.entries().back().sequence == 3U &&
            diagnostics.entries().back().compatibility_failure ==
                canvas_failure,
        "bounded structured diagnostics did not preserve failure context");

    SnapshotPublisher snapshots{};
    auto first_snapshot = ApplicationSnapshot{};
    first_snapshot.ui_open = true;
    first_snapshot.job = jobs.snapshot();
    first_snapshot.preview = preview.snapshot();
    first_snapshot.compatibility = CompatibilitySnapshot{
        CompatibilityStatus::RuntimeError,
        canvas_failure,
    };
    first_snapshot.diagnostics = diagnostics.entries();
    snapshots.publish(first_snapshot);
    const auto immutable_first = snapshots.read();

    auto second_snapshot = *immutable_first;
    second_snapshot.ui_open = false;
    snapshots.publish(std::move(second_snapshot));
    const auto immutable_second = snapshots.read();
    passed &= expect(
            immutable_first->revision == 1U &&
            immutable_first->ui_open &&
            immutable_first->compatibility.failure ==
                canvas_failure &&
            immutable_second->revision == 2U &&
            !immutable_second->ui_open,
        "UI snapshots were mutable or not monotonically revisioned");

    if (passed)
    {
        std::cout << "PASS application_state\n";
        return 0;
    }
    return 1;
}
