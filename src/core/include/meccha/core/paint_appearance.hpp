#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace meccha::core
{
inline constexpr double AppearanceHdrMaximum = 64.0;
inline constexpr double AppearanceCalibrationStepTexels = 4.0;
inline constexpr double AppearancePhysicalEmissionReadbackFloor =
    1.0 / 255.0;
inline constexpr double
    AppearancePhysicalEmissionMaximumChromaticityDelta = 0.10;
inline constexpr double AppearanceFallbackRoughness = 0.65;
inline constexpr double
    AppearancePhysicalEmissionMinimumImprovement = 0.15;
inline constexpr int AppearanceMinimumLocalResponseSamples = 64;

struct AppearanceRgb
{
    double r{};
    double g{};
    double b{};

    auto operator==(const AppearanceRgb&) const -> bool = default;
};

struct AppearanceHdrSample
{
    AppearanceRgb value{};
    bool finite{};
    bool clipped{};
};

struct EnvironmentProjectedCaptureInput
{
    double screen_x{0.5};
    double screen_y{0.5};
};

struct EnvironmentProjectedCaptureResult
{
    bool ok{};
    double capture_u{0.5};
    double capture_v{0.5};
};

[[nodiscard]] auto environment_projected_capture_coordinate(
    const EnvironmentProjectedCaptureInput& input)
    -> EnvironmentProjectedCaptureResult;

[[nodiscard]] auto appearance_rgb_finite(
    const AppearanceRgb& value) -> bool;
[[nodiscard]] auto appearance_sanitize_hdr(
    const AppearanceRgb& source) -> AppearanceHdrSample;
[[nodiscard]] auto appearance_clamp_albedo(
    const AppearanceRgb& source) -> AppearanceRgb;
[[nodiscard]] auto appearance_srgb_to_linear(double encoded)
    -> double;
[[nodiscard]] auto appearance_linear_to_srgb(double linear)
    -> double;
[[nodiscard]] auto appearance_reinhard_display(
    const AppearanceRgb& source) -> AppearanceRgb;
[[nodiscard]] auto appearance_oklab_delta_e(
    const AppearanceRgb& left,
    const AppearanceRgb& right) -> double;
[[nodiscard]] auto appearance_rgb_chromaticity_delta(
    const AppearanceRgb& left,
    const AppearanceRgb& right) -> double;
[[nodiscard]] auto appearance_huber_loss(
    double value,
    double delta = 0.05) -> double;
[[nodiscard]] auto appearance_luminance(
    const AppearanceRgb& source) -> double;

[[nodiscard]] auto appearance_intrinsic_emission_residual(
    const AppearanceRgb& isolated_hdr,
    const AppearanceRgb& base_srgb) -> AppearanceRgb;

struct AppearanceEmissionColorRescue
{
    AppearanceRgb albedo_srgb{};
    bool applied{};
    double residual_luminance{};
};

[[nodiscard]] auto appearance_rescue_emission_color(
    const AppearanceRgb& base_srgb,
    const AppearanceRgb& isolated_hdr,
    double residual_threshold) -> AppearanceEmissionColorRescue;

struct AppearanceClosedLoopCorrectionInput
{
    AppearanceRgb albedo_linear{};
    double emissive{};
    AppearanceRgb source_hdr{};
    AppearanceRgb rendered_hdr{};
    bool intrinsic_emission_roi{};
};

struct AppearanceClosedLoopCorrection
{
    bool supported{};
    AppearanceRgb albedo_linear{};
    double emissive{};
    double display_error{};
};

[[nodiscard]] auto appearance_albedo_closed_loop_correction(
    const AppearanceClosedLoopCorrectionInput& input)
    -> AppearanceClosedLoopCorrection;
[[nodiscard]] auto appearance_closed_loop_correction(
    const AppearanceClosedLoopCorrectionInput& input)
    -> AppearanceClosedLoopCorrection;
[[nodiscard]] auto appearance_emission_chromaticity_albedo(
    const AppearanceRgb& intrinsic_emission_hdr,
    const AppearanceRgb& fallback_albedo) -> AppearanceRgb;

struct AppearanceEmissionNoiseModel
{
    bool ok{};
    double median{};
    double mad{};
    double threshold{0.01};
    bool separated_signal{};
    int baseline_samples{};
};

[[nodiscard]] auto appearance_emission_noise_model(
    const std::vector<double>& luminance_samples)
    -> AppearanceEmissionNoiseModel;
[[nodiscard]] auto appearance_source_emission_noise_model(
    const std::vector<double>& luminance_samples)
    -> AppearanceEmissionNoiseModel;
[[nodiscard]] auto appearance_combine_emission_noise_models(
    const AppearanceEmissionNoiseModel& source,
    const AppearanceEmissionNoiseModel& target_e0)
    -> AppearanceEmissionNoiseModel;
[[nodiscard]] auto appearance_emission_sample_detected(
    double residual_luminance,
    const AppearanceEmissionNoiseModel& e0_noise) -> bool;

struct AppearanceEmissionGridPoint
{
    int x{};
    int y{};
};

struct AppearanceEmissionSurfacePoint
{
    AppearanceEmissionGridPoint screen{};
    double residual_luminance{};
    std::uint64_t surface_key{};
};

struct AppearanceEmissionSurfaceFilter
{
    std::vector<bool> keep{};
    int applied_regions{};
    int core_samples{};
    int core_surface_count{};
    int kept_samples{};
    int halo_rejected_samples{};
    double maximum_core_threshold{};
};

[[nodiscard]] auto appearance_filter_emission_surface_halo(
    const std::vector<AppearanceEmissionSurfacePoint>& points)
    -> AppearanceEmissionSurfaceFilter;

[[nodiscard]] auto appearance_emission_projected_value(
    double projected_emissive,
    bool intrinsic_emission_roi) -> double;

struct AppearanceEmissiveCalibration
{
    bool supported{};
    double emissive{};
    double response_energy{};
};

[[nodiscard]] auto appearance_calibrate_emissive(
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& fallback_hdr,
    const AppearanceRgb& endpoint_hdr)
    -> AppearanceEmissiveCalibration;

struct AppearanceBoundedResponseCalibration
{
    bool supported{};
    double parameter{};
    double response_energy{};
};

[[nodiscard]] auto appearance_calibrate_bounded_response(
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& baseline_hdr,
    const AppearanceRgb& endpoint_hdr,
    double baseline_parameter,
    double endpoint_parameter,
    double minimum_parameter,
    double maximum_parameter)
    -> AppearanceBoundedResponseCalibration;

struct AppearancePhysicalEmissionEvidenceInput
{
    AppearanceRgb source_residual_first{};
    AppearanceRgb source_residual_second{};
    double source_noise_floor_first{
        std::numeric_limits<double>::infinity()};
    double source_noise_floor_second{
        std::numeric_limits<double>::infinity()};
    AppearanceRgb source_hdr{};
    AppearanceRgb baseline_hdr{};
    AppearanceRgb endpoint_hdr{};
    double manual_emissive_floor{};
    bool source_distribution_separated{};
    bool camera_stable{};
    bool readback_calibrated{};
    bool packed_b_verified{};
};

struct AppearancePhysicalEmissionEvidence
{
    bool source_supported{};
    bool source_noise_floor_calibrated{};
    bool source_first_above_noise_floor{};
    bool source_second_above_noise_floor{};
    bool target_response_supported{};
    bool accepted{};
    double inferred_emissive{};
    double composed_emissive{};
    double source_repeatability_error{
        std::numeric_limits<double>::infinity()};
    double source_chromaticity_delta{
        std::numeric_limits<double>::infinity()};
    double response_energy{};
    double baseline_loss{
        std::numeric_limits<double>::infinity()};
    double candidate_loss{
        std::numeric_limits<double>::infinity()};
};

[[nodiscard]] auto appearance_compose_physical_emissive(
    double manual_emissive_floor,
    double inferred_emissive) -> double;

struct AppearancePhysicalEmissionMaterialInput
{
    AppearanceRgb albedo{};
    AppearanceRgb source_residual_first{};
    AppearanceRgb source_residual_second{};
    double manual_emissive_floor{};
    double inferred_emissive{};
    bool dual_evidence_accepted{};
};

struct AppearancePhysicalEmissionMaterial
{
    AppearanceRgb albedo{};
    double emissive{};
    bool chromaticity_carrier_applied{};
};

[[nodiscard]] auto appearance_compose_physical_emission_material(
    const AppearancePhysicalEmissionMaterialInput& input)
    -> AppearancePhysicalEmissionMaterial;
[[nodiscard]] auto appearance_physical_emission_evidence(
    const AppearancePhysicalEmissionEvidenceInput& input)
    -> AppearancePhysicalEmissionEvidence;

enum class AppearancePhysicalEmissionComponentRejection : std::uint8_t
{
    None,
    DualEvidenceUnavailable,
    CameraUnstable,
    ReadbackUncalibrated,
    PackedBNotVerified,
    QuantizedEmissiveZero,
    NonEmissionLossRegressed,
    RoiImprovementBelowThreshold,
};

struct AppearancePhysicalEmissionComponentValidationInput
{
    int paired_samples{};
    double baseline_loss{
        std::numeric_limits<double>::infinity()};
    double candidate_loss{
        std::numeric_limits<double>::infinity()};
    int non_emission_paired_samples{};
    double baseline_non_emission_loss{
        std::numeric_limits<double>::infinity()};
    double candidate_non_emission_loss{
        std::numeric_limits<double>::infinity()};
    bool dual_evidence_prevalidated{};
    bool camera_stable{};
    bool readback_calibrated{};
    bool packed_b_verified{};
    int painted_emissive_nonzero_pixels{};
};

struct AppearancePhysicalEmissionComponentValidation
{
    bool accepted{};
    AppearancePhysicalEmissionComponentRejection rejection{
        AppearancePhysicalEmissionComponentRejection::
            DualEvidenceUnavailable};
    double roi_improvement{
        -std::numeric_limits<double>::infinity()};
    double non_emission_loss_delta{
        std::numeric_limits<double>::infinity()};
};

[[nodiscard]] auto appearance_validate_physical_emission_component(
    const AppearancePhysicalEmissionComponentValidationInput& input)
    -> AppearancePhysicalEmissionComponentValidation;

enum class AppearanceCorrectionBoundary : std::uint8_t
{
    Front,
    Back,
};

enum class AppearanceCorrectionFieldFailure : std::uint8_t
{
    None,
    InvalidInput,
    SideUnanchored,
};

struct AppearanceCorrectionFieldEdge
{
    int first{-1};
    int second{-1};
};

struct AppearanceCorrectionFieldAnchor
{
    int vertex{-1};
    AppearanceRgb value{};
    double weight{};
    AppearanceCorrectionBoundary boundary{
        AppearanceCorrectionBoundary::Front};
};

struct AppearanceCorrectionFieldInput
{
    int vertex_count{};
    std::vector<AppearanceCorrectionFieldEdge> edges{};
    std::vector<bool> side_vertices{};
    std::vector<AppearanceCorrectionFieldAnchor> anchors{};
};

struct AppearanceCorrectionFieldResult
{
    bool ok{};
    AppearanceCorrectionFieldFailure failure{
        AppearanceCorrectionFieldFailure::None};
    std::vector<AppearanceRgb> values{};
    std::vector<bool> resolved{};
    int front_anchor_vertices{};
    int back_anchor_vertices{};
    int side_components{};
    int one_boundary_side_components{};
    int unanchored_side_components{};
    int iterations{};
    std::uint64_t hash{1469598103934665603ULL};
};

[[nodiscard]] auto appearance_solve_correction_field(
    const AppearanceCorrectionFieldInput& input)
    -> AppearanceCorrectionFieldResult;
[[nodiscard]] auto appearance_calibrate_albedo_blend(
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& display_albedo_capture_hdr,
    const AppearanceRgb& base_albedo_capture_hdr)
    -> AppearanceBoundedResponseCalibration;

struct AppearanceAlbedoRgbCalibration
{
    bool supported{};
    int responsive_channels{};
    AppearanceRgb albedo{};
    std::array<bool, 3> channel_supported{};
};

[[nodiscard]] auto appearance_calibrate_albedo_chromaticity(
    const AppearanceRgb& base_albedo,
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& base_albedo_capture_hdr)
    -> AppearanceAlbedoRgbCalibration;

struct AppearanceAlbedoChromaticityGain
{
    bool supported{};
    int responsive_channels{};
    AppearanceRgb gain{1.0, 1.0, 1.0};
};

[[nodiscard]] auto appearance_robust_albedo_chromaticity_gain(
    const std::array<std::vector<double>, 3>&
        log_gain_estimates)
    -> AppearanceAlbedoChromaticityGain;
[[nodiscard]] auto appearance_apply_albedo_chromaticity_gain(
    const AppearanceRgb& base_albedo,
    const AppearanceRgb& gain) -> AppearanceRgb;

[[nodiscard]] auto appearance_quantize_unit(double value)
    -> std::uint8_t;

enum class AppearanceReadbackTransform : std::uint8_t
{
    Identity,
    SwapRedBlue,
};

struct AppearanceReadbackCalibration
{
    bool ok{};
    AppearanceReadbackTransform transform{
        AppearanceReadbackTransform::Identity};
    double median_error{
        std::numeric_limits<double>::infinity()};
    double runner_up_median{
        std::numeric_limits<double>::infinity()};
};

[[nodiscard]] auto appearance_calibrate_linear_readback(
    const std::vector<AppearanceRgb>& expected,
    const std::vector<AppearanceRgb>& raw,
    double maximum_median_error = 0.04)
    -> AppearanceReadbackCalibration;

} // namespace meccha::core
