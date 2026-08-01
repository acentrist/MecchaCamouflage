#include <meccha/runtime/paint_capture_codec.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <span>
#include <vector>

namespace meccha::runtime
{
namespace
{
auto valid_color(const PaintCaptureLinearColor& color) -> bool
{
    return std::isfinite(color.red) &&
           std::isfinite(color.green) &&
           std::isfinite(color.blue) &&
           std::isfinite(color.alpha);
}

auto finite(double value) -> bool
{
    return std::isfinite(value);
}

auto linear_to_srgb(double value) -> double
{
    value = std::clamp(value, 0.0, 1.0);
    return value <= 0.0031308
               ? value * 12.92
               : 1.055 * std::pow(value, 1.0 / 2.4) -
                     0.055;
}

auto to_srgb8(float value) -> std::uint8_t
{
    return static_cast<std::uint8_t>(std::lround(
        linear_to_srgb(static_cast<double>(value)) *
        255.0));
}
} // namespace

auto decode_runtime_transform(
    const RuntimeTransform& transform)
    -> std::expected<
        core::PaintReferenceBoneTransform,
        PaintCaptureEncodingError>
{
    const auto& rotation = transform.rotation;
    const auto& translation = transform.translation;
    const auto& scale = transform.scale;
    const auto rotation_length = std::sqrt(
        rotation.x * rotation.x +
        rotation.y * rotation.y +
        rotation.z * rotation.z +
        rotation.w * rotation.w);
    if (!std::isfinite(rotation_length) ||
        rotation_length <= 1.0e-12 ||
        !std::isfinite(translation.x) ||
        !std::isfinite(translation.y) ||
        !std::isfinite(translation.z) ||
        !std::isfinite(scale.x) ||
        !std::isfinite(scale.y) ||
        !std::isfinite(scale.z) ||
        std::abs(scale.x) <= 1.0e-12 ||
        std::abs(scale.y) <= 1.0e-12 ||
        std::abs(scale.z) <= 1.0e-12)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidTransform);
    }
    return core::PaintReferenceBoneTransform{
        core::Vector3d{
            translation.x,
            translation.y,
            translation.z,
        },
        core::PaintQuaternion{
            rotation.x / rotation_length,
            rotation.y / rotation_length,
            rotation.z / rotation_length,
            rotation.w / rotation_length,
        },
        core::Vector3d{scale.x, scale.y, scale.z},
    };
}

auto encode_create_paint_capture_render_target(
    const PaintCaptureRenderTargetInput& input)
    -> std::expected<
        CreatePaintCaptureRenderTargetParameters,
        PaintCaptureEncodingError>
{
    if (input.world_context_object == nullptr ||
        input.width == 0U || input.height == 0U ||
        input.width > core::MaximumPaintCaptureDimension ||
        input.height > core::MaximumPaintCaptureDimension ||
        (input.format !=
             PaintCaptureRenderTargetFormat::Rgba8Srgb &&
         input.format !=
             PaintCaptureRenderTargetFormat::Rgba16Float) ||
        input.width >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()) ||
        input.height >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max()))
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidRenderTarget);
    }
    return CreatePaintCaptureRenderTargetParameters{
        input.world_context_object,
        static_cast<std::int32_t>(input.width),
        static_cast<std::int32_t>(input.height),
        1,
        input.format,
        {},
        PaintCaptureLinearColor{},
        false,
        false,
        {},
        nullptr,
    };
}

auto encode_read_paint_capture_render_target(
    void* world_context_object,
    void* texture_render_target,
    bool normalize)
    -> std::expected<
        ReadPaintCaptureRenderTargetParameters,
        PaintCaptureEncodingError>
{
    if (world_context_object == nullptr ||
        texture_render_target == nullptr)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidRenderTarget);
    }
    return ReadPaintCaptureRenderTargetParameters{
        world_context_object,
        texture_render_target,
        {},
        normalize,
        false,
        {},
    };
}

