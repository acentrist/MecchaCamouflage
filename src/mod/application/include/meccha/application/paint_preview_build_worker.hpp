#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/application/paint_planning_worker.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/core/paint_preview.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace meccha::application
{
struct PaintPreviewBuildRequest
{
    PaintPlanningRequest planning{};
    PaintTextureImage original{};
};

enum class PaintPreviewBuildFailureKind : std::uint8_t
{
    Capture,
    Planner,
    Composer,
    WorkerException,
};

struct PaintPreviewBuildFailure
{
    PaintPreviewBuildFailureKind kind{
        PaintPreviewBuildFailureKind::Planner};
    std::optional<core::PaintPlanError> planner_error{};
    std::optional<core::PaintPreviewComposeError> compose_error{};
    std::optional<core::PaintCaptureRequestError> capture_error{};

    auto operator==(const PaintPreviewBuildFailure&) const -> bool =
        default;
};

using PaintPreviewBuildResult = std::expected<
    std::shared_ptr<const PaintTextureImage>,
    PaintPreviewBuildFailure>;

struct PaintPreviewBuildCompletion
{
    JobGeneration generation{};
    PaintPreviewBuildResult result;
};

enum class PaintPreviewBuildStartError : std::uint8_t
{
    InvalidGeneration,
    Busy,
    Stopped,
    ThreadStart,
};

enum class PaintPreviewBuildCancelResult : std::uint8_t
{
    Requested,
    AlreadyCompleted,
    Idle,
    StaleGeneration,
};

class PaintPreviewBuildWorker
{
public:
    explicit PaintPreviewBuildWorker(PaintPlanBuilder& builder);
    PaintPreviewBuildWorker(const PaintPreviewBuildWorker&) = delete;
    auto operator=(const PaintPreviewBuildWorker&)
        -> PaintPreviewBuildWorker& = delete;
    ~PaintPreviewBuildWorker();

    [[nodiscard]] auto start(
        JobGeneration generation,
        PaintPreviewBuildRequest request)
        -> std::expected<void, PaintPreviewBuildStartError>;

    [[nodiscard]] auto request_cancel(JobGeneration generation)
        -> PaintPreviewBuildCancelResult;

    [[nodiscard]] auto poll()
        -> std::optional<PaintPreviewBuildCompletion>;

    auto shutdown() noexcept -> void;

private:
    enum class State : std::uint8_t
    {
        Idle,
        Running,
        Completed,
    };

    auto run(
        JobGeneration generation,
        PaintPreviewBuildRequest request,
        std::stop_token cancellation) noexcept -> void;

    PaintPlanBuilder& builder_;
    std::mutex mutex_{};
    std::jthread worker_{};
    std::optional<PaintPreviewBuildCompletion> completion_{};
    JobGeneration active_generation_{};
    State state_{State::Idle};
    bool stopped_{};
};
} // namespace meccha::application
