#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace meccha::ui
{
inline constexpr std::size_t MaximumCanvasPrimitives = 8'192U;
inline constexpr std::size_t MaximumCanvasClipDepth = 16U;
inline constexpr std::size_t MaximumCanvasTextBytes = 4'096U;
inline constexpr std::size_t MaximumCanvasFrameTextBytes =
    1U * 1024U * 1024U;
inline constexpr double MaximumCanvasTextScale = 8.0;

struct CanvasPoint
{
    double x{};
    double y{};

    auto operator==(const CanvasPoint&) const -> bool = default;
};

struct CanvasRect
{
    double x{};
    double y{};
    double width{};
    double height{};

    auto operator==(const CanvasRect&) const -> bool = default;
};

struct CanvasUvRect
{
    double left{};
    double top{};
    double right{1.0};
    double bottom{1.0};

    auto operator==(const CanvasUvRect&) const -> bool = default;
};

struct CanvasColor
{
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255U};

    auto operator==(const CanvasColor&) const -> bool = default;
};

struct CanvasViewport
{
    double width{};
    double height{};
    double dpi_scale{1.0};

    auto operator==(const CanvasViewport&) const -> bool = default;
};

struct CanvasTextureHandle
{
    std::uint64_t identity{};

    auto operator==(const CanvasTextureHandle&) const -> bool = default;
};

struct CanvasLinePrimitive
{
    CanvasPoint start{};
    CanvasPoint end{};
    CanvasColor color{};
    double thickness{1.0};
    CanvasRect clip{};

    auto operator==(const CanvasLinePrimitive&) const -> bool = default;
};

struct CanvasBoxPrimitive
{
    CanvasRect rect{};
    CanvasColor color{};
    CanvasRect clip{};

    auto operator==(const CanvasBoxPrimitive&) const -> bool = default;
};

struct CanvasTextPrimitive
{
    CanvasPoint anchor{};
    std::string utf8{};
    CanvasColor color{};
    double scale{1.0};
    CanvasRect clip{};

    auto operator==(const CanvasTextPrimitive&) const -> bool = default;
};

struct CanvasTexturePrimitive
{
    CanvasTextureHandle texture{};
    CanvasRect rect{};
    CanvasUvRect uv{};
    CanvasColor tint{};
    CanvasRect clip{};

    auto operator==(const CanvasTexturePrimitive&) const -> bool = default;
};

using CanvasPrimitive = std::variant<
    CanvasLinePrimitive,
    CanvasBoxPrimitive,
    CanvasTextPrimitive,
    CanvasTexturePrimitive>;

struct CanvasFrame
{
    CanvasViewport viewport{};
    std::vector<CanvasPrimitive> primitives{};
};

enum class CanvasError : std::uint8_t
{
    InvalidViewport,
    InvalidGeometry,
    InvalidThickness,
    InvalidScale,
    InvalidTexture,
    InvalidUv,
    InvalidText,
    TextLimit,
    PrimitiveLimit,
    ClipDepth,
    ClipUnderflow,
    UnbalancedClip,
};

class CanvasFrameBuilder
{
public:
    explicit CanvasFrameBuilder(
        CanvasViewport viewport,
        std::size_t primitive_limit = MaximumCanvasPrimitives);

    [[nodiscard]] auto push_clip(CanvasRect clip)
        -> std::expected<void, CanvasError>;
    [[nodiscard]] auto pop_clip()
        -> std::expected<void, CanvasError>;

    [[nodiscard]] auto add_line(
        CanvasPoint start,
        CanvasPoint end,
        CanvasColor color,
        double thickness)
        -> std::expected<bool, CanvasError>;

    [[nodiscard]] auto add_filled_box(
        CanvasRect rect,
        CanvasColor color)
        -> std::expected<bool, CanvasError>;

    [[nodiscard]] auto add_text(
        CanvasPoint anchor,
        std::string_view utf8,
        CanvasColor color,
        double scale)
        -> std::expected<bool, CanvasError>;

    [[nodiscard]] auto add_texture(
        CanvasTextureHandle texture,
        CanvasRect rect,
        CanvasUvRect uv,
        CanvasColor tint)
        -> std::expected<bool, CanvasError>;

    [[nodiscard]] auto finish() &&
        -> std::expected<CanvasFrame, CanvasError>;

private:
    [[nodiscard]] auto ready() const
        -> std::expected<void, CanvasError>;
    [[nodiscard]] auto reserve_primitive()
        -> std::expected<void, CanvasError>;
    [[nodiscard]] auto active_clip() const -> CanvasRect;

    CanvasViewport viewport_{};
    std::size_t primitive_limit_{};
    std::size_t text_bytes_{};
    std::optional<CanvasError> initial_error_{};
    std::vector<CanvasRect> clips_{};
    std::vector<CanvasPrimitive> primitives_{};
};
} // namespace meccha::ui
