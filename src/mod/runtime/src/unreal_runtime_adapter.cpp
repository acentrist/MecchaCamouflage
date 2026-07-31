#include <meccha/runtime/unreal_runtime_adapter.hpp>

#include <meccha/runtime/paint_call_codec.hpp>
#include <meccha/runtime/paint_preview_codec.hpp>
#include <meccha/runtime/paint_queue_codec.hpp>
#include <meccha/runtime/unreal_contracts.hpp>

#include "unreal_reflection_validation.hpp"

#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealFlags.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/World.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>

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
constexpr auto PawnClassPath = STR("/Script/Engine.Pawn");
constexpr auto CanvasClassPath = STR("/Script/Engine.Canvas");
constexpr auto RuntimePaintableClassPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent");
constexpr auto PaintAtUvWithBrushPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "PaintAtUVWithBrush");
constexpr auto GetRecordedStrokeCountPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "GetRecordedStrokeCount");
constexpr auto RuntimePaintReplicationManagerClassPath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager");
constexpr auto GetQueuedStrokeCountPath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager:"
        "GetQueuedStrokeCount");
constexpr auto GetQueuedStrokeCountForComponentPath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager:"
        "GetQueuedStrokeCountForComponent");
constexpr auto GetReplicationPressurePath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationManager:"
        "GetReplicationPressure");
constexpr auto ExportChannelToBytesPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "ExportChannelToBytes");
constexpr auto ImportChannelFromBytesPath =
    STR("/Script/PenguinHotel.RuntimePaintableComponent:"
        "ImportChannelFromBytes");
constexpr auto RuntimePaintReplicationPressurePath =
    STR("/Script/PenguinHotel.RuntimePaintReplicationPressure");
constexpr auto Vector2dPath =
    STR("/Script/CoreUObject.Vector2D");
constexpr auto PaintChannelDataPath =
    STR("/Script/PenguinHotel.PaintChannelData");
constexpr auto RuntimeBrushSettingsPath =
    STR("/Script/PenguinHotel.RuntimeBrushSettings");
constexpr auto ReceiveDrawHudParameterBytes = 8;
constexpr auto FailureMessage = "error.operation.failed";

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

struct PaintContracts
{
    UClass* player_controller_class{};
    UClass* pawn_class{};
    UClass* runtime_paintable_class{};
    UClass* replication_manager_class{};
    FObjectPropertyBase* acknowledged_pawn{};
    UFunction* paint_at_uv_with_brush{};
    UFunction* get_recorded_stroke_count{};
    UFunction* get_queued_stroke_count{};
    UFunction* get_queued_stroke_count_for_component{};
    UFunction* get_replication_pressure{};
    UFunction* export_channel_to_bytes{};
    UFunction* import_channel_from_bytes{};
    UScriptStruct* vector2d{};
    UScriptStruct* paint_channel_data{};
    UScriptStruct* runtime_brush_settings{};
    UScriptStruct* runtime_paint_replication_pressure{};
};

struct ActiveFrame
{
    application::HudFrameIdentity identity{};
    UObject* world{};
    UObject* controller{};
    UObject* hud{};
    UObject* canvas{};
};

