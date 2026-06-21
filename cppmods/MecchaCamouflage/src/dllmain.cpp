#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
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

#include "MecchaCamouflage/core/paint_core.hpp"
#include "MecchaCamouflage/diagnostics/artifact_writer.hpp"
#include "MecchaCamouflage/diagnostics/status_writer.hpp"

namespace
{
    using RC::CharType;
    using RC::StringType;
    namespace Unreal = RC::Unreal;

    constexpr auto ModTag = STR("[MecchaCamouflage]");
    constexpr double Pi = 3.14159265358979323846;
    constexpr int PaintChannelAlbedoMetallicRoughness = 5;
    constexpr int PaintChannelUnknown = -1;
    constexpr int ProjectionCaptureResolution = 1024;
    constexpr int ScreenProjectionGridX = 54;
    constexpr int ScreenProjectionGridY = 30;
    constexpr int MinQualityScreenHitUvSamples = 2048;
    constexpr int PixelAlignmentSampleLimit = 96;
    constexpr int SceneCaptureRenderTargetFormatRgba16f = 6;
    constexpr int SceneCaptureSourceFinalColorLdr = 2;

    struct Color
    {
        double r{0.42};
        double g{0.42};
        double b{0.36};
        double roughness{0.92};
        double metallic{0.0};
    };

    enum class BulkColorTransform
    {
        Identity,
        SwapRedBlue,
        SrgbToLinear,
        LinearToSrgb,
        SwapRedBlueSrgbToLinear,
        SwapRedBlueLinearToSrgb,
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
        StringType bool_variant{};
        StringType color_transform_backend{STR("identity")};
        StringType failure{STR("not_run")};
        ScreenTransform bulk_to_pixel_transform{};
        bool bulk_calibration_ok{false};
        int bulk_calibration_samples{0};
        int bulk_calibration_pairs{0};
        double bulk_calibration_best_median{0.0};
        double bulk_calibration_runner_up_median{0.0};
        StringType bulk_calibration_backend{STR("not_run")};
        std::vector<StringType> bulk_calibration_candidates{};
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
        bool actor_hidden_guard_active{false};
        bool target_mesh_hidden_guard_active{false};
        bool hide_target_component_called{false};
        bool actor_rotation_set_called{false};
        bool component_rotation_set_called{false};
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
    auto read_render_target_image_candidates(Unreal::UObject* world_context,
                                             Unreal::UObject* render_target,
                                             int width,
                                             int height) -> std::vector<RenderTargetImage>;
    auto get_kismet_rendering_library() -> Unreal::UObject*
    {
        return Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetRenderingLibrary"));
    }

    auto get_kismet_system_library() -> Unreal::UObject*
    {
        return Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
    }

    auto get_gameplay_statics() -> Unreal::UObject*
    {
        return Unreal::UObjectGlobals::StaticFindObject<Unreal::UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__GameplayStatics"));
    }

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
    auto transform_screen_coord(double value, double scale, double offset, bool flip, double pivot) -> double;
    auto effective_capture_coord(double capture_value, double fallback_value) -> double;
    auto read_bool_property_by_name(Unreal::UObject* object, const CharType* property_name) -> std::optional<bool>;
    auto write_bool_property_by_name(Unreal::UObject* object, const CharType* property_name, bool value) -> bool;
    auto call_bool_params(Unreal::UObject* object, const CharType* function_name, std::initializer_list<bool> values) -> bool;
    auto call_rotator_bool_params(Unreal::UObject* object,
                                  const CharType* function_name,
                                  const Unreal::FRotator& rotation,
                                  std::initializer_list<bool> values) -> bool;

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

    struct ComponentVisibilityGuard
    {
        Unreal::UObject* component{nullptr};
        bool active{false};
        std::optional<bool> original_hidden{};
        std::optional<bool> original_visible{};
        bool called_hidden{false};
        bool called_visibility{false};

        explicit ComponentVisibilityGuard(Unreal::UObject* in_component) : component(in_component), active(false)
        {
            if (!component)
            {
                return;
            }
            original_hidden = read_bool_property_by_name(component, STR("bHiddenInGame"));
            original_visible = read_bool_property_by_name(component, STR("bVisible"));
            called_hidden = call_bool_params(component, STR("SetHiddenInGame"), {true, true});
            called_visibility = call_bool_params(component, STR("SetVisibility"), {false, true});
            if (!called_hidden)
            {
                called_hidden = write_bool_property_by_name(component, STR("bHiddenInGame"), true);
            }
            if (!called_visibility)
            {
                called_visibility = write_bool_property_by_name(component, STR("bVisible"), false);
            }
            active = called_hidden || called_visibility;
        }

