#include <meccha/application/mesh_profile_codec.hpp>
#include <meccha/core/image_paint_plan.hpp>
#include <meccha/core/profile_paint_sampling.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using namespace meccha;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_paint_sampling: "
                  << message << '\n';
    }
    return condition;
}

auto read_file(const std::filesystem::path& path) -> std::string
{
    auto input = std::ifstream{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

struct ProfileCase
{
    core::BodyProfile body{};
    std::string_view raw_file{};
    std::string_view image_file{};
    std::size_t expected_samples{};
};

auto request(
    const std::filesystem::path& root,
    const ProfileCase& test)
    -> core::ImagePaintProfilePlanRequest
{
    auto sampling =
        application::decode_paint_sampling_profile(
            read_file(root / test.raw_file),
            test.body);
    auto image =
        application::decode_canonical_image_profile(
            read_file(root / test.image_file),
            test.body);
    if (!sampling || !image)
    {
        return {};
    }
    auto settings = core::ImageProjectSettings{};
    settings.body = test.body;
    settings.brush_size_texels = 4.0;
    return core::ImagePaintProfilePlanRequest{
        std::move(*sampling),
        std::move(*image),
        settings,
        std::make_shared<const std::vector<std::byte>>(
            core::CanonicalAtlasByteLength,
            std::byte{}),
    };
}
} // namespace

auto main(int argc, char** argv) -> int
{
    if (argc != 2)
    {
        std::cerr
            << "usage: image_paint_sampling_test "
               "<profile-directory>\n";
        return 2;
    }
    const auto root = std::filesystem::path{argv[1]};
    constexpr std::array cases{
        ProfileCase{
            core::BodyProfile::Round,
            "paintman.mesh-profile-v2.json",
            "paintman.image-profile-v2.json",
            39'214U,
        },
        ProfileCase{
            core::BodyProfile::Cube,
            "paintman_cube.mesh-profile-v2.json",
            "paintman_cube.image-profile-v2.json",
            39'201U,
        },
        ProfileCase{
            core::BodyProfile::Fukuyoka,
            "paintman_hukuyoka.mesh-profile-v2.json",
            "paintman_hukuyoka.image-profile-v2.json",
            36'811U,
        },
    };

    auto passed = true;
    for (const auto& test : cases)
    {
        auto input = request(root, test);
        const auto sampled = core::sample_paint_profile(
            input.sampling_profile,
            input.image_profile,
            input.settings.brush_size_texels);
        const auto planned =
            core::build_image_paint_plan_from_profile(input);
        if (planned &&
            planned->generated_samples != test.expected_samples)
        {
            std::cerr << test.raw_file << ": expected "
                      << test.expected_samples << ", got "
                      << planned->generated_samples << '\n';
        }
        passed &= expect(
            sampled && planned &&
                sampled->size() == test.expected_samples &&
                planned->generated_samples ==
                    test.expected_samples &&
                planned->transparent_samples ==
                    test.expected_samples &&
                planned->opaque_samples == 0U &&
                planned->paint.strokes.empty(),
            "a frozen profile did not produce its deterministic "
            "bounded UV sample inventory");
        passed &= expect(
            sampled && !sampled->empty() &&
                sampled->front().paint_u >= 0.0 &&
                sampled->front().paint_u <= 1.0 &&
                sampled->front().paint_v >= 0.0 &&
                sampled->front().paint_v <= 1.0 &&
                sampled->front().image.u >= 0.0 &&
                sampled->front().image.u <= 1.0 &&
                sampled->front().image.v >= 0.0 &&
                sampled->front().image.v <= 1.0,
            "shared profile samples did not retain bounded raw and "
            "canonical coordinates");

        const auto repeated =
            core::build_image_paint_plan_from_profile(input);
        passed &= expect(
            planned && repeated &&
                planned->generated_samples ==
                    repeated->generated_samples &&
                planned->paint.strokes ==
                    repeated->paint.strokes &&
                planned->paint.fill_end ==
                    repeated->paint.fill_end &&
                planned->paint.paint_count ==
                    repeated->paint.paint_count,
            "profile sampling was not deterministic");
    }

    auto mismatched_topology = request(root, cases.front());
    auto indices = std::vector<std::uint32_t>{
        *mismatched_topology.image_profile.geometry.indices};
    std::swap(indices[0U], indices[1U]);
    mismatched_topology.image_profile.geometry.indices =
        std::make_shared<
            const std::vector<std::uint32_t>>(
            std::move(indices));
    passed &= expect(
        core::build_image_paint_plan_from_profile(
            mismatched_topology) ==
            std::unexpected(
                core::ImagePaintPlanError::InvalidProfile),
        "a raw/image topology mismatch was accepted");

    auto malformed_identity = request(root, cases.front());
    malformed_identity.sampling_profile.identity.index_count = 1U;
    const auto malformed_fields = core::validate_pair(
        malformed_identity.sampling_profile,
        malformed_identity.image_profile);
    passed &= expect(
        std::ranges::contains(
            malformed_fields,
            core::PaintSamplingProfileField::Triangles) &&
            std::ranges::contains(
                malformed_fields,
                core::PaintSamplingProfileField::PairTopology),
        "malformed index arithmetic did not fail closed");

    auto invalid_uv = request(root, cases.front());
    auto vertices = std::vector<core::PaintSamplingVertex>{
        *invalid_uv.sampling_profile.vertices};
    vertices.front().u =
        std::numeric_limits<double>::quiet_NaN();
    invalid_uv.sampling_profile.vertices =
        std::make_shared<
            const std::vector<core::PaintSamplingVertex>>(
            std::move(vertices));
    passed &= expect(
        core::sample_paint_profile(
            invalid_uv.sampling_profile,
            invalid_uv.image_profile,
            invalid_uv.settings.brush_size_texels) ==
            std::unexpected(
                core::ProfilePaintSamplingError::InvalidProfile),
        "shared sampling accepted a non-finite sampling UV");
    passed &= expect(
        core::build_image_paint_plan_from_profile(invalid_uv) ==
            std::unexpected(
                core::ImagePaintPlanError::InvalidProfile),
        "a non-finite sampling UV was accepted");

    const auto invalid_brush = request(root, cases.front());
    passed &= expect(
        core::sample_paint_profile(
            invalid_brush.sampling_profile,
            invalid_brush.image_profile,
            0.0) ==
            std::unexpected(
                core::ProfilePaintSamplingError::InvalidBrushSize),
        "shared sampling accepted a zero brush size");

    auto over_limit = request(root, cases.front());
    over_limit.settings.brush_size_texels = 1.0;
    passed &= expect(
        core::sample_paint_profile(
            over_limit.sampling_profile,
            over_limit.image_profile,
            over_limit.settings.brush_size_texels) ==
            std::unexpected(
                core::ProfilePaintSamplingError::ResourceLimit),
        "shared sampling exceeded the global sample limit");
    passed &= expect(
        core::build_image_paint_plan_from_profile(over_limit) ==
            std::unexpected(
                core::ImagePaintPlanError::ResourceLimit),
        "sampling exceeded the global sample limit");

    auto cancelled = std::stop_source{};
    cancelled.request_stop();
    passed &= expect(
        core::sample_paint_profile(
            request(root, cases.front()).sampling_profile,
            request(root, cases.front()).image_profile,
            4.0,
            cancelled.get_token()) ==
            std::unexpected(
                core::ProfilePaintSamplingError::Cancelled),
        "pre-cancelled shared profile sampling published samples");
    passed &= expect(
        core::build_image_paint_plan_from_profile(
            request(root, cases.front()),
            cancelled.get_token()) ==
            std::unexpected(
                core::ImagePaintPlanError::Cancelled),
        "pre-cancelled profile sampling published a plan");

    if (passed)
    {
        std::cout << "PASS image_paint_sampling\n";
    }
    return passed ? 0 : 1;
}