auto decode_paint_capture_linear_colors(
    const ReadPaintCaptureRenderTargetParameters& parameters,
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<
        std::vector<PaintCaptureLinearColor>,
        PaintCaptureEncodingError>
{
    if (!parameters.return_value ||
        width == 0U || height == 0U ||
        width > core::MaximumPaintCaptureDimension ||
        height > core::MaximumPaintCaptureDimension ||
        width > std::numeric_limits<std::size_t>::max() /
                    height)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidReadback);
    }
    const auto expected_count =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height);
    const auto& array = parameters.out_linear_samples;
    if (array.data == nullptr || array.count < 0 ||
        array.capacity < array.count ||
        static_cast<std::size_t>(array.count) !=
            expected_count)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidReadback);
    }
    auto output = std::vector<PaintCaptureLinearColor>{
        array.data,
        array.data + expected_count};
    for (const auto& color : output)
    {
        if (!valid_color(color))
        {
            return std::unexpected(
                PaintCaptureEncodingError::InvalidReadback);
        }
    }
    return output;
}

auto build_paint_scene_capture_plan(
    const core::PaintSettings& settings)
    -> std::expected<
        PaintSceneCapturePlan,
        PaintCaptureEncodingError>
{
    if (!core::validate(settings).empty())
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidSettings);
    }

    auto plan = PaintSceneCapturePlan{};
    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::BaseColor,
        PaintSceneCaptureSource::BaseColor,
        PaintCaptureRenderTargetFormat::Rgba8Srgb,
        PaintSceneCaptureProfile::Standard,
        false,
        false,
    });
    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::FinalColorHdr,
        PaintSceneCaptureSource::FinalColorHdr,
        PaintCaptureRenderTargetFormat::Rgba16Float,
        PaintSceneCaptureProfile::Standard,
        false,
        true,
    });

    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::IntrinsicEmissionHdr,
        PaintSceneCaptureSource::FinalColorHdr,
        PaintCaptureRenderTargetFormat::Rgba16Float,
        PaintSceneCaptureProfile::IntrinsicEmission,
        false,
        true,
    });
    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::IntrinsicEmissionRepeatHdr,
        PaintSceneCaptureSource::FinalColorHdr,
        PaintCaptureRenderTargetFormat::Rgba16Float,
        PaintSceneCaptureProfile::IntrinsicEmission,
        false,
        true,
    });
    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::FinalToneCurveHdr,
        PaintSceneCaptureSource::FinalToneCurveHdr,
        PaintCaptureRenderTargetFormat::Rgba16Float,
        PaintSceneCaptureProfile::Standard,
        false,
        true,
    });
    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::Normal,
        PaintSceneCaptureSource::Normal,
        PaintCaptureRenderTargetFormat::Rgba16Float,
        PaintSceneCaptureProfile::Standard,
        false,
        true,
    });
    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::SceneDepth,
        PaintSceneCaptureSource::SceneDepth,
        PaintCaptureRenderTargetFormat::Rgba16Float,
        PaintSceneCaptureProfile::Standard,
        false,
        true,
    });
    plan.passes.push_back(PaintSceneCapturePass{
        PaintSceneCapturePassKind::FinalColorLdr,
        PaintSceneCaptureSource::FinalColorLdr,
        PaintCaptureRenderTargetFormat::Rgba8Srgb,
        PaintSceneCaptureProfile::Standard,
        false,
        false,
    });
    plan.requires_preview_feedback = true;
    return plan;
}

auto paint_appearance_feedback_capture_plan()
    -> const std::array<PaintSceneCapturePass, 3U>&
{
    static constexpr auto Plan =
        std::array<PaintSceneCapturePass, 3U>{
            PaintSceneCapturePass{
                PaintSceneCapturePassKind::FinalColorHdr,
                PaintSceneCaptureSource::FinalColorHdr,
                PaintCaptureRenderTargetFormat::Rgba16Float,
                PaintSceneCaptureProfile::Standard,
                false,
                true,
                PaintSceneCaptureSubject::TargetVisible,
            },
            PaintSceneCapturePass{
                PaintSceneCapturePassKind::BaseColor,
                PaintSceneCaptureSource::BaseColor,
                PaintCaptureRenderTargetFormat::Rgba8Srgb,
                PaintSceneCaptureProfile::Standard,
                false,
                false,
                PaintSceneCaptureSubject::TargetVisible,
            },
            PaintSceneCapturePass{
                PaintSceneCapturePassKind::IntrinsicEmissionHdr,
                PaintSceneCaptureSource::FinalColorHdr,
                PaintCaptureRenderTargetFormat::Rgba16Float,
                PaintSceneCaptureProfile::IntrinsicEmission,
                false,
                true,
                PaintSceneCaptureSubject::TargetVisible,
            },
        };
    return Plan;
}

