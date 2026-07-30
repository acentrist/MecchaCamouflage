#include <meccha/core/esp.hpp>

namespace meccha::core
{
auto esp_scope_matches(EspScope scope, EspRole role) -> bool
{
    return role != EspRole::Spectator &&
           (scope == EspScope::All ||
            (scope == EspScope::Hider && role == EspRole::Hider) ||
            (scope == EspScope::Hunter && role == EspRole::Hunter));
}

auto select_esp_pawn_source(
    EspRole roster_role,
    EspRole player_array_pawn_role,
    EspRole role_roster_pawn_role,
    bool same_player_state) -> EspPawnSource
{
    const auto active_roster_role =
        roster_role == EspRole::Hider ||
        roster_role == EspRole::Hunter;
    const auto current_is_not_avatar =
        player_array_pawn_role == EspRole::Unknown ||
        player_array_pawn_role == EspRole::Spectator;
    return same_player_state && active_roster_role &&
                   current_is_not_avatar &&
                   role_roster_pawn_role == roster_role
               ? EspPawnSource::RoleRoster
               : EspPawnSource::PlayerArray;
}

auto esp_geometry_capabilities(
    bool spectator,
    bool root_is_capsule,
    bool skeletal_mesh) -> EspGeometryCapabilities
{
    return {
        spectator,
        !spectator && root_is_capsule,
        !spectator && skeletal_mesh,
    };
}

auto expand_screen_bounds(
    ScreenBounds bounds,
    double horizontal_ratio,
    double vertical_ratio) -> ScreenBounds
{
    if (bounds.right <= bounds.left || bounds.bottom <= bounds.top)
    {
        return bounds;
    }
    const auto horizontal =
        horizontal_ratio > 0.0
            ? (bounds.right - bounds.left) * horizontal_ratio
            : 0.0;
    const auto vertical =
        vertical_ratio > 0.0
            ? (bounds.bottom - bounds.top) * vertical_ratio
            : 0.0;
    return {
        bounds.left - horizontal,
        bounds.top - vertical,
        bounds.right + horizontal,
        bounds.bottom + vertical,
    };
}

auto projection_scale_from_sample(
    double center,
    double raw_screen,
    double engine_screen) -> double
{
    const auto raw_delta = raw_screen - center;
    if (raw_delta > -1.0 && raw_delta < 1.0)
    {
        return -1.0;
    }
    const auto scale = (engine_screen - center) / raw_delta;
    return scale >= 0.5 && scale <= 2.5 ? scale : -1.0;
}
} // namespace meccha::core
