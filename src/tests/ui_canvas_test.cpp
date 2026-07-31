#include <meccha/core/fallback_glyph_atlas.hpp>
#include <meccha/core/png_encoder.hpp>
#include <meccha/ui/canvas.hpp>
#include <meccha/ui/fallback_glyph_compositor.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace
{
auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL ui_canvas: " << message << '\n';
    }
    return condition;
}

auto make_atlas()
    -> std::expected<
        meccha::core::FallbackGlyphAtlas,
        meccha::core::FallbackGlyphAtlasError>
{
    using namespace meccha::core;
    constexpr auto Width = std::uint32_t{96U};
    constexpr auto Height = std::uint32_t{96U};
    auto pixels = std::vector<std::byte>(
        static_cast<std::size_t>(Width) * Height * 4U);
    auto png = encode_png_rgba8(Width, Height, pixels);
    if (!png)
    {
        return std::unexpected(
            FallbackGlyphAtlasError::InvalidPng);
    }
    const auto glyphs = std::array{
        FallbackGlyph{U'A', 0U, 48U},
        FallbackGlyph{U'日', 1U, 48U},
        FallbackGlyph{U'\uFFFD', 2U, 48U},
    };
    constexpr auto required = std::array{U'A', U'日'};
    return FallbackGlyphAtlas::create(
        FallbackGlyphAtlasGeometry{
            Width,
            Height,
            48U,
            48U,
            2U,
            32U,
            6U,
            4U,
        },
        glyphs,
        std::make_shared<const std::vector<std::byte>>(
            std::move(*png)),
        required);
}
} // namespace

