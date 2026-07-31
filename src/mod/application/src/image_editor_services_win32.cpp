#include <meccha/application/image_editor_services_win32.hpp>

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace meccha::application
{
namespace
{
auto invalid(Win32ImageEditorServicesErrorCode code)
    -> std::unexpected<Win32ImageEditorServicesError>
{
    return std::unexpected(
        Win32ImageEditorServicesError{code});
}

auto construction_error()
    -> std::unexpected<Win32ImageEditorServicesError>
{
    return std::unexpected(Win32ImageEditorServicesError{
        Win32ImageEditorServicesErrorCode::Construction,
        "Native Image Paint services could not be constructed.",
    });
}
} // namespace

Win32ImageEditorServices::Win32ImageEditorServices(
    std::filesystem::path local_app_data,
    std::chrono::milliseconds draft_debounce)
    : config_storage_{local_app_data},
      config_store_{config_storage_},
      project_storage_{std::move(local_app_data)},
      project_store_{project_storage_, preset_hasher_},
      persistence_{project_store_, config_store_},
      session_{
          decoder_,
          composer_,
          project_store_,
          persistence_,
          draft_debounce}
{
}

Win32ImageEditorServices::~Win32ImageEditorServices()
{
    session_.shutdown(true);
}

auto Win32ImageEditorServices::create(
    std::filesystem::path local_app_data,
    std::chrono::milliseconds draft_debounce)
    -> std::expected<
        std::unique_ptr<Win32ImageEditorServices>,
        Win32ImageEditorServicesError>
{
    try
    {
        if (local_app_data.empty() ||
            !local_app_data.is_absolute())
        {
            return invalid(
                Win32ImageEditorServicesErrorCode::
                    InvalidLocalAppData);
        }
        if (draft_debounce <= std::chrono::milliseconds::zero() ||
            draft_debounce >
                MaximumImageEditorDraftDebounce)
        {
            return invalid(
                Win32ImageEditorServicesErrorCode::
                    InvalidDraftDebounce);
        }
        return std::unique_ptr<Win32ImageEditorServices>{
            new Win32ImageEditorServices{
                std::move(local_app_data),
                draft_debounce,
            }};
    }
    catch (const std::bad_alloc&)
    {
        return construction_error();
    }
    catch (const std::system_error&)
    {
        return construction_error();
    }
    catch (...)
    {
        return construction_error();
    }
}

auto Win32ImageEditorServices::create(
    std::chrono::milliseconds draft_debounce)
    -> std::expected<
        std::unique_ptr<Win32ImageEditorServices>,
        Win32ImageEditorServicesError>
{
    try
    {
        const auto local_app_data = resolve_local_app_data();
        if (!local_app_data)
        {
            return std::unexpected(
                Win32ImageEditorServicesError{
                    Win32ImageEditorServicesErrorCode::
                        LocalAppDataResolution,
                    local_app_data.error().detail,
                });
        }
        return create(*local_app_data, draft_debounce);
    }
    catch (...)
    {
        return construction_error();
    }
}

auto Win32ImageEditorServices::config_storage()
    -> AtomicTextStorage&
{
    return config_storage_;
}

auto Win32ImageEditorServices::image_editor()
    -> ImageEditorSessionPort&
{
    return session_;
}

auto Win32ImageEditorServices::data_root() const
    -> const std::filesystem::path&
{
    return config_storage_.root();
}

auto Win32ImageEditorServices::image_projects_root() const
    -> const std::filesystem::path&
{
    return project_storage_.root();
}
} // namespace meccha::application
