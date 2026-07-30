#include <meccha/application/application_root.hpp>
#include <meccha/application/image_paint_game_runtime.hpp>
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
#include <variant>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};
constexpr auto AssetId =
    std::string_view{
        "11111111111111111111111111111111"
        "11111111111111111111111111111111"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL application_root_image_paint: "
                  << message << '\n';
    }
    return condition;
}

auto image_profile(core::BodyProfile body)
    -> core::CanonicalImageProfile
{
    const auto identity = core::expected_mesh_profile(
        body,
        core::MeshProfileRole::ImageReference);
    auto positions = std::vector<core::Vector3d>(
        identity.vertex_count,
        core::Vector3d{});
    positions[0U] = {-1.0, -1.0, -1.0};
    positions[1U] = {1.0, -1.0, -1.0};
    positions[2U] = {0.0, -1.0, 1.0};
    auto indices = std::vector<std::uint32_t>(
        identity.index_count,
        0U);
    indices[0U] = 0U;
    indices[1U] = 1U;
    indices[2U] = 2U;
    indices.back() =
        static_cast<std::uint32_t>(
            identity.vertex_count - 1U);
    auto profile = core::build_canonical_image_profile(
        core::ImageReferenceGeometry{
            identity,
            std::make_shared<
                const std::vector<core::Vector3d>>(
                std::move(positions)),
            std::make_shared<
                const std::vector<std::uint32_t>>(
                std::move(indices)),
        });
    return std::move(*profile);
}

auto project() -> std::shared_ptr<const core::ImageProject>
{
    auto source =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x11},
            });
    return std::make_shared<const core::ImageProject>(
        core::ImageProject{
            core::ImageProjectSchemaVersion,
            std::string{ProjectId},
            "Project",
            7U,
            {},
            {core::ImageLayer{
                std::string{AssetId},
                "source.png",
                core::ImageMime::Png,
                source->size(),
            }},
            {core::ImageSourceAsset{
                std::string{AssetId},
                core::ImageMime::Png,
                source,
            }},
            std::make_shared<const std::vector<std::byte>>(
                core::CanonicalAtlasByteLength,
                std::byte{0xFF}),
        });
}

class FakeStorage final : public AtomicTextStorage
{
public:
    auto read_text(std::string_view, std::size_t)
        -> std::expected<
            std::optional<std::string>,
            TextStorageError> override
    {
        return std::nullopt;
    }

    auto write_text_atomic(std::string_view, std::string_view)
        -> std::expected<void, TextStorageError> override
    {
        return {};
    }
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
    [[nodiscard]] auto is_game_thread() const noexcept
        -> bool override
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
    [[nodiscard]] auto is_game_thread() const noexcept
        -> bool override
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
        return {};
    }

    auto restore(const PaintPreviewSnapshot&)
        -> std::expected<void, RuntimeExecutionError> override
    {
        return {};
    }
};

class UnusedPaintRuntime final : public PaintGameRuntimePort
{
public:
    auto capture(const core::PaintSettings&)
        -> std::expected<
            CapturedPaintJob,
            RuntimeExecutionError> override
    {
        return std::unexpected(RuntimeExecutionError{
            RuntimeExecutionErrorCode::OperationFailure,
            std::nullopt,
        });
    }

    auto observe_queues(RuntimeObjectHandle, JobGeneration)
        -> std::expected<
            PaintQueueObservation,
            RuntimeExecutionError> override
    {
        return PaintQueueObservation{};
    }
};

class ReadyProject final : public ImageProjectReadinessPort
{
public:
    ReadyProject()
        : project_{project()}
    {
    }

    [[nodiscard]] auto snapshot() const
        -> ImageEditorPipelineSnapshot override
    {
        return ImageEditorPipelineSnapshot{
            ImageEditorPipelinePhase::Ready,
            1U,
            project_->project_id,
            current_revision_,
            false,
            std::nullopt,
        };
    }

    [[nodiscard]] auto ready_project(
        std::string_view project_id,
        std::uint64_t project_revision) const
        -> std::shared_ptr<const core::ImageProject> override
    {
        return project_id == project_->project_id &&
                       project_revision == project_->revision
                   ? project_
                   : nullptr;
    }

    auto set_current_revision(std::uint64_t revision) -> void
    {
        current_revision_ = revision;
    }

private:
    std::shared_ptr<const core::ImageProject> project_{};
    std::uint64_t current_revision_{7U};
};

