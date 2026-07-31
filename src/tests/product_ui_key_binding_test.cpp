#include <meccha/product_ui/product_ui_key_binding.hpp>

#include <cstddef>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

class FakeRegistrar final
    : public meccha::product_ui::
          ProductUiFunctionKeyRegistrationPort
{
public:
    auto register_function_key(
        meccha::core::FunctionKey key,
        meccha::product_ui::ProductUiFunctionKeyCallback callback)
        -> std::expected<
            void,
            meccha::product_ui::
                ProductUiFunctionKeyRegistrationError> override
    {
        if (throw_at && calls == *throw_at)
        {
            throw std::runtime_error{"injected registration exception"};
        }
        if (fail_at && calls == *fail_at)
        {
            return std::unexpected(
                meccha::product_ui::
                    ProductUiFunctionKeyRegistrationError{
                        "injected registration failure",
                    });
        }
        ++calls;
        keys.push_back(key);
        callbacks.push_back(std::move(callback));
        return {};
    }

    std::vector<meccha::core::FunctionKey> keys{};
    std::vector<
        meccha::product_ui::ProductUiFunctionKeyCallback>
        callbacks{};
    std::optional<std::size_t> fail_at{};
    std::optional<std::size_t> throw_at{};
    std::size_t calls{};
};
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto passed = true;
    auto queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto registrar = FakeRegistrar{};
    auto binding =
        product_ui::ProductUiFunctionKeyBinding{queue};

    const auto started = binding.start(registrar);
    const auto active = binding.snapshot();
    passed &= expect(
        started && registrar.calls == 24U &&
            registrar.keys.front() == core::FunctionKey::F1 &&
            registrar.keys.back() == core::FunctionKey::F24 &&
            active.phase ==
                product_ui::ProductUiKeyBindingPhase::Active &&
            active.registered_keys == 24U,
        "the binding did not register F1-F24 exactly once");

    registrar.callbacks.at(8U)();
    registrar.callbacks.at(0U)();
    registrar.callbacks.at(23U)();
    const auto batch = queue->drain();
    passed &= expect(
        batch && batch->function_keys.size() == 6U &&
            batch->function_keys.at(0U).key ==
                core::FunctionKey::F9 &&
            batch->function_keys.at(2U).key ==
                core::FunctionKey::F1 &&
            batch->function_keys.at(4U).key ==
                core::FunctionKey::F24,
        "registered callbacks did not preserve physical key order");

    const auto duplicate = binding.start(registrar);
    passed &= expect(
        !duplicate &&
            duplicate.error().code ==
                product_ui::ProductUiKeyBindingErrorCode::
                    InvalidState &&
            registrar.calls == 24U,
        "a second start duplicated key registration");

    binding.stop();
    registrar.callbacks.at(8U)();
    passed &= expect(
        binding.snapshot().phase ==
                product_ui::ProductUiKeyBindingPhase::Stopped &&
            queue->drain() ==
                std::unexpected(
                    product_ui::ProductUiInputDrainError::
                        Stopped),
        "stopped callbacks remained active");

    auto failed_queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto failing_registrar = FakeRegistrar{};
    failing_registrar.fail_at = 5U;
    auto failed_binding =
        product_ui::ProductUiFunctionKeyBinding{failed_queue};
    const auto failed =
        failed_binding.start(failing_registrar);
    passed &= expect(
        !failed &&
            failed.error().code ==
                product_ui::ProductUiKeyBindingErrorCode::
                    Registration &&
            failed.error().key == core::FunctionKey::F6 &&
            failed_binding.snapshot().phase ==
                product_ui::ProductUiKeyBindingPhase::Failed &&
            failed_binding.snapshot().registered_keys == 5U,
        "partial registration did not fail on the exact key");
    failing_registrar.callbacks.front()();
    passed &= expect(
        failed_queue->drain() ==
            std::unexpected(
                product_ui::ProductUiInputDrainError::Stopped),
        "a partially registered callback remained active");

    auto throwing_queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto throwing_registrar = FakeRegistrar{};
    throwing_registrar.throw_at = 0U;
    auto throwing_binding =
        product_ui::ProductUiFunctionKeyBinding{throwing_queue};
    const auto threw =
        throwing_binding.start(throwing_registrar);
    passed &= expect(
        !threw &&
            threw.error().code ==
                product_ui::ProductUiKeyBindingErrorCode::
                    Unexpected &&
            threw.error().key == core::FunctionKey::F1 &&
            throwing_queue->drain() ==
                std::unexpected(
                    product_ui::ProductUiInputDrainError::
                        Stopped),
        "a registration exception escaped or left callbacks active");

    auto missing_registrar = FakeRegistrar{};
    auto missing_binding =
        product_ui::ProductUiFunctionKeyBinding{nullptr};
    const auto missing = missing_binding.start(missing_registrar);
    passed &= expect(
        !missing &&
            missing.error().code ==
                product_ui::ProductUiKeyBindingErrorCode::
                    InvalidQueue &&
            missing_registrar.calls == 0U,
        "a missing callback queue reached registration");

    auto retained_queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto retained_callback =
        product_ui::ProductUiFunctionKeyCallback{};
    {
        auto retained_registrar = FakeRegistrar{};
        auto retained_binding =
            product_ui::ProductUiFunctionKeyBinding{
                retained_queue};
        passed &= expect(
            retained_binding.start(retained_registrar).has_value(),
            "the lifetime binding did not start");
        retained_callback =
            retained_registrar.callbacks.front();
    }
    retained_callback();
    passed &= expect(
        retained_queue->drain() ==
            std::unexpected(
                product_ui::ProductUiInputDrainError::Stopped),
        "a callback outlived its destroyed binding unsafely");

    return passed ? 0 : 1;
}
