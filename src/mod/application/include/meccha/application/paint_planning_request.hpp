#pragma once

#include <meccha/core/paint_capture_request.hpp>
#include <meccha/core/paint_plan.hpp>

#include <utility>
#include <variant>

namespace meccha::application
{
struct PaintPlanningRequest
{
    std::variant<
        core::PaintPlanRequest,
        core::PaintCaptureInput>
        value{core::PaintPlanRequest{}};

    PaintPlanningRequest() = default;

    PaintPlanningRequest(core::PaintPlanRequest request)
        : value{std::move(request)}
    {
    }

    PaintPlanningRequest(core::PaintCaptureInput input)
        : value{std::move(input)}
    {
    }
};

[[nodiscard]] inline auto paint_settings(
    const PaintPlanningRequest& request) noexcept
    -> const core::PaintSettings&
{
    return std::visit(
        [](const auto& value) -> const core::PaintSettings&
        {
            return value.settings;
        },
        request.value);
}
} // namespace meccha::application
