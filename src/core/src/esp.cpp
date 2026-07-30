#include <meccha/core/esp.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto Pi = 3.14159265358979323846;
constexpr auto ProjectionEpsilon = 0.000001;

auto finite(EspScreenPoint point) -> bool
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

auto finite(EspWorldPoint point) -> bool
{
    return std::isfinite(point.x) &&
           std::isfinite(point.y) &&
           std::isfinite(point.z);
}

auto valid_viewport(EspViewport viewport) -> bool
{
    return std::isfinite(viewport.width) &&
           std::isfinite(viewport.height) &&
           viewport.width >= 1.0 &&
           viewport.height >= 1.0 &&
           viewport.width <= 16384.0 &&
           viewport.height <= 16384.0;
}

auto valid_view(const EspView& view) -> bool
{
    const auto valid_constraint =
        view.aspect_constraint ==
            EspAspectConstraint::MaintainYFov ||
        view.aspect_constraint ==
            EspAspectConstraint::MaintainXFov ||
        view.aspect_constraint ==
            EspAspectConstraint::MajorAxisFov;
    return finite(view.location) &&
           std::isfinite(view.pitch_degrees) &&
           std::isfinite(view.yaw_degrees) &&
           std::isfinite(view.roll_degrees) &&
           std::isfinite(view.field_of_view_degrees) &&
           view.field_of_view_degrees >= 20.0 &&
           view.field_of_view_degrees <= 170.0 &&
           std::isfinite(view.aspect_ratio) &&
           view.aspect_ratio >= 0.1 &&
           view.aspect_ratio <= 100.0 &&
           std::isfinite(view.projection_scale_x) &&
           std::isfinite(view.projection_scale_y) &&
           view.projection_scale_x >= 0.5 &&
           view.projection_scale_x <= 2.5 &&
           view.projection_scale_y >= 0.5 &&
           view.projection_scale_y <= 2.5 &&
           valid_constraint;
}

struct EspAxes
{
    EspWorldPoint forward{};
    EspWorldPoint right{};
    EspWorldPoint up{};
};

auto axes(const EspView& view) -> EspAxes
{
    const auto pitch =
        view.pitch_degrees * Pi / 180.0;
    const auto yaw =
        view.yaw_degrees * Pi / 180.0;
    const auto roll =
        view.roll_degrees * Pi / 180.0;
    const auto sp = std::sin(pitch);
    const auto cp = std::cos(pitch);
    const auto sy = std::sin(yaw);
    const auto cy = std::cos(yaw);
    const auto sr = std::sin(roll);
    const auto cr = std::cos(roll);
    return EspAxes{
        {cp * cy, cp * sy, sp},
        {
            sr * sp * cy - cr * sy,
            sr * sp * sy + cr * cy,
            -sr * cp,
        },
        {
            -(cr * sp * cy + sr * sy),
            cy * sr - cr * sp * sy,
            cr * cp,
        },
    };
}

