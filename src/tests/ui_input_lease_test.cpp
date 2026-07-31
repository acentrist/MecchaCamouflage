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
        17U,
        true,
        false,
        true,
        meccha::ui::RuntimeInputModeHandling::PreserveUnchanged,
    };
    std::uint64_t active_owner{initial.owner_identity};
    std::vector<std::string> calls{};
    std::vector<meccha::ui::RuntimeInputState> restored{};
    int apply_failures{};
    int restore_failures{};
    int owner_failures{};
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

    auto current_owner() -> std::expected<
        std::uint64_t,
        meccha::ui::InputPortError> override
    {
        calls.emplace_back("current_owner");
        if (owner_failures-- > 0)
        {
            return std::unexpected(
                meccha::ui::InputPortError{
                    "owner validation failed"});
        }
        return active_owner;
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
        restored.push_back(state);
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
            port.calls ==
                std::vector<std::string>{
                    "capture",
                    "apply",
                    "current_owner",
                },
        "stable open did not validate the exact runtime owner");

    const auto first_owner = port.initial;
    port.initial.owner_identity = 29U;
    port.active_owner = 29U;
    const auto rebound = lease.reconcile(true, port);
    passed &= expect(
        rebound &&
            rebound->phase == InputLeasePhase::Held &&
            rebound->previous == port.initial &&
            port.restored ==
                std::vector<RuntimeInputState>{first_owner} &&
            std::vector<std::string>(
                port.calls.end() - 4,
                port.calls.end()) ==
                std::vector<std::string>{
                    "current_owner",
                    "restore",
                    "capture",
                    "apply",
                },
        "owner replacement did not restore before reacquiring");

    const auto closed = lease.reconcile(false, port);
    passed &= expect(
        closed &&
            closed->phase == InputLeasePhase::Released &&
            !closed->previous &&
            port.calls.back() == "restore",
        "close did not restore and release the exact lease");

    FakeInputPort owner_failure{};
    InputLeaseController owner_lease{};
    passed &= expect(
        owner_lease.reconcile(true, owner_failure).has_value(),
        "owner-validation fixture did not acquire");
    owner_failure.owner_failures = 1;
    const auto failed_owner =
        owner_lease.reconcile(true, owner_failure);
    passed &= expect(
        !failed_owner &&
            failed_owner.error().kind ==
                InputLeaseFailureKind::OwnerValidation &&
            owner_lease.snapshot().phase ==
                InputLeasePhase::Held &&
            owner_lease.snapshot().previous ==
                owner_failure.initial &&
            owner_lease.reconcile(true, owner_failure)
                .has_value(),
        "owner-validation failure discarded or mutated the lease");

    FakeInputPort replacement_restore_failure{};
    InputLeaseController replacement_lease{};
    passed &= expect(
        replacement_lease
            .reconcile(true, replacement_restore_failure)
            .has_value(),
        "owner-replacement fixture did not acquire");
    replacement_restore_failure.restore_failures = 1;
    replacement_restore_failure.initial.owner_identity = 41U;
    replacement_restore_failure.active_owner = 41U;
    const auto failed_replacement =
        replacement_lease.reconcile(
            true,
            replacement_restore_failure);
    passed &= expect(
        !failed_replacement &&
            failed_replacement.error().kind ==
                InputLeaseFailureKind::Restore &&
            replacement_lease.snapshot().phase ==
                InputLeasePhase::Restoring,
        "failed owner replacement did not retain restore state");
    const auto retried_replacement =
        replacement_lease.reconcile(
            true,
            replacement_restore_failure);
    passed &= expect(
        retried_replacement &&
            retried_replacement->phase ==
                InputLeasePhase::Held &&
            retried_replacement->previous ==
                replacement_restore_failure.initial,
        "owner replacement did not reacquire after restore retry");

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
    invalid_capture.initial.input_mode =
        static_cast<RuntimeInputModeHandling>(0xFFU);
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