auto encode_paint_scene_capture_camera(
    const core::EspView& view,
    std::uint32_t width,
    std::uint32_t height)
    -> std::expected<
        PaintSceneCaptureCamera,
        PaintCaptureEncodingError>
{
    if (width == 0U || height == 0U ||
        width > core::MaximumPaintCaptureDimension ||
        height > core::MaximumPaintCaptureDimension ||
        !finite(view.location.x) ||
        !finite(view.location.y) ||
        !finite(view.location.z) ||
        !finite(view.pitch_degrees) ||
        !finite(view.yaw_degrees) ||
        !finite(view.roll_degrees) ||
        !finite(view.field_of_view_degrees) ||
        view.field_of_view_degrees < 20.0 ||
        view.field_of_view_degrees > 170.0 ||
        !finite(view.aspect_ratio) ||
        view.aspect_constraint !=
            core::EspAspectConstraint::MaintainXFov ||
        !finite(view.projection_scale_x) ||
        !finite(view.projection_scale_y) ||
        view.projection_scale_x < 0.5 ||
        view.projection_scale_x > 2.5 ||
        view.projection_scale_y < 0.5 ||
        view.projection_scale_y > 2.5)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidCamera);
    }
    const auto target_aspect =
        static_cast<double>(width) /
        static_cast<double>(height);
    const auto aspect_tolerance =
        std::max(1.0, target_aspect) * 1.0e-6;
    if (std::abs(view.aspect_ratio - target_aspect) >
        aspect_tolerance)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidCamera);
    }
    return PaintSceneCaptureCamera{
        EspVector3dAbi{
            view.location.x,
            view.location.y,
            view.location.z,
        },
        EspRotatorAbi{
            view.pitch_degrees,
            view.yaw_degrees,
            view.roll_degrees,
        },
        static_cast<float>(
            view.field_of_view_degrees),
        width,
        height,
    };
}

auto convert_paint_capture_linear_colors_to_srgb8(
    std::span<const PaintCaptureLinearColor> colors)
    -> std::expected<
        std::vector<core::Rgb8>,
        PaintCaptureEncodingError>
{
    if (colors.empty() ||
        colors.size() >
            static_cast<std::size_t>(
                core::MaximumPaintCaptureDimension) *
                core::MaximumPaintCaptureDimension)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidColor);
    }
    auto converted = std::vector<core::Rgb8>{};
    converted.reserve(colors.size());
    for (const auto& color : colors)
    {
        if (!valid_color(color))
        {
            return std::unexpected(
                PaintCaptureEncodingError::InvalidColor);
        }
        converted.push_back(core::Rgb8{
            to_srgb8(color.red),
            to_srgb8(color.green),
            to_srgb8(color.blue),
        });
    }
    return converted;
}

auto convert_paint_capture_linear_colors_to_hdr(
    std::span<const PaintCaptureLinearColor> colors)
    -> std::expected<
        std::vector<core::AppearanceRgb>,
        PaintCaptureEncodingError>
{
    if (colors.empty() ||
        colors.size() >
            static_cast<std::size_t>(
                core::MaximumPaintCaptureDimension) *
                core::MaximumPaintCaptureDimension)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidColor);
    }
    auto converted = std::vector<core::AppearanceRgb>{};
    converted.reserve(colors.size());
    for (const auto& color : colors)
    {
        if (!valid_color(color))
        {
            return std::unexpected(
                PaintCaptureEncodingError::InvalidColor);
        }
        converted.push_back(core::AppearanceRgb{
            static_cast<double>(color.red),
            static_cast<double>(color.green),
            static_cast<double>(color.blue),
        });
    }
    return converted;
}

