#include "product_panel_diagnostics.hpp"

#include <cmath>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::product_ui::detail
{
namespace
{
constexpr auto DiagnosticsScrollIndex = std::size_t{4U};
constexpr auto SummaryRowCount = std::size_t{4U};
constexpr auto TextColor =
    ui::CanvasColor{220U, 224U, 232U, 255U};
constexpr auto InformationColor =
    ui::CanvasColor{150U, 205U, 255U, 255U};
constexpr auto WarningColor =
    ui::CanvasColor{255U, 210U, 110U, 255U};
constexpr auto ErrorColor =
    ui::CanvasColor{255U, 130U, 130U, 255U};

struct DiagnosticsLine
{
    std::string text{};
    ui::CanvasColor color{TextColor};
};

auto runtime_state_label(
    application::ApplicationRuntimePhase phase,
    const ProductPanelLabels& labels) -> std::string_view
{
    switch (phase)
    {
    case application::ApplicationRuntimePhase::Cold:
    case application::ApplicationRuntimePhase::Initializing:
        return labels.diagnostics_state_labels[0U];
    case application::ApplicationRuntimePhase::Compatible:
        return labels.diagnostics_state_labels[1U];
    case application::ApplicationRuntimePhase::ShuttingDown:
    case application::ApplicationRuntimePhase::Stopped:
        return labels.diagnostics_state_labels[2U];
    case application::ApplicationRuntimePhase::Incompatible:
        return labels.diagnostics_state_labels[3U];
    }
    return labels.diagnostics_state_labels[3U];
}

auto compatibility_state_label(
    application::CompatibilityStatus status,
    const ProductPanelLabels& labels) -> std::string_view
{
    switch (status)
    {
    case application::CompatibilityStatus::Unknown:
        return labels.diagnostics_state_labels[0U];
    case application::CompatibilityStatus::Compatible:
        return labels.diagnostics_state_labels[1U];
    case application::CompatibilityStatus::UnsupportedGame:
    case application::CompatibilityStatus::RuntimeError:
        return labels.diagnostics_state_labels[3U];
    }
    return labels.diagnostics_state_labels[3U];
}

auto contract_name(
    application::RuntimeContractId contract) -> std::string_view
{
    switch (contract)
    {
    case application::RuntimeContractId::RuntimeInitialization:
        return "RuntimeInitialization";
    case application::RuntimeContractId::HudCallback:
        return "HudCallback";
    case application::RuntimeContractId::World:
        return "World";
    case application::RuntimeContractId::PlayerController:
        return "PlayerController";
    case application::RuntimeContractId::Hud:
        return "Hud";
    case application::RuntimeContractId::Canvas:
        return "Canvas";
    case application::RuntimeContractId::PaintAtUvWithBrush:
        return "PaintAtUvWithBrush";
    case application::RuntimeContractId::ImagePaintTexture:
        return "ImagePaintTexture";
    case application::RuntimeContractId::TextureMutation:
        return "TextureMutation";
    case application::RuntimeContractId::InputControl:
        return "InputControl";
    }
    return "UnknownContract";
}

auto failure_kind_name(
    application::ContractFailureKind kind) -> std::string_view
{
    switch (kind)
    {
    case application::ContractFailureKind::MissingObject:
        return "MissingObject";
    case application::ContractFailureKind::WrongClass:
        return "WrongClass";
    case application::ContractFailureKind::MissingProperty:
        return "MissingProperty";
    case application::ContractFailureKind::WrongPropertyKind:
        return "WrongPropertyKind";
    case application::ContractFailureKind::MissingFunction:
        return "MissingFunction";
    case application::ContractFailureKind::ParameterSizeMismatch:
        return "ParameterSizeMismatch";
    case application::ContractFailureKind::StaleObject:
        return "StaleObject";
    case application::ContractFailureKind::InvalidValue:
        return "InvalidValue";
    case application::ContractFailureKind::CallbackFailure:
        return "CallbackFailure";
    case application::ContractFailureKind::ExecutionFailure:
        return "ExecutionFailure";
    case application::ContractFailureKind::UnsupportedGameBuild:
        return "UnsupportedGameBuild";
    }
    return "UnknownFailure";
}

auto severity_label(
    application::DiagnosticSeverity severity,
    const ProductPanelLabels& labels) -> std::string_view
{
    switch (severity)
    {
    case application::DiagnosticSeverity::Information:
        return labels.diagnostics_severity_labels[0U];
    case application::DiagnosticSeverity::Warning:
        return labels.diagnostics_severity_labels[1U];
    case application::DiagnosticSeverity::Error:
        return labels.diagnostics_severity_labels[2U];
    }
    return labels.diagnostics_severity_labels[2U];
}

auto severity_color(
    application::DiagnosticSeverity severity) -> ui::CanvasColor
{
    switch (severity)
    {
    case application::DiagnosticSeverity::Information:
        return InformationColor;
    case application::DiagnosticSeverity::Warning:
        return WarningColor;
    case application::DiagnosticSeverity::Error:
        return ErrorColor;
    }
    return ErrorColor;
}

auto queue_line(
    std::string_view label,
    const application::QueuePresentation& queue,
    const ProductPanelLabels& labels) -> std::string
{
    const auto percent = static_cast<unsigned>(
        std::lround(queue.utilization * 100.0));
    return std::string{label} + ": " +
           std::to_string(queue.queued) + " / " +
           std::to_string(queue.capacity) + "  (" +
           std::to_string(percent) + "%)  " +
           std::string{
               queue.accepting
                   ? labels.diagnostics_state_labels[1U]
                   : labels.diagnostics_state_labels[2U]};
}

auto diagnostic_line(
    const application::DiagnosticEntry& entry,
    const ProductPanelLabels& labels) -> DiagnosticsLine
{
    auto text = "#" + std::to_string(entry.sequence) + "  " +
                std::string{severity_label(entry.severity, labels)} +
                "  ";
    text += entry.message_key == "error.operation.failed"
                ? labels.diagnostics_failure
                : entry.message_key;
    if (entry.command_id)
    {
        text += "  cmd=" + std::to_string(*entry.command_id);
    }
    if (entry.compatibility_failure)
    {
        text += "  ";
        text += contract_name(
            entry.compatibility_failure->contract);
        text += "/";
        text += failure_kind_name(
            entry.compatibility_failure->kind);
    }
    return {
        std::move(text),
        severity_color(entry.severity),
    };
}

auto add_clipped_text(
    ui::CanvasFrameBuilder& canvas,
    const ui::CanvasRect& clip,
    ui::CanvasPoint anchor,
    const DiagnosticsLine& line,
    double scale) -> std::expected<void, ProductPanelError>
{
    const auto pushed = canvas.push_clip(clip);
    if (!pushed)
    {
        return std::unexpected(
            ProductPanelError{pushed.error()});
    }
    const auto added = canvas.add_text(
        anchor,
        line.text,
        line.color,
        scale);
    const auto popped = canvas.pop_clip();
    if (!added)
    {
        return std::unexpected(
            ProductPanelError{added.error()});
    }
    if (!popped)
    {
        return std::unexpected(
            ProductPanelError{popped.error()});
    }
    return {};
}
} // namespace

auto compose_diagnostics_section(
    ui::CanvasFrameBuilder& canvas,
    const ui::PanelLayout& layout,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels,
    const ProductPanelInput& input,
    ProductPanelState& state)
    -> std::expected<void, ProductPanelError>
{
    auto lines = std::vector<DiagnosticsLine>{};
    lines.reserve(
        SummaryRowCount + model.diagnostics.entries.size() + 1U);
    lines.push_back({
        labels.diagnostics_runtime + ": " +
            std::string{runtime_state_label(
                model.diagnostics.runtime_phase,
                labels)},
        TextColor,
    });
    lines.push_back({
        labels.diagnostics_compatibility + ": " +
            std::string{compatibility_state_label(
                model.diagnostics.compatibility.status,
                labels)},
        TextColor,
    });
    lines.push_back({
        queue_line(
            labels.diagnostics_command_queue,
            model.diagnostics.command_queue,
            labels),
        TextColor,
    });
    lines.push_back({
        queue_line(
            labels.diagnostics_runtime_queue,
            model.diagnostics.runtime_queue,
            labels),
        TextColor,
    });
    if (model.diagnostics.omitted != 0U)
    {
        lines.push_back({
            labels.section_labels[4U] + ": +" +
                std::to_string(model.diagnostics.omitted),
            WarningColor,
        });
    }
    for (const auto& entry : model.diagnostics.entries)
    {
        lines.push_back(diagnostic_line(entry, labels));
    }
    if (model.diagnostics.entries.empty())
    {
        lines.push_back({
            labels.diagnostics_empty,
            InformationColor,
        });
    }

    const auto row_height = 34.0 * layout.effective_scale;
    const auto scroll = ui::update_scroll_container(
        state.section_scroll[DiagnosticsScrollIndex],
        ui::ScrollContainerInput{
            layout.content,
            layout.content,
            row_height * static_cast<double>(lines.size()),
            row_height,
            input.pointer,
        });
    if (!scroll)
    {
        return std::unexpected(
            ProductPanelError{scroll.error()});
    }
    state.section_scroll[DiagnosticsScrollIndex] =
        scroll->state;

    for (auto index = std::size_t{};
         index < lines.size();
         ++index)
    {
        const auto row = ui::CanvasRect{
            layout.content.x,
            scroll->content_origin_y +
                static_cast<double>(index) * row_height,
            layout.content.width,
            row_height,
        };
        if (row.y + row.height <= layout.content.y ||
            row.y >= layout.content.y + layout.content.height)
        {
            continue;
        }
        const auto added = add_clipped_text(
            canvas,
            layout.content,
            {
                row.x + 8.0 * layout.effective_scale,
                row.y + 9.0 * layout.effective_scale,
            },
            lines[index],
            0.8 * layout.effective_scale);
        if (!added)
        {
            return added;
        }
    }
    return {};
}
} // namespace meccha::product_ui::detail
