#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MecchaCamouflage::Core
{
    struct Color
    {
        double r{0.0};
        double g{0.0};
        double b{0.0};
        double roughness{0.0};
        double metallic{0.0};
    };

    struct PaintSeed
    {
        double u{0.0};
        double v{0.0};
        Color color{};
        bool floor_like{false};
        int priority{0};
        int radius{2};
        double weight{0.0};
    };

    struct ChannelBuffer
    {
        int width{0};
        int height{0};
        std::vector<std::uint8_t> bytes{};
    };

    struct TextureWriteStats
    {
        int uv_coverage{0};
        int filled_by_direct{0};
        int filled_by_extension{0};
        int filled_by_floor{0};
        int preserved_original{0};
        int direct_texels{0};
        unsigned worker_threads{1};
    };

    struct TextureAssemblyResult
    {
        ChannelBuffer albedo{};
        ChannelBuffer metallic{};
        ChannelBuffer roughness{};
        TextureWriteStats stats{};
    };

    enum AtlasTexelFlags : std::uint16_t
    {
        AtlasTexelValid = 1 << 0,
        AtlasTexelOverlap = 1 << 1,
        AtlasTexelDegenerate = 1 << 2,
        AtlasTexelDirect = 1 << 3,
        AtlasTexelInferred = 1 << 4,
    };

    struct AtlasTexel
    {
        int triangle_id{-1};
        int chart_id{-1};
        double bary0{0.0};
        double bary1{0.0};
        std::uint16_t flags{0};
    };

    struct UvChart
    {
        int id{-1};
        int valid_texels{0};
        int direct_texels{0};
        int inferred_texels{0};
        double coverage_ratio{0.0};
    };

    struct RuntimeAtlasProbeReport
    {
        bool ok{false};
        std::string source{"runtime_probe"};
        std::string failure{"not_run"};
        int texture_width{0};
        int texture_height{0};
        int valid_texels{0};
        int chart_count{0};
        int overlap_texels{0};
        int degenerate_texels{0};
        double overlap_ratio{0.0};
    };

    struct AtlasCoverageInput
    {
        int texture_width{0};
        int texture_height{0};
        int valid_texels{0};
        int direct_texels{0};
        int inferred_texels{0};
        int chart_count{0};
        double min_chart_coverage{0.0};
        double lower_body_coverage{1.0};
        double side_coverage{1.0};
        double back_coverage{1.0};
    };

    struct AtlasCoverageReport
    {
        bool ok{false};
        std::string failure{"not_evaluated"};
        double valid_coverage_ratio{0.0};
        double direct_coverage_ratio{0.0};
        double inferred_coverage_ratio{0.0};
        double min_chart_coverage{0.0};
        int valid_texels{0};
        int covered_texels{0};
        int direct_texels{0};
        int inferred_texels{0};
        int chart_count{0};
        bool lower_body_undercovered{false};
        bool side_undercovered{false};
        bool back_undercovered{false};
    };

    struct AtlasBuildInput
    {
        int texture_width{0};
        int texture_height{0};
        std::vector<std::uint8_t> valid_mask{};
        std::vector<int> chart_ids{};
        std::vector<std::uint8_t> direct_mask{};
    };

    struct AtlasBuildResult
    {
        bool ok{false};
        std::string failure{"not_evaluated"};
        std::vector<AtlasTexel> texels{};
        std::vector<UvChart> charts{};
        AtlasCoverageReport coverage{};
    };

    struct SurfaceSamplePlan
    {
        int target_samples{0};
        int max_samples_per_tick{0};
        int chart_count{0};
        bool requires_material_evidence{true};
    };

    struct SurfaceSampleEvidence
    {
        int accepted_samples{0};
        int rejected_samples{0};
        int material_resolved_samples{0};
        int scene_capture_samples{0};
        bool exact_material_source{false};
    };

    struct CaptureQualityInput
    {
        bool image_ok{false};
        bool bulk_calibration_ok{false};
        int background_pixels{0};
        int trace_hits{0};
        int min_hits{0};
        bool uniform{false};
        bool clear_suspect{false};
        double bulk_best_median{0.0};
        double capture_trace_chroma_avg{0.0};
        double capture_trace_chroma_p95{0.0};
        double capture_rgb_max{1.0};
        double capture_rgb_range{1.0};
        double capture_luma_range{1.0};
    };

    struct CaptureQualityDecision
    {
        bool ok{false};
        std::string failure{"not_evaluated"};
    };

    constexpr double MaxBulkMedianRgbError = 0.18;
    constexpr double MaxCaptureTraceChromaAvg = 0.50;
    constexpr double MaxCaptureTraceChromaP95 = 0.72;
    constexpr double MinCaptureRgbMax = 0.035;
    constexpr double MinCaptureRgbRange = 0.018;
    constexpr double MinCaptureLumaRange = 0.010;

    enum class PipelineStage
    {
        Idle,
        ResolveTarget,
        CoarseHit,
        RefinedHit,
        BackgroundTrace,
        CaptureScene,
        BulkReadback,
        Calibration,
        SideQuery,
        Assembly,
        Import,
        Verify,
        Complete,
        Failed,
    };

    struct PhaseTiming
    {
        double resolve_ms{0.0};
        double coarse_hit_ms{0.0};
        double refined_hit_ms{0.0};
        double background_trace_ms{0.0};
        double capture_scene_ms{0.0};
        double bulk_readback_ms{0.0};
        double calibration_ms{0.0};
        double side_query_ms{0.0};
        double assembly_ms{0.0};
        double import_ms{0.0};
        double verify_ms{0.0};
    };

    struct PipelineJob
    {
        std::uint64_t id{0};
        PipelineStage stage{PipelineStage::Idle};
        bool no_import{false};
        bool fallback_used{false};
        std::string failure{"not_run"};
        PhaseTiming timing{};
    };

    struct RuntimeCapabilities
    {
        bool game_thread_tick{false};
        bool bulk_render_target_readback{false};
        bool pixel_render_target_readback{false};
        bool import_channel_from_bytes{false};
        bool paint_at_uv{false};
        bool paint_at_uv_with_brush{false};
        bool paint_stroke_uv{false};
        bool dominant_material_patterns{false};
        bool material_parameter_names{false};
        bool runtime_atlas_probe{false};
        bool mesh_paint_texture{false};
        bool mesh_paint_uv_channel{false};
        bool render_data_cpu_access{false};
        bool skinned_pose_snapshot{false};
        bool non_blocking_texture_update{false};
        bool chunked_paint_api{false};
        bool batched_reflected_paint_api{false};
        bool server_paint_api{false};
        bool multicast_paint_api{false};
        bool texture_sync_api{false};
    };

    struct CaptureSizingInput
    {
        int viewport_width{0};
        int viewport_height{0};
        int texture_width{0};
        int texture_height{0};
        int failed_attempts{0};
        bool require_viewport_size{false};
    };

    struct CaptureSizingDecision
    {
        int width{0};
        int height{0};
        double scale{1.0};
        bool uses_viewport_size{false};
        std::string reason{"not_evaluated"};
    };

    struct AdaptiveSamplingInput
    {
        int viewport_width{0};
        int viewport_height{0};
        int texture_width{0};
        int texture_height{0};
        double bbox_width_px{0.0};
        double bbox_height_px{0.0};
        int existing_seed_count{0};
        int duplicate_texels{0};
        int attempts{0};
    };

    struct AdaptiveSamplingPolicy
    {
        int target_front_hits{0};
        int preferred_front_hits{0};
        int min_front_hits{0};
        int hard_max_attempts{0};
        int refine_grid_x{0};
        int refine_grid_y{0};
        int side_view_count{0};
        int side_grid_x{0};
        int side_grid_y{0};
        int target_side_seeds{0};
        int hard_side_attempts{0};
        int min_side_seeds{0};
        bool duplicate_limited{false};
    };

    struct FrontCoverageInput
    {
        double coarse_min_nx{0.0};
        double coarse_min_ny{0.0};
        double coarse_max_nx{0.0};
        double coarse_max_ny{0.0};
        double refined_min_nx{0.0};
        double refined_min_ny{0.0};
        double refined_max_nx{0.0};
        double refined_max_ny{0.0};
        int refine_grid_x{0};
        int refine_grid_y{0};
        int sample_count{0};
        int min_samples{0};
        int target_samples{0};
        int refined_grid_cursor{0};
        int refined_total_cells{0};
        int vertical_band_hits{0};
        int vertical_band_count{0};
        bool hit_budget_exhausted{false};
    };

    struct FrontCoverageReport
    {
        bool ok{false};
        bool failed{true};
        bool reaches_coarse_bottom{false};
        bool reaches_coarse_top{false};
        bool reaches_coarse_left{false};
        bool reaches_coarse_right{false};
        bool refined_grid_complete{false};
        double refined_cell_width{0.0};
        double refined_cell_height{0.0};
        int vertical_band_hits{0};
        int vertical_band_count{0};
        std::string failure{"not_evaluated"};
    };

    struct VirtualView
    {
        double yaw_degrees{0.0};
        double pitch_degrees{0.0};
    };

    struct UvCoverageInput
    {
        int texture_width{0};
        int texture_height{0};
        int covered_texels{0};
        int direct_texels{0};
        int side_seeds{0};
        bool side_budget_exhausted{false};
        int duplicate_texels{0};
        int attempts{0};
    };

    struct UvCoverageReport
    {
        bool ok{false};
        std::string failure{"not_evaluated"};
        double coverage_ratio{0.0};
        double direct_ratio{0.0};
        double duplicate_rate{0.0};
        bool duplicate_limited{false};
        bool side_exhausted_low_coverage{false};
    };

    enum class CaptureReadbackBackend
    {
        Unknown,
        CachedBulk,
        PreferredBulkRaw,
        CandidateBulk,
    };

    struct CaptureQualityReport
    {
        bool ok{false};
        std::string failure{"not_run"};
        bool low_luma_suspect{false};
        bool chroma_validation_failed{false};
    };

    enum class MaterialConfidence
    {
        Unknown,
        PreservedOriginal,
        ScalarParameter,
        TextureParameter,
        ConstantParameter,
    };

    struct MaterialResolveInput
    {
        double original_roughness{0.0};
        double original_metallic{0.0};
        bool has_roughness_scalar{false};
        double scalar_roughness{0.0};
        bool has_metallic_scalar{false};
        double scalar_metallic{0.0};
        bool floor_like{false};
    };

    struct ResolvedMaterial
    {
        double roughness{0.0};
        double metallic{0.0};
        MaterialConfidence confidence{MaterialConfidence::Unknown};
    };

    struct SurfaceMaterialEvidence
    {
        bool has_roughness_scalar{false};
        bool has_metallic_scalar{false};
        double scalar_roughness{0.0};
        double scalar_metallic{0.0};
        std::string material_source{"unknown"};
        MaterialConfidence confidence{MaterialConfidence::Unknown};
    };

    struct MaterialEvidence
    {
        bool has_base_color_constant{false};
        Color base_color_constant{};
        bool has_base_color_texture{false};
        bool has_readable_texture{false};
        bool has_roughness_scalar{false};
        bool has_metallic_scalar{false};
        double scalar_roughness{0.0};
        double scalar_metallic{0.0};
        std::string material_source{"unknown"};
        MaterialConfidence confidence{MaterialConfidence::Unknown};
    };

    enum class ApplyBackend
    {
        Unknown,
        NonBlockingTextureUpdate,
        ChunkedPaintApi,
        BatchedReflectedPaintApi,
        BlockingImportChannelFromBytes,
        Unavailable,
    };

    struct ApplyBackendProbe
    {
        ApplyBackend backend{ApplyBackend::Unknown};
        bool ok{false};
        bool blocking_only{false};
        bool dev_diagnostic_allowed{false};
        std::string failure{"not_run"};
    };

    struct ReplicatedStrokePlanInput
    {
        int sample_count{0};
        int min_quality_samples{0};
        int max_replicated_strokes_per_tick{0};
        bool dev_diagnostic_allowed{false};
        bool apply_backend_ok{false};
    };

    struct ReplicatedStrokePlan
    {
        bool ok{false};
        bool partial{false};
        bool quality_success{false};
        int strokes_per_tick{24};
        std::string failure{"not_run"};
    };

    struct SampledReadbackTickInput
    {
        int total_samples{0};
        int cursor{0};
        int max_samples_per_tick{0};
        double estimated_sample_ms{0.0};
        double soft_budget_ms{4.0};
        double hard_budget_ms{8.0};
    };

    struct SampledReadbackTickPlan
    {
        int next_cursor{0};
        int samples_this_tick{0};
        bool complete{false};
        bool frame_budget_overrun{false};
        bool hard_budget_overrun{false};
        double budget_ms{0.0};
    };

    struct PrecisionBrushInput
    {
        int texture_width{0};
        int texture_height{0};
        int seed_radius_px{1};
        double texture_min_radius{0.0};
        double texture_max_radius{1.0};
        bool precision_mode{true};
    };

    struct PrecisionBrushDecision
    {
        double radius{0.0};
        double requested_radius{0.0};
        double texture_min_radius{0.0};
        double texture_max_radius{1.0};
        double brush_footprint_texels{0.0};
        bool clamped_by_game_min{false};
        std::string source{"not_run"};
    };

    struct SurfaceStretchSeed
    {
        double u{0.0};
        double v{0.0};
        double screen_x{0.0};
        double screen_y{0.0};
        double normal_x{0.0};
        double normal_y{0.0};
        double normal_z{1.0};
        Color color{};
        bool direct{true};
        int radius{1};
    };

    struct SurfaceStretchPolicy
    {
        double max_uv_distance{0.015};
        double max_screen_distance{48.0};
        double min_normal_dot{0.72};
        int max_inferred{0};
    };

    struct SurfaceStretchReport
    {
        std::vector<PaintSeed> inferred{};
        int direct_preserved{0};
        int rejected_seam{0};
        int normal_limit{0};
    };

    struct MicroFillSource
    {
        double u{0.0};
        double v{0.0};
        double screen_x{0.0};
        double screen_y{0.0};
        double normal_x{0.0};
        double normal_y{0.0};
        double normal_z{1.0};
        Color color{};
        bool direct{true};
        bool verified{false};
        int radius{1};
        bool floor_like{false};
    };

    struct MicroFillPolicy
    {
        double brush_radius_uv{0.003};
        double max_uv_distance{0.018};
        double max_screen_distance{64.0};
        double min_normal_dot{0.72};
        int max_gap_cells{4};
        int max_candidates{0};
    };

    struct MicroFillReport
    {
        std::vector<PaintSeed> candidates{};
        int direct_preserved{0};
        int rejected_unbounded{0};
        int rejected_normal{0};
        int rejected_unverified_source{0};
    };

    struct SideCoverageInput
    {
        bool front_quality_success{false};
        int side_samples{0};
        int inferred_side_samples{0};
        int min_side_samples{0};
        bool budget_exhausted{false};
    };

    struct SideCoverageReport
    {
        bool front_quality_success{false};
        bool side_quality_success{false};
        bool side_quality_failed{false};
        double inferred_ratio{0.0};
        std::string failure{"not_evaluated"};
    };

    struct PaintRunDiagnostics
    {
        PhaseTiming timing{};
        bool frame_budget_overrun{false};
        bool readback_backend_cached{false};
        CaptureReadbackBackend readback_backend{CaptureReadbackBackend::Unknown};
        CaptureQualityReport capture_quality{};
        SurfaceMaterialEvidence material_evidence{};
    };

    struct FrameBudget
    {
        double soft_budget_ms{0.0};
        double hard_budget_ms{0.0};
        double consumed_ms{0.0};
        bool overrun{false};
        bool hard_overrun{false};

        auto consume(double elapsed_ms) -> bool;
    };

    auto clamp_unit(double value) -> double;
    auto byte_from_unit(double value) -> std::uint8_t;
    auto chroma_distance_rgb(const Color& a, const Color& b) -> double;
    auto hash_bytes(const std::vector<std::uint8_t>& bytes) -> std::uint64_t;
    auto changed_byte_count(const std::vector<std::uint8_t>& before,
                            const std::vector<std::uint8_t>& after) -> int;

    auto evaluate_capture_quality(const CaptureQualityInput& input) -> CaptureQualityReport;
    auto validate_capture_quality(const CaptureQualityInput& input) -> CaptureQualityDecision;
    auto choose_capture_dimensions(const CaptureSizingInput& input) -> CaptureSizingDecision;
    auto choose_adaptive_sampling_policy(const AdaptiveSamplingInput& input) -> AdaptiveSamplingPolicy;
    auto stratified_grid_linear_index(int grid_width, int grid_height, int ordinal) -> int;
    auto stratified_grid_order(int grid_width, int grid_height, int limit) -> std::vector<int>;
    auto evaluate_front_coverage(const FrontCoverageInput& input) -> FrontCoverageReport;
    auto generate_golden_angle_views(int count, double pitch_limit_degrees) -> std::vector<VirtualView>;
    auto evaluate_uv_coverage(const UvCoverageInput& input) -> UvCoverageReport;
    auto evaluate_runtime_atlas_probe(const RuntimeAtlasProbeReport& input) -> RuntimeAtlasProbeReport;
    auto evaluate_atlas_coverage(const AtlasCoverageInput& input) -> AtlasCoverageReport;
    auto build_runtime_atlas_from_masks(const AtlasBuildInput& input) -> AtlasBuildResult;
    auto choose_apply_backend(const RuntimeCapabilities& capabilities, bool dev_diagnostic_allowed) -> ApplyBackendProbe;
    auto plan_replicated_stroke_apply(const ReplicatedStrokePlanInput& input) -> ReplicatedStrokePlan;
    auto plan_sampled_readback_tick(const SampledReadbackTickInput& input) -> SampledReadbackTickPlan;
    auto estimate_seed_radius_for_density(int texture_width, int texture_height, int seed_count) -> int;
    auto choose_precision_brush_radius(const PrecisionBrushInput& input) -> PrecisionBrushDecision;
    auto infer_surface_stretch_seeds(const std::vector<SurfaceStretchSeed>& seeds,
                                     const SurfaceStretchPolicy& policy) -> SurfaceStretchReport;
    auto plan_front_micro_fill(const std::vector<MicroFillSource>& sources,
                               const MicroFillPolicy& policy) -> MicroFillReport;
    auto evaluate_side_coverage(const SideCoverageInput& input) -> SideCoverageReport;
    auto merge_nearby_paint_seeds(const std::vector<PaintSeed>& seeds,
                                  double brush_radius_uv) -> std::vector<PaintSeed>;
    auto is_floor_like_label(const std::string& label) -> bool;
    auto resolve_material_channels(const MaterialResolveInput& input) -> ResolvedMaterial;
    auto resolve_verified_material_evidence(const MaterialEvidence& evidence,
                                            double original_roughness,
                                            double original_metallic) -> ResolvedMaterial;
    auto should_send_material_channels(MaterialConfidence confidence) -> bool;

    auto assemble_direct_texture(const ChannelBuffer& albedo_before,
                                 const ChannelBuffer& metallic_before,
                                 const ChannelBuffer& roughness_before,
                                 const std::vector<PaintSeed>& seeds) -> TextureAssemblyResult;
    auto assemble_chart_aware_texture(const ChannelBuffer& albedo_before,
                                      const ChannelBuffer& metallic_before,
                                      const ChannelBuffer& roughness_before,
                                      const AtlasBuildResult& atlas,
                                      const std::vector<PaintSeed>& seeds) -> TextureAssemblyResult;
}