        ~ComponentVisibilityGuard()
        {
            if (!component || !active)
            {
                return;
            }
            if (original_hidden)
            {
                if (!call_bool_params(component, STR("SetHiddenInGame"), {*original_hidden, true}))
                {
                    write_bool_property_by_name(component, STR("bHiddenInGame"), *original_hidden);
                }
            }
            else if (called_hidden)
            {
                call_bool_params(component, STR("SetHiddenInGame"), {false, true});
            }
            if (original_visible)
            {
                if (!call_bool_params(component, STR("SetVisibility"), {*original_visible, true}))
                {
                    write_bool_property_by_name(component, STR("bVisible"), *original_visible);
                }
            }
            else if (called_visibility)
            {
                call_bool_params(component, STR("SetVisibility"), {true, true});
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

    auto srgb_to_linear_component(double value) -> double
    {
        value = clamp(value, 0.0, 1.0);
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    }

    auto linear_to_srgb_component(double value) -> double
    {
        value = clamp(value, 0.0, 1.0);
        return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
    }

    auto bulk_color_transform_label(BulkColorTransform transform) -> const CharType*
    {
        switch (transform)
        {
        case BulkColorTransform::Identity:
            return STR("identity");
        case BulkColorTransform::SwapRedBlue:
            return STR("swap_rb");
        case BulkColorTransform::SrgbToLinear:
            return STR("srgb_to_linear");
        case BulkColorTransform::LinearToSrgb:
            return STR("linear_to_srgb");
        case BulkColorTransform::SwapRedBlueSrgbToLinear:
            return STR("swap_rb_srgb_to_linear");
        case BulkColorTransform::SwapRedBlueLinearToSrgb:
            return STR("swap_rb_linear_to_srgb");
        }
        return STR("unknown");
    }

    auto apply_bulk_color_transform(Color color, BulkColorTransform transform) -> Color
    {
        const auto swap_rb = [&]() {
            std::swap(color.r, color.b);
        };
        const auto srgb_to_linear = [&]() {
            color.r = srgb_to_linear_component(color.r);
            color.g = srgb_to_linear_component(color.g);
            color.b = srgb_to_linear_component(color.b);
        };
        const auto linear_to_srgb = [&]() {
            color.r = linear_to_srgb_component(color.r);
            color.g = linear_to_srgb_component(color.g);
            color.b = linear_to_srgb_component(color.b);
        };

        switch (transform)
        {
        case BulkColorTransform::Identity:
            break;
        case BulkColorTransform::SwapRedBlue:
            swap_rb();
            break;
        case BulkColorTransform::SrgbToLinear:
            srgb_to_linear();
            break;
        case BulkColorTransform::LinearToSrgb:
            linear_to_srgb();
            break;
        case BulkColorTransform::SwapRedBlueSrgbToLinear:
            swap_rb();
            srgb_to_linear();
            break;
        case BulkColorTransform::SwapRedBlueLinearToSrgb:
            swap_rb();
            linear_to_srgb();
            break;
        }
        color.r = clamp(color.r, 0.0, 1.0);
        color.g = clamp(color.g, 0.0, 1.0);
        color.b = clamp(color.b, 0.0, 1.0);
        return color;
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

    auto rotator_from_axes(const Unreal::FVector& forward,
                           const Unreal::FVector& right,
                           const Unreal::FVector& up) -> Unreal::FRotator
    {
        const auto dir = normalize(forward);
        const auto yaw = std::atan2(dir.Y(), dir.X()) * 180.0 / Pi;
        const auto xy = std::sqrt(dir.X() * dir.X() + dir.Y() * dir.Y());
        const auto pitch = std::atan2(dir.Z(), xy) * 180.0 / Pi;
        const auto cp = std::max(0.000001, std::cos(pitch * Pi / 180.0));
        const auto sr = clamp(-right.Z() / cp, -1.0, 1.0);
        const auto cr = clamp(up.Z() / cp, -1.0, 1.0);
        const auto roll = std::atan2(sr, cr) * 180.0 / Pi;
        return Unreal::FRotator{pitch, yaw, roll};
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

    auto read_object_from_struct(const Unreal::UStruct* structure, uint8_t* base, const CharType* name) -> Unreal::UObject*
    {
        auto* property = find_struct_property(structure, name);
        return property ? read_object(property, base) : nullptr;
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

    auto write_rotator(Unreal::FProperty* property, uint8_t* container, const Unreal::FRotator& value) -> bool
    {
        auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property);
        if (!struct_prop)
        {
            return false;
        }
        if (write_struct_numbers(struct_prop,
                                 container,
                                 {{STR("Pitch"), value.GetPitch()},
                                  {STR("Yaw"), value.GetYaw()},
                                  {STR("Roll"), value.GetRoll()}}))
        {
            return true;
        }
        const auto size = std::min<int32_t>(property->GetElementSize(), static_cast<int32_t>(sizeof(Unreal::FRotator)));
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

    auto hash_component_paint_state(Unreal::UObject* component) -> uint64_t
    {
        auto hash = 1469598103934665603ULL;
        if (!component)
        {
            return hash;
        }
        hash = fnv1a_update_string(hash, component->GetFullName());
        for (const auto channel : {0, 1, 2})
        {
            auto bytes = export_channel_bytes(component, channel);
            hash = fnv1a_update(hash, &channel, sizeof(channel));
            hash = fnv1a_update(hash, &bytes.width, sizeof(bytes.width));
            hash = fnv1a_update(hash, &bytes.height, sizeof(bytes.height));
            hash = fnv1a_update(hash, &bytes.hash, sizeof(bytes.hash));
            if (!bytes.failure.empty())
            {
                hash = fnv1a_update_string(hash, bytes.failure);
            }
        }
        return hash;
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

    auto call_bool_params(Unreal::UObject* object, const CharType* function_name, std::initializer_list<bool> values) -> bool
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
        auto value = values.begin();
        auto wrote = 0;
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (value == values.end())
            {
                break;
            }
            if (!Unreal::CastField<Unreal::FBoolProperty>(property) && prop_type_name(property) != STR("BoolProperty"))
            {
                continue;
            }
            if (write_bool(property, params.data(), *value))
            {
                ++wrote;
                ++value;
            }
        }
        if (wrote <= 0)
        {
            return false;
        }
        object->ProcessEvent(function, params.data());
        return true;
    }

    auto call_rotator_bool_params(Unreal::UObject* object,
                                  const CharType* function_name,
                                  const Unreal::FRotator& rotation,
                                  std::initializer_list<bool> values) -> bool
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
        bool wrote_rotation = false;
        auto bool_value = values.begin();
        for (auto* property : function->ForEachProperty())
        {
            if (!property || !property->HasAnyPropertyFlags(Unreal::CPF_Parm) ||
                property->HasAnyPropertyFlags(Unreal::CPF_ReturnParm))
            {
                continue;
            }
            if (!wrote_rotation)
            {
                if (auto* struct_prop = Unreal::CastField<Unreal::FStructProperty>(property))
                {
                    const auto* structure = struct_type(struct_prop);
                    const auto struct_name = structure ? structure->GetName() : StringType{};
                    const auto prop_name = lower_copy(property->GetName());
                    if (contains_text(struct_name, STR("Rotator")) || contains_text(prop_name, STR("rotation")))
                    {
                        wrote_rotation = write_rotator(property, params.data(), rotation);
                        continue;
                    }
                }
            }
            if (bool_value != values.end() &&
                (Unreal::CastField<Unreal::FBoolProperty>(property) || prop_type_name(property) == STR("BoolProperty")))
            {
                if (write_bool(property, params.data(), *bool_value))
                {
                    ++bool_value;
                }
            }
        }
        if (!wrote_rotation)
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
            const StringType label_text = label ? StringType{label} : StringType{};
            if (label_text == STR("raw"))
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
                const StringType label_text = label ? StringType{label} : StringType{};
                if (label_text == STR("raw"))
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

    auto read_render_target_image_candidate(Unreal::UObject* world_context,
                                            Unreal::UObject* render_target,
                                            int width,
                                            int height,
                                            const CharType* function_path,
                                            bool write_color_bool,
                                            bool color_bool_value) -> RenderTargetImage
    {
        RenderTargetImage out{};
        out.width = width;
        out.height = height;
        out.expected_pixels = width > 0 && height > 0 ? width * height : 0;
        out.bool_variant = write_color_bool ? (color_bool_value ? STR("color_bool_true") : STR("color_bool_false"))
                                            : STR("no_color_bool");
        if (!world_context || !render_target || out.expected_pixels <= 0)
        {
            out.failure = STR("render_target_image_prereq_unavailable");
            return out;
        }
        auto* self = get_kismet_rendering_library();
        auto* function = find_function(function_path);
        if (!self || !function)
        {
            out.failure = STR("bulk_read_function_unavailable");
            return out;
        }

        ++out.bulk_candidates;
        ++out.bulk_available;
        std::vector<uint8_t> params(static_cast<size_t>(function->GetParmsSize()), 0);
        std::vector<Unreal::FArrayProperty*> array_params{};
        bool had_color_bool = false;
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
                had_color_bool = true;
                if (write_color_bool)
                {
                    write_bool(property, params.data(), color_bool_value);
                }
            }
        }

        if (!had_color_bool && write_color_bool)
        {
            out.failure = STR("bulk_color_bool_unavailable");
            return out;
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
        return out;
    }

    auto read_render_target_image_candidates(Unreal::UObject* world_context,
                                             Unreal::UObject* render_target,
                                             int width,
                                             int height) -> std::vector<RenderTargetImage>
    {
        const std::array<const CharType*, 4> function_paths{
            STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTargetRaw"),
            STR("/Script/Engine.RenderingLibrary:ReadRenderTargetRaw"),
            STR("/Script/Engine.KismetRenderingLibrary:ReadRenderTarget"),
            STR("/Script/Engine.RenderingLibrary:ReadRenderTarget")};

        std::vector<RenderTargetImage> images{};
        images.reserve(function_paths.size() * 3);
        for (const auto* path : function_paths)
        {
            images.push_back(read_render_target_image_candidate(world_context, render_target, width, height, path, false, false));
            images.push_back(read_render_target_image_candidate(world_context, render_target, width, height, path, true, false));
            images.push_back(read_render_target_image_candidate(world_context, render_target, width, height, path, true, true));
        }
        return images;
    }

    auto read_render_target_image(Unreal::UObject* world_context,
                                  Unreal::UObject* render_target,
                                  int width,
                                  int height) -> RenderTargetImage
    {
        for (auto& image : read_render_target_image_candidates(world_context, render_target, width, height))
        {
            if (image.ok)
            {
                return image;
            }
        }
        RenderTargetImage out{};
        out.width = width;
        out.height = height;
        out.expected_pixels = width > 0 && height > 0 ? width * height : 0;
        out.failure = STR("bulk_read_candidate_unavailable");
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
        std::optional<ActorHiddenGuard> hidden_guard{};
        if (hide_pawn)
        {
            hidden_guard.emplace(pawn);
            result.diagnostics.actor_hidden_guard_active = hidden_guard->active;
        }
        Unreal::UObject* target_mesh = nullptr;
        std::optional<ComponentVisibilityGuard> target_mesh_guard{};
        if (hide_pawn)
        {
            if (auto* paint_component = find_runtime_paint_component_for(pawn))
            {
                target_mesh = find_target_mesh_for_runtime_paint(paint_component, pawn);
            }
            if (target_mesh && target_mesh != pawn)
            {
                target_mesh_guard.emplace(target_mesh);
                result.diagnostics.target_mesh_hidden_guard_active = target_mesh_guard->active;
            }
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
        result.diagnostics.actor_rotation_set_called =
            call_rotator_bool_params(capture_actor, STR("K2_SetActorRotation"), rotation, {false});
        result.diagnostics.component_rotation_set_called =
            call_rotator_bool_params(capture_component, STR("K2_SetWorldRotation"), rotation, {false, false});
        const auto capture_actor_rotation = call_no_params_return_rotator(capture_actor, STR("K2_GetActorRotation"));
        const auto capture_component_rotation = call_no_params_return_rotator(capture_component, STR("K2_GetComponentRotation"));
        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} scene_capture_rotation intended=({}, {}, {}) actor_set={} component_set={} actor_ok={} actor=({}, {}, {}) component_ok={} component=({}, {}, {})\n"),
            ModTag,
            rotation.GetPitch(),
            rotation.GetYaw(),
            rotation.GetRoll(),
            result.diagnostics.actor_rotation_set_called ? 1 : 0,
            result.diagnostics.component_rotation_set_called ? 1 : 0,
            capture_actor_rotation ? 1 : 0,
            capture_actor_rotation ? capture_actor_rotation->GetPitch() : 0.0,
            capture_actor_rotation ? capture_actor_rotation->GetYaw() : 0.0,
            capture_actor_rotation ? capture_actor_rotation->GetRoll() : 0.0,
            capture_component_rotation ? 1 : 0,
            capture_component_rotation ? capture_component_rotation->GetPitch() : 0.0,
            capture_component_rotation ? capture_component_rotation->GetYaw() : 0.0,
            capture_component_rotation ? capture_component_rotation->GetRoll() : 0.0);

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
        if (hide_pawn && target_mesh)
        {
            result.diagnostics.hide_target_component_called =
                call_object_param(capture_component, STR("HideComponent"), target_mesh);
        }

        const auto capture_start = SteadyClock::now();
        result.diagnostics.capture_scene_called = call_no_params(capture_component, STR("CaptureScene"));
        result.capture_ms = elapsed_ms_since(capture_start);

        const auto readback_start = SteadyClock::now();
        auto image_candidates = read_render_target_image_candidates(pawn, render_target, rt_width, rt_height);
        result.image = image_candidates.empty() ? RenderTargetImage{} : image_candidates.front();
        result.readback_ms = elapsed_ms_since(readback_start);
        if (calibration_samples && !calibration_samples->empty() && calibration_limit > 0)
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
            const std::array<Candidate, 4> screen_candidates{
                make_candidate(false, false, STR("bulk_identity")),
                make_candidate(true, false, STR("bulk_flip_x")),
                make_candidate(false, true, STR("bulk_flip_y")),
                make_candidate(true, true, STR("bulk_flip_xy"))};
            const std::array<BulkColorTransform, 6> color_candidates{
                BulkColorTransform::Identity,
                BulkColorTransform::SwapRedBlue,
                BulkColorTransform::SrgbToLinear,
                BulkColorTransform::LinearToSrgb,
                BulkColorTransform::SwapRedBlueSrgbToLinear,
                BulkColorTransform::SwapRedBlueLinearToSrgb};

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
            size_t best_image_index = static_cast<size_t>(-1);
            ScreenTransform best_screen_transform{};
            const CharType* best_screen_label = STR("not_selected");
            BulkColorTransform best_color_transform = BulkColorTransform::Identity;
            std::vector<StringType> calibration_candidate_logs{};
            for (size_t image_i = 0; image_i < image_candidates.size(); ++image_i)
            {
                const auto& image = image_candidates[image_i];
                if (!image.ok)
                {
                    continue;
                }
                for (const auto& screen_candidate : screen_candidates)
                {
                    for (const auto color_transform : color_candidates)
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
                                                                       image.width,
                                                                       image.height,
                                                                       screen_candidate.transform);
                            const auto pixel_index = static_cast<size_t>(pixel.second * image.width + pixel.first);
                            if (pixel_index >= image.pixels.size())
                            {
                                continue;
                            }
                            const auto bulk_color = apply_bulk_color_transform(image.pixels[pixel_index], color_transform);
                            distances.push_back(color_distance_rgb(*pixel_api_colors[i], bulk_color));
                        }
                        const auto pairs = static_cast<int>(distances.size());
                        const auto median = pairs > 0 ? median_value(std::move(distances)) : 1000000.0;
                        auto candidate_log = image.function_name + STR("|") + image.bool_variant + STR("|") +
                                             screen_candidate.label + STR("|") + bulk_color_transform_label(color_transform);
                        calibration_candidate_logs.push_back(candidate_log);
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("{} bulk_readback_calibration candidate={} pairs={} median_rgb_error={} image={} bool_variant={} screen_transform={} color_transform={} flip=({}, {}) scale=({}, {}) offset=({}, {})\n"),
                            ModTag,
                            candidate_log,
                            pairs,
                            median,
                            image.function_name,
                            image.bool_variant,
                            screen_candidate.label,
                            bulk_color_transform_label(color_transform),
                            screen_candidate.transform.flip_x ? 1 : 0,
                            screen_candidate.transform.flip_y ? 1 : 0,
                            screen_candidate.transform.scale_x,
                            screen_candidate.transform.scale_y,
                            screen_candidate.transform.offset_x,
                            screen_candidate.transform.offset_y);
                        if (median < best_median)
                        {
                            runner_up_median = best_median;
                            best_median = median;
                            best_pairs = pairs;
                            best_image_index = image_i;
                            best_screen_transform = screen_candidate.transform;
                            best_screen_label = screen_candidate.label;
                            best_color_transform = color_transform;
                        }
                        else if (median < runner_up_median)
                        {
                            runner_up_median = median;
                        }
                    }
                }
            }

