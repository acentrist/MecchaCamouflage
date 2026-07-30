#include <meccha/application/image_editor_pipeline.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <mutex>
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
constexpr auto AssetId =
    std::string_view{
        "11111111111111111111111111111111"
        "11111111111111111111111111111111"};

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_editor_pipeline: "
                  << message << '\n';
    }
    return condition;
}

auto project(std::uint64_t revision) -> core::ImageProject
{
    auto encoded =
        std::make_shared<const std::vector<std::byte>>(
            std::initializer_list<std::byte>{
                std::byte{0x11},
            });
    return core::ImageProject{
        core::ImageProjectSchemaVersion,
        std::string{ProjectId},
        "Project",
        revision,
        {},
        {core::ImageLayer{
            std::string{AssetId},
            "source.png",
            core::ImageMime::Png,
            encoded->size(),
        }},
        {core::ImageSourceAsset{
            std::string{AssetId},
            core::ImageMime::Png,
            encoded,
        }},
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            std::byte{0x00}),
    };
}

class TestDecoder final : public ImageSourceDecoder
{
public:
    auto decode(
        std::string_view asset_id,
        core::ImageMime,
        std::span<const std::byte>,
        std::stop_token)
        -> std::expected<
            core::DecodedImageSource,
            ImageDecodeError> override
    {
        return core::DecodedImageSource{
            std::string{asset_id},
            1U,
            1U,
            std::make_shared<const std::vector<std::byte>>(
                std::initializer_list<std::byte>{
                    std::byte{0x44},
                    std::byte{0x55},
                    std::byte{0x66},
                    std::byte{0xFF},
                }),
        };
    }
};

class TestComposer final : public ImageAtlasComposer
{
public:
    auto compose(
        const core::ImageProjectSettings&,
        std::span<const core::ImageLayer>,
        std::span<const core::DecodedImageSource>,
        std::stop_token)
        -> std::expected<
            core::ImageAtlasComposition,
            core::ImageComposeError> override
    {
        return core::ImageAtlasComposition{
            std::vector<std::byte>(
                core::CanonicalAtlasByteLength,
                std::byte{0x7A}),
            1U,
            1U,
            1U,
            1U,
        };
    }
};

class SupersededDecoder final : public ImageSourceDecoder
{
public:
    auto decode(
        std::string_view asset_id,
        core::ImageMime,
        std::span<const std::byte>,
        std::stop_token cancellation)
        -> std::expected<
            core::DecodedImageSource,
            ImageDecodeError> override
    {
        auto call = std::size_t{};
        {
            const auto lock = std::scoped_lock{mutex_};
            call = ++calls_;
        }
        condition_.notify_all();
        if (call == 1U)
        {
            auto lock = std::unique_lock{mutex_};
            while (!cancellation.stop_requested())
            {
                condition_.wait_for(lock, 1ms);
            }
            return std::unexpected(ImageDecodeError::Cancelled);
        }
        return core::DecodedImageSource{
            std::string{asset_id},
            1U,
            1U,
            std::make_shared<const std::vector<std::byte>>(
                4U,
                std::byte{0x55}),
        };
    }

    auto wait_until_first_decode() -> bool
    {
        auto lock = std::unique_lock{mutex_};
        return condition_.wait_for(
            lock,
            5ms,
            [&] { return calls_ != 0U; });
    }

private:
    std::mutex mutex_{};
    std::condition_variable condition_{};
    std::size_t calls_{};
};

class FailingDecoder final : public ImageSourceDecoder
{
public:
    auto decode(
        std::string_view,
        core::ImageMime,
        std::span<const std::byte>,
        std::stop_token)
        -> std::expected<
            core::DecodedImageSource,
            ImageDecodeError> override
    {
        return std::unexpected(ImageDecodeError::MalformedImage);
    }
};

class SupersededComposer final : public ImageAtlasComposer
{
public:
    auto compose(
        const core::ImageProjectSettings&,
        std::span<const core::ImageLayer>,
        std::span<const core::DecodedImageSource>,
        std::stop_token cancellation)
        -> std::expected<
            core::ImageAtlasComposition,
            core::ImageComposeError> override
    {
        auto call = std::size_t{};
        {
            const auto lock = std::scoped_lock{mutex_};
            call = ++calls_;
        }
        condition_.notify_all();
        if (call == 1U)
        {
            auto lock = std::unique_lock{mutex_};
            while (!cancellation.stop_requested())
            {
                condition_.wait_for(lock, 1ms);
            }
            return std::unexpected(
                core::ImageComposeError::Cancelled);
        }
        return core::ImageAtlasComposition{
            std::vector<std::byte>(
                core::CanonicalAtlasByteLength,
                std::byte{0x6B}),
            1U,
            1U,
            1U,
            1U,
        };
    }