struct BoundFrame
{
    application::HudFrameIdentity identity{};
    FWeakObjectPtr world{};
    FWeakObjectPtr controller{};
    FWeakObjectPtr hud{};
    FWeakObjectPtr canvas{};
    FWeakObjectPtr pawn{};
    FWeakObjectPtr component{};
    FWeakObjectPtr replication_manager{};
    std::uint64_t component_identity{};
    std::uint64_t component_generation{};
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
        property->GetElementSize() !=
            static_cast<int>(sizeof(void*)) ||
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

auto object_is_live_exact(
    UObject* object,
    UClass* expected_class) -> bool
{
    constexpr auto rejected_flags = static_cast<EObjectFlags>(
        RF_ClassDefaultObject | RF_ArchetypeObject |
        RF_BeginDestroyed | RF_FinishDestroyed);
    return object_is_live(object, expected_class) &&
           object->GetClassPrivate() == expected_class &&
           !object->HasAnyFlags(rejected_flags);
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

auto runtime_failure(
    application::RuntimeContractId contract,
    application::ContractFailureKind kind)
    -> std::unexpected<application::RuntimeExecutionError>
{
    return std::unexpected(application::RuntimeExecutionError{
        application::RuntimeExecutionErrorCode::OperationFailure,
        application::CompatibilityFailure{
            contract,
            kind,
            FailureMessage,
        },
    });
}

class ExportedChannelBuffer final
{
public:
    explicit ExportedChannelBuffer(RuntimeByteArrayAbi& array)
        : array_{array}
    {
    }

    ExportedChannelBuffer(const ExportedChannelBuffer&) = delete;
    auto operator=(const ExportedChannelBuffer&)
        -> ExportedChannelBuffer& = delete;

    ~ExportedChannelBuffer() noexcept
    {
        if (array_.data != nullptr && GMalloc != nullptr &&
            *GMalloc != nullptr)
        {
            try
            {
                (*GMalloc)->Free(
                    const_cast<std::byte*>(array_.data));
            }
            catch (...)
            {
            }
        }
        array_ = {};
    }

private:
    RuntimeByteArrayAbi& array_;
};

auto export_channel(
    UObject* component,
    UFunction* function,
    RuntimePaintChannel channel)
    -> std::expected<
        std::vector<std::byte>,
        application::RuntimeExecutionError>
{
    if (component == nullptr || function == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::MissingObject);
    }

    auto parameters = ExportChannelToBytesParametersAbi{};
    [[maybe_unused]] auto release =
        ExportedChannelBuffer{parameters.out_data};
    parameters.channel = channel;
    component->ProcessEvent(function, &parameters);

    if (!parameters.return_value ||
        parameters.out_data.data == nullptr ||
        parameters.out_data.count <= 0 ||
        parameters.out_data.capacity <
            parameters.out_data.count ||
        static_cast<std::size_t>(
            parameters.out_data.capacity) >
            MaximumPaintChannelBytes ||
        static_cast<std::size_t>(
            parameters.out_data.count) >
            MaximumPaintChannelBytes)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::InvalidValue);
    }

    auto bytes = std::vector<std::byte>(
        static_cast<std::size_t>(
            parameters.out_data.count));
    std::memcpy(
        bytes.data(),
        parameters.out_data.data,
        bytes.size());
    return bytes;
}

auto import_channel(
    UObject* component,
    UFunction* function,
    RuntimePaintChannel channel,
    std::span<const std::byte> bytes)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    if (component == nullptr || function == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::MissingObject);
    }
    const auto encoded =
        encode_channel_import(channel, bytes);
    if (!encoded)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::InvalidValue);
    }

    auto parameters = *encoded;
    component->ProcessEvent(function, &parameters);
    if (!parameters.return_value)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::ExecutionFailure);
    }
    return {};
}

auto import_channel_verified(
    UObject* component,
    UFunction* import_function,
    UFunction* export_function,
    RuntimePaintChannel import_channel_value,
    RuntimePaintChannel verification_channel,
    std::span<const std::byte> bytes)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    const auto imported = import_channel(
        component,
        import_function,
        import_channel_value,
        bytes);
    if (!imported)
    {
        return imported;
    }

    const auto readback = export_channel(
        component,
        export_function,
        verification_channel);
    if (!readback)
    {
        return std::unexpected(readback.error());
    }
    if (readback->size() != bytes.size() ||
        !std::equal(
            readback->begin(),
            readback->end(),
            bytes.begin()))
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::InvalidValue);
    }
    return {};
}

