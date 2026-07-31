#include <meccha/application/image_paint_profile_catalog.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
using namespace meccha;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL image_paint_profile_catalog: "
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
} // namespace

auto main(int argc, char** argv) -> int
{
    if (argc != 2)
    {
        std::cerr
            << "usage: image_paint_profile_catalog_test "
               "<profile-directory>\n";
        return 2;
    }
    const auto root =
        std::filesystem::absolute(std::filesystem::path{argv[1]});
    auto passed = true;

    const auto loaded =
        application::load_image_paint_profile_catalog(root);
    passed &= expect(
        loaded &&
            loaded->size() ==
                application::ImagePaintProfilePairCount,
        "the exact packaged profile catalog did not preload");
    for (const auto body : {
             core::BodyProfile::Round,
             core::BodyProfile::Cube,
             core::BodyProfile::Fukuyoka,
         })
    {
        const auto pair = loaded ? loaded->find(body) : nullptr;
        passed &= expect(
            pair &&
                pair->sampling.identity ==
                    core::expected_mesh_profile(
                        body,
                        core::MeshProfileRole::Raw) &&
                pair->image.geometry.identity ==
                    core::expected_mesh_profile(
                        body,
                        core::MeshProfileRole::ImageReference) &&
                core::validate_pair(
                    pair->sampling,
                    pair->image)
                    .empty() &&
                pair->unreal_asset_path.starts_with("/Game/") &&
                pair->unreal_asset_path.ends_with(
                    "." +
                    pair->sampling.identity.export_name),
            "a body lookup did not retain its immutable exact pair");
    }

    const auto round_raw =
        read_file(root / "paintman.mesh-profile-v2.json");
    const auto round_image =
        read_file(root / "paintman.image-profile-v2.json");
    const auto cube_raw =
        read_file(root / "paintman_cube.mesh-profile-v2.json");
    const auto cube_image =
        read_file(root / "paintman_cube.image-profile-v2.json");
    const auto fukuyoka_raw =
        read_file(root / "paintman_hukuyoka.mesh-profile-v2.json");
    const auto fukuyoka_image =
        read_file(root / "paintman_hukuyoka.image-profile-v2.json");
    const auto duplicate = std::array{
        application::ImagePaintProfileDocuments{
            core::BodyProfile::Round,
            round_raw,
            round_image,
        },
        application::ImagePaintProfileDocuments{
            core::BodyProfile::Round,
            round_raw,
            round_image,
        },
        application::ImagePaintProfileDocuments{
            core::BodyProfile::Cube,
            cube_raw,
            cube_image,
        },
    };
    const auto duplicate_result =
        application::ImagePaintProfileCatalog::create(duplicate);
    passed &= expect(
        !duplicate_result &&
            duplicate_result.error().code ==
                application::ImagePaintProfileCatalogErrorCode::
                    DuplicateBody,
        "a duplicate body replaced an immutable catalog entry");

    const auto incomplete = std::array{
        duplicate[0U],
        duplicate[2U],
    };
    const auto incomplete_result =
        application::ImagePaintProfileCatalog::create(incomplete);
    passed &= expect(
        !incomplete_result &&
            incomplete_result.error().code ==
                application::ImagePaintProfileCatalogErrorCode::
                    InvalidDocumentCount,
        "an incomplete profile set was accepted");

    auto malformed = duplicate;
    malformed[0U].raw_json = "{}";
    malformed[1U].body = core::BodyProfile::Fukuyoka;
    const auto malformed_result =
        application::ImagePaintProfileCatalog::create(malformed);
    passed &= expect(
        !malformed_result &&
            malformed_result.error().code ==
                application::ImagePaintProfileCatalogErrorCode::
                    RawProfile &&
            malformed_result.error().body ==
                core::BodyProfile::Round &&
            malformed_result.error().codec_error ==
                application::MeshProfileCodecErrorCode::
                    MalformedJson,
        "a malformed raw profile did not fail with exact context");

    auto mismatched_image = round_image;
    const auto indices_label =
        mismatched_image.find("\"Indices\"");
    const auto first_lod_index =
        indices_label == std::string::npos
            ? std::string::npos
            : mismatched_image.find('0', indices_label);
    const auto first_triangle =
        mismatched_image.find(R"("I0":  0)");
    if (first_lod_index != std::string::npos)
    {
        mismatched_image[first_lod_index] = '1';
    }
    if (first_triangle != std::string::npos)
    {
        mismatched_image.replace(
            first_triangle,
            std::string_view{R"("I0":  0)"}.size(),
            R"("I0":  1)");
    }
    const auto mismatched = std::array{
        application::ImagePaintProfileDocuments{
            core::BodyProfile::Round,
            round_raw,
            mismatched_image,
        },
        application::ImagePaintProfileDocuments{
            core::BodyProfile::Cube,
            cube_raw,
            cube_image,
        },
        application::ImagePaintProfileDocuments{
            core::BodyProfile::Fukuyoka,
            fukuyoka_raw,
            fukuyoka_image,
        },
    };
    const auto mismatched_result =
        application::ImagePaintProfileCatalog::create(mismatched);
    passed &= expect(
        first_lod_index != std::string::npos &&
            first_triangle != std::string::npos &&
            !mismatched_result &&
            mismatched_result.error().code ==
                application::ImagePaintProfileCatalogErrorCode::
                    InvalidPair &&
            mismatched_result.error().body ==
                core::BodyProfile::Round,
        "a mismatched raw/ImageReference topology pair was accepted");

    const auto missing_directory =
        application::load_image_paint_profile_catalog(
            root / "not-a-profile-directory");
    passed &= expect(
        !missing_directory &&
            missing_directory.error().code ==
                application::ImagePaintProfileCatalogErrorCode::
                    InvalidDirectory,
        "a missing profile directory was not rejected");

    if (passed)
    {
        std::cout << "PASS image_paint_profile_catalog\n";
    }
    return passed ? 0 : 1;
}
