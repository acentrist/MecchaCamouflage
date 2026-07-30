#include <meccha/application/mesh_profile_codec.hpp>

#include <meccha/core/image_guide.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <stop_token>
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
        std::cerr << "FAIL image_guide: " << message << '\n';
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

auto tile_alpha_counts(
    const core::ImageGuideBitmap& guide)
    -> std::array<std::size_t, 4U>
{
    auto result = std::array<std::size_t, 4U>{};
    for (auto y = std::uint32_t{};
         y < guide.height;
         ++y)
    {
        for (auto x = std::uint32_t{};
             x < guide.width;
             ++x)
        {
            const auto offset =
                (static_cast<std::size_t>(y) * guide.width + x) *
                4U;
            if (std::to_integer<std::uint8_t>(
                    guide.rgba->at(offset + 3U)) != 0U)
            {
                ++result[std::min<std::size_t>(
                    x / core::CanonicalAtlasTileWidth,
                    result.size() - 1U)];
            }
        }
    }
    return result;
}
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;

    if (argc != 2)
    {
        std::cerr << "usage: image_guide_test <profile-directory>\n";
        return 2;
    }

    const auto root = std::filesystem::path{argv[1]};
    struct Case
    {
        std::string_view file{};
        core::BodyProfile body{};
    };
    constexpr auto cases = std::array{
        Case{
            "paintman.image-profile-v2.json",
            core::BodyProfile::Round,
        },
        Case{
            "paintman_cube.image-profile-v2.json",
            core::BodyProfile::Cube,
        },
        Case{
            "paintman_hukuyoka.image-profile-v2.json",
            core::BodyProfile::Fukuyoka,
        },
    };

    auto passed = true;
    auto generated = std::vector<core::ImageGuideBitmap>{};
    for (const auto& test : cases)
    {
        const auto profile =
            application::decode_canonical_image_profile(
                read_file(root / test.file),
                test.body);
        const auto guide =
            profile
                ? core::build_image_guide_bitmap(*profile)
                : std::expected<
                      core::ImageGuideBitmap,
                      core::ImageGuideError>{
                      std::unexpected(
                          core::ImageGuideError::InvalidProfile)};
        const auto repeated =
            profile
                ? core::build_image_guide_bitmap(*profile)
                : std::expected<
                      core::ImageGuideBitmap,
                      core::ImageGuideError>{
                      std::unexpected(
                          core::ImageGuideError::InvalidProfile)};
        const auto counts =
            guide ? tile_alpha_counts(*guide)
                  : std::array<std::size_t, 4U>{};
        const auto expected_bone_segments =
            profile && profile->geometry.bones
                ? static_cast<std::size_t>(
                      std::ranges::count_if(
                          *profile->geometry.bones,
                          [](const core::ImageReferenceBone& bone)
                          {
                              return bone.parent.has_value();
                          })) *
                      4U
                : 0U;
        const auto white_pixels =
            guide && guide->rgba &&
            std::ranges::all_of(
                std::views::iota(
                    std::size_t{},
                    guide->rgba->size() / 4U),
                [&guide](std::size_t pixel)
                {
                    const auto offset = pixel * 4U;
                    const auto alpha =
                        std::to_integer<std::uint8_t>(
                            guide->rgba->at(offset + 3U));
                    return alpha == 0U ||
                           (guide->rgba->at(offset) ==
                                std::byte{0xFF} &&
                            guide->rgba->at(offset + 1U) ==
                                std::byte{0xFF} &&
                            guide->rgba->at(offset + 2U) ==
                                std::byte{0xFF});
                });
        passed &= expect(
            profile && guide && repeated &&
                guide->schema_version ==
                    core::ImageGuideSchemaVersion &&
                guide->profile ==
                    core::expected_mesh_profile(
                        test.body,
                        core::MeshProfileRole::ImageReference) &&
                guide->width == core::CanonicalAtlasWidth &&
                guide->height == core::CanonicalAtlasHeight &&
                guide->rgba &&
                guide->rgba->size() ==
                    core::CanonicalAtlasByteLength &&
                guide->projected_triangles > 0U &&
                guide->projected_triangles <=
                    guide->profile.triangle_count * 4U &&
                guide->bone_segments ==
                    expected_bone_segments &&
                (test.body == core::BodyProfile::Cube
                     ? guide->projected_triangles <=
                           guide->profile.triangle_count
                     : guide->projected_triangles >
                           guide->profile.triangle_count) &&
                std::ranges::all_of(
                    counts,
                    [](std::size_t count)
                    {
                        return count > 1'000U;
                    }) &&
                white_pixels &&
                repeated->rgba &&
                *repeated->rgba == *guide->rgba &&
                repeated->projected_triangles ==
                    guide->projected_triangles &&
                repeated->bone_segments ==
                    guide->bone_segments,
            "a packaged profile did not produce one deterministic "
            "four-face guide");
        if (guide)
        {
            generated.push_back(*guide);
        }
    }

    passed &= expect(
        generated.size() == cases.size() &&
            *generated[0U].rgba != *generated[1U].rgba &&
            *generated[0U].rgba != *generated[2U].rgba &&
            *generated[1U].rgba != *generated[2U].rgba,
        "distinct body profiles produced the same guide");

    auto cancellation = std::stop_source{};
    cancellation.request_stop();
    const auto cancelled_profile =
        application::decode_canonical_image_profile(
            read_file(root / cases.front().file),
            cases.front().body);
    const auto cancelled =
        cancelled_profile
            ? core::build_image_guide_bitmap(
                  *cancelled_profile,
                  cancellation.get_token())
            : std::expected<
                  core::ImageGuideBitmap,
                  core::ImageGuideError>{
                  std::unexpected(
                      core::ImageGuideError::InvalidProfile)};
    passed &= expect(
        !cancelled &&
            cancelled.error() ==
                core::ImageGuideError::Cancelled,
        "pre-cancelled guide generation entered rasterization");

    if (passed)
    {
        std::cout << "PASS image_guide\n";
        return 0;
    }
    return 1;
}
