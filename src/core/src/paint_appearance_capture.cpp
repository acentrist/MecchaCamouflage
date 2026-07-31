#include <meccha/core/paint_appearance_capture.hpp>
#include <meccha/core/paint_capture_request.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
auto pixel_count(
    const PaintAppearanceCameraFingerprint& camera)
    -> std::optional<std::size_t>
{
    if (camera.width == 0U || camera.height == 0U ||
        camera.width > MaximumPaintCaptureDimension ||
        camera.height > MaximumPaintCaptureDimension ||
        camera.width >
            std::numeric_limits<std::size_t>::max() /
                camera.height)
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(camera.width) *
           static_cast<std::size_t>(camera.height);
}

template <typename Pixel>
auto valid_pass(
    const PaintAppearanceCapturedPass<Pixel>& pass,
    std::size_t expected_pixels) -> bool
{
    return pass.pixels != nullptr &&
           pass.pixels->size() == expected_pixels;
}

auto dot(Vector3d left, EspWorldPoint right) -> double
{
    return left.x * right.x +
           left.y * right.y +
           left.z * right.z;
}

auto camera_valid(
    const PaintAppearanceCameraFingerprint& camera) -> bool
{
    const auto direction_length = std::sqrt(
        camera.direction.x * camera.direction.x +
        camera.direction.y * camera.direction.y +
        camera.direction.z * camera.direction.z);
    return pixel_count(camera).has_value() &&
           std::isfinite(camera.viewport_width) &&
           std::isfinite(camera.viewport_height) &&
           camera.viewport_width >= 1.0 &&
           camera.viewport_height >= 1.0 &&
           std::isfinite(camera.location.x) &&
           std::isfinite(camera.location.y) &&
           std::isfinite(camera.location.z) &&
           std::isfinite(camera.direction.x) &&
           std::isfinite(camera.direction.y) &&
           std::isfinite(camera.direction.z) &&
           direction_length > 1.0e-12 &&
           std::isfinite(camera.field_of_view_degrees) &&
           camera.field_of_view_degrees >= 20.0 &&
           camera.field_of_view_degrees <= 170.0;
}

auto camera_matches(
    const PaintAppearanceCameraFingerprint& expected,
    const PaintAppearanceCameraFingerprint& actual) -> bool
{
    if (!camera_valid(expected) || !camera_valid(actual) ||
        expected.width != actual.width ||
        expected.height != actual.height ||
        expected.viewport_width != actual.viewport_width ||
        expected.viewport_height != actual.viewport_height ||
        std::abs(
            expected.field_of_view_degrees -
            actual.field_of_view_degrees) > 0.10)
    {
        return false;
    }
    const auto dx = expected.location.x - actual.location.x;
    const auto dy = expected.location.y - actual.location.y;
    const auto dz = expected.location.z - actual.location.z;
    const auto location_error =
        std::sqrt(dx * dx + dy * dy + dz * dz);
    const auto expected_length = std::sqrt(
        expected.direction.x * expected.direction.x +
        expected.direction.y * expected.direction.y +
        expected.direction.z * expected.direction.z);
    const auto actual_length = std::sqrt(
        actual.direction.x * actual.direction.x +
        actual.direction.y * actual.direction.y +
        actual.direction.z * actual.direction.z);
    const auto direction_dot =
        (expected.direction.x * actual.direction.x +
         expected.direction.y * actual.direction.y +
         expected.direction.z * actual.direction.z) /
        (expected_length * actual_length);
    constexpr auto Pi = 3.14159265358979323846;
    return location_error <= 1.0 &&
           direction_dot >= std::cos(0.10 * Pi / 180.0);
}
} // namespace

