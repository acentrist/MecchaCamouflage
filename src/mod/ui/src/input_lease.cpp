#include <meccha/ui/input_lease.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace meccha::ui
{
namespace
{
inline constexpr std::size_t MaximumInputFailureBytes = 512U;

auto bounded_detail(std::string detail) -> std::string
{
    if (detail.size() > MaximumInputFailureBytes)
    {
        detail.resize(MaximumInputFailureBytes);
    }
    return detail;
}

auto valid(const RuntimeInputState& state) -> bool
{
    switch (state.mode)
    {
    case RuntimeInputMode::GameOnly:
    case RuntimeInputMode::UiOnly:
    case RuntimeInputMode::GameAndUi:
        return true;
    }
    return false;
}
} // namespace

auto InputLeaseController::fail(
    InputLeaseFailureKind kind,
    std::string detail)
    -> std::unexpected<InputLeaseFailure>
{
    snapshot_.last_failure = kind;
    return std::unexpected(InputLeaseFailure{
        kind,
        bounded_detail(std::move(detail)),
    });
}

auto InputLeaseController::transition(InputLeasePhase phase)
    -> std::expected<void, InputLeaseFailure>
{
    if (snapshot_.generation ==
        std::numeric_limits<std::uint64_t>::max())
    {
        return fail(
            InputLeaseFailureKind::GenerationOverflow,
            "Input lease generation overflowed.");
    }
    ++snapshot_.generation;
    snapshot_.phase = phase;
    if (phase == InputLeasePhase::Released)
    {
        snapshot_.previous.reset();
    }
    return {};
}

auto InputLeaseController::acquire(InputLeasePort& port)
    -> std::expected<InputLeaseSnapshot, InputLeaseFailure>
{
    std::expected<RuntimeInputState, InputPortError> captured;
    try
    {
        captured = port.capture();
    }
    catch (const std::exception& error)
    {
        return fail(
            InputLeaseFailureKind::PortException,
            error.what());
    }
    catch (...)
    {
        return fail(
            InputLeaseFailureKind::PortException,
            "Input capture threw an unknown exception.");
    }
    if (!captured)
    {
        return fail(
            InputLeaseFailureKind::Capture,
            captured.error().detail);
    }
    if (!valid(*captured))
    {
        return fail(
            InputLeaseFailureKind::InvalidCapturedState,
            "Captured input mode is invalid.");
    }
    snapshot_.previous = *captured;

    std::expected<void, InputPortError> applied;
    try
    {
        applied = port.apply_panel_controls();
    }
    catch (const std::exception& error)
    {
        applied = std::unexpected(InputPortError{error.what()});
    }
    catch (...)
    {
        applied = std::unexpected(InputPortError{
            "Input apply threw an unknown exception."});
    }
    if (!applied)
    {
        std::expected<void, InputPortError> rolled_back;
        try
        {
            rolled_back = port.restore(*snapshot_.previous);
        }
        catch (const std::exception& error)
        {
            rolled_back =
                std::unexpected(InputPortError{error.what()});
        }
        catch (...)
        {
            rolled_back = std::unexpected(InputPortError{
                "Input rollback threw an unknown exception."});
        }
        if (!rolled_back)
        {
            if (const auto changed =
                    transition(InputLeasePhase::Restoring);
                !changed)
            {
                return std::unexpected(changed.error());
            }
            return fail(
                InputLeaseFailureKind::Rollback,
                rolled_back.error().detail);
        }
        if (const auto changed =
                transition(InputLeasePhase::Released);
            !changed)
        {
            return std::unexpected(changed.error());
        }
        return fail(
            InputLeaseFailureKind::Apply,
            applied.error().detail);
    }

    if (const auto changed = transition(InputLeasePhase::Held);
        !changed)
    {
        return std::unexpected(changed.error());
    }
    snapshot_.last_failure.reset();
    return snapshot_;
}

auto InputLeaseController::release(
    InputLeasePort& port,
    InputLeaseFailureKind failure_kind)
    -> std::expected<InputLeaseSnapshot, InputLeaseFailure>
{
    if (!snapshot_.previous)
    {
        if (const auto changed =
                transition(InputLeasePhase::Released);
            !changed)
        {
            return std::unexpected(changed.error());
        }
        snapshot_.last_failure.reset();
        return snapshot_;
    }

    std::expected<void, InputPortError> restored;
    try
    {
        restored = port.restore(*snapshot_.previous);
    }
    catch (const std::exception& error)
    {
        restored = std::unexpected(InputPortError{error.what()});
        failure_kind = InputLeaseFailureKind::PortException;
    }
    catch (...)
    {
        restored = std::unexpected(InputPortError{
            "Input restore threw an unknown exception."});
        failure_kind = InputLeaseFailureKind::PortException;
    }
    if (!restored)
    {
        if (snapshot_.phase != InputLeasePhase::Restoring)
        {
            if (const auto changed =
                    transition(InputLeasePhase::Restoring);
                !changed)
            {
                return std::unexpected(changed.error());
            }
        }
        return fail(failure_kind, restored.error().detail);
    }

    if (const auto changed = transition(InputLeasePhase::Released);
        !changed)
    {
        return std::unexpected(changed.error());
    }
    snapshot_.last_failure.reset();
    return snapshot_;
}

auto InputLeaseController::reconcile(
    bool panel_open,
    InputLeasePort& port)
    -> std::expected<InputLeaseSnapshot, InputLeaseFailure>
{
    if (snapshot_.phase == InputLeasePhase::Restoring)
    {
        return release(port, InputLeaseFailureKind::Restore);
    }
    if (panel_open &&
        snapshot_.phase == InputLeasePhase::Released)
    {
        return acquire(port);
    }
    if (!panel_open &&
        snapshot_.phase == InputLeasePhase::Held)
    {
        return release(port, InputLeaseFailureKind::Restore);
    }
    return snapshot_;
}

auto InputLeaseController::shutdown(InputLeasePort& port)
    -> std::expected<InputLeaseSnapshot, InputLeaseFailure>
{
    if (snapshot_.phase == InputLeasePhase::Released)
    {
        return snapshot_;
    }
    return release(port, InputLeaseFailureKind::Restore);
}

auto InputLeaseController::snapshot() const -> InputLeaseSnapshot
{
    return snapshot_;
}
} // namespace meccha::ui
