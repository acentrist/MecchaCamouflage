#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
enum class Region : std::uint8_t
{
    Front,
    Side,
    Back,
};

enum class RegionMode : std::uint8_t
{
    Paint,
    Fill,
    Skip,
};

struct Rgb8
{
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};

    auto operator==(const Rgb8&) const -> bool = default;
};

struct Material
{
    double metallic{};
    double roughness{1.0};
    double emissive{};

    auto operator==(const Material&) const -> bool = default;
};

struct PaintSettings
{
    double brush_size_texels{5.0};
    RegionMode front_mode{RegionMode::Skip};
    RegionMode side_mode{RegionMode::Paint};
    RegionMode back_mode{RegionMode::Paint};
    Material paint_material{};
    Rgb8 fill_color{255U, 255U, 255U};
    Material fill_material{1.0, 0.0, 0.0};
    double color_compression_tolerance_percent{5.0};

    auto operator==(const PaintSettings&) const -> bool = default;
};

enum class PaintSettingField : std::uint8_t
{
    BrushSize,
    FrontMode,
    SideMode,
    BackMode,
    PaintMetallic,
    PaintRoughness,
    PaintEmissive,
    FillMetallic,
    FillRoughness,
    FillEmissive,
    CompressionTolerance,
};

[[nodiscard]] auto validate(const PaintSettings& settings)
    -> std::vector<PaintSettingField>;

enum class ReplayPass : std::uint8_t
{
    Fill,
    Paint,
    Complete,
};

struct ReplayPassWindow
{
    ReplayPass pass{};
    std::size_t begin{};
    std::size_t end{};

    auto operator==(const ReplayPassWindow&) const -> bool = default;
};

[[nodiscard]] auto replay_pass_window(
    std::size_t offset,
    std::size_t total,
    std::size_t fill_end) -> ReplayPassWindow;

struct ReplayCandidate
{
    std::size_t sample_index{};
    Region region{};
    RegionMode mode{};
    int uv_island{};
    double u{};
    double v{};
    bool has_current_view_position{};
    double current_view_vertical{};
    double fallback_view_vertical{};
    double horizontal{};
    std::size_t original_ordinal{};
};

struct SpatialScanlineKey
{
    int row{};
    double horizontal{};
    std::size_t original_ordinal{};

    auto operator==(const SpatialScanlineKey&) const -> bool = default;
};

struct ReplayEntry
{
    std::size_t sample_index{};
    ReplayPass pass{};
    Region region{};
    SpatialScanlineKey spatial_key{};

    auto operator==(const ReplayEntry&) const -> bool = default;
};

struct ReplayPlan
{
    std::vector<ReplayEntry> entries{};
    std::size_t fill_end{};
    std::size_t fill_count{};
    std::size_t paint_count{};
    std::size_t fill_candidates{};
    std::size_t fill_deduplicated{};
    std::size_t paint_candidates{};
    std::size_t paint_deduplicated{};
    bool current_view_projection_fallback_used{};
    std::size_t current_view_projection_fallback_candidates{};
};

enum class ReplayPlanError : std::uint8_t
{
    InvalidArgument,
    InvalidCandidate,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_replay_plan(
    std::span<const ReplayCandidate> candidates,
    int texture_size,
    double brush_size_texels,
    double fill_radius_texels,
    std::stop_token cancellation = {})
    -> std::expected<ReplayPlan, ReplayPlanError>;

inline constexpr std::size_t MaximumAdaptivePaintSamples = 600'000U;
inline constexpr std::size_t MaximumAdaptiveReplayEntries = 600'000U;

struct AdaptivePaintSample
{
    double u{};
    double v{};
    Region region{};
    int uv_island{};
    double red{};
    double green{};
    double blue{};
    bool paint_eligible{};
    bool safe{};
    std::uint64_t material_key{};
    bool replay_relevant{true};

    auto operator==(const AdaptivePaintSample&) const -> bool = default;
};

struct AdaptiveReplayEntry
{
    ReplayEntry replay{};
    double radius_multiplier{1.0};
    bool has_color_override{};
    double red{};
    double green{};
    double blue{};