    auto wait_until_first_compose() -> bool
    {
        auto lock = std::unique_lock{mutex_};
        return condition_.wait_for(
            lock,
            5ms,
            [&] { return calls_ != 0U; });
    }

private:
    std::mutex mutex_{};
    std::condition_variable condition_{};
    std::size_t calls_{};
};

auto wait_until_settled(ImageEditorPipeline& pipeline) -> bool
{
    for (auto attempt = 0; attempt < 1000; ++attempt)
    {
        pipeline.update();
        const auto phase = pipeline.snapshot().phase;
        if (phase == ImageEditorPipelinePhase::Ready ||
            phase == ImageEditorPipelinePhase::Failed)
        {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}
} // namespace

auto main() -> int
{
    auto passed = true;
    auto decoder = TestDecoder{};
    auto composer = TestComposer{};
    auto pipeline =
        meccha::application::ImageEditorPipeline{
            decoder,
            composer,
        };

    const auto generation = pipeline.submit(project(7U));
    passed &= expect(
        generation.has_value() && *generation == 1U,
        "a valid project did not enter the editor pipeline");
    passed &= expect(
        wait_until_settled(pipeline),
        "the editor pipeline did not settle");

    const auto snapshot = pipeline.snapshot();
    const auto ready = pipeline.ready_project(ProjectId, 7U);
    const auto ready_content =
        pipeline.ready_content(ProjectId, 7U);
    passed &= expect(
        snapshot.phase ==
                ImageEditorPipelinePhase::Ready &&
            snapshot.generation == 1U &&
            snapshot.project_id == ProjectId &&
            snapshot.project_revision == 7U &&
            ready && ready->revision == 7U &&
            ready->canonical_atlas &&
            std::to_integer<std::uint8_t>(
                ready->canonical_atlas->front()) == 0x7AU &&
            ready_content &&
            ready_content->project == ready &&
            ready_content->decoded_sources &&
            ready_content->decoded_sources->size() == 1U &&
            ready_content->decoded_sources->front().asset_id ==
                AssetId &&
            ready_content->decoded_sources->front().width == 1U &&
            ready_content->decoded_sources->front().height == 1U &&
            ready_content->decoded_sources->front().rgba &&
            ready_content->decoded_sources->front().rgba->size() ==
                4U &&
            std::to_integer<std::uint8_t>(
                ready_content->decoded_sources->front()
                    .rgba->front()) == 0x44U,
        "decode and composition did not publish matching immutable "
        "project and source content");
    passed &= expect(
        !pipeline.ready_project(ProjectId, 6U) &&
            !pipeline.ready_content(ProjectId, 6U),
        "ready content was returned for a stale revision");

    const auto replaced = pipeline.replace(project(6U));
    passed &= expect(
        replaced.has_value() &&
            wait_until_settled(pipeline) &&
            pipeline.ready_project(ProjectId, 6U),
        "an explicit project load could not replace a newer editor revision");
    pipeline.clear();
    passed &= expect(
        pipeline.snapshot() == ImageEditorPipelineSnapshot{} &&
            !pipeline.ready_project(ProjectId, 6U) &&
            !pipeline.ready_content(ProjectId, 6U),
        "clearing an idle editor retained its ready content");

    const auto restored = pipeline.submit(project(7U));
    passed &= expect(
        restored.has_value() &&
            wait_until_settled(pipeline),
        "the cleared editor could not be reused");
    const auto current_ready =
        pipeline.ready_project(ProjectId, 7U);

    auto invalid = project(8U);
    invalid.canonical_atlas.reset();
    passed &= expect(
        pipeline.submit(std::move(invalid)) ==
                std::unexpected(
                    ImageEditorSubmitError::InvalidProject) &&
            pipeline.submit(project(7U)) ==
                std::unexpected(
                    ImageEditorSubmitError::StaleRevision) &&
            pipeline.ready_project(ProjectId, 7U) ==
                current_ready &&
            pipeline.ready_content(ProjectId, 7U) &&
            pipeline.ready_content(ProjectId, 7U)->project ==
                current_ready,
        "invalid or stale edits disturbed the ready content");

    auto superseded_decoder = SupersededDecoder{};
    auto coalescing_composer = TestComposer{};
    auto coalescing_pipeline =
        meccha::application::ImageEditorPipeline{
            superseded_decoder,
            coalescing_composer,
        };
    const auto first =
        coalescing_pipeline.submit(project(8U));
    passed &= expect(
        first.has_value() &&
            superseded_decoder.wait_until_first_decode(),
        "the superseded revision did not start decoding");
    const auto latest =
        coalescing_pipeline.submit(project(9U));
    passed &= expect(
        latest.has_value() && *latest == 2U &&
            coalescing_pipeline.snapshot().pending,
        "a newer edit was not accepted while decode was active");
    if (!latest)
    {
        coalescing_pipeline.shutdown();
    }
    else
    {
        passed &= expect(
            wait_until_settled(coalescing_pipeline),
            "the coalesced editor pipeline did not settle");
        const auto latest_ready =
            coalescing_pipeline.ready_project(ProjectId, 9U);
        passed &= expect(
            latest_ready && latest_ready->revision == 9U &&
                coalescing_pipeline.snapshot().generation == 2U &&
                coalescing_pipeline.snapshot().phase ==
                    ImageEditorPipelinePhase::Ready,
            "a superseded revision escaped or blocked the latest project");
    }

    auto composition_decoder = TestDecoder{};
    auto superseded_composer = SupersededComposer{};
    auto composition_pipeline =
        meccha::application::ImageEditorPipeline{
            composition_decoder,
            superseded_composer,
        };
    const auto composing =
        composition_pipeline.submit(project(10U));
    auto entered_composition = false;
    for (auto attempt = 0;
         attempt < 1000 && !entered_composition;
         ++attempt)
    {
        composition_pipeline.update();
        entered_composition =
            superseded_composer.wait_until_first_compose();
        if (!entered_composition)
        {
            std::this_thread::sleep_for(1ms);
        }
    }
    const auto replacement =
        composition_pipeline.submit(project(11U));
    passed &= expect(
        composing.has_value() && entered_composition &&
            replacement.has_value() && *replacement == 2U,
        "a newer edit was not accepted while composition was active");
    passed &= expect(
        wait_until_settled(composition_pipeline),
        "superseded composition did not settle");
    const auto recomposed =
        composition_pipeline.ready_project(ProjectId, 11U);
    passed &= expect(
        recomposed && recomposed->canonical_atlas &&
            std::to_integer<std::uint8_t>(
                recomposed->canonical_atlas->front()) == 0x6BU,
        "cancelled composition published instead of the latest revision");

    auto failing_decoder = FailingDecoder{};
    auto unused_composer = TestComposer{};
    auto failing_pipeline =
        meccha::application::ImageEditorPipeline{
            failing_decoder,
            unused_composer,
        };
    passed &= expect(
        failing_pipeline.submit(project(12U)).has_value() &&
            wait_until_settled(failing_pipeline),
        "the failing decode pipeline did not settle");
    const auto failure = failing_pipeline.snapshot();
    passed &= expect(
        failure.phase == ImageEditorPipelinePhase::Failed &&
            failure.failure &&
            failure.failure->kind ==
                ImageEditorPipelineFailureKind::Decode &&
            failure.failure->decode &&
            failure.failure->decode->decoder_error ==
                ImageDecodeError::MalformedImage &&
            !failing_pipeline.ready_project(ProjectId, 12U) &&
            !failing_pipeline.ready_content(ProjectId, 12U),
        "decode failure did not remain typed and fail closed");

    auto clearing_decoder = SupersededDecoder{};
    auto clearing_composer = TestComposer{};
    auto clearing_pipeline =
        meccha::application::ImageEditorPipeline{
            clearing_decoder,
            clearing_composer,
        };
    passed &= expect(
        clearing_pipeline.submit(project(13U)).has_value() &&
            clearing_decoder.wait_until_first_decode(),
        "the clear-during-work fixture did not start");
    clearing_pipeline.clear();
    for (auto attempt = 0;
         attempt < 1000 &&
         clearing_pipeline.snapshot().phase !=
             ImageEditorPipelinePhase::Empty;
         ++attempt)
    {
        clearing_pipeline.update();
        std::this_thread::sleep_for(1ms);
    }
    passed &= expect(
        clearing_pipeline.snapshot() ==
                ImageEditorPipelineSnapshot{} &&
            !clearing_pipeline.ready_project(ProjectId, 13U) &&
            !clearing_pipeline.ready_content(ProjectId, 13U),
        "clear allowed cancelled editor work to publish");

    composition_pipeline.shutdown();
    passed &= expect(
        composition_pipeline.snapshot().phase ==
                ImageEditorPipelinePhase::Stopped &&
            composition_pipeline.submit(project(12U)) ==
                std::unexpected(
                    ImageEditorSubmitError::Stopped) &&
            !composition_pipeline.ready_project(ProjectId, 11U) &&
            !composition_pipeline.ready_content(ProjectId, 11U),
        "shutdown retained output or accepted new editor work");

    if (passed)
    {
        std::cout << "PASS image_editor_pipeline\n";
    }
    return passed ? 0 : 1;
}
