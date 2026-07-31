#include <meccha/application/production_resources.hpp>

#include "strict_json.hpp"

#include <meccha/common/hash.hpp>
#include <meccha/core/png_encoder.hpp>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace production_resource_detail
{
constexpr auto MaximumFallbackManifestBytes =
    std::size_t{2U * 1024U * 1024U};
constexpr auto FallbackAtlasSha256 =
    std::string_view{
        "3b5fbf3eff04660e0342f6d1007ee947"
        "efdeccee77dad87b003f237f83c13d59"};

struct RawFallbackSource
{
    std::string name{};
    std::string tag{};
    std::string commit{};
    std::string file{};
    std::string sha256{};
    std::string url{};
    std::string license{};
    std::string license_sha256{};
};

struct RawFallbackAtlas
{
    std::string file{};
    std::string sha256{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t cell_width{};
    std::uint32_t cell_height{};
    std::uint32_t columns{};
    std::uint32_t point_size{};
    std::uint32_t padding_x{};
    std::uint32_t padding_y{};
    std::string pixel_format{};
};

struct RawFallbackGlyph
{
    std::string codepoint{};
    std::uint32_t index{};
    std::uint32_t advance{};
};

struct RawFallbackManifest
{
    std::uint32_t schema_version{};
    RawFallbackSource source{};
    RawFallbackAtlas atlas{};
    std::string generator{};
    std::vector<RawFallbackGlyph> glyphs{};
};

} // namespace production_resource_detail

namespace
{
using namespace production_resource_detail;

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

auto read_text_file(
    const std::filesystem::path& path,
    std::size_t limit,
    ProductionResourceErrorCode code,
    std::string_view description)
    -> std::expected<std::string, ProductionResourceError>
{
    auto stream = std::ifstream{
        path,
        std::ios::binary | std::ios::ate,
    };
    if (!stream)
    {
        return error(
            code,
            std::string{description} + " could not be opened.");
    }
    const auto end = stream.tellg();
    if (end <= 0 ||
        static_cast<std::uintmax_t>(end) > limit)
    {
        return error(
            code,
            std::string{description} + " size is invalid.");
    }
    auto content = std::string(
        static_cast<std::size_t>(end),
        '\0');
    stream.seekg(0, std::ios::beg);
    if (!stream.read(
            content.data(),
            static_cast<std::streamsize>(content.size())))
    {
        return error(
            code,
            std::string{description} + " could not be read.");
    }
    return content;
}

auto read_binary_file(
    const std::filesystem::path& path,
    std::size_t limit,
    ProductionResourceErrorCode code,
    std::string_view description)
    -> std::expected<
        std::shared_ptr<const std::vector<std::byte>>,
        ProductionResourceError>
{
    auto stream = std::ifstream{
        path,
        std::ios::binary | std::ios::ate,
    };
    if (!stream)
    {
        return error(
            code,
            std::string{description} + " could not be opened.");
    }
    const auto end = stream.tellg();
    if (end <= 0 ||
        static_cast<std::uintmax_t>(end) > limit)
    {
        return error(
            code,
            std::string{description} + " size is invalid.");
    }
    auto content = std::vector<std::byte>(
        static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(
            reinterpret_cast<char*>(content.data()),
            static_cast<std::streamsize>(content.size())))
    {
        return error(
            code,
            std::string{description} + " could not be read.");
    }
    return std::make_shared<const std::vector<std::byte>>(
        std::move(content));
}

auto parse_codepoint(std::string_view text)
    -> std::optional<char32_t>
{
    if (!text.starts_with("U+") || text.size() < 6U ||
        text.size() > 8U)
    {
        return std::nullopt;
    }
    const auto digits = text.substr(2U);
    if ((digits.size() > 4U && digits.front() == '0') ||
        !std::ranges::all_of(
            digits,
            [](char character)
            {
                return (character >= '0' && character <= '9') ||
                       (character >= 'A' && character <= 'F');
            }))
    {
        return std::nullopt;
    }
    auto value = std::uint32_t{};
    const auto [end, failure] = std::from_chars(
        digits.data(),
        digits.data() + digits.size(),
        value,
        16);
    if (failure != std::errc{} ||
        end != digits.data() + digits.size() ||
        value > 0x10FFFFU ||
        (value >= 0xD800U && value <= 0xDFFFU))
    {
        return std::nullopt;
    }
    return static_cast<char32_t>(value);
}

auto valid_fallback_provenance(
    const RawFallbackManifest& manifest) -> bool
{
    return manifest.schema_version == 1U &&
           manifest.source.name == "Noto Sans CJK" &&
           manifest.source.tag == "Sans2.004" &&
           manifest.source.commit ==
               "523d033d6cb47f4a80c58a35753646f5c3608a78" &&
           manifest.source.file == "NotoSansCJK-Regular.ttc" &&
           manifest.source.sha256 ==
               "b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a" &&
           manifest.source.url ==
               "https://github.com/notofonts/noto-cjk/blob/"
               "523d033d6cb47f4a80c58a35753646f5c3608a78/"
               "Sans/OTC/NotoSansCJK-Regular.ttc" &&
           manifest.source.license ==
               "SIL Open Font License 1.1" &&
           manifest.source.license_sha256 ==
               "6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2" &&
           manifest.generator.starts_with(
               "Version: ImageMagick ") &&
           manifest.generator.size() <= 256U &&
           manifest.atlas.file == "fallback-glyph-atlas.png" &&
           manifest.atlas.sha256 == FallbackAtlasSha256 &&
           manifest.atlas.width == 1536U &&
           manifest.atlas.height == 1440U &&
           manifest.atlas.cell_width == 48U &&
           manifest.atlas.cell_height == 48U &&
           manifest.atlas.columns == 32U &&
           manifest.atlas.point_size == 32U &&
           manifest.atlas.padding_x == 6U &&
           manifest.atlas.padding_y == 4U &&
           manifest.atlas.pixel_format == "RGBA8";
}

auto load_fallback_glyph_atlas(
    const std::filesystem::path& root,
    std::span<const char32_t> required_codepoints)
    -> std::expected<
        std::shared_ptr<const core::FallbackGlyphAtlas>,
        ProductionResourceError>
{
    const auto directory = root / "fonts" / "fallback";
    const auto manifest_text = read_text_file(
        directory / "fallback-glyph-atlas.json",
        MaximumFallbackManifestBytes,
        ProductionResourceErrorCode::FallbackGlyphAtlasRead,
        "The fallback glyph manifest");
    if (!manifest_text)
    {
        return std::unexpected(manifest_text.error());
    }
    const auto document =
        detail::validate_strict_json_document(*manifest_text);
    if (!document)
    {
        return error(
            ProductionResourceErrorCode::FallbackGlyphAtlasParse,
            document.error().detail);
    }
    auto manifest = RawFallbackManifest{};
    const auto parsed =
        glz::read<detail::StrictJson>(manifest, *manifest_text);
    if (parsed)
    {
        return error(
            ProductionResourceErrorCode::FallbackGlyphAtlasParse,
            glz::format_error(parsed, *manifest_text));
    }
    if (!valid_fallback_provenance(manifest) ||
        manifest.glyphs.empty() ||
        manifest.glyphs.size() > core::MaximumFallbackGlyphs)
    {
        return error(
            ProductionResourceErrorCode::FallbackGlyphAtlas,
            "The fallback glyph manifest is not the frozen resource.");
    }

    auto glyphs = std::vector<core::FallbackGlyph>{};
    glyphs.reserve(manifest.glyphs.size());
    for (const auto& raw : manifest.glyphs)
    {
        const auto codepoint = parse_codepoint(raw.codepoint);
        if (!codepoint)
        {
            return error(
                ProductionResourceErrorCode::FallbackGlyphAtlas,
                "A fallback glyph codepoint is invalid.");
        }
        glyphs.push_back(core::FallbackGlyph{
            *codepoint,
            raw.index,
            raw.advance,
        });
    }
    const auto png = read_binary_file(
        directory / manifest.atlas.file,
        static_cast<std::size_t>(
            core::MaximumCanonicalPngEncodedBytes),
        ProductionResourceErrorCode::FallbackGlyphAtlasRead,
        "The fallback glyph atlas");
    if (!png)
    {
        return std::unexpected(png.error());
    }
#ifdef _WIN32
    const auto digest = common::sha256_bytes(**png);
    if (!digest || common::sha256_hex(*digest) !=
                       manifest.atlas.sha256)
    {
        return error(
            ProductionResourceErrorCode::FallbackGlyphAtlas,
            "The fallback glyph atlas hash is invalid.");
    }
#endif
    auto atlas = core::FallbackGlyphAtlas::create(
        core::FallbackGlyphAtlasGeometry{
            manifest.atlas.width,
            manifest.atlas.height,
            manifest.atlas.cell_width,
            manifest.atlas.cell_height,
            manifest.atlas.columns,
            manifest.atlas.point_size,
            manifest.atlas.padding_x,
            manifest.atlas.padding_y,
        },
        glyphs,
        *png,
        required_codepoints);
    if (!atlas)
    {
        return error(
            ProductionResourceErrorCode::FallbackGlyphAtlas,
            "The fallback glyph atlas failed validation.");
    }
    return std::make_shared<const core::FallbackGlyphAtlas>(
        std::move(*atlas));
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

        auto fallback_glyph_atlas = load_fallback_glyph_atlas(
            resource_root,
            localization->codepoints());
        if (!fallback_glyph_atlas)
        {
            return std::unexpected(
                std::move(fallback_glyph_atlas.error()));
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
            std::move(*fallback_glyph_atlas),
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
