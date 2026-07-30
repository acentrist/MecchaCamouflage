#pragma once

#include <meccha/application/image_editor_contract.hpp>
#include <meccha/application/image_editor_pipeline.hpp>
#include <meccha/application/image_project_io_worker.hpp>
#include <meccha/application/image_project_persistence.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace meccha::application
{
enum class ImageEditorSessionStartError : std::uint8_t
{
    InvalidCommand,
    InvalidProject,
    NotReady,
    Busy,
    Stopped,
    PersistenceStart,
};

enum class ImageEditorSessionFailureKind : std::uint8_t
{
    Persistence,
    Pipeline,
    InvalidCompletion,
};

struct ImageEditorSessionFailure
{
    ImageEditorSessionFailureKind kind{
        ImageEditorSessionFailureKind::Persistence};
    std::optional<ImageProjectIoFailure> persistence{};
    std::optional<ImageEditorSubmitError> pipeline{};
    std::optional<core::ApplicationConfig> published_config{};

    auto operator==(const ImageEditorSessionFailure&) const
        -> bool = default;
};

struct ImageEditorSessionSuccess
{
    std::string project_id{};
    std::uint64_t project_revision{};
    std::optional<JobGeneration> pipeline_generation{};
    std::optional<core::ApplicationConfig> config{};
    bool deleted{};

    auto operator==(const ImageEditorSessionSuccess&) const
        -> bool = default;
};

using ImageEditorSessionResult = std::expected<
    ImageEditorSessionSuccess,
    ImageEditorSessionFailure>;

enum class ImageEditorStartupError : std::uint8_t
{
    AlreadyAttempted,
    Busy,
    Stopped,
    PersistenceException,
    Pipeline,
};

struct ImageEditorStartupSnapshot
{
    bool attempted{};
    RecoveredImageProjectSource source{
        RecoveredImageProjectSource::Blank};
    std::optional<JobGeneration> pipeline_generation{};
    std::vector<ImageProjectRecoveryDiagnostic> diagnostics{};
    std::optional<ImageEditorStartupError> failure{};
    std::optional<ImageEditorSubmitError> pipeline_error{};

    auto operator==(const ImageEditorStartupSnapshot&) const
        -> bool = default;
};

struct ImageEditorSessionCompletion
{
    CommandId command_id{};
    ImageProjectIoOperation operation{
        ImageProjectIoOperation::Load};
    ImageEditorSessionResult result;
};

struct ImageEditorSessionSnapshot
{
    ImageEditorPipelineSnapshot pipeline{};
    ImageEditorStartupSnapshot startup{};
    std::optional<CommandId> persistence_command{};
    std::optional<ImageProjectIoOperation> persistence_operation{};
    bool completion_pending{};
    ActiveDraftPersistenceSnapshot active_draft{};
    std::optional<ActiveDraftScheduleError> draft_schedule_error{};
    std::optional<ImageEditorDocumentSnapshot> document{};
    bool stopped{};

    auto operator==(const ImageEditorSessionSnapshot&) const
        -> bool = default;
};

class ImageEditorSessionPort : public ImageProjectReadinessPort
{
public:
    ImageEditorSessionPort() = default;
    ImageEditorSessionPort(const ImageEditorSessionPort&) = delete;
    auto operator=(const ImageEditorSessionPort&)
        -> ImageEditorSessionPort& = delete;
    ~ImageEditorSessionPort() override = default;

    [[nodiscard]] virtual auto recover_startup(
        const core::ApplicationConfig& config)
        -> std::expected<
            ImageEditorStartupSnapshot,
            ImageEditorStartupError> = 0;

    [[nodiscard]] virtual auto load(
        CommandId command_id,
        std::string project_id,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> = 0;

    [[nodiscard]] virtual auto import_project(
        CommandId command_id,
        std::shared_ptr<const std::vector<std::byte>> bytes,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> = 0;

    [[nodiscard]] virtual auto save(
        CommandId command_id,
        std::string_view project_id,
        std::uint64_t expected_revision,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> = 0;

    [[nodiscard]] virtual auto rename(
        CommandId command_id,
        std::string_view project_id,
        std::uint64_t expected_revision,
        std::string new_name)
        -> std::expected<void, ImageEditorSessionStartError> = 0;

    [[nodiscard]] virtual auto remove(
        CommandId command_id,
        std::string project_id,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> = 0;

    [[nodiscard]] virtual auto mutate(
        std::string_view project_id,
        std::uint64_t expected_revision,
        ImageEditorMutation mutation)
        -> std::expected<
            JobGeneration,
            ImageEditorMutationError> = 0;

    virtual auto update() -> void = 0;

    [[nodiscard]] virtual auto poll_completion()
        -> std::optional<ImageEditorSessionCompletion> = 0;

    virtual auto shutdown(bool flush_active_draft) noexcept
        -> void = 0;

    [[nodiscard]] virtual auto session_snapshot() const
        -> ImageEditorSessionSnapshot = 0;
};

class ImageEditorSession final : public ImageEditorSessionPort
{
public:
    ImageEditorSession(
        ImageSourceDecoder& decoder,
        ImageAtlasComposer& composer,
        ImageProjectStore& projects,
        ImageProjectPersistenceCoordinator& persistence,
        std::chrono::milliseconds draft_debounce);
    ImageEditorSession(const ImageEditorSession&) = delete;
    auto operator=(const ImageEditorSession&)
        -> ImageEditorSession& = delete;
    ~ImageEditorSession();

    [[nodiscard]] auto recover_startup(
        const core::ApplicationConfig& config)
        -> std::expected<
            ImageEditorStartupSnapshot,
            ImageEditorStartupError> override;

    [[nodiscard]] auto submit_edit(core::ImageProject project)
        -> std::expected<JobGeneration, ImageEditorSubmitError>;

    [[nodiscard]] auto mutate(
        std::string_view project_id,
        std::uint64_t expected_revision,
        ImageEditorMutation mutation)
        -> std::expected<
            JobGeneration,
            ImageEditorMutationError> override;

    [[nodiscard]] auto load(
        CommandId command_id,
        std::string project_id,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> override;

    [[nodiscard]] auto import_project(
        CommandId command_id,
        std::shared_ptr<const std::vector<std::byte>> bytes,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> override;

    [[nodiscard]] auto save(
        CommandId command_id,
        std::string_view project_id,
        std::uint64_t expected_revision,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> override;

    [[nodiscard]] auto rename(
        CommandId command_id,
        std::string_view project_id,
        std::uint64_t expected_revision,
        std::string new_name)
        -> std::expected<void, ImageEditorSessionStartError> override;

    [[nodiscard]] auto remove(
        CommandId command_id,
        std::string project_id,
        core::ApplicationConfig current_config)
        -> std::expected<void, ImageEditorSessionStartError> override;

    auto update() -> void override;

    [[nodiscard]] auto poll_completion()
        -> std::optional<ImageEditorSessionCompletion> override;

    auto shutdown(bool flush_active_draft) noexcept
        -> void override;

    [[nodiscard]] auto session_snapshot() const
        -> ImageEditorSessionSnapshot override;

    [[nodiscard]] auto snapshot() const
        -> ImageEditorPipelineSnapshot override;

    [[nodiscard]] auto ready_project(
        std::string_view project_id,
        std::uint64_t project_revision) const
        -> std::shared_ptr<const core::ImageProject> override;

private:
    struct ActivePersistenceOperation
    {
        CommandId command_id{};
        ImageProjectIoOperation operation{
            ImageProjectIoOperation::Load};
        std::string project_id{};
        bool waiting_for_draft{};
        std::optional<ImageProjectIoRequest> request{};
    };

    [[nodiscard]] auto admit(
        ActivePersistenceOperation operation)
        -> std::expected<void, ImageEditorSessionStartError>;
    auto start_waiting_operation() -> void;
    auto observe_ready_project() -> void;
    auto complete(ImageProjectIoCompletion completion) -> void;
    auto fail_completion(
        CommandId command_id,
        ImageProjectIoOperation operation,
        ImageEditorSessionFailure failure) -> void;
    [[nodiscard]] auto admit_project(
        core::ImageProject project,
        bool replace)
        -> std::expected<JobGeneration, ImageEditorSubmitError>;

    ImageEditorPipeline pipeline_;
    ImageProjectPersistenceCoordinator& persistence_;
    ImageProjectIoWorker persistence_worker_;
    ActiveDraftPersistenceWorker draft_worker_;
    std::optional<ActivePersistenceOperation> active_operation_{};
    std::optional<ImageEditorSessionCompletion> completion_{};
    ImageEditorStartupSnapshot startup_{};
    JobGeneration scheduled_draft_generation_{};
    std::optional<ActiveDraftScheduleError>
        draft_schedule_error_{};
    std::shared_ptr<const core::ImageProject> current_project_{};
    bool stopped_{};
};
} // namespace meccha::application
