#pragma once

#include <meccha/core/paint_appearance.hpp>
#include <meccha/core/paint_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
inline constexpr std::size_t MaximumPaintAppearanceSamples =
    600'000U;

struct PaintAppearanceObservation
{
    std::size_t raster_pixel{};
    double u{};
    double v{};
    Rgb8 base_color{};
    AppearanceRgb final_hdr{};
    AppearanceRgb tone_curve_hdr{};
    bool tone_curve_available{};
    AppearanceRgb intrinsic_emission_hdr{};
    bool intrinsic_emission_available{};
    AppearanceRgb normal{};
    bool normal_available{};
    double scene_depth{};
    bool depth_available{};
    double facing{};
    bool safe{};
    std::uint64_t source_surface_key{};
};

struct PaintAppearanceModelSample
{
    std::size_t source_index{};
    std::size_t raster_pixel{};
    double u{};
    double v{};
    AppearanceRgb base_linear{};
    AppearanceRgb display_linear{};
    AppearanceRgb source_final_hdr{};
    AppearanceRgb intrinsic_emission{};
    AppearanceRgb emission_albedo{};
    AppearanceRgb normal{};
    double scene_depth{};
    double facing{};
    bool normal_available{};
    bool depth_available{};
    bool emission_roi{};
    std::uint64_t source_surface_key{};
    std::size_t cluster{};
};

struct PaintAppearanceCluster
{
    std::vector<std::size_t> sample_indices{};
    AppearanceMaterial material{};
};

struct PaintAppearanceModel
{
    std::uint32_t width{};
    std::uint32_t height{};
    bool include_scene_lighting{};
    std::size_t supported_samples{};
    std::size_t emission_roi_samples{};
    AppearanceEmissionNoiseModel source_emission_noise{};
    AppearanceEmissionNoiseModel effective_emission_noise{};
    std::vector<PaintAppearanceModelSample> samples{};
    std::vector<PaintAppearanceCluster> clusters{};
};

struct PaintAppearanceClusterEvaluation
{
    int paired_samples{};
    double loss{
        std::numeric_limits<double>::infinity()};
    double median_delta_e{
        std::numeric_limits<double>::infinity()};
};

struct PaintAppearanceEvaluation
{
    int paired_samples{};
    bool camera_stable{};
    bool readback_calibrated{};
    double loss{
        std::numeric_limits<double>::infinity()};
    double median_delta_e{
        std::numeric_limits<double>::infinity()};
    double median_chromaticity_delta{
        std::numeric_limits<double>::infinity()};
    double maximum_chromaticity_delta{
        std::numeric_limits<double>::infinity()};
    int emission_roi_paired_samples{};
    double emission_roi_loss{
        std::numeric_limits<double>::infinity()};
    int non_emission_paired_samples{};
    double non_emission_loss{
        std::numeric_limits<double>::infinity()};
    std::vector<AppearanceHdrSample> target_hdr_by_sample{};
    std::vector<PaintAppearanceClusterEvaluation> clusters{};
};

struct PaintAppearanceEmissiveCalibration
{
    std::vector<double> parameters{};
    std::vector<int> cluster_responsive_samples{};
    std::vector<double> cluster_emissive{};
    std::vector<bool> cluster_supported{};
    int responsive_samples{};
    int eligible_responsive_samples{};
    int active_clusters{};
    int rejected_clusters{};
    int supported_emission_samples{};
    int rejected_emission_samples{};
    double emissive_mean{};
    double emissive_max{};
};

struct PaintAppearanceFeedbackAlbedo
{
    bool valid{};
    AppearanceRgb value{};
};

struct PaintAppearanceAlbedoCalibration
{
    std::vector<double> parameters{};
    std::vector<int> cluster_responsive_samples{};
    std::vector<double> cluster_blend{};
    std::vector<PaintAppearanceFeedbackAlbedo>
        feedback_albedo_by_sample{};
    int responsive_samples{};
    int active_clusters{};
    int calibrated_samples{};
    int responsive_channels{};
    double blend_mean{};
    double blend_min{1.0};
    double blend_max{};
    double mean_absolute_adjustment{};
};

