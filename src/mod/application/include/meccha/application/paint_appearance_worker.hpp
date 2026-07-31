#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/application/paint_preview_controller.hpp>
#include <meccha/core/paint_appearance_capture.hpp>
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
    std::shared_ptr<const std::vector<core::Rgb8>>
        base_colors{};
    std::shared_ptr<const std::vector<core::Rgb8>>
        scene_colors{};
    std::vector<double> parameters{};
    double brush_size_texels{};
    std::uint32_t texture_dimension{};
    PaintTextureImage original{};
};

struct PaintAppearanceTargetE0PrepareWork
{
    core::PaintAppearanceCameraFingerprint source_camera{};
    std::shared_ptr<const std::vector<
        core::PaintAppearanceReadbackReference>>
        readback_references{};
    core::PaintAppearanceFeedbackEvidence feedback_evidence{};
    core::PaintAppearanceTargetE0Evidence target_e0_evidence{};
};

struct PaintAppearanceGeometryPrepareWork
{
    core::PaintSamplingProfile sampling_profile{};
    core::CanonicalImageProfile image_profile{};
    std::vector<core::PaintReferenceBoneTransform>
        current_world_transforms{};
    double brush_size_texels{};
    core::EspView view{};
    core::EspViewport viewport{};
};

struct PaintAppearanceCapturePrepareWork
{
    std::shared_ptr<
        const std::vector<core::PaintCaptureGeometrySample>>
        geometry{};
    core::PaintAppearanceCaptureEvidence evidence{};
    bool include_scene_lighting{};
    std::optional<core::AppearanceEmissionNoiseModel>
        target_e0_noise{};
};

struct PaintAppearanceEvaluateWork
{
    std::shared_ptr<const core::PaintAppearanceModel> model{};
    std::shared_ptr<const std::vector<core::AppearanceRgb>>
        target_hdr{};
    bool camera_stable{};
    bool readback_calibrated{};
    core::AppearanceReadbackTransform transform{
        core::AppearanceReadbackTransform::Identity};
};

struct PaintAppearanceResolveWork
{
    std::shared_ptr<const core::PaintAppearanceModel> model{};
    std::shared_ptr<const std::vector<core::Rgb8>>
        base_colors{};
    std::shared_ptr<const std::vector<core::Rgb8>>
        scene_colors{};
    std::vector<double> parameters{};
};

using PaintAppearanceWorkRequest = std::variant<
    PaintAppearancePrepareWork,
    PaintAppearanceGeometryPrepareWork,
    PaintAppearanceCapturePrepareWork,
    PaintAppearanceCandidateWork,
    PaintAppearanceTargetE0PrepareWork,
    PaintAppearanceResolveWork,
    PaintAppearanceEvaluateWork>;

struct PaintAppearancePrepared
{
    std::shared_ptr<const core::PaintAppearanceModel> model{};
    std::vector<double> parameters{};
};

struct PaintAppearanceGeometryPrepared
{
    std::shared_ptr<
        const std::vector<core::PaintCaptureGeometrySample>>
        geometry{};
    std::shared_ptr<const std::vector<
        core::PaintAppearanceSourceQuery>>
        source_queries{};
};

struct PaintAppearanceCandidate
{
    std::shared_ptr<
        const std::vector<core::ResolvedPaintAppearance>>
        appearances{};
    std::shared_ptr<const PaintTextureImage> preview{};
    std::shared_ptr<const std::vector<
        core::PaintAppearanceReadbackReference>>
        readback_references{};
    std::vector<double> parameters{};
};

struct PaintAppearanceTargetE0Prepared
{
    core::PaintAppearanceFeedback feedback{};
    core::PaintAppearanceTargetE0 target_e0{};
};

struct PaintAppearanceEvaluated
{
    core::PaintAppearanceEvaluation evaluation{};
};

struct PaintAppearanceResolved
{
    std::shared_ptr<
        const std::vector<core::ResolvedPaintAppearance>>
        appearances{};
    std::vector<double> parameters{};
};

using PaintAppearanceWorkValue = std::variant<
    PaintAppearancePrepared,
    PaintAppearanceGeometryPrepared,
    PaintAppearanceCandidate,
    PaintAppearanceTargetE0Prepared,
    PaintAppearanceResolved,
    PaintAppearanceEvaluated>;

enum class PaintAppearanceWorkFailureKind : std::uint8_t
{
    InvalidRequest,
    CaptureGeometry,
    CaptureEvidence,
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
    std::optional<core::PaintCaptureGeometryError>
        geometry_error{};
    std::optional<core::PaintAppearanceCaptureError>
        capture_error{};

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
