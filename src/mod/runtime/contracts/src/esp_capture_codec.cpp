#include <meccha/runtime/esp_capture_codec.hpp>

#include <meccha/core/utf8.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

namespace meccha::runtime
{
namespace
{
constexpr auto Pi = 3.14159265358979323846;
constexpr auto MaximumViewportDimension = 16384;
constexpr auto MaximumCapsuleDimension = 1'000'000.0F;
constexpr auto ProjectionCenterTolerance = 2.0;

struct Axes
{
    EspVector3dAbi forward{};
    EspVector3dAbi right{};
    EspVector3dAbi up{};
};

auto finite(EspVector3dAbi value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

auto finite(EspRotatorAbi value) -> bool
{
    return std::isfinite(value.pitch) &&
           std::isfinite(value.yaw) &&
           std::isfinite(value.roll);
}

auto finite(core::EspScreenPoint value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y);
}

auto axes(EspRotatorAbi rotation) -> Axes
{
    const auto pitch = rotation.pitch * Pi / 180.0;
    const auto yaw = rotation.yaw * Pi / 180.0;
    const auto roll = rotation.roll * Pi / 180.0;
    const auto sp = std::sin(pitch);
    const auto cp = std::cos(pitch);
    const auto sy = std::sin(yaw);
    const auto cy = std::cos(yaw);
    const auto sr = std::sin(roll);
    const auto cr = std::cos(roll);
    return Axes{
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

auto transform(
    EspVector3dAbi location,
    const Axes& basis,
    EspVector3dAbi local) -> core::EspWorldPoint
{
    return {
        location.x + local.x * basis.forward.x +
            local.y * basis.right.x + local.z * basis.up.x,
        location.y + local.x * basis.forward.y +
            local.y * basis.right.y + local.z * basis.up.y,
        location.z + local.x * basis.forward.z +
            local.y * basis.right.z + local.z * basis.up.z,
    };
}
} // namespace

auto decode_esp_view(
    EspVector3dAbi location,
    EspRotatorAbi rotation,
    float field_of_view_degrees,
    std::int32_t viewport_width,
    std::int32_t viewport_height)
    -> std::expected<core::EspView, EspCaptureCodecError>
{
    if (viewport_width < 1 || viewport_height < 1 ||
        viewport_width > MaximumViewportDimension ||
        viewport_height > MaximumViewportDimension)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidViewport);
    }
    if (!finite(location) || !finite(rotation))
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidCamera);
    }
    if (!std::isfinite(field_of_view_degrees) ||
        field_of_view_degrees < 20.0F ||
        field_of_view_degrees > 170.0F)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidFieldOfView);
    }

    return core::EspView{
        {location.x, location.y, location.z},
        rotation.pitch,
        rotation.yaw,
        rotation.roll,
        static_cast<double>(field_of_view_degrees),
        static_cast<double>(viewport_width) /
            static_cast<double>(viewport_height),
        core::EspAspectConstraint::MaintainXFov,
        1.0,
        1.0,
    };
}

auto esp_projection_calibration_points(
    const core::EspView& view)
    -> std::expected<
        std::array<core::EspWorldPoint, 2U>,
        EspCaptureCodecError>
{
    const auto location = EspVector3dAbi{
        view.location.x,
        view.location.y,
        view.location.z,
    };
    const auto rotation = EspRotatorAbi{
        view.pitch_degrees,
        view.yaw_degrees,
        view.roll_degrees,
    };
    if (!finite(location) || !finite(rotation) ||
        !std::isfinite(view.field_of_view_degrees) ||
        view.field_of_view_degrees < 20.0 ||
        view.field_of_view_degrees > 170.0)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidCamera);
    }
    const auto basis = axes(rotation);
    return std::array{
        transform(location, basis, {1000.0, 100.0, 0.0}),
        transform(location, basis, {1000.0, 0.0, 100.0}),
    };
}

