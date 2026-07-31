#include <Mod/CppUserModBase.hpp>

#include <meccha/application/application_root.hpp>
#include <meccha/application/image_editor_services_win32.hpp>
#include <meccha/application/image_file_picker.hpp>
#include <meccha/application/image_project_codec.hpp>
#include <meccha/application/input_command_router.hpp>
#include <meccha/application/product_ui_effect_executor.hpp>
#include <meccha/application/production_resources.hpp>
#include <meccha/application/runtime_operation_executor.hpp>
#include <meccha/product_ui/image_editor_texture_coordinator.hpp>
#include <meccha/product_ui/product_ui_frame_coordinator.hpp>
#include <meccha/product_ui/product_ui_key_binding.hpp>
#include <meccha/runtime/unreal_runtime_adapter.hpp>
#include <meccha/ui/input_lease.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
#define MECCHA_EXPAND_UE_STRING_IMPL(value) STR(value)
#define MECCHA_EXPAND_UE_STRING(value) MECCHA_EXPAND_UE_STRING_IMPL(value)

static_assert(
    static_cast<std::uint8_t>(RC::Input::Key::F24) -
            static_cast<std::uint8_t>(RC::Input::Key::F1) +
            1U ==
        meccha::product_ui::
            ProductUiFunctionKeyRegistrationCount);

constinit std::byte ModuleAnchor{};

inline constexpr auto RuntimeQueueCapacity = std::size_t{4096U};
inline constexpr auto CommandQueueCapacity = std::size_t{256U};
inline constexpr auto DiagnosticCapacity = std::size_t{256U};

auto loaded_module_file()
    -> std::expected<std::filesystem::path, std::string>
{
    auto module = HMODULE{};
    constexpr auto flags =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExW(
            flags,
            reinterpret_cast<LPCWSTR>(&ModuleAnchor),
            &module) == FALSE)
    {
        return std::unexpected(
            "The loaded mod module could not be resolved.");
    }

    constexpr auto MaximumModulePathCharacters =
        std::size_t{32768U};
    auto path = std::vector<wchar_t>(
        MaximumModulePathCharacters,
        L'\0');
    const auto length = GetModuleFileNameW(
        module,
        path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0U ||
        static_cast<std::size_t>(length) >= path.size())
    {
        return std::unexpected(
            "The loaded mod module path is unavailable.");
    }
    path.resize(length);
    auto result = std::filesystem::path{
        std::wstring{path.begin(), path.end()}};
    if (!result.is_absolute())
    {
        return std::unexpected(
            "The loaded mod module path is not absolute.");
    }
    return result;
}

auto load_owned_production_resources()
    -> meccha::application::ProductionResources
{
    const auto module = loaded_module_file();
    if (!module)
    {
        throw std::runtime_error{module.error()};
    }
    const auto root =
        meccha::application::derive_production_resource_root(
            *module);
    if (!root)
    {
        throw std::runtime_error{root.error().detail};
    }
    auto resources =
        meccha::application::load_production_resources(*root);
    if (!resources)
    {
        throw std::runtime_error{resources.error().detail};
    }
    return std::move(*resources);
}

auto load_owned_image_editor_services()
    -> std::unique_ptr<
        meccha::application::Win32ImageEditorServices>
{
    auto services =
        meccha::application::Win32ImageEditorServices::create();
    if (!services)
    {
        throw std::runtime_error{services.error().detail};
    }
    return std::move(*services);
}

auto ue4ss_function_key(meccha::core::FunctionKey key)
    -> std::optional<RC::Input::Key>
{
    const auto value = static_cast<std::uint8_t>(key);
    const auto first = static_cast<std::uint8_t>(
        meccha::core::FunctionKey::F1);
    const auto last = static_cast<std::uint8_t>(
        meccha::core::FunctionKey::F24);
    if (value < first || value > last)
    {
        return std::nullopt;
    }
    return static_cast<RC::Input::Key>(
        static_cast<std::uint8_t>(RC::Input::Key::F1) +
        value - first);
}

