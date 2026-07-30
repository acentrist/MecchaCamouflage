#include <meccha/application/callback_barrier.hpp>
#include <meccha/application/game_thread_scheduler.hpp>
#include <meccha/application/runtime_lifecycle.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <expected>
#include <iostream>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::application;

auto paint_call(std::uint64_t request_id) -> PaintAtUvWithBrush
{
    return PaintAtUvWithBrush{
        request_id,
        RuntimeObjectHandle{10U, 1U},
        0.25,
        0.75,
        5.0,
        meccha::core::Rgb8{10U, 20U, 30U},
        meccha::core::Material{0.2, 0.8, 0.1},
        false,
    };
}

auto image_call(std::uint64_t request_id)
    -> UpdateImagePreviewTexture
{
    static const auto pixels =
        std::make_shared<const std::vector<std::byte>>(
            16U,
            std::byte{0x7F});
    return UpdateImagePreviewTexture{
        request_id,
        RuntimeObjectHandle{20U, 1U},
        2U,
        2U,
        pixels,
    };
}

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL application_runtime: " << message << '\n';
    }
    return condition;
}

class RecordingExecutor final : public GameThreadExecutor
{
public:
    explicit RecordingExecutor(bool game_thread)
        : game_thread_{game_thread}
    {
    }

    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return game_thread_;
    }

    auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> override
    {
        if (throw_on_execute)
        {
            throw std::runtime_error{"injected executor exception"};
        }
        if (fail_after != 0U && operations.size() == fail_after)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
                compatibility_failure,
            });
        }
        operations.push_back(operation);
        return {};
    }

    bool game_thread_{};
    std::size_t fail_after{};
    bool throw_on_execute{};
    std::optional<CompatibilityFailure> compatibility_failure{};
    std::vector<GameThreadOperation> operations{};
};

class FakeCallbacks final : public RuntimeCallbackPort
{
public:
    auto register_hud_callback(
        void* callback_context,
        HudCallback callback_function)
        -> std::expected<CallbackId, CallbackPortError> override
    {
        ++register_count;
        context = callback_context;
        callback = callback_function;
        return CallbackId{91U};
    }

    auto unregister_hud_callback(CallbackId id)
        -> std::expected<void, CallbackPortError> override
    {
        ++unregister_count;
        unregistered_id = id;
        callback = nullptr;
        context = nullptr;
        return {};
    }

    auto invoke(const HudFrameIdentity& identity) -> void
    {
        if (callback != nullptr)
        {
            callback(context, identity);
        }
    }

    void* context{};
    HudCallback callback{};
    std::size_t register_count{};
    std::size_t unregister_count{};
    CallbackId unregistered_id{};
};
} // namespace