    auto operator==(const AdaptiveReplayEntry&) const -> bool = default;
};

struct AdaptivePaintPlan
{
    std::vector<AdaptiveReplayEntry> entries{};
    std::size_t compressed_paint_entries{};
    std::size_t expanded_paint_entries{};
    int coverage_grid_size{};
    std::size_t representative_paint_entries{};
    double representative_error_sum{};
    double representative_error_max{};

    auto operator==(const AdaptivePaintPlan&) const -> bool = default;
};

enum class AdaptivePaintPlanError : std::uint8_t
{
    InvalidArgument,
    InvalidSample,
    InvalidReplayEntry,
    ResourceLimit,
    Cancelled,
};

[[nodiscard]] auto build_adaptive_paint_plan(
    std::span<const ReplayEntry> replay_entries,
    std::span<const AdaptivePaintSample> samples,
    double base_radius_uv,
    double tolerance_percent,
    double edge_margin_uv = 0.0,
    std::stop_token cancellation = {})
    -> std::expected<AdaptivePaintPlan, AdaptivePaintPlanError>;

struct ReplicationPacingInput
{
    int batches_per_second{};
    int strokes_per_batch{};
    int render_target_writes_per_frame{};
    int max_calls_per_tick{};
    int min_remote_frames_after_local_paint{};
    bool adaptive_remote_interval{};
    int max_adaptive_remote_frame_interval{};
};

struct ReplicationPacingPlan
{
    int batches_per_second{};
    int strokes_per_batch{};
    int network_strokes_per_second{};
    int receiver_strokes_per_second{};
    int effective_strokes_per_second{};
    int strokes_per_window{};
    int calls_per_tick{};
    int network_window_ms{};
    int cadence_ms{};
    int final_confirmation_ms{};

    auto operator==(const ReplicationPacingPlan&) const -> bool = default;
};

[[nodiscard]] auto replication_pacing_plan(
    const ReplicationPacingInput& input) -> ReplicationPacingPlan;

inline constexpr std::uint64_t LocalDispatchCpuBudgetUs = 4'000U;
inline constexpr std::uint64_t LocalDispatchNominalFrameUs = 16'667U;
inline constexpr int LocalDispatchMaximumAdaptiveDelayMs = 250;

[[nodiscard]] constexpr auto local_dispatch_adaptive_delay_ms(
    int requested_delay_ms,
    std::uint64_t observed_dispatch_us) -> int
{
    const auto base_delay = std::max(1, requested_delay_ms);
    if (observed_dispatch_us == 0U ||
        LocalDispatchCpuBudgetUs >= LocalDispatchNominalFrameUs)
    {
        return base_delay;
    }
    constexpr auto idle_ratio_numerator =
        LocalDispatchNominalFrameUs - LocalDispatchCpuBudgetUs;
    constexpr auto delay_denominator =
        LocalDispatchCpuBudgetUs * 1'000U;
    constexpr auto capped_observation_us =
        (static_cast<std::uint64_t>(
             LocalDispatchMaximumAdaptiveDelayMs) *
             delay_denominator +
         idle_ratio_numerator - 1U) /
        idle_ratio_numerator;
    if (observed_dispatch_us >= capped_observation_us)
    {
        return std::max(
            base_delay,
            LocalDispatchMaximumAdaptiveDelayMs);
    }
    const auto adaptive_delay_ms = static_cast<int>(
        (observed_dispatch_us * idle_ratio_numerator +
         delay_denominator - 1U) /
        delay_denominator);
    return std::max(base_delay, adaptive_delay_ms);
}

[[nodiscard]] auto visual_drain_complete(
    bool observer_available,
    bool observer_observed_activity,
    int visual_pending_strokes,
    bool outgoing_available,
    int outgoing_pending_strokes,
    int final_elapsed_ms,
    int final_confirmation_ms) -> bool;
} // namespace meccha::core
