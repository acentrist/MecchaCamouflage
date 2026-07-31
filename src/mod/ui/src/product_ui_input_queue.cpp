#include <meccha/product_ui/product_ui_input_queue.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <mutex>
#include <string_view>
#include <utility>

namespace meccha::product_ui
{
namespace
{
auto valid_function_key(core::FunctionKey key) -> bool
{
    const auto value = static_cast<std::uint8_t>(key);
    return value >=
               static_cast<std::uint8_t>(
                   core::FunctionKey::F1) &&
           value <=
               static_cast<std::uint8_t>(
                   core::FunctionKey::F24);
}

auto valid_insert(std::string_view utf8) -> bool
{
    if (utf8.empty())
    {
        return false;
    }
    const auto decoded = core::decode_utf8(utf8);
    return decoded &&
           std::ranges::none_of(
               *decoded,
               [](char32_t codepoint)
               {
                   return codepoint < U' ' ||
                          codepoint == U'\x7F';
               });
}

auto valid_text_event(const ui::TextEditEvent& event) -> bool
{
    switch (event.kind)
    {
    case ui::TextEditEventKind::Insert:
        return valid_insert(event.utf8);
    case ui::TextEditEventKind::MoveLeft:
    case ui::TextEditEventKind::MoveRight:
    case ui::TextEditEventKind::MoveHome:
    case ui::TextEditEventKind::MoveEnd:
    case ui::TextEditEventKind::Backspace:
    case ui::TextEditEventKind::DeleteForward:
    case ui::TextEditEventKind::Commit:
    case ui::TextEditEventKind::Cancel:
        return event.utf8.empty();
    }
    return false;
}
} // namespace

auto ProductUiInputQueue::record_function_key(
    core::FunctionKey key) -> ProductUiInputRecordResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return ProductUiInputRecordResult::Stopped;
    }
    if (!valid_function_key(key))
    {
        return ProductUiInputRecordResult::InvalidEvent;
    }
    if (overflowed_ ||
        function_keys_.size() >=
            MaximumQueuedProductUiFunctionKeyPresses)
    {
        overflowed_ = true;
        return ProductUiInputRecordResult::EventLimit;
    }
    function_keys_.push_back(key);
    return ProductUiInputRecordResult::Accepted;
}

auto ProductUiInputQueue::record_navigation(
    ProductUiNavigationInput input)
    -> ProductUiInputRecordResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return ProductUiInputRecordResult::Stopped;
    }
    if (overflowed_)
    {
        return ProductUiInputRecordResult::EventLimit;
    }
    return record_navigation_locked(input);
}

auto ProductUiInputQueue::record_navigation_locked(
    ProductUiNavigationInput input)
    -> ProductUiInputRecordResult
{
    switch (input)
    {
    case ProductUiNavigationInput::FocusNext:
        keyboard_.focus_next_pressed = true;
        break;
    case ProductUiNavigationInput::FocusPrevious:
        keyboard_.focus_previous_pressed = true;
        break;
    case ProductUiNavigationInput::Activate:
        keyboard_.activate_pressed = true;
        break;
    case ProductUiNavigationInput::Cancel:
        keyboard_.cancel_pressed = true;
        break;
    default:
        return ProductUiInputRecordResult::InvalidEvent;
    }
    return ProductUiInputRecordResult::Accepted;
}

auto ProductUiInputQueue::record_text_edit(ui::TextEditEvent event)
    -> ProductUiInputRecordResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return ProductUiInputRecordResult::Stopped;
    }
    return record_text_edit_locked(std::move(event));
}

auto ProductUiInputQueue::record_text_edit_locked(
    ui::TextEditEvent event) -> ProductUiInputRecordResult
{
    if (event.kind == ui::TextEditEventKind::Insert &&
        event.utf8.size() >
            ui::MaximumTextInputBytesPerFrame)
    {
        overflowed_ = true;
        return ProductUiInputRecordResult::EventLimit;
    }
    if (!valid_text_event(event))
    {
        return ProductUiInputRecordResult::InvalidEvent;
    }
    const auto inserted_bytes =
        event.kind == ui::TextEditEventKind::Insert
            ? event.utf8.size()
            : 0U;
    if (overflowed_ ||
        text_edit_events_.size() >=
            ui::MaximumTextEditEventsPerFrame ||
        inserted_bytes >
            ui::MaximumTextInputBytesPerFrame -
                text_input_bytes_)
    {
        overflowed_ = true;
        return ProductUiInputRecordResult::EventLimit;
    }
    text_input_bytes_ += inserted_bytes;
    text_edit_events_.push_back(std::move(event));
    return ProductUiInputRecordResult::Accepted;
}

auto ProductUiInputQueue::set_keyboard_input_mode(
    ProductUiKeyboardInputMode mode) noexcept -> void
{
    const auto lock = std::scoped_lock{mutex_};
    if (mode != ProductUiKeyboardInputMode::Disabled &&
        mode != ProductUiKeyboardInputMode::Navigation &&
        mode != ProductUiKeyboardInputMode::TextEdit)
    {
        mode = ProductUiKeyboardInputMode::Disabled;
    }
    if (keyboard_input_mode_ != mode)
    {
        clear_keyboard_locked();
        keyboard_input_mode_ = mode;
    }
}

