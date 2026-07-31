#include <meccha/application/esp_frame_coordinator.hpp>

#include <limits>
#include <utility>

namespace meccha::application
{
EspFrameCoordinator::EspFrameCoordinator(
    EspGameRuntimePort& runtime)
    : runtime_{runtime}
{
}

auto EspFrameCoordinator::tick(
    bool enabled,
    const core::EspSettings& settings,
    const HudFrameIdentity& expected_frame)
    -> std::expected<EspFrameSnapshot, EspFrameFailure>
{
    if (!enabled || !settings.enabled)
    {
        snapshot_.phase = EspFramePhase::Disabled;
        snapshot_.line_count = 0U;
        snapshot_.text_count = 0U;
        snapshot_.diagnostics = {};
        snapshot_.failure.reset();
        return snapshot_;
    }
    if (snapshot_.generation ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return fail(EspFrameFailure{
            EspFrameFailureKind::GenerationOverflow,
        });
    }

    try
    {
        auto captured = runtime_.capture_esp_frame();
        if (!captured)
        {
            return fail(EspFrameFailure{
                EspFrameFailureKind::Capture,
                captured.error(),
            });
        }
        if (!expected_frame.valid() ||
            captured->frame_identity != expected_frame)
        {
            return fail(EspFrameFailure{
                EspFrameFailureKind::InvalidFrameIdentity,
            });
        }

        auto frame = core::build_esp_primitive_frame(
            settings,
            captured->view,
            captured->viewport,
            captured->targets);
        if (!frame)
        {
            return fail(EspFrameFailure{
                EspFrameFailureKind::FrameBuild,
                std::nullopt,
                frame.error(),
            });
        }
        const auto drawn = runtime_.draw_esp_frame(
            expected_frame,
            *frame);
        if (!drawn)
        {
            return fail(EspFrameFailure{
                EspFrameFailureKind::Draw,
                drawn.error(),
            });
        }

        snapshot_ = EspFrameSnapshot{
            EspFramePhase::Active,
            snapshot_.generation + 1U,
            frame->lines.size(),
            frame->texts.size(),
            frame->diagnostics,
            std::nullopt,
        };
        return snapshot_;
    }
    catch (...)
    {
        return fail(EspFrameFailure{
            EspFrameFailureKind::PortException,
        });
    }
}

auto EspFrameCoordinator::snapshot() const -> EspFrameSnapshot
{
    return snapshot_;
}

auto EspFrameCoordinator::fail(EspFrameFailure failure)
    -> std::unexpected<EspFrameFailure>
{
    snapshot_.phase = EspFramePhase::Failed;
    snapshot_.line_count = 0U;
    snapshot_.text_count = 0U;
    snapshot_.diagnostics = {};
    snapshot_.failure = failure.kind;
    return std::unexpected(std::move(failure));
}
} // namespace meccha::application
