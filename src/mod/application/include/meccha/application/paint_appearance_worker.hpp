#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/core/paint_appearance_fit.hpp>
#include <meccha/core/paint_preview.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

namespace meccha::application
{
struct PaintAppearancePrepareWork
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<core::PaintAppearanceObservation>
        observations{};
    bool include_scene_lighting{};
    std::optional<core::AppearanceEmissionNoiseModel>
        target_e0_noise{};
};

struct PaintAppearanceCandidateWork
{
    std::shared_ptr<const core::PaintAppearanceModel> model{};
    std::vector<core::Rgb8> base_colors{};
    std::vector<core::Rgb8> scene_colors{};
    std::vector<double> parameters{};
    double brush_size_texels{};
    std::uint32_t texture_dimension{};
    PaintTextureImage original{};
};

struct PaintAppearanceEvaluateWork
{
    std::shared_ptr<const core::PaintAppearanceModel> model{};
    std::vector<core::AppearanceRgb> target_hdr{};
    bool camera_stable{};
    bool readback_calibrated{};
    core::AppearanceReadbackTransform transform{
        core::AppearanceReadbackTransform::Identity};
};

using PaintAppearanceWorkRequest = std::variant<
    PaintAppearancePrepareWork,
    PaintAppearanceCandidateWork,
    PaintAppearanceEvaluateWork>;

struct PaintAppearancePrepared
{
    std::shared_ptr<const core::PaintAppearanceModel> model{};
    std::vector<double> parameters{};
};

struct PaintAppearanceCandidate
{
    std::shared_ptr<
        const std::vector<core::ResolvedPaintAppearance>>
        appearances{};
    std::shared_ptr<const PaintTextureImage> preview{};
    std::vector<double> parameters{};
};

struct PaintAppearanceEvaluated
{
    core::PaintAppearanceEvaluation evaluation{};
};

using PaintAppearanceWorkValue = std::variant<
    PaintAppearancePrepared,
    PaintAppearanceCandidate,
    PaintAppearanceEvaluated>;

enum class PaintAppearanceWorkFailureKind : std::uint8_t
{
    InvalidRequest,
    Core,
    Composer,
    WorkerException,
};

struct PaintAppearanceWorkFailure
{
    PaintAppearanceWorkFailureKind kind{
        PaintAppearanceWorkFailureKind::InvalidRequest};
    std::optional<core::PaintAppearanceFitError> core_error{};
    std::optional<core::PaintPreviewComposeError>
        compose_error{};

    auto operator==(const PaintAppearanceWorkFailure&) const
        -> bool = default;
};

using PaintAppearanceWorkResult = std::expected<
    PaintAppearanceWorkValue,
    PaintAppearanceWorkFailure>;

struct PaintAppearanceWorkCompletion
{
    JobGeneration generation{};
    PaintAppearanceWorkResult result;
};

enum class PaintAppearanceWorkStartError : std::uint8_t
{
    InvalidGeneration,
    Busy,
    Stopped,
    ThreadStart,
};

enum class PaintAppearanceWorkCancelResult : std::uint8_t
{
    Requested,
    AlreadyCompleted,
    Idle,
    StaleGeneration,
};

class PaintAppearanceWorker
{
public:
    PaintAppearanceWorker() = default;
    PaintAppearanceWorker(const PaintAppearanceWorker&) = delete;
    auto operator=(const PaintAppearanceWorker&)
        -> PaintAppearanceWorker& = delete;
    ~PaintAppearanceWorker();

    [[nodiscard]] auto start(
        JobGeneration generation,
        PaintAppearanceWorkRequest request)
        -> std::expected<
            void,
            PaintAppearanceWorkStartError>;

    [[nodiscard]] auto request_cancel(
        JobGeneration generation)
        -> PaintAppearanceWorkCancelResult;

    [[nodiscard]] auto poll()
        -> std::optional<PaintAppearanceWorkCompletion>;

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
        PaintAppearanceWorkRequest request,
        std::stop_token cancellation) noexcept -> void;

    std::mutex mutex_{};
    std::jthread worker_{};
    std::optional<PaintAppearanceWorkCompletion> completion_{};
    JobGeneration active_generation_{};
    State state_{State::Idle};
    bool stopped_{};
};
} // namespace meccha::application