enum class PaintAppearanceTrialPhase : std::uint8_t
{
    Plus,
    Minus,
};

struct PaintAppearanceTrial
{
    int iteration{};
    PaintAppearanceTrialPhase phase{
        PaintAppearanceTrialPhase::Plus};
    std::vector<double> parameters{};
};

enum class PaintAppearanceFitSessionStage : std::uint8_t
{
    NeedPlus,
    AwaitPlus,
    NeedMinus,
    AwaitMinus,
    Complete,
};

struct PaintAppearanceFitSession
{
    std::size_t cluster_count{};
    std::uint64_t seed{};
    int iteration{};
    bool freeze_emissive{};
    PaintAppearanceFitSessionStage stage{
        PaintAppearanceFitSessionStage::NeedPlus};
    std::vector<double> fallback_parameters{};
    PaintAppearanceEvaluation fallback_evaluation{};
    std::vector<double> parameters{};
    AppearanceSpsaPair pair{};
    std::optional<PaintAppearanceEvaluation> plus_evaluation{};
    std::vector<double> best_parameters{};
    PaintAppearanceEvaluation best_evaluation{};
};

struct PaintAppearanceFitResult
{
    std::vector<double> parameters{};
    PaintAppearanceEvaluation evaluation{};
    bool accepted{};
    int iterations{};
};

