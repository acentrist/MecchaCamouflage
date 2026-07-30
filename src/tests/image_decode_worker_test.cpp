#include <meccha/application/image_decode_worker.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

constexpr auto ProjectId =
    std::string_view{"0123456789abcdef0123456789abcdef"};
constexpr auto FirstAsset =
    std::string_view{
        "11111111111111111111111111111111"
        "11111111111111111111111111111111"};
constexpr auto SecondAsset =
    std::string_view{
        "22222222222222222222222222222222"
        "22222222222222222222222222222222"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_decode_worker: "
                  << message << '\n';
    }
    return condition;
}

auto encoded(std::byte value)
    -> std::shared_ptr<const std::vector<std::byte>>
{
    return std::make_shared<const std::vector<std::byte>>(
        std::initializer_list<std::byte>{value});
}

auto request() -> ImageProjectDecodeRequest
{
    return ImageProjectDecodeRequest{
        std::string{ProjectId},
        7U,
        {
            core::ImageSourceAsset{
                std::string{FirstAsset},
                core::ImageMime::Png,
                encoded(std::byte{0x11}),
            },
            core::ImageSourceAsset{
                std::string{SecondAsset},
                core::ImageMime::WebP,
                encoded(std::byte{0x22}),
            },
        },
    };
}

class ControlledDecoder final : public ImageSourceDecoder
{
public:
    enum class Mode : std::uint8_t
    {
        Success,
        BlockUntilCancelled,
        InvalidResult,
        LargeResult,
        Throw,
    };

    auto decode(
        std::string_view asset_id,
        core::ImageMime,
        std::span<const std::byte>,
        std::stop_token cancellation)
        -> std::expected<
            core::DecodedImageSource,
            ImageDecodeError> override
    {
        auto mode = Mode::Success;
        {
            const auto lock = std::scoped_lock{mutex_};
            mode = mode_;
            seen_assets_.emplace_back(asset_id);
            entered_ = true;
        }
        condition_.notify_all();
        if (mode == Mode::BlockUntilCancelled)
        {
            auto lock = std::unique_lock{mutex_};
            while (!cancellation.stop_requested())
            {
                condition_.wait_for(lock, 1ms);
            }
            return std::unexpected(ImageDecodeError::Cancelled);
        }
        if (mode == Mode::Throw)
        {
            throw std::runtime_error{"decode failure"};
        }
        if (mode == Mode::InvalidResult)
        {
            return core::DecodedImageSource{
                "wrong-asset",
                1U,
                1U,
                std::make_shared<const std::vector<std::byte>>(
                    4U,
                    std::byte{}),
            };
        }
        if (mode == Mode::LargeResult)
        {
            if (!large_rgba_)
            {
                large_rgba_ =
                    std::make_shared<const std::vector<std::byte>>(
                        static_cast<std::size_t>(
                            core::MaximumDecodedImageBytes),
                        std::byte{});
            }
            return core::DecodedImageSource{
                std::string{asset_id},
                8192U,
                2048U,
                large_rgba_,
            };
        }
        return core::DecodedImageSource{
            std::string{asset_id},
            1U,
            1U,
            std::make_shared<const std::vector<std::byte>>(
                4U,
                std::byte{0x44}),
        };
    }

    auto set_mode(Mode mode) -> void
    {
        const auto lock = std::scoped_lock{mutex_};
        mode_ = mode;
        entered_ = false;
        seen_assets_.clear();
    }

    auto wait_until_entered() -> bool
    {
        auto lock = std::unique_lock{mutex_};
        return condition_.wait_for(
            lock,
            1s,
            [&] { return entered_; });
    }

