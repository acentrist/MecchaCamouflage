#include <meccha/core/fallback_glyph_atlas.hpp>

#include <meccha/core/png_encoder.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <functional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace meccha::core
{
namespace
{
auto valid_scalar(char32_t codepoint) -> bool
{
    return codepoint <= 0x10FFFFU &&
           !(codepoint >= 0xD800U && codepoint <= 0xDFFFU);
}

auto checked_product(
    std::uint32_t left,
    std::uint32_t right) -> std::optional<std::uint32_t>
{
    if (right != 0U &&
        left > std::numeric_limits<std::uint32_t>::max() / right)
    {
        return std::nullopt;
    }
    return left * right;
}
} // namespace

FallbackGlyphAtlas::FallbackGlyphAtlas(
    FallbackGlyphAtlasGeometry geometry,
    std::vector<FallbackGlyph> glyphs,
    std::shared_ptr<const std::vector<std::byte>> encoded_png)
    : geometry_{geometry},
      glyphs_{std::move(glyphs)},
      encoded_png_{std::move(encoded_png)}
{
}

auto FallbackGlyphAtlas::create(
    FallbackGlyphAtlasGeometry geometry,
    std::span<const FallbackGlyph> glyphs,
    std::shared_ptr<const std::vector<std::byte>> encoded_png,
    std::span<const char32_t> required_codepoints)
    -> std::expected<
        FallbackGlyphAtlas,
        FallbackGlyphAtlasError>
{
    try
    {
        if (glyphs.empty() ||
            glyphs.size() > MaximumFallbackGlyphs ||
            required_codepoints.size() > MaximumFallbackGlyphs)
        {
            return std::unexpected(
                FallbackGlyphAtlasError::ResourceLimit);
        }
        if (geometry.width == 0U || geometry.height == 0U ||
            geometry.cell_width == 0U ||
            geometry.cell_height == 0U ||
            geometry.columns == 0U ||
            geometry.point_size == 0U ||
            geometry.columns > glyphs.size() ||
            geometry.point_size > geometry.cell_height ||
            geometry.padding_x >
                (geometry.cell_width - 1U) / 2U ||
            geometry.padding_y >
                (geometry.cell_height - 1U) / 2U)
        {
            return std::unexpected(
                FallbackGlyphAtlasError::InvalidGeometry);
        }
        const auto width = checked_product(
            geometry.columns,
            geometry.cell_width);
        const auto rows = static_cast<std::uint32_t>(
            (glyphs.size() + geometry.columns - 1U) /
            geometry.columns);
        const auto height = checked_product(
            rows,
            geometry.cell_height);
        if (!width || !height || *width != geometry.width ||
            *height != geometry.height)
        {
            return std::unexpected(
                FallbackGlyphAtlasError::InvalidGeometry);
        }
        if (!encoded_png)
        {
            return std::unexpected(
                FallbackGlyphAtlasError::InvalidPng);
        }
        const auto png = inspect_png_rgba8(*encoded_png);
        if (!png || png->width != geometry.width ||
            png->height != geometry.height)
        {
            return std::unexpected(
                FallbackGlyphAtlasError::InvalidPng);
        }

        auto copied = std::vector<FallbackGlyph>{
            glyphs.begin(), glyphs.end()};
        for (auto index = std::size_t{};
             index < copied.size();
             ++index)
        {
            const auto& glyph = copied[index];
            if (!valid_scalar(glyph.codepoint) ||
                glyph.index != index || glyph.advance == 0U ||
                glyph.advance > geometry.cell_width ||
                (index != 0U &&
                 copied[index - 1U].codepoint >= glyph.codepoint))
            {
                return std::unexpected(
                    FallbackGlyphAtlasError::InvalidGlyphs);
            }
        }

        auto required = std::vector<char32_t>{};
        required.reserve(required_codepoints.size() + 1U);
        for (const auto codepoint : required_codepoints)
        {
            if (!valid_scalar(codepoint))
            {
                return std::unexpected(
                    FallbackGlyphAtlasError::InvalidCoverage);
            }
            if (codepoint != U'\n' && codepoint != U'\r' &&
                codepoint != U'\t')
            {
                required.push_back(codepoint);
            }
        }
        required.push_back(FallbackReplacementCodepoint);
        std::ranges::sort(required);
        const auto unique = std::ranges::unique(required);
        required.erase(unique.begin(), unique.end());
        if (required.size() != copied.size() ||
            !std::ranges::equal(
                required,
                copied,
                {},
                std::identity{},
                &FallbackGlyph::codepoint))
        {
            return std::unexpected(
                FallbackGlyphAtlasError::InvalidCoverage);
        }

        return FallbackGlyphAtlas{
            geometry,
            std::move(copied),
            std::move(encoded_png),
        };
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            FallbackGlyphAtlasError::ResourceLimit);
    }
    catch (...)
    {
        return std::unexpected(
            FallbackGlyphAtlasError::Construction);
    }
}

auto FallbackGlyphAtlas::geometry() const noexcept
    -> const FallbackGlyphAtlasGeometry&
{
    return geometry_;
}

auto FallbackGlyphAtlas::glyphs() const noexcept
    -> std::span<const FallbackGlyph>
{
    return glyphs_;
}

auto FallbackGlyphAtlas::encoded_png() const noexcept
    -> const std::shared_ptr<const std::vector<std::byte>>&
{
    return encoded_png_;
}

auto FallbackGlyphAtlas::find(char32_t codepoint) const noexcept
    -> const FallbackGlyph*
{
    const auto found = std::ranges::lower_bound(
        glyphs_,
        codepoint,
        {},
        &FallbackGlyph::codepoint);
    return found != glyphs_.end() && found->codepoint == codepoint
               ? &*found
               : nullptr;
}
} // namespace meccha::core
