#include <meccha/runtime/esp_capture_codec.hpp>

#include <array>
#include <cmath>

namespace meccha::runtime
{
namespace
{
constexpr auto Pi = 3.14159265358979323846;
constexpr auto MaximumViewportDimension = 16384;
constexpr auto MaximumCapsuleDimension = 1'000'000.0F;

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
