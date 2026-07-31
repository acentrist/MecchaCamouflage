#include <meccha/application/application_root.hpp>
#include <meccha/application/paint_game_runtime.hpp>
#include <meccha/application/paint_preview_controller.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL application_root_paint: "
                  << message << '\n';
    }
    return condition;
}

auto image(std::uint32_t dimension = 8U) -> PaintTextureImage
{
    const auto bytes = static_cast<std::size_t>(
        dimension * dimension * 4U);
    return PaintTextureImage{
        dimension,
        std::make_shared<const std::vector<std::byte>>(
            bytes,
            std::byte{0x11}),
        std::make_shared<const std::vector<std::byte>>(
            bytes,
            std::byte{0x22}),
    };
}

class FakeStorage final : public AtomicTextStorage
{
public:
    auto read_text(std::string_view, std::size_t)
        -> std::expected<std::optional<std::string>, TextStorageError>
        override
    {
        return text;
    }

    auto write_text_atomic(
        std::string_view,
        std::string_view value)
        -> std::expected<void, TextStorageError> override
    {
        ++write_attempts;
        if (fail_writes)
        {
            return std::unexpected(TextStorageError{
                TextStorageErrorCode::Io,
                "injected write failure",
            });
        }
        text = value;
        ++writes;
        return {};
    }

    std::optional<std::string> text{};
    std::size_t write_attempts{};
    std::size_t writes{};
    bool fail_writes{};
};

class FakeCallbacks final : public RuntimeCallbackPort
{
public:
    auto register_hud_callback(
        void* callback_context,
        HudCallback callback_function)
        -> std::expected<CallbackId, CallbackPortError> override
    {
        context = callback_context;
        callback = callback_function;
        return CallbackId{51U};
    }

    auto unregister_hud_callback(CallbackId)
        -> std::expected<void, CallbackPortError> override
    {
        callback = nullptr;
        context = nullptr;
        return {};
    }

    auto invoke(const HudFrameIdentity& identity) -> void
    {
        callback(context, identity);
    }

    void* context{};
    HudCallback callback{};
};

class RecordingExecutor final : public GameThreadExecutor
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return true;
    }

    auto execute(const GameThreadOperation& operation)
        -> std::expected<void, RuntimeExecutionError> override
    {
        operations.push_back(operation);
        if (events &&
            std::holds_alternative<RestoreTransientState>(
                operation))
        {
            events->push_back("transient_restore");
        }
        return {};
    }

    std::vector<GameThreadOperation> operations{};
    std::vector<std::string>* events{};
};

class FakeThreadContext final : public GameThreadContext
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return true;
    }
};

class FakePreviewRuntime final : public PaintPreviewRuntimePort
{
public:
    auto capture(RuntimeObjectHandle component)
        -> std::expected<
            PaintPreviewSnapshot,
            RuntimeExecutionError> override
    {
        ++capture_count;
        return PaintPreviewSnapshot{
            component,
            image(),
        };
    }

    auto apply(
        RuntimeObjectHandle component,
        const PaintTextureImage& applied)
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++apply_count;
        applied_component = component;
        applied_image = applied;
        return {};
    }

    auto restore(const PaintPreviewSnapshot& snapshot)
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++restore_count;
        restored_components.push_back(snapshot.component);
        if (restore_failures_remaining > 0U)
        {
            --restore_failures_remaining;
            if (events)
            {
                events->push_back("preview_restore_failed");
            }
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
                std::nullopt,
            });
        }
        if (events)
        {
            events->push_back("preview_restore");
        }
        return {};
    }

    std::size_t capture_count{};
    std::size_t apply_count{};
    std::size_t restore_count{};
    RuntimeObjectHandle applied_component{};
    PaintTextureImage applied_image{};
    std::vector<RuntimeObjectHandle> restored_components{};
    std::vector<std::string>* events{};
    std::size_t restore_failures_remaining{};
};

