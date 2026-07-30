#pragma once

#include <meccha/ui/canvas.hpp>

#include <array>
#include <cstdint>
#include <expected>

namespace meccha::ui
{
inline constexpr double MinimumUiScale = 0.75;
inline constexpr double MaximumUiScale = 2.0;

struct CanvasInsets
{
    double left{};
    double top{};
    double right{};
    double bottom{};

    auto operator==(const CanvasInsets&) const -> bool = default;
};

enum class PanelSection : std::uint8_t
{
    Paint,
    ImagePaint,
    Esp,
    Settings,
    Diagnostics,
};

inline constexpr std::array<PanelSection, 5> PanelSections{
    PanelSection::Paint,
    PanelSection::ImagePaint,
    PanelSection::Esp,
    PanelSection::Settings,
    PanelSection::Diagnostics,
};

struct PanelLayoutInput
{
    CanvasViewport viewport{};
    CanvasInsets safe_area{};
    double user_scale{1.0};
};

struct PanelLayout
{
    double effective_scale{1.0};
    bool compact_tabs{};
    CanvasRect safe_bounds{};
    CanvasRect panel{};
    CanvasRect tab_strip{};
    CanvasRect content{};
    CanvasRect status_strip{};
    std::array<CanvasRect, PanelSections.size()> section_tabs{};
};

enum class PanelLayoutError : std::uint8_t
{
    InvalidViewport,
    InvalidSafeArea,
    InvalidUiScale,
    InsufficientSafeArea,
};

[[nodiscard]] auto build_panel_layout(PanelLayoutInput input)
    -> std::expected<PanelLayout, PanelLayoutError>;
} // namespace meccha::ui
