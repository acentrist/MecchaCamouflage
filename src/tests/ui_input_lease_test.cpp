#include <meccha/ui/input_lease.hpp>

#include <expected>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_input_lease: " << message << '\n';
    }
    return condition;
}

class FakeInputPort final : public meccha::ui::InputLeasePort
{
public:
    meccha::ui::RuntimeInputState initial{
        true,
        false,
        true,
        meccha::ui::RuntimeInputMode::GameAndUi,
    };
    std::vector<std::string> calls{};
    int apply_failures{};
    int restore_failures{};
    bool throw_on_capture{};

    auto capture() -> std::expected<
        meccha::ui::RuntimeInputState,
        meccha::ui::InputPortError> override
    {
        calls.emplace_back("capture");
        if (throw_on_capture)
        {
            throw std::runtime_error{"capture failure"};
        }
        return initial;
    }

    auto apply_panel_controls()
        -> std::expected<void, meccha::ui::InputPortError> override
    {
        calls.emplace_back("apply");
        if (apply_failures-- > 0)
        {
            return std::unexpected(
                meccha::ui::InputPortError{"apply failed"});
        }
        return {};
    }

    auto restore(
        const meccha::ui::RuntimeInputState& state)
        -> std::expected<void, meccha::ui::InputPortError> override
    {
        calls.emplace_back("restore");
        if (restore_failures-- > 0)
        {
            return std::unexpected(
                meccha::ui::InputPortError{"restore failed"});
        }
        if (state != initial)
        {
            return std::unexpected(
                meccha::ui::InputPortError{"wrong restore state"});
        }
        return {};
    }
};
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    FakeInputPort port{};
    InputLeaseController lease{};

    const auto opened = lease.reconcile(true, port);
    passed &= expect(
        opened &&
            opened->phase == InputLeasePhase::Held &&
            opened->previous == port.initial &&
            port.calls ==
                std::vector<std::string>{"capture", "apply"},
        "open did not capture and hold the exact previous state");

    passed &= expect(
        lease.reconcile(true, port) &&
            port.calls.size() == 2U,
        "stable open repeated runtime mutations");

    const auto closed = lease.reconcile(false, port);
    passed &= expect(
        closed &&
            closed->phase == InputLeasePhase::Released &&
            !closed->previous &&
            port.calls.back() == "restore",
        "close did not restore and release the exact lease");

    FakeInputPort apply_failure{};
    apply_failure.apply_failures = 1;
    InputLeaseController apply_lease{};
    const auto failed_open =
        apply_lease.reconcile(true, apply_failure);
    passed &= expect(
        !failed_open &&
            failed_open.error().kind ==
                InputLeaseFailureKind::Apply &&
            apply_lease.snapshot().phase ==
                InputLeasePhase::Released &&
            apply_failure.calls ==
                std::vector<std::string>{
                    "capture",
                    "apply",
                    "restore",
                },
        "failed apply did not roll back before returning");

    FakeInputPort restore_failure{};
    restore_failure.restore_failures = 1;
    InputLeaseController retry_lease{};
    passed &= expect(
        retry_lease.reconcile(true, restore_failure).has_value(),
        "retry fixture did not acquire");
    const auto first_close =
        retry_lease.reconcile(false, restore_failure);
    passed &= expect(
        !first_close &&
            first_close.error().kind ==
                InputLeaseFailureKind::Restore &&
            retry_lease.snapshot().phase ==
                InputLeasePhase::Restoring &&
            retry_lease.snapshot().previous ==
                restore_failure.initial,
        "failed restore discarded the state needed for retry");
    const auto retried =
        retry_lease.reconcile(false, restore_failure);
    passed &= expect(
        retried &&
            retried->phase == InputLeasePhase::Released &&
            !retried->previous,
        "failed restore did not retry to exact release");

    FakeInputPort rollback_failure{};
    rollback_failure.apply_failures = 1;
    rollback_failure.restore_failures = 1;
    InputLeaseController rollback_lease{};
    const auto failed_rollback =
        rollback_lease.reconcile(true, rollback_failure);
    passed &= expect(
        !failed_rollback &&
            failed_rollback.error().kind ==
                InputLeaseFailureKind::Rollback &&
            rollback_lease.snapshot().phase ==
                InputLeasePhase::Restoring &&
            rollback_lease.snapshot().previous ==
                rollback_failure.initial &&
            rollback_lease.reconcile(
                false,
                rollback_failure).has_value(),
        "failed apply rollback did not retain and retry exact state");

    FakeInputPort invalid_capture{};
    invalid_capture.initial.mode =
        static_cast<RuntimeInputMode>(0xFFU);
    InputLeaseController invalid_lease{};
    const auto invalid_state =
        invalid_lease.reconcile(true, invalid_capture);
    passed &= expect(
        !invalid_state &&
            invalid_state.error().kind ==
                InputLeaseFailureKind::InvalidCapturedState &&
            invalid_capture.calls ==
                std::vector<std::string>{"capture"},
        "invalid captured state reached the runtime apply path");

    FakeInputPort shutdown_port{};
    InputLeaseController shutdown_lease{};
    passed &= expect(
        shutdown_lease.reconcile(true, shutdown_port) &&
            shutdown_lease.shutdown(shutdown_port) &&
            shutdown_lease.snapshot().phase ==
                InputLeasePhase::Released,
        "shutdown did not restore a held lease");

    FakeInputPort throwing{};
    throwing.throw_on_capture = true;
    InputLeaseController exception_lease{};
    const auto exception =
        exception_lease.reconcile(true, throwing);
    passed &= expect(
        !exception &&
            exception.error().kind ==
                InputLeaseFailureKind::PortException &&
            exception_lease.snapshot().phase ==
                InputLeasePhase::Released,
        "port exception escaped or mutated the lease");

    if (passed)
    {
        std::cout << "PASS ui_input_lease\n";
    }
    return passed ? 0 : 1;
}
