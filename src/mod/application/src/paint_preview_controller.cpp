#include <meccha/application/paint_preview_controller.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>

namespace meccha::application
{
namespace
{
constexpr std::uint32_t MaximumTextureDimension = 4096U;
constexpr std::uint64_t MaximumChannelBytes =
    64U * 1024U * 1024U;

auto failure(
    PaintPreviewErrorCode code,
    std::optional<RuntimeExecutionError> runtime_error =
        std::nullopt,
    std::optional<RuntimeExecutionError> recovery_error =
        std::nullopt) -> std::unexpected<PaintPreviewError>
{
    return std::unexpected(PaintPreviewError{
        code,
        std::move(runtime_error),
        std::move(recovery_error),
    });
}
} // namespace

PaintPreviewController::PaintPreviewController(
    GameThreadContext& thread_context,
    PaintPreviewRuntimePort& runtime,
    PreviewStateMachine& leases)
    : thread_context_{thread_context},
      runtime_{runtime},
      leases_{leases}
{
}

auto PaintPreviewController::begin(
    Feature feature,
    RuntimeObjectHandle component)
    -> std::expected<PaintPreviewAcquire, PaintPreviewError>
{
    if (!thread_context_.is_game_thread())
    {
        return failure(PaintPreviewErrorCode::WrongThread);
    }
    if (!component.valid())
    {
        return failure(
            PaintPreviewErrorCode::InvalidComponent);
    }
    if (!lease_consistent())
    {
        return failure(PaintPreviewErrorCode::StateFailure);
    }

    const auto lease = leases_.snapshot();
    if (snapshot_ && lease.feature &&
        *lease.feature == feature &&
        snapshot_->component == component)
    {
        return PaintPreviewAcquire::Reused;
    }

    const auto replacing = snapshot_.has_value();
    if (snapshot_)
    {
        const auto restored = runtime_.restore(*snapshot_);
        if (!restored)
        {
            return failure(
                PaintPreviewErrorCode::RestoreFailure,
                restored.error());
        }
        if (!release_restored_lease())
        {
            return failure(
                PaintPreviewErrorCode::StateFailure);
        }
    }

    auto captured = runtime_.capture(component);
    if (!captured)
    {
        return failure(
            PaintPreviewErrorCode::CaptureFailure,
            captured.error());
    }
    if (captured->component != component ||
        !valid_image(captured->original))
    {
        return failure(
            PaintPreviewErrorCode::InvalidSnapshot);
    }

    const auto acquired =
        leases_.acquire(feature, component.identity);
    if (acquired != PreviewAcquireResult::Created)
    {
        return failure(PaintPreviewErrorCode::StateFailure);
    }
    snapshot_ = std::move(*captured);
    return replacing
               ? PaintPreviewAcquire::Replaced
               : PaintPreviewAcquire::Created;
}

auto PaintPreviewController::apply(
    Feature feature,
    RuntimeObjectHandle component,
    const PaintTextureImage& image)
    -> std::expected<void, PaintPreviewError>
{
    if (!thread_context_.is_game_thread())
    {
        return failure(PaintPreviewErrorCode::WrongThread);
    }
    if (!component.valid())
    {
        return failure(
            PaintPreviewErrorCode::InvalidComponent);
    }
    if (!valid_image(image))
    {
        return failure(PaintPreviewErrorCode::InvalidImage);
    }
    if (!lease_consistent() || !snapshot_)
    {
        return failure(PaintPreviewErrorCode::NoPreview);
    }
    if (image.dimension != snapshot_->original.dimension)
    {
        return failure(PaintPreviewErrorCode::InvalidImage);
    }
    const auto lease = leases_.snapshot();
    if (snapshot_->component != component ||
        lease.component_identity != component.identity)
    {
        return failure(
            PaintPreviewErrorCode::InvalidComponent);
    }
    if (!lease.feature || *lease.feature != feature)
    {
        return failure(PaintPreviewErrorCode::WrongFeature);
    }

    const auto applied = runtime_.apply(component, image);
    if (applied)
    {
        return {};
    }

    const auto restored = runtime_.restore(*snapshot_);
    if (!restored)
    {
        return failure(
            PaintPreviewErrorCode::ApplyFailureRestorePending,
            applied.error(),
            restored.error());
    }
    if (!release_restored_lease())
    {
        return failure(PaintPreviewErrorCode::StateFailure);
    }
    return failure(
        PaintPreviewErrorCode::ApplyFailureRecovered,
        applied.error());
}

auto PaintPreviewController::source(
    Feature feature,
    RuntimeObjectHandle component)
    -> std::expected<PaintTextureImage, PaintPreviewError>
{
    if (!thread_context_.is_game_thread())
    {
        return failure(PaintPreviewErrorCode::WrongThread);
    }
    if (!component.valid())
    {
        return failure(
            PaintPreviewErrorCode::InvalidComponent);
    }
    if (!lease_consistent() || !snapshot_)
    {
        return failure(PaintPreviewErrorCode::NoPreview);
    }
    const auto lease = leases_.snapshot();
    if (snapshot_->component != component ||
        lease.component_identity != component.identity)
    {
        return failure(
            PaintPreviewErrorCode::InvalidComponent);
    }
    if (!lease.feature || *lease.feature != feature)
    {
        return failure(PaintPreviewErrorCode::WrongFeature);
    }
    return snapshot_->original;
}

auto PaintPreviewController::restore(
    RuntimeObjectHandle component)
    -> std::expected<PaintPreviewRestore, PaintPreviewError>
{
    if (!thread_context_.is_game_thread())
    {
        return failure(PaintPreviewErrorCode::WrongThread);
    }
    if (!lease_consistent())
    {
        return failure(PaintPreviewErrorCode::StateFailure);
    }
    if (!snapshot_)
    {
        return PaintPreviewRestore::NoPreview;
    }
    if (!component.valid() ||
        snapshot_->component != component)
    {
        return PaintPreviewRestore::WrongComponent;
    }

    const auto restored = runtime_.restore(*snapshot_);
    if (!restored)
    {
        return failure(
            PaintPreviewErrorCode::RestoreFailure,
            restored.error());
    }
    if (!release_restored_lease())
    {
        return failure(PaintPreviewErrorCode::StateFailure);
    }
    return PaintPreviewRestore::Restored;
}

auto PaintPreviewController::restore_active()
    -> std::expected<PaintPreviewRestore, PaintPreviewError>
{
    if (!thread_context_.is_game_thread())
    {
        return failure(PaintPreviewErrorCode::WrongThread);
    }
    if (!lease_consistent())
    {
        return failure(PaintPreviewErrorCode::StateFailure);
    }
    if (!snapshot_)
    {
        return PaintPreviewRestore::NoPreview;
    }
    return restore(snapshot_->component);
}

auto PaintPreviewController::expire_invalid_component(
    RuntimeObjectHandle component) -> bool
{
    if (!snapshot_ || snapshot_->component != component)
    {
        return false;
    }
    static_cast<void>(
        leases_.invalidate_component(component.identity));
    snapshot_.reset();
    return true;
}

auto PaintPreviewController::valid_image(
    const PaintTextureImage& image) const -> bool
{
    if (image.dimension == 0U ||
        image.dimension > MaximumTextureDimension ||
        image.albedo_rgba == nullptr ||
        image.packed_pbr_rgba == nullptr)
    {
        return false;
    }
    const auto dimension =
        static_cast<std::uint64_t>(image.dimension);
    if (dimension >
        std::numeric_limits<std::uint64_t>::max() / dimension)
    {
        return false;
    }
    const auto pixels = dimension * dimension;
    if (pixels >
        std::numeric_limits<std::uint64_t>::max() / 4U)
    {
        return false;
    }
    const auto expected = pixels * 4U;
    return expected <= MaximumChannelBytes &&
           image.albedo_rgba->size() == expected &&
           image.packed_pbr_rgba->size() == expected;
}

auto PaintPreviewController::lease_consistent() const -> bool
{
    const auto lease = leases_.snapshot();
    if (!snapshot_)
    {
        return !lease.feature &&
               lease.component_identity == 0U;
    }
    return lease.feature.has_value() &&
           lease.component_identity ==
               snapshot_->component.identity;
}

auto PaintPreviewController::release_restored_lease() -> bool
{
    if (!snapshot_)
    {
        return false;
    }
    const auto identity = snapshot_->component.identity;
    const auto restored = leases_.restore(identity);
    if (restored != PreviewRestoreResult::Restored)
    {
        static_cast<void>(
            leases_.invalidate_component(identity));
        snapshot_.reset();
        return false;
    }
    snapshot_.reset();
    return true;
}
} // namespace meccha::application
