#include <meccha/application/image_file_picker.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace meccha::application
{
namespace
{
namespace fs = std::filesystem;

constexpr auto MaximumPickerFiles = core::MaximumImageLayers;

template <typename Interface>
class ComObject
{
public:
    ComObject() = default;
    ComObject(const ComObject&) = delete;
    auto operator=(const ComObject&) -> ComObject& = delete;
    ComObject(ComObject&& other) noexcept
        : value_{std::exchange(other.value_, nullptr)}
    {
    }
    auto operator=(ComObject&& other) noexcept -> ComObject&
    {
        if (this != &other)
        {
            if (value_ != nullptr)
            {
                value_->Release();
            }
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    ~ComObject()
    {
        if (value_ != nullptr)
        {
            value_->Release();
        }
    }

    [[nodiscard]] auto put() -> Interface**
    {
        return &value_;
    }

    [[nodiscard]] auto get() const -> Interface*
    {
        return value_;
    }

private:
    Interface* value_{};
};

class ComInitialization
{
public:
    ComInitialization()
        : result_{CoInitializeEx(
              nullptr,
              COINIT_APARTMENTTHREADED |
                  COINIT_DISABLE_OLE1DDE)}
    {
    }
    ComInitialization(const ComInitialization&) = delete;
    auto operator=(const ComInitialization&)
        -> ComInitialization& = delete;
    ComInitialization(ComInitialization&& other) noexcept
        : result_{other.result_},
          owns_{std::exchange(other.owns_, false)}
    {
    }
    auto operator=(ComInitialization&&) -> ComInitialization& = delete;

    ~ComInitialization()
    {
        if (owns_)
        {
            CoUninitialize();
        }
    }

    [[nodiscard]] auto result() const -> HRESULT
    {
        return result_;
    }

private:
    HRESULT result_{};
    bool owns_{SUCCEEDED(result_)};
};

class Win32Handle
{
public:
    explicit Win32Handle(HANDLE value) : value_{value}
    {
    }
    Win32Handle(const Win32Handle&) = delete;
    auto operator=(const Win32Handle&) -> Win32Handle& = delete;
    ~Win32Handle()
    {
        if (value_ != nullptr &&
            value_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(value_);
        }
    }

    [[nodiscard]] auto get() const -> HANDLE
    {
        return value_;
    }

private:
    HANDLE value_{};
};

auto picker_error(
    ImageFilePickerErrorCode code,
    std::int32_t native_status,
    std::string detail)
    -> std::unexpected<ImageFilePickerError>
{
    return std::unexpected(ImageFilePickerError{
        code,
        native_status,
        std::move(detail),
    });
}

auto hresult_error(
    ImageFilePickerErrorCode code,
    std::string detail,
    HRESULT result)
    -> std::unexpected<ImageFilePickerError>
{
    return picker_error(
        code,
        static_cast<std::int32_t>(result),
        std::move(detail));
}

auto last_error(
    ImageFilePickerErrorCode code,
    std::string detail)
    -> std::unexpected<ImageFilePickerError>
{
    return picker_error(
        code,
        static_cast<std::int32_t>(GetLastError()),
        std::move(detail));
}

auto utf8(std::wstring_view value)
    -> std::expected<std::string, ImageFilePickerError>
{
    if (value.empty() ||
        value.size() >
            static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
    {
        return picker_error(
            ImageFilePickerErrorCode::InvalidFileName,
            0,
            "The selected file name is empty or too long.");
    }
    const auto count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0)
    {
        return last_error(
            ImageFilePickerErrorCode::InvalidFileName,
            "The selected file name is not valid Unicode.");
    }
    auto result =
        std::string(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            count,
            nullptr,
            nullptr) != count)
    {
        return last_error(
            ImageFilePickerErrorCode::InvalidFileName,
            "The selected file name could not be converted.");
    }
    return result;
}

auto lowercase_ascii(std::wstring value) -> std::wstring
{
    std::ranges::transform(
        value,
        value.begin(),
        [](wchar_t character)
        {
            return character >= L'A' && character <= L'Z'
                       ? static_cast<wchar_t>(
                             character - L'A' + L'a')
                       : character;
        });
    return value;
}

auto image_mime(const fs::path& path)
    -> std::optional<core::ImageMime>
{
    const auto extension =
        lowercase_ascii(path.extension().wstring());
    if (extension == L".png")
    {
        return core::ImageMime::Png;
    }
    if (extension == L".jpg" || extension == L".jpeg")
    {
        return core::ImageMime::Jpeg;
    }
    if (extension == L".webp")
    {
        return core::ImageMime::WebP;
    }
    return std::nullopt;
}

auto read_regular_file(
    const fs::path& path,
    std::size_t maximum_bytes)
    -> std::expected<
        std::shared_ptr<const std::vector<std::byte>>,
        ImageFilePickerError>
{
    if (!path.is_absolute() ||
        path.lexically_normal() != path ||
        path.filename().empty())
    {
        return picker_error(
            ImageFilePickerErrorCode::InvalidPath,
            0,
            "The selected file is not a normalized absolute path.");
    }

    const auto handle = Win32Handle{CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr)};
    if (handle.get() == INVALID_HANDLE_VALUE)
    {
        return last_error(
            ImageFilePickerErrorCode::FileOpen,
            "The selected file could not be opened safely.");
    }

    auto attributes = FILE_ATTRIBUTE_TAG_INFO{};
    if (!GetFileInformationByHandleEx(
            handle.get(),
            FileAttributeTagInfo,
            &attributes,
            sizeof(attributes)))
    {
        return last_error(
            ImageFilePickerErrorCode::FileOpen,
            "The selected file attributes could not be read.");
    }
    if ((attributes.FileAttributes &
         FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
    {
        return picker_error(
            ImageFilePickerErrorCode::ReparsePoint,
            0,
            "Reparse-point image and preset files are not accepted.");
    }
    if ((attributes.FileAttributes &
         FILE_ATTRIBUTE_DIRECTORY) != 0U)
    {
        return picker_error(
            ImageFilePickerErrorCode::InvalidPath,
            0,
            "The selected item is not a regular file.");
    }

    auto size = LARGE_INTEGER{};
    if (!GetFileSizeEx(handle.get(), &size))
    {
        return last_error(
            ImageFilePickerErrorCode::FileSize,
            "The selected file size could not be read.");
    }
    if (size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) >
            maximum_bytes)
    {
        return picker_error(
            ImageFilePickerErrorCode::FileSize,
            0,
            "The selected file is empty or exceeds its size limit.");
    }

    auto bytes = std::vector<std::byte>(
        static_cast<std::size_t>(size.QuadPart));
    auto offset = std::size_t{};
    while (offset < bytes.size())
    {
        const auto remaining = bytes.size() - offset;
        const auto request = static_cast<DWORD>(std::min(
            remaining,
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max())));
        auto read = DWORD{};
        if (!ReadFile(
                handle.get(),
                bytes.data() + offset,
                request,
                &read,
                nullptr) ||
            read == 0U)
        {
            return last_error(
                ImageFilePickerErrorCode::FileRead,
                "The selected file could not be read completely.");
        }
        offset += read;
    }
    return std::make_shared<const std::vector<std::byte>>(
        std::move(bytes));
}

auto selected_path(IShellItem& item)
    -> std::expected<fs::path, ImageFilePickerError>
{
    PWSTR raw_path{};
    const auto result = item.GetDisplayName(
        SIGDN_FILESYSPATH,
        &raw_path);
    if (FAILED(result) || raw_path == nullptr)
    {
        if (raw_path != nullptr)
        {
            CoTaskMemFree(raw_path);
        }
        return hresult_error(
            ImageFilePickerErrorCode::InvalidPath,
            "A selected item is not a filesystem path.",
            result);
    }
    auto path = fs::path{raw_path}.lexically_normal();
    CoTaskMemFree(raw_path);
    return path;
}

auto create_dialog(
    bool multiple,
    std::span<const COMDLG_FILTERSPEC> filters)
    -> std::expected<ComObject<IFileOpenDialog>, ImageFilePickerError>
{
    auto dialog = ComObject<IFileOpenDialog>{};
    auto result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(dialog.put()));
    if (FAILED(result))
    {
        return hresult_error(
            ImageFilePickerErrorCode::DialogCreation,
            "The Windows file picker could not be created.",
            result);
    }
    auto options = FILEOPENDIALOGOPTIONS{};
    result = dialog.get()->GetOptions(&options);
    if (FAILED(result))
    {
        return hresult_error(
            ImageFilePickerErrorCode::DialogConfiguration,
            "The Windows file picker options could not be read.",
            result);
    }
    options |= FOS_FORCEFILESYSTEM |
               FOS_FILEMUSTEXIST |
               FOS_PATHMUSTEXIST |
               FOS_DONTADDTORECENT;
    if (multiple)
    {
        options |= FOS_ALLOWMULTISELECT;
    }
    result = dialog.get()->SetOptions(options);
    if (FAILED(result))
    {
        return hresult_error(
            ImageFilePickerErrorCode::DialogConfiguration,
            "The Windows file picker options could not be set.",
            result);
    }
    if (filters.empty() ||
        filters.size() >
            static_cast<std::size_t>(
                std::numeric_limits<UINT>::max()))
    {
        return picker_error(
            ImageFilePickerErrorCode::DialogConfiguration,
            0,
            "The Windows file picker filter is invalid.");
    }
    result = dialog.get()->SetFileTypes(
        static_cast<UINT>(filters.size()),
        filters.data());
    if (FAILED(result))
    {
        return hresult_error(
            ImageFilePickerErrorCode::DialogConfiguration,
            "The Windows file picker filter could not be set.",
            result);
    }
    return dialog;
}

auto show_dialog(
    IFileOpenDialog& dialog,
    std::uintptr_t owner_window)
    -> std::expected<bool, ImageFilePickerError>
{
    const auto result = dialog.Show(
        reinterpret_cast<HWND>(owner_window));
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        return false;
    }
    if (FAILED(result))
    {
        return hresult_error(
            ImageFilePickerErrorCode::DialogFailure,
            "The Windows file picker failed.",
            result);
    }
    return true;
}

