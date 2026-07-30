#include <Mod/CppUserModBase.hpp>

namespace
{
#define MECCHA_EXPAND_UE_STRING_IMPL(value) STR(value)
#define MECCHA_EXPAND_UE_STRING(value) MECCHA_EXPAND_UE_STRING_IMPL(value)

class MecchaCamouflageMod final : public RC::CppUserModBase
{
public:
    MecchaCamouflageMod()
    {
        ModName = STR("MecchaCamouflage");
        ModVersion = MECCHA_EXPAND_UE_STRING(MECCHA_PRODUCT_VERSION);
        ModDescription = STR("In-game Paint, Image Paint, and ESP");
        ModAuthors = STR("MecchaCamouflage contributors");
    }

    auto on_unreal_init() -> void override
    {
    }

    auto on_update() -> void override
    {
    }
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
