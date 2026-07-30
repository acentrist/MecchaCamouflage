#include <meccha/application/esp_frame_coordinator.hpp>

#include <expected>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace
{
using namespace meccha::application;
namespace core = meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL esp_frame_coordinator: "
                  << message << '\n';
    }
    return condition;
}

class FakeRuntime final : public EspGameRuntimePort
{
public:
    auto capture()
        -> std::expected<
            CapturedEspFrame,
            RuntimeExecutionError> override
    {
        ++capture_count;
        if (throw_capture)
        {
            throw std::runtime_error{"injected capture exception"};
        }
        if (capture_failure)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
            });
        }
        return captured;
    }

    auto draw(
        const HudFrameIdentity& frame_identity,
        const core::EspPrimitiveFrame& frame)
        -> std::expected<void, RuntimeExecutionError> override
    {
        ++draw_count;
        drawn_identity = frame_identity;
        drawn_frame = frame;
        if (draw_failure)
        {
            return std::unexpected(RuntimeExecutionError{
                RuntimeExecutionErrorCode::OperationFailure,
            });
        }
        return {};
    }

    CapturedEspFrame captured{};
    core::EspPrimitiveFrame drawn_frame{};
    HudFrameIdentity drawn_identity{};
    std::size_t capture_count{};
    std::size_t draw_count{};
    bool capture_failure{};
    bool draw_failure{};
    bool throw_capture{};
};
} // namespace

auto main() -> int
{
    using namespace meccha;
    using namespace meccha::application;

    auto passed = true;
    constexpr auto Frame =
        HudFrameIdentity{1U, 2U, 3U, 4U};
    auto runtime = FakeRuntime{};
    runtime.captured = CapturedEspFrame{
        Frame,
        {},
        {1920.0, 1080.0},
        {core::EspTargetCapture{
            10U,
            20U,
            core::EspRole::Hider,
            core::EspRole::Unknown,
            "Player",
            core::EspWorldPoint{100.0, 0.0, 0.0},
        }},
    };
    auto coordinator = EspFrameCoordinator{runtime};

    const auto disabled = coordinator.tick(
        false,
        core::EspSettings{},
        Frame);
    passed &= expect(
        disabled &&
            disabled->phase == EspFramePhase::Disabled &&
            runtime.capture_count == 0U &&
            runtime.draw_count == 0U,
        "disabled ESP touched the runtime");

    const auto active = coordinator.tick(
        true,
        core::EspSettings{},
        Frame);
    passed &= expect(
        active &&
            active->phase == EspFramePhase::Active &&
            active->generation == 1U &&
            active->line_count == 1U &&
            active->text_count == 1U &&
            runtime.capture_count == 1U &&
            runtime.draw_count == 1U &&
            runtime.drawn_identity == Frame &&
            runtime.drawn_frame.lines.size() == 1U,
        "capture/build/draw did not publish one frame-scoped result");

    runtime.captured.frame_identity =
        HudFrameIdentity{9U, 2U, 3U, 4U};
    const auto stale = coordinator.tick(
        true,
        core::EspSettings{},
        Frame);
    passed &= expect(
        !stale &&
            stale.error().kind ==
                EspFrameFailureKind::InvalidFrameIdentity &&
            coordinator.snapshot().phase ==
                EspFramePhase::Failed &&
            coordinator.snapshot().generation == 1U &&
            runtime.draw_count == 1U,
        "a stale Canvas identity reached draw");

    runtime.captured.frame_identity = Frame;
    runtime.draw_failure = true;
    const auto failed_draw = coordinator.tick(
        true,
        core::EspSettings{},
        Frame);
    passed &= expect(
        !failed_draw &&
            failed_draw.error().kind ==
                EspFrameFailureKind::Draw &&
            failed_draw.error().runtime &&
            runtime.draw_count == 2U,
        "a runtime draw failure lost typed context");

    runtime.draw_failure = false;
    runtime.throw_capture = true;
    const auto exception = coordinator.tick(
        true,
        core::EspSettings{},
        Frame);
    passed &= expect(
        !exception &&
            exception.error().kind ==
                EspFrameFailureKind::PortException,
        "a runtime exception escaped the ESP frame boundary");

    if (passed)
    {
        std::cout << "PASS esp_frame_coordinator\n";
    }
    return passed ? 0 : 1;
}
