#pragma once

#include <meccha/core/paint_appearance.hpp>
#include <meccha/core/paint_capture_geometry.hpp>
#include <meccha/core/paint_plan.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
inline constexpr std::size_t MaximumPaintProjectiveSamples = 600'000U;

struct PaintProjectiveObservation
{
    std::size_t geometry_index{};
    std::size_t raster_pixel{};
    Region region{Region::Front};
    int uv_island{};
    double u{};
    double v{};
    std::uint32_t triangle_index{};
    std::uint32_t first_vertex{};
    std::uint32_t second_vertex{};
    std::uint32_t third_vertex{};
    double barycentric_a{};
    double barycentric_b{};
    double barycentric_c{};
    bool replay_relevant{true};
    bool calibration_sample{};
    Rgb8 base_color{};
    AppearanceRgb final_hdr{};
    AppearanceRgb intrinsic_emission_first_hdr{};
    AppearanceRgb intrinsic_emission_second_hdr{};
    double facing{};
    bool safe{};
    std::uint64_t source_surface_key{};
};

struct PaintProjectiveSample
{
    std::size_t geometry_index{};
    std::size_t raster_pixel{};
    Region region{Region::Front};
    int uv_island{};
    double u{};
    double v{};
    std::uint32_t triangle_index{};
    std::uint32_t first_vertex{};
    std::uint32_t second_vertex{};
    std::uint32_t third_vertex{};
    double barycentric_a{};
    double barycentric_b{};
    double barycentric_c{};
    bool replay_relevant{true};
    bool calibration_sample{};
    bool safe{};
    AppearanceRgb base_albedo{};
    AppearanceRgb source_final_hdr{};
    AppearanceRgb source_residual_first{};
    AppearanceRgb source_residual_second{};
    std::uint64_t source_surface_key{};
};

struct PaintProjectiveModel
{
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t replay_samples{};
    std::size_t calibration_samples{};
    AppearanceEmissionNoiseModel source_noise_first{};
    AppearanceEmissionNoiseModel source_noise_second{};
    std::vector<PaintProjectiveSample> samples{};
};

struct PaintProjectiveFeedback
{
    bool camera_stable{};
    bool readback_calibrated{};
    AppearanceReadbackTransform readback_transform{
        AppearanceReadbackTransform::Identity};
    std::vector<AppearanceHdrSample> target_hdr_by_sample{};
};

struct PaintProjectiveRaster
{
    std::vector<ResolvedPaintAppearance> appearances{};
    std::vector<bool> available{};
};

struct PaintProjectiveCalibration
{
    std::vector<AppearanceRgb> corrected_albedo_by_sample{};
    std::vector<bool> corrected_albedo_available{};
    std::vector<bool> physical_emission_candidate{};
    PaintProjectiveRaster endpoint{};
    int corrected_samples{};
    int emission_candidates{};
    int front_anchor_vertices{};
    int back_anchor_vertices{};
    int side_components{};
    int one_boundary_side_components{};
    std::uint64_t correction_field_hash{};
};

struct PaintProjectiveResolution
{
    PaintProjectiveRaster raster{};
    int local_albedo_acceptances{};
    int physical_emission_components{};
    int physical_emission_samples{};
};

enum class PaintProjectiveError : std::uint8_t
{
    InvalidInput,
    InvalidModel,
    InvalidEvidence,
    NoSupportedSamples,
    SideUnanchored,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto prepare_paint_projective_model(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const PaintProjectiveObservation> observations,
    std::stop_token cancellation = {})
    -> std::expected<PaintProjectiveModel, PaintProjectiveError>;

[[nodiscard]] auto build_paint_projective_baseline(
    const PaintProjectiveModel& model,
    const PaintSettings& settings,
    std::stop_token cancellation = {})
    -> std::expected<PaintProjectiveRaster, PaintProjectiveError>;

[[nodiscard]] auto build_paint_projective_trial_plan(
    const PaintProjectiveModel& model,
    std::span<const ResolvedPaintAppearance> appearances,
    std::uint32_t texture_dimension,
    std::stop_token cancellation = {})
    -> std::expected<PaintPlan, PaintProjectiveError>;

[[nodiscard]] auto evaluate_paint_projective_feedback(
    const PaintProjectiveModel& model,
    std::span<const AppearanceRgb> target_hdr,
    bool camera_stable,
    bool readback_calibrated,
    AppearanceReadbackTransform transform,
    std::stop_token cancellation = {})
    -> std::expected<PaintProjectiveFeedback, PaintProjectiveError>;

[[nodiscard]] auto calibrate_paint_projective_baseline(
    const PaintProjectiveModel& model,
    const PaintProjectiveFeedback& baseline,
    const AppearanceEmissionNoiseModel& target_e0_noise,
    const PaintSettings& settings,
    bool packed_b_verified,
    std::stop_token cancellation = {})
    -> std::expected<PaintProjectiveCalibration, PaintProjectiveError>;

[[nodiscard]] auto finalize_paint_projective_raster(
    const PaintProjectiveModel& model,
    const PaintProjectiveCalibration& calibration,
    const PaintProjectiveFeedback& baseline,
    const PaintProjectiveFeedback& endpoint,
    const AppearanceEmissionNoiseModel& target_e0_noise,
    const PaintSettings& settings,
    bool packed_b_verified,
    std::stop_token cancellation = {})
    -> std::expected<PaintProjectiveResolution, PaintProjectiveError>;
} // namespace meccha::core