auto main() -> int
{
    using namespace meccha::ui;

    auto passed = true;
    auto builder = CanvasFrameBuilder{
        CanvasViewport{800.0, 600.0, 1.0}};
    passed &= expect(
        builder.push_clip(
            CanvasRect{100.0, 100.0, 200.0, 100.0}).has_value(),
        "valid nested clip was rejected");

    const auto line = builder.add_line(
        CanvasPoint{0.0, 125.0},
        CanvasPoint{400.0, 125.0},
        CanvasColor{255U, 0U, 0U, 255U},
        2.0);
    passed &= expect(
        line && *line,
        "crossing line was not clipped into the active region");

    const auto box = builder.add_filled_box(
        CanvasRect{50.0, 50.0, 100.0, 100.0},
        CanvasColor{0U, 255U, 0U, 128U});
    passed &= expect(
        box && *box,
        "partially visible box was not emitted");

    const auto texture = builder.add_texture(
        CanvasTextureHandle{42U},
        CanvasRect{250.0, 50.0, 100.0, 100.0},
        CanvasUvRect{0.0, 0.0, 1.0, 1.0},
        CanvasColor{255U, 255U, 255U, 255U});
    passed &= expect(
        texture && *texture,
        "partially visible texture was not emitted");

    const auto text = builder.add_text(
        CanvasPoint{120.0, 120.0},
        "日本語",
        CanvasColor{255U, 255U, 255U, 255U},
        1.0);
    passed &= expect(text && *text, "valid localized text was rejected");
    passed &= expect(
        builder.add_text(
            CanvasPoint{20.0, 20.0},
            "outside",
            CanvasColor{255U, 255U, 255U, 255U},
            1.0) ==
            false,
        "text outside the active clip was emitted");

    passed &= expect(
        builder.pop_clip().has_value(),
        "nested clip did not pop");
    const auto frame = std::move(builder).finish();
    passed &= expect(
        frame && frame->primitives.size() == 4U,
        "valid frame did not preserve exact primitive count");
    if (!frame)
    {
        return 1;
    }

    const auto& line_primitive =
        std::get<CanvasLinePrimitive>(frame->primitives[0]);
    passed &= expect(
        line_primitive.start == CanvasPoint{100.0, 125.0} &&
            line_primitive.end == CanvasPoint{300.0, 125.0} &&
            line_primitive.clip ==
                CanvasRect{100.0, 100.0, 200.0, 100.0},
        "line clipping geometry drifted");

    const auto& box_primitive =
        std::get<CanvasBoxPrimitive>(frame->primitives[1]);
    passed &= expect(
        box_primitive.rect ==
            CanvasRect{100.0, 100.0, 50.0, 50.0},
        "filled box intersection drifted");

    const auto& texture_primitive =
        std::get<CanvasTexturePrimitive>(frame->primitives[2]);
    passed &= expect(
        texture_primitive.rect ==
                CanvasRect{250.0, 100.0, 50.0, 50.0} &&
            texture_primitive.uv ==
                CanvasUvRect{0.0, 0.5, 0.5, 1.0},
        "texture clipping did not preserve matching UVs");

    const auto& text_primitive =
        std::get<CanvasTextPrimitive>(frame->primitives[3]);
    passed &= expect(
        text_primitive.utf8 == "日本語" &&
            text_primitive.clip ==
                CanvasRect{100.0, 100.0, 200.0, 100.0},
        "localized text lost its active clip");

    auto bounded = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 1.0},
        1U};
    passed &= expect(
        bounded.add_filled_box(
            CanvasRect{0.0, 0.0, 10.0, 10.0},
            CanvasColor{1U, 2U, 3U, 255U}) == true,
        "first bounded primitive was rejected");
    const auto overflow = bounded.add_filled_box(
        CanvasRect{20.0, 20.0, 10.0, 10.0},
        CanvasColor{1U, 2U, 3U, 255U});
    passed &= expect(
        !overflow &&
            overflow.error() == CanvasError::PrimitiveLimit &&
            std::move(bounded).finish()->primitives.size() == 1U,
        "primitive overflow partially mutated the frame");

    auto invalid = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 1.0}};
    const auto invalid_text = std::string{
        "bad"} + static_cast<char>(0xC0);
    passed &= expect(
        invalid.add_text(
            CanvasPoint{1.0, 1.0},
            invalid_text,
            CanvasColor{255U, 255U, 255U, 255U},
            1.0) ==
            std::unexpected(CanvasError::InvalidText),
        "invalid UTF-8 text was accepted");
    passed &= expect(
        !invalid.push_clip(CanvasRect{0.0, 0.0, -1.0, 2.0}) &&
            std::move(invalid).finish()->primitives.empty(),
        "invalid geometry mutated the frame");

    auto empty_clip = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 1.0}};
    passed &= expect(
        empty_clip.push_clip(
            CanvasRect{700.0, 500.0, 10.0, 10.0}).has_value() &&
            empty_clip.add_line(
                CanvasPoint{0.0, 0.0},
                CanvasPoint{800.0, 600.0},
                CanvasColor{255U, 255U, 255U, 255U},
                1.0) == false &&
            empty_clip.add_line(
                CanvasPoint{0.0, 500.0},
                CanvasPoint{800.0, 500.0},
                CanvasColor{255U, 255U, 255U, 255U},
                1.0) == false,
        "an empty nested clip emitted a line or degenerate point");
    passed &= expect(
        std::move(empty_clip).finish() ==
            std::unexpected(CanvasError::UnbalancedClip),
        "an unbalanced clip stack produced a frame");

    auto invalid_viewport = CanvasFrameBuilder{
        CanvasViewport{0.0, 480.0, 1.0}};
    passed &= expect(
        std::move(invalid_viewport).finish() ==
            std::unexpected(CanvasError::InvalidViewport),
        "an invalid viewport produced a frame");

    auto scaled_text = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 4.0}};
    passed &= expect(
        scaled_text.add_text(
            CanvasPoint{1.0, 1.0},
            "DPI",
            CanvasColor{255U, 255U, 255U, 255U},
            MaximumCanvasTextScale) == true &&
            scaled_text.add_text(
                CanvasPoint{1.0, 20.0},
                "too large",
                CanvasColor{255U, 255U, 255U, 255U},
                MaximumCanvasTextScale + 0.01) ==
                std::unexpected(CanvasError::InvalidScale) &&
            std::move(scaled_text).finish()->primitives.size() ==
                1U,
        "bounded high-DPI text scale was not enforced");

    const auto atlas = make_atlas();
    passed &= expect(
        atlas && atlas->glyphs().size() == 3U &&
            atlas->find(U'日') != nullptr &&
            atlas->find(U'Ω') == nullptr,
        "a validated fallback atlas did not preserve lookup identity");
    if (!atlas)
    {
        return 1;
    }
    constexpr auto incomplete_coverage = std::array{U'A'};
    passed &= expect(
        meccha::core::FallbackGlyphAtlas::create(
            atlas->geometry(),
            atlas->glyphs(),
            atlas->encoded_png(),
            incomplete_coverage) ==
            std::unexpected(
                meccha::core::FallbackGlyphAtlasError::InvalidCoverage),
        "fallback atlas accepted a divergent required glyph set");
    auto unordered_glyphs =
        std::vector<meccha::core::FallbackGlyph>{
        atlas->glyphs().begin(), atlas->glyphs().end()};
    std::swap(unordered_glyphs[0], unordered_glyphs[1]);
    constexpr auto exact_coverage = std::array{U'A', U'日'};
    passed &= expect(
        meccha::core::FallbackGlyphAtlas::create(
            atlas->geometry(),
            unordered_glyphs,
            atlas->encoded_png(),
            exact_coverage) ==
            std::unexpected(
                meccha::core::FallbackGlyphAtlasError::InvalidGlyphs),
        "fallback atlas accepted unordered glyph identities");

    auto mixed_builder = CanvasFrameBuilder{
        CanvasViewport{640.0, 480.0, 1.0}};
    passed &= expect(
        mixed_builder.push_clip(
            CanvasRect{0.0, 0.0, 200.0, 60.0}).has_value() &&
            mixed_builder.add_text(
                CanvasPoint{10.0, 5.0},
                "A日B",
                CanvasColor{10U, 20U, 30U, 255U},
                1.0) == true &&
            mixed_builder.pop_clip().has_value(),
        "mixed glyph fixture could not be built");
    const auto mixed_frame = std::move(mixed_builder).finish();
    const auto mixed = mixed_frame
                           ? compose_fallback_glyphs(
                                 *mixed_frame,
                                 *atlas,
                                 CanvasTextureHandle{77U})
                           : std::expected<
                                 CanvasFrame,
                                 FallbackGlyphCompositionError>{
                                 std::unexpected(
                                     FallbackGlyphCompositionError::
                                         InvalidFrame)};
    passed &= expect(
        mixed && mixed->primitives.size() == 3U,
        "game-font-first mixed text did not isolate its fallback glyph");
    if (mixed)
    {
        passed &= expect(
            std::get<CanvasTextPrimitive>(
                mixed->primitives[0])
                    .utf8 == "A",
            "mixed text did not retain its ASCII prefix");
        passed &= expect(
            std::get<CanvasTexturePrimitive>(
                mixed->primitives[1])
                    .texture == CanvasTextureHandle{77U},
            "mixed text did not select the fallback texture");
        passed &= expect(
            std::get<CanvasTextPrimitive>(
                mixed->primitives[2])
                    .utf8 == "B",
            "mixed text did not retain its ASCII suffix");
        const auto& fallback =
            std::get<CanvasTexturePrimitive>(mixed->primitives[1]);
        passed &= expect(
            fallback.rect == CanvasRect{58.0, 5.0, 48.0, 48.0} &&
                fallback.uv ==
                    CanvasUvRect{0.5, 0.0, 1.0, 0.5} &&
                fallback.tint ==
                    CanvasColor{10U, 20U, 30U, 255U},
            "fallback glyph atlas geometry or tint drifted");
    }

    const auto clipped = compose_fallback_glyphs(
        CanvasFrame{
            CanvasViewport{640.0, 480.0, 1.0},
            {CanvasTextPrimitive{
                CanvasPoint{80.0, 5.0},
                "A",
                CanvasColor{255U, 255U, 255U, 255U},
                1.0,
                CanvasRect{0.0, 0.0, 100.0, 60.0},
            }}},
        *atlas,
        CanvasTextureHandle{77U});
    passed &= expect(
        clipped && clipped->primitives.size() == 1U &&
            std::get<CanvasTexturePrimitive>(
                clipped->primitives[0])
                    .rect == CanvasRect{80.0, 5.0, 20.0, 48.0} &&
            std::get<CanvasTexturePrimitive>(
                clipped->primitives[0])
                    .uv ==
                CanvasUvRect{0.0, 0.0, 20.0 / 96.0, 0.5},
        "a partially clipped game glyph did not route through exact atlas UVs");

    const auto replaced = compose_fallback_glyphs(
        CanvasFrame{
            CanvasViewport{640.0, 480.0, 1.0},
            {CanvasTextPrimitive{
                CanvasPoint{10.0, 5.0},
                "Ω",
                CanvasColor{255U, 255U, 255U, 255U},
                1.0,
                CanvasRect{0.0, 0.0, 100.0, 60.0},
            }}},
        *atlas,
        CanvasTextureHandle{77U});
    passed &= expect(
        replaced && replaced->primitives.size() == 1U &&
            std::get<CanvasTexturePrimitive>(
                replaced->primitives[0])
                    .uv == CanvasUvRect{0.0, 0.5, 0.5, 1.0},
        "an absent glyph did not use the reviewed replacement cell");

    const auto hidden = compose_fallback_glyphs(
        CanvasFrame{
            CanvasViewport{640.0, 480.0, 1.0},
            {CanvasTextPrimitive{
                CanvasPoint{200.0, 5.0},
                "日",
                CanvasColor{255U, 255U, 255U, 255U},
                1.0,
                CanvasRect{0.0, 0.0, 100.0, 60.0},
            }}},
        *atlas,
        CanvasTextureHandle{77U});
    passed &= expect(
        hidden && hidden->primitives.empty(),
        "a fully clipped fallback glyph was emitted");

    auto overflowing = CanvasFrame{
        CanvasViewport{640.0, 480.0, 1.0}, {}};
    overflowing.primitives.resize(
        MaximumCanvasPrimitives - 1U,
        CanvasBoxPrimitive{
            CanvasRect{0.0, 0.0, 1.0, 1.0},
            CanvasColor{1U, 2U, 3U, 255U},
            CanvasRect{0.0, 0.0, 640.0, 480.0},
        });
    overflowing.primitives.emplace_back(CanvasTextPrimitive{
        CanvasPoint{10.0, 5.0},
        "AA",
        CanvasColor{255U, 255U, 255U, 255U},
        1.0,
        CanvasRect{0.0, 0.0, 200.0, 60.0},
    });
    passed &= expect(
        compose_fallback_glyphs(
            overflowing,
            *atlas,
            CanvasTextureHandle{77U}) ==
            std::unexpected(
                FallbackGlyphCompositionError::PrimitiveLimit) &&
            overflowing.primitives.size() ==
                MaximumCanvasPrimitives,
        "fallback expansion overflow mutated or escaped its frame limit");

    passed &= expect(
        compose_fallback_glyphs(
            *mixed_frame,
            *atlas,
            CanvasTextureHandle{}) ==
            std::unexpected(
                FallbackGlyphCompositionError::InvalidTexture),
        "a zero fallback texture handle was accepted");

    if (passed)
    {
        std::cout << "PASS ui_canvas\n";
    }
    return passed ? 0 : 1;
}
