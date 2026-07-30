#include <meccha/application/application_root.hpp>
#include <meccha/application/paint_game_runtime.hpp>
#include <meccha/application/paint_preview_controller.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
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
        return {};
    }

    std::vector<GameThreadOperation> operations{};
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
    auto capture(RuntimeObjectHandle)
        -> std::expected<
            PaintPreviewSnapshot,
            RuntimeExecutionError> override
    {
        return std::unexpected(RuntimeExecutionError{
            RuntimeExecutionErrorCode::OperationFailure,
            std::nullopt,
        });
    }

    auto apply(RuntimeObjectHandle, const PaintTextureImage&)
        -> std::expected<void, RuntimeExecutionError> override
    {
        return std::unexpected(RuntimeExecutionError{
            RuntimeExecutionErrorCode::OperationFailure,
            std::nullopt,
        });
    }

    auto restore(const PaintPreviewSnapshot&)
        -> std::expected<void, RuntimeExecutionError> override
    {
        return std::unexpected(RuntimeExecutionError{
            RuntimeExecutionErrorCode::OperationFailure,
            std::nullopt,
        });
    }
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

    if (passed)
    {
        std::cout << "PASS application_root_paint\n";
    }
    return passed ? 0 : 1;
}
