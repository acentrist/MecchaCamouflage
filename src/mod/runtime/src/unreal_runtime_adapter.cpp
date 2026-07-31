#include <meccha/runtime/unreal_runtime_adapter.hpp>

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/World.hpp>

#include <cstdint>
#include <expected>
#include <mutex>
#include <optional>
#include <utility>

namespace meccha::runtime
{
namespace
{
using namespace RC::Unreal;

constexpr auto HudCallbackId = application::CallbackId{1U};
constexpr auto ReceiveDrawHudPath =
    STR("/Script/Engine.HUD:ReceiveDrawHUD");
constexpr auto HudClassPath = STR("/Script/Engine.HUD");
constexpr auto WorldClassPath = STR("/Script/Engine.World");
constexpr auto PlayerControllerClassPath =
    STR("/Script/Engine.PlayerController");
constexpr auto CanvasClassPath = STR("/Script/Engine.Canvas");
constexpr auto ReceiveDrawHudParameterBytes = 8;

struct HudContracts
{
    UFunction* receive_draw_hud{};
    UClass* hud_class{};
    UClass* world_class{};
    UClass* player_controller_class{};
    UClass* canvas_class{};
    FObjectPropertyBase* player_owner{};
    FObjectPropertyBase* canvas{};
};

auto find_class(const TCHAR* path) -> UClass*
{
    return UObjectGlobals::StaticFindObject<UClass*>(
        nullptr,
        nullptr,
        path);
}

auto validate_parameter(
    FProperty* property,
    const TCHAR* expected_name,
    int expected_offset) -> bool
{
    return property != nullptr &&
           property->GetName() == expected_name &&
           property->HasAllPropertyFlags(CPF_Parm) &&
           !property->HasAnyPropertyFlags(CPF_ReturnParm) &&
           CastField<FIntProperty>(property) != nullptr &&
           property->GetArrayDim() == 1 &&
           property->GetElementSize() ==
               static_cast<int>(sizeof(std::int32_t)) &&
           property->GetOffset_Internal() == expected_offset &&
           property->IsInContainer(ReceiveDrawHudParameterBytes);
}

auto find_object_property(
    UClass* owner,
    const TCHAR* name,
    UClass* expected_class) -> FObjectPropertyBase*
{
    if (owner == nullptr || expected_class == nullptr)
    {
        return nullptr;
    }
    auto* property =
        CastField<FObjectPropertyBase>(
            owner->FindProperty(FName{name, FNAME_Find}));
    if (property == nullptr ||
        property->GetArrayDim() != 1 ||
        property->GetPropertyClass().Get() != expected_class ||
        !property->IsInContainer(owner))
    {
        return nullptr;
    }
    return property;
}

auto resolve_hud_contracts() -> std::optional<HudContracts>
{
    auto contracts = HudContracts{};
    contracts.receive_draw_hud =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ReceiveDrawHudPath);
    contracts.hud_class = find_class(HudClassPath);
    contracts.world_class = find_class(WorldClassPath);
    contracts.player_controller_class =
        find_class(PlayerControllerClassPath);
    contracts.canvas_class = find_class(CanvasClassPath);
    if (contracts.receive_draw_hud == nullptr ||
        contracts.hud_class == nullptr ||
        contracts.world_class == nullptr ||
        contracts.player_controller_class == nullptr ||
        contracts.canvas_class == nullptr ||
        contracts.receive_draw_hud->GetOuterPrivate() !=
            contracts.hud_class ||
        contracts.receive_draw_hud->GetPropertiesSize() !=
            ReceiveDrawHudParameterBytes)
    {
        return std::nullopt;
    }

    auto* parameter = contracts.receive_draw_hud->GetFirstProperty();
    if (!validate_parameter(parameter, STR("SizeX"), 0))
    {
        return std::nullopt;
    }
    parameter = GetNextField(parameter);
    if (!validate_parameter(parameter, STR("SizeY"), 4) ||
        GetNextField(parameter) != nullptr)
    {
        return std::nullopt;
    }

    contracts.player_owner = find_object_property(
        contracts.hud_class,
        STR("PlayerOwner"),
        contracts.player_controller_class);
    contracts.canvas = find_object_property(
        contracts.hud_class,
        STR("Canvas"),
        contracts.canvas_class);
    if (contracts.player_owner == nullptr ||
        contracts.canvas == nullptr)
    {
        return std::nullopt;
    }
    return contracts;
}

auto object_is_live(UObject* object, UClass* expected_class) -> bool
{
    if (object == nullptr || expected_class == nullptr ||
        !UObject::IsReal(object) || !object->IsA(expected_class))
    {
        return false;
    }
    const auto weak = FWeakObjectPtr{object};
    return weak.Get() == object &&
           weak.ObjectSerialNumber != 0;
}

auto object_identity(UObject* object) -> std::uint64_t
{
    const auto weak = FWeakObjectPtr{object};
    return
        (static_cast<std::uint64_t>(
             static_cast<std::uint32_t>(
                 weak.ObjectSerialNumber))
         << 32U) |
        static_cast<std::uint32_t>(weak.ObjectIndex);
}

auto read_object(
    FObjectPropertyBase* property,
    UObject* container) -> UObject*
{
    if (property == nullptr || container == nullptr)
    {
        return nullptr;
    }
    const auto* value =
        property->ContainerPtrToValuePtr<void>(container);
    return property->GetObjectPropertyValue(value);
}
} // namespace

