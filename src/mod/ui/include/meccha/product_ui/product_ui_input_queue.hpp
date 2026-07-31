#pragma once

#include <meccha/application/input_command_router.hpp>
#include <meccha/ui/interaction.hpp>
#include <meccha/ui/text_edit.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <vector>

namespace meccha::product_ui
{
inline constexpr std::size_t
    MaximumQueuedProductUiFunctionKeyPresses = 32U;

enum class ProductUiNavigationInput : std::uint8_t
{
    FocusNext,
    FocusPrevious,
    Activate,
    Cancel,
};

enum class ProductUiInputRecordResult : std::uint8_t
{
    Accepted,
    InvalidEvent,
    EventLimit,
    Stopped,
};

enum class ProductUiInputDrainError : std::uint8_t
{
    EventLimit,
    Stopped,
};

struct ProductUiInputBatch
{
    std::vector<application::FunctionKeyEvent> function_keys{};
    ui::KeyboardNavigationFrame keyboard{};
    std::vector<ui::TextEditEvent> text_edit_events{};

    auto operator==(const ProductUiInputBatch&) const -> bool = default;
};

class ProductUiInputQueue
{
public:
    [[nodiscard]] auto record_function_key(core::FunctionKey key)
        -> ProductUiInputRecordResult;
    [[nodiscard]] auto record_navigation(
        ProductUiNavigationInput input)
        -> ProductUiInputRecordResult;
    [[nodiscard]] auto record_text_edit(ui::TextEditEvent event)
        -> ProductUiInputRecordResult;

    [[nodiscard]] auto drain()
        -> std::expected<
            ProductUiInputBatch,
            ProductUiInputDrainError>;

    auto discard() noexcept -> void;
    auto stop() noexcept -> void;

private:
    auto clear_locked() noexcept -> void;

    std::mutex mutex_{};
    std::vector<core::FunctionKey> function_keys_{};
    ui::KeyboardNavigationFrame keyboard_{};
    std::vector<ui::TextEditEvent> text_edit_events_{};
    std::size_t text_input_bytes_{};
    bool overflowed_{};
    bool stopped_{};
};
} // namespace meccha::product_ui