auto make_paint_appearance_camera_fingerprint(
    const EspView& view,
    EspViewport viewport,
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<
        PaintAppearanceCameraFingerprint,
        PaintAppearanceCaptureError>
{
    const auto count = pixel_count(
        PaintAppearanceCameraFingerprint{width, height});
    if (!count || !std::isfinite(viewport.width) ||
        !std::isfinite(viewport.height) ||
        viewport.width != static_cast<double>(width) ||
        viewport.height != static_cast<double>(height) ||
        !std::isfinite(view.location.x) ||
        !std::isfinite(view.location.y) ||
        !std::isfinite(view.location.z) ||
        !std::isfinite(view.pitch_degrees) ||
        !std::isfinite(view.yaw_degrees) ||
        !std::isfinite(view.roll_degrees) ||
        !std::isfinite(view.field_of_view_degrees) ||
        view.field_of_view_degrees < 20.0 ||
        view.field_of_view_degrees > 170.0 ||
        !std::isfinite(view.aspect_ratio) ||
        !std::isfinite(view.projection_scale_x) ||
        !std::isfinite(view.projection_scale_y))
    {
        return std::unexpected(
            PaintAppearanceCaptureError::InvalidEvidence);
    }
    const auto expected_aspect =
        static_cast<double>(width) /
        static_cast<double>(height);
    const auto aspect_tolerance =
        std::max(1.0, expected_aspect) * 1.0e-6;
    if (std::abs(view.aspect_ratio - expected_aspect) >
        aspect_tolerance)
    {
        return std::unexpected(
            PaintAppearanceCaptureError::InvalidEvidence);
    }
    constexpr auto Pi = 3.14159265358979323846;
    const auto pitch = view.pitch_degrees * Pi / 180.0;
    const auto yaw = view.yaw_degrees * Pi / 180.0;
    const auto cosine_pitch = std::cos(pitch);
    return PaintAppearanceCameraFingerprint{
        width,
        height,
        viewport.width,
        viewport.height,
        view.location,
        EspWorldPoint{
            cosine_pitch * std::cos(yaw),
            cosine_pitch * std::sin(yaw),
            std::sin(pitch),
        },
        view.field_of_view_degrees,
    };
}

auto build_paint_appearance_source_query_pixels(
    std::span<const PaintCaptureGeometrySample> geometry,
    std::uint32_t width,
    std::uint32_t height,
    std::stop_token cancellation)
    -> std::expected<
        std::vector<std::size_t>,
        PaintAppearanceCaptureError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintAppearanceCaptureError::Cancelled);
    }
    if (geometry.empty())
    {
        return std::unexpected(
            PaintAppearanceCaptureError::InvalidGeometry);
    }
    if (geometry.size() > MaximumPaintAppearanceSamples)
    {
        return std::unexpected(
            PaintAppearanceCaptureError::ResourceLimit);
    }
    const auto count = pixel_count(
        PaintAppearanceCameraFingerprint{
            width,
            height,
        });
    if (!count)
    {
        return std::unexpected(
            PaintAppearanceCaptureError::InvalidEvidence);
    }
    auto output = std::vector<std::size_t>{};
    output.reserve(geometry.size());
    for (auto index = std::size_t{};
         index < geometry.size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceCaptureError::Cancelled);
        }
        const auto& sample = geometry[index];
        if (!sample.projected ||
            !std::isfinite(sample.screen.x) ||
            !std::isfinite(sample.screen.y) ||
            sample.screen.x < 0.0 ||
            sample.screen.y < 0.0 ||
            sample.screen.x >= static_cast<double>(width) ||
            sample.screen.y >= static_cast<double>(height))
        {
            continue;
        }
        const auto x = static_cast<std::uint32_t>(
            std::floor(sample.screen.x));
        const auto y = static_cast<std::uint32_t>(
            std::floor(sample.screen.y));
        output.push_back(
            static_cast<std::size_t>(y) * width + x);
    }
    std::ranges::sort(output);
    const auto unique = std::ranges::unique(output);
    output.erase(unique.begin(), unique.end());
    if (output.empty())
    {
        return std::unexpected(
            PaintAppearanceCaptureError::NoSupportedSamples);
    }
    return output;
}