class MecchaCamouflageMod final
    : public RC::CppUserModBase,
      private meccha::product_ui::
          ProductUiFunctionKeyRegistrationPort
{
public:
    MecchaCamouflageMod()
        : resources_{load_owned_production_resources()},
          input_queue_{
              std::make_shared<
                  meccha::product_ui::ProductUiInputQueue>()},
          image_editor_services_{
              load_owned_image_editor_services()},
          runtime_{
              input_queue_,
              resources_.image_paint_profiles,
              resources_.fallback_glyph_atlas},
          runtime_executor_{
              runtime_,
              runtime_,
              runtime_,
              runtime_},
          application_{
              runtime_,
              runtime_executor_,
              image_editor_services_->config_storage(),
              runtime_,
              runtime_,
              runtime_,
              runtime_,
              image_editor_services_->image_editor(),
              runtime_,
              RuntimeQueueCapacity,
              CommandQueueCapacity,
              DiagnosticCapacity},
          key_binding_{input_queue_}
    {
        ui_effects_ = std::make_unique<
            meccha::application::ProductUiEffectExecutor>(
            application_,
            file_picker_,
            preset_hasher_);
        image_textures_ = std::make_unique<
            meccha::product_ui::ImageEditorTextureCoordinator>(
            runtime_,
            runtime_);
        ui_frames_ = std::make_unique<
            meccha::product_ui::ProductUiFrameCoordinator>(
            application_,
            application_,
            input_router_,
            resources_.localization,
            input_lease_,
            runtime_,
            runtime_,
            runtime_,
            ui_effects_.get(),
            &image_editor_services_->image_editor(),
            image_textures_.get(),
            std::span<const meccha::core::ImageGuideBitmap>{
                resources_.image_guides});
        ModName = STR("MecchaCamouflage");
        ModVersion = MECCHA_EXPAND_UE_STRING(MECCHA_PRODUCT_VERSION);
        ModDescription = STR("In-game Paint, Image Paint, and ESP");
        ModAuthors = STR("MecchaCamouflage contributors");
    }

    ~MecchaCamouflageMod() override
    {
        key_binding_.stop();
        input_router_.shutdown();
    }

    auto on_unreal_init() -> void override
    {
        if (initialization_attempted_)
        {
            return;
        }
        initialization_attempted_ = true;
        try
        {
            if (!ui_frames_ ||
                !application_
                     .attach_frame_extension(*ui_frames_))
            {
                return;
            }
            if (!key_binding_.start(*this))
            {
                return;
            }
            if (!application_.initialize())
            {
                key_binding_.stop();
                return;
            }
            initialized_ = true;
        }
        catch (...)
        {
            key_binding_.stop();
        }
    }

    auto on_update() -> void override
    {
        if (initialized_)
        {
            application_.on_update();
        }
    }

private:
    auto register_function_key(
        meccha::core::FunctionKey key,
        meccha::product_ui::ProductUiFunctionKeyCallback callback)
        -> std::expected<
            void,
            meccha::product_ui::
                ProductUiFunctionKeyRegistrationError> override
    {
        const auto mapped = ue4ss_function_key(key);
        if (!mapped)
        {
            return std::unexpected(
                meccha::product_ui::
                    ProductUiFunctionKeyRegistrationError{
                        "The function key is outside F1-F24.",
                    });
        }
        register_keydown_event(*mapped, std::move(callback));
        return {};
    }

    meccha::application::ProductionResources resources_;
    std::shared_ptr<meccha::product_ui::ProductUiInputQueue>
        input_queue_;
    std::unique_ptr<
        meccha::application::Win32ImageEditorServices>
        image_editor_services_;
    meccha::runtime::UnrealRuntimeAdapter runtime_;
    meccha::application::RuntimeOperationExecutor
        runtime_executor_;
    meccha::application::InputCommandRouter input_router_{};
    meccha::ui::InputLeaseController input_lease_{};
    meccha::application::NativeImageFilePicker file_picker_{};
    meccha::application::NativePresetHasher preset_hasher_{};
    std::unique_ptr<
        meccha::application::ProductUiEffectExecutor>
        ui_effects_{};
    std::unique_ptr<
        meccha::product_ui::ImageEditorTextureCoordinator>
        image_textures_{};
    std::unique_ptr<
        meccha::product_ui::ProductUiFrameCoordinator>
        ui_frames_{};
    meccha::application::ApplicationRoot application_;
    meccha::product_ui::ProductUiFunctionKeyBinding
        key_binding_;
    bool initialization_attempted_{};
    bool initialized_{};
};

#undef MECCHA_EXPAND_UE_STRING
#undef MECCHA_EXPAND_UE_STRING_IMPL
} // namespace

extern "C" __declspec(dllexport) RC::CppUserModBase*
start_mod() noexcept
{
    try
    {
        return new MecchaCamouflageMod{};
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) void
uninstall_mod(RC::CppUserModBase* mod) noexcept
{
    try
    {
        delete mod;
    }
    catch (...)
    {
    }
}