auto resolve_paint_contracts(UClass* player_controller_class)
    -> std::expected<
        PaintContracts,
        application::RuntimeExecutionError>
{
    auto contracts = PaintContracts{};
    contracts.player_controller_class = player_controller_class;
    contracts.pawn_class = find_class(PawnClassPath);
    contracts.runtime_paintable_class =
        find_class(RuntimePaintableClassPath);
    contracts.replication_manager_class =
        find_class(RuntimePaintReplicationManagerClassPath);
    contracts.paint_at_uv_with_brush =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            PaintAtUvWithBrushPath);
    contracts.get_recorded_stroke_count =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetRecordedStrokeCountPath);
    contracts.get_queued_stroke_count =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetQueuedStrokeCountPath);
    contracts.get_queued_stroke_count_for_component =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetQueuedStrokeCountForComponentPath);
    contracts.get_replication_pressure =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            GetReplicationPressurePath);
    contracts.export_channel_to_bytes =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ExportChannelToBytesPath);
    contracts.import_channel_from_bytes =
        UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr,
            nullptr,
            ImportChannelFromBytesPath);
    contracts.vector2d =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            Vector2dPath);
    contracts.paint_channel_data =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            PaintChannelDataPath);
    contracts.runtime_brush_settings =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            RuntimeBrushSettingsPath);
    contracts.runtime_paint_replication_pressure =
        UObjectGlobals::StaticFindObject<UScriptStruct*>(
            nullptr,
            nullptr,
            RuntimePaintReplicationPressurePath);
    if (player_controller_class == nullptr ||
        contracts.pawn_class == nullptr ||
        contracts.runtime_paintable_class == nullptr ||
        contracts.replication_manager_class == nullptr ||
        contracts.vector2d == nullptr ||
        contracts.paint_channel_data == nullptr ||
        contracts.runtime_brush_settings == nullptr ||
        contracts.runtime_paint_replication_pressure == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintAtUvWithBrush,
            application::ContractFailureKind::MissingObject);
    }
    if (contracts.paint_at_uv_with_brush == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintAtUvWithBrush,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.get_recorded_stroke_count == nullptr ||
        contracts.get_queued_stroke_count == nullptr ||
        contracts.get_queued_stroke_count_for_component == nullptr ||
        contracts.get_replication_pressure == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintQueueObservation,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.export_channel_to_bytes == nullptr ||
        contracts.import_channel_from_bytes == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::MissingFunction);
    }
    if (contracts.paint_at_uv_with_brush->GetOuterPrivate() !=
        contracts.runtime_paintable_class)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintAtUvWithBrush,
            application::ContractFailureKind::WrongClass);
    }
    if (contracts.get_recorded_stroke_count->GetOuterPrivate() !=
            contracts.runtime_paintable_class ||
        contracts.get_queued_stroke_count->GetOuterPrivate() !=
            contracts.replication_manager_class ||
        contracts.get_queued_stroke_count_for_component
                ->GetOuterPrivate() !=
            contracts.replication_manager_class ||
        contracts.get_replication_pressure->GetOuterPrivate() !=
            contracts.replication_manager_class)
    {
        return runtime_failure(
            application::RuntimeContractId::
                PaintQueueObservation,
            application::ContractFailureKind::WrongClass);
    }
    if (contracts.export_channel_to_bytes->GetOuterPrivate() !=
            contracts.runtime_paintable_class ||
        contracts.import_channel_from_bytes->GetOuterPrivate() !=
            contracts.runtime_paintable_class)
    {
        return runtime_failure(
            application::RuntimeContractId::TextureMutation,
            application::ContractFailureKind::WrongClass);
    }

    contracts.acknowledged_pawn = find_object_property(
        player_controller_class,
        STR("AcknowledgedPawn"),
        contracts.pawn_class);
    if (contracts.acknowledged_pawn == nullptr)
    {
        return runtime_failure(
            application::RuntimeContractId::PlayerController,
            application::ContractFailureKind::MissingProperty);
    }

    const auto vector_result = validate_unreal_record(
        contracts.vector2d,
        vector2d_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!vector_result)
    {
        return std::unexpected(vector_result.error());
    }
    const auto channel_result = validate_unreal_record(
        contracts.paint_channel_data,
        paint_channel_data_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!channel_result)
    {
        return std::unexpected(channel_result.error());
    }
    const auto brush_result = validate_unreal_record(
        contracts.runtime_brush_settings,
        runtime_brush_settings_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!brush_result)
    {
        return std::unexpected(brush_result.error());
    }
    const auto function_result = validate_unreal_record(
        contracts.paint_at_uv_with_brush,
        paint_at_uv_with_brush_contract(),
        application::RuntimeContractId::PaintAtUvWithBrush);
    if (!function_result)
    {
        return std::unexpected(function_result.error());
    }
    const auto recorded_result = validate_unreal_record(
        contracts.get_recorded_stroke_count,
        recorded_stroke_count_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!recorded_result)
    {
        return std::unexpected(recorded_result.error());
    }
    const auto pressure_struct_result = validate_unreal_record(
        contracts.runtime_paint_replication_pressure,
        runtime_paint_replication_pressure_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!pressure_struct_result)
    {
        return std::unexpected(pressure_struct_result.error());
    }
    const auto manager_count_result = validate_unreal_record(
        contracts.get_queued_stroke_count,
        queued_stroke_count_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!manager_count_result)
    {
        return std::unexpected(manager_count_result.error());
    }
    const auto component_count_result = validate_unreal_record(
        contracts.get_queued_stroke_count_for_component,
        queued_stroke_count_for_component_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!component_count_result)
    {
        return std::unexpected(component_count_result.error());
    }
    const auto pressure_result = validate_unreal_record(
        contracts.get_replication_pressure,
        replication_pressure_contract(),
        application::RuntimeContractId::PaintQueueObservation);
    if (!pressure_result)
    {
        return std::unexpected(pressure_result.error());
    }
    const auto export_result = validate_unreal_record(
        contracts.export_channel_to_bytes,
        export_channel_to_bytes_contract(),
        application::RuntimeContractId::TextureMutation);
    if (!export_result)
    {
        return std::unexpected(export_result.error());
    }
    const auto import_result = validate_unreal_record(
        contracts.import_channel_from_bytes,
        import_channel_from_bytes_contract(),
        application::RuntimeContractId::TextureMutation);
    if (!import_result)
    {
        return std::unexpected(import_result.error());
    }
    return contracts;
}

