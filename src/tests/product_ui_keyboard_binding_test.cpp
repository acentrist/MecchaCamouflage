#include <meccha/product_ui/product_ui_keyboard_binding.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
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

struct RegisteredKey
{
    meccha::product_ui::ProductUiKeyboardKey key{};
    meccha::product_ui::ProductUiKeyboardModifiers modifiers{};
    meccha::product_ui::ProductUiKeyboardCallback callback{};
};

class FakeRegistrar final
    : public meccha::product_ui::
          ProductUiKeyboardRegistrationPort
{
public:
    auto register_keyboard_key(
        meccha::product_ui::ProductUiKeyboardKey key,
        meccha::product_ui::ProductUiKeyboardModifiers modifiers,
        meccha::product_ui::ProductUiKeyboardCallback callback)
        -> std::expected<
            void,
            meccha::product_ui::
                ProductUiKeyboardRegistrationError> override
    {
        if (throw_at && registrations.size() == *throw_at)
        {
            throw std::runtime_error{"injected registration exception"};
        }
        if (fail_at && registrations.size() == *fail_at)
        {
            return std::unexpected(
                meccha::product_ui::
                    ProductUiKeyboardRegistrationError{
                        "injected registration failure",
                    });
        }
        registrations.push_back(RegisteredKey{
            key,
            modifiers,
            std::move(callback),
        });
        return {};
    }

    auto callback(
        meccha::product_ui::ProductUiKeyboardKey key,
        meccha::product_ui::ProductUiKeyboardModifiers modifiers)
        -> meccha::product_ui::ProductUiKeyboardCallback&
    {
        for (auto& registration : registrations)
        {
            if (registration.key == key &&
                registration.modifiers == modifiers)
            {
                return registration.callback;
            }
        }
        throw std::runtime_error{"registration not found"};
    }

    std::vector<RegisteredKey> registrations{};
    std::optional<std::size_t> fail_at{};
    std::optional<std::size_t> throw_at{};
};
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto passed = true;
    auto queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto translated_keys = std::vector<
        product_ui::ProductUiKeyboardKey>{};
    auto translator =
        [&translated_keys](product_ui::ProductUiKeyboardKey key)
        -> std::optional<std::string>
    {
        translated_keys.push_back(key);
        if (key == product_ui::ProductUiKeyboardKey::A)
        {
            return std::string{"\xE6\x97\xA5"};
        }
        if (key == product_ui::ProductUiKeyboardKey::Space)
        {
            return std::string{" "};
        }
        return std::nullopt;
    };
    auto registrar = FakeRegistrar{};
    auto binding = product_ui::ProductUiKeyboardBinding{
        queue,
        translator,
    };

    const auto started = binding.start(registrar);
    auto unique_registrations = std::set<
        std::pair<std::uint8_t, std::uint8_t>>{};
    for (const auto& registration : registrar.registrations)
    {
        unique_registrations.emplace(
            static_cast<std::uint8_t>(registration.key),
            static_cast<std::uint8_t>(registration.modifiers));
    }
    passed &= expect(
        started &&
            registrar.registrations.size() ==
                product_ui::ProductUiKeyboardRegistrationCount &&
            unique_registrations.size() ==
                registrar.registrations.size() &&
            started->registered_keys ==
                product_ui::ProductUiKeyboardRegistrationCount &&
            started->phase ==
                product_ui::ProductUiKeyboardBindingPhase::Active,
        "the keyboard binding did not register its frozen key set");

    const auto tab = product_ui::ProductUiKeyboardKey::Tab;
    const auto enter = product_ui::ProductUiKeyboardKey::Enter;
    const auto escape = product_ui::ProductUiKeyboardKey::Escape;
    const auto left = product_ui::ProductUiKeyboardKey::Left;
    const auto backspace =
        product_ui::ProductUiKeyboardKey::Backspace;
    const auto a = product_ui::ProductUiKeyboardKey::A;
    const auto space = product_ui::ProductUiKeyboardKey::Space;
    const auto none =
        product_ui::ProductUiKeyboardModifiers::None;
    const auto shift =
        product_ui::ProductUiKeyboardModifiers::Shift;

    registrar.callback(tab, none)();
    registrar.callback(a, none)();
    passed &= expect(
        queue->drain() == product_ui::ProductUiInputBatch{},
        "disabled production keyboard callbacks published input");
    passed &= expect(
        translated_keys.empty(),
        "disabled text input mutated the platform translator");

    queue->set_keyboard_input_mode(
        product_ui::ProductUiKeyboardInputMode::Navigation);
    registrar.callback(tab, none)();
    auto navigation = queue->drain();
    passed &= expect(
        navigation && navigation->keyboard.focus_next_pressed &&
            !navigation->keyboard.focus_previous_pressed,
        "Tab did not publish forward focus navigation");
    registrar.callback(tab, shift)();
    auto reverse = queue->drain();
    passed &= expect(
        reverse && reverse->keyboard.focus_previous_pressed,
        "Shift+Tab did not publish reverse focus navigation");
    registrar.callback(enter, none)();
    auto activation = queue->drain();
    passed &= expect(
        activation && activation->keyboard.activate_pressed &&
            activation->text_edit_events.empty(),
        "Enter did not activate a focused non-text control");
    registrar.callback(escape, none)();
    auto cancellation = queue->drain();
    passed &= expect(
        cancellation && cancellation->keyboard.cancel_pressed,
        "Escape did not cancel navigation state");
    registrar.callback(space, none)();
    auto space_activation = queue->drain();
    passed &= expect(
        space_activation &&
            space_activation->keyboard.activate_pressed &&
            translated_keys.empty(),
        "Space translated text outside an active text field");
    registrar.callback(a, none)();
    passed &= expect(
        queue->drain() == product_ui::ProductUiInputBatch{} &&
            translated_keys.empty(),
        "printable input escaped the text-edit guard");

    queue->set_keyboard_input_mode(
        product_ui::ProductUiKeyboardInputMode::TextEdit);
    registrar.callback(a, none)();
    registrar.callback(left, none)();
    registrar.callback(backspace, none)();
    registrar.callback(enter, none)();
    const auto editing = queue->drain();
    passed &= expect(
        editing && !editing->keyboard.activate_pressed &&
            editing->text_edit_events ==
                std::vector<ui::TextEditEvent>{
                    {ui::TextEditEventKind::Insert,
                     "\xE6\x97\xA5"},
                    {ui::TextEditEventKind::MoveLeft, {}},
                    {ui::TextEditEventKind::Backspace, {}},
                    {ui::TextEditEventKind::Commit, {}},
                } &&
            translated_keys ==
                std::vector{product_ui::ProductUiKeyboardKey::A},
        "text-edit callbacks did not preserve translated event order");

    translated_keys.clear();
    registrar.callback(space, shift)();
    const auto inserted_space = queue->drain();
    passed &= expect(
        inserted_space &&
            inserted_space->text_edit_events ==
                std::vector<ui::TextEditEvent>{
                    {ui::TextEditEventKind::Insert, " "},
                } &&
            translated_keys == std::vector{space},
        "Shift+Space did not use the text translator while editing");
    registrar.callback(escape, none)();
    const auto text_cancel = queue->drain();
    passed &= expect(
        text_cancel && !text_cancel->keyboard.cancel_pressed &&
            text_cancel->text_edit_events ==
                std::vector<ui::TextEditEvent>{
                    {ui::TextEditEventKind::Cancel, {}},
                },
        "Escape did not cancel the active text edit exclusively");

    registrar.callback(left, none)();
    static_cast<void>(
        queue->record_function_key(core::FunctionKey::F9));
    queue->set_keyboard_input_mode(
        product_ui::ProductUiKeyboardInputMode::Disabled);
    const auto transitioned = queue->drain();
    passed &= expect(
        transitioned && transitioned->text_edit_events.empty() &&
            transitioned->function_keys.size() == 2U,
        "a mode transition retained stale keyboard input or lost hotkeys");

    const auto duplicate = binding.start(registrar);
    passed &= expect(
        !duplicate &&
            duplicate.error().code ==
                product_ui::ProductUiKeyboardBindingErrorCode::
                    InvalidState,
        "a second keyboard start duplicated registration");

    binding.stop();
    queue->set_keyboard_input_mode(
        product_ui::ProductUiKeyboardInputMode::Navigation);
    registrar.callback(tab, none)();
    passed &= expect(
        queue->drain() == product_ui::ProductUiInputBatch{} &&
            binding.snapshot().phase ==
                product_ui::ProductUiKeyboardBindingPhase::Stopped,
        "a stopped keyboard callback remained active");

    auto failed_queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto failed_registrar = FakeRegistrar{};
    failed_registrar.fail_at = 3U;
    auto failed_binding = product_ui::ProductUiKeyboardBinding{
        failed_queue,
        translator,
    };
    const auto failed = failed_binding.start(failed_registrar);
    passed &= expect(
        !failed &&
            failed.error().code ==
                product_ui::ProductUiKeyboardBindingErrorCode::
                    Registration &&
            failed.error().key ==
                product_ui::ProductUiKeyboardKey::Escape &&
            failed_binding.snapshot().registered_keys == 3U &&
            failed_queue->drain() ==
                std::unexpected(
                    product_ui::ProductUiInputDrainError::Stopped),
        "partial keyboard registration did not fail closed");

    auto throwing_queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto throwing_registrar = FakeRegistrar{};
    throwing_registrar.throw_at = 0U;
    auto throwing_binding = product_ui::ProductUiKeyboardBinding{
        throwing_queue,
        translator,
    };
    const auto threw = throwing_binding.start(throwing_registrar);
    passed &= expect(
        !threw &&
            threw.error().code ==
                product_ui::ProductUiKeyboardBindingErrorCode::
                    Unexpected &&
            throwing_queue->drain() ==
                std::unexpected(
                    product_ui::ProductUiInputDrainError::Stopped),
        "a keyboard registration exception escaped or stayed live");

    auto retained_queue =
        std::make_shared<product_ui::ProductUiInputQueue>();
    auto retained_callback =
        product_ui::ProductUiKeyboardCallback{};
    {
        auto retained_registrar = FakeRegistrar{};
        auto retained_binding =
            product_ui::ProductUiKeyboardBinding{
                retained_queue,
                translator,
            };
        passed &= expect(
            retained_binding.start(retained_registrar).has_value(),
            "the keyboard lifetime binding did not start");
        retained_callback = retained_registrar.callback(tab, none);
    }
    retained_queue->set_keyboard_input_mode(
        product_ui::ProductUiKeyboardInputMode::Navigation);
    retained_callback();
    passed &= expect(
        retained_queue->drain() ==
            product_ui::ProductUiInputBatch{},
        "a keyboard callback outlived its binding unsafely");

    return passed ? 0 : 1;
}
