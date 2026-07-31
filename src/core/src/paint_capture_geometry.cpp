#include <meccha/core/paint_capture_geometry.hpp>

#include <meccha/core/paint_deformation.hpp>
#include <meccha/core/profile_paint_sampling.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <span>
#include <stop_token>
#include <vector>

namespace meccha::core
{
namespace
{
constexpr auto GeometryEpsilon = 1.0e-12;

auto finite(Vector3d value) -> bool
{
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

auto add(Vector3d left, Vector3d right) -> Vector3d
{
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z,
    };
}

auto multiply(Vector3d value, double scalar) -> Vector3d
{
    return {
        value.x * scalar,
        value.y * scalar,
        value.z * scalar,
    };
}

auto length_squared(Vector3d value) -> double
{
    return value.x * value.x +
           value.y * value.y +
           value.z * value.z;
}

auto normalize(Vector3d value) -> Vector3d
{
    const auto length = std::sqrt(length_squared(value));
    return length > GeometryEpsilon
               ? multiply(value, 1.0 / length)
               : Vector3d{};
}

auto region(ImageAtlasFace face) -> Region
{
    switch (face)
    {
    case ImageAtlasFace::Front:
        return Region::Front;
    case ImageAtlasFace::Back:
        return Region::Back;
    case ImageAtlasFace::Right:
    case ImageAtlasFace::Left:
        return Region::Side;
    }
    return Region::Side;
}

auto view_valid(const EspView& view, EspViewport viewport)
    -> bool
{
    const auto constraint =
        view.aspect_constraint ==
            EspAspectConstraint::MaintainYFov ||
        view.aspect_constraint ==
            EspAspectConstraint::MaintainXFov ||
        view.aspect_constraint ==
            EspAspectConstraint::MajorAxisFov;
    return std::isfinite(viewport.width) &&
           std::isfinite(viewport.height) &&
           viewport.width >= 1.0 &&
           viewport.height >= 1.0 &&
           viewport.width <= 16384.0 &&
           viewport.height <= 16384.0 &&
           std::isfinite(view.location.x) &&
           std::isfinite(view.location.y) &&
           std::isfinite(view.location.z) &&
           std::isfinite(view.pitch_degrees) &&
           std::isfinite(view.yaw_degrees) &&
           std::isfinite(view.roll_degrees) &&
           std::isfinite(view.field_of_view_degrees) &&
           view.field_of_view_degrees >= 20.0 &&
           view.field_of_view_degrees <= 170.0 &&
           std::isfinite(view.aspect_ratio) &&
           view.aspect_ratio >= 0.1 &&
           view.aspect_ratio <= 100.0 &&
           std::isfinite(view.projection_scale_x) &&
           std::isfinite(view.projection_scale_y) &&
           view.projection_scale_x >= 0.5 &&
           view.projection_scale_x <= 2.5 &&
           view.projection_scale_y >= 0.5 &&
           view.projection_scale_y <= 2.5 &&
           constraint;
}

auto map_sampling_error(ProfilePaintSamplingError error)
    -> PaintCaptureGeometryError
{
    switch (error)
    {
    case ProfilePaintSamplingError::ResourceLimit:
        return PaintCaptureGeometryError::ResourceLimit;
    case ProfilePaintSamplingError::Cancelled:
        return PaintCaptureGeometryError::Cancelled;
    case ProfilePaintSamplingError::InvalidProfile:
    case ProfilePaintSamplingError::InvalidBrushSize:
    case ProfilePaintSamplingError::EmptySamples:
        return PaintCaptureGeometryError::InvalidProfile;
    }
    return PaintCaptureGeometryError::InvalidProfile;
}

auto map_deformation_error(PaintDeformationError error)
    -> PaintCaptureGeometryError
{
    switch (error)
    {
    case PaintDeformationError::InvalidSkeleton:
        return PaintCaptureGeometryError::InvalidSkeleton;
    case PaintDeformationError::InvalidVertex:
        return PaintCaptureGeometryError::InvalidSample;
    case PaintDeformationError::ResourceLimit:
        return PaintCaptureGeometryError::ResourceLimit;
    case PaintDeformationError::Cancelled:
        return PaintCaptureGeometryError::Cancelled;
    }
    return PaintCaptureGeometryError::InvalidSample;
}
} // namespace

auto build_paint_capture_geometry(
    const PaintSamplingProfile& sampling_profile,
    const CanonicalImageProfile& image_profile,
    std::span<const PaintReferenceBoneTransform>
        current_world_transforms,
    double brush_size_texels,
    const EspView& view,
    EspViewport viewport,
    std::stop_token cancellation)
    -> std::expected<
        std::vector<PaintCaptureGeometrySample>,
        PaintCaptureGeometryError>
{
    if (cancellation.stop_requested())
    {
        return std::unexpected(
            PaintCaptureGeometryError::Cancelled);
    }
    if (!view_valid(view, viewport))
    {
        return std::unexpected(
            PaintCaptureGeometryError::InvalidView);
    }
    if (!validate_pair(
            sampling_profile,
            image_profile)
             .empty() ||
        !validate_deformation(sampling_profile).empty() ||
        !sampling_profile.deformation_vertices ||
        !sampling_profile.reference_bone_transforms ||
        !sampling_profile.bones ||
        !sampling_profile.triangles)
    {
        return std::unexpected(
            PaintCaptureGeometryError::InvalidProfile);
    }

    const auto sampled = sample_paint_profile(
        sampling_profile,
        image_profile,
        brush_size_texels,
        cancellation);
    if (!sampled)
    {
        return std::unexpected(
            map_sampling_error(sampled.error()));
    }
    const auto deformed = deform_paint_vertices(
        *sampling_profile.deformation_vertices,
        *sampling_profile.bones,
        *sampling_profile.reference_bone_transforms,
        current_world_transforms,
        cancellation);
    if (!deformed)
    {
        return std::unexpected(
            map_deformation_error(deformed.error()));
    }

    auto output = std::vector<PaintCaptureGeometrySample>{};
    output.reserve(sampled->size());
    for (auto index = std::size_t{};
         index < sampled->size();
         ++index)
    {
        if ((index % 256U) == 0U &&
            cancellation.stop_requested())
        {
            return std::unexpected(
                PaintCaptureGeometryError::Cancelled);
        }
        const auto& sample = (*sampled)[index];
        if (sample.image_anchor.triangle_index >=
            sampling_profile.triangles->size())
        {
            return std::unexpected(
                PaintCaptureGeometryError::InvalidSample);
        }
        const auto& triangle =
            (*sampling_profile.triangles)
                [sample.image_anchor.triangle_index];
        if (triangle.first >= deformed->size() ||
            triangle.second >= deformed->size() ||
            triangle.third >= deformed->size())
        {
            return std::unexpected(
                PaintCaptureGeometryError::InvalidSample);
        }
        const auto& first = (*deformed)[triangle.first];
        const auto& second = (*deformed)[triangle.second];
        const auto& third = (*deformed)[triangle.third];
        const auto position = add(
            add(
                multiply(
                    first.position,
                    sample.image_anchor.barycentric_a),
                multiply(
                    second.position,
                    sample.image_anchor.barycentric_b)),
            multiply(
                third.position,
                sample.image_anchor.barycentric_c));
        const auto normal = normalize(add(
            add(
                multiply(
                    first.normal,
                    sample.image_anchor.barycentric_a),
                multiply(
                    second.normal,
                    sample.image_anchor.barycentric_b)),
            multiply(
                third.normal,
                sample.image_anchor.barycentric_c)));
        if (!finite(position) || !finite(normal) ||
            length_squared(normal) <= GeometryEpsilon)
        {
            return std::unexpected(
                PaintCaptureGeometryError::InvalidSample);
        }
        const auto projected = project_esp_world_point(
            view,
            viewport,
            EspWorldPoint{
                position.x,
                position.y,
                position.z,
            });
        const auto inside =
            projected &&
            projected->x >= 0.0 &&
            projected->x < viewport.width &&
            projected->y >= 0.0 &&
            projected->y < viewport.height;
        output.push_back(PaintCaptureGeometrySample{
            region(sample.image.face),
            sample.uv_island,
            sample.paint_u,
            sample.paint_v,
            position,
            normal,
            inside,
            inside ? *projected : EspScreenPoint{},
            position.z,
            position.x,
        });
    }
    if (output.empty())
    {
        return std::unexpected(
            PaintCaptureGeometryError::InvalidSample);
    }
    return output;
}
} // namespace meccha::core