auto outer_chain_contains(UObject* object, UObject* expected)
    -> bool
{
    auto depth = 0U;
    for (auto* current =
             object == nullptr
                 ? nullptr
                 : object->GetOuterPrivate();
         current != nullptr && depth < 16U;
         current = current->GetOuterPrivate(), ++depth)
    {
        if (current == expected)
        {
            return true;
        }
    }
    return false;
}

auto resolve_owned_paint_component(
    const ActiveFrame& frame,
    const PaintContracts& contracts)
    -> std::optional<std::pair<UObject*, UObject*>>
{
    if (!object_is_live(
            frame.controller,
            contracts.player_controller_class))
    {
        return std::nullopt;
    }
    auto* pawn = read_object(
        contracts.acknowledged_pawn,
        frame.controller);
    if (!object_is_live(pawn, contracts.pawn_class))
    {
        return std::nullopt;
    }
    auto* component_property = find_object_property(
        pawn->GetClassPrivate(),
        STR("RuntimePaintable"),
        contracts.runtime_paintable_class);
    if (component_property == nullptr)
    {
        return std::nullopt;
    }
    auto* component = read_object(component_property, pawn);
    if (!object_is_live(
            component,
            contracts.runtime_paintable_class) ||
        !outer_chain_contains(component, pawn))
    {
        return std::nullopt;
    }
    return std::pair{pawn, component};
}

