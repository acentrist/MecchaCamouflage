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
        return std::nullopt;
    }

    auto write_text_atomic(std::string_view, std::string_view)
        -> std::expected<void, TextStorageError> override
    {
        ++writes;
        return {};
    }

    std::size_t writes{};
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
    auto capture(const core::PaintSettings& settings)
        -> std::expected<
            CapturedPaintJob,
            RuntimeExecutionError> override
    {
        ++capture_count;
        captured_settings = settings;
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
        return PaintQueueObservation{};
    }

    std::size_t capture_count{};
    std::size_t observe_count{};
    core::PaintSettings captured_settings{};
    RuntimeObjectHandle observed_component{};
    JobGeneration observed_generation{};
};
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto storage = FakeStorage{};
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
        root.initialize().has_value() &&
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
            !root.snapshot()->esp_enabled &&
            root.snapshot()->command_queue.queued == 0U,
        "UI/ESP commands did not mutate only frame-owned state");

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

    if (passed)
    {
        std::cout << "PASS application_root_paint\n";
    }
    return passed ? 0 : 1;
}
