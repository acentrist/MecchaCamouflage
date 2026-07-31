#include <meccha/product_ui/product_ui_pointer_capture.hpp>

#include <cmath>
#include <expected>

namespace meccha::product_ui
{
namespace
{
constexpr auto MinimumViewportWidth = 320.0;
constexpr auto MinimumViewportHeight = 200.0;
constexpr auto MaximumViewportExtent = 32'768.0;
constexpr auto MinimumDpiScale = 0.5;
constexpr auto MaximumDpiScale = 4.0;

auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto validate(const ProductUiPointerObservation& observation)
    -> std::expected<void, ProductUiPointerCaptureError>
{
    if (!finite(observation.viewport_width) ||
        !finite(observation.viewport_height) ||
        observation.viewport_width < MinimumViewportWidth ||
        observation.viewport_width > MaximumViewportExtent ||
        observation.viewport_height < MinimumViewportHeight ||
        observation.viewport_height > MaximumViewportExtent)
    {
        return std::unexpected(
            ProductUiPointerCaptureError::InvalidViewport);
    }
    if (!finite(observation.client_width) ||
        !finite(observation.client_height) ||
        observation.client_width <= 0.0 ||
        observation.client_width > MaximumViewportExtent ||
        observation.client_height <= 0.0 ||
        observation.client_height > MaximumViewportExtent)
    {
        return std::unexpected(
            ProductUiPointerCaptureError::InvalidClient);
    }
    if (!finite(observation.cursor_client_x) ||
        !finite(observation.cursor_client_y))
    {
        return std::unexpected(
            ProductUiPointerCaptureError::InvalidPointer);
    }
    if (!finite(observation.dpi_scale) ||
        observation.dpi_scale < MinimumDpiScale ||
        observation.dpi_scale > MaximumDpiScale)
    {
        return std::unexpected(ProductUiPointerCaptureError::InvalidDpi);
    }
    if (observation.owner_window == 0U)
    {
        return std::unexpected(
            ProductUiPointerCaptureError::InvalidWindow);
    }
    return {};
}
} // namespace

auto ProductUiPointerCapture::update(
    const ProductUiPointerObservation& observation)
    -> std::expected<
        ProductUiPointerCaptureFrame,
        ProductUiPointerCaptureError>
{
    const auto valid = validate(observation);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }

    auto next = state_;
    auto frame = ProductUiPointerCaptureFrame{
        ui::CanvasViewport{
            observation.viewport_width,
            observation.viewport_height,
            observation.dpi_scale,
        },
        ui::PointerFrame{},
        observation.focused,
        observation.owner_window,
    };

    const auto owner_changed =
        next.synchronized &&
        next.owner_window != observation.owner_window;
    if (!observation.focused)
    {
        frame.pointer.position = ui::CanvasPoint{-1.0, -1.0};
        frame.pointer.primary_released =
            next.previous_primary_down;
        next.synchronized = true;
        next.previous_primary_down = false;
        next.suppress_until_release =
            next.suppress_until_release || observation.primary_down;
        next.focused = false;
        next.owner_window = observation.owner_window;
        state_ = next;
        return frame;
    }

    frame.pointer.position = ui::CanvasPoint{
        observation.cursor_client_x *
            observation.viewport_width / observation.client_width,
        observation.cursor_client_y *
            observation.viewport_height / observation.client_height,
    };

    if (!next.synchronized)
    {
        next.synchronized = true;
        next.previous_primary_down = false;
        next.suppress_until_release = observation.primary_down;
    }
    else if (owner_changed)
    {
        frame.pointer.primary_released =
            next.previous_primary_down;
        next.previous_primary_down = false;
        next.suppress_until_release = observation.primary_down;
    }
    else if (next.suppress_until_release)
    {
        if (!observation.primary_down)
        {
            next.suppress_until_release = false;
        }
        next.previous_primary_down = false;
    }
    else
    {
        frame.pointer.primary_pressed =
            observation.primary_down && !next.previous_primary_down;
        frame.pointer.primary_down = observation.primary_down;
        frame.pointer.primary_released =
            !observation.primary_down && next.previous_primary_down;
        next.previous_primary_down = observation.primary_down;
    }

    next.focused = true;
    next.owner_window = observation.owner_window;
    state_ = next;
    return frame;
}

auto ProductUiPointerCapture::snapshot() const
    -> ProductUiPointerCaptureSnapshot
{
    return state_;
}
} // namespace meccha::product_ui