auto resolve_unique_replication_manager(
    UObject* world,
    const PaintContracts& contracts) -> UObject*
{
    if (world == nullptr ||
        contracts.replication_manager_class == nullptr)
    {
        return nullptr;
    }
    auto* match = static_cast<UObject*>(nullptr);
    auto count = std::size_t{};
    UObjectGlobals::ForEachUObject(
        [&](UObject* object, std::int32_t, std::int32_t)
            -> RC::LoopAction
        {
            if (!object_is_live_exact(
                    object,
                    contracts.replication_manager_class) ||
                object->GetWorld() != world)
            {
                return RC::LoopAction::Continue;
            }
            match = object;
            ++count;
            return count > 1U
                       ? RC::LoopAction::Break
                       : RC::LoopAction::Continue;
        });
    return count == 1U ? match : nullptr;
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
                if (hook_ids_ || detaching_)
                {
                    return std::unexpected(
                        application::CallbackPortError::
                            Registration);
                }
                hud_contracts_ = *resolved;
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
            clear_state_locked();
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
            if (id != HudCallbackId || !hook_ids_ ||
                !hud_contracts_ || detaching_)
            {
                return std::unexpected(
                    application::CallbackPortError::
                        Unregistration);
            }
            detaching_ = true;
            function = hud_contracts_->receive_draw_hud;
            ids = *hook_ids_;
        }

        try
        {
            UObjectGlobals::UnregisterHook(function, ids);
        }
        catch (...)
        {
            const auto lock = std::scoped_lock{mutex_};
            detaching_ = false;
            return std::unexpected(
                application::CallbackPortError::Unregistration);
        }

        auto lock = std::unique_lock{mutex_};
        idle_.wait(lock, [this] { return in_flight_ == 0U; });
        clear_state_locked();
        return {};
    }

    auto resolve_contracts()
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto hud = std::optional<HudContracts>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                hud = hud_contracts_;
            }
            if (!hud)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        RuntimeInitialization,
                    application::ContractFailureKind::
                        MissingObject);
            }
            auto resolved = resolve_paint_contracts(
                hud->player_controller_class);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        RuntimeInitialization,
                    application::ContractFailureKind::
                        ExecutionFailure);
            }
            paint_contracts_ = *resolved;
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    RuntimeInitialization,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto bind_frame(
        const application::HudFrameIdentity& identity)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto active = std::optional<ActiveFrame>{};
            auto paint = std::optional<PaintContracts>{};
            auto previous = std::optional<BoundFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                active = active_frame_;
                paint = paint_contracts_;
                previous = bound_frame_;
            }
            if (!active || active->identity != identity)
            {
                return runtime_failure(
                    application::RuntimeContractId::Hud,
                    application::ContractFailureKind::
                        StaleObject);
            }
            if (!paint)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        MissingObject);
            }

            auto* pawn = static_cast<UObject*>(nullptr);
            auto* component = static_cast<UObject*>(nullptr);
            auto* replication_manager =
                resolve_unique_replication_manager(
                    active->world,
                    *paint);
            if (replication_manager == nullptr)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        MissingObject);
            }
            if (const auto owned =
                    resolve_owned_paint_component(*active, *paint))
            {
                pawn = owned->first;
                component = owned->second;
            }
            else if (
                previous &&
                previous->identity.world == identity.world &&
                previous->identity.controller ==
                    identity.controller)
            {
                pawn = previous->pawn.Get();
                component = previous->component.Get();
                if (!object_is_live(pawn, paint->pawn_class) ||
                    !object_is_live(
                        component,
                        paint->runtime_paintable_class) ||
                    !outer_chain_contains(component, pawn))
                {
                    pawn = nullptr;
                    component = nullptr;
                }
            }
            if (pawn == nullptr || component == nullptr)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        MissingObject);
            }

            const auto component_id = object_identity(component);
            auto generation = std::uint64_t{1U};
            if (previous)
            {
                if (previous->component_identity == component_id &&
                    previous->component.Get() == component)
                {
                    generation = previous->component_generation;
                }
                else
                {
                    if (previous->component_generation ==
                        std::numeric_limits<std::uint64_t>::max())
                    {
                        return runtime_failure(
                            application::RuntimeContractId::
                                PaintAtUvWithBrush,
                            application::ContractFailureKind::
                                ExecutionFailure);
                    }
                    generation =
                        previous->component_generation + 1U;
                }
            }

            const auto lock = std::scoped_lock{mutex_};
            if (!active_frame_ ||
                active_frame_->identity != identity ||
                detaching_)
            {
                return runtime_failure(
                    application::RuntimeContractId::Hud,
                    application::ContractFailureKind::
                        StaleObject);
            }
            bound_frame_ = BoundFrame{
                identity,
                FWeakObjectPtr{active->world},
                FWeakObjectPtr{active->controller},
                FWeakObjectPtr{active->hud},
                FWeakObjectPtr{active->canvas},
                FWeakObjectPtr{pawn},
                FWeakObjectPtr{component},
                FWeakObjectPtr{replication_manager},
                component_id,
                generation,
            };
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::Hud,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto paint(const application::PaintAtUvWithBrush& request)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto contracts = std::optional<PaintContracts>{};
            auto bound = std::optional<BoundFrame>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                contracts = paint_contracts_;
                bound = bound_frame_;
                active = active_frame_;
            }
            if (!contracts || !bound || !active ||
                bound->identity != active->identity)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        StaleObject);
            }
            auto* component = bound->component.Get();
            if (!object_is_live(
                    component,
                    contracts->runtime_paintable_class) ||
                request.component.identity !=
                    bound->component_identity ||
                request.component.generation !=
                    bound->component_generation)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        StaleObject);
            }
            const auto parameters = encode_paint_call(request);
            if (!parameters)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintAtUvWithBrush,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            auto mutable_parameters = *parameters;
            component->ProcessEvent(
                contracts->paint_at_uv_with_brush,
                &mutable_parameters);
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    PaintAtUvWithBrush,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto observe_queues(
        application::RuntimeObjectHandle component_handle,
        application::JobGeneration generation)
        -> std::expected<
            application::PaintQueueObservation,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            auto contracts = std::optional<PaintContracts>{};
            auto bound = std::optional<BoundFrame>{};
            auto active = std::optional<ActiveFrame>{};
            {
                const auto lock = std::scoped_lock{mutex_};
                contracts = paint_contracts_;
                bound = bound_frame_;
                active = active_frame_;
            }
            if (!contracts || !bound || !active ||
                bound->identity != active->identity ||
                generation == 0U ||
                component_handle.identity !=
                    bound->component_identity ||
                component_handle.generation !=
                    bound->component_generation)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        StaleObject);
            }

            auto* component = bound->component.Get();
            auto* manager = bound->replication_manager.Get();
            if (!object_is_live(
                    component,
                    contracts->runtime_paintable_class) ||
                !object_is_live_exact(
                    manager,
                    contracts->replication_manager_class) ||
                manager->GetWorld() != active->world)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        StaleObject);
            }

            auto recorded = RecordedStrokeCountParamsAbi{};
            component->ProcessEvent(
                contracts->get_recorded_stroke_count,
                &recorded);

            auto component_count =
                QueuedStrokeCountForComponentParamsAbi{};
            component_count.paint_component = component;
            manager->ProcessEvent(
                contracts->get_queued_stroke_count_for_component,
                &component_count);

            auto manager_count = QueuedStrokeCountParamsAbi{};
            manager->ProcessEvent(
                contracts->get_queued_stroke_count,
                &manager_count);

            auto pressure = ReplicationPressureParamsAbi{};
            manager->ProcessEvent(
                contracts->get_replication_pressure,
                &pressure);

            const auto lock = std::scoped_lock{mutex_};
            if (detaching_ || !bound_frame_ ||
                bound_frame_->identity != bound->identity ||
                bound_frame_->component_identity !=
                    component_handle.identity ||
                bound_frame_->component_generation !=
                    component_handle.generation)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        StaleObject);
            }
            const auto observed = queue_tracker_.observe(
                component_handle,
                generation,
                PaintQueueCounters{
                    recorded.return_value,
                    component_count.return_value,
                    manager_count.return_value,
                    pressure.return_value,
                });
            if (!observed)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        PaintQueueObservation,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            return *observed;
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    PaintQueueObservation,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto capture_preview(
        application::RuntimeObjectHandle component_handle)
        -> std::expected<
            application::PaintPreviewSnapshot,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            const auto target =
                preview_target(component_handle);
            if (!target)
            {
                return std::unexpected(target.error());
            }

            auto albedo = export_channel(
                target->component,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::Albedo);
            if (!albedo)
            {
                return std::unexpected(albedo.error());
            }
            auto packed_pbr = export_channel(
                target->component,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::Emissive);
            if (!packed_pbr)
            {
                return std::unexpected(packed_pbr.error());
            }

            const auto dimension =
                infer_paint_texture_dimension(
                    *albedo,
                    *packed_pbr);
            if (!dimension)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            if (!preview_target(component_handle))
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        StaleObject);
            }

            return application::PaintPreviewSnapshot{
                component_handle,
                application::PaintTextureImage{
                    *dimension,
                    std::make_shared<
                        const std::vector<std::byte>>(
                        std::move(*albedo)),
                    std::make_shared<
                        const std::vector<std::byte>>(
                        std::move(*packed_pbr)),
                },
            };
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    TextureMutation,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto apply_preview(
        application::RuntimeObjectHandle component_handle,
        const application::PaintTextureImage& image)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        if (!IsInGameThreadRaw())
        {
            return std::unexpected(
                application::RuntimeExecutionError{
                    application::RuntimeExecutionErrorCode::
                        WrongThread,
                    std::nullopt,
                });
        }
        try
        {
            if (image.albedo_rgba == nullptr ||
                image.packed_pbr_rgba == nullptr)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        InvalidValue);
            }
            const auto dimension =
                infer_paint_texture_dimension(
                    *image.albedo_rgba,
                    *image.packed_pbr_rgba);
            if (!dimension || *dimension != image.dimension)
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        InvalidValue);
            }

            auto target = preview_target(component_handle);
            if (!target)
            {
                return std::unexpected(target.error());
            }
            const auto albedo = import_channel_verified(
                target->component,
                target->contracts.import_channel_from_bytes,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::Albedo,
                RuntimePaintChannel::Albedo,
                *image.albedo_rgba);
            if (!albedo)
            {
                return albedo;
            }

            target = preview_target(component_handle);
            if (!target)
            {
                return std::unexpected(target.error());
            }
            const auto packed_pbr = import_channel_verified(
                target->component,
                target->contracts.import_channel_from_bytes,
                target->contracts.export_channel_to_bytes,
                RuntimePaintChannel::
                    AlbedoMetallicRoughnessEmissive,
                RuntimePaintChannel::Emissive,
                *image.packed_pbr_rgba);
            if (!packed_pbr)
            {
                return packed_pbr;
            }
            if (!preview_target(component_handle))
            {
                return runtime_failure(
                    application::RuntimeContractId::
                        TextureMutation,
                    application::ContractFailureKind::
                        StaleObject);
            }
            return {};
        }
        catch (...)
        {
            return runtime_failure(
                application::RuntimeContractId::
                    TextureMutation,
                application::ContractFailureKind::
                    ExecutionFailure);
        }
    }

    auto restore_preview(
        const application::PaintPreviewSnapshot& snapshot)
        -> std::expected<
            void,
            application::RuntimeExecutionError>
    {
        return apply_preview(
            snapshot.component,
            snapshot.original);
    }

