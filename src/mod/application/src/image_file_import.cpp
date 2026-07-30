#include <meccha/application/image_file_import.hpp>

#include <meccha/common/hash.hpp>

#include <algorithm>
#include <limits>
#include <ranges>
#include <string_view>
#include <utility>

namespace meccha::application
{
namespace
{
auto import_error(
    ImageFileImportErrorCode code,
    std::string detail) -> std::unexpected<ImageFileImportError>
{
    return std::unexpected(ImageFileImportError{
        code,
        std::nullopt,
        std::move(detail),
    });
}

auto hash_error(PresetHashError error)
    -> std::unexpected<ImageFileImportError>
{
    return std::unexpected(ImageFileImportError{
        error.code == PresetHashErrorCode::Unavailable
            ? ImageFileImportErrorCode::HashUnavailable
            : ImageFileImportErrorCode::HashFailure,
        error,
        error.detail,
    });
}

auto same_source_metadata(
    const core::ImageLayer& layer,
    core::ImageMime mime,
    std::size_t byte_length) -> bool
{
    return layer.mime == mime &&
           layer.source_bytes == byte_length;
}

auto checked_add(
    std::size_t left,
    std::size_t right) -> std::optional<std::size_t>
{
    if (right > std::numeric_limits<std::size_t>::max() - left)
    {
        return std::nullopt;
    }
    return left + right;
}
} // namespace

auto prepare_image_file_import(
    const ImageEditorDocumentSnapshot& document,
    std::span<const PickedImageFile> files,
    PresetHasher& hasher)
    -> std::expected<AddImageLayersMutation, ImageFileImportError>
{
    if (!core::valid_image_project_id(document.project_id) ||
        document.revision == 0U ||
        document.layers.empty() ||
        document.layers.size() > core::MaximumImageLayers ||
        !core::validate(document.settings).empty() ||
        std::ranges::any_of(
            document.layers,
            [](const core::ImageLayer& layer)
            {
                return !core::validate(layer).empty();
            }))
    {
        return import_error(
            ImageFileImportErrorCode::InvalidDocument,
            "The active Image Paint document is invalid.");
    }
    if (files.empty())
    {
        return import_error(
            ImageFileImportErrorCode::EmptySelection,
            "No image files were selected.");
    }
    if (files.size() >
        core::MaximumImageLayers - document.layers.size())
    {
        return import_error(
            ImageFileImportErrorCode::LayerLimit,
            "The selected images exceed the project layer limit.");
    }

    auto existing_sources = std::vector<core::ImageLayer>{};
    existing_sources.reserve(document.layers.size());
    auto existing_bytes = std::size_t{};
    for (const auto& layer : document.layers)
    {
        const auto found = std::ranges::find_if(
            existing_sources,
            [&layer](const core::ImageLayer& candidate)
            {
                return candidate.asset_id == layer.asset_id;
            });
        if (found != existing_sources.end())
        {
            if (!same_source_metadata(
                    *found,
                    layer.mime,
                    layer.source_bytes))
            {
                return import_error(
                    ImageFileImportErrorCode::InvalidDocument,
                    "The active project has inconsistent source metadata.");
            }
            continue;
        }
        const auto total =
            checked_add(existing_bytes, layer.source_bytes);
        if (!total ||
            *total > core::MaximumProjectSourceBytes)
        {
            return import_error(
                ImageFileImportErrorCode::InvalidDocument,
                "The active project source size is invalid.");
        }
        existing_bytes = *total;
        existing_sources.push_back(layer);
    }

    auto mutation = AddImageLayersMutation{};
    mutation.layers.reserve(files.size());
    mutation.sources.reserve(files.size());
    auto total_bytes = existing_bytes;
    for (const auto& file : files)
    {
        if (!file.bytes || file.bytes->empty() ||
            file.bytes->size() > core::MaximumImageSourceBytes)
        {
            return import_error(
                ImageFileImportErrorCode::InvalidFile,
                "A selected image file is empty or exceeds 12 MiB.");
        }
        const auto digest = hasher.hash(*file.bytes);
        if (!digest)
        {
            return hash_error(digest.error());
        }
        const auto asset_id = common::sha256_hex(*digest);
        auto layer = core::ImageLayer{
            asset_id,
            file.file_name,
            file.mime,
            file.bytes->size(),
        };
        if (!core::validate(layer).empty())
        {
            return import_error(
                ImageFileImportErrorCode::InvalidFile,
                "A selected image has invalid name, codec, size, or placement.");
        }

        const auto existing = std::ranges::find_if(
            existing_sources,
            [&asset_id](const core::ImageLayer& candidate)
            {
                return candidate.asset_id == asset_id;
            });
        if (existing != existing_sources.end())
        {
            if (!same_source_metadata(
                    *existing,
                    file.mime,
                    file.bytes->size()))
            {
                return import_error(
                    ImageFileImportErrorCode::IdentityConflict,
                    "A selected image conflicts with an existing source identity.");
            }
        }
        else
        {
            const auto pending = std::ranges::find_if(
                mutation.sources,
                [&asset_id](
                    const core::ImageSourceAsset& source)
                {
                    return source.asset_id == asset_id;
                });
            if (pending != mutation.sources.end())
            {
                if (pending->mime != file.mime ||
                    !pending->bytes ||
                    *pending->bytes != *file.bytes)
                {
                    return import_error(
                        ImageFileImportErrorCode::IdentityConflict,
                        "Selected image bytes share a conflicting identity.");
                }
            }
            else
            {
                if (mutation.sources.size() >=
                    core::MaximumImageSources -
                        existing_sources.size())
                {
                    return import_error(
                        ImageFileImportErrorCode::SourceLimit,
                        "The selected images exceed the project source limit.");
                }
                const auto next_total =
                    checked_add(total_bytes, file.bytes->size());
                if (!next_total ||
                    *next_total >
                        core::MaximumProjectSourceBytes)
                {
                    return import_error(
                        ImageFileImportErrorCode::SourceBytesLimit,
                        "The selected images exceed the 64 MiB project source limit.");
                }
                total_bytes = *next_total;
                mutation.sources.push_back(
                    core::ImageSourceAsset{
                        asset_id,
                        file.mime,
                        file.bytes,
                    });
            }
        }
        mutation.layers.push_back(std::move(layer));
    }
    return mutation;
}
} // namespace meccha::application
