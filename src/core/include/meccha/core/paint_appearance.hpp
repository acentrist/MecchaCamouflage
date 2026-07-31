#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace meccha::core
{
inline constexpr double AppearanceHdrMaximum = 64.0;
inline constexpr double AppearanceFallbackRoughness = 0.65;
inline constexpr int AppearanceMaximumClusters = 8;
inline constexpr int AppearanceSpsaIterations = 3;
inline constexpr double AppearanceFitMedianDeltaEMaximum = 0.05;
inline constexpr double AppearanceFitMinimumImprovement = 0.15;
inline constexpr int AppearanceFitMinimumSamples = 256;

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
[[nodiscard]] auto appearance_calibrate_albedo_blend(
    const AppearanceRgb& source_hdr,
    const AppearanceRgb& display_albedo_capture_hdr,
    const AppearanceRgb& base_albedo_capture_hdr)
    -> AppearanceBoundedResponseCalibration;

struct AppearanceMaterial
{
    double albedo_blend{};
    double metallic{};
    double roughness{AppearanceFallbackRoughness};
    double emissive{};

    auto operator==(const AppearanceMaterial&) const
        -> bool = default;
};

struct AppearanceFallback
{
    AppearanceRgb albedo{};
    AppearanceMaterial material{};
};

[[nodiscard]] auto appearance_make_fallback(
    const AppearanceRgb& display_linear) -> AppearanceFallback;
[[nodiscard]] auto appearance_make_safe_fallback(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    bool base_available) -> AppearanceFallback;
[[nodiscard]] auto appearance_make_safe_final_fallback(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    bool base_available,
    bool intrinsic_emission_roi) -> AppearanceFallback;
[[nodiscard]] auto appearance_blend_albedo(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    double blend) -> AppearanceRgb;
[[nodiscard]] auto appearance_source_albedo_target(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear,
    const AppearanceRgb& emission_linear,
    bool emission_roi,
    bool include_shadows) -> AppearanceRgb;
[[nodiscard]] auto appearance_parameterized_albedo(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& target_linear,
    double parameter,
    bool emission_roi) -> AppearanceRgb;
[[nodiscard]] auto appearance_initial_albedo_blend(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& display_linear) -> double;
[[nodiscard]] auto appearance_initial_emissive(
    const AppearanceRgb& base_linear,
    const AppearanceRgb& final_hdr) -> double;
[[nodiscard]] auto appearance_quantize_unit(double value)
    -> std::uint8_t;
[[nodiscard]] auto appearance_material_key(
    const AppearanceMaterial& material,
    bool fallback) -> std::uint64_t;

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

struct AppearanceSpsaPair
{
    std::vector<double> plus{};
    std::vector<double> minus{};
    std::vector<double> direction{};
};

[[nodiscard]] auto appearance_parameter_bound(
    std::size_t parameter_index) -> double;
[[nodiscard]] auto appearance_spsa_hash(std::uint64_t value)
    -> std::uint64_t;
[[nodiscard]] auto appearance_spsa_pair(
    const std::vector<double>& parameters,
    int iteration,
    std::uint64_t seed) -> AppearanceSpsaPair;
[[nodiscard]] auto appearance_spsa_update(
    const std::vector<double>& parameters,
    const AppearanceSpsaPair& pair,
    double loss_plus,
    double loss_minus,
    int iteration) -> std::vector<double>;

struct AppearanceFitAcceptance
{
    int paired_samples{};
    bool camera_stable{};
    bool readback_calibrated{};
    double fallback_loss{
        std::numeric_limits<double>::infinity()};
    double best_loss{
        std::numeric_limits<double>::infinity()};
    double median_delta_e{
        std::numeric_limits<double>::infinity()};
};

[[nodiscard]] auto appearance_fit_accepted(
    const AppearanceFitAcceptance& value) -> bool;

struct AppearanceNonEmissionCandidateAcceptance
{
    int paired_samples{};
    int emissive_nonzero_samples{};
    bool camera_stable{};
    bool readback_calibrated{};
    bool packed_b_verified{};
    double fallback_loss{
        std::numeric_limits<double>::infinity()};
    double candidate_loss{
        std::numeric_limits<double>::infinity()};
    double candidate_median_delta_e{
        std::numeric_limits<double>::infinity()};
    int emission_roi_samples{};
    double emission_roi_loss_initial{
        std::numeric_limits<double>::infinity()};
    double emission_roi_loss_candidate{
        std::numeric_limits<double>::infinity()};
    double reference_max_chromaticity_delta{
        std::numeric_limits<double>::infinity()};
    double candidate_max_chromaticity_delta{
        std::numeric_limits<double>::infinity()};
};

[[nodiscard]] auto appearance_non_emission_candidate_accepted(
    const AppearanceNonEmissionCandidateAcceptance& value)
    -> bool;
} // namespace meccha::core
