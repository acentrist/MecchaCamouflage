#pragma once

#include <meccha/core/fallback_glyph_atlas.hpp>
#include <meccha/ui/canvas.hpp>

#include <cstdint>
#include <expected>

namespace meccha::ui
{
enum class FallbackGlyphCompositionError : std::uint8_t
{
    InvalidFrame,
    InvalidTexture,
    InvalidText,
    InvalidGeometry,
    PrimitiveLimit,
    Construction,
};

[[nodiscard]] auto compose_fallback_glyphs(
    const CanvasFrame& frame,
    const core::FallbackGlyphAtlas& atlas,
    CanvasTextureHandle fallback_texture)
    -> std::expected<
        CanvasFrame,
        FallbackGlyphCompositionError>;
} // namespace meccha::ui
