#include <Mod/CppUserModBase.hpp>

#include <meccha/product_ui/product_ui_key_binding.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <utility>

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
        : input_queue_{
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