            if (best_image_index != static_cast<size_t>(-1) && best_image_index < image_candidates.size())
            {
                result.image = image_candidates[best_image_index];
                for (auto& color : result.image.pixels)
                {
                    color = apply_bulk_color_transform(color, best_color_transform);
                }
                result.image.bulk_to_pixel_transform = best_screen_transform;
                result.image.color_transform_backend = bulk_color_transform_label(best_color_transform);
                result.image.bulk_calibration_backend = result.image.function_name + STR("|") + result.image.bool_variant +
                                                        STR("|") + best_screen_label + STR("|") +
                                                        bulk_color_transform_label(best_color_transform);
            }
            const auto separated_from_runner = runner_up_median >= 999999.0 ||
                                               best_median <= runner_up_median * 0.90 ||
                                               (runner_up_median - best_median) >= 0.012;
            result.image.bulk_calibration_samples = wanted;
            result.image.bulk_calibration_pairs = best_pairs;
            result.image.bulk_calibration_best_median = best_median < 999999.0 ? best_median : 0.0;
            result.image.bulk_calibration_runner_up_median = runner_up_median < 999999.0 ? runner_up_median : 0.0;
            result.image.bulk_calibration_candidates = std::move(calibration_candidate_logs);
            result.image.bulk_calibration_ok = best_image_index != static_cast<size_t>(-1) &&
                                               best_pairs >= std::min(16, wanted / 2) &&
                                               best_median <= 0.18 && separated_from_runner;
            RC::Output::send<RC::LogLevel::Verbose>(
                STR("{} bulk_readback_calibration selected={} ok={} samples={} pairs={} best_median={} runner_up_median={} flip=({}, {}) color_transform={} threshold=0.18 separated={} image_candidates={}\n"),
                ModTag,
                result.image.bulk_calibration_backend,
                result.image.bulk_calibration_ok ? 1 : 0,
                result.image.bulk_calibration_samples,
                result.image.bulk_calibration_pairs,
                result.image.bulk_calibration_best_median,
                result.image.bulk_calibration_runner_up_median,
                result.image.bulk_to_pixel_transform.flip_x ? 1 : 0,
                result.image.bulk_to_pixel_transform.flip_y ? 1 : 0,
                result.image.color_transform_backend,
                separated_from_runner ? 1 : 0,
                image_candidates.size());
        }

