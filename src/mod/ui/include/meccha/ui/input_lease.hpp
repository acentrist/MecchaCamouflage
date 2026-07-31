#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace meccha::ui
{
enum class RuntimeInputModeHandling : std::uint8_t
{
    PreserveUnchanged,
};

struct RuntimeInputState
{
    std::uint64_t owner_identity{};
    bool cursor_visible{};
    bool look_input_ignored{};
    bool movement_input_ignored{};
    RuntimeInputModeHandling input_mode{
        RuntimeInputModeHandling::PreserveUnchanged};

    auto operator==(const RuntimeInputState&) const -> bool = default;
};

struct InputPortError
{
    std::string detail{};

    auto operator==(const InputPortError&) const -> bool = default;
};

class InputLeasePort
{
public:
    InputLeasePort() = default;
    InputLeasePort(const InputLeasePort&) = delete;
    auto operator=(const InputLeasePort&) -> InputLeasePort& = delete;
    virtual ~InputLeasePort() = default;

    [[nodiscard]] virtual auto capture()
        -> std::expected<RuntimeInputState, InputPortError> = 0;
    [[nodiscard]] virtual auto apply_panel_controls()
        -> std::expected<void, InputPortError> = 0;
    [[nodiscard]] virtual auto current_owner()
        -> std::expected<std::uint64_t, InputPortError> = 0;
    [[nodiscard]] virtual auto restore(
        const RuntimeInputState& state)
        -> std::expected<void, InputPortError> = 0;
};

enum class InputLeasePhase : std::uint8_t
{
    Released,
    Held,
    Restoring,
};

enum class InputLeaseFailureKind : std::uint8_t
{
    Capture,
    InvalidCapturedState,
    OwnerValidation,
    Apply,
    Rollback,
    Restore,
    PortException,
    GenerationOverflow,
};

struct InputLeaseFailure
{
    InputLeaseFailureKind kind{InputLeaseFailureKind::Capture};
    std::string detail{};

    auto operator==(const InputLeaseFailure&) const -> bool = default;
};

struct InputLeaseSnapshot
{
    InputLeasePhase phase{InputLeasePhase::Released};
    std::uint64_t generation{};
    std::optional<RuntimeInputState> previous{};
    std::optional<InputLeaseFailureKind> last_failure{};

    auto operator==(const InputLeaseSnapshot&) const -> bool = default;
};

class InputLeaseController
{
public:
    [[nodiscard]] auto reconcile(
        bool panel_open,
        InputLeasePort& port)
        -> std::expected<InputLeaseSnapshot, InputLeaseFailure>;

    [[nodiscard]] auto shutdown(InputLeasePort& port)
        -> std::expected<InputLeaseSnapshot, InputLeaseFailure>;

    [[nodiscard]] auto snapshot() const -> InputLeaseSnapshot;

private:
    [[nodiscard]] auto acquire(InputLeasePort& port)
        -> std::expected<InputLeaseSnapshot, InputLeaseFailure>;
    [[nodiscard]] auto release(
        InputLeasePort& port,
        InputLeaseFailureKind failure_kind)
        -> std::expected<InputLeaseSnapshot, InputLeaseFailure>;
    [[nodiscard]] auto transition(InputLeasePhase phase)
        -> std::expected<void, InputLeaseFailure>;
    [[nodiscard]] auto fail(
        InputLeaseFailureKind kind,
        std::string detail)
        -> std::unexpected<InputLeaseFailure>;

    InputLeaseSnapshot snapshot_{};
};
} // namespace meccha::ui