enum class PaintAppearanceFitError : std::uint8_t
{
    InvalidInput,
    InvalidModel,
    InvalidParameters,
    InvalidResponse,
    NoSupportedSamples,
    NoPairedSamples,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto prepare_paint_appearance_model(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const PaintAppearanceObservation> observations,
    bool include_scene_lighting,
    std::optional<AppearanceEmissionNoiseModel>
        target_e0_noise = std::nullopt,
    std::stop_token cancellation = {})
    -> std::expected<
        PaintAppearanceModel,
        PaintAppearanceFitError>;

[[nodiscard]] auto paint_appearance_parameters(
    const PaintAppearanceModel& model)
    -> std::vector<double>;

[[nodiscard]] auto paint_appearance_fallback_parameters(
    const PaintAppearanceModel& model)
    -> std::vector<double>;

[[nodiscard]] auto paint_appearance_emissive_endpoint_parameters(
    const PaintAppearanceModel& model)
    -> std::expected<
        std::vector<double>,
        PaintAppearanceFitError>;

[[nodiscard]] auto calibrate_paint_appearance_emissive_endpoint(
    const PaintAppearanceModel& model,
    std::span<const double> source_parameters,
    const PaintAppearanceEvaluation& fallback,
    const PaintAppearanceEvaluation& endpoint)
    -> std::expected<
        PaintAppearanceEmissiveCalibration,
        PaintAppearanceFitError>;

[[nodiscard]] auto gate_paint_appearance_emissive_calibration(
    const PaintAppearanceModel& model,
    const PaintAppearanceEmissiveCalibration& calibration,
    const PaintAppearanceEvaluation& fallback,
    const PaintAppearanceEvaluation& calibrated)
    -> std::expected<
        PaintAppearanceEmissiveCalibration,
        PaintAppearanceFitError>;

[[nodiscard]] auto paint_appearance_albedo_endpoint_parameters(
    const PaintAppearanceModel& model,
    std::span<const double> calibrated_parameters)
    -> std::expected<
        std::vector<double>,
        PaintAppearanceFitError>;

[[nodiscard]] auto calibrate_paint_appearance_albedo_endpoint(
    const PaintAppearanceModel& model,
    std::span<const double> baseline_parameters,
    const PaintAppearanceEvaluation& baseline,
    const PaintAppearanceEvaluation& endpoint)
    -> std::expected<
        PaintAppearanceAlbedoCalibration,
        PaintAppearanceFitError>;

[[nodiscard]] auto paint_appearance_non_emission_parameters(
    const PaintAppearanceModel& model,
    std::span<const double> calibrated_parameters)
    -> std::expected<
        std::vector<double>,
        PaintAppearanceFitError>;

[[nodiscard]] auto
paint_appearance_feedback_albedo_authoritative(
    const PaintAppearanceModel& model,
    const PaintAppearanceAlbedoCalibration& calibration,
    const PaintAppearanceEvaluation& fallback,
    const PaintAppearanceEvaluation& albedo_endpoint,
    const PaintAppearanceEvaluation& candidate) -> bool;

[[nodiscard]] auto
paint_appearance_non_emission_candidate_accepted(
    const PaintAppearanceModel& model,
    std::span<const double> parameters,
    bool packed_b_verified,
    const PaintAppearanceEvaluation& fallback,
    const PaintAppearanceEvaluation& albedo_endpoint,
    const PaintAppearanceEvaluation& candidate) -> bool;

[[nodiscard]] auto paint_appearance_emission_candidate_accepted(
    const PaintAppearanceModel& model,
    const PaintAppearanceEmissiveCalibration& calibration,
    std::span<const double> parameters,
    bool packed_b_verified,
    const PaintAppearanceEvaluation& fallback,
    const PaintAppearanceEvaluation& candidate) -> bool;

[[nodiscard]] auto begin_paint_appearance_fit(
    const PaintAppearanceModel& model,
    std::vector<double> fallback_parameters,
    PaintAppearanceEvaluation fallback_evaluation,
    std::optional<std::vector<double>> initial_parameters =
        std::nullopt,
    std::optional<PaintAppearanceEvaluation> initial_evaluation =
        std::nullopt,
    bool freeze_emissive = false,
    std::uint64_t seed = 0x218a55e5ULL)
    -> std::expected<
        PaintAppearanceFitSession,
        PaintAppearanceFitError>;

[[nodiscard]] auto next_paint_appearance_trial(
    PaintAppearanceFitSession& session)
    -> std::expected<
        std::optional<PaintAppearanceTrial>,
        PaintAppearanceFitError>;

[[nodiscard]] auto observe_paint_appearance_trial(
    PaintAppearanceFitSession& session,
    PaintAppearanceEvaluation evaluation)
    -> std::expected<void, PaintAppearanceFitError>;

[[nodiscard]] auto finish_paint_appearance_fit(
    const PaintAppearanceFitSession& session)
    -> std::expected<
        PaintAppearanceFitResult,
        PaintAppearanceFitError>;

[[nodiscard]] auto resolve_paint_appearance_raster(
    const PaintAppearanceModel& model,
    std::span<const Rgb8> base_colors,
    std::span<const Rgb8> scene_colors,
    std::span<const double> parameters,
    std::span<const PaintAppearanceFeedbackAlbedo>
        feedback_albedo_by_sample = {},
    bool safe_fallback = false,
    std::stop_token cancellation = {})
    -> std::expected<
        std::vector<ResolvedPaintAppearance>,
        PaintAppearanceFitError>;

[[nodiscard]] auto build_paint_appearance_trial_plan(
    const PaintAppearanceModel& model,
    std::span<const ResolvedPaintAppearance> appearances,
    double brush_size_texels,
    std::uint32_t texture_dimension,
    std::stop_token cancellation = {})
    -> std::expected<
        PaintPlan,
        PaintAppearanceFitError>;

[[nodiscard]] auto evaluate_paint_appearance_response(
    const PaintAppearanceModel& model,
    std::span<const AppearanceRgb> target_hdr,
    bool camera_stable,
    bool readback_calibrated,
    AppearanceReadbackTransform transform,
    std::stop_token cancellation = {})
    -> std::expected<
        PaintAppearanceEvaluation,
        PaintAppearanceFitError>;
} // namespace meccha::core
