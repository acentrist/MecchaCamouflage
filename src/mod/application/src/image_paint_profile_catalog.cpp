#include <meccha/application/image_paint_profile_catalog.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
struct ProfileFiles
{
    core::BodyProfile body{};
    std::string_view raw{};
    std::string_view image{};
};

constexpr std::array ProfileFileSet{
    ProfileFiles{
        core::BodyProfile::Round,
        "paintman.mesh-profile-v2.json",
        "paintman.image-profile-v2.json",
    },
    ProfileFiles{
        core::BodyProfile::Cube,
        "paintman_cube.mesh-profile-v2.json",
        "paintman_cube.image-profile-v2.json",
    },
    ProfileFiles{
        core::BodyProfile::Fukuyoka,
        "paintman_hukuyoka.mesh-profile-v2.json",
        "paintman_hukuyoka.image-profile-v2.json",
    },
};

auto body_index(core::BodyProfile body)
    -> std::optional<std::size_t>
{
    switch (body)
    {
    case core::BodyProfile::Round:
        return 0U;
    case core::BodyProfile::Cube:
        return 1U;
    case core::BodyProfile::Fukuyoka:
        return 2U;
    }
    return std::nullopt;
}

auto error(
    ImagePaintProfileCatalogErrorCode code,
    std::optional<core::BodyProfile> body = std::nullopt,
    std::string detail = {})
    -> std::unexpected<ImagePaintProfileCatalogError>
{
    return std::unexpected(ImagePaintProfileCatalogError{
        code,
        body,
        std::nullopt,
        std::move(detail),
    });
}

auto codec_error(
    ImagePaintProfileCatalogErrorCode code,
    core::BodyProfile body,
    const MeshProfileCodecError& cause)
    -> std::unexpected<ImagePaintProfileCatalogError>
{
    return std::unexpected(ImagePaintProfileCatalogError{
        code,
        body,
        cause.code,
        cause.detail,
    });
}

auto read_profile_file(const std::filesystem::path& path)
    -> std::expected<std::string, ImagePaintProfileCatalogError>
{
    auto stream = std::ifstream{
        path,
        std::ios::binary | std::ios::ate,
    };
    if (!stream)
    {
        return error(
            ImagePaintProfileCatalogErrorCode::FileRead,
            std::nullopt,
            "Profile file could not be opened: " + path.string());
    }
    const auto end = stream.tellg();
    if (end < 0 ||
        static_cast<std::uintmax_t>(end) >
            MaximumMeshProfileBytes)
    {
        return error(
            ImagePaintProfileCatalogErrorCode::FileRead,
            std::nullopt,
            "Profile file size is invalid: " + path.string());
    }
    auto content = std::string(
        static_cast<std::size_t>(end),
        '\0');
    stream.seekg(0, std::ios::beg);
    if (!content.empty() &&
        !stream.read(
            content.data(),
            static_cast<std::streamsize>(content.size())))
    {
        return error(
            ImagePaintProfileCatalogErrorCode::FileRead,
            std::nullopt,
            "Profile file could not be read: " + path.string());
    }
    return content;
}
} // namespace

auto ImagePaintProfileCatalog::create(
    std::span<const ImagePaintProfileDocuments> documents)
    -> std::expected<
        ImagePaintProfileCatalog,
        ImagePaintProfileCatalogError>
{
    try
    {
        if (documents.size() != ImagePaintProfilePairCount)
        {
            return error(
                ImagePaintProfileCatalogErrorCode::
                    InvalidDocumentCount);
        }
        auto catalog = ImagePaintProfileCatalog{};
        for (const auto& document : documents)
        {
            const auto position = body_index(document.body);
            if (!position)
            {
                return error(
                    ImagePaintProfileCatalogErrorCode::MissingBody);
            }
            if (catalog.pairs_[*position])
            {
                return error(
                    ImagePaintProfileCatalogErrorCode::DuplicateBody,
                    document.body);
            }
            const auto sampling = decode_paint_sampling_profile(
                document.raw_json,
                document.body);
            if (!sampling)
            {
                return codec_error(
                    ImagePaintProfileCatalogErrorCode::RawProfile,
                    document.body,
                    sampling.error());
            }
            const auto image = decode_canonical_image_profile(
                document.image_json,
                document.body);
            if (!image)
            {
                return codec_error(
                    ImagePaintProfileCatalogErrorCode::ImageProfile,
                    document.body,
                    image.error());
            }
            if (!core::validate_pair(*sampling, *image).empty())
            {
                return error(
                    ImagePaintProfileCatalogErrorCode::InvalidPair,
                    document.body,
                    "Raw and ImageReference profiles do not form an "
                    "exact topology pair.");
            }
            catalog.pairs_[*position] =
                std::make_shared<const ImagePaintProfilePair>(
                    ImagePaintProfilePair{
                        std::move(*sampling),
                        std::move(*image),
                    });
        }
        for (const auto& pair : catalog.pairs_)
        {
            if (!pair)
            {
                return error(
                    ImagePaintProfileCatalogErrorCode::MissingBody);
            }
        }
        return catalog;
    }
    catch (...)
    {
        return error(
            ImagePaintProfileCatalogErrorCode::Construction);
    }
}

auto ImagePaintProfileCatalog::find(
    core::BodyProfile body) const noexcept
    -> std::shared_ptr<const ImagePaintProfilePair>
{
    const auto position = body_index(body);
    return position ? pairs_[*position] : nullptr;
}

auto ImagePaintProfileCatalog::size() const noexcept -> std::size_t
{
    return pairs_.size();
}

auto load_image_paint_profile_catalog(
    const std::filesystem::path& profile_directory)
    -> std::expected<
        ImagePaintProfileCatalog,
        ImagePaintProfileCatalogError>
{
    try
    {
        auto filesystem_error = std::error_code{};
        if (profile_directory.empty() ||
            !profile_directory.is_absolute() ||
            !std::filesystem::is_directory(
                profile_directory,
                filesystem_error) ||
            filesystem_error)
        {
            return error(
                ImagePaintProfileCatalogErrorCode::
                    InvalidDirectory);
        }

        auto raw_documents =
            std::array<std::string, ImagePaintProfilePairCount>{};
        auto image_documents =
            std::array<std::string, ImagePaintProfilePairCount>{};
        auto documents = std::array<
            ImagePaintProfileDocuments,
            ImagePaintProfilePairCount>{};
        for (auto position = std::size_t{};
             position < ProfileFileSet.size();
             ++position)
        {
            const auto& files = ProfileFileSet[position];
            auto raw = read_profile_file(
                profile_directory / files.raw);
            if (!raw)
            {
                auto failure = raw.error();
                failure.body = files.body;
                return std::unexpected(std::move(failure));
            }
            auto image = read_profile_file(
                profile_directory / files.image);
            if (!image)
            {
                auto failure = image.error();
                failure.body = files.body;
                return std::unexpected(std::move(failure));
            }
            raw_documents[position] = std::move(*raw);
            image_documents[position] = std::move(*image);
            documents[position] = ImagePaintProfileDocuments{
                files.body,
                raw_documents[position],
                image_documents[position],
            };
        }
        return ImagePaintProfileCatalog::create(documents);
    }
    catch (...)
    {
        return error(
            ImagePaintProfileCatalogErrorCode::Construction);
    }
}
} // namespace meccha::application
