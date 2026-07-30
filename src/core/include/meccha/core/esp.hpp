#pragma once

#include <cstdint>

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
