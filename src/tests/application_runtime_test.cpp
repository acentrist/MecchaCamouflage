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
#include <mutex>
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
        1U,
        RuntimeObjectHandle{10U, 1U},
        0.25,
        0.75,
        5.0,
        1024U,
        meccha::core::Rgb8{10U, 20U, 30U},
        meccha::core::Material{0.2, 0.8, 0.1},
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
        if (paint_delay.count() > 0 &&
            std::holds_alternative<PaintAtUvWithBrush>(operation))
        {
            std::this_thread::sleep_for(paint_delay);
        }
        operations.push_back(operation);
        return {};
    }

    bool game_thread_{};
    std::size_t fail_after{};
    bool throw_on_execute{};
    std::optional<CompatibilityFailure> compatibility_failure{};
    std::chrono::microseconds paint_delay{};
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
        if (fail_unregistration)
        {
            return std::unexpected(CallbackPortError::Unregistration);
        }
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
    bool fail_unregistration{};
};

class ConcurrentCallbacks final : public RuntimeCallbackPort
{
public:
    auto register_hud_callback(
        void* callback_context,
        HudCallback callback_function)
        -> std::expected<CallbackId, CallbackPortError> override
    {
        const auto lock = std::scoped_lock{mutex};
        context = callback_context;
        callback = callback_function;
        return CallbackId{92U};
    }

    auto unregister_hud_callback(CallbackId id)
        -> std::expected<void, CallbackPortError> override
    {
        {
            const auto lock = std::scoped_lock{mutex};
            unregistered_id = id;
            callback = nullptr;
            context = nullptr;
        }
        unregister_entered.count_down();
        return {};
    }

    auto invoke(const HudFrameIdentity& identity) -> void
    {
        auto callback_context = static_cast<void*>(nullptr);
        auto callback_function = HudCallback{};
        {
            const auto lock = std::scoped_lock{mutex};
            callback_context = context;
            callback_function = callback;
        }
        if (callback_function != nullptr)
        {
            callback_function(callback_context, identity);
        }
    }

    std::mutex mutex{};
    void* context{};
    HudCallback callback{};
    CallbackId unregistered_id{};
    std::latch unregister_entered{1};
};

class BlockingObserver final : public RuntimeLifecycleObserver
{
public:
    auto on_hud_frame_complete(
        const std::expected<std::size_t, RuntimeLifecycleError>&,
        const RuntimeLifecycleSnapshot&) noexcept -> void override
    {
        if (block.load(std::memory_order_acquire))
        {
            entered.count_down();
            release.wait();
        }
    }

    std::atomic<bool> block{};
    std::latch entered{1};
    std::latch release{1};
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
            scheduler.schedule(paint_call(43U)) ==
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

    GameThreadScheduler priority_scheduler{3U};
    static_cast<void>(
        priority_scheduler.schedule(paint_call(44U)));
    static_cast<void>(
        priority_scheduler.schedule(paint_call(45U)));
    static_cast<void>(priority_scheduler.schedule(
        RestoreTransientState{9U}));
    RecordingExecutor priority_executor{true};
    const auto priority_drain =
        priority_scheduler.drain(priority_executor, 1U);
    passed &= expect(
        priority_drain && *priority_drain == 1U &&
            priority_executor.operations ==
                std::vector<GameThreadOperation>{
                    RestoreTransientState{9U}} &&
            priority_scheduler.snapshot().queued == 2U,
        "control work was starved behind Paint frame work");

