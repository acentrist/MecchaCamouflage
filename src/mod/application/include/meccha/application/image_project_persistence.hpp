#pragma once

#include <meccha/application/config_store.hpp>
#include <meccha/application/image_project_store.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace meccha::application
{
enum class RecoveredImageProjectSource : std::uint8_t
{
    Blank,
    NamedProject,
    ActiveDraft,
};

enum class ImageProjectRecoveryDiagnosticCode : std::uint8_t
{
    MissingNamedProject,
    InvalidNamedProject,
    MissingActiveDraft,
    InvalidActiveDraft,
    ActiveDraftIdentityMismatch,
};

struct ImageProjectRecoveryDiagnostic
{
    ImageProjectRecoveryDiagnosticCode code{};
    std::string detail{};

    auto operator==(const ImageProjectRecoveryDiagnostic&) const
        -> bool = default;
};

struct ImageProjectRecoveryResult
{
    std::optional<core::ImageProject> project{};
    RecoveredImageProjectSource source{
        RecoveredImageProjectSource::Blank};
    std::vector<ImageProjectRecoveryDiagnostic> diagnostics{};
};

enum class ImageProjectPersistenceErrorCode : std::uint8_t
{
    Project,
    Configuration,
};

struct ImageProjectPersistenceError
{
    ImageProjectPersistenceErrorCode code{};
    std::optional<ImageProjectStoreError> project{};
    std::optional<ConfigStoreError> configuration{};
    std::string detail{};

    auto operator==(const ImageProjectPersistenceError&) const
        -> bool = default;
};

struct ActivatedImageProject
{
    core::ImageProject project{};
    core::ApplicationConfig config{};
};

struct DeletedImageProject
{
    bool deleted{};
    core::ApplicationConfig config{};
};

class ImageProjectPersistenceCoordinator
{
public:
    ImageProjectPersistenceCoordinator(
        ImageProjectStore& projects,
        ConfigStore& configuration);

    [[nodiscard]] auto recover(
        const core::ApplicationConfig& config)
        -> ImageProjectRecoveryResult;

    [[nodiscard]] auto save_named_and_activate(
        const core::ImageProject& project,
        std::uint64_t expected_revision,
        const core::ApplicationConfig& current_config)
        -> std::expected<
            core::ApplicationConfig,
            ImageProjectPersistenceError>;

    [[nodiscard]] auto load_named_and_activate(
        std::string_view project_id,
        const core::ApplicationConfig& current_config)
        -> std::expected<
            ActivatedImageProject,
            ImageProjectPersistenceError>;

    [[nodiscard]] auto import_named_and_activate(
        std::span<const std::byte> bytes,
        const core::ApplicationConfig& current_config)
        -> std::expected<
            ActivatedImageProject,
            ImageProjectPersistenceError>;

    [[nodiscard]] auto delete_named_and_deactivate(
        std::string_view project_id,
        const core::ApplicationConfig& current_config)
        -> std::expected<
            DeletedImageProject,
            ImageProjectPersistenceError>;

    [[nodiscard]] auto save_active_draft_and_activate(
        const core::ImageProject& project,
        const core::ApplicationConfig& current_config)
        -> std::expected<
            core::ApplicationConfig,
            ImageProjectPersistenceError>;

private:
    ImageProjectStore& projects_;
    ConfigStore& configuration_;
};

enum class ActiveDraftScheduleError : std::uint8_t
{
    InvalidProject,
    Stopped,
    GenerationOverflow,
};

struct ActiveDraftPersistenceSnapshot
{
    std::uint64_t scheduled_generation{};
    std::uint64_t completed_generation{};
    bool pending{};
    bool in_flight{};
    bool stopped{};
    std::optional<ImageProjectStoreError> last_error{};

    auto operator==(const ActiveDraftPersistenceSnapshot&) const
        -> bool = default;
};

class ActiveDraftPersistenceWorker
{
public:
    ActiveDraftPersistenceWorker(
        ImageProjectStore& projects,
        std::chrono::milliseconds debounce_interval);
    ActiveDraftPersistenceWorker(
        const ActiveDraftPersistenceWorker&) = delete;
    auto operator=(const ActiveDraftPersistenceWorker&)
        -> ActiveDraftPersistenceWorker& = delete;
    ~ActiveDraftPersistenceWorker();

    [[nodiscard]] auto schedule(
        std::shared_ptr<const core::ImageProject> project)
        -> std::expected<std::uint64_t, ActiveDraftScheduleError>;

    [[nodiscard]] auto wait_until_idle(
        std::chrono::milliseconds timeout) -> bool;

    auto shutdown(bool flush_pending) noexcept -> void;

    [[nodiscard]] auto snapshot() const
        -> ActiveDraftPersistenceSnapshot;

private:
    auto run() noexcept -> void;

    ImageProjectStore& projects_;
    std::chrono::milliseconds debounce_interval_;
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    std::shared_ptr<const core::ImageProject> pending_project_{};
    std::chrono::steady_clock::time_point deadline_{};
    std::uint64_t scheduled_generation_{};
    std::uint64_t pending_generation_{};
    std::uint64_t completed_generation_{};
    bool in_flight_{};
    bool stopping_{};
    bool flush_pending_{};
    bool stopped_{};
    std::optional<ImageProjectStoreError> last_error_{};
    std::thread worker_;
};
} // namespace meccha::application
