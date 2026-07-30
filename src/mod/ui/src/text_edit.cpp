#include <meccha/ui/text_edit.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace meccha::ui
{
namespace
{
auto continuation(char value) -> bool
{
    return (static_cast<std::uint8_t>(value) & 0xC0U) == 0x80U;
}

auto cursor_boundary(
    std::string_view value,
    std::size_t cursor) -> bool
{
    return cursor <= value.size() &&
           (cursor == value.size() ||
            !continuation(value[cursor]));
}

auto previous_boundary(
    std::string_view value,
    std::size_t cursor) -> std::size_t
{
    if (cursor == 0U)
    {
        return 0U;
    }
    auto result = cursor - 1U;
    while (result > 0U && continuation(value[result]))
    {
        --result;
    }
    return result;
}

auto next_boundary(
    std::string_view value,
    std::size_t cursor) -> std::size_t
{
    if (cursor >= value.size())
    {
        return value.size();
    }
    auto result = cursor + 1U;
    while (result < value.size() &&
           continuation(value[result]))
    {
        ++result;
    }
    return result;
}

auto valid_single_line(std::string_view value) -> bool
{
    const auto decoded = core::decode_utf8(value);
    if (!decoded)
    {
        return false;
    }
    return std::ranges::none_of(
        *decoded,
        [](char32_t codepoint)
        {
            return codepoint < U' ' || codepoint == U'\x7F';
        });
}

auto valid_limit(std::size_t maximum_bytes) -> bool
{
    return maximum_bytes > 0U &&
           maximum_bytes <= MaximumTextFieldBytes;
}

auto valid_state(
    const TextEditState& state,
    std::size_t maximum_bytes) -> bool
{
    if (state.value.size() > maximum_bytes ||
        !valid_single_line(state.value) ||
        !cursor_boundary(state.value, state.cursor_byte))
    {
        return false;
    }
    if (!state.editing)
    {
        return state.original.empty();
    }
    return state.original.size() <= maximum_bytes &&
           valid_single_line(state.original);
}

} // namespace

auto begin_text_edit(
    std::string value,
    std::size_t maximum_bytes)
    -> std::expected<TextEditState, TextEditError>
{
    if (!valid_limit(maximum_bytes))
    {
        return std::unexpected(TextEditError::InvalidLimit);
    }
    if (value.size() > maximum_bytes)
    {
        return std::unexpected(TextEditError::TextLimit);
    }
    if (!valid_single_line(value))
    {
        return std::unexpected(TextEditError::InvalidText);
    }
    auto original = value;
    const auto cursor = value.size();
    return TextEditState{
        true,
        std::move(value),
        std::move(original),
        cursor,
    };
}

auto update_text_edit(
    TextEditState previous,
    std::span<const TextEditEvent> events,
    std::size_t maximum_bytes)
    -> std::expected<TextEditUpdate, TextEditError>
{
    if (!valid_limit(maximum_bytes))
    {
        return std::unexpected(TextEditError::InvalidLimit);
    }
    if (!valid_state(previous, maximum_bytes) ||
        !previous.editing)
    {
        return std::unexpected(TextEditError::InvalidState);
    }
    if (events.size() > MaximumTextEditEventsPerFrame)
    {
        return std::unexpected(TextEditError::EventLimit);
    }

    const auto initial_value = previous.value;
    auto terminal = false;
    auto inserted_bytes = std::size_t{};
    auto committed = false;
    auto cancelled = false;
    for (const auto& event : events)
    {
        if (terminal)
        {
            return std::unexpected(TextEditError::InvalidSequence);
        }
        if (event.kind == TextEditEventKind::Insert &&
            (event.utf8.empty() ||
             !valid_single_line(event.utf8)))
        {
            return std::unexpected(TextEditError::InvalidText);
        }
        if (event.kind != TextEditEventKind::Insert &&
            !event.utf8.empty())
        {
            return std::unexpected(TextEditError::InvalidEvent);
        }

        switch (event.kind)
        {
        case TextEditEventKind::Insert:
            if (event.utf8.size() >
                MaximumTextInputBytesPerFrame - inserted_bytes)
            {
                return std::unexpected(TextEditError::EventLimit);
            }
            inserted_bytes += event.utf8.size();
            if (event.utf8.size() >
                maximum_bytes - previous.value.size())
            {
                return std::unexpected(TextEditError::TextLimit);
            }
            previous.value.insert(
                previous.cursor_byte,
                event.utf8);
            previous.cursor_byte += event.utf8.size();
            break;
        case TextEditEventKind::MoveLeft:
            previous.cursor_byte = previous_boundary(
                previous.value,
                previous.cursor_byte);
            break;
        case TextEditEventKind::MoveRight:
            previous.cursor_byte = next_boundary(
                previous.value,
                previous.cursor_byte);
            break;
        case TextEditEventKind::MoveHome:
            previous.cursor_byte = 0U;
            break;
        case TextEditEventKind::MoveEnd:
            previous.cursor_byte = previous.value.size();
            break;
        case TextEditEventKind::Backspace:
        {
            const auto begin = previous_boundary(
                previous.value,
                previous.cursor_byte);
            previous.value.erase(
                begin,
                previous.cursor_byte - begin);
            previous.cursor_byte = begin;
            break;
        }
        case TextEditEventKind::DeleteForward:
        {
            const auto end = next_boundary(
                previous.value,
                previous.cursor_byte);
            previous.value.erase(
                previous.cursor_byte,
                end - previous.cursor_byte);
            break;
        }
        case TextEditEventKind::Commit:
            previous.editing = false;
            previous.original.clear();
            previous.cursor_byte = previous.value.size();
            committed = true;
            terminal = true;
            break;
        case TextEditEventKind::Cancel:
            previous.value = std::move(previous.original);
            previous.original.clear();
            previous.cursor_byte = previous.value.size();
            previous.editing = false;
            cancelled = true;
            terminal = true;
            break;
        }
    }

    const auto changed = previous.value != initial_value;
    return TextEditUpdate{
        std::move(previous),
        changed,
        committed,
        cancelled,
    };
}
} // namespace meccha::ui
