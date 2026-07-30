#include <meccha/ui/text_edit.hpp>

#include <array>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_text_edit: " << message << '\n';
    }
    return condition;
}
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    const auto begun = begin_text_edit("A日本", 32U);
    passed &= expect(
        begun && begun->editing &&
            begun->value == "A日本" &&
            begun->original == "A日本" &&
            begun->cursor_byte == begun->value.size(),
        "begin did not capture the exact UTF-8 value and end cursor");
    if (!begun)
    {
        return 1;
    }

    const auto edited = update_text_edit(
        *begun,
        std::array{
            TextEditEvent{TextEditEventKind::MoveHome, {}},
            TextEditEvent{TextEditEventKind::DeleteForward, {}},
            TextEditEvent{TextEditEventKind::Insert, "語"},
            TextEditEvent{TextEditEventKind::MoveEnd, {}},
        },
        32U);
    passed &= expect(
        edited && edited->state.editing &&
            edited->state.value == "語日本" &&
            edited->state.cursor_byte ==
                edited->state.value.size() &&
            edited->changed &&
            !edited->committed &&
            !edited->cancelled,
        "ordered UTF-8 navigation/edit events corrupted byte boundaries");

    const auto cancelled = update_text_edit(
        edited->state,
        std::array{
            TextEditEvent{TextEditEventKind::Cancel, {}},
        },
        32U);
    passed &= expect(
        cancelled && !cancelled->state.editing &&
            cancelled->state.value == "A日本" &&
            cancelled->cancelled &&
            cancelled->changed,
        "cancel did not restore the exact captured value");

    const auto recommenced = begin_text_edit("name", 8U);
    const auto committed = update_text_edit(
        *recommenced,
        std::array{
            TextEditEvent{TextEditEventKind::MoveHome, {}},
            TextEditEvent{TextEditEventKind::DeleteForward, {}},
            TextEditEvent{TextEditEventKind::Insert, "N"},
            TextEditEvent{TextEditEventKind::Commit, {}},
        },
        8U);
    passed &= expect(
        committed && !committed->state.editing &&
            committed->state.value == "Name" &&
            committed->committed &&
            committed->changed,
        "commit did not preserve the ordered edited value");

    passed &= expect(
        update_text_edit(
            *recommenced,
            std::array{
                TextEditEvent{
                    TextEditEventKind::Insert,
                    "toolong",
                },
            },
            8U) ==
            std::unexpected(TextEditError::TextLimit),
        "text byte bound was not enforced");
    passed &= expect(
        update_text_edit(
            *recommenced,
            std::array{
                TextEditEvent{
                    TextEditEventKind::Insert,
                    "\n",
                },
            },
            8U) ==
            std::unexpected(TextEditError::InvalidText),
        "single-line text accepted a control character");
    passed &= expect(
        update_text_edit(
            *recommenced,
            std::array{
                TextEditEvent{TextEditEventKind::Commit, {}},
                TextEditEvent{TextEditEventKind::MoveLeft, {}},
            },
            8U) ==
            std::unexpected(TextEditError::InvalidSequence),
        "events after a terminal edit event were accepted");

    const auto backspace_state = begin_text_edit("日本", 16U);
    const auto backspaced = update_text_edit(
        *backspace_state,
        std::array{
            TextEditEvent{TextEditEventKind::Backspace, {}},
        },
        16U);
    passed &= expect(
        backspaced &&
            backspaced->state.value == "日" &&
            backspaced->state.cursor_byte ==
                backspaced->state.value.size(),
        "UTF-8 backspace split a code point");

    auto excessive_events = std::vector<TextEditEvent>(
        MaximumTextEditEventsPerFrame + 1U,
        TextEditEvent{TextEditEventKind::MoveLeft, {}});
    passed &= expect(
        update_text_edit(
            *recommenced,
            excessive_events,
            8U) ==
            std::unexpected(TextEditError::EventLimit),
        "per-frame text event bound was not enforced");

    passed &= expect(
        update_text_edit(
            TextEditState{
                true,
                "日本",
                "日本",
                2U,
            },
            {},
            16U) ==
            std::unexpected(TextEditError::InvalidState),
        "a cursor inside a UTF-8 continuation sequence was accepted");

    passed &= expect(
        update_text_edit(
            *recommenced,
            std::array{
                TextEditEvent{
                    TextEditEventKind::MoveLeft,
                    "payload",
                },
            },
            8U) ==
            std::unexpected(TextEditError::InvalidEvent),
        "a non-insert event payload was accepted");

    if (passed)
    {
        std::cout << "PASS ui_text_edit\n";
    }
    return passed ? 0 : 1;
}
