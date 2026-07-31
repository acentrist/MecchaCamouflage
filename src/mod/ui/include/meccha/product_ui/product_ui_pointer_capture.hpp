#pragma once

#include <meccha/ui/canvas.hpp>
#include <meccha/ui/interaction.hpp>

#include <cstdint>
#include <expected>

namespace meccha::product_ui
{
struct ProductUiPointerObservation
{
    double viewport_width{};
    double viewport_height{};
    double client_width{};
    double client_height{};
    double cursor_client_x{};
    double cursor_client_y{};
    double dpi_scale{1.0};
    std::uintptr_t owner_window{};
    bool focused{};
    bool primary_down{};
};

enum class ProductUiPointerCaptureError : std::uint8_t
{
    InvalidViewport,
    InvalidClient,
    InvalidPointer,
    InvalidDpi,
    InvalidWindow,
};

struct ProductUiPointerCaptureFrame
{
    ui::CanvasViewport viewport{};
    ui::PointerFrame pointer{};
    bool function_key_input_available{};
    std::uintptr_t owner_window{};

    auto operator==(const ProductUiPointerCaptureFrame&) const
        -> bool = default;
};

struct ProductUiPointerCaptureSnapshot
{
    bool synchronized{};
    bool previous_primary_down{};
    bool suppress_until_release{};
    bool focused{};
    std::uintptr_t owner_window{};

    auto operator==(const ProductUiPointerCaptureSnapshot&) const
        -> bool = default;
};

class ProductUiPointerCapture
{
public:
    [[nodiscard]] auto update(
        const ProductUiPointerObservation& observation)
        -> std::expected<
            ProductUiPointerCaptureFrame,
            ProductUiPointerCaptureError>;

    [[nodiscard]] auto snapshot() const
        -> ProductUiPointerCaptureSnapshot;

private:
    ProductUiPointerCaptureSnapshot state_{};
};
} // namespace meccha::product_ui
