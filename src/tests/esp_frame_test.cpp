#include <meccha/core/esp.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL esp_frame: " << message << '\n';
    }
    return condition;
}

auto near(double left, double right) -> bool
{
    return std::abs(left - right) < 0.000001;
}

auto target(
    std::uint64_t identity,
    EspRole role,
    std::string name) -> EspTargetCapture
{
    auto bones = std::vector<EspWorldPoint>{
        {100.0, -30.0, -70.0},
        {100.0, -15.0, -50.0},
        {100.0, 0.0, -30.0},
        {100.0, 15.0, -10.0},
        {100.0, 30.0, 10.0},
        {100.0, 15.0, 30.0},
        {100.0, 0.0, 50.0},
        {100.0, -15.0, 70.0},
    };
    auto edges = std::vector<EspSkeletonEdge>{};
    for (auto index = std::size_t{1U};
         index < bones.size();
         ++index)
    {
        edges.push_back(
            EspSkeletonEdge{index - 1U, index});
    }
    return EspTargetCapture{
        identity,
        identity + 100U,
        role,
        EspRole::Unknown,
        std::move(name),
        EspWorldPoint{100.0, 0.0, 0.0},
        {
            {100.0, -35.0, -75.0},
            {100.0, 35.0, 75.0},
        },
        EspSkeletonPose{
            std::move(bones),
            std::move(edges),
        },
    };
}
} // namespace