auto subtract(EspWorldPoint left, EspWorldPoint right)
    -> EspWorldPoint
{
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

auto dot(EspWorldPoint left, EspWorldPoint right) -> double
{
    return left.x * right.x +
           left.y * right.y +
           left.z * right.z;
}

auto clipped_axis(
    double edge,
    double delta,
    double& lower,
    double& upper) -> bool
{
    if (std::abs(delta) < ProjectionEpsilon)
    {
        return edge >= 0.0;
    }
    const auto ratio = edge / delta;
    if (delta < 0.0)
    {
        lower = std::max(lower, ratio);
    }
    else
    {
        upper = std::min(upper, ratio);
    }
    return lower <= upper;
}

auto valid_name(std::string_view name) -> bool
{
    if (name.empty() ||
        name.size() > MaximumEspNameBytes ||
        !valid_utf8(name))
    {
        return false;
    }
    return !std::all_of(
        name.begin(),
        name.end(),
        [](unsigned char value)
        {
            return value >= '0' && value <= '9';
        });
}

auto valid_pose(const EspSkeletonPose& pose) -> bool
{
    if (pose.bones.size() < 2U ||
        pose.bones.size() > MaximumEspBones ||
        pose.edges.empty() ||
        pose.edges.size() > MaximumEspSkeletonEdges)
    {
        return false;
    }
    for (const auto point : pose.bones)
    {
        if (!finite(point))
        {
            return false;
        }
    }
    for (const auto edge : pose.edges)
    {
        if (edge.parent >= pose.bones.size() ||
            edge.child >= pose.bones.size() ||
            edge.parent == edge.child)
        {
            return false;
        }
    }
    return true;
}

auto projected_bounds(
    const EspView& view,
    EspViewport viewport,
    std::span<const EspWorldPoint> points,
    std::size_t minimum_projected)
    -> std::optional<ScreenBounds>
{
    auto bounds = ScreenBounds{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
    };
    auto projected = std::size_t{};
    for (const auto point : points)
    {
        const auto screen =
            project_esp_world_point(view, viewport, point);
        if (!screen)
        {
            continue;
        }
        bounds.left = std::min(bounds.left, screen->x);
        bounds.top = std::min(bounds.top, screen->y);
        bounds.right = std::max(bounds.right, screen->x);
        bounds.bottom = std::max(bounds.bottom, screen->y);
        ++projected;
    }
    if (projected < minimum_projected ||
        bounds.right - bounds.left < 2.0 ||
        bounds.bottom - bounds.top < 2.0)
    {
        return std::nullopt;
    }
    return bounds;
}

auto add_line(
    EspPrimitiveFrame& frame,
    EspViewport viewport,
    std::uint64_t target_identity,
    EspPrimitiveKind kind,
    EspScreenPoint start,
    EspScreenPoint end,
    Rgb8 color,
    double thickness) -> bool
{
    if (frame.lines.size() >= MaximumEspLines)
    {
        return false;
    }
    const auto clipped =
        clip_esp_line_to_viewport(
            viewport,
            start,
            end);
    if (!clipped)
    {
        return false;
    }
    frame.lines.push_back(EspLinePrimitive{
        target_identity,
        kind,
        clipped->first,
        clipped->second,
        color,
        thickness,
    });
    return true;
}

auto add_corner_box(
    EspPrimitiveFrame& frame,
    EspViewport viewport,
    std::uint64_t target_identity,
    ScreenBounds bounds,
    Rgb8 color) -> bool
{
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;
    const auto horizontal = std::min(
        std::max(3.0, width * 0.30),
        width / 2.0);
    const auto vertical = std::min(
        std::max(3.0, height * 0.20),
        height / 2.0);
    const auto segments = std::array{
        std::pair{
            EspScreenPoint{bounds.left, bounds.top},
            EspScreenPoint{bounds.left + horizontal, bounds.top}},
        std::pair{
            EspScreenPoint{bounds.left, bounds.top},
            EspScreenPoint{bounds.left, bounds.top + vertical}},
        std::pair{
            EspScreenPoint{bounds.right, bounds.top},
            EspScreenPoint{bounds.right - horizontal, bounds.top}},
        std::pair{
            EspScreenPoint{bounds.right, bounds.top},
            EspScreenPoint{bounds.right, bounds.top + vertical}},
        std::pair{
            EspScreenPoint{bounds.left, bounds.bottom},
            EspScreenPoint{bounds.left + horizontal, bounds.bottom}},
        std::pair{
            EspScreenPoint{bounds.left, bounds.bottom},
            EspScreenPoint{bounds.left, bounds.bottom - vertical}},
        std::pair{
            EspScreenPoint{bounds.right, bounds.bottom},
            EspScreenPoint{bounds.right - horizontal, bounds.bottom}},
        std::pair{
            EspScreenPoint{bounds.right, bounds.bottom},
            EspScreenPoint{bounds.right, bounds.bottom - vertical}},
    };
    auto added = false;
    for (const auto& [start, end] : segments)
    {
        added |= add_line(
            frame,
            viewport,
            target_identity,
            EspPrimitiveKind::Box,
            start,
            end,
            color,
            2.0);
    }
    return added;
}
} // namespace

auto validate(const EspSettings& settings)
    -> std::vector<EspSettingField>
{
    if (settings.scope != EspScope::All &&
        settings.scope != EspScope::Hider &&
        settings.scope != EspScope::Hunter)
    {
        return {EspSettingField::Scope};
    }
    return {};
}

auto esp_scope_matches(EspScope scope, EspRole role) -> bool
{
    return role != EspRole::Spectator &&
           (scope == EspScope::All ||
            (scope == EspScope::Hider && role == EspRole::Hider) ||
            (scope == EspScope::Hunter && role == EspRole::Hunter));
}