        call_no_params(capture_actor, STR("K2_DestroyActor"));
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

    auto is_floor_like_object(Unreal::UObject* actor, Unreal::UObject* component) -> bool
    {
        const auto text = lower_copy(object_name_or_empty(actor) + STR(" ") + object_name_or_empty(component));
        return contains_any_text(text,
                                 {STR("floor"),
                                  STR("ground"),
                                  STR("terrain"),
                                  STR("landscape"),
                                  STR("road"),
                                  STR("asphalt"),
                                  STR("concrete"),
                                  STR("tile"),
                                  STR("wood"),
                                  STR("grass"),
                                  STR("dirt"),
                                  STR("soil"),
                                  STR("sand"),
                                  STR("stone"),
                                  STR("rock"),
                                  STR("plane"),
                                  STR("walk"),
                                  STR("stage")});
    }

    auto luma(const Color& color) -> double
    {
        return color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722;
    }

    auto is_red_paint_artifact(const Color& color) -> bool
    {
        return color.r > 0.78 && color.g < 0.22 && color.b < 0.22;
    }

    auto infer_surface_material(Color color, bool floor_like) -> Color
    {
        if (floor_like)
        {
            color.roughness = clamp(std::max(color.roughness, 0.86), 0.86, 0.99);
            color.metallic = clamp(color.metallic, 0.0, 0.12);
            return color;
        }
        color.roughness = clamp(color.roughness <= 0.0 ? 0.92 : color.roughness, 0.35, 0.99);
        color.metallic = clamp(color.metallic, 0.0, 1.0);
        return color;
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

    auto first_vector_from_functions(Unreal::UObject* object, std::initializer_list<const CharType*> function_names)
        -> std::optional<Unreal::FVector>
    {
        for (const auto* function_name : function_names)
        {
            if (auto value = call_no_params_return_vector(object, function_name))
            {
                return value;
            }
        }
        return std::nullopt;
    }

    auto first_rotator_from_functions(Unreal::UObject* object, std::initializer_list<const CharType*> function_names)
        -> std::optional<Unreal::FRotator>
    {
        for (const auto* function_name : function_names)
        {
            if (auto value = call_no_params_return_rotator(object, function_name))
            {
                return value;
            }
        }
        return std::nullopt;
    }

    auto log_pawn_camera_component_candidates(Unreal::UObject* pawn, Unreal::UObject* camera_manager) -> void
    {
        if (!pawn)
        {
            return;
        }
        const auto pawn_name = pawn->GetFullName();
        int owned_count = 0;
        int logged = 0;
        for (const auto* class_name : {STR("CameraComponent"), STR("SpringArmComponent")})
        {
            std::vector<Unreal::UObject*> objects{};
            Unreal::UObjectGlobals::FindObjects(512, class_name, nullptr, objects, 0, 0, false);
            for (auto* object : objects)
            {
                if (!object)
                {
                    continue;
                }
                auto* owner = call_no_params_return_object(object, STR("GetOwner"));
                const auto object_name = object->GetFullName();
                const auto owned = owner == pawn || object_name.find(pawn_name) != StringType::npos;
                if (!owned)
                {
                    continue;
                }
                ++owned_count;
                if (logged >= 12)
                {
                    continue;
                }
                ++logged;
                const auto location = first_vector_from_functions(
                    object,
                    {STR("K2_GetComponentLocation"), STR("GetComponentLocation"), STR("GetWorldLocation")});
                const auto rotation = first_rotator_from_functions(
                    object,
                    {STR("K2_GetComponentRotation"), STR("GetComponentRotation"), STR("GetWorldRotation")});
                const auto forward = rotation ? rotator_forward(*rotation) : Unreal::FVector{};
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("{} camera_component_candidate class={} object={} owner={} selected_camera_manager={} loc_ok={} loc=({}, {}, {}) rot_ok={} rot=({}, {}, {}) forward=({}, {}, {})\n"),
                    ModTag,
                    class_name,
                    object_name,
                    owner ? owner->GetFullName() : STR("<null>"),
                    camera_manager ? camera_manager->GetFullName() : STR("<null>"),
                    location ? 1 : 0,
                    location ? location->X() : 0.0,
                    location ? location->Y() : 0.0,
                    location ? location->Z() : 0.0,
                    rotation ? 1 : 0,
                    rotation ? rotation->GetPitch() : 0.0,
                    rotation ? rotation->GetYaw() : 0.0,
                    rotation ? rotation->GetRoll() : 0.0,
                    rotation ? forward.X() : 0.0,
                    rotation ? forward.Y() : 0.0,
                    rotation ? forward.Z() : 0.0);
            }
        }
        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} camera_component_candidates pawn={} owned_count={} logged={}\n"),
            ModTag,
            pawn_name,
            owned_count,
            logged);
    }


    auto fov_from_deproject_sample(double center_to_sample_angle, double screen_half_fraction) -> double
    {
        if (center_to_sample_angle <= 0.0001 || screen_half_fraction <= 0.0001)
        {
            return 0.0;
        }
        return 2.0 * std::atan(std::tan(center_to_sample_angle) / screen_half_fraction);
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

        const auto deproject_forward = normalize(center.direction);
        auto deproject_right = normalize(sub(right_ray.direction, mul(deproject_forward, dot(right_ray.direction, deproject_forward))));
        auto deproject_up = normalize(sub(up_ray.direction, mul(deproject_forward, dot(up_ray.direction, deproject_forward))));
        if (length(deproject_right) < 0.01)
        {
            deproject_right = camera_rotation ? rotator_right(*camera_rotation)
                                              : normalize(cross(vec(0.0, 0.0, 1.0), deproject_forward));
        }
        if (length(deproject_up) < 0.01)
        {
            deproject_up = camera_rotation ? rotator_up(*camera_rotation)
                                           : normalize(cross(deproject_forward, deproject_right));
        }
        auto deproject_basis_up = normalize(cross(deproject_forward, deproject_right));
        if (length(deproject_basis_up) < 0.01)
        {
            deproject_basis_up = deproject_up;
        }
        if (dot(deproject_basis_up, deproject_up) < 0.0)
        {
            deproject_right = mul(deproject_right, -1.0);
            deproject_basis_up = mul(deproject_basis_up, -1.0);
        }
        deproject_right = normalize(deproject_right);
        deproject_up = normalize(deproject_basis_up);

        const auto base_forward = deproject_forward;
        auto forward = rotate_yaw_pitch(base_forward, yaw_offset, pitch_offset);
        auto right = deproject_right;
        if (length(right) < 0.01)
        {
            right = normalize(cross(vec(0.0, 0.0, 1.0), forward));
        }
        auto up = deproject_up;
        if (length(up) < 0.01)
        {
            up = normalize(cross(forward, right));
        }
        right = normalize(cross(up, forward));
        up = normalize(cross(forward, right));

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
            STR("{} deproject_frame ok viewport={}x{} eye=({}, {}, {}) camera_eye=({}, {}, {}) deproject_eye=({}, {}, {}) camera_rot_ok={} camera_rot=({}, {}, {}) forward=({}, {}, {}) deproject_forward=({}, {}, {}) right=({}, {}, {}) up=({}, {}, {}) scene_capture_fov={} fov_source={} camera_fov={} camera_fov_fallback={} fov_axis=corrected_deproject_horizontal hfov_perspective={} vfov_perspective={} hfov_from_vfov={} old_linear_hfov={} old_linear_vfov={} camera_deproject_angle_delta={} sample=({}, {})\n"),
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
            camera_rotation ? 1 : 0,
            camera_rotation ? camera_rotation->GetPitch() : 0.0,
            camera_rotation ? camera_rotation->GetYaw() : 0.0,
            camera_rotation ? camera_rotation->GetRoll() : 0.0,
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
        frame.source = STR("controller_deproject_axes_camera_eye");
        frame.rotation = rotator_from_axes(forward, right, up);
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
        log_pawn_camera_component_candidates(pawn, camera);
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
        AlignmentResult alignment{};
        alignment.backend = STR("tps_deproject_identity_alignment_disabled");
        alignment.selected_fov_degrees = frame->fov_degrees;
        alignment.runner_up_backend = STR("<disabled>");
        double alignment_ms = 0.0;
        bool mask_alignment_ok = false;
        const ScreenTransform alignment_transform{};
        const double alignment_fov = frame->fov_degrees;
        ScreenTransform capture_transform{};
        double capture_fov = frame->fov_degrees;
        StringType capture_transform_backend = STR("tps_deproject_frame");
        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} screen_body capture_alignment validator=show_only_body_mask validator_blocking=0 alignment_used=0 disabled_for_f10=1 selected_transform_backend={} selected_fov={} alignment_ok={} alignment_score={} positive_rate={} negative_rate={} positive_hits={} negative_hits={} alignment_runner_up={} alignment_runner_up_fov={} alignment_runner_up_score={} alignment_ratio={} alignment_samples={} negative_samples={} alignment_candidates={} alignment_transform_scale=({}, {}) alignment_transform_offset=({}, {}) alignment_transform_flip=({}, {}) capture_transform_backend={} capture_fov={} capture_transform_scale=({}, {}) capture_transform_offset=({}, {}) capture_transform_flip=({}, {}) alignment_rt={}x{} final_rt={}x{} fov_candidates={} t_alignment_ms={}\n"),
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
            0,
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
                STR("{} screen_body capture_alignment validator_unavailable=1 validator_blocking=0 continue_with_deproject=1 rejected_backend={} rejected_fov={} rejected_score={} positive_rate={} negative_rate={} positive_hits={} negative_hits={} trace_hits={} capture_transform_backend={} capture_fov={} capture_transform=identity\n"),
                ModTag,
                alignment.backend,
                alignment_fov,
                alignment.projection_align_score,
                alignment.mask_positive_rate,
                alignment.mask_negative_rate,
                alignment.mask_positive_hits,
                alignment.mask_negative_hits,
                trace_background_hits,
                capture_transform_backend,
                capture_fov);
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
        RC::Output::send<RC::LogLevel::Verbose>(
            STR("{} screen_body capture_filter actor_hidden_guard={} target_mesh_hidden_guard={} hide_actor_components_called={} hide_target_component_called={} actor_rotation_set={} component_rotation_set={} capture_scene_called={} texture_target_written={} capture_source_written={} capture_every_frame_written={} capture_on_movement_written={}\n"),
            ModTag,
            pixel_diag.actor_hidden_guard_active ? 1 : 0,
            pixel_diag.target_mesh_hidden_guard_active ? 1 : 0,
            pixel_diag.hide_actor_components_called ? 1 : 0,
            pixel_diag.hide_target_component_called ? 1 : 0,
            pixel_diag.actor_rotation_set_called ? 1 : 0,
            pixel_diag.component_rotation_set_called ? 1 : 0,
            pixel_diag.capture_scene_called ? 1 : 0,
            pixel_diag.texture_target_written ? 1 : 0,
            pixel_diag.capture_source_written ? 1 : 0,
            pixel_diag.capture_every_frame_written ? 1 : 0,
            pixel_diag.capture_on_movement_written ? 1 : 0);
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
            readback_backend = STR("validated_bulk");
        }
        else
        {
            readback_backend = STR("validated_bulk_failed_no_pixel_fallback");
        }
        const auto capture_ms = elapsed_ms_since(capture_start);
        const auto color_summary = summarize_capture_colors(background_colors);
        const auto color_quality = summarize_capture_quality(background_colors);
        state.background_pixels = color_summary.pixels;
        state.capture_pixels_ready = color_summary.pixels > 0 && !color_summary.uniform && !color_summary.clear_suspect;
        const auto write_debug_artifacts = [&](const char* stage,
                                               const StringType& failure,
                                               bool validation_ok,
                                               double chroma_avg,
                                               double chroma_p95,
                                               const std::vector<uint8_t>* target_albedo,
                                               int target_albedo_width,
                                               int target_albedo_height) {
#if MECCHA_CAMOUFLAGE_DIAGNOSTICS
            MecchaCamouflage::Diagnostics::RunArtifactData artifact{};
            artifact.run_id = state.play_id;
            artifact.stage = stage ? stage : "unknown";
            artifact.failure = narrow_ascii(failure);
            artifact.readback_backend = narrow_ascii(readback_backend);
            artifact.validation_ok = validation_ok;
            artifact.image_ok = image_ok;
            artifact.bulk_calibration_ok = image_bulk_calibration_ok;
            artifact.bulk_pairs = bulk_capture.image.bulk_calibration_pairs;
            artifact.bulk_best_median = bulk_capture.image.bulk_calibration_best_median;
            artifact.bulk_runner_up_median = bulk_capture.image.bulk_calibration_runner_up_median;
            artifact.capture_trace_chroma_avg = chroma_avg;
            artifact.capture_trace_chroma_p95 = chroma_p95;
            artifact.viewport_width = viewport.width;
            artifact.viewport_height = viewport.height;
            artifact.bulk_calibration_candidates.reserve(bulk_capture.image.bulk_calibration_candidates.size());
            for (const auto& candidate : bulk_capture.image.bulk_calibration_candidates)
            {
                artifact.bulk_calibration_candidates.push_back(narrow_ascii(candidate));
            }
            artifact.samples.reserve(samples.size());
            for (size_t i = 0; i < samples.size(); ++i)
            {
                MecchaCamouflage::Diagnostics::DiagnosticSample out{};
                out.screen_x = samples[i].screen_x;
                out.screen_y = samples[i].screen_y;
                out.u = samples[i].u;
                out.v = samples[i].v;
                out.world_x = samples[i].world_position.X();
                out.world_y = samples[i].world_position.Y();
                out.world_z = samples[i].world_position.Z();
                out.has_capture = i < background_colors.size() && background_colors[i].has_value();
                if (out.has_capture)
                {
                    const auto& color = *background_colors[i];
                    out.capture = MecchaCamouflage::Core::Color{color.r, color.g, color.b, color.roughness, color.metallic};
                }
                out.has_trace = i < traced_material_colors.size() && traced_material_colors[i].has_value();
                if (out.has_trace)
                {
                    const auto& color = *traced_material_colors[i];
                    out.trace = MecchaCamouflage::Core::Color{color.r, color.g, color.b, color.roughness, color.metallic};
                }
                if (out.has_capture && out.has_trace)
                {
                    out.chroma_distance = MecchaCamouflage::Core::chroma_distance_rgb(out.capture, out.trace);
                    out.rejected = out.chroma_distance > MecchaCamouflage::Core::MaxCaptureTraceChromaP95;
                }
                artifact.samples.push_back(out);
            }
            if (bulk_capture.image.ok && !bulk_capture.image.pixels.empty())
            {
                MecchaCamouflage::Diagnostics::DiagnosticImage image{};
                image.width = bulk_capture.image.width;
                image.height = bulk_capture.image.height;
                image.pixels.reserve(bulk_capture.image.pixels.size());
                for (const auto& color : bulk_capture.image.pixels)
                {
                    image.pixels.push_back(MecchaCamouflage::Core::Color{
                        color.r,
                        color.g,
                        color.b,
                        color.roughness,
                        color.metallic});
                }
                artifact.capture_preview = std::move(image);
            }
            if (target_albedo && target_albedo_width > 0 && target_albedo_height > 0)
            {
                artifact.target_albedo = MecchaCamouflage::Diagnostics::DiagnosticAlbedo{
                    target_albedo_width,
                    target_albedo_height,
                    *target_albedo};
            }
            const auto artifact_path = MecchaCamouflage::Diagnostics::write_run_artifacts(artifact);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} diagnostics_artifacts path={} stage={} validation_ok={} fallback_used=0\n"),
                ModTag,
                RC::ensure_str(artifact_path.c_str()),
                RC::ensure_str(artifact.stage.c_str()),
                validation_ok ? 1 : 0);
