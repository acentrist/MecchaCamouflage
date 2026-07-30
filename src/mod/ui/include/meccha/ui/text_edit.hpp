#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace meccha::ui
{
inline constexpr std::size_t MaximumTextFieldBytes = 4'096U;
inline constexpr std::size_t MaximumTextEditEventsPerFrame = 64U;
inline constexpr std::size_t MaximumTextInputBytesPerFrame = 4'096U;

enum class TextEditEventKind : std::uint8_t
{
    Insert,
    MoveLeft,
    MoveRight,
    MoveHome,
    MoveEnd,
    Backspace,
    DeleteForward,
    Commit,
    Cancel,
};

struct TextEditEvent
{
    TextEditEventKind kind{TextEditEventKind::Insert};
    std::string utf8{};

    auto operator==(const TextEditEvent&) const -> bool = default;
};

struct TextEditState
{
    bool editing{};
    std::string value{};
    std::string original{};
    std::size_t cursor_byte{};

    auto operator==(const TextEditState&) const -> bool = default;
};

struct TextEditUpdate
{
    TextEditState state{};
    bool changed{};
    bool committed{};
    bool cancelled{};
};

enum class TextEditError : std::uint8_t
{
    InvalidLimit,
    InvalidState,
    InvalidEvent,
    InvalidText,
    TextLimit,
    EventLimit,
    InvalidSequence,
};

[[nodiscard]] auto begin_text_edit(
    std::string value,
    std::size_t maximum_bytes = MaximumTextFieldBytes)
    -> std::expected<TextEditState, TextEditError>;

[[nodiscard]] auto update_text_edit(
    TextEditState previous,
    std::span<const TextEditEvent> events,
    std::size_t maximum_bytes = MaximumTextFieldBytes)
    -> std::expected<TextEditUpdate, TextEditError>;
} // namespace meccha::ui