class UnrealRuntimeAdapter::Impl
{
public:
    ~Impl()
    {
        detach_noexcept();
    }

    auto attach(void* context, application::HudCallback callback)
        -> std::expected<
            application::CallbackId,
            application::CallbackPortError>
    {
        if (context == nullptr || callback == nullptr)
        {
            return std::unexpected(
                application::CallbackPortError::Registration);
        }

        try
        {
            const auto resolved = resolve_hud_contracts();
            if (!resolved)
            {
                return std::unexpected(
                    application::CallbackPortError::Registration);
            }

            {
                const auto lock = std::scoped_lock{mutex_};
                if (hook_ids_)
                {
                    return std::unexpected(
                        application::CallbackPortError::Registration);
                }
                contracts_ = *resolved;
                callback_context_ = context;
                callback_ = callback;
            }

            const auto ids = UObjectGlobals::RegisterHook(
                resolved->receive_draw_hud,
                &Impl::pre_hook,
                &Impl::post_hook,
                this);
            {
                const auto lock = std::scoped_lock{mutex_};
                hook_ids_ = ids;
            }
            return HudCallbackId;
        }
        catch (...)
        {
            const auto lock = std::scoped_lock{mutex_};
            contracts_.reset();
            callback_context_ = nullptr;
            callback_ = nullptr;
            hook_ids_.reset();
            return std::unexpected(
                application::CallbackPortError::Registration);
        }
    }

    auto detach(application::CallbackId id)
        -> std::expected<
            void,
            application::CallbackPortError>
    {
        auto function = static_cast<UFunction*>(nullptr);
        auto ids = std::pair<int, int>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (id != HudCallbackId || !hook_ids_ || !contracts_)
            {
                return std::unexpected(
                    application::CallbackPortError::Unregistration);
            }
            function = contracts_->receive_draw_hud;
            ids = *hook_ids_;
        }

        try
        {
            UObjectGlobals::UnregisterHook(function, ids);
        }
        catch (...)
        {
            return std::unexpected(
                application::CallbackPortError::Unregistration);
        }

        const auto lock = std::scoped_lock{mutex_};
        hook_ids_.reset();
        contracts_.reset();
        callback_context_ = nullptr;
        callback_ = nullptr;
        return {};
    }

private:
    static auto pre_hook(
        UnrealScriptFunctionCallableContext&,
        void*) -> void
    {
    }

    static auto post_hook(
        UnrealScriptFunctionCallableContext& context,
        void* custom_data) -> void
    {
        if (custom_data == nullptr)
        {
            return;
        }
        try
        {
            static_cast<Impl*>(custom_data)->dispatch(context.Context);
        }
        catch (...)
        {
        }
    }

    auto dispatch(UObject* hud) -> void
    {
        auto contracts = std::optional<HudContracts>{};
        auto callback_context = static_cast<void*>(nullptr);
        auto callback = application::HudCallback{};
        {
            const auto lock = std::scoped_lock{mutex_};
            contracts = contracts_;
            callback_context = callback_context_;
            callback = callback_;
        }
        if (!contracts || callback_context == nullptr ||
            callback == nullptr)
        {
            return;
        }

        auto identity = application::HudFrameIdentity{};
        if (IsInGameThreadRaw() &&
            object_is_live(hud, contracts->hud_class))
        {
            auto* world = hud->GetWorld();
            auto* controller =
                read_object(contracts->player_owner, hud);
            auto* canvas = read_object(contracts->canvas, hud);
            if (object_is_live(world, contracts->world_class) &&
                object_is_live(
                    controller,
                    contracts->player_controller_class) &&
                object_is_live(canvas, contracts->canvas_class))
            {
                identity = application::HudFrameIdentity{
                    object_identity(world),
                    object_identity(controller),
                    object_identity(hud),
                    object_identity(canvas),
                };
            }
        }
        callback(callback_context, identity);
    }

    auto detach_noexcept() noexcept -> void
    {
        auto function = static_cast<UFunction*>(nullptr);
        auto ids = std::optional<std::pair<int, int>>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (contracts_)
            {
                function = contracts_->receive_draw_hud;
            }
            ids = hook_ids_;
            hook_ids_.reset();
            contracts_.reset();
            callback_context_ = nullptr;
            callback_ = nullptr;
        }
        if (function != nullptr && ids)
        {
            try
            {
                UObjectGlobals::UnregisterHook(function, *ids);
            }
            catch (...)
            {
            }
        }
    }

    std::mutex mutex_{};
    std::optional<HudContracts> contracts_{};
    std::optional<std::pair<int, int>> hook_ids_{};
    void* callback_context_{};
    application::HudCallback callback_{};
};

UnrealRuntimeAdapter::UnrealRuntimeAdapter()
    : impl_{std::make_unique<Impl>()}
{
}

UnrealRuntimeAdapter::~UnrealRuntimeAdapter() = default;

auto UnrealRuntimeAdapter::register_hud_callback(
    void* context,
    application::HudCallback callback)
    -> std::expected<
        application::CallbackId,
        application::CallbackPortError>
{
    return impl_->attach(context, callback);
}

auto UnrealRuntimeAdapter::unregister_hud_callback(
    application::CallbackId id)
    -> std::expected<
        void,
        application::CallbackPortError>
{
    return impl_->detach(id);
}

auto UnrealRuntimeAdapter::is_game_thread() const noexcept -> bool
{
    return RC::Unreal::IsInGameThreadRaw();
}
} // namespace meccha::runtime
