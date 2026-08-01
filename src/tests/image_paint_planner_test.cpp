#include <meccha/core/image_paint_plan.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stop_token>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha::core;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_paint_planner: "
                  << message << '\n';
    }
    return condition;
}

auto pixel_offset(double u, double v) -> std::size_t
{
    const auto x = static_cast<std::uint32_t>(
        std::lround(
            std::clamp(u, 0.0, 1.0) *
            static_cast<double>(CanonicalAtlasWidth - 1U)));
    const auto y = static_cast<std::uint32_t>(
        std::lround(
            (1.0 - std::clamp(v, 0.0, 1.0)) *
            static_cast<double>(CanonicalAtlasHeight - 1U)));
    return (static_cast<std::size_t>(y) *
                CanonicalAtlasWidth +
            x) *
           4U;
}

auto write_pixel(
    std::vector<std::byte>& atlas,
    double u,
    double v,
    Rgb8 color,
    std::uint8_t alpha) -> void
{
    const auto offset = pixel_offset(u, v);
    atlas[offset] = static_cast<std::byte>(color.red);
    atlas[offset + 1U] = static_cast<std::byte>(color.green);
    atlas[offset + 2U] = static_cast<std::byte>(color.blue);
    atlas[offset + 3U] = static_cast<std::byte>(alpha);
}

auto sample(
    double paint_u,
    std::size_t triangle_index,
    bool safe = true) -> CapturedImagePaintSample
{
    return CapturedImagePaintSample{
        triangle_index == 0U
            ? Region::Front
            : (triangle_index == 2U
                   ? Region::Back
                   : Region::Side),
        static_cast<int>(triangle_index),
        paint_u,
        0.5,
        true,
        1.0 - paint_u,
        1.0 - paint_u,
        paint_u,
        ImageTriangleAnchor{
            triangle_index,
            1.0 / 3.0,
            1.0 / 3.0,
            1.0 / 3.0,
        },
        safe,
    };
}

auto image_profile(BodyProfile body) -> CanonicalImageProfile
{
    const auto identity = expected_mesh_profile(
        body,
        MeshProfileRole::ImageReference);
    auto positions = std::vector<Vector3d>(
        identity.vertex_count,
        Vector3d{});
    positions[0U] = {-1.0, -1.0, -1.0};
    positions[1U] = {1.0, -1.0, -1.0};
    positions[2U] = {0.0, -1.0, 1.0};
    positions[3U] = {1.0, -1.0, -1.0};
    positions[4U] = {1.0, 1.0, -1.0};
    positions[5U] = {1.0, 0.0, 1.0};
    positions[6U] = {1.0, 1.0, -1.0};
    positions[7U] = {-1.0, 1.0, -1.0};
    positions[8U] = {0.0, 1.0, 1.0};
    positions[9U] = {-1.0, 1.0, -1.0};
    positions[10U] = {-1.0, -1.0, -1.0};
    positions[11U] = {-1.0, 0.0, 1.0};
    auto indices = std::vector<std::uint32_t>(
        identity.index_count,
        0U);
    for (auto index = std::size_t{}; index < 12U; ++index)
    {
        indices[index] = static_cast<std::uint32_t>(index);
    }
    indices.back() =
        static_cast<std::uint32_t>(identity.vertex_count - 1U);
    auto built = build_canonical_image_profile(
        ImageReferenceGeometry{
            identity,
            std::make_shared<const std::vector<Vector3d>>(
                std::move(positions)),
            std::make_shared<const std::vector<std::uint32_t>>(
                std::move(indices)),
        });
    return std::move(*built);
}

auto request(BodyProfile body = BodyProfile::Round)
    -> ImagePaintPlanRequest
{
    auto profile = image_profile(body);
    auto atlas = std::vector<std::byte>(
        CanonicalAtlasByteLength,
        std::byte{});
    const auto front = map_image_triangle(
        profile,
        ImageTriangleAnchor{0U, 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0});
    const auto right = map_image_triangle(
        profile,
        ImageTriangleAnchor{1U, 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0});
    const auto back = map_image_triangle(
        profile,
        ImageTriangleAnchor{2U, 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0});
    const auto left = map_image_triangle(
        profile,
        ImageTriangleAnchor{3U, 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0});
    write_pixel(
        atlas,
        front->u,
        front->v,
        Rgb8{255U, 0U, 0U},
        255U);
    write_pixel(
        atlas,
        right->u,
        right->v,
        Rgb8{0U, 255U, 0U},
        0U);
    write_pixel(
        atlas,
        back->u,
        back->v,
        Rgb8{0U, 0U, 255U},
        255U);
    write_pixel(
        atlas,
        left->u,
        left->v,
        Rgb8{12U, 34U, 56U},
        ImageBackgroundAlphaMarker);

    auto settings = ImageProjectSettings{};
    settings.body = body;
    settings.alpha = AlphaMode::Background;
    settings.front = FaceBaseMode::Fill;
    settings.right = FaceBaseMode::Skip;
    settings.back = FaceBaseMode::Skip;
    settings.left = FaceBaseMode::Fill;
    settings.brush_size_texels = 4.0;
    settings.color_compression_tolerance_percent = 0.0;
    settings.image_material = Material{0.2, 0.3, 0.4};
    settings.fill_color = Rgb8{200U, 201U, 202U};
    settings.fill_material = Material{0.8, 0.1, 0.6};

    return ImagePaintPlanRequest{
        expected_mesh_profile(body, MeshProfileRole::Raw),
        std::move(profile),
        settings,
        std::make_shared<const std::vector<std::byte>>(
            std::move(atlas)),
        {
            sample(0.1, 0U),
            sample(0.3, 1U),
            sample(0.6, 2U),
            sample(0.9, 3U),
        },
    };
}
} // namespace

