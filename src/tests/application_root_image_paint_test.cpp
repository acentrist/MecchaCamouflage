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
#include <tuple>
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
    positions.back() = positions[2U];
    auto indices = std::vector<std::uint32_t>(
        identity.index_count,
        0U);
    for (auto triangle = std::size_t{};
         triangle < identity.triangle_count;
         ++triangle)
    {
        const auto base = triangle * 3U;
        indices[base] = 0U;
        indices[base + 1U] = 1U;
        indices[base + 2U] = 2U;
    }
    indices.back() =
        static_cast<std::uint32_t>(
            identity.vertex_count - 1U);
    auto bones = std::vector<core::ImageReferenceBone>{};
    bones.reserve(identity.bone_count);
    for (auto index = std::size_t{};
         index < identity.bone_count;
         ++index)
    {
        bones.push_back(core::ImageReferenceBone{
            index == 0U
                ? std::nullopt
                : std::optional<std::size_t>{0U},
            core::Vector3d{
                static_cast<double>(index),
                0.0,
                0.0,
            },
        });
    }
    auto profile = core::build_canonical_image_profile(
        core::ImageReferenceGeometry{
            identity,
            std::make_shared<
                const std::vector<core::Vector3d>>(
                std::move(positions)),
            std::make_shared<
                const std::vector<std::uint32_t>>(
                std::move(indices)),
            std::make_shared<
                const std::vector<core::ImageReferenceBone>>(
                std::move(bones)),
        });
    return std::move(*profile);
}

auto sampling_profile(core::BodyProfile body)
    -> core::PaintSamplingProfile
{
    const auto identity = core::expected_mesh_profile(
        body,
        core::MeshProfileRole::Raw);
    auto vertices = std::vector<core::PaintSamplingVertex>(
        identity.vertex_count,
        core::PaintSamplingVertex{0.5, 0.5});
    vertices[0U] = {0.5, 0.5};
    vertices[1U] = {0.5001, 0.5};
    vertices[2U] = {0.5, 0.5001};
    vertices.back() = vertices[2U];
    auto triangles =
        std::vector<core::PaintSamplingTriangle>(
            identity.triangle_count);
    for (auto& triangle : triangles)
    {
        triangle =
            core::PaintSamplingTriangle{0U, 1U, 2U, 0U};
    }
    triangles.back().third =
        static_cast<std::uint32_t>(
            identity.vertex_count - 1U);
    auto bones = std::vector<core::PaintSamplingBone>{};
    bones.reserve(identity.bone_count);
    for (auto index = std::size_t{};
         index < identity.bone_count;
         ++index)
    {
        bones.push_back(core::PaintSamplingBone{
            "bone_" + std::to_string(index),
            index == 0U
                ? std::nullopt
                : std::optional<std::size_t>{0U},
        });
    }
    return core::PaintSamplingProfile{
        identity,
        std::make_shared<
            const std::vector<core::PaintSamplingVertex>>(
            std::move(vertices)),
        std::make_shared<
            const std::vector<core::PaintSamplingTriangle>>(
            std::move(triangles)),
        std::make_shared<
            const std::vector<core::PaintSamplingBone>>(
            std::move(bones)),
    };
}

