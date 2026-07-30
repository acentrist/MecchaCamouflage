#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
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
    double side_source_max_uv{0.08};
    double front_back_source_max_uv{0.45};
    RegionMode front_mode{RegionMode::Skip};
    RegionMode side_mode{RegionMode::Paint};
    RegionMode back_mode{RegionMode::Paint};
    bool auto_material{};
    bool include_scene_lighting{};
    Material paint_material{};
    Rgb8 fill_color{255U, 255U, 255U};
    Material fill_material{1.0, 0.0, 0.0};
    double color_compression_tolerance_percent{5.0};

    auto operator==(const PaintSettings&) const -> bool = default;
};

enum class PaintSettingField : std::uint8_t
{
    BrushSize,
    SideSourceMaximum,
    FrontBackSourceMaximum,
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

[[nodiscard]] auto build_replay_plan(
    std::span<const ReplayCandidate> candidates,
    int texture_size,
    double brush_size_texels,
    double fill_radius_texels) -> ReplayPlan;

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

[[nodiscard]] auto visual_drain_complete(
    bool observer_available,
    bool observer_observed_activity,
    int visual_pending_strokes,
    bool outgoing_available,
    int outgoing_pending_strokes,
    int final_elapsed_ms,
    int final_confirmation_ms) -> bool;
} // namespace meccha::core
