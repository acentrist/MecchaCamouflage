#include <meccha/application/paint_preview_controller.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace meccha::application;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL paint_preview_controller: "
                  << message << '\n';
    }
    return condition;
}

auto image(std::uint32_t dimension = 2U) -> PaintTextureImage
{
    const auto bytes = static_cast<std::size_t>(
        dimension * dimension * 4U);
    auto albedo = std::vector<std::byte>(
        bytes,
        std::byte{0x11});
    auto pbr = std::vector<std::byte>(
        bytes,
        std::byte{0x22});
    return PaintTextureImage{
        dimension,
        std::make_shared<const std::vector<std::byte>>(
            std::move(albedo)),
        std::make_shared<const std::vector<std::byte>>(
            std::move(pbr)),
    };
}

class FakeThreadContext final : public GameThreadContext
{
public:
    [[nodiscard]] auto is_game_thread() const noexcept -> bool override
    {
        return game_thread;
    }

    bool game_thread{true};
};

class FakePreviewRuntime final : public PaintPreviewRuntimePort
{
public:
    auto capture(RuntimeObjectHandle component)
        -> std::expected<
            PaintPreviewSnapshot,
            RuntimeExecutionError> override
    {
        ++capture_count;
        if (capture_failure)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
                std::nullopt,
            });
        }
        return PaintPreviewSnapshot{
            component,
            malformed_capture
                ? PaintTextureImage{}
                : image(),
        };
    }

    auto apply(
        RuntimeObjectHandle component,
        const PaintTextureImage& applied)
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++apply_count;
        last_applied_component = component;
        last_applied = applied;
        if (apply_failure)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
                std::nullopt,
            });
        }
        return {};
    }

    auto restore(const PaintPreviewSnapshot& snapshot)
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++restore_count;
        restored_components.push_back(snapshot.component);
        if (restore_failure)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
                std::nullopt,
            });
        }
        return {};
    }

    std::size_t capture_count{};
    std::size_t apply_count{};
    std::size_t restore_count{};
    RuntimeObjectHandle last_applied_component{};
    PaintTextureImage last_applied{};
    std::vector<RuntimeObjectHandle> restored_components{};
    bool capture_failure{};
    bool malformed_capture{};
    bool apply_failure{};
    bool restore_failure{};
};
} // namespace

auto main() -> int
{
    using namespace meccha::application;

    auto passed = true;
    auto thread = FakeThreadContext{};
    auto runtime = FakePreviewRuntime{};
    auto leases = PreviewStateMachine{};
    auto previews =
        PaintPreviewController{thread, runtime, leases};
    constexpr auto First = RuntimeObjectHandle{10U, 1U};
    constexpr auto Second = RuntimeObjectHandle{20U, 2U};

    thread.game_thread = false;
    const auto wrong_thread =
        previews.begin(Feature::Paint, First);
    passed &= expect(
        !wrong_thread &&
            wrong_thread.error().code ==
                PaintPreviewErrorCode::WrongThread &&
            runtime.capture_count == 0U,
        "preview capture crossed the game-thread boundary");

    thread.game_thread = true;
    passed &= expect(
        previews.begin(Feature::Paint, First) ==
                PaintPreviewAcquire::Created &&
            previews.begin(Feature::Paint, First) ==
                PaintPreviewAcquire::Reused &&
            runtime.capture_count == 1U &&
            leases.snapshot().feature == Feature::Paint &&
            leases.snapshot().component_identity ==
                First.identity,
        "same-component preview did not retain one original snapshot");
    const auto source =
        previews.source(Feature::Paint, First);
    passed &= expect(
        source &&
            source->dimension == 2U &&
            source->albedo_rgba &&
            source->packed_pbr_rgba,
        "the active lease did not expose immutable composition input");
    passed &= expect(
        previews.apply(Feature::Paint, First, image()).has_value() &&
            runtime.apply_count == 1U &&
            runtime.last_applied_component == First,
        "a valid preview frame was not applied");
    const auto wrong_dimension =
        previews.apply(Feature::Paint, First, image(4U));
    passed &= expect(
        !wrong_dimension &&
            wrong_dimension.error().code ==
                PaintPreviewErrorCode::InvalidImage &&
            runtime.apply_count == 1U,
        "a preview with the wrong captured dimension was applied");

    passed &= expect(
        previews.begin(Feature::ImagePaint, Second) ==
                PaintPreviewAcquire::Replaced &&
            runtime.restore_count == 1U &&
            runtime.restored_components.back() == First &&
            runtime.capture_count == 2U &&
            leases.snapshot().feature == Feature::ImagePaint &&
            leases.snapshot().component_identity ==
                Second.identity,
        "preview replacement did not restore before recapture");

    passed &= expect(
        previews.restore(First) ==
                PaintPreviewRestore::WrongComponent &&
            !previews.source(Feature::Paint, Second) &&
            runtime.restore_count == 1U &&
            previews.restore(Second) ==
                PaintPreviewRestore::Restored &&
            runtime.restore_count == 2U &&
            !leases.snapshot().feature &&
            previews.restore(Second) ==
                PaintPreviewRestore::NoPreview,
        "exact restore ownership or repeated-restore guard failed");

    passed &= expect(
        previews.begin(Feature::Paint, First).has_value(),
        "apply-recovery fixture could not acquire");
    runtime.apply_failure = true;
    const auto recovered =
        previews.apply(Feature::Paint, First, image());
    passed &= expect(
        !recovered &&
            recovered.error().code ==
                PaintPreviewErrorCode::ApplyFailureRecovered &&
            !leases.snapshot().feature,
        "failed preview apply did not restore and release its snapshot");

    runtime.apply_failure = false;
    passed &= expect(
        previews.begin(Feature::Paint, First).has_value(),
        "pending-restore fixture could not acquire");
    runtime.apply_failure = true;
    runtime.restore_failure = true;
    const auto pending =
        previews.apply(Feature::Paint, First, image());
    passed &= expect(
        !pending &&
            pending.error().code ==
                PaintPreviewErrorCode::ApplyFailureRestorePending &&
            leases.snapshot().feature == Feature::Paint,
        "a failed recovery discarded the only restorable snapshot");
    runtime.apply_failure = false;
    runtime.restore_failure = false;
    passed &= expect(
        previews.restore_active() ==
                PaintPreviewRestore::Restored,
        "a retained recovery snapshot could not be retried");

    runtime.malformed_capture = true;
    const auto malformed =
        previews.begin(Feature::Paint, First);
    passed &= expect(
        !malformed &&
            malformed.error().code ==
                PaintPreviewErrorCode::InvalidSnapshot &&
            !leases.snapshot().feature,
        "a malformed runtime snapshot acquired preview ownership");

    runtime.malformed_capture = false;
    passed &= expect(
        previews.begin(Feature::Paint, First).has_value() &&
            previews.expire_invalid_component(First) &&
            !leases.snapshot().feature &&
            previews.restore(First) ==
                PaintPreviewRestore::NoPreview,
        "an invalidated component retained an unusable snapshot");

    if (passed)
    {
        std::cout << "PASS paint_preview_controller\n";
    }
    return passed ? 0 : 1;
}
