#include <meccha/application/image_composition_worker.hpp>

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
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using namespace meccha;
using namespace meccha::application;
using namespace std::chrono_literals;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_composition_worker: "
                  << message << '\n';
    }
    return condition;
}

auto request() -> ImageCompositionRequest
{
    auto rgba = std::make_shared<const std::vector<std::byte>>(
        std::initializer_list<std::byte>{
            std::byte{0x11},
            std::byte{0x22},
            std::byte{0x33},
            std::byte{0xFF},
        });
    return ImageCompositionRequest{
        "0123456789abcdef0123456789abcdef",
        7U,
        core::ImageProjectSettings{},
        std::vector{core::ImageLayer{
            "asset",
            "image.png",
            core::ImageMime::Png,
            4U,
        }},
        std::vector{core::DecodedImageSource{
            "asset",
            1U,
            1U,
            std::move(rgba),
        }},
    };
}

class ControlledComposer final : public ImageAtlasComposer
{
public:
    auto compose(
        const core::ImageProjectSettings&,
        std::span<const core::ImageLayer> layers,
        std::span<const core::DecodedImageSource> sources,
        std::stop_token cancellation)
        -> std::expected<
            core::ImageAtlasComposition,
            core::ImageComposeError> override
    {
        auto call = std::size_t{};
        {
            const auto lock = std::scoped_lock{mutex_};
            call = ++calls_;
            layer_count_ = layers.size();
            source_count_ = sources.size();
            entered_ = true;
        }
        condition_.notify_all();

        if (call == 1U)
        {
            auto lock = std::unique_lock{mutex_};
            while (!cancellation.stop_requested())
            {
                condition_.wait_for(lock, 1ms);
            }
            return std::unexpected(core::ImageComposeError::Cancelled);
        }
        if (call == 3U)
        {
            throw std::runtime_error{"composition failure"};
        }
        return core::ImageAtlasComposition{
            std::vector<std::byte>(
                core::CanonicalAtlasByteLength,
                std::byte{0x44}),
            1U,
            1U,
            1U,
            1U,
        };
    }

    auto wait_until_entered() -> bool
    {
        auto lock = std::unique_lock{mutex_};
        return condition_.wait_for(
            lock,
            1s,
            [&] { return entered_; });
    }

    auto reset_entered() -> void
    {
        const auto lock = std::scoped_lock{mutex_};
        entered_ = false;
    }

    [[nodiscard]] auto layer_count() const -> std::size_t
    {
        const auto lock = std::scoped_lock{mutex_};
        return layer_count_;
    }

    [[nodiscard]] auto source_count() const -> std::size_t
    {
        const auto lock = std::scoped_lock{mutex_};
        return source_count_;
    }

private:
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    std::size_t calls_{};
    std::size_t layer_count_{};
    std::size_t source_count_{};
    bool entered_{};
};

auto wait_for_completion(ImageCompositionWorker& worker)
    -> std::optional<ImageCompositionCompletion>
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
    auto composer = ControlledComposer{};
    auto worker = ImageCompositionWorker{composer};
    auto input = request();

    auto invalid_generation = input;
    passed &= expect(
        worker.start(0U, std::move(invalid_generation)) ==
            std::unexpected(
                ImageCompositionStartError::InvalidGeneration),
        "generation zero was accepted");
    auto invalid_identity = input;
    invalid_identity.project_id = "not-an-id";
    passed &= expect(
        worker.start(1U, std::move(invalid_identity)) ==
            std::unexpected(
                ImageCompositionStartError::InvalidProjectIdentity),
        "an invalid project identity was accepted");
    auto invalid_revision = input;
    invalid_revision.project_revision = 0U;
    passed &= expect(
        worker.start(1U, std::move(invalid_revision)) ==
            std::unexpected(
                ImageCompositionStartError::InvalidProjectRevision),
        "revision zero was accepted");

    passed &= expect(
        worker.start(11U, input).has_value() &&
            composer.wait_until_entered(),
        "the first immutable composition request did not start");
    input.layers.clear();
    input.sources.clear();
    passed &= expect(
        composer.layer_count() == 1U &&
            composer.source_count() == 1U,
        "the worker retained caller-owned mutable collections");
    passed &= expect(
        worker.start(12U, request()) ==
            std::unexpected(ImageCompositionStartError::Busy),
        "a second concurrent composition was accepted");
    passed &= expect(
        worker.request_cancel(12U) ==
                ImageCompositionCancelResult::StaleGeneration &&
            worker.request_cancel(11U) ==
                ImageCompositionCancelResult::Requested,
        "cancellation did not validate and stop the active generation");

    const auto cancelled = wait_for_completion(worker);
    passed &= expect(
        cancelled &&
            cancelled->generation == 11U &&
            cancelled->project_id ==
                "0123456789abcdef0123456789abcdef" &&
            cancelled->project_revision == 7U &&
            !cancelled->result &&
            cancelled->result.error().kind ==
                ImageCompositionFailureKind::Composer &&
            cancelled->result.error().compose_error ==
                core::ImageComposeError::Cancelled,
        "cancelled composition did not publish a fully tagged failure");

    composer.reset_entered();
    auto second = request();
    second.project_revision = 8U;
    passed &= expect(
        worker.start(12U, std::move(second)).has_value() &&
            composer.wait_until_entered(),
        "the worker could not be reused after collection");
    const auto completed = wait_for_completion(worker);
    passed &= expect(
        completed &&
            completed->generation == 12U &&
            completed->project_revision == 8U &&
            completed->result &&
            completed->result.value()->rgba.size() ==
                core::CanonicalAtlasByteLength &&
            std::to_integer<std::uint8_t>(
                completed->result.value()->rgba.front()) == 0x44U,
        "successful composition did not publish an immutable tagged atlas");

    composer.reset_entered();
    auto third = request();
    third.project_revision = 9U;
    passed &= expect(
        worker.start(13U, std::move(third)).has_value() &&
            composer.wait_until_entered(),
        "the exception fixture did not start");
    const auto failed = wait_for_completion(worker);
    passed &= expect(
        failed &&
            failed->generation == 13U &&
            failed->project_revision == 9U &&
            !failed->result &&
            failed->result.error().kind ==
                ImageCompositionFailureKind::WorkerException &&
            !failed->result.error().compose_error,
        "an exception crossed the composition worker boundary");

    worker.shutdown();
    passed &= expect(
        worker.start(14U, request()) ==
            std::unexpected(ImageCompositionStartError::Stopped),
        "a stopped composition worker accepted new work");

    if (passed)
    {
        std::cout << "PASS image_composition_worker\n";
    }
    return passed ? 0 : 1;
}
