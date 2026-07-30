#pragma once

#include <meccha/application/job_state.hpp>
#include <meccha/core/image_compositor.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace meccha::application
{
struct ImageCompositionRequest
{
    std::string project_id{};
    std::uint64_t project_revision{};
    core::ImageProjectSettings settings{};
    std::vector<core::ImageLayer> layers{};
    std::vector<core::DecodedImageSource> sources{};
};

class ImageAtlasComposer
{
public:
    ImageAtlasComposer() = default;
    ImageAtlasComposer(const ImageAtlasComposer&) = delete;
    auto operator=(const ImageAtlasComposer&) -> ImageAtlasComposer& =
        delete;
    virtual ~ImageAtlasComposer() = default;

    [[nodiscard]] virtual auto compose(
        const core::ImageProjectSettings& settings,
        std::span<const core::ImageLayer> layers,
        std::span<const core::DecodedImageSource> sources,
        std::stop_token cancellation)
        -> std::expected<
            core::ImageAtlasComposition,
            core::ImageComposeError> = 0;
};

class CoreImageAtlasComposer final : public ImageAtlasComposer
{
public:
    [[nodiscard]] auto compose(
        const core::ImageProjectSettings& settings,
        std::span<const core::ImageLayer> layers,
        std::span<const core::DecodedImageSource> sources,
        std::stop_token cancellation)
        -> std::expected<
            core::ImageAtlasComposition,
            core::ImageComposeError> override;
};

enum class ImageCompositionFailureKind : std::uint8_t
{
    Composer,
    WorkerException,
};

struct ImageCompositionFailure
{
    ImageCompositionFailureKind kind{
        ImageCompositionFailureKind::Composer};
    std::optional<core::ImageComposeError> compose_error{};

    auto operator==(const ImageCompositionFailure&) const -> bool =
        default;
};

using ImageCompositionResult = std::expected<
    std::shared_ptr<const core::ImageAtlasComposition>,
    ImageCompositionFailure>;

struct ImageCompositionCompletion
{
    JobGeneration generation{};
    std::string project_id{};
    std::uint64_t project_revision{};
    ImageCompositionResult result;
};

enum class ImageCompositionStartError : std::uint8_t
{
    InvalidGeneration,
    InvalidProjectIdentity,
    InvalidProjectRevision,
    Busy,
    Stopped,
    ThreadStart,
};

enum class ImageCompositionCancelResult : std::uint8_t
{
    Requested,
    AlreadyCompleted,
    Idle,
    StaleGeneration,
};

class ImageCompositionWorker
{
public:
    explicit ImageCompositionWorker(ImageAtlasComposer& composer);
    ImageCompositionWorker(const ImageCompositionWorker&) = delete;
    auto operator=(const ImageCompositionWorker&)
        -> ImageCompositionWorker& = delete;
    ~ImageCompositionWorker();

    [[nodiscard]] auto start(
        JobGeneration generation,
        ImageCompositionRequest request)
        -> std::expected<void, ImageCompositionStartError>;

    [[nodiscard]] auto request_cancel(JobGeneration generation)
        -> ImageCompositionCancelResult;

    [[nodiscard]] auto poll()
        -> std::optional<ImageCompositionCompletion>;

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
        ImageCompositionRequest request,
        std::stop_token cancellation) noexcept -> void;

    ImageAtlasComposer& composer_;
    std::mutex mutex_{};
    std::jthread worker_{};
    std::optional<ImageCompositionCompletion> completion_{};
    JobGeneration active_generation_{};
    State state_{State::Idle};
    bool stopped_{};
};
} // namespace meccha::application
