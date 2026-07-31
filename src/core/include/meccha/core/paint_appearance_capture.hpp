#pragma once

#include <meccha/core/paint_appearance_fit.hpp>
#include <meccha/core/paint_capture_geometry.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
enum class PaintAppearanceCaptureError : std::uint8_t;

struct PaintAppearanceCameraFingerprint
{
    std::uint32_t width{};
    std::uint32_t height{};
    double viewport_width{};
    double viewport_height{};
    EspWorldPoint location{};
    EspWorldPoint direction{};
    double field_of_view_degrees{};

    auto operator==(
        const PaintAppearanceCameraFingerprint&) const
        -> bool = default;
};

[[nodiscard]] auto make_paint_appearance_camera_fingerprint(
    const EspView& view,
    EspViewport viewport,
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<
        PaintAppearanceCameraFingerprint,
        PaintAppearanceCaptureError>;

template <typename Pixel>
struct PaintAppearanceCapturedPass
{
    PaintAppearanceCameraFingerprint camera{};
    std::shared_ptr<const std::vector<Pixel>> pixels{};
};

struct PaintAppearanceSourcePixel
{
    bool visible{};
    std::uint64_t surface_key{};

    auto operator==(const PaintAppearanceSourcePixel&) const
        -> bool = default;
};

struct PaintAppearanceCaptureEvidence
{
    PaintAppearanceCapturedPass<Rgb8> base_color{};
    PaintAppearanceCapturedPass<AppearanceRgb> final_hdr{};
    PaintAppearanceCapturedPass<AppearanceRgb>
        tone_curve_hdr{};
    PaintAppearanceCapturedPass<AppearanceRgb>
        intrinsic_emission_hdr{};
    PaintAppearanceCapturedPass<AppearanceRgb> normal{};
    PaintAppearanceCapturedPass<double> scene_depth{};
    PaintAppearanceCapturedPass<Rgb8> final_ldr{};
    std::shared_ptr<
        const std::vector<PaintAppearanceSourcePixel>>
        source_pixels{};
};

enum class PaintAppearanceCaptureError : std::uint8_t
{
    InvalidEvidence,
    CameraChanged,
    InvalidGeometry,
    NoSupportedSamples,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_paint_appearance_source_query_pixels(
    std::span<const PaintCaptureGeometrySample> geometry,
    std::uint32_t width,
    std::uint32_t height,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<std::size_t>,
        PaintAppearanceCaptureError>;

[[nodiscard]] auto build_paint_appearance_observations(
    std::span<const PaintCaptureGeometrySample> geometry,
    const PaintAppearanceCaptureEvidence& evidence,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<PaintAppearanceObservation>,
        PaintAppearanceCaptureError>;
} // namespace meccha::core