class FakePaintRuntime final : public PaintGameRuntimePort
{
public:
    [[nodiscard]] auto make_captured(
        const core::PaintSettings& settings) const
        -> CapturedPaintJob
    {
        auto sample = core::CapturedPaintSample{};
        sample.region = core::Region::Side;
        sample.u = 0.25;
        sample.v = 0.75;
        sample.has_current_view_position = true;
        sample.current_view_vertical = 0.25;
        sample.fallback_view_vertical = 0.25;
        sample.horizontal = 0.25;
        sample.intrinsic_color = core::Rgb8{10U, 20U, 30U};
        sample.scene_color = sample.intrinsic_color;
        if (settings.auto_material)
        {
            sample.automatic_appearance =
                core::ResolvedPaintAppearance{
                    core::Rgb8{40U, 50U, 60U},
                    core::Material{0.25, 0.5, 0.75},
                };
            sample.automatic_appearance_available = true;
        }
        sample.safe = true;
        return CapturedPaintJob{
            RuntimeObjectHandle{71U, 9U},
            core::PaintPlanRequest{
                core::expected_mesh_profile(
                    core::BodyProfile::Round,
                    core::MeshProfileRole::Raw),
                settings,
                {sample},
            },
            core::ReplicationPacingPlan{
                100,
                10,
                100,
                100,
                100,
                1,
                1,
                10,
                1,
                0,
            },
        };
    }

    auto capture(const core::PaintSettings& settings)
        -> std::expected<
            CapturedPaintJob,
            RuntimeExecutionError> override
    {
        ++capture_count;
        captured_settings = settings;
        return make_captured(settings);
    }

