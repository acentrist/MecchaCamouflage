#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/core/paint_appearance_capture.hpp>
#include <meccha/core/paint_preview.hpp>
#include <meccha/core/paint_projective_pipeline.hpp>

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
struct PaintAppearanceGeometryPrepareWork
{
    core::PaintSamplingProfile sampling_profile{};
    core::CanonicalImageProfile image_profile{};
    std::vector<core::PaintReferenceBoneTransform>
        current_world_transforms{};
    double replay_brush_size_texels{};
    core::EspView view{};
    core::EspViewport viewport{};
};

struct PaintAppearanceCapturePrepareWork
{
    std::shared_ptr<const std::vector<
        core::PaintCaptureGeometrySample>> geometry{};
    core::PaintAppearanceCaptureEvidence evidence{};
    core::PaintSettings settings{};
};

struct PaintAppearanceCandidateWork
{
    std::shared_ptr<const core::PaintProjectiveModel> model{};
    std::shared_ptr<const core::PaintProjectiveRaster> raster{};
    PaintTextureImage original{};
};

struct PaintAppearanceBaselineCalibrateWork
{
    std::shared_ptr<const core::PaintProjectiveModel> model{};
    core::PaintAppearanceCameraFingerprint source_camera{};
    std::shared_ptr<const std::vector<
        core::PaintAppearanceReadbackReference>> readback_references{};
    core::PaintAppearanceFeedbackEvidence feedback_evidence{};
    core::PaintAppearanceTargetE0Evidence target_e0_evidence{};
    core::PaintSettings settings{};
    bool packed_b_verified{};
};

struct PaintAppearanceFinalizeWork
{
    std::shared_ptr<const core::PaintProjectiveModel> model{};
    std::shared_ptr<const core::PaintProjectiveCalibration>
        calibration{};
    core::PaintProjectiveFeedback baseline{};
    core::PaintAppearanceCameraFingerprint source_camera{};
    core::PaintAppearanceCapturedPass<core::AppearanceRgb>
        endpoint_final_hdr{};
    core::AppearanceEmissionNoiseModel target_e0_noise{};
    core::PaintSettings settings{};
    bool packed_b_verified{};
};

using PaintAppearanceWorkRequest = std::variant<
    PaintAppearanceGeometryPrepareWork,
    PaintAppearanceCapturePrepareWork,
    PaintAppearanceCandidateWork,
    PaintAppearanceBaselineCalibrateWork,
    PaintAppearanceFinalizeWork>;

struct PaintAppearanceGeometryPrepared
{
    std::shared_ptr<const std::vector<
        core::PaintCaptureGeometrySample>> geometry{};
    std::shared_ptr<const std::vector<
        core::PaintAppearanceSourceQuery>> source_queries{};
    std::size_t replay_samples{};
    std::size_t calibration_samples{};
};

struct PaintAppearancePrepared
{
    std::shared_ptr<const core::PaintProjectiveModel> model{};
    std::shared_ptr<const core::PaintProjectiveRaster> baseline{};
};

struct PaintAppearanceCandidate
{
    std::shared_ptr<const PaintTextureImage> preview{};
    std::shared_ptr<const std::vector<
        core::PaintAppearanceReadbackReference>> readback_references{};
};

struct PaintAppearanceBaselineCalibrated
{
    core::PaintProjectiveFeedback feedback{};
    core::PaintAppearanceTargetE0 target_e0{};
    std::shared_ptr<const core::PaintProjectiveCalibration>
        calibration{};
};

struct PaintAppearanceResolved
{
    std::shared_ptr<const std::vector<
        core::ResolvedPaintAppearance>> appearances{};
    std::shared_ptr<const std::vector<bool>> available{};
    int local_albedo_acceptances{};
    int physical_emission_components{};
    int physical_emission_samples{};
};

using PaintAppearanceWorkValue = std::variant<
    PaintAppearanceGeometryPrepared,
    PaintAppearancePrepared,
    PaintAppearanceCandidate,
    PaintAppearanceBaselineCalibrated,
    PaintAppearanceResolved>;

enum class PaintAppearanceWorkFailureKind : std::uint8_t
{
    InvalidRequest,
    CaptureGeometry,
    CaptureEvidence,
    Projective,
    Composer,
    WorkerException,
};

struct PaintAppearanceWorkFailure
{
    PaintAppearanceWorkFailureKind kind{
        PaintAppearanceWorkFailureKind::InvalidRequest};
    std::optional<core::PaintProjectiveError> projective_error{};
    std::optional<core::PaintPreviewComposeError> compose_error{};
    std::optional<core::PaintCaptureGeometryError> geometry_error{};
    std::optional<core::PaintAppearanceCaptureError> capture_error{};

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
        -> std::expected<void, PaintAppearanceWorkStartError>;

    [[nodiscard]] auto request_cancel(JobGeneration generation)
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
