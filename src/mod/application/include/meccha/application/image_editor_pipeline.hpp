#pragma once

#include <meccha/application/image_composition_worker.hpp>
#include <meccha/application/image_decode_worker.hpp>
#include <meccha/application/job_state.hpp>
#include <meccha/core/image_project.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace meccha::application
{
enum class ImageEditorPipelinePhase : std::uint8_t
{
    Empty,
    Decoding,
    Composing,
    Ready,
    Failed,
    Stopped,
};

enum class ImageEditorPipelineFailureKind : std::uint8_t
{
    Decode,
    Composition,
    InvalidCompletion,
};

struct ImageEditorPipelineFailure
{
    ImageEditorPipelineFailureKind kind{
        ImageEditorPipelineFailureKind::Decode};
    std::optional<ImageProjectDecodeFailure> decode{};
    std::optional<ImageCompositionFailure> composition{};

    auto operator==(const ImageEditorPipelineFailure&) const
        -> bool = default;
};

struct ImageEditorPipelineSnapshot
{
    ImageEditorPipelinePhase phase{
        ImageEditorPipelinePhase::Empty};
    JobGeneration generation{};
    std::string project_id{};
    std::uint64_t project_revision{};
    bool pending{};
    std::optional<ImageEditorPipelineFailure> failure{};

    auto operator==(const ImageEditorPipelineSnapshot&) const
        -> bool = default;
};

enum class ImageEditorSubmitError : std::uint8_t
{
    InvalidProject,
    StaleRevision,
    Stopped,
    GenerationOverflow,
    DecodeStart,
};

class ImageProjectReadinessPort
{
public:
    ImageProjectReadinessPort() = default;
    ImageProjectReadinessPort(
        const ImageProjectReadinessPort&) = delete;
    auto operator=(const ImageProjectReadinessPort&)
        -> ImageProjectReadinessPort& = delete;
    virtual ~ImageProjectReadinessPort() = default;

    [[nodiscard]] virtual auto snapshot() const
        -> ImageEditorPipelineSnapshot = 0;

    [[nodiscard]] virtual auto ready_project(
        std::string_view project_id,
        std::uint64_t project_revision) const
        -> std::shared_ptr<const core::ImageProject> = 0;
};

class ImageEditorPipeline final
    : public ImageProjectReadinessPort
{
public:
    ImageEditorPipeline(
        ImageSourceDecoder& decoder,
        ImageAtlasComposer& composer);
    ImageEditorPipeline(const ImageEditorPipeline&) = delete;
    auto operator=(const ImageEditorPipeline&)
        -> ImageEditorPipeline& = delete;
    ~ImageEditorPipeline();

    [[nodiscard]] auto submit(core::ImageProject project)
        -> std::expected<JobGeneration, ImageEditorSubmitError>;

    [[nodiscard]] auto replace(core::ImageProject project)
        -> std::expected<JobGeneration, ImageEditorSubmitError>;

    auto clear() noexcept -> void;
    auto update() -> void;
    auto shutdown() noexcept -> void;

    [[nodiscard]] auto snapshot() const
        -> ImageEditorPipelineSnapshot override;

    [[nodiscard]] auto ready_project(
        std::string_view project_id,
        std::uint64_t project_revision) const
        -> std::shared_ptr<const core::ImageProject> override;

private:
    [[nodiscard]] auto admit(
        core::ImageProject project,
        bool enforce_monotonic_revision)
        -> std::expected<JobGeneration, ImageEditorSubmitError>;
    auto start(
        JobGeneration generation,
        std::shared_ptr<const core::ImageProject> project)
        -> bool;
    auto start_pending() -> bool;
    auto clear_completed() noexcept -> void;
    auto fail(ImageEditorPipelineFailure failure) -> void;

    ImageProjectDecodeWorker decode_worker_;
    ImageCompositionWorker composition_worker_;
    ImageEditorPipelineSnapshot snapshot_{};
    std::shared_ptr<const core::ImageProject> active_project_{};
    std::shared_ptr<const core::ImageProject> pending_project_{};
    std::shared_ptr<const core::ImageProject> ready_project_{};
    JobGeneration active_generation_{};
    JobGeneration pending_generation_{};
    JobGeneration next_generation_{1U};
    ImageEditorPipelinePhase active_phase_{
        ImageEditorPipelinePhase::Empty};
    bool work_active_{};
    bool clear_pending_{};
    bool stopped_{};
};
} // namespace meccha::application
