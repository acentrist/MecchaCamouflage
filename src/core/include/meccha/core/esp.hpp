#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

[[nodiscard]] auto should_refresh_esp_avatar_directory(
    bool unresolved_active_avatar,
    bool world_changed,
    std::uint64_t now_ms,
    std::uint64_t last_refresh_ms,
    std::uint64_t refresh_interval_ms) -> bool;

[[nodiscard]] auto esp_cached_avatar_binding_usable(
    bool verified_when_cached,
    bool same_world,
    bool player_still_present,
    bool candidate_live,
    EspRole expected_role,
    EspRole candidate_role) -> bool;

[[nodiscard]] auto resolve_esp_target_role(
    EspRole roster_role,
    EspRole current_pawn_role) -> EspRole;

[[nodiscard]] auto esp_role_color(
    EspRole role,
    Rgb8 hider_color,
    Rgb8 hunter_color) -> Rgb8;

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

inline constexpr std::size_t MaximumEspTargets = 64U;
inline constexpr std::size_t MaximumEspBones = 128U;
inline constexpr std::size_t MaximumEspSkeletonEdges = 127U;
inline constexpr std::size_t MaximumEspLines =
    MaximumEspTargets * (8U + MaximumEspSkeletonEdges + 1U);
inline constexpr std::size_t MaximumEspTexts = MaximumEspTargets;
inline constexpr std::size_t MaximumEspNameBytes = 256U;

struct EspScreenPoint
{
    double x{};
    double y{};

    auto operator==(const EspScreenPoint&) const -> bool = default;
};

struct EspWorldPoint
{
    double x{};
    double y{};
    double z{};

    auto operator==(const EspWorldPoint&) const -> bool = default;
};

struct EspViewport
{
    double width{};
    double height{};

    auto operator==(const EspViewport&) const -> bool = default;
};

enum class EspAspectConstraint : std::uint8_t
{
    MaintainYFov,
    MaintainXFov,
    MajorAxisFov,
};

struct EspView
{
    EspWorldPoint location{};
    double pitch_degrees{};
    double yaw_degrees{};
    double roll_degrees{};
    double field_of_view_degrees{90.0};
    double aspect_ratio{16.0 / 9.0};
    EspAspectConstraint aspect_constraint{
        EspAspectConstraint::MaintainYFov};
    double projection_scale_x{1.0};
    double projection_scale_y{1.0};

    auto operator==(const EspView&) const -> bool = default;
};

struct EspSkeletonEdge
{
    std::size_t parent{};
    std::size_t child{};

    auto operator==(const EspSkeletonEdge&) const -> bool = default;
};

struct EspSkeletonPose
{
    std::vector<EspWorldPoint> bones{};
    std::vector<EspSkeletonEdge> edges{};

    auto operator==(const EspSkeletonPose&) const -> bool = default;
};

struct EspTargetCapture
{
    std::uint64_t player_identity{};
    std::uint64_t avatar_identity{};
    EspRole roster_role{EspRole::Unknown};
    EspRole current_pawn_role{EspRole::Unknown};
    std::string display_name{};
    std::optional<EspWorldPoint> origin{};
    std::vector<EspWorldPoint> capsule_samples{};
    std::optional<EspSkeletonPose> skeleton{};

    auto operator==(const EspTargetCapture&) const -> bool = default;
};

enum class EspPrimitiveKind : std::uint8_t
{
    Box,
    Skeleton,
    Snapline,
};

struct EspLinePrimitive
{
    std::uint64_t target_identity{};
    EspPrimitiveKind kind{EspPrimitiveKind::Box};
    EspScreenPoint start{};
    EspScreenPoint end{};
    Rgb8 color{};
    double thickness{};

    auto operator==(const EspLinePrimitive&) const -> bool = default;
};

struct EspTextPrimitive
{
    std::uint64_t target_identity{};
    EspScreenPoint anchor{};
    std::string utf8{};
    Rgb8 color{};

    auto operator==(const EspTextPrimitive&) const -> bool = default;
};

struct EspFrameDiagnostics
{
    std::size_t captured_targets{};
    std::size_t drawn_targets{};
    std::size_t filtered_spectators{};
    std::size_t filtered_scope{};
    std::size_t invalid_targets{};
    std::size_t missing_spatial_geometry{};
    std::size_t invalid_capsules{};
    std::size_t invalid_skeletons{};

    auto operator==(const EspFrameDiagnostics&) const -> bool = default;
};

struct EspPrimitiveFrame
{
    std::vector<EspLinePrimitive> lines{};
    std::vector<EspTextPrimitive> texts{};
    EspFrameDiagnostics diagnostics{};
};

enum class EspFrameError : std::uint8_t
{
    InvalidSettings,
    InvalidViewport,
    InvalidView,
    ResourceLimit,
};

[[nodiscard]] auto project_esp_world_point(
    const EspView& view,
    EspViewport viewport,
    EspWorldPoint world) -> std::optional<EspScreenPoint>;

[[nodiscard]] auto project_esp_snapline_target(
    const EspView& view,
    EspViewport viewport,
    EspWorldPoint world) -> std::optional<EspScreenPoint>;

[[nodiscard]] auto clip_esp_line_to_viewport(
    EspViewport viewport,
    EspScreenPoint start,
    EspScreenPoint end)
    -> std::optional<
        std::pair<EspScreenPoint, EspScreenPoint>>;

[[nodiscard]] auto build_esp_primitive_frame(
    const EspSettings& settings,
    const EspView& view,
    EspViewport viewport,
    std::span<const EspTargetCapture> targets)
    -> std::expected<EspPrimitiveFrame, EspFrameError>;
} // namespace meccha::core