auto initialize_com()
    -> std::expected<ComInitialization, ImageFilePickerError>
{
    auto com = ComInitialization{};
    if (FAILED(com.result()) &&
        com.result() != RPC_E_CHANGED_MODE)
    {
        return hresult_error(
            ImageFilePickerErrorCode::ComInitialization,
            "COM could not be initialized for the file picker.",
            com.result());
    }
    return com;
}
} // namespace

auto NativeImageFilePicker::pick_images(
    std::uintptr_t owner_window)
    -> ImageFilePickerResult<std::vector<PickedImageFile>>
{
    const auto com = initialize_com();
    if (!com)
    {
        return std::unexpected(com.error());
    }
    constexpr auto Filters = std::array{
        COMDLG_FILTERSPEC{
            L"Images (*.png;*.jpg;*.jpeg;*.webp)",
            L"*.png;*.jpg;*.jpeg;*.webp",
        },
    };
    auto dialog = create_dialog(true, Filters);
    if (!dialog)
    {
        return std::unexpected(dialog.error());
    }
    const auto shown = show_dialog(*dialog->get(), owner_window);
    if (!shown)
    {
        return std::unexpected(shown.error());
    }
    if (!*shown)
    {
        return std::nullopt;
    }

    auto selections = ComObject<IShellItemArray>{};
    auto result = dialog->get()->GetResults(selections.put());
    if (FAILED(result))
    {
        return hresult_error(
            ImageFilePickerErrorCode::SelectionFailure,
            "The selected images could not be enumerated.",
            result);
    }
    auto count = DWORD{};
    result = selections.get()->GetCount(&count);
    if (FAILED(result) || count == 0U)
    {
        return hresult_error(
            ImageFilePickerErrorCode::SelectionFailure,
            "The Windows file picker returned no images.",
            result);
    }
    if (count > MaximumPickerFiles)
    {
        return picker_error(
            ImageFilePickerErrorCode::TooManyFiles,
            0,
            "The selected images exceed the layer limit.");
    }

    auto files = std::vector<PickedImageFile>{};
    files.reserve(count);
    auto total_bytes = std::size_t{};
    for (auto index = DWORD{}; index < count; ++index)
    {
        auto item = ComObject<IShellItem>{};
        result = selections.get()->GetItemAt(
            index,
            item.put());
        if (FAILED(result))
        {
            return hresult_error(
                ImageFilePickerErrorCode::SelectionFailure,
                "A selected image could not be read.",
                result);
        }
        const auto path = selected_path(*item.get());
        if (!path)
        {
            return std::unexpected(path.error());
        }
        const auto mime = image_mime(*path);
        if (!mime)
        {
            return picker_error(
                ImageFilePickerErrorCode::UnsupportedExtension,
                0,
                "Image Paint accepts PNG, JPEG, and WebP files only.");
        }
        const auto file_name =
            utf8(path->filename().wstring());
        if (!file_name)
        {
            return std::unexpected(file_name.error());
        }
        const auto bytes = read_regular_file(
            *path,
            core::MaximumImageSourceBytes);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        if ((*bytes)->size() >
            core::MaximumProjectSourceBytes - total_bytes)
        {
            return picker_error(
                ImageFilePickerErrorCode::FileSize,
                0,
                "The selected images exceed the 64 MiB project source limit.");
        }
        total_bytes += (*bytes)->size();
        files.push_back(PickedImageFile{
            *file_name,
            *mime,
            *bytes,
        });
    }
    return std::optional<std::vector<PickedImageFile>>{
        std::move(files)};
}