    GameThreadScheduler timed_scheduler{3U};
    static_cast<void>(timed_scheduler.schedule(paint_call(46U)));
    static_cast<void>(timed_scheduler.schedule(paint_call(47U)));
    RecordingExecutor timed_executor{true};
    timed_executor.paint_delay = std::chrono::milliseconds{8};
    const auto timed_drain = timed_scheduler.drain(timed_executor, 2U);
    passed &= expect(
        timed_drain && *timed_drain == 1U &&
            timed_scheduler.snapshot().queued == 1U &&
            timed_scheduler.last_paint_dispatch_us(1U) >=
                meccha::core::LocalDispatchCpuBudgetUs &&
            timed_scheduler.last_paint_dispatch_us(2U) == 0U,
        "the measured non-preemptible Paint slice did not yield at its CPU budget");

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
        lifecycle.schedule(paint_call(71U)));
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

    FakeCallbacks retry_callbacks{};
    RecordingExecutor retry_executor{true};
    RuntimeLifecycle retry_lifecycle{
        retry_callbacks,
        retry_executor,
        2U,
    };
    passed &= expect(
        retry_lifecycle.initialize().has_value(),
        "the retry lifecycle did not initialize");
    retry_callbacks.invoke(FirstFrame);
    retry_executor.fail_after = 2U;
    retry_executor.compatibility_failure = CompatibilityFailure{
        RuntimeContractId::Canvas,
        ContractFailureKind::StaleObject,
        "error.operation.failed",
    };
    constexpr auto ReplacementFrame = HudFrameIdentity{
        101U,
        201U,
        301U,
        401U,
    };
    retry_callbacks.invoke(ReplacementFrame);
    passed &= expect(
        retry_executor.operations.size() == 2U &&
            retry_lifecycle.snapshot().frame_identity ==
                FirstFrame &&
            retry_lifecycle.snapshot()
                    .last_compatibility_failure ==
                retry_executor.compatibility_failure,
        "a failed frame rebind published an unvalidated identity");
    retry_executor.fail_after = 0U;
    retry_callbacks.invoke(ReplacementFrame);
    passed &= expect(
        retry_executor.operations.back() ==
                GameThreadOperation{
                    RebindHudFrame{ReplacementFrame}} &&
            retry_lifecycle.snapshot().frame_identity ==
                ReplacementFrame,
        "a transient frame rebind failure was not retried");

    retry_executor.fail_after = retry_executor.operations.size();
    passed &= expect(
        retry_lifecycle.request_shutdown(11U).has_value(),
        "the retry lifecycle did not start shutdown");
    retry_callbacks.invoke(ReplacementFrame);
    passed &= expect(
        retry_lifecycle.snapshot().last_error ==
                RuntimeLifecycleError::ExecutionFailed &&
            !retry_lifecycle.finalize_shutdown() &&
            retry_lifecycle.finalize_shutdown().error() ==
                RuntimeLifecycleError::PendingGameThreadRestore,
        "a failed restore allowed callback unregistration");
    retry_executor.fail_after = 0U;
    retry_callbacks.invoke(ReplacementFrame);
    passed &= expect(
        retry_executor.operations.back() ==
            GameThreadOperation{RestoreTransientState{11U}},
        "a transient shutdown restore failure was not retried");

    retry_callbacks.fail_unregistration = true;
    const auto failed_unregistration =
        retry_lifecycle.finalize_shutdown();
    passed &= expect(
        !failed_unregistration &&
            failed_unregistration.error() ==
                RuntimeLifecycleError::CallbackUnregistration &&
            retry_lifecycle.snapshot().phase ==
                RuntimePhase::Failed &&
            retry_callbacks.callback != nullptr,
        "an unregistration failure was reported as a clean stop");
    retry_callbacks.fail_unregistration = false;

    FakeCallbacks destructor_callbacks{};
    {
        RecordingExecutor destructor_executor{true};
        RuntimeLifecycle destructor_lifecycle{
            destructor_callbacks,
            destructor_executor,
            2U,
        };
        passed &= expect(
            destructor_lifecycle.initialize().has_value(),
            "the destructor-recovery lifecycle did not initialize");
        destructor_callbacks.invoke(FirstFrame);
        passed &= expect(
            destructor_lifecycle.request_shutdown(12U).has_value(),
            "the destructor-recovery lifecycle did not begin shutdown");
        destructor_callbacks.invoke(FirstFrame);
        destructor_callbacks.fail_unregistration = true;
        passed &= expect(
            !destructor_lifecycle.finalize_shutdown(),
            "the destructor-recovery fault was not injected");
        destructor_callbacks.fail_unregistration = false;
    }
    passed &= expect(
        destructor_callbacks.unregister_count == 2U &&
            destructor_callbacks.callback == nullptr &&
            destructor_callbacks.context == nullptr,
        "destruction did not retry exact callback unregistration");

    auto repeated_unregistrations = std::size_t{};
    for (auto iteration = std::size_t{}; iteration < 128U; ++iteration)
    {
        FakeCallbacks repeated_callbacks{};
        RecordingExecutor repeated_executor{true};
        {
            RuntimeLifecycle repeated{
                repeated_callbacks,
                repeated_executor,
                2U,
            };
            passed &= expect(
                repeated.initialize().has_value(),
                "a repeated lifecycle did not initialize");
            repeated_callbacks.invoke(FirstFrame);
            passed &= expect(
                repeated.request_shutdown(iteration + 1U).has_value(),
                "a repeated lifecycle did not begin shutdown");
            repeated_callbacks.invoke(FirstFrame);
            passed &= expect(
                repeated.finalize_shutdown().has_value(),
                "a repeated lifecycle did not finalize");
        }
        repeated_unregistrations +=
            repeated_callbacks.unregister_count;
    }
    passed &= expect(
        repeated_unregistrations == 128U,
        "repeated lifecycle teardown did not unregister exactly once");

    ConcurrentCallbacks concurrent_callbacks{};
    RecordingExecutor concurrent_executor{true};
    BlockingObserver concurrent_observer{};
    RuntimeLifecycle concurrent_lifecycle{
        concurrent_callbacks,
        concurrent_executor,
        2U,
        &concurrent_observer,
    };
    passed &= expect(
        concurrent_lifecycle.initialize().has_value(),
        "the concurrent-uninstall lifecycle did not initialize");
    concurrent_callbacks.invoke(FirstFrame);
    passed &= expect(
        concurrent_lifecycle.request_shutdown(129U).has_value(),
        "the concurrent-uninstall lifecycle did not begin shutdown");
    concurrent_callbacks.invoke(FirstFrame);

    concurrent_observer.block.store(true, std::memory_order_release);
    auto callback_thread = std::thread{
        [&]
        {
            concurrent_callbacks.invoke(FirstFrame);
        }};
    concurrent_observer.entered.wait();

    auto finalize_finished = std::atomic<bool>{false};
    auto finalize_succeeded = false;
    auto finalize_thread = std::thread{
        [&]
        {
            finalize_succeeded =
                concurrent_lifecycle.finalize_shutdown().has_value();
            finalize_finished.store(true, std::memory_order_release);
        }};
    concurrent_callbacks.unregister_entered.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    passed &= expect(
        !finalize_finished.load(std::memory_order_acquire),
        "finalize returned while an admitted observer callback was in flight");

    concurrent_observer.release.count_down();
    callback_thread.join();
    finalize_thread.join();
    passed &= expect(
        finalize_succeeded &&
            concurrent_callbacks.unregistered_id == 92U &&
            concurrent_lifecycle.snapshot().phase ==
                RuntimePhase::Stopped,
        "concurrent callback drain did not finish exact unregistration");

    if (passed)
    {
        std::cout << "PASS application_runtime\n";
        return 0;
    }
    return 1;
}