auto image_settings() -> core::ImageProjectSettings
{
    auto settings = core::ImageProjectSettings{};
    settings.color_compression_tolerance_percent = 1.0;
    return settings;
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
            image_settings(),
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

class ReadyProject final : public ImageEditorSessionPort
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

    [[nodiscard]] auto ready_content(
        std::string_view,
        std::uint64_t) const
        -> std::shared_ptr<
            const ImageEditorReadyContent> override
    {
        return {};
    }

    [[nodiscard]] auto recover_startup(
        const core::ApplicationConfig& config)
        -> std::expected<
            ImageEditorStartupSnapshot,
            ImageEditorStartupError> override
    {
        ++recovery_count;
        recovered_config = config;
        if (fail_recovery)
        {
            return std::unexpected(
                ImageEditorStartupError::PersistenceException);
        }
        startup_.attempted = true;
        startup_.source =
            RecoveredImageProjectSource::NamedProject;
        startup_.pipeline_generation = 1U;
        return startup_;
    }

    auto set_current_revision(std::uint64_t revision) -> void
    {
        current_revision_ = revision;
    }

    auto load(
        CommandId command_id,
        std::string project_id,
        core::ApplicationConfig config)
        -> std::expected<
            void,
            ImageEditorSessionStartError> override
    {
        ++load_count;
        config.active_image_project =
            core::ActiveImageProjectReference{
                core::ImageProjectReferenceKind::NamedProject,
                project_id,
            };
        completion_ = ImageEditorSessionCompletion{
            command_id,
            ImageProjectIoOperation::Load,
            ImageEditorSessionSuccess{
                std::move(project_id),
                current_revision_,
                1U,
                std::move(config),
                false,
            },
        };
        return {};
    }

    auto import_project(
        CommandId command_id,
        std::shared_ptr<const std::vector<std::byte>> bytes,
        core::ApplicationConfig config)
        -> std::expected<
            void,
            ImageEditorSessionStartError> override
    {
        ++import_count;
        imported_bytes = std::move(bytes);
        config.active_image_project =
            core::ActiveImageProjectReference{
                core::ImageProjectReferenceKind::NamedProject,
                std::string{ProjectId},
            };
        completion_ = ImageEditorSessionCompletion{
            command_id,
            ImageProjectIoOperation::Import,
            ImageEditorSessionSuccess{
                std::string{ProjectId},
                current_revision_,
                1U,
                std::move(config),
                false,
            },
        };
        return {};
    }

    auto save(
        CommandId command_id,
        std::string_view project_id,
        std::uint64_t expected_revision,
        core::ApplicationConfig config)
        -> std::expected<
            void,
            ImageEditorSessionStartError> override
    {
        ++save_count;
        saved_expected_revision = expected_revision;
        completion_ = ImageEditorSessionCompletion{
            command_id,
            ImageProjectIoOperation::Save,
            ImageEditorSessionSuccess{
                std::string{project_id},
                current_revision_,
                std::nullopt,
                std::move(config),
                false,
            },
        };
        return {};
    }

    auto rename(
        CommandId command_id,
        std::string_view project_id,
        std::uint64_t expected_revision,
        std::string new_name)
        -> std::expected<
            void,
            ImageEditorSessionStartError> override
    {
        ++rename_count;
        renamed_expected_revision = expected_revision;
        renamed_to = std::move(new_name);
        completion_ = ImageEditorSessionCompletion{
            command_id,
            ImageProjectIoOperation::Rename,
            ImageEditorSessionSuccess{
                std::string{project_id},
                current_revision_,
            },
        };
        return {};
    }

    auto remove(
        CommandId command_id,
        std::string project_id,
        core::ApplicationConfig config)
        -> std::expected<
            void,
            ImageEditorSessionStartError> override
    {
        ++delete_count;
        config.active_image_project.reset();
        completion_ = ImageEditorSessionCompletion{
            command_id,
            ImageProjectIoOperation::Delete,
            ImageEditorSessionSuccess{
                std::move(project_id),
                current_revision_,
                std::nullopt,
                std::move(config),
                true,
            },
        };
        return {};
    }

    auto mutate(
        std::string_view project_id,
        std::uint64_t expected_revision,
        ImageEditorMutation mutation)
        -> std::expected<
            JobGeneration,
            ImageEditorMutationError> override
    {
        ++mutation_count;
        mutated_project_id = project_id;
        mutated_expected_revision = expected_revision;
        last_mutation = std::move(mutation);
        return 2U;
    }

    auto update() -> void override
    {
    }

    auto poll_completion()
        -> std::optional<
            ImageEditorSessionCompletion> override
    {
        auto completed = std::move(completion_);
        completion_.reset();
        return completed;
    }

    auto shutdown(bool) noexcept -> void override
    {
        stopped_ = true;
    }

    auto session_snapshot() const
        -> ImageEditorSessionSnapshot override
    {
        auto value = ImageEditorSessionSnapshot{};
        value.pipeline = snapshot();
        value.startup = startup_;
        value.document = ImageEditorDocumentSnapshot{
            project_->project_id,
            project_->display_name,
            project_->revision,
            project_->settings,
            project_->layers,
        };
        value.stopped = stopped_;
        return value;
    }

    [[nodiscard]] auto operation_counts() const
        -> std::vector<std::size_t>
    {
        return {
            import_count,
            load_count,
            save_count,
            rename_count,
            delete_count,
        };
    }

    [[nodiscard]] auto expected_revisions() const
        -> std::pair<std::uint64_t, std::uint64_t>
    {
        return {
            saved_expected_revision,
            renamed_expected_revision,
        };
    }

    [[nodiscard]] auto renamed_name() const
        -> std::string_view
    {
        return renamed_to;
    }

    [[nodiscard]] auto mutation_state() const
        -> std::tuple<
            std::size_t,
            std::string_view,
            std::uint64_t,
            bool>
    {
        return {
            mutation_count,
            mutated_project_id,
            mutated_expected_revision,
            last_mutation &&
                std::holds_alternative<
                    ReplaceImageLayerMutation>(
                    *last_mutation),
        };
    }

    [[nodiscard]] auto stopped() const -> bool
    {
        return stopped_;
    }

    [[nodiscard]] auto recovery_counts() const
        -> std::size_t
    {
        return recovery_count;
    }

    auto inject_recovery_failure() -> void
    {
        fail_recovery = true;
    }

private:
    std::shared_ptr<const core::ImageProject> project_{};
    std::uint64_t current_revision_{7U};
    std::optional<ImageEditorSessionCompletion> completion_{};
    std::size_t import_count{};
    std::size_t load_count{};
    std::size_t save_count{};
    std::size_t rename_count{};
    std::size_t delete_count{};
    std::uint64_t saved_expected_revision{};
    std::uint64_t renamed_expected_revision{};
    std::string renamed_to{};
    std::size_t mutation_count{};
    std::string mutated_project_id{};
    std::uint64_t mutated_expected_revision{};
    std::optional<ImageEditorMutation> last_mutation{};
    std::shared_ptr<const std::vector<std::byte>>
        imported_bytes{};
    core::ApplicationConfig recovered_config{};
    ImageEditorStartupSnapshot startup_{};
    std::size_t recovery_count{};
    bool fail_recovery{};
    bool stopped_{};
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
            sampling_profile(body),
            image_profile(body),
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

class FakeEspRuntime final : public EspGameRuntimePort
{
public:
    auto capture_esp_frame()
        -> std::expected<
            CapturedEspFrame,
            RuntimeExecutionError> override
    {
        ++capture_count;
        return CapturedEspFrame{
            frame_identity,
            {},
            {1920.0, 1080.0},
            {core::EspTargetCapture{
                91U,
                92U,
                core::EspRole::Hider,
                core::EspRole::Unknown,
                "Player",
                core::EspWorldPoint{100.0, 0.0, 0.0},
            }},
        };
    }

    auto draw_esp_frame(
        const HudFrameIdentity& identity,
        const core::EspPrimitiveFrame& frame)
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++draw_count;
        drawn_identity = identity;
        line_count = frame.lines.size();
        text_count = frame.texts.size();
        return {};
    }

    HudFrameIdentity frame_identity{};
    HudFrameIdentity drawn_identity{};
    std::size_t capture_count{};
    std::size_t draw_count{};
    std::size_t line_count{};
    std::size_t text_count{};
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
    constexpr auto Frame =
        HudFrameIdentity{1U, 2U, 3U, 4U};
    auto esp = FakeEspRuntime{};
    esp.frame_identity = Frame;
    const auto fixture_plan =
        core::build_image_paint_plan_from_profile(
            core::ImagePaintProfilePlanRequest{
                sampling_profile(core::BodyProfile::Round),
                image_profile(core::BodyProfile::Round),
                project()->settings,
                project()->canonical_atlas,
            });
    if (!fixture_plan)
    {
        std::cerr << "fixture planner error: "
                  << static_cast<int>(fixture_plan.error())
                  << '\n';
    }
    else if (fixture_plan->paint.strokes.size() != 1U)
    {
        std::cerr << "fixture planner strokes: "
                  << fixture_plan->paint.strokes.size()
                  << ", samples: "
                  << fixture_plan->generated_samples
                  << '\n';
    }
    passed &= expect(
        fixture_plan &&
            fixture_plan->paint.strokes.size() == 1U,
        "the immutable Image Paint runtime fixture was invalid");
    auto root = ApplicationRoot{
        callbacks,
        executor,
        storage,
        paint,
        thread,
        preview,
        image,
        projects,
        esp,
        4U,
        2U,
        8U,
    };

    passed &= expect(
        root.initialize().has_value(),
        "the Image Paint composition root did not initialize");
    passed &= expect(
        projects.recovery_counts() == 1U &&
            root.snapshot()->image_editor.startup.attempted,
        "the composition root did not perform editor startup recovery");

    const auto route = [&callbacks, &root, &Frame](
                           ApplicationCommand command)
    {
        const auto enqueued =
            root.enqueue_command(std::move(command));
        callbacks.invoke(Frame);
        callbacks.invoke(Frame);
        return enqueued;
    };

    callbacks.invoke(Frame);
    passed &= expect(
        !root.snapshot()->ui_open &&
            root.snapshot()->esp.phase ==
                EspFramePhase::Active &&
            root.snapshot()->esp.line_count == 1U &&
            root.snapshot()->esp.text_count == 1U &&
            esp.capture_count == 1U &&
            esp.draw_count == 1U &&
            esp.drawn_identity == Frame &&
            esp.line_count == 1U &&
            esp.text_count == 1U,
        "ESP did not render while the control panel was closed");

    passed &= expect(
        route(ToggleUi{201U}) ==
                CommandEnqueueResult::Accepted &&
            root.snapshot()->ui_open &&
            esp.draw_count == 3U &&
            route(ToggleUi{202U}) ==
                CommandEnqueueResult::Accepted &&
            !root.snapshot()->ui_open &&
            esp.draw_count == 5U,
        "ESP rendering was coupled to the control panel state");

    const auto before_disable = esp.draw_count;
    passed &= expect(
        route(ToggleEsp{203U}) ==
                CommandEnqueueResult::Accepted &&
            !root.snapshot()->esp_enabled &&
            root.snapshot()->esp.phase ==
                EspFramePhase::Disabled &&
            esp.capture_count == before_disable &&
            esp.draw_count == before_disable &&
            route(ToggleEsp{204U}) ==
                CommandEnqueueResult::Accepted &&
            root.snapshot()->esp_enabled &&
            root.snapshot()->esp.phase ==
                EspFramePhase::Active &&
            esp.capture_count == before_disable + 2U &&
            esp.draw_count == before_disable + 2U,
        "the typed ESP toggle did not gate frame capture and draw");

    passed &= expect(
        route(ImportImageProject{
            250U,
            std::make_shared<const std::vector<std::byte>>(
                std::initializer_list<std::byte>{
                    std::byte{0x01},
                }),
        }) == CommandEnqueueResult::Accepted &&
            root.snapshot()->settings.active_image_project &&
            route(LoadImageProject{
            251U,
            std::string{ProjectId},
        }) == CommandEnqueueResult::Accepted &&
            root.snapshot()->settings.active_image_project &&
            route(SaveImageProject{
                252U,
                std::string{ProjectId},
                6U,
            }) == CommandEnqueueResult::Accepted &&
            route(RenameImageProject{
                253U,
                std::string{ProjectId},
                7U,
                "Renamed",
            }) == CommandEnqueueResult::Accepted &&
            route(DeleteImageProject{
                254U,
                std::string{ProjectId},
            }) == CommandEnqueueResult::Accepted &&
            !root.snapshot()->settings.active_image_project &&
            projects.operation_counts() ==
                std::vector<std::size_t>{
                    1U,
                    1U,
                    1U,
                    1U,
                    1U,
                } &&
            projects.expected_revisions() ==
                std::pair<std::uint64_t, std::uint64_t>{
                    6U,
                    7U,
                } &&
            projects.renamed_name() == "Renamed",
        "typed Image project commands did not route through the editor session");

    auto edited_layer =
        root.snapshot()
            ->image_editor.document->layers.front();
    edited_layer.center_x = 0.25;
    passed &= expect(
        route(MutateImageProject{
            255U,
            std::string{ProjectId},
            7U,
            ReplaceImageLayerMutation{
                0U,
                std::string{AssetId},
                edited_layer,
            },
        }) == CommandEnqueueResult::Accepted &&
            projects.mutation_state() ==
                std::tuple<
                    std::size_t,
                    std::string_view,
                    std::uint64_t,
                    bool>{
                    1U,
                    ProjectId,
                    7U,
                    true,
                },
        "the typed Image editor mutation did not route through the session");

    passed &= expect(
        root.enqueue_command(StartImagePaint{
            301U,
            std::string{ProjectId},
            7U,
        }) == CommandEnqueueResult::Accepted,
        "the composition root rejected a typed Image Paint command");

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
            completed->image_editor.pipeline.phase ==
                ImageEditorPipelinePhase::Ready &&
            completed->image_editor.pipeline.project_id ==
                ProjectId &&
            completed->image_editor.pipeline.project_revision ==
                7U &&
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
        root.finalize_shutdown().has_value() &&
            projects.stopped(),
        "Image Paint cancellation did not release final shutdown");

    auto failed_storage = FakeStorage{};
    auto failed_callbacks = FakeCallbacks{};
    auto failed_executor = RecordingExecutor{};
    auto failed_thread = FakeThreadContext{};
    auto failed_preview = FakePreviewRuntime{};
    auto failed_paint = UnusedPaintRuntime{};
    auto failed_projects = ReadyProject{};
    auto failed_image = FakeImageRuntime{};
    auto failed_esp = FakeEspRuntime{};
    failed_projects.inject_recovery_failure();
    auto failed_root = ApplicationRoot{
        failed_callbacks,
        failed_executor,
        failed_storage,
        failed_paint,
        failed_thread,
        failed_preview,
        failed_image,
        failed_projects,
        failed_esp,
        4U,
        2U,
        8U,
    };
    const auto failed_initialization =
        failed_root.initialize();
    passed &= expect(
        !failed_initialization &&
            failed_initialization.error() ==
                ApplicationRootError::ImageEditorRecovery &&
            failed_callbacks.callback == nullptr &&
            failed_root.snapshot()->runtime_phase ==
                ApplicationRuntimePhase::Incompatible,
        "editor recovery failure registered runtime callbacks or lost its root state");

    if (passed)
    {
        std::cout << "PASS application_root_image_paint\n";
    }
    return passed ? 0 : 1;
}
