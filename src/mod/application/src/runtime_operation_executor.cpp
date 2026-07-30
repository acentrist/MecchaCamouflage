#include <meccha/application/runtime_operation_executor.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <type_traits>
#include <variant>

namespace meccha::application
{
namespace
{
constexpr auto FailureMessage = "error.operation.failed";
constexpr std::uint32_t MaximumTextureDimension = 4096U;
constexpr std::uint64_t MaximumTextureBytes =
    64U * 1024U * 1024U;

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
    if (request.request_id == 0U)
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
        request.brush_size_texels > 10.0 ||
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

auto validate(const UpdateImagePreviewTexture& request)
    -> std::expected<void, RuntimeExecutionError>
{
    if (request.request_id == 0U)
    {
        return invalid(
            RuntimeContractId::TextureMutation,
            ContractFailureKind::InvalidValue);
    }
    if (!request.texture.valid())
    {
        return invalid(
            RuntimeContractId::TextureMutation,
            ContractFailureKind::StaleObject);
    }
    if (request.width == 0U || request.height == 0U ||
        request.width > MaximumTextureDimension ||
        request.height > MaximumTextureDimension ||
        request.rgba == nullptr)
    {
        return invalid(
            RuntimeContractId::TextureMutation,
            ContractFailureKind::InvalidValue);
    }
    const auto pixels =
        static_cast<std::uint64_t>(request.width) *
        static_cast<std::uint64_t>(request.height);
    if (pixels >
        std::numeric_limits<std::uint64_t>::max() / 4U)
    {
        return invalid(
            RuntimeContractId::TextureMutation,
            ContractFailureKind::ParameterSizeMismatch);
    }
    const auto expected_bytes = pixels * 4U;
    if (expected_bytes > MaximumTextureBytes ||
        request.rgba->size() != expected_bytes)
    {
        return invalid(
            RuntimeContractId::TextureMutation,
            ContractFailureKind::ParameterSizeMismatch);
    }
    return {};
}
} // namespace

RuntimeOperationExecutor::RuntimeOperationExecutor(
    GameThreadContext& thread_context,
    UnrealRuntimePort& runtime)
    : thread_context_{thread_context},
      runtime_{runtime}
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
                return runtime_.resolve_initial_contracts();
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
                return runtime_.rebind_hud_frame(request.identity);
            }
            else if constexpr (
                std::is_same_v<Request, PaintAtUvWithBrush>)
            {
                const auto validated = validate(request);
                if (!validated)
                {
                    return validated;
                }
                return runtime_.paint_at_uv_with_brush(request);
            }
            else if constexpr (
                std::is_same_v<
                    Request,
                    UpdateImagePreviewTexture>)
            {
                const auto validated = validate(request);
                if (!validated)
                {
                    return validated;
                }
                return runtime_.update_image_preview_texture(request);
            }
            else
            {
                if (request.shutdown_generation == 0U)
                {
                    return invalid(
                        RuntimeContractId::InputControl,
                        ContractFailureKind::InvalidValue);
                }
                return runtime_.restore_transient_state(
                    request.shutdown_generation);
            }
        },
        operation);
}
} // namespace meccha::application
