#include <meccha/application/mesh_profile_codec.hpp>

#include <meccha/core/image_project.hpp>
#include <meccha/core/mesh_profile.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
        std::cerr << "FAIL mesh_profile_codec: " << message << '\n';
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
} // namespace

auto main(int argc, char** argv) -> int
{
    using namespace meccha;

    if (argc != 2)
    {
        std::cerr << "usage: mesh_profile_codec_test <profile-directory>\n";
        return 2;
    }
    const auto root = std::filesystem::path{argv[1]};
    struct Case
    {
        std::string_view file{};
        core::BodyProfile body{};
        core::MeshProfileRole role{};
    };
    constexpr std::array cases{
        Case{
            "paintman.mesh-profile-v2.json",
            core::BodyProfile::Round,
            core::MeshProfileRole::Raw,
        },
        Case{
            "paintman.image-profile-v2.json",
            core::BodyProfile::Round,
            core::MeshProfileRole::ImageReference,
        },
        Case{
            "paintman_cube.mesh-profile-v2.json",
            core::BodyProfile::Cube,
            core::MeshProfileRole::Raw,
        },
        Case{
            "paintman_cube.image-profile-v2.json",
            core::BodyProfile::Cube,
            core::MeshProfileRole::ImageReference,
        },
        Case{
            "paintman_hukuyoka.mesh-profile-v2.json",
            core::BodyProfile::Fukuyoka,
            core::MeshProfileRole::Raw,
        },
        Case{
            "paintman_hukuyoka.image-profile-v2.json",
            core::BodyProfile::Fukuyoka,
            core::MeshProfileRole::ImageReference,
        },
    };

    auto passed = true;
    for (const auto& test : cases)
    {
        const auto json = read_file(root / test.file);
        const auto decoded =
            application::decode_mesh_profile_identity(
                json,
                test.body,
                test.role);
        if (!decoded)
        {
            std::cerr << test.file << ": "
                      << decoded.error().detail << " fields:";
            for (const auto field : decoded.error().fields)
            {
                std::cerr << ' ' << static_cast<int>(field);
            }
            std::cerr << '\n';
        }
        passed &= expect(
            decoded &&
                *decoded ==
                    core::expected_mesh_profile(
                        test.body,
                        test.role),
            "a packaged mesh profile failed its frozen identity");
    }

    auto aliased = read_file(
        root / "paintman_hukuyoka.mesh-profile-v2.json");
    auto position = std::size_t{};
    while ((position = aliased.find("hukuyoka", position)) !=
           std::string::npos)
    {
        aliased.replace(position, 8U, "fukuyoka");
        position += 8U;
    }
    const auto rejected_alias =
        application::decode_mesh_profile_identity(
            aliased,
            core::BodyProfile::Fukuyoka,
            core::MeshProfileRole::Raw);
    passed &= expect(
        !rejected_alias &&
            rejected_alias.error().code ==
                application::MeshProfileCodecErrorCode::InvalidProfile,
        "a profile using the UI alias as an asset name was accepted");

    auto invalid_index =
        read_file(root / "paintman.mesh-profile-v2.json");
    const auto index_position =
        invalid_index.find(R"("I0":  0)");
    if (index_position != std::string::npos)
    {
        invalid_index.replace(
            index_position,
            std::string_view{R"("I0":  0)"}.size(),
            R"("I0":  1660)");
    }
    const auto rejected_index =
        application::decode_mesh_profile_identity(
            invalid_index,
            core::BodyProfile::Round,
            core::MeshProfileRole::Raw);
    passed &= expect(
        index_position != std::string::npos &&
            !rejected_index &&
            rejected_index.error().code ==
                application::MeshProfileCodecErrorCode::InvalidProfile &&
            rejected_index.error().fields ==
                std::vector{core::MeshProfileField::IndexBounds},
        "an out-of-bounds packaged triangle index was accepted");

    const auto oversized =
        application::decode_mesh_profile_identity(
            std::string(
                application::MaximumMeshProfileBytes + 1U,
                ' '),
            core::BodyProfile::Round,
            core::MeshProfileRole::Raw);
    passed &= expect(
        !oversized &&
            oversized.error().code ==
                application::MeshProfileCodecErrorCode::TooLarge,
        "an oversized profile was accepted");

    if (passed)
    {
        std::cout << "PASS mesh_profile_codec\n";
        return 0;
    }
    return 1;
}