class FakeImageRuntime final : public ImagePaintGameRuntimePort
{
public:
    auto capture(core::BodyProfile body)
        -> std::expected<
            CapturedImagePaintJob,
            RuntimeExecutionError> override
    {
        ++capture_count;
        captured_body = body;
        return CapturedImagePaintJob{
            RuntimeObjectHandle{81U, 5U},
            core::expected_mesh_profile(
                body,
                core::MeshProfileRole::Raw),
            image_profile(body),
            {core::CapturedImagePaintSample{
                core::Region::Front,
                0,
                0.5,
                0.5,
                true,
                0.5,
                0.5,
                0.5,
                core::ImageTriangleAnchor{
                    0U,
                    1.0 / 3.0,
                    1.0 / 3.0,
                    1.0 / 3.0,
                },
                true,
            }},
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
    core::BodyProfile captured_body{};
    RuntimeObjectHandle observed_component{};
    JobGeneration observed_generation{};
    bool hold_queues{};
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
    auto preview = FakePreviewRuntime{};
    auto paint = UnusedPaintRuntime{};
    auto projects = ReadyProject{};
    auto image = FakeImageRuntime{};
    auto root = ApplicationRoot{
        callbacks,
        executor,
        storage,
        paint,
        thread,
        preview,
        image,
        projects,
        4U,
        2U,
        8U,
    };

    passed &= expect(
        root.initialize().has_value() &&
            root.enqueue_command(StartImagePaint{
                301U,
                std::string{ProjectId},
                7U,
            }) == CommandEnqueueResult::Accepted,
        "the composition root rejected a typed Image Paint command");

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
        if (std::holds_alternative<PaintAtUvWithBrush>(
                operation))
        {
            ++paint_calls;
        }
    }
    const auto completed = root.snapshot();
    passed &= expect(
        completed->job.phase == JobPhase::Completed &&
            completed->job.feature == Feature::ImagePaint &&
            completed->job.command_id == 301U &&
            completed->image_editor.phase ==
                ImageEditorPipelinePhase::Ready &&
            completed->image_editor.project_id == ProjectId &&
            completed->image_editor.project_revision == 7U &&
            completed->job.progress.total == 1U &&
            completed->job.progress.submitted == 1U &&
            image.capture_count == 1U &&
            image.captured_body == core::BodyProfile::Round &&
            image.observe_count > 0U &&
            image.observed_component ==
                RuntimeObjectHandle{81U, 5U} &&
            image.observed_generation ==
                completed->job.generation &&
            paint_calls == 1U,
        "Image Paint capture, planning, dispatch, or drain was not connected");

    const auto diagnostics =
        completed->diagnostics.size();
    passed &= expect(
        root.enqueue_command(StartImagePaint{
            302U,
            std::string{ProjectId},
            6U,
        }) == CommandEnqueueResult::Accepted,
        "the stale project command did not reach validation");
    callbacks.invoke(Frame);
    passed &= expect(
        image.capture_count == 1U &&
            root.snapshot()->diagnostics.size() ==
                diagnostics + 1U,
        "a stale project revision reached game capture");

    image.hold_queues = true;
    passed &= expect(
        root.enqueue_command(StartImagePaint{
            303U,
            std::string{ProjectId},
            7U,
        }) == CommandEnqueueResult::Accepted,
        "the cancellable Image Paint job was rejected");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        if (root.snapshot()->job.phase == JobPhase::Draining)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Draining &&
            root.enqueue_command(CancelImagePaint{304U}) ==
                CommandEnqueueResult::Accepted,
        "the Image Paint fixture did not retain queue pressure");
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Cancelling,
        "typed Image Paint cancellation bypassed queue drain");
    image.hold_queues = false;
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Cancelled,
        "Image Paint cancellation did not reach a drained terminal state");

    image.hold_queues = true;
    passed &= expect(
        root.enqueue_command(StartImagePaint{
            305U,
            std::string{ProjectId},
            7U,
        }) == CommandEnqueueResult::Accepted,
        "the stale-during-dispatch Image Paint job was rejected");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        if (root.snapshot()->job.phase == JobPhase::Draining)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    const auto before_stale =
        root.snapshot()->diagnostics.size();
    projects.set_current_revision(8U);
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Cancelling,
        "a project edit did not cancel active Image Paint dispatch");
    image.hold_queues = false;
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Cancelled &&
            root.snapshot()->diagnostics.size() ==
                before_stale + 1U,
        "stale Image Paint did not drain and report its typed boundary");

    projects.set_current_revision(7U);
    image.hold_queues = true;
    passed &= expect(
        root.enqueue_command(StartImagePaint{
            306U,
            std::string{ProjectId},
            7U,
        }) == CommandEnqueueResult::Accepted,
        "the shutdown Image Paint job was rejected");
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        callbacks.invoke(Frame);
        if (root.snapshot()->job.phase == JobPhase::Draining)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Draining &&
            root.request_shutdown(99U).has_value(),
        "shutdown did not observe the active Image Paint queues");
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Cancelling &&
            !root.finalize_shutdown(),
        "shutdown quiesced before Image Paint cancellation drained");
    image.hold_queues = false;
    callbacks.invoke(Frame);
    passed &= expect(
        root.snapshot()->job.phase == JobPhase::Cancelled &&
            !root.finalize_shutdown(),
        "shutdown did not terminally cancel Image Paint");
    callbacks.invoke(Frame);
    passed &= expect(
        root.finalize_shutdown().has_value(),
        "Image Paint cancellation did not release final shutdown");

    if (passed)
    {
        std::cout << "PASS application_root_image_paint\n";
    }
    return passed ? 0 : 1;
}