auto main() -> int
{
    bool passed = true;

    GameThreadScheduler scheduler{2U};
    passed &= expect(
        scheduler.schedule(ResolveInitialContracts{}) ==
                ScheduleResult::Accepted &&
            scheduler.schedule(paint_call(42U)) ==
                ScheduleResult::Accepted &&
            scheduler.schedule(image_call(43U)) ==
                ScheduleResult::Full,
        "the bounded queue did not apply backpressure");

    RecordingExecutor wrong_thread{false};
    const auto rejected = scheduler.drain(wrong_thread, 2U);
    passed &= expect(
        !rejected &&
            rejected.error().code ==
                RuntimeExecutionErrorCode::WrongThread &&
            scheduler.snapshot().queued == 2U &&
            wrong_thread.operations.empty(),
        "a non-game thread executed or consumed Unreal work");

    RecordingExecutor game_thread{true};
    const auto first_drain = scheduler.drain(game_thread, 1U);
    passed &= expect(
        first_drain && *first_drain == 1U &&
            game_thread.operations ==
                std::vector<GameThreadOperation>{
                    ResolveInitialContracts{}} &&
            scheduler.snapshot().queued == 1U,
        "the game-thread budget or FIFO order was not preserved");

    game_thread.fail_after = 1U;
    const auto failed_drain = scheduler.drain(game_thread, 1U);
    passed &= expect(
        !failed_drain &&
            failed_drain.error().code ==
                RuntimeExecutionErrorCode::OperationFailure &&
            scheduler.snapshot().queued == 1U,
        "a failed Unreal operation was lost from the queue");

    game_thread.fail_after = 0U;
    const auto final_drain = scheduler.drain(game_thread, 2U);
    passed &= expect(
        final_drain && *final_drain == 1U &&
            game_thread.operations.back() ==
                GameThreadOperation{paint_call(42U)},
        "a retried Unreal operation was not executed");

    scheduler.close();
    passed &= expect(
        scheduler.schedule(RestoreTransientState{1U}) ==
                ScheduleResult::Closed &&
            !scheduler.snapshot().accepting,
        "the closed scheduler accepted new work");

    GameThreadScheduler discarded{3U};
    static_cast<void>(discarded.schedule(RebindHudFrame{
        HudFrameIdentity{1U, 2U, 3U, 4U}}));
    static_cast<void>(discarded.schedule(RestoreTransientState{8U}));
    passed &= expect(
        discarded.discard() == 2U &&
            discarded.snapshot().queued == 0U,
        "queued operations were not discarded deterministically");

    CallbackBarrier barrier{};
    auto lease = barrier.try_enter();
    passed &= expect(
        lease.has_value() && barrier.in_flight() == 1U,
        "the callback lease was not acquired");

    std::latch waiter_started{1};
    std::atomic<bool> waiter_finished{false};
    std::thread waiter{
        [&]
        {
            barrier.begin_close();
            waiter_started.count_down();
            barrier.wait_for_idle();
            waiter_finished.store(true, std::memory_order_release);
        }};
    waiter_started.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    passed &= expect(
        !barrier.accepting() && !barrier.try_enter() &&
            !waiter_finished.load(std::memory_order_acquire),
        "closing admitted a callback or ignored an in-flight callback");

    lease.reset();
    waiter.join();
    passed &= expect(
        waiter_finished.load(std::memory_order_acquire) &&
            barrier.in_flight() == 0U,
        "callback drain did not complete after the last lease");

    FakeCallbacks callbacks{};
    RecordingExecutor lifecycle_executor{true};
    RuntimeLifecycle lifecycle{callbacks, lifecycle_executor, 4U};
    passed &= expect(
        lifecycle.initialize().has_value() &&
            callbacks.register_count == 1U &&
            lifecycle.snapshot().phase == RuntimePhase::Running,
        "the runtime lifecycle did not register its HUD callback");

    const auto invalid_frame = lifecycle.on_hud_frame(
        HudFrameIdentity{100U, 0U, 300U, 400U},
        4U);
    passed &= expect(
        !invalid_frame &&
            invalid_frame.error() ==
                RuntimeLifecycleError::InvalidFrameIdentity &&
            lifecycle_executor.operations.empty() &&
            lifecycle.snapshot().last_error ==
                RuntimeLifecycleError::InvalidFrameIdentity,
        "an incomplete HUD frame identity reached Unreal execution");

    lifecycle.on_update();
    lifecycle.on_update();
    passed &= expect(
        lifecycle.snapshot().update_ticks == 2U &&
            lifecycle_executor.operations.empty(),
        "on_update touched the Unreal executor");

    constexpr auto FirstFrame = HudFrameIdentity{
        100U,
        200U,
        300U,
        400U,
    };
    callbacks.invoke(FirstFrame);
    passed &= expect(
        lifecycle_executor.operations ==
            std::vector<GameThreadOperation>{
                ResolveInitialContracts{},
                RebindHudFrame{FirstFrame},
            },
        "the first HUD frame did not resolve then bind on the game thread");

    static_cast<void>(
        lifecycle.schedule(paint_call(70U)));
    callbacks.invoke(FirstFrame);
    passed &= expect(
        lifecycle_executor.operations.back() ==
                GameThreadOperation{paint_call(70U)} &&
            lifecycle_executor.operations.size() == 3U,
        "a stable-world HUD frame did not drain scheduled work");

    const auto replaced_controller = HudFrameIdentity{
        FirstFrame.world,
        201U,
        FirstFrame.hud,
        FirstFrame.canvas,
    };
    callbacks.invoke(replaced_controller);
    passed &= expect(
        lifecycle_executor.operations.back() ==
                GameThreadOperation{
                    RebindHudFrame{replaced_controller}} &&
            lifecycle.snapshot().frame_identity ==
                replaced_controller,
        "a controller replacement did not invalidate the HUD binding");

    static_cast<void>(
        lifecycle.schedule(image_call(71U)));
    passed &= expect(
        lifecycle.request_shutdown(9U).has_value() &&
            lifecycle.schedule(paint_call(72U)) ==
                ScheduleResult::Closed &&
            lifecycle.snapshot().queue.queued == 0U,
        "shutdown did not close and cancel queued work");
    const auto premature_finalize = lifecycle.finalize_shutdown();
    passed &= expect(
        !premature_finalize &&
            premature_finalize.error() ==
                RuntimeLifecycleError::PendingGameThreadRestore,
        "callbacks were unregistered before game-thread restoration");

    callbacks.invoke(replaced_controller);
    passed &= expect(
        lifecycle_executor.operations.back() ==
            GameThreadOperation{RestoreTransientState{9U}},
        "shutdown restoration did not run through the game-thread executor");
    passed &= expect(
        lifecycle.finalize_shutdown().has_value() &&
            callbacks.unregister_count == 1U &&
            callbacks.unregistered_id == 91U &&
            lifecycle.snapshot().phase == RuntimePhase::Stopped,
        "the restored runtime did not unregister and stop exactly once");

    FakeCallbacks throwing_callbacks{};
    RecordingExecutor throwing_executor{true};
    throwing_executor.throw_on_execute = true;
    RuntimeLifecycle throwing_lifecycle{
        throwing_callbacks,
        throwing_executor,
        2U,
    };
    passed &= expect(
        throwing_lifecycle.initialize().has_value(),
        "the throwing lifecycle did not initialize");
    throwing_callbacks.invoke(FirstFrame);
    passed &= expect(
        throwing_lifecycle.snapshot().last_error ==
                RuntimeLifecycleError::ExecutionFailed &&
            throwing_executor.operations.empty(),
        "an executor exception crossed the HUD callback boundary");

    if (passed)
    {
        std::cout << "PASS application_runtime\n";
        return 0;
    }
    return 1;
}
