#include <Mod/CppUserModBase.hpp>

#include <meccha/application/production_resources.hpp>
#include <meccha/product_ui/product_ui_key_binding.hpp>

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
          key_binding_{input_queue_}
    {
        ModName = STR("MecchaCamouflage");
        ModVersion = MECCHA_EXPAND_UE_STRING(MECCHA_PRODUCT_VERSION);
        ModDescription = STR("In-game Paint, Image Paint, and ESP");
        ModAuthors = STR("MecchaCamouflage contributors");
    }

    ~MecchaCamouflageMod() override
    {
        key_binding_.stop();
    }

    auto on_unreal_init() -> void override
    {
        // Key registration starts only after the frame consumer and
        // application composition root are owned.
    }

    auto on_update() -> void override
    {
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
    meccha::product_ui::ProductUiFunctionKeyBinding
        key_binding_;
};

#undef MECCHA_EXPAND_UE_STRING
#undef MECCHA_EXPAND_UE_STRING_IMPL
} // namespace

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new MecchaCamouflageMod{};
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