#else
            (void)stage;
            (void)failure;
            (void)validation_ok;
            (void)chroma_avg;
            (void)chroma_p95;
            (void)target_albedo;
            (void)target_albedo_width;
            (void)target_albedo_height;
#endif
        };
        if (!bulk_capture.image.ok || !bulk_capture.image.bulk_calibration_ok)
        {
            state.failures = 1;
            state.last_failure = STR("bulk_calibration_failed_no_paint");
            state.verified_paint_function = STR("ImportChannelFromBytes.screen_body_direct_only");
            write_debug_artifacts("bulk_validation_failed", state.last_failure, false, 0.0, 0.0, nullptr, 0, 0);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} readback_backend={} image_ok={} image_failure={} image_bulk_calibration_ok={} bulk_backend={} bulk_pairs={} bulk_best_median={} bulk_runner_up_median={} threshold={} capture_ms={} no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1 fallback_used=0 side_enabled=0\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                readback_backend,
                image_ok ? 1 : 0,
                image_failure,
                image_bulk_calibration_ok ? 1 : 0,
                bulk_capture.image.bulk_calibration_backend,
                bulk_capture.image.bulk_calibration_pairs,
                bulk_capture.image.bulk_calibration_best_median,
                bulk_capture.image.bulk_calibration_runner_up_median,
                MecchaCamouflage::Core::MaxBulkMedianRgbError,
                capture_ms);
            return false;
        }
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
        std::vector<double> capture_trace_chroma_values{};
        capture_trace_chroma_values.reserve(samples.size());
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
                    capture_trace_chroma_values.push_back(distance);
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
        const auto capture_trace_chroma_avg =
            capture_trace_pairs > 0 ? capture_trace_chroma_sum / static_cast<double>(capture_trace_pairs) : 0.0;
        const auto capture_trace_chroma_p95 = percentile_sorted(capture_trace_chroma_values, 0.95);

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

        const auto quality_decision = MecchaCamouflage::Core::validate_capture_quality(MecchaCamouflage::Core::CaptureQualityInput{
            image_ok,
            image_bulk_calibration_ok,
            color_summary.pixels,
            trace_background_hits,
            min_paint_hits,
            color_summary.uniform,
            color_summary.clear_suspect,
            bulk_capture.image.bulk_calibration_best_median,
            capture_trace_chroma_avg,
            capture_trace_chroma_p95});
        if (!quality_decision.ok)
        {
            state.failures = 1;
            state.last_failure = RC::ensure_str(quality_decision.failure.c_str());
            state.verified_paint_function = STR("ImportChannelFromBytes.screen_body_direct_only");
            write_debug_artifacts("quality_validation_failed",
                                  state.last_failure,
                                  false,
                                  capture_trace_chroma_avg,
                                  capture_trace_chroma_p95,
                                  nullptr,
                                  0,
                                  0);
            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play screen_body refused reason={} validation_failed=1 failure={} readback_backend={} bulk_best_median={} capture_trace_chroma_avg={} capture_trace_chroma_p95={} thresholds=({}, {}, {}) background_pixels={} trace_hits={} min_samples={} no_import=1 no_clear=1 no_commit=1 no_mesh_hide=1 fallback_used=0 side_enabled=0\n"),
                ModTag,
                reason.empty() ? STR("<none>") : reason,
                state.last_failure,
                readback_backend,
                bulk_capture.image.bulk_calibration_best_median,
                capture_trace_chroma_avg,
                capture_trace_chroma_p95,
                MecchaCamouflage::Core::MaxBulkMedianRgbError,
                MecchaCamouflage::Core::MaxCaptureTraceChromaAvg,
                MecchaCamouflage::Core::MaxCaptureTraceChromaP95,
                color_summary.pixels,
                trace_background_hits,
                min_paint_hits);
            return false;
        }

        const auto atlas_start = SteadyClock::now();
        std::vector<MecchaCamouflage::Core::PaintSeed> core_seeds{};
        core_seeds.reserve(paint_seeds.size());
        for (const auto& seed : paint_seeds)
        {
            core_seeds.push_back(MecchaCamouflage::Core::PaintSeed{
                seed.u,
                seed.v,
                MecchaCamouflage::Core::Color{
                    seed.color.r,
                    seed.color.g,
                    seed.color.b,
                    seed.color.roughness,
                    seed.color.metallic},
                seed.floor_like});
        }
        const auto assembled_texture = MecchaCamouflage::Core::assemble_direct_texture(
            MecchaCamouflage::Core::ChannelBuffer{albedo_before.width, albedo_before.height, albedo_before.bytes},
            MecchaCamouflage::Core::ChannelBuffer{metallic_before.width, metallic_before.height, metallic_before.bytes},
            MecchaCamouflage::Core::ChannelBuffer{roughness_before.width, roughness_before.height, roughness_before.bytes},
            core_seeds);
        auto albedo_target = assembled_texture.albedo.bytes;
        auto metallic_target = assembled_texture.metallic.bytes;
        auto roughness_target = assembled_texture.roughness.bytes;
        const auto write_stats = assembled_texture.stats;
        const auto direct_texel_count = write_stats.direct_texels;
        const auto worker_count = write_stats.worker_threads;
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
        write_debug_artifacts("pre_import",
                              STR("ok"),
                              true,
                              capture_trace_chroma_avg,
                              capture_trace_chroma_p95,
                              &albedo_target,
                              albedo_before.width,
                              albedo_before.height);

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
        state.verified_paint_function = STR("ImportChannelFromBytes.screen_body_direct_only");
        state.last_failure = state.verified_visible_backend ? STR("play_screen_body_direct_only_applied")
                                                            : STR("play_screen_body_direct_only_unverified");

        RC::Output::send<RC::LogLevel::Warning>(
            STR("{} play screen_body result reason={} success={} visible_backend={} queued_strokes=0 import_backend=1 albedo_import_ok={} albedo_observed={} metallic_import_ok={} metallic_observed={} roughness_import_ok={} roughness_observed={} missing_color={} hash_before={} hash_after={} hash_changed={} body_samples={} paint_seeds={} target_samples={} min_samples={} hard_max_attempts={} coarse_hits={} refined_hits={} bbox_norm=({}, {})-({}, {}) bbox_px={}x{} refine_grid={}x{} rt_size={}x{} texture_size={}x{} uv_coverage={} filled_by_direct={} filled_by_extension={} filled_by_floor={} direct_texels={} edge_texels={} extruded_texels={} fallback_extruded_texels={} preserved_direct={} preserved_original={} worker_threads={} readback_backend={} trace_primary=1 no_scene_capture=0 texture_source_verified=1 capture_color_used={} trace_only_color_used={} capture_trace_pairs={} capture_trace_chroma_avg={} capture_trace_chroma_p95={} capture_trace_chroma_max={} selected_capture_fov={} capture_transform_backend={} capture_transform_scale=({}, {}) capture_transform_offset=({}, {}) capture_transform_flip=({}, {}) image_ok={} image_failure={} image_bulk_calibration_ok={} image_bulk_transform_flip=({}, {}) image_bulk_transform_scale=({}, {}) image_bulk_transform_offset=({}, {}) background_pixels={} capture_uniform={} capture_clear_suspect={} rgb_min=({}, {}, {}) rgb_avg=({}, {}, {}) rgb_max=({}, {}, {}) metallic_avg={} roughness_avg={} channel={} label=ImportChannelFromBytes first_failure={} t_hit_ms={} t_trace_ms={} t_alignment_ms={} t_capture_ms={} t_export_ms={} t_seed_ms={} t_atlas_ms={} t_import_ms={} t_total_ms={} frame_fov={} fov_source={} camera_fov={} deproject_hfov={} viewport={}x{} no_clear=1 no_commit=1 no_mesh_hide=1 no_palette_fallback=1 no_trace_color_fallback=1 fallback_used=0 side_enabled=0 uv_extend=0 fill_mode=direct_only_no_side direct_layer_priority=1\n"),
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
            direct_texel_count,
            0,
            0,
            0,
            direct_texel_count,
            write_stats.preserved_original,
            worker_count,
            readback_backend,
            capture_color_used,
            trace_only_color_used,
            capture_trace_pairs,
            capture_trace_chroma_avg,
            capture_trace_chroma_p95,
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
            stop_ui_tick_hook();
            RC::Output::send<RC::LogLevel::Verbose>(STR("{} destroyed\n"), ModTag);
        }

        auto on_program_start() -> void override
        {
            register_camouflage_hotkey();
        }

        auto on_unreal_init() -> void override
        {
            m_state.unreal_initialized = true;
            install_ui_tick_hook();
            RC::Output::send<RC::LogLevel::Verbose>(STR("{} Unreal initialized; command_hook={} ui_tick_hook={} hotkey={}\n"),
                                                    ModTag,
                                                    m_state.command_hook_installed ? 1 : 0,
                                                    m_ui_tick_hook_id != Unreal::Hook::ERROR_ID ? 1 : 0,
                                                    m_hotkey_registered ? 1 : 0);
        }

      private:
        ProbeState m_state{};
        Unreal::Hook::GlobalCallbackId m_ui_tick_hook_id{Unreal::Hook::ERROR_ID};
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
            MecchaCamouflage::Diagnostics::write_last_status_file(MecchaCamouflage::Diagnostics::LastStatusSnapshot{
                phase,
                target_resolve_ok,
                m_state.play_id,
                m_state.success,
                m_state.failures,
                m_state.verified_visible_backend,
                m_state.body_trace_hits,
                m_state.uv_hits,
                narrow_ascii(m_state.verified_paint_function),
                narrow_ascii(m_state.last_failure),
                narrow_ascii(m_state.current_world),
                narrow_ascii(m_state.current_pawn),
                narrow_ascii(m_state.current_component)});
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

        auto run_play() -> void
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
            m_state.verified_paint_function = STR("PaintAtScreenPosition.body_mask");
            m_state.verified_paint_channel = PaintChannelAlbedoMetallicRoughness;

            RC::Output::send<RC::LogLevel::Warning>(
                STR("{} play started id={} version={} route=f10_front_paint backend=validated_bulk_only actual_model_paint=1 viewport_resolution_capture=1 scene_capture_color=1 trace_primary=1 capture_alignment=disabled alignment_used=0 front_screen_paint=1 import_fallback=0 fallback_used=0 side_enabled=0 no_gui=1 no_umg_overlay=1 no_material_shader=1 no_clear=1 no_commit=1 no_mesh_hide=1 no_trace_color_fallback=1\n"),
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
