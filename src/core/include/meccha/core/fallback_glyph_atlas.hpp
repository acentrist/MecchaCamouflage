#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <vector>

namespace meccha::core
{
inline constexpr std::size_t MaximumFallbackGlyphs = 4'096U;
inline constexpr char32_t FallbackReplacementCodepoint = U'\uFFFD';

struct FallbackGlyphAtlasGeometry
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t cell_width{};
    std::uint32_t cell_height{};
    std::uint32_t columns{};
    std::uint32_t point_size{};
    std::uint32_t padding_x{};
    std::uint32_t padding_y{};

    auto operator==(const FallbackGlyphAtlasGeometry&) const
        -> bool = default;
};

struct FallbackGlyph
{
    char32_t codepoint{};
    std::uint32_t index{};
    std::uint32_t advance{};

    auto operator==(const FallbackGlyph&) const -> bool = default;
};

enum class FallbackGlyphAtlasError : std::uint8_t
{
    InvalidGeometry,
    InvalidGlyphs,
    InvalidCoverage,
    InvalidPng,
    ResourceLimit,
    Construction,
};

class FallbackGlyphAtlas
{
public:
    [[nodiscard]] static auto create(
        FallbackGlyphAtlasGeometry geometry,
        std::span<const FallbackGlyph> glyphs,
        std::shared_ptr<const std::vector<std::byte>> encoded_png,
        std::span<const char32_t> required_codepoints)
        -> std::expected<
            FallbackGlyphAtlas,
            FallbackGlyphAtlasError>;

    [[nodiscard]] auto geometry() const noexcept
        -> const FallbackGlyphAtlasGeometry&;
    [[nodiscard]] auto glyphs() const noexcept
        -> std::span<const FallbackGlyph>;
    [[nodiscard]] auto encoded_png() const noexcept
        -> const std::shared_ptr<
            const std::vector<std::byte>>&;
    [[nodiscard]] auto find(char32_t codepoint) const noexcept
        -> const FallbackGlyph*;

private:
    FallbackGlyphAtlas(
        FallbackGlyphAtlasGeometry geometry,
        std::vector<FallbackGlyph> glyphs,
        std::shared_ptr<const std::vector<std::byte>> encoded_png);

    FallbackGlyphAtlasGeometry geometry_{};
    std::vector<FallbackGlyph> glyphs_{};
    std::shared_ptr<const std::vector<std::byte>> encoded_png_{};
};
} // namespace meccha::core
