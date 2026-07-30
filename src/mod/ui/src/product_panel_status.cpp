#include "product_panel_status.hpp"

#include <cmath>
#include <cstdint>
#include <expected>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace meccha::product_ui::detail
{
namespace
{
constexpr auto StatusBackground =
    ui::CanvasColor{25U, 28U, 35U, 245U};
constexpr auto StatusText =
    ui::CanvasColor{220U, 224U, 232U, 255U};

auto duration_text(std::uint64_t milliseconds) -> std::string
{
    constexpr auto MillisecondsPerSecond =
        std::uint64_t{1'000U};
    constexpr auto SecondsPerMinute = std::uint64_t{60U};
    constexpr auto MinutesPerHour = std::uint64_t{60U};
    constexpr auto MillisecondsPerMinute =
        MillisecondsPerSecond * SecondsPerMinute;
    constexpr auto MillisecondsPerHour =
        MillisecondsPerMinute * MinutesPerHour;

    const auto hours = milliseconds / MillisecondsPerHour;
    const auto minutes =
        (milliseconds / MillisecondsPerMinute) %
        MinutesPerHour;
    const auto seconds =
        (milliseconds / MillisecondsPerSecond) %
        SecondsPerMinute;
    const auto remainder =
        milliseconds % MillisecondsPerSecond;

    auto stream = std::ostringstream{};
    stream << std::setfill('0');
    if (hours > 0U)
    {
        stream << hours << ':' << std::setw(2) << minutes;
    }
    else
    {
        stream << std::setw(2) << minutes;
    }
    stream << ':' << std::setw(2) << seconds << '.'
           << std::setw(3) << remainder;
    return std::move(stream).str();
}

auto status_line(
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels) -> std::string
{
    const auto percentage = static_cast<unsigned>(
        std::lround(model.progress.fraction * 100.0));
    const auto eta =
        model.progress.eta_ms
            ? duration_text(*model.progress.eta_ms)
            : std::string{"—"};
    return labels.status_progress + " " +
           std::to_string(model.progress.completed) + " / " +
           std::to_string(model.progress.total) + " (" +
           std::to_string(percentage) + "%)   |   " +
           labels.status_elapsed + " " +
           duration_text(model.progress.elapsed_ms) +
           "   |   " + labels.status_eta + " " + eta +
           "   |   " + labels.status_queue + " " +
           std::to_string(
               model.diagnostics.command_queue.queued) +
           " / " +
           std::to_string(
               model.diagnostics.command_queue.capacity);
}
} // namespace

auto compose_status_strip(
    ui::CanvasFrameBuilder& canvas,
    const ui::PanelLayout& layout,
    const application::ProductUiModel& model,
    const ProductPanelLabels& labels)
    -> std::expected<void, ProductPanelError>
{
    const auto background =
        canvas.add_filled_box(
            layout.status_strip,
            StatusBackground);
    if (!background)
    {
        return std::unexpected(
            ProductPanelError{background.error()});
    }
    const auto pushed =
        canvas.push_clip(layout.status_strip);
    if (!pushed)
    {
        return std::unexpected(
            ProductPanelError{pushed.error()});
    }
    const auto text = canvas.add_text(
        {
            layout.status_strip.x +
                12.0 * layout.effective_scale,
            layout.status_strip.y +
                14.0 * layout.effective_scale,
        },
        status_line(model, labels),
        StatusText,
        0.85 * layout.effective_scale);
    const auto popped = canvas.pop_clip();
    if (!text)
    {
        return std::unexpected(
            ProductPanelError{text.error()});
    }
    if (!popped)
    {
        return std::unexpected(
            ProductPanelError{popped.error()});
    }
    return {};
}
} // namespace meccha::product_ui::detail
