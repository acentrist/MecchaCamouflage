#pragma once

#include <meccha/core/paint_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
inline constexpr std::size_t MaximumPaintPreviewStrokes = 600'000U;
inline constexpr std::uint64_t MaximumPaintPreviewPixelOperations =
    200'000'000U;

struct PaintPreviewComposition
{
    std::uint32_t dimension{};
    std::vector<std::byte> albedo_rgba{};
    std::vector<std::byte> packed_pbr_rgba{};
    std::size_t strokes_composed{};
    std::uint64_t pixels_touched{};
    std::uint64_t pixels_changed{};
};

enum class PaintPreviewComposeError : std::uint8_t
{
    InvalidDimension,
    InvalidBuffer,
    InvalidPlan,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto compose_paint_preview(
    std::uint32_t dimension,
    std::span<const std::byte> original_albedo_rgba,
    std::span<const std::byte> original_packed_pbr_rgba,
    const PaintPlan& plan,
    std::stop_token cancellation = {})
    -> std::expected<
        PaintPreviewComposition,
        PaintPreviewComposeError>;
} // namespace meccha::core
