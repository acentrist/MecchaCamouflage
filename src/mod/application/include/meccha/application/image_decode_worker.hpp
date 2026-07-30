#pragma once

#include <meccha/application/image_decoder.hpp>
#include <meccha/application/job_state.hpp>
#include <meccha/core/image_project.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace meccha::application
{
struct ImageProjectDecodeRequest
{
    std::string project_id{};
    std::uint64_t project_revision{};
    std::vector<core::ImageSourceAsset> sources{};
};

enum class ImageProjectDecodeFailureKind : std::uint8_t
{
    Decoder,
    InvalidResult,
    ResourceLimit,
    Cancelled,
    WorkerException,
};

struct ImageProjectDecodeFailure
{
    ImageProjectDecodeFailureKind kind{
        ImageProjectDecodeFailureKind::Decoder};
    std::optional<ImageDecodeError> decoder_error{};
    std::optional<std::size_t> source_index{};

    auto operator==(const ImageProjectDecodeFailure&) const
        -> bool = default;
};

using ImageProjectDecodeResult = std::expected<
    std::shared_ptr<
        const std::vector<core::DecodedImageSource>>,
    ImageProjectDecodeFailure>;

struct ImageProjectDecodeCompletion
{
    JobGeneration generation{};
    std::string project_id{};
    std::uint64_t project_revision{};
    ImageProjectDecodeResult result;
};

enum class ImageProjectDecodeStartError : std::uint8_t
{
    InvalidGeneration,
    InvalidProjectIdentity,
    InvalidProjectRevision,
    InvalidSources,
    Busy,
    Stopped,
    ThreadStart,
};

enum class ImageProjectDecodeCancelResult : std::uint8_t
{
    Requested,
    AlreadyCompleted,
    Idle,
    StaleGeneration,
};

class ImageProjectDecodeWorker
{
public:
    explicit ImageProjectDecodeWorker(ImageSourceDecoder& decoder);
    ImageProjectDecodeWorker(const ImageProjectDecodeWorker&) = delete;
    auto operator=(const ImageProjectDecodeWorker&)
        -> ImageProjectDecodeWorker& = delete;
    ~ImageProjectDecodeWorker();

    [[nodiscard]] auto start(
        JobGeneration generation,
        ImageProjectDecodeRequest request)
        -> std::expected<void, ImageProjectDecodeStartError>;

    [[nodiscard]] auto request_cancel(JobGeneration generation)
        -> ImageProjectDecodeCancelResult;

    [[nodiscard]] auto poll()
        -> std::optional<ImageProjectDecodeCompletion>;

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
        ImageProjectDecodeRequest request,
        std::stop_token cancellation) noexcept -> void;

    ImageSourceDecoder& decoder_;
    std::mutex mutex_{};
    std::jthread worker_{};
    std::optional<ImageProjectDecodeCompletion> completion_{};
    JobGeneration active_generation_{};
    State state_{State::Idle};
    bool stopped_{};
};
} // namespace meccha::application
