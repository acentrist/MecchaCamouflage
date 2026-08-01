#include <meccha/application/runtime_operation_executor.hpp>
#include <meccha/core/paint_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL runtime_operation_executor: "
                  << message << '\n';
    }
    return condition;
}

class FakeThreadContext final : public GameThreadContext
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return game_thread;
    }

    bool game_thread{true};
};

class FakeRuntimePort final
    : public UnrealFrameRuntimePort,
      public PaintStrokeRuntimePort,
      public TransientStateRuntimePort
{
public:
    auto resolve_initial_contracts()
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++resolve_calls;
        return {};
    }

    auto rebind_hud_frame(const HudFrameIdentity& identity)
        -> std::expected<void, RuntimeExecutionError> override
    {
        frames.push_back(identity);
        return {};
    }

    auto paint_at_uv_with_brush(
        const PaintAtUvWithBrush& request)
        -> std::expected<void, RuntimeExecutionError> override
    {
        paint_calls.push_back(request);
        if (paint_failure)
        {
            return std::unexpected(*paint_failure);
        }
        return {};
    }

    auto restore_transient_state(std::uint64_t generation)
        -> std::expected<void, RuntimeExecutionError> override
    {
        restore_generations.push_back(generation);
        return {};
    }

    std::size_t resolve_calls{};
    std::vector<HudFrameIdentity> frames{};
    std::vector<PaintAtUvWithBrush> paint_calls{};
    std::vector<std::uint64_t> restore_generations{};
    std::optional<RuntimeExecutionError> paint_failure{};
};
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    FakeThreadContext thread{};
    FakeRuntimePort runtime{};
    RuntimeOperationExecutor executor{
        thread,
        runtime,
        runtime,
        runtime,
    };

    const auto component = RuntimeObjectHandle{100U, 7U};
    const auto paint = PaintAtUvWithBrush{
        10U,
        1U,
        component,
        0.25,
        0.75,
        5.0,
        1024U,
        core::Rgb8{10U, 20U, 30U},
        core::Material{0.2, 0.8, 0.1},
    };
    GameThreadScheduler scheduler{3U};
    static_cast<void>(scheduler.schedule(paint));
    static_cast<void>(
        scheduler.schedule(RestoreTransientState{11U}));
    const auto drained = scheduler.drain(executor, 2U);
    passed &= expect(
        drained && *drained == 2U &&
            runtime.paint_calls == std::vector{paint} &&
            runtime.restore_generations ==
                std::vector<std::uint64_t>{11U},
        "valid typed operations did not reach the runtime port");

    thread.game_thread = false;
    const auto wrong_thread = executor.execute(paint);
    passed &= expect(
        !wrong_thread &&
            wrong_thread.error().code ==
                RuntimeExecutionErrorCode::WrongThread &&
            runtime.paint_calls.size() == 1U,
        "direct off-thread execution reached the runtime port");
    thread.game_thread = true;

    auto fill = paint;
    fill.brush_size_texels = core::PaintFillRadiusTexels;
    passed &= expect(
        executor.execute(fill).has_value() &&
            runtime.paint_calls.size() == 2U,
        "the fixed Fill radius was rejected as a settings value");
    auto excessive_radius = paint;
    excessive_radius.brush_size_texels = 1025.0;
    const auto rejected_radius =
        executor.execute(excessive_radius);
    passed &= expect(
        !rejected_radius &&
            rejected_radius.error().code ==
                RuntimeExecutionErrorCode::InvalidRequest &&
            runtime.paint_calls.size() == 2U,
        "an excessive effective Paint radius reached the runtime");

    auto missing_texture_dimension = paint;
    missing_texture_dimension.texture_dimension = 0U;
    const auto rejected_dimension =
        executor.execute(missing_texture_dimension);
    passed &= expect(
        !rejected_dimension &&
            rejected_dimension.error().code ==
                RuntimeExecutionErrorCode::InvalidRequest &&
            runtime.paint_calls.size() == 2U,
        "Paint without a captured texture dimension reached the runtime");

    const auto invalid =
        executor.execute(RestoreTransientState{0U});
    passed &= expect(
        !invalid &&
            invalid.error().code ==
                RuntimeExecutionErrorCode::InvalidRequest &&
            invalid.error().compatibility_failure ==
                CompatibilityFailure{
                    RuntimeContractId::InputControl,
                    ContractFailureKind::InvalidValue,
                    "error.operation.failed",
                } &&
            runtime.restore_generations.size() == 1U,
        "an invalid shutdown generation reached transient restore");

    runtime.paint_failure = RuntimeExecutionError{
        RuntimeExecutionErrorCode::OperationFailure,
        CompatibilityFailure{
            RuntimeContractId::PaintAtUvWithBrush,
            ContractFailureKind::MissingFunction,
            "error.operation.failed",
        },
    };
    const auto failed_paint = executor.execute(paint);
    passed &= expect(
        !failed_paint &&
            failed_paint.error() == *runtime.paint_failure,
        "the runtime port's structured Paint failure was lost");

    if (passed)
    {
        std::cout << "PASS runtime_operation_executor\n";
        return 0;
    }
    return 1;
}