private:
    struct PreviewTarget
    {
        PaintContracts contracts{};
        BoundFrame bound{};
        UObject* component{};
    };

    auto preview_target(
        application::RuntimeObjectHandle component_handle)
        -> std::expected<
            PreviewTarget,
            application::RuntimeExecutionError>
    {
        auto contracts = std::optional<PaintContracts>{};
        auto bound = std::optional<BoundFrame>{};
        auto active = std::optional<ActiveFrame>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            contracts = paint_contracts_;
            bound = bound_frame_;
            active = active_frame_;
        }
        if (!contracts || !bound || !active ||
            bound->identity != active->identity ||
            component_handle.identity !=
                bound->component_identity ||
            component_handle.generation !=
                bound->component_generation)
        {
            return runtime_failure(
                application::RuntimeContractId::TextureMutation,
                application::ContractFailureKind::StaleObject);
        }
        auto* component = bound->component.Get();
        if (!object_is_live(
                component,
                contracts->runtime_paintable_class))
        {
            return runtime_failure(
                application::RuntimeContractId::TextureMutation,
                application::ContractFailureKind::StaleObject);
        }
        return PreviewTarget{
            *contracts,
            *bound,
            component,
        };
    }

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
            static_cast<Impl*>(custom_data)->dispatch(
                context.Context);
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
        auto sequence = std::uint64_t{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (detaching_ || !hud_contracts_ ||
                callback_context_ == nullptr ||
                callback_ == nullptr)
            {
                return;
            }
            contracts = hud_contracts_;
            callback_context = callback_context_;
            callback = callback_;
            ++in_flight_;
            sequence = ++dispatch_sequence_;
        }

        auto identity = application::HudFrameIdentity{};
        auto frame = std::optional<ActiveFrame>{};
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
                frame = ActiveFrame{
                    identity,
                    world,
                    controller,
                    hud,
                    canvas,
                };
            }
        }
        {
            const auto lock = std::scoped_lock{mutex_};
            if (!detaching_ && frame)
            {
                active_frame_ = *frame;
                active_frame_sequence_ = sequence;
            }
        }

        try
        {
            callback(callback_context, identity);
        }
        catch (...)
        {
            finish_dispatch(sequence);
            throw;
        }
        finish_dispatch(sequence);
    }

    auto finish_dispatch(std::uint64_t sequence) noexcept -> void
    {
        const auto lock = std::scoped_lock{mutex_};
        if (active_frame_sequence_ == sequence)
        {
            active_frame_.reset();
            active_frame_sequence_ = 0U;
        }
        if (in_flight_ > 0U)
        {
            --in_flight_;
        }
        if (in_flight_ == 0U)
        {
            idle_.notify_all();
        }
    }

    auto clear_state_locked() -> void
    {
        hook_ids_.reset();
        hud_contracts_.reset();
        paint_contracts_.reset();
        active_frame_.reset();
        bound_frame_.reset();
        queue_tracker_.reset();
        callback_context_ = nullptr;
        callback_ = nullptr;
        active_frame_sequence_ = 0U;
        detaching_ = false;
    }

    auto detach_noexcept() noexcept -> void
    {
        auto function = static_cast<UFunction*>(nullptr);
        auto ids = std::optional<std::pair<int, int>>{};
        {
            const auto lock = std::scoped_lock{mutex_};
            if (hud_contracts_)
            {
                function =
                    hud_contracts_->receive_draw_hud;
            }
            ids = hook_ids_;
            detaching_ = true;
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

        auto lock = std::unique_lock{mutex_};
        idle_.wait(lock, [this] { return in_flight_ == 0U; });
        clear_state_locked();
    }

    std::mutex mutex_{};
    std::condition_variable idle_{};
    std::optional<HudContracts> hud_contracts_{};
    std::optional<PaintContracts> paint_contracts_{};
    std::optional<ActiveFrame> active_frame_{};
    std::optional<BoundFrame> bound_frame_{};
    PaintQueueObservationTracker queue_tracker_{};
    std::optional<std::pair<int, int>> hook_ids_{};
    void* callback_context_{};
    application::HudCallback callback_{};
    std::size_t in_flight_{};
    std::uint64_t dispatch_sequence_{};
    std::uint64_t active_frame_sequence_{};
    bool detaching_{};
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

auto UnrealRuntimeAdapter::resolve_initial_contracts()
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->resolve_contracts();
}

auto UnrealRuntimeAdapter::rebind_hud_frame(
    const application::HudFrameIdentity& identity)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->bind_frame(identity);
}

auto UnrealRuntimeAdapter::paint_at_uv_with_brush(
    const application::PaintAtUvWithBrush& request)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->paint(request);
}

auto UnrealRuntimeAdapter::observe_paint_queues(
    application::RuntimeObjectHandle component,
    application::JobGeneration generation)
    -> std::expected<
        application::PaintQueueObservation,
        application::RuntimeExecutionError>
{
    return impl_->observe_queues(component, generation);
}

auto UnrealRuntimeAdapter::capture(
    application::RuntimeObjectHandle component)
    -> std::expected<
        application::PaintPreviewSnapshot,
        application::RuntimeExecutionError>
{
    return impl_->capture_preview(component);
}

auto UnrealRuntimeAdapter::apply(
    application::RuntimeObjectHandle component,
    const application::PaintTextureImage& image)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->apply_preview(component, image);
}

auto UnrealRuntimeAdapter::restore(
    const application::PaintPreviewSnapshot& snapshot)
    -> std::expected<
        void,
        application::RuntimeExecutionError>
{
    return impl_->restore_preview(snapshot);
}
} // namespace meccha::runtime
