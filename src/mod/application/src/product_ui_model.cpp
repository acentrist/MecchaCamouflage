#include <meccha/application/product_ui_model.hpp>
#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace meccha::application
{
namespace
{
auto active_job(JobPhase phase) -> bool
{
    return phase == JobPhase::Planning ||
           phase == JobPhase::Dispatching ||
           phase == JobPhase::Cancelling ||
           phase == JobPhase::Draining;
}

auto cancellable_job(JobPhase phase) -> bool
{
    return phase == JobPhase::Planning ||
           phase == JobPhase::Dispatching ||
           phase == JobPhase::Draining;
}

auto queue_valid(std::size_t queued, std::size_t capacity)
    -> bool
{
    return queued <= capacity;
}

auto present_queue(
    std::size_t queued,
    std::size_t capacity,
    bool accepting) -> QueuePresentation
{
    return {
        queued,
        capacity,
        capacity == 0U
            ? 0.0
            : static_cast<double>(queued) /
                  static_cast<double>(capacity),
        accepting,
    };
}

auto progress_valid(const JobProgress& progress) -> bool
{
    if (progress.fill_count >
        std::numeric_limits<std::size_t>::max() -
            progress.paint_count)
    {
        return false;
    }
    return progress.fill_count + progress.paint_count ==
               progress.total &&
           progress.submitted <= progress.total &&
           std::isfinite(progress.queue_pressure) &&
           progress.queue_pressure >= 0.0 &&
           progress.queue_pressure <= 1.0;
}

auto diagnostics_valid(
    const std::vector<DiagnosticEntry>& entries) -> bool
{
    auto previous_sequence = std::uint64_t{};
    return std::ranges::all_of(
        entries,
        [&previous_sequence](const DiagnosticEntry& entry)
        {
            const auto valid =
                entry.sequence > previous_sequence &&
                !entry.message_key.empty() &&
                entry.message_key.size() <=
                    MaximumProductUiDiagnosticKeyBytes &&
                core::valid_utf8(entry.message_key);
            previous_sequence = entry.sequence;
            return valid;
        });
}

auto feature_actions(
    Feature feature,
    bool runtime_ready,
    bool job_is_active,
    bool feature_ready,
    const JobSnapshot& job,
    const PreviewLeaseSnapshot& preview)
    -> FeatureActionAvailability
{
    const auto owns_job =
        job.feature && *job.feature == feature;
    return {
        runtime_ready && !job_is_active && feature_ready,
        runtime_ready && !job_is_active && feature_ready,
        runtime_ready && preview.feature &&
            *preview.feature == feature,
        runtime_ready && owns_job &&
            cancellable_job(job.phase),
    };
}

auto image_ready(const ApplicationSnapshot& snapshot) -> bool
{
    if (!snapshot.image_editor.document)
    {
        return false;
    }
    const auto& document = *snapshot.image_editor.document;
    const auto& pipeline = snapshot.image_editor.pipeline;
    return core::valid_image_project_id(document.project_id) &&
           document.revision != 0U &&
           pipeline.phase == ImageEditorPipelinePhase::Ready &&
           pipeline.project_id == document.project_id &&
           pipeline.project_revision == document.revision &&
           !pipeline.pending && !pipeline.failure;
}
} // namespace

auto build_product_ui_model(
    const ApplicationSnapshot& snapshot)
    -> std::expected<ProductUiModel, ProductUiModelError>
{
    if (!core::validate(snapshot.settings).empty())
    {
        return std::unexpected(
            ProductUiModelError::InvalidSettings);
    }
    if (!queue_valid(
            snapshot.command_queue.queued,
            snapshot.command_queue.capacity) ||
        !queue_valid(
            snapshot.runtime_queue.queued,
            snapshot.runtime_queue.capacity))
    {
        return std::unexpected(
            ProductUiModelError::InvalidQueue);
    }
    if (!progress_valid(snapshot.job.progress))
    {
        return std::unexpected(
            ProductUiModelError::InvalidProgress);
    }
    if (!diagnostics_valid(snapshot.diagnostics))
    {
        return std::unexpected(
            ProductUiModelError::InvalidDiagnostics);
    }

    const auto job_is_active =
        active_job(snapshot.job.phase);
    const auto command_admission_ready =
        snapshot.command_queue.accepting &&
        snapshot.command_queue.queued <
            snapshot.command_queue.capacity;
    const auto runtime_ready =
        snapshot.runtime_phase ==
            ApplicationRuntimePhase::Compatible &&
        snapshot.compatibility.status ==
            CompatibilityStatus::Compatible &&
        command_admission_ready &&
        snapshot.runtime_queue.accepting;
    const auto editor_busy =
        snapshot.image_editor.persistence_command.has_value() ||
        snapshot.image_editor.completion_pending ||
        snapshot.image_editor.stopped;
    const auto project_safe =
        runtime_ready && !job_is_active && !editor_busy;
    const auto has_document =
        snapshot.image_editor.document.has_value();
    const auto ready_image =
        image_ready(snapshot) && !editor_busy;
    const auto pipeline_busy =
        snapshot.image_editor.pipeline.phase ==
            ImageEditorPipelinePhase::Decoding ||
        snapshot.image_editor.pipeline.phase ==
            ImageEditorPipelinePhase::Composing ||
        snapshot.image_editor.pipeline.pending;

    auto model = ProductUiModel{};
    model.source_revision = snapshot.revision;
    model.ui_open = snapshot.ui_open;
    model.paint.settings = snapshot.settings.paint;
    model.paint.actions = feature_actions(
        Feature::Paint,
        runtime_ready,
        job_is_active,
        true,
        snapshot.job,
        snapshot.preview);
    model.image_paint.settings =
        has_document
            ? snapshot.image_editor.document->settings
            : snapshot.settings.image_paint;
    model.image_paint.document =
        snapshot.image_editor.document;
    model.image_paint.pipeline =
        snapshot.image_editor.pipeline;
    model.image_paint.actions = feature_actions(
        Feature::ImagePaint,
        runtime_ready,
        job_is_active,
        ready_image,
        snapshot.job,
        snapshot.preview);
    model.image_paint.project = {
        project_safe && has_document,
        project_safe,
        project_safe && ready_image,
        project_safe && ready_image,
        project_safe && has_document && !pipeline_busy,
        editor_busy,
    };
    model.esp = {
        snapshot.esp_enabled,
        command_admission_ready &&
            snapshot.runtime_phase !=
                ApplicationRuntimePhase::ShuttingDown &&
            snapshot.runtime_phase !=
                ApplicationRuntimePhase::Stopped,
        snapshot.settings.esp,
        snapshot.esp,
    };
    model.settings = {
        snapshot.settings,
        runtime_ready && !job_is_active,
    };
    model.diagnostics.runtime_phase =
        snapshot.runtime_phase;
    model.diagnostics.compatibility =
        snapshot.compatibility;
    model.diagnostics.command_queue = present_queue(
        snapshot.command_queue.queued,
        snapshot.command_queue.capacity,
        snapshot.command_queue.accepting);
    model.diagnostics.runtime_queue = present_queue(
        snapshot.runtime_queue.queued,
        snapshot.runtime_queue.capacity,
        snapshot.runtime_queue.accepting);
    const auto omitted =
        snapshot.diagnostics.size() >
                MaximumProductUiDiagnostics
            ? snapshot.diagnostics.size() -
                  MaximumProductUiDiagnostics
            : 0U;
    model.diagnostics.omitted = omitted;
    model.diagnostics.entries.assign(
        snapshot.diagnostics.begin() +
            static_cast<std::ptrdiff_t>(omitted),
        snapshot.diagnostics.end());
    model.job = snapshot.job;
    model.preview = snapshot.preview;
    model.progress = {
        snapshot.job.progress.submitted,
        snapshot.job.progress.total,
        snapshot.job.progress.total == 0U
            ? 0.0
            : static_cast<double>(
                  snapshot.job.progress.submitted) /
                  static_cast<double>(
                      snapshot.job.progress.total),
        snapshot.job.progress.queue_pressure,
        snapshot.job.progress.elapsed_ms,
        snapshot.job.progress.eta_ms,
    };
    return model;
}
} // namespace meccha::application