auto calibrate_esp_view(
    core::EspView view,
    core::EspViewport viewport,
    core::EspScreenPoint horizontal_engine_sample,
    core::EspScreenPoint vertical_engine_sample)
    -> std::expected<core::EspView, EspCaptureCodecError>
{
    if (!finite(horizontal_engine_sample) ||
        !finite(vertical_engine_sample))
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidProjectionSample);
    }
    const auto samples =
        esp_projection_calibration_points(view);
    if (!samples)
    {
        return std::unexpected(samples.error());
    }
    auto raw_view = view;
    raw_view.projection_scale_x = 1.0;
    raw_view.projection_scale_y = 1.0;
    const auto horizontal_raw = core::project_esp_world_point(
        raw_view,
        viewport,
        (*samples)[0U]);
    const auto vertical_raw = core::project_esp_world_point(
        raw_view,
        viewport,
        (*samples)[1U]);
    if (!horizontal_raw || !vertical_raw)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidProjectionSample);
    }
    const auto center = core::EspScreenPoint{
        viewport.width / 2.0,
        viewport.height / 2.0,
    };
    if (std::abs(
            horizontal_engine_sample.y - center.y) >
            ProjectionCenterTolerance ||
        std::abs(
            vertical_engine_sample.x - center.x) >
            ProjectionCenterTolerance)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidProjectionSample);
    }
    const auto scale_x = core::projection_scale_from_sample(
        center.x,
        horizontal_raw->x,
        horizontal_engine_sample.x);
    const auto scale_y = core::projection_scale_from_sample(
        center.y,
        vertical_raw->y,
        vertical_engine_sample.y);
    if (scale_x < 0.0 || scale_y < 0.0)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidProjectionSample);
    }
    view.projection_scale_x = scale_x;
    view.projection_scale_y = scale_y;
    return view;
}

auto sample_esp_capsule(
    EspVector3dAbi location,
    EspRotatorAbi rotation,
    float scaled_radius,
    float scaled_half_height)
    -> std::expected<
        std::vector<core::EspWorldPoint>,
        EspCaptureCodecError>
{
    if (!finite(location) || !finite(rotation) ||
        !std::isfinite(scaled_radius) ||
        !std::isfinite(scaled_half_height) ||
        scaled_radius <= 0.0F ||
        scaled_half_height < scaled_radius ||
        scaled_radius > MaximumCapsuleDimension ||
        scaled_half_height > MaximumCapsuleDimension)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidCapsule);
    }

    const auto basis = axes(rotation);
    const auto radius = static_cast<double>(scaled_radius);
    const auto half_height =
        static_cast<double>(scaled_half_height);
    const auto ring_height = half_height - radius;
    auto samples = std::vector<core::EspWorldPoint>{};
    samples.reserve(18U);
    samples.push_back(transform(
        location,
        basis,
        {0.0, 0.0, half_height}));
    samples.push_back(transform(
        location,
        basis,
        {0.0, 0.0, -half_height}));
    for (auto index = std::size_t{}; index < 8U; ++index)
    {
        const auto angle =
            static_cast<double>(index) * Pi / 4.0;
        const auto x = radius * std::cos(angle);
        const auto y = radius * std::sin(angle);
        samples.push_back(transform(
            location,
            basis,
            {x, y, ring_height}));
        samples.push_back(transform(
            location,
            basis,
            {x, y, -ring_height}));
    }
    return samples;
}

