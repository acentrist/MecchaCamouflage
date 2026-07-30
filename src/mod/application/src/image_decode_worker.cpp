#include <meccha/application/image_decode_worker.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
auto lowercase_hex(std::string_view value, std::size_t length) -> bool
{
    return value.size() == length &&
           std::ranges::all_of(
               value,
               [](unsigned char character)
               {
                   return (character >= '0' && character <= '9') ||
                          (character >= 'a' && character <= 'f');
               });
}

auto valid_mime(core::ImageMime mime) -> bool
{
    return mime == core::ImageMime::Png ||
           mime == core::ImageMime::Jpeg ||
           mime == core::ImageMime::WebP;
}

auto valid_sources(
    const std::vector<core::ImageSourceAsset>& sources) -> bool
{
    if (sources.empty() ||
        sources.size() > core::MaximumImageSources)
    {
        return false;
    }
    auto identities = std::set<std::string, std::less<>>{};
    auto total = std::size_t{};
    for (const auto& source : sources)
    {
        if (!lowercase_hex(source.asset_id, 64U) ||
            !identities.insert(source.asset_id).second ||
            !valid_mime(source.mime) || !source.bytes ||
            source.bytes->empty() ||
            source.bytes->size() > core::MaximumImageSourceBytes ||
            source.bytes->size() >
                core::MaximumProjectSourceBytes - total)
        {
            return false;
        }
        total += source.bytes->size();
    }
    return true;
}

auto failure(
    ImageProjectDecodeFailureKind kind,
    std::optional<ImageDecodeError> decoder_error = std::nullopt,
    std::optional<std::size_t> source_index = std::nullopt)
    -> std::unexpected<ImageProjectDecodeFailure>
{
    return std::unexpected(ImageProjectDecodeFailure{
        kind,
        decoder_error,
        source_index,
    });
}
} // namespace

ImageProjectDecodeWorker::ImageProjectDecodeWorker(
    ImageSourceDecoder& decoder)
    : decoder_{decoder}
{
}

ImageProjectDecodeWorker::~ImageProjectDecodeWorker()
{
    shutdown();
}

auto ImageProjectDecodeWorker::start(
    JobGeneration generation,
    ImageProjectDecodeRequest request)
    -> std::expected<void, ImageProjectDecodeStartError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(
            ImageProjectDecodeStartError::Stopped);
    }
    if (generation == 0U)
    {
        return std::unexpected(
            ImageProjectDecodeStartError::InvalidGeneration);
    }
    if (!core::valid_image_project_id(request.project_id))
    {
        return std::unexpected(
            ImageProjectDecodeStartError::InvalidProjectIdentity);
    }
    if (request.project_revision == 0U)
    {
        return std::unexpected(
            ImageProjectDecodeStartError::InvalidProjectRevision);
    }
    if (!valid_sources(request.sources))
    {
        return std::unexpected(
            ImageProjectDecodeStartError::InvalidSources);
    }
    if (state_ != State::Idle || worker_.joinable())
    {
        return std::unexpected(ImageProjectDecodeStartError::Busy);
    }

    active_generation_ = generation;
    state_ = State::Running;
    try
    {
        worker_ = std::jthread{
            [this,
             generation,
             request = std::move(request)](
                std::stop_token cancellation) mutable {
                run(
                    generation,
                    std::move(request),
                    cancellation);
            }};
    }
    catch (...)
    {
        active_generation_ = 0U;
        state_ = State::Idle;
        return std::unexpected(
            ImageProjectDecodeStartError::ThreadStart);
    }
    return {};
}

auto ImageProjectDecodeWorker::request_cancel(
    JobGeneration generation) -> ImageProjectDecodeCancelResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Idle)
    {
        return ImageProjectDecodeCancelResult::Idle;
    }
    if (generation == 0U || generation != active_generation_)
    {
        return ImageProjectDecodeCancelResult::StaleGeneration;
    }
    if (state_ == State::Completed)
    {
        return ImageProjectDecodeCancelResult::AlreadyCompleted;
    }
    static_cast<void>(worker_.request_stop());
    return ImageProjectDecodeCancelResult::Requested;
}