    [[nodiscard]] auto seen_assets() const -> std::vector<std::string>
    {
        const auto lock = std::scoped_lock{mutex_};
        return seen_assets_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    Mode mode_{Mode::Success};
    bool entered_{};
    std::vector<std::string> seen_assets_{};
    std::shared_ptr<const std::vector<std::byte>> large_rgba_{};
};

auto wait_for_completion(ImageProjectDecodeWorker& worker)
    -> std::optional<ImageProjectDecodeCompletion>
{
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        if (auto result = worker.poll())
        {
            return result;
        }
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    auto decoder = ControlledDecoder{};
    auto worker = ImageProjectDecodeWorker{decoder};
    auto input = request();

    auto invalid_generation = input;
    passed &= expect(
        worker.start(0U, std::move(invalid_generation)) ==
            std::unexpected(
                ImageProjectDecodeStartError::InvalidGeneration),
        "generation zero was accepted");
    auto invalid_identity = input;
    invalid_identity.project_id = "invalid";
    passed &= expect(
        worker.start(1U, std::move(invalid_identity)) ==
            std::unexpected(
                ImageProjectDecodeStartError::
                    InvalidProjectIdentity),
        "an invalid project identity was accepted");
    auto invalid_revision = input;
    invalid_revision.project_revision = 0U;
    passed &= expect(
        worker.start(1U, std::move(invalid_revision)) ==
            std::unexpected(
                ImageProjectDecodeStartError::
                    InvalidProjectRevision),
        "revision zero was accepted");
    auto duplicate_sources = input;
    duplicate_sources.sources[1U].asset_id =
        duplicate_sources.sources[0U].asset_id;
    passed &= expect(
        worker.start(1U, std::move(duplicate_sources)) ==
            std::unexpected(
                ImageProjectDecodeStartError::InvalidSources),
        "duplicate source identities reached the worker thread");

    decoder.set_mode(
        ControlledDecoder::Mode::BlockUntilCancelled);
    passed &= expect(
        worker.start(11U, input).has_value() &&
            decoder.wait_until_entered(),
        "the first immutable decode request did not start");
    input.sources.clear();
    passed &= expect(
        decoder.seen_assets() ==
            std::vector<std::string>{std::string{FirstAsset}},
        "the worker retained caller-owned mutable sources");
    passed &= expect(
        worker.start(12U, request()) ==
                std::unexpected(
                    ImageProjectDecodeStartError::Busy) &&
            worker.request_cancel(12U) ==
                ImageProjectDecodeCancelResult::StaleGeneration &&
            worker.request_cancel(11U) ==
                ImageProjectDecodeCancelResult::Requested,
        "decode admission or cancellation ignored generation ownership");

    const auto cancelled = wait_for_completion(worker);
    passed &= expect(
        cancelled &&
            cancelled->generation == 11U &&
            cancelled->project_id == ProjectId &&
            cancelled->project_revision == 7U &&
            !cancelled->result &&
            cancelled->result.error().kind ==
                ImageProjectDecodeFailureKind::Cancelled,
        "cancelled decode did not publish a fully tagged result");

    decoder.set_mode(ControlledDecoder::Mode::Success);
    auto second = request();
    second.project_revision = 8U;
    passed &= expect(
        worker.start(12U, std::move(second)).has_value() &&
            decoder.wait_until_entered(),
        "the decode worker could not be reused");
    const auto completed = wait_for_completion(worker);
    passed &= expect(
        completed &&
            completed->generation == 12U &&
            completed->project_revision == 8U &&
            completed->result &&
            completed->result.value()->size() == 2U &&
            completed->result.value()->front().asset_id ==
                FirstAsset &&
            completed->result.value()->back().asset_id ==
                SecondAsset,
        "successful decode did not preserve ordered immutable sources");

    decoder.set_mode(ControlledDecoder::Mode::InvalidResult);
    auto invalid_result = request();
    invalid_result.project_revision = 9U;
    passed &= expect(
        worker.start(13U, std::move(invalid_result)).has_value() &&
            decoder.wait_until_entered(),
        "the invalid-result fixture did not start");
    const auto rejected = wait_for_completion(worker);
    passed &= expect(
        rejected && !rejected->result &&
            rejected->result.error().kind ==
                ImageProjectDecodeFailureKind::InvalidResult &&
            rejected->result.error().source_index == 0U,
        "an invalid decoder result was published");

    decoder.set_mode(ControlledDecoder::Mode::LargeResult);
    auto resource_limited = request();
    resource_limited.project_revision = 10U;
    resource_limited.sources.clear();
    for (auto index = 0; index < 5; ++index)
    {
        resource_limited.sources.push_back(core::ImageSourceAsset{
            std::string(
                64U,
                static_cast<char>('1' + index)),
            core::ImageMime::Png,
            encoded(static_cast<std::byte>(index + 1)),
        });
    }
    passed &= expect(
        worker.start(14U, std::move(resource_limited)).has_value() &&
            decoder.wait_until_entered(),
        "the aggregate decoded-resource fixture did not start");
    const auto bounded = wait_for_completion(worker);
    passed &= expect(
        bounded && !bounded->result &&
            bounded->result.error().kind ==
                ImageProjectDecodeFailureKind::ResourceLimit &&
            bounded->result.error().source_index == 4U,
        "aggregate decoded bytes exceeded the project limit");

    decoder.set_mode(ControlledDecoder::Mode::Throw);
    auto throwing = request();
    throwing.project_revision = 11U;
    passed &= expect(
        worker.start(15U, std::move(throwing)).has_value() &&
            decoder.wait_until_entered(),
        "the exception fixture did not start");
    const auto failed = wait_for_completion(worker);
    passed &= expect(
        failed && !failed->result &&
            failed->result.error().kind ==
                ImageProjectDecodeFailureKind::WorkerException,
        "an exception crossed the decode worker boundary");

    worker.shutdown();
    passed &= expect(
        worker.start(16U, request()) ==
            std::unexpected(
                ImageProjectDecodeStartError::Stopped),
        "a stopped decode worker accepted new work");

    if (passed)
    {
        std::cout << "PASS image_decode_worker\n";
    }
    return passed ? 0 : 1;
}
