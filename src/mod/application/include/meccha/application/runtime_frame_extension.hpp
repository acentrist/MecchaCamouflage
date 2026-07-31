#pragma once

#include <meccha/application/compatibility.hpp>
#include <meccha/application/game_thread_scheduler.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace meccha::application
{
enum class RuntimeFrameExtensionStage : std::uint8_t
{
    Frame,
    Shutdown,
};

struct RuntimeFrameExtensionError
{
    RuntimeFrameExtensionStage stage{};
    std::optional<CompatibilityFailure> compatibility_failure{};
    std::string message_key{"error.operation.failed"};

    auto operator==(const RuntimeFrameExtensionError&) const
        -> bool = default;
};

class RuntimeFrameExtensionPort
{
public:
    RuntimeFrameExtensionPort() = default;
    RuntimeFrameExtensionPort(
        const RuntimeFrameExtensionPort&) = delete;
    auto operator=(const RuntimeFrameExtensionPort&)
        -> RuntimeFrameExtensionPort& = delete;
    virtual ~RuntimeFrameExtensionPort() = default;

    [[nodiscard]] virtual auto on_hud_frame(
        const HudFrameIdentity& identity) noexcept
        -> std::expected<void, RuntimeFrameExtensionError> = 0;

    [[nodiscard]] virtual auto restore_and_stop() noexcept
        -> std::expected<void, RuntimeFrameExtensionError> = 0;
};
} // namespace meccha::application