auto should_refresh_esp_avatar_directory(
    bool unresolved_active_avatar,
    bool world_changed,
    std::uint64_t now_ms,
    std::uint64_t last_refresh_ms,
    std::uint64_t refresh_interval_ms) -> bool
{
    if (!unresolved_active_avatar)
    {
        return false;
    }
    return world_changed ||
           last_refresh_ms == 0U ||
           now_ms < last_refresh_ms ||
           now_ms - last_refresh_ms >= refresh_interval_ms;
}

auto esp_cached_avatar_binding_usable(
    bool verified_when_cached,
    bool same_world,
    bool player_still_present,
    bool candidate_live,
    EspRole expected_role,
    EspRole candidate_role) -> bool
{
    return verified_when_cached &&
           same_world &&
           player_still_present &&
           candidate_live &&
           (expected_role == EspRole::Hider ||
            expected_role == EspRole::Hunter) &&
           candidate_role == expected_role;
}

auto resolve_esp_target_role(
    EspRole roster_role,
    EspRole current_pawn_role) -> EspRole
{
    return current_pawn_role == EspRole::Unknown
               ? roster_role
               : current_pawn_role;
}

auto esp_role_color(
    EspRole role,
    Rgb8 hider_color,
    Rgb8 hunter_color) -> Rgb8
{
    return role == EspRole::Hider
               ? hider_color
               : role == EspRole::Hunter
                     ? hunter_color
                     : Rgb8{255U, 255U, 255U};
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

auto project_esp_world_point(
    const EspView& view,
    EspViewport viewport,
    EspWorldPoint world) -> std::optional<EspScreenPoint>
{
    if (!valid_viewport(viewport) ||
        !valid_view(view) ||
        !finite(world))
    {
        return std::nullopt;
    }

    const auto basis = axes(view);
    const auto delta = subtract(world, view.location);
    const auto depth = dot(delta, basis.forward);
    if (!std::isfinite(depth) || depth < 1.0)
    {
        return std::nullopt;
    }
    const auto tangent = std::tan(
        view.field_of_view_degrees * Pi / 360.0);
    if (!std::isfinite(tangent) ||
        std::abs(tangent) < ProjectionEpsilon)
    {
        return std::nullopt;
    }

    auto constraint = view.aspect_constraint;
    if (constraint == EspAspectConstraint::MajorAxisFov)
    {
        constraint =
            viewport.width / viewport.height >=
                    view.aspect_ratio
                ? EspAspectConstraint::MaintainYFov
                : EspAspectConstraint::MaintainXFov;
    }
    const auto focal_y =
        constraint == EspAspectConstraint::MaintainYFov
            ? viewport.height / 2.0 / tangent
            : viewport.width / 2.0 / tangent /
                  view.aspect_ratio;
    const auto focal_x =
        constraint == EspAspectConstraint::MaintainYFov
            ? focal_y * view.aspect_ratio
            : viewport.width / 2.0 / tangent;
    const auto center = EspScreenPoint{
        viewport.width / 2.0,
        viewport.height / 2.0,
    };
    auto screen = EspScreenPoint{
        center.x +
            dot(delta, basis.right) * focal_x / depth,
        center.y -
            dot(delta, basis.up) * focal_y / depth,
    };
    screen.x =
        center.x +
        (screen.x - center.x) *
            view.projection_scale_x;
    screen.y =
        center.y +
        (screen.y - center.y) *
            view.projection_scale_y;
    return finite(screen)
               ? std::optional<EspScreenPoint>{screen}
               : std::nullopt;
}

auto project_esp_snapline_target(
    const EspView& view,
    EspViewport viewport,
    EspWorldPoint world) -> std::optional<EspScreenPoint>
{
    if (const auto projected =
            project_esp_world_point(
                view,
                viewport,
                world))
    {
        return projected;
    }
    if (!valid_viewport(viewport) ||
        !valid_view(view) ||
        !finite(world))
    {
        return std::nullopt;
    }

    const auto basis = axes(view);
    const auto delta = subtract(world, view.location);
    auto horizontal = dot(delta, basis.right);
    const auto vertical = dot(delta, basis.up);
    if (!std::isfinite(horizontal) ||
        !std::isfinite(vertical))
    {
        return std::nullopt;
    }
    if (std::abs(horizontal) < ProjectionEpsilon &&
        std::abs(vertical) < ProjectionEpsilon)
    {
        horizontal = 1.0;
    }
    const auto normalization = std::max(
        {std::abs(horizontal), std::abs(vertical), 1.0});
    const auto extent =
        std::max(viewport.width, viewport.height) * 2.0;
    const auto screen = EspScreenPoint{
        viewport.width / 2.0 +
            horizontal / normalization * extent,
        viewport.height / 2.0 -
            vertical / normalization * extent,
    };
    return finite(screen)
               ? std::optional<EspScreenPoint>{screen}
               : std::nullopt;
}

auto clip_esp_line_to_viewport(
    EspViewport viewport,
    EspScreenPoint start,
    EspScreenPoint end)
    -> std::optional<
        std::pair<EspScreenPoint, EspScreenPoint>>
{
    if (!valid_viewport(viewport) ||
        !finite(start) ||
        !finite(end))
    {
        return std::nullopt;
    }
    const auto dx = end.x - start.x;
    const auto dy = end.y - start.y;
    if (std::abs(dx) < ProjectionEpsilon &&
        std::abs(dy) < ProjectionEpsilon)
    {
        return std::nullopt;
    }

    auto lower = 0.0;
    auto upper = 1.0;
    if (!clipped_axis(start.x, -dx, lower, upper) ||
        !clipped_axis(
            viewport.width - start.x,
            dx,
            lower,
            upper) ||
        !clipped_axis(start.y, -dy, lower, upper) ||
        !clipped_axis(
            viewport.height - start.y,
            dy,
            lower,
            upper))
    {
        return std::nullopt;
    }

    const auto clipped_start = EspScreenPoint{
        std::clamp(
            start.x + lower * dx,
            0.0,
            viewport.width),
        std::clamp(
            start.y + lower * dy,
            0.0,
            viewport.height),
    };
    const auto clipped_end = EspScreenPoint{
        std::clamp(
            start.x + upper * dx,
            0.0,
            viewport.width),
        std::clamp(
            start.y + upper * dy,
            0.0,
            viewport.height),
    };
    return finite(clipped_start) && finite(clipped_end)
               ? std::optional{std::pair{
                     clipped_start,
                     clipped_end}}
               : std::nullopt;
}

auto build_esp_primitive_frame(
    const EspSettings& settings,
    const EspView& view,
    EspViewport viewport,
    std::span<const EspTargetCapture> targets)
    -> std::expected<EspPrimitiveFrame, EspFrameError>
{
    if (settings.scope != EspScope::All &&
        settings.scope != EspScope::Hider &&
        settings.scope != EspScope::Hunter)
    {
        return std::unexpected(
            EspFrameError::InvalidSettings);
    }
    if (!valid_viewport(viewport))
    {
        return std::unexpected(
            EspFrameError::InvalidViewport);
    }
    if (!valid_view(view))
    {
        return std::unexpected(EspFrameError::InvalidView);
    }
    if (targets.size() > MaximumEspTargets)
    {
        return std::unexpected(
            EspFrameError::ResourceLimit);
    }

    auto frame = EspPrimitiveFrame{};
    frame.lines.reserve(std::min(
        MaximumEspLines,
        targets.size() * std::size_t{16U}));
    frame.texts.reserve(targets.size());
    frame.diagnostics.captured_targets = targets.size();
    if (!settings.enabled)
    {
        return frame;
    }

    for (const auto& target : targets)
    {
        const auto role = resolve_esp_target_role(
            target.roster_role,
            target.current_pawn_role);
        if (!esp_scope_matches(settings.scope, role))
        {
            if (role == EspRole::Spectator)
            {
                ++frame.diagnostics.filtered_spectators;
            }
            else
            {
                ++frame.diagnostics.filtered_scope;
            }
            continue;
        }
        if (target.player_identity == 0U ||
            target.avatar_identity == 0U)
        {
            ++frame.diagnostics.invalid_targets;
            continue;
        }

        const auto origin =
            target.origin && finite(*target.origin)
                ? target.origin
                : std::nullopt;
        const auto capsule_valid =
            target.capsule_samples.size() <= 18U &&
            std::ranges::all_of(
                target.capsule_samples,
                [](EspWorldPoint point)
                {
                    return finite(point);
                });
        if (!capsule_valid)
        {
            ++frame.diagnostics.invalid_capsules;
        }
        const auto* pose =
            target.skeleton &&
                    valid_pose(*target.skeleton)
                ? &*target.skeleton
                : nullptr;
        if (target.skeleton && pose == nullptr)
        {
            ++frame.diagnostics.invalid_skeletons;
        }

        const auto color = esp_role_color(
            role,
            settings.hider_color,
            settings.hunter_color);
        auto bounds = projected_bounds(
            view,
            viewport,
            capsule_valid
                ? std::span<const EspWorldPoint>{
                      target.capsule_samples}
                : std::span<const EspWorldPoint>{},
            2U);
        if (bounds)
        {
            bounds = expand_screen_bounds(
                *bounds,
                0.08,
                0.04);
        }

        auto projected_bones =
            std::vector<std::optional<EspScreenPoint>>{};
        if (pose != nullptr)
        {
            projected_bones.reserve(
                pose->bones.size());
            for (const auto bone : pose->bones)
            {
                projected_bones.push_back(
                    project_esp_world_point(
                        view,
                        viewport,
                        bone));
            }
            auto pose_points = std::vector<EspWorldPoint>{};
            auto included = std::vector<bool>(
                pose->bones.size(),
                false);
            for (const auto edge : pose->edges)
            {
                for (const auto index :
                     {edge.parent, edge.child})
                {
                    if (!included[index])
                    {
                        included[index] = true;
                        pose_points.push_back(
                            pose->bones[index]);
                    }
                }
            }
            if (auto pose_bounds = projected_bounds(
                    view,
                    viewport,
                    pose_points,
                    8U))
            {
                bounds = expand_screen_bounds(
                    *pose_bounds,
                    0.10,
                    0.05);
            }
        }

        auto wrote = false;
        if (settings.boxes && bounds)
        {
            wrote |= add_corner_box(
                frame,
                viewport,
                target.player_identity,
                *bounds,
                color);
        }
        if (settings.skeletons && pose != nullptr)
        {
            for (const auto edge : pose->edges)
            {
                if (projected_bones[edge.parent] &&
                    projected_bones[edge.child])
                {
                    wrote |= add_line(
                        frame,
                        viewport,
                        target.player_identity,
                        EspPrimitiveKind::Skeleton,
                        *projected_bones[edge.parent],
                        *projected_bones[edge.child],
                        color,
                        1.5);
                }
            }
        }

        auto label_anchor =
            std::optional<EspScreenPoint>{};
        if (bounds)
        {
            label_anchor = EspScreenPoint{
                std::clamp(
                    (bounds->left + bounds->right) / 2.0,
                    2.0,
                    std::max(2.0, viewport.width - 2.0)),
                std::clamp(
                    bounds->top - 20.0,
                    2.0,
                    std::max(2.0, viewport.height - 2.0)),
            };
        }
        else if (origin)
        {
            label_anchor = project_esp_world_point(
                view,
                viewport,
                *origin);
        }

        if (settings.snaplines)
        {
            const auto start = EspScreenPoint{
                viewport.width / 2.0,
                std::max(0.0, viewport.height - 2.0),
            };
            auto end = std::optional<EspScreenPoint>{};
            if (bounds)
            {
                end = EspScreenPoint{
                    (bounds->left + bounds->right) / 2.0,
                    bounds->bottom,
                };
            }
            else if (origin)
            {
                end = project_esp_snapline_target(
                    view,
                    viewport,
                    *origin);
            }
            if (end)
            {
                wrote |= add_line(
                    frame,
                    viewport,
                    target.player_identity,
                    EspPrimitiveKind::Snapline,
                    start,
                    *end,
                    color,
                    1.25);
            }
        }

        auto label = std::string{};
        if (settings.names &&
            valid_name(target.display_name))
        {
            label = target.display_name;
        }
        if (settings.distance &&
            origin)
        {
            const auto delta =
                subtract(*origin, view.location);
            const auto centimeters = std::sqrt(dot(delta, delta));
            const auto meters = centimeters / 100.0;
            if (std::isfinite(meters) &&
                meters >= 0.0 &&
                meters < 100000.0)
            {
                if (!label.empty())
                {
                    label += "  ";
                }
                label += std::to_string(
                    static_cast<long long>(
                        std::llround(meters)));
                label += " m";
            }
        }
        if (!label.empty() && label_anchor &&
            frame.texts.size() < MaximumEspTexts)
        {
            frame.texts.push_back(EspTextPrimitive{
                target.player_identity,
                *label_anchor,
                std::move(label),
                color,
            });
            wrote = true;
        }

        if (wrote)
        {
            ++frame.diagnostics.drawn_targets;
        }
        else if (!bounds && !origin)
        {
            ++frame.diagnostics.missing_spatial_geometry;
        }
    }
    return frame;
}
} // namespace meccha::core