auto ProductUiInputQueue::keyboard_input_mode() const noexcept
    -> ProductUiKeyboardInputMode
{
    const auto lock = std::scoped_lock{mutex_};
    return keyboard_input_mode_;
}

auto ProductUiInputQueue::record_keyboard_navigation(
    ProductUiNavigationInput input)
    -> ProductUiInputRecordResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return ProductUiInputRecordResult::Stopped;
    }
    if (keyboard_input_mode_ !=
        ProductUiKeyboardInputMode::Navigation)
    {
        return ProductUiInputRecordResult::Ignored;
    }
    if (overflowed_)
    {
        return ProductUiInputRecordResult::EventLimit;
    }
    return record_navigation_locked(input);
}

auto ProductUiInputQueue::record_keyboard_text_edit(
    ui::TextEditEvent event) -> ProductUiInputRecordResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return ProductUiInputRecordResult::Stopped;
    }
    if (keyboard_input_mode_ !=
        ProductUiKeyboardInputMode::TextEdit)
    {
        return ProductUiInputRecordResult::Ignored;
    }
    if (keyboard_text_terminal_)
    {
        return ProductUiInputRecordResult::Ignored;
    }
    const auto terminal =
        event.kind == ui::TextEditEventKind::Commit ||
        event.kind == ui::TextEditEventKind::Cancel;
    const auto recorded =
        record_text_edit_locked(std::move(event));
    if (terminal &&
        recorded == ProductUiInputRecordResult::Accepted)
    {
        keyboard_text_terminal_ = true;
    }
    return recorded;
}

auto ProductUiInputQueue::record_keyboard_enter()
    -> ProductUiInputRecordResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return ProductUiInputRecordResult::Stopped;
    }
    if (keyboard_input_mode_ ==
        ProductUiKeyboardInputMode::Navigation)
    {
        if (overflowed_)
        {
            return ProductUiInputRecordResult::EventLimit;
        }
        return record_navigation_locked(
            ProductUiNavigationInput::Activate);
    }
    if (keyboard_input_mode_ ==
        ProductUiKeyboardInputMode::TextEdit)
    {
        if (keyboard_text_terminal_)
        {
            return ProductUiInputRecordResult::Ignored;
        }
        const auto recorded =
            record_text_edit_locked(ui::TextEditEvent{
                ui::TextEditEventKind::Commit,
                {},
            });
        if (recorded == ProductUiInputRecordResult::Accepted)
        {
            keyboard_text_terminal_ = true;
        }
        return recorded;
    }
    return ProductUiInputRecordResult::Ignored;
}

auto ProductUiInputQueue::record_keyboard_cancel()
    -> ProductUiInputRecordResult
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return ProductUiInputRecordResult::Stopped;
    }
    if (keyboard_input_mode_ ==
        ProductUiKeyboardInputMode::Navigation)
    {
        if (overflowed_)
        {
            return ProductUiInputRecordResult::EventLimit;
        }
        return record_navigation_locked(
            ProductUiNavigationInput::Cancel);
    }
    if (keyboard_input_mode_ ==
        ProductUiKeyboardInputMode::TextEdit)
    {
        if (keyboard_text_terminal_)
        {
            return ProductUiInputRecordResult::Ignored;
        }
        const auto recorded =
            record_text_edit_locked(ui::TextEditEvent{
                ui::TextEditEventKind::Cancel,
                {},
            });
        if (recorded == ProductUiInputRecordResult::Accepted)
        {
            keyboard_text_terminal_ = true;
        }
        return recorded;
    }
    return ProductUiInputRecordResult::Ignored;
}

auto ProductUiInputQueue::drain()
    -> std::expected<
        ProductUiInputBatch,
        ProductUiInputDrainError>
{
    const auto lock = std::scoped_lock{mutex_};
    if (stopped_)
    {
        return std::unexpected(
            ProductUiInputDrainError::Stopped);
    }
    if (overflowed_)
    {
        clear_locked();
        return std::unexpected(
            ProductUiInputDrainError::EventLimit);
    }
    auto batch = ProductUiInputBatch{
        {},
        keyboard_,
        std::move(text_edit_events_),
    };
    batch.function_keys.reserve(function_keys_.size() * 2U);
    for (const auto key : function_keys_)
    {
        batch.function_keys.push_back(
            application::FunctionKeyEvent{
                key,
                application::FunctionKeyEventKind::Pressed,
            });
        batch.function_keys.push_back(
            application::FunctionKeyEvent{
                key,
                application::FunctionKeyEventKind::Released,
            });
    }
    clear_locked();
    return batch;
}

auto ProductUiInputQueue::discard() noexcept -> void
{
    const auto lock = std::scoped_lock{mutex_};
    clear_locked();
}

auto ProductUiInputQueue::stop() noexcept -> void
{
    const auto lock = std::scoped_lock{mutex_};
    clear_locked();
    stopped_ = true;
}

auto ProductUiInputQueue::clear_locked() noexcept -> void
{
    function_keys_.clear();
    clear_keyboard_locked();
    overflowed_ = false;
}

auto ProductUiInputQueue::clear_keyboard_locked() noexcept -> void
{
    keyboard_ = {};
    text_edit_events_.clear();
    text_input_bytes_ = 0U;
    keyboard_text_terminal_ = false;
}
} // namespace meccha::product_ui
