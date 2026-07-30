#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/core/image_paint_plan.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace meccha::application
{
struct ImagePaintPlanningRequest
{
    std::string project_id{};
    std::uint64_t project_revision{};
    core::ImagePaintPlanRequest plan{};
};

class ImagePaintPlanBuilder
{
public:
    ImagePaintPlanBuilder() = default;
    ImagePaintPlanBuilder(const ImagePaintPlanBuilder&) = delete;
    auto operator=(const ImagePaintPlanBuilder&)
        -> ImagePaintPlanBuilder& = delete;
    virtual ~ImagePaintPlanBuilder() = default;

    [[nodiscard]] virtual auto build(
        const core::ImagePaintPlanRequest& request,
        std::stop_token cancellation)
        -> std::expected<
            core::ImagePaintPlan,
            core::ImagePaintPlanError> = 0;
};

class CoreImagePaintPlanBuilder final
    : public ImagePaintPlanBuilder
{
public:
    [[nodiscard]] auto build(
        const core::ImagePaintPlanRequest& request,
        std::stop_token cancellation)
        -> std::expected<
            core::ImagePaintPlan,
            core::ImagePaintPlanError> override;
};

enum class ImagePaintPlanningFailureKind : std::uint8_t
{
    Planner,
    WorkerException,
};

struct ImagePaintPlanningFailure
{
    ImagePaintPlanningFailureKind kind{
        ImagePaintPlanningFailureKind::Planner};
    std::optional<core::ImagePaintPlanError> planner_error{};

    auto operator==(const ImagePaintPlanningFailure&) const -> bool =
        default;
};

using ImagePaintPlanningResult = std::expected<
    std::shared_ptr<const core::ImagePaintPlan>,
    ImagePaintPlanningFailure>;

struct ImagePaintPlanningCompletion
{
    JobGeneration generation{};
    std::string project_id{};
    std::uint64_t project_revision{};
    ImagePaintPlanningResult result;
};

enum class ImagePaintPlanningStartError : std::uint8_t
{
    InvalidGeneration,
    InvalidProjectIdentity,
    InvalidProjectRevision,
    Busy,
    Stopped,
    ThreadStart,
};

enum class ImagePaintPlanningCancelResult : std::uint8_t
{
    Requested,
    AlreadyCompleted,
    Idle,
    StaleGeneration,
};

class ImagePaintPlanningWorker
{
public:
    explicit ImagePaintPlanningWorker(ImagePaintPlanBuilder& builder);
    ImagePaintPlanningWorker(
        const ImagePaintPlanningWorker&) = delete;
    auto operator=(const ImagePaintPlanningWorker&)
        -> ImagePaintPlanningWorker& = delete;
    ~ImagePaintPlanningWorker();

    [[nodiscard]] auto start(
        JobGeneration generation,
        ImagePaintPlanningRequest request)
        -> std::expected<void, ImagePaintPlanningStartError>;

    [[nodiscard]] auto request_cancel(JobGeneration generation)
        -> ImagePaintPlanningCancelResult;

    [[nodiscard]] auto poll()
        -> std::optional<ImagePaintPlanningCompletion>;

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
        ImagePaintPlanningRequest request,
        std::stop_token cancellation) noexcept -> void;

    ImagePaintPlanBuilder& builder_;
    std::mutex mutex_{};
    std::jthread worker_{};
    std::optional<ImagePaintPlanningCompletion> completion_{};
    JobGeneration active_generation_{};
    State state_{State::Idle};
    bool stopped_{};
};
} // namespace meccha::application