auto main() -> int
{
    auto passed = true;
    const auto viewport = EspViewport{1920.0, 1080.0};
    const auto view = EspView{};

    const auto center = project_esp_world_point(
        view,
        viewport,
        {100.0, 0.0, 0.0});
    const auto right = project_esp_world_point(
        view,
        viewport,
        {100.0, 100.0, 0.0});
    passed &= expect(
        center && near(center->x, 960.0) &&
            near(center->y, 540.0) &&
            right && right->x > center->x &&
            near(right->y, center->y) &&
            !project_esp_world_point(
                view,
                viewport,
                {-100.0, 0.0, 0.0}),
        "perspective projection axes or behind-camera rejection drifted");

    const auto clipped = clip_esp_line_to_viewport(
        viewport,
        {960.0, 1078.0},
        {4000.0, -1000.0});
    passed &= expect(
        clipped &&
            clipped->first ==
                EspScreenPoint{960.0, 1078.0} &&
            near(clipped->second.x, 1920.0) &&
            clipped->second.y >= 0.0 &&
            clipped->second.y <= 1080.0 &&
            !clip_esp_line_to_viewport(
                viewport,
                {-10.0, -10.0},
                {-20.0, -20.0}),
        "viewport line clipping did not preserve the visible segment");

    auto hider = target(1U, EspRole::Hider, "名前");
    auto hunter = target(2U, EspRole::Hunter, "Hunter");
    auto spectator =
        target(3U, EspRole::Spectator, "Spectator");
    const auto targets = std::vector{
        hider,
        hunter,
        spectator,
    };
    auto settings = EspSettings{};
    settings.scope = EspScope::Hider;
    const auto frame = build_esp_primitive_frame(
        settings,
        view,
        viewport,
        targets);
    const auto box_lines =
        frame
            ? std::ranges::count_if(
                  frame->lines,
                  [](const EspLinePrimitive& line)
                  {
                      return line.kind ==
                             EspPrimitiveKind::Box;
                  })
            : 0;
    const auto skeleton_lines =
        frame
            ? std::ranges::count_if(
                  frame->lines,
                  [](const EspLinePrimitive& line)
                  {
                      return line.kind ==
                             EspPrimitiveKind::Skeleton;
                  })
            : 0;
    const auto snaplines =
        frame
            ? std::ranges::count_if(
                  frame->lines,
                  [](const EspLinePrimitive& line)
                  {
                      return line.kind ==
                             EspPrimitiveKind::Snapline;
                  })
            : 0;
    passed &= expect(
        frame &&
            frame->diagnostics ==
                EspFrameDiagnostics{
                    3U,
                    1U,
                    1U,
                    1U,
                    0U,
                    0U,
                } &&
            box_lines == 8 &&
            skeleton_lines == 7 &&
            snaplines == 1 &&
            frame->texts.size() == 1U &&
            frame->texts.front().utf8 == "名前  1 m" &&
            frame->texts.front().color ==
                settings.hider_color,
        "scope, primitives, localized label, or role color drifted");

    auto hunter_only = EspSettings{};
    hunter_only.scope = EspScope::Hunter;
    const auto hunter_frame = build_esp_primitive_frame(
        hunter_only,
        view,
        viewport,
        targets);
    passed &= expect(
        hunter_frame &&
            hunter_frame->diagnostics.drawn_targets == 1U &&
            hunter_frame->diagnostics.filtered_scope == 1U &&
            hunter_frame->diagnostics.filtered_spectators == 1U &&
            !hunter_frame->lines.empty() &&
            hunter_frame->lines.front().target_identity == 2U &&
            hunter_frame->lines.front().color ==
                hunter_only.hunter_color,
        "hunter-only scope did not exclude hiders and spectators");

    auto no_boxes = EspSettings{};
    no_boxes.boxes = false;
    const auto frame_without_boxes =
        build_esp_primitive_frame(
            no_boxes,
            view,
            viewport,
            std::span<const EspTargetCapture>{&hider, 1U});
    auto no_skeletons = EspSettings{};
    no_skeletons.skeletons = false;
    const auto frame_without_skeletons =
        build_esp_primitive_frame(
            no_skeletons,
            view,
            viewport,
            std::span<const EspTargetCapture>{&hider, 1U});
    auto no_snaplines = EspSettings{};
    no_snaplines.snaplines = false;
    const auto frame_without_snaplines =
        build_esp_primitive_frame(
            no_snaplines,
            view,
            viewport,
            std::span<const EspTargetCapture>{&hider, 1U});
    auto distance_only = EspSettings{};
    distance_only.names = false;
    const auto distance_only_frame =
        build_esp_primitive_frame(
            distance_only,
            view,
            viewport,
            std::span<const EspTargetCapture>{&hider, 1U});
    auto name_only = EspSettings{};
    name_only.distance = false;
    const auto name_only_frame =
        build_esp_primitive_frame(
            name_only,
            view,
            viewport,
            std::span<const EspTargetCapture>{&hider, 1U});
    passed &= expect(
        frame_without_boxes &&
            std::ranges::none_of(
                frame_without_boxes->lines,
                [](const EspLinePrimitive& line)
                {
                    return line.kind ==
                           EspPrimitiveKind::Box;
                }) &&
            frame_without_skeletons &&
            std::ranges::none_of(
                frame_without_skeletons->lines,
                [](const EspLinePrimitive& line)
                {
                    return line.kind ==
                           EspPrimitiveKind::Skeleton;
                }) &&
            frame_without_snaplines &&
            std::ranges::none_of(
                frame_without_snaplines->lines,
                [](const EspLinePrimitive& line)
                {
                    return line.kind ==
                           EspPrimitiveKind::Snapline;
                }) &&
            distance_only_frame &&
            distance_only_frame->texts.size() == 1U &&
            distance_only_frame->texts.front().utf8 == "1 m" &&
            name_only_frame &&
            name_only_frame->texts.size() == 1U &&
            name_only_frame->texts.front().utf8 == "名前",
        "an independent primitive toggle affected another ESP primitive");

    auto x_fov_view = view;
    x_fov_view.aspect_ratio = 4.0 / 3.0;
    x_fov_view.aspect_constraint =
        EspAspectConstraint::MaintainXFov;
    auto major_axis_view = x_fov_view;
    major_axis_view.aspect_constraint =
        EspAspectConstraint::MajorAxisFov;
    const auto x_fov_point = project_esp_world_point(
        x_fov_view,
        viewport,
        {100.0, 100.0, 0.0});
    const auto major_axis_point = project_esp_world_point(
        major_axis_view,
        viewport,
        {100.0, 100.0, 0.0});
    passed &= expect(
        x_fov_point && major_axis_point &&
            near(x_fov_point->x, 1920.0) &&
            near(major_axis_point->x, 1680.0),
        "aspect-constraint projection selection drifted");

    auto behind = EspTargetCapture{
        4U,
        104U,
        EspRole::Hunter,
        EspRole::Unknown,
        {},
        EspWorldPoint{-100.0, 100.0, 0.0},
    };
    auto snapline_only = EspSettings{};
    snapline_only.boxes = false;
    snapline_only.skeletons = false;
    snapline_only.names = false;
    snapline_only.distance = false;
    const auto distant = build_esp_primitive_frame(
        snapline_only,
        view,
        viewport,
        std::span<const EspTargetCapture>{&behind, 1U});
    passed &= expect(
        distant && distant->lines.size() == 1U &&
            distant->lines.front().kind ==
                EspPrimitiveKind::Snapline &&
            near(distant->lines.front().end.x, viewport.width),
        "a behind-camera snapline did not clip to the viewport edge");

    auto current_avatar = hider;
    current_avatar.current_pawn_role = EspRole::Hunter;
    const auto current_role_frame = build_esp_primitive_frame(
        EspSettings{},
        view,
        viewport,
        std::span<const EspTargetCapture>{
            &current_avatar,
            1U});
    passed &= expect(
        current_role_frame &&
            !current_role_frame->lines.empty() &&
            current_role_frame->lines.front().color ==
                EspSettings{}.hunter_color &&
            resolve_esp_target_role(
                EspRole::Hider,
                EspRole::Hunter) ==
                EspRole::Hunter &&
            should_refresh_esp_avatar_directory(
                true,
                false,
                1250U,
                1000U,
                250U) &&
            esp_cached_avatar_binding_usable(
                true,
                true,
                true,
                true,
                EspRole::Hider,
                EspRole::Hider),
        "role change or avatar cache policy retained stale identity");

    auto invalid = hider;
    invalid.skeleton->edges.front().child =
        invalid.skeleton->bones.size();
    const auto invalid_frame = build_esp_primitive_frame(
        settings,
        view,
        viewport,
        std::span<const EspTargetCapture>{&invalid, 1U});
    passed &= expect(
        invalid_frame &&
            std::ranges::count_if(
                invalid_frame->lines,
                [](const EspLinePrimitive& line)
                {
                    return line.kind ==
                           EspPrimitiveKind::Box;
                }) == 8 &&
            std::ranges::none_of(
                invalid_frame->lines,
                [](const EspLinePrimitive& line)
                {
                    return line.kind ==
                           EspPrimitiveKind::Skeleton;
                }) &&
            invalid_frame->diagnostics.invalid_skeletons == 1U,
        "invalid skeleton topology did not fail closed to capsule geometry");

    const auto excessive =
        std::vector<EspTargetCapture>(
            MaximumEspTargets + 1U);
    passed &= expect(
        build_esp_primitive_frame(
            settings,
            view,
            viewport,
            excessive) ==
                std::unexpected(EspFrameError::ResourceLimit) &&
            build_esp_primitive_frame(
                settings,
                EspView{
                    {},
                    0.0,
                    0.0,
                    0.0,
                    std::numeric_limits<double>::quiet_NaN(),
                },
                viewport,
                {}) ==
                std::unexpected(EspFrameError::InvalidView) &&
            build_esp_primitive_frame(
                EspSettings{
                    true,
                    static_cast<EspScope>(255U),
                },
                view,
                viewport,
                {}) ==
                std::unexpected(
                    EspFrameError::InvalidSettings),
        "ESP settings, resource, or finite-view bounds did not fail closed");

    if (passed)
    {
        std::cout << "PASS esp_frame\n";
    }
    return passed ? 0 : 1;
}
