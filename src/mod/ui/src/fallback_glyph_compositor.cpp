#include <meccha/ui/fallback_glyph_compositor.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <new>
#include <optional>
#include <string>
#include <utility>

namespace meccha::ui
{
namespace
{
auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto valid_rect(const CanvasRect& rect) -> bool
{
    return finite(rect.x) && finite(rect.y) &&
           finite(rect.width) && finite(rect.height) &&
           rect.width > 0.0 && rect.height > 0.0;
}

auto right(const CanvasRect& rect) -> double
{
    return rect.x + rect.width;
}

auto bottom(const CanvasRect& rect) -> double
{
    return rect.y + rect.height;
}

auto contains(const CanvasRect& outer, const CanvasRect& inner)
    -> bool
{
    return inner.x >= outer.x && inner.y >= outer.y &&
           right(inner) <= right(outer) &&
           bottom(inner) <= bottom(outer);
}

auto intersect(
    const CanvasRect& left,
    const CanvasRect& right_rect) -> std::optional<CanvasRect>
{
    const auto x0 = std::max(left.x, right_rect.x);
    const auto y0 = std::max(left.y, right_rect.y);
    const auto x1 = std::min(right(left), right(right_rect));
    const auto y1 = std::min(bottom(left), bottom(right_rect));
    if (x1 <= x0 || y1 <= y0)
    {
        return std::nullopt;
    }
    return CanvasRect{x0, y0, x1 - x0, y1 - y0};
}

auto printable_ascii(char32_t codepoint) -> bool
{
    return codepoint >= U' ' && codepoint <= U'~';
}

auto append(
    CanvasFrame& output,
    CanvasPrimitive primitive)
    -> std::expected<void, FallbackGlyphCompositionError>
{
    if (output.primitives.size() >= MaximumCanvasPrimitives)
    {
        return std::unexpected(
            FallbackGlyphCompositionError::PrimitiveLimit);
    }
    output.primitives.emplace_back(std::move(primitive));
    return {};
}
} // namespace

auto compose_fallback_glyphs(
    const CanvasFrame& frame,
    const core::FallbackGlyphAtlas& atlas,
    CanvasTextureHandle fallback_texture)
    -> std::expected<
        CanvasFrame,
        FallbackGlyphCompositionError>
{
    if (fallback_texture.identity == 0U)
    {
        return std::unexpected(
            FallbackGlyphCompositionError::InvalidTexture);
    }
    if (!finite(frame.viewport.width) ||
        !finite(frame.viewport.height) ||
        !finite(frame.viewport.dpi_scale) ||
        frame.viewport.width < 320.0 ||
        frame.viewport.width > 32'768.0 ||
        frame.viewport.height < 200.0 ||
        frame.viewport.height > 32'768.0 ||
        frame.viewport.dpi_scale < 0.5 ||
        frame.viewport.dpi_scale > 4.0 ||
        frame.primitives.size() > MaximumCanvasPrimitives)
    {
        return std::unexpected(
            FallbackGlyphCompositionError::InvalidFrame);
    }

    try
    {
        auto output = CanvasFrame{frame.viewport, {}};
        output.primitives.reserve(frame.primitives.size());
        const auto& geometry = atlas.geometry();
        const auto* replacement =
            atlas.find(core::FallbackReplacementCodepoint);
        if (replacement == nullptr || geometry.width == 0U ||
            geometry.height == 0U || geometry.columns == 0U)
        {
            return std::unexpected(
                FallbackGlyphCompositionError::InvalidGeometry);
        }
        auto input_text_bytes = std::size_t{};

        for (const auto& primitive : frame.primitives)
        {
            const auto* text =
                std::get_if<CanvasTextPrimitive>(&primitive);
            if (text == nullptr)
            {
                const auto added = append(output, primitive);
                if (!added)
                {
                    return std::unexpected(added.error());
                }
                continue;
            }
            if (text->utf8.empty() ||
                text->utf8.size() > MaximumCanvasTextBytes ||
                text->utf8.size() >
                    MaximumCanvasFrameTextBytes -
                        input_text_bytes ||
                !finite(text->anchor.x) ||
                !finite(text->anchor.y) ||
                !finite(text->scale) || text->scale <= 0.0 ||
                text->scale > MaximumCanvasTextScale ||
                !valid_rect(text->clip))
            {
                return std::unexpected(
                    FallbackGlyphCompositionError::InvalidText);
            }
            input_text_bytes += text->utf8.size();
            const auto decoded = core::decode_utf8(text->utf8);
            if (!decoded || decoded->empty())
            {
                return std::unexpected(
                    FallbackGlyphCompositionError::InvalidText);
            }

            auto pen_x = text->anchor.x;
            const auto cell_width =
                static_cast<double>(geometry.cell_width) *
                text->scale;
            const auto cell_height =
                static_cast<double>(geometry.cell_height) *
                text->scale;
            if (!finite(cell_width) || !finite(cell_height) ||
                cell_width <= 0.0 || cell_height <= 0.0)
            {
                return std::unexpected(
                    FallbackGlyphCompositionError::InvalidGeometry);
            }

            for (const auto codepoint : *decoded)
            {
                if (codepoint == U'\n' || codepoint == U'\r' ||
                    codepoint == U'\t')
                {
                    return std::unexpected(
                        FallbackGlyphCompositionError::InvalidText);
                }
                const auto* exact = atlas.find(codepoint);
                const auto* glyph = exact != nullptr
                                        ? exact
                                        : replacement;
                const auto cell = CanvasRect{
                    pen_x,
                    text->anchor.y,
                    cell_width,
                    cell_height,
                };
                if (!valid_rect(cell))
                {
                    return std::unexpected(
                        FallbackGlyphCompositionError::InvalidGeometry);
                }

                if (printable_ascii(codepoint) &&
                    contains(text->clip, cell))
                {
                    const auto added = append(
                        output,
                        CanvasTextPrimitive{
                            CanvasPoint{pen_x, text->anchor.y},
                            std::string(
                                1U,
                                static_cast<char>(codepoint)),
                            text->color,
                            text->scale,
                            text->clip,
                        });
                    if (!added)
                    {
                        return std::unexpected(added.error());
                    }
                }
                else if (const auto visible =
                             intersect(cell, text->clip))
                {
                    const auto column =
                        glyph->index % geometry.columns;
                    const auto row =
                        glyph->index / geometry.columns;
                    const auto left =
                        static_cast<double>(
                            column * geometry.cell_width) /
                        geometry.width;
                    const auto top =
                        static_cast<double>(
                            row * geometry.cell_height) /
                        geometry.height;
                    const auto uv_width =
                        static_cast<double>(geometry.cell_width) /
                        geometry.width;
                    const auto uv_height =
                        static_cast<double>(geometry.cell_height) /
                        geometry.height;
                    const auto x0 =
                        (visible->x - cell.x) / cell.width;
                    const auto y0 =
                        (visible->y - cell.y) / cell.height;
                    const auto x1 =
                        (right(*visible) - cell.x) / cell.width;
                    const auto y1 =
                        (bottom(*visible) - cell.y) / cell.height;
                    const auto added = append(
                        output,
                        CanvasTexturePrimitive{
                            fallback_texture,
                            *visible,
                            CanvasUvRect{
                                left + x0 * uv_width,
                                top + y0 * uv_height,
                                left + x1 * uv_width,
                                top + y1 * uv_height,
                            },
                            text->color,
                            text->clip,
                        });
                    if (!added)
                    {
                        return std::unexpected(added.error());
                    }
                }

                const auto advance = static_cast<double>(
                    exact != nullptr ? exact->advance
                                     : geometry.cell_width);
                pen_x += advance * text->scale;
                if (!finite(pen_x))
                {
                    return std::unexpected(
                        FallbackGlyphCompositionError::InvalidGeometry);
                }
            }
        }
        return output;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            FallbackGlyphCompositionError::PrimitiveLimit);
    }
    catch (...)
    {
        return std::unexpected(
            FallbackGlyphCompositionError::Construction);
    }
}
} // namespace meccha::ui