auto convert_paint_capture_linear_colors_to_depth(
    std::span<const PaintCaptureLinearColor> colors)
    -> std::expected<
        std::vector<double>,
        PaintCaptureEncodingError>
{
    if (colors.empty() ||
        colors.size() >
            static_cast<std::size_t>(
                core::MaximumPaintCaptureDimension) *
                core::MaximumPaintCaptureDimension)
    {
        return std::unexpected(
            PaintCaptureEncodingError::InvalidColor);
    }
    auto converted = std::vector<double>{};
    converted.reserve(colors.size());
    for (const auto& color : colors)
    {
        if (!valid_color(color))
        {
            return std::unexpected(
                PaintCaptureEncodingError::InvalidColor);
        }
        converted.push_back(
            static_cast<double>(color.red));
    }
    return converted;
}

auto paint_brush_plane_visual_contract()
    -> PaintBrushPlaneVisualContract
{
    return PaintBrushPlaneVisualContract{
        "/Game/BluePrints/cLeon/BP_BrushPlane."
        "BP_BrushPlane_C",
        {
            PaintBrushPlaneVisualComponent{
                "Plane",
                PaintBrushPlaneVisualKind::StaticMesh,
            },
            PaintBrushPlaneVisualComponent{
                "Plane1",
                PaintBrushPlaneVisualKind::StaticMesh,
            },
            PaintBrushPlaneVisualComponent{
                "Niagara",
                PaintBrushPlaneVisualKind::Niagara,
            },
        },
    };
}

auto paint_intrinsic_emission_show_flags()
    -> const std::array<PaintShowFlagSetting, 33U>&
{
    static constexpr auto settings =
        std::array<PaintShowFlagSetting, 33U>{
            PaintShowFlagSetting{"Lighting", false},
            PaintShowFlagSetting{"DeferredLighting", false},
            PaintShowFlagSetting{"DirectLighting", false},
            PaintShowFlagSetting{"SkyLighting", false},
            PaintShowFlagSetting{"DynamicShadows", false},
            PaintShowFlagSetting{"ContactShadows", false},
            PaintShowFlagSetting{
                "RayTracedDistanceFieldShadows",
                false,
            },
            PaintShowFlagSetting{"GlobalIllumination", false},
            PaintShowFlagSetting{
                "LumenGlobalIllumination",
                false,
            },
            PaintShowFlagSetting{
                "ReflectionEnvironment",
                false,
            },
            PaintShowFlagSetting{
                "ScreenSpaceReflections",
                false,
            },
            PaintShowFlagSetting{"LumenReflections", false},
            PaintShowFlagSetting{"AmbientOcclusion", false},
            PaintShowFlagSetting{"AmbientCubemap", false},
            PaintShowFlagSetting{"Bloom", false},
            PaintShowFlagSetting{"PostProcessing", false},
            PaintShowFlagSetting{"Tonemapper", false},
            PaintShowFlagSetting{"EyeAdaptation", false},
            PaintShowFlagSetting{"LocalExposure", false},
            PaintShowFlagSetting{"ColorGrading", false},
            PaintShowFlagSetting{"Fog", false},
            PaintShowFlagSetting{"VolumetricFog", false},
            PaintShowFlagSetting{"Atmosphere", false},
            PaintShowFlagSetting{"SkyAtmosphere", false},
            PaintShowFlagSetting{"Cloud", false},
            PaintShowFlagSetting{"VolumetricCloud", false},
            PaintShowFlagSetting{"LightShafts", false},
            PaintShowFlagSetting{"MotionBlur", false},
            PaintShowFlagSetting{"DepthOfField", false},
            PaintShowFlagSetting{"LensFlares", false},
            PaintShowFlagSetting{"Vignette", false},
            PaintShowFlagSetting{"FilmGrain", false},
            PaintShowFlagSetting{"UnlitViewmode", false},
        };
    return settings;
}
} // namespace meccha::runtime
