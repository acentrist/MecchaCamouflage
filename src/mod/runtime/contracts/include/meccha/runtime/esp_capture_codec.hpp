#pragma once

#include <meccha/core/esp.hpp>

#include <cstdint>
#include <expected>
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

static_assert(sizeof(EspVector3dAbi) == 0x18U);
static_assert(sizeof(EspRotatorAbi) == 0x18U);
static_assert(sizeof(EspVectorReturnParametersAbi) == 0x18U);
static_assert(sizeof(EspRotatorReturnParametersAbi) == 0x18U);
static_assert(sizeof(EspFloatReturnParametersAbi) == 0x04U);

enum class EspCaptureCodecError : std::uint8_t
{
    InvalidViewport,
    InvalidCamera,
    InvalidFieldOfView,
    InvalidCapsule,
};

[[nodiscard]] auto decode_esp_view(
    EspVector3dAbi location,
    EspRotatorAbi rotation,
    float field_of_view_degrees,
    std::int32_t viewport_width,
    std::int32_t viewport_height)
    -> std::expected<core::EspView, EspCaptureCodecError>;

[[nodiscard]] auto sample_esp_capsule(
    EspVector3dAbi location,
    EspRotatorAbi rotation,
    float scaled_radius,
    float scaled_half_height)
    -> std::expected<
        std::vector<core::EspWorldPoint>,
        EspCaptureCodecError>;

[[nodiscard]] auto should_refresh_esp_capture_directory(
    bool unresolved_active_avatar,
    bool same_scope,
    bool invalid_cached_binding,
    std::uint64_t now_ms,
    std::uint64_t last_refresh_ms,
    std::uint64_t refresh_interval_ms) -> bool;
} // namespace meccha::runtime
