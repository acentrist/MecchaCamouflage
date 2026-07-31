#pragma once

#include <meccha/core/esp.hpp>
#include <meccha/core/paint_sampling_profile.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace meccha::runtime
{
struct EspVector3dAbi
{
    double x{};
    double y{};
    double z{};

    auto operator==(const EspVector3dAbi&) const -> bool = default;
};

struct EspRotatorAbi
{
    double pitch{};
    double yaw{};
    double roll{};

    auto operator==(const EspRotatorAbi&) const -> bool = default;
};

struct EspVector2dAbi
{
    double x{};
    double y{};

    auto operator==(const EspVector2dAbi&) const -> bool = default;
};

struct EspNameAbi
{
    std::uint32_t comparison_index{};
    std::uint32_t display_index{};
    std::uint32_t number{};
};

struct EspVectorReturnParametersAbi
{
    EspVector3dAbi return_value{};
};

struct EspRotatorReturnParametersAbi
{
    EspRotatorAbi return_value{};
};

struct EspFloatReturnParametersAbi
{
    float return_value{};
};

struct EspProjectWorldLocationToScreenParametersAbi
{
    EspVector3dAbi world_location{};
    EspVector2dAbi screen_location{};
    bool player_viewport_relative{};
    bool return_value{};
    std::array<std::byte, 6U> padding{};
};

struct EspGetSocketLocationParametersAbi
{
    EspNameAbi in_socket_name{};
    std::uint32_t padding{};
    EspVector3dAbi return_value{};
};

static_assert(sizeof(EspVector3dAbi) == 0x18U);
static_assert(sizeof(EspRotatorAbi) == 0x18U);
static_assert(sizeof(EspVector2dAbi) == 0x10U);
static_assert(sizeof(EspNameAbi) == 0x0CU);
static_assert(sizeof(EspVectorReturnParametersAbi) == 0x18U);
static_assert(sizeof(EspRotatorReturnParametersAbi) == 0x18U);
static_assert(sizeof(EspFloatReturnParametersAbi) == 0x04U);
static_assert(
    sizeof(EspProjectWorldLocationToScreenParametersAbi) ==
    0x30U);
static_assert(
    offsetof(
        EspProjectWorldLocationToScreenParametersAbi,
        screen_location) == 0x18U);
static_assert(
    offsetof(
        EspProjectWorldLocationToScreenParametersAbi,
        player_viewport_relative) == 0x28U);
static_assert(
    offsetof(
        EspProjectWorldLocationToScreenParametersAbi,
        return_value) == 0x29U);
static_assert(
    sizeof(EspGetSocketLocationParametersAbi) == 0x28U);
static_assert(
    offsetof(
        EspGetSocketLocationParametersAbi,
        return_value) == 0x10U);

enum class EspCaptureCodecError : std::uint8_t
{
    InvalidViewport,
    InvalidCamera,
    InvalidFieldOfView,
    InvalidCapsule,
    InvalidProjectionSample,
    InvalidSkeleton,
};

[[nodiscard]] auto decode_esp_view(
    EspVector3dAbi location,
    EspRotatorAbi rotation,
    float field_of_view_degrees,
    std::int32_t viewport_width,
    std::int32_t viewport_height)
    -> std::expected<core::EspView, EspCaptureCodecError>;

[[nodiscard]] auto esp_projection_calibration_points(
    const core::EspView& view)
    -> std::expected<
        std::array<core::EspWorldPoint, 2U>,
        EspCaptureCodecError>;

[[nodiscard]] auto calibrate_esp_view(
    core::EspView view,
    core::EspViewport viewport,
    core::EspScreenPoint horizontal_engine_sample,
    core::EspScreenPoint vertical_engine_sample)
    -> std::expected<core::EspView, EspCaptureCodecError>;

[[nodiscard]] auto sample_esp_capsule(
    EspVector3dAbi location,
    EspRotatorAbi rotation,
    float scaled_radius,
    float scaled_half_height)
    -> std::expected<
        std::vector<core::EspWorldPoint>,
        EspCaptureCodecError>;

[[nodiscard]] auto build_esp_skeleton_pose(
    std::span<const core::PaintSamplingBone> bones,
    std::span<const EspVector3dAbi> positions)
    -> std::expected<
        core::EspSkeletonPose,
        EspCaptureCodecError>;

[[nodiscard]] auto validate_esp_skeleton_topology(
    const core::EspSkeletonPose& pose,
    std::span<const core::ImageReferenceBone> reference_bones)
    -> bool;

[[nodiscard]] auto should_refresh_esp_capture_directory(
    bool unresolved_active_avatar,
    bool same_scope,
    bool invalid_cached_binding,
    std::uint64_t now_ms,
    std::uint64_t last_refresh_ms,
    std::uint64_t refresh_interval_ms) -> bool;
} // namespace meccha::runtime