auto main() -> int
{
    using namespace meccha::core;

    auto passed = true;
    const auto planned = build_image_paint_plan(request());
    passed &= expect(
        planned &&
            planned->paint.texture_dimension ==
                expected_mesh_profile(
                    BodyProfile::Round,
                    MeshProfileRole::Raw)
                    .texture_size &&
            planned->paint.fill_count == 2U &&
            planned->paint.paint_count == 2U &&
            planned->paint.fill_end == 2U &&
            planned->opaque_samples == 2U &&
            planned->transparent_samples == 1U &&
            planned->background_marker_samples == 1U,
        "face routing, alpha classification, or pass counts drifted");
    if (!planned)
    {
        return 1;
    }
    passed &= expect(
        planned->paint.strokes[0U].source_sample == 3U &&
            planned->paint.strokes[1U].source_sample == 0U &&
            planned->paint.strokes[0U].pass == ReplayPass::Fill &&
            planned->paint.strokes[1U].pass == ReplayPass::Fill,
        "Fill was not restricted to the independently enabled faces");
    passed &= expect(
        planned->paint.strokes[2U].source_sample == 2U &&
            planned->paint.strokes[3U].source_sample == 0U &&
            planned->paint.strokes[2U].pass == ReplayPass::Paint &&
            planned->paint.strokes[3U].pass == ReplayPass::Paint,
        "opaque Image Paint did not overwrite in a separate Paint pass");
    for (auto index = std::size_t{};
         index < planned->paint.fill_end;
         ++index)
    {
        passed &= expect(
            planned->paint.strokes[index].color ==
                    planned->settings.fill_color &&
                planned->paint.strokes[index].material ==
                    planned->settings.fill_material &&
                planned->paint.strokes[index].radius_texels ==
                    PaintFillRadiusTexels,
            "Fill did not retain its independent project appearance");
    }
    passed &= expect(
        planned->paint.strokes[2U].color ==
                Rgb8{0U, 0U, 255U} &&
            planned->paint.strokes[3U].color ==
                Rgb8{255U, 0U, 0U} &&
            planned->paint.strokes[2U].material ==
                planned->settings.image_material &&
            planned->paint.strokes[2U].radius_texels ==
                planned->settings.brush_size_texels,
        "atlas color or Image Paint material reached the Paint plan "
        "incorrectly");

    auto skip_mode_with_marker = request();
    skip_mode_with_marker.settings.alpha = AlphaMode::Skip;
    const auto marker_plan =
        build_image_paint_plan(skip_mode_with_marker);
    passed &= expect(
        marker_plan &&
            marker_plan->background_marker_samples == 1U &&
            marker_plan->paint.paint_count == 2U,
        "the reserved Background marker was painted after an alpha-mode "
        "change");

    for (const auto body : {
             BodyProfile::Round,
             BodyProfile::Cube,
             BodyProfile::Fukuyoka,
         })
    {
        const auto body_plan = build_image_paint_plan(request(body));
        passed &= expect(
            body_plan && body_plan->settings.body == body,
            "an accepted body profile pair did not plan");
    }

    auto mismatched = request(BodyProfile::Cube);
    mismatched.image_profile = image_profile(BodyProfile::Round);
    passed &= expect(
        build_image_paint_plan(mismatched) ==
            std::unexpected(ImagePaintPlanError::InvalidProfile),
        "mismatched raw/reference body profiles were accepted");

    auto invalid_atlas = request();
    invalid_atlas.atlas =
        std::make_shared<const std::vector<std::byte>>(3U);
    passed &= expect(
        build_image_paint_plan(invalid_atlas) ==
            std::unexpected(ImagePaintPlanError::InvalidAtlas),
        "a truncated canonical atlas was accepted");

    auto invalid_sample = request();
    invalid_sample.samples.front().image_anchor.barycentric_a =
        std::numeric_limits<double>::quiet_NaN();
    passed &= expect(
        build_image_paint_plan(invalid_sample) ==
            std::unexpected(ImagePaintPlanError::InvalidSample),
        "a non-finite atlas coordinate was accepted");
    auto invalid_face = request();
    invalid_face.samples.front().image_anchor.triangle_index =
        invalid_face.image_profile.geometry.identity.triangle_count;
    passed &= expect(
        build_image_paint_plan(invalid_face) ==
            std::unexpected(ImagePaintPlanError::InvalidSample),
        "an out-of-range image-reference triangle was accepted");

    auto unsafe = request();
    unsafe.samples.front().safe = false;
    const auto unsafe_plan = build_image_paint_plan(unsafe);
    passed &= expect(
        unsafe_plan &&
            unsafe_plan->paint.fill_count == 1U &&
            unsafe_plan->paint.paint_count == 1U &&
            unsafe_plan->unsafe_samples == 1U,
        "an unsafe mapped sample entered Fill or Paint");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        build_image_paint_plan(
            request(),
            cancelled.get_token()) ==
            std::unexpected(ImagePaintPlanError::Cancelled),
        "a pre-cancelled Image Paint plan was published");

    if (passed)
    {
        std::cout << "PASS image_paint_planner\n";
    }
    return passed ? 0 : 1;
}
