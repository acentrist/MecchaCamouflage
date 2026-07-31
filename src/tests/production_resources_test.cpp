#include <meccha/application/production_resources.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{
using namespace meccha;

auto expect(bool condition, std::string_view message) -> bool
{
    if (!condition)
    {
        std::cerr << "FAIL production_resources: "
                  << message << '\n';
    }
    return condition;
}
} // namespace

auto main(int argc, char** argv) -> int
{
    if (argc != 2)
    {
        std::cerr
            << "usage: production_resources_test <resource-root>\n";
        return 2;
    }

    auto passed = true;
    const auto root =
        std::filesystem::absolute(std::filesystem::path{argv[1]});
    const auto loaded =
        application::load_production_resources(root);
    passed &= expect(
        loaded &&
            loaded->image_paint_profiles &&
            loaded->image_paint_profiles->size() ==
                application::ImagePaintProfilePairCount &&
            loaded->localization.locale_count() ==
                application::SupportedLocaleInfo.size(),
        "the complete production catalog did not load");

    constexpr auto Bodies = std::array{
        core::BodyProfile::Round,
        core::BodyProfile::Cube,
        core::BodyProfile::Fukuyoka,
    };
    for (auto index = std::size_t{};
         loaded && index < Bodies.size();
         ++index)
    {
        const auto& guide = loaded->image_guides[index];
        passed &= expect(
            guide.profile ==
                    core::expected_mesh_profile(
                        Bodies[index],
                        core::MeshProfileRole::ImageReference) &&
                guide.width == core::CanonicalAtlasWidth &&
                guide.height == core::CanonicalAtlasHeight &&
                guide.rgba &&
                guide.rgba->size() ==
                    core::CanonicalAtlasByteLength &&
                guide.encoded_png &&
                !guide.encoded_png->empty(),
            "a production Image Paint guide is incomplete");
    }

    const auto relative =
        application::load_production_resources("resources");
    passed &= expect(
        !relative &&
            relative.error().code ==
                application::ProductionResourceErrorCode::
                    InvalidRoot,
        "a relative production resource root was accepted");

    const auto missing =
        application::load_production_resources(
            root / "missing-production-resources");
    passed &= expect(
        !missing &&
            missing.error().code ==
                application::ProductionResourceErrorCode::
                    ProfileCatalog,
        "a missing production resource tree did not fail closed");

    if (passed)
    {
        std::cout << "PASS production_resources\n";
        return 0;
    }
    return 1;
}
