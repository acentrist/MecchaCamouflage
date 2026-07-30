#pragma once

#include <meccha/application/image_file_import.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace meccha::application
{
struct PickedImageProjectFile
{
    std::string file_name{};
    std::shared_ptr<const std::vector<std::byte>> bytes{};

    auto operator==(const PickedImageProjectFile&) const -> bool = default;
};

enum class ImageFilePickerErrorCode : std::uint8_t
{
    ComInitialization,
    DialogCreation,
    DialogConfiguration,
    DialogFailure,
    SelectionFailure,
    TooManyFiles,
    InvalidPath,
    InvalidFileName,
    UnsupportedExtension,
    FileOpen,
    ReparsePoint,
    FileSize,
    FileRead,
};

struct ImageFilePickerError
{
    ImageFilePickerErrorCode code{};
    std::int32_t native_status{};
    std::string detail{};

    auto operator==(const ImageFilePickerError&) const -> bool = default;
};

template <typename Selection>
using ImageFilePickerResult = std::expected<
    std::optional<Selection>,
    ImageFilePickerError>;

class ImageFilePickerPort
{
public:
    ImageFilePickerPort() = default;
    ImageFilePickerPort(const ImageFilePickerPort&) = delete;
    auto operator=(const ImageFilePickerPort&)
        -> ImageFilePickerPort& = delete;
    ImageFilePickerPort(ImageFilePickerPort&&) = delete;
    auto operator=(ImageFilePickerPort&&)
        -> ImageFilePickerPort& = delete;
    virtual ~ImageFilePickerPort() = default;

    [[nodiscard]] virtual auto pick_images(
        std::uintptr_t owner_window)
        -> ImageFilePickerResult<std::vector<PickedImageFile>> = 0;

    [[nodiscard]] virtual auto pick_image_project(
        std::uintptr_t owner_window)
        -> ImageFilePickerResult<PickedImageProjectFile> = 0;
};

#ifdef _WIN32
class NativeImageFilePicker final : public ImageFilePickerPort
{
public:
    [[nodiscard]] auto pick_images(
        std::uintptr_t owner_window)
        -> ImageFilePickerResult<
            std::vector<PickedImageFile>> override;

    [[nodiscard]] auto pick_image_project(
        std::uintptr_t owner_window)
        -> ImageFilePickerResult<PickedImageProjectFile> override;
};
#endif
} // namespace meccha::application
