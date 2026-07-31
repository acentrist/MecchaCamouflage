#include <meccha/application/runtime_operation_executor.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <variant>

namespace meccha::application
{
namespace
{
constexpr auto FailureMessage = "error.operation.failed";
constexpr std::uint32_t MaximumTextureDimension = 4096U;
constexpr double MaximumEffectiveBrushRadiusTexels = 1024.0;

auto invalid(
    RuntimeContractId contract,
    ContractFailureKind kind)
    -> std::unexpected<RuntimeExecutionError>
{
    return std::unexpected(RuntimeExecutionError{
        RuntimeExecutionErrorCode::InvalidRequest,
        CompatibilityFailure{
            contract,
            kind,
            FailureMessage,
        },
    });
}

auto unit(double value) -> bool
{
    return std::isfinite(value) &&
           value >= 0.0 && value <= 1.0;
}

auto validate(const PaintAtUvWithBrush& request)
    -> std::expected<void, RuntimeExecutionError>
{
    if (request.request_id == 0U ||
        request.job_generation == 0U)
    {
        return invalid(
            RuntimeContractId::PaintAtUvWithBrush,
            ContractFailureKind::InvalidValue);
    }
    if (!request.component.valid())
    {
        return invalid(
            RuntimeContractId::PaintAtUvWithBrush,
            ContractFailureKind::StaleObject);
    }
    if (!unit(request.u) || !unit(request.v) ||
        !std::isfinite(request.brush_size_texels) ||
        request.brush_size_texels < 1.0 ||
        request.brush_size_texels >
            MaximumEffectiveBrushRadiusTexels ||
        request.texture_dimension == 0U ||
        request.texture_dimension > MaximumTextureDimension ||
        request.brush_size_texels >
            static_cast<double>(request.texture_dimension) ||
        !unit(request.material.metallic) ||
        !unit(request.material.roughness) ||
        !unit(request.material.emissive))
    {
        return invalid(
            RuntimeContractId::PaintAtUvWithBrush,
            ContractFailureKind::InvalidValue);
    }
    return {};
}

} // namespace

RuntimeOperationExecutor::RuntimeOperationExecutor(
    GameThreadContext& thread_context,
    UnrealFrameRuntimePort& frame_runtime,
    PaintStrokeRuntimePort& paint_runtime,
    TransientStateRuntimePort& transient_runtime)
    : thread_context_{thread_context},
      frame_runtime_{frame_runtime},
      paint_runtime_{paint_runtime},
      transient_runtime_{transient_runtime}
{
}

auto RuntimeOperationExecutor::is_game_thread() const noexcept -> bool
{
    return thread_context_.is_game_thread();
}

auto RuntimeOperationExecutor::execute(
    const GameThreadOperation& operation)
    -> std::expected<void, RuntimeExecutionError>
{
    if (!is_game_thread())
    {
        return std::unexpected(RuntimeExecutionError{
            RuntimeExecutionErrorCode::WrongThread,
            std::nullopt,
        });
    }
    return std::visit(
        [this](const auto& request)
            -> std::expected<void, RuntimeExecutionError>
        {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (
                std::is_same_v<Request, ResolveInitialContracts>)
            {
                return frame_runtime_.resolve_initial_contracts();
            }
            else if constexpr (
                std::is_same_v<Request, RebindHudFrame>)
            {
                if (!request.identity.valid())
                {
                    return invalid(
                        RuntimeContractId::Canvas,
                        ContractFailureKind::StaleObject);
                }
                return frame_runtime_.rebind_hud_frame(
                    request.identity);
            }
            else if constexpr (
                std::is_same_v<Request, PaintAtUvWithBrush>)
            {
                const auto validated = validate(request);
                if (!validated)
                {
                    return validated;
                }
                return paint_runtime_.paint_at_uv_with_brush(
                    request);
            }
            else
            {
                if (request.shutdown_generation == 0U)
                {
                    return invalid(
                        RuntimeContractId::InputControl,
                        ContractFailureKind::InvalidValue);
                }
                return transient_runtime_.restore_transient_state(
                    request.shutdown_generation);
            }
        },
        operation);
}
} // namespace meccha::application