auto ImageProjectDecodeWorker::poll()
    -> std::optional<ImageProjectDecodeCompletion>
{
    auto completed_thread = std::jthread{};
    auto result = std::optional<ImageProjectDecodeCompletion>{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (state_ != State::Completed || !completion_)
        {
            return std::nullopt;
        }
        result = std::move(completion_);
        completion_.reset();
        completed_thread = std::move(worker_);
        active_generation_ = 0U;
        state_ = State::Idle;
    }
    if (completed_thread.joinable())
    {
        completed_thread.join();
    }
    return result;
}

auto ImageProjectDecodeWorker::shutdown() noexcept -> void
{
    auto stopped_thread = std::jthread{};
    {
        const auto lock = std::scoped_lock{mutex_};
        if (stopped_ && !worker_.joinable())
        {
            return;
        }
        stopped_ = true;
        if (worker_.joinable())
        {
            static_cast<void>(worker_.request_stop());
            stopped_thread = std::move(worker_);
        }
    }
    if (stopped_thread.joinable())
    {
        stopped_thread.join();
    }
    const auto lock = std::scoped_lock{mutex_};
    completion_.reset();
    active_generation_ = 0U;
    state_ = State::Idle;
}

auto ImageProjectDecodeWorker::run(
    JobGeneration generation,
    ImageProjectDecodeRequest request,
    std::stop_token cancellation) noexcept -> void
{
    auto decoded =
        std::vector<core::DecodedImageSource>{};
    auto result =
        std::optional<ImageProjectDecodeCompletion>{};
    try
    {
        decoded.reserve(request.sources.size());
        auto total_decoded_bytes = std::uint64_t{};
        for (auto index = std::size_t{};
             index < request.sources.size();
             ++index)
        {
            if (cancellation.stop_requested())
            {
                result = ImageProjectDecodeCompletion{
                    generation,
                    request.project_id,
                    request.project_revision,
                    failure(
                        ImageProjectDecodeFailureKind::Cancelled,
                        ImageDecodeError::Cancelled,
                        index),
                };
                break;
            }
            const auto& source = request.sources[index];
            auto image = decoder_.decode(
                source.asset_id,
                source.mime,
                *source.bytes,
                cancellation);
            if (!image)
            {
                const auto cancelled =
                    image.error() == ImageDecodeError::Cancelled ||
                    cancellation.stop_requested();
                result = ImageProjectDecodeCompletion{
                    generation,
                    request.project_id,
                    request.project_revision,
                    failure(
                        cancelled
                            ? ImageProjectDecodeFailureKind::
                                  Cancelled
                            : ImageProjectDecodeFailureKind::
                                  Decoder,
                        image.error(),
                        index),
                };
                break;
            }
            const auto bytes = checked_decoded_rgba_bytes(
                image->width,
                image->height);
            if (!bytes || image->asset_id != source.asset_id ||
                !image->rgba || image->rgba->size() != *bytes)
            {
                result = ImageProjectDecodeCompletion{
                    generation,
                    request.project_id,
                    request.project_revision,
                    failure(
                        ImageProjectDecodeFailureKind::InvalidResult,
                        std::nullopt,
                        index),
                };
                break;
            }
            if (*bytes >
                core::MaximumDecodedProjectBytes -
                    total_decoded_bytes)
            {
                result = ImageProjectDecodeCompletion{
                    generation,
                    request.project_id,
                    request.project_revision,
                    failure(
                        ImageProjectDecodeFailureKind::ResourceLimit,
                        std::nullopt,
                        index),
                };
                break;
            }
            total_decoded_bytes += *bytes;
            decoded.push_back(std::move(*image));
        }
        if (!result)
        {
            result = ImageProjectDecodeCompletion{
                generation,
                request.project_id,
                request.project_revision,
                std::make_shared<
                    const std::vector<core::DecodedImageSource>>(
                    std::move(decoded)),
            };
        }
    }
    catch (...)
    {
        result = ImageProjectDecodeCompletion{
            generation,
            request.project_id,
            request.project_revision,
            failure(
                ImageProjectDecodeFailureKind::WorkerException),
        };
    }

    const auto lock = std::scoped_lock{mutex_};
    if (state_ == State::Running &&
        active_generation_ == generation)
    {
        completion_ = std::move(result);
        state_ = State::Completed;
    }
}
} // namespace meccha::application
