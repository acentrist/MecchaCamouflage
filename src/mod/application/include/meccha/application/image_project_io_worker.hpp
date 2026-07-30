#pragma once

#include <meccha/application/image_project_persistence.hpp>
#include <meccha/application/job_state.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace meccha::application
{
struct ImageProjectLoadRequest
{
    CommandId command_id{};
    std::string project_id{};
    core::ApplicationConfig current_config{};
};

struct ImageProjectSaveRequest
{
    CommandId command_id{};
    std::shared_ptr<const core::ImageProject> project{};
    std::uint64_t expected_revision{};
    core::ApplicationConfig current_config{};
};

struct ImageProjectRenameRequest
{
    CommandId command_id{};
    std::string project_id{};
    std::string new_name{};
};

struct ImageProjectDeleteRequest
{
    CommandId command_id{};
    std::string project_id{};
    core::ApplicationConfig current_config{};
};

using ImageProjectIoRequest = std::variant<
    ImageProjectLoadRequest,
    ImageProjectSaveRequest,
    ImageProjectRenameRequest,
    ImageProjectDeleteRequest>;

enum class ImageProjectIoOperation : std::uint8_t
{
    Load,
    Save,
    Rename,
    Delete,
};

struct SavedImageProject
{
    std::shared_ptr<const core::ImageProject> project{};
    core::ApplicationConfig config{};
};

using ImageProjectIoValue = std::variant<
    ActivatedImageProject,
    SavedImageProject,
    core::ImageProject,
    DeletedImageProject>;

enum class ImageProjectIoFailureKind : std::uint8_t
{
    Persistence,
    ProjectStore,
    WorkerException,
};

struct ImageProjectIoFailure
{
    ImageProjectIoFailureKind kind{
        ImageProjectIoFailureKind::Persistence};
    std::optional<ImageProjectPersistenceError> persistence{};
    std::optional<ImageProjectStoreError> project_store{};
};

using ImageProjectIoResult = std::expected<
    ImageProjectIoValue,
    ImageProjectIoFailure>;

struct ImageProjectIoCompletion
{
    CommandId command_id{};
    ImageProjectIoOperation operation{
        ImageProjectIoOperation::Load};
    ImageProjectIoResult result;
};

enum class ImageProjectIoStartError : std::uint8_t
{
    InvalidCommand,
    InvalidRequest,
    Busy,
    Stopped,
    ThreadStart,
};

class ImageProjectIoWorker
{
public:
    ImageProjectIoWorker(
        ImageProjectStore& projects,
        ImageProjectPersistenceCoordinator& persistence);
    ImageProjectIoWorker(const ImageProjectIoWorker&) = delete;
    auto operator=(const ImageProjectIoWorker&)
        -> ImageProjectIoWorker& = delete;
    ~ImageProjectIoWorker();

    [[nodiscard]] auto start(ImageProjectIoRequest request)
        -> std::expected<void, ImageProjectIoStartError>;

    [[nodiscard]] auto poll()
        -> std::optional<ImageProjectIoCompletion>;

    auto shutdown() noexcept -> void;

private:
    enum class State : std::uint8_t
    {
        Idle,
        Running,
        Completed,
    };

    auto run(ImageProjectIoRequest request) noexcept -> void;

    ImageProjectStore& projects_;
    ImageProjectPersistenceCoordinator& persistence_;
    std::mutex mutex_{};
    std::jthread worker_{};
    std::optional<ImageProjectIoCompletion> completion_{};
    State state_{State::Idle};
    bool stopped_{};
};
} // namespace meccha::application
