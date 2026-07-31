#pragma once

#include <meccha/core/paint_appearance_fit.hpp>
#include <meccha/core/paint_capture_geometry.hpp>

#include <cstddef>
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

[[nodiscard]] auto paint_appearance_camera_matches(
    const PaintAppearanceCameraFingerprint& expected,
    const PaintAppearanceCameraFingerprint& actual) -> bool;

template <typename Pixel>
struct PaintAppearanceCapturedPass
{
    PaintAppearanceCameraFingerprint camera{};
    std::shared_ptr<const std::vector<Pixel>> pixels{};
};

inline constexpr std::size_t MaximumPaintAppearanceSourceQueries =
    8192U;

struct PaintAppearanceSourceSample
{
    bool visible{};
    std::uint64_t surface_key{};

    auto operator==(const PaintAppearanceSourceSample&) const
        -> bool = default;
};

struct PaintAppearanceSourceQuery
{
    std::size_t geometry_index{};
    std::size_t raster_pixel{};
    EspScreenPoint screen{};
    Vector3d world_position{};
    std::uint64_t surface_key{};
    PaintSamplingVertex first_uv{};
    PaintSamplingVertex second_uv{};
    PaintSamplingVertex third_uv{};

    auto operator==(const PaintAppearanceSourceQuery&) const
        -> bool = default;
};

struct PaintAppearanceSourceHit
{
    bool hit{};
    double u{};
    double v{};
    Vector3d world_position{};

    auto operator==(const PaintAppearanceSourceHit&) const
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
        const std::vector<PaintAppearanceSourceSample>>
        source_samples{};
};

struct PaintAppearanceReadbackReference
{
    std::size_t raster_pixel{};
    AppearanceRgb expected_linear{};

    auto operator==(
        const PaintAppearanceReadbackReference&) const
        -> bool = default;
};

struct PaintAppearanceFeedbackEvidence
{
    PaintAppearanceCapturedPass<AppearanceRgb> base_color{};
    PaintAppearanceCapturedPass<AppearanceRgb> final_hdr{};
};

struct PaintAppearanceFeedback
{
    std::shared_ptr<const std::vector<AppearanceRgb>>
        target_hdr{};
    AppearanceReadbackCalibration readback{};
    bool camera_stable{};
};

struct PaintAppearanceTargetE0Evidence
{
    PaintAppearanceCapturedPass<AppearanceRgb> base_color{};
    PaintAppearanceCapturedPass<AppearanceRgb>
        intrinsic_emission_hdr{};
};

struct PaintAppearanceTargetE0
{
    AppearanceEmissionNoiseModel noise{};
    std::size_t paired_samples{};
    bool camera_stable{};
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

[[nodiscard]] auto build_paint_appearance_source_queries(
    std::span<const PaintCaptureGeometrySample> geometry,
    const PaintSamplingProfile& sampling_profile,
    std::uint32_t width,
    std::uint32_t height,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<PaintAppearanceSourceQuery>,
        PaintAppearanceCaptureError>;

[[nodiscard]] auto resolve_paint_appearance_source_hit(
    const PaintAppearanceSourceQuery& query,
    const PaintAppearanceSourceHit& hit)
    -> std::expected<
        PaintAppearanceSourceSample,
        PaintAppearanceCaptureError>;

[[nodiscard]] auto build_paint_appearance_observations(
    std::span<const PaintCaptureGeometrySample> geometry,
    const PaintAppearanceCaptureEvidence& evidence,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<PaintAppearanceObservation>,
        PaintAppearanceCaptureError>;

[[nodiscard]] auto build_paint_appearance_readback_references(
    const PaintAppearanceModel& model,
    std::uint32_t texture_dimension,
    std::span<const std::byte> preview_albedo_rgba,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<PaintAppearanceReadbackReference>,
        PaintAppearanceCaptureError>;

[[nodiscard]] auto prepare_paint_appearance_feedback(
    const PaintAppearanceCameraFingerprint& source_camera,
    std::span<const PaintAppearanceReadbackReference>
        readback_references,
    const PaintAppearanceFeedbackEvidence& evidence,
    std::stop_token cancellation = {})
    -> std::expected<
        PaintAppearanceFeedback,
        PaintAppearanceCaptureError>;

[[nodiscard]] auto prepare_paint_appearance_target_e0(
    const PaintAppearanceCameraFingerprint& source_camera,
    std::span<const PaintAppearanceReadbackReference>
        readback_references,
    const PaintAppearanceTargetE0Evidence& evidence,
    const AppearanceReadbackCalibration& readback,
    std::stop_token cancellation = {})
    -> std::expected<
        PaintAppearanceTargetE0,
        PaintAppearanceCaptureError>;
} // namespace meccha::core
