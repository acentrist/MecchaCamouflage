#include <meccha/application/production_resources.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace meccha::application
{
namespace
{
auto error(
    ProductionResourceErrorCode code,
    std::string detail)
    -> std::unexpected<ProductionResourceError>
{
    return std::unexpected(ProductionResourceError{
        code,
        std::move(detail),
    });
}

auto read_localization(const std::filesystem::path& path)
    -> std::expected<std::string, ProductionResourceError>
{
    auto stream = std::ifstream{path, std::ios::binary};
    if (!stream)
    {
        return error(
            ProductionResourceErrorCode::LocalizationRead,
            "The localization catalog could not be opened.");
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end <= 0 ||
        static_cast<std::uint64_t>(end) >
            MaximumLocalizationBytes)
    {
        return error(
            ProductionResourceErrorCode::LocalizationRead,
            "The localization catalog size is invalid.");
    }
    auto content = std::string(
        static_cast<std::size_t>(end),
        '\0');
    stream.seekg(0, std::ios::beg);
    stream.read(
        content.data(),
        static_cast<std::streamsize>(content.size()));
    if (!stream)
    {
        return error(
            ProductionResourceErrorCode::LocalizationRead,
            "The localization catalog could not be read.");
    }
    return content;
}
} // namespace

auto derive_production_resource_root(
    const std::filesystem::path& module_file)
    -> std::expected<
        std::filesystem::path,
        ProductionResourceError>
{
    try
    {
        if (module_file.empty() ||
            !module_file.is_absolute() ||
            module_file.lexically_normal() != module_file ||
            module_file.filename() != "main.dll")
        {
            return error(
                ProductionResourceErrorCode::InvalidModulePath,
                "The production module path is invalid.");
        }
        const auto dll_directory = module_file.parent_path();
        const auto mod_directory = dll_directory.parent_path();
        if (dll_directory.filename() != "dlls" ||
            mod_directory.filename() != "MecchaCamouflage")
        {
            return error(
                ProductionResourceErrorCode::InvalidModulePath,
                "The production module layout is invalid.");
        }
        const auto resource_root =
            mod_directory / "resources";
        if (!resource_root.is_absolute() ||
            resource_root.lexically_normal() != resource_root)
        {
            return error(
                ProductionResourceErrorCode::InvalidModulePath,
                "The production resource path is invalid.");
        }
        return resource_root;
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return error(
            ProductionResourceErrorCode::InvalidModulePath,
            "The production module path could not be inspected.");
    }
    catch (const std::system_error&)
    {
        return error(
            ProductionResourceErrorCode::InvalidModulePath,
            "The production module path could not be constructed.");
    }
    catch (...)
    {
        return error(
            ProductionResourceErrorCode::Construction,
            "The production resource path could not be derived.");
    }
}

auto load_production_resources(
    const std::filesystem::path& resource_root)
    -> std::expected<
        ProductionResources,
        ProductionResourceError>
{
    try
    {
        if (resource_root.empty() ||
            !resource_root.is_absolute())
        {
            return error(
                ProductionResourceErrorCode::InvalidRoot,
                "The production resource root must be absolute.");
        }

        auto catalog = load_image_paint_profile_catalog(
            resource_root / "mesh-profiles");
        if (!catalog)
        {
            return error(
                ProductionResourceErrorCode::ProfileCatalog,
                catalog.error().detail);
        }

        auto localization_text = read_localization(
            resource_root / "localization" / "catalog.json");
        if (!localization_text)
        {
            return std::unexpected(
                std::move(localization_text.error()));
        }
        auto localization =
            LocalizationCatalog::parse(*localization_text);
        if (!localization)
        {
            return error(
                ProductionResourceErrorCode::LocalizationParse,
                localization.error().detail);
        }

        auto shared_catalog =
            std::make_shared<const ImagePaintProfileCatalog>(
                std::move(*catalog));
        auto guides = std::array<core::ImageGuideBitmap, 3U>{};
        constexpr auto Bodies = std::array{
            core::BodyProfile::Round,
            core::BodyProfile::Cube,
            core::BodyProfile::Fukuyoka,
        };
        for (auto index = std::size_t{};
             index < Bodies.size();
             ++index)
        {
            const auto pair =
                shared_catalog->find(Bodies[index]);
            if (!pair)
            {
                return error(
                    ProductionResourceErrorCode::ProfileCatalog,
                    "The production profile catalog is incomplete.");
            }
            auto guide =
                core::build_image_guide_bitmap(pair->image);
            if (!guide)
            {
                return error(
                    ProductionResourceErrorCode::ImageGuide,
                    "A production Image Paint guide could not be "
                    "built.");
            }
            guides[index] = std::move(*guide);
        }

        return ProductionResources{
            std::move(shared_catalog),
            std::move(*localization),
            std::move(guides),
        };
    }
    catch (const std::bad_alloc&)
    {
        return error(
            ProductionResourceErrorCode::Construction,
            "Production resources could not be allocated.");
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return error(
            ProductionResourceErrorCode::Construction,
            "Production resource paths could not be inspected.");
    }
    catch (const std::system_error&)
    {
        return error(
            ProductionResourceErrorCode::Construction,
            "Production resources could not be constructed.");
    }
    catch (...)
    {
        return error(
            ProductionResourceErrorCode::Construction,
            "Production resource loading failed.");
    }
}
} // namespace meccha::application
