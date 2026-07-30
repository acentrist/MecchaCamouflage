#pragma once

#include <cstdint>

#include <meccha/core/paint.hpp>

namespace meccha::core
{
enum class EspRole : std::uint8_t
{
    Unknown,
    Hider,
    Hunter,
    Spectator,
};

enum class EspScope : std::uint8_t
{
    All,
    Hider,
    Hunter,
};

enum class EspPawnSource : std::uint8_t
{
    PlayerArray,
    RoleRoster,
};

struct EspSettings
{
    bool enabled{true};
    EspScope scope{EspScope::All};
    bool boxes{true};
    bool skeletons{true};
    bool names{true};
    bool distance{true};
    bool snaplines{true};
    Rgb8 hider_color{0U, 255U, 136U};
    Rgb8 hunter_color{255U, 0U, 0U};

    auto operator==(const EspSettings&) const -> bool = default;
};

[[nodiscard]] auto esp_scope_matches(EspScope scope, EspRole role) -> bool;

[[nodiscard]] auto select_esp_pawn_source(
    EspRole roster_role,
    EspRole player_array_pawn_role,
    EspRole role_roster_pawn_role,
    bool same_player_state) -> EspPawnSource;

struct EspGeometryCapabilities
{
    bool spectator{};
    bool root_capsule{};
    bool skeletal_mesh{};

    auto operator==(const EspGeometryCapabilities&) const -> bool = default;
};

[[nodiscard]] auto esp_geometry_capabilities(
    bool spectator,
    bool root_is_capsule,
    bool skeletal_mesh) -> EspGeometryCapabilities;

struct ScreenBounds
{
    double left{};
    double top{};
    double right{};
    double bottom{};

    auto operator==(const ScreenBounds&) const -> bool = default;
};

[[nodiscard]] auto expand_screen_bounds(
    ScreenBounds bounds,
    double horizontal_ratio,
    double vertical_ratio) -> ScreenBounds;

[[nodiscard]] auto projection_scale_from_sample(
    double center,
    double raw_screen,
    double engine_screen) -> double;
} // namespace meccha::core
