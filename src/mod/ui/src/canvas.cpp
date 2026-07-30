#include <meccha/ui/canvas.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <expected>
#include <optional>
#include <utility>

namespace meccha::ui
{
namespace
{
auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto valid_viewport(const CanvasViewport& viewport) -> bool
{
    return finite(viewport.width) && finite(viewport.height) &&
           finite(viewport.dpi_scale) &&
           viewport.width >= 320.0 &&
           viewport.width <= 32'768.0 &&
           viewport.height >= 200.0 &&
           viewport.height <= 32'768.0 &&
           viewport.dpi_scale >= 0.5 &&
           viewport.dpi_scale <= 4.0;
}

auto valid_point(const CanvasPoint& point) -> bool
{
    return finite(point.x) && finite(point.y);
}

auto valid_rect(const CanvasRect& rect) -> bool
{
    return finite(rect.x) && finite(rect.y) &&
           finite(rect.width) && finite(rect.height) &&
           rect.width > 0.0 && rect.height > 0.0;
}

auto valid_uv(const CanvasUvRect& uv) -> bool
{
    return finite(uv.left) && finite(uv.top) &&
           finite(uv.right) && finite(uv.bottom) &&
           uv.left >= 0.0 && uv.top >= 0.0 &&
           uv.right <= 1.0 && uv.bottom <= 1.0 &&
           uv.right > uv.left && uv.bottom > uv.top;
}

auto right(const CanvasRect& rect) -> double
{
    return rect.x + rect.width;
}

auto bottom(const CanvasRect& rect) -> double
{
    return rect.y + rect.height;
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

auto contains(const CanvasRect& rect, const CanvasPoint& point) -> bool
{
    return point.x >= rect.x && point.x < right(rect) &&
           point.y >= rect.y && point.y < bottom(rect);
}

auto clipped_line(
    CanvasPoint start,
    CanvasPoint end,
    const CanvasRect& clip)
    -> std::optional<std::pair<CanvasPoint, CanvasPoint>>
{
    const auto dx = end.x - start.x;
    const auto dy = end.y - start.y;
    const std::array p{-dx, dx, -dy, dy};
    const std::array q{
        start.x - clip.x,
        right(clip) - start.x,
        start.y - clip.y,
        bottom(clip) - start.y,
    };
    auto entering = 0.0;
    auto leaving = 1.0;
    for (auto index = std::size_t{}; index < p.size(); ++index)
    {
        if (p[index] == 0.0)
        {
            if (q[index] < 0.0)
            {
                return std::nullopt;
            }
            continue;
        }
        const auto ratio = q[index] / p[index];
        if (p[index] < 0.0)
        {
            entering = std::max(entering, ratio);
        }
        else
        {
            leaving = std::min(leaving, ratio);
        }
        if (entering > leaving)
        {
            return std::nullopt;
        }
    }
    return std::pair{
        CanvasPoint{
            start.x + entering * dx,
            start.y + entering * dy,
        },
        CanvasPoint{
            start.x + leaving * dx,
            start.y + leaving * dy,
        },
    };
}
} // namespace

CanvasFrameBuilder::CanvasFrameBuilder(
    CanvasViewport viewport,
    std::size_t primitive_limit)
    : viewport_{viewport},
      primitive_limit_{primitive_limit}
{
    if (!valid_viewport(viewport_))
    {
        initial_error_ = CanvasError::InvalidViewport;
        return;
    }
    if (primitive_limit_ == 0U ||
        primitive_limit_ > MaximumCanvasPrimitives)
    {
        initial_error_ = CanvasError::PrimitiveLimit;
        return;
    }
    clips_.push_back(
        CanvasRect{0.0, 0.0, viewport_.width, viewport_.height});
    primitives_.reserve(std::min<std::size_t>(primitive_limit_, 256U));
}

auto CanvasFrameBuilder::ready() const
    -> std::expected<void, CanvasError>
{
    if (initial_error_)
    {
        return std::unexpected(*initial_error_);
    }
    return {};
}

auto CanvasFrameBuilder::active_clip() const -> CanvasRect
{
    return clips_.back();
}

auto CanvasFrameBuilder::reserve_primitive()
    -> std::expected<void, CanvasError>
{
    if (primitives_.size() >= primitive_limit_)
    {
        return std::unexpected(CanvasError::PrimitiveLimit);
    }
    return {};
}

auto CanvasFrameBuilder::push_clip(CanvasRect clip)
    -> std::expected<void, CanvasError>
{
    if (const auto state = ready(); !state)
    {
        return state;
    }
    if (!valid_rect(clip))
    {
        return std::unexpected(CanvasError::InvalidGeometry);
    }
    if (clips_.size() >= MaximumCanvasClipDepth)
    {
        return std::unexpected(CanvasError::ClipDepth);
    }
    const auto visible = intersect(active_clip(), clip);
    clips_.push_back(
        visible.value_or(CanvasRect{clip.x, clip.y, 0.0, 0.0}));
    return {};
}

auto CanvasFrameBuilder::pop_clip()
    -> std::expected<void, CanvasError>
{
    if (const auto state = ready(); !state)
    {
        return state;
    }
    if (clips_.size() <= 1U)
    {
        return std::unexpected(CanvasError::ClipUnderflow);
    }
    clips_.pop_back();
    return {};
}

auto CanvasFrameBuilder::add_line(
    CanvasPoint start,
    CanvasPoint end,
    CanvasColor color,
    double thickness) -> std::expected<bool, CanvasError>
{
    if (const auto state = ready(); !state)
    {
        return std::unexpected(state.error());
    }
    if (!valid_point(start) || !valid_point(end))
    {
        return std::unexpected(CanvasError::InvalidGeometry);
    }
    if (!finite(thickness) || thickness < 0.25 || thickness > 16.0)
    {
        return std::unexpected(CanvasError::InvalidThickness);
    }
    if (!valid_rect(active_clip()))
    {
        return false;
    }
    const auto visible = clipped_line(start, end, active_clip());
    if (!visible)
    {
        return false;
    }
    if (const auto capacity = reserve_primitive(); !capacity)
    {
        return std::unexpected(capacity.error());
    }
    primitives_.emplace_back(CanvasLinePrimitive{
        visible->first,
        visible->second,
        color,
        thickness,
        active_clip(),
    });
    return true;
}

auto CanvasFrameBuilder::add_filled_box(
    CanvasRect rect,
    CanvasColor color) -> std::expected<bool, CanvasError>
{
    if (const auto state = ready(); !state)
    {
        return std::unexpected(state.error());
    }
    if (!valid_rect(rect))
    {
        return std::unexpected(CanvasError::InvalidGeometry);
    }
    const auto visible = intersect(rect, active_clip());
    if (!visible)
    {
        return false;
    }
    if (const auto capacity = reserve_primitive(); !capacity)
    {
        return std::unexpected(capacity.error());
    }
    primitives_.emplace_back(
        CanvasBoxPrimitive{*visible, color, active_clip()});
    return true;
}

auto CanvasFrameBuilder::add_text(
    CanvasPoint anchor,
    std::string_view utf8,
    CanvasColor color,
    double scale) -> std::expected<bool, CanvasError>
{
    if (const auto state = ready(); !state)
    {
        return std::unexpected(state.error());
    }
    if (!valid_point(anchor))
    {
        return std::unexpected(CanvasError::InvalidGeometry);
    }
    if (!finite(scale) || scale < 0.25 ||
        scale > MaximumCanvasTextScale)
    {
        return std::unexpected(CanvasError::InvalidScale);
    }
    if (utf8.empty() || utf8.size() > MaximumCanvasTextBytes ||
        !core::decode_utf8(utf8))
    {
        return std::unexpected(CanvasError::InvalidText);
    }
    if (utf8.size() >
        MaximumCanvasFrameTextBytes - text_bytes_)
    {
        return std::unexpected(CanvasError::TextLimit);
    }
    if (!contains(active_clip(), anchor))
    {
        return false;
    }
    if (const auto capacity = reserve_primitive(); !capacity)
    {
        return std::unexpected(capacity.error());
    }
    primitives_.emplace_back(CanvasTextPrimitive{
        anchor,
        std::string{utf8},
        color,
        scale,
        active_clip(),
    });
    text_bytes_ += utf8.size();
    return true;
}

auto CanvasFrameBuilder::add_texture(
    CanvasTextureHandle texture,
    CanvasRect rect,
    CanvasUvRect uv,
    CanvasColor tint) -> std::expected<bool, CanvasError>
{
    if (const auto state = ready(); !state)
    {
        return std::unexpected(state.error());
    }
    if (texture.identity == 0U)
    {
        return std::unexpected(CanvasError::InvalidTexture);
    }
    if (!valid_rect(rect))
    {
        return std::unexpected(CanvasError::InvalidGeometry);
    }
    if (!valid_uv(uv))
    {
        return std::unexpected(CanvasError::InvalidUv);
    }
    const auto visible = intersect(rect, active_clip());
    if (!visible)
    {
        return false;
    }
    if (const auto capacity = reserve_primitive(); !capacity)
    {
        return std::unexpected(capacity.error());
    }
    const auto x0 = (visible->x - rect.x) / rect.width;
    const auto y0 = (visible->y - rect.y) / rect.height;
    const auto x1 = (right(*visible) - rect.x) / rect.width;
    const auto y1 = (bottom(*visible) - rect.y) / rect.height;
    const auto clipped_uv = CanvasUvRect{
        uv.left + x0 * (uv.right - uv.left),
        uv.top + y0 * (uv.bottom - uv.top),
        uv.left + x1 * (uv.right - uv.left),
        uv.top + y1 * (uv.bottom - uv.top),
    };
    primitives_.emplace_back(CanvasTexturePrimitive{
        texture,
        *visible,
        clipped_uv,
        tint,
        active_clip(),
    });
    return true;
}

auto CanvasFrameBuilder::finish() &&
    -> std::expected<CanvasFrame, CanvasError>
{
    if (const auto state = ready(); !state)
    {
        return std::unexpected(state.error());
    }
    if (clips_.size() != 1U)
    {
        return std::unexpected(CanvasError::UnbalancedClip);
    }
    return CanvasFrame{viewport_, std::move(primitives_)};
}
} // namespace meccha::ui