auto build_esp_skeleton_pose(
    std::span<const core::PaintSamplingBone> bones,
    std::span<const EspVector3dAbi> positions)
    -> std::expected<
        core::EspSkeletonPose,
        EspCaptureCodecError>
{
    if (bones.size() < 2U ||
        bones.size() > core::MaximumEspBones ||
        positions.size() != bones.size())
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidSkeleton);
    }
    auto pose = core::EspSkeletonPose{};
    pose.bones.reserve(positions.size());
    pose.edges.reserve(bones.size() - 1U);
    for (auto index = std::size_t{};
         index < bones.size();
         ++index)
    {
        const auto& bone = bones[index];
        const auto duplicate =
            std::ranges::any_of(
                bones.first(index),
                [&](const core::PaintSamplingBone& candidate)
                {
                    return candidate.name == bone.name;
                });
        if (bone.name.empty() || bone.name.size() > 128U ||
            !core::valid_utf8(bone.name) || duplicate ||
            !finite(positions[index]) ||
            (index == 0U && bone.parent) ||
            (index != 0U &&
             (!bone.parent || *bone.parent >= index)))
        {
            return std::unexpected(
                EspCaptureCodecError::InvalidSkeleton);
        }
        pose.bones.push_back(core::EspWorldPoint{
            positions[index].x,
            positions[index].y,
            positions[index].z,
        });
        if (bone.parent)
        {
            pose.edges.push_back(core::EspSkeletonEdge{
                *bone.parent,
                index,
            });
        }
    }
    if (pose.edges.empty() ||
        pose.edges.size() > core::MaximumEspSkeletonEdges)
    {
        return std::unexpected(
            EspCaptureCodecError::InvalidSkeleton);
    }
    return pose;
}

auto validate_esp_skeleton_topology(
    const core::EspSkeletonPose& pose,
    std::span<const core::ImageReferenceBone> reference_bones)
    -> bool
{
    if (pose.bones.size() < 2U ||
        pose.bones.size() != reference_bones.size() ||
        pose.edges.size() != pose.bones.size() - 1U)
    {
        return false;
    }
    const auto distance =
        [](core::EspWorldPoint left,
           core::EspWorldPoint right) -> double
    {
        const auto x = left.x - right.x;
        const auto y = left.y - right.y;
        const auto z = left.z - right.z;
        return std::sqrt(x * x + y * y + z * z);
    };
    auto logarithmic_ratios = std::vector<double>{};
    logarithmic_ratios.reserve(pose.edges.size());
    for (auto child = std::size_t{1U};
         child < pose.bones.size();
         ++child)
    {
        const auto parent = reference_bones[child].parent;
        const auto& edge = pose.edges[child - 1U];
        if (!parent || *parent >= child ||
            edge.parent != *parent || edge.child != child)
        {
            return false;
        }
        const auto reference_length = distance(
            {
                reference_bones[child].position.x,
                reference_bones[child].position.y,
                reference_bones[child].position.z,
            },
            {
                reference_bones[*parent].position.x,
                reference_bones[*parent].position.y,
                reference_bones[*parent].position.z,
            });
        const auto current_length = distance(
            pose.bones[child],
            pose.bones[*parent]);
        if (!std::isfinite(reference_length) ||
            !std::isfinite(current_length))
        {
            return false;
        }
        if (reference_length <= 0.01 ||
            current_length <= 0.01)
        {
            continue;
        }
        logarithmic_ratios.push_back(
            std::log(current_length / reference_length));
    }
    const auto required = std::min(
        pose.edges.size(),
        std::max(
            std::size_t{3U},
            pose.bones.size() / 3U));
    if (logarithmic_ratios.size() < required)
    {
        return false;
    }
    auto mean = 0.0;
    for (const auto value : logarithmic_ratios)
    {
        mean += value;
    }
    mean /= static_cast<double>(logarithmic_ratios.size());
    const auto uniform_scale = std::exp(mean);
    if (!std::isfinite(uniform_scale) ||
        uniform_scale < 0.05 || uniform_scale > 20.0)
    {
        return false;
    }
    auto deviation = 0.0;
    for (const auto value : logarithmic_ratios)
    {
        deviation += std::abs(value - mean);
    }
    deviation /= static_cast<double>(
        logarithmic_ratios.size());
    return std::isfinite(deviation) && deviation <= 0.45;
}

auto should_refresh_esp_capture_directory(
    bool unresolved_active_avatar,
    bool same_scope,
    bool invalid_cached_binding,
    std::uint64_t now_ms,
    std::uint64_t last_refresh_ms,
    std::uint64_t refresh_interval_ms) -> bool
{
    if (!unresolved_active_avatar)
    {
        return false;
    }
    return invalid_cached_binding ||
           core::should_refresh_esp_avatar_directory(
               true,
               !same_scope,
               now_ms,
               last_refresh_ms,
               refresh_interval_ms);
}
} // namespace meccha::runtime