auto NativeImageFilePicker::pick_image_project(
    std::uintptr_t owner_window)
    -> ImageFilePickerResult<PickedImageProjectFile>
{
    const auto com = initialize_com();
    if (!com)
    {
        return std::unexpected(com.error());
    }
    constexpr auto Filters = std::array{
        COMDLG_FILTERSPEC{
            L"MecchaCamouflage v2 projects (*.mcpreset)",
            L"*.mcpreset",
        },
    };
    auto dialog = create_dialog(false, Filters);
    if (!dialog)
    {
        return std::unexpected(dialog.error());
    }
    const auto shown = show_dialog(*dialog->get(), owner_window);
    if (!shown)
    {
        return std::unexpected(shown.error());
    }
    if (!*shown)
    {
        return std::nullopt;
    }

    auto item = ComObject<IShellItem>{};
    const auto result = dialog->get()->GetResult(item.put());
    if (FAILED(result))
    {
        return hresult_error(
            ImageFilePickerErrorCode::SelectionFailure,
            "The selected Image Paint project could not be read.",
            result);
    }
    const auto path = selected_path(*item.get());
    if (!path)
    {
        return std::unexpected(path.error());
    }
    if (lowercase_ascii(path->extension().wstring()) !=
        L".mcpreset")
    {
        return picker_error(
            ImageFilePickerErrorCode::UnsupportedExtension,
            0,
            "Only v2 .mcpreset files can be selected.");
    }
    const auto file_name = utf8(path->filename().wstring());
    if (!file_name)
    {
        return std::unexpected(file_name.error());
    }
    const auto bytes = read_regular_file(
        *path,
        MaximumPresetContainerBytes);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    return std::optional<PickedImageProjectFile>{
        PickedImageProjectFile{
            *file_name,
            *bytes,
        }};
}
} // namespace meccha::application