    auto begin_automatic_capture(
        const core::PaintSettings& settings,
        JobGeneration generation)
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++automatic_begin_count;
        automatic_settings = settings;
        automatic_generation = generation;
        automatic_advance_count = 0U;
        automatic_cancel_count = 0U;
        automatic_active = true;
        return {};
    }

    auto advance_automatic_capture(JobGeneration generation)
        -> std::expected<
            std::optional<CapturedPaintJob>,
            RuntimeExecutionError> override
    {
        if (!automatic_active ||
            generation != automatic_generation)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::InvalidRequest,
                std::nullopt,
            });
        }
        ++automatic_advance_count;
        if (fail_next_automatic_advance)
        {
            fail_next_automatic_advance = false;
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
                std::nullopt,
            });
        }
        if (automatic_advance_count <
            automatic_complete_after)
        {
            return std::optional<CapturedPaintJob>{};
        }
        automatic_active = false;
        return std::optional<CapturedPaintJob>{
            make_captured(automatic_settings)};
    }

    auto cancel_automatic_capture(JobGeneration generation)
        -> std::expected<bool, RuntimeExecutionError> override
    {
        if (!automatic_active ||
            generation != automatic_generation)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::InvalidRequest,
                std::nullopt,
            });
        }
        ++automatic_cancel_count;
        if (automatic_cancel_count <
            automatic_cancel_after)
        {
            return false;
        }
        automatic_active = false;
        if (events)
        {
            events->push_back("automatic_restore");
        }
        return true;
    }

    auto observe_queues(
        RuntimeObjectHandle component,
        JobGeneration generation)
        -> std::expected<
            PaintQueueObservation,
            RuntimeExecutionError> override
    {
        ++observe_count;
        observed_component = component;
        observed_generation = generation;
        return hold_queues
                   ? PaintQueueObservation{
                         true,
                         true,
                         1U,
                         true,
                         1U,
                     }
                   : PaintQueueObservation{};
    }

    std::size_t capture_count{};
    std::size_t observe_count{};
    core::PaintSettings captured_settings{};
    RuntimeObjectHandle observed_component{};
    JobGeneration observed_generation{};
    bool hold_queues{};
    std::size_t automatic_begin_count{};
    std::size_t automatic_advance_count{};
    std::size_t automatic_cancel_count{};
    std::size_t automatic_complete_after{3U};
    std::size_t automatic_cancel_after{2U};
    core::PaintSettings automatic_settings{};
    JobGeneration automatic_generation{};
    bool automatic_active{};
    bool fail_next_automatic_advance{};
    std::vector<std::string>* events{};
};
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto storage = FakeStorage{};
    auto initial_config = core::ApplicationConfig{};
    initial_config.esp.enabled = false;
    const auto initial_json = encode_config(initial_config);
    if (!initial_json)
    {
        return 1;
    }
    storage.text = *initial_json;
    auto callbacks = FakeCallbacks{};
    auto executor = RecordingExecutor{};
    auto thread = FakeThreadContext{};
    auto preview_runtime = FakePreviewRuntime{};
    auto paint_runtime = FakePaintRuntime{};
    auto shutdown_events = std::vector<std::string>{};
    executor.events = &shutdown_events;
    preview_runtime.events = &shutdown_events;
    auto root = ApplicationRoot{
        callbacks,
        executor,
        storage,
        paint_runtime,
        thread,
        preview_runtime,
        4U,
        2U,
        8U,
    };

    passed &= expect(
        root.initialize().has_value(),
        "the composition root did not initialize");
    passed &= expect(
        !root.snapshot()->esp_enabled &&
            !root.snapshot()->settings.esp.enabled,
        "persisted ESP enablement did not initialize runtime state");
    passed &= expect(
        root.enqueue_command(StartPaint{
            101U,
            core::PaintSettings{},
        }) == CommandEnqueueResult::Accepted &&
            root.snapshot()->command_queue.queued == 1U,
        "the composition root did not accept a typed Paint command");

    constexpr auto Frame =
        HudFrameIdentity{1U, 2U, 3U, 4U};
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        if (root.snapshot()->job.phase == JobPhase::Completed)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }

    auto paint_calls = std::size_t{};
    for (const auto& operation : executor.operations)
    {
        if (std::holds_alternative<PaintAtUvWithBrush>(operation))
        {
            ++paint_calls;
        }
    }
    const auto completed = root.snapshot();
    passed &= expect(
        completed->runtime_phase ==
                ApplicationRuntimePhase::Compatible &&
            completed->job.phase == JobPhase::Completed &&
            completed->job.feature == Feature::Paint &&
            completed->job.command_id == 101U &&
            completed->job.progress.total == 1U &&
            completed->job.progress.submitted == 1U &&
            completed->command_queue.queued == 0U &&
            paint_runtime.capture_count == 1U &&
            paint_runtime.captured_settings ==
                core::PaintSettings{} &&
            paint_runtime.observe_count > 0U &&
            paint_runtime.observed_component ==
                RuntimeObjectHandle{71U, 9U} &&
            paint_runtime.observed_generation ==
                completed->job.generation &&
            paint_calls == 1U,
        "typed capture, planning, dispatch, observation, or drain was not "
        "connected end to end");

    auto automatic_storage = FakeStorage{};
    auto automatic_callbacks = FakeCallbacks{};
    auto automatic_executor = RecordingExecutor{};
    auto automatic_thread = FakeThreadContext{};
    auto automatic_preview_runtime = FakePreviewRuntime{};
    auto automatic_runtime = FakePaintRuntime{};
    auto automatic_root = ApplicationRoot{
        automatic_callbacks,
        automatic_executor,
        automatic_storage,
        automatic_runtime,
        automatic_thread,
        automatic_preview_runtime,
        4U,
        2U,
        8U,
    };
    auto automatic_settings = core::PaintSettings{};
    automatic_settings.auto_material = true;
    passed &= expect(
        automatic_root.initialize().has_value() &&
            automatic_root.enqueue_command(StartPaint{
                111U,
                automatic_settings,
            }) == CommandEnqueueResult::Accepted,
        "the multi-frame Auto Material fixture did not start");
    for (auto attempt = 0;
         attempt < 10 &&
         automatic_runtime.automatic_begin_count == 0U;
         ++attempt)
    {
        automatic_callbacks.invoke(Frame);
    }
    auto automatic_paint_calls_after_admission =
        std::size_t{};
    for (const auto& operation : automatic_executor.operations)
    {
        if (std::holds_alternative<PaintAtUvWithBrush>(
                operation))
        {
            ++automatic_paint_calls_after_admission;
        }
    }
    passed &= expect(
        automatic_runtime.automatic_begin_count == 1U &&
            automatic_runtime.capture_count == 0U &&
            automatic_runtime.automatic_advance_count == 0U &&
            automatic_paint_calls_after_admission == 0U,
        "Auto Material used the synchronous capture path or dispatched in "
        "its admission frame");
    automatic_callbacks.invoke(Frame);
    auto automatic_paint_calls_after_first_advance =
        std::size_t{};
    for (const auto& operation : automatic_executor.operations)
    {
        if (std::holds_alternative<PaintAtUvWithBrush>(
                operation))
        {
            ++automatic_paint_calls_after_first_advance;
        }
    }
    passed &= expect(
        automatic_runtime.automatic_advance_count == 1U &&
            automatic_paint_calls_after_first_advance == 0U,
        "Auto Material completed without a later HUD-frame feedback step");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        automatic_callbacks.invoke(Frame);
        if (automatic_root.snapshot()->job.phase ==
            JobPhase::Completed)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    auto automatic_paint_calls = std::size_t{};
    for (const auto& operation : automatic_executor.operations)
    {
        if (std::holds_alternative<PaintAtUvWithBrush>(
                operation))
        {
            ++automatic_paint_calls;
        }
    }
    passed &= expect(
        automatic_runtime.automatic_advance_count == 3U &&
            automatic_root.snapshot()->job.command_id == 111U &&
            automatic_root.snapshot()->job.phase ==
                JobPhase::Completed &&
            automatic_paint_calls == 1U,
        "a restored Auto Material capture did not enter normal planning and "
        "bounded dispatch");

    auto cancelled_storage = FakeStorage{};
    auto cancelled_callbacks = FakeCallbacks{};
    auto cancelled_executor = RecordingExecutor{};
    auto cancelled_thread = FakeThreadContext{};
    auto cancelled_preview_runtime = FakePreviewRuntime{};
    auto cancelled_runtime = FakePaintRuntime{};
    cancelled_runtime.automatic_complete_after = 100U;
    auto cancelled_root = ApplicationRoot{
        cancelled_callbacks,
        cancelled_executor,
        cancelled_storage,
        cancelled_runtime,
        cancelled_thread,
        cancelled_preview_runtime,
        4U,
        2U,
        8U,
    };
    passed &= expect(
        cancelled_root.initialize().has_value() &&
            cancelled_root.enqueue_command(StartPaint{
                112U,
                automatic_settings,
            }) == CommandEnqueueResult::Accepted,
        "the cancellable Auto Material fixture did not start");
    for (auto attempt = 0;
         attempt < 10 &&
         cancelled_runtime.automatic_begin_count == 0U;
         ++attempt)
    {
        cancelled_callbacks.invoke(Frame);
    }
    passed &= expect(
        cancelled_root.enqueue_command(CancelPaint{113U}) ==
            CommandEnqueueResult::Accepted,
        "the active Auto Material cancellation was rejected");
    cancelled_callbacks.invoke(Frame);
    cancelled_callbacks.invoke(Frame);
    auto cancelled_paint_calls = std::size_t{};
    for (const auto& operation : cancelled_executor.operations)
    {
        if (std::holds_alternative<PaintAtUvWithBrush>(
                operation))
        {
            ++cancelled_paint_calls;
        }
    }
    passed &= expect(
        cancelled_runtime.automatic_cancel_count == 2U &&
            !cancelled_runtime.automatic_active &&
            cancelled_paint_calls == 0U,
        "Auto Material cancellation did not wait for exact restoration or "
        "admitted a partial Paint job");

    auto failed_storage = FakeStorage{};
    auto failed_callbacks = FakeCallbacks{};
    auto failed_executor = RecordingExecutor{};
    auto failed_thread = FakeThreadContext{};
    auto failed_preview_runtime = FakePreviewRuntime{};
    auto failed_runtime = FakePaintRuntime{};
    failed_runtime.automatic_complete_after = 100U;
    failed_runtime.fail_next_automatic_advance = true;
    auto failed_root = ApplicationRoot{
        failed_callbacks,
        failed_executor,
        failed_storage,
        failed_runtime,
        failed_thread,
        failed_preview_runtime,
        4U,
        2U,
        8U,
    };
    passed &= expect(
        failed_root.initialize().has_value() &&
            failed_root.enqueue_command(StartPaint{
                114U,
                automatic_settings,
            }) == CommandEnqueueResult::Accepted,
        "the failed Auto Material fixture did not start");
    for (auto attempt = 0;
         attempt < 10 &&
         failed_runtime.automatic_begin_count == 0U;
         ++attempt)
    {
        failed_callbacks.invoke(Frame);
    }
    failed_callbacks.invoke(Frame);
    failed_callbacks.invoke(Frame);
    failed_callbacks.invoke(Frame);
    auto failed_paint_calls = std::size_t{};
    for (const auto& operation : failed_executor.operations)
    {
        if (std::holds_alternative<PaintAtUvWithBrush>(
                operation))
        {
            ++failed_paint_calls;
        }
    }
    passed &= expect(
        failed_runtime.automatic_advance_count == 1U &&
            failed_runtime.automatic_cancel_count == 2U &&
            !failed_runtime.automatic_active &&
            failed_paint_calls == 0U,
        "a failed Auto Material feedback stage bypassed restoration or "
        "published a partial job");

    auto automatic_shutdown_storage = FakeStorage{};
    auto automatic_shutdown_callbacks = FakeCallbacks{};
    auto automatic_shutdown_executor = RecordingExecutor{};
    auto automatic_shutdown_thread = FakeThreadContext{};
    auto automatic_shutdown_preview_runtime = FakePreviewRuntime{};
    auto automatic_shutdown_runtime = FakePaintRuntime{};
    automatic_shutdown_runtime.automatic_complete_after = 100U;
    auto automatic_shutdown_events =
        std::vector<std::string>{};
    automatic_shutdown_executor.events =
        &automatic_shutdown_events;
    automatic_shutdown_runtime.events =
        &automatic_shutdown_events;
    auto automatic_shutdown_root = ApplicationRoot{
        automatic_shutdown_callbacks,
        automatic_shutdown_executor,
        automatic_shutdown_storage,
        automatic_shutdown_runtime,
        automatic_shutdown_thread,
        automatic_shutdown_preview_runtime,
        4U,
        2U,
        8U,
    };
    passed &= expect(
        automatic_shutdown_root.initialize().has_value() &&
            automatic_shutdown_root.enqueue_command(StartPaint{
                115U,
                automatic_settings,
            }) == CommandEnqueueResult::Accepted,
        "the Auto Material shutdown fixture did not start");
    for (auto attempt = 0;
         attempt < 10 &&
         automatic_shutdown_runtime.automatic_begin_count ==
             0U;
         ++attempt)
    {
        automatic_shutdown_callbacks.invoke(Frame);
    }
    passed &= expect(
        automatic_shutdown_root.request_shutdown(91U)
            .has_value() &&
            !automatic_shutdown_root.finalize_shutdown(),
        "shutdown did not wait for active Auto Material ownership");
    auto automatic_shutdown_finalized = false;
    for (auto attempt = 0; attempt < 10; ++attempt)
    {
        automatic_shutdown_callbacks.invoke(Frame);
        if (automatic_shutdown_root.finalize_shutdown())
        {
            automatic_shutdown_finalized = true;
            break;
        }
    }
    passed &= expect(
        automatic_shutdown_events ==
                std::vector<std::string>{
                    "automatic_restore",
                    "transient_restore"} &&
            automatic_shutdown_finalized,
        "shutdown did not restore Auto Material before generic transient "
        "state and lifecycle finalization");

    passed &= expect(
        root.enqueue_command(ToggleUi{102U}) ==
                CommandEnqueueResult::Accepted &&
            root.enqueue_command(ToggleEsp{103U}) ==
                CommandEnqueueResult::Accepted &&
            root.enqueue_command(ToggleUi{104U}) ==
                CommandEnqueueResult::Full,
        "the root bypassed bounded command backpressure");
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->ui_open &&
            root.snapshot()->esp_enabled &&
            root.snapshot()->settings.esp.enabled &&
            storage.writes == 1U &&
            root.snapshot()->command_queue.queued == 0U,
        "ESP config ownership or durable toggle handling diverged");

    storage.fail_writes = true;
    passed &= expect(
        root.enqueue_command(ToggleEsp{150U}) ==
            CommandEnqueueResult::Accepted,
        "the ESP persistence failure fixture was rejected");
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->esp_enabled &&
            root.snapshot()->settings.esp.enabled &&
            storage.write_attempts == 2U &&
            storage.writes == 1U &&
            root.snapshot()->diagnostics.back().command_id == 150U,
        "a failed ESP save changed state or lost its command diagnostic");
    storage.fail_writes = false;

    passed &= expect(
        root.enqueue_command(PreviewPaint{
            105U,
            core::PaintSettings{},
        }) == CommandEnqueueResult::Accepted,
        "the root rejected a typed Paint preview command");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        if (preview_runtime.apply_count == 1U)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    passed &= expect(
        paint_runtime.capture_count == 2U &&
            preview_runtime.capture_count == 1U &&
            preview_runtime.apply_count == 1U &&
            preview_runtime.applied_component ==
                RuntimeObjectHandle{71U, 9U} &&
            preview_runtime.applied_image.dimension == 8U &&
            root.snapshot()->preview.feature == Feature::Paint,
        "Paint preview capture, off-thread build, or game-thread apply was "
        "not connected");

    passed &= expect(
        root.enqueue_command(RestorePaintPreview{106U}) ==
            CommandEnqueueResult::Accepted,
        "the root rejected explicit Paint preview restoration");
    callbacks.invoke(Frame);
    passed &= expect(
        preview_runtime.restore_count == 1U &&
            !root.snapshot()->preview.feature,
        "explicit Paint preview restoration did not release the exact lease");

    passed &= expect(
        root.enqueue_command(PreviewPaint{
            107U,
            core::PaintSettings{},
        }) == CommandEnqueueResult::Accepted,
        "the replacement preview fixture was rejected");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        if (preview_runtime.apply_count == 2U)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    passed &= expect(
        root.enqueue_command(StartPaint{
            108U,
            core::PaintSettings{},
        }) == CommandEnqueueResult::Accepted,
        "the post-preview Paint fixture was rejected");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        const auto current = root.snapshot();
        if (current->job.command_id == 108U &&
            current->job.phase == JobPhase::Completed)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    passed &= expect(
        preview_runtime.restore_count == 2U &&
            !root.snapshot()->preview.feature &&
            root.snapshot()->job.command_id == 108U &&
            root.snapshot()->job.phase == JobPhase::Completed,
        "real Paint did not restore an active preview before capture and "
        "dispatch");

    passed &= expect(
        root.enqueue_command(PreviewPaint{
            109U,
            core::PaintSettings{},
        }) == CommandEnqueueResult::Accepted,
        "the shutdown preview fixture was rejected");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        if (preview_runtime.apply_count == 3U)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    shutdown_events.clear();
    preview_runtime.restore_failures_remaining = 1U;
    passed &= expect(
        root.request_shutdown(77U).has_value() &&
            !root.finalize_shutdown() &&
            root.snapshot()->runtime_phase ==
                ApplicationRuntimePhase::ShuttingDown,
        "shutdown did not wait for project-owned preview restoration");
    callbacks.invoke(Frame);
    passed &= expect(
        shutdown_events ==
                std::vector<std::string>{
                    "preview_restore_failed"} &&
            root.snapshot()->preview.feature == Feature::Paint &&
            !root.finalize_shutdown(),
        "failed project preview restoration did not retain ownership and "
        "block lifecycle quiescing");
    callbacks.invoke(Frame);
    passed &= expect(
        shutdown_events ==
                std::vector<std::string>{
                    "preview_restore_failed",
                    "preview_restore"} &&
            !root.snapshot()->preview.feature &&
            !root.finalize_shutdown(),
        "project-owned preview restoration did not precede lifecycle "
        "quiescing");
    callbacks.invoke(Frame);
    passed &= expect(
        shutdown_events ==
                std::vector<std::string>{
                    "preview_restore_failed",
                    "preview_restore",
                    "transient_restore"} &&
            root.finalize_shutdown().has_value(),
        "generic transient restoration or callback finalization bypassed the "
        "project-owned preview barrier");

    auto active_storage = FakeStorage{};
    auto active_callbacks = FakeCallbacks{};
    auto active_executor = RecordingExecutor{};
    auto active_thread = FakeThreadContext{};
    auto active_preview_runtime = FakePreviewRuntime{};
    auto active_paint_runtime = FakePaintRuntime{};
    active_paint_runtime.hold_queues = true;
    auto active_root = ApplicationRoot{
        active_callbacks,
        active_executor,
        active_storage,
        active_paint_runtime,
        active_thread,
        active_preview_runtime,
        4U,
        2U,
        8U,
    };
    passed &= expect(
        active_root.initialize().has_value() &&
            active_root.enqueue_command(StartPaint{
                201U,
                core::PaintSettings{},
            }) == CommandEnqueueResult::Accepted,
        "the active-Paint shutdown fixture did not start");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        active_callbacks.invoke(Frame);
        if (active_root.snapshot()->job.phase ==
            JobPhase::Draining)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    passed &= expect(
        active_root.snapshot()->job.phase == JobPhase::Draining &&
            active_root.request_shutdown(88U).has_value(),
        "the shutdown fixture did not retain an observed active Paint job");
    active_callbacks.invoke(Frame);
    passed &= expect(
        active_root.snapshot()->job.phase == JobPhase::Cancelling &&
            !active_root.finalize_shutdown(),
        "shutdown quiesced before the active Paint generation drained");
    active_paint_runtime.hold_queues = false;
    active_callbacks.invoke(Frame);
    passed &= expect(
        active_root.snapshot()->job.phase == JobPhase::Cancelled &&
            !active_root.finalize_shutdown(),
        "shutdown did not terminally cancel the active Paint generation");
    active_callbacks.invoke(Frame);
    passed &= expect(
        active_root.finalize_shutdown().has_value(),
        "active Paint cancellation did not release lifecycle finalization");

    if (passed)
    {
        std::cout << "PASS application_root_paint\n";
    }
    return passed ? 0 : 1;
}
