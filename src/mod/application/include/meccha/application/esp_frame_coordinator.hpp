#pragma once

#include <meccha/application/game_thread_scheduler.hpp>
#include <meccha/application/runtime_operation_executor.hpp>
#include <meccha/core/esp.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace meccha::application
{
struct CapturedEspFrame
{
    HudFrameIdentity frame_identity{};
    core::EspView view{};
    core::EspViewport viewport{};
    std::vector<core::EspTargetCapture> targets{};
};

class EspGameRuntimePort
{
public:
    EspGameRuntimePort() = default;
    EspGameRuntimePort(const EspGameRuntimePort&) = delete;
    auto operator=(const EspGameRuntimePort&)
        -> EspGameRuntimePort& = delete;
    virtual ~EspGameRuntimePort() = default;

    [[nodiscard]] virtual auto capture()
        -> std::expected<
            CapturedEspFrame,
            RuntimeExecutionError> = 0;

    virtual auto draw(
        const HudFrameIdentity& frame_identity,
        const core::EspPrimitiveFrame& frame)
        -> std::expected<void, RuntimeExecutionError> = 0;
};

enum class EspFramePhase : std::uint8_t
{
    Disabled,
    Active,
    Failed,
};

enum class EspFrameFailureKind : std::uint8_t
{
    Capture,
    InvalidFrameIdentity,
    FrameBuild,
    Draw,
    PortException,
    GenerationOverflow,
};

struct EspFrameFailure
{
    EspFrameFailureKind kind{EspFrameFailureKind::Capture};
    std::optional<RuntimeExecutionError> runtime{};
    std::optional<core::EspFrameError> frame{};
};

struct EspFrameSnapshot
{
    EspFramePhase phase{EspFramePhase::Disabled};
    std::uint64_t generation{};
    std::size_t line_count{};
    std::size_t text_count{};
    core::EspFrameDiagnostics diagnostics{};
    std::optional<EspFrameFailureKind> failure{};

    auto operator==(const EspFrameSnapshot&) const -> bool = default;
};

class EspFrameCoordinator
{
public:
    explicit EspFrameCoordinator(EspGameRuntimePort& runtime);

    [[nodiscard]] auto tick(
        bool enabled,
        const core::EspSettings& settings,
        const HudFrameIdentity& expected_frame)
        -> std::expected<
            EspFrameSnapshot,
            EspFrameFailure>;

    [[nodiscard]] auto snapshot() const -> EspFrameSnapshot;

private:
    auto fail(EspFrameFailure failure)
        -> std::unexpected<EspFrameFailure>;

    EspGameRuntimePort& runtime_;
    EspFrameSnapshot snapshot_{};
};
} // namespace meccha::application
