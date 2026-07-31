#pragma once

#include <meccha/application/config_storage_win32.hpp>
#include <meccha/application/image_composition_worker.hpp>
#include <meccha/application/image_decoder.hpp>
#include <meccha/application/image_editor_session.hpp>
#include <meccha/application/image_project_storage_win32.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace meccha::application
{
inline constexpr auto DefaultImageEditorDraftDebounce =
    std::chrono::milliseconds{750};
inline constexpr auto MaximumImageEditorDraftDebounce =
    std::chrono::milliseconds{60'000};

enum class Win32ImageEditorServicesErrorCode : std::uint8_t
{
    InvalidLocalAppData,
    InvalidDraftDebounce,
    LocalAppDataResolution,
    Construction,
};

struct Win32ImageEditorServicesError
{
    Win32ImageEditorServicesErrorCode code{};
    std::string detail{};

    auto operator==(const Win32ImageEditorServicesError&) const
        -> bool = default;
};

class Win32ImageEditorServices final
{
public:
    Win32ImageEditorServices(
        const Win32ImageEditorServices&) = delete;
    auto operator=(const Win32ImageEditorServices&)
        -> Win32ImageEditorServices& = delete;
    ~Win32ImageEditorServices();

    [[nodiscard]] static auto create(
        std::filesystem::path local_app_data,
        std::chrono::milliseconds draft_debounce =
            DefaultImageEditorDraftDebounce)
        -> std::expected<
            std::unique_ptr<Win32ImageEditorServices>,
            Win32ImageEditorServicesError>;

    [[nodiscard]] static auto create(
        std::chrono::milliseconds draft_debounce =
            DefaultImageEditorDraftDebounce)
        -> std::expected<
            std::unique_ptr<Win32ImageEditorServices>,
            Win32ImageEditorServicesError>;

    [[nodiscard]] auto config_storage()
        -> AtomicTextStorage&;
    [[nodiscard]] auto image_editor()
        -> ImageEditorSessionPort&;

    [[nodiscard]] auto data_root() const
        -> const std::filesystem::path&;
    [[nodiscard]] auto image_projects_root() const
        -> const std::filesystem::path&;

private:
    Win32ImageEditorServices(
        std::filesystem::path local_app_data,
        std::chrono::milliseconds draft_debounce);

    Win32AtomicTextStorage config_storage_;
    ConfigStore config_store_;
    Win32AtomicProjectStorage project_storage_;
    NativePresetHasher preset_hasher_{};
    ImageProjectStore project_store_;
    ImageProjectPersistenceCoordinator persistence_;
    NativeImageSourceDecoder decoder_{};
    CoreImageAtlasComposer composer_{};
    ImageEditorSession session_;
};
} // namespace meccha::application
