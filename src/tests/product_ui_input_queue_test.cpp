#include <meccha/product_ui/product_ui_input_queue.hpp>

#include <atomic>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
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
} // namespace

auto main() -> int
{
    using namespace meccha;

    auto passed = true;
    auto queue = product_ui::ProductUiInputQueue{};
    passed &= expect(
        queue.record_function_key(core::FunctionKey::F9) ==
            product_ui::ProductUiInputRecordResult::Accepted,
        "a function-key edge was not accepted");
    passed &= expect(
        queue.record_navigation(
            product_ui::ProductUiNavigationInput::
                FocusPrevious) ==
            product_ui::ProductUiInputRecordResult::Accepted,
        "a navigation edge was not accepted");
    passed &= expect(
        queue.record_text_edit(
            ui::TextEditEvent{
                ui::TextEditEventKind::Insert,
                "\xE6\x97\xA5\xE6\x9C\xAC",
            }) ==
            product_ui::ProductUiInputRecordResult::Accepted,
        "a valid localized text edge was not accepted");

    const auto drained = queue.drain();
    passed &= expect(
        drained &&
            drained->function_keys ==
                std::vector<application::FunctionKeyEvent>{
                    {
                        core::FunctionKey::F9,
                        application::FunctionKeyEventKind::
                            Pressed,
                    },
                    {
                        core::FunctionKey::F9,
                        application::FunctionKeyEventKind::
                            Released,
                    },
                } &&
            drained->keyboard.focus_previous_pressed &&
            drained->text_edit_events ==
                std::vector<ui::TextEditEvent>{
                    {
                        ui::TextEditEventKind::Insert,
                        "\xE6\x97\xA5\xE6\x9C\xAC",
                    },
                },
        "input edges did not drain into one immutable HUD frame");

    const auto empty = queue.drain();
    passed &= expect(
        empty && empty->function_keys.empty() &&
            !empty->keyboard.focus_previous_pressed &&
            empty->text_edit_events.empty(),
        "drained input edges were replayed");

    auto overflow = product_ui::ProductUiInputQueue{};
    for (auto index = std::size_t{};
         index <
         product_ui::MaximumQueuedProductUiFunctionKeyPresses;
         ++index)
    {
        passed &= expect(
            overflow.record_function_key(
                core::FunctionKey::F1) ==
                product_ui::ProductUiInputRecordResult::Accepted,
            "an in-limit function-key edge was rejected");
    }
    passed &= expect(
        overflow.record_function_key(core::FunctionKey::F2) ==
            product_ui::ProductUiInputRecordResult::EventLimit,
        "an overflowing function-key edge was accepted");
    passed &= expect(
        overflow.drain() ==
            std::unexpected(
                product_ui::ProductUiInputDrainError::
                    EventLimit),
        "an overflowed input frame was partially published");
    const auto recovered = overflow.drain();
    passed &= expect(
        recovered && recovered->function_keys.empty(),
        "the input queue did not recover after rejecting overflow");

    auto invalid = product_ui::ProductUiInputQueue{};
    passed &= expect(
        invalid.record_function_key(
            static_cast<core::FunctionKey>(0U)) ==
            product_ui::ProductUiInputRecordResult::InvalidEvent,
        "an out-of-range function key was accepted");
    passed &= expect(
        invalid.record_navigation(
            static_cast<
                product_ui::ProductUiNavigationInput>(0xFFU)) ==
            product_ui::ProductUiInputRecordResult::InvalidEvent,
        "an out-of-range navigation edge was accepted");
    passed &= expect(
        invalid.record_text_edit(
            ui::TextEditEvent{
                ui::TextEditEventKind::Insert,
                "\xF0\x28\x8C\x28",
            }) ==
            product_ui::ProductUiInputRecordResult::InvalidEvent,
        "malformed UTF-8 input was accepted");
    passed &= expect(
        invalid.record_text_edit(
            ui::TextEditEvent{
                ui::TextEditEventKind::Commit,
                "unexpected",
            }) ==
            product_ui::ProductUiInputRecordResult::InvalidEvent,
        "a non-insert text payload was accepted");

    auto text_overflow = product_ui::ProductUiInputQueue{};
    for (auto index = std::size_t{};
         index < ui::MaximumTextEditEventsPerFrame;
         ++index)
    {
        passed &= expect(
            text_overflow.record_text_edit(
                ui::TextEditEvent{
                    ui::TextEditEventKind::Insert,
                    "x",
                }) ==
                product_ui::ProductUiInputRecordResult::Accepted,
            "an in-limit text edge was rejected");
    }
    passed &= expect(
        text_overflow.record_text_edit(
            ui::TextEditEvent{
                ui::TextEditEventKind::Backspace,
                {},
            }) ==
            product_ui::ProductUiInputRecordResult::EventLimit,
        "an overflowing text edge was accepted");
    passed &= expect(
        text_overflow.drain() ==
            std::unexpected(
                product_ui::ProductUiInputDrainError::
                    EventLimit),
        "an overflowed text frame was partially published");

    auto byte_overflow = product_ui::ProductUiInputQueue{};
    passed &= expect(
        byte_overflow.record_text_edit(
            ui::TextEditEvent{
                ui::TextEditEventKind::Insert,
                std::string(
                    ui::MaximumTextInputBytesPerFrame + 1U,
                    'x'),
            }) ==
            product_ui::ProductUiInputRecordResult::EventLimit,
        "an oversized text payload was accepted");

    auto lifecycle = product_ui::ProductUiInputQueue{};
    static_cast<void>(
        lifecycle.record_function_key(core::FunctionKey::F9));
    lifecycle.discard();
    const auto discarded = lifecycle.drain();
    passed &= expect(
        discarded && discarded->function_keys.empty(),
        "focus-loss discard published stale input");
    static_cast<void>(
        lifecycle.record_function_key(core::FunctionKey::F9));
    lifecycle.stop();
    passed &= expect(
        lifecycle.record_function_key(core::FunctionKey::F1) ==
            product_ui::ProductUiInputRecordResult::Stopped &&
            lifecycle.drain() ==
                std::unexpected(
                    product_ui::ProductUiInputDrainError::
                        Stopped),
        "a stopped input queue accepted or published events");

    auto concurrent = product_ui::ProductUiInputQueue{};
    auto accepted = std::atomic_size_t{};
    auto producers = std::vector<std::jthread>{};
    producers.reserve(4U);
    for (auto producer = std::size_t{}; producer < 4U;
         ++producer)
    {
        producers.emplace_back(
            [&concurrent, &accepted]
            {
                for (auto event = std::size_t{};
                     event < 8U;
                     ++event)
                {
                    if (concurrent.record_function_key(
                            core::FunctionKey::F1) ==
                        product_ui::ProductUiInputRecordResult::
                            Accepted)
                    {
                        accepted.fetch_add(
                            1U,
                            std::memory_order_relaxed);
                    }
                }
            });
    }
    producers.clear();
    const auto concurrent_batch = concurrent.drain();
    passed &= expect(
        accepted.load(std::memory_order_relaxed) == 32U &&
            concurrent_batch &&
            concurrent_batch->function_keys.size() == 64U,
        "concurrent input callbacks lost or duplicated edges");

    return passed ? 0 : 1;
}
