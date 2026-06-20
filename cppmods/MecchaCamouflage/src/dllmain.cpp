#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <future>
#include <initializer_list>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Input/KeyDef.hpp>
#include <Mod/CppUserModBase.hpp>
#include <String/StringType.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Core/Containers/ScriptArray.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Rotator.hpp>
#include <Unreal/UGameViewportClient.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealCoreStructs.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/World.hpp>
#include <UnrealDef.hpp>

namespace
{
    using RC::CharType;
    using RC::StringType;
    namespace Unreal = RC::Unreal;

    constexpr auto ModTag = STR("[MecchaCamouflage]");
    constexpr auto LabTag = STR("[MecchaCamouflage]");
    constexpr double Pi = 3.14159265358979323846;
    constexpr int PaintChannelAlbedoMetallicRoughness = 5;
    constexpr int PaintChannelUnknown = -1;
    constexpr int ProjectionCaptureResolution = 1024;
    constexpr int ProjectionPrimaryGrid = 320;
    constexpr int ProjectionAuxGrid = 96;
    constexpr int ProjectionFillAtlasResolution = 256;
    constexpr int ProjectionDilationPasses = 8;
    constexpr int ScreenProjectionStrictDilationPasses = 1;
    constexpr int ScreenProjectionExactSplatRadius = 0;
    constexpr int ScreenProjectionFillSplatRadius = 2;
    constexpr double ProjectionTuningDefaultScale = 1.0;
    constexpr double ProjectionTuningMinScale = 0.86;
    constexpr double ProjectionTuningMaxScale = 1.14;
    constexpr double ProjectionTuningScaleStep = 0.004;
    constexpr double ProjectionTuningOffsetStep = 0.002;
    constexpr int ScreenProjectionGridX = 54;
    constexpr int ScreenProjectionGridY = 30;
    constexpr int ScreenProjectionRefineMinGrid = 48;
    constexpr int ScreenProjectionRefineMaxGrid = 512;
    constexpr double ScreenProjectionRefineTargetStepPx = 1.4;
    constexpr int HighResProjectionRtWidth = 2048;
    constexpr double ScreenProjectionBrushRadius = 25.0;
    constexpr int MinScreenHitUvSamples = 8;
    constexpr int TargetScreenHitUvSamples = 30000;
    constexpr int MinQualityScreenHitUvSamples = 2048;
    constexpr int DenseScreenHitHardMaxAttempts = 180000;
    constexpr int AlignmentSampleLimit = 384;
    constexpr int PixelAlignmentSampleLimit = 96;
    constexpr int ProjectionAuditUvResolution = 512;
    constexpr int ProjectionAuditExportSampleLimit = 80;
    constexpr double ProjectionAuditMaxReprojectP95Px = 2.0;
    constexpr double ProjectionAuditMaxUvConflictPixelsPct = 0.06;
    constexpr double ProjectionAuditMaxSameUvColorVariance = 0.085;
    constexpr double ProjectionAuditMaxPredictedScreenMae = 0.055;
    constexpr double ProjectionAuditMaxPredictedBadPixelPct = 0.08;
    constexpr double ProjectionAuditBadPixelDistance = 0.12;
    constexpr int SceneCaptureRenderTargetFormatRgba16f = 6;
    constexpr int SceneCaptureSourceFinalColorLdr = 2;
    constexpr int XrayOverlayRtMaxWidth = 2048;
    constexpr int XrayOverlayCalibrationGrid = 8;
    constexpr int XrayOverlayMinMaskPixels = 256;
    constexpr int XrayOverlayMaxMaskRuns = 30000;
    constexpr double XrayOverlayMaxMaskCoverage = 0.70;
    constexpr double XrayOverlayMinDeltaThreshold = 0.025;
    constexpr double XrayOverlayMaxDeltaThreshold = 0.12;

    struct Color
    {
        double r{0.42};
        double g{0.42};
        double b{0.36};
        double roughness{0.92};
        double metallic{0.0};
    };

    struct TraceHit
    {
        bool hit{false};
        bool has_uv{false};
        Unreal::UObject* actor{nullptr};
        Unreal::UObject* component{nullptr};
        Unreal::FVector location{};
        double u{0.0};
        double v{0.0};
        bool accepted_by_owner{false};
        bool accepted_by_spatial_fallback{false};
        int trace_channel{-1};
    };

    struct Sample
    {
        Unreal::FVector world_position{};
        double u{0.0};
        double v{0.0};
        Color color{};
        double weight{1.0};
        double brush_radius{0.035};
    };

    struct ScreenSpaceHitResult
    {
        bool params_ok{false};
        bool success{false};
        bool has_uv{false};
        double u{0.0};
        double v{0.0};
        Unreal::FVector world_position{};
        Unreal::FVector normal{};
        StringType failure{};
    };

    struct ScreenHitSample
    {
        double screen_x{0.0};
        double screen_y{0.0};
        double nx{0.0};
        double ny{0.0};
        double capture_nx{-1.0};
        double capture_ny{-1.0};
        double u{0.0};
        double v{0.0};
        Unreal::FVector world_position{};
        Unreal::FVector normal{};
        Color color{};
        bool floor_like{false};
    };

    struct ScreenTransform
    {
        double scale_x{1.0};
        double scale_y{1.0};
        double offset_x{0.0};
        double offset_y{0.0};
        bool flip_x{false};
        bool flip_y{false};
        double pivot_x{0.5};
        double pivot_y{0.5};
    };

    struct UvAtlasTransform
    {
        bool swap_uv{false};
        bool flip_u{false};
        bool flip_v{false};
    };

    struct DecodedCalibrationUv
    {
        bool ok{false};
        double u{0.0};
        double v{0.0};
        double error{1000000.0};
    };

    struct RenderTargetImage
    {
        bool ok{false};
        int width{0};
        int height{0};
        int expected_pixels{0};
        int decoded_pixels{0};
        int bulk_candidates{0};
        int bulk_available{0};
        StringType backend{STR("unavailable")};
        StringType function_name{};
        StringType array_param{};
        StringType inner_type{};
        StringType failure{STR("not_run")};
        ScreenTransform bulk_to_pixel_transform{};
        bool bulk_calibration_ok{false};
        int bulk_calibration_samples{0};
        int bulk_calibration_pairs{0};
        double bulk_calibration_best_median{0.0};
        double bulk_calibration_runner_up_median{0.0};
        StringType bulk_calibration_backend{STR("not_run")};
        std::vector<Color> pixels{};
    };

    struct PhaseTimings
    {
        double export_ms{0.0};
        double coarse_hit_ms{0.0};
        double dense_hit_ms{0.0};
        double capture_ms{0.0};
        double readback_ms{0.0};
        double alignment_ms{0.0};
        double atlas_ms{0.0};
        double import_ms{0.0};
        double total_ms{0.0};
    };

    struct AlignmentResult
    {
        ScreenTransform transform{};
        double projection_align_score{0.0};
        double body_delta_median{0.0};
        double runner_up_score{0.0};
        double score_ratio{0.0};
        bool sky_misalign_suspect{false};
        int samples{0};
        int candidate_count{0};
        int visible_reads{0};
        int hidden_reads{0};
        double selected_fov_degrees{0.0};
        double runner_up_fov_degrees{0.0};
        double mask_positive_rate{0.0};
        double mask_negative_rate{0.0};
        int mask_positive_hits{0};
        int mask_negative_hits{0};
        StringType runner_up_backend{STR("<none>")};
        StringType backend{STR("identity")};
    };

    struct CaptureColorSummary
    {
        int pixels{0};
        double min_r{1.0};
        double min_g{1.0};
        double min_b{1.0};
        double avg_r{0.0};
        double avg_g{0.0};
        double avg_b{0.0};
        double max_r{0.0};
        double max_g{0.0};
        double max_b{0.0};
        int near_uniform_samples{0};
        bool uniform{false};
        bool clear_suspect{false};
    };

    struct CaptureColorQuality
    {
        CaptureColorSummary summary{};
        double avg_chroma{0.0};
        double luma_min{1.0};
        double luma_max{0.0};
        double luma_range{0.0};
        double rgb_range{0.0};
        double score{-1.0};
    };

    struct ProjectionAuditBin
    {
        int count{0};
        double r_sum{0.0};
        double g_sum{0.0};
        double b_sum{0.0};
        double rgb_sq_sum{0.0};
        double min_screen_x{1.0e30};
        double min_screen_y{1.0e30};
        double max_screen_x{-1.0e30};
        double max_screen_y{-1.0e30};
    };

    struct ProjectionAuditResult
    {
        int body_mask_pixels{0};
        int audit_samples{0};
        int projected_ok{0};
        int uv_texels{0};
        int uv_conflict_texels{0};
        int uv_conflict_pixels{0};
        int predicted_bad_pixels{0};
        double reproject_error_p50{0.0};
        double reproject_error_p95{0.0};
        double reproject_error_max{0.0};
        double uv_conflict_pixels_pct{0.0};
        double same_uv_color_variance{0.0};
        double predicted_screen_mae{0.0};
        double predicted_screen_p95{0.0};
        double predicted_bad_pixel_pct{0.0};
        bool paint_would_apply{false};
        StringType reason{STR("not_run")};
    };

    struct BackgroundBehindSample
    {
        bool hit{false};
        TraceHit trace{};
        Color color{};
        double distance{0.0};
        int channel_attempts{0};
        int self_skips{0};
        bool floor_like{false};
    };

    struct ProjectedCaptureCoordStats
    {
        int ok{0};
        int failed{0};
        int out_of_view{0};
        double delta_sum_px{0.0};
        double delta_max_px{0.0};
        StringType first_failure{};
    };

    struct RenderTargetReadDiagnostics
    {
        int raw_attempts{0};
        int raw_success{0};
        int pixel_attempts{0};
        int pixel_success{0};
        StringType first_function{};
        StringType first_struct{};
        int first_struct_size{0};
    };

    struct CaptureGridDiagnostics
    {
        bool scene_capture_class{false};
        bool render_target{false};
        bool capture_actor{false};
        bool capture_component{false};
        bool texture_target_written{false};
        bool capture_source_written{false};
        bool capture_every_frame_written{false};
        bool capture_on_movement_written{false};
        bool persist_rendering_state_written{false};
        bool capture_scene_called{false};
        bool hide_actor_components_called{false};
        bool show_only_actor_components_called{false};
        bool clear_show_only_components_called{false};
        bool primitive_render_mode_written{false};
        int requested_render_target_format{SceneCaptureRenderTargetFormatRgba16f};
        int requested_capture_source{SceneCaptureSourceFinalColorLdr};
        int read_pixels{0};
        int missing_pixels{0};
        RenderTargetReadDiagnostics read{};
    };

    struct TimedCaptureImage
    {
        RenderTargetImage image{};
        CaptureGridDiagnostics diagnostics{};
        double capture_ms{0.0};
        double readback_ms{0.0};
    };

    struct XrayOverlayRun
    {
        int y{0};
        int x0{0};
        int x1{0};
    };

    struct XrayOverlayCapture
    {
        bool ok{false};
        Unreal::UObject* render_target{nullptr};
        Unreal::UObject* capture_actor{nullptr};
        Unreal::UObject* capture_component{nullptr};
        CaptureGridDiagnostics diagnostics{};
        double capture_ms{0.0};
        StringType failure{STR("not_run")};
    };

    struct XrayOverlayState
    {
        bool active{false};
        bool hook_installed{false};
        bool drawing{false};
        bool umg_overlay_active{false};
        bool actor_hide_fallback{false};
        bool pawn_hidden{false};
        Unreal::UObject* hidden_pawn{nullptr};
        Unreal::UObject* render_target{nullptr};
        Unreal::UObject* capture_actor{nullptr};
        Unreal::UObject* capture_component{nullptr};
        Unreal::UObject* umg_canvas{nullptr};
        Unreal::UObject* umg_image{nullptr};
        Unreal::UObject* umg_slot{nullptr};
        std::vector<Unreal::UObject*> umg_run_images{};
        std::vector<Unreal::UObject*> umg_run_slots{};
        bool mesh_material_active{false};
        Unreal::UObject* mesh_object{nullptr};
        std::vector<Unreal::UObject*> mesh_original_materials{};
        std::vector<Unreal::UObject*> mesh_dynamic_materials{};
        int rt_width{0};
        int rt_height{0};
        int viewport_width{0};
        int viewport_height{0};
        int mask_pixels{0};
        int mask_runs{0};
        int estimated_draw_calls{0};
        int umg_canvas_candidates{0};
        int umg_images{0};
        int mesh_material_slots{0};
        int mesh_dynamic_materials_created{0};
        int mesh_texture_param_calls{0};
        int mesh_scalar_param_calls{0};
        int hud_frames{0};
        int last_frame_draw_calls{0};
        int last_frame_draw_failures{0};
        int total_draw_calls{0};
        int total_draw_failures{0};
        double mask_coverage_pct{0.0};
        double delta_min{0.0};
        double delta_avg{0.0};
        double delta_max{0.0};
        double delta_threshold{0.0};
        double visible_capture_ms{0.0};
        double hidden_capture_ms{0.0};
        double draw_capture_ms{0.0};
        double readback_ms{0.0};
        double mask_ms{0.0};
        double total_ms{0.0};
        bool visible_bulk_ok{false};
        bool hidden_bulk_ok{false};
        bool visible_bulk_relaxed{false};
        bool hidden_bulk_relaxed{false};
        bool bulk_calibration_ok{false};
        StringType visible_backend{STR("unavailable")};
        StringType hidden_backend{STR("unavailable")};
        StringType visible_calibration{STR("not_run")};
        StringType hidden_calibration{STR("not_run")};
        StringType umg_canvas_name{};
        StringType umg_failure{STR("not_run")};
        StringType failure{STR("not_run")};
        std::vector<XrayOverlayRun> runs{};
    };

    struct CalibrationStats
    {
        int samples{0};
        int clamped{0};
        double gain_min{1000000.0};
        double gain_max{0.0};
        double gain_sum{0.0};
        int gain_values{0};
    };

    struct ScreenHitCollectionStats
    {
        int attempts{0};
        int params_ok{0};
        int hit_success{0};
        int hit_uv_count{0};
        int floor_hits{0};
        int color_samples{0};
        int failures{0};
        double min_u{1.0};
        double min_v{1.0};
        double max_u{0.0};
        double max_v{0.0};
        double min_nx{1.0};
        double min_ny{1.0};
        double max_nx{0.0};
        double max_ny{0.0};
        Unreal::FVector min_world{};
        Unreal::FVector max_world{};
        bool has_world_bounds{false};
        StringType first_failure{};
    };

    struct BodyTraceDebugStats
    {
        int trace_calls{0};
        int trace_channel_attempts{0};
        int trace_no_hit{0};
        int uv_owner{0};
        int uv_spatial{0};
        int uv_floor_rejected{0};
        int uv_far_rejected{0};
        int no_uv_close{0};
        int no_uv_far_rejected{0};
        int exhausted{0};
    };

    struct ProbeState
    {
        bool unreal_initialized{false};
        bool command_hook_installed{false};
        bool queue_active{false};
        bool cancelled{false};
        bool paint_path_available{false};
        bool capture_path_available{false};
        bool trace_path_available{false};
        bool uv_path_available{false};
        bool capture_pixels_ready{false};
        bool uv_mapping_ready{false};
        bool official_paint_pipeline_ready{false};
        int runtime_paint_components{0};
        int skeletal_mesh_components{0};
        int scene_capture_functions{0};
        int trace_functions{0};
        int paint_functions{0};
        int uv_functions{0};
        int pipeline_property_candidates{0};
        int pipeline_function_candidates{0};
        int commit_sync_candidates{0};
        int render_target_candidates{0};
        int paint_capture_calls{0};
        int paint_capture_matches{0};
        int commit_calls{0};
        int views{0};
        int visible_samples{0};
        int uv_hits{0};
        int background_pixels{0};
        int atlas_bins{0};
        int queued_strokes{0};
        int success{0};
        int failures{0};
        int body_trace_hits{0};
        int background_trace_hits{0};
        int paint_world_success{0};
        int paint_uv_success{0};
        uint64_t paint_state_hash_before{0};
        uint64_t paint_state_hash_after{0};
        uint64_t play_id{0};
        bool paint_capture_enabled{false};
        bool verified_visible_backend{false};
        int verified_paint_channel{PaintChannelUnknown};
        StringType verified_paint_function{};
        StringType current_world{};
        StringType current_pawn{};
        StringType current_component{};
        StringType last_failure{STR("not_run")};
    };

    struct PaintBackend
    {
        const CharType* function_name{STR("")};
        int channel{PaintChannelUnknown};
        double radius{0.006};
        double opacity{0.62};
        double hardness{0.92};
    };

    struct PaintParamWriteStats
    {
        bool wrote_color{false};
        bool wrote_uv{false};
        bool wrote_world_position{false};
        bool wrote_brush{false};
        bool saw_brush{false};
        bool wrote_channel{false};
        bool strict_channel_data{false};
        bool used_channel_data_layout_fallback{false};
        StringType color_param{};
        StringType color_struct{};
        StringType uv_param{};
        StringType brush_param{};
        StringType channel_label{};
        StringType failure{};
    };

    struct ViewportInfo
    {
        int width{1920};
        int height{1080};
        bool fallback{true};
    };

    auto set_actor_hidden(Unreal::UObject* actor, bool hidden) -> bool;
    auto create_render_target(Unreal::UObject* world_context,
                              int width,
                              int height,
                              const Color& clear_color = Color{0.0, 0.0, 0.0, 1.0, 0.0}) -> Unreal::UObject*;
    auto read_render_target_pixel(Unreal::UObject* world_context,
                                  Unreal::UObject* render_target,
                                  int x,
                                  int y,
                                  RenderTargetReadDiagnostics* diagnostics = nullptr) -> std::optional<Color>;
    auto read_render_target_image(Unreal::UObject* world_context,
                                  Unreal::UObject* render_target,
                                  int width,
                                  int height) -> RenderTargetImage;
    auto execute_line_trace(Unreal::UObject* world_context,
                            const Unreal::FVector& start,
                            const Unreal::FVector& end,
                            bool ignore_self,
                            int trace_channel,
                            bool trace_complex) -> TraceHit;
    auto sample_image_for_hit(const RenderTargetImage& image,
                              const ScreenHitSample& sample,
                              const ScreenTransform& transform) -> std::optional<Color>;
    auto infer_surface_material(Color color, bool floor_like) -> Color;
    auto get_render_target_for_channel(Unreal::UObject* component, int channel) -> Unreal::UObject*;
    auto render_target_dimensions(Unreal::UObject* render_target) -> std::pair<int, int>;
    auto channel_enum_label(Unreal::UFunction* function, int channel) -> StringType;
    auto is_floor_like_object(Unreal::UObject* actor, Unreal::UObject* component) -> bool;

    struct ActorHiddenGuard
    {
        Unreal::UObject* actor{nullptr};
        bool active{false};

        explicit ActorHiddenGuard(Unreal::UObject* in_actor) : actor(in_actor), active(false)
        {
            if (actor)
            {
                active = set_actor_hidden(actor, true);
            }
        }

        ~ActorHiddenGuard()
        {
            if (active && actor)
            {
                set_actor_hidden(actor, false);
            }
        }
    };

    auto lower_copy(StringType text) -> StringType
    {
        std::transform(text.begin(), text.end(), text.begin(), [](CharType c) {
            if (c >= static_cast<CharType>('A') && c <= static_cast<CharType>('Z'))
            {
                return static_cast<CharType>(c + (static_cast<CharType>('a') - static_cast<CharType>('A')));
            }
            return c;
        });
        return text;
    }

    auto contains_text(const StringType& text, const CharType* needle) -> bool
    {
        return text.find(needle) != StringType::npos;
    }

    auto contains_any_text(const StringType& text, const std::vector<const CharType*>& needles) -> bool
    {
        for (const auto* needle : needles)
        {
            if (contains_text(text, needle))
            {
                return true;
            }
        }
        return false;
    }

    using SteadyClock = std::chrono::steady_clock;

    auto elapsed_ms(const SteadyClock::time_point& start, const SteadyClock::time_point& end) -> double
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    auto elapsed_ms_since(const SteadyClock::time_point& start) -> double
    {
        return elapsed_ms(start, SteadyClock::now());
    }

    auto color_distance_rgb(const Color& a, const Color& b) -> double
    {
        const auto dr = a.r - b.r;
        const auto dg = a.g - b.g;
        const auto db = a.b - b.b;
        return std::sqrt(dr * dr + dg * dg + db * db);
    }

    auto chroma_distance_rgb(const Color& a, const Color& b) -> double
    {
        const auto normalize = [](const Color& color) {
            const auto sum = std::max(0.000001, color.r + color.g + color.b);
            return std::array<double, 3>{color.r / sum, color.g / sum, color.b / sum};
        };
        const auto ca = normalize(a);
        const auto cb = normalize(b);
        const auto dr = ca[0] - cb[0];
        const auto dg = ca[1] - cb[1];
        const auto db = ca[2] - cb[2];
        return std::sqrt(dr * dr + dg * dg + db * db);
    }

    auto apply_uv_atlas_transform(double u, double v, const UvAtlasTransform& transform) -> std::pair<double, double>
    {
        auto out_u = transform.swap_uv ? v : u;
        auto out_v = transform.swap_uv ? u : v;
        if (transform.flip_u)
        {
            out_u = 1.0 - out_u;
        }
        if (transform.flip_v)
        {
            out_v = 1.0 - out_v;
        }
        return {std::clamp(out_u, 0.0, 0.999999), std::clamp(out_v, 0.0, 0.999999)};
    }

    auto uv_calibration_color(double u, double v) -> Color
    {
        const auto uu = std::clamp(u, 0.0, 1.0);
        const auto vv = std::clamp(v, 0.0, 1.0);
        Color color{};
        color.r = std::clamp(0.12 + 0.78 * uu, 0.02, 0.98);
        color.g = std::clamp(0.12 + 0.78 * vv, 0.02, 0.98);
        color.b = std::clamp(0.86 - 0.42 * uu + 0.18 * vv, 0.02, 0.98);
        color.roughness = 0.94;
        color.metallic = 0.0;
        return color;
    }

    auto decode_uv_calibration_color(const Color& observed) -> DecodedCalibrationUv
    {
        DecodedCalibrationUv out{};
        constexpr int coarse_grid = 48;
        auto best_x = 0;
        auto best_y = 0;
        for (int y = 0; y < coarse_grid; ++y)
        {
            for (int x = 0; x < coarse_grid; ++x)
            {
                const auto u = (static_cast<double>(x) + 0.5) / static_cast<double>(coarse_grid);
                const auto v = (static_cast<double>(y) + 0.5) / static_cast<double>(coarse_grid);
                const auto expected = uv_calibration_color(u, v);
                const auto error = chroma_distance_rgb(observed, expected);
                if (error < out.error)
                {
                    out.error = error;
                    out.u = u;
                    out.v = v;
                    best_x = x;
                    best_y = y;
                }
            }
        }

        constexpr int refine_steps = 8;
        const auto coarse_step = 1.0 / static_cast<double>(coarse_grid);
        const auto center_u = (static_cast<double>(best_x) + 0.5) * coarse_step;
        const auto center_v = (static_cast<double>(best_y) + 0.5) * coarse_step;
        for (int y = -refine_steps; y <= refine_steps; ++y)
        {
            for (int x = -refine_steps; x <= refine_steps; ++x)
            {
                const auto u = std::clamp(center_u + static_cast<double>(x) * coarse_step / static_cast<double>(refine_steps),
                                          0.0,
                                          0.999999);
                const auto v = std::clamp(center_v + static_cast<double>(y) * coarse_step / static_cast<double>(refine_steps),
                                          0.0,
                                          0.999999);
                const auto expected = uv_calibration_color(u, v);
                const auto error = chroma_distance_rgb(observed, expected);
                if (error < out.error)
                {
                    out.error = error;
                    out.u = u;
                    out.v = v;
                }
            }
        }
        out.ok = out.error < 0.22;
        return out;
    }

    auto median_value(std::vector<double> values) -> double
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const auto middle = values.size() / 2;
        if ((values.size() % 2) == 0 && middle > 0)
        {
            return (values[middle - 1] + values[middle]) * 0.5;
        }
        return values[middle];
    }

    auto pipeline_token_match(const StringType& text) -> bool
    {
        return contains_any_text(text,
                                 {STR("paint"),
                                  STR("brush"),
                                  STR("stroke"),
                                  STR("texture"),
                                  STR("rendertarget"),
                                  STR("render_target"),
                                  STR("material"),
                                  STR("channel"),
                                  STR("layer"),
                                  STR("atlas"),
                                  STR("apply"),
                                  STR("commit"),
                                  STR("update"),
                                  STR("save"),
                                  STR("sync"),
                                  STR("replic"),
                                  STR("server"),
                                  STR("multicast"),
                                  STR("net")});
    }

    auto commit_sync_token_match(const StringType& text) -> bool
    {
        return contains_any_text(text,
                                 {STR("commit"),
                                  STR("apply"),
                                  STR("update"),
                                  STR("save"),
                                  STR("sync"),
                                  STR("replic"),
                                  STR("server"),
                                  STR("multicast"),
                                  STR("net")});
    }

    auto render_target_token_match(const StringType& text) -> bool
    {
        return contains_any_text(text,
                                 {STR("texture"),
                                  STR("rendertarget"),
                                  STR("render_target"),
                                  STR("target"),
                                  STR("material"),
                                  STR("atlas"),
                                  STR("layer")});
    }

    auto object_name_or_empty(Unreal::UObject* object) -> StringType
    {
        if (!object)
        {
            return {};
        }
        return object->GetFullName();
    }

    auto narrow_ascii(const StringType& text) -> std::string
    {
        std::string out{};
        out.reserve(text.size());
        for (const auto ch : text)
        {
            const auto value = static_cast<uint32_t>(ch);
            out.push_back(value >= 32 && value <= 126 ? static_cast<char>(value) : '?');
        }
        return out;
    }

    auto object_instance_path(Unreal::UObject* object) -> StringType
    {
        auto full_name = object_name_or_empty(object);
        const auto pos = full_name.find(static_cast<CharType>(' '));
        if (pos != StringType::npos && pos + 1 < full_name.size())
        {
            return full_name.substr(pos + 1);
        }
        return full_name;
    }

    auto object_is_or_belongs_to(Unreal::UObject* object, Unreal::UObject* owner) -> bool
    {
        if (!object || !owner)
        {
            return false;
        }
        if (object == owner)
        {
            return true;
        }
        const auto object_path = lower_copy(object_instance_path(object));
        const auto owner_path = lower_copy(object_instance_path(owner));
        return !object_path.empty() && !owner_path.empty() && object_path.find(owner_path) != StringType::npos;
    }

    auto trace_hit_belongs_to_pawn(const TraceHit& hit, Unreal::UObject* pawn) -> bool
    {
        return object_is_or_belongs_to(hit.actor, pawn) || object_is_or_belongs_to(hit.component, pawn);
    }

    auto fnv1a_update(uint64_t hash, const void* data, size_t size) -> uint64_t
    {
        const auto* bytes = reinterpret_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<uint64_t>(bytes[i]);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    auto fnv1a_update_string(uint64_t hash, const StringType& text) -> uint64_t
    {
        for (const auto ch : text)
        {
            const auto value = static_cast<uint64_t>(ch);
            hash = fnv1a_update(hash, &value, sizeof(value));
        }
        return hash;
    }

    auto clamp(double value, double min_value, double max_value) -> double
    {
        return std::max(min_value, std::min(max_value, value));
    }

    auto worker_count_for_items(size_t item_count) -> unsigned
    {
        const auto hardware = std::max(1U, std::thread::hardware_concurrency());
        const auto useful = item_count < 65536 ? 1U : std::min<unsigned>(hardware, static_cast<unsigned>((item_count + 65535) / 65536));
        return std::max(1U, useful);
    }

    template <typename Fn>
    auto parallel_ranges(size_t item_count, Fn&& fn) -> void
    {
        const auto workers = worker_count_for_items(item_count);
        if (workers <= 1 || item_count == 0)
        {
            fn(0, item_count, 0);
            return;
        }

        std::vector<std::thread> threads{};
        threads.reserve(workers);
        for (unsigned worker = 0; worker < workers; ++worker)
        {
            const auto begin = (item_count * static_cast<size_t>(worker)) / static_cast<size_t>(workers);
            const auto end = (item_count * static_cast<size_t>(worker + 1)) / static_cast<size_t>(workers);
            threads.emplace_back([begin, end, worker, &fn]() {
                fn(begin, end, worker);
            });
        }
        for (auto& thread : threads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    double g_projection_tuning_scale_x{ProjectionTuningDefaultScale};
    double g_projection_tuning_scale_y{ProjectionTuningDefaultScale};
    double g_projection_tuning_offset_x{0.0};
    double g_projection_tuning_offset_y{0.0};

    auto reset_projection_tuning(bool neutral) -> void
    {
        const auto scale = neutral ? 1.0 : ProjectionTuningDefaultScale;
        g_projection_tuning_scale_x = scale;
        g_projection_tuning_scale_y = scale;
        g_projection_tuning_offset_x = 0.0;
        g_projection_tuning_offset_y = 0.0;
    }

    auto nudge_projection_scale(double delta) -> void
    {
        g_projection_tuning_scale_x = clamp(g_projection_tuning_scale_x + delta,
                                            ProjectionTuningMinScale,
                                            ProjectionTuningMaxScale);
        g_projection_tuning_scale_y = clamp(g_projection_tuning_scale_y + delta,
                                            ProjectionTuningMinScale,
                                            ProjectionTuningMaxScale);
    }

    auto nudge_projection_offset(double dx, double dy) -> void
    {
        g_projection_tuning_offset_x = clamp(g_projection_tuning_offset_x + dx, -0.08, 0.08);
        g_projection_tuning_offset_y = clamp(g_projection_tuning_offset_y + dy, -0.08, 0.08);
    }

    auto make_body_bbox_projection_transform(const ScreenHitCollectionStats& stats) -> ScreenTransform
    {
        ScreenTransform transform{};
        transform.scale_x = g_projection_tuning_scale_x;
        transform.scale_y = g_projection_tuning_scale_y;
        transform.offset_x = g_projection_tuning_offset_x;
        transform.offset_y = g_projection_tuning_offset_y;
        if (stats.hit_uv_count > 0 && stats.max_nx > stats.min_nx && stats.max_ny > stats.min_ny)
        {
            transform.pivot_x = clamp((stats.min_nx + stats.max_nx) * 0.5, 0.0, 1.0);
            transform.pivot_y = clamp((stats.min_ny + stats.max_ny) * 0.5, 0.0, 1.0);
        }
        return transform;
    }

    auto log_projection_tuning(const StringType& context, const ScreenTransform& applied) -> void
    {
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} projection_tuning context={} default_scale={} scale=({}, {}) offset=({}, {}) pivot=({}, {}) step_scale={} step_offset={} note=sample_hidden_background_around_body_bbox\n"),
            ModTag,
            context.empty() ? STR("<none>") : context,
            ProjectionTuningDefaultScale,
            applied.scale_x,
            applied.scale_y,
            applied.offset_x,
            applied.offset_y,
            applied.pivot_x,
            applied.pivot_y,
            ProjectionTuningScaleStep,
            ProjectionTuningOffsetStep);
    }

    auto vec(double x, double y, double z) -> Unreal::FVector
    {
        return Unreal::FVector{x, y, z};
    }

    auto add(const Unreal::FVector& a, const Unreal::FVector& b) -> Unreal::FVector
    {
        return vec(a.X() + b.X(), a.Y() + b.Y(), a.Z() + b.Z());
    }

    auto sub(const Unreal::FVector& a, const Unreal::FVector& b) -> Unreal::FVector
    {
        return vec(a.X() - b.X(), a.Y() - b.Y(), a.Z() - b.Z());
    }

    auto mul(const Unreal::FVector& a, double s) -> Unreal::FVector
    {
        return vec(a.X() * s, a.Y() * s, a.Z() * s);
    }

    auto length(const Unreal::FVector& a) -> double
    {
        return std::sqrt(a.X() * a.X() + a.Y() * a.Y() + a.Z() * a.Z());
    }

    auto normalize(const Unreal::FVector& a) -> Unreal::FVector
    {
        const auto len = length(a);
        if (len < 0.0001)
        {
            return vec(1.0, 0.0, 0.0);
        }
        return mul(a, 1.0 / len);
    }

    auto dot(const Unreal::FVector& a, const Unreal::FVector& b) -> double
    {
        return a.X() * b.X() + a.Y() * b.Y() + a.Z() * b.Z();
    }

    auto cross(const Unreal::FVector& a, const Unreal::FVector& b) -> Unreal::FVector
    {
        return vec(a.Y() * b.Z() - a.Z() * b.Y(),
                   a.Z() * b.X() - a.X() * b.Z(),
                   a.X() * b.Y() - a.Y() * b.X());
    }

    auto rotator_forward(const Unreal::FRotator& rotator) -> Unreal::FVector
    {
        const auto pitch = rotator.GetPitch() * Pi / 180.0;
        const auto yaw = rotator.GetYaw() * Pi / 180.0;
        const auto cp = std::cos(pitch);
        return normalize(vec(cp * std::cos(yaw), cp * std::sin(yaw), std::sin(pitch)));
    }

    auto rotator_right(const Unreal::FRotator& rotator) -> Unreal::FVector
    {
        const auto pitch = rotator.GetPitch() * Pi / 180.0;
        const auto yaw = rotator.GetYaw() * Pi / 180.0;
        const auto roll = rotator.GetRoll() * Pi / 180.0;
        const auto sp = std::sin(pitch);
        const auto cp = std::cos(pitch);
        const auto sy = std::sin(yaw);
        const auto cy = std::cos(yaw);
        const auto sr = std::sin(roll);
        const auto cr = std::cos(roll);
        return normalize(vec(sr * sp * cy - cr * sy,
                             sr * sp * sy + cr * cy,
                             -sr * cp));
    }

    auto rotator_up(const Unreal::FRotator& rotator) -> Unreal::FVector
    {
        const auto pitch = rotator.GetPitch() * Pi / 180.0;
        const auto yaw = rotator.GetYaw() * Pi / 180.0;
        const auto roll = rotator.GetRoll() * Pi / 180.0;
        const auto sp = std::sin(pitch);
        const auto cp = std::cos(pitch);
        const auto sy = std::sin(yaw);
        const auto cy = std::cos(yaw);
        const auto sr = std::sin(roll);
        const auto cr = std::cos(roll);
        return normalize(vec(-(cr * sp * cy + sr * sy),
                             cy * sr - cr * sp * sy,
                             cr * cp));
    }

    auto rotate_yaw_pitch(const Unreal::FVector& forward, double yaw_degrees, double pitch_degrees) -> Unreal::FVector
    {
        const auto yaw = yaw_degrees * Pi / 180.0;
        const auto pitch = pitch_degrees * Pi / 180.0;
        const auto up = vec(0.0, 0.0, 1.0);
        auto right = normalize(cross(forward, up));
        if (length(right) < 0.01)
        {
            right = vec(0.0, 1.0, 0.0);
        }
        auto yawed = normalize(add(mul(forward, std::cos(yaw)), mul(right, std::sin(yaw))));
        auto local_right = normalize(cross(yawed, up));
        auto local_up = normalize(cross(local_right, yawed));
        return normalize(add(mul(yawed, std::cos(pitch)), mul(local_up, std::sin(pitch))));
    }

    auto rotator_from_forward(const Unreal::FVector& forward) -> Unreal::FRotator
    {
        const auto dir = normalize(forward);
        const auto yaw = std::atan2(dir.Y(), dir.X()) * 180.0 / Pi;
        const auto xy = std::sqrt(dir.X() * dir.X() + dir.Y() * dir.Y());
        const auto pitch = std::atan2(dir.Z(), xy) * 180.0 / Pi;
        return Unreal::FRotator{pitch, yaw, 0.0};
    }

    auto hash_u32(std::uint32_t x) -> std::uint32_t
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    auto noise01(int x, int y, std::uint32_t seed) -> double
    {
        const auto h = hash_u32(static_cast<std::uint32_t>(x) * 374761393U ^
                                static_cast<std::uint32_t>(y) * 668265263U ^ seed);
        return static_cast<double>(h & 0x00ffffffU) / static_cast<double>(0x01000000U);
    }

    auto noise01(double u, double v, double scale, std::uint32_t seed) -> double
    {
        return noise01(static_cast<int>(std::floor(u * scale)), static_cast<int>(std::floor(v * scale)), seed);
    }

    auto mix_color(const Color& a, const Color& b, double t) -> Color
    {
        t = clamp(t, 0.0, 1.0);
        Color out{};
        out.r = a.r * (1.0 - t) + b.r * t;
        out.g = a.g * (1.0 - t) + b.g * t;
        out.b = a.b * (1.0 - t) + b.b * t;
        out.roughness = a.roughness * (1.0 - t) + b.roughness * t;
        out.metallic = a.metallic * (1.0 - t) + b.metallic * t;
        return out;
    }

    auto starts_with(const StringType& text, const StringType& prefix) -> bool
    {
        return text.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), text.begin());
    }

    auto first_token(StringType text) -> StringType
    {
        const auto pos = text.find(static_cast<CharType>(' '));
        if (pos != StringType::npos)
        {
            text.resize(pos);
        }
        return lower_copy(text);
    }

    auto find_function(const CharType* full_name) -> Unreal::UFunction*
    {
        return Unreal::UObjectGlobals::StaticFindObject<Unreal::UFunction*>(nullptr, nullptr, full_name);
    }

    auto count_class_instances(const CharType* class_name, int limit = 4096) -> int
    {
        std::vector<Unreal::UObject*> objects{};
        Unreal::UObjectGlobals::FindObjects(static_cast<size_t>(limit), class_name, nullptr, objects, 0, 0, false);
        return static_cast<int>(objects.size());
    }

    auto find_runtime_paint_object_with_uv() -> Unreal::UObject*
    {
        std::vector<Unreal::UObject*> objects{};
        Unreal::UObjectGlobals::FindObjects(256, STR("RuntimePaintableComponent"), nullptr, objects, 0, 0, false);
        for (auto* object : objects)
        {
            if (!object)
            {
                continue;
            }
            if (object->GetFunctionByNameInChain(STR("PaintAtUVWithBrush")) ||
                object->GetFunctionByNameInChain(STR("PaintAtUV")) ||
                object->GetFunctionByNameInChain(STR("PaintStrokeUV")))
            {
                return object;
            }
        }
        return nullptr;
    }

    auto find_function_property(Unreal::UStruct* function, const CharType* name) -> Unreal::FProperty*
    {
        if (!function)
        {
            return nullptr;
        }
        for (auto* property : function->ForEachProperty())
        {
            if (property && property->GetName() == name)
            {
                return property;
            }
        }
        return nullptr;
    }

    auto find_struct_property(const Unreal::UStruct* structure, const CharType* name) -> Unreal::FProperty*
    {
        if (!structure)
        {
            return nullptr;
        }
        for (auto* property : const_cast<Unreal::UStruct*>(structure)->ForEachProperty())
        {
            if (property && property->GetName() == name)
            {
                return property;
            }
        }
        return nullptr;
    }

    auto prop_type_name(Unreal::FProperty* property) -> StringType
    {
        return property ? property->GetClass().GetName() : StringType{};
    }

    auto struct_type(Unreal::FStructProperty* property) -> Unreal::UStruct*
    {
        return property ? Unreal::ToRawPtr(property->GetStruct()) : nullptr;
    }

    auto prop_value_ptr(uint8_t* container, Unreal::FProperty* property) -> uint8_t*
    {
        return container + property->GetOffset_Internal();
    }

    auto read_bool(Unreal::FProperty* property, uint8_t* container) -> bool
    {
        if (!property)
        {
            return false;
        }
        if (auto* bool_prop = Unreal::CastField<Unreal::FBoolProperty>(property))
        {
            return bool_prop->GetPropertyValue(prop_value_ptr(container, property));
        }
        return *reinterpret_cast<bool*>(prop_value_ptr(container, property));
    }

    auto write_bool(Unreal::FProperty* property, uint8_t* container, bool value) -> bool
    {
        if (!property)
        {
            return false;
        }
        if (auto* bool_prop = Unreal::CastField<Unreal::FBoolProperty>(property))
        {
            bool_prop->SetPropertyValue(prop_value_ptr(container, property), value);
            return true;
        }
        if (property->GetElementSize() >= static_cast<int32_t>(sizeof(bool)))
        {
            *reinterpret_cast<bool*>(prop_value_ptr(container, property)) = value;
            return true;
        }
        return false;
    }

    auto write_number(Unreal::FProperty* property, uint8_t* container, double value) -> bool
    {
        if (!property)
        {
            return false;
        }
        const auto type = prop_type_name(property);
        auto* dest = prop_value_ptr(container, property);
        if (type == STR("DoubleProperty"))
        {
            *reinterpret_cast<double*>(dest) = value;
            return true;
        }
        if (type == STR("FloatProperty"))
        {
            *reinterpret_cast<float*>(dest) = static_cast<float>(value);
            return true;
        }
        if (type == STR("IntProperty"))
        {
            *reinterpret_cast<int32_t*>(dest) = static_cast<int32_t>(value);
            return true;
        }
        if (type == STR("UInt32Property"))
        {
            *reinterpret_cast<uint32_t*>(dest) = static_cast<uint32_t>(value);
            return true;
        }
        if (type == STR("ByteProperty") || type == STR("EnumProperty"))
        {
            if (property->GetElementSize() >= static_cast<int32_t>(sizeof(int32_t)))
            {
                *reinterpret_cast<int32_t*>(dest) = static_cast<int32_t>(value);
            }
            else
            {
                *reinterpret_cast<uint8_t*>(dest) = static_cast<uint8_t>(value);
            }
            return true;
        }
        return false;
    }

    auto write_name(Unreal::FProperty* property, uint8_t* container, const CharType* value) -> bool
    {
        if (!property || !value)
        {
            return false;
        }
        if (prop_type_name(property) != STR("NameProperty"))
        {
            return false;
        }
        *reinterpret_cast<Unreal::FName*>(prop_value_ptr(container, property)) = Unreal::FName(value);
        return true;
    }

    auto write_string(Unreal::FProperty* property,
                      uint8_t* container,
                      const CharType* value,
                      std::vector<Unreal::FProperty*>* cleanup_properties = nullptr) -> bool
    {
        auto* str_prop = Unreal::CastField<Unreal::FStrProperty>(property);
        if (!str_prop || !container || !value)
        {
            return false;
        }
        auto* dest = prop_value_ptr(container, property);
        str_prop->InitializeValue(dest);
        const Unreal::FString text{value};
        str_prop->SetPropertyValue(dest, text);
        if (cleanup_properties)
        {
            cleanup_properties->push_back(property);
        }
        return true;
    }

    auto cleanup_written_properties(uint8_t* container, const std::vector<Unreal::FProperty*>& properties) -> void
    {
        if (!container)
        {
            return;
        }
        for (auto it = properties.rbegin(); it != properties.rend(); ++it)
        {
            if (*it)
            {
                (*it)->DestroyValue(prop_value_ptr(container, *it));
            }
        }
    }

    auto read_number(Unreal::FProperty* property, uint8_t* container) -> std::optional<double>
    {
        if (!property)
        {
            return std::nullopt;
        }
        const auto type = prop_type_name(property);
        auto* src = prop_value_ptr(container, property);
        if (type == STR("DoubleProperty"))
        {
            return *reinterpret_cast<double*>(src);
        }
        if (type == STR("FloatProperty"))
        {
            return static_cast<double>(*reinterpret_cast<float*>(src));
        }
        if (type == STR("IntProperty"))
        {
            return static_cast<double>(*reinterpret_cast<int32_t*>(src));
        }
        if (type == STR("UInt32Property"))
        {
            return static_cast<double>(*reinterpret_cast<uint32_t*>(src));
        }
        if (type == STR("ByteProperty") || type == STR("EnumProperty"))
        {
            if (property->GetElementSize() >= static_cast<int32_t>(sizeof(int32_t)))
            {
                return static_cast<double>(*reinterpret_cast<int32_t*>(src));
            }
            return static_cast<double>(*reinterpret_cast<uint8_t*>(src));
        }
        return std::nullopt;
    }

    auto write_object(Unreal::FProperty* property, uint8_t* container, Unreal::UObject* value) -> bool
    {
        if (!property)
        {
            return false;
        }
        const auto type = prop_type_name(property);
        if (contains_text(type, STR("ObjectProperty")) || contains_text(type, STR("ObjectPtrProperty")) ||
            contains_text(type, STR("ClassProperty")))
        {
            // UE4SS's FObjectPropertyBase virtual accessors are not available in this UE5.6 shipping build.
            // Object/ObjectPtr/Class params are pointer-sized in the ProcessEvent param slab, so write raw.
            *reinterpret_cast<Unreal::UObject**>(prop_value_ptr(container, property)) = value;
            return true;
        }
        return false;
    }

    auto read_object(Unreal::FProperty* property, uint8_t* container) -> Unreal::UObject*
    {
        if (!property)
        {
            return nullptr;
        }
        const auto type = prop_type_name(property);
        if (contains_text(type, STR("WeakObjectProperty")))
        {
            return reinterpret_cast<Unreal::FWeakObjectPtr*>(prop_value_ptr(container, property))->Get();
        }
        if (contains_text(type, STR("ObjectProperty")) || contains_text(type, STR("ObjectPtrProperty")) ||
            contains_text(type, STR("ClassProperty")))
        {
            return *reinterpret_cast<Unreal::UObject**>(prop_value_ptr(container, property));
        }
        return nullptr;
    }

    auto read_int_property_by_name(Unreal::UObject* object, const CharType* property_name) -> std::optional<int>
    {
        if (!object || !object->GetClassPrivate())
        {
            return std::nullopt;
        }
        auto* property = object->GetClassPrivate()->FindProperty(Unreal::FName(property_name));
        if (!property)
        {
            return std::nullopt;
        }
        if (auto value = read_number(property, reinterpret_cast<uint8_t*>(object)))
        {
            return static_cast<int>(*value);
        }
        return std::nullopt;
    }

    auto read_number_property_by_name(Unreal::UObject* object, const CharType* property_name) -> std::optional<double>
    {
        if (!object || !object->GetClassPrivate())
        {
            return std::nullopt;
        }
        auto* property = object->GetClassPrivate()->FindProperty(Unreal::FName(property_name));
        return property ? read_number(property, reinterpret_cast<uint8_t*>(object)) : std::nullopt;
    }

    auto read_bool_property_by_name(Unreal::UObject* object, const CharType* property_name) -> std::optional<bool>
    {
        if (!object || !object->GetClassPrivate())
        {
            return std::nullopt;
        }
        auto* property = object->GetClassPrivate()->FindProperty(Unreal::FName(property_name));
        if (!property)
        {
            return std::nullopt;
        }
        return read_bool(property, reinterpret_cast<uint8_t*>(object));
    }

    auto read_object_property_by_name(Unreal::UObject* object, const CharType* property_name) -> Unreal::UObject*
    {
        if (!object || !object->GetClassPrivate())
        {
            return nullptr;
        }
        auto* property = object->GetClassPrivate()->FindProperty(Unreal::FName(property_name));
        return property ? read_object(property, reinterpret_cast<uint8_t*>(object)) : nullptr;
    }

    auto write_struct_numbers(Unreal::FStructProperty* struct_prop,
                              uint8_t* container,
                              std::initializer_list<std::pair<const CharType*, double>> values) -> bool
    {
        if (!struct_prop)
        {
            return false;
        }
        auto* base = prop_value_ptr(container, struct_prop);
        bool wrote = false;
        const auto* structure = struct_type(struct_prop);
        for (const auto& item : values)
        {
            if (auto* inner = find_struct_property(structure, item.first))
            {
                wrote = write_number(inner, base, item.second) || wrote;
            }
        }
        return wrote;
    }

    auto write_vector(Unreal::FProperty* property, uint8_t* container, const Unreal::FVector& value) -> bool
    {
        auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
        if (!struct_prop)
        {
            return false;
        }
        if (write_struct_numbers(struct_prop, container, {{STR("X"), value.X()}, {STR("Y"), value.Y()}, {STR("Z"), value.Z()}}))
        {
            return true;
        }
        const auto size = std::min<int32_t>(property->GetElementSize(), Unreal::FVector::StaticSize());
        std::memcpy(prop_value_ptr(container, property), &value, static_cast<size_t>(size));
        return true;
    }

    auto read_vector_value(Unreal::FProperty* property, uint8_t* container) -> std::optional<Unreal::FVector>
    {
        auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
        if (!struct_prop)
        {
            return std::nullopt;
        }
        auto* vec_base = prop_value_ptr(container, struct_prop);
        const auto* structure = struct_type(struct_prop);
        auto* x_prop = find_struct_property(structure, STR("X"));
        auto* y_prop = find_struct_property(structure, STR("Y"));
        auto* z_prop = find_struct_property(structure, STR("Z"));
        if (x_prop && y_prop && z_prop)
        {
            auto read_num = [](Unreal::FProperty* p, uint8_t* c) -> double {
                auto* value = prop_value_ptr(c, p);
                if (Unreal::CastField<Unreal::FFloatProperty>(p))
                {
                    float out{};
                    std::memcpy(&out, value, sizeof(float));
                    return out;
                }
                if (Unreal::CastField<Unreal::FDoubleProperty>(p))
                {
                    double out{};
                    std::memcpy(&out, value, sizeof(double));
                    return out;
                }
                if (Unreal::CastField<Unreal::FIntProperty>(p))
                {
                    int32_t out{};
                    std::memcpy(&out, value, sizeof(int32_t));
                    return static_cast<double>(out);
                }
                return 0.0;
            };
            return vec(read_num(x_prop, vec_base), read_num(y_prop, vec_base), read_num(z_prop, vec_base));
        }
        Unreal::FVector out{};
        std::memcpy(&out, vec_base, static_cast<size_t>(std::min<int32_t>(property->GetElementSize(), Unreal::FVector::StaticSize())));
        return out;
    }

    auto read_vector_from_struct(const Unreal::UStruct* structure, uint8_t* base, const CharType* name) -> std::optional<Unreal::FVector>
    {
        auto* property = find_struct_property(structure, name);
        return property ? read_vector_value(property, base) : std::nullopt;
    }

    auto read_vector2(Unreal::FProperty* property, uint8_t* container) -> std::optional<std::pair<double, double>>
    {
        auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
        if (!struct_prop)
        {
            return std::nullopt;
        }
        auto* base = prop_value_ptr(container, struct_prop);
        const auto* structure = struct_type(struct_prop);
        auto* x_prop = find_struct_property(structure, STR("X"));
        auto* y_prop = find_struct_property(structure, STR("Y"));
        if (x_prop && y_prop)
        {
            auto read_num = [](Unreal::FProperty* p, uint8_t* c) -> double {
                auto* value = prop_value_ptr(c, p);
                if (Unreal::CastField<Unreal::FFloatProperty>(p))
                {
                    float out{};
                    std::memcpy(&out, value, sizeof(float));
                    return out;
                }
                if (Unreal::CastField<Unreal::FDoubleProperty>(p))
                {
                    double out{};
                    std::memcpy(&out, value, sizeof(double));
                    return out;
                }
                return 0.0;
            };
            return std::make_pair(read_num(x_prop, base), read_num(y_prop, base));
        }
        Unreal::FVector2D out{};
        std::memcpy(&out, base, static_cast<size_t>(std::min<int32_t>(property->GetElementSize(), Unreal::FVector2D::StaticSize())));
        return std::make_pair(out.X(), out.Y());
    }

    auto read_vector2_from_struct(const Unreal::UStruct* structure, uint8_t* base, const CharType* name)
        -> std::optional<std::pair<double, double>>
    {
        auto* property = find_struct_property(structure, name);
        return property ? read_vector2(property, base) : std::nullopt;
    }

    auto decode_screen_space_paint_result(Unreal::UFunction* function, uint8_t* params) -> ScreenSpaceHitResult
    {
        ScreenSpaceHitResult out{};
        auto* return_prop = Unreal::CastField<Unreal::FStructProperty>(function ? function->GetReturnProperty() : nullptr);
        if (!return_prop)
        {
            out.failure = STR("screen_result_return_struct_unavailable");
            return out;
        }

        out.params_ok = true;
        auto* base = prop_value_ptr(params, return_prop);
        const auto* structure = struct_type(return_prop);
        if (!structure)
        {
            out.failure = STR("screen_result_struct_unavailable");
            return out;
        }
        if (auto* success_prop = find_struct_property(structure, STR("bSuccess")))
        {
            out.success = read_bool(success_prop, base);
        }
        else
        {
            out.failure = STR("screen_result_success_unavailable");
        }
        if (auto uv = read_vector2_from_struct(structure, base, STR("HitUV")))
        {
            out.u = uv->first;
            out.v = uv->second;
            out.has_uv = std::isfinite(out.u) && std::isfinite(out.v);
        }
        else if (out.failure.empty())
        {
            out.failure = STR("screen_result_hit_uv_unavailable");
        }
        if (auto world_position = read_vector_from_struct(structure, base, STR("HitWorldPosition")))
        {
            out.world_position = *world_position;
        }
        if (auto normal = read_vector_from_struct(structure, base, STR("HitNormal")))
        {
            out.normal = *normal;
        }
        return out;
    }

    auto write_vector2(Unreal::FProperty* property, uint8_t* container, double u, double v) -> bool
    {
        auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
        if (!struct_prop)
        {
            return false;
        }
        if (write_struct_numbers(struct_prop, container, {{STR("X"), u}, {STR("Y"), v}}))
        {
            return true;
        }
        auto* dest = prop_value_ptr(container, property);
        if (property->GetElementSize() == static_cast<int32_t>(sizeof(float) * 2))
        {
            const float values[2]{static_cast<float>(u), static_cast<float>(v)};
            std::memcpy(dest, values, sizeof(values));
            return true;
        }
        if (property->GetElementSize() >= static_cast<int32_t>(sizeof(double) * 2))
        {
            const double values[2]{u, v};
            std::memcpy(dest, values, sizeof(values));
            return true;
        }
        Unreal::FVector2D value{u, v};
        const auto size = std::min<int32_t>(property->GetElementSize(), Unreal::FVector2D::StaticSize());
        std::memcpy(dest, &value, static_cast<size_t>(size));
        return true;
    }

    auto write_linear_color(Unreal::FStructProperty* struct_prop, uint8_t* container, const Color& color, double alpha = 1.0) -> bool
    {
        return write_struct_numbers(struct_prop,
                                    container,
                                    {{STR("R"), color.r}, {STR("G"), color.g}, {STR("B"), color.b}, {STR("A"), alpha}});
    }

    auto read_linear_color(Unreal::FStructProperty* struct_prop, uint8_t* container) -> std::optional<Color>
    {
        if (!struct_prop)
        {
            return std::nullopt;
        }
        auto* base = prop_value_ptr(container, struct_prop);
        const auto* structure = struct_type(struct_prop);
        auto read_component = [&](const CharType* name, int raw_index) -> double {
            if (auto* property = find_struct_property(structure, name))
            {
                auto* value = prop_value_ptr(base, property);
                if (Unreal::CastField<Unreal::FFloatProperty>(property))
                {
                    float out{};
                    std::memcpy(&out, value, sizeof(float));
                    return static_cast<double>(out);
                }
                if (Unreal::CastField<Unreal::FDoubleProperty>(property))
                {
                    double out{};
                    std::memcpy(&out, value, sizeof(double));
                    return out;
                }
            }
            if (struct_prop->GetElementSize() >= static_cast<int32_t>((raw_index + 1) * sizeof(float)))
            {
                float out{};
                std::memcpy(&out, base + raw_index * sizeof(float), sizeof(float));
                return static_cast<double>(out);
            }
            return 0.0;
        };
        Color out{};
        out.r = clamp(read_component(STR("R"), 0), 0.0, 1.0);
        out.g = clamp(read_component(STR("G"), 1), 0.0, 1.0);
        out.b = clamp(read_component(STR("B"), 2), 0.0, 1.0);
        out.roughness = 0.92;
        out.metallic = 0.0;
        return out;
    }

    auto read_color_struct(Unreal::FStructProperty* struct_prop, uint8_t* container) -> std::optional<Color>
    {
        if (!struct_prop)
        {
            return std::nullopt;
        }
        auto* base = prop_value_ptr(container, struct_prop);
        const auto* structure = struct_type(struct_prop);
        const auto structure_name = structure ? lower_copy(structure->GetName()) : StringType{};

        if (contains_text(structure_name, STR("linearcolor")))
        {
            return read_linear_color(struct_prop, container);
        }

        auto read_byte_component = [&](const CharType* name, int raw_index, bool bgr_fallback) -> std::optional<double> {
            if (auto* property = find_struct_property(structure, name))
            {
                if (auto value = read_number(property, base))
                {
                    return clamp(*value > 1.0 ? *value / 255.0 : *value, 0.0, 1.0);
                }
            }
            if (struct_prop->GetElementSize() >= static_cast<int32_t>(raw_index + 1))
            {
                const auto index = bgr_fallback && raw_index < 3 ? 2 - raw_index : raw_index;
                return static_cast<double>(*(base + index)) / 255.0;
            }
            return std::nullopt;
        };

        if (contains_text(structure_name, STR("color")) || struct_prop->GetElementSize() <= 8)
        {
            Color out{};
            const auto r = read_byte_component(STR("R"), 0, true);
            const auto g = read_byte_component(STR("G"), 1, true);
            const auto b = read_byte_component(STR("B"), 2, true);
            if (r && g && b)
            {
                out.r = *r;
                out.g = *g;
                out.b = *b;
                out.roughness = 0.92;
                out.metallic = 0.0;
                return out;
            }
        }

        return read_linear_color(struct_prop, container);
    }

    auto fill_paint_channel_data_strict(Unreal::FStructProperty* struct_prop,
                                        uint8_t* container,
                                        const Color& color,
                                        PaintParamWriteStats* stats,
                                        int apply_mode_value = 0) -> bool
    {
        if (!struct_prop)
        {
            return false;
        }
        auto* base = prop_value_ptr(container, struct_prop);
        const auto* structure = struct_type(struct_prop);
        if (!structure)
        {
            if (stats)
            {
                stats->failure = STR("paint_channel_data_struct_unavailable");
            }
            return false;
        }

        const auto structure_name = lower_copy(structure->GetName());
        if (!contains_text(structure_name, STR("paintchanneldata")))
        {
            if (stats)
            {
                stats->failure = STR("non_paint_channel_data_struct_rejected");
            }
            return false;
        }

        if (stats)
        {
            stats->strict_channel_data = true;
            stats->color_struct = structure->GetName();
        }

        bool wrote_albedo = false;
        bool wrote_metallic = false;
        bool wrote_roughness = false;
        bool wrote_height = false;
        bool wrote_apply_mode = false;

        if (auto* albedo = Unreal::CastField<Unreal::FStructProperty>(find_struct_property(structure, STR("AlbedoColor"))))
        {
            wrote_albedo = write_linear_color(albedo, base, color);
        }

        if (auto* metallic = find_struct_property(structure, STR("Metallic")))
        {
            wrote_metallic = write_number(metallic, base, color.metallic);
        }
        if (auto* roughness = find_struct_property(structure, STR("Roughness")))
        {
            wrote_roughness = write_number(roughness, base, color.roughness);
        }
        if (auto* height = find_struct_property(structure, STR("Height")))
        {
            wrote_height = write_number(height, base, 0.0);
        }
        if (auto* apply_mode = find_struct_property(structure, STR("ApplyMode")))
        {
            wrote_apply_mode = write_number(apply_mode, base, static_cast<double>(apply_mode_value));
        }

        if (!wrote_albedo && stats)
        {
            stats->failure = STR("paint_channel_data_albedo_color_unresolved");
        }
        else if (!wrote_metallic && stats)
        {
            stats->failure = STR("paint_channel_data_metallic_unresolved");
        }
        else if (!wrote_roughness && stats)
        {
            stats->failure = STR("paint_channel_data_roughness_unresolved");
        }
        else if (!wrote_height && stats)
        {
            stats->failure = STR("paint_channel_data_height_unresolved");
        }
        else if (!wrote_apply_mode && stats)
        {
            stats->failure = STR("paint_channel_data_apply_mode_unresolved");
        }
        if (wrote_albedo && wrote_metallic && wrote_roughness && wrote_height && wrote_apply_mode)
        {
            if (stats)
            {
                stats->failure.clear();
            }
            return true;
        }
        if (stats && stats->failure.empty())
        {
            stats->failure = STR("paint_channel_data_fields_unresolved");
        }
        return false;
    }

    auto fill_brush_settings(Unreal::FStructProperty* struct_prop, uint8_t* container, double radius, double opacity, double hardness) -> bool
    {
        if (!struct_prop)
        {
            return false;
        }
        auto* base = prop_value_ptr(container, struct_prop);
        const auto* structure = struct_type(struct_prop);
        bool wrote = false;
        const std::array<const CharType*, 4> radius_fields{STR("Radius"), STR("BrushRadius"), STR("Size"), STR("BrushSize")};
        for (const auto* field : radius_fields)
        {
            if (auto* inner = find_struct_property(structure, field))
            {
                wrote = write_number(inner, base, radius) || wrote;
            }
        }
        const std::array<const CharType*, 3> opacity_fields{STR("Opacity"), STR("BrushOpacity"), STR("Strength")};
        for (const auto* field : opacity_fields)
        {
            if (auto* inner = find_struct_property(structure, field))
            {
                wrote = write_number(inner, base, opacity) || wrote;
            }
        }
        const std::array<const CharType*, 2> hardness_fields{STR("Hardness"), STR("BrushHardness")};
        for (const auto* field : hardness_fields)
        {
            if (auto* inner = find_struct_property(structure, field))
            {
                wrote = write_number(inner, base, hardness) || wrote;
            }
        }
        const std::array<const CharType*, 3> falloff_fields{STR("Falloff"), STR("BrushFalloff"), STR("BrushFalloffAmount")};
        for (const auto* field : falloff_fields)
        {
            if (auto* inner = find_struct_property(structure, field))
            {
                wrote = write_number(inner, base, 0.18) || wrote;
            }
        }
        if (auto* inner = find_struct_property(structure, STR("Spacing")))
        {
            wrote = write_number(inner, base, 0.10) || wrote;
        }
        if (auto* inner = find_struct_property(structure, STR("Rotation")))
        {
            wrote = write_number(inner, base, 0.0) || wrote;
        }
        if (!wrote && struct_prop->GetElementSize() >= 16)
        {
            auto* raw = reinterpret_cast<float*>(base);
            raw[0] = static_cast<float>(clamp(radius, 0.001, 1.0));
            raw[1] = static_cast<float>(clamp(opacity, 0.0, 1.0));
            raw[2] = static_cast<float>(clamp(hardness, 0.0, 1.0));
            raw[3] = 0.18f;
            wrote = true;
        }
        return wrote;
    }

    auto read_return_bool(Unreal::UFunction* function, uint8_t* params) -> bool
    {
        auto* return_prop = function ? function->GetReturnProperty() : nullptr;
        return return_prop ? read_bool(return_prop, params) : true;
    }

    auto read_return_object(Unreal::UFunction* function, uint8_t* params) -> Unreal::UObject*
    {
        auto* return_prop = function ? function->GetReturnProperty() : nullptr;
        return return_prop ? read_object(return_prop, params) : nullptr;
    }

    auto read_return_number(Unreal::UFunction* function, uint8_t* params) -> std::optional<double>
    {
        auto* return_prop = function ? function->GetReturnProperty() : nullptr;
        return return_prop ? read_number(return_prop, params) : std::nullopt;
    }

    auto call_no_params_return_number(Unreal::UObject* object, const CharType* function_name) -> std::optional<double>
    {
        if (!object)
        {
            return std::nullopt;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return std::nullopt;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        object->ProcessEvent(function, params.data());
        return read_return_number(function, params.data());
    }

    struct ArrayParamProbeStats
    {
        bool valid{false};
        int num{0};
        int max{0};
        int element_size{0};
        int first0{-1};
        int first1{-1};
        int first2{-1};
        int first3{-1};
        uint64_t hash{1469598103934665603ULL};
        StringType inner_type{};
    };

    auto read_array_param_stats(Unreal::FArrayProperty* array_property, uint8_t* container) -> ArrayParamProbeStats
    {
        ArrayParamProbeStats stats{};
        if (!array_property)
        {
            return stats;
        }
        auto* inner = array_property->GetInner();
        auto* array = reinterpret_cast<Unreal::FScriptArray*>(prop_value_ptr(container, array_property));
        if (!inner || !array)
        {
            return stats;
        }

        stats.inner_type = prop_type_name(inner);
        stats.element_size = std::max(1, inner->GetSize());
        stats.num = array->NumUnchecked();
        stats.max = array->Max();
        if (stats.num < 0 || stats.max < stats.num || stats.num > 64 * 1024 * 1024)
        {
            return stats;
        }

        stats.valid = true;
        auto* data = static_cast<uint8_t*>(array->GetData());
        const size_t bytes = static_cast<size_t>(stats.num) * static_cast<size_t>(stats.element_size);
        if (data && bytes > 0)
        {
            const auto sample_bytes = std::min<size_t>(bytes, 64 * 1024);
            stats.hash = fnv1a_update(stats.hash, data, sample_bytes);
            stats.first0 = data[0];
            if (bytes > 1)
            {
                stats.first1 = data[1];
            }
            if (bytes > 2)
            {
                stats.first2 = data[2];
            }
            if (bytes > 3)
            {
                stats.first3 = data[3];
            }
        }
        return stats;
    }

    auto cleanup_array_param(Unreal::FArrayProperty* array_property, uint8_t* container) -> void
    {
        if (!array_property)
        {
            return;
        }
        auto* inner = array_property->GetInner();
        auto* array = reinterpret_cast<Unreal::FScriptArray*>(prop_value_ptr(container, array_property));
        if (!inner || !array)
        {
            return;
        }
        const auto num = array->NumUnchecked();
        const auto max = array->Max();
        if (num >= 0 && max >= num && num < 64 * 1024 * 1024)
        {
            array->Empty(0, inner->GetSize(), inner->GetMinAlignment());
        }
    }

    struct ChannelByteBuffer
    {
        bool ok{false};
        int channel{PaintChannelUnknown};
        int width{0};
        int height{0};
        int num{0};
        int max{0};
        int element_size{0};
        int first0{-1};
        int first1{-1};
        int first2{-1};
        int first3{-1};
        uint64_t hash{1469598103934665603ULL};
        StringType label{};
        StringType target_name{};
        StringType failure{};
        std::vector<uint8_t> bytes{};
    };

    auto hash_bytes(const std::vector<uint8_t>& bytes) -> uint64_t
    {
        return bytes.empty() ? 1469598103934665603ULL
                             : fnv1a_update(1469598103934665603ULL, bytes.data(), bytes.size());
    }

    auto export_channel_bytes(Unreal::UObject* component, int channel) -> ChannelByteBuffer
    {
        ChannelByteBuffer out{};
        out.channel = channel;
        if (!component)
        {
            out.failure = STR("runtime_paint_component_unavailable");
            return out;
        }
        auto* function = component->GetFunctionByNameInChain(STR("ExportChannelToBytes"));
        if (!function)
        {
            out.failure = STR("export_channel_to_bytes_unavailable");
            return out;
        }

        out.label = channel_enum_label(function, channel);
        auto* target = get_render_target_for_channel(component, channel);
        const auto [width, height] = render_target_dimensions(target);
        out.width = width;
        out.height = height;
        out.target_name = target ? target->GetFullName() : STR("<null>");

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        std::vector<Unreal::FArrayProperty*> array_params{};
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (auto* array_prop = Unreal::CastField<Unreal::FArrayProperty>(property))
            {
                new (prop_value_ptr(params.data(), array_prop)) Unreal::FScriptArray{};
                array_params.push_back(array_prop);
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("channel")))
            {
                write_number(property, params.data(), static_cast<double>(channel));
            }
        }

        component->ProcessEvent(function, params.data());
        const auto return_ok = read_return_bool(function, params.data());
        if (!return_ok)
        {
            out.failure = STR("export_return_false");
        }
        if (array_params.empty())
        {
            out.failure = out.failure.empty() ? STR("export_array_param_unavailable") : out.failure;
            return out;
        }

        auto* array_prop = array_params.front();
        const auto stats = read_array_param_stats(array_prop, params.data());
        out.num = stats.num;
        out.max = stats.max;
        out.element_size = stats.element_size;
        out.first0 = stats.first0;
        out.first1 = stats.first1;
        out.first2 = stats.first2;
        out.first3 = stats.first3;
        auto* array = reinterpret_cast<Unreal::FScriptArray*>(prop_value_ptr(params.data(), array_prop));
        if (return_ok && stats.valid && stats.element_size == 1 && stats.num > 0 && array && array->GetData())
        {
            auto* data = static_cast<uint8_t*>(array->GetData());
            out.bytes.assign(data, data + stats.num);
            out.hash = hash_bytes(out.bytes);
            out.ok = true;
        }
        else if (out.failure.empty())
        {
            out.failure = STR("export_array_invalid_or_non_byte");
        }
        for (auto* prop : array_params)
        {
            cleanup_array_param(prop, params.data());
        }
        return out;
    }

    auto import_channel_bytes(Unreal::UObject* component, int channel, const std::vector<uint8_t>& bytes, StringType& failure) -> bool
    {
        failure.clear();
        if (!component)
        {
            failure = STR("runtime_paint_component_unavailable");
            return false;
        }
        if (bytes.empty())
        {
            failure = STR("import_bytes_empty");
            return false;
        }
        auto* function = component->GetFunctionByNameInChain(STR("ImportChannelFromBytes"));
        if (!function)
        {
            failure = STR("import_channel_from_bytes_unavailable");
            return false;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        std::vector<Unreal::FArrayProperty*> array_params{};
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (auto* array_prop = Unreal::CastField<Unreal::FArrayProperty>(property))
            {
                auto* inner = array_prop->GetInner();
                auto* array = new (prop_value_ptr(params.data(), array_prop)) Unreal::FScriptArray{};
                array_params.push_back(array_prop);
                if (!inner || inner->GetSize() != 1)
                {
                    failure = STR("import_array_inner_not_byte_sized");
                    continue;
                }
                array->AddZeroed(static_cast<int32_t>(bytes.size()), inner->GetSize(), inner->GetMinAlignment());
                std::memcpy(array->GetData(), bytes.data(), bytes.size());
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("channel")))
            {
                write_number(property, params.data(), static_cast<double>(channel));
            }
        }

        if (array_params.empty() && failure.empty())
        {
            failure = STR("import_array_param_unavailable");
        }
        const auto can_call = failure.empty();
        if (can_call)
        {
            component->ProcessEvent(function, params.data());
        }
        const auto ok = can_call && read_return_bool(function, params.data());
        if (!ok && failure.empty())
        {
            failure = STR("import_return_false");
        }
        for (auto* prop : array_params)
        {
            cleanup_array_param(prop, params.data());
        }
        return ok;
    }

    auto call_no_params_return_object(Unreal::UObject* object, const CharType* function_name) -> Unreal::UObject*
    {
        if (!object)
        {
            return nullptr;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return nullptr;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        object->ProcessEvent(function, params.data());
        return read_return_object(function, params.data());
    }

    auto call_number_return_object(Unreal::UObject* object, const CharType* function_name, double value) -> Unreal::UObject*
    {
        if (!object)
        {
            return nullptr;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return nullptr;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote_number = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (write_number(property, params.data(), value))
            {
                wrote_number = true;
                break;
            }
        }
        if (!wrote_number)
        {
            return nullptr;
        }
        object->ProcessEvent(function, params.data());
        return read_return_object(function, params.data());
    }

    auto call_no_params_return_bool(Unreal::UObject* object, const CharType* function_name) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        object->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto call_no_params_return_vector(Unreal::UObject* object, const CharType* function_name) -> std::optional<Unreal::FVector>
    {
        if (!object)
        {
            return std::nullopt;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return std::nullopt;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        object->ProcessEvent(function, params.data());
        auto* return_prop = Unreal::CastField<Unreal::FStructProperty>(function->GetReturnProperty());
        if (!return_prop)
        {
            return std::nullopt;
        }
        Unreal::FVector out{};
        std::memcpy(&out,
                    prop_value_ptr(params.data(), return_prop),
                    static_cast<size_t>(std::min<int32_t>(return_prop->GetElementSize(), Unreal::FVector::StaticSize())));
        return out;
    }

    auto call_no_params_return_rotator(Unreal::UObject* object, const CharType* function_name) -> std::optional<Unreal::FRotator>
    {
        if (!object)
        {
            return std::nullopt;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return std::nullopt;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        object->ProcessEvent(function, params.data());
        auto* return_prop = Unreal::CastField<Unreal::FStructProperty>(function->GetReturnProperty());
        if (!return_prop)
        {
            return std::nullopt;
        }
        Unreal::FRotator out{};
        std::memcpy(&out,
                    prop_value_ptr(params.data(), return_prop),
                    static_cast<size_t>(std::min<int32_t>(return_prop->GetElementSize(), static_cast<int32_t>(sizeof(Unreal::FRotator)))));
        return out;
    }

    auto is_live_player_controller(Unreal::UObject* controller, Unreal::UObject* world_context = nullptr) -> bool
    {
        if (!controller)
        {
            return false;
        }
        const auto text = lower_copy(controller->GetFullName());
        if (contains_text(text, STR("default__")))
        {
            return false;
        }
        auto* controller_world = controller->GetWorld();
        if (!controller_world)
        {
            return false;
        }
        auto* expected_world = world_context ? world_context->GetWorld() : nullptr;
        return !expected_world || controller_world == expected_world;
    }

    auto is_live_pawn_candidate(Unreal::UObject* pawn, Unreal::UObject* world_context = nullptr) -> bool
    {
        if (!pawn)
        {
            return false;
        }
        const auto text = lower_copy(pawn->GetFullName());
        if (contains_text(text, STR("default__")))
        {
            return false;
        }
        auto* pawn_world = pawn->GetWorld();
        if (!pawn_world)
        {
            return false;
        }
        auto* expected_world = world_context ? world_context->GetWorld() : nullptr;
        return !expected_world || pawn_world == expected_world;
    }

    auto find_runtime_paint_component_owned_by(Unreal::UObject* pawn) -> Unreal::UObject*
    {
        auto* pawn_world = pawn ? pawn->GetWorld() : nullptr;
        if (!pawn)
        {
            return nullptr;
        }
        std::vector<Unreal::UObject*> objects{};
        Unreal::UObjectGlobals::FindObjects(256, STR("RuntimePaintableComponent"), nullptr, objects, 0, 0, false);
        for (auto* object : objects)
        {
            if (!object)
            {
                continue;
            }
            auto* owner = call_no_params_return_object(object, STR("GetOwner"));
            auto* object_world = object->GetWorld();
            const bool same_world = !pawn_world || !object_world || object_world == pawn_world;
            const bool has_uv_paint = object->GetFunctionByNameInChain(STR("PaintAtUVWithBrush")) ||
                                      object->GetFunctionByNameInChain(STR("PaintAtUV")) ||
                                      object->GetFunctionByNameInChain(STR("PaintStrokeUV"));
            if (owner == pawn && same_world && has_uv_paint)
            {
                return object;
            }
        }
        return nullptr;
    }

    auto find_player_controller() -> Unreal::UObject*
    {
        std::vector<Unreal::UObject*> controllers{};
        Unreal::UObjectGlobals::FindObjects(512, STR("PlayerController"), nullptr, controllers, 0, 0, false);
        for (auto* controller : controllers)
        {
            if (!is_live_player_controller(controller))
            {
                continue;
            }
            auto* pawn = call_no_params_return_object(controller, STR("GetPawn"));
            if (pawn && call_no_params_return_bool(pawn, STR("IsPlayerControlled")))
            {
                return controller;
            }
        }
        for (auto* controller : controllers)
        {
            if (!is_live_player_controller(controller))
            {
                continue;
            }
            auto* pawn = call_no_params_return_object(controller, STR("GetPawn"));
            auto* view_target = call_no_params_return_object(controller, STR("GetViewTarget"));
            auto* camera = call_no_params_return_object(controller, STR("GetPlayerCameraManager"));
            auto* camera_view_target = call_no_params_return_object(camera, STR("GetViewTarget"));
            if (find_runtime_paint_component_owned_by(view_target) ||
                find_runtime_paint_component_owned_by(camera_view_target) ||
                find_runtime_paint_component_owned_by(pawn))
            {
                return controller;
            }
            if (controller && (pawn || camera))
            {
                return controller;
            }
        }
        for (auto* controller : controllers)
        {
            if (is_live_player_controller(controller))
            {
                return controller;
            }
        }
        return nullptr;
    }

    auto find_player_controller_for_pawn(Unreal::UObject* pawn) -> Unreal::UObject*
    {
        if (!pawn)
        {
            return find_player_controller();
        }

        if (auto* controller = call_no_params_return_object(pawn, STR("GetController")))
        {
            if (is_live_player_controller(controller, pawn))
            {
                return controller;
            }
        }
        if (auto* controller = read_object_property_by_name(pawn, STR("Controller")))
        {
            if (is_live_player_controller(controller, pawn))
            {
                return controller;
            }
        }

        std::vector<Unreal::UObject*> controllers{};
        Unreal::UObjectGlobals::FindObjects(512, STR("PlayerController"), nullptr, controllers, 0, 0, false);
        for (auto* controller : controllers)
        {
            if (!is_live_player_controller(controller, pawn))
            {
                continue;
            }
            auto* controlled_pawn = call_no_params_return_object(controller, STR("GetPawn"));
            auto* view_target = call_no_params_return_object(controller, STR("GetViewTarget"));
            auto* camera = call_no_params_return_object(controller, STR("GetPlayerCameraManager"));
            auto* camera_view_target = call_no_params_return_object(camera, STR("GetViewTarget"));
            if (controlled_pawn == pawn || view_target == pawn || camera_view_target == pawn ||
                object_is_or_belongs_to(controlled_pawn, pawn) ||
                object_is_or_belongs_to(view_target, pawn) ||
                object_is_or_belongs_to(camera_view_target, pawn))
            {
                return controller;
            }
        }
        for (auto* controller : controllers)
        {
            if (is_live_player_controller(controller, pawn))
            {
                return controller;
            }
        }
        return nullptr;
    }

    auto find_player_pawn() -> Unreal::UObject*
    {
        struct Candidate
        {
            Unreal::UObject* pawn{nullptr};
            int score{-1000000};
            StringType source{};
        };

        if (auto* controller = find_player_controller())
        {
            auto* controller_pawn = call_no_params_return_object(controller, STR("GetPawn"));
            auto* controller_view_target = call_no_params_return_object(controller, STR("GetViewTarget"));
            auto* camera = call_no_params_return_object(controller, STR("GetPlayerCameraManager"));
            auto* camera_view_target = call_no_params_return_object(camera, STR("GetViewTarget"));

            Candidate best{};
            const auto consider = [&](Unreal::UObject* pawn, int base_score, const CharType* source) {
                if (!is_live_pawn_candidate(pawn, controller))
                {
                    return;
                }
                int score = base_score;
                if (call_no_params_return_bool(pawn, STR("IsPlayerControlled")))
                {
                    score += 20;
                }
                if (call_no_params_return_object(pawn, STR("GetController")) == controller ||
                    read_object_property_by_name(pawn, STR("Controller")) == controller)
                {
                    score += 12;
                }
                if (find_runtime_paint_component_owned_by(pawn))
                {
                    score += 80;
                }
                const auto lower = lower_copy(pawn->GetFullName());
                if (contains_text(lower, STR("character")) || contains_text(lower, STR("pawn")))
                {
                    score += 5;
                }
                if (!best.pawn || score > best.score)
                {
                    best.pawn = pawn;
                    best.score = score;
                    best.source = source;
                }
            };

            consider(controller_pawn, 110, STR("controller_pawn"));
            consider(controller_view_target, 125, STR("controller_view_target"));
            consider(camera_view_target, 120, STR("camera_view_target"));

            if (best.pawn && find_runtime_paint_component_owned_by(best.pawn))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} target_select source={} score={} controller={} selected_pawn={} controller_pawn={} controller_view_target={} camera_view_target={}\n"),
                    ModTag,
                    best.source.empty() ? STR("<none>") : best.source,
                    best.score,
                    controller->GetFullName(),
                    best.pawn->GetFullName(),
                    controller_pawn ? controller_pawn->GetFullName() : STR("<null>"),
                    controller_view_target ? controller_view_target->GetFullName() : STR("<null>"),
                    camera_view_target ? camera_view_target->GetFullName() : STR("<null>"));
                return best.pawn;
            }
            if (controller_pawn && call_no_params_return_bool(controller_pawn, STR("IsPlayerControlled")))
            {
                return controller_pawn;
            }
            if (best.pawn)
            {
                return best.pawn;
            }
        }
        std::vector<Unreal::UObject*> pawns{};
        Unreal::UObjectGlobals::FindObjects(128, STR("Pawn"), nullptr, pawns, 0, 0, false);
        for (auto* pawn : pawns)
        {
            if (is_live_pawn_candidate(pawn) && call_no_params_return_bool(pawn, STR("IsPlayerControlled")) &&
                find_runtime_paint_component_owned_by(pawn))
            {
                return pawn;
            }
        }
        for (auto* pawn : pawns)
        {
            if (is_live_pawn_candidate(pawn) && call_no_params_return_bool(pawn, STR("IsPlayerControlled")))
            {
                return pawn;
            }
        }
        return nullptr;
    }

    auto get_viewport_info(Unreal::UObject* controller) -> ViewportInfo
    {
        ViewportInfo out{};
        if (!controller)
        {
            return out;
        }
        auto* function = controller->GetFunctionByNameInChain(STR("GetViewportSize"));
        if (!function)
        {
            return out;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        controller->ProcessEvent(function, params.data());
        int width = 0;
        int height = 0;
        std::vector<int> numeric_values{};
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            auto value = read_number(property, params.data());
            if (!value)
            {
                continue;
            }
            const auto int_value = static_cast<int>(*value);
            const auto name = lower_copy(property->GetName());
            if ((contains_text(name, STR("sizex")) || contains_text(name, STR("width")) || name == STR("x")) &&
                int_value > 0)
            {
                width = int_value;
            }
            else if ((contains_text(name, STR("sizey")) || contains_text(name, STR("height")) || name == STR("y")) &&
                     int_value > 0)
            {
                height = int_value;
            }
            if (int_value > 0)
            {
                numeric_values.push_back(int_value);
            }
        }
        if ((width <= 0 || height <= 0) && numeric_values.size() >= 2)
        {
            width = numeric_values[0];
            height = numeric_values[1];
        }
        if (width > 0 && height > 0)
        {
            out.width = width;
            out.height = height;
            out.fallback = false;
        }
        return out;
    }

    auto camera_manager_for_controller(Unreal::UObject* controller) -> Unreal::UObject*
    {
        if (!controller)
        {
            return nullptr;
        }
        if (auto* camera = call_no_params_return_object(controller, STR("GetPlayerCameraManager")))
        {
            return camera;
        }
        if (!controller->GetClassPrivate())
        {
            return nullptr;
        }
        auto* property = controller->GetClassPrivate()->FindProperty(Unreal::FName(STR("PlayerCameraManager")));
        return property ? read_object(property, reinterpret_cast<uint8_t*>(controller)) : nullptr;
    }

    auto find_player_camera_manager() -> Unreal::UObject*
    {
        if (auto* controller = find_player_controller())
        {
            if (auto* camera = camera_manager_for_controller(controller))
            {
                return camera;
            }
        }

        std::vector<Unreal::UObject*> controllers{};
        Unreal::UObjectGlobals::FindObjects(512, STR("PlayerController"), nullptr, controllers, 0, 0, false);
        for (auto* controller : controllers)
        {
            if (!is_live_player_controller(controller))
            {
                continue;
            }
            if (auto* camera = camera_manager_for_controller(controller))
            {
                return camera;
            }
        }
        return nullptr;
    }

    auto find_runtime_paint_component_for(Unreal::UObject* pawn) -> Unreal::UObject*
    {
        return find_runtime_paint_component_owned_by(pawn);
    }

    auto find_target_mesh_for_runtime_paint(Unreal::UObject* component, Unreal::UObject* pawn) -> Unreal::UObject*
    {
        const std::array<const CharType*, 6> fields{STR("TargetMeshComponent"),
                                                    STR("TargetMesh"),
                                                    STR("MeshComponent"),
                                                    STR("SkeletalMeshComponent"),
                                                    STR("Mesh"),
                                                    STR("OwnerMesh")};
        for (const auto* field : fields)
        {
            if (auto* object = read_object_property_by_name(component, field))
            {
                return object;
            }
        }
        if (component && component->GetClassPrivate())
        {
            for (auto* property : component->GetClassPrivate()->ForEachProperty())
            {
                if (!property)
                {
                    continue;
                }
                const auto text = lower_copy(property->GetName() + STR(" ") + prop_type_name(property));
                if ((contains_text(text, STR("mesh")) || contains_text(text, STR("target"))) &&
                    (contains_text(text, STR("object")) || contains_text(text, STR("component"))))
                {
                    if (auto* object = read_object(property, reinterpret_cast<uint8_t*>(component)))
                    {
                        return object;
                    }
                }
            }
        }
        for (const auto* field : fields)
        {
            if (auto* object = read_object_property_by_name(pawn, field))
            {
                return object;
            }
        }
        return nullptr;
    }

    auto find_capture_component(Unreal::UObject* actor) -> Unreal::UObject*
    {
        if (!actor)
        {
            return nullptr;
        }
        if (auto* component = call_no_params_return_object(actor, STR("GetCaptureComponent2D")))
        {
            return component;
        }
        const std::array<const CharType*, 4> fields{
            STR("CaptureComponent2D"), STR("SceneCaptureComponent2D"), STR("SceneCapture"), STR("CaptureComponent")};
        for (const auto* field : fields)
        {
            if (actor->GetClassPrivate())
            {
                if (auto* property = actor->GetClassPrivate()->FindProperty(Unreal::FName(field)))
                {
                    if (auto* object = read_object(property, reinterpret_cast<uint8_t*>(actor)))
                    {
                        return object;
                    }
                }
            }
        }
        return nullptr;
    }

    auto camera_contract_token_match(const StringType& text) -> bool
    {
        return contains_any_text(text,
                                 {STR("camera"),
                                  STR("spring"),
                                  STR("arm"),
                                  STR("boom"),
                                  STR("view"),
                                  STR("target"),
                                  STR("fov"),
                                  STR("fieldofview"),
                                  STR("viewport"),
                                  STR("paintview"),
                                  STR("keepcamera"),
                                  STR("freecamera"),
                                  STR("defeultfps"),
                                  STR("prelocation")});
    }

    auto camera_contract_function_token_match(const StringType& text) -> bool
    {
        return contains_any_text(text,
                                 {STR("camera"),
                                  STR("viewtarget"),
                                  STR("view target"),
                                  STR("paintview"),
                                  STR("keeprotation"),
                                  STR("freecamera"),
                                  STR("camerapitch"),
                                  STR("camerareset"),
                                  STR("resetbodyandcamera"),
                                  STR("getfovangle"),
                                  STR("getcameralocation"),
                                  STR("getcamerarotation"),
                                  STR("blueprintupdatecamera")});
    }

    auto camera_struct_value_token_match(const StringType& text) -> bool
    {
        return contains_any_text(text,
                                 {STR("target"),
                                  STR("pov"),
                                  STR("location"),
                                  STR("rotation"),
                                  STR("fov"),
                                  STR("desiredfov"),
                                  STR("aspect"),
                                  STR("projection"),
                                  STR("ortho"),
                                  STR("offcenter"),
                                  STR("constrain"),
                                  STR("view"),
                                  STR("camera"),
                                  STR("pitch"),
                                  STR("yaw"),
                                  STR("roll"),
                                  STR("prelocation"),
                                  STR("paintcameradistancerange")});
    }

    auto read_struct_named_triplet(Unreal::FProperty* property,
                                   uint8_t* container,
                                   const CharType* first,
                                   const CharType* second,
                                   const CharType* third) -> std::optional<std::array<double, 3>>
    {
        auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
        if (!struct_prop)
        {
            return std::nullopt;
        }
        auto* base = prop_value_ptr(container, struct_prop);
        const auto* structure = struct_type(struct_prop);
        auto* first_prop = find_struct_property(structure, first);
        auto* second_prop = find_struct_property(structure, second);
        auto* third_prop = find_struct_property(structure, third);
        if (!first_prop || !second_prop || !third_prop)
        {
            return std::nullopt;
        }
        auto first_value = read_number(first_prop, base);
        auto second_value = read_number(second_prop, base);
        auto third_value = read_number(third_prop, base);
        if (!first_value || !second_value || !third_value)
        {
            return std::nullopt;
        }
        return std::array<double, 3>{*first_value, *second_value, *third_value};
    }

    auto dump_camera_struct_values(const CharType* owner_label,
                                   Unreal::UObject* owner,
                                   const StringType& prefix,
                                   const Unreal::UStruct* structure,
                                   uint8_t* base,
                                   int depth,
                                   int& printed,
                                   int limit) -> void
    {
        if (!structure || !base || printed >= limit || depth > 4)
        {
            return;
        }
        for (auto* property : const_cast<Unreal::UStruct*>(structure)->ForEachProperty())
        {
            if (!property || printed >= limit)
            {
                continue;
            }

            const auto name = prefix + property->GetName();
            auto text = lower_copy(name + STR(" ") + prop_type_name(property));
            if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                if (auto* nested_structure = struct_type(struct_prop))
                {
                    text += STR(" ");
                    text += lower_copy(nested_structure->GetName() + STR(" ") + nested_structure->GetFullName());
                }
            }
            const bool selected = camera_struct_value_token_match(text);

            const auto type = prop_type_name(property);
            if (selected)
            {
                if (auto* selected_struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                {
                    auto* selected_structure = struct_type(selected_struct_prop);
                    const auto structure_text = lower_copy(selected_structure ? selected_structure->GetName() + STR(" ") +
                                                                                   selected_structure->GetFullName()
                                                                             : StringType{});
                    const bool scalar_struct = contains_any_text(structure_text,
                                                                  {STR("vector"),
                                                                   STR("vector2"),
                                                                   STR("rotator"),
                                                                   STR("color"),
                                                                   STR("linearcolor")});
                    if (!scalar_struct)
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("{} game_camera_struct_value owner_label={} owner={} path={} type={} struct={} kind=struct_enter\n"),
                            ModTag,
                            owner_label,
                            owner ? owner->GetFullName() : STR("<null>"),
                            name,
                            type,
                            selected_structure ? selected_structure->GetFullName() : STR("<null>"));
                        ++printed;
                        dump_camera_struct_values(owner_label,
                                                  owner,
                                                  name + STR("."),
                                                  selected_structure,
                                                  prop_value_ptr(base, property),
                                                  depth + 1,
                                                  printed,
                                                  limit);
                        continue;
                    }
                }
                if (auto* value_object = read_object(property, base))
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} game_camera_struct_value owner_label={} owner={} path={} type={} kind=object value={}\n"),
                        ModTag,
                        owner_label,
                        owner ? owner->GetFullName() : STR("<null>"),
                        name,
                        type,
                        value_object->GetFullName());
                    ++printed;
                    continue;
                }
                if (auto number = read_number(property, base))
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} game_camera_struct_value owner_label={} owner={} path={} type={} kind=number value={}\n"),
                        ModTag,
                        owner_label,
                        owner ? owner->GetFullName() : STR("<null>"),
                        name,
                        type,
                        *number);
                    ++printed;
                    continue;
                }
                if (type == STR("BoolProperty"))
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} game_camera_struct_value owner_label={} owner={} path={} type={} kind=bool value={}\n"),
                        ModTag,
                        owner_label,
                        owner ? owner->GetFullName() : STR("<null>"),
                        name,
                        type,
                        read_bool(property, base) ? 1 : 0);
                    ++printed;
                    continue;
                }
                if (auto rotator = read_struct_named_triplet(property, base, STR("Pitch"), STR("Yaw"), STR("Roll")))
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} game_camera_struct_value owner_label={} owner={} path={} type={} kind=rotator pitch={} yaw={} roll={}\n"),
                        ModTag,
                        owner_label,
                        owner ? owner->GetFullName() : STR("<null>"),
                        name,
                        type,
                        (*rotator)[0],
                        (*rotator)[1],
                        (*rotator)[2]);
                    ++printed;
                    continue;
                }
                if (auto vector = read_struct_named_triplet(property, base, STR("X"), STR("Y"), STR("Z")))
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} game_camera_struct_value owner_label={} owner={} path={} type={} kind=vector x={} y={} z={}\n"),
                        ModTag,
                        owner_label,
                        owner ? owner->GetFullName() : STR("<null>"),
                        name,
                        type,
                        (*vector)[0],
                        (*vector)[1],
                        (*vector)[2]);
                    ++printed;
                    continue;
                }
                auto* vector2_struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
                auto* vector2_structure = struct_type(vector2_struct_prop);
                const auto vector2_structure_text = lower_copy(vector2_structure ? vector2_structure->GetName() + STR(" ") +
                                                                                       vector2_structure->GetFullName()
                                                                                 : StringType{});
                if (contains_text(vector2_structure_text, STR("vector2")))
                {
                    if (auto vector2 = read_vector2(property, base))
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("{} game_camera_struct_value owner_label={} owner={} path={} type={} kind=vector2 x={} y={}\n"),
                            ModTag,
                            owner_label,
                            owner ? owner->GetFullName() : STR("<null>"),
                            name,
                            type,
                            vector2->first,
                            vector2->second);
                        ++printed;
                        continue;
                    }
                }
            }

            if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                auto* nested_structure = struct_type(struct_prop);
                const bool should_recurse = selected || depth < 2 || contains_text(lower_copy(name), STR("pov"));
                if (should_recurse)
                {
                    dump_camera_struct_values(owner_label,
                                              owner,
                                              name + STR("."),
                                              nested_structure,
                                              prop_value_ptr(base, property),
                                              depth + 1,
                                              printed,
                                              limit);
                }
            }
        }
    }

    auto dump_camera_contract_properties(const CharType* owner_label, Unreal::UObject* object, int limit = 80) -> void
    {
        if (!object || !object->GetClassPrivate())
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} game_camera_object label={} object={} class=<null> properties=0\n"),
                ModTag,
                owner_label,
                object ? object->GetFullName() : STR("<null>"));
            return;
        }

        auto* klass = object->GetClassPrivate();
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} game_camera_object label={} object={} class={} world={}\n"),
            ModTag,
            owner_label,
            object->GetFullName(),
            klass->GetFullName(),
            object->GetWorld() ? object->GetWorld()->GetFullName() : STR("<null>"));

        int matched = 0;
        int printed = 0;
        for (auto* property : klass->ForEachProperty())
        {
            if (!property)
            {
                continue;
            }
            auto text = lower_copy(property->GetName() + STR(" ") + prop_type_name(property));
            if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                if (auto* structure = struct_type(struct_prop))
                {
                    text += STR(" ");
                    text += lower_copy(structure->GetName() + STR(" ") + structure->GetFullName());
                }
            }
            if (!camera_contract_token_match(text))
            {
                continue;
            }

            ++matched;
            if (printed >= limit)
            {
                continue;
            }

            auto* base = reinterpret_cast<uint8_t*>(object);
            const auto type = prop_type_name(property);
            if (auto* value_object = read_object(property, base))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_property owner_label={} owner={} name={} type={} offset={} size={} kind=object value={}\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    type,
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    value_object->GetFullName());
            }
            else if (auto number = read_number(property, base))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_property owner_label={} owner={} name={} type={} offset={} size={} kind=number value={}\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    type,
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    *number);
            }
            else if (type == STR("BoolProperty"))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_property owner_label={} owner={} name={} type={} offset={} size={} kind=bool value={}\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    type,
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    read_bool(property, base) ? 1 : 0);
            }
            else if (auto rotator = read_struct_named_triplet(property, base, STR("Pitch"), STR("Yaw"), STR("Roll")))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_property owner_label={} owner={} name={} type={} offset={} size={} kind=rotator pitch={} yaw={} roll={}\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    type,
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    (*rotator)[0],
                    (*rotator)[1],
                    (*rotator)[2]);
            }
            else if (auto vector = read_struct_named_triplet(property, base, STR("X"), STR("Y"), STR("Z")))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_property owner_label={} owner={} name={} type={} offset={} size={} kind=vector x={} y={} z={}\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    type,
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    (*vector)[0],
                    (*vector)[1],
                    (*vector)[2]);
            }
            else if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                auto* structure = struct_type(struct_prop);
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_property owner_label={} owner={} name={} type={} struct={} offset={} size={} kind=struct_decode\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    type,
                    structure ? structure->GetFullName() : STR("<null>"),
                    property->GetOffset_Internal(),
                    property->GetElementSize());
                int struct_printed = 0;
                dump_camera_struct_values(owner_label,
                                          object,
                                          property->GetName() + STR("."),
                                          structure,
                                          prop_value_ptr(base, property),
                                          0,
                                          struct_printed,
                                          80);
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_struct_summary owner_label={} owner={} name={} values={}\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    struct_printed);
            }
            else
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_property owner_label={} owner={} name={} type={} offset={} size={} kind=unread flags={}\n"),
                    ModTag,
                    owner_label,
                    object->GetFullName(),
                    property->GetName(),
                    type,
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    static_cast<uint64_t>(property->GetPropertyFlags()));
            }
            ++printed;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} game_camera_property_summary owner_label={} object={} matched={} printed={} limit={}\n"),
            ModTag,
            owner_label,
            object->GetFullName(),
            matched,
            printed,
            limit);
    }

    auto dump_camera_contract_functions(const CharType* owner_label, Unreal::UObject* object, int limit = 80) -> void
    {
        if (!object || !object->GetClassPrivate())
        {
            return;
        }
        auto* klass = object->GetClassPrivate();
        int matched = 0;
        int printed = 0;
        for (auto* function : Unreal::TFieldRange<Unreal::UFunction>(klass, Unreal::EFieldIterationFlags::IncludeAll))
        {
            if (!function)
            {
                continue;
            }
            const auto text = lower_copy(function->GetName() + STR(" ") + function->GetFullName());
            if (!camera_contract_function_token_match(text))
            {
                continue;
            }

            ++matched;
            if (printed >= limit)
            {
                continue;
            }

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} game_camera_function owner_label={} owner={} name={} full={} parms_size={} num_parms={}\n"),
                ModTag,
                owner_label,
                object->GetFullName(),
                function->GetName(),
                function->GetFullName(),
                function->GetParmsSize(),
                static_cast<int>(function->GetNumParms()));

            int params_printed = 0;
            for (auto* property : function->ForEachProperty())
            {
                if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm))
                {
                    continue;
                }
                if (params_printed >= 12)
                {
                    break;
                }
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} game_camera_function_param owner_label={} function={} name={} type={} return={} offset={} size={}\n"),
                    ModTag,
                    owner_label,
                    function->GetName(),
                    property->GetName(),
                    prop_type_name(property),
                    property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm) ? 1 : 0,
                    property->GetOffset_Internal(),
                    property->GetElementSize());
                ++params_printed;
            }
            ++printed;
        }
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} game_camera_function_summary owner_label={} object={} matched={} printed={} limit={}\n"),
            ModTag,
            owner_label,
            object->GetFullName(),
            matched,
            printed,
            limit);
    }

    auto dump_owned_camera_contract_components(Unreal::UObject* pawn, int limit = 48) -> void
    {
        if (!pawn)
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} game_camera_owned_component_summary pawn=<null> matched=0 printed=0\n"),
                ModTag);
            return;
        }

        const std::array<const CharType*, 7> class_names{
            STR("CameraComponent"),
            STR("SpringArmComponent"),
            STR("SceneComponent"),
            STR("ActorComponent"),
            STR("SkeletalMeshComponent"),
            STR("CapsuleComponent"),
            STR("DynamicCapsuleHeightControlComponent")};
        const auto pawn_path = lower_copy(object_instance_path(pawn));
        std::vector<Unreal::UObject*> objects{};
        std::unordered_set<StringType> seen{};
        for (const auto* class_name : class_names)
        {
            std::vector<Unreal::UObject*> found{};
            Unreal::UObjectGlobals::FindObjects(4096, class_name, nullptr, found, 0, 0, false);
            for (auto* object : found)
            {
                if (!object)
                {
                    continue;
                }
                if (seen.insert(object->GetFullName()).second)
                {
                    objects.push_back(object);
                }
            }
        }
        int matched = 0;
        int printed = 0;
        for (auto* object : objects)
        {
            if (!object)
            {
                continue;
            }
            auto* owner = call_no_params_return_object(object, STR("GetOwner"));
            const auto object_path = lower_copy(object_instance_path(object));
            const bool path_belongs_to_pawn = !pawn_path.empty() && contains_text(object_path, pawn_path.c_str());
            if (owner != pawn && !path_belongs_to_pawn)
            {
                continue;
            }
            const auto text = lower_copy(object->GetFullName() + STR(" ") +
                                         (object->GetClassPrivate() ? object->GetClassPrivate()->GetFullName() : StringType{}));
            if (!camera_contract_token_match(text))
            {
                continue;
            }

            ++matched;
            if (printed >= limit)
            {
                continue;
            }

            auto location = call_no_params_return_vector(object, STR("K2_GetComponentLocation"));
            auto rotation = call_no_params_return_rotator(object, STR("K2_GetComponentRotation"));
            auto* parent = call_no_params_return_object(object, STR("GetAttachParent"));
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} game_camera_owned_component pawn={} component={} class={} location=({}, {}, {}) rotation=(pitch={}, yaw={}, roll={}) attach_parent={}\n"),
                ModTag,
                pawn->GetFullName(),
                object->GetFullName(),
                object->GetClassPrivate() ? object->GetClassPrivate()->GetFullName() : STR("<null>"),
                location ? location->X() : 0.0,
                location ? location->Y() : 0.0,
                location ? location->Z() : 0.0,
                rotation ? rotation->GetPitch() : 0.0,
                rotation ? rotation->GetYaw() : 0.0,
                rotation ? rotation->GetRoll() : 0.0,
                parent ? parent->GetFullName() : STR("<null>"));
            if (contains_text(text, STR("cameracomponent")) || contains_text(text, STR("springarmcomponent")))
            {
                dump_camera_contract_properties(STR("owned_camera_component"), object, 40);
            }
            ++printed;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} game_camera_owned_component_summary pawn={} matched={} printed={} limit={}\n"),
            ModTag,
            pawn->GetFullName(),
            matched,
            printed,
            limit);
    }

    auto write_object_property_by_name(Unreal::UObject* object, const CharType* property_name, Unreal::UObject* value) -> bool
    {
        if (!object || !object->GetClassPrivate())
        {
            return false;
        }
        auto* property = object->GetClassPrivate()->FindProperty(Unreal::FName(property_name));
        if (!property)
        {
            return false;
        }
        return write_object(property, reinterpret_cast<uint8_t*>(object), value);
    }

    auto write_number_property_by_name(Unreal::UObject* object, const CharType* property_name, double value) -> bool
    {
        if (!object || !object->GetClassPrivate())
        {
            return false;
        }
        auto* property = object->GetClassPrivate()->FindProperty(Unreal::FName(property_name));
        if (!property)
        {
            return false;
        }
        return write_number(property, reinterpret_cast<uint8_t*>(object), value);
    }

    auto write_bool_property_by_name(Unreal::UObject* object, const CharType* property_name, bool value) -> bool
    {
        if (!object || !object->GetClassPrivate())
        {
            return false;
        }
        auto* property = object->GetClassPrivate()->FindProperty(Unreal::FName(property_name));
        if (!property)
        {
            return false;
        }
        return write_bool(property, reinterpret_cast<uint8_t*>(object), value);
    }

    auto call_no_params(Unreal::UObject* object, const CharType* function_name) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        object->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto call_no_params_void(Unreal::UObject* object, const CharType* function_name) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        object->ProcessEvent(function, params.data());
        return true;
    }

    auto call_object_param(Unreal::UObject* object, const CharType* function_name, Unreal::UObject* value) -> bool
    {
        if (!object || !value)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (Unreal::CastField<Unreal::FObjectProperty>(property))
            {
                wrote = write_object(property, params.data(), value) || wrote;
            }
        }
        if (!wrote)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return true;
    }

    auto call_object_return_object(Unreal::UObject* object, const CharType* function_name, Unreal::UObject* value) -> Unreal::UObject*
    {
        if (!object || !value)
        {
            return nullptr;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return nullptr;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            wrote = write_object(property, params.data(), value) || wrote;
        }
        if (!wrote)
        {
            return nullptr;
        }
        object->ProcessEvent(function, params.data());
        return read_return_object(function, params.data());
    }

    auto call_set_material(Unreal::UObject* mesh, int slot, Unreal::UObject* material) -> bool
    {
        if (!mesh || !material)
        {
            return false;
        }
        auto* function = mesh->GetFunctionByNameInChain(STR("SetMaterial"));
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote_index = false;
        bool wrote_material = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (!wrote_index && (contains_text(name, STR("index")) || contains_text(name, STR("element"))) &&
                write_number(property, params.data(), static_cast<double>(slot)))
            {
                wrote_index = true;
                continue;
            }
            if (!wrote_material && write_object(property, params.data(), material))
            {
                wrote_material = true;
            }
        }
        if (!wrote_index || !wrote_material)
        {
            return false;
        }
        mesh->ProcessEvent(function, params.data());
        return true;
    }

    auto create_dynamic_material_instance(Unreal::UObject* mesh,
                                          int slot,
                                          Unreal::UObject* source_material,
                                          const CharType* optional_name) -> Unreal::UObject*
    {
        if (!mesh)
        {
            return nullptr;
        }
        const std::array<const CharType*, 3> functions{STR("CreateDynamicMaterialInstance"),
                                                       STR("CreateAndSetMaterialInstanceDynamicFromMaterial"),
                                                       STR("CreateAndSetMaterialInstanceDynamic")};
        for (const auto* function_name : functions)
        {
            auto* function = mesh->GetFunctionByNameInChain(function_name);
            if (!function)
            {
                continue;
            }
            std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
            bool wrote_index = false;
            for (auto* property : function->ForEachProperty())
            {
                if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                    property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
                {
                    continue;
                }
                const auto name = lower_copy(property->GetName());
                if (!wrote_index && (contains_text(name, STR("index")) || contains_text(name, STR("element"))) &&
                    write_number(property, params.data(), static_cast<double>(slot)))
                {
                    wrote_index = true;
                    continue;
                }
                if (source_material && (contains_text(name, STR("source")) || contains_text(name, STR("parent")) ||
                                        contains_text(name, STR("material"))))
                {
                    write_object(property, params.data(), source_material);
                    continue;
                }
                if (optional_name && contains_text(name, STR("name")))
                {
                    write_name(property, params.data(), optional_name);
                }
            }
            if (!wrote_index)
            {
                continue;
            }
            mesh->ProcessEvent(function, params.data());
            if (auto* dynamic_material = read_return_object(function, params.data()))
            {
                call_set_material(mesh, slot, dynamic_material);
                return dynamic_material;
            }
        }
        return nullptr;
    }

    auto call_name_object_param(Unreal::UObject* object,
                                const CharType* function_name,
                                const CharType* parameter_name,
                                Unreal::UObject* value) -> bool
    {
        if (!object || !function_name || !parameter_name || !value)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote_name = false;
        bool wrote_object = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (!wrote_name && write_name(property, params.data(), parameter_name))
            {
                wrote_name = true;
                continue;
            }
            if (!wrote_object && write_object(property, params.data(), value))
            {
                wrote_object = true;
            }
        }
        if (!wrote_name || !wrote_object)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return true;
    }

    auto call_name_number_param(Unreal::UObject* object,
                                const CharType* function_name,
                                const CharType* parameter_name,
                                double value) -> bool
    {
        if (!object || !function_name || !parameter_name)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote_name = false;
        bool wrote_number = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (!wrote_name && write_name(property, params.data(), parameter_name))
            {
                wrote_name = true;
                continue;
            }
            if (!wrote_number && write_number(property, params.data(), value))
            {
                wrote_number = true;
            }
        }
        if (!wrote_name || !wrote_number)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return true;
    }

    auto call_name_return_number(Unreal::UObject* object,
                                 const CharType* function_name,
                                 const CharType* parameter_name) -> std::optional<double>
    {
        if (!object || !function_name || !parameter_name)
        {
            return std::nullopt;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote_name = false;
        std::vector<Unreal::FProperty*> numeric_params{};
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm))
            {
                continue;
            }
            if (property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (!wrote_name && write_name(property, params.data(), parameter_name))
            {
                wrote_name = true;
                continue;
            }
            if (read_number(property, params.data()))
            {
                numeric_params.push_back(property);
            }
        }
        if (!wrote_name)
        {
            return std::nullopt;
        }

        object->ProcessEvent(function, params.data());
        if (auto value = read_return_number(function, params.data()))
        {
            return value;
        }
        for (auto* property : numeric_params)
        {
            if (auto value = read_number(property, params.data()))
            {
                return value;
            }
        }
        return std::nullopt;
    }

    auto read_material_scalar_parameter(Unreal::UObject* material,
                                        std::initializer_list<const CharType*> names) -> std::optional<double>
    {
        if (!material)
        {
            return std::nullopt;
        }
        for (const auto* name : names)
        {
            if (!name)
            {
                continue;
            }
            for (const auto* function_name : {STR("K2_GetScalarParameterValue"), STR("GetScalarParameterValue")})
            {
                if (auto value = call_name_return_number(material, function_name, name))
                {
                    if (std::isfinite(*value))
                    {
                        return clamp(*value, 0.0, 1.0);
                    }
                }
            }
            if (auto value = read_number_property_by_name(material, name))
            {
                if (std::isfinite(*value))
                {
                    return clamp(*value, 0.0, 1.0);
                }
            }
        }
        return std::nullopt;
    }

    auto call_single_bool_param(Unreal::UObject* object, const CharType* function_name, bool value) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            wrote = write_bool(property, params.data(), value) || wrote;
        }
        if (!wrote)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto call_vector2_param(Unreal::UObject* object, const CharType* function_name, double x, double y) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            wrote = write_vector2(property, params.data(), x, y) || wrote;
        }
        if (!wrote)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto call_anchors_param(Unreal::UObject* object,
                            const CharType* function_name,
                            double min_x,
                            double min_y,
                            double max_x,
                            double max_y) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
            auto* structure = struct_type(struct_prop);
            if (!struct_prop || !structure)
            {
                continue;
            }
            auto* base = prop_value_ptr(params.data(), property);
            auto* minimum = Unreal::CastField<Unreal::FStructProperty>(find_struct_property(structure, STR("Minimum")));
            auto* maximum = Unreal::CastField<Unreal::FStructProperty>(find_struct_property(structure, STR("Maximum")));
            if (minimum && maximum)
            {
                wrote = write_vector2(minimum, base, min_x, min_y) || wrote;
                wrote = write_vector2(maximum, base, max_x, max_y) || wrote;
            }
        }
        if (!wrote)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto call_margin_param(Unreal::UObject* object,
                           const CharType* function_name,
                           double left,
                           double top,
                           double right,
                           double bottom) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
            if (struct_prop)
            {
                wrote = write_struct_numbers(struct_prop,
                                             params.data(),
                                             {{STR("Left"), left},
                                              {STR("Top"), top},
                                              {STR("Right"), right},
                                              {STR("Bottom"), bottom}}) ||
                        wrote;
            }
        }
        if (!wrote)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto configure_scene_capture_actor_filter(Unreal::UObject* capture_component,
                                              Unreal::UObject* pawn,
                                              bool hide_pawn,
                                              bool show_only_pawn,
                                              CaptureGridDiagnostics* diagnostics = nullptr) -> void
    {
        if (!capture_component || !pawn)
        {
            return;
        }
        if (show_only_pawn)
        {
            const auto cleared = call_no_params_void(capture_component, STR("ClearShowOnlyComponents"));
            const auto show_only = call_object_param(capture_component, STR("ShowOnlyActorComponents"), pawn);
            const auto primitive_mode_written =
                write_number_property_by_name(capture_component, STR("PrimitiveRenderMode"), 2.0);
            if (diagnostics)
            {
                diagnostics->clear_show_only_components_called = cleared;
                diagnostics->show_only_actor_components_called = show_only;
                diagnostics->primitive_render_mode_written = primitive_mode_written;
            }
        }
        else if (hide_pawn)
        {
            const auto hidden = call_object_param(capture_component, STR("HideActorComponents"), pawn);
            if (diagnostics)
            {
                diagnostics->hide_actor_components_called = hidden;
            }
        }
    }

    auto capture_background_grid(Unreal::UObject* pawn,
                                 const Unreal::FVector& eye,
                                 const Unreal::FVector& look_at,
                                 int grid_width,
                                 int grid_height,
                                 ProbeState& state,
                                 double fov_degrees = 42.0,
                                 CaptureGridDiagnostics* diagnostics = nullptr) -> std::vector<std::optional<Color>>
    {
        constexpr int capture_width = ProjectionCaptureResolution;
        const auto capture_height = std::max(1,
                                             static_cast<int>(std::round(static_cast<double>(ProjectionCaptureResolution) *
                                                                        static_cast<double>(std::max(1, grid_height)) /
                                                                        static_cast<double>(std::max(1, grid_width)))));
        std::vector<std::optional<Color>> colors(static_cast<size_t>(grid_width * grid_height));

        if (!pawn || state.cancelled)
        {
            return colors;
        }
        auto* world = pawn->GetWorld();
        if (!world)
        {
            return colors;
        }
        auto* scene_capture_class =
            Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, STR("/Script/Engine.SceneCapture2D"));
        if (diagnostics)
        {
            diagnostics->scene_capture_class = scene_capture_class != nullptr;
        }
        if (!scene_capture_class)
        {
            return colors;
        }
        auto* render_target = create_render_target(pawn, capture_width, capture_height);
        if (diagnostics)
        {
            diagnostics->render_target = render_target != nullptr;
        }
        if (!render_target)
        {
            return colors;
        }

        auto rotation = rotator_from_forward(sub(look_at, eye));
        auto* capture_actor = world->SpawnActor(scene_capture_class, &eye, &rotation);
        if (diagnostics)
        {
            diagnostics->capture_actor = capture_actor != nullptr;
        }
        if (!capture_actor)
        {
            return colors;
        }
        auto* capture_component = find_capture_component(capture_actor);
        if (diagnostics)
        {
            diagnostics->capture_component = capture_component != nullptr;
        }
        if (!capture_component)
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            return colors;
        }
        const auto texture_target_written = write_object_property_by_name(capture_component, STR("TextureTarget"), render_target);
        if (diagnostics)
        {
            diagnostics->texture_target_written = texture_target_written;
        }
        if (!texture_target_written)
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            return colors;
        }
        write_number_property_by_name(capture_component, STR("FOVAngle"), clamp(fov_degrees, 10.0, 150.0));
        const auto capture_source_written =
            write_number_property_by_name(capture_component, STR("CaptureSource"), SceneCaptureSourceFinalColorLdr);
        const auto capture_every_frame_written =
            write_bool_property_by_name(capture_component, STR("bCaptureEveryFrame"), false);
        const auto capture_on_movement_written =
            write_bool_property_by_name(capture_component, STR("bCaptureOnMovement"), false);
        const auto persist_rendering_state_written =
            write_bool_property_by_name(capture_component, STR("bAlwaysPersistRenderingState"), true);
        if (diagnostics)
        {
            diagnostics->capture_source_written = capture_source_written;
            diagnostics->capture_every_frame_written = capture_every_frame_written;
            diagnostics->capture_on_movement_written = capture_on_movement_written;
            diagnostics->persist_rendering_state_written = persist_rendering_state_written;
        }
        configure_scene_capture_actor_filter(capture_component, pawn, true, false, diagnostics);

        ActorHiddenGuard hidden_guard{pawn};
        const auto capture_scene_called = call_no_params(capture_component, STR("CaptureScene"));
        if (diagnostics)
        {
            diagnostics->capture_scene_called = capture_scene_called;
        }

        for (int y = 0; y < grid_height; ++y)
        {
            for (int x = 0; x < grid_width; ++x)
            {
                if (state.cancelled)
                {
                    break;
                }
                const auto px = static_cast<int>((static_cast<double>(x) / static_cast<double>(std::max(1, grid_width - 1))) *
                                                 static_cast<double>(capture_width - 1));
                const auto py = static_cast<int>((static_cast<double>(y) / static_cast<double>(std::max(1, grid_height - 1))) *
                                                 static_cast<double>(capture_height - 1));
                auto color = read_render_target_pixel(pawn, render_target, px, py, diagnostics ? &diagnostics->read : nullptr);
                if (diagnostics)
                {
                    color ? ++diagnostics->read_pixels : ++diagnostics->missing_pixels;
                }
                colors[static_cast<size_t>(y * grid_width + x)] = color;
            }
        }

        call_no_params(capture_actor, STR("K2_DestroyActor"));
        return colors;
    }

    auto transform_screen_coord(double value, double scale, double offset, bool flip, double pivot = 0.5) -> double
    {
        pivot = clamp(pivot, 0.0, 1.0);
        auto out = clamp((value - pivot) * scale + pivot + offset, 0.0, 0.999999);
        if (flip)
        {
            out = 0.999999 - out;
        }
        return clamp(out, 0.0, 0.999999);
    }

    auto effective_capture_coord(double capture_value, double fallback_value) -> double
    {
        if (std::isfinite(capture_value) && capture_value >= 0.0 && capture_value <= 1.0)
        {
            return capture_value;
        }
        return fallback_value;
    }

    auto screen_pixel_for_sample(const ScreenHitSample& sample,
                                 int rt_width,
                                 int rt_height,
                                 const ScreenTransform& transform) -> std::pair<int, int>
    {
        const auto base_nx = effective_capture_coord(sample.capture_nx, sample.nx);
        const auto base_ny = effective_capture_coord(sample.capture_ny, sample.ny);
        const auto tx = transform_screen_coord(base_nx,
                                               transform.scale_x,
                                               transform.offset_x,
                                               transform.flip_x,
                                               transform.pivot_x);
        const auto ty = transform_screen_coord(base_ny,
                                               transform.scale_y,
                                               transform.offset_y,
                                               transform.flip_y,
                                               transform.pivot_y);
        const auto px = std::min(rt_width - 1,
                                 std::max(0, static_cast<int>(tx * static_cast<double>(rt_width))));
        const auto py = std::min(rt_height - 1,
                                 std::max(0, static_cast<int>(ty * static_cast<double>(rt_height))));
        return {px, py};
    }

    auto body_mask_clear_color() -> Color
    {
        return Color{1.0, 0.0, 1.0, 1.0, 0.0};
    }

    auto capture_screen_sample_colors(Unreal::UObject* pawn,
                                      const Unreal::FVector& eye,
                                      const Unreal::FVector& look_at,
                                      const std::vector<ScreenHitSample>& samples,
                                      int rt_width,
                                      int rt_height,
                                      bool hide_pawn,
                                      ProbeState& state,
                                      double fov_degrees,
                                      CaptureGridDiagnostics* diagnostics = nullptr,
                                      const ScreenTransform& transform = ScreenTransform{},
                                      const Unreal::FRotator* rotation_override = nullptr,
                                      bool show_only_pawn = false) -> std::vector<std::optional<Color>>
    {
        std::vector<std::optional<Color>> colors(samples.size());
        if (!pawn || samples.empty() || rt_width <= 0 || rt_height <= 0 || state.cancelled)
        {
            return colors;
        }
        auto* world = pawn->GetWorld();
        if (!world)
        {
            return colors;
        }
        auto* scene_capture_class =
            Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, STR("/Script/Engine.SceneCapture2D"));
        if (diagnostics)
        {
            diagnostics->scene_capture_class = scene_capture_class != nullptr;
        }
        if (!scene_capture_class)
        {
            return colors;
        }
        const auto clear_color = show_only_pawn ? body_mask_clear_color() : Color{0.0, 0.0, 0.0, 1.0, 0.0};
        auto* render_target = create_render_target(pawn, rt_width, rt_height, clear_color);
        if (diagnostics)
        {
            diagnostics->render_target = render_target != nullptr;
        }
        if (!render_target)
        {
            return colors;
        }

        auto rotation = rotation_override ? *rotation_override : rotator_from_forward(sub(look_at, eye));
        auto* capture_actor = world->SpawnActor(scene_capture_class, &eye, &rotation);
        if (diagnostics)
        {
            diagnostics->capture_actor = capture_actor != nullptr;
        }
        if (!capture_actor)
        {
            return colors;
        }
        auto* capture_component = find_capture_component(capture_actor);
        if (diagnostics)
        {
            diagnostics->capture_component = capture_component != nullptr;
        }
        if (!capture_component)
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            return colors;
        }

        const auto texture_target_written = write_object_property_by_name(capture_component, STR("TextureTarget"), render_target);
        if (diagnostics)
        {
            diagnostics->texture_target_written = texture_target_written;
        }
        if (!texture_target_written)
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            return colors;
        }
        write_number_property_by_name(capture_component, STR("FOVAngle"), clamp(fov_degrees, 10.0, 150.0));
        const auto capture_source_written =
            write_number_property_by_name(capture_component, STR("CaptureSource"), SceneCaptureSourceFinalColorLdr);
        const auto capture_every_frame_written =
            write_bool_property_by_name(capture_component, STR("bCaptureEveryFrame"), false);
        const auto capture_on_movement_written =
            write_bool_property_by_name(capture_component, STR("bCaptureOnMovement"), false);
        const auto persist_rendering_state_written =
            write_bool_property_by_name(capture_component, STR("bAlwaysPersistRenderingState"), true);
        if (diagnostics)
        {
            diagnostics->capture_source_written = capture_source_written;
            diagnostics->capture_every_frame_written = capture_every_frame_written;
            diagnostics->capture_on_movement_written = capture_on_movement_written;
            diagnostics->persist_rendering_state_written = persist_rendering_state_written;
        }
        configure_scene_capture_actor_filter(capture_component, pawn, hide_pawn, show_only_pawn, diagnostics);

        std::optional<ActorHiddenGuard> hidden_guard{};
        if (hide_pawn)
        {
            hidden_guard.emplace(pawn);
        }
        const auto capture_scene_called = call_no_params(capture_component, STR("CaptureScene"));
        if (diagnostics)
        {
            diagnostics->capture_scene_called = capture_scene_called;
        }

        for (size_t i = 0; i < samples.size(); ++i)
        {
            if (state.cancelled)
            {
                break;
            }
            const auto pixel = screen_pixel_for_sample(samples[i], rt_width, rt_height, transform);
            const auto px = pixel.first;
            const auto py = pixel.second;
            auto color = read_render_target_pixel(pawn, render_target, px, py, diagnostics ? &diagnostics->read : nullptr);
            if (diagnostics)
            {
                color ? ++diagnostics->read_pixels : ++diagnostics->missing_pixels;
            }
            colors[i] = color;
        }

        call_no_params(capture_actor, STR("K2_DestroyActor"));
        return colors;
    }

    auto capture_render_target_image(Unreal::UObject* pawn,
                                     const Unreal::FVector& eye,
                                     const Unreal::FVector& look_at,
                                     int rt_width,
                                     int rt_height,
                                     bool hide_pawn,
                                     ProbeState& state,
                                     double fov_degrees,
                                     const Unreal::FRotator* rotation_override = nullptr,
                                     const std::vector<ScreenHitSample>* calibration_samples = nullptr,
                                     const ScreenTransform* calibration_base_transform = nullptr,
                                     int calibration_limit = 128) -> TimedCaptureImage
    {
        TimedCaptureImage result{};
        result.image.width = rt_width;
        result.image.height = rt_height;
        result.image.expected_pixels = rt_width > 0 && rt_height > 0 ? rt_width * rt_height : 0;
        if (!pawn || rt_width <= 0 || rt_height <= 0 || state.cancelled)
        {
            result.image.failure = STR("capture_image_prereq_unavailable");
            return result;
        }
        auto* world = pawn->GetWorld();
        if (!world)
        {
            result.image.failure = STR("world_unavailable");
            return result;
        }
        auto* scene_capture_class =
            Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, STR("/Script/Engine.SceneCapture2D"));
        result.diagnostics.scene_capture_class = scene_capture_class != nullptr;
        if (!scene_capture_class)
        {
            result.image.failure = STR("scene_capture_class_unavailable");
            return result;
        }
        auto* render_target = create_render_target(pawn, rt_width, rt_height);
        result.diagnostics.render_target = render_target != nullptr;
        if (!render_target)
        {
            result.image.failure = STR("render_target_unavailable");
            return result;
        }

        auto rotation = rotation_override ? *rotation_override : rotator_from_forward(sub(look_at, eye));
        auto* capture_actor = world->SpawnActor(scene_capture_class, &eye, &rotation);
        result.diagnostics.capture_actor = capture_actor != nullptr;
        if (!capture_actor)
        {
            result.image.failure = STR("capture_actor_spawn_failed");
            return result;
        }
        auto* capture_component = find_capture_component(capture_actor);
        result.diagnostics.capture_component = capture_component != nullptr;
        if (!capture_component)
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            result.image.failure = STR("capture_component_unavailable");
            return result;
        }

        result.diagnostics.texture_target_written =
            write_object_property_by_name(capture_component, STR("TextureTarget"), render_target);
        write_number_property_by_name(capture_component, STR("FOVAngle"), clamp(fov_degrees, 10.0, 150.0));
        result.diagnostics.capture_source_written =
            write_number_property_by_name(capture_component, STR("CaptureSource"), SceneCaptureSourceFinalColorLdr);
        result.diagnostics.capture_every_frame_written =
            write_bool_property_by_name(capture_component, STR("bCaptureEveryFrame"), false);
        result.diagnostics.capture_on_movement_written =
            write_bool_property_by_name(capture_component, STR("bCaptureOnMovement"), false);
        result.diagnostics.persist_rendering_state_written =
            write_bool_property_by_name(capture_component, STR("bAlwaysPersistRenderingState"), true);
        if (!result.diagnostics.texture_target_written)
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            result.image.failure = STR("texture_target_write_failed");
            return result;
        }
        configure_scene_capture_actor_filter(capture_component, pawn, hide_pawn, false, &result.diagnostics);

        std::optional<ActorHiddenGuard> hidden_guard{};
        if (hide_pawn)
        {
            hidden_guard.emplace(pawn);
        }
        const auto capture_start = SteadyClock::now();
        result.diagnostics.capture_scene_called = call_no_params(capture_component, STR("CaptureScene"));
        result.capture_ms = elapsed_ms_since(capture_start);

        const auto readback_start = SteadyClock::now();
        result.image = read_render_target_image(pawn, render_target, rt_width, rt_height);
        result.readback_ms = elapsed_ms_since(readback_start);
        if (result.image.ok && calibration_samples && !calibration_samples->empty() && calibration_limit > 0)
        {
            struct Candidate
            {
                ScreenTransform transform{};
                const CharType* label{STR("identity")};
            };
            auto base_transform = calibration_base_transform ? *calibration_base_transform : ScreenTransform{};
            const auto make_candidate = [&](bool flip_x, bool flip_y, const CharType* label) {
                auto transform = base_transform;
                transform.flip_x = transform.flip_x != flip_x;
                transform.flip_y = transform.flip_y != flip_y;
                return Candidate{transform, label};
            };
            const std::array<Candidate, 4> candidates{
                make_candidate(false, false, STR("bulk_identity")),
                make_candidate(true, false, STR("bulk_flip_x")),
                make_candidate(false, true, STR("bulk_flip_y")),
                make_candidate(true, true, STR("bulk_flip_xy"))};

            const auto wanted = std::min<int>(calibration_limit, static_cast<int>(calibration_samples->size()));
            std::vector<std::optional<Color>> pixel_api_colors(static_cast<size_t>(wanted));
            std::vector<ScreenHitSample> selected_samples{};
            selected_samples.reserve(static_cast<size_t>(wanted));
            const auto stride = static_cast<double>(calibration_samples->size()) / static_cast<double>(std::max(1, wanted));
            for (int i = 0; i < wanted; ++i)
            {
                const auto sample_index = std::min<size_t>(
                    calibration_samples->size() - 1,
                    static_cast<size_t>(std::floor((static_cast<double>(i) + 0.5) * stride)));
                const auto& sample = (*calibration_samples)[sample_index];
                selected_samples.push_back(sample);
                const auto pixel = screen_pixel_for_sample(sample, rt_width, rt_height, base_transform);
                pixel_api_colors[static_cast<size_t>(i)] =
                    read_render_target_pixel(pawn, render_target, pixel.first, pixel.second, &result.diagnostics.read);
            }

            double best_median = 1000000.0;
            double runner_up_median = 1000000.0;
            int best_pairs = 0;
            const Candidate* best_candidate = nullptr;
            for (const auto& candidate : candidates)
            {
                std::vector<double> distances{};
                distances.reserve(selected_samples.size());
                for (size_t i = 0; i < selected_samples.size(); ++i)
                {
                    if (!pixel_api_colors[i])
                    {
                        continue;
                    }
                    const auto pixel = screen_pixel_for_sample(selected_samples[i],
                                                               result.image.width,
                                                               result.image.height,
                                                               candidate.transform);
                    const auto image_index = static_cast<size_t>(pixel.second * result.image.width + pixel.first);
                    if (image_index >= result.image.pixels.size())
                    {
                        continue;
                    }
                    distances.push_back(color_distance_rgb(*pixel_api_colors[i], result.image.pixels[image_index]));
                }
                const auto pairs = static_cast<int>(distances.size());
                const auto median = pairs > 0 ? median_value(std::move(distances)) : 1000000.0;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} bulk_readback_calibration candidate={} pairs={} median_rgb_error={} flip=({}, {}) scale=({}, {}) offset=({}, {})\n"),
                    ModTag,
                    candidate.label,
                    pairs,
                    median,
                    candidate.transform.flip_x ? 1 : 0,
                    candidate.transform.flip_y ? 1 : 0,
                    candidate.transform.scale_x,
                    candidate.transform.scale_y,
                    candidate.transform.offset_x,
                    candidate.transform.offset_y);
                if (median < best_median)
                {
                    runner_up_median = best_median;
                    best_median = median;
                    best_pairs = pairs;
                    best_candidate = &candidate;
                }
                else if (median < runner_up_median)
                {
                    runner_up_median = median;
                }
            }

            result.image.bulk_calibration_samples = wanted;
            result.image.bulk_calibration_pairs = best_pairs;
            result.image.bulk_calibration_best_median = best_median < 999999.0 ? best_median : 0.0;
            result.image.bulk_calibration_runner_up_median = runner_up_median < 999999.0 ? runner_up_median : 0.0;
            if (best_candidate)
            {
                result.image.bulk_to_pixel_transform = best_candidate->transform;
                result.image.bulk_calibration_backend = best_candidate->label;
            }
            const auto separated_from_runner = runner_up_median >= 999999.0 ||
                                               best_median <= runner_up_median * 0.90 ||
                                               (runner_up_median - best_median) >= 0.012;
            result.image.bulk_calibration_ok = best_candidate && best_pairs >= std::min(16, wanted / 2) &&
                                               best_median <= 0.18 && separated_from_runner;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} bulk_readback_calibration selected={} ok={} samples={} pairs={} best_median={} runner_up_median={} flip=({}, {}) threshold=0.18 separated={}\n"),
                ModTag,
                result.image.bulk_calibration_backend,
                result.image.bulk_calibration_ok ? 1 : 0,
                result.image.bulk_calibration_samples,
                result.image.bulk_calibration_pairs,
                result.image.bulk_calibration_best_median,
                result.image.bulk_calibration_runner_up_median,
                result.image.bulk_to_pixel_transform.flip_x ? 1 : 0,
                result.image.bulk_to_pixel_transform.flip_y ? 1 : 0,
                separated_from_runner ? 1 : 0);
        }

        call_no_params(capture_actor, STR("K2_DestroyActor"));
        return result;
    }

    auto select_alignment_samples(const std::vector<ScreenHitSample>& samples, int limit) -> std::vector<ScreenHitSample>
    {
        std::vector<ScreenHitSample> subset{};
        if (samples.empty() || limit <= 0)
        {
            return subset;
        }
        const auto wanted = std::min<int>(limit, static_cast<int>(samples.size()));
        subset.reserve(static_cast<size_t>(wanted));
        const auto stride = static_cast<double>(samples.size()) / static_cast<double>(wanted);
        for (int i = 0; i < wanted; ++i)
        {
            const auto index = std::min<size_t>(samples.size() - 1,
                                                static_cast<size_t>(std::floor((static_cast<double>(i) + 0.5) * stride)));
            subset.push_back(samples[index]);
        }
        return subset;
    }

    auto find_best_screen_alignment(const RenderTargetImage& visible_image,
                                    const RenderTargetImage& hidden_image,
                                    const std::vector<ScreenHitSample>& samples) -> AlignmentResult
    {
        AlignmentResult result{};
        result.samples = static_cast<int>(samples.size());
        result.backend = visible_image.ok && hidden_image.ok ? STR("image_grid_search") : STR("identity_no_image");
        if (!visible_image.ok || !hidden_image.ok || samples.empty())
        {
            result.sky_misalign_suspect = false;
            return result;
        }

        const std::array<double, 5> scales{0.96, 0.98, 1.0, 1.02, 1.04};
        const std::array<double, 5> offsets{-0.012, -0.006, 0.0, 0.006, 0.012};
        double best_score = -1.0;
        double best_median = 0.0;
        ScreenTransform best_transform{};

        for (const auto scale_x : scales)
        {
            for (const auto scale_y : scales)
            {
                for (const auto offset_x : offsets)
                {
                    for (const auto offset_y : offsets)
                    {
                        ScreenTransform transform{scale_x, scale_y, offset_x, offset_y};
                        std::vector<double> distances{};
                        distances.reserve(samples.size());
                        for (const auto& sample : samples)
                        {
                            auto visible = sample_image_for_hit(visible_image, sample, transform);
                            auto hidden = sample_image_for_hit(hidden_image, sample, transform);
                            if (!visible || !hidden)
                            {
                                continue;
                            }
                            distances.push_back(color_distance_rgb(*visible, *hidden));
                        }
                        const auto median = median_value(std::move(distances));
                        if (median > best_score)
                        {
                            best_score = median;
                            best_median = median;
                            best_transform = transform;
                        }
                    }
                }
            }
        }

        result.transform = best_transform;
        result.projection_align_score = std::max(0.0, best_score);
        result.body_delta_median = best_median;
        result.sky_misalign_suspect = result.samples >= 32 && result.projection_align_score < 0.012;
        return result;
    }

    auto sample_hidden_background_from_image(const RenderTargetImage& hidden_image,
                                             const std::vector<ScreenHitSample>& samples,
                                             const ScreenTransform& transform) -> std::vector<std::optional<Color>>
    {
        std::vector<std::optional<Color>> colors(samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
        {
            auto color = sample_image_for_hit(hidden_image, samples[i], transform);
            if (color)
            {
                *color = infer_surface_material(*color, samples[i].floor_like);
            }
            colors[i] = color;
        }
        return colors;
    }

    auto probe_scene_capture_pixels(Unreal::UObject* pawn, ProbeState& state) -> bool
    {
        constexpr int capture_width = 96;
        constexpr int capture_height = 96;
        constexpr int grid = 72;

        if (!pawn)
        {
            state.last_failure = STR("player_pawn_unavailable");
            return false;
        }
        auto* world = pawn->GetWorld();
        if (!world)
        {
            state.last_failure = STR("world_unavailable");
            return false;
        }
        auto* scene_capture_class =
            Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, STR("/Script/Engine.SceneCapture2D"));
        if (!scene_capture_class)
        {
            state.last_failure = STR("scene_capture_class_unavailable");
            return false;
        }
        auto* render_target = create_render_target(pawn, capture_width, capture_height);
        if (!render_target)
        {
            state.last_failure = STR("render_target_create_failed");
            return false;
        }

        auto* camera = find_player_camera_manager();
        auto location = call_no_params_return_vector(camera, STR("GetCameraLocation"));
        auto rotation = call_no_params_return_rotator(camera, STR("GetCameraRotation"));
        if (!location)
        {
            location = call_no_params_return_vector(pawn, STR("K2_GetActorLocation"));
        }
        if (!location)
        {
            state.last_failure = STR("capture_location_unavailable");
            return false;
        }
        Unreal::FRotator capture_rotation{};
        if (rotation)
        {
            capture_rotation = *rotation;
        }

        auto* capture_actor = world->SpawnActor(scene_capture_class, &*location, &capture_rotation);
        if (!capture_actor)
        {
            state.last_failure = STR("scene_capture_spawn_failed");
            return false;
        }
        auto* capture_component = find_capture_component(capture_actor);
        if (!capture_component)
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            state.last_failure = STR("scene_capture_component_unavailable");
            return false;
        }
        if (!write_object_property_by_name(capture_component, STR("TextureTarget"), render_target))
        {
            call_no_params(capture_actor, STR("K2_DestroyActor"));
            state.last_failure = STR("scene_capture_texture_target_set_failed");
            return false;
        }

        ActorHiddenGuard hidden_guard{pawn};
        call_no_params(capture_component, STR("CaptureScene"));

        int pixels = 0;
        double luminance_sum = 0.0;
        for (int y = 0; y < grid; ++y)
        {
            for (int x = 0; x < grid; ++x)
            {
                if (state.cancelled)
                {
                    break;
                }
                const auto px = static_cast<int>((static_cast<double>(x) / static_cast<double>(grid - 1)) * (capture_width - 1));
                const auto py = static_cast<int>((static_cast<double>(y) / static_cast<double>(grid - 1)) * (capture_height - 1));
                if (auto color = read_render_target_pixel(pawn, render_target, px, py))
                {
                    ++pixels;
                    luminance_sum += color->r * 0.2126 + color->g * 0.7152 + color->b * 0.0722;
                }
            }
        }

        call_no_params(capture_actor, STR("K2_DestroyActor"));

        state.background_pixels = pixels;
        state.capture_pixels_ready = pixels > 0;
        if (!state.capture_pixels_ready)
        {
            state.last_failure = STR("scene_capture_pixel_read_failed");
            return false;
        }
        const auto avg_luma = luminance_sum / static_cast<double>(std::max(1, pixels));
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} capture probe pixels={} avg_luma={}\n"),
            ModTag,
            pixels,
            avg_luma);
        return true;
    }

    auto classify_background(Unreal::UObject* actor, Unreal::UObject* component, const Unreal::FVector& location) -> Color
    {
        auto text = lower_copy(object_name_or_empty(actor) + STR(" ") + object_name_or_empty(component));
        Color color{};
        if (contains_text(text, STR("grass")) || contains_text(text, STR("leaf")) || contains_text(text, STR("foliage")) ||
            contains_text(text, STR("moss")) || contains_text(text, STR("plant")))
        {
            color = {0.22, 0.33, 0.19, 0.94, 0.0};
        }
        else if (contains_text(text, STR("wood")) || contains_text(text, STR("tree")) || contains_text(text, STR("brown")) ||
                 contains_text(text, STR("plank")))
        {
            color = {0.42, 0.31, 0.20, 0.91, 0.0};
        }
        else if (contains_text(text, STR("metal")) || contains_text(text, STR("steel")) || contains_text(text, STR("iron")) ||
                 contains_text(text, STR("pipe")))
        {
            color = {0.38, 0.40, 0.39, 0.68, 0.35};
        }
        else if (contains_text(text, STR("rock")) || contains_text(text, STR("stone")) || contains_text(text, STR("concrete")) ||
                 contains_text(text, STR("wall")))
        {
            color = {0.46, 0.46, 0.42, 0.93, 0.0};
        }
        else if (contains_text(text, STR("floor")) || contains_text(text, STR("tile")) || contains_text(text, STR("ground")) ||
                 contains_text(text, STR("asphalt")))
        {
            color = {0.30, 0.31, 0.29, 0.94, 0.0};
        }
        else if (contains_text(text, STR("snow")) || contains_text(text, STR("white")))
        {
            color = {0.78, 0.80, 0.76, 0.96, 0.0};
        }
        else
        {
            const auto noise = std::sin(location.X() * 0.013 + location.Y() * 0.019 + location.Z() * 0.007);
            color = {0.36 + noise * 0.06, 0.37 + noise * 0.04, 0.32 + noise * 0.05, 0.93, 0.0};
        }

        const auto shade = 0.88 + 0.12 * std::sin(location.X() * 0.021 + location.Y() * 0.017);
        color.r = clamp(color.r * shade, 0.02, 0.95);
        color.g = clamp(color.g * shade, 0.02, 0.95);
        color.b = clamp(color.b * shade, 0.02, 0.95);
        return color;
    }

    auto is_red_paint_artifact(const Color& color) -> bool
    {
        return color.r > 0.42 && color.g < 0.12 && color.b < 0.12 && color.r > color.g * 3.0 && color.r > color.b * 3.0;
    }

    auto sanitize_background_color(const Color& captured, const Color& material_hint) -> Color
    {
        if (!is_red_paint_artifact(captured))
        {
            return captured;
        }

        Color fallback = material_hint;
        if (is_red_paint_artifact(fallback))
        {
            fallback = Color{0.34, 0.37, 0.31, 0.94, 0.0};
        }
        fallback.r = clamp(fallback.r, 0.05, 0.72);
        fallback.g = clamp(fallback.g, 0.08, 0.76);
        fallback.b = clamp(fallback.b, 0.06, 0.70);
        fallback.roughness = clamp(fallback.roughness, 0.72, 0.98);
        fallback.metallic = 0.0;
        return fallback;
    }

    auto trace_nearest_background_behind_sample(Unreal::UObject* pawn,
                                                const Unreal::FVector& eye,
                                                const ScreenHitSample& sample,
                                                double start_offset = -1.0) -> BackgroundBehindSample
    {
        if (!pawn)
        {
            return {};
        }

        const auto ray_dir = normalize(sub(sample.world_position, eye));
        const auto trace_once = [&](double offset) -> BackgroundBehindSample {
            BackgroundBehindSample out{};
            auto start = add(sample.world_position, mul(ray_dir, std::max(0.5, offset)));
            const auto end = add(sample.world_position, mul(ray_dir, 7200.0));
            for (int step = 0; step < 8; ++step)
            {
                bool saw_self = false;
                for (const auto channel : {0, 1, 2, 3, 4, 5, 6})
                {
                    ++out.channel_attempts;
                    auto hit = execute_line_trace(pawn, start, end, true, channel, true);
                    if (!hit.hit)
                    {
                        continue;
                    }
                    if (trace_hit_belongs_to_pawn(hit, pawn))
                    {
                        saw_self = true;
                        ++out.self_skips;
                        start = add(hit.location, mul(ray_dir, 6.0 + static_cast<double>(step) * 2.0));
                        break;
                    }

                    out.hit = true;
                    out.trace = hit;
                    out.distance = length(sub(hit.location, sample.world_position));
                    out.floor_like = is_floor_like_object(hit.actor, hit.component);
                    Color material_color = classify_background(hit.actor, hit.component, hit.location);
                    bool material_scalar_used = false;
                    if (auto* material = call_number_return_object(hit.component, STR("GetMaterial"), 0.0))
                    {
                        material_color = classify_background(hit.actor, material, hit.location);
                        if (auto roughness = read_material_scalar_parameter(
                                material,
                                {STR("Roughness"),
                                 STR("roughness"),
                                 STR("RoughnessValue"),
                                 STR("RoughnessScalar"),
                                 STR("MaterialRoughness")}))
                        {
                            material_color.roughness = *roughness;
                            material_scalar_used = true;
                        }
                        if (auto metallic = read_material_scalar_parameter(
                                material,
                                {STR("Metallic"),
                                 STR("metallic"),
                                 STR("MetallicValue"),
                                 STR("MetallicScalar"),
                                 STR("MaterialMetallic")}))
                        {
                            material_color.metallic = *metallic;
                            material_scalar_used = true;
                        }
                    }
                    out.color = material_color;
                    if (!material_scalar_used)
                    {
                        out.color = infer_surface_material(out.color, out.floor_like);
                    }
                    else
                    {
                        out.color.roughness = out.floor_like ? clamp(std::max(out.color.roughness, 0.86), 0.86, 0.99)
                                                             : clamp(out.color.roughness, 0.0, 1.0);
                        out.color.metallic = out.floor_like ? clamp(out.color.metallic, 0.0, 0.12)
                                                            : clamp(out.color.metallic, 0.0, 1.0);
                    }
                    return out;
                }
                if (!saw_self)
                {
                    break;
                }
            }
            return out;
        };

        if (start_offset >= 0.0)
        {
            return trace_once(start_offset);
        }

        BackgroundBehindSample best{};
        const std::array<double, 7> offsets{{1.0, 4.0, 8.0, 12.0, 20.0, 28.0, 64.0}};
        for (const auto offset : offsets)
        {
            auto candidate = trace_once(offset);
            best.self_skips += candidate.self_skips;
            best.channel_attempts += candidate.channel_attempts;
            if (!candidate.hit)
            {
                continue;
            }
            if (!best.hit || candidate.distance < best.distance)
            {
                best = candidate;
            }
            if (candidate.floor_like && candidate.distance <= 96.0)
            {
                return candidate;
            }
        }
        return best;
    }

    auto summarize_capture_colors(const std::vector<std::optional<Color>>& colors) -> CaptureColorSummary
    {
        CaptureColorSummary out{};
        Color first{};
        bool have_first = false;
        for (const auto& maybe_color : colors)
        {
            if (!maybe_color)
            {
                continue;
            }
            const auto& color = *maybe_color;
            if (!have_first)
            {
                first = color;
                have_first = true;
            }
            ++out.pixels;
            out.min_r = std::min(out.min_r, color.r);
            out.min_g = std::min(out.min_g, color.g);
            out.min_b = std::min(out.min_b, color.b);
            out.max_r = std::max(out.max_r, color.r);
            out.max_g = std::max(out.max_g, color.g);
            out.max_b = std::max(out.max_b, color.b);
            out.avg_r += color.r;
            out.avg_g += color.g;
            out.avg_b += color.b;
            if (std::abs(color.r - first.r) < 0.004 && std::abs(color.g - first.g) < 0.004 &&
                std::abs(color.b - first.b) < 0.004)
            {
                ++out.near_uniform_samples;
            }
        }
        if (out.pixels > 0)
        {
            const auto inv = 1.0 / static_cast<double>(out.pixels);
            out.avg_r *= inv;
            out.avg_g *= inv;
            out.avg_b *= inv;
            const auto range = std::max({out.max_r - out.min_r, out.max_g - out.min_g, out.max_b - out.min_b});
            out.uniform = range < 0.006 || out.near_uniform_samples >= static_cast<int>(out.pixels * 0.985);
            out.clear_suspect = out.uniform;
        }
        return out;
    }

    auto summarize_capture_quality(const std::vector<std::optional<Color>>& colors) -> CaptureColorQuality
    {
        CaptureColorQuality out{};
        out.summary = summarize_capture_colors(colors);
        if (out.summary.pixels <= 0)
        {
            return out;
        }

        double chroma_sum = 0.0;
        for (const auto& maybe_color : colors)
        {
            if (!maybe_color)
            {
                continue;
            }
            const auto& color = *maybe_color;
            const auto max_channel = std::max({color.r, color.g, color.b});
            const auto min_channel = std::min({color.r, color.g, color.b});
            chroma_sum += max_channel - min_channel;
            const auto lum = color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
            out.luma_min = std::min(out.luma_min, lum);
            out.luma_max = std::max(out.luma_max, lum);
        }

        const auto denom = static_cast<double>(std::max(1, out.summary.pixels));
        out.avg_chroma = chroma_sum / denom;
        out.luma_range = std::max(0.0, out.luma_max - out.luma_min);
        out.rgb_range = std::max({out.summary.max_r - out.summary.min_r,
                                  out.summary.max_g - out.summary.min_g,
                                  out.summary.max_b - out.summary.min_b});
        out.score = out.avg_chroma * 1.8 + out.luma_range * 1.4 + out.rgb_range * 0.8;
        if (out.summary.uniform || out.summary.clear_suspect || out.summary.pixels < 32)
        {
            out.score = -1.0;
        }
        return out;
    }

    auto capture_delta_median(const std::vector<std::optional<Color>>& visible,
                              const std::vector<std::optional<Color>>& hidden,
                              int& pairs) -> double
    {
        std::vector<double> distances{};
        const auto count = std::min(visible.size(), hidden.size());
        distances.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            if (!visible[i] || !hidden[i])
            {
                continue;
            }
            distances.push_back(color_distance_rgb(*visible[i], *hidden[i]));
        }
        pairs = static_cast<int>(distances.size());
        return median_value(std::move(distances));
    }

    auto body_mask_capture_visible(const std::optional<Color>& maybe_color) -> bool
    {
        if (!maybe_color)
        {
            return false;
        }
        const auto& color = *maybe_color;
        if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b))
        {
            return false;
        }
        const auto max_channel = std::max({color.r, color.g, color.b});
        const auto min_channel = std::min({color.r, color.g, color.b});
        const auto lum = color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
        const auto clear_distance = color_distance_rgb(color, body_mask_clear_color());
        return clear_distance > 0.18 &&
               (max_channel > 0.018 || lum > 0.012 || (max_channel - min_channel) > 0.006);
    }

    auto make_body_mask_negative_samples(double min_nx,
                                         double min_ny,
                                         double max_nx,
                                         double max_ny,
                                         int viewport_width,
                                         int viewport_height,
                                         int limit) -> std::vector<ScreenHitSample>
    {
        std::vector<ScreenHitSample> samples{};
        if (viewport_width <= 0 || viewport_height <= 0 || limit <= 0)
        {
            return samples;
        }

        const auto body_min_x = clamp(std::min(min_nx, max_nx), 0.0, 1.0);
        const auto body_max_x = clamp(std::max(min_nx, max_nx), 0.0, 1.0);
        const auto body_min_y = clamp(std::min(min_ny, max_ny), 0.0, 1.0);
        const auto body_max_y = clamp(std::max(min_ny, max_ny), 0.0, 1.0);
        const auto gap = 0.026;
        const int per_band = 12;

        const auto add_sample = [&](double nx, double ny) {
            nx = clamp(nx, 0.001, 0.999);
            ny = clamp(ny, 0.001, 0.999);
            ScreenHitSample sample{};
            sample.nx = nx;
            sample.ny = ny;
            sample.capture_nx = nx;
            sample.capture_ny = ny;
            sample.screen_x = nx * static_cast<double>(std::max(1, viewport_width - 1));
            sample.screen_y = ny * static_cast<double>(std::max(1, viewport_height - 1));
            samples.push_back(sample);
        };
        const auto lerp = [](double a, double b, double t) {
            return a + (b - a) * t;
        };

        if (body_min_y > gap)
        {
            const auto y = clamp(body_min_y - gap, 0.001, 0.999);
            for (int i = 0; i < per_band; ++i)
            {
                add_sample(lerp(body_min_x, body_max_x, (static_cast<double>(i) + 0.5) / per_band), y);
            }
        }
        if (body_max_y < 1.0 - gap)
        {
            const auto y = clamp(body_max_y + gap, 0.001, 0.999);
            for (int i = 0; i < per_band; ++i)
            {
                add_sample(lerp(body_min_x, body_max_x, (static_cast<double>(i) + 0.5) / per_band), y);
            }
        }
        if (body_min_x > gap)
        {
            const auto x = clamp(body_min_x - gap, 0.001, 0.999);
            for (int i = 0; i < per_band; ++i)
            {
                add_sample(x, lerp(body_min_y, body_max_y, (static_cast<double>(i) + 0.5) / per_band));
            }
        }
        if (body_max_x < 1.0 - gap)
        {
            const auto x = clamp(body_max_x + gap, 0.001, 0.999);
            for (int i = 0; i < per_band; ++i)
            {
                add_sample(x, lerp(body_min_y, body_max_y, (static_cast<double>(i) + 0.5) / per_band));
            }
        }

        if (samples.empty())
        {
            add_sample(0.04, 0.04);
            add_sample(0.96, 0.04);
            add_sample(0.04, 0.96);
            add_sample(0.96, 0.96);
        }
        return select_alignment_samples(samples, limit);
    }

    auto find_best_body_mask_pixel_alignment(Unreal::UObject* pawn,
                                             const Unreal::FVector& eye,
                                             const Unreal::FVector& look_at,
                                             const std::vector<ScreenHitSample>& body_samples,
                                             const std::vector<ScreenHitSample>& negative_samples,
                                             int rt_width,
                                             int rt_height,
                                             ProbeState& state,
                                             const std::vector<double>& fov_candidates,
                                             const Unreal::FRotator* rotation_override = nullptr) -> AlignmentResult
    {
        struct Candidate
        {
            ScreenTransform transform{};
            const CharType* label{STR("mask_identity")};
        };

        AlignmentResult result{};
        result.backend = STR("body_mask_no_samples");
        auto positive_samples = select_alignment_samples(body_samples, 64);
        auto negative_subset = select_alignment_samples(negative_samples, 64);
        result.samples = static_cast<int>(positive_samples.size());
        if (!pawn || positive_samples.empty() || negative_subset.empty() || rt_width <= 0 || rt_height <= 0 ||
            fov_candidates.empty() || state.cancelled)
        {
            return result;
        }

        std::vector<ScreenHitSample> combined_samples{};
        combined_samples.reserve(positive_samples.size() + negative_subset.size());
        combined_samples.insert(combined_samples.end(), positive_samples.begin(), positive_samples.end());
        combined_samples.insert(combined_samples.end(), negative_subset.begin(), negative_subset.end());

        std::vector<Candidate> candidates{};
        const auto add_candidate = [&](double scale_x,
                                       double scale_y,
                                       double offset_x,
                                       double offset_y,
                                       const CharType* label) {
            candidates.push_back(Candidate{ScreenTransform{scale_x, scale_y, offset_x, offset_y, false, false}, label});
        };
        add_candidate(1.0, 1.0, 0.0, 0.0, STR("mask_identity"));
        const std::array<double, 3> offsets{-0.045, 0.0, 0.045};
        for (const auto offset_x : offsets)
        {
            for (const auto offset_y : offsets)
            {
                if (std::abs(offset_x) < 0.000001 && std::abs(offset_y) < 0.000001)
                {
                    continue;
                }
                add_candidate(1.0, 1.0, offset_x, offset_y, STR("mask_offset_grid"));
            }
        }
        add_candidate(0.97, 0.97, 0.0, 0.0, STR("mask_scale_097"));
        add_candidate(1.03, 1.03, 0.0, 0.0, STR("mask_scale_103"));

        result.candidate_count = static_cast<int>(candidates.size() * fov_candidates.size());
        double best_score = -1000000.0;
        double runner_up_score = -1000000.0;
        const Candidate* best_candidate = nullptr;
        const Candidate* runner_up_candidate = nullptr;
        double best_fov = 0.0;
        double runner_up_fov = 0.0;
        double best_positive_rate = 0.0;
        double best_negative_rate = 0.0;
        int best_positive_hits = 0;
        int best_negative_hits = 0;

        for (const auto raw_fov : fov_candidates)
        {
            if (state.cancelled)
            {
                break;
            }
            const auto fov = clamp(raw_fov, 10.0, 150.0);
            for (const auto& candidate : candidates)
            {
                if (state.cancelled)
                {
                    break;
                }
                CaptureGridDiagnostics visible_diag{};
                auto colors = capture_screen_sample_colors(pawn,
                                                            eye,
                                                            look_at,
                                                            combined_samples,
                                                            rt_width,
                                                            rt_height,
                                                            false,
                                                            state,
                                                            fov,
                                                            &visible_diag,
                                                            candidate.transform,
                                                            rotation_override,
                                                            true);
                result.visible_reads += visible_diag.read_pixels;

                int positive_hits = 0;
                int negative_hits = 0;
                double positive_clear_distance_sum = 0.0;
                double negative_clear_distance_sum = 0.0;
                int positive_clear_distance_count = 0;
                int negative_clear_distance_count = 0;
                std::vector<std::optional<Color>> positive_colors(positive_samples.size());
                std::vector<std::optional<Color>> negative_colors(negative_subset.size());
                for (size_t i = 0; i < positive_samples.size() && i < colors.size(); ++i)
                {
                    positive_colors[i] = colors[i];
                    if (colors[i])
                    {
                        positive_clear_distance_sum += color_distance_rgb(*colors[i], body_mask_clear_color());
                        ++positive_clear_distance_count;
                    }
                    if (body_mask_capture_visible(colors[i]))
                    {
                        ++positive_hits;
                    }
                }
                for (size_t i = 0; i < negative_subset.size(); ++i)
                {
                    const auto color_index = positive_samples.size() + i;
                    if (color_index < colors.size())
                    {
                        negative_colors[i] = colors[color_index];
                    }
                    if (color_index < colors.size() && colors[color_index])
                    {
                        negative_clear_distance_sum += color_distance_rgb(*colors[color_index], body_mask_clear_color());
                        ++negative_clear_distance_count;
                    }
                    if (color_index < colors.size() && body_mask_capture_visible(colors[color_index]))
                    {
                        ++negative_hits;
                    }
                }
                const auto positive_summary = summarize_capture_colors(positive_colors);
                const auto negative_summary = summarize_capture_colors(negative_colors);

                const auto positive_rate =
                    static_cast<double>(positive_hits) / static_cast<double>(std::max<size_t>(1, positive_samples.size()));
                const auto negative_rate =
                    static_cast<double>(negative_hits) / static_cast<double>(std::max<size_t>(1, negative_subset.size()));
                const auto offset_penalty =
                    (std::abs(candidate.transform.offset_x) + std::abs(candidate.transform.offset_y)) * 0.75;
                const auto scale_penalty =
                    (std::abs(candidate.transform.scale_x - 1.0) + std::abs(candidate.transform.scale_y - 1.0)) * 0.45;
                const auto score = positive_rate - negative_rate * 1.15 - offset_penalty - scale_penalty;

                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} body_mask_alignment candidate={} fov={} score={} positive_rate={} negative_rate={} positive_hits={} negative_hits={} positive_samples={} negative_samples={} read_pixels={} missing={} show_only={} clear_show_only={} primitive_mode={} scale=({}, {}) offset=({}, {}) positive_rgb_avg=({}, {}, {}) negative_rgb_avg=({}, {}, {}) positive_clear_distance_avg={} negative_clear_distance_avg={} sentinel_rgb=(1, 0, 1)\n"),
                    ModTag,
                    candidate.label,
                    fov,
                    score,
                    positive_rate,
                    negative_rate,
                    positive_hits,
                    negative_hits,
                    positive_samples.size(),
                    negative_subset.size(),
                    visible_diag.read_pixels,
                    visible_diag.missing_pixels,
                    visible_diag.show_only_actor_components_called ? 1 : 0,
                    visible_diag.clear_show_only_components_called ? 1 : 0,
                    visible_diag.primitive_render_mode_written ? 1 : 0,
                    candidate.transform.scale_x,
                    candidate.transform.scale_y,
                    candidate.transform.offset_x,
                    candidate.transform.offset_y,
                    positive_summary.avg_r,
                    positive_summary.avg_g,
                    positive_summary.avg_b,
                    negative_summary.avg_r,
                    negative_summary.avg_g,
                    negative_summary.avg_b,
                    positive_clear_distance_count > 0
                        ? positive_clear_distance_sum / static_cast<double>(positive_clear_distance_count)
                        : 0.0,
                    negative_clear_distance_count > 0
                        ? negative_clear_distance_sum / static_cast<double>(negative_clear_distance_count)
                        : 0.0);

                if (visible_diag.read_pixels < std::min<int>(16, static_cast<int>(combined_samples.size() / 2)))
                {
                    continue;
                }
                if (score > best_score)
                {
                    runner_up_score = best_score;
                    runner_up_candidate = best_candidate;
                    runner_up_fov = best_fov;
                    best_score = score;
                    best_candidate = &candidate;
                    best_fov = fov;
                    best_positive_rate = positive_rate;
                    best_negative_rate = negative_rate;
                    best_positive_hits = positive_hits;
                    best_negative_hits = negative_hits;
                }
                else if (score > runner_up_score)
                {
                    runner_up_score = score;
                    runner_up_candidate = &candidate;
                    runner_up_fov = fov;
                }
            }
        }

        if (!best_candidate)
        {
            result.backend = STR("body_mask_no_valid_candidate");
            result.sky_misalign_suspect = true;
            return result;
        }

        result.transform = best_candidate->transform;
        result.backend = best_candidate->label;
        result.selected_fov_degrees = best_fov;
        result.projection_align_score = best_score;
        result.body_delta_median = best_score;
        result.runner_up_score = runner_up_score > -999999.0 ? runner_up_score : 0.0;
        result.runner_up_backend = runner_up_candidate ? runner_up_candidate->label : STR("<none>");
        result.runner_up_fov_degrees = runner_up_fov;
        result.score_ratio = std::abs(result.runner_up_score) > 0.000001
                                 ? result.projection_align_score / result.runner_up_score
                                 : (std::abs(result.projection_align_score) > 0.000001 ? 999.0 : 0.0);
        result.mask_positive_rate = best_positive_rate;
        result.mask_negative_rate = best_negative_rate;
        result.mask_positive_hits = best_positive_hits;
        result.mask_negative_hits = best_negative_hits;
        result.hidden_reads = static_cast<int>(negative_subset.size());
        result.sky_misalign_suspect = result.mask_positive_rate < 0.48 || result.mask_negative_rate > 0.50 ||
                                      result.projection_align_score < 0.18;
        return result;
    }

    auto find_best_pixel_api_alignment(Unreal::UObject* pawn,
                                       const Unreal::FVector& eye,
                                       const Unreal::FVector& look_at,
                                       const std::vector<ScreenHitSample>& samples,
                                       int rt_width,
                                       int rt_height,
                                       ProbeState& state,
                                       double fov_degrees,
                                       const Unreal::FRotator* rotation_override = nullptr) -> AlignmentResult
    {
        struct Candidate
        {
            ScreenTransform transform{};
            const CharType* label{STR("grid")};
        };

        AlignmentResult result{};
        result.backend = STR("pixel_api_no_samples");
        auto alignment_samples = select_alignment_samples(samples, PixelAlignmentSampleLimit);
        result.samples = static_cast<int>(alignment_samples.size());
        if (!pawn || alignment_samples.empty() || rt_width <= 0 || rt_height <= 0 || state.cancelled)
        {
            return result;
        }

        std::vector<Candidate> candidates{};
        const auto add_candidate = [&](double scale_x,
                                       double scale_y,
                                       double offset_x,
                                       double offset_y,
                                       bool flip_x,
                                       bool flip_y,
                                       const CharType* label) {
            candidates.push_back(Candidate{ScreenTransform{scale_x, scale_y, offset_x, offset_y, flip_x, flip_y}, label});
        };
        const std::array<double, 5> offsets{-0.09, -0.045, 0.0, 0.045, 0.09};
        for (const auto offset_x : offsets)
        {
            for (const auto offset_y : offsets)
            {
                add_candidate(1.0, 1.0, offset_x, offset_y, false, false, STR("offset_grid"));
            }
        }
        add_candidate(0.96, 0.96, 0.0, 0.0, false, false, STR("scale_096"));
        add_candidate(1.04, 1.04, 0.0, 0.0, false, false, STR("scale_104"));
        add_candidate(1.0, 1.0, 0.0, 0.0, false, true, STR("flip_y"));
        add_candidate(1.0, 1.0, 0.0, 0.0, true, false, STR("flip_x"));
        add_candidate(1.0, 1.0, 0.0, 0.0, true, true, STR("flip_xy"));

        result.candidate_count = static_cast<int>(candidates.size());
        double best_score = -1.0;
        double runner_up_score = -1.0;
        const Candidate* best_candidate = nullptr;
        const Candidate* runner_up_candidate = nullptr;

        for (const auto& candidate : candidates)
        {
            if (state.cancelled)
            {
                break;
            }
            CaptureGridDiagnostics visible_diag{};
            auto visible_colors = capture_screen_sample_colors(pawn,
                                                               eye,
                                                               look_at,
                                                               alignment_samples,
                                                               rt_width,
                                                               rt_height,
                                                               false,
                                                               state,
                                                               fov_degrees,
                                                               &visible_diag,
                                                               candidate.transform,
                                                               rotation_override,
                                                               true);
            CaptureGridDiagnostics hidden_diag{};
            auto hidden_colors = capture_screen_sample_colors(pawn,
                                                              eye,
                                                              look_at,
                                                              alignment_samples,
                                                              rt_width,
                                                              rt_height,
                                                              true,
                                                              state,
                                                              fov_degrees,
                                                              &hidden_diag,
                                                              candidate.transform,
                                                              rotation_override);
            result.visible_reads += visible_diag.read_pixels;
            result.hidden_reads += hidden_diag.read_pixels;

            int pairs = 0;
            const auto score = capture_delta_median(visible_colors, hidden_colors, pairs);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} alignment_probe candidate={} score={} pairs={} visible_reads={} hidden_reads={} missing=({}, {}) scale=({}, {}) offset=({}, {}) flip=({}, {}) visible_show_only={} visible_clear_show_only={} visible_primitive_mode={} hidden_hide_actor_components={}\n"),
                ModTag,
                candidate.label,
                score,
                pairs,
                visible_diag.read_pixels,
                hidden_diag.read_pixels,
                visible_diag.missing_pixels,
                hidden_diag.missing_pixels,
                candidate.transform.scale_x,
                candidate.transform.scale_y,
                candidate.transform.offset_x,
                candidate.transform.offset_y,
                candidate.transform.flip_x ? 1 : 0,
                candidate.transform.flip_y ? 1 : 0,
                visible_diag.show_only_actor_components_called ? 1 : 0,
                visible_diag.clear_show_only_components_called ? 1 : 0,
                visible_diag.primitive_render_mode_written ? 1 : 0,
                hidden_diag.hide_actor_components_called ? 1 : 0);

            if (pairs < std::min(16, static_cast<int>(alignment_samples.size() / 2)))
            {
                continue;
            }
            if (score > best_score)
            {
                runner_up_score = best_score;
                runner_up_candidate = best_candidate;
                best_score = score;
                best_candidate = &candidate;
            }
            else if (score > runner_up_score)
            {
                runner_up_score = score;
                runner_up_candidate = &candidate;
            }
        }

        if (!best_candidate)
        {
            result.backend = STR("pixel_api_no_valid_candidate");
            result.sky_misalign_suspect = true;
            return result;
        }

        result.transform = best_candidate->transform;
        result.backend = best_candidate->label;
        result.projection_align_score = std::max(0.0, best_score);
        result.body_delta_median = result.projection_align_score;
        result.runner_up_score = std::max(0.0, runner_up_score);
        result.runner_up_backend = runner_up_candidate ? runner_up_candidate->label : STR("<none>");
        result.score_ratio = result.runner_up_score > 0.000001
                                 ? result.projection_align_score / result.runner_up_score
                                 : (result.projection_align_score > 0.000001 ? 999.0 : 0.0);
        result.sky_misalign_suspect = result.samples >= 32 && result.projection_align_score < 0.012;
        return result;
    }

    auto find_best_pixel_api_fov_alignment(Unreal::UObject* pawn,
                                           const Unreal::FVector& eye,
                                           const Unreal::FVector& look_at,
                                           const std::vector<ScreenHitSample>& samples,
                                           int rt_width,
                                           int rt_height,
                                           ProbeState& state,
                                           const std::vector<double>& fov_candidates,
                                           const ScreenTransform& transform,
                                           const Unreal::FRotator* rotation_override = nullptr) -> AlignmentResult
    {
        AlignmentResult result{};
        result.backend = STR("fov_probe_no_samples");
        auto alignment_samples = select_alignment_samples(samples, PixelAlignmentSampleLimit);
        result.samples = static_cast<int>(alignment_samples.size());
        result.candidate_count = static_cast<int>(fov_candidates.size());
        if (!pawn || alignment_samples.empty() || rt_width <= 0 || rt_height <= 0 || fov_candidates.empty() || state.cancelled)
        {
            return result;
        }

        double best_score = -1.0;
        double runner_up_score = -1.0;
        double best_fov = 0.0;
        double runner_up_fov = 0.0;

        for (const auto candidate_fov_raw : fov_candidates)
        {
            if (state.cancelled)
            {
                break;
            }
            const auto candidate_fov = clamp(candidate_fov_raw, 10.0, 150.0);
            CaptureGridDiagnostics visible_diag{};
            auto visible_colors = capture_screen_sample_colors(pawn,
                                                               eye,
                                                               look_at,
                                                               alignment_samples,
                                                               rt_width,
                                                               rt_height,
                                                               false,
                                                               state,
                                                               candidate_fov,
                                                               &visible_diag,
                                                               transform,
                                                               rotation_override,
                                                               true);
            CaptureGridDiagnostics hidden_diag{};
            auto hidden_colors = capture_screen_sample_colors(pawn,
                                                              eye,
                                                              look_at,
                                                              alignment_samples,
                                                              rt_width,
                                                              rt_height,
                                                              true,
                                                              state,
                                                              candidate_fov,
                                                              &hidden_diag,
                                                              transform,
                                                              rotation_override);
            result.visible_reads += visible_diag.read_pixels;
            result.hidden_reads += hidden_diag.read_pixels;

            int pairs = 0;
            const auto score = capture_delta_median(visible_colors, hidden_colors, pairs);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} fov_probe candidate_fov={} score={} pairs={} visible_reads={} hidden_reads={} missing=({}, {}) scale=({}, {}) offset=({}, {}) pivot=({}, {}) visible_show_only={} visible_clear_show_only={} visible_primitive_mode={} hidden_hide_actor_components={}\n"),
                ModTag,
                candidate_fov,
                score,
                pairs,
                visible_diag.read_pixels,
                hidden_diag.read_pixels,
                visible_diag.missing_pixels,
                hidden_diag.missing_pixels,
                transform.scale_x,
                transform.scale_y,
                transform.offset_x,
                transform.offset_y,
                transform.pivot_x,
                transform.pivot_y,
                visible_diag.show_only_actor_components_called ? 1 : 0,
                visible_diag.clear_show_only_components_called ? 1 : 0,
                visible_diag.primitive_render_mode_written ? 1 : 0,
                hidden_diag.hide_actor_components_called ? 1 : 0);

            if (pairs < std::min(16, static_cast<int>(alignment_samples.size() / 2)))
            {
                continue;
            }
            if (score > best_score)
            {
                runner_up_score = best_score;
                runner_up_fov = best_fov;
                best_score = score;
                best_fov = candidate_fov;
            }
            else if (score > runner_up_score)
            {
                runner_up_score = score;
                runner_up_fov = candidate_fov;
            }
        }

        if (best_fov <= 0.0)
        {
            result.backend = STR("fov_probe_no_valid_candidate");
            result.sky_misalign_suspect = true;
            return result;
        }

        result.transform = transform;
        result.backend = STR("fov_probe_visible_hidden_delta");
        result.projection_align_score = std::max(0.0, best_score);
        result.body_delta_median = result.projection_align_score;
        result.runner_up_score = std::max(0.0, runner_up_score);
        result.selected_fov_degrees = best_fov;
        result.runner_up_fov_degrees = runner_up_fov;
        result.runner_up_backend = STR("fov_probe_runner_up");
        result.score_ratio = result.runner_up_score > 0.000001
                                 ? result.projection_align_score / result.runner_up_score
                                 : (result.projection_align_score > 0.000001 ? 999.0 : 0.0);
        result.sky_misalign_suspect = result.samples >= 32 && result.projection_align_score < 0.012;
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} fov_probe selected_fov={} score={} runner_up_fov={} runner_up_score={} score_ratio={} samples={} candidates={} sky_misalign_suspect={}\n"),
            ModTag,
            result.selected_fov_degrees,
            result.projection_align_score,
            result.runner_up_fov_degrees,
            result.runner_up_score,
            result.score_ratio,
            result.samples,
            result.candidate_count,
            result.sky_misalign_suspect ? 1 : 0);
        return result;
    }

    auto read_object_from_struct(const Unreal::UStruct* structure, uint8_t* base, const CharType* name) -> Unreal::UObject*
    {
        auto* property = find_struct_property(structure, name);
        if (!property)
        {
            return nullptr;
        }
        if (auto* object = read_object(property, base))
        {
            return object;
        }
        if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
        {
            auto* nested = prop_value_ptr(base, struct_prop);
            if (auto* object = read_object_from_struct(struct_type(struct_prop), nested, STR("Actor")))
            {
                return object;
            }
            if (auto* object = read_object_from_struct(struct_type(struct_prop), nested, STR("ReferenceObject")))
            {
                return object;
            }
        }
        return nullptr;
    }

    auto extract_hit(Unreal::UFunction* function, uint8_t* params) -> TraceHit
    {
        TraceHit hit{};
        auto* out_hit_prop = Unreal::CastField<Unreal::FStructProperty>(find_function_property(function, STR("OutHit")));
        if (!out_hit_prop)
        {
            return hit;
        }
        auto* hit_base = prop_value_ptr(params, out_hit_prop);
        const auto* hit_struct = struct_type(out_hit_prop);
        if (auto* blocking_prop = find_struct_property(hit_struct, STR("bBlockingHit")))
        {
            hit.hit = read_bool(blocking_prop, hit_base);
        }
        if (!hit.hit)
        {
            hit.hit = read_return_bool(function, params);
        }
        if (auto location = read_vector_from_struct(hit_struct, hit_base, STR("ImpactPoint")))
        {
            hit.location = *location;
        }
        else if (auto fallback_location = read_vector_from_struct(hit_struct, hit_base, STR("Location")))
        {
            hit.location = *fallback_location;
        }
        hit.actor = read_object_from_struct(hit_struct, hit_base, STR("Actor"));
        if (!hit.actor)
        {
            hit.actor = read_object_from_struct(hit_struct, hit_base, STR("HitObjectHandle"));
        }
        hit.component = read_object_from_struct(hit_struct, hit_base, STR("Component"));
        return hit;
    }

    auto get_kismet_system_library() -> Unreal::UObject*
    {
        return Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
    }

    auto get_kismet_rendering_library() -> Unreal::UObject*
    {
        return Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetRenderingLibrary"));
    }

    auto get_gameplay_statics() -> Unreal::UObject*
    {
        return Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
    }

    auto set_actor_hidden(Unreal::UObject* actor, bool hidden) -> bool
    {
        if (!actor)
        {
            return false;
        }
        auto* function = actor->GetFunctionByNameInChain(STR("SetActorHiddenInGame"));
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (property && property->HasAnyPropertyFlags(Unreal::CPF_Parm) &&
                !property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                write_bool(property, params.data(), hidden);
            }
        }
        actor->ProcessEvent(function, params.data());
        return true;
    }

    auto create_render_target(Unreal::UObject* world_context, int width, int height, const Color& clear_color) -> Unreal::UObject*
    {
        auto* function = find_function(STR("/Script/Engine.KismetRenderingLibrary:CreateRenderTarget2D"));
        auto* self = get_kismet_rendering_library();
        if (!function || !self || !world_context)
        {
            return nullptr;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("worldcontext")))
            {
                write_object(property, params.data(), world_context);
            }
            else if (name == STR("width"))
            {
                write_number(property, params.data(), static_cast<double>(width));
            }
            else if (name == STR("height"))
            {
                write_number(property, params.data(), static_cast<double>(height));
            }
            else if (contains_text(name, STR("format")))
            {
                write_number(property, params.data(), static_cast<double>(SceneCaptureRenderTargetFormatRgba16f));
            }
            else if (contains_text(name, STR("clear")))
            {
                if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                {
                    write_linear_color(struct_prop, params.data(), clear_color, 1.0);
                }
            }
            else if (contains_text(name, STR("mip")))
            {
                write_bool(property, params.data(), false);
            }
        }

        self->ProcessEvent(function, params.data());
        return read_return_object(function, params.data());
    }

    auto read_render_target_pixel_with_function(Unreal::UObject* world_context,
                                                Unreal::UObject* render_target,
                                                int x,
                                                int y,
                                                const CharType* function_path,
                                                const CharType* label,
                                                RenderTargetReadDiagnostics* diagnostics = nullptr) -> std::optional<Color>
    {
        if (diagnostics)
        {
            if (label && StringType{label} == STR("raw"))
            {
                ++diagnostics->raw_attempts;
            }
            else
            {
                ++diagnostics->pixel_attempts;
            }
        }
        auto* function = find_function(function_path);
        auto* self = get_kismet_rendering_library();
        if (!function || !self || !world_context || !render_target)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("worldcontext")))
            {
                write_object(property, params.data(), world_context);
            }
            else if (contains_text(name, STR("rendertarget")) || contains_text(name, STR("texture")))
            {
                write_object(property, params.data(), render_target);
            }
            else if (name == STR("x"))
            {
                write_number(property, params.data(), static_cast<double>(x));
            }
            else if (name == STR("y"))
            {
                write_number(property, params.data(), static_cast<double>(y));
            }
        }

        self->ProcessEvent(function, params.data());
        if (auto* return_prop = Unreal::CastField<Unreal::FStructProperty>(function->GetReturnProperty()))
        {
            if (diagnostics && diagnostics->first_function.empty())
            {
                auto* structure = struct_type(return_prop);
                diagnostics->first_function = function->GetFullName();
                diagnostics->first_struct = structure ? structure->GetName() : STR("<null>");
                diagnostics->first_struct_size = return_prop->GetElementSize();
            }
            auto color = read_color_struct(return_prop, params.data());
            if (color && diagnostics)
            {
                if (label && StringType{label} == STR("raw"))
                {
                    ++diagnostics->raw_success;
                }
                else
                {
                    ++diagnostics->pixel_success;
                }
            }
            return color;
        }
        return std::nullopt;
    }

    auto read_render_target_pixel(Unreal::UObject* world_context,
                                  Unreal::UObject* render_target,
                                  int x,
                                  int y,
                                  RenderTargetReadDiagnostics* diagnostics) -> std::optional<Color>
    {
        if (auto raw = read_render_target_pixel_with_function(world_context,
                                                             render_target,
                                                             x,
                                                             y,
                                                             STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetRawPixel"),
                                                             STR("raw"),
                                                             diagnostics))
        {
            return raw;
        }
        return read_render_target_pixel_with_function(world_context,
                                                      render_target,
                                                      x,
                                                      y,
                                                      STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetPixel"),
                                                      STR("pixel"),
                                                      diagnostics);
    }

    auto decode_render_target_color_array(Unreal::FArrayProperty* array_property,
                                          uint8_t* params,
                                          int expected_pixels,
                                          std::vector<Color>& pixels,
                                          StringType& failure) -> bool
    {
        if (!array_property || !params || expected_pixels <= 0)
        {
            failure = STR("array_decode_prereq_unavailable");
            return false;
        }
        const auto stats = read_array_param_stats(array_property, params);
        if (!stats.valid)
        {
            failure = STR("array_stats_invalid");
            return false;
        }
        if (stats.num < expected_pixels)
        {
            failure = STR("array_smaller_than_render_target");
            return false;
        }
        auto* inner = array_property->GetInner();
        auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(inner);
        auto* array = reinterpret_cast<Unreal::FScriptArray*>(prop_value_ptr(params, array_property));
        auto* data = array ? static_cast<uint8_t*>(array->GetData()) : nullptr;
        if (!struct_prop || !data)
        {
            failure = STR("array_inner_not_color_struct");
            return false;
        }

        pixels.clear();
        pixels.reserve(static_cast<size_t>(expected_pixels));
        const auto element_size = std::max(1, inner->GetSize());
        for (int i = 0; i < expected_pixels; ++i)
        {
            auto* element = data + static_cast<size_t>(i) * static_cast<size_t>(element_size);
            auto color = read_color_struct(struct_prop, element);
            if (!color)
            {
                failure = STR("array_color_decode_failed");
                pixels.clear();
                return false;
            }
            color->metallic = 0.0;
            color->roughness = 0.94;
            pixels.push_back(*color);
        }
        return true;
    }

    auto read_render_target_image(Unreal::UObject* world_context,
                                  Unreal::UObject* render_target,
                                  int width,
                                  int height) -> RenderTargetImage
    {
        RenderTargetImage out{};
        out.width = width;
        out.height = height;
        out.expected_pixels = width > 0 && height > 0 ? width * height : 0;
        if (!world_context || !render_target || out.expected_pixels <= 0)
        {
            out.failure = STR("render_target_image_prereq_unavailable");
            return out;
        }
        auto* self = get_kismet_rendering_library();
        if (!self)
        {
            out.failure = STR("kismet_rendering_library_unavailable");
            return out;
        }

        const std::array<const CharType*, 4> candidate_functions{
            STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTarget"),
            STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetRaw"),
            STR("/Script/Engine.RenderingLibrary:ReadRenderTarget"),
            STR("/Script/Engine.RenderingLibrary:ReadRenderTargetRaw")};
        for (const auto* function_path : candidate_functions)
        {
            ++out.bulk_candidates;
            auto* function = find_function(function_path);
            if (!function)
            {
                continue;
            }
            ++out.bulk_available;

            std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
            std::vector<Unreal::FArrayProperty*> array_params{};
            for (auto* property : function->ForEachProperty())
            {
                if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm))
                {
                    continue;
                }
                if (auto* array_prop = Unreal::CastField<Unreal::FArrayProperty>(property))
                {
                    new (prop_value_ptr(params.data(), array_prop)) Unreal::FScriptArray{};
                    array_params.push_back(array_prop);
                    continue;
                }
                if (property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
                {
                    continue;
                }
                const auto name = lower_copy(property->GetName());
                if (contains_text(name, STR("worldcontext")))
                {
                    write_object(property, params.data(), world_context);
                }
                else if (contains_text(name, STR("rendertarget")) || contains_text(name, STR("texture")))
                {
                    write_object(property, params.data(), render_target);
                }
                else if (contains_text(name, STR("normaliz")) || contains_text(name, STR("srgb")))
                {
                    write_bool(property, params.data(), false);
                }
            }

            self->ProcessEvent(function, params.data());
            StringType failure{};
            for (auto* array_prop : array_params)
            {
                std::vector<Color> pixels{};
                if (decode_render_target_color_array(array_prop, params.data(), out.expected_pixels, pixels, failure))
                {
                    const auto stats = read_array_param_stats(array_prop, params.data());
                    out.ok = true;
                    out.backend = STR("bulk_array");
                    out.function_name = function->GetFullName();
                    out.array_param = array_prop->GetName();
                    out.inner_type = stats.inner_type;
                    out.decoded_pixels = static_cast<int>(pixels.size());
                    out.failure.clear();
                    out.pixels = std::move(pixels);
                    for (auto* prop : array_params)
                    {
                        cleanup_array_param(prop, params.data());
                    }
                    return out;
                }
            }
            for (auto* prop : array_params)
            {
                cleanup_array_param(prop, params.data());
            }
            out.failure = failure.empty() ? STR("bulk_array_decode_failed") : failure;
        }

        if (out.bulk_available <= 0)
        {
            out.failure = STR("bulk_read_function_unavailable");
        }
        return out;
    }

    auto sample_image_at(const RenderTargetImage& image, double nx, double ny) -> std::optional<Color>
    {
        if (!image.ok || image.width <= 0 || image.height <= 0 || image.pixels.empty())
        {
            return std::nullopt;
        }
        const auto px = std::min(image.width - 1,
                                 std::max(0, static_cast<int>(clamp(nx, 0.0, 0.999999) *
                                                             static_cast<double>(image.width))));
        const auto py = std::min(image.height - 1,
                                 std::max(0, static_cast<int>(clamp(ny, 0.0, 0.999999) *
                                                             static_cast<double>(image.height))));
        const auto index = static_cast<size_t>(py * image.width + px);
        if (index >= image.pixels.size())
        {
            return std::nullopt;
        }
        return image.pixels[index];
    }

    auto sample_image_for_hit(const RenderTargetImage& image,
                              const ScreenHitSample& sample,
                              const ScreenTransform& transform) -> std::optional<Color>
    {
        const auto base_nx = effective_capture_coord(sample.capture_nx, sample.nx);
        const auto base_ny = effective_capture_coord(sample.capture_ny, sample.ny);
        return sample_image_at(image,
                               transform_screen_coord(base_nx,
                                                      transform.scale_x,
                                                      transform.offset_x,
                                                      transform.flip_x,
                                                      transform.pivot_x),
                               transform_screen_coord(base_ny,
                                                      transform.scale_y,
                                                      transform.offset_y,
                                                      transform.flip_y,
                                                      transform.pivot_y));
    }

    auto try_find_collision_uv(Unreal::UFunction* trace_function, uint8_t* trace_params) -> std::optional<std::pair<double, double>>
    {
        auto* trace_hit_prop = Unreal::CastField<Unreal::FStructProperty>(find_function_property(trace_function, STR("OutHit")));
        auto* function = find_function(STR("/Script/Engine.GameplayStatics:FindCollisionUV"));
        auto* self = get_gameplay_statics();
        if (!trace_hit_prop || !function || !self)
        {
            return std::nullopt;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("hit")))
            {
                const auto copy_size = std::min<int32_t>(property->GetElementSize(), trace_hit_prop->GetElementSize());
                std::memcpy(prop_value_ptr(params.data(), property),
                            prop_value_ptr(trace_params, trace_hit_prop),
                            static_cast<size_t>(copy_size));
            }
            else if (contains_text(name, STR("uvchannel")) || name == STR("channel"))
            {
                write_number(property, params.data(), 0.0);
            }
        }

        self->ProcessEvent(function, params.data());
        if (!read_return_bool(function, params.data()))
        {
            return std::nullopt;
        }
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("uv")))
            {
                if (auto uv = read_vector2(property, params.data()))
                {
                    return uv;
                }
            }
        }
        return std::nullopt;
    }

    auto execute_line_trace(Unreal::UObject* world_context,
                            const Unreal::FVector& start,
                            const Unreal::FVector& end,
                            bool ignore_self,
                            int trace_channel = 0,
                            bool trace_complex = false) -> TraceHit
    {
        TraceHit hit{};
        auto* function = find_function(STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
        auto* self = get_kismet_system_library();
        if (!function || !self || !world_context)
        {
            return hit;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        Color trace_color{0.0, 0.0, 0.0, 1.0, 0.0};
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("worldcontext")))
            {
                write_object(property, params.data(), world_context);
            }
            else if (name == STR("start"))
            {
                write_vector(property, params.data(), start);
            }
            else if (name == STR("end"))
            {
                write_vector(property, params.data(), end);
            }
            else if (name == STR("tracechannel"))
            {
                write_number(property, params.data(), static_cast<double>(trace_channel));
            }
            else if (name == STR("btracecomplex"))
            {
                write_bool(property, params.data(), trace_complex);
            }
            else if (name == STR("drawdebugtype"))
            {
                write_number(property, params.data(), 0.0);
            }
            else if (name == STR("bignoreself"))
            {
                write_bool(property, params.data(), ignore_self);
            }
            else if (name == STR("tracecolor") || name == STR("tracehitcolor"))
            {
                if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                {
                    write_linear_color(struct_prop, params.data(), trace_color, 0.0);
                }
            }
            else if (name == STR("drawtime"))
            {
                write_number(property, params.data(), 0.0);
            }
        }

        self->ProcessEvent(function, params.data());
        hit = extract_hit(function, params.data());
        hit.hit = hit.hit && read_return_bool(function, params.data());
        hit.trace_channel = trace_channel;
        if (hit.hit)
        {
            if (auto uv = try_find_collision_uv(function, params.data()))
            {
                hit.has_uv = true;
                hit.u = uv->first;
                hit.v = uv->second;
            }
        }
        return hit;
    }

    auto execute_body_uv_trace(Unreal::UObject* pawn,
                               const Unreal::FVector& pawn_location,
                               const Unreal::FVector& start,
                               const Unreal::FVector& end,
                               BodyTraceDebugStats* debug_stats = nullptr) -> TraceHit
    {
        if (!pawn)
        {
            return {};
        }

        if (debug_stats)
        {
            ++debug_stats->trace_calls;
        }
        const auto ray_dir = normalize(sub(end, start));
        auto current_start = start;
        for (int step = 0; step < 6; ++step)
        {
            bool any_hit = false;
            std::optional<TraceHit> close_no_uv_hit{};
            for (const auto channel : {0, 1, 2, 3, 4, 5, 6})
            {
                if (debug_stats)
                {
                    ++debug_stats->trace_channel_attempts;
                }
                auto hit = execute_line_trace(pawn, current_start, end, false, channel, true);
                if (!hit.hit)
                {
                    continue;
                }
                any_hit = true;

                const auto belongs_to_pawn = trace_hit_belongs_to_pawn(hit, pawn);
                const auto distance_to_pawn = length(sub(hit.location, pawn_location));
                const auto close_to_pawn = distance_to_pawn <= 280.0;
                const auto floor_like = is_floor_like_object(hit.actor, hit.component);
                if (hit.has_uv && belongs_to_pawn)
                {
                    if (debug_stats)
                    {
                        ++debug_stats->uv_owner;
                    }
                    hit.accepted_by_owner = true;
                    return hit;
                }
                if (hit.has_uv && close_to_pawn && !floor_like)
                {
                    if (debug_stats)
                    {
                        ++debug_stats->uv_spatial;
                    }
                    hit.accepted_by_spatial_fallback = true;
                    return hit;
                }
                if (hit.has_uv && floor_like)
                {
                    if (debug_stats)
                    {
                        ++debug_stats->uv_floor_rejected;
                    }
                    continue;
                }
                if (!belongs_to_pawn && !close_to_pawn)
                {
                    if (debug_stats)
                    {
                        if (hit.has_uv)
                        {
                            ++debug_stats->uv_far_rejected;
                        }
                        else
                        {
                            ++debug_stats->no_uv_far_rejected;
                        }
                    }
                    continue;
                }
                if (!hit.has_uv && close_to_pawn)
                {
                    if (debug_stats)
                    {
                        ++debug_stats->no_uv_close;
                    }
                    if (!close_no_uv_hit)
                    {
                        close_no_uv_hit = hit;
                    }
                }
            }

            if (close_no_uv_hit)
            {
                current_start = add(close_no_uv_hit->location, mul(ray_dir, 8.0 + static_cast<double>(step) * 4.0));
                if (length(sub(end, current_start)) < 16.0)
                {
                    break;
                }
                continue;
            }

            if (!any_hit)
            {
                if (debug_stats)
                {
                    ++debug_stats->trace_no_hit;
                }
                return {};
            }
            return {};
        }
        if (debug_stats)
        {
            ++debug_stats->exhausted;
        }
        return {};
    }

    auto fill_paint_params(Unreal::UFunction* function,
                           std::vector<uint8_t>& params,
                           const Unreal::FVector* world_position,
                           std::optional<std::pair<double, double>> uv,
                           Unreal::UObject* mesh,
                           const Color& color,
                           int channel,
                           double radius,
                           double opacity,
                           double hardness,
                           PaintParamWriteStats* stats = nullptr,
                           int apply_mode = 0) -> bool
    {
        bool wrote_color = false;
        bool wrote_position = false;
        bool wrote_uv = false;
        bool wrote_brush = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("mesh")) || contains_text(name, STR("component")))
            {
                if (mesh)
                {
                    write_object(property, params.data(), mesh);
                }
            }
            else if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                const auto struct_name = lower_copy(struct_type(struct_prop)->GetName());
                if (uv && (contains_text(struct_name, STR("vector2")) || contains_text(name, STR("uv"))))
                {
                    wrote_uv = write_vector2(property, params.data(), uv->first, uv->second) || wrote_uv;
                    if (wrote_uv && stats)
                    {
                        stats->uv_param = property->GetName();
                    }
                }
                else if (world_position && (contains_text(name, STR("world")) || contains_text(name, STR("position")) ||
                                            contains_text(name, STR("location")) || contains_text(struct_name, STR("vector"))))
                {
                    wrote_position = write_vector(property, params.data(), *world_position) || wrote_position;
                }
                else if (contains_text(name, STR("brush")) || contains_text(struct_name, STR("brush")))
                {
                    if (stats)
                    {
                        stats->saw_brush = true;
                        stats->brush_param = property->GetName();
                    }
                    wrote_brush = fill_brush_settings(struct_prop, params.data(), radius, opacity, hardness) || wrote_brush;
                }
                else if (contains_text(name, STR("channeldata")) || contains_text(struct_name, STR("paintchanneldata")))
                {
                    const auto wrote = fill_paint_channel_data_strict(struct_prop, params.data(), color, stats, apply_mode);
                    if (wrote && stats)
                    {
                        stats->color_param = property->GetName();
                    }
                    wrote_color = wrote || wrote_color;
                }
            }
            else if (name == STR("channel") || contains_text(name, STR("paintchannel")))
            {
                if (write_number(property, params.data(), static_cast<double>(channel)) && stats)
                {
                    stats->wrote_channel = true;
                }
            }
            else if (contains_text(name, STR("radius")) || contains_text(name, STR("size")))
            {
                write_number(property, params.data(), radius);
            }
            else if (contains_text(name, STR("opacity")) || contains_text(name, STR("strength")))
            {
                write_number(property, params.data(), opacity);
            }
            else if (contains_text(name, STR("hardness")))
            {
                write_number(property, params.data(), hardness);
            }
        }
        if (stats)
        {
            stats->wrote_color = wrote_color;
            stats->wrote_uv = wrote_uv;
            stats->wrote_world_position = wrote_position;
            stats->wrote_brush = wrote_brush;
            if (stats->failure.empty())
            {
                if (!wrote_color)
                {
                    stats->failure = STR("paint_channel_data_param_unresolved");
                }
                else if (uv && !wrote_uv)
                {
                    stats->failure = STR("paint_uv_param_unresolved");
                }
                else if (world_position && !wrote_position)
                {
                    stats->failure = STR("paint_world_position_param_unresolved");
                }
                else if (stats->saw_brush && !wrote_brush)
                {
                    stats->failure = STR("brush_param_unresolved");
                }
            }
        }
        return wrote_color && ((world_position && wrote_position) || (uv && wrote_uv) || (!world_position && !uv)) &&
               (!stats || !stats->saw_brush || wrote_brush);
    }

    auto paint_at_uv_named(Unreal::UObject* component,
                           const CharType* function_name,
                           int channel,
                           double u,
                           double v,
                           const Color& color,
                           double radius,
                           double opacity,
                           double hardness,
                           PaintParamWriteStats* stats = nullptr,
                           int apply_mode = 0) -> bool
    {
        if (!component)
        {
            return false;
        }
        auto* function = component->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        if (!fill_paint_params(function, params, nullptr, std::make_pair(u, v), nullptr, color, channel, radius, opacity, hardness, stats, apply_mode))
        {
            return false;
        }
        component->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto paint_at_uv(Unreal::UObject* component,
                     double u,
                     double v,
                     const Color& color,
                     double radius,
                     double opacity,
                     double hardness) -> bool
    {
        const std::array<const CharType*, 4> function_order{
            STR("PaintAtUV"),
            STR("PaintAtUVWithBrush"),
            STR("RequestPaintOnServer"),
            STR("SendPaintToServer")};
        for (const auto* function_name : function_order)
        {
            if (paint_at_uv_named(component,
                                  function_name,
                                  PaintChannelAlbedoMetallicRoughness,
                                  u,
                                  v,
                                  color,
                                  radius,
                                  opacity,
                                  hardness))
            {
                return true;
            }
        }
        return false;
    }

    auto paint_probe_call(Unreal::UObject* component,
                          const CharType* function_name,
                          int channel,
                          int apply_mode,
                          double u,
                          double v,
                          const std::optional<Unreal::FVector>& world_position,
                          Unreal::UObject* mesh,
                          const Color& color,
                          double radius,
                          double opacity,
                          double hardness,
                          PaintParamWriteStats* stats) -> bool
    {
        if (!component)
        {
            return false;
        }
        auto* function = component->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            if (stats)
            {
                stats->failure = STR("paint_probe_function_unavailable");
            }
            return false;
        }

        bool wrote_color = false;
        bool wrote_position = false;
        bool wrote_uv = false;
        bool wrote_brush = false;
        bool saw_brush = false;
        bool saw_mesh = false;
        bool wrote_mesh = false;

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("mesh")) || contains_text(name, STR("component")))
            {
                saw_mesh = true;
                if (mesh)
                {
                    wrote_mesh = write_object(property, params.data(), mesh) || wrote_mesh;
                }
            }
            else if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                const auto* structure = struct_type(struct_prop);
                const auto struct_name = structure ? lower_copy(structure->GetName()) : StringType{};
                if (contains_text(name, STR("brush")) || contains_text(struct_name, STR("brush")))
                {
                    saw_brush = true;
                    if (stats)
                    {
                        stats->saw_brush = true;
                        stats->brush_param = property->GetName();
                    }
                    wrote_brush = fill_brush_settings(struct_prop, params.data(), radius, opacity, hardness) || wrote_brush;
                }
                else if (contains_text(name, STR("channeldata")) || contains_text(struct_name, STR("paintchanneldata")))
                {
                    const auto wrote = fill_paint_channel_data_strict(struct_prop, params.data(), color, stats, apply_mode);
                    if (wrote && stats)
                    {
                        stats->color_param = property->GetName();
                    }
                    wrote_color = wrote || wrote_color;
                }
                else if (contains_text(struct_name, STR("vector2")) || contains_text(name, STR("uv")))
                {
                    const auto is_end = contains_text(name, STR("end")) || contains_text(name, STR("to")) || contains_text(name, STR("stop"));
                    const auto uu = is_end ? clamp(u + std::max(radius, 0.012) * 0.75, 0.002, 0.998) : u;
                    const auto vv = is_end ? clamp(v + std::max(radius, 0.012) * 0.25, 0.002, 0.998) : v;
                    wrote_uv = write_vector2(property, params.data(), uu, vv) || wrote_uv;
                    if (wrote_uv && stats && stats->uv_param.empty())
                    {
                        stats->uv_param = property->GetName();
                    }
                }
                else if (world_position &&
                         (contains_text(name, STR("world")) || contains_text(name, STR("position")) ||
                          contains_text(name, STR("location")) || contains_text(struct_name, STR("vector"))))
                {
                    wrote_position = write_vector(property, params.data(), *world_position) || wrote_position;
                }
            }
            else if (name == STR("channel") || contains_text(name, STR("paintchannel")))
            {
                if (write_number(property, params.data(), static_cast<double>(channel)) && stats)
                {
                    stats->wrote_channel = true;
                }
            }
            else if (contains_text(name, STR("radius")) || contains_text(name, STR("size")))
            {
                write_number(property, params.data(), radius);
            }
            else if (contains_text(name, STR("opacity")) || contains_text(name, STR("strength")))
            {
                write_number(property, params.data(), opacity);
            }
            else if (contains_text(name, STR("hardness")))
            {
                write_number(property, params.data(), hardness);
            }
        }

        if (stats)
        {
            stats->wrote_color = wrote_color;
            stats->wrote_uv = wrote_uv;
            stats->wrote_world_position = wrote_position;
            stats->wrote_brush = wrote_brush;
            if (stats->failure.empty())
            {
                if (!wrote_color)
                {
                    stats->failure = STR("paint_channel_data_param_unresolved");
                }
                else if (saw_mesh && !wrote_mesh)
                {
                    stats->failure = STR("paint_mesh_param_unresolved");
                }
                else if (world_position && !wrote_position && contains_text(lower_copy(StringType(function_name)), STR("world")))
                {
                    stats->failure = STR("paint_world_position_param_unresolved");
                }
                else if (!world_position && !wrote_uv)
                {
                    stats->failure = STR("paint_uv_param_unresolved");
                }
                else if (saw_brush && !wrote_brush)
                {
                    stats->failure = STR("brush_param_unresolved");
                }
            }
        }

        const auto wants_world = contains_text(lower_copy(StringType(function_name)), STR("world"));
        const auto params_ok = wrote_color && (!saw_mesh || wrote_mesh) && (!saw_brush || wrote_brush) &&
                               ((wants_world && wrote_position) || (!wants_world && wrote_uv));
        if (!params_ok)
        {
            return false;
        }
        component->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto call_single_number_param(Unreal::UObject* object, const CharType* function_name, double value) -> bool
    {
        if (!object)
        {
            return false;
        }
        auto* function = object->GetFunctionByNameInChain(function_name);
        if (!function)
        {
            return false;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        bool wrote = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            wrote = write_number(property, params.data(), value) || wrote;
        }
        if (!wrote)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return read_return_bool(function, params.data());
    }

    auto configure_screen_brush(Unreal::UObject* component, double radius, double opacity, double hardness) -> bool
    {
        if (!component)
        {
            return false;
        }
        const auto radius_ok = call_single_number_param(component, STR("SetBrushRadius"), radius);
        const auto opacity_ok = call_single_number_param(component, STR("SetBrushOpacity"), opacity);
        const auto hardness_ok = call_single_number_param(component, STR("SetBrushHardness"), hardness);
        call_single_number_param(component, STR("SetBrushFalloff"), 0.0);

        bool property_ok = false;
        if (component->GetClassPrivate())
        {
            if (auto* brush_prop = Unreal::CastField<Unreal::FStructProperty>(
                    component->GetClassPrivate()->FindProperty(Unreal::FName(STR("CurrentBrushSettings")))))
            {
                property_ok = fill_brush_settings(brush_prop, reinterpret_cast<uint8_t*>(component), radius, opacity, hardness);
            }
        }
        return (radius_ok || property_ok) && (opacity_ok || property_ok) && (hardness_ok || property_ok);
    }

    auto paint_at_screen_position(Unreal::UObject* component,
                                  Unreal::UObject* mesh,
                                  Unreal::UObject* controller,
                                  double screen_x,
                                  double screen_y,
                                  const Color& color,
                                  int channel,
                                  bool use_cached_triangles,
                                  PaintParamWriteStats* stats = nullptr,
                                  int apply_mode = 0) -> bool
    {
        if (!component)
        {
            if (stats)
            {
                stats->failure = STR("runtime_paint_component_unavailable");
            }
            return false;
        }
        auto* function = component->GetFunctionByNameInChain(STR("PaintAtScreenPosition"));
        if (!function)
        {
            if (stats)
            {
                stats->failure = STR("paint_at_screen_position_unavailable");
            }
            return false;
        }

        bool saw_mesh = false;
        bool wrote_mesh = false;
        bool saw_controller = false;
        bool wrote_controller = false;
        bool wrote_screen_position = false;
        bool wrote_color = false;
        bool wrote_channel = false;
        bool wrote_cached = false;

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }

            const auto name = lower_copy(property->GetName());
            if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                const auto* structure = struct_type(struct_prop);
                const auto struct_name = structure ? lower_copy(structure->GetName()) : StringType{};
                if (contains_text(name, STR("screen")) || contains_text(struct_name, STR("vector2")))
                {
                    wrote_screen_position = write_vector2(property, params.data(), screen_x, screen_y) || wrote_screen_position;
                    if (wrote_screen_position && stats)
                    {
                        stats->uv_param = property->GetName();
                    }
                }
                else if (contains_text(name, STR("channeldata")) || contains_text(struct_name, STR("paintchanneldata")))
                {
                    const auto wrote = fill_paint_channel_data_strict(struct_prop, params.data(), color, stats, apply_mode);
                    if (wrote && stats)
                    {
                        stats->color_param = property->GetName();
                    }
                    wrote_color = wrote || wrote_color;
                }
            }
            else if (contains_text(name, STR("controller")))
            {
                saw_controller = true;
                if (controller)
                {
                    wrote_controller = write_object(property, params.data(), controller) || wrote_controller;
                }
            }
            else if (contains_text(name, STR("mesh")) || contains_text(name, STR("component")))
            {
                saw_mesh = true;
                if (mesh)
                {
                    wrote_mesh = write_object(property, params.data(), mesh) || wrote_mesh;
                }
            }
            else if (name == STR("channel") || contains_text(name, STR("paintchannel")))
            {
                wrote_channel = write_number(property, params.data(), static_cast<double>(channel)) || wrote_channel;
                if (wrote_channel && stats)
                {
                    stats->wrote_channel = true;
                }
            }
            else if (contains_text(name, STR("cached")) || contains_text(name, STR("triangle")))
            {
                wrote_cached = write_bool(property, params.data(), use_cached_triangles) || wrote_cached;
            }
        }

        if (stats)
        {
            stats->wrote_color = wrote_color;
            stats->wrote_uv = wrote_screen_position;
            stats->wrote_world_position = false;
            stats->wrote_brush = true;
            if (stats->channel_label.empty())
            {
                stats->channel_label = channel_enum_label(function, channel);
            }
            if (stats->failure.empty())
            {
                if (!wrote_color)
                {
                    stats->failure = STR("screen_paint_channel_data_unresolved");
                }
                else if (!wrote_screen_position)
                {
                    stats->failure = STR("screen_position_param_unresolved");
                }
                else if (saw_mesh && !wrote_mesh)
                {
                    stats->failure = STR("screen_mesh_param_unresolved");
                }
                else if (saw_controller && !wrote_controller)
                {
                    stats->failure = STR("screen_controller_param_unresolved");
                }
                else if (!wrote_channel)
                {
                    stats->failure = STR("screen_channel_param_unresolved");
                }
                else if (!wrote_cached)
                {
                    stats->failure = STR("screen_cached_triangles_param_unresolved");
                }
            }
        }

        const auto params_ok = wrote_color && wrote_screen_position && wrote_channel && (!saw_mesh || wrote_mesh) &&
                               (!saw_controller || wrote_controller) && wrote_cached;
        if (!params_ok)
        {
            return false;
        }
        component->ProcessEvent(function, params.data());
        const auto result = decode_screen_space_paint_result(function, params.data());
        if (stats && (!result.success || !result.has_uv) && stats->failure.empty())
        {
            stats->failure = result.failure.empty() ? STR("screen_paint_result_unsuccessful") : result.failure;
        }
        return result.success && result.has_uv;
    }

    auto hit_test_at_screen_position(Unreal::UObject* component,
                                     Unreal::UObject* mesh,
                                     Unreal::UObject* controller,
                                     double screen_x,
                                     double screen_y,
                                     bool use_cached_triangles) -> ScreenSpaceHitResult
    {
        ScreenSpaceHitResult out{};
        if (!component)
        {
            out.failure = STR("runtime_paint_component_unavailable");
            return out;
        }
        auto* function = component->GetFunctionByNameInChain(STR("HitTestAtScreenPosition"));
        if (!function)
        {
            out.failure = STR("hit_test_at_screen_position_unavailable");
            return out;
        }

        bool saw_mesh = false;
        bool wrote_mesh = false;
        bool saw_controller = false;
        bool wrote_controller = false;
        bool wrote_screen_position = false;
        bool wrote_cached = false;

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
            {
                const auto* structure = struct_type(struct_prop);
                const auto struct_name = structure ? lower_copy(structure->GetName()) : StringType{};
                if (contains_text(name, STR("screen")) || contains_text(struct_name, STR("vector2")))
                {
                    wrote_screen_position = write_vector2(property, params.data(), screen_x, screen_y) || wrote_screen_position;
                }
            }
            else if (contains_text(name, STR("controller")))
            {
                saw_controller = true;
                if (controller)
                {
                    wrote_controller = write_object(property, params.data(), controller) || wrote_controller;
                }
            }
            else if (contains_text(name, STR("mesh")) || contains_text(name, STR("component")))
            {
                saw_mesh = true;
                if (mesh)
                {
                    wrote_mesh = write_object(property, params.data(), mesh) || wrote_mesh;
                }
            }
            else if (contains_text(name, STR("cached")) || contains_text(name, STR("triangle")))
            {
                wrote_cached = write_bool(property, params.data(), use_cached_triangles) || wrote_cached;
            }
        }

        const auto params_ok = wrote_screen_position && (!saw_mesh || wrote_mesh) && (!saw_controller || wrote_controller) &&
                               wrote_cached;
        if (!params_ok)
        {
            out.failure = !wrote_screen_position ? STR("screen_hit_position_param_unresolved")
                          : (saw_mesh && !wrote_mesh) ? STR("screen_hit_mesh_param_unresolved")
                          : (saw_controller && !wrote_controller) ? STR("screen_hit_controller_param_unresolved")
                          : STR("screen_hit_cached_triangles_param_unresolved");
            return out;
        }

        component->ProcessEvent(function, params.data());
        out = decode_screen_space_paint_result(function, params.data());
        out.params_ok = true;
        if (!out.success && out.failure.empty())
        {
            out.failure = STR("screen_hit_result_unsuccessful");
        }
        return out;
    }

    auto clear_component(Unreal::UObject* component) -> bool
    {
        if (!component)
        {
            return false;
        }
        bool ok = false;
        if (auto* clear_all = component->GetFunctionByNameInChain(STR("ClearAllChannels")))
        {
            std::vector<uint8_t> params(static_cast<size_t>(clear_all->GetParmsSize()), 0);
            component->ProcessEvent(clear_all, params.data());
            ok = read_return_bool(clear_all, params.data()) || ok;
            if (ok)
            {
                return true;
            }
        }
        if (auto* clear_channel = component->GetFunctionByNameInChain(STR("ClearChannel")))
        {
            for (int channel = 0; channel < 8; ++channel)
            {
                std::vector<uint8_t> params(static_cast<size_t>(clear_channel->GetParmsSize()), 0);
                for (auto* property : clear_channel->ForEachProperty())
                {
                    if (property && property->HasAnyPropertyFlags(Unreal::CPF_Parm) &&
                        !property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
                    {
                        write_number(property, params.data(), static_cast<double>(channel));
                    }
                }
                component->ProcessEvent(clear_channel, params.data());
                ok = read_return_bool(clear_channel, params.data()) || ok;
            }
        }
        return ok;
    }

    auto non_return_param_count(Unreal::UFunction* function) -> int
    {
        int count = 0;
        if (!function)
        {
            return count;
        }
        for (auto* property : function->ForEachProperty())
        {
            if (property && property->HasAnyPropertyFlags(Unreal::CPF_Parm) &&
                !property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                ++count;
            }
        }
        return count;
    }

    auto get_render_target_for_channel(Unreal::UObject* component, int channel) -> Unreal::UObject*
    {
        if (!component)
        {
            return nullptr;
        }
        auto* function = component->GetFunctionByNameInChain(STR("GetRenderTarget"));
        if (!function)
        {
            return nullptr;
        }
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (name == STR("channel") || contains_text(name, STR("paintchannel")))
            {
                write_number(property, params.data(), static_cast<double>(channel));
            }
        }
        component->ProcessEvent(function, params.data());
        return read_return_object(function, params.data());
    }

    auto render_target_dimensions(Unreal::UObject* render_target) -> std::pair<int, int>
    {
        int width = 256;
        int height = 256;
        const std::array<const CharType*, 4> width_fields{STR("SizeX"), STR("Width"), STR("SurfaceWidth"), STR("TargetWidth")};
        const std::array<const CharType*, 4> height_fields{STR("SizeY"), STR("Height"), STR("SurfaceHeight"), STR("TargetHeight")};
        for (const auto* field : width_fields)
        {
            if (auto value = read_int_property_by_name(render_target, field))
            {
                width = *value;
                break;
            }
        }
        for (const auto* field : height_fields)
        {
            if (auto value = read_int_property_by_name(render_target, field))
            {
                height = *value;
                break;
            }
        }
        return {std::max(1, std::min(width, 4096)), std::max(1, std::min(height, 4096))};
    }

    auto hash_render_target_pixels(Unreal::UObject* world_context,
                                   Unreal::UObject* render_target,
                                   uint64_t hash,
                                   int sample_grid = 18) -> uint64_t
    {
        if (!world_context || !render_target)
        {
            return hash;
        }
        const auto [width, height] = render_target_dimensions(render_target);
        hash = fnv1a_update_string(hash, render_target->GetFullName());
        hash = fnv1a_update(hash, &width, sizeof(width));
        hash = fnv1a_update(hash, &height, sizeof(height));
        sample_grid = std::max(3, std::min(sample_grid, 32));
        for (int y = 0; y < sample_grid; ++y)
        {
            for (int x = 0; x < sample_grid; ++x)
            {
                const auto px = static_cast<int>((static_cast<double>(x) / static_cast<double>(sample_grid - 1)) *
                                                 static_cast<double>(width - 1));
                const auto py = static_cast<int>((static_cast<double>(y) / static_cast<double>(sample_grid - 1)) *
                                                 static_cast<double>(height - 1));
                if (auto pixel = read_render_target_pixel(world_context, render_target, px, py))
                {
                    float packed[5]{
                        static_cast<float>(pixel->r),
                        static_cast<float>(pixel->g),
                        static_cast<float>(pixel->b),
                        static_cast<float>(pixel->roughness),
                        static_cast<float>(pixel->metallic)};
                    hash = fnv1a_update(hash, packed, sizeof(packed));
                }
                else
                {
                    const int miss = -1;
                    hash = fnv1a_update(hash, &miss, sizeof(miss));
                }
            }
        }
        return hash;
    }

    auto push_unique_object(std::vector<Unreal::UObject*>& objects, Unreal::UObject* object) -> void
    {
        if (!object)
        {
            return;
        }
        const auto full_name = object->GetFullName();
        for (auto* existing : objects)
        {
            if (existing && existing->GetFullName() == full_name)
            {
                return;
            }
        }
        objects.push_back(object);
    }

    auto collect_component_render_targets(Unreal::UObject* component) -> std::vector<Unreal::UObject*>
    {
        std::vector<Unreal::UObject*> targets{};
        if (!component || !component->GetClassPrivate())
        {
            return targets;
        }
        auto* klass = component->GetClassPrivate();
        for (auto* property : klass->ForEachProperty())
        {
            if (!property)
            {
                continue;
            }
            const auto property_text = lower_copy(property->GetName() + STR(" ") + prop_type_name(property));
            if (!render_target_token_match(property_text))
            {
                continue;
            }
            if (auto* object = read_object(property, reinterpret_cast<uint8_t*>(component)))
            {
                push_unique_object(targets, object);
            }
        }
        for (int channel = 0; channel < 8; ++channel)
        {
            push_unique_object(targets, get_render_target_for_channel(component, channel));
        }
        return targets;
    }

    struct RenderTargetProbeStats
    {
        Unreal::UObject* target{nullptr};
        StringType target_name{STR("<null>")};
        int width{0};
        int height{0};
        int samples{0};
        int valid{0};
        double min_r{1.0};
        double min_g{1.0};
        double min_b{1.0};
        double max_r{0.0};
        double max_g{0.0};
        double max_b{0.0};
        double avg_r{0.0};
        double avg_g{0.0};
        double avg_b{0.0};
        double uniformity{0.0};
        std::vector<Color> pixels{};
    };

    auto select_probe_render_target(Unreal::UObject* component, int channel) -> Unreal::UObject*
    {
        if (auto* channel_target = get_render_target_for_channel(component, channel))
        {
            return channel_target;
        }
        auto targets = collect_component_render_targets(component);
        return targets.empty() ? nullptr : targets.front();
    }

    auto read_render_target_probe_stats(Unreal::UObject* component,
                                        Unreal::UObject* render_target,
                                        int sample_grid = 12) -> RenderTargetProbeStats
    {
        RenderTargetProbeStats stats{};
        stats.target = render_target;
        stats.target_name = object_name_or_empty(render_target);
        if (!component || !render_target)
        {
            return stats;
        }
        const auto [width, height] = render_target_dimensions(render_target);
        stats.width = width;
        stats.height = height;
        sample_grid = std::max(3, std::min(sample_grid, 24));
        stats.samples = sample_grid * sample_grid;
        stats.pixels.reserve(static_cast<size_t>(stats.samples));

        double sum_r = 0.0;
        double sum_g = 0.0;
        double sum_b = 0.0;
        for (int y = 0; y < sample_grid; ++y)
        {
            for (int x = 0; x < sample_grid; ++x)
            {
                const auto px = static_cast<int>((static_cast<double>(x) / static_cast<double>(sample_grid - 1)) *
                                                 static_cast<double>(width - 1));
                const auto py = static_cast<int>((static_cast<double>(y) / static_cast<double>(sample_grid - 1)) *
                                                 static_cast<double>(height - 1));
                auto pixel = read_render_target_pixel(component, render_target, px, py);
                if (!pixel)
                {
                    stats.pixels.push_back(Color{});
                    continue;
                }
                stats.pixels.push_back(*pixel);
                ++stats.valid;
                stats.min_r = std::min(stats.min_r, pixel->r);
                stats.min_g = std::min(stats.min_g, pixel->g);
                stats.min_b = std::min(stats.min_b, pixel->b);
                stats.max_r = std::max(stats.max_r, pixel->r);
                stats.max_g = std::max(stats.max_g, pixel->g);
                stats.max_b = std::max(stats.max_b, pixel->b);
                sum_r += pixel->r;
                sum_g += pixel->g;
                sum_b += pixel->b;
            }
        }
        if (stats.valid <= 0)
        {
            return stats;
        }
        const auto inv = 1.0 / static_cast<double>(stats.valid);
        stats.avg_r = sum_r * inv;
        stats.avg_g = sum_g * inv;
        stats.avg_b = sum_b * inv;

        double variance = 0.0;
        for (const auto& pixel : stats.pixels)
        {
            const auto dr = pixel.r - stats.avg_r;
            const auto dg = pixel.g - stats.avg_g;
            const auto db = pixel.b - stats.avg_b;
            variance += dr * dr + dg * dg + db * db;
        }
        variance /= static_cast<double>(std::max(1, stats.valid));
        stats.uniformity = 1.0 - clamp(std::sqrt(variance) * 4.0, 0.0, 1.0);
        return stats;
    }

    auto count_changed_probe_pixels(const RenderTargetProbeStats& before, const RenderTargetProbeStats& after) -> int
    {
        const auto count = std::min(before.pixels.size(), after.pixels.size());
        int changed = 0;
        for (size_t i = 0; i < count; ++i)
        {
            const auto dr = before.pixels[i].r - after.pixels[i].r;
            const auto dg = before.pixels[i].g - after.pixels[i].g;
            const auto db = before.pixels[i].b - after.pixels[i].b;
            if ((dr * dr + dg * dg + db * db) > 0.00035)
            {
                ++changed;
            }
        }
        return changed;
    }

    auto hash_component_paint_state(Unreal::UObject* component) -> uint64_t
    {
        auto hash = 1469598103934665603ULL;
        if (!component || !component->GetClassPrivate())
        {
            return hash;
        }

        auto* klass = component->GetClassPrivate();
        hash = fnv1a_update_string(hash, component->GetFullName());
        for (auto* property : klass->ForEachProperty())
        {
            if (!property)
            {
                continue;
            }
            const auto property_text = lower_copy(property->GetName() + STR(" ") + prop_type_name(property));
            if (!pipeline_token_match(property_text))
            {
                continue;
            }

            hash = fnv1a_update_string(hash, property->GetName());
            hash = fnv1a_update_string(hash, prop_type_name(property));
            const auto offset = property->GetOffset_Internal();
            const auto size = property->GetElementSize();
            hash = fnv1a_update(hash, &offset, sizeof(offset));
            hash = fnv1a_update(hash, &size, sizeof(size));
            if (size > 0 && size <= 256)
            {
                hash = fnv1a_update(hash, prop_value_ptr(reinterpret_cast<uint8_t*>(component), property), static_cast<size_t>(size));
            }

            if (auto* object = read_object(property, reinterpret_cast<uint8_t*>(component)))
            {
                hash = fnv1a_update_string(hash, object->GetFullName());
            }
        }
        auto render_targets = collect_component_render_targets(component);
        const auto target_count = static_cast<int>(render_targets.size());
        hash = fnv1a_update(hash, &target_count, sizeof(target_count));
        for (auto* render_target : render_targets)
        {
            hash = hash_render_target_pixels(component, render_target, hash, 18);
        }
        return hash;
    }

    auto call_commit_sync_candidates(Unreal::UObject* component, bool verbose, bool allow_texture_sync_requests = false) -> int
    {
        if (!component || !component->GetClassPrivate())
        {
            return 0;
        }
        int calls = 0;
        for (auto* function : Unreal::TFieldRange<Unreal::UFunction>(component->GetClassPrivate(), Unreal::EFieldIterationFlags::IncludeAll))
        {
            if (!function)
            {
                continue;
            }
            const auto name = lower_copy(function->GetName());
            const auto text = lower_copy(function->GetName() + STR(" ") + function->GetFullName());
            if (!commit_sync_token_match(text))
            {
                continue;
            }
            const auto is_flush_candidate = contains_any_text(name,
                                                              {STR("flushrecordedstrokestoserver"),
                                                               STR("sendstrokebatchtoserver")});
            const auto is_texture_request = contains_any_text(name,
                                                              {STR("requestfulltexturesync"),
                                                               STR("serverrequesttexturesync")});
            if (!is_flush_candidate && !(allow_texture_sync_requests && is_texture_request))
            {
                if (verbose && is_texture_request)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} commit_sync skipped texture request name={} full={}\n"),
                        ModTag,
                        function->GetName(),
                        function->GetFullName());
                }
                continue;
            }
            if (contains_any_text(text, {STR("clear"), STR("reset"), STR("remove"), STR("delete"), STR("destroy"), STR("load")}))
            {
                continue;
            }
            if (non_return_param_count(function) != 0)
            {
                continue;
            }
            std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
            component->ProcessEvent(function, params.data());
            ++calls;
            if (verbose && calls <= 12)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} commit_sync called name={} full={}\n"),
                    ModTag,
                    function->GetName(),
                    function->GetFullName());
            }
        }
        return calls;
    }

    auto channel_enum_label(Unreal::UFunction* function, int channel) -> StringType
    {
        if (!function)
        {
            return STR("<unknown>");
        }
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (!(name == STR("channel") || contains_text(name, STR("paintchannel"))))
            {
                continue;
            }
            Unreal::UEnum* enum_obj = nullptr;
            if (auto* enum_prop = Unreal::CastField<Unreal::FEnumProperty>(property))
            {
                enum_obj = Unreal::ToRawPtr(enum_prop->GetEnum());
            }
            else if (auto* byte_prop = Unreal::CastField<Unreal::FByteProperty>(property))
            {
                enum_obj = Unreal::ToRawPtr(byte_prop->GetEnum());
            }
            if (!enum_obj)
            {
                return STR("<no-enum>");
            }
            auto label = enum_obj->GetNameByValue(channel).ToString();
            return label.empty() ? STR("<unnamed>") : label;
        }
        return STR("<no-channel-param>");
    }

    auto probe_verified_paint_backend(Unreal::UObject* component, ProbeState& state, bool verbose) -> std::optional<PaintBackend>
    {
        state.verified_paint_channel = PaintChannelUnknown;
        state.verified_paint_function.clear();
        state.verified_visible_backend = false;
        if (!component)
        {
            state.last_failure = STR("runtime_paint_component_unavailable");
            return std::nullopt;
        }

        const std::array<const CharType*, 4> function_order{
            STR("PaintAtUV"),
            STR("PaintAtUVWithBrush"),
            STR("RequestPaintOnServer"),
            STR("SendPaintToServer")};
        // Channels 0..2 behave like individual RGB/scalar channels in this game path.
        // They can make the whole body red/green/blue, so play only probes composite/material channels.
        const std::array<int, 5> channel_order{5, 4, 6, 7, 3};
        const std::array<std::pair<double, double>, 8> probe_uvs{
            std::make_pair(0.125, 0.125),
            std::make_pair(0.250, 0.250),
            std::make_pair(0.375, 0.375),
            std::make_pair(0.500, 0.500),
            std::make_pair(0.625, 0.625),
            std::make_pair(0.750, 0.750),
            std::make_pair(0.875, 0.875),
            std::make_pair(0.500, 0.250)};

        clear_component(component);
        state.commit_calls += call_commit_sync_candidates(component, false);
        int probe_index = 0;
        for (const auto* function_name : function_order)
        {
            if (!component->GetFunctionByNameInChain(function_name))
            {
                continue;
            }
            auto* function = component->GetFunctionByNameInChain(function_name);
            for (const auto channel : channel_order)
            {
                const auto label = channel_enum_label(function, channel);
                const auto label_ok = contains_text(lower_copy(label), STR("albedometallicroughness"));
                if (!label_ok)
                {
                    if (verbose)
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("{} channel_probe skipped function={} channel={} label={} reason=not_albedo_metallic_roughness\n"),
                            ModTag,
                            function_name,
                            channel,
                            label);
                    }
                    ++probe_index;
                    continue;
                }
                const auto before = hash_component_paint_state(component);
                const auto uv = probe_uvs[static_cast<size_t>(probe_index % static_cast<int>(probe_uvs.size()))];
                const auto shade = 0.18 + 0.035 * static_cast<double>(probe_index % 8);
                const Color neutral{shade, shade, shade, 0.94, 0.0};
                PaintParamWriteStats param_stats{};
                param_stats.channel_label = label;
                const auto ok = paint_at_uv_named(component,
                                                  function_name,
                                                  channel,
                                                  uv.first,
                                                  uv.second,
                                                  neutral,
                                                  0.006,
                                                  0.60,
                                                  0.95,
                                                  &param_stats);
                state.commit_calls += call_commit_sync_candidates(component, false);
                const auto after = hash_component_paint_state(component);
                const auto changed = before != after;
                const auto usable = ok && param_stats.wrote_color && param_stats.wrote_uv && param_stats.wrote_channel &&
                                    param_stats.failure.empty();
                if (verbose)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} channel_probe function={} channel={} label={} ok={} usable={} hash_observed={} hash_before={} hash_after={} color_param={} color_struct={} uv_param={} brush_param={} strict_channel_data={} layout_fallback={} param_failure={}\n"),
                        ModTag,
                        function_name,
                        channel,
                        label,
                        ok ? 1 : 0,
                        usable ? 1 : 0,
                        changed ? 1 : 0,
                        before,
                        after,
                        param_stats.color_param.empty() ? STR("<none>") : param_stats.color_param,
                        param_stats.color_struct.empty() ? STR("<none>") : param_stats.color_struct,
                        param_stats.uv_param.empty() ? STR("<none>") : param_stats.uv_param,
                        param_stats.brush_param.empty() ? STR("<none>") : param_stats.brush_param,
                        param_stats.strict_channel_data ? 1 : 0,
                        param_stats.used_channel_data_layout_fallback ? 1 : 0,
                        param_stats.failure.empty() ? STR("<none>") : param_stats.failure);
                }
                ++probe_index;
                if (usable)
                {
                    PaintBackend backend{};
                    backend.function_name = function_name;
                    backend.channel = channel;
                    backend.radius = contains_text(lower_copy(StringType(function_name)), STR("brush")) ? 0.060 : 0.035;
                    backend.opacity = 0.72;
                    backend.hardness = 0.96;
                    state.verified_paint_channel = channel;
                    state.verified_paint_function = function_name;
                    state.verified_visible_backend = changed;
                    state.last_failure = changed ? STR("paint_backend_verified_hash_observed")
                                                 : STR("paint_backend_api_verified_hash_untracked");
                    return backend;
                }
            }
        }

        state.last_failure = STR("paint_channel_backend_unverified");
        return std::nullopt;
    }

    auto make_visibility_samples(Unreal::UObject* pawn, ProbeState& state) -> std::vector<Sample>
    {
        std::vector<Sample> samples{};
        auto pawn_location = call_no_params_return_vector(pawn, STR("K2_GetActorLocation"));
        if (!pawn_location)
        {
            pawn_location = vec(0.0, 0.0, 0.0);
        }

        auto* camera = find_player_camera_manager();
        auto camera_location = call_no_params_return_vector(camera, STR("GetCameraLocation"));
        auto camera_rotation = call_no_params_return_rotator(camera, STR("GetCameraRotation"));

        Unreal::FVector primary_dir{};
        if (camera_location)
        {
            primary_dir = normalize(sub(*camera_location, *pawn_location));
        }
        else if (camera_rotation)
        {
            primary_dir = mul(rotator_forward(*camera_rotation), -1.0);
        }
        else if (auto actor_forward = call_no_params_return_vector(pawn, STR("GetActorForwardVector")))
        {
            primary_dir = mul(*actor_forward, -1.0);
        }
        else
        {
            primary_dir = vec(-1.0, 0.0, 0.0);
        }

        const std::array<double, 7> yaw_offsets{-55.0, -32.0, -16.0, 0.0, 16.0, 32.0, 55.0};
        const std::array<double, 5> pitch_offsets{-22.0, -10.0, 0.0, 14.0, 30.0};
        const std::array<double, 2> distances{470.0, 820.0};
        constexpr int ray_grid_width = 31;
        constexpr int ray_grid_height = 37;
        constexpr double capture_fov_degrees = 42.0;
        const auto fov_tan = std::tan((capture_fov_degrees * Pi / 180.0) * 0.5);

        state.views = 0;
        state.visible_samples = 0;
        state.body_trace_hits = 0;
        state.background_trace_hits = 0;
        state.background_pixels = 0;
        state.uv_hits = 0;

        constexpr int uv_bin_resolution = 80;
        std::vector<uint8_t> occupied_uv_bins(static_cast<size_t>(uv_bin_resolution * uv_bin_resolution), 0);

        for (const auto yaw : yaw_offsets)
        {
            for (const auto pitch : pitch_offsets)
            {
                for (const auto distance : distances)
                {
                    ++state.views;
                    auto view_dir = rotate_yaw_pitch(primary_dir, yaw, pitch);
                    const auto eye = add(add(*pawn_location, mul(view_dir, distance)), vec(0.0, 0.0, 72.0 + pitch * 2.0));
                    auto right = normalize(cross(view_dir, vec(0.0, 0.0, 1.0)));
                    if (length(right) < 0.01)
                    {
                        right = vec(0.0, 1.0, 0.0);
                    }

                    const auto center_weight = 1.0 / (1.0 + std::abs(yaw) * 0.018 + std::abs(pitch) * 0.025 + (distance > 600.0 ? 0.18 : 0.0));
                    const auto look_at = add(*pawn_location, vec(0.0, 0.0, 16.0));
                    const auto camera_forward = normalize(sub(look_at, eye));
                    auto capture_colors = capture_background_grid(pawn,
                                                                  eye,
                                                                  look_at,
                                                                  ray_grid_width,
                                                                  ray_grid_height,
                                                                  state);
                    auto local_right = normalize(cross(camera_forward, vec(0.0, 0.0, 1.0)));
                    if (length(local_right) < 0.01)
                    {
                        local_right = vec(0.0, 1.0, 0.0);
                    }
                    const auto local_up = normalize(cross(local_right, camera_forward));
                    const auto aspect = static_cast<double>(ray_grid_width) / static_cast<double>(ray_grid_height);
                    for (int hx_index = 0; hx_index < ray_grid_width; ++hx_index)
                    {
                        const auto sx = (static_cast<double>(hx_index) / static_cast<double>(ray_grid_width - 1)) * 2.0 - 1.0;
                        for (int vz_index = 0; vz_index < ray_grid_height; ++vz_index)
                        {
                            const auto sy = 1.0 - (static_cast<double>(vz_index) / static_cast<double>(ray_grid_height - 1)) * 2.0;
                            const auto ray_dir_for_pixel = normalize(add(add(camera_forward, mul(local_right, sx * fov_tan * aspect)),
                                                                         mul(local_up, sy * fov_tan)));
                            const auto target = add(eye, mul(ray_dir_for_pixel, 5200.0));
                            auto body_hit = execute_body_uv_trace(pawn, *pawn_location, eye, target);
                            if (!body_hit.hit)
                            {
                                continue;
                            }

                            ++state.body_trace_hits;
                            ++state.uv_hits;

                            const auto u = clamp(body_hit.u, 0.0, 0.999999);
                            const auto v = clamp(body_hit.v, 0.0, 0.999999);
                            const auto uv_x = static_cast<int>(u * static_cast<double>(uv_bin_resolution));
                            const auto uv_y = static_cast<int>(v * static_cast<double>(uv_bin_resolution));
                            const auto uv_bin = static_cast<size_t>(uv_y * uv_bin_resolution + uv_x);
                            if (occupied_uv_bins[uv_bin])
                            {
                                continue;
                            }
                            occupied_uv_bins[uv_bin] = 1;

                            const auto background_start = add(body_hit.location, mul(ray_dir_for_pixel, 210.0));
                            const auto background_end = add(body_hit.location, mul(ray_dir_for_pixel, 5200.0));
                            TraceHit background_hit{};
                            for (const auto channel : {0, 1, 2, 3, 4, 5, 6})
                            {
                                background_hit = execute_line_trace(pawn, background_start, background_end, true, channel);
                                if (background_hit.hit)
                                {
                                    break;
                                }
                            }
                            Color color{};
                            Color material_hint{0.34, 0.36, 0.32, 0.94, 0.0};
                            if (background_hit.hit)
                            {
                                ++state.background_trace_hits;
                                material_hint = classify_background(background_hit.actor, background_hit.component, background_hit.location);
                            }
                            const auto capture_index = static_cast<size_t>(vz_index * ray_grid_width + hx_index);
                            if (capture_index < capture_colors.size() && capture_colors[capture_index])
                            {
                                color = sanitize_background_color(*capture_colors[capture_index], material_hint);
                                color.roughness = material_hint.roughness;
                                color.metallic = material_hint.metallic;
                                ++state.background_pixels;
                            }
                            else
                            {
                                color = material_hint;
                                if (background_hit.hit)
                                {
                                    ++state.background_pixels;
                                }
                            }

                            const auto phase = std::sin(body_hit.location.X() * 0.031 + body_hit.location.Y() * 0.047 + body_hit.location.Z() * 0.021);
                            const auto sampled_luma = color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
                            const auto dark_preserve = sampled_luma < 0.30 ? 0.78 : 1.0;
                            color.r = clamp(color.r * dark_preserve + phase * 0.010, 0.015, 0.95);
                            color.g = clamp(color.g * dark_preserve + phase * 0.008, 0.015, 0.95);
                            color.b = clamp(color.b * dark_preserve + phase * 0.010, 0.015, 0.95);
                            color.roughness = clamp(color.roughness + std::abs(phase) * 0.018, 0.62, 0.96);
                            color.metallic = clamp(color.metallic, 0.0, 0.6);

                            Sample sample{};
                            sample.world_position = body_hit.location;
                            sample.u = u;
                            sample.v = v;
                            sample.color = color;
                            const auto silhouette = std::max(std::abs(sx), std::abs(sy));
                            sample.weight = center_weight * (1.0 + silhouette * 0.35);
                            sample.brush_radius = clamp(0.026 + center_weight * 0.024 + std::abs(phase) * 0.014, 0.018, 0.075);
                            samples.push_back(sample);
                            ++state.visible_samples;
                        }
                    }
                }
            }
        }
        state.atlas_bins = static_cast<int>(samples.size());
        return samples;
    }

    struct AtlasCell
    {
        double r{0.0};
        double g{0.0};
        double b{0.0};
        double roughness{0.0};
        double metallic{0.0};
        double weight{0.0};
    };

    auto add_to_cell(AtlasCell& cell, const Color& color, double weight) -> void
    {
        cell.r += color.r * weight;
        cell.g += color.g * weight;
        cell.b += color.b * weight;
        cell.roughness += color.roughness * weight;
        cell.metallic += color.metallic * weight;
        cell.weight += weight;
    }

    auto color_distance_sq(const Color& a, const Color& b) -> double
    {
        const auto dr = a.r - b.r;
        const auto dg = a.g - b.g;
        const auto db = a.b - b.b;
        const auto dl = (a.r * 0.2126 + a.g * 0.7152 + a.b * 0.0722) -
                        (b.r * 0.2126 + b.g * 0.7152 + b.b * 0.0722);
        return dr * dr * 0.65 + dg * dg * 0.65 + db * db * 0.65 + dl * dl * 2.4;
    }

    auto luma(const Color& color) -> double
    {
        return color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
    }

    auto infer_surface_material(Color color, bool floor_like) -> Color
    {
        const auto lum = luma(color);
        const auto max_channel = std::max(color.r, std::max(color.g, color.b));
        const auto min_channel = std::min(color.r, std::min(color.g, color.b));
        const auto chroma = max_channel - min_channel;
        const auto existing_roughness = std::isfinite(color.roughness) ? clamp(color.roughness, 0.0, 1.0) : 0.92;
        const auto existing_metallic = std::isfinite(color.metallic) ? clamp(color.metallic, 0.0, 1.0) : 0.0;

        auto roughness_from_color = 0.86 - (lum - 0.45) * 0.22 + chroma * 0.18;
        roughness_from_color = floor_like ? clamp(std::max(roughness_from_color, 0.90), 0.86, 0.99)
                                          : clamp(roughness_from_color, 0.42, 0.99);

        const auto specular_hint = clamp((lum - 0.58) * 1.35 - chroma * 0.85, 0.0, 1.0);
        const auto metallic_from_color = clamp(specular_hint * 0.32, 0.0, 0.35);

        color.roughness = floor_like ? clamp(existing_roughness * 0.35 + roughness_from_color * 0.65, 0.86, 0.99)
                                     : clamp(existing_roughness * 0.55 + roughness_from_color * 0.45, 0.40, 0.99);
        color.metallic = floor_like ? clamp(existing_metallic * 0.25, 0.0, 0.12)
                                    : clamp(std::max(existing_metallic, metallic_from_color), 0.0, 0.60);
        return color;
    }

    auto scale_luma(const Color& color, double scale) -> Color
    {
        Color out = color;
        out.r = clamp(out.r * scale, 0.015, 0.98);
        out.g = clamp(out.g * scale, 0.015, 0.98);
        out.b = clamp(out.b * scale, 0.015, 0.98);
        out.roughness = clamp(out.roughness, 0.62, 0.98);
        out.metallic = clamp(out.metallic, 0.0, 0.6);
        return out;
    }

    struct PaletteSummary
    {
        double min_r{1.0};
        double min_g{1.0};
        double min_b{1.0};
        double max_r{0.0};
        double max_g{0.0};
        double max_b{0.0};
        double avg_r{0.0};
        double avg_g{0.0};
        double avg_b{0.0};
        int red_dominant_colors{0};
        bool red_dominant{false};
    };

    auto summarize_palette(const std::vector<Color>& palette) -> PaletteSummary
    {
        PaletteSummary summary{};
        double sum_r = 0.0;
        double sum_g = 0.0;
        double sum_b = 0.0;
        for (const auto& color : palette)
        {
            summary.min_r = std::min(summary.min_r, color.r);
            summary.min_g = std::min(summary.min_g, color.g);
            summary.min_b = std::min(summary.min_b, color.b);
            summary.max_r = std::max(summary.max_r, color.r);
            summary.max_g = std::max(summary.max_g, color.g);
            summary.max_b = std::max(summary.max_b, color.b);
            sum_r += color.r;
            sum_g += color.g;
            sum_b += color.b;
            if (is_red_paint_artifact(color))
            {
                ++summary.red_dominant_colors;
            }
        }
        const auto count = static_cast<double>(std::max<size_t>(palette.size(), 1));
        summary.avg_r = sum_r / count;
        summary.avg_g = sum_g / count;
        summary.avg_b = sum_b / count;
        summary.red_dominant =
            !palette.empty() &&
            (summary.red_dominant_colors > static_cast<int>(palette.size() / 2) ||
             (summary.avg_r > 0.40 && summary.avg_g < 0.14 && summary.avg_b < 0.14 &&
              summary.avg_r > summary.avg_g * 2.4 && summary.avg_r > summary.avg_b * 2.4));
        return summary;
    }

    auto fallback_camo_palette() -> std::vector<Color>
    {
        return {Color{0.23, 0.31, 0.20, 0.94, 0.0},
                Color{0.13, 0.16, 0.13, 0.97, 0.0},
                Color{0.38, 0.41, 0.36, 0.93, 0.0},
                Color{0.50, 0.49, 0.42, 0.91, 0.0},
                Color{0.20, 0.24, 0.25, 0.90, 0.0},
                Color{0.31, 0.36, 0.28, 0.95, 0.0},
                Color{0.08, 0.10, 0.09, 0.98, 0.0},
                Color{0.45, 0.44, 0.34, 0.92, 0.0}};
    }

    auto cell_to_color(const AtlasCell& cell, const Color& fallback) -> Color
    {
        if (cell.weight <= 0.0001)
        {
            return fallback;
        }
        const auto inv = 1.0 / cell.weight;
        Color out{};
        out.r = clamp(cell.r * inv, 0.02, 0.95);
        out.g = clamp(cell.g * inv, 0.02, 0.95);
        out.b = clamp(cell.b * inv, 0.02, 0.95);
        out.roughness = clamp(cell.roughness * inv, 0.62, 0.96);
        out.metallic = clamp(cell.metallic * inv, 0.0, 0.6);
        return out;
    }

    auto build_palette(const std::vector<Sample>& samples) -> std::vector<Color>
    {
        std::array<AtlasCell, 64> bins{};
        for (const auto& sample : samples)
        {
            const auto r = std::min(3, static_cast<int>(clamp(sample.color.r, 0.0, 0.999) * 4.0));
            const auto g = std::min(3, static_cast<int>(clamp(sample.color.g, 0.0, 0.999) * 4.0));
            const auto b = std::min(3, static_cast<int>(clamp(sample.color.b, 0.0, 0.999) * 4.0));
            add_to_cell(bins[static_cast<size_t>(r * 16 + g * 4 + b)], sample.color, sample.weight);
        }

        std::vector<int> order{};
        order.reserve(bins.size());
        for (int i = 0; i < static_cast<int>(bins.size()); ++i)
        {
            if (bins[static_cast<size_t>(i)].weight > 0.0001)
            {
                order.push_back(i);
            }
        }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return bins[static_cast<size_t>(a)].weight > bins[static_cast<size_t>(b)].weight;
        });

        std::vector<Color> palette{};
        for (const auto index : order)
        {
            const auto color = cell_to_color(bins[static_cast<size_t>(index)], Color{0.34, 0.36, 0.32, 0.92, 0.0});
            bool too_close = false;
            for (const auto& existing : palette)
            {
                if (color_distance_sq(color, existing) < 0.006)
                {
                    too_close = true;
                    break;
                }
            }
            if (!too_close)
            {
                palette.push_back(color);
            }
            if (palette.size() >= 16)
            {
                break;
            }
        }

        if (!samples.empty())
        {
            std::vector<Color> sorted_sample_colors{};
            sorted_sample_colors.reserve(samples.size());
            for (const auto& sample : samples)
            {
                sorted_sample_colors.push_back(sample.color);
            }
            std::sort(sorted_sample_colors.begin(), sorted_sample_colors.end(), [](const Color& a, const Color& b) {
                return luma(a) < luma(b);
            });
            const std::array<double, 8> percentiles{0.02, 0.08, 0.18, 0.35, 0.55, 0.72, 0.88, 0.97};
            for (const auto percentile : percentiles)
            {
                if (palette.size() >= 12)
                {
                    break;
                }
                const auto index = std::min(sorted_sample_colors.size() - 1,
                                            static_cast<size_t>(percentile * static_cast<double>(sorted_sample_colors.size() - 1)));
                auto color = sorted_sample_colors[index];
                if (percentile <= 0.18)
                {
                    color = scale_luma(color, 0.78);
                }
                bool too_close = false;
                for (const auto& existing : palette)
                {
                    if (color_distance_sq(color, existing) < 0.003)
                    {
                        too_close = true;
                        break;
                    }
                }
                if (!too_close)
                {
                    palette.push_back(color);
                }
            }
            while (palette.size() < 8 && !sorted_sample_colors.empty())
            {
                const auto seed = sorted_sample_colors[palette.size() % sorted_sample_colors.size()];
                const auto scale = palette.size() % 2 == 0 ? 0.72 : 1.18;
                palette.push_back(scale_luma(seed, scale));
            }
        }
        if (palette.empty())
        {
            palette.push_back(Color{0.24, 0.28, 0.22, 0.94, 0.0});
            palette.push_back(Color{0.16, 0.18, 0.16, 0.96, 0.0});
            palette.push_back(Color{0.42, 0.43, 0.41, 0.72, 0.25});
        }
        return palette;
    }

    auto build_target_atlas(const std::vector<Sample>& samples, int resolution, const std::vector<Color>& palette)
        -> std::vector<Color>
    {
        std::vector<AtlasCell> cells(static_cast<size_t>(resolution * resolution));
        const auto fallback = palette.empty() ? Color{0.34, 0.36, 0.32, 0.92, 0.0} : palette.front();
        for (const auto& sample : samples)
        {
            const auto x = std::min(resolution - 1, std::max(0, static_cast<int>(sample.u * static_cast<double>(resolution))));
            const auto y = std::min(resolution - 1, std::max(0, static_cast<int>(sample.v * static_cast<double>(resolution))));
            add_to_cell(cells[static_cast<size_t>(y * resolution + x)], sample.color, sample.weight);
        }

        std::vector<Color> atlas(static_cast<size_t>(resolution * resolution), fallback);
        for (int y = 0; y < resolution; ++y)
        {
            for (int x = 0; x < resolution; ++x)
            {
                const auto index = static_cast<size_t>(y * resolution + x);
                if (cells[index].weight > 0.0001)
                {
                    atlas[index] = cell_to_color(cells[index], fallback);
                    continue;
                }

                const auto u = (static_cast<double>(x) + 0.5) / static_cast<double>(resolution);
                const auto v = (static_cast<double>(y) + 0.5) / static_cast<double>(resolution);
                double best = 1000.0;
                const Sample* best_sample = nullptr;
                for (const auto& sample : samples)
                {
                    const auto du = sample.u - u;
                    const auto dv = sample.v - v;
                    const auto score = du * du + dv * dv - sample.weight * 0.0007;
                    if (score < best)
                    {
                        best = score;
                        best_sample = &sample;
                    }
                }
                atlas[index] = best_sample ? best_sample->color : fallback;
            }
        }

        for (int pass = 0; pass < 2; ++pass)
        {
            auto next = atlas;
            for (int y = 0; y < resolution; ++y)
            {
                for (int x = 0; x < resolution; ++x)
                {
                    AtlasCell avg{};
                    for (int oy = -1; oy <= 1; ++oy)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            const auto xx = std::min(resolution - 1, std::max(0, x + ox));
                            const auto yy = std::min(resolution - 1, std::max(0, y + oy));
                            add_to_cell(avg, atlas[static_cast<size_t>(yy * resolution + xx)], 1.0);
                        }
                    }
                    const auto smoothed = cell_to_color(avg, atlas[static_cast<size_t>(y * resolution + x)]);
                    next[static_cast<size_t>(y * resolution + x)] =
                        mix_color(atlas[static_cast<size_t>(y * resolution + x)], smoothed, 0.22);
                }
            }
            atlas.swap(next);
        }

        return atlas;
    }

    auto build_procedural_camo_atlas(int resolution, const std::vector<Color>& palette) -> std::vector<Color>
    {
        const auto safe_palette = palette.empty() ? fallback_camo_palette() : palette;
        std::vector<Color> atlas(static_cast<size_t>(resolution * resolution), safe_palette.front());
        for (int y = 0; y < resolution; ++y)
        {
            for (int x = 0; x < resolution; ++x)
            {
                const auto u = (static_cast<double>(x) + 0.5) / static_cast<double>(resolution);
                const auto v = (static_cast<double>(y) + 0.5) / static_cast<double>(resolution);
                const auto macro_x = static_cast<int>(std::floor((u + noise01(u, v, 5.0, 0x4721U) * 0.12) * 7.0));
                const auto macro_y = static_cast<int>(std::floor((v + noise01(u, v, 6.0, 0x8911U) * 0.10) * 9.0));
                const auto index_a = static_cast<size_t>(hash_u32(static_cast<std::uint32_t>(macro_x * 181 + macro_y * 263 + 29)) %
                                                         static_cast<std::uint32_t>(safe_palette.size()));
                const auto index_b = static_cast<size_t>(hash_u32(static_cast<std::uint32_t>(x * 73 + y * 151 + 0x33acU)) %
                                                         static_cast<std::uint32_t>(safe_palette.size()));
                auto color = mix_color(safe_palette[index_a], safe_palette[index_b], 0.28);
                const auto grain = noise01(u, v, 42.0, 0x51f0U) - 0.5;
                color.r = clamp(color.r + grain * 0.035, 0.03, 0.72);
                color.g = clamp(color.g + grain * 0.035, 0.05, 0.78);
                color.b = clamp(color.b + grain * 0.030, 0.04, 0.72);
                color.roughness = clamp(color.roughness, 0.82, 0.98);
                color.metallic = 0.0;
                atlas[static_cast<size_t>(y * resolution + x)] = color;
            }
        }
        return atlas;
    }

    auto atlas_lookup(const std::vector<Color>& atlas, int resolution, double u, double v) -> Color
    {
        if (atlas.empty())
        {
            return Color{0.34, 0.36, 0.32, 0.92, 0.0};
        }
        const auto x = std::min(resolution - 1, std::max(0, static_cast<int>(clamp(u, 0.0, 0.999999) * resolution)));
        const auto y = std::min(resolution - 1, std::max(0, static_cast<int>(clamp(v, 0.0, 0.999999) * resolution)));
        return atlas[static_cast<size_t>(y * resolution + x)];
    }

    auto disruptive_color(const std::vector<Color>& atlas,
                          int resolution,
                          const std::vector<Color>& palette,
                          double u,
                          double v,
                          int layer) -> Color
    {
        auto target = atlas_lookup(atlas, resolution, u, v);
        if (palette.empty())
        {
            return target;
        }

        const auto warped_u = u + (noise01(u, v, 6.0, 0x91e2U) - 0.5) * 0.18;
        const auto warped_v = v + (noise01(u, v, 7.0, 0x53a9U) - 0.5) * 0.16;
        const auto macro_x = static_cast<int>(std::floor(warped_u * 6.0));
        const auto macro_y = static_cast<int>(std::floor(warped_v * 8.0));
        const auto macro_pick = static_cast<size_t>(hash_u32(static_cast<std::uint32_t>(macro_x * 131 + macro_y * 197 + 17)) %
                                                   static_cast<std::uint32_t>(palette.size()));
        const auto mid_pick = static_cast<size_t>(hash_u32(static_cast<std::uint32_t>(
                                           static_cast<int>(std::floor(u * 17.0)) * 191 +
                                           static_cast<int>(std::floor(v * 19.0)) * 389 + 71)) %
                                       static_cast<std::uint32_t>(palette.size()));

        Color patch = palette[macro_pick];
        if (layer >= 2)
        {
            patch = mix_color(patch, palette[mid_pick], 0.55);
        }

        const auto band = 0.5 + 0.5 * std::sin((u * 7.2 + v * 4.4 + noise01(u, v, 5.0, 0x7721U)) * Pi);
        double patch_strength = 0.62;
        if (layer == 1)
        {
            patch_strength = 0.46;
        }
        else if (layer >= 2)
        {
            patch_strength = 0.22 + band * 0.18;
        }
        auto out = mix_color(target, patch, patch_strength);

        const auto grain = noise01(u, v, 43.0, 0xa17dU) - 0.5;
        if (layer >= 2)
        {
            out.r = clamp(out.r + grain * 0.035, 0.02, 0.95);
            out.g = clamp(out.g + grain * 0.030, 0.02, 0.95);
            out.b = clamp(out.b + grain * 0.035, 0.02, 0.95);
        }
        out.roughness = clamp(out.roughness, 0.62, 0.96);
        out.metallic = clamp(out.metallic, 0.0, 0.6);
        return out;
    }

    auto byte_from_unit(double value) -> uint8_t
    {
        return static_cast<uint8_t>(clamp(value, 0.0, 1.0) * 255.0 + 0.5);
    }

    auto changed_byte_count(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after) -> int
    {
        const auto count = std::min(before.size(), after.size());
        int changed = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (before[i] != after[i])
            {
                ++changed;
            }
        }
        changed += static_cast<int>(before.size() > after.size() ? before.size() - after.size() : after.size() - before.size());
        return changed;
    }

    auto rgba_buffer_ready(const ChannelByteBuffer& channel) -> bool
    {
        if (!channel.ok || channel.width <= 0 || channel.height <= 0)
        {
            return false;
        }
        const auto required = static_cast<size_t>(channel.width) * static_cast<size_t>(channel.height) * 4;
        return channel.bytes.size() >= required;
    }

    auto build_uv_calibration_albedo(const ChannelByteBuffer& channel) -> std::vector<uint8_t>
    {
        auto bytes = channel.bytes;
        if (!rgba_buffer_ready(channel))
        {
            return bytes;
        }
        for (int y = 0; y < channel.height; ++y)
        {
            for (int x = 0; x < channel.width; ++x)
            {
                const auto offset = static_cast<size_t>(y * channel.width + x) * 4;
                if (offset + 2 >= bytes.size())
                {
                    continue;
                }
                const auto color = uv_calibration_color((static_cast<double>(x) + 0.5) /
                                                        static_cast<double>(std::max(1, channel.width)),
                                                        (static_cast<double>(y) + 0.5) /
                                                        static_cast<double>(std::max(1, channel.height)));
                bytes[offset + 0] = byte_from_unit(color.r);
                bytes[offset + 1] = byte_from_unit(color.g);
                bytes[offset + 2] = byte_from_unit(color.b);
                if (offset + 3 < bytes.size())
                {
                    bytes[offset + 3] = 255;
                }
            }
        }
        return bytes;
    }

    auto build_scalar_channel(const ChannelByteBuffer& channel, double value) -> std::vector<uint8_t>
    {
        auto bytes = channel.bytes;
        if (!rgba_buffer_ready(channel))
        {
            return bytes;
        }
        const auto byte = byte_from_unit(value);
        for (int y = 0; y < channel.height; ++y)
        {
            for (int x = 0; x < channel.width; ++x)
            {
                const auto offset = static_cast<size_t>(y * channel.width + x) * 4;
                if (offset < bytes.size())
                {
                    bytes[offset] = byte;
                }
            }
        }
        return bytes;
    }

    auto sample_rgba_bytes(const std::vector<uint8_t>& bytes, int width, int height, double u, double v)
        -> std::optional<Color>
    {
        if (bytes.empty() || width <= 0 || height <= 0)
        {
            return std::nullopt;
        }
        const auto x = std::min(width - 1, std::max(0, static_cast<int>(std::floor(clamp(u, 0.0, 0.999999) * static_cast<double>(width)))));
        const auto y = std::min(height - 1, std::max(0, static_cast<int>(std::floor(clamp(v, 0.0, 0.999999) * static_cast<double>(height)))));
        const auto offset = static_cast<size_t>(y * width + x) * 4;
        if (offset + 2 >= bytes.size())
        {
            return std::nullopt;
        }
        Color color{};
        color.r = static_cast<double>(bytes[offset + 0]) / 255.0;
        color.g = static_cast<double>(bytes[offset + 1]) / 255.0;
        color.b = static_cast<double>(bytes[offset + 2]) / 255.0;
        return color;
    }

    auto sample_channel_color_nearest(const ChannelByteBuffer& channel, double u, double v) -> std::optional<Color>
    {
        if (!rgba_buffer_ready(channel))
        {
            return std::nullopt;
        }
        const auto x = std::min(channel.width - 1,
                                std::max(0, static_cast<int>(clamp(u, 0.0, 0.999999) * static_cast<double>(channel.width))));
        const auto y = std::min(channel.height - 1,
                                std::max(0, static_cast<int>(clamp(v, 0.0, 0.999999) * static_cast<double>(channel.height))));
        const auto offset = static_cast<size_t>(y * channel.width + x) * 4;
        if (offset + 2 >= channel.bytes.size())
        {
            return std::nullopt;
        }
        Color out{};
        out.r = static_cast<double>(channel.bytes[offset + 0]) / 255.0;
        out.g = static_cast<double>(channel.bytes[offset + 1]) / 255.0;
        out.b = static_cast<double>(channel.bytes[offset + 2]) / 255.0;
        out.roughness = 0.94;
        out.metallic = 0.0;
        return out;
    }

    auto record_gain_value(CalibrationStats& stats, double value, bool clamped) -> void
    {
        stats.gain_min = std::min(stats.gain_min, value);
        stats.gain_max = std::max(stats.gain_max, value);
        stats.gain_sum += value;
        ++stats.gain_values;
        if (clamped)
        {
            ++stats.clamped;
        }
    }

    auto calibrate_projection_color(const Color& hidden_background,
                                    const Color& visible_body,
                                    const Color& current_albedo,
                                    bool floor_like,
                                    CalibrationStats& stats) -> Color
    {
        constexpr double MinAlbedo = 0.035;
        constexpr double MinGain = 0.38;
        constexpr double MaxGain = 2.80;
        auto calibrate_channel = [&](double hidden, double visible, double albedo) -> double {
            const auto safe_albedo = std::max(MinAlbedo, albedo);
            const auto raw_gain = visible / safe_albedo;
            const auto clamped_gain = clamp(raw_gain, MinGain, MaxGain);
            record_gain_value(stats, clamped_gain, std::abs(raw_gain - clamped_gain) > 0.0001);
            return clamp(hidden / std::max(MinGain, clamped_gain), 0.012, 0.985);
        };

        Color out{};
        out.r = calibrate_channel(hidden_background.r, visible_body.r, current_albedo.r);
        out.g = calibrate_channel(hidden_background.g, visible_body.g, current_albedo.g);
        out.b = calibrate_channel(hidden_background.b, visible_body.b, current_albedo.b);
        out.roughness = floor_like ? 0.985 : clamp(std::max(hidden_background.roughness, 0.94), 0.88, 0.99);
        out.metallic = 0.0;
        ++stats.samples;
        return out;
    }

    struct RgbByteSummary
    {
        int pixels{0};
        int red_dominant_pixels{0};
        int near_uniform_samples{0};
        double min_r{1.0};
        double min_g{1.0};
        double min_b{1.0};
        double max_r{0.0};
        double max_g{0.0};
        double max_b{0.0};
        double avg_r{0.0};
        double avg_g{0.0};
        double avg_b{0.0};
    };

    auto summarize_rgb_bytes(const std::vector<uint8_t>& bytes, int width, int height) -> RgbByteSummary
    {
        RgbByteSummary summary{};
        if (width <= 0 || height <= 0 || bytes.size() < 4)
        {
            return summary;
        }
        const auto pixels = std::min(static_cast<size_t>(width) * static_cast<size_t>(height), bytes.size() / 4);
        summary.pixels = static_cast<int>(pixels);
        double sum_r = 0.0;
        double sum_g = 0.0;
        double sum_b = 0.0;
        for (size_t i = 0; i < pixels; ++i)
        {
            const auto offset = i * 4;
            const auto r = static_cast<double>(bytes[offset + 0]) / 255.0;
            const auto g = static_cast<double>(bytes[offset + 1]) / 255.0;
            const auto b = static_cast<double>(bytes[offset + 2]) / 255.0;
            summary.min_r = std::min(summary.min_r, r);
            summary.min_g = std::min(summary.min_g, g);
            summary.min_b = std::min(summary.min_b, b);
            summary.max_r = std::max(summary.max_r, r);
            summary.max_g = std::max(summary.max_g, g);
            summary.max_b = std::max(summary.max_b, b);
            sum_r += r;
            sum_g += g;
            sum_b += b;
            if (r > 0.40 && g < 0.16 && b < 0.16 && r > g * 2.4 && r > b * 2.4)
            {
                ++summary.red_dominant_pixels;
            }
        }
        const auto denom = static_cast<double>(std::max<int>(summary.pixels, 1));
        summary.avg_r = sum_r / denom;
        summary.avg_g = sum_g / denom;
        summary.avg_b = sum_b / denom;
        summary.near_uniform_samples = (summary.max_r - summary.min_r < 0.018 &&
                                        summary.max_g - summary.min_g < 0.018 &&
                                        summary.max_b - summary.min_b < 0.018)
                                           ? 1
                                           : 0;
        return summary;
    }

    struct ScalarByteSummary
    {
        int pixels{0};
        int near_zero_pixels{0};
        int high_pixels{0};
        int near_uniform_samples{0};
        double min_value{1.0};
        double max_value{0.0};
        double avg_value{0.0};
    };

    auto summarize_scalar_bytes(const std::vector<uint8_t>& bytes, int width, int height) -> ScalarByteSummary
    {
        ScalarByteSummary summary{};
        if (width <= 0 || height <= 0 || bytes.size() < 4)
        {
            return summary;
        }
        const auto pixels = std::min(static_cast<size_t>(width) * static_cast<size_t>(height), bytes.size() / 4);
        summary.pixels = static_cast<int>(pixels);
        double sum = 0.0;
        for (size_t i = 0; i < pixels; ++i)
        {
            const auto value = static_cast<double>(bytes[i * 4]) / 255.0;
            summary.min_value = std::min(summary.min_value, value);
            summary.max_value = std::max(summary.max_value, value);
            sum += value;
            if (value < 0.012)
            {
                ++summary.near_zero_pixels;
            }
            if (value > 0.86)
            {
                ++summary.high_pixels;
            }
        }
        const auto denom = static_cast<double>(std::max<int>(summary.pixels, 1));
        summary.avg_value = sum / denom;
        summary.near_uniform_samples = summary.max_value - summary.min_value < 0.006 ? 1 : 0;
        return summary;
    }

    struct ProjectionFrame
    {
        Unreal::FVector eye{};
        Unreal::FVector forward{};
        Unreal::FVector right{};
        Unreal::FVector up{};
        double fov_degrees{42.0};
        bool fov_fallback{true};
        StringType source{STR("camera_manager")};
        Unreal::FRotator rotation{};
        bool has_rotation{false};
        double deproject_hfov{0.0};
        double deproject_vfov{0.0};
        double camera_fov_degrees{0.0};
        double legacy_linear_hfov{0.0};
        double legacy_linear_vfov{0.0};
        double camera_deproject_angle_delta{0.0};
        StringType fov_source{STR("fallback")};
    };

    struct DeprojectRay
    {
        bool ok{false};
        Unreal::FVector location{};
        Unreal::FVector direction{};
        StringType failure{STR("not_run")};
    };

    struct ProjectedScreenPoint
    {
        bool ok{false};
        double x{0.0};
        double y{0.0};
        StringType failure{STR("not_run")};
    };

    struct LabHitSample
    {
        bool ok{false};
        ScreenHitSample sample{};
        int sample_index{-1};
        StringType failure{STR("not_run")};
    };

    struct ProjectionTexel
    {
        double r{0.0};
        double g{0.0};
        double b{0.0};
        double roughness{0.0};
        double metallic{0.0};
        double weight{0.0};
        int priority{0};
        bool floor_like{false};
        bool dilated{false};
    };

    struct TerminalExtrudeStats
    {
        int direct_texels{0};
        int edge_texels{0};
        int extruded_texels{0};
        int fallback_extruded_texels{0};
        int preserved_direct{0};
        int preserved_original{0};
        int worker_threads{0};
    };

    struct ViewProjectionStats
    {
        int primary_view_pixels{0};
        int aux_view_pixels{0};
        int floor_view_pixels{0};
        int body_hits{0};
        int owner_body_hits{0};
        int spatial_fallback_body_hits{0};
        int rejected_body_hits{0};
        int uv_hits{0};
        int background_hits{0};
        int projected_samples{0};
        int uv_coverage{0};
        int filled_by_primary{0};
        int filled_by_aux{0};
        int filled_by_floor{0};
        int filled_by_interpolation{0};
        int preserved_original{0};
        BodyTraceDebugStats trace_debug{};
        double camera_fov{42.0};
        bool camera_fov_fallback{true};
    };

    auto camera_fov_from_manager(Unreal::UObject* camera) -> std::pair<double, bool>
    {
        if (camera)
        {
            for (const auto* function_name : {STR("GetFOVAngle"), STR("GetCameraFOV"), STR("GetFOV")})
            {
                if (auto value = call_no_params_return_number(camera, function_name))
                {
                    if (*value >= 10.0 && *value <= 150.0)
                    {
                        return {*value, false};
                    }
                }
            }
            for (const auto* property_name : {STR("FOVAngle"), STR("DefaultFOV"), STR("FieldOfView"), STR("FOV")})
            {
                if (auto value = read_number_property_by_name(camera, property_name))
                {
                    if (*value >= 10.0 && *value <= 150.0)
                    {
                        return {*value, false};
                    }
                }
            }
        }
        return {42.0, true};
    }

    auto fov_from_deproject_sample(double center_to_sample_angle, double screen_half_fraction) -> double
    {
        if (center_to_sample_angle <= 0.0001 || screen_half_fraction <= 0.0001)
        {
            return 0.0;
        }
        return 2.0 * std::atan(std::tan(center_to_sample_angle) / screen_half_fraction);
    }

    auto is_floor_like_object(Unreal::UObject* actor, Unreal::UObject* component) -> bool
    {
        const auto text = lower_copy(object_name_or_empty(actor) + STR(" ") + object_name_or_empty(component));
        return contains_any_text(text,
                                 {STR("floor"),
                                  STR("ground"),
                                  STR("tile"),
                                  STR("asphalt"),
                                  STR("concrete"),
                                  STR("stage"),
                                  STR("terrain"),
                                  STR("road"),
                                  STR("deck")});
    }

    auto deproject_screen_position(Unreal::UObject* controller, double screen_x, double screen_y) -> DeprojectRay
    {
        DeprojectRay out{};
        if (!controller)
        {
            out.failure = STR("controller_unavailable");
            return out;
        }
        auto* function = controller->GetFunctionByNameInChain(STR("DeprojectScreenPositionToWorld"));
        if (!function)
        {
            out.failure = STR("deproject_function_unavailable");
            return out;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        int numeric_index = 0;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (Unreal::CastField<Unreal::FStructProperty>(property))
            {
                continue;
            }
            if (contains_text(name, STR("screenx")) || contains_text(name, STR("screen_x")) ||
                name == STR("x") || numeric_index == 0)
            {
                write_number(property, params.data(), screen_x);
                ++numeric_index;
            }
            else if (contains_text(name, STR("screeny")) || contains_text(name, STR("screen_y")) ||
                     name == STR("y") || numeric_index == 1)
            {
                write_number(property, params.data(), screen_y);
                ++numeric_index;
            }
        }

        controller->ProcessEvent(function, params.data());
        out.ok = read_return_bool(function, params.data());
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (!Unreal::CastField<Unreal::FStructProperty>(property))
            {
                continue;
            }
            if (contains_text(name, STR("worldlocation")) || contains_text(name, STR("world_location")) ||
                contains_text(name, STR("location")))
            {
                if (auto location = read_vector_value(property, params.data()))
                {
                    out.location = *location;
                }
            }
            else if (contains_text(name, STR("worlddirection")) || contains_text(name, STR("world_direction")) ||
                     contains_text(name, STR("direction")))
            {
                if (auto direction = read_vector_value(property, params.data()))
                {
                    out.direction = normalize(*direction);
                }
            }
        }
        if (!out.ok)
        {
            out.failure = STR("deproject_return_false");
        }
        else if (length(out.direction) < 0.01)
        {
            out.ok = false;
            out.failure = STR("deproject_direction_invalid");
        }
        else
        {
            out.failure.clear();
        }
        return out;
    }

    auto project_world_location_to_screen(Unreal::UObject* controller,
                                          const Unreal::FVector& world_position,
                                          bool player_viewport_relative = false) -> ProjectedScreenPoint
    {
        ProjectedScreenPoint out{};
        if (!controller)
        {
            out.failure = STR("controller_unavailable");
            return out;
        }
        auto* function = controller->GetFunctionByNameInChain(STR("ProjectWorldLocationToScreen"));
        if (!function)
        {
            out.failure = STR("project_world_location_to_screen_unavailable");
            return out;
        }

        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        int struct_index = 0;
        bool wrote_world = false;
        bool saw_screen = false;
        bool wrote_relative = false;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (Unreal::CastField<Unreal::FStructProperty>(property))
            {
                if ((contains_text(name, STR("world")) || contains_text(name, STR("location"))) && !contains_text(name, STR("screen")))
                {
                    wrote_world = write_vector(property, params.data(), world_position) || wrote_world;
                }
                else if (contains_text(name, STR("screen")) || struct_index == 1)
                {
                    saw_screen = true;
                }
                ++struct_index;
            }
            else if (contains_text(name, STR("viewport")) || contains_text(name, STR("relative")))
            {
                wrote_relative = write_bool(property, params.data(), player_viewport_relative) || wrote_relative;
            }
        }

        if (!wrote_world)
        {
            out.failure = STR("project_world_param_unresolved");
            return out;
        }

        controller->ProcessEvent(function, params.data());
        const auto return_ok = read_return_bool(function, params.data());
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                !Unreal::CastField<Unreal::FStructProperty>(property))
            {
                continue;
            }
            const auto name = lower_copy(property->GetName());
            if (contains_text(name, STR("screen")))
            {
                if (auto screen = read_vector2(property, params.data()))
                {
                    out.x = screen->first;
                    out.y = screen->second;
                    out.ok = return_ok && std::isfinite(out.x) && std::isfinite(out.y);
                    out.failure = out.ok ? StringType{} : STR("project_return_false_or_invalid");
                    return out;
                }
            }
        }

        out.failure = saw_screen ? STR("project_screen_param_read_failed") : STR("project_screen_param_unresolved");
        if (!return_ok && out.failure.empty())
        {
            out.failure = STR("project_return_false");
        }
        (void)wrote_relative;
        return out;
    }

    auto assign_projected_capture_coords(Unreal::UObject* controller,
                                         const ViewportInfo& viewport,
                                         std::vector<ScreenHitSample>& samples) -> ProjectedCaptureCoordStats
    {
        ProjectedCaptureCoordStats stats{};
        if (!controller || viewport.width <= 0 || viewport.height <= 0)
        {
            stats.failed = static_cast<int>(samples.size());
            stats.first_failure = STR("projected_capture_prereq_unavailable");
            return stats;
        }

        const auto width = static_cast<double>(std::max(1, viewport.width));
        const auto height = static_cast<double>(std::max(1, viewport.height));
        for (auto& sample : samples)
        {
            const auto projected = project_world_location_to_screen(controller, sample.world_position, false);
            if (!projected.ok)
            {
                ++stats.failed;
                if (stats.first_failure.empty())
                {
                    stats.first_failure = projected.failure.empty()
                                              ? STR("project_world_location_to_screen_failed")
                                              : projected.failure;
                }
                continue;
            }

            const auto nx = projected.x / width;
            const auto ny = projected.y / height;
            const auto outside = nx < -0.02 || ny < -0.02 || nx > 1.02 || ny > 1.02;
            if (outside)
            {
                ++stats.out_of_view;
            }
            sample.capture_nx = clamp(nx, 0.0, 0.999999);
            sample.capture_ny = clamp(ny, 0.0, 0.999999);
            const auto expected_x = sample.screen_x >= 0.0 && sample.screen_x <= 1.0
                                        ? sample.nx * width
                                        : sample.screen_x;
            const auto expected_y = sample.screen_y >= 0.0 && sample.screen_y <= 1.0
                                        ? sample.ny * height
                                        : sample.screen_y;
            const auto dx = projected.x - expected_x;
            const auto dy = projected.y - expected_y;
            const auto delta = std::sqrt(dx * dx + dy * dy);
            stats.delta_sum_px += delta;
            stats.delta_max_px = std::max(stats.delta_max_px, delta);
            ++stats.ok;
        }
        return stats;
    }

    auto find_lab_hit_sample(Unreal::UObject* component,
                             Unreal::UObject* pawn,
                             Unreal::UObject* mesh,
                             Unreal::UObject* controller,
                             const ViewportInfo& viewport) -> LabHitSample
    {
        LabHitSample out{};
        if (!component || !pawn || !mesh || !controller)
        {
            out.failure = STR("lab_hit_prereq_unavailable");
            return out;
        }
        const std::array<std::pair<double, double>, 13> points{{
            {0.50, 0.50},
            {0.48, 0.48},
            {0.52, 0.48},
            {0.48, 0.54},
            {0.52, 0.54},
            {0.50, 0.60},
            {0.46, 0.56},
            {0.54, 0.56},
            {0.50, 0.42},
            {0.44, 0.50},
            {0.56, 0.50},
            {0.50, 0.36},
            {0.50, 0.68},
        }};
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto nx = points[i].first;
            const auto ny = points[i].second;
            const auto screen_x = nx * static_cast<double>(std::max(1, viewport.width));
            const auto screen_y = ny * static_cast<double>(std::max(1, viewport.height));
            auto hit = hit_test_at_screen_position(component, mesh, controller, screen_x, screen_y, true);
            if (!hit.success || !hit.has_uv)
            {
                out.failure = hit.failure.empty() ? STR("hit_test_unsuccessful") : hit.failure;
                continue;
            }

            out.ok = true;
            out.sample_index = static_cast<int>(i) + 1;
            out.sample.screen_x = screen_x;
            out.sample.screen_y = screen_y;
            out.sample.nx = nx;
            out.sample.ny = ny;
            out.sample.u = hit.u;
            out.sample.v = hit.v;
            out.sample.world_position = hit.world_position;
            out.sample.normal = hit.normal;
            out.failure.clear();
            return out;
        }
        return out;
    }

    auto collect_lab_hit_samples(Unreal::UObject* component,
                                 Unreal::UObject* pawn,
                                 Unreal::UObject* mesh,
                                 Unreal::UObject* controller,
                                 const ViewportInfo& viewport) -> std::vector<LabHitSample>
    {
        std::vector<LabHitSample> hits{};
        if (!component || !pawn || !mesh || !controller)
        {
            return hits;
        }

        const std::array<std::pair<double, double>, 25> points{{
            {0.50, 0.50},
            {0.44, 0.50}, {0.56, 0.50}, {0.50, 0.40}, {0.50, 0.60},
            {0.40, 0.38}, {0.50, 0.38}, {0.60, 0.38},
            {0.40, 0.48}, {0.50, 0.48}, {0.60, 0.48},
            {0.40, 0.58}, {0.50, 0.58}, {0.60, 0.58},
            {0.40, 0.68}, {0.50, 0.68}, {0.60, 0.68},
            {0.34, 0.44}, {0.66, 0.44}, {0.34, 0.56}, {0.66, 0.56},
            {0.46, 0.30}, {0.54, 0.30}, {0.46, 0.74}, {0.54, 0.74},
        }};

        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto nx = points[i].first;
            const auto ny = points[i].second;
            const auto screen_x = nx * static_cast<double>(std::max(1, viewport.width));
            const auto screen_y = ny * static_cast<double>(std::max(1, viewport.height));
            auto hit = hit_test_at_screen_position(component, mesh, controller, screen_x, screen_y, true);
            if (!hit.success || !hit.has_uv)
            {
                continue;
            }

            LabHitSample sample{};
            sample.ok = true;
            sample.sample_index = static_cast<int>(i) + 1;
            sample.sample.screen_x = screen_x;
            sample.sample.screen_y = screen_y;
            sample.sample.nx = nx;
            sample.sample.ny = ny;
            sample.sample.u = hit.u;
            sample.sample.v = hit.v;
            sample.sample.world_position = hit.world_position;
            sample.sample.normal = hit.normal;
            hits.push_back(sample);
        }
        return hits;
    }

    auto make_projection_frame_from_deproject(Unreal::UObject* controller,
                                              const ViewportInfo& viewport,
                                              double yaw_offset,
                                              double pitch_offset) -> std::optional<ProjectionFrame>
    {
        if (!controller || viewport.width <= 0 || viewport.height <= 0)
        {
            return std::nullopt;
        }
        const auto center_x = static_cast<double>(viewport.width) * 0.5;
        const auto center_y = static_cast<double>(viewport.height) * 0.5;
        const auto sample_dx = std::max(16.0, static_cast<double>(viewport.width) * 0.25);
        const auto sample_dy = std::max(16.0, static_cast<double>(viewport.height) * 0.25);
        auto center = deproject_screen_position(controller, center_x, center_y);
        auto right_ray = deproject_screen_position(controller, std::min(static_cast<double>(viewport.width - 1), center_x + sample_dx), center_y);
        auto up_ray = deproject_screen_position(controller, center_x, std::max(0.0, center_y - sample_dy));
        if (!center.ok || !right_ray.ok || !up_ray.ok)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} deproject_frame unavailable center={} right={} up={} failures=({}, {}, {})\n"),
                ModTag,
                center.ok ? 1 : 0,
                right_ray.ok ? 1 : 0,
                up_ray.ok ? 1 : 0,
                center.failure.empty() ? STR("<none>") : center.failure,
                right_ray.failure.empty() ? STR("<none>") : right_ray.failure,
                up_ray.failure.empty() ? STR("<none>") : up_ray.failure);
            return std::nullopt;
        }

        auto* camera = camera_manager_for_controller(controller);
        auto camera_eye = call_no_params_return_vector(camera, STR("GetCameraLocation"));
        auto camera_rotation = call_no_params_return_rotator(camera, STR("GetCameraRotation"));
        const auto camera_fov = camera_fov_from_manager(camera);

        const auto base_forward = camera_rotation ? rotator_forward(*camera_rotation) : center.direction;
        auto forward = rotate_yaw_pitch(base_forward, yaw_offset, pitch_offset);
        auto right = camera_rotation ? rotator_right(*camera_rotation)
                                     : normalize(sub(right_ray.direction, mul(center.direction, dot(right_ray.direction, center.direction))));
        if (length(right) < 0.01)
        {
            right = normalize(cross(vec(0.0, 0.0, 1.0), forward));
        }
        auto up = camera_rotation ? rotator_up(*camera_rotation)
                                  : normalize(sub(up_ray.direction, mul(center.direction, dot(up_ray.direction, center.direction))));
        if (length(up) < 0.01)
        {
            up = normalize(cross(forward, right));
        }
        if (!camera_rotation)
        {
            right = normalize(cross(up, forward));
            up = normalize(cross(forward, right));
        }

        const auto right_angle = std::acos(clamp(dot(center.direction, right_ray.direction), -1.0, 1.0));
        const auto up_angle = std::acos(clamp(dot(center.direction, up_ray.direction), -1.0, 1.0));
        const auto right_fraction = sample_dx / (static_cast<double>(viewport.width) * 0.5);
        const auto up_fraction = sample_dy / (static_cast<double>(viewport.height) * 0.5);
        const auto estimated_hfov = fov_from_deproject_sample(right_angle, right_fraction);
        const auto estimated_vfov = fov_from_deproject_sample(up_angle, up_fraction);
        const auto aspect = static_cast<double>(std::max(1, viewport.width)) / static_cast<double>(std::max(1, viewport.height));
        const auto deproject_hfov_from_vfov = estimated_vfov > 0.0001
                                                  ? 2.0 * std::atan(std::tan(estimated_vfov * 0.5) * std::max(0.1, aspect))
                                                  : 0.0;
        const auto corrected_deproject_hfov = estimated_hfov > 0.0001 ? estimated_hfov : deproject_hfov_from_vfov;
        auto fov_degrees = corrected_deproject_hfov > 0.0001
                               ? corrected_deproject_hfov * 180.0 / Pi
                               : camera_fov.first;
        fov_degrees = clamp(fov_degrees, 10.0, 150.0);
        const auto fov_used_deproject = corrected_deproject_hfov > 0.0001;
        const auto angle_delta = std::acos(clamp(dot(base_forward, center.direction), -1.0, 1.0)) * 180.0 / Pi;
        const auto eye = camera_eye ? *camera_eye : center.location;

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} deproject_frame ok viewport={}x{} eye=({}, {}, {}) camera_eye=({}, {}, {}) deproject_eye=({}, {}, {}) forward=({}, {}, {}) deproject_forward=({}, {}, {}) right=({}, {}, {}) up=({}, {}, {}) scene_capture_fov={} fov_source={} camera_fov={} camera_fov_fallback={} fov_axis=corrected_deproject_horizontal hfov_perspective={} vfov_perspective={} hfov_from_vfov={} old_linear_hfov={} old_linear_vfov={} camera_deproject_angle_delta={} sample=({}, {})\n"),
            ModTag,
            viewport.width,
            viewport.height,
            eye.X(),
            eye.Y(),
            eye.Z(),
            camera_eye ? camera_eye->X() : 0.0,
            camera_eye ? camera_eye->Y() : 0.0,
            camera_eye ? camera_eye->Z() : 0.0,
            center.location.X(),
            center.location.Y(),
            center.location.Z(),
            forward.X(),
            forward.Y(),
            forward.Z(),
            center.direction.X(),
            center.direction.Y(),
            center.direction.Z(),
            right.X(),
            right.Y(),
            right.Z(),
            up.X(),
            up.Y(),
            up.Z(),
            fov_degrees,
            fov_used_deproject ? STR("corrected_deproject_hfov") : STR("camera_manager"),
            camera_fov.first,
            camera_fov.second ? 1 : 0,
            estimated_hfov * 180.0 / Pi,
            estimated_vfov * 180.0 / Pi,
            deproject_hfov_from_vfov * 180.0 / Pi,
            right_fraction > 0.0001 ? (2.0 * right_angle / right_fraction) * 180.0 / Pi : 0.0,
            up_fraction > 0.0001 ? (2.0 * up_angle / up_fraction) * 180.0 / Pi : 0.0,
            angle_delta,
            sample_dx,
            sample_dy);

        ProjectionFrame frame{};
        frame.eye = eye;
        frame.forward = forward;
        frame.right = right;
        frame.up = up;
        frame.fov_degrees = fov_degrees;
        frame.fov_fallback = !fov_used_deproject && camera_fov.second;
        frame.source = camera_rotation ? STR("camera_manager_with_deproject_diagnostics")
                                       : STR("controller_deproject_corrected_fov");
        frame.rotation = camera_rotation ? *camera_rotation : rotator_from_forward(forward);
        frame.has_rotation = true;
        frame.deproject_hfov = estimated_hfov * 180.0 / Pi;
        frame.deproject_vfov = estimated_vfov * 180.0 / Pi;
        frame.camera_fov_degrees = camera_fov.first;
        frame.legacy_linear_hfov = right_fraction > 0.0001 ? (2.0 * right_angle / right_fraction) * 180.0 / Pi : 0.0;
        frame.legacy_linear_vfov = up_fraction > 0.0001 ? (2.0 * up_angle / up_fraction) * 180.0 / Pi : 0.0;
        frame.camera_deproject_angle_delta = angle_delta;
        frame.fov_source = fov_used_deproject ? STR("corrected_deproject_hfov") : STR("camera_manager");
        return frame;
    }

    auto make_projection_frame(Unreal::UObject* pawn, double yaw_offset, double pitch_offset) -> std::optional<ProjectionFrame>
    {
        auto* controller = find_player_controller_for_pawn(pawn);
        auto* camera = controller ? camera_manager_for_controller(controller) : find_player_camera_manager();
        auto eye = call_no_params_return_vector(camera, STR("GetCameraLocation"));
        auto rotation = call_no_params_return_rotator(camera, STR("GetCameraRotation"));
        if (!eye || !rotation)
        {
            return std::nullopt;
        }
        const auto fov = camera_fov_from_manager(camera);
        auto forward = rotate_yaw_pitch(rotator_forward(*rotation), yaw_offset, pitch_offset);
        auto right = rotator_right(*rotation);
        if (length(right) < 0.01)
        {
            right = vec(0.0, 1.0, 0.0);
        }
        auto up = rotator_up(*rotation);
        if (length(up) < 0.01)
        {
            up = vec(0.0, 0.0, 1.0);
        }
        (void)pawn;
        ProjectionFrame frame{};
        frame.eye = *eye;
        frame.forward = forward;
        frame.right = right;
        frame.up = up;
        frame.fov_degrees = fov.first;
        frame.fov_fallback = fov.second;
        frame.source = STR("camera_manager");
        frame.rotation = *rotation;
        frame.has_rotation = true;
        frame.camera_fov_degrees = fov.first;
        frame.fov_source = fov.second ? STR("fallback") : STR("camera_manager");
        return frame;
    }

    auto make_orbit_projection_frame(Unreal::UObject* pawn,
                                     const ProjectionFrame& primary,
                                     double yaw_offset_degrees,
                                     double pitch_offset_degrees) -> std::optional<ProjectionFrame>
    {
        if (!pawn)
        {
            return std::nullopt;
        }

        const auto pawn_location = call_no_params_return_vector(pawn, STR("K2_GetActorLocation"));
        if (!pawn_location)
        {
            return std::nullopt;
        }

        const auto target = add(*pawn_location, vec(0.0, 0.0, 82.0));
        auto offset = sub(primary.eye, target);
        auto radius = length(offset);
        if (radius < 80.0)
        {
            offset = add(mul(primary.forward, -260.0), vec(0.0, 0.0, 32.0));
            radius = length(offset);
        }
        radius = clamp(radius, 120.0, 900.0);

        const auto base_yaw = std::atan2(offset.Y(), offset.X());
        const auto base_horizontal = std::sqrt(offset.X() * offset.X() + offset.Y() * offset.Y());
        auto elevation = std::atan2(offset.Z(), std::max(1.0, base_horizontal));
        elevation = clamp(elevation + pitch_offset_degrees * Pi / 180.0, -58.0 * Pi / 180.0, 68.0 * Pi / 180.0);
        const auto yaw = base_yaw + yaw_offset_degrees * Pi / 180.0;
        const auto horizontal = std::max(35.0, radius * std::cos(elevation));
        const auto eye = add(target,
                             vec(horizontal * std::cos(yaw),
                                 horizontal * std::sin(yaw),
                                 radius * std::sin(elevation)));
        const auto forward = normalize(sub(target, eye));
        auto right = normalize(cross(vec(0.0, 0.0, 1.0), forward));
        if (length(right) < 0.01)
        {
            right = vec(0.0, 1.0, 0.0);
        }
        auto up = normalize(cross(forward, right));
        if (length(up) < 0.01)
        {
            up = vec(0.0, 0.0, 1.0);
        }

        ProjectionFrame frame = primary;
        frame.eye = eye;
        frame.forward = forward;
        frame.right = right;
        frame.up = up;
        frame.rotation = rotator_from_forward(forward);
        frame.has_rotation = true;
        frame.source = STR("orbit_aux_player_camera");
        return frame;
    }

    auto build_projection_fill_atlas(const std::vector<Sample>& samples, int resolution) -> std::vector<Color>
    {
        std::vector<AtlasCell> cells(static_cast<size_t>(resolution * resolution));
        AtlasCell all{};
        for (const auto& sample : samples)
        {
            const auto x = std::min(resolution - 1, std::max(0, static_cast<int>(sample.u * static_cast<double>(resolution))));
            const auto y = std::min(resolution - 1, std::max(0, static_cast<int>(sample.v * static_cast<double>(resolution))));
            add_to_cell(cells[static_cast<size_t>(y * resolution + x)], sample.color, sample.weight);
            add_to_cell(all, sample.color, sample.weight);
        }
        const auto fallback = cell_to_color(all, Color{0.34, 0.36, 0.32, 0.94, 0.0});
        std::vector<Color> atlas(static_cast<size_t>(resolution * resolution), fallback);
        for (int y = 0; y < resolution; ++y)
        {
            for (int x = 0; x < resolution; ++x)
            {
                const auto index = static_cast<size_t>(y * resolution + x);
                if (cells[index].weight > 0.0001)
                {
                    atlas[index] = cell_to_color(cells[index], fallback);
                    continue;
                }

                const auto u = (static_cast<double>(x) + 0.5) / static_cast<double>(resolution);
                const auto v = (static_cast<double>(y) + 0.5) / static_cast<double>(resolution);
                double best_score = 1000000.0;
                const Sample* best_sample = nullptr;
                for (const auto& sample : samples)
                {
                    const auto du = sample.u - u;
                    const auto dv = sample.v - v;
                    const auto score = du * du + dv * dv - sample.weight * 0.0004;
                    if (score < best_score)
                    {
                        best_score = score;
                        best_sample = &sample;
                    }
                }
                atlas[index] = best_sample ? best_sample->color : fallback;
            }
        }

        for (int pass = 0; pass < 2; ++pass)
        {
            auto next = atlas;
            for (int y = 0; y < resolution; ++y)
            {
                for (int x = 0; x < resolution; ++x)
                {
                    AtlasCell avg{};
                    for (int oy = -1; oy <= 1; ++oy)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            const auto xx = std::min(resolution - 1, std::max(0, x + ox));
                            const auto yy = std::min(resolution - 1, std::max(0, y + oy));
                            add_to_cell(avg, atlas[static_cast<size_t>(yy * resolution + xx)], 1.0);
                        }
                    }
                    next[static_cast<size_t>(y * resolution + x)] =
                        mix_color(atlas[static_cast<size_t>(y * resolution + x)],
                                  cell_to_color(avg, atlas[static_cast<size_t>(y * resolution + x)]),
                                  0.18);
                }
            }
            atlas.swap(next);
        }
        return atlas;
    }

    auto splat_projection_texel(std::vector<ProjectionTexel>& texels,
                                int width,
                                int height,
                                double u,
                                double v,
                                const Color& color,
                                double base_weight,
                                int priority,
                                bool floor_like,
                                int forced_radius = -1) -> void
    {
        if (width <= 0 || height <= 0 || texels.empty())
        {
            return;
        }
        const auto cx = std::min(width - 1, std::max(0, static_cast<int>(clamp(u, 0.0, 0.999999) * static_cast<double>(width))));
        const auto cy = std::min(height - 1, std::max(0, static_cast<int>(clamp(v, 0.0, 0.999999) * static_cast<double>(height))));
        const auto radius = forced_radius >= 0 ? forced_radius : (priority >= 4 ? 7 : (floor_like ? 4 : 3));
        for (int oy = -radius; oy <= radius; ++oy)
        {
            for (int ox = -radius; ox <= radius; ++ox)
            {
                const auto x = cx + ox;
                const auto y = cy + oy;
                if (x < 0 || y < 0 || x >= width || y >= height)
                {
                    continue;
                }
                const auto dist = std::sqrt(static_cast<double>(ox * ox + oy * oy));
                if (dist > static_cast<double>(radius) + 0.001)
                {
                    continue;
                }
                auto& texel = texels[static_cast<size_t>(y * width + x)];
                if (texel.weight > 0.0001 && priority < texel.priority)
                {
                    continue;
                }
                if (priority > texel.priority)
                {
                    texel = ProjectionTexel{};
                    texel.priority = priority;
                }
                const auto weight = base_weight * (1.0 - dist / (static_cast<double>(radius) + 1.0));
                texel.r += color.r * weight;
                texel.g += color.g * weight;
                texel.b += color.b * weight;
                texel.roughness += color.roughness * weight;
                texel.metallic += color.metallic * weight;
                texel.weight += weight;
                texel.priority = std::max(texel.priority, priority);
                texel.floor_like = texel.floor_like || floor_like;
            }
        }
    }

    auto compensate_projected_albedo(Color color, bool floor_like) -> Color
    {
        const auto lum = luma(color);
        double lift = 1.02;
        if (lum < 0.18)
        {
            lift = 1.10;
        }
        else if (lum < 0.34)
        {
            lift = 1.06;
        }
        else if (lum > 0.58)
        {
            lift = 1.00;
        }
        color.r = clamp(color.r * lift, 0.018, 0.98);
        color.g = clamp(color.g * lift, 0.018, 0.98);
        color.b = clamp(color.b * lift, 0.018, 0.98);
        return infer_surface_material(color, floor_like);
    }

    auto compensate_projected_albedo_preserve_material(Color color, bool floor_like) -> Color
    {
        const auto roughness = clamp(color.roughness, 0.0, 1.0);
        const auto metallic = clamp(color.metallic, 0.0, 1.0);
        const auto lum = luma(color);
        double lift = 1.02;
        if (lum < 0.18)
        {
            lift = 1.10;
        }
        else if (lum < 0.34)
        {
            lift = 1.06;
        }
        else if (lum > 0.58)
        {
            lift = 1.00;
        }
        color.r = clamp(color.r * lift, 0.018, 0.98);
        color.g = clamp(color.g * lift, 0.018, 0.98);
        color.b = clamp(color.b * lift, 0.018, 0.98);
        color.roughness = floor_like ? clamp(std::max(roughness, 0.86), 0.86, 0.99) : roughness;
        color.metallic = floor_like ? clamp(metallic, 0.0, 0.12) : metallic;
        return color;
    }

    auto dilate_projection_texels(std::vector<ProjectionTexel>& texels, int width, int height, int passes) -> void
    {
        if (width <= 0 || height <= 0 || texels.empty())
        {
            return;
        }
        for (int pass = 0; pass < passes; ++pass)
        {
            auto next = texels;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const auto index = static_cast<size_t>(y * width + x);
                    if (texels[index].weight > 0.0001)
                    {
                        continue;
                    }

                    double r = 0.0;
                    double g = 0.0;
                    double b = 0.0;
                    double roughness = 0.0;
                    double metallic = 0.0;
                    double weight = 0.0;
                    bool floor_like = false;
                    for (int oy = -1; oy <= 1; ++oy)
                    {
                        for (int ox = -1; ox <= 1; ++ox)
                        {
                            if (ox == 0 && oy == 0)
                            {
                                continue;
                            }
                            const auto xx = x + ox;
                            const auto yy = y + oy;
                            if (xx < 0 || yy < 0 || xx >= width || yy >= height)
                            {
                                continue;
                            }
                            const auto& source = texels[static_cast<size_t>(yy * width + xx)];
                            if (source.weight <= 0.0001)
                            {
                                continue;
                            }
                            const auto inv = 1.0 / source.weight;
                            const auto w = (std::abs(ox) + std::abs(oy) == 1) ? 1.0 : 0.64;
                            r += source.r * inv * w;
                            g += source.g * inv * w;
                            b += source.b * inv * w;
                            roughness += source.roughness * inv * w;
                            metallic += source.metallic * inv * w;
                            weight += w;
                            floor_like = floor_like || source.floor_like;
                        }
                    }
                    if (weight <= 0.0001)
                    {
                        continue;
                    }

                    auto& target = next[index];
                    target.r = r / weight;
                    target.g = g / weight;
                    target.b = b / weight;
                    target.roughness = roughness / weight;
                    target.metallic = metallic / weight;
                    target.weight = 1.0;
                    target.priority = 1;
                    target.floor_like = floor_like;
                    target.dilated = true;
                }
            }
            texels.swap(next);
        }
    }

    auto flood_fill_projection_texel_holes(std::vector<ProjectionTexel>& texels, int width, int height) -> int
    {
        if (width <= 0 || height <= 0 || texels.empty())
        {
            return 0;
        }

        std::deque<int> queue{};
        std::vector<uint8_t> visited(texels.size(), 0);
        for (int index = 0; index < static_cast<int>(texels.size()); ++index)
        {
            if (texels[static_cast<size_t>(index)].weight > 0.0001)
            {
                visited[static_cast<size_t>(index)] = 1;
                queue.push_back(index);
            }
        }

        int filled = 0;
        const int offsets[4][2]{{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (!queue.empty())
        {
            const auto source_index = queue.front();
            queue.pop_front();
            const auto sx = source_index % width;
            const auto sy = source_index / width;
            const auto& source = texels[static_cast<size_t>(source_index)];
            if (source.weight <= 0.0001)
            {
                continue;
            }

            const auto inv = 1.0 / source.weight;
            for (const auto& offset : offsets)
            {
                const auto nx = sx + offset[0];
                const auto ny = sy + offset[1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                {
                    continue;
                }

                const auto neighbor_index = ny * width + nx;
                const auto neighbor_size = static_cast<size_t>(neighbor_index);
                if (neighbor_size >= visited.size() || visited[neighbor_size])
                {
                    continue;
                }

                auto& target = texels[neighbor_size];
                target.r = source.r * inv;
                target.g = source.g * inv;
                target.b = source.b * inv;
                target.roughness = source.roughness * inv;
                target.metallic = source.metallic * inv;
                target.weight = 1.0;
                target.priority = 1;
                target.floor_like = source.floor_like;
                target.dilated = true;
                visited[neighbor_size] = 1;
                queue.push_back(neighbor_index);
                ++filled;
            }
        }

        return filled;
    }

    auto projection_texel_painted(const ProjectionTexel& texel) -> bool
    {
        return texel.weight > 0.0001;
    }

    auto normalized_projection_texel(const ProjectionTexel& source, int priority, bool dilated) -> ProjectionTexel
    {
        ProjectionTexel out{};
        if (!projection_texel_painted(source))
        {
            return out;
        }
        const auto inv = 1.0 / source.weight;
        out.r = clamp(source.r * inv, 0.0, 1.0);
        out.g = clamp(source.g * inv, 0.0, 1.0);
        out.b = clamp(source.b * inv, 0.0, 1.0);
        out.roughness = clamp(source.roughness * inv, 0.0, 1.0);
        out.metallic = clamp(source.metallic * inv, 0.0, 1.0);
        out.weight = 1.0;
        out.priority = priority;
        out.floor_like = source.floor_like;
        out.dilated = dilated;
        return out;
    }

    auto edge_terminal_extrude_projection_texels(const std::vector<ProjectionTexel>& direct_texels,
                                                 int width,
                                                 int height,
                                                 std::vector<ProjectionTexel>& out_texels) -> TerminalExtrudeStats
    {
        TerminalExtrudeStats stats{};
        const auto texture_pixels = static_cast<size_t>(std::max(0, width)) *
                                    static_cast<size_t>(std::max(0, height));
        out_texels = direct_texels;
        if (width <= 0 || height <= 0 || direct_texels.size() != texture_pixels || texture_pixels == 0)
        {
            return stats;
        }

        std::vector<uint8_t> direct_mask(texture_pixels, 0);
        std::vector<int> edge_indices{};
        edge_indices.reserve(texture_pixels / 16);
        const auto is_direct_index = [&](int x, int y) -> bool {
            if (x < 0 || y < 0 || x >= width || y >= height)
            {
                return false;
            }
            return projection_texel_painted(direct_texels[static_cast<size_t>(y * width + x)]);
        };

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const auto index = static_cast<size_t>(y * width + x);
                if (!projection_texel_painted(direct_texels[index]))
                {
                    continue;
                }
                direct_mask[index] = 1;
                ++stats.direct_texels;
                ++stats.preserved_direct;
                if (x == 0 || y == 0 || x + 1 == width || y + 1 == height ||
                    !is_direct_index(x + 1, y) || !is_direct_index(x - 1, y) ||
                    !is_direct_index(x, y + 1) || !is_direct_index(x, y - 1))
                {
                    edge_indices.push_back(static_cast<int>(index));
                }
            }
        }
        stats.edge_texels = static_cast<int>(edge_indices.size());
        if (stats.direct_texels <= 0 || edge_indices.empty())
        {
            stats.preserved_original = static_cast<int>(texture_pixels) - stats.direct_texels;
            return stats;
        }

        constexpr int UnsetDistance = 1000000000;
        std::vector<int> best_source(texture_pixels, -1);
        std::vector<int> best_distance(texture_pixels, UnsetDistance);
        std::vector<int> best_priority(texture_pixels, -1);
        const auto consider_source = [&](size_t target, int source, int distance) {
            if (target >= texture_pixels || source < 0 || static_cast<size_t>(source) >= texture_pixels || direct_mask[target])
            {
                return;
            }
            const auto& source_texel = direct_texels[static_cast<size_t>(source)];
            if (!projection_texel_painted(source_texel))
            {
                return;
            }
            const auto priority = source_texel.priority;
            const bool replace = best_source[target] < 0 ||
                                 distance < best_distance[target] ||
                                 (distance == best_distance[target] && priority > best_priority[target]) ||
                                 (distance == best_distance[target] && priority == best_priority[target] &&
                                  source < best_source[target]);
            if (!replace)
            {
                return;
            }
            best_source[target] = source;
            best_distance[target] = distance;
            best_priority[target] = priority;
        };

        const auto worker_count = worker_count_for_items(texture_pixels);
        stats.worker_threads = worker_count;
        parallel_ranges(static_cast<size_t>(height), [&](size_t begin, size_t end, unsigned) {
            for (int y = static_cast<int>(begin); y < static_cast<int>(end); ++y)
            {
                int last_direct = -1;
                for (int x = 0; x < width; ++x)
                {
                    const auto index = static_cast<size_t>(y * width + x);
                    if (direct_mask[index])
                    {
                        last_direct = static_cast<int>(index);
                    }
                    else if (last_direct >= 0)
                    {
                        consider_source(index, last_direct, x - (last_direct % width));
                    }
                }

                last_direct = -1;
                for (int x = width - 1; x >= 0; --x)
                {
                    const auto index = static_cast<size_t>(y * width + x);
                    if (direct_mask[index])
                    {
                        last_direct = static_cast<int>(index);
                    }
                    else if (last_direct >= 0)
                    {
                        consider_source(index, last_direct, (last_direct % width) - x);
                    }
                }
            }
        });
        for (int x = 0; x < width; ++x)
        {
            int last_direct = -1;
            for (int y = 0; y < height; ++y)
            {
                const auto index = static_cast<size_t>(y * width + x);
                if (direct_mask[index])
                {
                    last_direct = static_cast<int>(index);
                }
                else if (last_direct >= 0)
                {
                    consider_source(index, last_direct, y - (last_direct / width));
                }
            }

            last_direct = -1;
            for (int y = height - 1; y >= 0; --y)
            {
                const auto index = static_cast<size_t>(y * width + x);
                if (direct_mask[index])
                {
                    last_direct = static_cast<int>(index);
                }
                else if (last_direct >= 0)
                {
                    consider_source(index, last_direct, (last_direct / width) - y);
                }
            }
        }

        for (size_t index = 0; index < texture_pixels; ++index)
        {
            if (direct_mask[index] || best_source[index] < 0)
            {
                continue;
            }
            out_texels[index] =
                normalized_projection_texel(direct_texels[static_cast<size_t>(best_source[index])], 1, true);
            ++stats.extruded_texels;
        }

        std::deque<std::pair<int, int>> queue{};
        std::vector<uint8_t> visited(texture_pixels, 0);
        for (const auto edge_index : edge_indices)
        {
            if (edge_index < 0 || static_cast<size_t>(edge_index) >= texture_pixels)
            {
                continue;
            }
            visited[static_cast<size_t>(edge_index)] = 1;
            queue.emplace_back(edge_index, edge_index);
        }
        const int offsets[4][2]{{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (!queue.empty())
        {
            const auto [index, source] = queue.front();
            queue.pop_front();
            const auto x = index % width;
            const auto y = index / width;
            for (const auto& offset : offsets)
            {
                const auto nx = x + offset[0];
                const auto ny = y + offset[1];
                if (nx < 0 || ny < 0 || nx >= width || ny >= height)
                {
                    continue;
                }
                const auto neighbor = ny * width + nx;
                const auto neighbor_index = static_cast<size_t>(neighbor);
                if (neighbor_index >= texture_pixels || visited[neighbor_index])
                {
                    continue;
                }
                visited[neighbor_index] = 1;
                if (!projection_texel_painted(out_texels[neighbor_index]))
                {
                    out_texels[neighbor_index] =
                        normalized_projection_texel(direct_texels[static_cast<size_t>(source)], 1, true);
                    ++stats.fallback_extruded_texels;
                }
                queue.emplace_back(neighbor, source);
            }
        }

        for (const auto& texel : out_texels)
        {
            if (!projection_texel_painted(texel))
            {
                ++stats.preserved_original;
            }
        }
        return stats;
    }

    auto sample_projection_texel_color(const std::vector<ProjectionTexel>& texels,
                                       int width,
                                       int height,
                                       double u,
                                       double v) -> std::optional<Color>
    {
        if (width <= 0 || height <= 0 || texels.empty())
        {
            return std::nullopt;
        }
        const auto x = std::min(width - 1,
                                std::max(0, static_cast<int>(clamp(u, 0.0, 0.999999) *
                                                            static_cast<double>(width))));
        const auto y = std::min(height - 1,
                                std::max(0, static_cast<int>(clamp(v, 0.0, 0.999999) *
                                                            static_cast<double>(height))));
        const auto index = static_cast<size_t>(y * width + x);
        if (index >= texels.size() || texels[index].weight <= 0.0001)
        {
            return std::nullopt;
        }
        const auto inv = 1.0 / texels[index].weight;
        Color out{};
        out.r = clamp(texels[index].r * inv, 0.0, 1.0);
        out.g = clamp(texels[index].g * inv, 0.0, 1.0);
        out.b = clamp(texels[index].b * inv, 0.0, 1.0);
        out.roughness = clamp(texels[index].roughness * inv, 0.0, 1.0);
        out.metallic = clamp(texels[index].metallic * inv, 0.0, 1.0);
        return out;
    }

    auto percentile_sorted(std::vector<double> values, double percentile) -> double
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const auto clamped = clamp(percentile, 0.0, 1.0);
        const auto index = std::min(values.size() - 1,
                                    static_cast<size_t>(std::floor(clamped * static_cast<double>(values.size() - 1))));
        return values[index];
    }

    auto compute_projection_audit(const std::vector<ScreenHitSample>& samples,
                                  const std::vector<ProjectionTexel>& texels,
                                  int texture_width,
                                  int texture_height,
                                  Unreal::UObject* controller,
                                  const ViewportInfo& viewport,
                                  bool export_samples) -> ProjectionAuditResult
    {
        ProjectionAuditResult result{};
        result.body_mask_pixels = static_cast<int>(samples.size());
        result.audit_samples = static_cast<int>(samples.size());
        if (samples.empty() || texture_width <= 0 || texture_height <= 0 || texels.empty())
        {
            result.reason = STR("projection_audit_no_samples");
            return result;
        }

        std::vector<ProjectionAuditBin> bins(static_cast<size_t>(ProjectionAuditUvResolution) *
                                             static_cast<size_t>(ProjectionAuditUvResolution));
        std::vector<double> reproject_errors{};
        std::vector<double> prediction_errors{};
        reproject_errors.reserve(samples.size());
        prediction_errors.reserve(samples.size());

        struct AuditSampleRecord
        {
            int index{0};
            double screen_x{0.0};
            double screen_y{0.0};
            double u{0.0};
            double v{0.0};
            double target_r{0.0};
            double target_g{0.0};
            double target_b{0.0};
            double predicted_r{0.0};
            double predicted_g{0.0};
            double predicted_b{0.0};
            double predicted_error{0.0};
            double reproject_error{-1.0};
        };
        std::vector<AuditSampleRecord> export_records{};
        if (export_samples)
        {
            export_records.reserve(samples.size());
        }

        double prediction_error_sum = 0.0;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const auto& sample = samples[i];
            const auto expected_x = clamp(sample.nx, 0.0, 0.999999) * static_cast<double>(std::max(1, viewport.width));
            const auto expected_y = clamp(sample.ny, 0.0, 0.999999) * static_cast<double>(std::max(1, viewport.height));

            const auto bx = std::min(ProjectionAuditUvResolution - 1,
                                     std::max(0, static_cast<int>(clamp(sample.u, 0.0, 0.999999) *
                                                                 static_cast<double>(ProjectionAuditUvResolution))));
            const auto by = std::min(ProjectionAuditUvResolution - 1,
                                     std::max(0, static_cast<int>(clamp(sample.v, 0.0, 0.999999) *
                                                                 static_cast<double>(ProjectionAuditUvResolution))));
            auto& bin = bins[static_cast<size_t>(by * ProjectionAuditUvResolution + bx)];
            ++bin.count;
            bin.r_sum += sample.color.r;
            bin.g_sum += sample.color.g;
            bin.b_sum += sample.color.b;
            bin.rgb_sq_sum += sample.color.r * sample.color.r +
                              sample.color.g * sample.color.g +
                              sample.color.b * sample.color.b;
            bin.min_screen_x = std::min(bin.min_screen_x, expected_x);
            bin.min_screen_y = std::min(bin.min_screen_y, expected_y);
            bin.max_screen_x = std::max(bin.max_screen_x, expected_x);
            bin.max_screen_y = std::max(bin.max_screen_y, expected_y);

            double reproject_error = -1.0;
            const auto projected = project_world_location_to_screen(controller, sample.world_position, false);
            if (projected.ok)
            {
                const auto dx = projected.x - expected_x;
                const auto dy = projected.y - expected_y;
                reproject_error = std::sqrt(dx * dx + dy * dy);
                reproject_errors.push_back(reproject_error);
                ++result.projected_ok;
            }

            auto predicted = sample_projection_texel_color(texels, texture_width, texture_height, sample.u, sample.v);
            double predicted_error = 1.0;
            Color predicted_color{};
            if (predicted)
            {
                predicted_color = *predicted;
                predicted_error = color_distance_rgb(predicted_color, sample.color);
            }
            prediction_errors.push_back(predicted_error);
            prediction_error_sum += predicted_error;
            if (predicted_error > ProjectionAuditBadPixelDistance)
            {
                ++result.predicted_bad_pixels;
            }

            if (export_samples)
            {
                AuditSampleRecord record{};
                record.index = static_cast<int>(i);
                record.screen_x = expected_x;
                record.screen_y = expected_y;
                record.u = sample.u;
                record.v = sample.v;
                record.target_r = sample.color.r;
                record.target_g = sample.color.g;
                record.target_b = sample.color.b;
                record.predicted_r = predicted_color.r;
                record.predicted_g = predicted_color.g;
                record.predicted_b = predicted_color.b;
                record.predicted_error = predicted_error;
                record.reproject_error = reproject_error;
                export_records.push_back(record);
            }
        }

        double weighted_variance_sum = 0.0;
        int variance_pixels = 0;
        for (const auto& bin : bins)
        {
            if (bin.count <= 0)
            {
                continue;
            }
            ++result.uv_texels;
            const auto inv = 1.0 / static_cast<double>(bin.count);
            const auto mean_r = bin.r_sum * inv;
            const auto mean_g = bin.g_sum * inv;
            const auto mean_b = bin.b_sum * inv;
            const auto variance = std::max(0.0,
                                           bin.rgb_sq_sum * inv -
                                               (mean_r * mean_r + mean_g * mean_g + mean_b * mean_b));
            const auto variance_root = std::sqrt(variance);
            weighted_variance_sum += variance_root * static_cast<double>(bin.count);
            variance_pixels += bin.count;
            const auto screen_span = std::sqrt((bin.max_screen_x - bin.min_screen_x) * (bin.max_screen_x - bin.min_screen_x) +
                                               (bin.max_screen_y - bin.min_screen_y) * (bin.max_screen_y - bin.min_screen_y));
            if (bin.count > 1 && (variance_root > ProjectionAuditMaxSameUvColorVariance || screen_span > 18.0))
            {
                ++result.uv_conflict_texels;
                result.uv_conflict_pixels += bin.count;
            }
        }

        result.reproject_error_p50 = median_value(reproject_errors);
        result.reproject_error_p95 = percentile_sorted(reproject_errors, 0.95);
        result.reproject_error_max = percentile_sorted(reproject_errors, 1.0);
        result.predicted_screen_mae = samples.empty() ? 1.0 : prediction_error_sum / static_cast<double>(samples.size());
        result.predicted_screen_p95 = percentile_sorted(prediction_errors, 0.95);
        result.predicted_bad_pixel_pct = samples.empty()
                                             ? 1.0
                                             : static_cast<double>(result.predicted_bad_pixels) / static_cast<double>(samples.size());
        result.uv_conflict_pixels_pct = samples.empty()
                                            ? 1.0
                                            : static_cast<double>(result.uv_conflict_pixels) / static_cast<double>(samples.size());
        result.same_uv_color_variance = variance_pixels > 0
                                            ? weighted_variance_sum / static_cast<double>(variance_pixels)
                                            : 0.0;

        if (result.projected_ok <= 0)
        {
            result.reason = STR("projection_audit_reproject_unavailable");
        }
        else if (result.reproject_error_p95 > ProjectionAuditMaxReprojectP95Px)
        {
            result.reason = STR("projection_audit_reproject_error");
        }
        else if (result.uv_conflict_pixels_pct > ProjectionAuditMaxUvConflictPixelsPct)
        {
            result.reason = STR("projection_audit_uv_conflict");
        }
        else if (result.same_uv_color_variance > ProjectionAuditMaxSameUvColorVariance)
        {
            result.reason = STR("projection_audit_same_uv_color_variance");
        }
        else if (result.predicted_screen_mae > ProjectionAuditMaxPredictedScreenMae)
        {
            result.reason = STR("projection_audit_predicted_screen_mae");
        }
        else if (result.predicted_bad_pixel_pct > ProjectionAuditMaxPredictedBadPixelPct)
        {
            result.reason = STR("projection_audit_predicted_bad_pixels");
        }
        else
        {
            result.paint_would_apply = true;
            result.reason = STR("projection_audit_pass");
        }

        if (export_samples)
        {
            std::sort(export_records.begin(), export_records.end(), [](const AuditSampleRecord& a, const AuditSampleRecord& b) {
                return a.predicted_error > b.predicted_error;
            });
            const auto limit = std::min(static_cast<int>(export_records.size()), ProjectionAuditExportSampleLimit);
            for (int i = 0; i < limit; ++i)
            {
                const auto& record = export_records[static_cast<size_t>(i)];
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} projection_export_sample rank={} sample_index={} screen=({}, {}) hit_uv=({}, {}) target_rgb=({}, {}, {}) predicted_rgb=({}, {}, {}) predicted_error={} reproject_error={}\n"),
                    ModTag,
                    i,
                    record.index,
                    record.screen_x,
                    record.screen_y,
                    record.u,
                    record.v,
                    record.target_r,
                    record.target_g,
                    record.target_b,
                    record.predicted_r,
                    record.predicted_g,
                    record.predicted_b,
                    record.predicted_error,
                    record.reproject_error);
            }
        }

        return result;
    }

    auto sample_projection_view(Unreal::UObject* pawn,
                                const ProjectionFrame& frame,
                                int grid_width,
                                int grid_height,
                                bool primary,
                                std::vector<ProjectionTexel>& texels,
                                int texture_width,
                                int texture_height,
                                std::vector<Sample>& projected_samples,
                                ViewProjectionStats& stats,
                                ProbeState& state) -> void
    {
        const auto look_at = add(frame.eye, mul(frame.forward, 1000.0));
        auto capture_colors = capture_background_grid(pawn, frame.eye, look_at, grid_width, grid_height, state, frame.fov_degrees);
        const auto fov_tan = std::tan((clamp(frame.fov_degrees, 10.0, 150.0) * Pi / 180.0) * 0.5);
        const auto aspect = static_cast<double>(grid_width) / static_cast<double>(std::max(1, grid_height));
        const auto pawn_location = call_no_params_return_vector(pawn, STR("K2_GetActorLocation")).value_or(vec(0.0, 0.0, 0.0));
        for (int y = 0; y < grid_height; ++y)
        {
            for (int x = 0; x < grid_width; ++x)
            {
                const auto sx = (static_cast<double>(x) / static_cast<double>(std::max(1, grid_width - 1))) * 2.0 - 1.0;
                const auto sy = 1.0 - (static_cast<double>(y) / static_cast<double>(std::max(1, grid_height - 1))) * 2.0;
                const auto ray_dir = normalize(add(add(frame.forward, mul(frame.right, sx * fov_tan * aspect)),
                                                   mul(frame.up, sy * fov_tan)));
                const auto target = add(frame.eye, mul(ray_dir, 5600.0));
                auto body_hit = execute_body_uv_trace(pawn, pawn_location, frame.eye, target, &stats.trace_debug);
                if (!body_hit.hit)
                {
                    ++stats.rejected_body_hits;
                    continue;
                }
                ++stats.body_hits;
                if (body_hit.accepted_by_owner)
                {
                    ++stats.owner_body_hits;
                }
                if (body_hit.accepted_by_spatial_fallback)
                {
                    ++stats.spatial_fallback_body_hits;
                }
                ++stats.uv_hits;

                Color material_hint{0.34, 0.36, 0.32, 0.94, 0.0};
                bool floor_like = false;
                const auto background_start = add(body_hit.location, mul(ray_dir, 26.0));
                const auto background_end = add(body_hit.location, mul(ray_dir, 5600.0));
                TraceHit background_hit{};
                for (const auto channel : {0, 1, 2, 3, 4, 5, 6})
                {
                    background_hit = execute_line_trace(pawn, background_start, background_end, true, channel);
                    if (background_hit.hit)
                    {
                        break;
                    }
                }
                if (background_hit.hit)
                {
                    ++stats.background_hits;
                    material_hint = classify_background(background_hit.actor, background_hit.component, background_hit.location);
                    floor_like = floor_like || is_floor_like_object(background_hit.actor, background_hit.component);
                }

                const auto floor_start = add(body_hit.location, vec(0.0, 0.0, 24.0));
                const auto floor_end = add(body_hit.location, vec(0.0, 0.0, -520.0));
                auto floor_hit = execute_line_trace(pawn, floor_start, floor_end, true, 0, false);
                const auto floor_distance = floor_hit.hit ? body_hit.location.Z() - floor_hit.location.Z() : 100000.0;
                if (floor_hit.hit && floor_distance >= -8.0 && floor_distance <= 85.0 &&
                    is_floor_like_object(floor_hit.actor, floor_hit.component))
                {
                    floor_like = true;
                    if (!background_hit.hit)
                    {
                        material_hint = classify_background(floor_hit.actor, floor_hit.component, floor_hit.location);
                    }
                }

                const auto capture_index = static_cast<size_t>(y * grid_width + x);
                Color color = material_hint;
                if (capture_index < capture_colors.size() && capture_colors[capture_index])
                {
                    color = sanitize_background_color(*capture_colors[capture_index], material_hint);
                    color.roughness = material_hint.roughness;
                    color.metallic = material_hint.metallic;
                    if (primary)
                    {
                        ++stats.primary_view_pixels;
                    }
                    else
                    {
                        ++stats.aux_view_pixels;
                    }
                }
                else if (floor_like)
                {
                    color = material_hint;
                }
                else
                {
                    continue;
                }
                if (floor_like)
                {
                    ++stats.floor_view_pixels;
                    color.roughness = clamp(color.roughness, 0.86, 0.98);
                }
                color = compensate_projected_albedo(color, floor_like);

                const auto priority = primary ? 4 : (floor_like ? 3 : 2);
                const auto base_weight = primary ? (floor_like ? 28.0 : 24.0) : (floor_like ? 7.0 : 3.0);
                const auto u = clamp(body_hit.u, 0.0, 0.999999);
                const auto v = clamp(body_hit.v, 0.0, 0.999999);
                splat_projection_texel(texels, texture_width, texture_height, u, v, color, base_weight, priority, floor_like);

                Sample sample{};
                sample.world_position = body_hit.location;
                sample.u = u;
                sample.v = v;
                sample.color = color;
                sample.weight = base_weight;
                projected_samples.push_back(sample);
                ++stats.projected_samples;
            }
        }
    }

    auto collect_view_background_samples(Unreal::UObject* pawn, const ProjectionFrame& frame, ProbeState& state)
        -> std::vector<Sample>
    {
        constexpr int grid = 48;
        const auto look_at = add(frame.eye, mul(frame.forward, 1000.0));
        auto colors = capture_background_grid(pawn, frame.eye, look_at, grid, grid, state, frame.fov_degrees);
        std::vector<Sample> samples{};
        samples.reserve(colors.size());
        const Color neutral_hint{0.34, 0.36, 0.32, 0.94, 0.0};
        for (int y = 0; y < grid; ++y)
        {
            for (int x = 0; x < grid; ++x)
            {
                const auto index = static_cast<size_t>(y * grid + x);
                if (index >= colors.size() || !colors[index])
                {
                    continue;
                }
                Sample sample{};
                sample.u = (static_cast<double>(x) + 0.5) / static_cast<double>(grid);
                sample.v = (static_cast<double>(y) + 0.5) / static_cast<double>(grid);
                sample.color = sanitize_background_color(*colors[index], neutral_hint);
                sample.color = infer_surface_material(sample.color, false);
                sample.weight = 1.0;
                samples.push_back(sample);
            }
        }
        return samples;
    }

    auto collect_screen_hit_samples(Unreal::UObject* component,
                                    Unreal::UObject* pawn,
                                    Unreal::UObject* mesh,
                                    Unreal::UObject* controller,
                                    const ViewportInfo& viewport,
                                    const std::vector<std::optional<Color>>& capture_colors,
                                    int color_grid_width,
                                    int color_grid_height,
                                    int grid_width,
                                    int grid_height,
                                    bool normalized_coords,
                                    ProbeState& state,
                                    ScreenHitCollectionStats& stats,
                                    double min_nx = 0.0,
                                    double max_nx = 1.0,
                                    double min_ny = 0.0,
                                    double max_ny = 1.0,
                                    int target_hits = 0,
                                    int hard_max_attempts = 0,
                                    bool enable_floor_trace = true) -> std::vector<ScreenHitSample>
    {
        std::vector<ScreenHitSample> samples{};
        samples.reserve(static_cast<size_t>(grid_width * grid_height));
        const Color neutral_hint{0.34, 0.36, 0.32, 0.94, 0.0};
        for (int y = 0; y < grid_height; ++y)
        {
            for (int x = 0; x < grid_width; ++x)
            {
                if (state.cancelled)
                {
                    return samples;
                }
                if (hard_max_attempts > 0 && stats.attempts >= hard_max_attempts)
                {
                    return samples;
                }
                const auto local_nx = (static_cast<double>(x) + 0.5) / static_cast<double>(std::max(1, grid_width));
                const auto local_ny = (static_cast<double>(y) + 0.5) / static_cast<double>(std::max(1, grid_height));
                const auto nx = clamp(min_nx + local_nx * std::max(0.0, max_nx - min_nx), 0.0, 1.0);
                const auto ny = clamp(min_ny + local_ny * std::max(0.0, max_ny - min_ny), 0.0, 1.0);
                const auto screen_x = normalized_coords ? nx : nx * static_cast<double>(viewport.width);
                const auto screen_y = normalized_coords ? ny : ny * static_cast<double>(viewport.height);
                ++stats.attempts;
                auto hit = hit_test_at_screen_position(component, mesh, controller, screen_x, screen_y, true);
                if (hit.params_ok)
                {
                    ++stats.params_ok;
                }
                if (!hit.success)
                {
                    ++stats.failures;
                    if (stats.first_failure.empty())
                    {
                        stats.first_failure = hit.failure.empty() ? STR("screen_hit_unsuccessful") : hit.failure;
                    }
                    continue;
                }
                ++stats.hit_success;
                if (!hit.has_uv)
                {
                    ++stats.failures;
                    if (stats.first_failure.empty())
                    {
                        stats.first_failure = STR("screen_hit_uv_unavailable");
                    }
                    continue;
                }

                ++stats.hit_uv_count;
                stats.min_u = std::min(stats.min_u, hit.u);
                stats.min_v = std::min(stats.min_v, hit.v);
                stats.max_u = std::max(stats.max_u, hit.u);
                stats.max_v = std::max(stats.max_v, hit.v);
                stats.min_nx = std::min(stats.min_nx, nx);
                stats.min_ny = std::min(stats.min_ny, ny);
                stats.max_nx = std::max(stats.max_nx, nx);
                stats.max_ny = std::max(stats.max_ny, ny);
                if (!stats.has_world_bounds)
                {
                    stats.min_world = hit.world_position;
                    stats.max_world = hit.world_position;
                    stats.has_world_bounds = true;
                }
                else
                {
                    stats.min_world = vec(std::min(stats.min_world.X(), hit.world_position.X()),
                                          std::min(stats.min_world.Y(), hit.world_position.Y()),
                                          std::min(stats.min_world.Z(), hit.world_position.Z()));
                    stats.max_world = vec(std::max(stats.max_world.X(), hit.world_position.X()),
                                          std::max(stats.max_world.Y(), hit.world_position.Y()),
                                          std::max(stats.max_world.Z(), hit.world_position.Z()));
                }

                Color material_hint{0.34, 0.36, 0.32, 0.94, 0.0};
                bool floor_like = false;
                if (enable_floor_trace)
                {
                    const auto floor_start = add(hit.world_position, vec(0.0, 0.0, 24.0));
                    const auto floor_end = add(hit.world_position, vec(0.0, 0.0, -520.0));
                    TraceHit floor_hit{};
                    for (const auto channel : {0, 1, 2, 3, 4, 5, 6})
                    {
                        floor_hit = execute_line_trace(pawn, floor_start, floor_end, true, channel, false);
                        if (floor_hit.hit)
                        {
                            break;
                        }
                    }
                    const auto floor_distance = floor_hit.hit ? hit.world_position.Z() - floor_hit.location.Z() : 100000.0;
                    if (floor_hit.hit && floor_distance >= -12.0 && floor_distance <= 95.0 &&
                        is_floor_like_object(floor_hit.actor, floor_hit.component))
                    {
                        ++stats.floor_hits;
                        floor_like = true;
                        material_hint = classify_background(floor_hit.actor, floor_hit.component, floor_hit.location);
                    }
                }

                const auto capture_x = std::min(std::max(0, color_grid_width - 1),
                                                std::max(0, static_cast<int>(nx * static_cast<double>(color_grid_width))));
                const auto capture_y = std::min(std::max(0, color_grid_height - 1),
                                                std::max(0, static_cast<int>(ny * static_cast<double>(color_grid_height))));
                const auto capture_index = static_cast<size_t>(capture_y * color_grid_width + capture_x);
                if (capture_index >= capture_colors.size() || !capture_colors[capture_index])
                {
                    ++stats.failures;
                    if (stats.first_failure.empty())
                    {
                        stats.first_failure = STR("screen_hit_background_capture_missing");
                    }
                    continue;
                }
                Color color = *capture_colors[capture_index];
                color.roughness = floor_like ? clamp(material_hint.roughness, 0.86, 0.99)
                                             : clamp(color.roughness, 0.40, 0.99);
                color.metallic = material_hint.metallic;
                color = compensate_projected_albedo(color, floor_like);

                ScreenHitSample sample{};
                sample.screen_x = screen_x;
                sample.screen_y = screen_y;
                sample.nx = nx;
                sample.ny = ny;
                sample.u = clamp(hit.u, 0.0, 0.999999);
                sample.v = clamp(hit.v, 0.0, 0.999999);
                sample.world_position = hit.world_position;
                sample.normal = hit.normal;
                sample.color = color;
                sample.floor_like = floor_like;
                samples.push_back(sample);
                ++stats.color_samples;
                if (target_hits > 0 && stats.hit_uv_count >= target_hits)
                {
                    return samples;
                }
            }
        }
        return samples;
    }

    auto log_screen_hit_stats(const CharType* label, const CharType* coord_mode, const ScreenHitCollectionStats& stats)
        -> void
    {
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} {} coord_mode={} attempts={} params_ok={} screen_hit_success={} hit_uv_count={} floor_hits={} color_samples={} failures={} uv_min=({}, {}) uv_max=({}, {}) screen_bbox=({}, {})-({}, {}) world_min=({}, {}, {}) world_max=({}, {}, {}) first_failure={}\n"),
            ModTag,
            label,
            coord_mode,
            stats.attempts,
            stats.params_ok,
            stats.hit_success,
            stats.hit_uv_count,
            stats.floor_hits,
            stats.color_samples,
            stats.failures,
            stats.hit_uv_count > 0 ? stats.min_u : 0.0,
            stats.hit_uv_count > 0 ? stats.min_v : 0.0,
            stats.hit_uv_count > 0 ? stats.max_u : 0.0,
            stats.hit_uv_count > 0 ? stats.max_v : 0.0,
            stats.hit_uv_count > 0 ? stats.min_nx : 0.0,
            stats.hit_uv_count > 0 ? stats.min_ny : 0.0,
            stats.hit_uv_count > 0 ? stats.max_nx : 0.0,
            stats.hit_uv_count > 0 ? stats.max_ny : 0.0,
            stats.has_world_bounds ? stats.min_world.X() : 0.0,
            stats.has_world_bounds ? stats.min_world.Y() : 0.0,
            stats.has_world_bounds ? stats.min_world.Z() : 0.0,
            stats.has_world_bounds ? stats.max_world.X() : 0.0,
            stats.has_world_bounds ? stats.max_world.Y() : 0.0,
            stats.has_world_bounds ? stats.max_world.Z() : 0.0,
            stats.first_failure.empty() ? STR("<none>") : stats.first_failure);
    }

    auto apply_screen_hit_import_cloak(Unreal::UObject* component,
                                       Unreal::UObject* pawn,
                                       ProbeState& state,
                                       bool audit_only = false,
                                       bool export_audit_samples = false) -> bool
    {
        const auto total_start = SteadyClock::now();
        PhaseTimings timings{};
        state.queued_strokes = 0;
        state.success = 0;
        state.failures = 0;
        state.paint_world_success = 0;
        state.paint_uv_success = 0;
        state.commit_calls = 0;
        state.body_trace_hits = 0;
        state.background_trace_hits = 0;
        state.visible_samples = 0;
        state.uv_hits = 0;
        state.background_pixels = 0;
        state.atlas_bins = 0;
        state.verified_visible_backend = false;
        state.verified_paint_channel = audit_only ? PaintChannelUnknown : 0;
        state.verified_paint_function = audit_only ? STR("projection_audit_no_import")
                                                   : STR("ImportChannelFromBytes");

        auto* controller = find_player_controller_for_pawn(pawn);
        auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
        auto viewport = get_viewport_info(controller);
        auto frame = make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0);
        if (!frame)
        {
            frame = make_projection_frame(pawn, 0.0, 0.0);
        }
        if (!component || !pawn || !controller || !mesh || !frame)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_hit_prereq_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen hit refused component={} pawn={} controller={} mesh={} frame={}\n"),
                ModTag,
                component ? component->GetFullName() : STR("<null>"),
                pawn ? pawn->GetFullName() : STR("<null>"),
                controller ? controller->GetFullName() : STR("<null>"),
                mesh ? mesh->GetFullName() : STR("<null>"),
                frame ? 1 : 0);
            return false;
        }

        const auto export_start = SteadyClock::now();
        const auto albedo_before = export_channel_bytes(component, 0);
        const auto metallic_before = export_channel_bytes(component, 1);
        const auto roughness_before = export_channel_bytes(component, 2);
        timings.export_ms = elapsed_ms_since(export_start);
        if (!rgba_buffer_ready(albedo_before) || !rgba_buffer_ready(metallic_before) || !rgba_buffer_ready(roughness_before))
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_hit_export_failed");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen hit refused export_ok=({}, {}, {}) failures=({}, {}, {})\n"),
                ModTag,
                albedo_before.ok ? 1 : 0,
                metallic_before.ok ? 1 : 0,
                roughness_before.ok ? 1 : 0,
                albedo_before.failure.empty() ? STR("<none>") : albedo_before.failure,
                metallic_before.failure.empty() ? STR("<none>") : metallic_before.failure,
                roughness_before.failure.empty() ? STR("<none>") : roughness_before.failure);
            return false;
        }

        const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
        std::vector<std::optional<Color>> dummy_colors(1, Color{0.34, 0.36, 0.32, 0.94, 0.0});

        const auto coarse_start = SteadyClock::now();
        ScreenHitCollectionStats pixel_stats{};
        auto pixel_samples = collect_screen_hit_samples(component,
                                                        pawn,
                                                        mesh,
                                                        controller,
                                                        viewport,
                                                        dummy_colors,
                                                        1,
                                                        1,
                                                        ScreenProjectionGridX,
                                                        ScreenProjectionGridY,
                                                        false,
                                                        state,
                                                        pixel_stats,
                                                        0.0,
                                                        1.0,
                                                        0.0,
                                                        1.0,
                                                        0,
                                                        0,
                                                        false);
        timings.coarse_hit_ms = elapsed_ms_since(coarse_start);
        log_screen_hit_stats(STR("play screen_hit_collect"), STR("viewport_pixels"), pixel_stats);

        const auto dense_start = SteadyClock::now();
        if (pixel_stats.hit_uv_count >= MinScreenHitUvSamples &&
            pixel_stats.hit_uv_count < TargetScreenHitUvSamples &&
            pixel_stats.max_nx > pixel_stats.min_nx &&
            pixel_stats.max_ny > pixel_stats.min_ny)
        {
            const auto margin_x = std::max(0.035, (pixel_stats.max_nx - pixel_stats.min_nx) * 0.22);
            const auto margin_y = std::max(0.045, (pixel_stats.max_ny - pixel_stats.min_ny) * 0.22);
            const auto refine_min_nx = clamp(pixel_stats.min_nx - margin_x, 0.0, 1.0);
            const auto refine_max_nx = clamp(pixel_stats.max_nx + margin_x, 0.0, 1.0);
            const auto refine_min_ny = clamp(pixel_stats.min_ny - margin_y, 0.0, 1.0);
            const auto refine_max_ny = clamp(pixel_stats.max_ny + margin_y, 0.0, 1.0);
            const auto refine_width_px = (refine_max_nx - refine_min_nx) * static_cast<double>(viewport.width);
            const auto refine_height_px = (refine_max_ny - refine_min_ny) * static_cast<double>(viewport.height);
            auto refine_grid_width = std::min(
                ScreenProjectionRefineMaxGrid,
                std::max(ScreenProjectionRefineMinGrid,
                         static_cast<int>(std::ceil(refine_width_px / ScreenProjectionRefineTargetStepPx))));
            auto refine_grid_height = std::min(
                ScreenProjectionRefineMaxGrid,
                std::max(ScreenProjectionRefineMinGrid,
                         static_cast<int>(std::ceil(refine_height_px / ScreenProjectionRefineTargetStepPx))));
            const auto refine_attempts = refine_grid_width * refine_grid_height;
            if (refine_attempts > DenseScreenHitHardMaxAttempts)
            {
                const auto scale = std::sqrt(static_cast<double>(DenseScreenHitHardMaxAttempts) /
                                             static_cast<double>(std::max(1, refine_attempts)));
                refine_grid_width = std::max(ScreenProjectionRefineMinGrid, static_cast<int>(std::floor(refine_grid_width * scale)));
                refine_grid_height = std::max(ScreenProjectionRefineMinGrid, static_cast<int>(std::floor(refine_grid_height * scale)));
            }

            ScreenHitCollectionStats refined_stats{};
            auto refined_samples = collect_screen_hit_samples(component,
                                                              pawn,
                                                              mesh,
                                                              controller,
                                                              viewport,
                                                              dummy_colors,
                                                              1,
                                                              1,
                                                              refine_grid_width,
                                                              refine_grid_height,
                                                              false,
                                                              state,
                                                              refined_stats,
                                                              refine_min_nx,
                                                              refine_max_nx,
                                                              refine_min_ny,
                                                              refine_max_ny,
                                                              0,
                                                              DenseScreenHitHardMaxAttempts,
                                                              false);
            log_screen_hit_stats(STR("play screen_hit_collect"), STR("viewport_pixels_refined"), refined_stats);
            if (refined_stats.hit_uv_count > pixel_stats.hit_uv_count)
            {
                pixel_stats = refined_stats;
                pixel_samples = std::move(refined_samples);
            }
        }
        timings.dense_hit_ms = elapsed_ms_since(dense_start);

        ScreenHitCollectionStats normalized_stats{};
        std::vector<ScreenHitSample> normalized_samples{};
        if (pixel_stats.hit_uv_count < MinScreenHitUvSamples)
        {
            normalized_samples = collect_screen_hit_samples(component,
                                                           pawn,
                                                           mesh,
                                                           controller,
                                                           viewport,
                                                           dummy_colors,
                                                           1,
                                                           1,
                                                           ScreenProjectionGridX,
                                                           ScreenProjectionGridY,
                                                           true,
                                                           state,
                                                           normalized_stats,
                                                           0.0,
                                                           1.0,
                                                           0.0,
                                                           1.0,
                                                           0,
                                                           0,
                                                           false);
            log_screen_hit_stats(STR("play screen_hit_collect"), STR("normalized_0_1"), normalized_stats);
        }

        const auto use_normalized = normalized_stats.hit_uv_count > pixel_stats.hit_uv_count;
        const auto& chosen_stats = use_normalized ? normalized_stats : pixel_stats;
        auto& chosen_samples = use_normalized ? normalized_samples : pixel_samples;
        state.visible_samples = chosen_stats.hit_success;
        state.uv_hits = chosen_stats.hit_uv_count;
        state.body_trace_hits = chosen_stats.hit_success;
        state.background_trace_hits = chosen_stats.floor_hits;
        state.atlas_bins = static_cast<int>(chosen_samples.size());
        state.uv_mapping_ready = chosen_stats.hit_uv_count >= MinScreenHitUvSamples;

        if (chosen_stats.hit_uv_count < MinScreenHitUvSamples || chosen_samples.empty())
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_hit_uv_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen hit refused coord_mode={} screen_hit_success={} hit_uv_count={} samples={} min_required={} first_failure={}\n"),
                ModTag,
                use_normalized ? STR("normalized_0_1") : STR("viewport_pixels"),
                chosen_stats.hit_success,
                chosen_stats.hit_uv_count,
                static_cast<int>(chosen_samples.size()),
                MinScreenHitUvSamples,
                chosen_stats.first_failure.empty() ? STR("<none>") : chosen_stats.first_failure);
            return false;
        }
        if (chosen_stats.hit_uv_count < MinQualityScreenHitUvSamples)
        {
            state.failures = 1;
            state.last_failure = STR("play_dense_uv_quality_insufficient");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen hit refused quality coord_mode={} dense_attempts={} dense_hit_uv_count={} samples={} min_quality={} hard_max_attempts={} first_failure={}\n"),
                ModTag,
                use_normalized ? STR("normalized_0_1") : STR("viewport_pixels"),
                chosen_stats.attempts,
                chosen_stats.hit_uv_count,
                static_cast<int>(chosen_samples.size()),
                MinQualityScreenHitUvSamples,
                DenseScreenHitHardMaxAttempts,
                chosen_stats.first_failure.empty() ? STR("<none>") : chosen_stats.first_failure);
            return false;
        }

        const auto rt_width = std::max(512, HighResProjectionRtWidth);
        const auto rt_height = std::max(
            256,
            static_cast<int>(std::round(static_cast<double>(rt_width) *
                                        static_cast<double>(std::max(1, viewport.height)) /
                                        static_cast<double>(std::max(1, viewport.width)))));

        AlignmentResult alignment{};
        alignment.backend = STR("camera_pov_identity");
        alignment.transform = ScreenTransform{};
        alignment.sky_misalign_suspect = false;
        alignment.score_ratio = 1.0;
        log_projection_tuning(STR("play_camera_pov_identity"), alignment.transform);

        const auto pixel_read_start = SteadyClock::now();
        std::vector<std::optional<Color>> hidden_background_colors(chosen_samples.size());
        std::vector<std::optional<Color>> trace_material_colors(chosen_samples.size());
        std::vector<uint8_t> traced_floor_like_by_sample(chosen_samples.size(), 0);
        int trace_background_hits = 0;
        int trace_background_misses = 0;
        int trace_floor_hits = 0;
        int trace_self_skips = 0;
        int trace_channel_attempts = 0;
        double trace_distance_min = 1000000000.0;
        double trace_distance_max = 0.0;
        double trace_distance_sum = 0.0;
        for (size_t i = 0; i < chosen_samples.size(); ++i)
        {
            if (state.cancelled)
            {
                break;
            }
            const auto traced = trace_nearest_background_behind_sample(pawn, frame->eye, chosen_samples[i]);
            trace_self_skips += traced.self_skips;
            trace_channel_attempts += traced.channel_attempts;
            if (!traced.hit)
            {
                ++trace_background_misses;
                continue;
            }

            ++trace_background_hits;
            if (traced.floor_like)
            {
                ++trace_floor_hits;
                traced_floor_like_by_sample[i] = 1;
            }
            trace_distance_min = std::min(trace_distance_min, traced.distance);
            trace_distance_max = std::max(trace_distance_max, traced.distance);
            trace_distance_sum += traced.distance;
            auto color = traced.color;
            color.r = clamp(color.r, 0.012, 0.985);
            color.g = clamp(color.g, 0.012, 0.985);
            color.b = clamp(color.b, 0.012, 0.985);
            color = infer_surface_material(color, traced.floor_like);
            trace_material_colors[i] = color;
            hidden_background_colors[i] = color;
        }
        ++state.views;

        const auto trace_material_summary = summarize_capture_colors(trace_material_colors);
        StringType readback_backend = STR("trace_validated_scene_capture");
        state.background_trace_hits = trace_background_hits;

        if (trace_background_hits < MinQualityScreenHitUvSamples)
        {
            timings.readback_ms = elapsed_ms_since(pixel_read_start);
            state.failures = 1;
            state.last_failure = STR("play_nearest_background_trace_insufficient");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play nearest-background refused readback_backend={} trace_hits={} trace_misses={} min_quality={} samples={} trace_self_skips={} trace_channel_attempts={} trace_distance_min={} trace_distance_avg={} trace_distance_max={}\n"),
                ModTag,
                readback_backend,
                trace_background_hits,
                trace_background_misses,
                MinQualityScreenHitUvSamples,
                static_cast<int>(chosen_samples.size()),
                trace_self_skips,
                trace_channel_attempts,
                trace_background_hits > 0 ? trace_distance_min : 0.0,
                trace_background_hits > 0 ? trace_distance_sum / static_cast<double>(trace_background_hits) : 0.0,
                trace_distance_max);
            return false;
        }

        auto scene_capture_fov = frame->fov_degrees;
        if (scene_capture_fov < 10.0 || scene_capture_fov > 150.0)
        {
            scene_capture_fov = frame->deproject_hfov;
        }
        if (scene_capture_fov < 10.0 || scene_capture_fov > 150.0)
        {
            scene_capture_fov = frame->camera_fov_degrees;
        }
        if (scene_capture_fov < 10.0 || scene_capture_fov > 150.0)
        {
            scene_capture_fov = 90.0;
        }
        std::vector<double> color_fov_candidates{scene_capture_fov};
        alignment.selected_fov_degrees = scene_capture_fov;
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} camera_pov_capture deterministic=1 no_fov_probe=1 no_pixel_alignment=1 selected_fov={} fov_source=deproject_viewport_horizontal frame_fov={} camera_pov_fov={} deproject_hfov={} deproject_vfov={} legacy_linear_hfov={} viewport={}x{} rt_size={}x{} transform_scale=({}, {}) transform_offset=({}, {}) transform_pivot=({}, {}) transform_flip=({}, {}) reason=player_camera_manager_pov_matches_first_person_camera\n"),
            ModTag,
            scene_capture_fov,
            frame->fov_degrees,
            frame->camera_fov_degrees,
            frame->deproject_hfov,
            frame->deproject_vfov,
            frame->legacy_linear_hfov,
            viewport.width,
            viewport.height,
            rt_width,
            rt_height,
            alignment.transform.scale_x,
            alignment.transform.scale_y,
            alignment.transform.offset_x,
            alignment.transform.offset_y,
            alignment.transform.pivot_x,
            alignment.transform.pivot_y,
            alignment.transform.flip_x ? 1 : 0,
            alignment.transform.flip_y ? 1 : 0);

        const auto color_capture_start = SteadyClock::now();
        const auto color_probe_samples = select_alignment_samples(chosen_samples, AlignmentSampleLimit);
        double color_selected_fov = 0.0;
        CaptureColorQuality color_probe_best{};
        int color_probe_reads = 0;
        int color_probe_missing = 0;
        for (const auto candidate_fov : color_fov_candidates)
        {
            if (state.cancelled)
            {
                break;
            }
            CaptureGridDiagnostics probe_diag{};
            auto probe_colors = capture_screen_sample_colors(pawn,
                                                             frame->eye,
                                                             look_at,
                                                             color_probe_samples,
                                                             rt_width,
                                                             rt_height,
                                                             true,
                                                             state,
                                                             candidate_fov,
                                                             &probe_diag,
                                                             alignment.transform,
                                                             frame->has_rotation ? &frame->rotation : nullptr);
            color_probe_reads += probe_diag.read_pixels;
            color_probe_missing += probe_diag.missing_pixels;
            const auto quality = summarize_capture_quality(probe_colors);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} play color_capture_probe fov={} score={} pixels={} missing={} avg_chroma={} luma_range={} rgb_range={} uniform={} clear_suspect={} read_pixels={}\n"),
                ModTag,
                candidate_fov,
                quality.score,
                quality.summary.pixels,
                probe_diag.missing_pixels,
                quality.avg_chroma,
                quality.luma_range,
                quality.rgb_range,
                quality.summary.uniform ? 1 : 0,
                quality.summary.clear_suspect ? 1 : 0,
                probe_diag.read_pixels);
            if (quality.score > color_probe_best.score)
            {
                color_probe_best = quality;
                color_selected_fov = candidate_fov;
            }
        }

        if (color_selected_fov <= 0.0 || color_probe_best.score < 0.0)
        {
            timings.capture_ms = elapsed_ms_since(color_capture_start);
            timings.readback_ms = elapsed_ms_since(pixel_read_start);
            state.failures = 1;
            state.last_failure = STR("play_trace_validated_capture_probe_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play trace-validated capture refused reason=probe_unavailable trace_hits={} trace_misses={} material_rgb_min=({}, {}, {}) material_rgb_avg=({}, {}, {}) material_rgb_max=({}, {}, {}) fov_candidates={} probe_reads={} probe_missing={} best_score={} best_pixels={} best_uniform={} best_clear={}\n"),
                ModTag,
                trace_background_hits,
                trace_background_misses,
                trace_material_summary.min_r,
                trace_material_summary.min_g,
                trace_material_summary.min_b,
                trace_material_summary.avg_r,
                trace_material_summary.avg_g,
                trace_material_summary.avg_b,
                trace_material_summary.max_r,
                trace_material_summary.max_g,
                trace_material_summary.max_b,
                static_cast<int>(color_fov_candidates.size()),
                color_probe_reads,
                color_probe_missing,
                color_probe_best.score,
                color_probe_best.summary.pixels,
                color_probe_best.summary.uniform ? 1 : 0,
                color_probe_best.summary.clear_suspect ? 1 : 0);
            return false;
        }

        TimedCaptureImage full_capture_image{};
        full_capture_image = capture_render_target_image(pawn,
                                                         frame->eye,
                                                         look_at,
                                                         rt_width,
                                                         rt_height,
                                                         true,
                                                         state,
                                                         color_selected_fov,
                                                         frame->has_rotation ? &frame->rotation : nullptr,
                                                         &chosen_samples,
                                                         &alignment.transform,
                                                         192);
        std::vector<std::optional<Color>> captured_colors{};
        StringType color_source{};
        int color_full_reads = 0;
        int color_full_missing = 0;
        if (full_capture_image.image.ok)
        {
            if (!full_capture_image.image.bulk_calibration_ok)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play bulk readback unverified; falling back to pixel api image_backend={} calibration_backend={} pairs={} best_median={} runner_up_median={} flip=({}, {}) reason=bulk_array_does_not_match_pixel_api\n"),
                    ModTag,
                    full_capture_image.image.backend.empty() ? STR("<none>") : full_capture_image.image.backend,
                    full_capture_image.image.bulk_calibration_backend,
                    full_capture_image.image.bulk_calibration_pairs,
                    full_capture_image.image.bulk_calibration_best_median,
                    full_capture_image.image.bulk_calibration_runner_up_median,
                    full_capture_image.image.bulk_to_pixel_transform.flip_x ? 1 : 0,
                    full_capture_image.image.bulk_to_pixel_transform.flip_y ? 1 : 0);
            }
            else
            {
                color_source = STR("trace_validated_scene_capture_bulk_image");
                readback_backend = STR("trace_validated_scene_capture_bulk_image");
                captured_colors = sample_hidden_background_from_image(full_capture_image.image,
                                                                      chosen_samples,
                                                                      full_capture_image.image.bulk_to_pixel_transform);
                color_full_reads = full_capture_image.image.decoded_pixels;
            }
        }
        if (captured_colors.empty())
        {
            CaptureGridDiagnostics full_diag{};
            captured_colors = capture_screen_sample_colors(pawn,
                                                           frame->eye,
                                                           look_at,
                                                           chosen_samples,
                                                           rt_width,
                                                           rt_height,
                                                           true,
                                                           state,
                                                           color_selected_fov,
                                                           &full_diag,
                                                           alignment.transform,
                                                           frame->has_rotation ? &frame->rotation : nullptr);
            color_source = full_capture_image.image.ok
                               ? STR("trace_validated_scene_capture_pixel_api_after_bulk_unverified")
                               : STR("trace_validated_scene_capture_pixel_api");
            readback_backend = full_capture_image.image.ok
                                   ? STR("trace_validated_scene_capture_pixel_api_after_bulk_unverified")
                                   : STR("trace_validated_scene_capture_pixel_api");
            color_full_reads = full_diag.read_pixels;
            color_full_missing = full_diag.missing_pixels;
        }

        hidden_background_colors.assign(chosen_samples.size(), std::nullopt);
        int capture_colors_used = 0;
        int capture_colors_rejected_missing = 0;
        const auto capture_count = std::min(captured_colors.size(), trace_material_colors.size());
        for (size_t i = 0; i < capture_count; ++i)
        {
            if (!trace_material_colors[i])
            {
                continue;
            }
            if (!captured_colors[i])
            {
                ++capture_colors_rejected_missing;
                continue;
            }
            auto color = *captured_colors[i];
            color.r = clamp(color.r, 0.012, 0.985);
            color.g = clamp(color.g, 0.012, 0.985);
            color.b = clamp(color.b, 0.012, 0.985);
            color.roughness = (i < traced_floor_like_by_sample.size() && traced_floor_like_by_sample[i] != 0)
                                  ? 0.985
                                  : clamp(trace_material_colors[i]->roughness, 0.40, 0.99);
            color.metallic = trace_material_colors[i]->metallic;
            color = infer_surface_material(color, i < traced_floor_like_by_sample.size() && traced_floor_like_by_sample[i] != 0);
            hidden_background_colors[i] = color;
            ++capture_colors_used;
        }

        const auto background_summary = summarize_capture_colors(hidden_background_colors);
        const auto background_quality = summarize_capture_quality(hidden_background_colors);
        timings.capture_ms = elapsed_ms_since(color_capture_start);
        timings.readback_ms = elapsed_ms_since(pixel_read_start);
        state.background_pixels = background_summary.pixels;
        state.capture_pixels_ready = background_summary.pixels > 0 && !background_summary.uniform &&
                                     !background_summary.clear_suspect;

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play color_capture_selected source={} fov={} probe_score={} probe_pixels={} full_pixels={} full_reads={} full_missing={} image_backend={} image_decoded_pixels={} image_failure={} bulk_calibration_ok={} bulk_calibration_backend={} bulk_pairs={} bulk_best_median={} bulk_runner_up_median={} bulk_flip=({}, {}) avg_chroma={} luma_range={} rgb_range={} capture_used={} capture_missing={} trace_material_rgb_avg=({}, {}, {})\n"),
            ModTag,
            color_source,
            color_selected_fov,
            color_probe_best.score,
            color_probe_best.summary.pixels,
            background_summary.pixels,
            color_full_reads,
            color_full_missing,
            full_capture_image.image.backend.empty() ? STR("<none>") : full_capture_image.image.backend,
            full_capture_image.image.decoded_pixels,
            full_capture_image.image.failure.empty() ? STR("<none>") : full_capture_image.image.failure,
            full_capture_image.image.bulk_calibration_ok ? 1 : 0,
            full_capture_image.image.bulk_calibration_backend,
            full_capture_image.image.bulk_calibration_pairs,
            full_capture_image.image.bulk_calibration_best_median,
            full_capture_image.image.bulk_calibration_runner_up_median,
            full_capture_image.image.bulk_to_pixel_transform.flip_x ? 1 : 0,
            full_capture_image.image.bulk_to_pixel_transform.flip_y ? 1 : 0,
            background_quality.avg_chroma,
            background_quality.luma_range,
            background_quality.rgb_range,
            capture_colors_used,
            capture_colors_rejected_missing,
            trace_material_summary.avg_r,
            trace_material_summary.avg_g,
            trace_material_summary.avg_b);

        if (background_summary.pixels < MinQualityScreenHitUvSamples || background_summary.uniform ||
            background_summary.clear_suspect)
        {
            state.failures = 1;
            state.last_failure = STR("play_trace_validated_capture_color_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play trace-validated capture refused readback_backend={} trace_hits={} trace_misses={} capture_used={} capture_missing={} min_quality={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) near_uniform={} capture_uniform={} clear_suspect={} selected_fov={} score={} image_backend={} image_failure={}\n"),
                ModTag,
                readback_backend,
                trace_background_hits,
                trace_background_misses,
                capture_colors_used,
                capture_colors_rejected_missing,
                MinQualityScreenHitUvSamples,
                background_summary.min_r,
                background_summary.min_g,
                background_summary.min_b,
                background_summary.avg_r,
                background_summary.avg_g,
                background_summary.avg_b,
                background_summary.max_r,
                background_summary.max_g,
                background_summary.max_b,
                background_summary.near_uniform_samples,
                background_summary.uniform ? 1 : 0,
                background_summary.clear_suspect ? 1 : 0,
                color_selected_fov,
                background_quality.score,
                full_capture_image.image.backend.empty() ? STR("<none>") : full_capture_image.image.backend,
                full_capture_image.image.failure.empty() ? STR("<none>") : full_capture_image.image.failure);
            return false;
        }

        std::vector<ScreenHitSample> calibrated_samples{};
        calibrated_samples.reserve(chosen_samples.size());
        for (size_t i = 0; i < chosen_samples.size(); ++i)
        {
            if (i >= hidden_background_colors.size() || !hidden_background_colors[i])
            {
                continue;
            }
            auto sample = chosen_samples[i];
            sample.floor_like = i < traced_floor_like_by_sample.size() && traced_floor_like_by_sample[i] != 0;
            sample.color = *hidden_background_colors[i];
            sample.color.r = clamp(sample.color.r, 0.012, 0.985);
            sample.color.g = clamp(sample.color.g, 0.012, 0.985);
            sample.color.b = clamp(sample.color.b, 0.012, 0.985);
            sample.color = infer_surface_material(sample.color, sample.floor_like);
            calibrated_samples.push_back(sample);
        }
        state.atlas_bins = static_cast<int>(calibrated_samples.size());

        if (static_cast<int>(calibrated_samples.size()) < MinQualityScreenHitUvSamples)
        {
            state.failures = 1;
            state.last_failure = STR("play_tps_exact_color_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play tps refused readback_backend={} exact_color_reads={} calibrated_samples={} min_quality={} dense_hit_uv_count={} rt_size={}x{} background_pixels={}\n"),
                ModTag,
                readback_backend,
                background_summary.pixels,
                static_cast<int>(calibrated_samples.size()),
                MinQualityScreenHitUvSamples,
                chosen_stats.hit_uv_count,
                rt_width,
                rt_height,
                background_summary.pixels);
            return false;
        }

        UvAtlasTransform uv_atlas_transform{};
        StringType uv_atlas_transform_label = audit_only ? STR("identity_audit_no_visual_calibration")
                                                         : STR("identity_unverified");
        int uv_atlas_calibration_pairs = 0;
        double uv_atlas_calibration_best = 0.0;
        double uv_atlas_calibration_runner_up = 0.0;
        if (!audit_only)
        {
            const auto uv_calibration_start = SteadyClock::now();
            auto calibration_albedo = albedo_before.bytes;
            for (int y = 0; y < albedo_before.height; ++y)
            {
                for (int x = 0; x < albedo_before.width; ++x)
                {
                    const auto offset = static_cast<size_t>(y * albedo_before.width + x) * 4;
                    if (offset + 2 >= calibration_albedo.size())
                    {
                        continue;
                    }
                    const auto color = uv_calibration_color((static_cast<double>(x) + 0.5) /
                                                            static_cast<double>(std::max(1, albedo_before.width)),
                                                            (static_cast<double>(y) + 0.5) /
                                                            static_cast<double>(std::max(1, albedo_before.height)));
                    calibration_albedo[offset + 0] = byte_from_unit(color.r);
                    calibration_albedo[offset + 1] = byte_from_unit(color.g);
                    calibration_albedo[offset + 2] = byte_from_unit(color.b);
                }
            }

            StringType uv_calibration_import_failure{};
            const auto uv_calibration_import_ok =
                import_channel_bytes(component, 0, calibration_albedo, uv_calibration_import_failure);
            if (!uv_calibration_import_ok)
            {
                StringType restore_failure{};
                import_channel_bytes(component, 0, albedo_before.bytes, restore_failure);
                timings.alignment_ms += elapsed_ms_since(uv_calibration_start);
                state.failures = 1;
                state.last_failure = STR("play_uv_atlas_calibration_import_failed");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play uv atlas calibration refused import_ok=0 failure={} restore_failure={}\n"),
                    ModTag,
                    uv_calibration_import_failure.empty() ? STR("<none>") : uv_calibration_import_failure,
                    restore_failure.empty() ? STR("<none>") : restore_failure);
                return false;
            }

            const auto uv_calibration_samples = select_alignment_samples(calibrated_samples, 768);
            CaptureGridDiagnostics uv_calibration_diag{};
            const auto visible_calibration_colors = capture_screen_sample_colors(pawn,
                                                                                 frame->eye,
                                                                                 look_at,
                                                                                 uv_calibration_samples,
                                                                                 rt_width,
                                                                                 rt_height,
                                                                                 false,
                                                                                 state,
                                                                                 color_selected_fov,
                                                                                 &uv_calibration_diag,
                                                                                 alignment.transform,
                                                                                 frame->has_rotation ? &frame->rotation : nullptr,
                                                                                 true);
            struct UvCandidate
            {
                UvAtlasTransform transform{};
                const CharType* label{STR("identity")};
            };
            const std::array<UvCandidate, 8> uv_candidates{
                UvCandidate{UvAtlasTransform{false, false, false}, STR("uv_identity")},
                UvCandidate{UvAtlasTransform{false, true, false}, STR("uv_flip_u")},
                UvCandidate{UvAtlasTransform{false, false, true}, STR("uv_flip_v")},
                UvCandidate{UvAtlasTransform{false, true, true}, STR("uv_flip_uv")},
                UvCandidate{UvAtlasTransform{true, false, false}, STR("uv_swap")},
                UvCandidate{UvAtlasTransform{true, true, false}, STR("uv_swap_flip_u")},
                UvCandidate{UvAtlasTransform{true, false, true}, STR("uv_swap_flip_v")},
                UvCandidate{UvAtlasTransform{true, true, true}, STR("uv_swap_flip_uv")}};

            double best_median = 1000000.0;
            double runner_up_median = 1000000.0;
            int best_pairs = 0;
            const UvCandidate* best_candidate = nullptr;
            for (const auto& candidate : uv_candidates)
            {
                std::vector<double> distances{};
                distances.reserve(uv_calibration_samples.size());
                const auto count = std::min(uv_calibration_samples.size(), visible_calibration_colors.size());
                for (size_t i = 0; i < count; ++i)
                {
                    if (!visible_calibration_colors[i])
                    {
                        continue;
                    }
                    const auto uv = apply_uv_atlas_transform(uv_calibration_samples[i].u,
                                                             uv_calibration_samples[i].v,
                                                             candidate.transform);
                    const auto expected = uv_calibration_color(uv.first, uv.second);
                    distances.push_back(chroma_distance_rgb(*visible_calibration_colors[i], expected));
                }
                const auto pairs = static_cast<int>(distances.size());
                const auto median = pairs > 0 ? median_value(std::move(distances)) : 1000000.0;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} uv_atlas_calibration candidate={} pairs={} median_chroma_error={} swap={} flip=({}, {}) visible_reads={} missing={}\n"),
                    ModTag,
                    candidate.label,
                    pairs,
                    median,
                    candidate.transform.swap_uv ? 1 : 0,
                    candidate.transform.flip_u ? 1 : 0,
                    candidate.transform.flip_v ? 1 : 0,
                    uv_calibration_diag.read_pixels,
                    uv_calibration_diag.missing_pixels);
                if (median < best_median)
                {
                    runner_up_median = best_median;
                    best_median = median;
                    best_pairs = pairs;
                    best_candidate = &candidate;
                }
                else if (median < runner_up_median)
                {
                    runner_up_median = median;
                }
            }

            const auto separated_from_runner = runner_up_median >= 999999.0 ||
                                               best_median <= runner_up_median * 0.86 ||
                                               (runner_up_median - best_median) >= 0.025;
            const auto uv_calibration_ok = best_candidate &&
                                           best_pairs >= std::min(64, static_cast<int>(uv_calibration_samples.size() / 2)) &&
                                           best_median <= 0.22 &&
                                           separated_from_runner;
            uv_atlas_calibration_pairs = best_pairs;
            uv_atlas_calibration_best = best_median < 999999.0 ? best_median : 0.0;
            uv_atlas_calibration_runner_up = runner_up_median < 999999.0 ? runner_up_median : 0.0;
            if (best_candidate)
            {
                uv_atlas_transform = best_candidate->transform;
                uv_atlas_transform_label = best_candidate->label;
            }

            timings.alignment_ms += elapsed_ms_since(uv_calibration_start);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} uv_atlas_calibration selected={} ok={} samples={} pairs={} best_median={} runner_up_median={} swap={} flip=({}, {}) visible_reads={} missing={} import_ok=1 threshold=0.22 separated={}\n"),
                ModTag,
                uv_atlas_transform_label,
                uv_calibration_ok ? 1 : 0,
                static_cast<int>(uv_calibration_samples.size()),
                uv_atlas_calibration_pairs,
                uv_atlas_calibration_best,
                uv_atlas_calibration_runner_up,
                uv_atlas_transform.swap_uv ? 1 : 0,
                uv_atlas_transform.flip_u ? 1 : 0,
                uv_atlas_transform.flip_v ? 1 : 0,
                uv_calibration_diag.read_pixels,
                uv_calibration_diag.missing_pixels,
                separated_from_runner ? 1 : 0);

            if (!uv_calibration_ok)
            {
                StringType restore_failure{};
                import_channel_bytes(component, 0, albedo_before.bytes, restore_failure);
                state.failures = 1;
                state.last_failure = STR("play_uv_atlas_mapping_unverified");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play uv atlas calibration refused selected={} pairs={} best_median={} runner_up_median={} restore_failure={} reason=hit_uv_does_not_match_import_atlas\n"),
                    ModTag,
                    uv_atlas_transform_label,
                    uv_atlas_calibration_pairs,
                    uv_atlas_calibration_best,
                    uv_atlas_calibration_runner_up,
                    restore_failure.empty() ? STR("<none>") : restore_failure);
                return false;
            }
        }

        auto atlas_samples = calibrated_samples;
        for (auto& sample : atlas_samples)
        {
            const auto uv = apply_uv_atlas_transform(sample.u, sample.v, uv_atlas_transform);
            sample.u = uv.first;
            sample.v = uv.second;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play nearest-background projection single_tps_view=1 aux_views=0 scene_capture_color=1 background_source=surface_ray_nearest_non_self_color_from_hidden_scene_capture dense_attempts={} dense_hit_uv_count={} target_dense_hits={} min_dense_hits={} hard_max_attempts={} samples={} coord_mode={} screen_bbox_norm=({}, {})-({}, {}) screen_bbox_px=({}, {})-({}, {}) rt_size={}x{} readback_backend={} exact_color_reads={} trace_hits={} trace_misses={} trace_floor_hits={} trace_self_skips={} trace_channel_attempts={} trace_distance_min={} trace_distance_avg={} trace_distance_max={} capture_uniform={} clear_suspect={} background_pixels={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) projection_align_score={} body_delta_median={} alignment_backend={} alignment_runner_up={} alignment_score_ratio={} selected_fov={} runner_up_fov={} screen_transform_scale=({}, {}) screen_transform_offset=({}, {}) screen_transform_pivot=({}, {}) screen_transform_flip=({}, {}) sky_misalign_suspect={} uv_atlas_transform={} uv_atlas_swap={} uv_atlas_flip=({}, {}) uv_atlas_calibration_pairs={} uv_atlas_calibration_best={} uv_atlas_calibration_runner_up={} gain_mode=direct_hidden_scene_capture frame_fov={} fov_source={} fov_fallback={} camera_fov={} deproject_hfov={} deproject_vfov={} legacy_linear_hfov={} camera_deproject_angle_delta={} capture_rotation_override={} frame_source={} viewport={}x{} viewport_fallback={} component={} mesh={} controller={}\n"),
            ModTag,
            chosen_stats.attempts,
            chosen_stats.hit_uv_count,
            TargetScreenHitUvSamples,
            MinQualityScreenHitUvSamples,
            DenseScreenHitHardMaxAttempts,
            static_cast<int>(calibrated_samples.size()),
            use_normalized ? STR("normalized_0_1") : STR("viewport_pixels"),
            chosen_stats.min_nx,
            chosen_stats.min_ny,
            chosen_stats.max_nx,
            chosen_stats.max_ny,
            static_cast<int>(chosen_stats.min_nx * static_cast<double>(viewport.width)),
            static_cast<int>(chosen_stats.min_ny * static_cast<double>(viewport.height)),
            static_cast<int>(chosen_stats.max_nx * static_cast<double>(viewport.width)),
            static_cast<int>(chosen_stats.max_ny * static_cast<double>(viewport.height)),
            rt_width,
            rt_height,
            readback_backend,
            background_summary.pixels,
            trace_background_hits,
            trace_background_misses,
            trace_floor_hits,
            trace_self_skips,
            trace_channel_attempts,
            trace_background_hits > 0 ? trace_distance_min : 0.0,
            trace_background_hits > 0 ? trace_distance_sum / static_cast<double>(trace_background_hits) : 0.0,
            trace_distance_max,
            background_summary.uniform ? 1 : 0,
            background_summary.clear_suspect ? 1 : 0,
            background_summary.pixels,
            background_summary.min_r,
            background_summary.min_g,
            background_summary.min_b,
            background_summary.avg_r,
            background_summary.avg_g,
            background_summary.avg_b,
            background_summary.max_r,
            background_summary.max_g,
            background_summary.max_b,
            alignment.projection_align_score,
            alignment.body_delta_median,
            alignment.backend,
            alignment.runner_up_backend,
            alignment.score_ratio,
            color_selected_fov,
            alignment.runner_up_fov_degrees,
            alignment.transform.scale_x,
            alignment.transform.scale_y,
            alignment.transform.offset_x,
            alignment.transform.offset_y,
            alignment.transform.pivot_x,
            alignment.transform.pivot_y,
            alignment.transform.flip_x ? 1 : 0,
            alignment.transform.flip_y ? 1 : 0,
            alignment.sky_misalign_suspect ? 1 : 0,
            uv_atlas_transform_label,
            uv_atlas_transform.swap_uv ? 1 : 0,
            uv_atlas_transform.flip_u ? 1 : 0,
            uv_atlas_transform.flip_v ? 1 : 0,
            uv_atlas_calibration_pairs,
            uv_atlas_calibration_best,
            uv_atlas_calibration_runner_up,
            frame->fov_degrees,
            frame->fov_source.empty() ? STR("<none>") : frame->fov_source,
            frame->fov_fallback ? 1 : 0,
            frame->camera_fov_degrees,
            frame->deproject_hfov,
            frame->deproject_vfov,
            frame->legacy_linear_hfov,
            frame->camera_deproject_angle_delta,
            frame->has_rotation ? 1 : 0,
            frame->source,
            viewport.width,
            viewport.height,
            viewport.fallback ? 1 : 0,
            component->GetFullName(),
            mesh->GetFullName(),
            controller->GetFullName());

        const auto atlas_start = SteadyClock::now();

        std::vector<ProjectionTexel> texels(static_cast<size_t>(albedo_before.width) * static_cast<size_t>(albedo_before.height));
        for (const auto& sample : atlas_samples)
        {
            const auto priority = sample.floor_like ? 7 : 6;
            const auto weight = sample.floor_like ? 64.0 : 48.0;
            splat_projection_texel(texels,
                                   albedo_before.width,
                                   albedo_before.height,
                                   sample.u,
                                   sample.v,
                                   sample.color,
                                   weight,
                                   priority,
                                   sample.floor_like,
                                   ScreenProjectionExactSplatRadius);
        }
        for (const auto& sample : atlas_samples)
        {
            const auto priority = sample.floor_like ? 3 : 2;
            const auto weight = sample.floor_like ? 18.0 : 12.0;
            splat_projection_texel(texels,
                                   albedo_before.width,
                                   albedo_before.height,
                                   sample.u,
                                   sample.v,
                                   sample.color,
                                   weight,
                                   priority,
                                   sample.floor_like,
                                   ScreenProjectionFillSplatRadius);
        }
        dilate_projection_texels(texels, albedo_before.width, albedo_before.height, ScreenProjectionStrictDilationPasses);
        const auto global_filled = flood_fill_projection_texel_holes(texels, albedo_before.width, albedo_before.height);
        const auto projection_audit = compute_projection_audit(atlas_samples,
                                                               texels,
                                                               albedo_before.width,
                                                               albedo_before.height,
                                                               controller,
                                                               viewport,
                                                               export_audit_samples);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} projection_audit result={} audit_only={} paint_would_apply={} body_mask_pixels={} audit_samples={} projected_ok={} reproject_error_p50={} reproject_error_p95={} reproject_error_max={} uv_audit_resolution={} uv_texels={} uv_conflict_texels={} uv_conflict_pixels={} uv_conflict_pixels_pct={} same_uv_color_variance={} predicted_screen_mae={} predicted_screen_p95={} predicted_bad_pixels={} predicted_bad_pixel_pct={} thresholds=(reproject_p95<={},uv_conflict_pct<={},same_uv_var<={},screen_mae<={},bad_pct<={})\n"),
            ModTag,
            projection_audit.reason,
            audit_only ? 1 : 0,
            projection_audit.paint_would_apply ? 1 : 0,
            projection_audit.body_mask_pixels,
            projection_audit.audit_samples,
            projection_audit.projected_ok,
            projection_audit.reproject_error_p50,
            projection_audit.reproject_error_p95,
            projection_audit.reproject_error_max,
            ProjectionAuditUvResolution,
            projection_audit.uv_texels,
            projection_audit.uv_conflict_texels,
            projection_audit.uv_conflict_pixels,
            projection_audit.uv_conflict_pixels_pct,
            projection_audit.same_uv_color_variance,
            projection_audit.predicted_screen_mae,
            projection_audit.predicted_screen_p95,
            projection_audit.predicted_bad_pixels,
            projection_audit.predicted_bad_pixel_pct,
            ProjectionAuditMaxReprojectP95Px,
            ProjectionAuditMaxUvConflictPixelsPct,
            ProjectionAuditMaxSameUvColorVariance,
            ProjectionAuditMaxPredictedScreenMae,
            ProjectionAuditMaxPredictedBadPixelPct);

        auto albedo_target = albedo_before.bytes;
        auto metallic_target = metallic_before.bytes;
        auto roughness_target = roughness_before.bytes;
        int uv_coverage = 0;
        int filled_by_direct = 0;
        int filled_by_floor = 0;
        int filled_by_interpolation = 0;
        int preserved_original = 0;
        const auto write_scalar = [&](std::vector<uint8_t>& bytes, int x, int y, int width, double value) {
            const auto offset = static_cast<size_t>(y * width + x) * 4;
            if (offset < bytes.size())
            {
                bytes[offset] = byte_from_unit(value);
            }
        };

        for (int y = 0; y < albedo_before.height; ++y)
        {
            for (int x = 0; x < albedo_before.width; ++x)
            {
                const auto index = static_cast<size_t>(y * albedo_before.width + x);
                const auto offset = index * 4;
                if (index >= texels.size() || texels[index].weight <= 0.0001)
                {
                    ++preserved_original;
                    continue;
                }

                const auto inv = 1.0 / texels[index].weight;
                const auto r = clamp(texels[index].r * inv, 0.02, 0.98);
                const auto g = clamp(texels[index].g * inv, 0.02, 0.98);
                const auto b = clamp(texels[index].b * inv, 0.02, 0.98);
                const auto roughness = clamp(texels[index].roughness * inv, 0.72, 0.99);
                const auto metallic = clamp(texels[index].metallic * inv, 0.0, 0.60);
                if (offset + 2 < albedo_target.size())
                {
                    albedo_target[offset + 0] = byte_from_unit(r);
                    albedo_target[offset + 1] = byte_from_unit(g);
                    albedo_target[offset + 2] = byte_from_unit(b);
                }
                if (x < metallic_before.width && y < metallic_before.height)
                {
                    write_scalar(metallic_target, x, y, metallic_before.width, metallic);
                }
                if (x < roughness_before.width && y < roughness_before.height)
                {
                    write_scalar(roughness_target, x, y, roughness_before.width, roughness);
                }
                ++uv_coverage;
                if (texels[index].floor_like)
                {
                    ++filled_by_floor;
                }
                if (texels[index].dilated)
                {
                    ++filled_by_interpolation;
                }
                else
                {
                    ++filled_by_direct;
                }
            }
        }

        timings.atlas_ms = elapsed_ms_since(atlas_start);
        if (audit_only || !projection_audit.paint_would_apply)
        {
            if (!audit_only)
            {
                StringType restore_failure{};
                import_channel_bytes(component, 0, albedo_before.bytes, restore_failure);
            }
            timings.total_ms = elapsed_ms_since(total_start);
            state.success = projection_audit.paint_would_apply ? 1 : 0;
            state.failures = projection_audit.paint_would_apply ? 0 : 1;
            state.verified_visible_backend = false;
            state.paint_state_hash_before = hash_component_paint_state(component);
            state.paint_state_hash_after = state.paint_state_hash_before;
            state.last_failure = audit_only
                                     ? (projection_audit.paint_would_apply ? STR("projection_audit_ready_no_paint")
                                                                           : STR("play_uv_mapping_not_solvable"))
                                     : STR("play_uv_mapping_not_solvable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play projection audit stopped no_import=1 audit_only={} paint_would_apply={} queued_strokes=0 success={} failures={} failure={} reason={} t_total_ms={}\n"),
                ModTag,
                audit_only ? 1 : 0,
                projection_audit.paint_would_apply ? 1 : 0,
                state.success,
                state.failures,
                state.last_failure,
                projection_audit.reason,
                timings.total_ms);
            return false;
        }

        const auto import_start = SteadyClock::now();
        const auto component_hash_before = hash_component_paint_state(component);
        state.paint_state_hash_before = component_hash_before;
        state.paint_state_hash_after = component_hash_before;
        const auto albedo_target_hash = hash_bytes(albedo_target);
        const auto metallic_target_hash = hash_bytes(metallic_target);
        const auto roughness_target_hash = hash_bytes(roughness_target);
        const auto albedo_changed = changed_byte_count(albedo_before.bytes, albedo_target);
        const auto metallic_changed = changed_byte_count(metallic_before.bytes, metallic_target);
        const auto roughness_changed = changed_byte_count(roughness_before.bytes, roughness_target);
        const auto rgb_summary = summarize_rgb_bytes(albedo_target, albedo_before.width, albedo_before.height);
        const auto metallic_summary = summarize_scalar_bytes(metallic_target, metallic_before.width, metallic_before.height);
        const auto roughness_summary = summarize_scalar_bytes(roughness_target, roughness_before.width, roughness_before.height);

        StringType albedo_import_failure{};
        StringType metallic_import_failure{};
        StringType roughness_import_failure{};
        const auto albedo_import_ok = import_channel_bytes(component, 0, albedo_target, albedo_import_failure);
        const auto metallic_import_ok = import_channel_bytes(component, 1, metallic_target, metallic_import_failure);
        const auto roughness_import_ok = import_channel_bytes(component, 2, roughness_target, roughness_import_failure);

        const auto albedo_after = export_channel_bytes(component, 0);
        const auto metallic_after = export_channel_bytes(component, 1);
        const auto roughness_after = export_channel_bytes(component, 2);
        timings.import_ms = elapsed_ms_since(import_start);
        const auto albedo_observed = albedo_after.ok && albedo_after.hash == albedo_target_hash;
        const auto metallic_observed = metallic_after.ok && metallic_after.hash == metallic_target_hash;
        const auto roughness_observed = roughness_after.ok && roughness_after.hash == roughness_target_hash;
        const auto component_hash_after = hash_component_paint_state(component);
        const auto component_changed = component_hash_after != component_hash_before;
        const auto all_observed = albedo_import_ok && metallic_import_ok && roughness_import_ok &&
                                  albedo_observed && metallic_observed && roughness_observed;
        const auto imported_texture_changed = albedo_changed > 0 || metallic_changed > 0 || roughness_changed > 0;
        state.paint_state_hash_after = component_hash_after;
        state.success = (albedo_import_ok && albedo_observed ? 1 : 0) +
                        (metallic_import_ok && metallic_observed ? 1 : 0) +
                        (roughness_import_ok && roughness_observed ? 1 : 0);
        state.failures = 3 - state.success;
        state.verified_visible_backend = all_observed && imported_texture_changed;
        state.last_failure = state.verified_visible_backend ? STR("play_projection_solver_applied")
                                                            : STR("play_trace_validated_capture_color_unverified");
        timings.total_ms = elapsed_ms_since(total_start);

        RC::Output::send<RC::LogLevel::Verbose>(
              STR("{} play tps aligned projection coverage single_tps_view=1 aux_views=0 floor_trace=0 alignment_probe=0 fill_mode=exact_observed_uv_then_low_priority_fill global_fill=1 global_filled={} local_dilation_passes={} exact_splat_radius={} fill_splat_radius={} coord_mode={} screen_hit_success={} hit_uv_count={} dense_hit_uv_count={} samples={} uv_coverage={} coverage_ratio={} filled_by_direct={} direct_coverage_ratio={} filled_by_floor={} filled_by_interpolation={} interpolation_ratio={} preserved_original={} preserved_ratio={} capture_uniform={} capture_clear_suspect={} readback_backend={} queued_strokes=0 no_clear=1 no_commit=1\n"),
            ModTag,
              global_filled,
              ScreenProjectionStrictDilationPasses,
              ScreenProjectionExactSplatRadius,
              ScreenProjectionFillSplatRadius,
              use_normalized ? STR("normalized_0_1") : STR("viewport_pixels"),
            chosen_stats.hit_success,
            chosen_stats.hit_uv_count,
            chosen_stats.hit_uv_count,
            static_cast<int>(calibrated_samples.size()),
            uv_coverage,
            rgb_summary.pixels > 0 ? static_cast<double>(uv_coverage) / static_cast<double>(rgb_summary.pixels) : 0.0,
            filled_by_direct,
            uv_coverage > 0 ? static_cast<double>(filled_by_direct) / static_cast<double>(uv_coverage) : 0.0,
            filled_by_floor,
            filled_by_interpolation,
            uv_coverage > 0 ? static_cast<double>(filled_by_interpolation) / static_cast<double>(uv_coverage) : 0.0,
            preserved_original,
            rgb_summary.pixels > 0 ? static_cast<double>(preserved_original) / static_cast<double>(rgb_summary.pixels) : 0.0,
            background_summary.uniform ? 1 : 0,
            background_summary.clear_suspect ? 1 : 0,
            readback_backend);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play texture summary pixels={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) red_pixels={} near_uniform={} albedo_changed_bytes={} metallic_changed_bytes={} roughness_changed_bytes={}\n"),
            ModTag,
            rgb_summary.pixels,
            rgb_summary.min_r,
            rgb_summary.min_g,
            rgb_summary.min_b,
            rgb_summary.avg_r,
            rgb_summary.avg_g,
            rgb_summary.avg_b,
            rgb_summary.max_r,
            rgb_summary.max_g,
            rgb_summary.max_b,
            rgb_summary.red_dominant_pixels,
            rgb_summary.near_uniform_samples,
            albedo_changed,
            metallic_changed,
            roughness_changed);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play material summary metallic_pixels={} metallic_min={} metallic_avg={} metallic_max={} metallic_zero_pixels={} metallic_near_uniform={} roughness_pixels={} roughness_min={} roughness_avg={} roughness_max={} roughness_high_pixels={} roughness_near_uniform={}\n"),
            ModTag,
            metallic_summary.pixels,
            metallic_summary.min_value,
            metallic_summary.avg_value,
            metallic_summary.max_value,
            metallic_summary.near_zero_pixels,
            metallic_summary.near_uniform_samples,
            roughness_summary.pixels,
            roughness_summary.min_value,
            roughness_summary.avg_value,
            roughness_summary.max_value,
            roughness_summary.high_pixels,
            roughness_summary.near_uniform_samples);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play tps aligned import apply backend=ImportChannelFromBytes queued_strokes=0 success={} failures={} visible_backend={} component_changed={} imported_texture_changed={} component_hash_before={} component_hash_after={} albedo_hash_before={} albedo_hash_target={} albedo_hash_after={} albedo_import_ok={} albedo_observed={} metallic_hash_before={} metallic_hash_target={} metallic_hash_after={} metallic_import_ok={} metallic_observed={} roughness_hash_before={} roughness_hash_target={} roughness_hash_after={} roughness_import_ok={} roughness_observed={} albedo_failure={} metallic_failure={} roughness_failure={} albedo_after_failure={} metallic_after_failure={} roughness_after_failure={}\n"),
            ModTag,
            state.success,
            state.failures,
            state.verified_visible_backend ? 1 : 0,
            component_changed ? 1 : 0,
            imported_texture_changed ? 1 : 0,
            component_hash_before,
            component_hash_after,
            albedo_before.hash,
            albedo_target_hash,
            albedo_after.hash,
            albedo_import_ok ? 1 : 0,
            albedo_observed ? 1 : 0,
            metallic_before.hash,
            metallic_target_hash,
            metallic_after.hash,
            metallic_import_ok ? 1 : 0,
            metallic_observed ? 1 : 0,
            roughness_before.hash,
            roughness_target_hash,
            roughness_after.hash,
            roughness_import_ok ? 1 : 0,
            roughness_observed ? 1 : 0,
            albedo_import_failure.empty() ? STR("<none>") : albedo_import_failure,
            metallic_import_failure.empty() ? STR("<none>") : metallic_import_failure,
            roughness_import_failure.empty() ? STR("<none>") : roughness_import_failure,
            albedo_after.failure.empty() ? STR("<none>") : albedo_after.failure,
            metallic_after.failure.empty() ? STR("<none>") : metallic_after.failure,
            roughness_after.failure.empty() ? STR("<none>") : roughness_after.failure);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play timing t_export_ms={} t_coarse_hit_ms={} t_dense_hit_ms={} t_capture_ms={} t_readback_ms={} t_alignment_ms={} t_atlas_ms={} t_import_ms={} t_total_ms={} readback_backend={} projection_align_score={} alignment_runner_up={} alignment_score_ratio={} sky_misalign_suspect={}\n"),
            ModTag,
            timings.export_ms,
            timings.coarse_hit_ms,
            timings.dense_hit_ms,
            timings.capture_ms,
            timings.readback_ms,
            timings.alignment_ms,
            timings.atlas_ms,
            timings.import_ms,
            timings.total_ms,
            readback_backend,
            alignment.projection_align_score,
            alignment.runner_up_score,
            alignment.score_ratio,
            alignment.sky_misalign_suspect ? 1 : 0);
        return state.verified_visible_backend;
    }

    auto apply_import_texture_camouflage(Unreal::UObject* component,
                                         const std::vector<Sample>& samples,
                                         ProbeState& state) -> bool;

    auto apply_screen_position_projection_cloak(Unreal::UObject* component,
                                                Unreal::UObject* pawn,
                                                ProbeState& state,
                                                const StringType& reason) -> bool
    {
        state.queued_strokes = 0;
        state.success = 0;
        state.failures = 0;
        state.paint_world_success = 0;
        state.paint_uv_success = 0;
        state.commit_calls = 0;
        state.verified_visible_backend = false;
        state.verified_paint_channel = PaintChannelAlbedoMetallicRoughness;
        state.verified_paint_function = STR("PaintAtScreenPosition");

        auto* controller = find_player_controller_for_pawn(pawn);
        auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
        auto frame = make_projection_frame(pawn, 0.0, 0.0);
        if (!component || !pawn || !controller || !mesh || !frame)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_projection_prereq_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen projection refused reason={} component={} pawn={} controller={} mesh={} frame={}\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                component ? component->GetFullName() : STR("<null>"),
                pawn ? pawn->GetFullName() : STR("<null>"),
                controller ? controller->GetFullName() : STR("<null>"),
                mesh ? mesh->GetFullName() : STR("<null>"),
                frame ? 1 : 0);
            return false;
        }

        auto viewport = get_viewport_info(controller);
        const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
        auto capture_colors = capture_background_grid(pawn,
                                                      frame->eye,
                                                      look_at,
                                                      ScreenProjectionGridX,
                                                      ScreenProjectionGridY,
                                                      state,
                                                      frame->fov_degrees);
        int color_samples = 0;
        double r_sum = 0.0;
        double g_sum = 0.0;
        double b_sum = 0.0;
        double min_r = 1.0;
        double min_g = 1.0;
        double min_b = 1.0;
        double max_r = 0.0;
        double max_g = 0.0;
        double max_b = 0.0;
        const Color neutral_hint{0.34, 0.36, 0.32, 0.94, 0.0};
        for (const auto& color : capture_colors)
        {
            if (!color)
            {
                continue;
            }
            auto sanitized = sanitize_background_color(*color, neutral_hint);
            sanitized = compensate_projected_albedo(sanitized, false);
            ++color_samples;
            r_sum += sanitized.r;
            g_sum += sanitized.g;
            b_sum += sanitized.b;
            min_r = std::min(min_r, sanitized.r);
            min_g = std::min(min_g, sanitized.g);
            min_b = std::min(min_b, sanitized.b);
            max_r = std::max(max_r, sanitized.r);
            max_g = std::max(max_g, sanitized.g);
            max_b = std::max(max_b, sanitized.b);
        }
        state.views += 1;
        state.background_pixels = color_samples;
        state.capture_pixels_ready = color_samples > 0;
        state.atlas_bins = color_samples;
        if (color_samples < 16)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_projection_capture_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen projection refused reason={} color_samples={} grid={}x{} camera_fov={} fov_fallback={} viewport={}x{} viewport_fallback={}\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                color_samples,
                ScreenProjectionGridX,
                ScreenProjectionGridY,
                frame->fov_degrees,
                frame->fov_fallback ? 1 : 0,
                viewport.width,
                viewport.height,
                viewport.fallback ? 1 : 0);
            return false;
        }

        const auto brush_ok = configure_screen_brush(component, ScreenProjectionBrushRadius, 1.0, 0.80);
        const auto initial_hash = hash_component_paint_state(component);
        state.paint_state_hash_before = initial_hash;
        state.paint_state_hash_after = initial_hash;

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play screen projection start reason={} grid={}x{} color_samples={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) camera_fov={} fov_fallback={} viewport={}x{} viewport_fallback={} brush_radius={} brush_ok={} component={} mesh={} controller={} no_clear=1 no_commit=1\n"),
            ModTag,
            reason.empty() ? STR("<none>") : reason,
            ScreenProjectionGridX,
            ScreenProjectionGridY,
            color_samples,
            min_r,
            min_g,
            min_b,
            r_sum / static_cast<double>(std::max(1, color_samples)),
            g_sum / static_cast<double>(std::max(1, color_samples)),
            b_sum / static_cast<double>(std::max(1, color_samples)),
            max_r,
            max_g,
            max_b,
            frame->fov_degrees,
            frame->fov_fallback ? 1 : 0,
            viewport.width,
            viewport.height,
            viewport.fallback ? 1 : 0,
            ScreenProjectionBrushRadius,
            brush_ok ? 1 : 0,
            component->GetFullName(),
            mesh->GetFullName(),
            controller->GetFullName());

        const auto run_mode = [&](const CharType* coord_mode, bool normalized_coords) -> bool {
            const auto hash_before = hash_component_paint_state(component);
            int calls = 0;
            int api_ok = 0;
            int param_failures = 0;
            StringType first_failure{};
            PaintParamWriteStats first_stats{};
            for (int y = 0; y < ScreenProjectionGridY; ++y)
            {
                for (int x = 0; x < ScreenProjectionGridX; ++x)
                {
                    if (state.cancelled)
                    {
                        break;
                    }
                    const auto index = static_cast<size_t>(y * ScreenProjectionGridX + x);
                    if (index >= capture_colors.size() || !capture_colors[index])
                    {
                        continue;
                    }
                    const auto nx = (static_cast<double>(x) + 0.5) / static_cast<double>(ScreenProjectionGridX);
                    const auto ny = (static_cast<double>(y) + 0.5) / static_cast<double>(ScreenProjectionGridY);
                    const auto sx = normalized_coords ? nx : nx * static_cast<double>(viewport.width);
                    const auto sy = normalized_coords ? ny : ny * static_cast<double>(viewport.height);
                    auto color = sanitize_background_color(*capture_colors[index], neutral_hint);
                    color = compensate_projected_albedo(color, false);
                    color = infer_surface_material(color, false);

                    PaintParamWriteStats stats{};
                    stats.channel_label =
                        channel_enum_label(component->GetFunctionByNameInChain(STR("PaintAtScreenPosition")),
                                           PaintChannelAlbedoMetallicRoughness);
                    ++calls;
                    const auto ok = paint_at_screen_position(component,
                                                             mesh,
                                                             controller,
                                                             sx,
                                                             sy,
                                                             color,
                                                             PaintChannelAlbedoMetallicRoughness,
                                                             true,
                                                             &stats,
                                                             0);
                    if (calls == 1)
                    {
                        first_stats = stats;
                    }
                    if (ok)
                    {
                        ++api_ok;
                    }
                    else
                    {
                        ++param_failures;
                        if (first_failure.empty())
                        {
                            first_failure = stats.failure.empty() ? STR("screen_paint_call_failed") : stats.failure;
                        }
                        if (api_ok == 0)
                        {
                            break;
                        }
                    }
                }
            }
            const auto hash_after = hash_component_paint_state(component);
            const auto changed = hash_after != hash_before;
            state.paint_state_hash_after = hash_after;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} play screen projection attempt coord_mode={} calls={} api_ok={} param_failures={} hash_before={} hash_after={} hash_changed={} first_param color={} screen={} channel={} channel_label={} failure={}\n"),
                ModTag,
                coord_mode,
                calls,
                api_ok,
                param_failures,
                hash_before,
                hash_after,
                changed ? 1 : 0,
                first_stats.wrote_color ? 1 : 0,
                first_stats.wrote_uv ? 1 : 0,
                first_stats.wrote_channel ? 1 : 0,
                first_stats.channel_label.empty() ? STR("<unknown>") : first_stats.channel_label,
                first_failure.empty() ? STR("<none>") : first_failure);

            if (changed)
            {
                state.success = api_ok;
                state.failures = param_failures;
                state.paint_uv_success = api_ok;
                state.visible_samples = api_ok;
                state.uv_hits = api_ok;
                state.verified_visible_backend = true;
                state.last_failure = STR("play_screen_projection_applied");
                return true;
            }

            state.success = 0;
            state.failures = param_failures > 0 ? param_failures : 1;
            state.visible_samples = api_ok;
            state.uv_hits = api_ok;
            return false;
        };

        if (run_mode(STR("viewport_pixels"), false))
        {
            return true;
        }
        if (!state.cancelled && run_mode(STR("normalized_0_1"), true))
        {
            return true;
        }

        std::vector<Sample> import_samples{};
        import_samples.reserve(capture_colors.size());
        for (int y = 0; y < ScreenProjectionGridY; ++y)
        {
            for (int x = 0; x < ScreenProjectionGridX; ++x)
            {
                const auto index = static_cast<size_t>(y * ScreenProjectionGridX + x);
                if (index >= capture_colors.size() || !capture_colors[index])
                {
                    continue;
                }
                Sample sample{};
                sample.u = (static_cast<double>(x) + 0.5) / static_cast<double>(ScreenProjectionGridX);
                sample.v = (static_cast<double>(y) + 0.5) / static_cast<double>(ScreenProjectionGridY);
                sample.color = sanitize_background_color(*capture_colors[index], neutral_hint);
                sample.color = compensate_projected_albedo(sample.color, false);
                sample.color = infer_surface_material(sample.color, false);
                sample.weight = 1.0;
                import_samples.push_back(sample);
            }
        }
        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} play screen projection falling back to ImportChannelFromBytes reason={} screen_paint_failed=1 import_samples={} min_samples=128\n"),
            ModTag,
            reason.empty() ? STR("<none>") : reason,
            static_cast<int>(import_samples.size()));
        if (import_samples.size() >= 128 && apply_import_texture_camouflage(component, import_samples, state))
        {
            state.last_failure = STR("play_screen_projection_failed_import_fallback_applied");
            return true;
        }

        state.verified_visible_backend = false;
        state.last_failure = STR("play_screen_projection_no_visible_change");
        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} play screen projection failed reason={} paint_changed=0 hash_initial={} hash_final={} color_samples={} queued_strokes=0 no_clear=1 no_commit=1\n"),
            ModTag,
            reason.empty() ? STR("<none>") : reason,
            initial_hash,
            state.paint_state_hash_after,
            color_samples);
        return false;
    }

    auto apply_screen_body_paint_cloak(Unreal::UObject* component,
                                       Unreal::UObject* pawn,
                                       ProbeState& state,
                                       const StringType& reason) -> bool
    {
        const auto total_start = SteadyClock::now();
        state.queued_strokes = 0;
        state.success = 0;
        state.failures = 0;
        state.paint_world_success = 0;
        state.paint_uv_success = 0;
        state.commit_calls = 0;
        state.body_trace_hits = 0;
        state.background_trace_hits = 0;
        state.visible_samples = 0;
        state.uv_hits = 0;
        state.background_pixels = 0;
        state.atlas_bins = 0;
        state.capture_pixels_ready = false;
        state.uv_mapping_ready = false;
        state.verified_visible_backend = false;
        state.verified_paint_channel = PaintChannelAlbedoMetallicRoughness;
        state.verified_paint_function = STR("PaintAtScreenPosition.body_mask");

        auto* controller = find_player_controller_for_pawn(pawn);
        auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
        auto viewport = get_viewport_info(controller);
        auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                : std::optional<ProjectionFrame>{};
        if (!component || !pawn || !controller || !mesh || viewport.width <= 0 || viewport.height <= 0)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_body_prereq_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} component={} pawn={} controller={} mesh={} viewport={}x{} frame={} no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                component ? component->GetFullName() : STR("<null>"),
                pawn ? pawn->GetFullName() : STR("<null>"),
                controller ? controller->GetFullName() : STR("<null>"),
                mesh ? mesh->GetFullName() : STR("<null>"),
                viewport.width,
                viewport.height,
                frame ? 1 : 0);
            return false;
        }
        if (!frame)
        {
            state.failures = 1;
            state.last_failure = STR("play_camera_deproject_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} controller={} viewport={}x{} frame=0 no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                controller->GetFullName(),
                viewport.width,
                viewport.height);
            return false;
        }

        auto* camera = camera_manager_for_controller(controller);
        auto* controller_view_target = call_no_params_return_object(controller, STR("GetViewTarget"));
        auto* camera_view_target = call_no_params_return_object(camera, STR("GetViewTarget"));
        auto* camera_owner = call_no_params_return_object(camera, STR("GetOwner"));
        const auto pawn_location = call_no_params_return_vector(pawn, STR("K2_GetActorLocation"));
        const auto mesh_location = call_no_params_return_vector(mesh, STR("K2_GetComponentLocation"));
        const auto target_location = mesh_location ? *mesh_location : pawn_location.value_or(frame->eye);
        const auto camera_to_target = sub(target_location, frame->eye);
        const auto camera_to_target_len = length(camera_to_target);
        const auto camera_to_target_dir = camera_to_target_len > 0.01 ? normalize(camera_to_target) : frame->forward;
        const auto camera_target_angle = std::acos(clamp(dot(camera_to_target_dir, frame->forward), -1.0, 1.0)) * 180.0 / Pi;
        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} screen_body camera_alignment camera={} camera_owner={} camera_view_target={} controller_view_target={} pawn={} mesh={} eye=({}, {}, {}) forward=({}, {}, {}) right=({}, {}, {}) up=({}, {}, {}) pawn_location=({}, {}, {}) mesh_location=({}, {}, {}) camera_to_target_len={} camera_to_target_forward={} camera_to_target_right={} camera_to_target_up={} camera_target_angle={} frame_source={} fov={} fov_source={} camera_fov={} viewport={}x{}\n"),
            ModTag,
            camera ? camera->GetFullName() : STR("<null>"),
            camera_owner ? camera_owner->GetFullName() : STR("<null>"),
            camera_view_target ? camera_view_target->GetFullName() : STR("<null>"),
            controller_view_target ? controller_view_target->GetFullName() : STR("<null>"),
            pawn->GetFullName(),
            mesh->GetFullName(),
            frame->eye.X(),
            frame->eye.Y(),
            frame->eye.Z(),
            frame->forward.X(),
            frame->forward.Y(),
            frame->forward.Z(),
            frame->right.X(),
            frame->right.Y(),
            frame->right.Z(),
            frame->up.X(),
            frame->up.Y(),
            frame->up.Z(),
            pawn_location ? pawn_location->X() : 0.0,
            pawn_location ? pawn_location->Y() : 0.0,
            pawn_location ? pawn_location->Z() : 0.0,
            mesh_location ? mesh_location->X() : 0.0,
            mesh_location ? mesh_location->Y() : 0.0,
            mesh_location ? mesh_location->Z() : 0.0,
            camera_to_target_len,
            dot(camera_to_target, frame->forward),
            dot(camera_to_target, frame->right),
            dot(camera_to_target, frame->up),
            camera_target_angle,
            frame->source,
            frame->fov_degrees,
            frame->fov_source,
            frame->camera_fov_degrees,
            viewport.width,
            viewport.height);

        const auto hit_start = SteadyClock::now();
        const std::vector<std::optional<Color>> dummy_colors(1, Color{0.34, 0.36, 0.32, 0.96, 0.0});
        ScreenHitCollectionStats coarse_stats{};
        auto coarse_samples = collect_screen_hit_samples(component,
                                                         pawn,
                                                         mesh,
                                                         controller,
                                                         viewport,
                                                         dummy_colors,
                                                         1,
                                                         1,
                                                         ScreenProjectionGridX,
                                                         ScreenProjectionGridY,
                                                         false,
                                                         state,
                                                         coarse_stats,
                                                         0.0,
                                                         1.0,
                                                         0.0,
                                                         1.0,
                                                         0,
                                                         0,
                                                         false);
        log_screen_hit_stats(STR("screen_body_coarse"), STR("viewport_pixels"), coarse_stats);
        bool use_normalized_coords = false;
        ScreenHitCollectionStats normalized_coarse_stats{};
        std::vector<ScreenHitSample> normalized_coarse_samples{};
        if (coarse_samples.empty())
        {
            normalized_coarse_samples = collect_screen_hit_samples(component,
                                                                   pawn,
                                                                   mesh,
                                                                   controller,
                                                                   viewport,
                                                                   dummy_colors,
                                                                   1,
                                                                   1,
                                                                   ScreenProjectionGridX,
                                                                   ScreenProjectionGridY,
                                                                   true,
                                                                   state,
                                                                   normalized_coarse_stats,
                                                                   0.0,
                                                                   1.0,
                                                                   0.0,
                                                                   1.0,
                                                                   0,
                                                                   0,
                                                                   false);
            log_screen_hit_stats(STR("screen_body_coarse"), STR("normalized_0_1"), normalized_coarse_stats);
            if (!normalized_coarse_samples.empty())
            {
                use_normalized_coords = true;
            }
        }

        auto& active_coarse_stats = use_normalized_coords ? normalized_coarse_stats : coarse_stats;
        auto& active_coarse_samples = use_normalized_coords ? normalized_coarse_samples : coarse_samples;
        if (active_coarse_samples.empty())
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_body_no_body_hits");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} input_ok=1 target_resolve_ok=1 body_hits=0 coarse_attempts={} coarse_hits={} normalized_attempts={} normalized_hits={} first_failure={} normalized_first_failure={} no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1 no_screen_projection_fallback=1 no_trace_color_fallback=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                coarse_stats.attempts,
                coarse_stats.hit_uv_count,
                normalized_coarse_stats.attempts,
                normalized_coarse_stats.hit_uv_count,
                coarse_stats.first_failure.empty() ? STR("<none>") : coarse_stats.first_failure,
                normalized_coarse_stats.first_failure.empty() ? STR("<none>") : normalized_coarse_stats.first_failure);
            return false;
        }

        const auto pad_x = 0.012;
        const auto pad_y = 0.018;
        const auto min_nx = clamp(active_coarse_stats.min_nx - pad_x, 0.0, 1.0);
        const auto max_nx = clamp(active_coarse_stats.max_nx + pad_x, 0.0, 1.0);
        const auto min_ny = clamp(active_coarse_stats.min_ny - pad_y, 0.0, 1.0);
        const auto max_ny = clamp(active_coarse_stats.max_ny + pad_y, 0.0, 1.0);
        const auto bbox_w_px = std::max(1.0, (max_nx - min_nx) * static_cast<double>(viewport.width));
        const auto bbox_h_px = std::max(1.0, (max_ny - min_ny) * static_cast<double>(viewport.height));
        constexpr int target_paint_hits = 30000;
        constexpr int min_paint_hits = 2048;
        constexpr int hard_max_attempts = 45000;
        const auto bbox_aspect = clamp(bbox_w_px / std::max(1.0, bbox_h_px), 0.25, 4.0);
        auto refine_grid_x = std::max(24, static_cast<int>(std::round(std::sqrt(static_cast<double>(hard_max_attempts) * bbox_aspect))));
        auto refine_grid_y = std::max(24, static_cast<int>(std::ceil(static_cast<double>(hard_max_attempts) / static_cast<double>(refine_grid_x))));
        while (refine_grid_x * refine_grid_y > hard_max_attempts && refine_grid_y > 24)
        {
            --refine_grid_y;
        }
        while (refine_grid_x * refine_grid_y > hard_max_attempts && refine_grid_x > 24)
        {
            --refine_grid_x;
        }
        ScreenHitCollectionStats refined_stats{};
        auto samples = collect_screen_hit_samples(component,
                                                  pawn,
                                                  mesh,
                                                  controller,
                                                  viewport,
                                                  dummy_colors,
                                                  1,
                                                  1,
                                                  refine_grid_x,
                                                  refine_grid_y,
                                                  use_normalized_coords,
                                                  state,
                                                  refined_stats,
                                                  min_nx,
                                                  max_nx,
                                                  min_ny,
                                                  max_ny,
                                                  target_paint_hits,
                                                  hard_max_attempts,
                                                  false);
        const auto hit_ms = elapsed_ms_since(hit_start);
        log_screen_hit_stats(STR("screen_body_refined"),
                             use_normalized_coords ? STR("normalized_0_1") : STR("viewport_pixels"),
                             refined_stats);
        if (static_cast<int>(samples.size()) < min_paint_hits)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_body_quality_insufficient");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} samples={} min_samples={} target_samples={} hard_max_attempts={} bbox_norm=({}, {})-({}, {}) bbox_px={}x{} refine_grid={}x{} hit_ms={} no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                samples.size(),
                min_paint_hits,
                target_paint_hits,
                hard_max_attempts,
                min_nx,
                min_ny,
                max_nx,
                max_ny,
                bbox_w_px,
                bbox_h_px,
                refine_grid_x,
                refine_grid_y,
                hit_ms);
            return false;
        }

        const auto projected_capture_stats = assign_projected_capture_coords(controller, viewport, samples);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} screen_body projected_capture_coords ok={} failed={} out_of_view={} delta_avg_px={} delta_max_px={} first_failure={}\n"),
            ModTag,
            projected_capture_stats.ok,
            projected_capture_stats.failed,
            projected_capture_stats.out_of_view,
            projected_capture_stats.ok > 0
                ? projected_capture_stats.delta_sum_px / static_cast<double>(projected_capture_stats.ok)
                : 0.0,
            projected_capture_stats.delta_max_px,
            projected_capture_stats.first_failure.empty() ? STR("<none>") : projected_capture_stats.first_failure);

        const auto trace_start = SteadyClock::now();
        std::vector<std::optional<Color>> traced_material_colors(samples.size());
        std::vector<uint8_t> traced_floor_like(samples.size(), 0);
        int trace_background_hits = 0;
        int trace_background_misses = 0;
        int trace_floor_hits = 0;
        int trace_self_skips = 0;
        int trace_channel_attempts = 0;
        double trace_distance_sum = 0.0;
        double trace_distance_max = 0.0;
        double trace_forward_sum = 0.0;
        double trace_right_sum = 0.0;
        double trace_right_abs_sum = 0.0;
        double trace_up_sum = 0.0;
        double trace_up_abs_sum = 0.0;
        double trace_project_delta_sum = 0.0;
        double trace_project_delta_max = 0.0;
        int trace_project_samples = 0;
        const auto trace_project_stride = std::max<size_t>(1, samples.size() / 512);
        for (size_t i = 0; i < samples.size(); ++i)
        {
            if (state.cancelled)
            {
                break;
            }
            const auto traced = trace_nearest_background_behind_sample(pawn, frame->eye, samples[i]);
            trace_self_skips += traced.self_skips;
            trace_channel_attempts += traced.channel_attempts;
            if (!traced.hit)
            {
                ++trace_background_misses;
                continue;
            }
            ++trace_background_hits;
            if (traced.floor_like)
            {
                ++trace_floor_hits;
                traced_floor_like[i] = 1;
            }
            trace_distance_sum += traced.distance;
            trace_distance_max = std::max(trace_distance_max, traced.distance);
            const auto background_delta = sub(traced.trace.location, samples[i].world_position);
            const auto forward_offset = dot(background_delta, frame->forward);
            const auto right_offset = dot(background_delta, frame->right);
            const auto up_offset = dot(background_delta, frame->up);
            trace_forward_sum += forward_offset;
            trace_right_sum += right_offset;
            trace_right_abs_sum += std::abs(right_offset);
            trace_up_sum += up_offset;
            trace_up_abs_sum += std::abs(up_offset);
            if (i % trace_project_stride == 0)
            {
                const auto projected_background = project_world_location_to_screen(controller, traced.trace.location, false);
                if (projected_background.ok)
                {
                    const auto dx = projected_background.x - samples[i].screen_x;
                    const auto dy = projected_background.y - samples[i].screen_y;
                    const auto delta_px = std::sqrt(dx * dx + dy * dy);
                    trace_project_delta_sum += delta_px;
                    trace_project_delta_max = std::max(trace_project_delta_max, delta_px);
                    ++trace_project_samples;
                }
            }
            traced_material_colors[i] = traced.color;
        }
        const auto trace_ms = elapsed_ms_since(trace_start);
        state.background_trace_hits = trace_background_hits;
        state.body_trace_hits = refined_stats.hit_success;
        state.uv_hits = refined_stats.hit_uv_count;

        const auto rt_width = std::max(1, viewport.width);
        const auto rt_height = std::max(1, viewport.height);
        const int alignment_rt_width = std::max(
            320,
            std::min(rt_width, 1280));
        const int alignment_rt_height = std::max(
            180,
            static_cast<int>(std::round(static_cast<double>(alignment_rt_width) *
                                        static_cast<double>(rt_height) /
                                        static_cast<double>(std::max(1, rt_width)))));
        const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
        std::vector<double> fov_candidates{};
        const auto add_fov_candidate = [&](double fov) {
            if (!std::isfinite(fov) || fov < 10.0 || fov > 150.0)
            {
                return;
            }
            for (const auto existing : fov_candidates)
            {
                if (std::abs(existing - fov) < 0.25)
                {
                    return;
                }
            }
            fov_candidates.push_back(fov);
        };
        add_fov_candidate(frame->fov_degrees);
        add_fov_candidate(frame->camera_fov_degrees);
        add_fov_candidate(frame->deproject_hfov);
        add_fov_candidate(frame->deproject_vfov);
        add_fov_candidate(frame->legacy_linear_hfov);
        add_fov_candidate(90.0);
        const auto negative_mask_samples =
            make_body_mask_negative_samples(min_nx, min_ny, max_nx, max_ny, viewport.width, viewport.height, 64);
        const auto alignment_start = SteadyClock::now();
        auto alignment = find_best_body_mask_pixel_alignment(pawn,
                                                             frame->eye,
                                                             look_at,
                                                             samples,
                                                             negative_mask_samples,
                                                             alignment_rt_width,
                                                             alignment_rt_height,
                                                             state,
                                                             fov_candidates,
                                                             frame->has_rotation ? &frame->rotation : nullptr);
        const double alignment_ms = elapsed_ms_since(alignment_start);
        const bool mask_alignment_ok = !alignment.sky_misalign_suspect &&
                                       alignment.mask_positive_rate >= 0.48 &&
                                       alignment.mask_negative_rate <= 0.50 &&
                                       alignment.projection_align_score >= 0.18;
        const ScreenTransform alignment_transform = alignment.transform;
        const double alignment_fov = alignment.selected_fov_degrees > 0.0 ? alignment.selected_fov_degrees
                                                                          : frame->fov_degrees;
        ScreenTransform capture_transform{};
        double capture_fov = frame->fov_degrees;
        StringType capture_transform_backend = STR("tps_deproject_frame");
        if (mask_alignment_ok)
        {
            capture_transform = alignment_transform;
            capture_fov = alignment_fov;
            capture_transform_backend = alignment.backend;
        }
        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} screen_body capture_alignment validator=show_only_body_mask validator_blocking=0 selected_transform_backend={} selected_fov={} alignment_ok={} alignment_score={} positive_rate={} negative_rate={} positive_hits={} negative_hits={} alignment_runner_up={} alignment_runner_up_fov={} alignment_runner_up_score={} alignment_ratio={} alignment_samples={} negative_samples={} alignment_candidates={} alignment_transform_scale=({}, {}) alignment_transform_offset=({}, {}) alignment_transform_flip=({}, {}) capture_transform_backend={} capture_fov={} capture_transform_scale=({}, {}) capture_transform_offset=({}, {}) capture_transform_flip=({}, {}) alignment_rt={}x{} final_rt={}x{} fov_candidates={} t_alignment_ms={}\n"),
            ModTag,
            alignment.backend,
            alignment_fov,
            mask_alignment_ok ? 1 : 0,
            alignment.projection_align_score,
            alignment.mask_positive_rate,
            alignment.mask_negative_rate,
            alignment.mask_positive_hits,
            alignment.mask_negative_hits,
            alignment.runner_up_backend,
            alignment.runner_up_fov_degrees,
            alignment.runner_up_score,
            alignment.score_ratio,
            alignment.samples,
            negative_mask_samples.size(),
            alignment.candidate_count,
            alignment_transform.scale_x,
            alignment_transform.scale_y,
            alignment_transform.offset_x,
            alignment_transform.offset_y,
            alignment_transform.flip_x ? 1 : 0,
            alignment_transform.flip_y ? 1 : 0,
            capture_transform_backend,
            capture_fov,
            capture_transform.scale_x,
            capture_transform.scale_y,
            capture_transform.offset_x,
            capture_transform.offset_y,
            capture_transform.flip_x ? 1 : 0,
            capture_transform.flip_y ? 1 : 0,
            alignment_rt_width,
            alignment_rt_height,
            rt_width,
            rt_height,
            fov_candidates.size(),
            alignment_ms);
        if (!mask_alignment_ok)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body continuing reason={} mask_alignment_ok=0 backend={} score={} positive_rate={} negative_rate={} rejected_fov={} rejected_transform_scale=({}, {}) rejected_transform_offset=({}, {}) fallback_capture_backend={} fallback_capture_fov={} fallback_transform_scale=({}, {}) fallback_transform_offset=({}, {}) trace_hits={} no_import=0 no_clear=1 no_commit=1 no_mesh_hide=1 no_palette_fallback=1 no_trace_color_fallback=1 validator_non_blocking=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                alignment.backend,
                alignment.projection_align_score,
                alignment.mask_positive_rate,
                alignment.mask_negative_rate,
                alignment_fov,
                alignment_transform.scale_x,
                alignment_transform.scale_y,
                alignment_transform.offset_x,
                alignment_transform.offset_y,
                capture_transform_backend,
                capture_fov,
                capture_transform.scale_x,
                capture_transform.scale_y,
                capture_transform.offset_x,
                capture_transform.offset_y,
                trace_background_hits);
        }

        const auto capture_start = SteadyClock::now();
        CaptureGridDiagnostics pixel_diag{};
        auto bulk_capture = capture_render_target_image(pawn,
                                                        frame->eye,
                                                        look_at,
                                                        rt_width,
                                                        rt_height,
                                                        true,
                                                        state,
                                                        capture_fov,
                                                        frame->has_rotation ? &frame->rotation : nullptr,
                                                        &samples,
                                                        &capture_transform,
                                                        128);
        pixel_diag = bulk_capture.diagnostics;
        std::vector<std::optional<Color>> background_colors(samples.size());
        bool image_ok = bulk_capture.image.ok;
        StringType image_failure = bulk_capture.image.failure.empty() ? STR("<none>") : bulk_capture.image.failure;
        bool image_bulk_calibration_ok = bulk_capture.image.bulk_calibration_ok;
        ScreenTransform image_bulk_transform = bulk_capture.image.bulk_to_pixel_transform;
        StringType readback_backend = mask_alignment_ok ? STR("mask_validated_scene_capture_bulk_image")
                                                       : STR("tps_deproject_scene_capture_bulk_image");
        if (bulk_capture.image.ok && bulk_capture.image.bulk_calibration_ok)
        {
            background_colors = sample_hidden_background_from_image(bulk_capture.image,
                                                                    samples,
                                                                    bulk_capture.image.bulk_to_pixel_transform);
            pixel_diag.read_pixels = bulk_capture.image.decoded_pixels;
            pixel_diag.missing_pixels = 0;
        }
        else
        {
            background_colors = capture_screen_sample_colors(pawn,
                                                             frame->eye,
                                                             look_at,
                                                             samples,
                                                             rt_width,
                                                             rt_height,
                                                             true,
                                                             state,
                                                             capture_fov,
                                                             &pixel_diag,
                                                             capture_transform,
                                                             frame->has_rotation ? &frame->rotation : nullptr);
            readback_backend = mask_alignment_ok ? STR("mask_validated_scene_capture_pixel_api_after_bulk_unverified")
                                                 : STR("tps_deproject_scene_capture_pixel_api_after_bulk_unverified");
        }
        const auto capture_ms = elapsed_ms_since(capture_start);
        const auto color_summary = summarize_capture_colors(background_colors);
        const auto color_quality = summarize_capture_quality(background_colors);
        state.background_pixels = color_summary.pixels;
        state.capture_pixels_ready = color_summary.pixels > 0 && !color_summary.uniform && !color_summary.clear_suspect;
        if (trace_background_hits < min_paint_hits)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_body_background_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} background_pixels={} trace_hits={} min_pixels={} readback_backend={} image_ok={} image_failure={} pixel_reads={} pixel_missing={} capture_ms={} trace_ms={} trace_primary=1 no_scene_capture=0 no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1 no_trace_color_fallback=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                color_summary.pixels,
                trace_background_hits,
                min_paint_hits,
                readback_backend,
                image_ok ? 1 : 0,
                image_failure,
                pixel_diag.read_pixels,
                pixel_diag.missing_pixels,
                capture_ms,
                trace_ms);
            return false;
        }
        if (color_summary.pixels < min_paint_hits || color_summary.uniform || color_summary.clear_suspect)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_body_texture_color_unavailable_no_paint");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} texture_source_unverified=1 background_pixels={} min_pixels={} uniform={} clear_suspect={} near_uniform={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) color_score={} avg_chroma={} luma_range={} rgb_range={} readback_backend={} pixel_reads={} pixel_missing={} selected_fov={} transform_scale=({}, {}) transform_offset=({}, {}) capture_ms={} no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1 no_palette_fallback=1 no_trace_color_fallback=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                color_summary.pixels,
                min_paint_hits,
                color_summary.uniform ? 1 : 0,
                color_summary.clear_suspect ? 1 : 0,
                color_summary.near_uniform_samples,
                color_summary.min_r,
                color_summary.min_g,
                color_summary.min_b,
                color_summary.avg_r,
                color_summary.avg_g,
                color_summary.avg_b,
                color_summary.max_r,
                color_summary.max_g,
                color_summary.max_b,
                color_quality.score,
                color_quality.avg_chroma,
                color_quality.luma_range,
                color_quality.rgb_range,
                readback_backend,
                pixel_diag.read_pixels,
                pixel_diag.missing_pixels,
                capture_fov,
                capture_transform.scale_x,
                capture_transform.scale_y,
                capture_transform.offset_x,
                capture_transform.offset_y,
                capture_ms);
            return false;
        }

        struct ResolvedPaintSeed
        {
            double u{0.0};
            double v{0.0};
            Color color{};
            bool floor_like{false};
        };

        const auto export_start = SteadyClock::now();
        const auto albedo_before = export_channel_bytes(component, 0);
        const auto metallic_before = export_channel_bytes(component, 1);
        const auto roughness_before = export_channel_bytes(component, 2);
        const auto export_ms = elapsed_ms_since(export_start);
        if (!rgba_buffer_ready(albedo_before) || !rgba_buffer_ready(metallic_before) || !rgba_buffer_ready(roughness_before))
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_body_export_failed");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} export_ok=({}, {}, {}) failures=({}, {}, {}) no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                albedo_before.ok ? 1 : 0,
                metallic_before.ok ? 1 : 0,
                roughness_before.ok ? 1 : 0,
                albedo_before.failure.empty() ? STR("<none>") : albedo_before.failure,
                metallic_before.failure.empty() ? STR("<none>") : metallic_before.failure,
                roughness_before.failure.empty() ? STR("<none>") : roughness_before.failure);
            return false;
        }

        const auto seed_start = SteadyClock::now();
        std::vector<ResolvedPaintSeed> paint_seeds{};
        paint_seeds.reserve(samples.size());
        int missing_color = 0;
        int material_values = 0;
        int capture_color_used = 0;
        int trace_only_color_used = 0;
        int capture_trace_pairs = 0;
        double capture_trace_chroma_sum = 0.0;
        double capture_trace_chroma_max = 0.0;
        double roughness_min = 1.0;
        double roughness_max = 0.0;
        double roughness_sum = 0.0;
        double metallic_min = 1.0;
        double metallic_max = 0.0;
        double metallic_sum = 0.0;
        for (size_t i = 0; i < samples.size(); ++i)
        {
            if (state.cancelled)
            {
                break;
            }
            const auto has_capture_color = i < background_colors.size() && background_colors[i];
            const auto has_traced_color = i < traced_material_colors.size() && traced_material_colors[i];
            if (!has_capture_color)
            {
                ++missing_color;
                continue;
            }
            const auto floor_like = samples[i].floor_like || (i < traced_floor_like.size() && traced_floor_like[i] != 0);
            Color material_hint = has_traced_color ? *traced_material_colors[i] : Color{0.34, 0.36, 0.32, 0.94, 0.0};
            auto color = sanitize_background_color(*background_colors[i], material_hint);
            if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b))
            {
                ++missing_color;
                continue;
            }
            color.r = clamp(color.r, 0.01, 0.99);
            color.g = clamp(color.g, 0.01, 0.99);
            color.b = clamp(color.b, 0.01, 0.99);
            ++capture_color_used;
            if (has_traced_color)
            {
                const auto traced_material = *traced_material_colors[i];
                if (has_capture_color)
                {
                    const auto distance = chroma_distance_rgb(color, traced_material);
                    capture_trace_chroma_sum += distance;
                    capture_trace_chroma_max = std::max(capture_trace_chroma_max, distance);
                    ++capture_trace_pairs;
                }
                color.roughness = traced_material.roughness;
                color.metallic = traced_material.metallic;
            }
            color = compensate_projected_albedo_preserve_material(color, floor_like);
            roughness_min = std::min(roughness_min, color.roughness);
            roughness_max = std::max(roughness_max, color.roughness);
            roughness_sum += color.roughness;
            metallic_min = std::min(metallic_min, color.metallic);
            metallic_max = std::max(metallic_max, color.metallic);
            metallic_sum += color.metallic;
            ++material_values;

            paint_seeds.push_back(ResolvedPaintSeed{samples[i].u, samples[i].v, color, floor_like});
        }
        const auto seed_ms = elapsed_ms_since(seed_start);

        if (static_cast<int>(paint_seeds.size()) < min_paint_hits)
        {
            state.failures = 1;
            state.last_failure = STR("play_screen_body_seed_quality_insufficient");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} seeds={} min_samples={} missing_color={} capture_color_used={} trace_hits={} no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                static_cast<int>(paint_seeds.size()),
                min_paint_hits,
                missing_color,
                capture_color_used,
                trace_background_hits);
            return false;
        }

        const auto atlas_start = SteadyClock::now();
        const auto texture_pixels = static_cast<size_t>(std::max(0, albedo_before.width)) *
                                    static_cast<size_t>(std::max(0, albedo_before.height));
        std::vector<ProjectionTexel> direct_texels(texture_pixels);
        for (const auto& seed : paint_seeds)
        {
            const auto priority = seed.floor_like ? 12 : 11;
            const auto weight = seed.floor_like ? 88.0 : 72.0;
            splat_projection_texel(direct_texels,
                                   albedo_before.width,
                                   albedo_before.height,
                                   seed.u,
                                   seed.v,
                                   seed.color,
                                   weight,
                                   priority,
                                   seed.floor_like,
                                   2);
        }
        std::vector<ProjectionTexel> texels{};
        const auto terminal_fill = edge_terminal_extrude_projection_texels(direct_texels,
                                                                           albedo_before.width,
                                                                           albedo_before.height,
                                                                           texels);

        auto albedo_target = albedo_before.bytes;
        auto metallic_target = metallic_before.bytes;
        auto roughness_target = roughness_before.bytes;
        struct TextureWriteStats
        {
            int uv_coverage{0};
            int filled_by_direct{0};
            int filled_by_extension{0};
            int filled_by_floor{0};
            int preserved_original{0};
        };
        const auto worker_count = worker_count_for_items(texture_pixels);
        std::vector<TextureWriteStats> worker_stats(static_cast<size_t>(worker_count));
        const auto write_scalar = [](std::vector<uint8_t>& bytes, int x, int y, int width, double value) {
            const auto offset = static_cast<size_t>(y * width + x) * 4;
            if (offset < bytes.size())
            {
                bytes[offset] = byte_from_unit(value);
            }
        };
        parallel_ranges(texture_pixels, [&](size_t begin, size_t end, unsigned worker) {
            auto& local = worker_stats[static_cast<size_t>(worker)];
            for (size_t index = begin; index < end; ++index)
            {
                if (index >= texels.size())
                {
                    continue;
                }
                const auto y = static_cast<int>(index / static_cast<size_t>(std::max(1, albedo_before.width)));
                const auto x = static_cast<int>(index - static_cast<size_t>(y * albedo_before.width));
                const auto offset = index * 4;
                const auto& source_texel = texels[index];
                if (!projection_texel_painted(source_texel) || offset + 2 >= albedo_target.size())
                {
                    ++local.preserved_original;
                    continue;
                }

                const auto inv = 1.0 / source_texel.weight;
                const auto r = clamp(source_texel.r * inv, 0.02, 0.98);
                const auto g = clamp(source_texel.g * inv, 0.02, 0.98);
                const auto b = clamp(source_texel.b * inv, 0.02, 0.98);
                const auto roughness = clamp(source_texel.roughness * inv, 0.0, 1.0);
                const auto metallic = clamp(source_texel.metallic * inv, 0.0, 1.0);

                albedo_target[offset + 0] = byte_from_unit(r);
                albedo_target[offset + 1] = byte_from_unit(g);
                albedo_target[offset + 2] = byte_from_unit(b);
                if (x < metallic_before.width && y < metallic_before.height)
                {
                    write_scalar(metallic_target, x, y, metallic_before.width, metallic);
                }
                if (x < roughness_before.width && y < roughness_before.height)
                {
                    write_scalar(roughness_target, x, y, roughness_before.width, roughness);
                }

                ++local.uv_coverage;
                if (source_texel.priority >= 8)
                {
                    ++local.filled_by_direct;
                }
                else
                {
                    ++local.filled_by_extension;
                }
                if (source_texel.floor_like)
                {
                    ++local.filled_by_floor;
                }
            }
        });
        TextureWriteStats write_stats{};
        for (const auto& local : worker_stats)
        {
            write_stats.uv_coverage += local.uv_coverage;
            write_stats.filled_by_direct += local.filled_by_direct;
            write_stats.filled_by_extension += local.filled_by_extension;
            write_stats.filled_by_floor += local.filled_by_floor;
            write_stats.preserved_original += local.preserved_original;
        }
        const auto atlas_ms = elapsed_ms_since(atlas_start);

        const auto import_start = SteadyClock::now();
        const auto hash_before = hash_component_paint_state(component);
        const auto albedo_target_hash = hash_bytes(albedo_target);
        const auto metallic_target_hash = hash_bytes(metallic_target);
        const auto roughness_target_hash = hash_bytes(roughness_target);
        const auto albedo_changed = changed_byte_count(albedo_before.bytes, albedo_target);
        const auto metallic_changed = changed_byte_count(metallic_before.bytes, metallic_target);
        const auto roughness_changed = changed_byte_count(roughness_before.bytes, roughness_target);
        const auto rgb_summary = summarize_rgb_bytes(albedo_target, albedo_before.width, albedo_before.height);
        const auto metallic_summary = summarize_scalar_bytes(metallic_target, metallic_before.width, metallic_before.height);
        const auto roughness_summary = summarize_scalar_bytes(roughness_target, roughness_before.width, roughness_before.height);

        StringType albedo_import_failure{};
        StringType metallic_import_failure{};
        StringType roughness_import_failure{};
        const auto albedo_import_ok = import_channel_bytes(component, 0, albedo_target, albedo_import_failure);
        const auto metallic_import_ok = import_channel_bytes(component, 1, metallic_target, metallic_import_failure);
        const auto roughness_import_ok = import_channel_bytes(component, 2, roughness_target, roughness_import_failure);

        const auto albedo_after = export_channel_bytes(component, 0);
        const auto metallic_after = export_channel_bytes(component, 1);
        const auto roughness_after = export_channel_bytes(component, 2);
        const auto albedo_observed = albedo_after.ok && albedo_after.hash == albedo_target_hash;
        const auto metallic_observed = metallic_after.ok && metallic_after.hash == metallic_target_hash;
        const auto roughness_observed = roughness_after.ok && roughness_after.hash == roughness_target_hash;
        const auto hash_after = hash_component_paint_state(component);
        const auto all_observed = albedo_import_ok && metallic_import_ok && roughness_import_ok &&
                                  albedo_observed && metallic_observed && roughness_observed;
        const auto changed = hash_after != hash_before;
        const auto import_ms = elapsed_ms_since(import_start);
        const auto total_ms = elapsed_ms_since(total_start);

        state.queued_strokes = 0;
        state.success = (albedo_import_ok && albedo_observed ? 1 : 0) +
                        (metallic_import_ok && metallic_observed ? 1 : 0) +
                        (roughness_import_ok && roughness_observed ? 1 : 0);
        state.failures = 3 - state.success;
        state.paint_uv_success = state.success;
        state.visible_samples = static_cast<int>(paint_seeds.size());
        state.uv_hits = refined_stats.hit_uv_count;
        state.body_trace_hits = refined_stats.hit_success;
        state.atlas_bins = static_cast<int>(paint_seeds.size());
        state.paint_state_hash_before = hash_before;
        state.paint_state_hash_after = hash_after;
        state.verified_visible_backend = all_observed && changed;
        state.verified_paint_function = STR("ImportChannelFromBytes.screen_body_edge_terminal_extrude");
        state.last_failure = state.verified_visible_backend ? STR("play_screen_body_edge_terminal_extrude_applied")
                                                            : STR("play_screen_body_edge_terminal_extrude_unverified");

        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} play screen_body result reason={} success={} visible_backend={} queued_strokes=0 import_backend=1 albedo_import_ok={} albedo_observed={} metallic_import_ok={} metallic_observed={} roughness_import_ok={} roughness_observed={} missing_color={} hash_before={} hash_after={} hash_changed={} body_samples={} paint_seeds={} target_samples={} min_samples={} hard_max_attempts={} coarse_hits={} refined_hits={} bbox_norm=({}, {})-({}, {}) bbox_px={}x{} refine_grid={}x{} rt_size={}x{} texture_size={}x{} uv_coverage={} filled_by_direct={} filled_by_extension={} filled_by_floor={} direct_texels={} edge_texels={} extruded_texels={} fallback_extruded_texels={} preserved_direct={} preserved_original={} worker_threads={} readback_backend={} trace_primary=1 no_scene_capture=0 texture_source_verified=1 capture_color_used={} trace_only_color_used={} capture_trace_pairs={} capture_trace_chroma_avg={} capture_trace_chroma_max={} selected_capture_fov={} capture_transform_backend={} capture_transform_scale=({}, {}) capture_transform_offset=({}, {}) capture_transform_flip=({}, {}) image_ok={} image_failure={} image_bulk_calibration_ok={} image_bulk_transform_flip=({}, {}) image_bulk_transform_scale=({}, {}) image_bulk_transform_offset=({}, {}) background_pixels={} capture_uniform={} capture_clear_suspect={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) metallic_avg={} roughness_avg={} channel={} label=ImportChannelFromBytes first_failure={} t_hit_ms={} t_trace_ms={} t_alignment_ms={} t_capture_ms={} t_export_ms={} t_seed_ms={} t_atlas_ms={} t_import_ms={} t_total_ms={} frame_fov={} fov_source={} camera_fov={} deproject_hfov={} viewport={}x{} no_clear=1 no_commit=1 no_mesh_hide=1 no_palette_fallback=1 no_trace_color_fallback=1 uv_extend=1 fill_mode=edge_terminal_extrude direct_layer_priority=1\n"),
            ModTag,
            reason.empty() ? STR("<none>") : reason,
            state.success,
            state.verified_visible_backend ? 1 : 0,
            albedo_import_ok ? 1 : 0,
            albedo_observed ? 1 : 0,
            metallic_import_ok ? 1 : 0,
            metallic_observed ? 1 : 0,
            roughness_import_ok ? 1 : 0,
            roughness_observed ? 1 : 0,
            missing_color,
            hash_before,
            hash_after,
            changed ? 1 : 0,
            samples.size(),
            static_cast<int>(paint_seeds.size()),
            target_paint_hits,
            min_paint_hits,
            hard_max_attempts,
            active_coarse_stats.hit_uv_count,
            refined_stats.hit_uv_count,
            min_nx,
            min_ny,
            max_nx,
            max_ny,
            bbox_w_px,
            bbox_h_px,
            refine_grid_x,
            refine_grid_y,
            rt_width,
            rt_height,
            albedo_before.width,
            albedo_before.height,
            write_stats.uv_coverage,
            write_stats.filled_by_direct,
            write_stats.filled_by_extension,
            write_stats.filled_by_floor,
            terminal_fill.direct_texels,
            terminal_fill.edge_texels,
            terminal_fill.extruded_texels,
            terminal_fill.fallback_extruded_texels,
            terminal_fill.preserved_direct,
            write_stats.preserved_original,
            worker_count,
            readback_backend,
            capture_color_used,
            trace_only_color_used,
            capture_trace_pairs,
            capture_trace_pairs > 0 ? capture_trace_chroma_sum / static_cast<double>(capture_trace_pairs) : 0.0,
            capture_trace_chroma_max,
            capture_fov,
            capture_transform_backend,
            capture_transform.scale_x,
            capture_transform.scale_y,
            capture_transform.offset_x,
            capture_transform.offset_y,
            capture_transform.flip_x ? 1 : 0,
            capture_transform.flip_y ? 1 : 0,
            image_ok ? 1 : 0,
            image_failure,
            image_bulk_calibration_ok ? 1 : 0,
            image_bulk_transform.flip_x ? 1 : 0,
            image_bulk_transform.flip_y ? 1 : 0,
            image_bulk_transform.scale_x,
            image_bulk_transform.scale_y,
            image_bulk_transform.offset_x,
            image_bulk_transform.offset_y,
            color_summary.pixels,
            color_summary.uniform ? 1 : 0,
            color_summary.clear_suspect ? 1 : 0,
            color_summary.min_r,
            color_summary.min_g,
            color_summary.min_b,
            color_summary.avg_r,
            color_summary.avg_g,
            color_summary.avg_b,
            color_summary.max_r,
            color_summary.max_g,
            color_summary.max_b,
            metallic_summary.avg_value,
            roughness_summary.avg_value,
            PaintChannelAlbedoMetallicRoughness,
            state.last_failure,
            hit_ms,
            trace_ms,
            alignment_ms,
            capture_ms,
            export_ms,
            seed_ms,
            atlas_ms,
            import_ms,
            total_ms,
            frame->fov_degrees,
            frame->fov_source,
            frame->camera_fov_degrees,
            frame->deproject_hfov,
            viewport.width,
            viewport.height);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play screen_body projection_material projected_ok={} projected_failed={} projected_out_of_view={} projected_delta_avg_px={} projected_delta_max_px={} trace_hits={} trace_misses={} trace_floor_hits={} trace_self_skips={} trace_channel_attempts={} trace_distance_avg={} trace_distance_max={} trace_forward_avg={} trace_right_avg={} trace_right_abs_avg={} trace_up_avg={} trace_up_abs_avg={} trace_project_samples={} trace_project_delta_avg_px={} trace_project_delta_max_px={} roughness_samples={} roughness_min={} roughness_avg={} roughness_max={} metallic_min={} metallic_avg={} metallic_max={} t_trace_ms={} readback_backend={}\n"),
            ModTag,
            projected_capture_stats.ok,
            projected_capture_stats.failed,
            projected_capture_stats.out_of_view,
            projected_capture_stats.ok > 0
                ? projected_capture_stats.delta_sum_px / static_cast<double>(projected_capture_stats.ok)
                : 0.0,
            projected_capture_stats.delta_max_px,
            trace_background_hits,
            trace_background_misses,
            trace_floor_hits,
            trace_self_skips,
            trace_channel_attempts,
            trace_background_hits > 0 ? trace_distance_sum / static_cast<double>(trace_background_hits) : 0.0,
            trace_distance_max,
            trace_background_hits > 0 ? trace_forward_sum / static_cast<double>(trace_background_hits) : 0.0,
            trace_background_hits > 0 ? trace_right_sum / static_cast<double>(trace_background_hits) : 0.0,
            trace_background_hits > 0 ? trace_right_abs_sum / static_cast<double>(trace_background_hits) : 0.0,
            trace_background_hits > 0 ? trace_up_sum / static_cast<double>(trace_background_hits) : 0.0,
            trace_background_hits > 0 ? trace_up_abs_sum / static_cast<double>(trace_background_hits) : 0.0,
            trace_project_samples,
            trace_project_samples > 0 ? trace_project_delta_sum / static_cast<double>(trace_project_samples) : 0.0,
            trace_project_delta_max,
            material_values,
            material_values > 0 ? roughness_min : 0.0,
            material_values > 0 ? roughness_sum / static_cast<double>(material_values) : 0.0,
            material_values > 0 ? roughness_max : 0.0,
            material_values > 0 ? metallic_min : 0.0,
            material_values > 0 ? metallic_sum / static_cast<double>(material_values) : 0.0,
            material_values > 0 ? metallic_max : 0.0,
            trace_ms,
            readback_backend);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play screen_body import_verify albedo_changed_bytes={} metallic_changed_bytes={} roughness_changed_bytes={} albedo_hash_before={} albedo_hash_target={} albedo_hash_after={} metallic_hash_before={} metallic_hash_target={} metallic_hash_after={} roughness_hash_before={} roughness_hash_target={} roughness_hash_after={} albedo_failure={} metallic_failure={} roughness_failure={} albedo_after_failure={} metallic_after_failure={} roughness_after_failure={}\n"),
            ModTag,
            albedo_changed,
            metallic_changed,
            roughness_changed,
            albedo_before.hash,
            albedo_target_hash,
            albedo_after.hash,
            metallic_before.hash,
            metallic_target_hash,
            metallic_after.hash,
            roughness_before.hash,
            roughness_target_hash,
            roughness_after.hash,
            albedo_import_failure.empty() ? STR("<none>") : albedo_import_failure,
            metallic_import_failure.empty() ? STR("<none>") : metallic_import_failure,
            roughness_import_failure.empty() ? STR("<none>") : roughness_import_failure,
            albedo_after.failure.empty() ? STR("<none>") : albedo_after.failure,
            metallic_after.failure.empty() ? STR("<none>") : metallic_after.failure,
            roughness_after.failure.empty() ? STR("<none>") : roughness_after.failure);
        return state.verified_visible_backend;
    }

    auto apply_background_palette_import_fallback(Unreal::UObject* component,
                                                  const ChannelByteBuffer& albedo_before,
                                                  const ChannelByteBuffer& metallic_before,
                                                  const ChannelByteBuffer& roughness_before,
                                                  const std::vector<Sample>& background_samples,
                                                  ProbeState& state,
                                                  const StringType& reason) -> bool
    {
        constexpr int atlas_resolution = 96;
        auto palette = build_palette(background_samples);
        auto palette_summary = summarize_palette(palette);
        const auto red_palette_replaced = palette_summary.red_dominant;
        if (red_palette_replaced)
        {
            palette = fallback_camo_palette();
            palette_summary = summarize_palette(palette);
        }
        auto atlas = build_procedural_camo_atlas(atlas_resolution, palette);

        auto albedo_target = albedo_before.bytes;
        auto metallic_target = metallic_before.bytes;
        auto roughness_target = roughness_before.bytes;
        const auto write_scalar = [&](std::vector<uint8_t>& bytes, int x, int y, int width, double value) {
            const auto offset = static_cast<size_t>(y * width + x) * 4;
            if (offset < bytes.size())
            {
                bytes[offset] = byte_from_unit(value);
            }
        };

        for (int y = 0; y < albedo_before.height; ++y)
        {
            for (int x = 0; x < albedo_before.width; ++x)
            {
                const auto u = (static_cast<double>(x) + 0.5) / static_cast<double>(std::max(1, albedo_before.width));
                const auto v = (static_cast<double>(y) + 0.5) / static_cast<double>(std::max(1, albedo_before.height));
                auto color = disruptive_color(atlas, atlas_resolution, palette, u, v, 3);
                const auto contour = std::sin((u * 23.0 + noise01(u, v, 11.0, 0x3119U) * 2.0) * Pi) *
                                     std::cos((v * 19.0 + noise01(u, v, 13.0, 0x72c1U) * 2.0) * Pi);
                color.r = clamp(color.r + contour * 0.020, 0.02, 0.90);
                color.g = clamp(color.g + contour * 0.018, 0.02, 0.90);
                color.b = clamp(color.b + contour * 0.020, 0.02, 0.90);
                color.roughness = clamp(std::max(color.roughness, 0.92), 0.86, 0.99);
                color = infer_surface_material(color, false);

                const auto offset = (static_cast<size_t>(y) * static_cast<size_t>(albedo_before.width) +
                                     static_cast<size_t>(x)) *
                                    4;
                if (offset + 2 < albedo_target.size())
                {
                    albedo_target[offset + 0] = byte_from_unit(color.r);
                    albedo_target[offset + 1] = byte_from_unit(color.g);
                    albedo_target[offset + 2] = byte_from_unit(color.b);
                }
                if (x < metallic_before.width && y < metallic_before.height)
                {
                    write_scalar(metallic_target, x, y, metallic_before.width, color.metallic);
                }
                if (x < roughness_before.width && y < roughness_before.height)
                {
                    write_scalar(roughness_target, x, y, roughness_before.width, color.roughness);
                }
            }
        }

        const auto component_hash_before = hash_component_paint_state(component);
        const auto albedo_target_hash = hash_bytes(albedo_target);
        const auto metallic_target_hash = hash_bytes(metallic_target);
        const auto roughness_target_hash = hash_bytes(roughness_target);
        const auto albedo_changed = changed_byte_count(albedo_before.bytes, albedo_target);
        const auto metallic_changed = changed_byte_count(metallic_before.bytes, metallic_target);
        const auto roughness_changed = changed_byte_count(roughness_before.bytes, roughness_target);
        const auto rgb_summary = summarize_rgb_bytes(albedo_target, albedo_before.width, albedo_before.height);

        StringType albedo_import_failure{};
        StringType metallic_import_failure{};
        StringType roughness_import_failure{};
        const auto albedo_import_ok = import_channel_bytes(component, 0, albedo_target, albedo_import_failure);
        const auto metallic_import_ok = import_channel_bytes(component, 1, metallic_target, metallic_import_failure);
        const auto roughness_import_ok = import_channel_bytes(component, 2, roughness_target, roughness_import_failure);

        const auto albedo_after = export_channel_bytes(component, 0);
        const auto metallic_after = export_channel_bytes(component, 1);
        const auto roughness_after = export_channel_bytes(component, 2);
        const auto albedo_observed = albedo_after.ok && albedo_after.hash == albedo_target_hash;
        const auto metallic_observed = metallic_after.ok && metallic_after.hash == metallic_target_hash;
        const auto roughness_observed = roughness_after.ok && roughness_after.hash == roughness_target_hash;
        const auto all_observed = albedo_import_ok && metallic_import_ok && roughness_import_ok &&
                                  albedo_observed && metallic_observed && roughness_observed;
        const auto component_hash_after = hash_component_paint_state(component);

        state.success = (albedo_import_ok && albedo_observed ? 1 : 0) +
                        (metallic_import_ok && metallic_observed ? 1 : 0) +
                        (roughness_import_ok && roughness_observed ? 1 : 0);
        state.failures = 3 - state.success;
        state.verified_visible_backend = all_observed;
        state.verified_paint_channel = 0;
        state.verified_paint_function = STR("ImportChannelFromBytes");
        state.paint_state_hash_before = component_hash_before;
        state.paint_state_hash_after = component_hash_after;
        state.atlas_bins = static_cast<int>(background_samples.size());
        state.background_pixels = std::max(state.background_pixels, static_cast<int>(background_samples.size()));
        state.capture_pixels_ready = !background_samples.empty();
        state.uv_mapping_ready = false;
        state.last_failure = all_observed ? STR("play_view_projection_fallback_background_palette_applied")
                                          : STR("play_view_projection_fallback_unverified");

        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} play view projection fallback reason={} background_samples={} atlas_resolution={} palette_colors={} red_palette_replaced={} visible_backend={} component_changed={} component_hash_before={} component_hash_after={}\n"),
            ModTag,
            reason.empty() ? STR("<none>") : reason,
            static_cast<int>(background_samples.size()),
            atlas_resolution,
            static_cast<int>(palette.size()),
            red_palette_replaced ? 1 : 0,
            all_observed ? 1 : 0,
            component_hash_before != component_hash_after ? 1 : 0,
            component_hash_before,
            component_hash_after);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} fallback texture summary pixels={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) red_pixels={} near_uniform={} albedo_changed_bytes={} metallic_changed_bytes={} roughness_changed_bytes={}\n"),
            ModTag,
            rgb_summary.pixels,
            rgb_summary.min_r,
            rgb_summary.min_g,
            rgb_summary.min_b,
            rgb_summary.avg_r,
            rgb_summary.avg_g,
            rgb_summary.avg_b,
            rgb_summary.max_r,
            rgb_summary.max_g,
            rgb_summary.max_b,
            rgb_summary.red_dominant_pixels,
            rgb_summary.near_uniform_samples,
            albedo_changed,
            metallic_changed,
            roughness_changed);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} fallback import apply no_paint_api=1 no_clear=1 no_commit=1 queued_strokes={} success={} failures={} albedo_import_ok={} albedo_observed={} metallic_import_ok={} metallic_observed={} roughness_import_ok={} roughness_observed={} albedo_failure={} metallic_failure={} roughness_failure={} albedo_after_failure={} metallic_after_failure={} roughness_after_failure={}\n"),
            ModTag,
            state.queued_strokes,
            state.success,
            state.failures,
            albedo_import_ok ? 1 : 0,
            albedo_observed ? 1 : 0,
            metallic_import_ok ? 1 : 0,
            metallic_observed ? 1 : 0,
            roughness_import_ok ? 1 : 0,
            roughness_observed ? 1 : 0,
            albedo_import_failure.empty() ? STR("<none>") : albedo_import_failure,
            metallic_import_failure.empty() ? STR("<none>") : metallic_import_failure,
            roughness_import_failure.empty() ? STR("<none>") : roughness_import_failure,
            albedo_after.failure.empty() ? STR("<none>") : albedo_after.failure,
            metallic_after.failure.empty() ? STR("<none>") : metallic_after.failure,
            roughness_after.failure.empty() ? STR("<none>") : roughness_after.failure);
        return all_observed;
    }

    auto apply_view_projection_cloak(Unreal::UObject* component,
                                     Unreal::UObject* pawn,
                                     ProbeState& state,
                                     bool supplemental_fill = false) -> bool
    {
        state.queued_strokes = 0;
        state.success = 0;
        state.failures = 0;
        state.paint_world_success = 0;
        state.paint_uv_success = 0;
        state.commit_calls = 0;
        state.views = 0;
        state.body_trace_hits = 0;
        state.visible_samples = 0;
        state.uv_hits = 0;
        state.background_trace_hits = 0;
        state.background_pixels = 0;
        state.atlas_bins = 0;
        state.verified_visible_backend = false;
        state.verified_paint_channel = 0;
        state.verified_paint_function = STR("ImportChannelFromBytes");
        if (!component || !pawn)
        {
            state.failures = 1;
            state.last_failure = STR("view_projection_target_unavailable");
            return false;
        }

        const auto albedo_before = export_channel_bytes(component, 0);
        const auto metallic_before = export_channel_bytes(component, 1);
        const auto roughness_before = export_channel_bytes(component, 2);
        if (!rgba_buffer_ready(albedo_before) || !rgba_buffer_ready(metallic_before) || !rgba_buffer_ready(roughness_before))
        {
            state.failures = 1;
            state.last_failure = STR("view_projection_export_failed");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play view projection refused: export failed albedo_ok={} metallic_ok={} roughness_ok={} albedo_failure={} metallic_failure={} roughness_failure={}\n"),
                ModTag,
                albedo_before.ok ? 1 : 0,
                metallic_before.ok ? 1 : 0,
                roughness_before.ok ? 1 : 0,
                albedo_before.failure.empty() ? STR("<none>") : albedo_before.failure,
                metallic_before.failure.empty() ? STR("<none>") : metallic_before.failure,
                roughness_before.failure.empty() ? STR("<none>") : roughness_before.failure);
            return false;
        }

        auto* controller = find_player_controller_for_pawn(pawn);
        auto viewport = get_viewport_info(controller);
        auto primary_frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                        : std::optional<ProjectionFrame>{};
        if (!primary_frame)
        {
            primary_frame = make_projection_frame(pawn, 0.0, 0.0);
        }
        if (!primary_frame)
        {
            state.failures = 1;
            state.last_failure = STR("camera_view_unavailable");
            return false;
        }

        ViewProjectionStats projection_stats{};
        projection_stats.camera_fov = primary_frame->fov_degrees;
        projection_stats.camera_fov_fallback = primary_frame->fov_fallback;
        const auto component_hash_before = hash_component_paint_state(component);
        state.paint_state_hash_before = component_hash_before;
        state.paint_state_hash_after = component_hash_before;

        std::vector<ProjectionTexel> texels(static_cast<size_t>(albedo_before.width) * static_cast<size_t>(albedo_before.height));
        std::vector<Sample> projected_samples{};
        projected_samples.reserve(160000);
        const auto primary_grid = supplemental_fill ? 40 : ProjectionPrimaryGrid;
        const auto aux_grid = supplemental_fill ? 32 : ProjectionAuxGrid;

        sample_projection_view(pawn,
                               *primary_frame,
                               primary_grid,
                               primary_grid,
                               true,
                               texels,
                               albedo_before.width,
                               albedo_before.height,
                               projected_samples,
                               projection_stats,
                               state);
        ++state.views;

        const bool use_aux_views = true;
        const std::array<std::pair<double, double>, 6> aux_offsets{
            std::make_pair(-45.0, 0.0),
            std::make_pair(45.0, 0.0),
            std::make_pair(-90.0, 0.0),
            std::make_pair(90.0, 0.0),
            std::make_pair(0.0, -35.0),
            std::make_pair(0.0, 35.0)};
        if (use_aux_views)
        {
            for (const auto& offset : aux_offsets)
            {
                if (state.cancelled)
                {
                    break;
                }
                if (auto frame = make_orbit_projection_frame(pawn, *primary_frame, offset.first, offset.second))
                {
                    frame->fov_degrees = primary_frame->fov_degrees;
                    frame->fov_fallback = primary_frame->fov_fallback;
                    sample_projection_view(pawn,
                                           *frame,
                                           aux_grid,
                                           aux_grid,
                                           false,
                                           texels,
                                           albedo_before.width,
                                           albedo_before.height,
                                           projected_samples,
                                           projection_stats,
                                           state);
                    ++state.views;
                }
            }
        }

        state.body_trace_hits = projection_stats.body_hits;
        state.visible_samples = projection_stats.body_hits;
        state.uv_hits = projection_stats.uv_hits;
        state.background_trace_hits = projection_stats.background_hits;
        state.background_pixels = projection_stats.primary_view_pixels + projection_stats.aux_view_pixels;
        state.atlas_bins = projection_stats.projected_samples;
        state.capture_pixels_ready = projection_stats.primary_view_pixels > 0;
        state.uv_mapping_ready = projection_stats.projected_samples > 64;
        if (projected_samples.size() < 64)
        {
            state.failures = 1;
            state.last_failure = STR("play_view_projection_failed_body_uv_unavailable");
            state.paint_state_hash_after = state.paint_state_hash_before;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play view projection insufficient: projected_samples={} primary_view_pixels={} body_hits={} owner_body_hits={} spatial_fallback_body_hits={} rejected_body_hits={} uv_hits={} camera_fov={} fov_fallback={} trace_calls={} trace_channel_attempts={} trace_no_hit={} uv_owner={} uv_spatial={} uv_floor_rejected={} uv_far_rejected={} no_uv_close={} no_uv_far_rejected={} exhausted={} fallback_disabled=1 paint_changed=0\n"),
                ModTag,
                projection_stats.projected_samples,
                projection_stats.primary_view_pixels,
                projection_stats.body_hits,
                projection_stats.owner_body_hits,
                projection_stats.spatial_fallback_body_hits,
                projection_stats.rejected_body_hits,
                projection_stats.uv_hits,
                projection_stats.camera_fov,
                projection_stats.camera_fov_fallback ? 1 : 0,
                projection_stats.trace_debug.trace_calls,
                projection_stats.trace_debug.trace_channel_attempts,
                projection_stats.trace_debug.trace_no_hit,
                projection_stats.trace_debug.uv_owner,
                projection_stats.trace_debug.uv_spatial,
                projection_stats.trace_debug.uv_floor_rejected,
                projection_stats.trace_debug.uv_far_rejected,
                projection_stats.trace_debug.no_uv_close,
                projection_stats.trace_debug.no_uv_far_rejected,
                projection_stats.trace_debug.exhausted);
            if (supplemental_fill)
            {
                return false;
            }
            return apply_screen_position_projection_cloak(component, pawn, state, state.last_failure);
        }

        dilate_projection_texels(texels, albedo_before.width, albedo_before.height, ProjectionDilationPasses);
        auto albedo_target = albedo_before.bytes;
        auto metallic_target = metallic_before.bytes;
        auto roughness_target = roughness_before.bytes;

        const auto write_scalar = [&](std::vector<uint8_t>& bytes, int x, int y, int width, double value) {
            const auto offset = static_cast<size_t>(y * width + x) * 4;
            if (offset < bytes.size())
            {
                bytes[offset] = byte_from_unit(value);
            }
        };

        for (int y = 0; y < albedo_before.height; ++y)
        {
            for (int x = 0; x < albedo_before.width; ++x)
            {
                const auto index = static_cast<size_t>(y * albedo_before.width + x);
                const auto offset = index * 4;
                Color color{};
                if (index < texels.size() && texels[index].weight > 0.0001)
                {
                    const auto inv = 1.0 / texels[index].weight;
                    color.r = clamp(texels[index].r * inv, 0.02, 0.98);
                    color.g = clamp(texels[index].g * inv, 0.02, 0.98);
                    color.b = clamp(texels[index].b * inv, 0.02, 0.98);
                    color.roughness = clamp(texels[index].roughness * inv, 0.68, 0.99);
                    color.metallic = clamp(texels[index].metallic * inv, 0.0, 0.60);
                    ++projection_stats.uv_coverage;
                    if (texels[index].priority >= 4)
                    {
                        ++projection_stats.filled_by_primary;
                    }
                    else if (texels[index].priority >= 2)
                    {
                        ++projection_stats.filled_by_aux;
                    }
                    else
                    {
                        ++projection_stats.filled_by_interpolation;
                    }
                    if (texels[index].floor_like)
                    {
                        ++projection_stats.filled_by_floor;
                    }
                }
                else
                {
                    ++projection_stats.preserved_original;
                    continue;
                }

                if (offset + 2 < albedo_target.size())
                {
                    albedo_target[offset + 0] = byte_from_unit(color.r);
                    albedo_target[offset + 1] = byte_from_unit(color.g);
                    albedo_target[offset + 2] = byte_from_unit(color.b);
                }
                if (x < metallic_before.width && y < metallic_before.height)
                {
                    write_scalar(metallic_target, x, y, metallic_before.width, color.metallic);
                }
                if (x < roughness_before.width && y < roughness_before.height)
                {
                    write_scalar(roughness_target, x, y, roughness_before.width, clamp(color.roughness, 0.72, 0.99));
                }
            }
        }

        const auto albedo_target_hash = hash_bytes(albedo_target);
        const auto metallic_target_hash = hash_bytes(metallic_target);
        const auto roughness_target_hash = hash_bytes(roughness_target);
        const auto albedo_changed = changed_byte_count(albedo_before.bytes, albedo_target);
        const auto metallic_changed = changed_byte_count(metallic_before.bytes, metallic_target);
        const auto roughness_changed = changed_byte_count(roughness_before.bytes, roughness_target);
        const auto rgb_summary = summarize_rgb_bytes(albedo_target, albedo_before.width, albedo_before.height);

        StringType albedo_import_failure{};
        StringType metallic_import_failure{};
        StringType roughness_import_failure{};
        const auto albedo_import_ok = import_channel_bytes(component, 0, albedo_target, albedo_import_failure);
        const auto metallic_import_ok = import_channel_bytes(component, 1, metallic_target, metallic_import_failure);
        const auto roughness_import_ok = import_channel_bytes(component, 2, roughness_target, roughness_import_failure);

        const auto albedo_after = export_channel_bytes(component, 0);
        const auto metallic_after = export_channel_bytes(component, 1);
        const auto roughness_after = export_channel_bytes(component, 2);
        const auto albedo_observed = albedo_after.ok && albedo_after.hash == albedo_target_hash;
        const auto metallic_observed = metallic_after.ok && metallic_after.hash == metallic_target_hash;
        const auto roughness_observed = roughness_after.ok && roughness_after.hash == roughness_target_hash;
        const auto all_observed = albedo_import_ok && metallic_import_ok && roughness_import_ok &&
                                  albedo_observed && metallic_observed && roughness_observed;
        const auto component_hash_after = hash_component_paint_state(component);
        state.paint_state_hash_after = component_hash_after;
        state.success = (albedo_import_ok && albedo_observed ? 1 : 0) +
                        (metallic_import_ok && metallic_observed ? 1 : 0) +
                        (roughness_import_ok && roughness_observed ? 1 : 0);
        state.failures = 3 - state.success;
        state.verified_visible_backend = all_observed;
        state.last_failure = all_observed ? STR("play_view_projection_applied")
                                          : STR("play_view_projection_unverified");

        const auto total_texture_pixels = std::max(1, albedo_before.width * albedo_before.height);
        const auto direct_texels = projection_stats.filled_by_primary + projection_stats.filled_by_aux;
        const auto direct_coverage_ratio = static_cast<double>(direct_texels) / static_cast<double>(total_texture_pixels);
        const auto dilated_ratio =
            static_cast<double>(projection_stats.filled_by_interpolation) / static_cast<double>(total_texture_pixels);
        const auto preserved_ratio =
            static_cast<double>(projection_stats.preserved_original) / static_cast<double>(total_texture_pixels);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play view projection coverage capture_res={} primary_grid={} aux_grid={} supplemental_fill={} dilation_passes={} primary_view_pixels={} aux_view_pixels={} floor_view_pixels={} uv_coverage={} direct_coverage_ratio={} filled_by_primary={} filled_by_aux={} filled_by_floor={} filled_by_local_dilation={} local_dilation_ratio={} preserved_original={} preserved_ratio={} projected_samples={} body_hits={} owner_body_hits={} spatial_fallback_body_hits={} rejected_body_hits={} uv_hits={} background_hits={} camera_fov={} fov_fallback={} views={} trace_calls={} trace_channel_attempts={} trace_no_hit={} uv_floor_rejected={} uv_far_rejected={} no_uv_close={} no_uv_far_rejected={} exhausted={} no_paint_api=1 no_clear=1 no_commit=1\n"),
            ModTag,
            ProjectionCaptureResolution,
            primary_grid,
            aux_grid,
            supplemental_fill ? 1 : 0,
            ProjectionDilationPasses,
            projection_stats.primary_view_pixels,
            projection_stats.aux_view_pixels,
            projection_stats.floor_view_pixels,
            projection_stats.uv_coverage,
            direct_coverage_ratio,
            projection_stats.filled_by_primary,
            projection_stats.filled_by_aux,
            projection_stats.filled_by_floor,
            projection_stats.filled_by_interpolation,
            dilated_ratio,
            projection_stats.preserved_original,
            preserved_ratio,
            projection_stats.projected_samples,
            projection_stats.body_hits,
            projection_stats.owner_body_hits,
            projection_stats.spatial_fallback_body_hits,
            projection_stats.rejected_body_hits,
            projection_stats.uv_hits,
            projection_stats.background_hits,
            projection_stats.camera_fov,
            projection_stats.camera_fov_fallback ? 1 : 0,
            state.views,
            projection_stats.trace_debug.trace_calls,
            projection_stats.trace_debug.trace_channel_attempts,
            projection_stats.trace_debug.trace_no_hit,
            projection_stats.trace_debug.uv_floor_rejected,
            projection_stats.trace_debug.uv_far_rejected,
            projection_stats.trace_debug.no_uv_close,
            projection_stats.trace_debug.no_uv_far_rejected,
            projection_stats.trace_debug.exhausted);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play texture summary pixels={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) red_pixels={} near_uniform={} albedo_changed_bytes={} metallic_changed_bytes={} roughness_changed_bytes={}\n"),
            ModTag,
            rgb_summary.pixels,
            rgb_summary.min_r,
            rgb_summary.min_g,
            rgb_summary.min_b,
            rgb_summary.avg_r,
            rgb_summary.avg_g,
            rgb_summary.avg_b,
            rgb_summary.max_r,
            rgb_summary.max_g,
            rgb_summary.max_b,
            rgb_summary.red_dominant_pixels,
            rgb_summary.near_uniform_samples,
            albedo_changed,
            metallic_changed,
            roughness_changed);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play view projection apply no_paint_api=1 no_clear=1 no_commit=1 queued_strokes={} success={} failures={} visible_backend={} component_changed={} component_hash_before={} component_hash_after={} albedo_label={} metallic_label={} roughness_label={} albedo_hash_before={} albedo_hash_target={} albedo_hash_after={} albedo_import_ok={} albedo_observed={} metallic_hash_before={} metallic_hash_target={} metallic_hash_after={} metallic_import_ok={} metallic_observed={} roughness_hash_before={} roughness_hash_target={} roughness_hash_after={} roughness_import_ok={} roughness_observed={} albedo_failure={} metallic_failure={} roughness_failure={} albedo_after_failure={} metallic_after_failure={} roughness_after_failure={}\n"),
            ModTag,
            state.queued_strokes,
            state.success,
            state.failures,
            state.verified_visible_backend ? 1 : 0,
            component_hash_before != component_hash_after ? 1 : 0,
            component_hash_before,
            component_hash_after,
            albedo_before.label,
            metallic_before.label,
            roughness_before.label,
            albedo_before.hash,
            albedo_target_hash,
            albedo_after.hash,
            albedo_import_ok ? 1 : 0,
            albedo_observed ? 1 : 0,
            metallic_before.hash,
            metallic_target_hash,
            metallic_after.hash,
            metallic_import_ok ? 1 : 0,
            metallic_observed ? 1 : 0,
            roughness_before.hash,
            roughness_target_hash,
            roughness_after.hash,
            roughness_import_ok ? 1 : 0,
            roughness_observed ? 1 : 0,
            albedo_import_failure.empty() ? STR("<none>") : albedo_import_failure,
            metallic_import_failure.empty() ? STR("<none>") : metallic_import_failure,
            roughness_import_failure.empty() ? STR("<none>") : roughness_import_failure,
            albedo_after.failure.empty() ? STR("<none>") : albedo_after.failure,
            metallic_after.failure.empty() ? STR("<none>") : metallic_after.failure,
            roughness_after.failure.empty() ? STR("<none>") : roughness_after.failure);
        return all_observed;
    }

    auto apply_import_texture_camouflage(Unreal::UObject* component,
                                         const std::vector<Sample>& samples,
                                         ProbeState& state) -> bool
    {
        constexpr int atlas_resolution = 96;
        state.queued_strokes = 0;
        state.success = 0;
        state.failures = 0;
        state.paint_world_success = 0;
        state.paint_uv_success = 0;
        state.commit_calls = 0;
        state.verified_visible_backend = false;
        state.verified_paint_channel = 0;
        state.verified_paint_function = STR("ImportChannelFromBytes");
        if (!component)
        {
            state.failures = 1;
            state.last_failure = STR("runtime_paint_component_unavailable");
            return false;
        }
        if (samples.size() < 128)
        {
            state.failures = 1;
            state.last_failure = STR("insufficient_uv_background_samples");
            return false;
        }

        auto palette = build_palette(samples);
        auto source_palette_summary = summarize_palette(palette);
        const auto red_palette_replaced = source_palette_summary.red_dominant;
        if (red_palette_replaced)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} palette rejected reason=red_artifact_source colors={} r_avg={} g_avg={} b_avg={} red_dominant_colors={}\n"),
                ModTag,
                static_cast<int>(palette.size()),
                source_palette_summary.avg_r,
                source_palette_summary.avg_g,
                source_palette_summary.avg_b,
                source_palette_summary.red_dominant_colors);
            palette = fallback_camo_palette();
        }

        auto atlas = red_palette_replaced ? build_procedural_camo_atlas(atlas_resolution, palette)
                                          : build_target_atlas(samples, atlas_resolution, palette);
        state.atlas_bins = static_cast<int>(samples.size());
        const auto component_hash_before = hash_component_paint_state(component);
        state.paint_state_hash_before = component_hash_before;
        state.paint_state_hash_after = component_hash_before;

        const auto palette_summary = summarize_palette(palette);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} palette summary colors={} r_min={} r_avg={} r_max={} g_min={} g_avg={} g_max={} b_min={} b_avg={} b_max={} red_dominant_colors={} red_dominant={} fallback={}\n"),
            ModTag,
            static_cast<int>(palette.size()),
            palette_summary.min_r,
            palette_summary.avg_r,
            palette_summary.max_r,
            palette_summary.min_g,
            palette_summary.avg_g,
            palette_summary.max_g,
            palette_summary.min_b,
            palette_summary.avg_b,
            palette_summary.max_b,
            palette_summary.red_dominant_colors,
            palette_summary.red_dominant ? 1 : 0,
            red_palette_replaced ? 1 : 0);
        for (int i = 0; i < std::min<int>(static_cast<int>(palette.size()), 12); ++i)
        {
            const auto& color = palette[static_cast<size_t>(i)];
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} palette color index={} r={} g={} b={} roughness={} metallic={}\n"),
                ModTag,
                i,
                color.r,
                color.g,
                color.b,
                color.roughness,
                color.metallic);
        }

        const auto albedo_before = export_channel_bytes(component, 0);
        const auto metallic_before = export_channel_bytes(component, 1);
        const auto roughness_before = export_channel_bytes(component, 2);
        if (!rgba_buffer_ready(albedo_before) || !rgba_buffer_ready(metallic_before) || !rgba_buffer_ready(roughness_before))
        {
            state.failures = 1;
            state.last_failure = STR("import_texture_export_failed");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play import refused: export failed albedo_ok={} metallic_ok={} roughness_ok={} albedo_failure={} metallic_failure={} roughness_failure={}\n"),
                ModTag,
                albedo_before.ok ? 1 : 0,
                metallic_before.ok ? 1 : 0,
                roughness_before.ok ? 1 : 0,
                albedo_before.failure.empty() ? STR("<none>") : albedo_before.failure,
                metallic_before.failure.empty() ? STR("<none>") : metallic_before.failure,
                roughness_before.failure.empty() ? STR("<none>") : roughness_before.failure);
            return false;
        }

        auto albedo_target = albedo_before.bytes;
        auto metallic_target = metallic_before.bytes;
        auto roughness_target = roughness_before.bytes;

        const auto write_albedo = [&](int x, int y, int width, int height) {
            const auto offset = static_cast<size_t>(y * width + x) * 4;
            if (offset + 2 >= albedo_target.size())
            {
                return;
            }
            const auto u = (static_cast<double>(x) + 0.5) / static_cast<double>(width);
            const auto v = (static_cast<double>(y) + 0.5) / static_cast<double>(height);
            auto color = disruptive_color(atlas, atlas_resolution, palette, u, v, 3);
            const auto micro = noise01(u, v, 91.0, 0x7b4dU) - 0.5;
            const auto contour = std::sin((u * 19.0 + v * 13.0 + noise01(u, v, 11.0, 0x4cf1U)) * Pi);
            if (!palette.empty() && contour > 0.58)
            {
                const auto pick = static_cast<size_t>(hash_u32(static_cast<std::uint32_t>(
                                           static_cast<int>(std::floor(u * 23.0)) * 109 +
                                           static_cast<int>(std::floor(v * 29.0)) * 251 + 0x5591U)) %
                                       static_cast<std::uint32_t>(palette.size()));
                color = mix_color(color, palette[pick], 0.18);
            }
            color.r = clamp(color.r + micro * 0.028, 0.02, 0.92);
            color.g = clamp(color.g + micro * 0.024, 0.02, 0.92);
            color.b = clamp(color.b + micro * 0.028, 0.02, 0.92);
            albedo_target[offset + 0] = byte_from_unit(color.r);
            albedo_target[offset + 1] = byte_from_unit(color.g);
            albedo_target[offset + 2] = byte_from_unit(color.b);
        };

        const auto write_scalar = [&](std::vector<uint8_t>& bytes,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      double value) {
            const auto offset = static_cast<size_t>(y * width + x) * 4;
            if (offset >= bytes.size())
            {
                return;
            }
            bytes[offset] = byte_from_unit(value);
        };

        for (int y = 0; y < albedo_before.height; ++y)
        {
            for (int x = 0; x < albedo_before.width; ++x)
            {
                write_albedo(x, y, albedo_before.width, albedo_before.height);
            }
        }

        for (int y = 0; y < metallic_before.height; ++y)
        {
            for (int x = 0; x < metallic_before.width; ++x)
            {
                write_scalar(metallic_target, x, y, metallic_before.width, metallic_before.height, 0.0);
            }
        }

        for (int y = 0; y < roughness_before.height; ++y)
        {
            for (int x = 0; x < roughness_before.width; ++x)
            {
                const auto u = (static_cast<double>(x) + 0.5) / static_cast<double>(roughness_before.width);
                const auto v = (static_cast<double>(y) + 0.5) / static_cast<double>(roughness_before.height);
                auto color = disruptive_color(atlas, atlas_resolution, palette, u, v, 3);
                const auto variation = noise01(u, v, 57.0, 0x87aaU) - 0.5;
                write_scalar(roughness_target,
                             x,
                             y,
                             roughness_before.width,
                             roughness_before.height,
                             clamp(color.roughness + variation * 0.035, 0.78, 0.98));
            }
        }

        const auto albedo_target_hash = hash_bytes(albedo_target);
        const auto metallic_target_hash = hash_bytes(metallic_target);
        const auto roughness_target_hash = hash_bytes(roughness_target);
        const auto albedo_changed = changed_byte_count(albedo_before.bytes, albedo_target);
        const auto metallic_changed = changed_byte_count(metallic_before.bytes, metallic_target);
        const auto roughness_changed = changed_byte_count(roughness_before.bytes, roughness_target);
        const auto rgb_summary = summarize_rgb_bytes(albedo_target, albedo_before.width, albedo_before.height);

        StringType albedo_import_failure{};
        StringType metallic_import_failure{};
        StringType roughness_import_failure{};
        const auto albedo_import_ok = import_channel_bytes(component, 0, albedo_target, albedo_import_failure);
        const auto metallic_import_ok = import_channel_bytes(component, 1, metallic_target, metallic_import_failure);
        const auto roughness_import_ok = import_channel_bytes(component, 2, roughness_target, roughness_import_failure);

        const auto albedo_after = export_channel_bytes(component, 0);
        const auto metallic_after = export_channel_bytes(component, 1);
        const auto roughness_after = export_channel_bytes(component, 2);
        const auto albedo_observed = albedo_after.ok && albedo_after.hash == albedo_target_hash;
        const auto metallic_observed = metallic_after.ok && metallic_after.hash == metallic_target_hash;
        const auto roughness_observed = roughness_after.ok && roughness_after.hash == roughness_target_hash;
        const auto all_observed = albedo_import_ok && metallic_import_ok && roughness_import_ok &&
                                  albedo_observed && metallic_observed && roughness_observed;
        const auto component_hash_after = hash_component_paint_state(component);
        state.paint_state_hash_after = component_hash_after;
        state.success = (albedo_import_ok && albedo_observed ? 1 : 0) +
                        (metallic_import_ok && metallic_observed ? 1 : 0) +
                        (roughness_import_ok && roughness_observed ? 1 : 0);
        state.failures = 3 - state.success;
        state.verified_visible_backend = all_observed;
        state.last_failure = all_observed ? STR("play_import_texture_applied")
                                          : STR("play_import_texture_unverified");

        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play texture summary pixels={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) red_pixels={} near_uniform={} albedo_changed_bytes={} metallic_changed_bytes={} roughness_changed_bytes={}\n"),
            ModTag,
            rgb_summary.pixels,
            rgb_summary.min_r,
            rgb_summary.min_g,
            rgb_summary.min_b,
            rgb_summary.avg_r,
            rgb_summary.avg_g,
            rgb_summary.avg_b,
            rgb_summary.max_r,
            rgb_summary.max_g,
            rgb_summary.max_b,
            rgb_summary.red_dominant_pixels,
            rgb_summary.near_uniform_samples,
            albedo_changed,
            metallic_changed,
            roughness_changed);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play import apply no_paint_api=1 no_clear=1 no_commit=1 atlas_resolution={} samples={} queued_strokes={} success={} failures={} visible_backend={} component_changed={} component_hash_before={} component_hash_after={} albedo_label={} metallic_label={} roughness_label={} albedo_hash_before={} albedo_hash_target={} albedo_hash_after={} albedo_import_ok={} albedo_observed={} metallic_hash_before={} metallic_hash_target={} metallic_hash_after={} metallic_import_ok={} metallic_observed={} roughness_hash_before={} roughness_hash_target={} roughness_hash_after={} roughness_import_ok={} roughness_observed={} albedo_failure={} metallic_failure={} roughness_failure={} albedo_after_failure={} metallic_after_failure={} roughness_after_failure={}\n"),
            ModTag,
            atlas_resolution,
            static_cast<int>(samples.size()),
            state.queued_strokes,
            state.success,
            state.failures,
            state.verified_visible_backend ? 1 : 0,
            component_hash_before != component_hash_after ? 1 : 0,
            component_hash_before,
            component_hash_after,
            albedo_before.label,
            metallic_before.label,
            roughness_before.label,
            albedo_before.hash,
            albedo_target_hash,
            albedo_after.hash,
            albedo_import_ok ? 1 : 0,
            albedo_observed ? 1 : 0,
            metallic_before.hash,
            metallic_target_hash,
            metallic_after.hash,
            metallic_import_ok ? 1 : 0,
            metallic_observed ? 1 : 0,
            roughness_before.hash,
            roughness_target_hash,
            roughness_after.hash,
            roughness_import_ok ? 1 : 0,
            roughness_observed ? 1 : 0,
            albedo_import_failure.empty() ? STR("<none>") : albedo_import_failure,
            metallic_import_failure.empty() ? STR("<none>") : metallic_import_failure,
            roughness_import_failure.empty() ? STR("<none>") : roughness_import_failure,
            albedo_after.failure.empty() ? STR("<none>") : albedo_after.failure,
            metallic_after.failure.empty() ? STR("<none>") : metallic_after.failure,
            roughness_after.failure.empty() ? STR("<none>") : roughness_after.failure);
        return all_observed;
    }

    auto apply_quality_atlas_camouflage(Unreal::UObject* component,
                                        const std::vector<Sample>& samples,
                                        ProbeState& state) -> bool
    {
        constexpr int atlas_resolution = 64;
        state.queued_strokes = 0;
        state.success = 0;
        state.failures = 0;
        state.paint_uv_success = 0;
        if (!component)
        {
            state.last_failure = STR("runtime_paint_component_unavailable");
            return false;
        }
        if (samples.size() < 128)
        {
            state.last_failure = STR("insufficient_uv_background_samples");
            return false;
        }

        auto palette = build_palette(samples);
        auto source_palette_summary = summarize_palette(palette);
        const auto red_palette_replaced = source_palette_summary.red_dominant;
        if (red_palette_replaced)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} palette rejected reason=red_artifact_source colors={} r_avg={} g_avg={} b_avg={} red_dominant_colors={}\n"),
                ModTag,
                static_cast<int>(palette.size()),
                source_palette_summary.avg_r,
                source_palette_summary.avg_g,
                source_palette_summary.avg_b,
                source_palette_summary.red_dominant_colors);
            palette = fallback_camo_palette();
        }
        auto atlas = red_palette_replaced ? build_procedural_camo_atlas(atlas_resolution, palette)
                                          : build_target_atlas(samples, atlas_resolution, palette);
        state.atlas_bins = static_cast<int>(samples.size());
        const auto hash_initial = hash_component_paint_state(component);
        state.paint_state_hash_before = hash_initial;
        state.paint_state_hash_after = hash_initial;
        state.commit_calls = 0;
        state.verified_visible_backend = false;

        const auto palette_summary = summarize_palette(palette);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} palette summary colors={} r_min={} r_avg={} r_max={} g_min={} g_avg={} g_max={} b_min={} b_avg={} b_max={} red_dominant_colors={} red_dominant={} fallback={}\n"),
            ModTag,
            static_cast<int>(palette.size()),
            palette_summary.min_r,
            palette_summary.avg_r,
            palette_summary.max_r,
            palette_summary.min_g,
            palette_summary.avg_g,
            palette_summary.max_g,
            palette_summary.min_b,
            palette_summary.avg_b,
            palette_summary.max_b,
            palette_summary.red_dominant_colors,
            palette_summary.red_dominant ? 1 : 0,
            red_palette_replaced ? 1 : 0);
        for (int i = 0; i < std::min<int>(static_cast<int>(palette.size()), 12); ++i)
        {
            const auto& color = palette[static_cast<size_t>(i)];
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} palette color index={} r={} g={} b={} roughness={} metallic={}\n"),
                ModTag,
                i,
                color.r,
                color.g,
                color.b,
                color.roughness,
                color.metallic);
        }

        auto backend = probe_verified_paint_backend(component, state, false);
        const auto hash_after_probe = hash_component_paint_state(component);
        if (!backend)
        {
            state.paint_state_hash_after = hash_after_probe;
            return false;
        }
        clear_component(component);
        state.commit_calls += call_commit_sync_candidates(component, false);
        const auto hash_after_clear = hash_component_paint_state(component);
        auto* backend_function = component->GetFunctionByNameInChain(backend->function_name);
        const auto backend_label = channel_enum_label(backend_function, backend->channel);

        PaintParamWriteStats first_param_stats{};
        bool captured_param_stats = false;

        auto paint_stroke = [&](double u, double v, double radius, double opacity, double hardness, const Color& color) {
            if (state.cancelled)
            {
                return;
            }
            ++state.queued_strokes;
            const auto effective_radius = std::min(radius, backend->radius);
            const auto effective_opacity = std::max(opacity, backend->opacity);
            const auto effective_hardness = std::max(hardness, backend->hardness);
            PaintParamWriteStats stroke_param_stats{};
            auto* param_stats = captured_param_stats ? nullptr : &stroke_param_stats;
            if (param_stats)
            {
                param_stats->channel_label = backend_label;
            }
            if (paint_at_uv_named(component,
                                  backend->function_name,
                                  backend->channel,
                                  clamp(u, 0.002, 0.998),
                                  clamp(v, 0.002, 0.998),
                                  color,
                                  effective_radius,
                                  effective_opacity,
                                  effective_hardness,
                                  param_stats))
            {
                ++state.success;
                ++state.paint_uv_success;
            }
            else
            {
                ++state.failures;
            }
            if (!captured_param_stats)
            {
                first_param_stats = stroke_param_stats;
                captured_param_stats = true;
            }
        };

        const auto detail_grid = samples.size() > 1200 ? 46 : (samples.size() > 400 ? 38 : 32);
        const auto run_grid = [&](int grid,
                                  int layer,
                                  double base_radius,
                                  double radius_jitter,
                                  double opacity,
                                  double hardness,
                                  double jitter,
                                  double density) {
            for (int y = 0; y < grid; ++y)
            {
                for (int x = 0; x < grid; ++x)
                {
                    if (state.cancelled)
                    {
                        return;
                    }
                    const auto keep = noise01(x, y, static_cast<std::uint32_t>(0x6100 + layer * 971));
                    if (keep > density)
                    {
                        continue;
                    }
                    const auto jx = (noise01(x, y, static_cast<std::uint32_t>(0x1234 + layer * 31)) - 0.5) * jitter;
                    const auto jy = (noise01(x, y, static_cast<std::uint32_t>(0x8231 + layer * 47)) - 0.5) * jitter;
                    const auto u = (static_cast<double>(x) + 0.5 + jx) / static_cast<double>(grid);
                    const auto v = (static_cast<double>(y) + 0.5 + jy) / static_cast<double>(grid);
                    const auto radius = clamp(base_radius + (keep - 0.5) * radius_jitter, 0.006, 0.18);
                    auto color = disruptive_color(atlas, atlas_resolution, palette, u, v, layer);
                    paint_stroke(u, v, radius, opacity, hardness, color);
                }
            }
        };

        run_grid(12, 0, 0.062, 0.018, 0.92, 0.46, 0.34, 1.00);
        run_grid(20, 1, 0.036, 0.012, 0.78, 0.58, 0.42, 0.96);
        run_grid(32, 2, 0.018, 0.008, 0.55, 0.72, 0.58, 0.70);
        run_grid(detail_grid, 3, 0.009, 0.004, 0.38, 0.88, 0.66, 0.25);

        const auto hash_after_strokes_before_commit = hash_component_paint_state(component);
        state.commit_calls += call_commit_sync_candidates(component, false);
        const auto hash_after_commit = hash_component_paint_state(component);
        state.paint_state_hash_after = hash_after_commit;
        const auto strokes_changed = hash_after_strokes_before_commit != hash_after_clear;
        const auto commit_preserved_paint = hash_after_commit != hash_after_clear;

        const auto ratio = state.queued_strokes > 0
                               ? static_cast<double>(state.success) / static_cast<double>(state.queued_strokes)
                               : 0.0;
        const auto api_applied = !state.cancelled && state.queued_strokes > 0 && ratio >= 0.85;
        const auto hash_observed = strokes_changed && commit_preserved_paint;
        state.verified_visible_backend = api_applied;
        state.last_failure = api_applied ? (hash_observed ? STR("play_quality_atlas_applied_hash_observed")
                                                          : STR("play_quality_atlas_applied_hash_untracked"))
                                         : STR("paint_apply_failed_or_incomplete");
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} paint params function={} channel={} label={} color_param={} color_struct={} uv_param={} brush_param={} wrote_color={} wrote_uv={} wrote_brush={} wrote_channel={} strict_channel_data={} layout_fallback={} param_failure={}\n"),
            ModTag,
            backend->function_name,
            backend->channel,
            backend_label,
            first_param_stats.color_param.empty() ? STR("<none>") : first_param_stats.color_param,
            first_param_stats.color_struct.empty() ? STR("<none>") : first_param_stats.color_struct,
            first_param_stats.uv_param.empty() ? STR("<none>") : first_param_stats.uv_param,
            first_param_stats.brush_param.empty() ? STR("<none>") : first_param_stats.brush_param,
            first_param_stats.wrote_color ? 1 : 0,
            first_param_stats.wrote_uv ? 1 : 0,
            first_param_stats.wrote_brush ? 1 : 0,
            first_param_stats.wrote_channel ? 1 : 0,
            first_param_stats.strict_channel_data ? 1 : 0,
            first_param_stats.used_channel_data_layout_fallback ? 1 : 0,
            first_param_stats.failure.empty() ? STR("<none>") : first_param_stats.failure);
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} play apply complete apply_path={} channel={} label={} samples={} palette_colors={} queued_strokes={} success={} failures={} success_ratio={} api_applied={} commit_called={} paint_hash_before={} paint_hash_after={} hash_initial={} hash_after_probe={} hash_after_clear={} hash_after_strokes_before_commit={} hash_after_commit={} strokes_changed={} commit_preserved_paint={} hash_observed={} visible_backend={}\n"),
            ModTag,
            backend->function_name,
            backend->channel,
            backend_label,
            static_cast<int>(samples.size()),
            static_cast<int>(palette.size()),
            state.queued_strokes,
            state.success,
            state.failures,
            ratio,
            api_applied ? 1 : 0,
            state.commit_calls,
            state.paint_state_hash_before,
            state.paint_state_hash_after,
            hash_initial,
            hash_after_probe,
            hash_after_clear,
            hash_after_strokes_before_commit,
            hash_after_commit,
            strokes_changed ? 1 : 0,
            commit_preserved_paint ? 1 : 0,
            hash_observed ? 1 : 0,
            state.verified_visible_backend ? 1 : 0);
        return api_applied;
    }

    class MecchaCamouflageMod final : public RC::CppUserModBase
    {
      public:
        MecchaCamouflageMod() : CppUserModBase()
        {
            ModName = STR("MecchaCamouflage");
            ModVersion = STR("1.0.0");
            ModDescription = STR("In-engine camera-matched camouflage painter");
            ModAuthors = STR("meccha-camouflage");
            RC::Output::send<RC::LogLevel::Verbose>(STR("{} constructed version={}\n"), ModTag, ModVersion);
        }

        ~MecchaCamouflageMod() override
        {
            m_state.queue_active = false;
            m_state.cancelled = true;
            disable_cloak_overlay(false);
            stop_ui_tick_hook();
            stop_paint_capture();
            RC::Output::send<RC::LogLevel::Verbose>(STR("{} destroyed\n"), ModTag);
        }

        auto on_program_start() -> void override
        {
            register_camouflage_hotkey();
        }

        auto on_unreal_init() -> void override
        {
            m_state.unreal_initialized = true;
            install_console_hook();
            install_ui_tick_hook();
            RC::Output::send<RC::LogLevel::Verbose>(STR("{} Unreal initialized; command_hook={} ui_tick_hook={} hotkey={}\n"),
                                                    ModTag,
                                                    m_state.command_hook_installed ? 1 : 0,
                                                    m_ui_tick_hook_id != Unreal::Hook::ERROR_ID ? 1 : 0,
                                                    m_hotkey_registered ? 1 : 0);
        }

      private:
        ProbeState m_state{};
        Unreal::Hook::GlobalCallbackId m_paint_capture_hook_id{Unreal::Hook::ERROR_ID};
        Unreal::Hook::GlobalCallbackId m_ui_tick_hook_id{Unreal::Hook::ERROR_ID};
        std::unordered_set<StringType> m_seen_paint_capture_functions{};
        XrayOverlayState m_cloak{};
        bool m_uv_calibration_active{false};
        StringType m_uv_calibration_component{};
        std::vector<uint8_t> m_uv_calibration_original_albedo{};
        std::vector<uint8_t> m_uv_calibration_original_metallic{};
        std::vector<uint8_t> m_uv_calibration_original_roughness{};
        uint64_t m_uv_calibration_original_albedo_hash{0};
        uint64_t m_uv_calibration_albedo_hash{0};
        uint64_t m_uv_calibration_metallic_hash{0};
        uint64_t m_uv_calibration_roughness_hash{0};
        int m_uv_calibration_width{0};
        int m_uv_calibration_height{0};
        bool m_screen_dot_active{false};
        StringType m_screen_dot_component{};
        std::vector<uint8_t> m_screen_dot_original_albedo{};
        std::vector<uint8_t> m_screen_dot_original_metallic{};
        std::vector<uint8_t> m_screen_dot_original_roughness{};
        uint64_t m_screen_dot_original_hash{0};
        bool m_ui_play_requested{false};
        bool m_ui_play_running{false};
        int m_ui_clicks{0};
        bool m_hotkey_registered{false};
        bool m_ui_tick_seen{false};
        int m_ui_tick_frames{0};

        auto register_camouflage_hotkey() -> void
        {
            if (m_hotkey_registered)
            {
                return;
            }
            register_keydown_event(RC::Input::Key::F10, [this]() {
                request_camouflage_from_ui();
            });
            m_hotkey_registered = true;
            RC::Output::send<RC::LogLevel::Warning>(STR("{} hotkey=F10 registered=1 action=camouflage\n"), ModTag);
        }

        auto install_console_hook() -> void
        {
            if (m_state.command_hook_installed)
            {
                return;
            }

            Unreal::Hook::RegisterProcessConsoleExecCallback(
                [this](Unreal::UObject* context, const Unreal::TCHAR* cmd, Unreal::FOutputDevice& ar, Unreal::UObject* executor) -> bool {
                    (void)context;
                    (void)ar;
                    (void)executor;
                    if (!cmd)
                    {
                        return false;
                    }
                    const StringType command{RC::ToCharTypePtr(cmd)};
                    try
                    {
                        return handle_command(first_token(command));
                    }
                    catch (const std::exception& error)
                    {
                        m_state.queue_active = false;
                        m_state.cancelled = true;
                        m_state.last_failure = STR("command_exception");
                        RC::Output::send<RC::LogLevel::Error>(
                            STR("{} command exception: {}\n"),
                            ModTag,
                            RC::ensure_str(error.what()));
                        return true;
                    }
                    catch (...)
                    {
                        m_state.queue_active = false;
                        m_state.cancelled = true;
                        m_state.last_failure = STR("unknown_command_exception");
                        RC::Output::send<RC::LogLevel::Error>(STR("{} command exception: unknown\n"), ModTag);
                        return true;
                    }
                });

            m_state.command_hook_installed = true;
        }

        auto start_paint_capture() -> void
        {
            m_state.paint_capture_enabled = true;
            m_state.paint_capture_calls = 0;
            m_state.paint_capture_matches = 0;
            m_seen_paint_capture_functions.clear();
            if (m_paint_capture_hook_id != Unreal::Hook::ERROR_ID)
            {
                RC::Output::send<RC::LogLevel::Verbose>(STR("{} paint_capture already enabled\n"), ModTag);
                return;
            }

            Unreal::Hook::FCallbackOptions options{};
            options.OwnerModName = STR("MecchaCamouflage");
            options.HookName = STR("PaintCapture");
            options.bReadonly = true;
            m_paint_capture_hook_id = Unreal::Hook::RegisterProcessEventPostCallback(
                [this](Unreal::Hook::TCallbackIterationData<void>&,
                       Unreal::UObject* context,
                       Unreal::UFunction* function,
                       void* params) {
                    (void)params;
                    if (!m_state.paint_capture_enabled || !context || !function)
                    {
                        return;
                    }
                    ++m_state.paint_capture_calls;
                    const auto context_text = lower_copy(context->GetFullName() + STR(" ") +
                                                         (context->GetClassPrivate() ? context->GetClassPrivate()->GetFullName() : StringType{}));
                    if (!contains_text(context_text, STR("runtimepaintable")))
                    {
                        return;
                    }
                    const auto function_text = lower_copy(function->GetName() + STR(" ") + function->GetFullName());
                    if (!pipeline_token_match(function_text))
                    {
                        return;
                    }
                    ++m_state.paint_capture_matches;
                    const auto key = function->GetFullName();
                    if (m_seen_paint_capture_functions.insert(key).second && m_seen_paint_capture_functions.size() <= 80)
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("{} paint_capture func context={} name={} full={} parms_size={} non_return_params={} commit_sync={}\n"),
                            ModTag,
                            context->GetFullName(),
                            function->GetName(),
                            function->GetFullName(),
                            function->GetParmsSize(),
                            non_return_param_count(function),
                            commit_sync_token_match(function_text) ? 1 : 0);
                        dump_function_signature(context, function->GetName().c_str(), true);
                    }
                },
                options);
            RC::Output::send<RC::LogLevel::Verbose>(STR("{} paint_capture enabled hook_id={}\n"), ModTag, m_paint_capture_hook_id);
        }

        auto stop_paint_capture() -> void
        {
            m_state.paint_capture_enabled = false;
            if (m_paint_capture_hook_id != Unreal::Hook::ERROR_ID)
            {
                Unreal::Hook::UnregisterCallback(m_paint_capture_hook_id);
                m_paint_capture_hook_id = Unreal::Hook::ERROR_ID;
            }
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} paint_capture disabled calls={} matches={} unique_functions={}\n"),
                ModTag,
                m_state.paint_capture_calls,
                m_state.paint_capture_matches,
                static_cast<int>(m_seen_paint_capture_functions.size()));
        }

        auto install_ui_tick_hook() -> void
        {
            if (m_ui_tick_hook_id != Unreal::Hook::ERROR_ID)
            {
                return;
            }

            Unreal::Hook::FCallbackOptions options{};
            options.OwnerModName = STR("MecchaCamouflage");
            options.HookName = STR("CamouflageUITick");
            options.bReadonly = false;
            m_ui_tick_hook_id = Unreal::Hook::RegisterGameViewportClientTickPostCallback(
                [this](Unreal::Hook::TCallbackIterationData<void>&,
                       Unreal::UGameViewportClient* context,
                       float delta_seconds) {
                    tick_camouflage_ui(context, delta_seconds);
                },
                options);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} ui_tick_hook installed={} hook_id={}\n"),
                ModTag,
                m_ui_tick_hook_id != Unreal::Hook::ERROR_ID ? 1 : 0,
                m_ui_tick_hook_id);
        }

        auto stop_ui_tick_hook() -> void
        {
            if (m_ui_tick_hook_id != Unreal::Hook::ERROR_ID)
            {
                Unreal::Hook::UnregisterCallback(m_ui_tick_hook_id);
                m_ui_tick_hook_id = Unreal::Hook::ERROR_ID;
            }
        }

        auto tick_camouflage_ui(Unreal::UGameViewportClient* viewport, float delta_seconds) -> void
        {
            (void)delta_seconds;
            if (!viewport)
            {
                return;
            }
            if (!m_ui_tick_seen)
            {
                m_ui_tick_seen = true;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} ui_tick active viewport={}\n"),
                    ModTag,
                    viewport->GetFullName());
            }

            ++m_ui_tick_frames;
            run_pending_ui_camouflage();
        }

        auto destroy_cloak_capture_actor() -> void
        {
            if (m_cloak.capture_actor)
            {
                call_no_params(m_cloak.capture_actor, STR("K2_DestroyActor"));
            }
            m_cloak.capture_actor = nullptr;
            m_cloak.capture_component = nullptr;
            m_cloak.render_target = nullptr;
        }

        auto remove_cloak_umg_overlay() -> void
        {
            for (auto* image : m_cloak.umg_run_images)
            {
                if (image)
                {
                    call_no_params_void(image, STR("RemoveFromParent"));
                    if (m_cloak.umg_canvas)
                    {
                        call_object_param(m_cloak.umg_canvas, STR("RemoveChild"), image);
                    }
                }
            }
            m_cloak.umg_run_images.clear();
            m_cloak.umg_run_slots.clear();
            if (m_cloak.umg_image)
            {
                call_no_params_void(m_cloak.umg_image, STR("RemoveFromParent"));
                if (m_cloak.umg_canvas)
                {
                    call_object_param(m_cloak.umg_canvas, STR("RemoveChild"), m_cloak.umg_image);
                }
            }
            m_cloak.umg_overlay_active = false;
            m_cloak.umg_canvas = nullptr;
            m_cloak.umg_image = nullptr;
            m_cloak.umg_slot = nullptr;
            m_cloak.umg_images = 0;
        }

        auto find_cloak_canvas_panel() -> Unreal::UObject*
        {
            std::vector<Unreal::UObject*> panels{};
            Unreal::UObjectGlobals::FindObjects(4096, STR("CanvasPanel"), nullptr, panels, 0, 0, false);
            m_cloak.umg_canvas_candidates = static_cast<int>(panels.size());
            Unreal::UObject* best = nullptr;
            int best_score = -100000;
            for (auto* panel : panels)
            {
                if (!panel)
                {
                    continue;
                }
                const auto full = panel->GetFullName();
                const auto lower = lower_copy(full);
                int score = 0;
                if (contains_text(lower, STR("widgettree")))
                {
                    score += 20;
                }
                if (contains_text(lower, STR("wbp_cleonmain")) || contains_text(lower, STR("wbp_link_mainui")) ||
                    contains_text(lower, STR("wbp_cleongame_common")))
                {
                    score += 120;
                }
                if (contains_text(lower, STR("main")) || contains_text(lower, STR("hud")) ||
                    contains_text(lower, STR("ui")))
                {
                    score += 25;
                }
                if (contains_text(lower, STR("mecchacamo")))
                {
                    score -= 200;
                }
                if (contains_text(lower, STR("debug")) || contains_text(lower, STR("loading")))
                {
                    score -= 25;
                }
                if (call_no_params_return_bool(panel, STR("IsRendered")))
                {
                    score += 80;
                }
                if (call_no_params_return_bool(panel, STR("IsVisible")))
                {
                    score += 40;
                }
                if (call_no_params_return_bool(panel, STR("IsInViewport")))
                {
                    score += 20;
                }
                if (!best || score > best_score)
                {
                    best = panel;
                    best_score = score;
                }
            }
            if (best)
            {
                m_cloak.umg_canvas_name = best->GetFullName();
                m_cloak.umg_failure.clear();
            }
            else
            {
                m_cloak.umg_canvas_name.clear();
                m_cloak.umg_failure = STR("umg_canvas_unavailable");
            }
            return best;
        }

        auto write_slate_brush_texture(Unreal::FStructProperty* brush_prop,
                                       uint8_t* container,
                                       Unreal::UObject* texture,
                                       double image_width,
                                       double image_height,
                                       double uv_min_x = 0.0,
                                       double uv_min_y = 0.0,
                                       double uv_max_x = 1.0,
                                       double uv_max_y = 1.0) -> bool
        {
            if (!brush_prop || !texture)
            {
                return false;
            }
            auto* base = prop_value_ptr(container, brush_prop);
            auto* structure = struct_type(brush_prop);
            if (!structure)
            {
                return false;
            }

            bool wrote = false;
            if (auto* draw_as = find_struct_property(structure, STR("DrawAs")))
            {
                wrote = write_number(draw_as, base, 3.0) || wrote;
            }
            if (auto* tiling = find_struct_property(structure, STR("Tiling")))
            {
                wrote = write_number(tiling, base, 0.0) || wrote;
            }
            if (auto* mirroring = find_struct_property(structure, STR("Mirroring")))
            {
                wrote = write_number(mirroring, base, 0.0) || wrote;
            }
            if (auto* image_type = find_struct_property(structure, STR("ImageType")))
            {
                wrote = write_number(image_type, base, 1.0) || wrote;
            }
            if (auto* image_size = find_struct_property(structure, STR("ImageSize")))
            {
                wrote = write_vector2(image_size, base, image_width, image_height) || wrote;
            }
            if (auto* resource = find_struct_property(structure, STR("ResourceObject")))
            {
                wrote = write_object(resource, base, texture) || wrote;
            }
            if (auto* has_uobject = find_struct_property(structure, STR("bHasUObject")))
            {
                wrote = write_bool(has_uobject, base, true) || wrote;
            }
            if (auto* dynamic = find_struct_property(structure, STR("bIsDynamicallyLoaded")))
            {
                wrote = write_bool(dynamic, base, false) || wrote;
            }
            if (auto* tint_prop = Unreal::CastField<Unreal::FStructProperty>(find_struct_property(structure, STR("TintColor"))))
            {
                auto* tint_base = prop_value_ptr(base, tint_prop);
                auto* tint_struct = struct_type(tint_prop);
                if (tint_struct)
                {
                    if (auto* specified =
                            Unreal::CastField<Unreal::FStructProperty>(find_struct_property(tint_struct, STR("SpecifiedColor"))))
                    {
                        Color white{1.0, 1.0, 1.0, 1.0, 0.0};
                        wrote = write_linear_color(specified, tint_base, white, 1.0) || wrote;
                    }
                    if (auto* use_rule = find_struct_property(tint_struct, STR("ColorUseRule")))
                    {
                        wrote = write_number(use_rule, tint_base, 0.0) || wrote;
                    }
                }
            }
            if (auto* uv_prop = Unreal::CastField<Unreal::FStructProperty>(find_struct_property(structure, STR("UVRegion"))))
            {
                auto* uv_base = prop_value_ptr(base, uv_prop);
                auto* uv_struct = struct_type(uv_prop);
                if (uv_struct)
                {
                    if (auto* min_prop = Unreal::CastField<Unreal::FStructProperty>(find_struct_property(uv_struct, STR("min"))))
                    {
                        wrote = write_vector2(min_prop, uv_base, uv_min_x, uv_min_y) || wrote;
                    }
                    if (auto* max_prop = Unreal::CastField<Unreal::FStructProperty>(find_struct_property(uv_struct, STR("max"))))
                    {
                        wrote = write_vector2(max_prop, uv_base, uv_max_x, uv_max_y) || wrote;
                    }
                    if (auto* valid = find_struct_property(uv_struct, STR("bIsValid")))
                    {
                        wrote = write_bool(valid, uv_base, true) || wrote;
                    }
                }
            }
            return wrote;
        }

        auto set_umg_image_render_target_brush(Unreal::UObject* image,
                                               Unreal::UObject* texture,
                                               int image_width,
                                               int image_height,
                                               double uv_min_x = 0.0,
                                               double uv_min_y = 0.0,
                                               double uv_max_x = 1.0,
                                               double uv_max_y = 1.0) -> bool
        {
            if (!image || !texture)
            {
                return false;
            }
            bool set_brush = false;
            if (auto* function = image->GetFunctionByNameInChain(STR("SetBrush")))
            {
                std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
                for (auto* property : function->ForEachProperty())
                {
                    if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                        property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
                    {
                        continue;
                    }
                    if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                    {
                        set_brush = write_slate_brush_texture(struct_prop,
                                                              params.data(),
                                                              texture,
                                                              static_cast<double>(image_width),
                                                              static_cast<double>(image_height),
                                                              uv_min_x,
                                                              uv_min_y,
                                                              uv_max_x,
                                                              uv_max_y) ||
                                    set_brush;
                    }
                }
                if (set_brush)
                {
                    image->ProcessEvent(function, params.data());
                }
            }

            const auto set_resource = call_object_param(image, STR("SetBrushResourceObject"), texture);
            call_vector2_param(image, STR("SetDesiredSizeOverride"), static_cast<double>(image_width), static_cast<double>(image_height));
            return set_brush || set_resource;
        }

        auto install_umg_fullscreen_overlay(Unreal::UObject* render_target) -> bool
        {
            m_cloak.umg_failure = STR("not_run");
            if (!render_target || m_cloak.viewport_width <= 0 || m_cloak.viewport_height <= 0)
            {
                m_cloak.umg_failure = STR("umg_prereq_unavailable");
                return false;
            }
            auto* canvas = find_cloak_canvas_panel();
            if (!canvas)
            {
                if (m_cloak.umg_failure.empty())
                {
                    m_cloak.umg_failure = STR("umg_canvas_unavailable");
                }
                return false;
            }

            auto* image_class =
                Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            if (!image_class)
            {
                m_cloak.umg_failure = STR("umg_image_class_unavailable");
                return false;
            }

            auto* image = Unreal::UObjectGlobals::NewObject<Unreal::UObject>(canvas, image_class);
            if (!image)
            {
                m_cloak.umg_failure = STR("umg_image_create_failed");
                return false;
            }

            call_single_number_param(image, STR("SetVisibility"), 3.0);
            call_single_number_param(image, STR("SetRenderOpacity"), 1.0);
            call_single_number_param(image, STR("SetOpacity"), 1.0);
            const auto brush_ok = set_umg_image_render_target_brush(image, render_target, m_cloak.rt_width, m_cloak.rt_height);
            if (!brush_ok)
            {
                call_no_params_void(image, STR("RemoveFromParent"));
                m_cloak.umg_failure = STR("umg_image_brush_failed");
                return false;
            }

            auto* slot = call_object_return_object(canvas, STR("AddChildToCanvas"), image);
            if (!slot)
            {
                call_no_params_void(image, STR("RemoveFromParent"));
                m_cloak.umg_failure = STR("umg_add_child_failed");
                return false;
            }

            call_anchors_param(slot, STR("SetAnchors"), 0.0, 0.0, 1.0, 1.0);
            call_margin_param(slot, STR("SetOffsets"), 0.0, 0.0, 0.0, 0.0);
            call_vector2_param(slot, STR("SetAlignment"), 0.0, 0.0);
            call_vector2_param(slot, STR("SetPosition"), 0.0, 0.0);
            call_vector2_param(slot,
                               STR("SetSize"),
                               static_cast<double>(m_cloak.viewport_width),
                               static_cast<double>(m_cloak.viewport_height));
            call_single_bool_param(slot, STR("SetAutoSize"), false);
            call_single_number_param(slot, STR("SetZOrder"), -10000.0);
            call_no_params_void(image, STR("ForceLayoutPrepass"));
            call_no_params_void(canvas, STR("ForceLayoutPrepass"));

            m_cloak.umg_canvas = canvas;
            m_cloak.umg_image = image;
            m_cloak.umg_slot = slot;
            m_cloak.umg_overlay_active = true;
            m_cloak.umg_images = 1;
            m_cloak.umg_failure.clear();
            return true;
        }

        auto install_umg_masked_run_overlay(Unreal::UObject* render_target) -> bool
        {
            m_cloak.umg_failure = STR("not_run");
            if (!render_target || m_cloak.viewport_width <= 0 || m_cloak.viewport_height <= 0 ||
                m_cloak.rt_width <= 0 || m_cloak.rt_height <= 0 || m_cloak.runs.empty())
            {
                m_cloak.umg_failure = STR("umg_masked_prereq_unavailable");
                return false;
            }

            auto* canvas = find_cloak_canvas_panel();
            if (!canvas)
            {
                if (m_cloak.umg_failure.empty())
                {
                    m_cloak.umg_failure = STR("umg_canvas_unavailable");
                }
                return false;
            }

            auto* image_class =
                Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, STR("/Script/UMG.Image"));
            if (!image_class)
            {
                m_cloak.umg_failure = STR("umg_image_class_unavailable");
                return false;
            }

            const auto sx = static_cast<double>(m_cloak.viewport_width) / static_cast<double>(std::max(1, m_cloak.rt_width));
            const auto sy = static_cast<double>(m_cloak.viewport_height) / static_cast<double>(std::max(1, m_cloak.rt_height));
            const auto inv_w = 1.0 / static_cast<double>(std::max(1, m_cloak.rt_width));
            const auto inv_h = 1.0 / static_cast<double>(std::max(1, m_cloak.rt_height));
            int created = 0;
            int failures = 0;
            m_cloak.umg_run_images.reserve(m_cloak.runs.size());
            m_cloak.umg_run_slots.reserve(m_cloak.runs.size());

            for (const auto& run : m_cloak.runs)
            {
                const auto run_width = std::max(1, run.x1 - run.x0);
                auto* image = Unreal::UObjectGlobals::NewObject<Unreal::UObject>(canvas, image_class);
                if (!image)
                {
                    ++failures;
                    continue;
                }

                call_single_number_param(image, STR("SetVisibility"), 3.0);
                call_single_number_param(image, STR("SetRenderOpacity"), 1.0);
                call_single_number_param(image, STR("SetOpacity"), 1.0);
                const auto uv_min_x = static_cast<double>(run.x0) * inv_w;
                const auto uv_min_y = static_cast<double>(run.y) * inv_h;
                const auto uv_max_x = static_cast<double>(run.x1) * inv_w;
                const auto uv_max_y = static_cast<double>(run.y + 1) * inv_h;
                const auto brush_ok = set_umg_image_render_target_brush(image,
                                                                         render_target,
                                                                         run_width,
                                                                         1,
                                                                         uv_min_x,
                                                                         uv_min_y,
                                                                         uv_max_x,
                                                                         uv_max_y);
                if (!brush_ok)
                {
                    call_no_params_void(image, STR("RemoveFromParent"));
                    ++failures;
                    continue;
                }

                auto* slot = call_object_return_object(canvas, STR("AddChildToCanvas"), image);
                if (!slot)
                {
                    call_no_params_void(image, STR("RemoveFromParent"));
                    ++failures;
                    continue;
                }

                const auto screen_x = static_cast<double>(run.x0) * sx;
                const auto screen_y = static_cast<double>(run.y) * sy;
                const auto screen_w = static_cast<double>(run_width) * sx;
                const auto screen_h = std::max(1.0, sy);
                call_anchors_param(slot, STR("SetAnchors"), 0.0, 0.0, 0.0, 0.0);
                call_margin_param(slot, STR("SetOffsets"), screen_x, screen_y, screen_w, screen_h);
                call_vector2_param(slot, STR("SetAlignment"), 0.0, 0.0);
                call_vector2_param(slot, STR("SetPosition"), screen_x, screen_y);
                call_vector2_param(slot, STR("SetSize"), screen_w, screen_h);
                call_single_bool_param(slot, STR("SetAutoSize"), false);
                call_single_number_param(slot, STR("SetZOrder"), 100000.0);

                m_cloak.umg_run_images.push_back(image);
                m_cloak.umg_run_slots.push_back(slot);
                ++created;
            }

            call_no_params_void(canvas, STR("ForceLayoutPrepass"));
            if (created <= 0)
            {
                m_cloak.umg_failure = STR("umg_masked_no_images_created");
                return false;
            }

            m_cloak.umg_canvas = canvas;
            m_cloak.umg_image = nullptr;
            m_cloak.umg_slot = nullptr;
            m_cloak.umg_overlay_active = true;
            m_cloak.umg_images = created;
            m_cloak.estimated_draw_calls = created;
            if (failures > 0 && created < std::max(1, static_cast<int>(m_cloak.runs.size()) * 9 / 10))
            {
                m_cloak.umg_failure = STR("umg_masked_too_many_image_failures");
                return false;
            }
            m_cloak.umg_failure.clear();
            return true;
        }

        auto restore_cloak_hidden_pawn() -> bool
        {
            const auto had_hidden_pawn = m_cloak.pawn_hidden;
            const auto restored = !had_hidden_pawn || set_actor_hidden(m_cloak.hidden_pawn, false);
            m_cloak.actor_hide_fallback = false;
            m_cloak.pawn_hidden = false;
            m_cloak.hidden_pawn = nullptr;
            return restored;
        }

        auto restore_mesh_material_cloak() -> bool
        {
            if (!m_cloak.mesh_material_active)
            {
                return true;
            }
            bool restored = true;
            for (size_t slot = 0; slot < m_cloak.mesh_original_materials.size(); ++slot)
            {
                auto* material = m_cloak.mesh_original_materials[slot];
                if (material)
                {
                    restored = call_set_material(m_cloak.mesh_object, static_cast<int>(slot), material) && restored;
                }
            }
            m_cloak.mesh_material_active = false;
            m_cloak.mesh_object = nullptr;
            m_cloak.mesh_original_materials.clear();
            m_cloak.mesh_dynamic_materials.clear();
            m_cloak.mesh_material_slots = 0;
            m_cloak.mesh_dynamic_materials_created = 0;
            m_cloak.mesh_texture_param_calls = 0;
            m_cloak.mesh_scalar_param_calls = 0;
            return restored;
        }

        auto disable_cloak_overlay(bool log) -> void
        {
            const auto was_active = m_cloak.active;
            const auto was_pawn_hidden = m_cloak.pawn_hidden;
            const auto pawn_restore_ok = restore_cloak_hidden_pawn();
            const auto material_restore_ok = restore_mesh_material_cloak();
            remove_cloak_umg_overlay();
            destroy_cloak_capture_actor();
            m_cloak.active = false;
            m_cloak.drawing = false;
            m_cloak.runs.clear();
            m_cloak.mask_pixels = 0;
            m_cloak.mask_runs = 0;
            m_cloak.estimated_draw_calls = 0;
            m_cloak.last_frame_draw_calls = 0;
            m_cloak.last_frame_draw_failures = 0;
            if (log)
            {
                m_state.verified_visible_backend = false;
                m_state.last_failure = STR("cloak_overlay_disabled");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} cloak_off active_before={} pawn_hidden_before={} pawn_restore_ok={} material_restore_ok={} overlay_active=0 umg_overlay_active=0 mesh_material_active=0 no_paint=1 no_import=1 no_strokes=1 no_commit=1\n"),
                    ModTag,
                    was_active ? 1 : 0,
                    was_pawn_hidden ? 1 : 0,
                    pawn_restore_ok ? 1 : 0,
                    material_restore_ok ? 1 : 0);
            }
        }

        auto make_xray_calibration_samples(const ViewportInfo& viewport) const -> std::vector<ScreenHitSample>
        {
            std::vector<ScreenHitSample> samples{};
            samples.reserve(static_cast<size_t>(XrayOverlayCalibrationGrid * XrayOverlayCalibrationGrid));
            for (int y = 0; y < XrayOverlayCalibrationGrid; ++y)
            {
                for (int x = 0; x < XrayOverlayCalibrationGrid; ++x)
                {
                    ScreenHitSample sample{};
                    sample.nx = (static_cast<double>(x) + 0.5) / static_cast<double>(XrayOverlayCalibrationGrid);
                    sample.ny = (static_cast<double>(y) + 0.5) / static_cast<double>(XrayOverlayCalibrationGrid);
                    sample.screen_x = sample.nx * static_cast<double>(std::max(1, viewport.width));
                    sample.screen_y = sample.ny * static_cast<double>(std::max(1, viewport.height));
                    samples.push_back(sample);
                }
            }
            return samples;
        }

        auto render_image_summary(const RenderTargetImage& image) const -> CaptureColorSummary
        {
            CaptureColorSummary out{};
            Color first{};
            bool have_first = false;
            if (!image.ok)
            {
                return out;
            }
            for (const auto& color : image.pixels)
            {
                if (!have_first)
                {
                    first = color;
                    have_first = true;
                }
                ++out.pixels;
                out.min_r = std::min(out.min_r, color.r);
                out.min_g = std::min(out.min_g, color.g);
                out.min_b = std::min(out.min_b, color.b);
                out.max_r = std::max(out.max_r, color.r);
                out.max_g = std::max(out.max_g, color.g);
                out.max_b = std::max(out.max_b, color.b);
                out.avg_r += color.r;
                out.avg_g += color.g;
                out.avg_b += color.b;
                if (std::abs(color.r - first.r) < 0.004 && std::abs(color.g - first.g) < 0.004 &&
                    std::abs(color.b - first.b) < 0.004)
                {
                    ++out.near_uniform_samples;
                }
            }
            if (out.pixels > 0)
            {
                const auto inv = 1.0 / static_cast<double>(out.pixels);
                out.avg_r *= inv;
                out.avg_g *= inv;
                out.avg_b *= inv;
                const auto range = std::max({out.max_r - out.min_r, out.max_g - out.min_g, out.max_b - out.min_b});
                out.uniform = range < 0.006 || out.near_uniform_samples >= static_cast<int>(out.pixels * 0.985);
                out.clear_suspect = out.uniform;
            }
            return out;
        }

        auto xray_sample_image_screen_pixel(const RenderTargetImage& image,
                                            int x,
                                            int y,
                                            const ScreenTransform& transform) const -> std::optional<Color>
        {
            if (!image.ok || image.width <= 0 || image.height <= 0)
            {
                return std::nullopt;
            }
            const auto nx = (static_cast<double>(x) + 0.5) / static_cast<double>(image.width);
            const auto ny = (static_cast<double>(y) + 0.5) / static_cast<double>(image.height);
            return sample_image_at(image,
                                   transform_screen_coord(nx,
                                                          transform.scale_x,
                                                          transform.offset_x,
                                                          transform.flip_x,
                                                          transform.pivot_x),
                                   transform_screen_coord(ny,
                                                          transform.scale_y,
                                                          transform.offset_y,
                                                          transform.flip_y,
                                                          transform.pivot_y));
        }

        auto xray_bulk_readback_orientation_ok(const RenderTargetImage& image) const -> bool
        {
            if (!image.ok || image.backend != STR("bulk_array"))
            {
                return false;
            }
            if (image.bulk_calibration_ok)
            {
                return true;
            }
            if (image.bulk_calibration_pairs < std::min(16, std::max(1, image.bulk_calibration_samples / 2)))
            {
                return false;
            }
            const auto best = image.bulk_calibration_best_median;
            const auto runner_up = image.bulk_calibration_runner_up_median;
            if (best <= 0.0 || runner_up <= 0.0)
            {
                return false;
            }
            const auto separated = best <= runner_up * 0.65 || (runner_up - best) >= 0.10;
            return best <= 0.36 && separated;
        }

        auto build_xray_mask_runs(const RenderTargetImage& visible_image,
                                  const RenderTargetImage& hidden_image,
                                  const CaptureColorSummary& hidden_summary,
                                  XrayOverlayState& cloak) const -> bool
        {
            const auto mask_start = SteadyClock::now();
            if (!visible_image.ok || !hidden_image.ok || visible_image.width <= 0 || visible_image.height <= 0 ||
                visible_image.width != hidden_image.width || visible_image.height != hidden_image.height)
            {
                cloak.failure = STR("cloak_readback_image_invalid");
                return false;
            }

            const auto width = visible_image.width;
            const auto height = visible_image.height;
            const auto total_pixels = width * height;
            auto visible_transform = visible_image.bulk_calibration_ok ? visible_image.bulk_to_pixel_transform : ScreenTransform{};
            auto hidden_transform = hidden_image.bulk_calibration_ok ? hidden_image.bulk_to_pixel_transform : visible_transform;
            if (!visible_image.bulk_calibration_ok)
            {
                cloak.failure = STR("cloak_bulk_readback_unverified");
                return false;
            }
            if (!hidden_image.bulk_calibration_ok && !hidden_summary.uniform)
            {
                cloak.failure = STR("cloak_bulk_readback_unverified");
                return false;
            }

            std::vector<double> deltas(static_cast<size_t>(total_pixels), 0.0);
            std::vector<double> sorted_deltas{};
            sorted_deltas.reserve(static_cast<size_t>(total_pixels));
            double delta_min = 1000000.0;
            double delta_max = 0.0;
            double delta_sum = 0.0;
            int valid_pixels = 0;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    auto visible = xray_sample_image_screen_pixel(visible_image, x, y, visible_transform);
                    auto hidden = xray_sample_image_screen_pixel(hidden_image, x, y, hidden_transform);
                    if (!visible || !hidden)
                    {
                        continue;
                    }
                    const auto delta = color_distance_rgb(*visible, *hidden);
                    const auto index = static_cast<size_t>(y * width + x);
                    deltas[index] = delta;
                    sorted_deltas.push_back(delta);
                    delta_min = std::min(delta_min, delta);
                    delta_max = std::max(delta_max, delta);
                    delta_sum += delta;
                    ++valid_pixels;
                }
            }
            if (valid_pixels < std::max(1, total_pixels / 2))
            {
                cloak.failure = STR("cloak_readback_valid_pixels_insufficient");
                return false;
            }

            const auto quantile = [&](double q) -> double {
                if (sorted_deltas.empty())
                {
                    return 0.0;
                }
                const auto index = std::min<size_t>(sorted_deltas.size() - 1,
                                                    static_cast<size_t>(std::floor(q * static_cast<double>(sorted_deltas.size() - 1))));
                std::nth_element(sorted_deltas.begin(), sorted_deltas.begin() + static_cast<std::ptrdiff_t>(index), sorted_deltas.end());
                return sorted_deltas[index];
            };
            const auto p95 = quantile(0.95);
            const auto p99 = quantile(0.99);
            const auto threshold = clamp(std::max(XrayOverlayMinDeltaThreshold, std::max(p95 * 0.45, p99 * 0.18)),
                                         XrayOverlayMinDeltaThreshold,
                                         XrayOverlayMaxDeltaThreshold);

            std::vector<uint8_t> mask(static_cast<size_t>(total_pixels), 0);
            int mask_pixels = 0;
            for (int i = 0; i < total_pixels; ++i)
            {
                if (deltas[static_cast<size_t>(i)] >= threshold)
                {
                    mask[static_cast<size_t>(i)] = 1;
                    ++mask_pixels;
                }
            }

            for (int y = 0; y < height; ++y)
            {
                int x = 0;
                while (x < width)
                {
                    while (x < width && mask[static_cast<size_t>(y * width + x)])
                    {
                        ++x;
                    }
                    const auto gap_start = x;
                    while (x < width && !mask[static_cast<size_t>(y * width + x)])
                    {
                        ++x;
                    }
                    const auto gap_end = x;
                    if (gap_start > 0 && gap_end < width && gap_end - gap_start <= 3)
                    {
                        for (int gx = gap_start; gx < gap_end; ++gx)
                        {
                            auto& value = mask[static_cast<size_t>(y * width + gx)];
                            if (!value)
                            {
                                value = 1;
                                ++mask_pixels;
                            }
                        }
                    }
                }
            }

            std::vector<uint8_t> dilated = mask;
            for (int y = 1; y + 1 < height; ++y)
            {
                for (int x = 1; x + 1 < width; ++x)
                {
                    const auto index = static_cast<size_t>(y * width + x);
                    if (mask[index])
                    {
                        continue;
                    }
                    bool neighbor = false;
                    for (int dy = -1; dy <= 1 && !neighbor; ++dy)
                    {
                        for (int dx = -1; dx <= 1; ++dx)
                        {
                            if (mask[static_cast<size_t>((y + dy) * width + (x + dx))])
                            {
                                neighbor = true;
                                break;
                            }
                        }
                    }
                    if (neighbor)
                    {
                        dilated[index] = 1;
                        ++mask_pixels;
                    }
                }
            }
            mask = std::move(dilated);

            std::vector<XrayOverlayRun> runs{};
            runs.reserve(static_cast<size_t>(height * 3));
            mask_pixels = 0;
            for (int y = 0; y < height; ++y)
            {
                int x = 0;
                while (x < width)
                {
                    while (x < width && !mask[static_cast<size_t>(y * width + x)])
                    {
                        ++x;
                    }
                    const auto x0 = x;
                    while (x < width && mask[static_cast<size_t>(y * width + x)])
                    {
                        ++x;
                    }
                    const auto x1 = x;
                    const auto run_width = x1 - x0;
                    if (run_width >= 2)
                    {
                        runs.push_back(XrayOverlayRun{y, x0, x1});
                        mask_pixels += run_width;
                    }
                }
            }

            const auto coverage = static_cast<double>(mask_pixels) / static_cast<double>(std::max(1, total_pixels));
            cloak.delta_min = delta_min < 999999.0 ? delta_min : 0.0;
            cloak.delta_avg = delta_sum / static_cast<double>(std::max(1, valid_pixels));
            cloak.delta_max = delta_max;
            cloak.delta_threshold = threshold;
            cloak.mask_pixels = mask_pixels;
            cloak.mask_runs = static_cast<int>(runs.size());
            cloak.estimated_draw_calls = cloak.mask_runs;
            cloak.mask_coverage_pct = coverage;
            cloak.mask_ms = elapsed_ms_since(mask_start);
            if (mask_pixels < XrayOverlayMinMaskPixels)
            {
                cloak.failure = STR("cloak_empty_body_mask");
                return false;
            }
            if (coverage > XrayOverlayMaxMaskCoverage)
            {
                cloak.failure = STR("cloak_mask_coverage_implausible");
                return false;
            }
            if (static_cast<int>(runs.size()) > XrayOverlayMaxMaskRuns)
            {
                cloak.failure = STR("cloak_mask_too_fragmented");
                return false;
            }
            cloak.runs = std::move(runs);
            return true;
        }

        auto create_xray_overlay_capture(Unreal::UObject* pawn,
                                         const ProjectionFrame& frame,
                                         int rt_width,
                                         int rt_height) -> XrayOverlayCapture
        {
            XrayOverlayCapture result{};
            if (!pawn || rt_width <= 0 || rt_height <= 0)
            {
                result.failure = STR("cloak_capture_prereq_unavailable");
                return result;
            }
            auto* world = pawn->GetWorld();
            if (!world)
            {
                result.failure = STR("world_unavailable");
                return result;
            }
            auto* scene_capture_class =
                Unreal::UObjectGlobals::StaticFindObject<Unreal::UClass*>(nullptr, nullptr, STR("/Script/Engine.SceneCapture2D"));
            result.diagnostics.scene_capture_class = scene_capture_class != nullptr;
            if (!scene_capture_class)
            {
                result.failure = STR("scene_capture_class_unavailable");
                return result;
            }
            result.render_target = create_render_target(pawn, rt_width, rt_height);
            result.diagnostics.render_target = result.render_target != nullptr;
            if (!result.render_target)
            {
                result.failure = STR("render_target_unavailable");
                return result;
            }

            const auto rotation = frame.has_rotation ? frame.rotation : rotator_from_forward(frame.forward);
            result.capture_actor = world->SpawnActor(scene_capture_class, &frame.eye, &rotation);
            result.diagnostics.capture_actor = result.capture_actor != nullptr;
            if (!result.capture_actor)
            {
                result.failure = STR("capture_actor_spawn_failed");
                return result;
            }
            result.capture_component = find_capture_component(result.capture_actor);
            result.diagnostics.capture_component = result.capture_component != nullptr;
            if (!result.capture_component)
            {
                call_no_params(result.capture_actor, STR("K2_DestroyActor"));
                result.capture_actor = nullptr;
                result.failure = STR("capture_component_unavailable");
                return result;
            }

            result.diagnostics.texture_target_written =
                write_object_property_by_name(result.capture_component, STR("TextureTarget"), result.render_target);
            write_number_property_by_name(result.capture_component, STR("FOVAngle"), clamp(frame.fov_degrees, 10.0, 150.0));
            result.diagnostics.capture_source_written =
                write_number_property_by_name(result.capture_component, STR("CaptureSource"), SceneCaptureSourceFinalColorLdr);
            result.diagnostics.capture_every_frame_written =
                write_bool_property_by_name(result.capture_component, STR("bCaptureEveryFrame"), false);
            result.diagnostics.capture_on_movement_written =
                write_bool_property_by_name(result.capture_component, STR("bCaptureOnMovement"), false);
            result.diagnostics.persist_rendering_state_written =
                write_bool_property_by_name(result.capture_component, STR("bAlwaysPersistRenderingState"), true);
            configure_scene_capture_actor_filter(result.capture_component, pawn, true, false, &result.diagnostics);
            if (!result.diagnostics.texture_target_written)
            {
                call_no_params(result.capture_actor, STR("K2_DestroyActor"));
                result.capture_actor = nullptr;
                result.failure = STR("texture_target_write_failed");
                return result;
            }

            ActorHiddenGuard hidden_guard{pawn};
            const auto capture_start = SteadyClock::now();
            result.diagnostics.capture_scene_called = call_no_params(result.capture_component, STR("CaptureScene"));
            result.capture_ms = elapsed_ms_since(capture_start);
            if (!result.diagnostics.capture_scene_called)
            {
                call_no_params(result.capture_actor, STR("K2_DestroyActor"));
                result.capture_actor = nullptr;
                result.failure = STR("capture_scene_failed");
                return result;
            }
            result.ok = true;
            result.failure.clear();
            return result;
        }

        auto log_f10_target_state(const CharType* phase, uint64_t next_play_id) -> bool
        {
            auto* controller = find_player_controller();
            auto* controller_pawn = call_no_params_return_object(controller, STR("GetPawn"));
            auto* controller_view_target = call_no_params_return_object(controller, STR("GetViewTarget"));
            auto* camera = camera_manager_for_controller(controller);
            auto* camera_view_target = call_no_params_return_object(camera, STR("GetViewTarget"));
            auto* selected_pawn = find_player_pawn();
            auto* component = selected_pawn ? find_runtime_paint_component_for(selected_pawn) : nullptr;
            auto* world = selected_pawn ? selected_pawn->GetWorld() : (controller ? controller->GetWorld() : nullptr);
            const bool target_resolve_ok = selected_pawn && component;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} {} source=F10 clicks={} next_play_id={} input_ok=1 target_resolve_ok={} controller={} controller_pawn={} controller_view_target={} camera_view_target={} selected_pawn={} component={} world={}\n"),
                ModTag,
                phase,
                m_ui_clicks,
                next_play_id,
                target_resolve_ok ? 1 : 0,
                controller ? controller->GetFullName() : STR("<null>"),
                controller_pawn ? controller_pawn->GetFullName() : STR("<null>"),
                controller_view_target ? controller_view_target->GetFullName() : STR("<null>"),
                camera_view_target ? camera_view_target->GetFullName() : STR("<null>"),
                selected_pawn ? selected_pawn->GetFullName() : STR("<null>"),
                component ? component->GetFullName() : STR("<null>"),
                world ? world->GetFullName() : STR("<null>"));
            return target_resolve_ok;
        }

        auto write_last_status_file(const char* phase, bool target_resolve_ok) const -> void
        {
            const std::array<const char*, 4> paths{
                "MecchaCamouflage_last_status.txt",
                "Chameleon/Binaries/Win64/ue4ss/Mods/MecchaCamouflage/MecchaCamouflage_last_status.txt",
                "ue4ss/Mods/MecchaCamouflage/MecchaCamouflage_last_status.txt",
                "Mods/MecchaCamouflage/MecchaCamouflage_last_status.txt"};

            for (const auto* path : paths)
            {
                std::ofstream file{path, std::ios::trunc};
                if (!file)
                {
                    continue;
                }
                file << "mod=MecchaCamouflage\n";
                file << "phase=" << phase << "\n";
                file << "hotkey=F10\n";
                file << "input_ok=1\n";
                file << "target_resolve_ok=" << (target_resolve_ok ? 1 : 0) << "\n";
                file << "play_id=" << m_state.play_id << "\n";
                file << "success=" << m_state.success << "\n";
                file << "failures=" << m_state.failures << "\n";
                file << "visible_backend=" << (m_state.verified_visible_backend ? 1 : 0) << "\n";
                file << "body_hits=" << m_state.body_trace_hits << "\n";
                file << "uv_hits=" << m_state.uv_hits << "\n";
                file << "paint_backend=" << narrow_ascii(m_state.verified_paint_function) << "\n";
                file << "last_failure=" << narrow_ascii(m_state.last_failure) << "\n";
                file << "world=" << narrow_ascii(m_state.current_world) << "\n";
                file << "pawn=" << narrow_ascii(m_state.current_pawn) << "\n";
                file << "component=" << narrow_ascii(m_state.current_component) << "\n";
            }
        }

        auto request_camouflage_from_ui() -> void
        {
            if (m_ui_play_running || m_ui_play_requested)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} job_request source=F10 ignored=busy clicks={} input_ok=1 running={} requested={}\n"),
                    ModTag,
                    m_ui_clicks,
                    m_ui_play_running ? 1 : 0,
                    m_ui_play_requested ? 1 : 0);
                return;
            }
            m_ui_play_requested = true;
            ++m_ui_clicks;
            log_f10_target_state(STR("job_request"), m_state.play_id + 1);
        }

        auto run_pending_ui_camouflage() -> void
        {
            if (!m_ui_play_requested || m_ui_play_running)
            {
                return;
            }
            m_ui_play_requested = false;
            m_ui_play_running = true;
            log_f10_target_state(STR("job_start"), m_state.play_id + 1);
            run_play();
            const bool target_resolve_ok = m_state.last_failure != STR("player_pawn_unavailable") &&
                                           m_state.last_failure != STR("runtime_paint_component_unavailable") &&
                                           m_state.last_failure != STR("play_screen_body_prereq_unavailable");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} job_done source=F10 play_id={} input_ok=1 target_resolve_ok={} body_hits={} uv_hits={} success={} failures={} visible_backend={} last_failure={}\n"),
                ModTag,
                m_state.play_id,
                target_resolve_ok ? 1 : 0,
                m_state.body_trace_hits,
                m_state.uv_hits,
                m_state.success,
                m_state.failures,
                m_state.verified_visible_backend ? 1 : 0,
                m_state.last_failure);
            write_last_status_file("job_done", target_resolve_ok);
            m_ui_play_running = false;
        }

        auto print_cloak_status() const -> void
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} cloak_status version={} overlay_active={} hook_installed={} umg_overlay_active={} umg_images={} mesh_material_active={} mesh_slots={} mesh_dynamic_materials={} mesh_texture_param_calls={} mesh_scalar_param_calls={} umg_canvas_candidates={} umg_canvas={} umg_failure={} actor_hide_fallback={} pawn_hidden={} rt_size={}x{} viewport={}x{} mask_pixels={} mask_coverage_pct={} mask_runs={} estimated_draw_calls={} hud_frames={} last_frame_draw_calls={} last_frame_draw_failures={} total_draw_calls={} total_draw_failures={} bulk_calibration_ok={} visible_bulk_ok={} hidden_bulk_ok={} visible_bulk_relaxed={} hidden_bulk_relaxed={} visible_backend={} hidden_backend={} visible_calibration={} hidden_calibration={} delta_min={} delta_avg={} delta_max={} delta_threshold={} t_visible_capture_ms={} t_hidden_capture_ms={} t_draw_capture_ms={} t_readback_ms={} t_mask_ms={} t_total_ms={} no_gui=1 no_paint=1 no_import=1 no_strokes=1 no_commit=1 no_mesh_hide=1 last_failure={}\n"),
                ModTag,
                ModVersion,
                m_cloak.active ? 1 : 0,
                m_cloak.hook_installed ? 1 : 0,
                m_cloak.umg_overlay_active ? 1 : 0,
                m_cloak.umg_images,
                m_cloak.mesh_material_active ? 1 : 0,
                m_cloak.mesh_material_slots,
                m_cloak.mesh_dynamic_materials_created,
                m_cloak.mesh_texture_param_calls,
                m_cloak.mesh_scalar_param_calls,
                m_cloak.umg_canvas_candidates,
                m_cloak.umg_canvas_name.empty() ? STR("<none>") : m_cloak.umg_canvas_name,
                m_cloak.umg_failure.empty() ? STR("<none>") : m_cloak.umg_failure,
                m_cloak.actor_hide_fallback ? 1 : 0,
                m_cloak.pawn_hidden ? 1 : 0,
                m_cloak.rt_width,
                m_cloak.rt_height,
                m_cloak.viewport_width,
                m_cloak.viewport_height,
                m_cloak.mask_pixels,
                m_cloak.mask_coverage_pct,
                m_cloak.mask_runs,
                m_cloak.estimated_draw_calls,
                m_cloak.hud_frames,
                m_cloak.last_frame_draw_calls,
                m_cloak.last_frame_draw_failures,
                m_cloak.total_draw_calls,
                m_cloak.total_draw_failures,
                m_cloak.bulk_calibration_ok ? 1 : 0,
                m_cloak.visible_bulk_ok ? 1 : 0,
                m_cloak.hidden_bulk_ok ? 1 : 0,
                m_cloak.visible_bulk_relaxed ? 1 : 0,
                m_cloak.hidden_bulk_relaxed ? 1 : 0,
                m_cloak.visible_backend.empty() ? STR("<none>") : m_cloak.visible_backend,
                m_cloak.hidden_backend.empty() ? STR("<none>") : m_cloak.hidden_backend,
                m_cloak.visible_calibration.empty() ? STR("<none>") : m_cloak.visible_calibration,
                m_cloak.hidden_calibration.empty() ? STR("<none>") : m_cloak.hidden_calibration,
                m_cloak.delta_min,
                m_cloak.delta_avg,
                m_cloak.delta_max,
                m_cloak.delta_threshold,
                m_cloak.visible_capture_ms,
                m_cloak.hidden_capture_ms,
                m_cloak.draw_capture_ms,
                m_cloak.readback_ms,
                m_cloak.mask_ms,
                m_cloak.total_ms,
                m_cloak.failure.empty() ? m_state.last_failure : m_cloak.failure);
        }

        auto apply_mesh_material_render_target(Unreal::UObject* mesh, Unreal::UObject* render_target) -> bool
        {
            if (!mesh || !render_target)
            {
                m_cloak.failure = STR("mesh_material_prereq_unavailable");
                return false;
            }

            int slot_count = static_cast<int>(call_no_params_return_number(mesh, STR("GetNumMaterials")).value_or(0.0));
            if (slot_count <= 0)
            {
                for (int slot = 0; slot < 8; ++slot)
                {
                    if (call_number_return_object(mesh, STR("GetMaterial"), static_cast<double>(slot)))
                    {
                        slot_count = slot + 1;
                    }
                }
            }
            slot_count = std::max(0, std::min(slot_count, 16));
            if (slot_count <= 0)
            {
                m_cloak.failure = STR("mesh_material_slots_unavailable");
                return false;
            }

            const std::array<const CharType*, 12> texture_params{STR("MecchaCamoTexture"),
                                                                 STR("MecchaCamoCapture"),
                                                                 STR("CloakTexture"),
                                                                 STR("CloakCapture"),
                                                                 STR("BackgroundTexture"),
                                                                 STR("BackgroundCapture"),
                                                                 STR("ScreenTexture"),
                                                                 STR("ScreenCapture"),
                                                                 STR("SceneCapture"),
                                                                 STR("BaseColorTexture"),
                                                                 STR("AlbedoTexture"),
                                                                 STR("Texture")};
            const std::array<const CharType*, 6> scalar_params{STR("MecchaCamoEnabled"),
                                                               STR("CloakEnabled"),
                                                               STR("UseCloak"),
                                                               STR("UseScreenProjection"),
                                                               STR("Opacity"),
                                                               STR("Alpha")};

            m_cloak.mesh_object = mesh;
            m_cloak.mesh_material_active = true;
            m_cloak.mesh_material_slots = slot_count;
            m_cloak.mesh_original_materials.clear();
            m_cloak.mesh_dynamic_materials.clear();
            m_cloak.mesh_original_materials.reserve(static_cast<size_t>(slot_count));
            m_cloak.mesh_dynamic_materials.reserve(static_cast<size_t>(slot_count));

            for (int slot = 0; slot < slot_count; ++slot)
            {
                auto* original = call_number_return_object(mesh, STR("GetMaterial"), static_cast<double>(slot));
                m_cloak.mesh_original_materials.push_back(original);
                auto* dynamic_material = create_dynamic_material_instance(mesh, slot, original, STR("MecchaCamoCloak"));
                if (!dynamic_material)
                {
                    m_cloak.mesh_dynamic_materials.push_back(nullptr);
                    continue;
                }
                m_cloak.mesh_dynamic_materials.push_back(dynamic_material);
                ++m_cloak.mesh_dynamic_materials_created;
                for (const auto* name : texture_params)
                {
                    if (call_name_object_param(dynamic_material, STR("SetTextureParameterValue"), name, render_target))
                    {
                        ++m_cloak.mesh_texture_param_calls;
                    }
                }
                for (const auto* name : scalar_params)
                {
                    if (call_name_number_param(dynamic_material, STR("SetScalarParameterValue"), name, 1.0))
                    {
                        ++m_cloak.mesh_scalar_param_calls;
                    }
                }
            }

            if (m_cloak.mesh_dynamic_materials_created <= 0)
            {
                m_cloak.failure = STR("mesh_dynamic_material_unavailable");
                restore_mesh_material_cloak();
                return false;
            }
            if (m_cloak.mesh_texture_param_calls <= 0)
            {
                m_cloak.failure = STR("mesh_material_texture_parameter_unavailable");
                restore_mesh_material_cloak();
                return false;
            }

            m_cloak.mesh_material_active = true;
            return true;
        }

        auto run_mesh_material_cloak_play() -> void
        {
            const auto total_start = SteadyClock::now();
            disable_cloak_overlay(false);
            m_cloak = XrayOverlayState{};

            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.current_world.clear();
            m_state.current_pawn.clear();
            m_state.current_component.clear();
            m_state.verified_visible_backend = false;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.paint_uv_success = 0;
            m_state.paint_world_success = 0;
            m_state.commit_calls = 0;
            m_state.verified_paint_function = STR("mesh_material_render_target");
            m_state.verified_paint_channel = PaintChannelUnknown;

            if (m_screen_dot_active)
            {
                restore_screen_paint_dot(false);
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play started id={} version={} route=mesh_material_cloak backend=DynamicMaterial.RenderTarget no_gui=1 no_paint=1 no_import=1 no_strokes=1 no_clear=1 no_commit=1 no_uv_atlas=1 no_mesh_hide=1 one_shot=1\n"),
                ModTag,
                m_state.play_id,
                ModVersion);

            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            auto* mesh = component ? find_target_mesh_for_runtime_paint(component, pawn) : nullptr;
            if (!mesh && pawn)
            {
                mesh = read_object_property_by_name(pawn, STR("Mesh"));
            }
            auto* controller = pawn ? find_player_controller_for_pawn(pawn) : find_player_controller();
            auto viewport = get_viewport_info(controller);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};
            if (!pawn || !controller || !frame || !mesh)
            {
                m_state.failures = 1;
                m_state.last_failure = !pawn ? STR("player_pawn_unavailable")
                                             : (!controller ? STR("player_controller_unavailable")
                                                            : (!frame ? STR("camera_projection_frame_unavailable")
                                                                      : STR("target_mesh_unavailable")));
                m_cloak.failure = m_state.last_failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play mesh_material refused reason={} pawn={} controller={} mesh={} viewport={}x{} no_gui=1 no_paint=1 no_import=1 no_strokes=1 no_commit=1\n"),
                    ModTag,
                    m_state.last_failure,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    mesh ? mesh->GetFullName() : STR("<null>"),
                    viewport.width,
                    viewport.height);
                return;
            }

            m_state.current_world = object_name_or_empty(pawn->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(mesh);
            m_cloak.viewport_width = viewport.width;
            m_cloak.viewport_height = viewport.height;
            const auto aspect = static_cast<double>(std::max(1, viewport.width)) /
                                static_cast<double>(std::max(1, viewport.height));
            m_cloak.rt_width = std::min(XrayOverlayRtMaxWidth, std::max(320, viewport.width));
            m_cloak.rt_height = std::max(1, static_cast<int>(std::round(static_cast<double>(m_cloak.rt_width) / std::max(0.1, aspect))));

            auto capture = create_xray_overlay_capture(pawn, *frame, m_cloak.rt_width, m_cloak.rt_height);
            m_cloak.draw_capture_ms = capture.capture_ms;
            if (!capture.ok || !capture.render_target)
            {
                m_state.failures = 1;
                m_state.last_failure = STR("mesh_material_capture_failed");
                m_cloak.failure = capture.failure.empty() ? m_state.last_failure : capture.failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play mesh_material refused reason={} capture_failure={} rt_size={}x{} no_gui=1 no_paint=1 no_import=1 no_strokes=1 no_commit=1\n"),
                    ModTag,
                    m_state.last_failure,
                    m_cloak.failure.empty() ? STR("<none>") : m_cloak.failure,
                    m_cloak.rt_width,
                    m_cloak.rt_height);
                return;
            }

            m_cloak.render_target = capture.render_target;
            m_cloak.capture_actor = capture.capture_actor;
            m_cloak.capture_component = capture.capture_component;
            if (!apply_mesh_material_render_target(mesh, m_cloak.render_target))
            {
                m_state.failures = 1;
                m_state.last_failure = m_cloak.failure.empty() ? STR("mesh_material_shader_unavailable") : m_cloak.failure;
                m_cloak.total_ms = elapsed_ms_since(total_start);
                destroy_cloak_capture_actor();
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play mesh_material refused reason={} mesh={} rt_size={}x{} mesh_slots={} dynamic_materials={} texture_param_calls={} scalar_param_calls={} note=material_must_have_screen_space_cloak_shader no_gui=1 no_paint=1 no_import=1 no_strokes=1 no_commit=1 no_mesh_hide=1\n"),
                    ModTag,
                    m_state.last_failure,
                    mesh->GetFullName(),
                    m_cloak.rt_width,
                    m_cloak.rt_height,
                    m_cloak.mesh_material_slots,
                    m_cloak.mesh_dynamic_materials_created,
                    m_cloak.mesh_texture_param_calls,
                    m_cloak.mesh_scalar_param_calls);
                return;
            }

            m_cloak.active = true;
            m_cloak.failure = STR("play_mesh_material_render_target_applied_unverified_shader");
            m_cloak.total_ms = elapsed_ms_since(total_start);
            m_state.success = 1;
            m_state.failures = 0;
            m_state.capture_pixels_ready = true;
            m_state.visible_samples = 0;
            m_state.background_pixels = 0;
            m_state.atlas_bins = 0;
            m_state.verified_visible_backend = false;
            m_state.last_failure = m_cloak.failure;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play mesh_material applied mesh_material_active=1 overlay_active=1 umg_overlay_active=0 mesh={} rt_size={}x{} viewport={}x{} mesh_slots={} dynamic_materials={} texture_param_calls={} scalar_param_calls={} actor_hide_fallback=0 pawn_hidden=0 no_gui=1 no_paint=1 no_import=1 no_strokes=1 no_clear=1 no_commit=1 no_mesh_hide=1 one_shot=1 shader_visibility_unverified=1 camera_fov={} fov_source={} camera_eye=({}, {}, {}) t_draw_capture_ms={} t_total_ms={} last_failure={}\n"),
                ModTag,
                mesh->GetFullName(),
                m_cloak.rt_width,
                m_cloak.rt_height,
                viewport.width,
                viewport.height,
                m_cloak.mesh_material_slots,
                m_cloak.mesh_dynamic_materials_created,
                m_cloak.mesh_texture_param_calls,
                m_cloak.mesh_scalar_param_calls,
                frame->fov_degrees,
                frame->fov_source.empty() ? STR("<none>") : frame->fov_source,
                frame->eye.X(),
                frame->eye.Y(),
                frame->eye.Z(),
                m_cloak.draw_capture_ms,
                m_cloak.total_ms,
                m_state.last_failure);
        }

        auto run_xray_overlay_play() -> void
        {
            const auto total_start = SteadyClock::now();
            disable_cloak_overlay(false);
            m_cloak = XrayOverlayState{};
            m_cloak.hook_installed = false;

            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.current_world.clear();
            m_state.current_pawn.clear();
            m_state.current_component.clear();
            m_state.verified_visible_backend = false;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.paint_uv_success = 0;
            m_state.paint_world_success = 0;
            m_state.commit_calls = 0;
            m_state.verified_paint_function = STR("direct_screen_composite_umg_runs");
            m_state.verified_paint_channel = PaintChannelUnknown;

            if (m_screen_dot_active)
            {
                restore_screen_paint_dot(false);
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play started id={} version={} route=direct_screen_composite backend=UMG.masked_runs no_paint=1 no_import=1 no_strokes=1 no_clear=1 no_commit=1 no_uv_atlas=1 no_mesh_hide=1 one_shot=1\n"),
                ModTag,
                m_state.play_id,
                ModVersion);

            auto* pawn = find_player_pawn();
            auto* controller = pawn ? find_player_controller_for_pawn(pawn) : find_player_controller();
            auto viewport = get_viewport_info(controller);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};
            if (!pawn || !controller || !frame)
            {
                m_state.failures = 1;
                m_state.last_failure = !pawn ? STR("player_pawn_unavailable")
                                             : (!controller ? STR("player_controller_unavailable")
                                                            : STR("camera_projection_frame_unavailable"));
                m_cloak.failure = m_state.last_failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play xray_overlay refused reason={} pawn={} controller={} viewport={}x{} no_paint=1 no_import=1 no_strokes=1 no_commit=1\n"),
                    ModTag,
                    m_state.last_failure,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    viewport.width,
                    viewport.height);
                return;
            }

            auto* world = pawn->GetWorld();
            m_state.current_world = object_name_or_empty(world);
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = STR("<direct_screen_composite>");
            m_cloak.viewport_width = viewport.width;
            m_cloak.viewport_height = viewport.height;
            const auto aspect = static_cast<double>(std::max(1, viewport.width)) /
                                static_cast<double>(std::max(1, viewport.height));
            m_cloak.rt_width = std::min(XrayOverlayRtMaxWidth, std::max(320, viewport.width));
            m_cloak.rt_height = std::max(1, static_cast<int>(std::round(static_cast<double>(m_cloak.rt_width) / std::max(0.1, aspect))));

            const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
            const auto calibration_samples = make_xray_calibration_samples(viewport);
            auto visible_capture = capture_render_target_image(pawn,
                                                               frame->eye,
                                                               look_at,
                                                               m_cloak.rt_width,
                                                               m_cloak.rt_height,
                                                               false,
                                                               m_state,
                                                               frame->fov_degrees,
                                                               frame->has_rotation ? &frame->rotation : nullptr,
                                                               &calibration_samples,
                                                               nullptr,
                                                               static_cast<int>(calibration_samples.size()));
            auto hidden_capture = capture_render_target_image(pawn,
                                                              frame->eye,
                                                              look_at,
                                                              m_cloak.rt_width,
                                                              m_cloak.rt_height,
                                                              true,
                                                              m_state,
                                                              frame->fov_degrees,
                                                              frame->has_rotation ? &frame->rotation : nullptr,
                                                              &calibration_samples,
                                                              nullptr,
                                                              static_cast<int>(calibration_samples.size()));
            m_cloak.visible_capture_ms = visible_capture.capture_ms;
            m_cloak.hidden_capture_ms = hidden_capture.capture_ms;
            m_cloak.readback_ms = visible_capture.readback_ms + hidden_capture.readback_ms;
            m_cloak.visible_backend = visible_capture.image.backend;
            m_cloak.hidden_backend = hidden_capture.image.backend;
            m_cloak.visible_calibration = visible_capture.image.bulk_calibration_backend;
            m_cloak.hidden_calibration = hidden_capture.image.bulk_calibration_backend;
            const auto hidden_summary = render_image_summary(hidden_capture.image);
            const auto visible_bulk_strict = visible_capture.image.bulk_calibration_ok;
            const auto hidden_bulk_strict = hidden_capture.image.bulk_calibration_ok;
            const auto visible_bulk_ok = xray_bulk_readback_orientation_ok(visible_capture.image);
            const auto hidden_bulk_ok = xray_bulk_readback_orientation_ok(hidden_capture.image);
            if (visible_bulk_ok)
            {
                visible_capture.image.bulk_calibration_ok = true;
            }
            if (hidden_bulk_ok)
            {
                hidden_capture.image.bulk_calibration_ok = true;
            }
            m_cloak.visible_bulk_ok = visible_bulk_ok;
            m_cloak.hidden_bulk_ok = hidden_bulk_ok;
            m_cloak.visible_bulk_relaxed = visible_bulk_ok && !visible_bulk_strict;
            m_cloak.hidden_bulk_relaxed = hidden_bulk_ok && !hidden_bulk_strict;
            m_cloak.bulk_calibration_ok = visible_bulk_ok && (hidden_bulk_ok || hidden_summary.uniform);

            if (!visible_capture.image.ok || !hidden_capture.image.ok)
            {
                m_state.failures = 1;
                m_state.last_failure = STR("cloak_capture_failed");
                m_cloak.failure = m_state.last_failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play xray_overlay refused reason={} visible_ok={} hidden_ok={} visible_failure={} hidden_failure={} rt_size={}x{} no_paint=1 no_import=1 no_strokes=1 no_commit=1\n"),
                    ModTag,
                    m_state.last_failure,
                    visible_capture.image.ok ? 1 : 0,
                    hidden_capture.image.ok ? 1 : 0,
                    visible_capture.image.failure.empty() ? STR("<none>") : visible_capture.image.failure,
                    hidden_capture.image.failure.empty() ? STR("<none>") : hidden_capture.image.failure,
                    m_cloak.rt_width,
                    m_cloak.rt_height);
                return;
            }

            if (!build_xray_mask_runs(visible_capture.image, hidden_capture.image, hidden_summary, m_cloak))
            {
                m_state.failures = 1;
                m_state.last_failure = m_cloak.failure.empty() ? STR("cloak_empty_body_mask") : m_cloak.failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play xray_overlay refused reason={} visible_bulk_ok={} hidden_bulk_ok={} visible_bulk_relaxed={} hidden_bulk_relaxed={} hidden_uniform={} rt_size={}x{} mask_pixels={} mask_runs={} mask_coverage_pct={} delta_min={} delta_avg={} delta_max={} delta_threshold={} no_paint=1 no_import=1 no_strokes=1 no_commit=1\n"),
                    ModTag,
                    m_state.last_failure,
                    visible_bulk_ok ? 1 : 0,
                    hidden_bulk_ok ? 1 : 0,
                    m_cloak.visible_bulk_relaxed ? 1 : 0,
                    m_cloak.hidden_bulk_relaxed ? 1 : 0,
                    hidden_summary.uniform ? 1 : 0,
                    m_cloak.rt_width,
                    m_cloak.rt_height,
                    m_cloak.mask_pixels,
                    m_cloak.mask_runs,
                    m_cloak.mask_coverage_pct,
                    m_cloak.delta_min,
                    m_cloak.delta_avg,
                    m_cloak.delta_max,
                    m_cloak.delta_threshold);
                return;
            }

            auto draw_capture = create_xray_overlay_capture(pawn, *frame, m_cloak.rt_width, m_cloak.rt_height);
            m_cloak.draw_capture_ms = draw_capture.capture_ms;
            if (!draw_capture.ok || !draw_capture.render_target)
            {
                m_state.failures = 1;
                m_state.last_failure = STR("cloak_capture_failed");
                m_cloak.failure = draw_capture.failure.empty() ? m_state.last_failure : draw_capture.failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play xray_overlay refused reason={} capture_failure={} rt_size={}x{} no_paint=1 no_import=1 no_strokes=1 no_commit=1\n"),
                    ModTag,
                    m_state.last_failure,
                    m_cloak.failure.empty() ? STR("<none>") : m_cloak.failure,
                    m_cloak.rt_width,
                    m_cloak.rt_height);
                return;
            }

            m_cloak.render_target = draw_capture.render_target;
            m_cloak.capture_actor = draw_capture.capture_actor;
            m_cloak.capture_component = draw_capture.capture_component;
            m_cloak.actor_hide_fallback = false;
            m_cloak.pawn_hidden = false;
            m_cloak.hidden_pawn = nullptr;

            if (!install_umg_masked_run_overlay(m_cloak.render_target))
            {
                m_state.failures = 1;
                m_state.last_failure = STR("cloak_umg_masked_overlay_failed");
                m_cloak.failure = m_cloak.umg_failure.empty() ? m_state.last_failure : m_cloak.umg_failure;
                m_cloak.total_ms = elapsed_ms_since(total_start);
                destroy_cloak_capture_actor();
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play direct_screen_composite refused reason={} umg_failure={} umg_canvas_candidates={} umg_canvas={} rt_size={}x{} viewport={}x{} mask_runs={} no_paint=1 no_import=1 no_strokes=1 no_commit=1 no_mesh_hide=1 one_shot=1\n"),
                    ModTag,
                    m_state.last_failure,
                    m_cloak.failure.empty() ? STR("<none>") : m_cloak.failure,
                    m_cloak.umg_canvas_candidates,
                    m_cloak.umg_canvas_name.empty() ? STR("<none>") : m_cloak.umg_canvas_name,
                    m_cloak.rt_width,
                    m_cloak.rt_height,
                    viewport.width,
                    viewport.height,
                    m_cloak.mask_runs);
                return;
            }

            m_cloak.active = true;
            m_cloak.estimated_draw_calls = m_cloak.umg_images;
            m_cloak.failure = STR("play_direct_screen_composite_applied");
            m_cloak.total_ms = elapsed_ms_since(total_start);
            m_state.success = 1;
            m_state.failures = 0;
            m_state.capture_pixels_ready = true;
            m_state.visible_samples = m_cloak.mask_pixels;
            m_state.background_pixels = static_cast<int>(hidden_capture.image.pixels.size());
            m_state.atlas_bins = 0;
            m_state.verified_visible_backend = true;
            m_state.last_failure = STR("play_direct_screen_composite_applied");

            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play direct_screen_composite applied overlay_active=1 umg_overlay_active=1 umg_images={} umg_canvas_candidates={} umg_canvas={} actor_hide_fallback=0 pawn_hidden=0 no_paint=1 no_import=1 no_strokes=1 no_clear=1 no_commit=1 no_mesh_hide=1 one_shot=1 rt_size={}x{} viewport={}x{} mask_pixels={} mask_coverage_pct={} mask_runs={} estimated_draw_calls={} bulk_calibration_ok={} visible_bulk_ok={} hidden_bulk_ok={} visible_bulk_relaxed={} hidden_bulk_relaxed={} visible_backend={} hidden_backend={} visible_calibration={} hidden_calibration={} hidden_uniform={} delta_min={} delta_avg={} delta_max={} delta_threshold={} capture_visible_ok={} capture_hidden_ok={} capture_draw_ok={} camera_fov={} fov_source={} camera_eye=({}, {}, {}) t_visible_capture_ms={} t_hidden_capture_ms={} t_draw_capture_ms={} t_readback_ms={} t_mask_ms={} t_total_ms={} last_failure={}\n"),
                ModTag,
                m_cloak.umg_images,
                m_cloak.umg_canvas_candidates,
                m_cloak.umg_canvas_name.empty() ? STR("<none>") : m_cloak.umg_canvas_name,
                m_cloak.rt_width,
                m_cloak.rt_height,
                viewport.width,
                viewport.height,
                m_cloak.mask_pixels,
                m_cloak.mask_coverage_pct,
                m_cloak.mask_runs,
                m_cloak.estimated_draw_calls,
                m_cloak.bulk_calibration_ok ? 1 : 0,
                m_cloak.visible_bulk_ok ? 1 : 0,
                m_cloak.hidden_bulk_ok ? 1 : 0,
                m_cloak.visible_bulk_relaxed ? 1 : 0,
                m_cloak.hidden_bulk_relaxed ? 1 : 0,
                m_cloak.visible_backend.empty() ? STR("<none>") : m_cloak.visible_backend,
                m_cloak.hidden_backend.empty() ? STR("<none>") : m_cloak.hidden_backend,
                m_cloak.visible_calibration.empty() ? STR("<none>") : m_cloak.visible_calibration,
                m_cloak.hidden_calibration.empty() ? STR("<none>") : m_cloak.hidden_calibration,
                hidden_summary.uniform ? 1 : 0,
                m_cloak.delta_min,
                m_cloak.delta_avg,
                m_cloak.delta_max,
                m_cloak.delta_threshold,
                visible_capture.image.ok ? 1 : 0,
                hidden_capture.image.ok ? 1 : 0,
                draw_capture.ok ? 1 : 0,
                frame->fov_degrees,
                frame->fov_source.empty() ? STR("<none>") : frame->fov_source,
                frame->eye.X(),
                frame->eye.Y(),
                frame->eye.Z(),
                m_cloak.visible_capture_ms,
                m_cloak.hidden_capture_ms,
                m_cloak.draw_capture_ms,
                m_cloak.readback_ms,
                m_cloak.mask_ms,
                m_cloak.total_ms,
                m_state.last_failure);
        }

        auto handle_command(const StringType& command) -> bool
        {
            if (command == STR("mcam_core_ping"))
            {
                RC::Output::send<RC::LogLevel::Verbose>(STR("{} ping ok version={}\n"), ModTag, ModVersion);
                return true;
            }
            if (command == STR("mcam_core_status"))
            {
                log_projection_tuning(STR("status"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_cloak_status"))
            {
                print_cloak_status();
                return true;
            }
            if (command == STR("mcam_core_cloak_off"))
            {
                disable_cloak_overlay(true);
                print_cloak_status();
                return true;
            }
            if (command == STR("mcam_core_cloak_refresh"))
            {
                run_play();
                print_status();
                print_cloak_status();
                return true;
            }
            if (command == STR("mcam_core_projection_status"))
            {
                log_projection_tuning(STR("command_status"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_reset"))
            {
                reset_projection_tuning(false);
                log_projection_tuning(STR("command_reset_default"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_neutral"))
            {
                reset_projection_tuning(true);
                log_projection_tuning(STR("command_neutral"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_out"))
            {
                nudge_projection_scale(ProjectionTuningScaleStep);
                log_projection_tuning(STR("command_out"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_in"))
            {
                nudge_projection_scale(-ProjectionTuningScaleStep);
                log_projection_tuning(STR("command_in"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_left"))
            {
                nudge_projection_offset(-ProjectionTuningOffsetStep, 0.0);
                log_projection_tuning(STR("command_left"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_right"))
            {
                nudge_projection_offset(ProjectionTuningOffsetStep, 0.0);
                log_projection_tuning(STR("command_right"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_up"))
            {
                nudge_projection_offset(0.0, -ProjectionTuningOffsetStep);
                log_projection_tuning(STR("command_up"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_down"))
            {
                nudge_projection_offset(0.0, ProjectionTuningOffsetStep);
                log_projection_tuning(STR("command_down"), make_body_bbox_projection_transform(ScreenHitCollectionStats{}));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_cancel"))
            {
                m_state.cancelled = true;
                m_state.queue_active = false;
                m_state.last_failure = STR("cancelled");
                RC::Output::send<RC::LogLevel::Verbose>(STR("{} cancel ok\n"), ModTag);
                return true;
            }
            if (command == STR("mcam_core_probe"))
            {
                run_quality_probe(false);
                print_status();
                return true;
            }
            if (command == STR("mcam_core_dump_funcs"))
            {
                run_dependency_probe(true);
                dump_core_signatures(true);
                print_status();
                return true;
            }
            if (command == STR("mcam_core_paint_pipeline"))
            {
                run_dependency_probe(true);
                run_paint_pipeline_audit(true);
                print_status();
                return true;
            }
            if (command == STR("mcam_core_runtime_contract"))
            {
                run_runtime_contract_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_game_camera_contract"))
            {
                run_game_camera_contract_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_screen_contract"))
            {
                run_screen_contract_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_capture_contract"))
            {
                run_capture_contract_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_readback_contract"))
            {
                run_readback_contract_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_audit"))
            {
                run_projection_audit(false);
                print_status();
                return true;
            }
            if (command == STR("mcam_core_projection_export"))
            {
                run_projection_audit(true);
                print_status();
                return true;
            }
            if (command == STR("mcam_core_uv_atlas_cal_status"))
            {
                run_uv_atlas_cal_status();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_uv_atlas_cal_begin"))
            {
                run_uv_atlas_cal_begin();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_uv_atlas_cal_finish"))
            {
                run_uv_atlas_cal_finish();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_uv_atlas_cal_restore"))
            {
                restore_uv_atlas_calibration(true);
                print_status();
                return true;
            }
            if (command == STR("mcam_core_screen_paint_dot"))
            {
                run_screen_paint_dot_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_screen_paint_restore"))
            {
                restore_screen_paint_dot(true);
                print_status();
                return true;
            }
            if (command == STR("mcam_core_export_contract"))
            {
                run_export_contract_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_import_roundtrip_probe"))
            {
                run_import_roundtrip_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_import_patch_probe"))
            {
                run_import_patch_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_probe_paint_modes"))
            {
                run_probe_paint_modes();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_probe_commit_modes"))
            {
                run_probe_commit_modes();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_channel_probe"))
            {
                run_channel_probe();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_clear"))
            {
                run_clear_current_paint();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_capture_paint_on"))
            {
                start_paint_capture();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_capture_paint_off"))
            {
                stop_paint_capture();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_capture_paint_status"))
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} paint_capture enabled={} calls={} matches={} unique_functions={}\n"),
                    ModTag,
                    m_state.paint_capture_enabled ? 1 : 0,
                    m_state.paint_capture_calls,
                    m_state.paint_capture_matches,
                    static_cast<int>(m_seen_paint_capture_functions.size()));
                print_status();
                return true;
            }
            if (command == STR("mcam_core_paint_test"))
            {
                run_paint_test();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_brush_calibrate"))
            {
                run_brush_calibrate();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_capture_test"))
            {
                run_capture_test();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_trace_test"))
            {
                run_trace_test();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_uv_test"))
            {
                run_uv_test();
                print_status();
                return true;
            }
            if (command == STR("mcam_core_lab_probe_view") || command == STR("mcam_lab_probe_view"))
            {
                run_lab_probe_view();
                return true;
            }
            if (command == STR("mcam_core_lab_probe_mapping") || command == STR("mcam_lab_probe_mapping"))
            {
                run_lab_probe_mapping();
                return true;
            }
            if (command == STR("mcam_core_lab_probe_capture") || command == STR("mcam_lab_probe_capture"))
            {
                run_lab_probe_capture();
                return true;
            }
            if (command == STR("mcam_core_lab_probe_grid") || command == STR("mcam_lab_probe_grid"))
            {
                run_lab_probe_grid();
                return true;
            }
            if (command == STR("mcam_core_lab_probe_visibility") || command == STR("mcam_lab_probe_visibility"))
            {
                run_lab_probe_visibility();
                return true;
            }
            if (command == STR("mcam_core_play_disabled"))
            {
                run_play_disabled();
                print_status();
                return true;
            }
            if (command == STR("play") || command == STR("mcam_play") || command == STR("mcam_core_play"))
            {
                run_play();
                print_status();
                return true;
            }
            return false;
        }

        auto run_dependency_probe(bool quiet) -> void
        {
            m_state.runtime_paint_components = count_class_instances(STR("RuntimePaintableComponent"));
            m_state.skeletal_mesh_components = count_class_instances(STR("SkeletalMeshComponent"));

            m_state.scene_capture_functions = 0;
            m_state.scene_capture_functions += find_function(STR("/Script/Engine.KismetRenderingLibrary:CreateRenderTarget2D")) ? 1 : 0;
            m_state.scene_capture_functions += find_function(STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetPixel")) ? 1 : 0;
            m_state.scene_capture_functions += find_function(STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetRawPixel")) ? 1 : 0;
            m_state.scene_capture_functions += find_function(STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTarget")) ? 1 : 0;
            m_state.scene_capture_functions += find_function(STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetRaw")) ? 1 : 0;

            m_state.trace_functions = 0;
            m_state.trace_functions += find_function(STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle")) ? 1 : 0;
            m_state.trace_functions += find_function(STR("/Script/Engine.KismetSystemLibrary:LineTraceSingleForObjects")) ? 1 : 0;
            m_state.trace_functions += find_function(STR("/Script/Engine.KismetSystemLibrary:SphereTraceSingle")) ? 1 : 0;

            auto* paint_object = find_runtime_paint_object_with_uv();
            m_state.paint_functions = 0;
            m_state.paint_functions += paint_object && paint_object->GetFunctionByNameInChain(STR("PaintAtUVWithBrush")) ? 1 : 0;
            m_state.paint_functions += paint_object && paint_object->GetFunctionByNameInChain(STR("PaintAtUV")) ? 1 : 0;
            m_state.paint_functions += paint_object && paint_object->GetFunctionByNameInChain(STR("PaintStrokeUV")) ? 1 : 0;
            m_state.paint_functions += paint_object && paint_object->GetFunctionByNameInChain(STR("PaintAtWorldPosition")) ? 1 : 0;
            m_state.paint_functions += paint_object && paint_object->GetFunctionByNameInChain(STR("PaintAtScreenPosition")) ? 1 : 0;
            m_state.paint_functions += paint_object && paint_object->GetFunctionByNameInChain(STR("HitTestAtScreenPosition")) ? 1 : 0;

            m_state.uv_functions = m_state.paint_functions;
            m_state.paint_path_available = m_state.paint_functions > 0;
            m_state.capture_path_available = m_state.scene_capture_functions >= 2;
            m_state.trace_path_available = m_state.trace_functions > 0 && m_state.skeletal_mesh_components > 0;
            m_state.uv_path_available = paint_object != nullptr;

            m_state.views = 0;
            m_state.visible_samples = 0;
            m_state.uv_hits = 0;
            m_state.background_pixels = 0;
            m_state.atlas_bins = 0;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.body_trace_hits = 0;
            m_state.background_trace_hits = 0;
            m_state.paint_world_success = 0;
            m_state.paint_uv_success = 0;
            m_state.capture_pixels_ready = false;
            m_state.uv_mapping_ready = false;
            m_state.official_paint_pipeline_ready = false;
            m_state.pipeline_property_candidates = 0;
            m_state.pipeline_function_candidates = 0;
            m_state.commit_sync_candidates = 0;
            m_state.render_target_candidates = 0;

            if (!m_state.capture_path_available)
            {
                m_state.last_failure = STR("capture_path_unavailable");
            }
            else if (!m_state.trace_path_available)
            {
                m_state.last_failure = STR("trace_path_unavailable");
            }
            else if (!m_state.uv_path_available)
            {
                m_state.last_failure = STR("uv_mapping_unavailable");
            }
            else
            {
                m_state.last_failure = STR("ready");
            }

            if (!quiet)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} dependency probe paint_components={} skeletal_mesh_components={} capture_functions={} trace_functions={} paint_functions={} uv_functions={} capture_pixels={} uv_mapping={} official_pipeline={} failure={}\n"),
                    ModTag,
                    m_state.runtime_paint_components,
                    m_state.skeletal_mesh_components,
                    m_state.scene_capture_functions,
                    m_state.trace_functions,
                    m_state.paint_functions,
                    m_state.uv_functions,
                    m_state.capture_pixels_ready ? 1 : 0,
                    m_state.uv_mapping_ready ? 1 : 0,
                    m_state.official_paint_pipeline_ready ? 1 : 0,
                    m_state.last_failure);
            }
        }

        auto run_paint_pipeline_audit(bool verbose) -> void
        {
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                component = find_runtime_paint_object_with_uv();
            }
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} paint_pipeline failed: RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }

            m_state.pipeline_property_candidates = 0;
            m_state.pipeline_function_candidates = 0;
            m_state.commit_sync_candidates = 0;
            m_state.render_target_candidates = 0;

            auto* klass = component->GetClassPrivate();
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} paint_pipeline component={} class={}\n"),
                ModTag,
                component->GetFullName(),
                klass ? klass->GetFullName() : STR("<null>"));

            if (!klass)
            {
                m_state.last_failure = STR("runtime_paint_class_unavailable");
                return;
            }

            int printed_properties = 0;
            for (auto* property : klass->ForEachProperty())
            {
                if (!property)
                {
                    continue;
                }
                const auto text = lower_copy(property->GetName() + STR(" ") + prop_type_name(property));
                if (!pipeline_token_match(text))
                {
                    continue;
                }

                ++m_state.pipeline_property_candidates;
                if (render_target_token_match(text))
                {
                    ++m_state.render_target_candidates;
                }

                if (verbose && printed_properties < 80)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} pipeline prop name={} type={} offset={} size={} flags={}\n"),
                        ModTag,
                        property->GetName(),
                        prop_type_name(property),
                        property->GetOffset_Internal(),
                        property->GetElementSize(),
                        static_cast<uint64_t>(property->GetPropertyFlags()));
                    if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                    {
                        dump_struct_layout(struct_type(struct_prop), property->GetName() + STR("."), 0);
                    }
                    ++printed_properties;
                }
            }

            int printed_functions = 0;
            for (auto* function : Unreal::TFieldRange<Unreal::UFunction>(klass, Unreal::EFieldIterationFlags::IncludeAll))
            {
                if (!function)
                {
                    continue;
                }
                const auto text = lower_copy(function->GetName() + STR(" ") + function->GetFullName());
                if (!pipeline_token_match(text))
                {
                    continue;
                }

                ++m_state.pipeline_function_candidates;
                const bool is_commit_candidate = commit_sync_token_match(text);
                if (is_commit_candidate)
                {
                    ++m_state.commit_sync_candidates;
                }

                if (verbose && printed_functions < 120)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} pipeline func name={} full={} parms_size={} num_parms={} commit_sync={}\n"),
                        ModTag,
                        function->GetName(),
                        function->GetFullName(),
                        function->GetParmsSize(),
                        static_cast<int>(function->GetNumParms()),
                        is_commit_candidate ? 1 : 0);
                    if (is_commit_candidate || contains_text(text, STR("texture")) || contains_text(text, STR("target")))
                    {
                        dump_function_signature(component, function->GetName().c_str(), true);
                    }
                    ++printed_functions;
                }
            }

            m_state.official_paint_pipeline_ready =
                m_state.render_target_candidates > 0 && m_state.commit_sync_candidates > 0;
            m_state.last_failure =
                m_state.official_paint_pipeline_ready ? STR("paint_pipeline_candidates_found") : STR("official_paint_pipeline_unresolved");
            m_state.paint_state_hash_after = hash_component_paint_state(component);

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} paint_pipeline summary properties={} functions={} render_targets={} commit_sync={} official_ready={} state_hash={}\n"),
                ModTag,
                m_state.pipeline_property_candidates,
                m_state.pipeline_function_candidates,
                m_state.render_target_candidates,
                m_state.commit_sync_candidates,
                m_state.official_paint_pipeline_ready ? 1 : 0,
                m_state.paint_state_hash_after);
        }

        auto run_game_camera_contract_probe() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* controller = find_player_controller_for_pawn(pawn);
            if (!controller)
            {
                controller = find_player_controller();
            }
            auto* controller_pawn = call_no_params_return_object(controller, STR("GetPawn"));
            auto* controller_camera = call_no_params_return_object(controller, STR("GetPlayerCameraManager"));
            auto* controller_view_target = call_no_params_return_object(controller, STR("GetViewTarget"));
            auto* camera = controller_camera ? controller_camera : find_player_camera_manager();
            auto* camera_view_target = call_no_params_return_object(camera, STR("GetViewTarget"));
            auto* camera_owner = call_no_params_return_object(camera, STR("GetOwner"));
            auto* pawn_controller = call_no_params_return_object(pawn, STR("GetController"));
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* component_owner = call_no_params_return_object(component, STR("GetOwner"));
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            auto* mesh_owner = call_no_params_return_object(mesh, STR("GetOwner"));
            auto* mesh_attach_parent = call_no_params_return_object(mesh, STR("GetAttachParent"));

            auto viewport = get_viewport_info(controller);
            auto camera_location = call_no_params_return_vector(camera, STR("GetCameraLocation"));
            auto camera_rotation = call_no_params_return_rotator(camera, STR("GetCameraRotation"));
            auto control_rotation = call_no_params_return_rotator(controller, STR("GetControlRotation"));
            auto controller_location = call_no_params_return_vector(controller, STR("K2_GetActorLocation"));
            auto controller_rotation = call_no_params_return_rotator(controller, STR("K2_GetActorRotation"));
            auto pawn_location = call_no_params_return_vector(pawn, STR("K2_GetActorLocation"));
            auto pawn_rotation = call_no_params_return_rotator(pawn, STR("K2_GetActorRotation"));
            auto mesh_location = call_no_params_return_vector(mesh, STR("K2_GetComponentLocation"));
            auto mesh_rotation = call_no_params_return_rotator(mesh, STR("K2_GetComponentRotation"));
            const auto camera_fov = camera_fov_from_manager(camera);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};

            m_state.current_world = object_name_or_empty(pawn ? pawn->GetWorld() : (controller ? controller->GetWorld() : nullptr));
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.queued_strokes = 0;
            m_state.success = (controller && pawn && camera && camera_location && camera_rotation) ? 1 : 0;
            m_state.failures = m_state.success ? 0 : 1;
            m_state.verified_visible_backend = false;
            m_state.verified_paint_function = STR("game_camera_contract_no_paint");
            m_state.verified_paint_channel = PaintChannelUnknown;
            m_state.last_failure = m_state.success ? STR("game_camera_contract_ready_no_paint")
                                                   : STR("game_camera_contract_prereq_unavailable");

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} game_camera_contract summary version={} no_paint=1 no_import=1 no_clear=1 no_commit=1 viewport={}x{} viewport_fallback={} controller={} controller_pawn={} controller_view_target={} controller_camera_manager={} pawn={} pawn_controller={} camera_manager={} camera_owner={} camera_view_target={} component={} component_owner={} mesh={} mesh_owner={} mesh_attach_parent={} camera_location=({}, {}, {}) camera_rotation=(pitch={}, yaw={}, roll={}) camera_fov={} camera_fov_fallback={} control_rotation=(pitch={}, yaw={}, roll={}) controller_location=({}, {}, {}) controller_rotation=(pitch={}, yaw={}, roll={}) pawn_location=({}, {}, {}) pawn_rotation=(pitch={}, yaw={}, roll={}) mesh_location=({}, {}, {}) mesh_rotation=(pitch={}, yaw={}, roll={}) deproject_frame={} deproject_eye=({}, {}, {}) deproject_forward=({}, {}, {}) deproject_hfov={} deproject_vfov={} camera_deproject_angle_delta={} last_failure={}\n"),
                ModTag,
                ModVersion,
                viewport.width,
                viewport.height,
                viewport.fallback ? 1 : 0,
                controller ? controller->GetFullName() : STR("<null>"),
                controller_pawn ? controller_pawn->GetFullName() : STR("<null>"),
                controller_view_target ? controller_view_target->GetFullName() : STR("<null>"),
                controller_camera ? controller_camera->GetFullName() : STR("<null>"),
                pawn ? pawn->GetFullName() : STR("<null>"),
                pawn_controller ? pawn_controller->GetFullName() : STR("<null>"),
                camera ? camera->GetFullName() : STR("<null>"),
                camera_owner ? camera_owner->GetFullName() : STR("<null>"),
                camera_view_target ? camera_view_target->GetFullName() : STR("<null>"),
                component ? component->GetFullName() : STR("<null>"),
                component_owner ? component_owner->GetFullName() : STR("<null>"),
                mesh ? mesh->GetFullName() : STR("<null>"),
                mesh_owner ? mesh_owner->GetFullName() : STR("<null>"),
                mesh_attach_parent ? mesh_attach_parent->GetFullName() : STR("<null>"),
                camera_location ? camera_location->X() : 0.0,
                camera_location ? camera_location->Y() : 0.0,
                camera_location ? camera_location->Z() : 0.0,
                camera_rotation ? camera_rotation->GetPitch() : 0.0,
                camera_rotation ? camera_rotation->GetYaw() : 0.0,
                camera_rotation ? camera_rotation->GetRoll() : 0.0,
                camera_fov.first,
                camera_fov.second ? 1 : 0,
                control_rotation ? control_rotation->GetPitch() : 0.0,
                control_rotation ? control_rotation->GetYaw() : 0.0,
                control_rotation ? control_rotation->GetRoll() : 0.0,
                controller_location ? controller_location->X() : 0.0,
                controller_location ? controller_location->Y() : 0.0,
                controller_location ? controller_location->Z() : 0.0,
                controller_rotation ? controller_rotation->GetPitch() : 0.0,
                controller_rotation ? controller_rotation->GetYaw() : 0.0,
                controller_rotation ? controller_rotation->GetRoll() : 0.0,
                pawn_location ? pawn_location->X() : 0.0,
                pawn_location ? pawn_location->Y() : 0.0,
                pawn_location ? pawn_location->Z() : 0.0,
                pawn_rotation ? pawn_rotation->GetPitch() : 0.0,
                pawn_rotation ? pawn_rotation->GetYaw() : 0.0,
                pawn_rotation ? pawn_rotation->GetRoll() : 0.0,
                mesh_location ? mesh_location->X() : 0.0,
                mesh_location ? mesh_location->Y() : 0.0,
                mesh_location ? mesh_location->Z() : 0.0,
                mesh_rotation ? mesh_rotation->GetPitch() : 0.0,
                mesh_rotation ? mesh_rotation->GetYaw() : 0.0,
                mesh_rotation ? mesh_rotation->GetRoll() : 0.0,
                frame ? 1 : 0,
                frame ? frame->eye.X() : 0.0,
                frame ? frame->eye.Y() : 0.0,
                frame ? frame->eye.Z() : 0.0,
                frame ? frame->forward.X() : 0.0,
                frame ? frame->forward.Y() : 0.0,
                frame ? frame->forward.Z() : 0.0,
                frame ? frame->deproject_hfov : 0.0,
                frame ? frame->deproject_vfov : 0.0,
                frame ? frame->camera_deproject_angle_delta : 0.0,
                m_state.last_failure);

            dump_owned_camera_contract_components(pawn);
            dump_camera_contract_properties(STR("controller"), controller);
            dump_camera_contract_functions(STR("controller"), controller, 32);
            dump_camera_contract_properties(STR("camera_manager"), camera);
            dump_camera_contract_functions(STR("camera_manager"), camera, 32);
            dump_camera_contract_properties(STR("camera_view_target"), camera_view_target);
            dump_camera_contract_functions(STR("camera_view_target"), camera_view_target, 32);
            dump_camera_contract_properties(STR("controller_view_target"), controller_view_target);
            dump_camera_contract_functions(STR("controller_view_target"), controller_view_target, 32);
            dump_camera_contract_properties(STR("pawn"), pawn);
            dump_camera_contract_functions(STR("pawn"), pawn, 32);
            dump_camera_contract_properties(STR("mesh"), mesh);
            dump_camera_contract_properties(STR("runtime_paintable"), component);
        }

        auto run_runtime_contract_probe() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} runtime_contract failed: RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }

            auto* owner = call_no_params_return_object(component, STR("GetOwner"));
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            m_state.current_world = object_name_or_empty(component->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} runtime_contract component={} class={} owner={} pawn={} world={} target_mesh={}\n"),
                ModTag,
                component->GetFullName(),
                component->GetClassPrivate() ? component->GetClassPrivate()->GetFullName() : STR("<null>"),
                owner ? owner->GetFullName() : STR("<null>"),
                pawn ? pawn->GetFullName() : STR("<null>"),
                component->GetWorld() ? component->GetWorld()->GetFullName() : STR("<null>"),
                mesh ? mesh->GetFullName() : STR("<null>"));

            const std::array<const CharType*, 6> paint_functions{
                STR("PaintAtUV"),
                STR("PaintAtUVWithBrush"),
                STR("PaintStrokeUV"),
                STR("PaintAtWorldPosition"),
                STR("PaintAtScreenPosition"),
                STR("HitTestAtScreenPosition")};
            for (const auto* function_name : paint_functions)
            {
                auto* function = component->GetFunctionByNameInChain(function_name);
                if (!function)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} runtime_contract function={} available=0\n"),
                        ModTag,
                        function_name);
                    continue;
                }
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} runtime_contract function={} available=1 parms_size={} num_parms={}\n"),
                    ModTag,
                    function_name,
                    function->GetParmsSize(),
                    static_cast<int>(function->GetNumParms()));
                for (int channel = 0; channel < 8; ++channel)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} runtime_contract channel_label function={} channel={} label={}\n"),
                        ModTag,
                        function_name,
                        channel,
                        channel_enum_label(function, channel));
                }
            }

            auto targets = collect_component_render_targets(component);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} runtime_contract render_targets count={}\n"),
                ModTag,
                static_cast<int>(targets.size()));
            for (int channel = 0; channel < 8; ++channel)
            {
                auto* target = get_render_target_for_channel(component, channel);
                const auto stats = read_render_target_probe_stats(component, target, 8);
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} runtime_contract rt channel={} target={} size={}x{} valid={} rt_avg=({},{},{}) rt_min=({},{},{}) rt_max=({},{},{}) uniformity={}\n"),
                    ModTag,
                    channel,
                    target ? target->GetFullName() : STR("<null>"),
                    stats.width,
                    stats.height,
                    stats.valid,
                    stats.avg_r,
                    stats.avg_g,
                    stats.avg_b,
                    stats.min_r,
                    stats.min_g,
                    stats.min_b,
                    stats.max_r,
                    stats.max_g,
                    stats.max_b,
                    stats.uniformity);
            }

            run_paint_pipeline_audit(true);
            dump_core_signatures(true);
            m_state.last_failure = STR("runtime_contract_dumped");
        }

        auto run_screen_contract_probe() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            if (!component || !pawn || !controller || !mesh)
            {
                m_state.last_failure = STR("screen_contract_prereq_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} screen_contract failed component={} pawn={} controller={} mesh={}\n"),
                    ModTag,
                    component ? component->GetFullName() : STR("<null>"),
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    mesh ? mesh->GetFullName() : STR("<null>"));
                return;
            }

            auto viewport = get_viewport_info(controller);
            m_state.current_world = object_name_or_empty(pawn->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.views = 0;
            m_state.visible_samples = 0;
            m_state.uv_hits = 0;
            m_state.background_pixels = 0;
            m_state.atlas_bins = 0;
            m_state.verified_visible_backend = false;
            m_state.verified_paint_function = STR("HitTestAtScreenPosition");
            m_state.verified_paint_channel = PaintChannelAlbedoMetallicRoughness;

            const auto pose_independent = read_bool_property_by_name(component, STR("bPoseIndependentSkeletalPainting")).value_or(false);
            const auto min_screen_distance = read_number_property_by_name(component, STR("MinScreenPaintDistance")).value_or(-1.0);
            const auto realtime_sync = read_bool_property_by_name(component, STR("bRealtimeNetworkSync")).value_or(false);
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} screen_contract component={} pawn={} controller={} mesh={} viewport={}x{} viewport_fallback={} pose_independent={} min_screen_paint_distance={} realtime_sync={} grid={}x{} no_paint=1 no_clear=1 no_commit=1\n"),
                ModTag,
                component->GetFullName(),
                pawn->GetFullName(),
                controller->GetFullName(),
                mesh->GetFullName(),
                viewport.width,
                viewport.height,
                viewport.fallback ? 1 : 0,
                pose_independent ? 1 : 0,
                min_screen_distance,
                realtime_sync ? 1 : 0,
                ScreenProjectionGridX,
                ScreenProjectionGridY);

            std::vector<std::optional<Color>> dummy_colors(static_cast<size_t>(ScreenProjectionGridX * ScreenProjectionGridY),
                                                           Color{0.34, 0.36, 0.32, 0.94, 0.0});
            ScreenHitCollectionStats pixel_stats{};
            auto pixel_samples = collect_screen_hit_samples(component,
                                                            pawn,
                                                            mesh,
                                                            controller,
                                                            viewport,
                                                            dummy_colors,
                                                            ScreenProjectionGridX,
                                                            ScreenProjectionGridY,
                                                            ScreenProjectionGridX,
                                                            ScreenProjectionGridY,
                                                            false,
                                                            m_state,
                                                            pixel_stats);
            log_screen_hit_stats(STR("screen_contract"), STR("viewport_pixels"), pixel_stats);

            ScreenHitCollectionStats normalized_stats{};
            auto normalized_samples = collect_screen_hit_samples(component,
                                                                 pawn,
                                                                 mesh,
                                                                 controller,
                                                                 viewport,
                                                                 dummy_colors,
                                                                 ScreenProjectionGridX,
                                                                 ScreenProjectionGridY,
                                                                 ScreenProjectionGridX,
                                                                 ScreenProjectionGridY,
                                                                 true,
                                                                 m_state,
                                                                 normalized_stats);
            log_screen_hit_stats(STR("screen_contract"), STR("normalized_0_1"), normalized_stats);

            const auto& chosen_stats =
                normalized_stats.hit_uv_count > pixel_stats.hit_uv_count ? normalized_stats : pixel_stats;
            const auto chosen_samples =
                normalized_stats.hit_uv_count > pixel_stats.hit_uv_count ? normalized_samples.size() : pixel_samples.size();
            m_state.success = chosen_stats.hit_uv_count;
            m_state.failures = chosen_stats.failures;
            m_state.visible_samples = chosen_stats.hit_success;
            m_state.uv_hits = chosen_stats.hit_uv_count;
            m_state.body_trace_hits = chosen_stats.hit_success;
            m_state.background_trace_hits = chosen_stats.floor_hits;
            m_state.atlas_bins = static_cast<int>(chosen_samples);
            m_state.uv_mapping_ready = chosen_stats.hit_uv_count >= MinScreenHitUvSamples;
            m_state.last_failure = m_state.uv_mapping_ready ? STR("screen_contract_ready")
                                                            : STR("screen_contract_hit_uv_unavailable");
        }

        auto run_capture_contract_probe() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto frame = make_projection_frame(pawn, 0.0, 0.0);
            if (!pawn || !controller || !frame)
            {
                m_state.last_failure = STR("capture_contract_prereq_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} capture_contract failed pawn={} controller={} frame={}\n"),
                    ModTag,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    frame ? 1 : 0);
                return;
            }

            auto viewport = get_viewport_info(controller);
            const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
            CaptureGridDiagnostics capture_diag{};
            auto colors = capture_background_grid(pawn,
                                                  frame->eye,
                                                  look_at,
                                                  ScreenProjectionGridX,
                                                  ScreenProjectionGridY,
                                                  m_state,
                                                  frame->fov_degrees,
                                                  &capture_diag);
            const auto summary = summarize_capture_colors(colors);
            m_state.current_world = object_name_or_empty(pawn->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.views = 1;
            m_state.background_pixels = summary.pixels;
            m_state.capture_pixels_ready = summary.pixels > 0 && !summary.uniform && !summary.clear_suspect;
            m_state.success = m_state.capture_pixels_ready ? 1 : 0;
            m_state.failures = m_state.capture_pixels_ready ? 0 : 1;
            m_state.last_failure = m_state.capture_pixels_ready ? STR("capture_contract_ready")
                                                                : STR("capture_contract_uniform_or_empty");

            const auto viewport_aspect = static_cast<double>(viewport.width) / static_cast<double>(std::max(1, viewport.height));
            const auto capture_aspect =
                static_cast<double>(ScreenProjectionGridX) / static_cast<double>(std::max(1, ScreenProjectionGridY));
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} capture_contract component={} pawn={} controller={} grid={}x{} pixels={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) near_uniform={} capture_uniform={} clear_suspect={} scene_capture_class={} render_target={} rt_format={} capture_actor={} capture_component={} texture_target_written={} capture_source={} capture_source_written={} capture_every_frame_written={} capture_on_movement_written={} persist_rendering_state_written={} capture_scene_called={} read_pixels={} missing_pixels={} raw_attempts={} raw_success={} pixel_attempts={} pixel_success={} first_read_function={} first_read_struct={} first_read_struct_size={} camera_fov={} fov_fallback={} viewport={}x{} viewport_fallback={} viewport_aspect={} capture_aspect={} no_paint=1 no_clear=1 no_commit=1\n"),
                ModTag,
                component ? component->GetFullName() : STR("<null>"),
                pawn->GetFullName(),
                controller->GetFullName(),
                ScreenProjectionGridX,
                ScreenProjectionGridY,
                summary.pixels,
                summary.min_r,
                summary.min_g,
                summary.min_b,
                summary.avg_r,
                summary.avg_g,
                summary.avg_b,
                summary.max_r,
                summary.max_g,
                summary.max_b,
                summary.near_uniform_samples,
                summary.uniform ? 1 : 0,
                summary.clear_suspect ? 1 : 0,
                capture_diag.scene_capture_class ? 1 : 0,
                capture_diag.render_target ? 1 : 0,
                capture_diag.requested_render_target_format,
                capture_diag.capture_actor ? 1 : 0,
                capture_diag.capture_component ? 1 : 0,
                capture_diag.texture_target_written ? 1 : 0,
                capture_diag.requested_capture_source,
                capture_diag.capture_source_written ? 1 : 0,
                capture_diag.capture_every_frame_written ? 1 : 0,
                capture_diag.capture_on_movement_written ? 1 : 0,
                capture_diag.persist_rendering_state_written ? 1 : 0,
                capture_diag.capture_scene_called ? 1 : 0,
                capture_diag.read_pixels,
                capture_diag.missing_pixels,
                capture_diag.read.raw_attempts,
                capture_diag.read.raw_success,
                capture_diag.read.pixel_attempts,
                capture_diag.read.pixel_success,
                capture_diag.read.first_function.empty() ? STR("<none>") : capture_diag.read.first_function,
                capture_diag.read.first_struct.empty() ? STR("<none>") : capture_diag.read.first_struct,
                capture_diag.read.first_struct_size,
                frame->fov_degrees,
                frame->fov_fallback ? 1 : 0,
                viewport.width,
                viewport.height,
                viewport.fallback ? 1 : 0,
                viewport_aspect,
                capture_aspect);
        }

        auto run_readback_contract_probe() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto frame = make_projection_frame(pawn, 0.0, 0.0);
            if (!pawn || !controller || !frame)
            {
                m_state.last_failure = STR("readback_contract_prereq_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} readback_contract failed pawn={} controller={} frame={}\n"),
                    ModTag,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    frame ? 1 : 0);
                return;
            }

            auto viewport = get_viewport_info(controller);
            const auto grid_width = 48;
            const auto grid_height = std::max(
                20,
                static_cast<int>(std::round(static_cast<double>(grid_width) *
                                            static_cast<double>(std::max(1, viewport.height)) /
                                            static_cast<double>(std::max(1, viewport.width)))));
            const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
            CaptureGridDiagnostics capture_diag{};
            const auto read_start = SteadyClock::now();
            auto colors = capture_background_grid(pawn,
                                                  frame->eye,
                                                  look_at,
                                                  grid_width,
                                                  grid_height,
                                                  m_state,
                                                  frame->fov_degrees,
                                                  &capture_diag);
            const auto readback_ms = elapsed_ms_since(read_start);
            const auto summary = summarize_capture_colors(colors);
            const auto raw_pixel_available =
                find_function(STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetRawPixel")) != nullptr;
            const auto pixel_available =
                find_function(STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetPixel")) != nullptr;

            m_state.current_world = object_name_or_empty(pawn->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.views = 1;
            m_state.background_pixels = summary.pixels;
            m_state.capture_pixels_ready = summary.pixels > 0 && !summary.uniform && !summary.clear_suspect;
            m_state.success = m_state.capture_pixels_ready ? 1 : 0;
            m_state.failures = m_state.capture_pixels_ready ? 0 : 1;
            m_state.last_failure = m_state.capture_pixels_ready ? STR("readback_contract_pixel_api_ready")
                                                                : STR("readback_contract_pixel_api_failed");

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} readback_contract readback_backend=kismet_pixel_api ok={} pixels={} grid={}x{} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) near_uniform={} capture_uniform={} clear_suspect={} raw_pixel_available={} pixel_available={} scene_capture_class={} render_target={} capture_actor={} capture_component={} texture_target_written={} capture_source_written={} capture_scene_called={} read_pixels={} missing_pixels={} raw_attempts={} raw_success={} pixel_attempts={} pixel_success={} first_read_function={} first_read_struct={} first_read_struct_size={} t_readback_ms={} failure={} component={} pawn={} controller={} no_paint=1 no_clear=1 no_commit=1\n"),
                ModTag,
                m_state.capture_pixels_ready ? 1 : 0,
                summary.pixels,
                grid_width,
                grid_height,
                summary.min_r,
                summary.min_g,
                summary.min_b,
                summary.avg_r,
                summary.avg_g,
                summary.avg_b,
                summary.max_r,
                summary.max_g,
                summary.max_b,
                summary.near_uniform_samples,
                summary.uniform ? 1 : 0,
                summary.clear_suspect ? 1 : 0,
                raw_pixel_available ? 1 : 0,
                pixel_available ? 1 : 0,
                capture_diag.scene_capture_class ? 1 : 0,
                capture_diag.render_target ? 1 : 0,
                capture_diag.capture_actor ? 1 : 0,
                capture_diag.capture_component ? 1 : 0,
                capture_diag.texture_target_written ? 1 : 0,
                capture_diag.capture_source_written ? 1 : 0,
                capture_diag.capture_scene_called ? 1 : 0,
                capture_diag.read_pixels,
                capture_diag.missing_pixels,
                capture_diag.read.raw_attempts,
                capture_diag.read.raw_success,
                capture_diag.read.pixel_attempts,
                capture_diag.read.pixel_success,
                capture_diag.read.first_function.empty() ? STR("<none>") : capture_diag.read.first_function,
                capture_diag.read.first_struct.empty() ? STR("<none>") : capture_diag.read.first_struct,
                capture_diag.read.first_struct_size,
                readback_ms,
                m_state.capture_pixels_ready ? STR("<none>") : STR("pixel_capture_empty_or_uniform"),
                component ? component->GetFullName() : STR("<null>"),
                pawn->GetFullName(),
                controller->GetFullName());
        }

        auto run_export_contract_probe() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} export_contract failed: RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }

            m_state.current_world = object_name_or_empty(component->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.commit_calls = 0;
            m_state.paint_state_hash_before = hash_component_paint_state(component);
            m_state.paint_state_hash_after = m_state.paint_state_hash_before;

            auto get_num = [&](const CharType* name) -> double {
                if (auto value = read_number_property_by_name(component, name))
                {
                    return *value;
                }
                return -1.0;
            };
            auto get_bool = [&](const CharType* name) -> int {
                if (auto value = read_bool_property_by_name(component, name))
                {
                    return *value ? 1 : 0;
                }
                return -1;
            };

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} export_contract read_only=1 no_paint=1 no_clear=1 no_commit=1 component={} hash={} initialized={} stroking={} recorded_strokes={} realtime_sync={} auto_record={} auto_flush={} max_batches_per_tick={} max_replicated_per_tick={} async_prepare={}\n"),
                ModTag,
                component->GetFullName(),
                m_state.paint_state_hash_before,
                call_no_params_return_bool(component, STR("IsInitialized")) ? 1 : 0,
                call_no_params_return_bool(component, STR("IsStroking")) ? 1 : 0,
                call_no_params_return_number(component, STR("GetRecordedStrokeCount")).value_or(-1.0),
                get_bool(STR("bRealtimeNetworkSync")),
                get_bool(STR("bAutoRecordStrokes")),
                get_bool(STR("bAutoFlushStrokes")),
                get_num(STR("MaxNetworkBatchesPerTick")),
                get_num(STR("MaxReplicatedPaintStrokesPerTick")),
                get_bool(STR("bAsyncPrepareReplicatedPaint")));

            if (auto* options_prop = Unreal::CastField<Unreal::FStructProperty>(
                    component->GetClassPrivate()->FindProperty(Unreal::FName(STR("TextureOptions")))))
            {
                auto* base = prop_value_ptr(reinterpret_cast<uint8_t*>(component), options_prop);
                auto read_option = [&](const CharType* name) -> double {
                    if (auto* field = find_struct_property(struct_type(options_prop), name))
                    {
                        if (auto value = read_number(field, base))
                        {
                            return *value;
                        }
                    }
                    return -1.0;
                };
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} export_contract texture_options albedo_res={} metallic_res={} roughness_res={} height_res={} seam_bleed={} gutter_expand={} metallic_clear={} roughness_clear={} height_clear={} subdivision_pixel={} max_subdivision={} max_brush_tris={}\n"),
                    ModTag,
                    read_option(STR("AlbedoResolution")),
                    read_option(STR("MetallicResolution")),
                    read_option(STR("RoughnessResolution")),
                    read_option(STR("HeightResolution")),
                    read_option(STR("SeamBleedPixels")),
                    read_option(STR("GutterExpandPixels")),
                    read_option(STR("MetallicClearValue")),
                    read_option(STR("RoughnessClearValue")),
                    read_option(STR("HeightClearValue")),
                    read_option(STR("SubdivisionPixelSize")),
                    read_option(STR("MaxSubdivisionLevel")),
                    read_option(STR("MaxGeneratedBrushTriangles")));
            }

            if (auto* brush_prop = Unreal::CastField<Unreal::FStructProperty>(
                    component->GetClassPrivate()->FindProperty(Unreal::FName(STR("CurrentBrushSettings")))))
            {
                auto* base = prop_value_ptr(reinterpret_cast<uint8_t*>(component), brush_prop);
                auto read_brush = [&](const CharType* name) -> double {
                    if (auto* field = find_struct_property(struct_type(brush_prop), name))
                    {
                        if (auto value = read_number(field, base))
                        {
                            return *value;
                        }
                    }
                    return -1.0;
                };
                auto* brush_texture = read_object_from_struct(struct_type(brush_prop), base, STR("BrushTexture"));
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} export_contract current_brush radius={} hardness={} opacity={} spacing={} falloff={} blend_mode={} rotation={} texture={}\n"),
                    ModTag,
                    read_brush(STR("Radius")),
                    read_brush(STR("Hardness")),
                    read_brush(STR("Opacity")),
                    read_brush(STR("Spacing")),
                    read_brush(STR("Falloff")),
                    read_brush(STR("BlendMode")),
                    read_brush(STR("Rotation")),
                    brush_texture ? brush_texture->GetFullName() : STR("<null>"));
            }

            dump_function_signature(component, STR("GetRenderTarget"), true);
            dump_function_signature(component, STR("ExportChannelToBytes"), true);
            dump_function_signature(component, STR("ImportChannelFromBytes"), true);
            dump_function_signature(component, STR("GetRecordedStrokeCount"), true);

            auto* export_function = component->GetFunctionByNameInChain(STR("ExportChannelToBytes"));
            if (!export_function)
            {
                m_state.last_failure = STR("export_channel_to_bytes_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} export_contract failed: ExportChannelToBytes unavailable\n"), ModTag);
                return;
            }

            for (int channel = 0; channel < 6; ++channel)
            {
                auto* target = get_render_target_for_channel(component, channel);
                const auto [width, height] = render_target_dimensions(target);
                const auto label = channel_enum_label(export_function, channel);
                std::vector<uint8_t> params(static_cast<size_t>(export_function->GetParmsSize()), 0);
                std::vector<Unreal::FArrayProperty*> array_params{};
                for (auto* property : export_function->ForEachProperty())
                {
                    if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                        property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
                    {
                        continue;
                    }
                    if (auto* array_prop = Unreal::CastField<Unreal::FArrayProperty>(property))
                    {
                        new (prop_value_ptr(params.data(), array_prop)) Unreal::FScriptArray{};
                        array_params.push_back(array_prop);
                        continue;
                    }
                    const auto name = lower_copy(property->GetName());
                    if (contains_text(name, STR("channel")))
                    {
                        write_number(property, params.data(), static_cast<double>(channel));
                    }
                }

                component->ProcessEvent(export_function, params.data());
                const auto return_ok = read_return_bool(export_function, params.data());
                bool saw_array = false;
                for (auto* array_prop : array_params)
                {
                    const auto stats = read_array_param_stats(array_prop, params.data());
                    saw_array = true;
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} export_contract channel={} label={} ok={} target={} size={}x{} array_param={} valid={} num={} max={} elem_size={} inner={} hash={} first=({},{},{},{}) skipped_import=1\n"),
                        ModTag,
                        channel,
                        label,
                        return_ok ? 1 : 0,
                        target ? target->GetFullName() : STR("<null>"),
                        width,
                        height,
                        array_prop->GetName(),
                        stats.valid ? 1 : 0,
                        stats.num,
                        stats.max,
                        stats.element_size,
                        stats.inner_type.empty() ? STR("<none>") : stats.inner_type,
                        stats.hash,
                        stats.first0,
                        stats.first1,
                        stats.first2,
                        stats.first3);
                    cleanup_array_param(array_prop, params.data());
                }
                if (!saw_array)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("{} export_contract channel={} label={} ok={} target={} size={}x{} no_array_param=1\n"),
                        ModTag,
                        channel,
                        label,
                        return_ok ? 1 : 0,
                        target ? target->GetFullName() : STR("<null>"),
                        width,
                        height);
                }
                if (return_ok && saw_array)
                {
                    ++m_state.success;
                }
                else
                {
                    ++m_state.failures;
                }
            }

            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.last_failure = STR("export_contract_read_only_complete");
        }

        auto prepare_import_probe_component(const CharType* command_name) -> Unreal::UObject*
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} {} failed: RuntimePaintableComponent unavailable\n"), ModTag, command_name);
                return nullptr;
            }
            m_state.current_world = object_name_or_empty(component->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.commit_calls = 0;
            m_state.paint_uv_success = 0;
            m_state.paint_world_success = 0;
            m_state.paint_state_hash_before = hash_component_paint_state(component);
            m_state.paint_state_hash_after = m_state.paint_state_hash_before;
            return component;
        }

        auto run_import_roundtrip_probe() -> void
        {
            auto* component = prepare_import_probe_component(STR("import_roundtrip_probe"));
            if (!component)
            {
                return;
            }

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} import_roundtrip_probe start no_paint_api=1 no_clear=1 no_commit=1 component={} hash_before={}\n"),
                ModTag,
                component->GetFullName(),
                m_state.paint_state_hash_before);

            const std::array<int, 3> channels{0, 1, 2};
            for (const auto channel : channels)
            {
                const auto before = export_channel_bytes(component, channel);
                StringType import_failure{};
                const auto import_ok = before.ok && import_channel_bytes(component, channel, before.bytes, import_failure);
                const auto after = export_channel_bytes(component, channel);
                const auto same_hash = before.ok && after.ok && before.hash == after.hash && before.bytes.size() == after.bytes.size();
                if (import_ok && same_hash)
                {
                    ++m_state.success;
                }
                else
                {
                    ++m_state.failures;
                }
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} import_roundtrip_probe channel={} label={} export_ok={} import_ok={} after_export_ok={} bytes={} hash_before={} hash_after={} same_hash={} import_failure={} export_failure={} after_failure={}\n"),
                    ModTag,
                    channel,
                    before.label.empty() ? after.label : before.label,
                    before.ok ? 1 : 0,
                    import_ok ? 1 : 0,
                    after.ok ? 1 : 0,
                    static_cast<int>(before.bytes.size()),
                    before.hash,
                    after.hash,
                    same_hash ? 1 : 0,
                    import_failure.empty() ? STR("<none>") : import_failure,
                    before.failure.empty() ? STR("<none>") : before.failure,
                    after.failure.empty() ? STR("<none>") : after.failure);
            }

            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.last_failure = m_state.failures == 0 ? STR("import_roundtrip_probe_complete")
                                                         : STR("import_roundtrip_probe_failed");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} import_roundtrip_probe complete success={} failures={} component_hash_before={} component_hash_after={} changed={}\n"),
                ModTag,
                m_state.success,
                m_state.failures,
                m_state.paint_state_hash_before,
                m_state.paint_state_hash_after,
                m_state.paint_state_hash_before != m_state.paint_state_hash_after ? 1 : 0);
        }

        auto run_import_patch_probe() -> void
        {
            auto* component = prepare_import_probe_component(STR("import_patch_probe"));
            if (!component)
            {
                return;
            }

            constexpr int channel = 0;
            auto before = export_channel_bytes(component, channel);
            if (!before.ok || before.width <= 0 || before.height <= 0 || before.bytes.size() < 4)
            {
                m_state.failures = 1;
                m_state.last_failure = before.failure.empty() ? STR("import_patch_export_failed") : before.failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} import_patch_probe failed: export channel={} ok={} failure={}\n"),
                    ModTag,
                    channel,
                    before.ok ? 1 : 0,
                    m_state.last_failure);
                return;
            }

            auto patched = before.bytes;
            const int patch_size = std::max(24, std::min(before.width, before.height) / 12);
            const int start_x = std::max(0, before.width / 2 - patch_size / 2);
            const int start_y = std::max(0, before.height / 2 - patch_size / 2);
            int changed_bytes = 0;
            const auto palette = fallback_camo_palette();
            for (int y = 0; y < patch_size && start_y + y < before.height; ++y)
            {
                for (int x = 0; x < patch_size && start_x + x < before.width; ++x)
                {
                    const auto pixel = static_cast<size_t>((start_y + y) * before.width + (start_x + x)) * 4;
                    if (pixel + 2 >= patched.size())
                    {
                        continue;
                    }
                    const auto pick = static_cast<size_t>(hash_u32(static_cast<uint32_t>((x / 10) * 193 + (y / 10) * 389 + x * 7 + y * 13)) %
                                                          static_cast<uint32_t>(palette.size()));
                    auto color = palette[pick];
                    const auto grain = noise01(start_x + x, start_y + y, 0x6e21U) - 0.5;
                    color.r = clamp(color.r + grain * 0.04, 0.02, 0.92);
                    color.g = clamp(color.g + grain * 0.04, 0.02, 0.92);
                    color.b = clamp(color.b + grain * 0.04, 0.02, 0.92);
                    const std::array<uint8_t, 3> rgb{
                        static_cast<uint8_t>(clamp(color.r, 0.0, 1.0) * 255.0),
                        static_cast<uint8_t>(clamp(color.g, 0.0, 1.0) * 255.0),
                        static_cast<uint8_t>(clamp(color.b, 0.0, 1.0) * 255.0)};
                    for (int c = 0; c < 3; ++c)
                    {
                        if (patched[pixel + static_cast<size_t>(c)] != rgb[static_cast<size_t>(c)])
                        {
                            patched[pixel + static_cast<size_t>(c)] = rgb[static_cast<size_t>(c)];
                            ++changed_bytes;
                        }
                    }
                }
            }

            const auto patch_hash = hash_bytes(patched);
            StringType import_failure{};
            const auto import_ok = import_channel_bytes(component, channel, patched, import_failure);
            const auto after = export_channel_bytes(component, channel);
            const auto exported_patch_observed = after.ok && after.hash == patch_hash;
            m_state.success = import_ok && exported_patch_observed ? 1 : 0;
            m_state.failures = m_state.success ? 0 : 1;
            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.last_failure = m_state.success ? STR("import_patch_probe_applied")
                                                   : STR("import_patch_probe_unverified");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} import_patch_probe channel={} label={} no_paint_api=1 no_clear=1 no_commit=1 import_ok={} observed={} target={} size={}x{} patch_origin=({}, {}) patch_size={} changed_bytes={} hash_before={} patch_hash={} hash_after={} component_hash_before={} component_hash_after={} import_failure={} after_failure={}\n"),
                ModTag,
                channel,
                before.label,
                import_ok ? 1 : 0,
                exported_patch_observed ? 1 : 0,
                before.target_name,
                before.width,
                before.height,
                start_x,
                start_y,
                patch_size,
                changed_bytes,
                before.hash,
                patch_hash,
                after.hash,
                m_state.paint_state_hash_before,
                m_state.paint_state_hash_after,
                import_failure.empty() ? STR("<none>") : import_failure,
                after.failure.empty() ? STR("<none>") : after.failure);
        }

        auto run_probe_paint_modes() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} paint_mode_probe failed: RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }

            {
                auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
                constexpr int channel = PaintChannelAlbedoMetallicRoughness;
                const std::array<const CharType*, 6> paint_functions{
                    STR("PaintAtUV"),
                    STR("PaintAtUVWithBrush"),
                    STR("PaintStrokeUV"),
                    STR("PaintAtWorldPosition"),
                    STR("PaintAtScreenPosition"),
                    STR("HitTestAtScreenPosition")};
                const std::array<int, 3> apply_modes{0, 1, 2};
                auto* target = select_probe_render_target(component, channel);
                const auto baseline_hash = hash_component_paint_state(component);
                const auto baseline_stats = read_render_target_probe_stats(component, target, 12);

                m_state.current_world = object_name_or_empty(pawn->GetWorld());
                m_state.current_pawn = object_name_or_empty(pawn);
                m_state.current_component = object_name_or_empty(component);
                m_state.queued_strokes = 0;
                m_state.success = 0;
                m_state.failures = 0;
                m_state.paint_uv_success = 0;
                m_state.paint_world_success = 0;
                m_state.commit_calls = 0;
                m_state.paint_state_hash_before = baseline_hash;
                m_state.paint_state_hash_after = baseline_hash;

                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} paint_mode_probe read_only=1 no_paint=1 no_clear=1 no_commit=1 reason=unsafe_runtime_paint_contract component={} mesh={} channel={} target={} hash={} rt_valid={} rt_avg=({},{},{}) rt_min=({},{},{}) rt_max=({},{},{}) uniformity={}\n"),
                    ModTag,
                    component->GetFullName(),
                    mesh ? mesh->GetFullName() : STR("<null>"),
                    channel,
                    target ? target->GetFullName() : STR("<null>"),
                    baseline_hash,
                    baseline_stats.valid,
                    baseline_stats.avg_r,
                    baseline_stats.avg_g,
                    baseline_stats.avg_b,
                    baseline_stats.min_r,
                    baseline_stats.min_g,
                    baseline_stats.min_b,
                    baseline_stats.max_r,
                    baseline_stats.max_g,
                    baseline_stats.max_b,
                    baseline_stats.uniformity);

                for (const auto* function_name : paint_functions)
                {
                    auto* function = component->GetFunctionByNameInChain(function_name);
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} paint_mode_probe planned_function={} available={} parms_size={} num_parms={} channel={} label={} skipped_call=1\n"),
                        ModTag,
                        function_name,
                        function ? 1 : 0,
                        function ? function->GetParmsSize() : 0,
                        function ? static_cast<int>(function->GetNumParms()) : 0,
                        channel,
                        function ? channel_enum_label(function, channel) : STR("<missing>"));
                    if (function)
                    {
                        dump_function_signature(component, function_name, true);
                    }
                    for (const auto apply_mode : apply_modes)
                    {
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("{} paint_mode_probe planned_call function={} apply_mode={} channel={} skipped_call=1\n"),
                            ModTag,
                            function_name,
                            apply_mode,
                            channel);
                    }
                }

                m_state.last_failure = STR("paint_mode_probe_read_only_no_paint");
                return;
            }

            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            auto samples = make_visibility_samples(pawn, m_state);
            double u = 0.5;
            double v = 0.5;
            std::optional<Unreal::FVector> world_position{};
            if (!samples.empty())
            {
                u = clamp(samples.front().u, 0.05, 0.95);
                v = clamp(samples.front().v, 0.05, 0.95);
                world_position = samples.front().world_position;
            }
            else if (auto location = call_no_params_return_vector(pawn, STR("K2_GetActorLocation")))
            {
                world_position = *location;
            }

            constexpr int channel = PaintChannelAlbedoMetallicRoughness;
            const std::array<const CharType*, 6> paint_functions{
                STR("PaintAtUV"),
                STR("PaintAtUVWithBrush"),
                STR("PaintStrokeUV"),
                STR("PaintAtWorldPosition"),
                STR("PaintAtScreenPosition"),
                STR("HitTestAtScreenPosition")};
            const std::array<int, 3> apply_modes{0, 1, 2};
            const Color probe_color{0.12, 0.46, 0.68, 0.93, 0.0};

            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.paint_uv_success = 0;
            m_state.paint_world_success = 0;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} paint_mode_probe start component={} mesh={} sample_uv=({}, {}) sample_world={} channel={}\n"),
                ModTag,
                component->GetFullName(),
                mesh ? mesh->GetFullName() : STR("<null>"),
                u,
                v,
                world_position ? STR("1") : STR("0"),
                channel);

            for (const auto* function_name : paint_functions)
            {
                for (const auto apply_mode : apply_modes)
                {
                    clear_component(component);
                    const auto hash_before = hash_component_paint_state(component);
                    auto* target = select_probe_render_target(component, channel);
                    const auto before_stats = read_render_target_probe_stats(component, target, 12);
                    PaintParamWriteStats param_stats{};
                    auto* function = component->GetFunctionByNameInChain(function_name);
                    param_stats.channel_label = channel_enum_label(function, channel);
                    const auto wants_world = contains_text(lower_copy(StringType(function_name)), STR("world"));
                    const auto ok = paint_probe_call(component,
                                                     function_name,
                                                     channel,
                                                     apply_mode,
                                                     u,
                                                     v,
                                                     wants_world ? world_position : std::optional<Unreal::FVector>{},
                                                     mesh,
                                                     probe_color,
                                                     0.035,
                                                     1.0,
                                                     0.92,
                                                     &param_stats);
                    const auto hash_after = hash_component_paint_state(component);
                    const auto after_stats = read_render_target_probe_stats(component, target, 12);
                    const auto changed_pixels = count_changed_probe_pixels(before_stats, after_stats);
                    const auto changed_ratio = after_stats.valid > 0
                                                   ? static_cast<double>(changed_pixels) / static_cast<double>(after_stats.valid)
                                                   : 0.0;
                    if (ok)
                    {
                        ++m_state.success;
                        if (wants_world)
                        {
                            ++m_state.paint_world_success;
                        }
                        else
                        {
                            ++m_state.paint_uv_success;
                        }
                    }
                    else
                    {
                        ++m_state.failures;
                    }
                    ++m_state.queued_strokes;
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} paint_mode_probe function={} apply_mode={} channel={} label={} ok={} changed_pixels={} changed_ratio={} rt_valid={} rt_avg=({},{},{}) rt_min=({},{},{}) rt_max=({},{},{}) uniformity={} hash_before={} hash_after={} hash_changed={} target={} color_param={} uv_param={} brush_param={} wrote_color={} wrote_uv={} wrote_world={} wrote_brush={} wrote_channel={} failure={}\n"),
                        ModTag,
                        function_name,
                        apply_mode,
                        channel,
                        param_stats.channel_label.empty() ? STR("<none>") : param_stats.channel_label,
                        ok ? 1 : 0,
                        changed_pixels,
                        changed_ratio,
                        after_stats.valid,
                        after_stats.avg_r,
                        after_stats.avg_g,
                        after_stats.avg_b,
                        after_stats.min_r,
                        after_stats.min_g,
                        after_stats.min_b,
                        after_stats.max_r,
                        after_stats.max_g,
                        after_stats.max_b,
                        after_stats.uniformity,
                        hash_before,
                        hash_after,
                        hash_before != hash_after ? 1 : 0,
                        after_stats.target_name,
                        param_stats.color_param.empty() ? STR("<none>") : param_stats.color_param,
                        param_stats.uv_param.empty() ? STR("<none>") : param_stats.uv_param,
                        param_stats.brush_param.empty() ? STR("<none>") : param_stats.brush_param,
                        param_stats.wrote_color ? 1 : 0,
                        param_stats.wrote_uv ? 1 : 0,
                        param_stats.wrote_world_position ? 1 : 0,
                        param_stats.wrote_brush ? 1 : 0,
                        param_stats.wrote_channel ? 1 : 0,
                        param_stats.failure.empty() ? STR("<none>") : param_stats.failure);
                }
            }
            clear_component(component);
            m_state.commit_calls = call_commit_sync_candidates(component, false);
            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.last_failure = STR("paint_mode_probe_complete");
        }

        auto run_probe_commit_modes() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component || !component->GetClassPrivate())
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} commit_mode_probe failed: RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }

            {
                std::vector<Unreal::UFunction*> candidates{};
                for (auto* function : Unreal::TFieldRange<Unreal::UFunction>(component->GetClassPrivate(), Unreal::EFieldIterationFlags::IncludeAll))
                {
                    if (!function)
                    {
                        continue;
                    }
                    const auto text = lower_copy(function->GetName() + STR(" ") + function->GetFullName());
                    if (!commit_sync_token_match(text) || non_return_param_count(function) != 0)
                    {
                        continue;
                    }
                    if (contains_any_text(text, {STR("clear"), STR("reset"), STR("remove"), STR("delete"), STR("destroy"), STR("load")}))
                    {
                        continue;
                    }
                    candidates.push_back(function);
                    if (candidates.size() >= 24)
                    {
                        break;
                    }
                }

                const auto baseline_hash = hash_component_paint_state(component);
                m_state.current_world = object_name_or_empty(pawn->GetWorld());
                m_state.current_pawn = object_name_or_empty(pawn);
                m_state.current_component = object_name_or_empty(component);
                m_state.queued_strokes = 0;
                m_state.success = 0;
                m_state.failures = 0;
                m_state.commit_calls = 0;
                m_state.paint_state_hash_before = baseline_hash;
                m_state.paint_state_hash_after = baseline_hash;

                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} commit_mode_probe read_only=1 no_paint=1 no_clear=1 no_commit=1 reason=destructive_probe_disabled candidates={} component={} hash={}\n"),
                    ModTag,
                    static_cast<int>(candidates.size()),
                    component->GetFullName(),
                    baseline_hash);

                for (auto* candidate : candidates)
                {
                    const auto text = lower_copy(candidate->GetName() + STR(" ") + candidate->GetFullName());
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} commit_mode_probe candidate={} parms_size={} non_return_params={} texture_sync={} stroke_batch={} flush={} skipped_call=1\n"),
                        ModTag,
                        candidate->GetFullName(),
                        candidate->GetParmsSize(),
                        non_return_param_count(candidate),
                        contains_any_text(text, {STR("texture"), STR("sync")}) ? 1 : 0,
                        contains_text(text, STR("stroke")) ? 1 : 0,
                        contains_text(text, STR("flush")) ? 1 : 0);
                }

                m_state.last_failure = STR("commit_mode_probe_read_only_no_paint");
                return;
            }

            std::vector<Unreal::UFunction*> candidates{};
            for (auto* function : Unreal::TFieldRange<Unreal::UFunction>(component->GetClassPrivate(), Unreal::EFieldIterationFlags::IncludeAll))
            {
                if (!function)
                {
                    continue;
                }
                const auto text = lower_copy(function->GetName() + STR(" ") + function->GetFullName());
                if (!commit_sync_token_match(text) || non_return_param_count(function) != 0)
                {
                    continue;
                }
                if (contains_any_text(text, {STR("clear"), STR("reset"), STR("remove"), STR("delete"), STR("destroy"), STR("load")}))
                {
                    continue;
                }
                candidates.push_back(function);
                if (candidates.size() >= 24)
                {
                    break;
                }
            }

            constexpr int channel = PaintChannelAlbedoMetallicRoughness;
            const Color probe_color{0.18, 0.42, 0.62, 0.93, 0.0};
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.commit_calls = 0;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} commit_mode_probe start candidates={} component={}\n"),
                ModTag,
                static_cast<int>(candidates.size()),
                component->GetFullName());

            for (auto* candidate : candidates)
            {
                clear_component(component);
                auto* target = select_probe_render_target(component, channel);
                const auto clear_hash = hash_component_paint_state(component);
                const auto clear_stats = read_render_target_probe_stats(component, target, 12);
                PaintParamWriteStats param_stats{};
                const auto paint_ok = paint_probe_call(component,
                                                       STR("PaintAtUV"),
                                                       channel,
                                                       0,
                                                       0.5,
                                                       0.5,
                                                       std::optional<Unreal::FVector>{},
                                                       nullptr,
                                                       probe_color,
                                                       0.035,
                                                       1.0,
                                                       0.92,
                                                       &param_stats);
                const auto after_paint_hash = hash_component_paint_state(component);
                const auto after_paint_stats = read_render_target_probe_stats(component, target, 12);

                std::vector<uint8_t> params(static_cast<size_t>(candidate->GetParmsSize()), 0);
                component->ProcessEvent(candidate, params.data());
                ++m_state.commit_calls;
                const auto after_commit_hash = hash_component_paint_state(component);
                const auto after_commit_stats = read_render_target_probe_stats(component, target, 12);

                const auto paint_changed_pixels = count_changed_probe_pixels(clear_stats, after_paint_stats);
                const auto commit_changed_pixels = count_changed_probe_pixels(after_paint_stats, after_commit_stats);
                const auto remaining_changed_pixels = count_changed_probe_pixels(clear_stats, after_commit_stats);
                if (paint_ok)
                {
                    ++m_state.success;
                }
                else
                {
                    ++m_state.failures;
                }
                ++m_state.queued_strokes;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} commit_mode_probe candidate={} paint_ok={} paint_changed_pixels={} commit_changed_pixels={} remaining_changed_pixels={} clear_hash={} after_paint_hash={} after_commit_hash={} paint_hash_changed={} commit_hash_changed={} rt_avg_after_commit=({},{},{}) uniformity_after_commit={} target={} paint_failure={}\n"),
                    ModTag,
                    candidate->GetFullName(),
                    paint_ok ? 1 : 0,
                    paint_changed_pixels,
                    commit_changed_pixels,
                    remaining_changed_pixels,
                    clear_hash,
                    after_paint_hash,
                    after_commit_hash,
                    clear_hash != after_paint_hash ? 1 : 0,
                    after_paint_hash != after_commit_hash ? 1 : 0,
                    after_commit_stats.avg_r,
                    after_commit_stats.avg_g,
                    after_commit_stats.avg_b,
                    after_commit_stats.uniformity,
                    after_commit_stats.target_name,
                    param_stats.failure.empty() ? STR("<none>") : param_stats.failure);
            }
            clear_component(component);
            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.last_failure = STR("commit_mode_probe_complete");
        }

        auto run_channel_probe() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} channel_probe failed: current pawn RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }

            m_state.current_world = object_name_or_empty(pawn->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.commit_calls = 0;
            m_state.paint_state_hash_before = hash_component_paint_state(component);
            m_state.paint_state_hash_after = m_state.paint_state_hash_before;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.verified_paint_channel = PaintChannelUnknown;
            m_state.verified_paint_function.clear();
            m_state.verified_visible_backend = false;
            m_state.last_failure = STR("channel_probe_read_only_no_paint");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} channel_probe read_only=1 no_paint=1 no_clear=1 no_commit=1 reason=unsafe_runtime_paint_contract component={} hash={}\n"),
                ModTag,
                component->GetFullName(),
                m_state.paint_state_hash_before);
            return;

            auto backend = probe_verified_paint_backend(component, m_state, true);
            m_state.paint_state_hash_after = hash_component_paint_state(component);
            if (backend)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} channel_probe selected function={} channel={} hash_observed={} hash_before={} hash_after={} commit_called={} status={}\n"),
                    ModTag,
                    backend->function_name,
                    backend->channel,
                    m_state.verified_visible_backend ? 1 : 0,
                    m_state.paint_state_hash_before,
                    m_state.paint_state_hash_after,
                    m_state.commit_calls,
                    m_state.last_failure);
            }
            else
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} channel_probe failed: no function/channel accepted paint api hash_before={} hash_after={} commit_called={} failure={}\n"),
                    ModTag,
                    m_state.paint_state_hash_before,
                    m_state.paint_state_hash_after,
                    m_state.commit_calls,
                    m_state.last_failure);
            }
        }

        auto run_clear_current_paint() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} clear failed: current pawn RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }

            m_state.current_world = object_name_or_empty(pawn->GetWorld());
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.paint_state_hash_before = hash_component_paint_state(component);
            const auto ok = clear_component(component);
            m_state.commit_calls = call_commit_sync_candidates(component, false);
            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.verified_visible_backend = m_state.paint_state_hash_before != m_state.paint_state_hash_after;
            m_state.last_failure = ok ? STR("clear_requested") : STR("clear_function_unavailable_or_failed");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} clear current_component ok={} commit_called={} hash_before={} hash_after={} changed={}\n"),
                ModTag,
                ok ? 1 : 0,
                m_state.commit_calls,
                m_state.paint_state_hash_before,
                m_state.paint_state_hash_after,
                m_state.verified_visible_backend ? 1 : 0);
        }

        auto run_paint_test() -> void
        {
            run_dependency_probe(true);
            if (!m_state.paint_path_available)
            {
                m_state.last_failure = STR("paint_path_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} paint_test failed: PaintAtUVWithBrush/PaintAtUV/PaintStrokeUV unavailable\n"), ModTag);
                return;
            }
            auto* component = find_runtime_paint_object_with_uv();
            if (auto* pawn = find_player_pawn())
            {
                component = find_runtime_paint_component_for(pawn);
            }
            m_state.paint_state_hash_before = hash_component_paint_state(component);
            clear_component(component);
            m_state.commit_calls = call_commit_sync_candidates(component, false);
            int success = 0;
            int failures = 0;
            const std::array<Color, 4> colors{
                Color{0.18, 0.18, 0.18, 0.88, 0.0},
                Color{0.26, 0.26, 0.24, 0.92, 0.0},
                Color{0.34, 0.34, 0.31, 0.94, 0.0},
                Color{0.42, 0.42, 0.38, 0.96, 0.0}};
            const std::array<std::pair<double, double>, 4> uvs{
                std::make_pair(0.25, 0.25),
                std::make_pair(0.75, 0.25),
                std::make_pair(0.25, 0.75),
                std::make_pair(0.75, 0.75)};
            for (size_t i = 0; i < uvs.size(); ++i)
            {
                if (paint_at_uv(component, uvs[i].first, uvs[i].second, colors[i], 0.006, 0.65, 0.94))
                {
                    ++success;
                }
                else
                {
                    ++failures;
                }
            }
            m_state.success = success;
            m_state.failures = failures;
            m_state.paint_uv_success = success;
            m_state.commit_calls += call_commit_sync_candidates(component, false);
            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.verified_visible_backend = m_state.paint_state_hash_before != m_state.paint_state_hash_after;
            m_state.last_failure = success > 0 && m_state.verified_visible_backend ? STR("paint_test_ok") : STR("paint_test_state_hash_unchanged");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} paint_test uv_stamps success={} failures={} commit_called={} paint_hash_before={} paint_hash_after={} visible_backend={}\n"),
                ModTag,
                success,
                failures,
                m_state.commit_calls,
                m_state.paint_state_hash_before,
                m_state.paint_state_hash_after,
                m_state.verified_visible_backend ? 1 : 0);
        }

        auto run_brush_calibrate() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                m_state.last_failure = STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} brush_calibrate failed: current pawn RuntimePaintableComponent unavailable\n"), ModTag);
                return;
            }
            clear_component(component);
            m_state.paint_state_hash_before = hash_component_paint_state(component);
            m_state.commit_calls = call_commit_sync_candidates(component, false);
            const std::array<double, 4> radii{0.002, 0.004, 0.008, 0.016};
            const std::array<Color, 4> colors{
                Color{0.16, 0.16, 0.15, 0.92, 0.0},
                Color{0.24, 0.24, 0.22, 0.92, 0.0},
                Color{0.32, 0.32, 0.29, 0.92, 0.0},
                Color{0.40, 0.40, 0.36, 0.92, 0.0}};
            int success = 0;
            int failures = 0;
            for (size_t i = 0; i < radii.size(); ++i)
            {
                const auto u = 0.20 + static_cast<double>(i) * 0.20;
                const auto v = 0.50;
                if (paint_at_uv(component, u, v, colors[i], radii[i], 1.0, 0.95))
                {
                    ++success;
                }
                else
                {
                    ++failures;
                }
            }
            m_state.commit_calls += call_commit_sync_candidates(component, false);
            m_state.paint_state_hash_after = hash_component_paint_state(component);
            m_state.verified_visible_backend = m_state.paint_state_hash_before != m_state.paint_state_hash_after;
            m_state.success = success;
            m_state.failures = failures;
            m_state.paint_uv_success = success;
            m_state.last_failure = success > 0 && m_state.verified_visible_backend ? STR("brush_calibrate_ok") : STR("brush_calibrate_state_hash_unchanged");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} brush_calibrate radii=0.002,0.004,0.008,0.016 success={} failures={} commit_called={} paint_hash_before={} paint_hash_after={} visible_backend={}\n"),
                ModTag,
                success,
                failures,
                m_state.commit_calls,
                m_state.paint_state_hash_before,
                m_state.paint_state_hash_after,
                m_state.verified_visible_backend ? 1 : 0);
        }

        auto run_capture_test() -> void
        {
            run_dependency_probe(true);
            if (!m_state.capture_path_available)
            {
                m_state.last_failure = STR("capture_path_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} capture_test failed: SceneCapture/RenderTarget read functions unavailable\n"), ModTag);
                return;
            }
            auto* pawn = find_player_pawn();
            if (!pawn)
            {
                m_state.last_failure = STR("player_pawn_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} capture_test failed: player pawn unavailable\n"), ModTag);
                return;
            }
            if (!probe_scene_capture_pixels(pawn, m_state))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} capture_test failed: pixels={} failure={}\n"),
                    ModTag,
                    m_state.background_pixels,
                    m_state.last_failure);
                return;
            }
            m_state.last_failure = STR("capture_test_ok");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} capture_test ok pixels={}\n"),
                ModTag,
                m_state.background_pixels);
        }

        auto run_trace_test() -> void
        {
            run_dependency_probe(true);
            if (!m_state.trace_path_available)
            {
                m_state.last_failure = STR("trace_path_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} trace_test failed: trace function or skeletal mesh unavailable\n"), ModTag);
                return;
            }
            auto* pawn = find_player_pawn();
            if (!pawn)
            {
                m_state.last_failure = STR("player_pawn_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} trace_test failed: player pawn unavailable\n"), ModTag);
                return;
            }
            auto samples = make_visibility_samples(pawn, m_state);
            m_state.last_failure = samples.empty() ? STR("trace_no_visible_body_samples") : STR("trace_test_ok");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} trace_test views={} body_hits={} background_hits={} samples={}\n"),
                ModTag,
                m_state.views,
                m_state.body_trace_hits,
                m_state.background_trace_hits,
                static_cast<int>(samples.size()));
        }

        auto run_uv_test() -> void
        {
            run_dependency_probe(true);
            if (!m_state.uv_path_available)
            {
                m_state.last_failure = STR("uv_mapping_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} uv_test failed: RuntimePaintable UV paint path unavailable\n"), ModTag);
                return;
            }
            auto* pawn = find_player_pawn();
            if (!pawn)
            {
                m_state.last_failure = STR("player_pawn_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(STR("{} uv_test failed: player pawn unavailable\n"), ModTag);
                return;
            }
            auto samples = make_visibility_samples(pawn, m_state);
            m_state.visible_samples = static_cast<int>(samples.size());
            m_state.uv_mapping_ready = m_state.uv_hits > 0;
            m_state.atlas_bins = m_state.uv_hits;
            m_state.last_failure = m_state.uv_mapping_ready ? STR("uv_mapping_test_ok") : STR("body_hit_to_uv_failed");
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} uv_test views={} body_hits={} visible_samples={} uv_hits={} ready={}\n"),
                ModTag,
                m_state.views,
                m_state.body_trace_hits,
                m_state.visible_samples,
                m_state.uv_hits,
                m_state.uv_mapping_ready ? 1 : 0);
        }

        auto quality_gate_ready() const -> bool
        {
            return m_state.uv_mapping_ready && m_state.background_pixels > 128 && m_state.visible_samples > 128 &&
                   m_state.uv_hits > 128 && m_state.atlas_bins >= 128 &&
                   (m_state.capture_pixels_ready || m_state.background_trace_hits > 64);
        }

        auto run_quality_probe(bool quiet) -> void
        {
            run_dependency_probe(true);
            run_paint_pipeline_audit(false);

            auto* pawn = find_player_pawn();
            if (!pawn)
            {
                m_state.last_failure = STR("player_pawn_unavailable");
                if (!quiet)
                {
                    RC::Output::send<RC::LogLevel::Warning>(STR("{} probe failed: player pawn unavailable\n"), ModTag);
                }
                return;
            }

            auto samples = make_visibility_samples(pawn, m_state);
            const auto trace_background_hits = m_state.background_trace_hits;
            const auto trace_background_pixels = m_state.background_pixels;
            int capture_probe_pixels = 0;
            m_state.visible_samples = m_state.body_trace_hits;
            m_state.uv_mapping_ready = m_state.uv_hits > 0;
            m_state.atlas_bins = m_state.uv_hits;
            if (m_state.capture_path_available)
            {
                probe_scene_capture_pixels(pawn, m_state);
                capture_probe_pixels = m_state.background_pixels;
                m_state.background_pixels = std::max(trace_background_pixels, capture_probe_pixels);
            }

            if (!m_state.uv_mapping_ready)
            {
                m_state.last_failure = STR("body_hit_to_uv_failed");
            }
            else if (m_state.background_pixels <= 128)
            {
                m_state.last_failure = STR("insufficient_background_samples");
            }
            else if (!quality_gate_ready())
            {
                m_state.last_failure = STR("quality_gate_thresholds_not_met");
            }
            else
            {
                m_state.last_failure = STR("import_texture_gate_ready");
            }

            if (!quiet)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} probe views={} body_hits={} trace_background_hits={} trace_background_pixels={} capture_probe_pixels={} background_pixels={} visible_samples={} uv_hits={} atlas_bins={} official_pipeline={} ready={} failure={}\n"),
                    ModTag,
                    m_state.views,
                    m_state.body_trace_hits,
                    trace_background_hits,
                    trace_background_pixels,
                    capture_probe_pixels,
                    m_state.background_pixels,
                    m_state.visible_samples,
                    m_state.uv_hits,
                    m_state.atlas_bins,
                    m_state.official_paint_pipeline_ready ? 1 : 0,
                    quality_gate_ready() ? 1 : 0,
                    m_state.last_failure);
            }
        }

        auto run_uv_atlas_cal_status() -> void
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} uv_atlas_contract status version={} active={} component={} original_hash={} calibration_hash={} size={}x{} final_camo=0 commands=(mcam_core_uv_atlas_cal_begin,mcam_core_uv_atlas_cal_finish,mcam_core_uv_atlas_cal_restore)\n"),
                ModTag,
                ModVersion,
                m_uv_calibration_active ? 1 : 0,
                m_uv_calibration_component.empty() ? STR("<none>") : m_uv_calibration_component,
                m_uv_calibration_original_albedo_hash,
                m_uv_calibration_albedo_hash,
                m_uv_calibration_width,
                m_uv_calibration_height);
        }

        auto restore_uv_atlas_calibration(bool log_result) -> bool
        {
            if (!m_uv_calibration_active || m_uv_calibration_original_albedo.empty())
            {
                if (log_result)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} uv_atlas_contract restore skipped active={} saved_albedo={} final_camo=0\n"),
                        ModTag,
                        m_uv_calibration_active ? 1 : 0,
                        m_uv_calibration_original_albedo.empty() ? 0 : 1);
                }
                return false;
            }

            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                if (log_result)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("{} uv_atlas_contract restore failed component_unavailable active=1 saved_component={}\n"),
                        ModTag,
                        m_uv_calibration_component.empty() ? STR("<none>") : m_uv_calibration_component);
                }
                return false;
            }

            StringType albedo_failure{};
            StringType metallic_failure{};
            StringType roughness_failure{};
            const auto albedo_ok = import_channel_bytes(component, 0, m_uv_calibration_original_albedo, albedo_failure);
            const auto metallic_ok = m_uv_calibration_original_metallic.empty()
                                         ? true
                                         : import_channel_bytes(component, 1, m_uv_calibration_original_metallic, metallic_failure);
            const auto roughness_ok = m_uv_calibration_original_roughness.empty()
                                          ? true
                                          : import_channel_bytes(component, 2, m_uv_calibration_original_roughness, roughness_failure);
            const auto restored = albedo_ok && metallic_ok && roughness_ok;
            if (restored)
            {
                m_uv_calibration_active = false;
                m_uv_calibration_component.clear();
                m_uv_calibration_original_albedo.clear();
                m_uv_calibration_original_metallic.clear();
                m_uv_calibration_original_roughness.clear();
            }
            if (log_result || !restored)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} uv_atlas_contract restore restored={} albedo_ok={} metallic_ok={} roughness_ok={} albedo_failure={} metallic_failure={} roughness_failure={} component={} final_camo=0\n"),
                    ModTag,
                    restored ? 1 : 0,
                    albedo_ok ? 1 : 0,
                    metallic_ok ? 1 : 0,
                    roughness_ok ? 1 : 0,
                    albedo_failure.empty() ? STR("<none>") : albedo_failure,
                    metallic_failure.empty() ? STR("<none>") : metallic_failure,
                    roughness_failure.empty() ? STR("<none>") : roughness_failure,
                    component->GetFullName());
            }
            return restored;
        }

        auto run_uv_atlas_cal_begin() -> void
        {
            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.commit_calls = 0;
            m_state.verified_visible_backend = false;
            m_state.verified_paint_function = STR("uv_atlas_contract_diagnostic_import");
            m_state.verified_paint_channel = 0;

            if (m_uv_calibration_active && !restore_uv_atlas_calibration(false))
            {
                m_state.failures = 1;
                m_state.last_failure = STR("uv_atlas_calibration_restore_existing_failed");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} uv_atlas_contract begin refused existing_active=1 restore_failed=1 saved_component={} final_camo=0\n"),
                    ModTag,
                    m_uv_calibration_component.empty() ? STR("<none>") : m_uv_calibration_component);
                return;
            }

            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!pawn || !component)
            {
                m_state.failures = 1;
                m_state.last_failure = pawn ? STR("runtime_paint_component_unavailable") : STR("player_pawn_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} uv_atlas_contract begin refused pawn={} component={} final_camo=0\n"),
                    ModTag,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    component ? component->GetFullName() : STR("<null>"));
                return;
            }

            const auto albedo_before = export_channel_bytes(component, 0);
            const auto metallic_before = export_channel_bytes(component, 1);
            const auto roughness_before = export_channel_bytes(component, 2);
            if (!rgba_buffer_ready(albedo_before))
            {
                m_state.failures = 1;
                m_state.last_failure = STR("uv_atlas_calibration_export_failed");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} uv_atlas_contract begin refused albedo_export_ok={} size={}x{} bytes={} failure={} final_camo=0\n"),
                    ModTag,
                    albedo_before.ok ? 1 : 0,
                    albedo_before.width,
                    albedo_before.height,
                    static_cast<int>(albedo_before.bytes.size()),
                    albedo_before.failure.empty() ? STR("<none>") : albedo_before.failure);
                return;
            }

            auto calibration_albedo = build_uv_calibration_albedo(albedo_before);
            auto calibration_metallic = rgba_buffer_ready(metallic_before) ? build_scalar_channel(metallic_before, 0.0)
                                                                           : std::vector<uint8_t>{};
            auto calibration_roughness = rgba_buffer_ready(roughness_before) ? build_scalar_channel(roughness_before, 0.94)
                                                                             : std::vector<uint8_t>{};

            StringType albedo_failure{};
            StringType metallic_failure{};
            StringType roughness_failure{};
            const auto albedo_ok = import_channel_bytes(component, 0, calibration_albedo, albedo_failure);
            const auto metallic_ok = calibration_metallic.empty()
                                         ? true
                                         : import_channel_bytes(component, 1, calibration_metallic, metallic_failure);
            const auto roughness_ok = calibration_roughness.empty()
                                          ? true
                                          : import_channel_bytes(component, 2, calibration_roughness, roughness_failure);
            if (!albedo_ok)
            {
                m_state.failures = 1;
                m_state.last_failure = STR("uv_atlas_calibration_import_failed");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} uv_atlas_contract begin refused albedo_import_ok=0 failure={} final_camo=0\n"),
                    ModTag,
                    albedo_failure.empty() ? STR("<none>") : albedo_failure);
                return;
            }

            m_uv_calibration_active = true;
            m_uv_calibration_component = object_name_or_empty(component);
            m_uv_calibration_original_albedo = albedo_before.bytes;
            m_uv_calibration_original_metallic = metallic_before.bytes;
            m_uv_calibration_original_roughness = roughness_before.bytes;
            m_uv_calibration_original_albedo_hash = albedo_before.hash;
            m_uv_calibration_albedo_hash = hash_bytes(calibration_albedo);
            m_uv_calibration_metallic_hash = hash_bytes(calibration_metallic);
            m_uv_calibration_roughness_hash = hash_bytes(calibration_roughness);
            m_uv_calibration_width = albedo_before.width;
            m_uv_calibration_height = albedo_before.height;
            m_state.success = 1;
            m_state.failures = 0;
            m_state.last_failure = STR("uv_atlas_calibration_active_wait_then_finish");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} uv_atlas_contract begin imported=1 temporary_diagnostic_paint=1 final_camo=0 component={} size={}x{} original_hash={} calibration_hash={} metallic_ok={} roughness_ok={} metallic_failure={} roughness_failure={} next=mcam_core_uv_atlas_cal_finish restore=mcam_core_uv_atlas_cal_restore\n"),
                ModTag,
                component->GetFullName(),
                m_uv_calibration_width,
                m_uv_calibration_height,
                m_uv_calibration_original_albedo_hash,
                m_uv_calibration_albedo_hash,
                metallic_ok ? 1 : 0,
                roughness_ok ? 1 : 0,
                metallic_failure.empty() ? STR("<none>") : metallic_failure,
                roughness_failure.empty() ? STR("<none>") : roughness_failure);
        }

        auto run_uv_atlas_cal_finish() -> void
        {
            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.commit_calls = 0;
            m_state.verified_visible_backend = false;
            m_state.verified_paint_function = STR("uv_atlas_contract_visual_capture");
            m_state.verified_paint_channel = 0;

            if (!m_uv_calibration_active || m_uv_calibration_original_albedo.empty())
            {
                m_state.failures = 1;
                m_state.last_failure = STR("uv_atlas_calibration_not_active");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} uv_atlas_contract finish refused active={} saved_albedo={} run_begin_first=1 final_camo=0\n"),
                    ModTag,
                    m_uv_calibration_active ? 1 : 0,
                    m_uv_calibration_original_albedo.empty() ? 0 : 1);
                return;
            }

            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            auto* controller = pawn ? find_player_controller_for_pawn(pawn) : nullptr;
            auto* mesh = component ? find_target_mesh_for_runtime_paint(component, pawn) : nullptr;
            auto viewport = get_viewport_info(controller);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};
            if (!pawn || !component || !controller || !mesh || !frame)
            {
                m_state.failures = 1;
                m_state.last_failure = STR("uv_atlas_calibration_finish_prereq_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} uv_atlas_contract finish refused pawn={} component={} controller={} mesh={} frame={} restore_attempt=1 final_camo=0\n"),
                    ModTag,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    component ? component->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    mesh ? mesh->GetFullName() : STR("<null>"),
                    frame ? 1 : 0);
                restore_uv_atlas_calibration(false);
                return;
            }

            const auto current_albedo = export_channel_bytes(component, 0);
            const auto export_matches_calibration = current_albedo.ok && current_albedo.hash == m_uv_calibration_albedo_hash;
            std::vector<std::optional<Color>> dummy_colors(1, Color{0.34, 0.36, 0.32, 0.94, 0.0});
            ScreenHitCollectionStats stats{};
            auto samples = collect_screen_hit_samples(component,
                                                      pawn,
                                                      mesh,
                                                      controller,
                                                      viewport,
                                                      dummy_colors,
                                                      1,
                                                      1,
                                                      ScreenProjectionGridX,
                                                      ScreenProjectionGridY,
                                                      false,
                                                      m_state,
                                                      stats,
                                                      0.0,
                                                      1.0,
                                                      0.0,
                                                      1.0,
                                                      0,
                                                      0,
                                                      false);
            if (stats.hit_uv_count < MinScreenHitUvSamples)
            {
                ScreenHitCollectionStats normalized_stats{};
                auto normalized_samples = collect_screen_hit_samples(component,
                                                                    pawn,
                                                                    mesh,
                                                                    controller,
                                                                    viewport,
                                                                    dummy_colors,
                                                                    1,
                                                                    1,
                                                                    ScreenProjectionGridX,
                                                                    ScreenProjectionGridY,
                                                                    true,
                                                                    m_state,
                                                                    normalized_stats,
                                                                    0.0,
                                                                    1.0,
                                                                    0.0,
                                                                    1.0,
                                                                    0,
                                                                    0,
                                                                    false);
                if (normalized_stats.hit_uv_count > stats.hit_uv_count)
                {
                    stats = normalized_stats;
                    samples = std::move(normalized_samples);
                }
            }

            if (stats.hit_uv_count >= MinScreenHitUvSamples &&
                stats.hit_uv_count < 768 &&
                stats.max_nx > stats.min_nx &&
                stats.max_ny > stats.min_ny)
            {
                const auto margin_x = std::max(0.035, (stats.max_nx - stats.min_nx) * 0.22);
                const auto margin_y = std::max(0.045, (stats.max_ny - stats.min_ny) * 0.22);
                const auto refine_min_nx = clamp(stats.min_nx - margin_x, 0.0, 1.0);
                const auto refine_max_nx = clamp(stats.max_nx + margin_x, 0.0, 1.0);
                const auto refine_min_ny = clamp(stats.min_ny - margin_y, 0.0, 1.0);
                const auto refine_max_ny = clamp(stats.max_ny + margin_y, 0.0, 1.0);
                const auto refine_width_px = (refine_max_nx - refine_min_nx) * static_cast<double>(viewport.width);
                const auto refine_height_px = (refine_max_ny - refine_min_ny) * static_cast<double>(viewport.height);
                const auto refine_grid_width = std::min(384, std::max(64, static_cast<int>(std::ceil(refine_width_px / 3.0))));
                const auto refine_grid_height = std::min(384, std::max(64, static_cast<int>(std::ceil(refine_height_px / 3.0))));
                ScreenHitCollectionStats refined_stats{};
                auto refined_samples = collect_screen_hit_samples(component,
                                                                  pawn,
                                                                  mesh,
                                                                  controller,
                                                                  viewport,
                                                                  dummy_colors,
                                                                  1,
                                                                  1,
                                                                  refine_grid_width,
                                                                  refine_grid_height,
                                                                  false,
                                                                  m_state,
                                                                  refined_stats,
                                                                  refine_min_nx,
                                                                  refine_max_nx,
                                                                  refine_min_ny,
                                                                  refine_max_ny,
                                                                  1536,
                                                                  60000,
                                                                  false);
                if (refined_stats.hit_uv_count > stats.hit_uv_count)
                {
                    stats = refined_stats;
                    samples = std::move(refined_samples);
                }
            }

            const auto calibration_samples = select_alignment_samples(samples, 768);
            const auto rt_width = 1024;
            const auto rt_height = std::max(
                256,
                static_cast<int>(std::round(static_cast<double>(rt_width) *
                                            static_cast<double>(std::max(1, viewport.height)) /
                                            static_cast<double>(std::max(1, viewport.width)))));
            CaptureGridDiagnostics capture_diag{};
            const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
            const auto visible_colors = capture_screen_sample_colors(pawn,
                                                                     frame->eye,
                                                                     look_at,
                                                                     calibration_samples,
                                                                     rt_width,
                                                                     rt_height,
                                                                     false,
                                                                     m_state,
                                                                     frame->fov_degrees,
                                                                     &capture_diag,
                                                                     ScreenTransform{},
                                                                     frame->has_rotation ? &frame->rotation : nullptr,
                                                                     false);
            const auto visible_summary = summarize_capture_colors(visible_colors);
            const auto visible_rgb_range = std::max({visible_summary.max_r - visible_summary.min_r,
                                                     visible_summary.max_g - visible_summary.min_g,
                                                     visible_summary.max_b - visible_summary.min_b});

            struct Candidate
            {
                UvAtlasTransform transform{};
                const CharType* label{STR("uv_identity")};
            };
            const std::array<Candidate, 8> candidates{
                Candidate{UvAtlasTransform{false, false, false}, STR("uv_identity")},
                Candidate{UvAtlasTransform{false, true, false}, STR("uv_flip_u")},
                Candidate{UvAtlasTransform{false, false, true}, STR("uv_flip_v")},
                Candidate{UvAtlasTransform{false, true, true}, STR("uv_flip_uv")},
                Candidate{UvAtlasTransform{true, false, false}, STR("uv_swap")},
                Candidate{UvAtlasTransform{true, true, false}, STR("uv_swap_flip_u")},
                Candidate{UvAtlasTransform{true, false, true}, STR("uv_swap_flip_v")},
                Candidate{UvAtlasTransform{true, true, true}, STR("uv_swap_flip_uv")}};

            std::array<double, 8> medians{};
            std::array<double, 8> original_medians{};
            medians.fill(1000000.0);
            original_medians.fill(1000000.0);
            double best_median = 1000000.0;
            double runner_up_median = 1000000.0;
            double best_original_median = 1000000.0;
            int best_index = -1;
            int best_original_index = -1;
            int best_pairs = 0;
            int best_original_pairs = 0;
            for (int candidate_index = 0; candidate_index < static_cast<int>(candidates.size()); ++candidate_index)
            {
                std::vector<double> distances{};
                std::vector<double> original_distances{};
                const auto count = std::min(calibration_samples.size(), visible_colors.size());
                distances.reserve(count);
                original_distances.reserve(count);
                for (size_t i = 0; i < count; ++i)
                {
                    if (!visible_colors[i])
                    {
                        continue;
                    }
                    const auto uv = apply_uv_atlas_transform(calibration_samples[i].u,
                                                             calibration_samples[i].v,
                                                             candidates[static_cast<size_t>(candidate_index)].transform);
                    const auto expected = uv_calibration_color(uv.first, uv.second);
                    distances.push_back(chroma_distance_rgb(*visible_colors[i], expected));
                    if (auto original = sample_rgba_bytes(m_uv_calibration_original_albedo,
                                                          m_uv_calibration_width,
                                                          m_uv_calibration_height,
                                                          uv.first,
                                                          uv.second))
                    {
                        original_distances.push_back(chroma_distance_rgb(*visible_colors[i], *original));
                    }
                }
                const auto pairs = static_cast<int>(distances.size());
                const auto median = pairs > 0 ? median_value(std::move(distances)) : 1000000.0;
                const auto original_pairs = static_cast<int>(original_distances.size());
                const auto original_median = original_pairs > 0 ? median_value(std::move(original_distances)) : 1000000.0;
                medians[static_cast<size_t>(candidate_index)] = median;
                original_medians[static_cast<size_t>(candidate_index)] = original_median;
                if (median < best_median)
                {
                    runner_up_median = best_median;
                    best_median = median;
                    best_pairs = pairs;
                    best_index = candidate_index;
                }
                else if (median < runner_up_median)
                {
                    runner_up_median = median;
                }
                if (original_median < best_original_median)
                {
                    best_original_median = original_median;
                    best_original_pairs = original_pairs;
                    best_original_index = candidate_index;
                }
            }

            std::vector<double> decoded_errors{};
            decoded_errors.reserve(visible_colors.size());
            std::array<std::vector<double>, 8> decoded_hit_distances{};
            int decoded_pairs = 0;
            int decoded_confident_pairs = 0;
            double decoded_min_u = 1.0;
            double decoded_min_v = 1.0;
            double decoded_max_u = 0.0;
            double decoded_max_v = 0.0;
            const auto decoded_count = std::min(calibration_samples.size(), visible_colors.size());
            for (size_t i = 0; i < decoded_count; ++i)
            {
                if (!visible_colors[i])
                {
                    continue;
                }
                const auto decoded = decode_uv_calibration_color(*visible_colors[i]);
                ++decoded_pairs;
                decoded_errors.push_back(decoded.error);
                if (decoded.ok)
                {
                    ++decoded_confident_pairs;
                }
                decoded_min_u = std::min(decoded_min_u, decoded.u);
                decoded_min_v = std::min(decoded_min_v, decoded.v);
                decoded_max_u = std::max(decoded_max_u, decoded.u);
                decoded_max_v = std::max(decoded_max_v, decoded.v);
                for (int candidate_index = 0; candidate_index < static_cast<int>(candidates.size()); ++candidate_index)
                {
                    const auto uv = apply_uv_atlas_transform(calibration_samples[i].u,
                                                             calibration_samples[i].v,
                                                             candidates[static_cast<size_t>(candidate_index)].transform);
                    const auto du = decoded.u - uv.first;
                    const auto dv = decoded.v - uv.second;
                    decoded_hit_distances[static_cast<size_t>(candidate_index)].push_back(std::sqrt(du * du + dv * dv));
                }
            }

            std::array<double, 8> decoded_hit_medians{};
            decoded_hit_medians.fill(1000000.0);
            double decoded_best_hit_median = 1000000.0;
            int decoded_best_hit_index = -1;
            for (int candidate_index = 0; candidate_index < static_cast<int>(decoded_hit_distances.size()); ++candidate_index)
            {
                auto& distances = decoded_hit_distances[static_cast<size_t>(candidate_index)];
                const auto median = distances.empty() ? 1000000.0 : median_value(std::move(distances));
                decoded_hit_medians[static_cast<size_t>(candidate_index)] = median;
                if (median < decoded_best_hit_median)
                {
                    decoded_best_hit_median = median;
                    decoded_best_hit_index = candidate_index;
                }
            }
            const auto decoded_error_median = decoded_errors.empty() ? 1000000.0 : median_value(std::move(decoded_errors));
            const auto decoded_confident_ratio = decoded_pairs > 0
                                                     ? static_cast<double>(decoded_confident_pairs) / static_cast<double>(decoded_pairs)
                                                     : 0.0;
            const auto decoded_route_ready = decoded_pairs >= 128 &&
                                             decoded_error_median <= 0.18 &&
                                             decoded_confident_ratio >= 0.55 &&
                                             (decoded_max_u - decoded_min_u) >= 0.08 &&
                                             (decoded_max_v - decoded_min_v) >= 0.08;
            const CharType* decoded_selected_label =
                decoded_best_hit_index >= 0 ? candidates[static_cast<size_t>(decoded_best_hit_index)].label : STR("<none>");

            const auto separated_from_runner = runner_up_median >= 999999.0 ||
                                               best_median <= runner_up_median * 0.86 ||
                                               (runner_up_median - best_median) >= 0.025;
            const auto uv_contract_ok = best_index >= 0 &&
                                        export_matches_calibration &&
                                        best_pairs >= std::min(64, static_cast<int>(calibration_samples.size() / 2)) &&
                                        best_median <= 0.22 &&
                                        separated_from_runner;
            const CharType* selected_label = best_index >= 0 ? candidates[static_cast<size_t>(best_index)].label : STR("<none>");
            const CharType* original_selected_label = best_original_index >= 0 ? candidates[static_cast<size_t>(best_original_index)].label : STR("<none>");
            const auto closer_to_original = best_original_median < 999999.0 &&
                                            best_original_pairs >= std::min(32, std::max(1, best_pairs / 2)) &&
                                            best_original_median <= 0.28 &&
                                            best_original_median + 0.08 < best_median;
            const CharType* conclusion = STR("unknown");
            if (!export_matches_calibration)
            {
                conclusion = STR("import_not_persisted_or_overwritten");
            }
            else if (visible_summary.pixels <= 0 || capture_diag.read_pixels <= 0)
            {
                conclusion = STR("visible_capture_failed");
            }
            else if (visible_summary.uniform || visible_rgb_range < 0.05)
            {
                conclusion = STR("import_not_visible_to_material_or_capture_stale");
            }
            else if (uv_contract_ok)
            {
                conclusion = STR("hit_uv_matches_import_atlas");
            }
            else if (decoded_route_ready)
            {
                conclusion = STR("decoded_import_uv_available_hit_uv_invalid");
            }
            else if (closer_to_original)
            {
                conclusion = STR("import_export_buffer_not_rendered_to_visible_material");
            }
            else if (best_pairs < 64)
            {
                conclusion = STR("insufficient_body_hits_for_uv_contract");
            }
            else if (!separated_from_runner)
            {
                conclusion = STR("hit_uv_not_explainable_by_simple_flip_swap");
            }
            else
            {
                conclusion = STR("hit_uv_does_not_match_import_atlas");
            }

            const auto restored = restore_uv_atlas_calibration(false);
            m_state.success = uv_contract_ok ? 1 : 0;
            m_state.failures = uv_contract_ok ? 0 : 1;
            m_state.last_failure = uv_contract_ok ? STR("uv_atlas_contract_verified_restore_done")
                                                  : STR("uv_atlas_contract_unverified_restore_done");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} uv_atlas_contract finish ok={} conclusion={} visible_capture_mode=full_scene_body_pixels selected={} original_selected={} decoded_route_ready={} decoded_selected={} decoded_pairs={} decoded_confident_ratio={} decoded_error_median={} decoded_uv_range=({}, {})-({}, {}) decoded_best_hit_median={} export_matches_calibration={} current_hash={} calibration_hash={} original_hash={} samples={} hit_uv_count={} visible_reads={} missing={} visible_pixels={} visible_rgb_range={} visible_uniform={} best_median={} runner_up_median={} best_original_median={} separated={} pairs={} original_pairs={} medians_identity_flipU_flipV_flipUV_swap_swapFlipU_swapFlipV_swapFlipUV=({},{},{},{},{},{},{},{}) original_medians=({},{},{},{},{},{},{},{}) decoded_hit_medians=({},{},{},{},{},{},{},{}) rt={}x{} camera_fov={} frame_fov={} viewport={}x{} restored={} final_camo=0\n"),
                ModTag,
                uv_contract_ok ? 1 : 0,
                conclusion,
                selected_label,
                original_selected_label,
                decoded_route_ready ? 1 : 0,
                decoded_selected_label,
                decoded_pairs,
                decoded_confident_ratio,
                decoded_error_median < 999999.0 ? decoded_error_median : 0.0,
                decoded_pairs > 0 ? decoded_min_u : 0.0,
                decoded_pairs > 0 ? decoded_min_v : 0.0,
                decoded_pairs > 0 ? decoded_max_u : 0.0,
                decoded_pairs > 0 ? decoded_max_v : 0.0,
                decoded_best_hit_median < 999999.0 ? decoded_best_hit_median : 0.0,
                export_matches_calibration ? 1 : 0,
                current_albedo.hash,
                m_uv_calibration_albedo_hash,
                m_uv_calibration_original_albedo_hash,
                static_cast<int>(calibration_samples.size()),
                stats.hit_uv_count,
                capture_diag.read_pixels,
                capture_diag.missing_pixels,
                visible_summary.pixels,
                visible_rgb_range,
                visible_summary.uniform ? 1 : 0,
                best_median < 999999.0 ? best_median : 0.0,
                runner_up_median < 999999.0 ? runner_up_median : 0.0,
                best_original_median < 999999.0 ? best_original_median : 0.0,
                separated_from_runner ? 1 : 0,
                best_pairs,
                best_original_pairs,
                medians[0] < 999999.0 ? medians[0] : 0.0,
                medians[1] < 999999.0 ? medians[1] : 0.0,
                medians[2] < 999999.0 ? medians[2] : 0.0,
                medians[3] < 999999.0 ? medians[3] : 0.0,
                medians[4] < 999999.0 ? medians[4] : 0.0,
                medians[5] < 999999.0 ? medians[5] : 0.0,
                medians[6] < 999999.0 ? medians[6] : 0.0,
                medians[7] < 999999.0 ? medians[7] : 0.0,
                original_medians[0] < 999999.0 ? original_medians[0] : 0.0,
                original_medians[1] < 999999.0 ? original_medians[1] : 0.0,
                original_medians[2] < 999999.0 ? original_medians[2] : 0.0,
                original_medians[3] < 999999.0 ? original_medians[3] : 0.0,
                original_medians[4] < 999999.0 ? original_medians[4] : 0.0,
                original_medians[5] < 999999.0 ? original_medians[5] : 0.0,
                original_medians[6] < 999999.0 ? original_medians[6] : 0.0,
                original_medians[7] < 999999.0 ? original_medians[7] : 0.0,
                decoded_hit_medians[0] < 999999.0 ? decoded_hit_medians[0] : 0.0,
                decoded_hit_medians[1] < 999999.0 ? decoded_hit_medians[1] : 0.0,
                decoded_hit_medians[2] < 999999.0 ? decoded_hit_medians[2] : 0.0,
                decoded_hit_medians[3] < 999999.0 ? decoded_hit_medians[3] : 0.0,
                decoded_hit_medians[4] < 999999.0 ? decoded_hit_medians[4] : 0.0,
                decoded_hit_medians[5] < 999999.0 ? decoded_hit_medians[5] : 0.0,
                decoded_hit_medians[6] < 999999.0 ? decoded_hit_medians[6] : 0.0,
                decoded_hit_medians[7] < 999999.0 ? decoded_hit_medians[7] : 0.0,
                rt_width,
                rt_height,
                frame->camera_fov_degrees,
                frame->fov_degrees,
                viewport.width,
                viewport.height,
                restored ? 1 : 0);
        }

        auto restore_screen_paint_dot(bool log_result) -> bool
        {
            if (!m_screen_dot_active || m_screen_dot_original_albedo.empty())
            {
                if (log_result)
                {
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} screen_paint_dot restore skipped active={} saved_albedo={}\n"),
                        ModTag,
                        m_screen_dot_active ? 1 : 0,
                        m_screen_dot_original_albedo.empty() ? 0 : 1);
                }
                return false;
            }

            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            if (!component)
            {
                if (log_result)
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("{} screen_paint_dot restore failed component_unavailable saved_component={}\n"),
                        ModTag,
                        m_screen_dot_component.empty() ? STR("<none>") : m_screen_dot_component);
                }
                return false;
            }

            StringType albedo_failure{};
            StringType metallic_failure{};
            StringType roughness_failure{};
            const auto albedo_ok = import_channel_bytes(component, 0, m_screen_dot_original_albedo, albedo_failure);
            const auto metallic_ok = m_screen_dot_original_metallic.empty()
                                         ? true
                                         : import_channel_bytes(component, 1, m_screen_dot_original_metallic, metallic_failure);
            const auto roughness_ok = m_screen_dot_original_roughness.empty()
                                          ? true
                                          : import_channel_bytes(component, 2, m_screen_dot_original_roughness, roughness_failure);
            const auto restored = albedo_ok && metallic_ok && roughness_ok;
            if (restored)
            {
                m_screen_dot_active = false;
                m_screen_dot_component.clear();
                m_screen_dot_original_albedo.clear();
                m_screen_dot_original_metallic.clear();
                m_screen_dot_original_roughness.clear();
            }
            if (log_result || !restored)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} screen_paint_dot restore restored={} albedo_ok={} metallic_ok={} roughness_ok={} albedo_failure={} metallic_failure={} roughness_failure={} component={}\n"),
                    ModTag,
                    restored ? 1 : 0,
                    albedo_ok ? 1 : 0,
                    metallic_ok ? 1 : 0,
                    roughness_ok ? 1 : 0,
                    albedo_failure.empty() ? STR("<none>") : albedo_failure,
                    metallic_failure.empty() ? STR("<none>") : metallic_failure,
                    roughness_failure.empty() ? STR("<none>") : roughness_failure,
                    component->GetFullName());
            }
            return restored;
        }

        auto run_screen_paint_dot_probe() -> void
        {
            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.commit_calls = 0;
            m_state.verified_visible_backend = false;
            m_state.verified_paint_function = STR("PaintAtScreenPosition.dot_probe");
            m_state.verified_paint_channel = PaintChannelAlbedoMetallicRoughness;

            if (m_screen_dot_active)
            {
                restore_screen_paint_dot(false);
            }

            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : nullptr;
            auto* controller = pawn ? find_player_controller_for_pawn(pawn) : nullptr;
            auto* mesh = component ? find_target_mesh_for_runtime_paint(component, pawn) : nullptr;
            auto viewport = get_viewport_info(controller);
            if (!pawn || !component || !controller || !mesh || viewport.width <= 0 || viewport.height <= 0)
            {
                m_state.failures = 1;
                m_state.last_failure = STR("screen_paint_dot_prereq_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} screen_paint_dot refused pawn={} component={} controller={} mesh={} viewport={}x{}\n"),
                    ModTag,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    component ? component->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    mesh ? mesh->GetFullName() : STR("<null>"),
                    viewport.width,
                    viewport.height);
                return;
            }

            const auto albedo_before = export_channel_bytes(component, 0);
            const auto metallic_before = export_channel_bytes(component, 1);
            const auto roughness_before = export_channel_bytes(component, 2);
            if (!rgba_buffer_ready(albedo_before))
            {
                m_state.failures = 1;
                m_state.last_failure = STR("screen_paint_dot_export_failed");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} screen_paint_dot refused export_ok={} size={}x{} failure={}\n"),
                    ModTag,
                    albedo_before.ok ? 1 : 0,
                    albedo_before.width,
                    albedo_before.height,
                    albedo_before.failure.empty() ? STR("<none>") : albedo_before.failure);
                return;
            }

            std::vector<std::optional<Color>> dummy_colors(1, Color{0.34, 0.36, 0.32, 0.94, 0.0});
            ScreenHitCollectionStats stats{};
            auto samples = collect_screen_hit_samples(component,
                                                      pawn,
                                                      mesh,
                                                      controller,
                                                      viewport,
                                                      dummy_colors,
                                                      1,
                                                      1,
                                                      ScreenProjectionGridX,
                                                      ScreenProjectionGridY,
                                                      false,
                                                      m_state,
                                                      stats,
                                                      0.0,
                                                      1.0,
                                                      0.0,
                                                      1.0,
                                                      0,
                                                      0,
                                                      false);
            if (samples.empty())
            {
                m_state.failures = 1;
                m_state.last_failure = STR("screen_paint_dot_no_body_hit");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} screen_paint_dot refused hit_uv_count={} attempts={} first_failure={}\n"),
                    ModTag,
                    stats.hit_uv_count,
                    stats.attempts,
                    stats.first_failure.empty() ? STR("<none>") : stats.first_failure);
                return;
            }

            const auto sample = samples[static_cast<size_t>(samples.size() / 2)];
            const auto brush_ok = configure_screen_brush(component, 10.0, 1.0, 1.0);
            PaintParamWriteStats param_stats{};
            param_stats.channel_label =
                channel_enum_label(component->GetFunctionByNameInChain(STR("PaintAtScreenPosition")),
                                   PaintChannelAlbedoMetallicRoughness);
            const Color dot_color{0.95, 0.03, 0.85, 0.94, 0.0};
            const auto component_hash_before = hash_component_paint_state(component);
            const auto albedo_hash_before = albedo_before.hash;
            const auto api_ok = paint_at_screen_position(component,
                                                         mesh,
                                                         controller,
                                                         sample.screen_x,
                                                         sample.screen_y,
                                                         dot_color,
                                                         PaintChannelAlbedoMetallicRoughness,
                                                         true,
                                                         &param_stats,
                                                         0);
            const auto albedo_after = export_channel_bytes(component, 0);
            const auto component_hash_after = hash_component_paint_state(component);
            const auto albedo_changed = albedo_after.ok && albedo_after.hash != albedo_hash_before;
            const auto component_changed = component_hash_after != component_hash_before;

            if (albedo_changed || component_changed)
            {
                m_screen_dot_active = true;
                m_screen_dot_component = object_name_or_empty(component);
                m_screen_dot_original_albedo = albedo_before.bytes;
                m_screen_dot_original_metallic = metallic_before.bytes;
                m_screen_dot_original_roughness = roughness_before.bytes;
                m_screen_dot_original_hash = albedo_hash_before;
            }

            m_state.success = api_ok ? 1 : 0;
            m_state.failures = api_ok ? 0 : 1;
            m_state.queued_strokes = 1;
            m_state.paint_uv_success = api_ok ? 1 : 0;
            m_state.paint_state_hash_before = component_hash_before;
            m_state.paint_state_hash_after = component_hash_after;
            m_state.verified_visible_backend = api_ok && (albedo_changed || component_changed);
            m_state.last_failure = m_state.verified_visible_backend ? STR("screen_paint_dot_applied_restore_available")
                                                                    : STR("screen_paint_dot_unverified");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} screen_paint_dot result api_ok={} visible_backend={} brush_ok={} screen=({}, {}) norm=({}, {}) hit_uv=({}, {}) hit_world=({}, {}, {}) albedo_hash_before={} albedo_hash_after={} albedo_changed={} component_hash_before={} component_hash_after={} component_changed={} saved_for_restore={} function=PaintAtScreenPosition channel={} label={} color_param={} screen_param={} wrote_color={} wrote_screen={} wrote_channel={} failure={} restore=mcam_core_screen_paint_restore note=temporary_magenta_dot\n"),
                ModTag,
                api_ok ? 1 : 0,
                m_state.verified_visible_backend ? 1 : 0,
                brush_ok ? 1 : 0,
                sample.screen_x,
                sample.screen_y,
                sample.nx,
                sample.ny,
                sample.u,
                sample.v,
                sample.world_position.X(),
                sample.world_position.Y(),
                sample.world_position.Z(),
                albedo_hash_before,
                albedo_after.hash,
                albedo_changed ? 1 : 0,
                component_hash_before,
                component_hash_after,
                component_changed ? 1 : 0,
                m_screen_dot_active ? 1 : 0,
                PaintChannelAlbedoMetallicRoughness,
                param_stats.channel_label.empty() ? STR("<none>") : param_stats.channel_label,
                param_stats.color_param.empty() ? STR("<none>") : param_stats.color_param,
                param_stats.uv_param.empty() ? STR("<none>") : param_stats.uv_param,
                param_stats.wrote_color ? 1 : 0,
                param_stats.wrote_uv ? 1 : 0,
                param_stats.wrote_channel ? 1 : 0,
                param_stats.failure.empty() ? STR("<none>") : param_stats.failure);
        }

        auto run_play() -> void
        {
            disable_cloak_overlay(false);
            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.current_world.clear();
            m_state.current_pawn.clear();
            m_state.current_component.clear();
            m_state.verified_visible_backend = false;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.paint_uv_success = 0;
            m_state.paint_world_success = 0;
            m_state.commit_calls = 0;
            m_state.verified_paint_function = STR("PaintAtScreenPosition.body_mask");
            m_state.verified_paint_channel = PaintChannelAlbedoMetallicRoughness;

            if (m_screen_dot_active)
            {
                restore_screen_paint_dot(false);
            }

            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play started id={} version={} route=model_runtime_paint backend=screen_body_mask_validated_scene_capture actual_model_paint=1 viewport_resolution_capture=1 scene_capture_color=1 trace_primary=1 capture_alignment=show_only_body_mask mask_clear_sentinel=magenta trace_albedo_anchor=0 higher_density_screen_paint=1 supplemental_orbit_uv_import=0 front_screen_paint=1 import_fallback=0 no_gui=1 no_umg_overlay=1 no_material_shader=1 no_clear=1 no_commit=1 no_mesh_hide=1 no_trace_color_fallback=1\n"),
                ModTag,
                m_state.play_id,
                ModVersion);

            auto* pawn = find_player_pawn();
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            if (!pawn || !component)
            {
                m_state.failures = 1;
                m_state.last_failure = !pawn ? STR("player_pawn_unavailable") : STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} play model_runtime_paint refused reason={} pawn={} component={} actual_model_paint=0 no_gui=1 no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1\n"),
                    ModTag,
                    m_state.last_failure,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    component ? component->GetFullName() : STR("<null>"));
                return;
            }

            if (auto* world = pawn->GetWorld())
            {
                m_state.current_world = world->GetFullName();
            }
            m_state.current_pawn = pawn->GetFullName();
            m_state.current_component = component->GetFullName();
            const auto front_ok = apply_screen_body_paint_cloak(component, pawn, m_state, STR("model_runtime_paint_front_last"));
            (void)front_ok;
        }

        auto run_projection_audit(bool export_samples) -> void
        {
            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.current_world.clear();
            m_state.current_pawn.clear();
            m_state.current_component.clear();
            m_state.verified_visible_backend = false;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.paint_uv_success = 0;
            m_state.paint_world_success = 0;
            m_state.commit_calls = 0;
            m_state.verified_paint_function = STR("projection_audit_no_import");
            m_state.verified_paint_channel = PaintChannelUnknown;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} projection_audit started id={} version={} export_samples={} no_paint=1 no_import=1 no_clear=1 no_commit=1\n"),
                ModTag,
                m_state.play_id,
                ModVersion,
                export_samples ? 1 : 0);

            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            if (!pawn)
            {
                m_state.failures = 1;
                m_state.last_failure = STR("projection_audit_player_pawn_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} projection_audit refused: player pawn unavailable; no paint changed\n"),
                    ModTag);
                return;
            }
            auto* component = find_runtime_paint_component_for(pawn);
            auto* world = pawn->GetWorld();
            m_state.current_world = object_name_or_empty(world);
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            const auto export_backend_ready = component &&
                                              component->GetFunctionByNameInChain(STR("ExportChannelToBytes"));
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} projection_audit target id={} world={} pawn={} component={} export_backend={}\n"),
                ModTag,
                m_state.play_id,
                m_state.current_world.empty() ? STR("<null>") : m_state.current_world,
                m_state.current_pawn.empty() ? STR("<null>") : m_state.current_pawn,
                m_state.current_component.empty() ? STR("<null>") : m_state.current_component,
                export_backend_ready ? 1 : 0);

            if (!component || !export_backend_ready)
            {
                m_state.failures = 1;
                m_state.last_failure = component ? STR("projection_audit_export_backend_unavailable")
                                                 : STR("runtime_paint_component_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} projection_audit refused id={} export_backend={} failure={}\n"),
                    ModTag,
                    m_state.play_id,
                    export_backend_ready ? 1 : 0,
                    m_state.last_failure);
                return;
            }

            apply_screen_hit_import_cloak(component, pawn, m_state, true, export_samples);
        }

        auto run_play_disabled() -> void
        {
            m_state.queue_active = false;
            m_state.cancelled = false;
            ++m_state.play_id;
            m_state.queued_strokes = 0;
            m_state.success = 0;
            m_state.failures = 0;
            m_state.paint_uv_success = 0;
            m_state.paint_world_success = 0;
            m_state.commit_calls = 0;
            m_state.verified_visible_backend = false;
            m_state.verified_paint_function = STR("game_camera_contract_no_paint");
            m_state.verified_paint_channel = PaintChannelUnknown;
            m_state.last_failure = STR("play_disabled_game_camera_contract_unknown");
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play disabled id={} version={} reason=game_camera_contract_unknown no_paint=1 queued_strokes=0 no_import=1 no_clear=1 no_commit=1\n"),
                ModTag,
                m_state.play_id,
                ModVersion);
        }

        auto run_lab_probe_view() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            auto viewport = get_viewport_info(controller);
            auto* camera = find_player_camera_manager();
            auto camera_location = call_no_params_return_vector(camera, STR("GetCameraLocation"));
            auto camera_rotation = call_no_params_return_rotator(camera, STR("GetCameraRotation"));
            const auto camera_fov = camera_fov_from_manager(camera);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};

            m_state.current_world = object_name_or_empty(pawn ? pawn->GetWorld() : nullptr);
            m_state.current_pawn = object_name_or_empty(pawn);
            m_state.current_component = object_name_or_empty(component);
            m_state.queued_strokes = 0;
            m_state.success = pawn && controller && camera_location && camera_rotation ? 1 : 0;
            m_state.failures = m_state.success ? 0 : 1;
            m_state.last_failure = m_state.success ? STR("lab_probe_view_ready_no_paint")
                                                   : STR("lab_probe_view_prereq_unavailable");

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} probe_view source=cpp_bridge version={} no_paint=1 no_import=1 no_clear=1 no_commit=1 camera_location=({}, {}, {}) camera_rotation=(pitch={}, yaw={}, roll={}) camera_fov={} camera_fov_fallback={} viewport_size={}x{} viewport_fallback={} deproject_frame={} deproject_fov={} deproject_hfov={} deproject_vfov={} camera_deproject_angle_delta={} controller={} pawn={} component={} mesh={} last_error={}\n"),
                LabTag,
                ModVersion,
                camera_location ? camera_location->X() : 0.0,
                camera_location ? camera_location->Y() : 0.0,
                camera_location ? camera_location->Z() : 0.0,
                camera_rotation ? camera_rotation->GetPitch() : 0.0,
                camera_rotation ? camera_rotation->GetYaw() : 0.0,
                camera_rotation ? camera_rotation->GetRoll() : 0.0,
                camera_fov.first,
                camera_fov.second ? 1 : 0,
                viewport.width,
                viewport.height,
                viewport.fallback ? 1 : 0,
                frame ? 1 : 0,
                frame ? frame->fov_degrees : 0.0,
                frame ? frame->deproject_hfov : 0.0,
                frame ? frame->deproject_vfov : 0.0,
                frame ? frame->camera_deproject_angle_delta : 0.0,
                controller ? controller->GetFullName() : STR("<null>"),
                pawn ? pawn->GetFullName() : STR("<null>"),
                component ? component->GetFullName() : STR("<null>"),
                mesh ? mesh->GetFullName() : STR("<null>"),
                m_state.last_failure);
        }

        auto run_lab_probe_mapping() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            auto viewport = get_viewport_info(controller);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};
            if (!frame && pawn)
            {
                frame = make_projection_frame(pawn, 0.0, 0.0);
            }
            auto hit = find_lab_hit_sample(component, pawn, mesh, controller, viewport);
            if (!hit.ok || !frame)
            {
                m_state.success = 0;
                m_state.failures = 1;
                m_state.last_failure = !frame ? STR("lab_mapping_frame_unavailable") : hit.failure;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} probe_mapping result=failed source=cpp_bridge no_paint=1 hit_success=0 viewport_size={}x{} viewport_fallback={} component={} pawn={} controller={} mesh={} failure={}\n"),
                    LabTag,
                    viewport.width,
                    viewport.height,
                    viewport.fallback ? 1 : 0,
                    component ? component->GetFullName() : STR("<null>"),
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    mesh ? mesh->GetFullName() : STR("<null>"),
                    m_state.last_failure);
                return;
            }

            const auto projected = project_world_location_to_screen(controller, hit.sample.world_position, false);
            double roundtrip_error = -1.0;
            StringType coord_mode = STR("project_failed");
            if (projected.ok)
            {
                const auto dx = projected.x - hit.sample.screen_x;
                const auto dy = projected.y - hit.sample.screen_y;
                roundtrip_error = std::sqrt(dx * dx + dy * dy);
                coord_mode = roundtrip_error <= 2.0 ? STR("viewport_pixels") : STR("viewport_pixels_mismatch");
            }

            const auto traced = trace_nearest_background_behind_sample(pawn, frame->eye, hit.sample);
            auto* material = traced.hit ? call_number_return_object(traced.trace.component, STR("GetMaterial"), 0.0) : nullptr;
            m_state.success = hit.ok ? 1 : 0;
            m_state.failures = hit.ok ? 0 : 1;
            m_state.visible_samples = hit.ok ? 1 : 0;
            m_state.uv_hits = hit.ok ? 1 : 0;
            m_state.body_trace_hits = hit.ok ? 1 : 0;
            m_state.background_trace_hits = traced.hit ? 1 : 0;
            m_state.last_failure = hit.ok ? STR("lab_probe_mapping_ready_no_paint") : STR("lab_probe_mapping_failed");

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} probe_mapping result=ok source=cpp_bridge no_paint=1 sample_index={} screen=({}, {}) normalized=({}, {}) hit_uv=({}, {}) hit_world=({}, {}, {}) hit_normal=({}, {}, {}) projected_screen=({}, {}) project_ok={} project_failure={} roundtrip_px_error={} screen_coord_mode={} background_hit={} background_actor={} background_component={} background_material={} background_world=({}, {}, {}) background_distance={} floor_like={} trace_self_skips={} trace_channel_attempts={} camera_location=({}, {}, {}) viewport_size={}x{} viewport_fallback={} component={} mesh={} controller={}\n"),
                LabTag,
                hit.sample_index,
                hit.sample.screen_x,
                hit.sample.screen_y,
                hit.sample.nx,
                hit.sample.ny,
                hit.sample.u,
                hit.sample.v,
                hit.sample.world_position.X(),
                hit.sample.world_position.Y(),
                hit.sample.world_position.Z(),
                hit.sample.normal.X(),
                hit.sample.normal.Y(),
                hit.sample.normal.Z(),
                projected.ok ? projected.x : 0.0,
                projected.ok ? projected.y : 0.0,
                projected.ok ? 1 : 0,
                projected.failure.empty() ? STR("<none>") : projected.failure,
                roundtrip_error,
                coord_mode,
                traced.hit ? 1 : 0,
                traced.trace.actor ? traced.trace.actor->GetFullName() : STR("<null>"),
                traced.trace.component ? traced.trace.component->GetFullName() : STR("<null>"),
                material ? material->GetFullName() : STR("<null>"),
                traced.hit ? traced.trace.location.X() : 0.0,
                traced.hit ? traced.trace.location.Y() : 0.0,
                traced.hit ? traced.trace.location.Z() : 0.0,
                traced.hit ? traced.distance : -1.0,
                traced.floor_like ? 1 : 0,
                traced.self_skips,
                traced.channel_attempts,
                frame->eye.X(),
                frame->eye.Y(),
                frame->eye.Z(),
                viewport.width,
                viewport.height,
                viewport.fallback ? 1 : 0,
                component ? component->GetFullName() : STR("<null>"),
                mesh ? mesh->GetFullName() : STR("<null>"),
                controller ? controller->GetFullName() : STR("<null>"));
        }

        auto run_lab_probe_capture() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            auto viewport = get_viewport_info(controller);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};
            if (!frame && pawn)
            {
                frame = make_projection_frame(pawn, 0.0, 0.0);
            }
            auto hit = find_lab_hit_sample(component, pawn, mesh, controller, viewport);
            if (!frame)
            {
                m_state.success = 0;
                m_state.failures = 1;
                m_state.last_failure = STR("lab_capture_frame_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} probe_capture result=failed source=cpp_bridge no_paint=1 capture_rgb=(nil) error={} pawn={} controller={}\n"),
                    LabTag,
                    m_state.last_failure,
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"));
                return;
            }
            ScreenHitSample sample{};
            sample.nx = hit.ok ? hit.sample.nx : 0.5;
            sample.ny = hit.ok ? hit.sample.ny : 0.5;
            sample.screen_x = sample.nx * static_cast<double>(std::max(1, viewport.width));
            sample.screen_y = sample.ny * static_cast<double>(std::max(1, viewport.height));
            if (hit.ok)
            {
                sample = hit.sample;
            }

            const auto rt_width = 512;
            const auto rt_height = std::max(256,
                                            static_cast<int>(std::round(static_cast<double>(rt_width) *
                                                                       static_cast<double>(std::max(1, viewport.height)) /
                                                                       static_cast<double>(std::max(1, viewport.width)))));
            std::vector<ScreenHitSample> samples{sample};
            CaptureGridDiagnostics diag{};
            const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
            auto colors = capture_screen_sample_colors(pawn,
                                                       frame->eye,
                                                       look_at,
                                                       samples,
                                                       rt_width,
                                                       rt_height,
                                                       true,
                                                       m_state,
                                                       frame->fov_degrees,
                                                       &diag,
                                                       ScreenTransform{},
                                                       frame->has_rotation ? &frame->rotation : nullptr);
            const auto color = !colors.empty() ? colors.front() : std::optional<Color>{};
            m_state.success = color ? 1 : 0;
            m_state.failures = color ? 0 : 1;
            m_state.capture_pixels_ready = color.has_value();
            m_state.background_pixels = color ? 1 : 0;
            m_state.last_failure = color ? STR("lab_probe_capture_ready_no_paint") : STR("lab_probe_capture_failed");

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} probe_capture result={} source=cpp_bridge no_paint=1 no_import=1 no_clear=1 no_commit=1 capture_rgb=({}, {}, {}) capture_a=1 rt_size={}x{} screen=({}, {}) normalized=({}, {}) hit_available={} fov={} fov_source={} camera_location=({}, {}, {}) scene_capture_class={} render_target={} capture_actor={} capture_component={} texture_target_written={} capture_scene_called={} read_pixels={} missing_pixels={} raw_attempts={} raw_success={} pixel_attempts={} pixel_success={} first_read_function={} first_read_struct={} error={}\n"),
                LabTag,
                color ? STR("ok") : STR("failed"),
                color ? color->r : 0.0,
                color ? color->g : 0.0,
                color ? color->b : 0.0,
                rt_width,
                rt_height,
                sample.screen_x,
                sample.screen_y,
                sample.nx,
                sample.ny,
                hit.ok ? 1 : 0,
                frame->fov_degrees,
                frame->fov_source.empty() ? STR("<none>") : frame->fov_source,
                frame->eye.X(),
                frame->eye.Y(),
                frame->eye.Z(),
                diag.scene_capture_class ? 1 : 0,
                diag.render_target ? 1 : 0,
                diag.capture_actor ? 1 : 0,
                diag.capture_component ? 1 : 0,
                diag.texture_target_written ? 1 : 0,
                diag.capture_scene_called ? 1 : 0,
                diag.read_pixels,
                diag.missing_pixels,
                diag.read.raw_attempts,
                diag.read.raw_success,
                diag.read.pixel_attempts,
                diag.read.pixel_success,
                diag.read.first_function.empty() ? STR("<none>") : diag.read.first_function,
                diag.read.first_struct.empty() ? STR("<none>") : diag.read.first_struct,
                m_state.last_failure);
        }

        auto run_lab_probe_grid() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            auto viewport = get_viewport_info(controller);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};
            if (!frame && pawn)
            {
                frame = make_projection_frame(pawn, 0.0, 0.0);
            }

            auto hits = collect_lab_hit_samples(component, pawn, mesh, controller, viewport);
            if (!frame || hits.empty())
            {
                m_state.success = 0;
                m_state.failures = 1;
                m_state.last_failure = !frame ? STR("lab_grid_frame_unavailable") : STR("lab_grid_hit_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} probe_grid result=failed source=cpp_bridge no_paint=1 hit_count={} viewport_size={}x{} viewport_fallback={} component={} pawn={} controller={} mesh={} failure={}\n"),
                    LabTag,
                    static_cast<int>(hits.size()),
                    viewport.width,
                    viewport.height,
                    viewport.fallback ? 1 : 0,
                    component ? component->GetFullName() : STR("<null>"),
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    mesh ? mesh->GetFullName() : STR("<null>"),
                    m_state.last_failure);
                return;
            }

            std::vector<ScreenHitSample> samples{};
            samples.reserve(hits.size());
            for (const auto& hit : hits)
            {
                samples.push_back(hit.sample);
            }

            const auto rt_width = 1024;
            const auto rt_height = std::max(256,
                                            static_cast<int>(std::round(static_cast<double>(rt_width) *
                                                                       static_cast<double>(std::max(1, viewport.height)) /
                                                                       static_cast<double>(std::max(1, viewport.width)))));
            CaptureGridDiagnostics diag{};
            const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
            auto colors = capture_screen_sample_colors(pawn,
                                                       frame->eye,
                                                       look_at,
                                                       samples,
                                                       rt_width,
                                                       rt_height,
                                                       true,
                                                       m_state,
                                                       frame->fov_degrees,
                                                       &diag,
                                                       ScreenTransform{},
                                                       frame->has_rotation ? &frame->rotation : nullptr);

            int projected_ok = 0;
            int background_ok = 0;
            int floor_like_count = 0;
            int blackout_count = 0;
            int capture_ok = 0;
            double roundtrip_sum = 0.0;
            double roundtrip_max = 0.0;
            double distance_min = 1.0e30;
            double distance_sum = 0.0;
            double distance_max = 0.0;
            double min_r = 1.0;
            double min_g = 1.0;
            double min_b = 1.0;
            double sum_r = 0.0;
            double sum_g = 0.0;
            double sum_b = 0.0;
            double max_r = 0.0;
            double max_g = 0.0;
            double max_b = 0.0;

            for (size_t i = 0; i < hits.size(); ++i)
            {
                const auto& hit = hits[i];
                const auto projected = project_world_location_to_screen(controller, hit.sample.world_position, false);
                double roundtrip_error = -1.0;
                if (projected.ok)
                {
                    const auto dx = projected.x - hit.sample.screen_x;
                    const auto dy = projected.y - hit.sample.screen_y;
                    roundtrip_error = std::sqrt(dx * dx + dy * dy);
                    ++projected_ok;
                    roundtrip_sum += roundtrip_error;
                    roundtrip_max = std::max(roundtrip_max, roundtrip_error);
                }

                const auto traced = trace_nearest_background_behind_sample(pawn, frame->eye, hit.sample);
                auto* material = traced.hit ? call_number_return_object(traced.trace.component, STR("GetMaterial"), 0.0) : nullptr;
                const auto material_name = material ? material->GetFullName() : STR("<null>");
                if (traced.hit)
                {
                    ++background_ok;
                    distance_min = std::min(distance_min, traced.distance);
                    distance_sum += traced.distance;
                    distance_max = std::max(distance_max, traced.distance);
                    if (traced.floor_like)
                    {
                        ++floor_like_count;
                    }
                    if (contains_text(lower_copy(material_name), STR("blackout")))
                    {
                        ++blackout_count;
                    }
                }

                const bool has_color = i < colors.size() && colors[i].has_value();
                Color color{};
                if (has_color)
                {
                    color = *colors[i];
                    ++capture_ok;
                    min_r = std::min(min_r, color.r);
                    min_g = std::min(min_g, color.g);
                    min_b = std::min(min_b, color.b);
                    sum_r += color.r;
                    sum_g += color.g;
                    sum_b += color.b;
                    max_r = std::max(max_r, color.r);
                    max_g = std::max(max_g, color.g);
                    max_b = std::max(max_b, color.b);
                }

                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} probe_grid_sample source=cpp_bridge no_paint=1 index={} screen=({}, {}) normalized=({}, {}) hit_uv=({}, {}) hit_world=({}, {}, {}) hit_normal=({}, {}, {}) project_ok={} projected_screen=({}, {}) roundtrip_px_error={} background_hit={} background_material={} background_distance={} floor_like={} capture_ok={} capture_rgb=({}, {}, {})\n"),
                    LabTag,
                    hit.sample_index,
                    hit.sample.screen_x,
                    hit.sample.screen_y,
                    hit.sample.nx,
                    hit.sample.ny,
                    hit.sample.u,
                    hit.sample.v,
                    hit.sample.world_position.X(),
                    hit.sample.world_position.Y(),
                    hit.sample.world_position.Z(),
                    hit.sample.normal.X(),
                    hit.sample.normal.Y(),
                    hit.sample.normal.Z(),
                    projected.ok ? 1 : 0,
                    projected.ok ? projected.x : 0.0,
                    projected.ok ? projected.y : 0.0,
                    roundtrip_error,
                    traced.hit ? 1 : 0,
                    material_name,
                    traced.hit ? traced.distance : -1.0,
                    traced.floor_like ? 1 : 0,
                    has_color ? 1 : 0,
                    has_color ? color.r : 0.0,
                    has_color ? color.g : 0.0,
                    has_color ? color.b : 0.0);
            }

            const auto hit_count = static_cast<int>(hits.size());
            const auto roundtrip_avg = projected_ok > 0 ? roundtrip_sum / static_cast<double>(projected_ok) : -1.0;
            const auto distance_avg = background_ok > 0 ? distance_sum / static_cast<double>(background_ok) : -1.0;
            const auto capture_avg_r = capture_ok > 0 ? sum_r / static_cast<double>(capture_ok) : 0.0;
            const auto capture_avg_g = capture_ok > 0 ? sum_g / static_cast<double>(capture_ok) : 0.0;
            const auto capture_avg_b = capture_ok > 0 ? sum_b / static_cast<double>(capture_ok) : 0.0;
            m_state.success = hit_count > 0 ? 1 : 0;
            m_state.failures = hit_count > 0 ? 0 : 1;
            m_state.visible_samples = hit_count;
            m_state.uv_hits = hit_count;
            m_state.body_trace_hits = hit_count;
            m_state.background_trace_hits = background_ok;
            m_state.capture_pixels_ready = capture_ok > 0;
            m_state.background_pixels = capture_ok;
            m_state.last_failure = hit_count > 0 ? STR("lab_probe_grid_ready_no_paint") : STR("lab_probe_grid_failed");

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} probe_grid_summary result=ok source=cpp_bridge version={} no_paint=1 no_import=1 no_clear=1 no_commit=1 viewport_size={}x{} hit_count={} projected_ok={} roundtrip_avg={} roundtrip_max={} background_ok={} floor_like_count={} blackout_material_count={} background_distance_min={} background_distance_avg={} background_distance_max={} capture_ok={} rt_size={}x{} capture_rgb_min=({}, {}, {}) capture_rgb_avg=({}, {}, {}) capture_rgb_max=({}, {}, {}) fov={} fov_source={} camera_location=({}, {}, {}) scene_capture_class={} render_target={} capture_actor={} capture_component={} texture_target_written={} capture_scene_called={} read_pixels={} missing_pixels={} raw_attempts={} raw_success={} error={}\n"),
                LabTag,
                ModVersion,
                viewport.width,
                viewport.height,
                hit_count,
                projected_ok,
                roundtrip_avg,
                roundtrip_max,
                background_ok,
                floor_like_count,
                blackout_count,
                background_ok > 0 ? distance_min : -1.0,
                distance_avg,
                background_ok > 0 ? distance_max : -1.0,
                capture_ok,
                rt_width,
                rt_height,
                capture_ok > 0 ? min_r : 0.0,
                capture_ok > 0 ? min_g : 0.0,
                capture_ok > 0 ? min_b : 0.0,
                capture_avg_r,
                capture_avg_g,
                capture_avg_b,
                capture_ok > 0 ? max_r : 0.0,
                capture_ok > 0 ? max_g : 0.0,
                capture_ok > 0 ? max_b : 0.0,
                frame->fov_degrees,
                frame->fov_source.empty() ? STR("<none>") : frame->fov_source,
                frame->eye.X(),
                frame->eye.Y(),
                frame->eye.Z(),
                diag.scene_capture_class ? 1 : 0,
                diag.render_target ? 1 : 0,
                diag.capture_actor ? 1 : 0,
                diag.capture_component ? 1 : 0,
                diag.texture_target_written ? 1 : 0,
                diag.capture_scene_called ? 1 : 0,
                diag.read_pixels,
                diag.missing_pixels,
                diag.read.raw_attempts,
                diag.read.raw_success,
                m_state.last_failure);
        }

        auto run_lab_probe_visibility() -> void
        {
            run_dependency_probe(true);
            auto* pawn = find_player_pawn();
            auto* controller = find_player_controller_for_pawn(pawn);
            auto* component = pawn ? find_runtime_paint_component_for(pawn) : find_runtime_paint_object_with_uv();
            auto* mesh = find_target_mesh_for_runtime_paint(component, pawn);
            auto viewport = get_viewport_info(controller);
            auto frame = controller ? make_projection_frame_from_deproject(controller, viewport, 0.0, 0.0)
                                    : std::optional<ProjectionFrame>{};
            if (!frame && pawn)
            {
                frame = make_projection_frame(pawn, 0.0, 0.0);
            }

            auto hits = collect_lab_hit_samples(component, pawn, mesh, controller, viewport);
            if (!frame || hits.empty())
            {
                m_state.success = 0;
                m_state.failures = 1;
                m_state.last_failure = !frame ? STR("lab_visibility_frame_unavailable") : STR("lab_visibility_hit_unavailable");
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} probe_visibility result=failed source=cpp_bridge no_paint=1 hit_count={} viewport_size={}x{} component={} pawn={} controller={} mesh={} failure={}\n"),
                    LabTag,
                    static_cast<int>(hits.size()),
                    viewport.width,
                    viewport.height,
                    component ? component->GetFullName() : STR("<null>"),
                    pawn ? pawn->GetFullName() : STR("<null>"),
                    controller ? controller->GetFullName() : STR("<null>"),
                    mesh ? mesh->GetFullName() : STR("<null>"),
                    m_state.last_failure);
                return;
            }

            std::vector<ScreenHitSample> samples{};
            samples.reserve(hits.size());
            for (const auto& hit : hits)
            {
                samples.push_back(hit.sample);
            }

            const auto rt_width = 1024;
            const auto rt_height = std::max(256,
                                            static_cast<int>(std::round(static_cast<double>(rt_width) *
                                                                       static_cast<double>(std::max(1, viewport.height)) /
                                                                       static_cast<double>(std::max(1, viewport.width)))));
            const auto look_at = add(frame->eye, mul(frame->forward, 1000.0));
            CaptureGridDiagnostics visible_diag{};
            CaptureGridDiagnostics hidden_diag{};
            auto visible_colors = capture_screen_sample_colors(pawn,
                                                               frame->eye,
                                                               look_at,
                                                               samples,
                                                               rt_width,
                                                               rt_height,
                                                               false,
                                                               m_state,
                                                               frame->fov_degrees,
                                                               &visible_diag,
                                                               ScreenTransform{},
                                                               frame->has_rotation ? &frame->rotation : nullptr);
            auto hidden_colors = capture_screen_sample_colors(pawn,
                                                              frame->eye,
                                                              look_at,
                                                              samples,
                                                              rt_width,
                                                              rt_height,
                                                              true,
                                                              m_state,
                                                              frame->fov_degrees,
                                                              &hidden_diag,
                                                              ScreenTransform{},
                                                              frame->has_rotation ? &frame->rotation : nullptr);

            int visible_ok = 0;
            int hidden_ok = 0;
            int trace_offset_hits = 0;
            int trace_offset_blackout = 0;
            double delta_sum = 0.0;
            double delta_max = 0.0;
            const std::array<double, 6> offsets{{1.0, 4.0, 12.0, 28.0, 64.0, 160.0}};

            for (size_t i = 0; i < hits.size(); ++i)
            {
                const auto& hit = hits[i];
                const bool has_visible = i < visible_colors.size() && visible_colors[i].has_value();
                const bool has_hidden = i < hidden_colors.size() && hidden_colors[i].has_value();
                Color visible{};
                Color hidden{};
                if (has_visible)
                {
                    visible = *visible_colors[i];
                    ++visible_ok;
                }
                if (has_hidden)
                {
                    hidden = *hidden_colors[i];
                    ++hidden_ok;
                }
                const auto delta = has_visible && has_hidden
                                       ? std::abs(visible.r - hidden.r) + std::abs(visible.g - hidden.g) + std::abs(visible.b - hidden.b)
                                       : -1.0;
                if (delta >= 0.0)
                {
                    delta_sum += delta;
                    delta_max = std::max(delta_max, delta);
                }

                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} probe_visibility_sample source=cpp_bridge no_paint=1 index={} screen=({}, {}) normalized=({}, {}) hit_uv=({}, {}) visible_ok={} visible_rgb=({}, {}, {}) hidden_ok={} hidden_rgb=({}, {}, {}) visible_hidden_delta={}\n"),
                    LabTag,
                    hit.sample_index,
                    hit.sample.screen_x,
                    hit.sample.screen_y,
                    hit.sample.nx,
                    hit.sample.ny,
                    hit.sample.u,
                    hit.sample.v,
                    has_visible ? 1 : 0,
                    has_visible ? visible.r : 0.0,
                    has_visible ? visible.g : 0.0,
                    has_visible ? visible.b : 0.0,
                    has_hidden ? 1 : 0,
                    has_hidden ? hidden.r : 0.0,
                    has_hidden ? hidden.g : 0.0,
                    has_hidden ? hidden.b : 0.0,
                    delta);

                for (const auto offset : offsets)
                {
                    const auto traced = trace_nearest_background_behind_sample(pawn, frame->eye, hit.sample, offset);
                    auto* material = traced.hit ? call_number_return_object(traced.trace.component, STR("GetMaterial"), 0.0) : nullptr;
                    const auto material_name = material ? material->GetFullName() : STR("<null>");
                    if (traced.hit)
                    {
                        ++trace_offset_hits;
                        if (contains_text(lower_copy(material_name), STR("blackout")))
                        {
                            ++trace_offset_blackout;
                        }
                    }
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("{} probe_trace_offset_sample source=cpp_bridge no_paint=1 index={} offset={} background_hit={} background_material={} background_distance={} background_world=({}, {}, {}) floor_like={} self_skips={} channel_attempts={}\n"),
                        LabTag,
                        hit.sample_index,
                        offset,
                        traced.hit ? 1 : 0,
                        material_name,
                        traced.hit ? traced.distance : -1.0,
                        traced.hit ? traced.trace.location.X() : 0.0,
                        traced.hit ? traced.trace.location.Y() : 0.0,
                        traced.hit ? traced.trace.location.Z() : 0.0,
                        traced.floor_like ? 1 : 0,
                        traced.self_skips,
                        traced.channel_attempts);
                }
            }

            const auto hit_count = static_cast<int>(hits.size());
            const auto pair_count = std::min(visible_ok, hidden_ok);
            const auto delta_avg = pair_count > 0 ? delta_sum / static_cast<double>(pair_count) : -1.0;
            m_state.success = 1;
            m_state.failures = 0;
            m_state.visible_samples = hit_count;
            m_state.uv_hits = hit_count;
            m_state.body_trace_hits = hit_count;
            m_state.background_trace_hits = trace_offset_hits;
            m_state.capture_pixels_ready = visible_ok > 0 || hidden_ok > 0;
            m_state.background_pixels = hidden_ok;
            m_state.last_failure = STR("lab_probe_visibility_ready_no_paint");

            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} probe_visibility_summary result=ok source=cpp_bridge version={} no_paint=1 no_import=1 no_clear=1 no_commit=1 viewport_size={}x{} hit_count={} visible_ok={} hidden_ok={} visible_hidden_delta_avg={} visible_hidden_delta_max={} trace_offset_hits={} trace_offset_blackout={} rt_size={}x{} visible_read_pixels={} visible_missing_pixels={} hidden_read_pixels={} hidden_missing_pixels={} fov={} fov_source={} camera_location=({}, {}, {}) error={}\n"),
                LabTag,
                ModVersion,
                viewport.width,
                viewport.height,
                hit_count,
                visible_ok,
                hidden_ok,
                delta_avg,
                delta_max,
                trace_offset_hits,
                trace_offset_blackout,
                rt_width,
                rt_height,
                visible_diag.read_pixels,
                visible_diag.missing_pixels,
                hidden_diag.read_pixels,
                hidden_diag.missing_pixels,
                frame->fov_degrees,
                frame->fov_source.empty() ? STR("<none>") : frame->fov_source,
                frame->eye.X(),
                frame->eye.Y(),
                frame->eye.Z(),
                m_state.last_failure);
        }

        auto print_status() const -> void
        {
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} status version={} loaded=1 unreal_initialized={} command_hook={} play_id={} capture_funcs={} capture_pixels={} trace={} uv_actuator={} uv_mapping={} paint={} official_pipeline={} pipeline_props={} pipeline_funcs={} render_targets={} commit_sync={} commit_called={} visible_backend={} backend={} channel={} paint_capture={} views={} visible_samples={} body_hits={} background_hits={} uv_hits={} background_pixels={} atlas_bins={} queued_strokes={} success={} failures={} world_paint={} uv_paint={} paint_hash_before={} paint_hash_after={} ready={} overlay_active={} overlay_hook={} overlay_rt={}x{} overlay_mask_pixels={} overlay_runs={} overlay_hud_frames={} overlay_last_draw_calls={} world={} pawn={} component={} last_failure={}\n"),
                ModTag,
                ModVersion,
                m_state.unreal_initialized ? 1 : 0,
                m_state.command_hook_installed ? 1 : 0,
                m_state.play_id,
                m_state.scene_capture_functions,
                m_state.capture_pixels_ready ? 1 : 0,
                m_state.trace_path_available ? 1 : 0,
                m_state.uv_path_available ? 1 : 0,
                m_state.uv_mapping_ready ? 1 : 0,
                m_state.paint_path_available ? 1 : 0,
                m_state.official_paint_pipeline_ready ? 1 : 0,
                m_state.pipeline_property_candidates,
                m_state.pipeline_function_candidates,
                m_state.render_target_candidates,
                m_state.commit_sync_candidates,
                m_state.commit_calls,
                m_state.verified_visible_backend ? 1 : 0,
                m_state.verified_paint_function.empty() ? STR("<unverified>") : m_state.verified_paint_function,
                m_state.verified_paint_channel,
                m_state.paint_capture_enabled ? 1 : 0,
                m_state.views,
                m_state.visible_samples,
                m_state.body_trace_hits,
                m_state.background_trace_hits,
                m_state.uv_hits,
                m_state.background_pixels,
                m_state.atlas_bins,
                m_state.queued_strokes,
                m_state.success,
                m_state.failures,
                m_state.paint_world_success,
                m_state.paint_uv_success,
                m_state.paint_state_hash_before,
                m_state.paint_state_hash_after,
                quality_gate_ready() ? 1 : 0,
                m_cloak.active ? 1 : 0,
                m_cloak.hook_installed ? 1 : 0,
                m_cloak.rt_width,
                m_cloak.rt_height,
                m_cloak.mask_pixels,
                m_cloak.mask_runs,
                m_cloak.hud_frames,
                m_cloak.last_frame_draw_calls,
                m_state.current_world.empty() ? STR("<null>") : m_state.current_world,
                m_state.current_pawn.empty() ? STR("<null>") : m_state.current_pawn,
                m_state.current_component.empty() ? STR("<null>") : m_state.current_component,
                m_state.last_failure);
        }

        auto dump_struct_layout(Unreal::UStruct* structure, const StringType& prefix, int depth) const -> void
        {
            if (!structure || depth > 2)
            {
                return;
            }
            int printed = 0;
            for (auto* property : structure->ForEachProperty())
            {
                if (!property || printed >= 48)
                {
                    continue;
                }
                ++printed;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{}   struct_field {}{} type={} offset={} size={} flags={}\n"),
                    ModTag,
                    prefix,
                    property->GetName(),
                    prop_type_name(property),
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    static_cast<uint64_t>(property->GetPropertyFlags()));
                if (auto* nested = Unreal::CastField<Unreal::FStructProperty>(property))
                {
                    dump_struct_layout(struct_type(nested), prefix + property->GetName() + STR("."), depth + 1);
                }
            }
        }

        auto dump_function_signature(Unreal::UObject* object, const CharType* name, bool verbose) -> void
        {
            auto* function = object ? object->GetFunctionByNameInChain(name) : nullptr;
            if (!function)
            {
                return;
            }
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} sig {} parms_size={} num_parms={}\n"),
                ModTag,
                function->GetFullName(),
                function->GetParmsSize(),
                static_cast<int>(function->GetNumParms()));
            if (!verbose)
            {
                return;
            }
            for (auto* property : function->ForEachProperty())
            {
                if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm))
                {
                    continue;
                }
                auto extra = StringType{};
                if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                {
                    extra = STR(" struct=") + struct_type(struct_prop)->GetName();
                }
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{}   param name={} type={} offset={} size={} flags={}{}\n"),
                    ModTag,
                    property->GetName(),
                    prop_type_name(property),
                    property->GetOffset_Internal(),
                    property->GetElementSize(),
                    static_cast<uint64_t>(property->GetPropertyFlags()),
                    extra);
                if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                {
                    dump_struct_layout(struct_type(struct_prop), property->GetName() + STR("."), 0);
                }
            }
        }

        auto dump_core_signatures(bool verbose) -> void
        {
            auto* paint_object = find_runtime_paint_object_with_uv();
            if (paint_object)
            {
                dump_function_signature(paint_object, STR("PaintAtWorldPosition"), verbose);
                dump_function_signature(paint_object, STR("PaintAtUVWithBrush"), verbose);
                dump_function_signature(paint_object, STR("PaintAtUV"), verbose);
                dump_function_signature(paint_object, STR("PaintStrokeUV"), verbose);
                dump_function_signature(paint_object, STR("PaintAtScreenPosition"), verbose);
                dump_function_signature(paint_object, STR("HitTestAtScreenPosition"), verbose);
                dump_function_signature(paint_object, STR("ClearAllChannels"), verbose);
                dump_function_signature(paint_object, STR("ClearChannel"), verbose);
            }
            auto* trace = find_function(STR("/Script/Engine.KismetSystemLibrary:LineTraceSingle"));
            if (trace)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("{} sig {} parms_size={} num_parms={}\n"),
                    ModTag,
                    trace->GetFullName(),
                    trace->GetParmsSize(),
                    static_cast<int>(trace->GetNumParms()));
            }
        }
    };
}

#define MECCHA_CAMO_CORE_API __declspec(dllexport)
extern "C"
{
    MECCHA_CAMO_CORE_API RC::CppUserModBase* start_mod()
    {
        return new MecchaCamouflageMod();
    }

    MECCHA_CAMO_CORE_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
