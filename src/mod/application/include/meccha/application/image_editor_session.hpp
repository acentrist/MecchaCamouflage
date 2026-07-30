#pragma once

#include <meccha/application/image_editor_pipeline.hpp>
#include <meccha/application/image_project_io_worker.hpp>
#include <meccha/application/image_project_persistence.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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
    std::optional<CommandId> persistence_command{};
    std::optional<ImageProjectIoOperation> persistence_operation{};
    bool completion_pending{};
    ActiveDraftPersistenceSnapshot active_draft{};
    std::optional<ActiveDraftScheduleError> draft_schedule_error{};
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

    [[nodiscard]] virtual auto load(
        CommandId command_id,
        std::string project_id,
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

    [[nodiscard]] auto submit_edit(core::ImageProject project)
        -> std::expected<JobGeneration, ImageEditorSubmitError>;

    [[nodiscard]] auto load(
        CommandId command_id,
        std::string project_id,
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

    ImageEditorPipeline pipeline_;
    ImageProjectIoWorker persistence_worker_;
    ActiveDraftPersistenceWorker draft_worker_;
    std::optional<ActivePersistenceOperation> active_operation_{};
    std::optional<ImageEditorSessionCompletion> completion_{};
    JobGeneration scheduled_draft_generation_{};
    std::optional<ActiveDraftScheduleError>
        draft_schedule_error_{};
    bool stopped_{};
};
} // namespace meccha::application
