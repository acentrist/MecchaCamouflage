#include <meccha/application/product_ui_model.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL product_ui_model: "
                  << message << '\n';
    }
    return condition;
}

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};

auto ready_snapshot()
    -> meccha::application::ApplicationSnapshot
{
    using namespace meccha::application;

    auto snapshot = ApplicationSnapshot{};
    snapshot.revision = 17U;
    snapshot.runtime_phase =
        ApplicationRuntimePhase::Compatible;
    snapshot.compatibility.status =
        CompatibilityStatus::Compatible;
    snapshot.ui_open = true;
    snapshot.esp_enabled = true;
    snapshot.command_queue = CommandQueueSnapshot{
        2U,
        8U,
        true,
    };
    snapshot.runtime_queue = QueueSnapshot{
        3U,
        12U,
        true,
    };
    snapshot.image_editor.document =
        ImageEditorDocumentSnapshot{
            std::string{ProjectId},
            "Project",
            9U,
        };
    snapshot.image_editor.pipeline =
        ImageEditorPipelineSnapshot{
            ImageEditorPipelinePhase::Ready,
            4U,
            std::string{ProjectId},
            9U,
        };
    return snapshot;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    const auto ready = build_product_ui_model(
        ready_snapshot());
    passed &= expect(
        ready &&
            ready->sections == ProductUiSections &&
            ready->source_revision == 17U &&
            ready->ui_open &&
            ready->paint.actions.start &&
            ready->paint.actions.preview &&
            !ready->paint.actions.restore &&
            !ready->paint.actions.cancel &&
            ready->image_paint.actions.start &&
            ready->image_paint.actions.preview &&
            ready->image_paint.project.edit &&
            ready->image_paint.project.save &&
            ready->image_paint.project.rename &&
            ready->image_paint.project.remove &&
            ready->esp.enabled &&
            ready->esp.can_toggle &&
            ready->settings.can_apply &&
            ready->diagnostics.command_queue.utilization ==
                0.25 &&
            ready->diagnostics.runtime_queue.utilization ==
                0.25,
        "ready snapshot did not expose all sections and actions");

    auto running = ready_snapshot();
    running.ui_open = false;
    running.job = JobSnapshot{
        2U,
        JobPhase::Dispatching,
        Feature::Paint,
        33U,
        7U,
        JobProgress{
            2U,
            8U,
            10U,
            4U,
            2U,
            1U,
            0.5,
            1200U,
            800U,
        },
    };
    running.preview = PreviewLeaseSnapshot{
        1U,
        Feature::ImagePaint,
        44U,
        3U,
    };
    const auto active = build_product_ui_model(running);
    passed &= expect(
        active &&
            !active->ui_open &&
            active->esp.enabled &&
            active->job.phase == JobPhase::Dispatching &&
            active->job.feature == Feature::Paint &&
            !active->paint.actions.start &&
            !active->paint.actions.preview &&
            !active->paint.actions.restore &&
            active->paint.actions.cancel &&
            !active->image_paint.actions.start &&
            !active->image_paint.actions.preview &&
            active->image_paint.actions.restore &&
            !active->image_paint.actions.cancel &&
            active->progress.completed == 4U &&
            active->progress.total == 10U &&
            active->progress.fraction == 0.4 &&
            active->progress.queue_pressure == 0.5,
        "closed panel lost ESP/job state or exposed invalid feature actions");

    auto unavailable = ready_snapshot();
    unavailable.image_editor.pipeline.phase =
        ImageEditorPipelinePhase::Composing;
    unavailable.image_editor.pipeline.pending = true;
    unavailable.image_editor.persistence_command = 71U;
    unavailable.image_editor.persistence_operation =
        ImageProjectIoOperation::Save;
    const auto image_busy =
        build_product_ui_model(unavailable);
    passed &= expect(
        image_busy &&
            !image_busy->image_paint.actions.start &&
            !image_busy->image_paint.actions.preview &&
            !image_busy->image_paint.project.edit &&
            !image_busy->image_paint.project.load &&
            !image_busy->image_paint.project.save &&
            !image_busy->image_paint.project.rename &&
            !image_busy->image_paint.project.remove &&
            image_busy->image_paint.project.busy,
        "busy editor exposed unsafe project or Image Paint actions");

    auto backpressured = ready_snapshot();
    backpressured.command_queue.queued =
        backpressured.command_queue.capacity;
    const auto full_queue =
        build_product_ui_model(backpressured);
    passed &= expect(
        full_queue &&
            !full_queue->paint.actions.start &&
            !full_queue->image_paint.actions.preview &&
            !full_queue->esp.can_toggle &&
            !full_queue->settings.can_apply &&
            full_queue->diagnostics.command_queue.utilization ==
                1.0,
        "full command queue did not close UI action admission");

    auto composing = ready_snapshot();
    composing.image_editor.pipeline.phase =
        ImageEditorPipelinePhase::Composing;
    composing.image_editor.pipeline.pending = true;
    const auto editing =
        build_product_ui_model(composing);
    passed &= expect(
        editing &&
            editing->image_paint.project.edit &&
            editing->image_paint.project.load &&
            !editing->image_paint.project.save &&
            !editing->image_paint.project.rename &&
            !editing->image_paint.project.remove &&
            !editing->image_paint.project.busy,
        "composing editor did not distinguish coalesced edits from ready-only operations");

    auto diagnostics = ready_snapshot();
    for (std::size_t index = 0U;
         index < MaximumProductUiDiagnostics + 5U;
         ++index)
    {
        diagnostics.diagnostics.push_back(DiagnosticEntry{
            static_cast<std::uint64_t>(index + 1U),
            DiagnosticSeverity::Information,
            "message",
        });
    }
    const auto bounded =
        build_product_ui_model(diagnostics);
    passed &= expect(
        bounded &&
            bounded->diagnostics.entries.size() ==
                MaximumProductUiDiagnostics &&
            bounded->diagnostics.omitted == 5U &&
            bounded->diagnostics.entries.front().sequence == 6U &&
            bounded->diagnostics.entries.back().sequence ==
                MaximumProductUiDiagnostics + 5U,
        "diagnostics were not bounded to the newest entries");

    auto invalid_settings = ready_snapshot();
    invalid_settings.settings.schema_version = 0U;
    passed &= expect(
        build_product_ui_model(invalid_settings) ==
            std::unexpected(
                ProductUiModelError::InvalidSettings),
        "invalid settings entered the presentation boundary");

    auto invalid_queue = ready_snapshot();
    invalid_queue.runtime_queue.queued =
        invalid_queue.runtime_queue.capacity + 1U;
    passed &= expect(
        build_product_ui_model(invalid_queue) ==
            std::unexpected(
                ProductUiModelError::InvalidQueue),
        "invalid queue pressure entered the presentation boundary");

    auto invalid_progress = running;
    invalid_progress.job.progress.submitted =
        invalid_progress.job.progress.total + 1U;
    passed &= expect(
        build_product_ui_model(invalid_progress) ==
            std::unexpected(
                ProductUiModelError::InvalidProgress),
        "invalid job progress entered the presentation boundary");

    auto overflow_progress = running;
    overflow_progress.job.progress.fill_count =
        std::numeric_limits<std::size_t>::max();
    overflow_progress.job.progress.paint_count = 1U;
    overflow_progress.job.progress.total = 0U;
    passed &= expect(
        build_product_ui_model(overflow_progress) ==
            std::unexpected(
                ProductUiModelError::InvalidProgress),
        "overflowing job totals entered the presentation boundary");

    auto invalid_diagnostics = ready_snapshot();
    invalid_diagnostics.diagnostics = {
        DiagnosticEntry{
            2U,
            DiagnosticSeverity::Information,
            "message",
        },
        DiagnosticEntry{
            1U,
            DiagnosticSeverity::Information,
            "message",
        },
    };
    passed &= expect(
        build_product_ui_model(invalid_diagnostics) ==
            std::unexpected(
                ProductUiModelError::InvalidDiagnostics),
        "unordered diagnostics entered the presentation boundary");

    if (passed)
    {
        std::cout << "PASS product_ui_model\n";
    }
    return passed ? 0 : 1;
}