auto build_paint_appearance_observations(
    std::span<const PaintCaptureGeometrySample> geometry,
    const PaintAppearanceCaptureEvidence& evidence,
    std::stop_token cancellation)
    -> std::expected<
        std::vector<PaintAppearanceObservation>,
        PaintAppearanceCaptureError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintAppearanceCaptureError::Cancelled);
    }
    if (geometry.empty())
    {
        return std::unexpected(
            PaintAppearanceCaptureError::InvalidGeometry);
    }
    if (geometry.size() > MaximumPaintAppearanceSamples)
    {
        return std::unexpected(
            PaintAppearanceCaptureError::ResourceLimit);
    }
    const auto count = pixel_count(evidence.base_color.camera);
    if (!count || !camera_valid(evidence.base_color.camera) ||
        !valid_pass(evidence.base_color, *count) ||
        !valid_pass(evidence.final_hdr, *count) ||
        !valid_pass(evidence.tone_curve_hdr, *count) ||
        !valid_pass(
            evidence.intrinsic_emission_hdr,
            *count) ||
        !valid_pass(evidence.normal, *count) ||
        !valid_pass(evidence.scene_depth, *count) ||
        !valid_pass(evidence.final_ldr, *count) ||
        evidence.source_pixels == nullptr ||
        evidence.source_pixels->size() != *count)
    {
        return std::unexpected(
            PaintAppearanceCaptureError::InvalidEvidence);
    }
    const auto pass_cameras = std::array{
        &evidence.final_hdr.camera,
        &evidence.tone_curve_hdr.camera,
        &evidence.intrinsic_emission_hdr.camera,
        &evidence.normal.camera,
        &evidence.scene_depth.camera,
        &evidence.final_ldr.camera,
    };
    for (const auto* camera : pass_cameras)
    {
        if (!camera_matches(
                evidence.base_color.camera,
                *camera))
        {
            return std::unexpected(
                PaintAppearanceCaptureError::CameraChanged);
        }
    }

    auto output = std::vector<PaintAppearanceObservation>{};
    output.reserve(geometry.size());
    auto supported = std::size_t{};
    for (auto index = std::size_t{};
         index < geometry.size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintAppearanceCaptureError::Cancelled);
        }
        const auto& sample = geometry[index];
        if (!sample.projected ||
            !std::isfinite(sample.screen.x) ||
            !std::isfinite(sample.screen.y) ||
            sample.screen.x < 0.0 ||
            sample.screen.y < 0.0 ||
            sample.screen.x >=
                static_cast<double>(
                    evidence.base_color.camera.width) ||
            sample.screen.y >=
                static_cast<double>(
                    evidence.base_color.camera.height))
        {
            continue;
        }
        const auto x = static_cast<std::uint32_t>(
            std::floor(sample.screen.x));
        const auto y = static_cast<std::uint32_t>(
            std::floor(sample.screen.y));
        const auto pixel =
            static_cast<std::size_t>(y) *
                evidence.base_color.camera.width +
            x;
        const auto facing = dot(
            sample.world_normal,
            evidence.base_color.camera.direction);
        const auto& source =
            (*evidence.source_pixels)[pixel];
        const auto safe =
            source.visible &&
            std::isfinite(facing) &&
            facing < -0.001;
        supported += safe ? 1U : 0U;
        output.push_back(PaintAppearanceObservation{
            pixel,
            sample.u,
            sample.v,
            (*evidence.base_color.pixels)[pixel],
            (*evidence.final_hdr.pixels)[pixel],
            (*evidence.tone_curve_hdr.pixels)[pixel],
            true,
            (*evidence.intrinsic_emission_hdr.pixels)[pixel],
            true,
            (*evidence.normal.pixels)[pixel],
            true,
            (*evidence.scene_depth.pixels)[pixel],
            true,
            facing,
            safe,
            source.surface_key,
        });
    }
    if (supported == 0U)
    {
        return std::unexpected(
            PaintAppearanceCaptureError::NoSupportedSamples);
    }
    return output;
}
} // namespace meccha::core
