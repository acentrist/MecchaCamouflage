#pragma once

#include <meccha/application/image_editor_contract.hpp>
#include <meccha/application/image_project_codec.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace meccha::application
{
struct PickedImageFile
{
    std::string file_name{};
    core::ImageMime mime{core::ImageMime::Png};
    std::shared_ptr<const std::vector<std::byte>> bytes{};

    auto operator==(const PickedImageFile&) const -> bool = default;
};

enum class ImageFileImportErrorCode : std::uint8_t
{
    InvalidDocument,
    EmptySelection,
    LayerLimit,
    SourceLimit,
    SourceBytesLimit,
    InvalidFile,
    IdentityConflict,
    HashUnavailable,
    HashFailure,
};

struct ImageFileImportError
{
    ImageFileImportErrorCode code{};
    std::optional<PresetHashError> hash{};
    std::string detail{};

    auto operator==(const ImageFileImportError&) const -> bool = default;
};

[[nodiscard]] auto prepare_image_file_import(
    const ImageEditorDocumentSnapshot& document,
    std::span<const PickedImageFile> files,
    PresetHasher& hasher)
    -> std::expected<AddImageLayersMutation, ImageFileImportError>;
} // namespace meccha::application
